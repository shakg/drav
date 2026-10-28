cmake_minimum_required(VERSION 3.24)
project(drav VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Skia headers use `foreach` as an identifier.
add_compile_definitions(QT_NO_FOREACH)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(DRAV_BUILD_TESTS "Build unit tests" ON)
option(DRAV_PACKAGE "Bundle Qt into the install tree and configure CPack for a .deb" OFF)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 OpenGL Test)
include(cmake/Skia.cmake)

qt_standard_project_setup(REQUIRES 6.8)

add_subdirectory(src/core)
add_subdirectory(src/render)
add_subdirectory(src/app)
include(cmake/Packaging.cmake)

# Packages: `cmake --build build --target package` (or `cpack` in the build directory).
set(CPACK_PACKAGE_NAME drav)
set(CPACK_PACKAGE_VENDOR drav)
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Offline vector and UI design editor")
set(CPACK_PACKAGE_INSTALL_DIRECTORY drav)
set(CPACK_PACKAGE_EXECUTABLES drav drav)
set(CPACK_STRIP_FILES ON)
if(APPLE)
    set(CPACK_GENERATOR DragNDrop)
    set(CPACK_DMG_VOLUME_NAME drav)
elseif(WIN32)
    set(CPACK_GENERATOR ZIP)
else()
    set(CPACK_GENERATOR TGZ)
endif()
include(CPack)

if(DRAV_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
