#include "ANTHony/GradleRunner.h"

#include "Jvm.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
	#include <windows.h>
#else
	#include <cerrno>
	#include <csignal>
	#include <fcntl.h>
	#include <poll.h>
	#include <sys/types.h>
	#include <sys/wait.h>
	#include <unistd.h>

extern char **environ;
#endif

namespace fs = std::filesystem;

namespace
{

	// ---------------------------------------------------------------------------
	// Internal runner state.
	// ---------------------------------------------------------------------------
	struct GradleRunnerImpl {
		std::string _projectRoot;
		std::string _javaHome;
		std::string _bridgeJar;
		std::vector<std::pair<std::string, std::string>> _env;
		std::vector<std::string> _jvmArgs;
		std::vector<std::string> _gradleOpts;
		std::atomic<bool> _cancelRequested { false };
	};

	// ---------------------------------------------------------------------------
	// Small helpers.
	// ---------------------------------------------------------------------------
	bool isNullOrEmpty(const char *s)
	{
		return s == nullptr || s[0] == '\0';
	}

	char *dupString(const std::string &s)
	{
		char *buf = static_cast<char *>(std::malloc(s.size() + 1));
		if (buf == nullptr) {
			return nullptr;
		}
		std::memcpy(buf, s.data(), s.size());
		buf[s.size()] = '\0';
		return buf;
	}

	// Find the newest file with the given extension under a directory subtree.
	// Returns an empty string if none is found.
	std::string findNewestByExtension(const fs::path &root,
									  const std::string &extension)
	{
		std::string best;
		fs::file_time_type bestTime;
		bool found = false;

		std::error_code ec;
		if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) {
			return {};
		}

		for (fs::recursive_directory_iterator it(root, ec), end;
			 it != end && !ec; it.increment(ec)) {
			if (!it->is_regular_file(ec)) {
				continue;
			}
			if (it->path().extension() != extension) {
				continue;
			}
			auto t = it->last_write_time(ec);
			if (ec) {
				continue;
			}
			if (!found || t > bestTime) {
				best	 = it->path().string();
				bestTime = t;
				found	 = true;
			}
		}
		return best;
	}

