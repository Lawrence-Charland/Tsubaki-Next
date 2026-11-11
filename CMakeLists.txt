cmake_minimum_required(VERSION 3.10...3.28)

project(Tsubaki-Next VERSION 1.1.0 LANGUAGES CXX)

include(GNUInstallDirs)

find_package(OpenSSL REQUIRED)

# The parser is a separate project, fetched at configure time. SOURCE_SUBDIR
# names a directory that does not exist, so the library is populated but not
# added as a subproject; its source is compiled into tsubaki directly.
include(FetchContent)
FetchContent_Declare(
    LC-ArgParser
    GIT_REPOSITORY https://github.com/Lawrence-Charland/LC-ArgParser.git
    GIT_TAG 945d2b0c6adff8fdafc9f54023e5d5ec7f2bf43e
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
    src/help.cpp
    ${lc-argparser_SOURCE_DIR}/src/argparser.cpp
    src/worker.cpp
    src/cached_taskstream.cpp
    src/tsubaki_core.cpp)

target_compile_definitions(tsubaki PRIVATE TSUBAKI_VERSION="${PROJECT_VERSION}")

set_target_properties(tsubaki PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

target_include_directories(tsubaki PRIVATE ${CMAKE_SOURCE_DIR}/include)

target_include_directories(tsubaki PRIVATE ${lc-argparser_SOURCE_DIR}/include)

target_link_libraries(tsubaki PRIVATE OpenSSL::Crypto)

install(TARGETS tsubaki RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES COPYING DESTINATION ${CMAKE_INSTALL_DOCDIR})
