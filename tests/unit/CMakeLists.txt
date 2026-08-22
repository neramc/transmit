# The format layer is covered by GoogleTest because it has no Qt dependency and
# these are the tests that must stay runnable on a headless machine.

add_executable(transmit_format_tests
    HashTest.cpp
    SerializationTest.cpp
    PathTokenTest.cpp
    NameSanitizerTest.cpp
    CodecTest.cpp
    BlockPackerTest.cpp
    ContainerTest.cpp
    ManifestTest.cpp)

target_link_libraries(transmit_format_tests
    PRIVATE Transmit::Format Transmit::Warnings GTest::gtest GTest::gtest_main)

gtest_discover_tests(transmit_format_tests
    PROPERTIES LABELS format
    DISCOVERY_TIMEOUT 60)