// ---------------------------------------------------------------------------
// Tooling API backend (JNI).
// ---------------------------------------------------------------------------
#ifdef ANTHONY_USE_TOOLING_API

	// Convert a std::vector<std::string> into a Java String[].
	jobjectArray toJavaStringArray(JNIEnv *env,
								   const std::vector<std::string> &items)
	{
		jclass stringClass = env->FindClass("java/lang/String");
		jobjectArray arr = env->NewObjectArray(static_cast<jsize>(items.size()),
											   stringClass, nullptr);
		for (size_t i = 0; i < items.size(); ++i) {
			jstring s = env->NewStringUTF(items[i].c_str());
			env->SetObjectArrayElement(arr, static_cast<jsize>(i), s);
			env->DeleteLocalRef(s);
		}
		env->DeleteLocalRef(stringClass);
		return arr;
	}

	// Read a Java String into a std::string.
	std::string fromJavaString(JNIEnv *env, jstring s)
	{
		if (s == nullptr) {
			return {};
		}
		const char *chars  = env->GetStringUTFChars(s, nullptr);
		std::string result = chars == nullptr ? "" : chars;
		if (chars != nullptr) {
			env->ReleaseStringUTFChars(s, chars);
		}
		return result;
	}

	// Convert a std::vector<std::pair<std::string,std::string>> into a Java
	// java.util.HashMap<String,String>.
	jobject toJavaEnvMap(
		JNIEnv *env,
		const std::vector<std::pair<std::string, std::string>> &envVars)
	{
		jclass mapClass = env->FindClass("java/util/HashMap");
		jmethodID ctor	= env->GetMethodID(mapClass, "<init>", "()V");
		jmethodID put	= env->GetMethodID(
			mapClass, "put",
			"(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");
		jobject map = env->NewObject(mapClass, ctor);
		for (const auto &kv: envVars) {
			jstring key = env->NewStringUTF(kv.first.c_str());
			jstring val = env->NewStringUTF(kv.second.c_str());
			env->CallObjectMethod(map, put, key, val);
			env->DeleteLocalRef(key);
			env->DeleteLocalRef(val);
		}
		env->DeleteLocalRef(mapClass);
		return map;
	}

	// Locate the bridge jar. Prefers an explicit path, then a set of defaults.
	std::string findBridgeJar(const std::string &explicitPath)
	{
		if (!explicitPath.empty()) {
			return explicitPath;
		}
		std::vector<fs::path> candidates = {
			"gradle-runner-bridge.jar",
			"java/lib/build/libs/gradle-runner-bridge.jar",
			"../java/lib/build/libs/gradle-runner-bridge.jar",
		};
		for (const auto &c: candidates) {
			if (fs::exists(c)) {
				return c.string();
			}
		}
		return {};
	}

	struct ToolingResult {
		GradleRunnerError error = GRADLE_RUNNER_OK;
		std::string stdoutBuf;
		std::string stderrBuf;
		int exitCode = 0;
	};

	ToolingResult runTooling(GradleRunnerImpl *impl, const std::string &task,
							 const std::vector<std::string> &extraArgs,
							 int timeoutMs)
	{
		ToolingResult result;

		std::string bridgeJar = findBridgeJar(impl->_bridgeJar);
		if (bridgeJar.empty()) {
			result.error = GRADLE_RUNNER_BRIDGE_NOT_FOUND;
			return result;
		}

		GradleRunnerError rc =
			anthony::jvm::Jvm::init(impl->_javaHome, bridgeJar);
		if (rc != GRADLE_RUNNER_OK) {
			result.error = rc;
			return result;
		}

		JNIEnv *env = anthony::jvm::Jvm::env();
		if (env == nullptr) {
			result.error = GRADLE_RUNNER_JVM_INIT_FAILED;
			return result;
		}

		jclass bridgeClass = env->FindClass("com/anthony/GradleRunnerBridge");
		if (bridgeClass == nullptr) {
			env->ExceptionClear();
			result.error = GRADLE_RUNNER_BRIDGE_NOT_FOUND;
			return result;
		}

		// Build the task list: the primary task plus any extra args.
		std::vector<std::string> tasks;
		tasks.push_back(task);
		for (const auto &a: extraArgs) {
			tasks.push_back(a);
		}

		jstring projectDir = env->NewStringUTF(impl->_projectRoot.c_str());
		jstring javaHome =
			env->NewStringUTF(anthony::jvm::Jvm::resolvedJavaHome().c_str());
		jobjectArray tasksArr	   = toJavaStringArray(env, tasks);
		jobjectArray jvmArgsArr	   = toJavaStringArray(env, impl->_jvmArgs);
		jobjectArray gradleArgsArr = toJavaStringArray(env, impl->_gradleOpts);
		jobject envMap			   = toJavaEnvMap(env, impl->_env);

		jmethodID runTask = env->GetStaticMethodID(
			bridgeClass, "runTask",
			"(Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;"
			"[Ljava/lang/String;[Ljava/lang/String;Ljava/util/Map;I)I");
		jmethodID getStdout = env->GetStaticMethodID(bridgeClass, "getStdout",
													 "()Ljava/lang/String;");
		jmethodID getStderr = env->GetStaticMethodID(bridgeClass, "getStderr",
													 "()Ljava/lang/String;");
		jmethodID getLastError = env->GetStaticMethodID(
			bridgeClass, "getLastError", "()Ljava/lang/String;");
		jmethodID cancel = env->GetStaticMethodID(bridgeClass, "cancel", "()V");

		if (runTask == nullptr || getStdout == nullptr || getStderr == nullptr
			|| getLastError == nullptr || cancel == nullptr) {
			env->ExceptionClear();
			result.error = GRADLE_RUNNER_BRIDGE_NOT_FOUND;
			return result;
		}

		// Promote the objects passed to the worker thread to global references:
		// JNI local references are only valid on the thread that created them.
		jclass bridgeClassGlobal =
			static_cast<jclass>(env->NewGlobalRef(bridgeClass));
		jstring projectDirGlobal =
			static_cast<jstring>(env->NewGlobalRef(projectDir));
		jstring javaHomeGlobal =
			static_cast<jstring>(env->NewGlobalRef(javaHome));
		jobjectArray tasksArrGlobal =
			static_cast<jobjectArray>(env->NewGlobalRef(tasksArr));
		jobjectArray jvmArgsArrGlobal =
			static_cast<jobjectArray>(env->NewGlobalRef(jvmArgsArr));
		jobjectArray gradleArgsArrGlobal =
			static_cast<jobjectArray>(env->NewGlobalRef(gradleArgsArr));
		jobject envMapGlobal = env->NewGlobalRef(envMap);

		// Run the build on a worker thread so a timeout can cancel it. The
		// worker thread must attach its own JNIEnv (JNIEnv is thread-local).
		std::atomic<jint> javaExit { 0 };
		std::atomic<bool> done { false };
		std::thread worker([&]() {
			JNIEnv *workerEnv = anthony::jvm::Jvm::env();
			if (workerEnv == nullptr) {
				javaExit = 1;
				done	 = true;
				return;
			}
			javaExit = workerEnv->CallStaticIntMethod(
				bridgeClassGlobal, runTask, projectDirGlobal, javaHomeGlobal,
				tasksArrGlobal, jvmArgsArrGlobal, gradleArgsArrGlobal,
				envMapGlobal, static_cast<jint>(timeoutMs));
			done = true;
			anthony::jvm::Jvm::detach();
		});

		if (timeoutMs > 0) {
			auto deadline = std::chrono::steady_clock::now()
				+ std::chrono::milliseconds(timeoutMs);
			while (!done) {
				if (impl->_cancelRequested.load()) {
					env->CallStaticVoidMethod(bridgeClass, cancel);
					break;
				}
				if (std::chrono::steady_clock::now() >= deadline) {
					env->CallStaticVoidMethod(bridgeClass, cancel);
					break;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			}
		} else {
			while (!done) {
				if (impl->_cancelRequested.load()) {
					env->CallStaticVoidMethod(bridgeClass, cancel);
					break;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			}
		}

		worker.join();

		if (impl->_cancelRequested.load()) {
			result.error = GRADLE_RUNNER_CANCELLED;
		} else if (javaExit != 0) {
			// The build failed; report the error message and a non-zero exit
			// code.
			result.exitCode = static_cast<int>(javaExit);
		}

		jstring outStr = static_cast<jstring>(
			env->CallStaticObjectMethod(bridgeClass, getStdout));
		jstring errStr = static_cast<jstring>(
			env->CallStaticObjectMethod(bridgeClass, getStderr));
		jstring lastErr = static_cast<jstring>(
			env->CallStaticObjectMethod(bridgeClass, getLastError));

		result.stdoutBuf	  = fromJavaString(env, outStr);
		result.stderrBuf	  = fromJavaString(env, errStr);
		std::string lastError = fromJavaString(env, lastErr);
		if (!lastError.empty() && result.stderrBuf.empty()) {
			result.stderrBuf = lastError;
		}

		env->DeleteLocalRef(outStr);
		env->DeleteLocalRef(errStr);
		env->DeleteLocalRef(lastErr);
		env->DeleteLocalRef(projectDir);
		env->DeleteLocalRef(javaHome);
		env->DeleteLocalRef(tasksArr);
		env->DeleteLocalRef(jvmArgsArr);
		env->DeleteLocalRef(gradleArgsArr);
		env->DeleteLocalRef(envMap);
		env->DeleteLocalRef(bridgeClass);

		env->DeleteGlobalRef(bridgeClassGlobal);
		env->DeleteGlobalRef(projectDirGlobal);
		env->DeleteGlobalRef(javaHomeGlobal);
		env->DeleteGlobalRef(tasksArrGlobal);
		env->DeleteGlobalRef(jvmArgsArrGlobal);
		env->DeleteGlobalRef(gradleArgsArrGlobal);
		env->DeleteGlobalRef(envMapGlobal);

		return result;
	}

#endif	  // ANTHONY_USE_TOOLING_API

// ---------------------------------------------------------------------------
// Fallback backend: spawn the gradlew wrapper as a subprocess.
// ---------------------------------------------------------------------------
#ifndef _WIN32

	struct PosixResult {
		GradleRunnerError error = GRADLE_RUNNER_OK;
		std::string stdoutBuf;
		std::string stderrBuf;
		int exitCode = 0;
	};

	bool appendBytes(std::string &out, const char *bytes, size_t count)
	{
		try {
			out.append(bytes, count);
		} catch (const std::bad_alloc &) {
			return false;
		}
		return true;
	}

	bool drainFd(int fd, std::string &out)
	{
		char buf[4096];
		for (;;) {
			ssize_t n = ::read(fd, buf, sizeof(buf));
			if (n > 0) {
				if (!appendBytes(out, buf, static_cast<size_t>(n))) {
					return false;
				}
				continue;
			}
			if (n == 0) {
				return true;
			}
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				return true;
			}
			if (errno == EINTR) {
				continue;
			}
			return false;
		}
	}

	PosixResult
		runPosix(const fs::path &wrapper, const std::vector<std::string> &args,
				 const std::vector<std::pair<std::string, std::string>> &env,
				 const std::string &gradleOpts, int timeoutMs)
	{
		PosixResult result;

		int outPipe[2];
		int errPipe[2];
		if (::pipe(outPipe) != 0 || ::pipe(errPipe) != 0) {
			result.error = GRADLE_RUNNER_SPAWN_FAILED;
			return result;
		}

		pid_t pid = ::fork();
		if (pid < 0) {
			::close(outPipe[0]);
			::close(outPipe[1]);
			::close(errPipe[0]);
			::close(errPipe[1]);
			result.error = GRADLE_RUNNER_SPAWN_FAILED;
			return result;
		}

		if (pid == 0) {
			::dup2(outPipe[1], STDOUT_FILENO);
			::dup2(errPipe[1], STDERR_FILENO);
			::close(outPipe[0]);
			::close(outPipe[1]);
			::close(errPipe[0]);
			::close(errPipe[1]);

			std::vector<char *> argv;
			argv.push_back(const_cast<char *>(wrapper.c_str()));
			for (const auto &a: args) {
				argv.push_back(const_cast<char *>(a.c_str()));
			}
			argv.push_back(nullptr);

			std::vector<std::string> envStrings;
			for (char **e = environ; e != nullptr && *e != nullptr; ++e) {
				envStrings.emplace_back(*e);
			}
			for (const auto &kv: env) {
				std::string entry = kv.first + "=" + kv.second;
				bool replaced	  = false;
				for (auto &existing: envStrings) {
					if (existing.rfind(kv.first + "=", 0) == 0) {
						existing = entry;
						replaced = true;
						break;
					}
				}
				if (!replaced) {
					envStrings.push_back(entry);
				}
			}
			if (!gradleOpts.empty()) {
				std::string entry = "GRADLE_OPTS=" + gradleOpts;
				bool replaced	  = false;
				for (auto &existing: envStrings) {
					if (existing.rfind("GRADLE_OPTS=", 0) == 0) {
						existing = entry;
						replaced = true;
						break;
					}
				}
				if (!replaced) {
					envStrings.push_back(entry);
				}
			}

			std::vector<char *> envp;
			for (auto &e: envStrings) {
				envp.push_back(const_cast<char *>(e.c_str()));
			}
			envp.push_back(nullptr);

			::execve(wrapper.c_str(), argv.data(), envp.data());
			_exit(127);
		}

		::close(outPipe[1]);
		::close(errPipe[1]);

		int outFd = outPipe[0];
		int errFd = errPipe[0];

		for (int fd: { outFd, errFd }) {
			int flags = ::fcntl(fd, F_GETFL, 0);
			::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
		}

		auto start		 = std::chrono::steady_clock::now();
		bool timedOut	 = false;
		int status		 = 0;
		bool childExited = false;

		while (!childExited) {
			if (timeoutMs > 0) {
				auto elapsed =
					std::chrono::duration_cast<std::chrono::milliseconds>(
						std::chrono::steady_clock::now() - start)
						.count();
				if (elapsed >= timeoutMs) {
					timedOut = true;
					break;
				}
			}

			struct pollfd fds[2];
			fds[0].fd	  = outFd;
			fds[0].events = POLLIN;
			fds[1].fd	  = errFd;
			fds[1].events = POLLIN;

			int remaining = timeoutMs > 0
				? static_cast<int>(
					  timeoutMs
					  - std::chrono::duration_cast<std::chrono::milliseconds>(
							std::chrono::steady_clock::now() - start)
							.count())
				: -1;
			if (remaining < 0) {
				remaining = 0;
			}

			int pr = ::poll(fds, 2, remaining);
			if (pr < 0) {
				if (errno == EINTR) {
					continue;
				}
				result.error = GRADLE_RUNNER_IO_ERROR;
				break;
			}

			if (fds[0].revents & POLLIN) {
				if (!drainFd(outFd, result.stdoutBuf)) {
					result.error = GRADLE_RUNNER_IO_ERROR;
					break;
				}
			}
			if (fds[1].revents & POLLIN) {
				if (!drainFd(errFd, result.stderrBuf)) {
					result.error = GRADLE_RUNNER_IO_ERROR;
					break;
				}
			}

			pid_t w = ::waitpid(pid, &status, WNOHANG);
			if (w == pid) {
				childExited = true;
			} else if (w < 0 && errno != EINTR) {
				result.error = GRADLE_RUNNER_IO_ERROR;
				break;
			}
		}

		if (timedOut) {
			::kill(pid, SIGKILL);
			::waitpid(pid, &status, 0);
			result.error = GRADLE_RUNNER_TIMEOUT;
		} else if (result.error == GRADLE_RUNNER_OK) {
			drainFd(outFd, result.stdoutBuf);
			drainFd(errFd, result.stderrBuf);
			if (WIFEXITED(status)) {
				result.exitCode = WEXITSTATUS(status);
			} else {
				result.exitCode = -1;
			}
		}

		::close(outFd);
		::close(errFd);
		return result;
	}

#endif	  // !_WIN32

	fs::path resolveWrapper(const fs::path &root)
	{
#ifdef _WIN32
		fs::path candidate = root / "gradlew.bat";
#else
		fs::path candidate = root / "gradlew";
#endif
		if (fs::exists(candidate) && fs::is_regular_file(candidate)) {
			return candidate;
		}
		return {};
	}

	std::string buildGradleOpts(const std::vector<std::string> &jvmArgs)
	{
		if (jvmArgs.empty()) {
			return {};
		}
		std::string opts;
		for (const auto &arg: jvmArgs) {
			if (!opts.empty()) {
				opts += ' ';
			}
			opts += arg;
		}
		return opts;
	}

}	 // namespace

