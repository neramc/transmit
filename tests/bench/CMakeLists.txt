# The benchmark, built only when asked for. Not a ctest test: a number
# that fails the build because the machine was busy teaches everyone to
# ignore the build.

add_executable(transmit_bench BenchMain.cpp)
target_link_libraries(transmit_bench PRIVATE Transmit::Core Transmit::Warnings)
set_target_properties(transmit_bench PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
