#include "ANTHony/Jvm.hpp"

#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32
	#include <windows.h>
#else
	#include <dlfcn.h>
	#include <limits.h>
	#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace anthony::jvm
{

	namespace
	{

		// Function pointer type for JNI_CreateJavaVM.
		using CreateJavaVmFn = jint (*)(JavaVM **, void **, void *);

		JavaVM *g_vm = nullptr;
		std::string g_lastError;
		std::string g_javaHome;

		// Gradle 9.x supports running on Java 17 through 24. When multiple
		// JDKs are installed we prefer one in this range so the Gradle daemon
		// forked by the Tooling API can actually run.
		constexpr int kMinCompatibleJava = 17;
		constexpr int kMaxCompatibleJava = 24;

#ifdef _WIN32
		HMODULE g_jvmHandle = nullptr;
#else
		void *g_jvmHandle = nullptr;
#endif

		// Resolve the real path of an executable, following symlinks (e.g. the
		// macOS /usr/bin/java stub that points into a JDK).
		std::string resolveExecutable(const std::string &path)
		{
#ifdef _WIN32
			return path;
#else
			char buf[PATH_MAX];
			if (::realpath(path.c_str(), buf) != nullptr) {
				return std::string(buf);
			}
			return path;
#endif
		}

		// Parse the major Java version from a JDK's <home>/release file.
		// Handles "JAVA_VERSION=\"21.0.12.1\"" -> 21 and
		// "JAVA_VERSION=\"1.8.0_503\"" -> 8. Returns -1 if unparseable.
		int jdkMajorVersion(const std::string &home)
		{
			fs::path release = fs::path(home) / "release";
			std::ifstream in(release);
			if (!in) {
				return -1;
			}
			std::string line;
			while (std::getline(in, line)) {
				const std::string key = "JAVA_VERSION=";
				size_t pos			  = line.find(key);
				if (pos == std::string::npos) {
					continue;
				}
				std::string value = line.substr(pos + key.size());
				// Strip surrounding quotes.
				if (value.size() >= 2 && value.front() == '"'
					&& value.back() == '"') {
					value = value.substr(1, value.size() - 2);
				}
				// "1.8.0_503" -> major 8.
				if (value.rfind("1.", 0) == 0) {
					size_t dot		  = value.find('.', 2);
					std::string major = value.substr(
						2,
						dot == std::string::npos ? std::string::npos : dot - 2);
					try {
						return std::stoi(major);
					} catch (...) {
						return -1;
					}
				}
				// "21.0.12.1" -> major 21.
				size_t dot		  = value.find('.');
				std::string major = value.substr(
					0, dot == std::string::npos ? std::string::npos : dot);
				try {
					return std::stoi(major);
				} catch (...) {
					return -1;
				}
			}
			return -1;
		}

		// Enumerate candidate JDK home directories installed on this machine.
		std::vector<std::string> enumerateJdkHomes()
		{
			std::vector<std::string> homes;
			std::vector<fs::path> roots;

#ifdef _WIN32
			const char *programFiles = std::getenv("ProgramFiles");
			if (programFiles != nullptr) {
				roots.emplace_back(fs::path(programFiles) / "Java");
				roots.emplace_back(fs::path(programFiles) / "Eclipse Adoptium");
			}
#elif defined(__APPLE__)
			const char *home = std::getenv("HOME");
			roots.emplace_back("/Library/Java/JavaVirtualMachines");
			roots.emplace_back("/System/Library/Java/JavaVirtualMachines");
			if (home != nullptr) {
				roots.emplace_back(fs::path(home) / "Library" / "Java"
								   / "JavaVirtualMachines");
			}
#else
			roots.emplace_back("/usr/lib/jvm");
			roots.emplace_back("/usr/java");
			roots.emplace_back("/opt/java");
#endif

			for (const auto &root: roots) {
				std::error_code ec;
				if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) {
					continue;
				}
				for (const auto &entry: fs::directory_iterator(root, ec)) {
					if (ec) {
						break;
					}
					fs::path home = entry.path();
#ifdef __APPLE__
					home = home / "Contents" / "Home";
#endif
					if (fs::exists(home / "include" / "jni.h")
						|| fs::exists(home / "lib" / "server")
						|| fs::exists(home / "jre")) {
						homes.push_back(home.string());
					}
				}
			}
			return homes;
		}

		// Choose the best JDK home from the enumerated candidates: prefer one
		// whose major version is in the Gradle-compatible range, otherwise the
		// highest-major JDK available.
		std::string pickBestJdkHome(const std::vector<std::string> &homes)
		{
			std::string best;
			int bestMajor = -1;
			for (const auto &home: homes) {
				int major = jdkMajorVersion(home);
				if (major >= kMinCompatibleJava
					&& major <= kMaxCompatibleJava) {
					return home;
				}
				if (major > bestMajor) {
					bestMajor = major;
					best	  = home;
				}
			}
			return best;
		}

		// Locate the JDK home directory. Prefers an explicit javaHome, then
		// JAVA_HOME, then an enumerated JDK in the Gradle-compatible range,
		// then the platform-specific resolver (/usr/libexec/java_home on
		// macOS), then `java` on PATH.
		std::string findJavaHome(const std::string &javaHome)
		{
			if (!javaHome.empty()) {
				return javaHome;
			}
			const char *envHome = std::getenv("JAVA_HOME");
			if (envHome != nullptr && envHome[0] != '\0') {
				return std::string(envHome);
			}

			// Prefer a JDK whose major version is Gradle-compatible.
			std::string best = pickBestJdkHome(enumerateJdkHomes());
			if (!best.empty()) {
				return best;
			}

#ifdef __APPLE__
			// /usr/bin/java is a stub; the canonical resolver is
			// /usr/libexec/java_home.
			FILE *pipe = ::popen("/usr/libexec/java_home", "r");
			if (pipe != nullptr) {
				char buf[PATH_MAX];
				if (::fgets(buf, sizeof(buf), pipe) != nullptr) {
					std::string home(buf);
					::pclose(pipe);
					// Strip trailing newline.
					while (!home.empty()
						   && (home.back() == '\n' || home.back() == '\r')) {
						home.pop_back();
					}
					if (!home.empty()) {
						return home;
					}
				} else {
					::pclose(pipe);
				}
			}
#endif

			const char *path = std::getenv("PATH");
			if (path == nullptr) {
				return {};
			}
			std::string pathStr(path);
			size_t start = 0;
			while (start <= pathStr.size()) {
				size_t end = pathStr.find(':', start);
				if (end == std::string::npos) {
					end = pathStr.size();
				}
				std::string dir = pathStr.substr(start, end - start);
				if (!dir.empty()) {
					fs::path candidate = fs::path(dir) / "java";
					if (fs::exists(candidate)) {
						std::string resolved =
							resolveExecutable(candidate.string());
						// The java binary lives in <jdk>/bin/java.
						fs::path bin  = fs::path(resolved).parent_path();
						fs::path home = bin.parent_path();
						if (fs::exists(home / "include" / "jni.h")
							|| fs::exists(home / "lib" / "server")
							|| fs::exists(home / "jre")) {
							return home.string();
						}
					}
				}
				if (end == pathStr.size()) {
					break;
				}
				start = end + 1;
			}
			return {};
		}

		// Locate the JVM shared library within a JDK home.
		std::string findJvmLibrary(const std::string &home)
		{
			if (home.empty()) {
				return {};
			}
			fs::path base(home);
#ifdef _WIN32
			std::vector<fs::path> candidates = {
				base / "bin" / "server" / "jvm.dll",
				base / "jre" / "bin" / "server" / "jvm.dll",
			};
#elif defined(__APPLE__)
			std::vector<fs::path> candidates = {
				base / "lib" / "server" / "libjvm.dylib",
				base / "jre" / "lib" / "server" / "libjvm.dylib",
			};
#else
			std::vector<fs::path> candidates = {
				base / "lib" / "server" / "libjvm.so",
				base / "jre" / "lib" / "server" / "libjvm.so",
				base / "lib" / "amd64" / "server" / "libjvm.so",
			};
#endif
			for (const auto &c: candidates) {
				if (fs::exists(c)) {
					return c.string();
				}
			}
			return {};
		}

	}	 // namespace

	GradleRunnerError Jvm::init(const std::string &javaHome,
								const std::string &bridgeJar)
	{
		if (g_vm != nullptr) {
			return GRADLE_RUNNER_OK;
		}

		GradleRunnerError rc = loadLibrary(javaHome);
		if (rc != GRADLE_RUNNER_OK) {
			return rc;
		}
		return createVm(bridgeJar);
	}

	GradleRunnerError Jvm::loadLibrary(const std::string &javaHome)
	{
		std::string home = findJavaHome(javaHome);
		if (home.empty()) {
			g_lastError = "no JDK found (set JAVA_HOME or install a JDK)";
			return GRADLE_RUNNER_JVM_NOT_FOUND;
		}
		g_javaHome = home;

		std::string lib = findJvmLibrary(home);
		if (lib.empty()) {
			g_lastError = "JVM library not found under " + home;
			return GRADLE_RUNNER_JVM_NOT_FOUND;
		}

#ifdef _WIN32
		g_jvmHandle = LoadLibraryA(lib.c_str());
		if (g_jvmHandle == nullptr) {
			g_lastError = "failed to load " + lib;
			return GRADLE_RUNNER_JVM_INIT_FAILED;
		}
#else
		g_jvmHandle = ::dlopen(lib.c_str(), RTLD_NOW | RTLD_GLOBAL);
		if (g_jvmHandle == nullptr) {
			g_lastError = "failed to load " + lib + ": " + ::dlerror();
			return GRADLE_RUNNER_JVM_INIT_FAILED;
		}
#endif
		return GRADLE_RUNNER_OK;
	}

	GradleRunnerError Jvm::createVm(const std::string &bridgeJar)
	{
#ifdef _WIN32
		auto createFn = reinterpret_cast<CreateJavaVmFn>(
			GetProcAddress(g_jvmHandle, "JNI_CreateJavaVM"));
#else
		auto createFn = reinterpret_cast<CreateJavaVmFn>(
			::dlsym(g_jvmHandle, "JNI_CreateJavaVM"));
#endif
		if (createFn == nullptr) {
			g_lastError = "JNI_CreateJavaVM not found in JVM library";
			return GRADLE_RUNNER_JVM_INIT_FAILED;
		}

		std::string classpath = "-Djava.class.path=" + bridgeJar;

		JavaVMOption options[1];
		options[0].optionString = const_cast<char *>(classpath.c_str());

		JavaVMInitArgs vmArgs;
		vmArgs.version			  = JNI_VERSION_1_8;
		vmArgs.nOptions			  = 1;
		vmArgs.options			  = options;
		vmArgs.ignoreUnrecognized = JNI_FALSE;

		JNIEnv *env = nullptr;
		jint rc		= createFn(&g_vm, reinterpret_cast<void **>(&env), &vmArgs);
		if (rc != JNI_OK || g_vm == nullptr) {
			g_lastError =
				"JNI_CreateJavaVM failed with code " + std::to_string(rc);
			return GRADLE_RUNNER_JVM_INIT_FAILED;
		}
		return GRADLE_RUNNER_OK;
	}

	JNIEnv *Jvm::env()
	{
		if (g_vm == nullptr) {
			return nullptr;
		}
		JNIEnv *env = nullptr;
		jint rc =
			g_vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_8);
		if (rc == JNI_OK) {
			return env;
		}
		if (rc == JNI_EDETACHED) {
			if (g_vm->AttachCurrentThread(reinterpret_cast<void **>(&env),
										  nullptr)
				== JNI_OK) {
				return env;
			}
		}
		return nullptr;
	}

	void Jvm::detach()
	{
		if (g_vm != nullptr) {
			g_vm->DetachCurrentThread();
		}
	}

	const std::string &Jvm::lastError()
	{
		return g_lastError;
	}

	const std::string &Jvm::resolvedJavaHome()
	{
		return g_javaHome;
	}

}	 // namespace anthony::jvm
