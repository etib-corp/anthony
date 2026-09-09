#ifndef ANTHONY_JVM_HPP
#define ANTHONY_JVM_HPP

#include "ANTHony/GradleRunner.h"

#include <jni.h>

#include <string>

namespace anthony::jvm
{

	/**
	 * Embedded JVM manager.
	 *
	 * Loads a JVM in-process (via JNI_CreateJavaVM), resolves the bridge class,
	 * and provides thread-local JNIEnv access. The JVM is created lazily on
	 * first use and lives for the duration of the process.
	 */
	class Jvm
	{
		public:
		/**
		 * Initialize the JVM (idempotent). Resolves the JVM library from
		 * `javaHome` (if non-empty) or from `java` on PATH, then creates the
		 * JVM with the bridge jar on the classpath.
		 *
		 * @return GRADLE_RUNNER_OK on success, or a JVM-related error code.
		 */
		static GradleRunnerError init(const std::string &javaHome,
									  const std::string &bridgeJar);

		/** Return the JNIEnv for the calling thread, attaching if necessary. */
		static JNIEnv *env();

		/** Detach the calling thread from the JVM (no-op if not attached). */
		static void detach();

		/** Return the last error message produced during initialization. */
		static const std::string &lastError();

		/** Return the resolved JDK home (empty before init). */
		static const std::string &resolvedJavaHome();

		private:
		Jvm() = default;

		static GradleRunnerError loadLibrary(const std::string &javaHome);
		static GradleRunnerError createVm(const std::string &bridgeJar);
	};

}	 // namespace anthony::jvm

#endif	  // ANTHONY_JVM_HPP
