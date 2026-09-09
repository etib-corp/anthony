add_executable(gradle_runner_example simple/main.cpp)

target_link_libraries(gradle_runner_example PRIVATE anthony)

add_executable(gradle_runner_android android/main.cpp)

target_link_libraries(gradle_runner_android PRIVATE anthony)
