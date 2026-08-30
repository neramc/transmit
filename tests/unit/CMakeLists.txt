# The format layer is covered by GoogleTest because it has no Qt dependency and
# these are the tests that must stay runnable on a headless machine.

add_executable(transmit_format_tests
    HashTest.cpp
    Md5Test.cpp
    SerializationTest.cpp
    PathTokenTest.cpp
    NameSanitizerTest.cpp
    CodecTest.cpp
    BlockPackerTest.cpp
    ContainerTest.cpp
    FileIoTest.cpp
    ManifestTest.cpp
    VerificationTest.cpp
    TransferJournalTest.cpp)

target_link_libraries(transmit_format_tests
    PRIVATE Transmit::Format Transmit::Warnings GTest::gtest GTest::gtest_main)

# The shared temporary-directory helper is included as
# "support/TempDirectory.h", so the tests directory itself is on the
# include path.
target_include_directories(transmit_format_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")

gtest_discover_tests(transmit_format_tests
    PROPERTIES LABELS format
    DISCOVERY_TIMEOUT 60)
