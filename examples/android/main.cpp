#include "ANTHony/GradleRunner.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

int main(int argc, char **argv)
{
	const char *projectRoot = argc > 1 ? argv[1] : ".";
	const char *bridgeJar	= argc > 2 ? argv[2] : nullptr;
	const char *sdkPath		= argc > 3 ? argv[3] : nullptr;

	GradleRunner *runner = gradle_runner_create(projectRoot);
	if (runner == nullptr) {
		std::fprintf(stderr, "error: '%s' is not a valid project root\n",
					 projectRoot);
		return 1;
	}

	if (bridgeJar != nullptr) {
		gradle_runner_set_bridge_jar(runner, bridgeJar);
	}
	if (sdkPath != nullptr) {
		gradle_runner_set_android_sdk(runner, sdkPath);
	}

	char *out	 = nullptr;
	char *err	 = nullptr;
	int exitCode = 0;

	GradleRunnerError rc = gradle_runner_run_task(
		runner, "assembleDebug", nullptr, 0, 0, &out, &err, &exitCode);

	if (rc != GRADLE_RUNNER_OK) {
		std::fprintf(stderr, "error: %s\n", gradle_runner_error_string(rc));
		gradle_runner_free_string(out);
		gradle_runner_free_string(err);
		gradle_runner_destroy(runner);
		return 1;
	}

	std::printf("exit code: %d\n", exitCode);
	if (out != nullptr) {
		std::printf("--- stdout ---\n%s\n", out);
	}
	if (err != nullptr) {
		std::printf("--- stderr ---\n%s\n", err);
	}

	char *apk = nullptr;
	if (gradle_runner_find_apk(runner, &apk) == GRADLE_RUNNER_OK
		&& apk != nullptr) {
		std::printf("--- APK ---\n%s\n", apk);
		gradle_runner_free_string(apk);
	}

	gradle_runner_free_string(out);
	gradle_runner_free_string(err);
	gradle_runner_destroy(runner);
	return exitCode == 0 ? 0 : 1;
}
