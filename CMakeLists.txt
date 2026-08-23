cmake_minimum_required(VERSION 3.24)

project(Owear
    VERSION 0.1.0
    DESCRIPTION "Owear — native multiplatform desktop framework"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(APPLE)
    # CGDisplayCreateImage et al: disponibles hasta que el target sea ≥ 15
    set(CMAKE_OSX_DEPLOYMENT_TARGET
        "13.0" CACHE STRING "macOS deployment target" FORCE)
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(OW_BUILD_DEMO    "Build the demo host app"        ON)
option(OW_BUILD_MODULES "Build stock modules (fs, ...)"  ON)
option(OW_BUILD_TESTS   "Build unit tests"               ON)
option(OW_WARNINGS_AS_ERRORS "Treat warnings as errors"  OFF)
# OW_WITH_WEBVIEW2 se AUTO-DETECTA en src/CMakeLists.txt según exista
# deps/webview2/include/WebView2.h — override con -DOW_WITH_WEBVIEW2=ON/OFF.

# ── Platform detection (drives source selection; NO #ifdef feature gates) ──
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(OW_PLATFORM linux)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(OW_PLATFORM win)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(OW_PLATFORM mac)
else()
    message(FATAL_ERROR "Owear: unsupported platform ${CMAKE_SYSTEM_NAME}")
endif()
message(STATUS "Owear platform: ${OW_PLATFORM}")

if(APPLE)
    enable_language(OBJCXX) # los .mm del kernel (window/webview/control)
endif()

add_subdirectory(src)

# ── Todas las APIs (cada folder api/<nombre> = una API modular .owm) ─────────
add_subdirectory(api)

if(OW_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