// ---------------------------------------------------------------------------
// Public API.
// ---------------------------------------------------------------------------
extern "C" {

GradleRunner *gradle_runner_create(const char *project_root)
{
	if (isNullOrEmpty(project_root)) {
		return nullptr;
	}

	std::error_code ec;
	fs::path root(project_root);
	if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) {
		return nullptr;
	}

	auto *runner = new (std::nothrow) GradleRunnerImpl();
	if (runner == nullptr) {
		return nullptr;
	}
	runner->_projectRoot = fs::canonical(root, ec).string();
	if (ec) {
		runner->_projectRoot = root.string();
	}
	return reinterpret_cast<GradleRunner *>(runner);
}

void gradle_runner_destroy(GradleRunner *runner)
{
	delete reinterpret_cast<GradleRunnerImpl *>(runner);
}

GradleRunnerError gradle_runner_run_task(GradleRunner *runner, const char *task,
										 const char *const *extra_args,
										 int extra_args_count, int timeout_ms,
										 char **out_stdout, char **out_stderr,
										 int *exit_code)
{
	if (out_stdout != nullptr) {
		*out_stdout = nullptr;
	}
	if (out_stderr != nullptr) {
		*out_stderr = nullptr;
	}
	if (exit_code != nullptr) {
		*exit_code = 0;
	}

	if (runner == nullptr || isNullOrEmpty(task) || timeout_ms < 0
		|| (extra_args_count > 0 && extra_args == nullptr)) {
		return GRADLE_RUNNER_INVALID_ARGUMENT;
	}

	auto *impl = reinterpret_cast<GradleRunnerImpl *>(runner);
	impl->_cancelRequested.store(false);

	std::vector<std::string> extraArgs;
	for (int i = 0; i < extra_args_count; ++i) {
		if (extra_args[i] != nullptr) {
			extraArgs.push_back(extra_args[i]);
		}
	}

#ifdef ANTHONY_USE_TOOLING_API
	ToolingResult r = runTooling(impl, task, extraArgs, timeout_ms);
#else
	(void)impl;
	(void)extraArgs;
	ToolingResult r;
	r.error = GRADLE_RUNNER_INTERNAL_ERROR;
#endif

	// Fall back to the gradlew subprocess if the Tooling API is unavailable
	// (no bridge jar, no JVM) but a wrapper is present.
	if (r.error == GRADLE_RUNNER_BRIDGE_NOT_FOUND
		|| r.error == GRADLE_RUNNER_JVM_NOT_FOUND
		|| r.error == GRADLE_RUNNER_JVM_INIT_FAILED) {
		fs::path wrapper = resolveWrapper(impl->_projectRoot);
		if (!wrapper.empty()) {
			std::vector<std::string> args;
			args.push_back(task);
			for (const auto &a: extraArgs) {
				args.push_back(a);
			}
			for (const auto &opt: impl->_gradleOpts) {
				args.push_back(opt);
			}
			std::string gradleOpts = buildGradleOpts(impl->_jvmArgs);
#ifdef _WIN32
			r.error = GRADLE_RUNNER_SPAWN_FAILED;
#else
			PosixResult pr =
				runPosix(wrapper, args, impl->_env, gradleOpts, timeout_ms);
			r.error		= pr.error;
			r.stdoutBuf = std::move(pr.stdoutBuf);
			r.stderrBuf = std::move(pr.stderrBuf);
			r.exitCode	= pr.exitCode;
#endif
		}
	}

	if (r.error == GRADLE_RUNNER_OK || r.error == GRADLE_RUNNER_TIMEOUT
		|| r.error == GRADLE_RUNNER_CANCELLED) {
		if (out_stdout != nullptr) {
			*out_stdout = dupString(r.stdoutBuf);
		}
		if (out_stderr != nullptr) {
			*out_stderr = dupString(r.stderrBuf);
		}
		if (exit_code != nullptr) {
			*exit_code = r.exitCode;
		}
	}
	return r.error;
}

