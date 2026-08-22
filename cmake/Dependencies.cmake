# Dependency resolution. System packages are preferred; FetchContent is the
# fallback so a fresh checkout configures without manual setup.

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# ---------------------------------------------------------------- zstd (required)
find_package(zstd CONFIG QUIET)
if(NOT TARGET zstd::libzstd_shared AND NOT TARGET zstd::libzstd_static)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(PC_ZSTD QUIET IMPORTED_TARGET libzstd)
    endif()
endif()

add_library(transmit_zstd INTERFACE)
add_library(Transmit::Zstd ALIAS transmit_zstd)

if(TARGET zstd::libzstd_shared)
    target_link_libraries(transmit_zstd INTERFACE zstd::libzstd_shared)
    set(TRANSMIT_ZSTD_SOURCE "system (CONFIG)")
elseif(TARGET zstd::libzstd_static)
    target_link_libraries(transmit_zstd INTERFACE zstd::libzstd_static)
    set(TRANSMIT_ZSTD_SOURCE "system (CONFIG, static)")
elseif(TARGET PkgConfig::PC_ZSTD)
    target_link_libraries(transmit_zstd INTERFACE PkgConfig::PC_ZSTD)
    set(TRANSMIT_ZSTD_SOURCE "system (pkg-config)")
else()
    FetchContent_Declare(zstd_upstream
        GIT_REPOSITORY https://github.com/facebook/zstd.git
        GIT_TAG        v1.5.6
        SOURCE_SUBDIR  build/cmake)
    set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_SHARED   OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_STATIC   ON  CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(zstd_upstream)
    target_link_libraries(transmit_zstd INTERFACE libzstd_static)
    set(TRANSMIT_ZSTD_SOURCE "FetchContent v1.5.6")
endif()

# ---------------------------------------------------------------- zlib (required)
# The always-available fallback codec, so an archive can still be produced on a
# machine where neither zstd nor xz could be resolved.
find_package(ZLIB REQUIRED)
set(TRANSMIT_ZLIB_SOURCE "system ${ZLIB_VERSION_STRING}")

# ---------------------------------------------------------------- liblzma (optional)
set(TRANSMIT_LZMA_ENABLED OFF)
if(TRANSMIT_WITH_LZMA)
    find_package(LibLZMA QUIET)
    if(LibLZMA_FOUND)
        set(TRANSMIT_LZMA_ENABLED ON)
        set(TRANSMIT_LZMA_SOURCE "system ${LIBLZMA_VERSION_STRING}")
    else()
        message(STATUS "liblzma not found - the Extreme (xz) preset will be unavailable")
        set(TRANSMIT_LZMA_SOURCE "not found")
    endif()
endif()

# ---------------------------------------------------------------- OpenSSL (optional)
set(TRANSMIT_OPENSSL_ENABLED OFF)
if(TRANSMIT_WITH_OPENSSL)
    find_package(OpenSSL 1.1 QUIET COMPONENTS Crypto)
    if(OpenSSL_FOUND)
        set(TRANSMIT_OPENSSL_ENABLED ON)
        set(TRANSMIT_OPENSSL_SOURCE "system ${OPENSSL_VERSION}")
    else()
        message(STATUS "OpenSSL not found - archive encryption will be unavailable")
        set(TRANSMIT_OPENSSL_SOURCE "not found")
    endif()
endif()

# ---------------------------------------------------------------- SQLite3 (required)
# Needed for the online-backup API (consistent capture of live app databases)
# and for rewriting paths stored inside application databases.
find_package(SQLite3 QUIET)
if(NOT SQLite3_FOUND)
    message(FATAL_ERROR
        "SQLite3 development files are required.\n"
        "  Debian/Ubuntu : apt install libsqlite3-dev\n"
        "  Fedora/RHEL   : dnf install sqlite-devel\n"
        "  Arch          : pacman -S sqlite\n"
        "  macOS         : brew install sqlite (or use the SDK copy)\n"
        "  Windows       : vcpkg install sqlite3, then pass -DCMAKE_TOOLCHAIN_FILE=...")
endif()
set(TRANSMIT_SQLITE_SOURCE "system ${SQLite3_VERSION}")

# ---------------------------------------------------------------- Qt 6 (app only)
if(TRANSMIT_BUILD_APP OR TRANSMIT_BUILD_TESTS)
    find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Concurrent Sql Network)
    if(TRANSMIT_BUILD_TESTS)
        find_package(Qt6 6.4 REQUIRED COMPONENTS Test)
    endif()
    qt_standard_project_setup(REQUIRES 6.4)
endif()

# ---------------------------------------------------------------- GoogleTest (tests only)
if(TRANSMIT_BUILD_TESTS)
    find_package(GTest QUIET)
    if(NOT GTest_FOUND)
        FetchContent_Declare(googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG        v1.15.2)
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    endif()
    include(GoogleTest)
endif()
