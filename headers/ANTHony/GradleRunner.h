#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Opaque handle to a Gradle runner instance.
 *
 * A runner is bound to a single Gradle project root and may be reused to run
 * multiple tasks. It is not thread-safe: a given instance must not be used
 * concurrently from multiple threads.
 */
typedef struct GradleRunner GradleRunner;

/**
 * Result codes returned by the runner API.
 *
 * A non-zero Gradle exit code is *not* an error: it is reported through the
 * `exit_code` out-parameter of gradle_runner_run_task(). These codes describe
 * only transport-level failures (bad arguments, missing wrapper, spawn
 * failure, timeout, I/O problems).
 */
typedef enum GradleRunnerError {
	GRADLE_RUNNER_OK				= 0,
	GRADLE_RUNNER_INVALID_ARGUMENT	= 1,
	GRADLE_RUNNER_PROJECT_NOT_FOUND = 2,
	GRADLE_RUNNER_WRAPPER_NOT_FOUND = 3,
	GRADLE_RUNNER_SPAWN_FAILED		= 4,
	GRADLE_RUNNER_TIMEOUT			= 5,
	GRADLE_RUNNER_IO_ERROR			= 6,
	GRADLE_RUNNER_INTERNAL_ERROR	= 7,
	GRADLE_RUNNER_JVM_NOT_FOUND		= 8,
	GRADLE_RUNNER_JVM_INIT_FAILED	= 9,
	GRADLE_RUNNER_BRIDGE_NOT_FOUND	= 10,
	GRADLE_RUNNER_CANCELLED			= 11
} GradleRunnerError;

/**
 * Create a runner bound to the given Gradle project root.
 *
 * @param project_root Path to the directory containing `gradlew` (or
 *                     `gradlew.bat` on Windows). May be relative or absolute.
 * @return A new runner, or NULL on allocation failure.
 *
 * The path is validated for existence and directory-ness at creation time;
 * the wrapper itself is resolved lazily at run time.
 */
GradleRunner *gradle_runner_create(const char *project_root);

/**
 * Destroy a runner and release all associated resources.
 *
 * @param runner The runner to destroy. May be NULL (no-op).
 */
void gradle_runner_destroy(GradleRunner *runner);

/**
 * Run a Gradle task in the bound project.
 *
 * @param runner          The runner.
 * @param task            Task name, e.g. "build", "test", "myTask".
 * @param extra_args      Array of extra arguments passed to Gradle after the
 *                        task. May be NULL if extra_args_count is 0.
 * @param extra_args_count Number of entries in extra_args.
 * @param timeout_ms      Maximum wall-clock time in milliseconds. 0 means no
 *                        timeout. On timeout the child is killed and
 *                        GRADLE_RUNNER_TIMEOUT is returned.
 * @param out_stdout      Receives captured stdout. Set to NULL on hard errors.
 *                        Caller must free with gradle_runner_free_string().
 * @param out_stderr      Receives captured stderr. Set to NULL on hard errors.
 *                        Caller must free with gradle_runner_free_string().
 * @param exit_code       Receives the child exit code. Only meaningful when
 *                        the return value is GRADLE_RUNNER_OK.
 * @return GRADLE_RUNNER_OK on success (regardless of the Gradle exit code),
 *         or an error code describing a transport-level failure.
 *
 * Ownership: on success (and on timeout, where partial output is returned),
 * the caller owns *out_stdout and *out_stderr and must release them with
 * gradle_runner_free_string(). On other errors both are set to NULL.
 */
GradleRunnerError gradle_runner_run_task(GradleRunner *runner, const char *task,
										 const char *const *extra_args,
										 int extra_args_count, int timeout_ms,
										 char **out_stdout, char **out_stderr,
										 int *exit_code);

/**
 * Set an environment variable for subsequent runs.
 *
 * @param runner The runner.
 * @param key    Variable name (must be non-NULL, non-empty).
 * @param value  Variable value (may be NULL, treated as empty).
 * @return GRADLE_RUNNER_OK, or GRADLE_RUNNER_INVALID_ARGUMENT.
 */
GradleRunnerError gradle_runner_set_env(GradleRunner *runner, const char *key,
										const char *value);

/**
 * Add a JVM argument for subsequent runs.
 *
 * JVM arguments are forwarded to Gradle through the GRADLE_OPTS environment
 * variable. @param arg must be non-NULL.
 *
 * @return GRADLE_RUNNER_OK, or GRADLE_RUNNER_INVALID_ARGUMENT.
 */
GradleRunnerError gradle_runner_add_jvm_arg(GradleRunner *runner,
											const char *arg);