GradleRunnerError gradle_runner_set_env(GradleRunner *runner, const char *key,
										const char *value)
{
	if (runner == nullptr || isNullOrEmpty(key)) {
		return GRADLE_RUNNER_INVALID_ARGUMENT;
	}
	reinterpret_cast<GradleRunnerImpl *>(runner)->_env.emplace_back(
		key, value == nullptr ? "" : value);
	return GRADLE_RUNNER_OK;
}

GradleRunnerError gradle_runner_add_jvm_arg(GradleRunner *runner,
											const char *arg)
{
	if (runner == nullptr || isNullOrEmpty(arg)) {
		return GRADLE_RUNNER_INVALID_ARGUMENT;
	}
	reinterpret_cast<GradleRunnerImpl *>(runner)->_jvmArgs.emplace_back(arg);
	return GRADLE_RUNNER_OK;
}

GradleRunnerError gradle_runner_add_gradle_opt(GradleRunner *runner,
											   const char *opt)
{
	if (runner == nullptr || isNullOrEmpty(opt)) {
		return GRADLE_RUNNER_INVALID_ARGUMENT;
	}
	reinterpret_cast<GradleRunnerImpl *>(runner)->_gradleOpts.emplace_back(opt);
	return GRADLE_RUNNER_OK;
}

