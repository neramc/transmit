# Fuzz targets over the format layer, which is where bytes from another
# machine first arrive.
#
# Two builds from one source. With TRANSMIT_BUILD_FUZZERS=ON and clang,
# each target is a libFuzzer binary; otherwise it is an ordinary program
# that replays a directory of inputs. The second form is what keeps a
# crasher fixed on a machine that cannot fuzz - the CI job that finds one
# commits it under corpus/regressions/ and the ctest target below replays
# it forever after.

set(TRANSMIT_FUZZ_TARGETS
    FuzzManifest
    FuzzContainerOpen
    FuzzSerialization
    FuzzPathToken)

foreach(target ${TRANSMIT_FUZZ_TARGETS})
    add_executable(transmit_fuzz_${target} ${target}.cpp)
    target_link_libraries(transmit_fuzz_${target} PRIVATE Transmit::Format Transmit::Warnings)
    target_include_directories(transmit_fuzz_${target}
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")

    if(TRANSMIT_BUILD_FUZZERS)
        target_compile_definitions(transmit_fuzz_${target} PRIVATE TRANSMIT_FUZZING_ENABLED)
        target_compile_options(transmit_fuzz_${target}
            PRIVATE -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
        target_link_options(transmit_fuzz_${target}
            PRIVATE -fsanitize=fuzzer,address,undefined)
    else()
        # Replaying the committed crashers is a normal test, so it runs
        # everywhere rather than only where the fuzzer builds.
        add_test(NAME FuzzRegression.${target}
                 COMMAND transmit_fuzz_${target}
                         "${CMAKE_CURRENT_SOURCE_DIR}/corpus/regressions/${target}")
        set_tests_properties(FuzzRegression.${target} PROPERTIES LABELS fuzz)
    endif()
endforeach()
