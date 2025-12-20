cmake_minimum_required(VERSION 3.19)
project(MegaCustomApp VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set default build type if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Options
option(BUILD_TESTS "Build test suite" ON)
option(BUILD_GUI "Build GUI interface (requires Qt6)" OFF)
option(USE_SYSTEM_MEGA "Use system-installed Mega SDK" OFF)

# Find required packages
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)
find_package(ZLIB REQUIRED)

# VCPKG integration (if available)
if(DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "Vcpkg toolchain file")
endif()

# Find or build Mega SDK
if(USE_SYSTEM_MEGA)
    find_package(mega REQUIRED)
else()
    # We'll build Mega SDK as part of this project
    set(MEGA_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sdk")
    if(NOT EXISTS ${MEGA_SDK_DIR})
        message(STATUS "Mega SDK not found. Please clone it to third_party/sdk")
    endif()
endif()

# PCRE2 for regex support (optional for initial build)
find_package(PCRE2 QUIET)
if(NOT PCRE2_FOUND)
    message(WARNING "PCRE2 not found. Regex features will be disabled.")
endif()

# JSON library for configuration (optional for initial build)
find_package(nlohmann_json 3.2.0 QUIET)
if(NOT nlohmann_json_FOUND)
    message(WARNING "nlohmann_json not found. JSON configuration will be disabled.")
endif()

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sdk/include
    ${CMAKE_CURRENT_BINARY_DIR}
)

# Collect source files (check if they exist first)
file(GLOB CORE_SOURCES src/core/*.cpp)
file(GLOB OPERATIONS_SOURCES src/operations/*.cpp)
file(GLOB FEATURES_SOURCES src/features/*.cpp)
file(GLOB UTILS_SOURCES src/utils/*.cpp)
file(GLOB UI_SOURCES src/ui/*.cpp)

# Combine all sources
set(ALL_LIB_SOURCES
    ${CORE_SOURCES}
    ${OPERATIONS_SOURCES}
    ${FEATURES_SOURCES}
    ${UTILS_SOURCES}
)

# Only create library if we have source files
if(ALL_LIB_SOURCES)
    add_library(mega_custom_core STATIC ${ALL_LIB_SOURCES})
else()
    # Create a dummy library with just headers for now
    add_library(mega_custom_core INTERFACE)
    target_include_directories(mega_custom_core INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
endif()

# Only link libraries if it's not an INTERFACE library
if(ALL_LIB_SOURCES)
    target_link_libraries(mega_custom_core
        ${CMAKE_THREAD_LIBS_INIT}
        OpenSSL::SSL
        OpenSSL::Crypto
        CURL::libcurl
        ZLIB::ZLIB
    )

    if(nlohmann_json_FOUND)
        target_link_libraries(mega_custom_core nlohmann_json::nlohmann_json)
    endif()

    if(PCRE2_FOUND)
        target_link_libraries(mega_custom_core pcre2-8)
    endif()
endif()

# Main executable (CLI)
add_executable(megacustom
    src/main.cpp
    ${UI_SOURCES}
)

# Link appropriate libraries
if(ALL_LIB_SOURCES)
    target_link_libraries(megacustom
        mega_custom_core
    )
else()
    # Link libraries directly if no core library
    target_link_libraries(megacustom
        ${CMAKE_THREAD_LIBS_INIT}
        OpenSSL::SSL
        OpenSSL::Crypto
        CURL::libcurl
        ZLIB::ZLIB
    )

    if(nlohmann_json_FOUND)
        target_link_libraries(megacustom nlohmann_json::nlohmann_json)
    endif()

    if(PCRE2_FOUND)
        target_link_libraries(megacustom pcre2-8)
    endif()

    target_include_directories(megacustom PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
endif()

# GUI executable (optional)
if(BUILD_GUI)
    find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)
    qt_standard_project_setup()

    file(GLOB_RECURSE GUI_SOURCES
        src/gui/*.cpp
    )

    qt_add_executable(megacustom-gui
        ${GUI_SOURCES}
    )

    target_link_libraries(megacustom-gui
        mega_custom_core
        Qt6::Core
        Qt6::Widgets
        Qt6::Network
    )
endif()

# Tests
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Installation
install(TARGETS megacustom
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/
    DESTINATION include/megacustom
)

install(DIRECTORY config/
    DESTINATION share/megacustom/config
)

install(DIRECTORY examples/
    DESTINATION share/megacustom/examples
)

# Package configuration
include(CPack)
set(CPACK_PACKAGE_NAME "MegaCustomApp")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Advanced Mega.nz SDK Application")
set(CPACK_PACKAGE_VENDOR "Custom")