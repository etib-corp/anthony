package com.anthony;

import org.gradle.tooling.BuildLauncher;
import org.gradle.tooling.CancellationTokenSource;
import org.gradle.tooling.GradleConnector;
import org.gradle.tooling.ProjectConnection;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.OutputStream;
import java.util.Map;
import java.util.concurrent.atomic.AtomicReference;

/**
 * JNI-facing bridge between the C++ library and the Gradle Tooling API.
 *
 * All methods are static and thread-safe with respect to a single running
 * build: the C++ side serializes access, and {@link #cancel()} may be called
 * from another thread to interrupt a build in progress.
 */
public final class GradleRunnerBridge {

    private static final AtomicReference<ProjectConnection> CONNECTION =
            new AtomicReference<>();
    private static final AtomicReference<CancellationTokenSource> CANCEL_SOURCE =
            new AtomicReference<>();

    private static final ByteArrayOutputStream STDOUT =
            new ByteArrayOutputStream();
    private static final ByteArrayOutputStream STDERR =
            new ByteArrayOutputStream();
    private static volatile String lastError = "";

    private GradleRunnerBridge() {
    }

    /**
     * Run one or more Gradle tasks in the given project directory.
     *
     * @param projectDir absolute path to the Gradle project root
     * @param javaHome   JDK home for the Gradle daemon (may be empty to use the
     *                   client JVM)
     * @param tasks      task names to run (may be empty to run default tasks)
     * @param jvmArgs    JVM arguments for the Gradle daemon
     * @param gradleArgs extra Gradle command-line arguments
     * @param env        environment variables for the build (may be empty)
     * @param timeoutMs  timeout in milliseconds; 0 means no timeout
     * @return 0 on success, non-zero on failure
     */
    public static int runTask(String projectDir, String javaHome,
                              String[] tasks, String[] jvmArgs,
                              String[] gradleArgs, Map<String, String> env,
                              int timeoutMs) {
        reset();
        try {
            ProjectConnection connection = GradleConnector.newConnector()
                    .forProjectDirectory(new File(projectDir))
                    .connect();
            CONNECTION.set(connection);

            BuildLauncher launcher = connection.newBuild();
            if (javaHome != null && !javaHome.isEmpty()) {
                launcher.setJavaHome(new File(javaHome));
            }
            if (tasks != null && tasks.length > 0) {
                launcher.forTasks(tasks);
            }
            if (jvmArgs != null && jvmArgs.length > 0) {
                launcher.setJvmArguments(jvmArgs);
            }
            if (gradleArgs != null && gradleArgs.length > 0) {
                launcher.withArguments(gradleArgs);
            }
            if (env != null && !env.isEmpty()) {
                launcher.setEnvironmentVariables(env);
            }
            launcher.setStandardOutput(new TeeOutputStream(STDOUT));
            launcher.setStandardError(new TeeOutputStream(STDERR));

            CancellationTokenSource cancelSource =
                    GradleConnector.newCancellationTokenSource();
            CANCEL_SOURCE.set(cancelSource);
            launcher.withCancellationToken(cancelSource.token());

            launcher.run();
            return 0;
        } catch (Throwable t) {
            lastError = describe(t);
            return 1;
        } finally {
            CANCEL_SOURCE.set(null);
            ProjectConnection connection = CONNECTION.getAndSet(null);
            if (connection != null) {
                try {
                    connection.close();
                } catch (Throwable ignored) {
                    // Best-effort close.
                }
            }
        }
    }

    /** Cancel a build currently in progress (no-op if none is running). */
    public static void cancel() {
        CancellationTokenSource cancelSource = CANCEL_SOURCE.get();
        if (cancelSource != null) {
            cancelSource.cancel();
        }
    }

    /** Return captured stdout from the most recent run. */
    public static String getStdout() {
        synchronized (STDOUT) {
            return STDOUT.toString();
        }
    }

    /** Return captured stderr from the most recent run. */
    public static String getStderr() {
        synchronized (STDERR) {
            return STDERR.toString();
        }
    }

    /** Return the last error message, or an empty string if none. */
    public static String getLastError() {
        return lastError;
    }

    private static void reset() {
        synchronized (STDOUT) {
            STDOUT.reset();
        }
        synchronized (STDERR) {
            STDERR.reset();
        }
        lastError = "";
    }

    private static String describe(Throwable t) {
        StringBuilder sb = new StringBuilder();
        Throwable cur = t;
        while (cur != null) {
            if (sb.length() > 0) {
                sb.append(": ");
            }
            sb.append(cur.getClass().getSimpleName());
            if (cur.getMessage() != null) {
                sb.append(": ").append(cur.getMessage());
            }
            cur = cur.getCause();
        }
        return sb.toString();
    }

    /** OutputStream that forwards to a shared buffer (thread-safe). */
    private static final class TeeOutputStream extends OutputStream {
        private final ByteArrayOutputStream target;

        TeeOutputStream(ByteArrayOutputStream target) {
            this.target = target;
        }

        @Override
        public void write(int b) {
            synchronized (target) {
                target.write(b);
            }
        }

        @Override
        public void write(byte[] b, int off, int len) {
            synchronized (target) {
                target.write(b, off, len);
            }
        }
    }
}