GradleRunnerError gradle_runner_set_java_home(GradleRunner *runner,
											  const char *java_home)
{
	if (runner == nullptr || isNullOrEmpty(java_home)) {
		return GRADLE_RUNNER_INVALID_ARGUMENT;
	}
	reinterpret_cast<GradleRunnerImpl *>(runner)->_javaHome = java_home;
	return GRADLE_RUNNER_OK;
}

GradleRunnerError gradle_runner_set_bridge_jar(GradleRunner *runner,
											   const char *bridge_jar)
{
	if (runner == nullptr || isNullOrEmpty(bridge_jar)) {
		return GRADLE_RUNNER_INVALID_ARGUMENT;
	}
	reinterpret_cast<GradleRunnerImpl *>(runner)->_bridgeJar = bridge_jar;
	return GRADLE_RUNNER_OK;
}

GradleRunnerError gradle_runner_cancel(GradleRunner *runner)
{
	if (runner == nullptr) {
		return GRADLE_RUNNER_INVALID_ARGUMENT;
	}
	reinterpret_cast<GradleRunnerImpl *>(runner)->_cancelRequested.store(true);
	return GRADLE_RUNNER_OK;
}

GradleRunnerError gradle_runner_set_android_sdk(GradleRunner *runner,
												const char *sdk_path)
{
	if (runner == nullptr || isNullOrEmpty(sdk_path)) {
		return GRADLE_RUNNER_INVALID_ARGUMENT;
	}
	auto *impl = reinterpret_cast<GradleRunnerImpl *>(runner);
	impl->_env.emplace_back("ANDROID_HOME", sdk_path);
	impl->_env.emplace_back("ANDROID_SDK_ROOT", sdk_path);
	return GRADLE_RUNNER_OK;
}

