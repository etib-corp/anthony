/*
 Copyright (c) 2026 ETIB Corporation

 Permission is hereby granted, free of charge, to any person obtaining a copy of
 this software and associated documentation files (the "Software"), to deal in
 the Software without restriction, including without limitation the rights to
 use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 of the Software, and to permit persons to whom the Software is furnished to do
 so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all
 copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 SOFTWARE.
 */

#include "test_gradle_runner.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include <ANTHony/GradleRunner.h>

namespace anthony::tests
{
	void TestGradleRunner::SetUp(void)
	{
	}

	void TestGradleRunner::TearDown(void)
	{
	}

	TEST_F(TestGradleRunner, CreateRejectsNullRoot)
	{
		EXPECT_EQ(gradle_runner_create(nullptr), nullptr);
	}

	TEST_F(TestGradleRunner, CreateRejectsEmptyRoot)
	{
		EXPECT_EQ(gradle_runner_create(""), nullptr);
	}

	TEST_F(TestGradleRunner, CreateRejectsNonExistentRoot)
	{
		EXPECT_EQ(gradle_runner_create("/nonexistent/path/evan-test"), nullptr);
	}

	TEST_F(TestGradleRunner, CreateRejectsFileInsteadOfDirectory)
	{
		// An existing regular file is not a project directory.
		const std::string path =
			std::string(std::getenv("TMPDIR")) + "/evan-not-a-dir";
		FILE *file = std::fopen(path.c_str(), "wb");
		ASSERT_NE(file, nullptr);
		std::fclose(file);

		EXPECT_EQ(gradle_runner_create(path.c_str()), nullptr);
		std::remove(path.c_str());
	}

	TEST_F(TestGradleRunner, CreateAndDestroyValidDirectory)
	{
		const std::string dir =
			std::string(std::getenv("TMPDIR")) + "/evan-dir";
		ASSERT_EQ(std::filesystem::create_directory(dir), true);

		GradleRunner *runner = gradle_runner_create(dir.c_str());
		EXPECT_NE(runner, nullptr);

		gradle_runner_destroy(runner);
		gradle_runner_destroy(nullptr);	   // must be a no-op
		std::filesystem::remove_all(dir);
	}

	TEST_F(TestGradleRunner, SetEnvRejectsNullOrEmptyKey)
	{
		const std::string dir =
			std::string(std::getenv("TMPDIR")) + "/evan-env";
		ASSERT_EQ(std::filesystem::create_directory(dir), true);
		GradleRunner *runner = gradle_runner_create(dir.c_str());
		ASSERT_NE(runner, nullptr);

		EXPECT_EQ(gradle_runner_set_env(nullptr, "KEY", "value"),
				  GRADLE_RUNNER_INVALID_ARGUMENT);
		EXPECT_EQ(gradle_runner_set_env(runner, nullptr, "value"),
				  GRADLE_RUNNER_INVALID_ARGUMENT);
		EXPECT_EQ(gradle_runner_set_env(runner, "", "value"),
				  GRADLE_RUNNER_INVALID_ARGUMENT);
		EXPECT_EQ(gradle_runner_set_env(runner, "KEY", "value"),
				  GRADLE_RUNNER_OK);

		gradle_runner_destroy(runner);
		std::filesystem::remove_all(dir);
	}

	TEST_F(TestGradleRunner, AddJvmArgRejectsNullOrEmpty)
	{
		const std::string dir =
			std::string(std::getenv("TMPDIR")) + "/evan-jvm";
		ASSERT_EQ(std::filesystem::create_directory(dir), true);
		GradleRunner *runner = gradle_runner_create(dir.c_str());
		ASSERT_NE(runner, nullptr);

		EXPECT_EQ(gradle_runner_add_jvm_arg(nullptr, "-Xmx256m"),
				  GRADLE_RUNNER_INVALID_ARGUMENT);
		EXPECT_EQ(gradle_runner_add_jvm_arg(runner, nullptr),
				  GRADLE_RUNNER_INVALID_ARGUMENT);
		EXPECT_EQ(gradle_runner_add_jvm_arg(runner, ""),
				  GRADLE_RUNNER_INVALID_ARGUMENT);
		EXPECT_EQ(gradle_runner_add_jvm_arg(runner, "-Xmx256m"),
				  GRADLE_RUNNER_OK);

		gradle_runner_destroy(runner);
		std::filesystem::remove_all(dir);
	}

	TEST_F(TestGradleRunner, AddGradleOptRejectsNullOrEmpty)
	{
		const std::string dir =
			std::string(std::getenv("TMPDIR")) + "/evan-opt";
		ASSERT_EQ(std::filesystem::create_directory(dir), true);
		GradleRunner *runner = gradle_runner_create(dir.c_str());
		ASSERT_NE(runner, nullptr);

		EXPECT_EQ(gradle_runner_add_gradle_opt(nullptr, "--stacktrace"),
				  GRADLE_RUNNER_INVALID_ARGUMENT);
		EXPECT_EQ(gradle_runner_add_gradle_opt(runner, nullptr),
				  GRADLE_RUNNER_INVALID_ARGUMENT);
		EXPECT_EQ(gradle_runner_add_gradle_opt(runner, ""),
				  GRADLE_RUNNER_INVALID_ARGUMENT);
		EXPECT_EQ(gradle_runner_add_gradle_opt(runner, "--stacktrace"),
				  GRADLE_RUNNER_OK);

		gradle_runner_destroy(runner);
		std::filesystem::remove_all(dir);
	}

