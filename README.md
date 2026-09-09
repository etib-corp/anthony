# ANTHony

ANTHony

## What It Provides

- A C-compatible API for loading and running a Maven/Gradle-based Java/Kotlin
  project, capturing stdout/stderr and the exit code, with support for
  environment variables, JVM args, Gradle options, and an optional per-run
  timeout.
- Two backends:
  - **Gradle Tooling API** (default) — embeds a JVM in-process and drives the
    build through the official `org.gradle:gradle-tooling-api`, wrapper-aware
    and version-independent.
  - **`gradlew` subprocess** — a fallback used automatically when the Tooling
    API is unavailable (no JDK, no bridge jar) but a `gradlew` wrapper exists.

## Gradle Runner

The public API lives in `headers/ANTHony/GradleRunner.h`. It exposes an opaque
`GradleRunner` handle plus the following functions:

- `gradle_runner_create` / `gradle_runner_destroy`
- `gradle_runner_run_task` (task, extra args, timeout, stdout/stderr/exit code)
- `gradle_runner_set_env`, `gradle_runner_add_jvm_arg`,
  `gradle_runner_add_gradle_opt`
- `gradle_runner_set_java_home`, `gradle_runner_set_bridge_jar`,
  `gradle_runner_cancel`
- `gradle_runner_set_android_sdk`, `gradle_runner_set_signing_config`,
  `gradle_runner_find_apk`, `gradle_runner_find_aab`
- `gradle_runner_free_string`, `gradle_runner_error_string`

A minimal C++ usage example:

```cpp
#include "ANTHony/GradleRunner.h"

GradleRunner *runner = gradle_runner_create("/path/to/project");
gradle_runner_set_bridge_jar(runner, "/path/to/gradle-runner-bridge.jar");

char *out = nullptr;
char *err = nullptr;
int exitCode = 0;

GradleRunnerError rc = gradle_runner_run_task(
    runner, "build", nullptr, 0, 0, &out, &err, &exitCode);

if (rc == GRADLE_RUNNER_OK) {
    // use out / err / exitCode
}

gradle_runner_free_string(out);
gradle_runner_free_string(err);
gradle_runner_destroy(runner);
```

See `examples/simple/main.cpp` for a complete runnable example.

### Building Android projects

The library can build Android projects programmatically. Android builds are
ordinary Gradle builds driven by the Android Gradle Plugin, so any task
(`assembleDebug`, `assembleRelease`, `bundleRelease`, `test`, ...) works through
`gradle_runner_run_task`. Build variants and flavors are selected by task name
(e.g. `assemble<Flavor><BuildType>`).

```cpp
GradleRunner *runner = gradle_runner_create("/path/to/android/project");
gradle_runner_set_bridge_jar(runner, "/path/to/gradle-runner-bridge.jar");
gradle_runner_set_android_sdk(runner, "/path/to/Android/sdk");

char *out = nullptr, *err = nullptr;
int exitCode = 0;
GradleRunnerError rc = gradle_runner_run_task(
    runner, "assembleDebug", nullptr, 0, 0, &out, &err, &exitCode);

char *apk = nullptr;
gradle_runner_find_apk(runner, &apk);   // newest *.apk under build outputs
gradle_runner_find_aab(runner, &aab);   // newest *.aab under build outputs

gradle_runner_free_string(apk);
gradle_runner_free_string(out);
gradle_runner_free_string(err);
gradle_runner_destroy(runner);
```

- `gradle_runner_set_android_sdk` sets `ANDROID_HOME`/`ANDROID_SDK_ROOT` (the
  non-invasive alternative to `local.properties`).
- `gradle_runner_set_signing_config` injects the AGP
  `android.injected.signing.*` properties for release signing without editing
  the project's build files.
- `gradle_runner_find_apk` / `gradle_runner_find_aab` return the newest build
  artifact by modification time.

See `examples/android/main.cpp` for a complete runnable example.

### Building the Java bridge

The Tooling API backend needs a small Java bridge packaged as a fat jar. The
bridge is a Gradle project (generated with `gradle init`) under `java/`, with a
committed `gradlew` wrapper. Build it with:

```sh
cd java
./gradlew fatJar
# produces java/lib/build/libs/gradle-runner-bridge.jar
```

The C++ library locates the bridge jar via `gradle_runner_set_bridge_jar()`, or
by searching `gradle-runner-bridge.jar`,
`java/lib/build/libs/gradle-runner-bridge.jar`, and
`../java/lib/build/libs/gradle-runner-bridge.jar` relative to the working
directory. A CMake convenience target is also available:

```sh
cmake --build build --target gradle_runner_bridge
```

### JVM discovery

The embedded JVM is located from, in order: `gradle_runner_set_java_home()`,
the `JAVA_HOME` environment variable, `/usr/libexec/java_home` (macOS), then
`java` on `PATH`. The Gradle daemon forked by the Tooling API must run on a
Java version supported by the target Gradle version (e.g. Java 17–24 for Gradle
9.x).

## Key Concepts


## Quickstart

ANTHony is a library; it is consumed by an application (such as XIDER) that
provides a platform implementation. A minimal setup selects one backend and one
platform:

```sh
cmake -S . -B build
cmake --build build
```

## Building

### Dependencies

- CMake 3.10+
- A C++20 compiler
- A JDK (for the Tooling API backend; `-DANTHONY_USE_TOOLING_API=OFF` to build
  without it)
- Doxygen + Graphviz (only for `-DBUILD_DOCS=ON`)

### Configure and build

Select exactly one backend and one platform:

```sh
cmake -S . -B build \
  -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Optional options:

- `-DBUILD_TESTING=ON` — build and run the test suite.
- `-DBUILD_DOCS=ON` — build Doxygen documentation.
- `-DBUILD_BENCHMARKS=ON` — build the benchmark harness.
- `-DANTHONY_USE_TOOLING_API=OFF` — disable the JNI/Tooling API backend and use
  only the `gradlew` subprocess fallback.

## Consuming as a dependency

Once installed, downstream projects can use `find_package(anthony)`:

```sh
cmake --install build --prefix /path/to/prefix
```

```cmake
find_package(anthony REQUIRED)
target_link_libraries(my_app PRIVATE anthony::anthony)
```

## Documentation

- [How ANTHony Works](docs/HOW_ANTHONY_WORKS.md)
- [Architecture](docs/ARCHITECTURE.md)
- [Getting Started](docs/GETTING_STARTED.md)
- [Versioning & Support](docs/VERSIONING.md)
- [Technical Choices](docs/TECHNICAL_CHOICES.md)
- [Code Conventions](docs/CODE_CONVENTIONS.md)
- [Commit Conventions](docs/COMMIT_CONVENTIONS.md)

## Contributing

We welcome contributions from the community! If you're interested in
contributing to ANTHony, please check out our
[Contributing Guidelines](CONTRIBUTING.md) for more information on how to get
involved.

## License

ANTHony is released under the [MIT License](LICENSE). See
[CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md) for our community standards and
[SECURITY.md](SECURITY.md) for reporting vulnerabilities.
