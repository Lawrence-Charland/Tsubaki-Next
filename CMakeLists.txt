# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2026 Lawrence Charland

cmake_minimum_required(VERSION 3.10...3.28)

project(Tsubaki-Next VERSION 2.2.0 LANGUAGES CXX)

include(GNUInstallDirs)

# The sources rely on POSIX headers: dlfcn.h (dynamic providers), unistd.h and
# fcntl.h (file reads), pthread.h (worker threads), sys/ioctl.h (terminal
# width). Linux, macOS, the BSDs, illumos, Solaris and Cygwin all provide
# those; native Windows does not, and is not supported.
if(WIN32 AND NOT CYGWIN)
    message(FATAL_ERROR
        "Tsubaki-Next targets POSIX systems only (dlfcn.h, unistd.h, "
        "pthread.h, sys/ioctl.h). Native Windows is not supported; build "
        "under Cygwin or WSL instead.")
endif()

# Homebrew keeps OpenSSL outside the default search path on macOS.
if(APPLE AND NOT DEFINED OPENSSL_ROOT_DIR)
    foreach(_keg "/opt/homebrew/opt/openssl@3" "/usr/local/opt/openssl@3")
        if(EXISTS "${_keg}/include/openssl/evp.h")
            set(OPENSSL_ROOT_DIR "${_keg}")
            break()
        endif()
    endforeach()
endif()

# <openssl/core_names.h> and EVP_MAC are used unconditionally, so headers and
# library must both be 3.0 or newer.
find_package(OpenSSL 3.0 REQUIRED)

# FindOpenSSL reports LibreSSL's own version number, which also starts with 3,
# but LibreSSL has no EVP_MAC and no core_names.h.
if(OPENSSL_VERSION MATCHES "LibreSSL")
    message(FATAL_ERROR
        "LibreSSL (${OPENSSL_VERSION}) provides no EVP_MAC; install OpenSSL "
        "3.0 or newer and point OPENSSL_ROOT_DIR at it.")
endif()

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

message(STATUS "Tsubaki-Next: building for ${CMAKE_SYSTEM_NAME} "
               "(${CMAKE_SYSTEM_PROCESSOR}, ${CMAKE_CXX_COMPILER_ID})")

# The LC-ArgParser is a separate project, fetched at configure time. SOURCE_SUBDIR
# names a directory that does not exist, so the library is populated but not
# added as a subproject; its source is compiled into tsubaki directly.
include(FetchContent)
FetchContent_Declare(
    LC-ArgParser
    GIT_REPOSITORY https://github.com/Lawrence-Charland/LC-ArgParser.git
    GIT_TAG d4917987e8b22da811fa09ecbc5b4f20a1a081e4
    SOURCE_SUBDIR _build-from-source)
FetchContent_MakeAvailable(LC-ArgParser)

# exe: tsubaki

add_executable(tsubaki
    src/main.cpp
    src/dirscanner.cpp
    src/listscanner.cpp
    src/utilities.cpp
    src/basic_taskstream.cpp
    src/fanin_taskstream.cpp
    src/checksum.cpp
    src/checksum_api.cpp
    src/help.cpp
    src/worker.cpp
    src/cached_taskstream.cpp
    src/sum_core.cpp
    ${lc-argparser_SOURCE_DIR}/src/argparser.cpp
    ${lc-argparser_SOURCE_DIR}/src/ap_script.cpp
    src/verify_core.cpp
    src/cmp_core.cpp
    src/dpframework.cpp
    src/algo_info_core.cpp)

    # ---- build provenance for `tsubaki -V` ----
set(TSUBAKI_GIT_COMMIT "unknown")
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE _tsubaki_sha
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=no
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE _tsubaki_dirty
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(NOT "${_tsubaki_sha}" STREQUAL "")
        set(TSUBAKI_GIT_COMMIT "${_tsubaki_sha}")
        if(NOT "${_tsubaki_dirty}" STREQUAL "")
            string(APPEND TSUBAKI_GIT_COMMIT "-dirty")
        endif()
    endif()
endif()

# `tsubaki -V` reports the build time in Beijing time (UTC+8), whatever the
# build host's zone. Ask date for the offset too, so a host without
# Asia/Shanghai tzdata falls back to UTC instead of mislabelling local time.
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env TZ=Asia/Shanghai LC_ALL=C
            date "+%Y-%m-%dT%H:%M:%S%z"
    OUTPUT_VARIABLE TSUBAKI_BUILD_TIMESTAMP
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE _tsubaki_date_rc)
if(NOT _tsubaki_date_rc EQUAL 0 OR
   NOT TSUBAKI_BUILD_TIMESTAMP MATCHES "\\+0800$")
    string(TIMESTAMP TSUBAKI_BUILD_TIMESTAMP "%Y-%m-%dT%H:%M:%SZ" UTC)
endif()

target_compile_definitions(tsubaki PRIVATE
    TSUBAKI_VERSION="${PROJECT_VERSION}"
    TSUBAKI_GIT_COMMIT="${TSUBAKI_GIT_COMMIT}"
    TSUBAKI_BUILD_TIMESTAMP="${TSUBAKI_BUILD_TIMESTAMP}")

set_target_properties(tsubaki PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

target_include_directories(tsubaki PRIVATE ${CMAKE_SOURCE_DIR}/include)

target_include_directories(tsubaki PRIVATE ${lc-argparser_SOURCE_DIR}/include)

# ${CMAKE_DL_LIBS} is empty where dlopen lives in libc (macOS, the BSDs) and
# resolves to dl where it is a separate library (glibc, illumos, Cygwin).
target_link_libraries(tsubaki PRIVATE
    OpenSSL::Crypto
    Threads::Threads
    ${CMAKE_DL_LIBS})

install(TARGETS tsubaki RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