/**
 * Add a Gradle option (e.g. "--stacktrace", "--no-daemon") for subsequent
 * runs. @param opt must be non-NULL.
 *
 * @return GRADLE_RUNNER_OK, or GRADLE_RUNNER_INVALID_ARGUMENT.
 */
GradleRunnerError gradle_runner_add_gradle_opt(GradleRunner *runner,
											   const char *opt);

/**
 * Set the JDK home directory used to locate the embedded JVM.
 *
 * If not set, the library resolves the JVM from JAVA_HOME, then from `java`
 * on PATH. Must be called before the first gradle_runner_run_task().
 *
 * @return GRADLE_RUNNER_OK, or GRADLE_RUNNER_INVALID_ARGUMENT.
 */
GradleRunnerError gradle_runner_set_java_home(GradleRunner *runner,
											  const char *java_home);

/**
 * Set the path to the Gradle runner bridge fat jar.
 *
 * If not set, the library searches a set of default locations (see README).
 * Must be called before the first gradle_runner_run_task().
 *
 * @return GRADLE_RUNNER_OK, or GRADLE_RUNNER_INVALID_ARGUMENT.
 */
GradleRunnerError gradle_runner_set_bridge_jar(GradleRunner *runner,
											   const char *bridge_jar);

/**
 * Cancel a build currently in progress.
 *
 * This is safe to call from another thread while gradle_runner_run_task() is
 * blocking. The in-progress run returns GRADLE_RUNNER_CANCELLED.
 *
 * @return GRADLE_RUNNER_OK, or GRADLE_RUNNER_INVALID_ARGUMENT.
 */
GradleRunnerError gradle_runner_cancel(GradleRunner *runner);

/**
 * Set the Android SDK location for subsequent runs.
 *
 * Sets the ANDROID_HOME and ANDROID_SDK_ROOT environment variables, which the
 * Android Gradle Plugin uses to locate the SDK. This is the non-invasive
 * alternative to writing `local.properties` (sdk.dir=...) into the project.
 *
 * @param runner   The runner.
 * @param sdk_path Path to the Android SDK root (e.g. ~/Library/Android/sdk).
 * @return GRADLE_RUNNER_OK, or GRADLE_RUNNER_INVALID_ARGUMENT.
 */
GradleRunnerError gradle_runner_set_android_sdk(GradleRunner *runner,
												const char *sdk_path);

/**
 * Configure release signing for subsequent runs.
 *
 * Injects the Android Gradle Plugin's `android.injected.signing.*` project
 * properties, allowing release builds to be signed without editing the
 * project's build files. Any NULL/empty parameter is skipped.
 *
 * @param runner         The runner.
 * @param store_file     Path to the keystore file.
 * @param store_password Keystore password.
 * @param key_alias      Key alias within the keystore.
 * @param key_password   Key password.
 * @return GRADLE_RUNNER_OK, or GRADLE_RUNNER_INVALID_ARGUMENT.
 */
GradleRunnerError gradle_runner_set_signing_config(GradleRunner *runner,
												   const char *store_file,
												   const char *store_password,
												   const char *key_alias,
												   const char *key_password);

/**
 * Find the most recently produced APK under the project's build outputs.
 *
 * Searches the project's build outputs for `*.apk` and returns the
 * newest by modification time. Call after a successful `assemble*` task.
 *
 * @param runner   The runner.
 * @param out_path Receives the APK path. Caller must free with
 *                 gradle_runner_free_string(). Set to NULL if none found.
 * @return GRADLE_RUNNER_OK, or GRADLE_RUNNER_INVALID_ARGUMENT.
 */
GradleRunnerError gradle_runner_find_apk(GradleRunner *runner, char **out_path);

/**
 * Find the most recently produced AAB under the project's build outputs.
 *
 * Searches the project's build outputs for `*.aab` and returns the
 * newest by modification time. Call after a successful `bundle*` task.
 *
 * @param runner   The runner.
 * @param out_path Receives the AAB path. Caller must free with
 *                 gradle_runner_free_string(). Set to NULL if none found.
 * @return GRADLE_RUNNER_OK, or GRADLE_RUNNER_INVALID_ARGUMENT.
 */
GradleRunnerError gradle_runner_find_aab(GradleRunner *runner, char **out_path);

/**
 * Free a string returned by gradle_runner_run_task().
 *
 * @param s The string to free. May be NULL (no-op).
 */
void gradle_runner_free_string(char *s);

/**
 * Return a human-readable description of an error code.
 *
 * The returned pointer is statically allocated and must not be freed.
 */
const char *gradle_runner_error_string(GradleRunnerError error);

#ifdef __cplusplus
}
#endif

#endif /* ANTHONY_GRADLE_RUNNER_H */