	TEST_F(TestGradleRunner, SetJavaHomeRejectsNullOrEmpty)
	{
		const std::string dir = std::string(std::getenv("TMPDIR")) + "/evan-jh";
		ASSERT_EQ(std::filesystem::create_directory(dir), true);
		GradleRunner *runner = gradle_runner_create(dir.c_str());
		ASSERT_NE(runner, nullptr);

		EXPECT_EQ(gradle_runner_set_java_home(nullptr, "/opt/jdk"),
				  GRADLE_RUNNER_INVALID_ARGUMENT);
		EXPECT_EQ(gradle_runner_set_java_home(runner, nullptr),
				  GRADLE_RUNNER_INVALID_ARGUMENT);
		EXPECT_EQ(gradle_runner_set_java_home(runner, ""),
				  GRADLE_RUNNER_INVALID_ARGUMENT);
		EXPECT_EQ(gradle_runner_set_java_home(runner, "/opt/jdk"),
				  GRADLE_RUNNER_OK);

		gradle_runner_destroy(runner);
		std::filesystem::remove_all(dir);
	}

	TEST_F(TestGradleRunner, SetBridgeJarRejectsNullOrEmpty)
	{
		const std::string dir = std::string(std::getenv("TMPDIR")) + "/evan-bj";
		ASSERT_EQ(std::filesystem::create_directory(dir), true);
		GradleRunner *runner = gradle_runner_create(dir.c_str());
		ASSERT_NE(runner, nullptr);

		EXPECT_EQ(gradle_runner_set_bridge_jar(nullptr, "bridge.jar"),
				  GRADLE_RUNNER_INVALID_ARGUMENT);
		EXPECT_EQ(gradle_runner_set_bridge_jar(runner, nullptr),
				  GRADLE_RUNNER_INVALID_ARGUMENT);
		EXPECT_EQ(gradle_runner_set_bridge_jar(runner, ""),
				  GRADLE_RUNNER_INVALID_ARGUMENT);
		EXPECT_EQ(gradle_runner_set_bridge_jar(runner, "bridge.jar"),
				  GRADLE_RUNNER_OK);

		gradle_runner_destroy(runner);
		std::filesystem::remove_all(dir);
	}

	TEST_F(TestGradleRunner, SetAndroidSdkRejectsNullOrEmpty)
	{
		const std::string dir =
			std::string(std::getenv("TMPDIR")) + "/evan-sdk";
		ASSERT_EQ(std::filesystem::create_directory(dir), true);
		GradleRunner *runner = gradle_runner_create(dir.c_str());
		ASSERT_NE(runner, nullptr);

		EXPECT_EQ(gradle_runner_set_android_sdk(nullptr, "/sdk"),
				  GRADLE_RUNNER_INVALID_ARGUMENT);
		EXPECT_EQ(gradle_runner_set_android_sdk(runner, nullptr),
				  GRADLE_RUNNER_INVALID_ARGUMENT);
		EXPECT_EQ(gradle_runner_set_android_sdk(runner, ""),
				  GRADLE_RUNNER_INVALID_ARGUMENT);
		EXPECT_EQ(gradle_runner_set_android_sdk(runner, "/sdk"),
				  GRADLE_RUNNER_OK);

		gradle_runner_destroy(runner);
		std::filesystem::remove_all(dir);
	}

	TEST_F(TestGradleRunner, SetSigningConfigRejectsNullRunner)
	{
		EXPECT_EQ(gradle_runner_set_signing_config(nullptr, "store", "spass",
												   "alias", "kpass"),
				  GRADLE_RUNNER_INVALID_ARGUMENT);
	}

	TEST_F(TestGradleRunner, CancelRejectsNullRunner)
	{
		EXPECT_EQ(gradle_runner_cancel(nullptr),
				  GRADLE_RUNNER_INVALID_ARGUMENT);
	}

	TEST_F(TestGradleRunner, FindApkRejectsNullRunner)
	{
		char *path = nullptr;
		EXPECT_EQ(gradle_runner_find_apk(nullptr, &path),
				  GRADLE_RUNNER_INVALID_ARGUMENT);
	}

	TEST_F(TestGradleRunner, FindAabRejectsNullRunner)
	{
		char *path = nullptr;
		EXPECT_EQ(gradle_runner_find_aab(nullptr, &path),
				  GRADLE_RUNNER_INVALID_ARGUMENT);
	}

	TEST_F(TestGradleRunner, FindApkNoArtifactsReturnsOkAndNullPath)
	{
		const std::string dir =
			std::string(std::getenv("TMPDIR")) + "/evan-empty-apk";
		ASSERT_EQ(std::filesystem::create_directory(dir), true);
		GradleRunner *runner = gradle_runner_create(dir.c_str());
		ASSERT_NE(runner, nullptr);

		char *path = reinterpret_cast<char *>(0x1);
		EXPECT_EQ(gradle_runner_find_apk(runner, &path), GRADLE_RUNNER_OK);
		EXPECT_EQ(path, nullptr);

		gradle_runner_destroy(runner);
		std::filesystem::remove_all(dir);
	}

	TEST_F(TestGradleRunner, ErrorStringsAreNonEmptyAndStable)
	{
		EXPECT_NE(gradle_runner_error_string(GRADLE_RUNNER_OK), nullptr);
		EXPECT_NE(gradle_runner_error_string(GRADLE_RUNNER_INVALID_ARGUMENT),
				  nullptr);
		EXPECT_NE(gradle_runner_error_string(GRADLE_RUNNER_TIMEOUT), nullptr);
		EXPECT_EQ(gradle_runner_error_string(GRADLE_RUNNER_OK),
				  gradle_runner_error_string(GRADLE_RUNNER_OK));
	}
}	 // namespace anthony::tests