GradleRunnerError gradle_runner_set_signing_config(GradleRunner *runner,
												   const char *store_file,
												   const char *store_password,
												   const char *key_alias,
												   const char *key_password)
{
	if (runner == nullptr) {
		return GRADLE_RUNNER_INVALID_ARGUMENT;
	}
	auto *impl = reinterpret_cast<GradleRunnerImpl *>(runner);
	if (!isNullOrEmpty(store_file)) {
		impl->_gradleOpts.emplace_back(
			std::string("-Pandroid.injected.signing.store.file=") + store_file);
	}
	if (!isNullOrEmpty(store_password)) {
		impl->_gradleOpts.emplace_back(
			std::string("-Pandroid.injected.signing.store.password=")
			+ store_password);
	}
	if (!isNullOrEmpty(key_alias)) {
		impl->_gradleOpts.emplace_back(
			std::string("-Pandroid.injected.signing.key.alias=") + key_alias);
	}
	if (!isNullOrEmpty(key_password)) {
		impl->_gradleOpts.emplace_back(
			std::string("-Pandroid.injected.signing.key.password=")
			+ key_password);
	}
	return GRADLE_RUNNER_OK;
}

GradleRunnerError gradle_runner_find_apk(GradleRunner *runner, char **out_path)
{
	if (out_path != nullptr) {
		*out_path = nullptr;
	}
	if (runner == nullptr) {
		return GRADLE_RUNNER_INVALID_ARGUMENT;
	}
	auto *impl = reinterpret_cast<GradleRunnerImpl *>(runner);
	std::string found =
		findNewestByExtension(fs::path(impl->_projectRoot), ".apk");
	if (out_path != nullptr && !found.empty()) {
		*out_path = dupString(found);
	}
	return GRADLE_RUNNER_OK;
}

