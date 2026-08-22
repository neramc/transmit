# The format layer is deliberately free of Qt: it holds the archive container,
# the codecs and the cross-OS path rules, all of which are pure computation.
# That keeps them testable without a GUI toolkit and keeps the heavy lifting
# out of any single UI framework's way.

add_library(transmit_format STATIC
    Bytes.cpp
    Result.cpp
    Serialization.cpp
    FileIo.cpp
    PathToken.cpp
    NameSanitizer.cpp
    Manifest.cpp
    VolumeSplitter.cpp
    BlockPacker.cpp
    Container.cpp
    codec/Codec.cpp
    codec/StoreCodec.cpp
    codec/DeflateCodec.cpp
    codec/ZstdCodec.cpp
    codec/XzCodec.cpp
    crypto/ArchiveCipher.cpp
    hash/Blake2b.cpp
    hash/Crc32.cpp)

add_library(Transmit::Format ALIAS transmit_format)

target_include_directories(transmit_format PUBLIC "${CMAKE_SOURCE_DIR}/src")

target_link_libraries(transmit_format
    PUBLIC  Transmit::Warnings
    PRIVATE Transmit::Zstd ZLIB::ZLIB)

if(TRANSMIT_LZMA_ENABLED)
    target_link_libraries(transmit_format PRIVATE LibLZMA::LibLZMA)
    target_compile_definitions(transmit_format PUBLIC TRANSMIT_HAVE_LZMA)
endif()

if(TRANSMIT_OPENSSL_ENABLED)
    target_link_libraries(transmit_format PRIVATE OpenSSL::Crypto)
    target_compile_definitions(transmit_format PUBLIC TRANSMIT_HAVE_OPENSSL)
endif()

target_compile_definitions(transmit_format PUBLIC
    TRANSMIT_VERSION="${PROJECT_VERSION}")
