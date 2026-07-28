cmake_minimum_required(VERSION 3.25)
project(SDR9700 VERSION 26.07.01 LANGUAGES C CXX)

include(CTest)

set(SDR9700_DISPLAY_VERSION "26.07.01")

if(APPLE)
    if(NOT CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
        message(FATAL_ERROR "SDR9700 supports macOS on Apple Silicon only.")
    endif()

    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "macOS target architecture" FORCE)
    endif()
    if(NOT CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
        message(FATAL_ERROR "SDR9700 macOS builds must use CMAKE_OSX_ARCHITECTURES=arm64.")
    endif()
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(SDR9700_ALLOWED_BUILD_TYPES Release Debug)
if(NOT CMAKE_BUILD_TYPE IN_LIST SDR9700_ALLOWED_BUILD_TYPES)
    message(FATAL_ERROR "Unsupported CMAKE_BUILD_TYPE '${CMAKE_BUILD_TYPE}'. Use Release or Debug.")
endif()

set(SDR9700_DEBUG_BUILD 0)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(SDR9700_DEBUG_BUILD 1)
endif()

find_package(Qt6 6.2 REQUIRED COMPONENTS
    Core
    Widgets
    Network
    Multimedia
    MultimediaWidgets
)
if(BUILD_TESTING)
    find_package(Qt6 6.2 REQUIRED COMPONENTS Test)
endif()

add_subdirectory(src)
