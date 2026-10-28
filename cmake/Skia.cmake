# Locates a static Skia build (headers = Skia source root, libraries = gn output dir).
# Configure with -DSKIA_DIR=<skia checkout> [-DSKIA_LIB_DIR=<skia>/out/<config>].
# scripts/build-skia.sh produces a compatible build.
set(SKIA_DIR "$ENV{SKIA_DIR}" CACHE PATH "Skia source root")
set(SKIA_LIB_DIR "${SKIA_DIR}/out/static" CACHE PATH "Skia gn output directory")

if(NOT EXISTS "${SKIA_DIR}/include/core/SkCanvas.h")
    message(FATAL_ERROR "Skia not found. Set SKIA_DIR (see scripts/build-skia.sh).")
endif()

find_package(PkgConfig REQUIRED)
find_package(OpenGL REQUIRED)
pkg_check_modules(SKIA_DEPS REQUIRED
    fontconfig freetype2 harfbuzz harfbuzz-subset icu-uc libpng libjpeg libwebp libwebpdemux libwebpmux expat zlib)

add_library(Skia::Skia INTERFACE IMPORTED GLOBAL)
target_include_directories(Skia::Skia INTERFACE "${SKIA_DIR}")
target_compile_definitions(Skia::Skia INTERFACE
    SK_GANESH SK_GL SK_ENABLE_PARAGRAPH SK_SHAPER_HARFBUZZ_AVAILABLE SK_SHAPER_UNICODE_AVAILABLE
    SK_UNICODE_ICU_IMPLEMENTATION SK_ENABLE_SVG SK_R32_SHIFT=16)
# Order matters for static linking: dependents first.
target_link_libraries(Skia::Skia INTERFACE
    "${SKIA_LIB_DIR}/libskparagraph.a"
    "${SKIA_LIB_DIR}/libsvg.a"
    "${SKIA_LIB_DIR}/libskshaper.a"
    "${SKIA_LIB_DIR}/libskunicode_icu.a"
    "${SKIA_LIB_DIR}/libskunicode_core.a"
    "${SKIA_LIB_DIR}/libskia.a"
    ${SKIA_DEPS_LINK_LIBRARIES} OpenGL::GL ${CMAKE_DL_LIBS})
# Some .pc files list include dirs that do not exist (e.g. glib via harfbuzz); keep only real ones.
foreach(dir IN LISTS SKIA_DEPS_INCLUDE_DIRS)
    if(EXISTS "${dir}")
        target_include_directories(Skia::Skia SYSTEM INTERFACE "${dir}")
    endif()
endforeach()
