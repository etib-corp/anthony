cmake_minimum_required(VERSION 3.10)

if(POLICY CMP0077)
    cmake_policy(SET CMP0077 NEW)
endif()

project(anthony
    VERSION 1.0.0
    DESCRIPTION "ANTHony"
    HOMEPAGE_URL "https://etib-corp.github.io/anthony"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 01. Policy & standard setup

# 02. Options & configuration
option(BUILD_DOCS "Build Doxygen documentation" OFF)
option(BUILD_DOCS_ONLY "Configure only the documentation target" OFF)
option(BUILD_ALL_DOCS "Build documentation for all libraries" OFF)
option(BUILD_BENCHMARKS "Build the benchmark harness" OFF)
option(BUILD_TESTING "Build the test suite" OFF)
option(DEV_MODE "Enable development mode" OFF)
option(ANTHONY_USE_TOOLING_API "Use the Gradle Tooling API via JNI (requires a JDK)" ON)

if(BUILD_DOCS_ONLY)
    set(BUILD_DOCS ON)
    set(BUILD_TESTING OFF)
endif()

set(_SKIP_PLATFORM_VALIDATION FALSE)
if(BUILD_DOCS_ONLY)
    set(_SKIP_PLATFORM_VALIDATION TRUE)
endif()

# 03. Global properties

# Paths
set(HEADERS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/headers")
set(SOURCES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/sources")

if(NOT BUILD_DOCS_ONLY)
    # Dependencies
    include(FetchContent)

    # 04. Library definition
    include(cmake/Sources.cmake)

    if(BUILD_SHARED_LIBS)
        add_library(${PROJECT_NAME} SHARED)
    else()
        add_library(${PROJECT_NAME} STATIC)
    endif()

    target_sources(${PROJECT_NAME}
        PRIVATE
            ${ANTHONY_SOURCES_COMMON}
    )

    # 05. Include directories
    target_include_directories(${PROJECT_NAME}
        PUBLIC
            $<BUILD_INTERFACE:${HEADERS_DIR}>
            $<INSTALL_INTERFACE:include>
    )

    # 06. Link dependencies
    # target_link_libraries(${PROJECT_NAME}
    #     PUBLIC
    # )

    # 06b. JNI (Gradle Tooling API backend)
    if(ANTHONY_USE_TOOLING_API)
        find_package(JNI)
        if(JNI_FOUND)
            target_compile_definitions(${PROJECT_NAME}
                PRIVATE
                    ANTHONY_USE_TOOLING_API
            )
            # We only need the JNI headers: the JVM is loaded dynamically at
            # runtime (dlopen/LoadLibrary), so we must NOT link against a
            # specific libjvm (that would pin the client JVM and clash with the
            # runtime-selected JDK).
            target_include_directories(${PROJECT_NAME}
                PRIVATE
                    ${JNI_INCLUDE_DIRS}
            )
            if(UNIX)
                target_link_libraries(${PROJECT_NAME} PRIVATE ${CMAKE_DL_LIBS})
            endif()
        else()
            message(WARNING
                "JNI not found; building without the Gradle Tooling API backend "
                "(gradlew subprocess fallback will be used).")
        endif()
    endif()

    # 08. Backend-specific configuration

    if(DEV_MODE)
        target_compile_definitions(
            ${PROJECT_NAME}
            PRIVATE
            DEV
        )
    endif()

    # 08b. Packaging & consumability
    include(GNUInstallDirs)

    install(TARGETS ${PROJECT_NAME}
        EXPORT ${PROJECT_NAME}Targets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    install(DIRECTORY ${HEADERS_DIR}/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

    install(EXPORT ${PROJECT_NAME}Targets
        FILE ${PROJECT_NAME}Targets.cmake
        NAMESPACE ${PROJECT_NAME}::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
    )

    include(CMakePackageConfigHelpers)
    configure_package_config_file(
        cmake/${PROJECT_NAME}Config.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
    )
    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )
    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
    )

    # CPack packaging
    set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
    set(CPACK_PACKAGE_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}")
    set(CPACK_PACKAGE_VENDOR "ETIB Corporation")
    set(CPACK_PACKAGE_CONTACT "clement-antoine.xavier@epitech.eu")
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
    set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
    include(CPack)
endif()

# 09. Documentations

if(BUILD_ALL_DOCS OR BUILD_DOCS_ONLY)
    message(STATUS "Building documentation...")
    include(cmake/Docs.cmake)
    add_subdirectory(docs)
else()
    message(STATUS "Skipping documentation...")
endif()

# 10. Tests

if(BUILD_TESTING)
    message(STATUS "Building tests...")
    add_subdirectory(tests)
else()
    message(STATUS "Skipping tests...")
endif()

# 11. Benchmarks

if(BUILD_BENCHMARKS)
    message(STATUS "Building benchmarks...")
    add_subdirectory(benchmarks)
endif()

# 12. Examples

add_subdirectory(examples)

# 13. Java bridge (Gradle Tooling API)

if(ANTHONY_USE_TOOLING_API)
    set(_GRADLEW "${CMAKE_CURRENT_SOURCE_DIR}/java/gradlew")
    if(EXISTS "${_GRADLEW}")
        add_custom_target(gradle_runner_bridge
            COMMAND ${_GRADLEW} fatJar
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/java
            COMMENT "Building the Gradle runner bridge fat jar"
            VERBATIM
        )
    else()
        message(STATUS
            "Gradle wrapper not found; skipping gradle_runner_bridge target. "
            "Build the bridge jar manually in java/ (see README).")
    endif()
endif()
