#!/usr/bin/env bash
# Builds the static Skia (Ganesh/GL + paragraph + SVG + PDF) that drav links against.
# Uses system (/usr) or conda ($CONDA_PREFIX) libraries for freetype, harfbuzz, ICU, libpng, libjpeg, libwebp, expat, zlib.
#
#   scripts/build-skia.sh [skia-dir]        (default: third_party/skia)
#
# Then configure drav with -DSKIA_DIR=<skia-dir>.
set -euo pipefail

SKIA_DIR="${1:-$(dirname "$0")/../third_party/skia}"
SKIA_BRANCH="${SKIA_BRANCH:-chrome/m138}"
PREFIX="${CONDA_PREFIX:-/usr}"

if [ ! -d "$SKIA_DIR/.git" ]; then
    git clone --depth 1 -b "$SKIA_BRANCH" https://github.com/google/skia.git "$SKIA_DIR"
fi
cd "$SKIA_DIR"

if ! command -v gn >/dev/null; then
    python3 bin/fetch-gn
    PATH="$PWD/bin:$PATH"
fi
command -v ninja >/dev/null || { echo "ninja not found"; exit 1; }

CC="${CC:-cc}"
CXX="${CXX:-c++}"
GN_CFLAGS=""
for f in $(pkg-config --cflags freetype2 harfbuzz icu-uc fontconfig); do
    [ -d "${f#-I}" ] && GN_CFLAGS+="\"$f\", "
done

# -I/usr/include breaks GCC's #include_next, so only add the prefix when it is not the system one.
PREFIX_CFLAGS=""
PREFIX_LDFLAGS=""
if [ "$PREFIX" != "/usr" ]; then
    PREFIX_CFLAGS="\"-I$PREFIX/include\", "
    PREFIX_LDFLAGS="\"-L$PREFIX/lib\""
fi

mkdir -p out/static
cat > out/static/args.gn <<ARGS
is_official_build = true
is_debug = false
is_component_build = false
cc = "$CC"
cxx = "$CXX"
extra_cflags = [ ${PREFIX_CFLAGS}${GN_CFLAGS}"-Wno-psabi" ]
extra_ldflags = [ ${PREFIX_LDFLAGS} ]
skia_use_gl = true
skia_use_egl = false
skia_use_x11 = false
skia_use_vulkan = false
skia_enable_graphite = false
skia_enable_ganesh = true
skia_use_fontconfig = true
skia_use_freetype = true
skia_use_system_freetype2 = true
skia_use_system_expat = true
skia_use_system_zlib = true
skia_use_system_libpng = true
skia_use_system_libjpeg_turbo = true
skia_use_system_libwebp = true
skia_use_system_harfbuzz = true
skia_use_system_icu = true
skia_use_icu = true
skia_use_harfbuzz = true
skia_enable_skshaper = true
skia_enable_skunicode = true
skia_enable_skparagraph = true
skia_enable_svg = true
skia_enable_pdf = true
skia_use_wuffs = false
skia_use_libheif = false
skia_use_dng_sdk = false
skia_use_piex = false
skia_use_libjxl_decode = false
skia_use_libavif = false
skia_use_libgrapheme = false
skia_use_icu4x = false
skia_enable_tools = false
skia_use_expat = true
ARGS

gn gen out/static
ninja -C out/static skia modules/svg modules/skparagraph modules/skshaper modules/skunicode
echo "Skia built in $(pwd)/out/static"