GradleRunnerError gradle_runner_find_aab(GradleRunner *runner, char **out_path)
{
	if (out_path != nullptr) {
		*out_path = nullptr;
	}
	if (runner == nullptr) {
		return GRADLE_RUNNER_INVALID_ARGUMENT;
	}
	auto *impl = reinterpret_cast<GradleRunnerImpl *>(runner);
	std::string found =
		findNewestByExtension(fs::path(impl->_projectRoot), ".aab");
	if (out_path != nullptr && !found.empty()) {
		*out_path = dupString(found);
	}
	return GRADLE_RUNNER_OK;
}

void gradle_runner_free_string(char *s)
{
	std::free(s);
}

const char *gradle_runner_error_string(GradleRunnerError error)
{
	switch (error) {
		case GRADLE_RUNNER_OK:
			return "ok";
		case GRADLE_RUNNER_INVALID_ARGUMENT:
			return "invalid argument";
		case GRADLE_RUNNER_PROJECT_NOT_FOUND:
			return "project root not found";
		case GRADLE_RUNNER_WRAPPER_NOT_FOUND:
			return "gradle wrapper not found";
		case GRADLE_RUNNER_SPAWN_FAILED:
			return "failed to spawn gradle process";
		case GRADLE_RUNNER_TIMEOUT:
			return "gradle run timed out";
		case GRADLE_RUNNER_IO_ERROR:
			return "I/O error while capturing output";
		case GRADLE_RUNNER_INTERNAL_ERROR:
			return "internal error";
		case GRADLE_RUNNER_JVM_NOT_FOUND:
			return "JVM not found";
		case GRADLE_RUNNER_JVM_INIT_FAILED:
			return "JVM initialization failed";
		case GRADLE_RUNNER_BRIDGE_NOT_FOUND:
			return "gradle runner bridge jar not found";
		case GRADLE_RUNNER_CANCELLED:
			return "gradle run cancelled";
	}
	return "unknown error";
}

}	 // extern "C"
