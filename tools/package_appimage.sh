#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=${BUILD_DIR:-"$root/build-appimage"}
appdir=$build_dir/AppDir
version=${RELEASE_VERSION:-$(tr -d " \t\r\n" < "$root/VERSION")}
[ -n "$version" ] || { echo "VERSION is empty" >&2; exit 1; }
version=${version#v}
output=${OUTPUT:-"$root/dist/wild9-$version-linux-x86_64.AppImage"}
tools_dir=$build_dir/appimage-tools
fw=$root/psxrecomp
payload_name=wild9recomp

. "$fw/tools/release_overlay_stage.sh"
psx_release_stage_init "$fw"

linuxdeploy_url=https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
linuxdeploy_sha=36a2d7e274d12e1050d0e9ecfe11d339ed54720b2bec464c286d53f8b07f5c62
appimagetool_url=https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
appimagetool_sha=a6d71e2b6cd66f8e8d16c37ad164658985e0cf5fcaa950c90a482890cb9d13e0

if [ ! -f "$root/generated/SLUS_004.25_dispatch.c" ]; then
    echo "Missing generated game sources. Regenerate them first:" >&2
    echo "  python3 psxrecomp/psxrecomp_cli.py generate \\" >&2
    echo "      --config game.toml --project-root . --disc '<your Wild 9 .cue>'" >&2
    exit 1
fi

psx_emitters_present() {
    for _pfx_emitter in psxrecomp-game psxrecomp-bios; do
        if [ ! -x "$1/$_pfx_emitter" ] && [ ! -x "$1/$_pfx_emitter.exe" ]; then
            return 1
        fi
    done
    return 0
}

bios_build=${PSXRECOMP_BIOS_BUILD:-../build-recompiler}
if ! psx_emitters_present "$fw/$bios_build"; then
    bios_build=recompiler/build
fi
if ! psx_emitters_present "$fw/$bios_build"; then
    cmake -S "$fw/recompiler" -B "$fw/$bios_build" -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build "$fw/$bios_build" --target psxrecomp-game psxrecomp-bios -j "${BUILD_JOBS:-$(getconf _NPROCESSORS_ONLN)}"
fi

if [ -f "$fw/bios/openbios.bin" ] && [ ! -f "$fw/generated/OpenBIOS_dispatch.c" ]; then
    (cd "$fw" && PSXRECOMP_BIOS_BUILD="$bios_build" tools/regen_bios.sh --config bios/OpenBIOS.toml)
fi

if [ "${SKIP_RUNTIME_BUILD:-0}" != 1 ]; then
    sdl_args=""
    if [ -n "${PSX_SDL3_SOURCE_DIR:-}" ] && [ -f "${PSX_SDL3_SOURCE_DIR}/CMakeLists.txt" ]; then
        sdl_dir=${PSX_SDL3_SOURCE_DIR}
        if command -v cygpath >/dev/null 2>&1; then
            sdl_dir=$(cygpath -m "${sdl_dir}")
        elif [ -d "${sdl_dir}" ]; then
            sdl_dir=$(cd "${sdl_dir}" && pwd)
        fi
        sdl_args="-DFETCHCONTENT_SOURCE_DIR_SDL3=${sdl_dir}"
        echo "using prebuilt SDL3 source at ${sdl_dir}"
    fi
    if cmake -S "$root" -B "$build_dir" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER_LAUNCHER= \
        -DCMAKE_CXX_COMPILER_LAUNCHER= \
        -DPSX_DEBUG_TOOLS=OFF \
        -DPSX_SDL_BACKEND=SDL3 \
        -DWILD9_SETUP_WIZARD=OFF \
        -DPSXRECOMP_FORCE_SETUP_HOST=OFF \
        -DPSXRECOMP_REQUIRE_GAME_C=ON \
        $sdl_args \
        "${CMAKE_EXTRA_ARGS:-}" > "$build_dir-configure.log" 2>&1; then
        cat "$build_dir-configure.log"
    else
        configure_status=$?
        cat "$build_dir-configure.log" >&2
        echo "packaging: cmake configure failed (exit $configure_status)." >&2
        exit "$configure_status"
    fi
else
    if [ ! -d "$build_dir" ]; then
        echo "packaging: SKIP_RUNTIME_BUILD=1 but $build_dir does not exist." >&2
        exit 1
    fi
fi

cmake --build "$build_dir" --target psx-runtime -j "${BUILD_JOBS:-$(getconf _NPROCESSORS_ONLN)}"

. "$root/tools/product_build_verify.sh"
exe=$build_dir/Wild9_Recompiled
psx_verify_product_build "$exe" "$build_dir-configure.log"

case "$appdir" in
    "$build_dir"/*) ;;
    *) echo "Refusing unsafe AppDir path: $appdir" >&2; exit 1 ;;
esac
rm -rf -- "$appdir"
mkdir -p "$appdir/usr/bin" "$appdir/usr/share/$payload_name"

install -m 0755 "$exe" "$appdir/usr/bin/Wild9_Recompiled"
install -m 0755 "$root/packaging/linux/AppRun" "$appdir/AppRun"
install -m 0644 "$root/packaging/linux/io.github.plinkr.Wild9Recomp.desktop" \
    "$appdir/io.github.plinkr.Wild9Recomp.desktop"

payload=$appdir/usr/share/$payload_name
cp -a "$build_dir/assets" "$payload/assets"
cp -a "$build_dir/bios" "$payload/bios"
psx_add_mod_catalog --build-path "$build_dir" --stage "$payload" \
                    --runtime-target psx-runtime
mkdir -p "$payload/licenses"
notices=0
for notice in "$fw"/runtime/licenses/*-NOTICES.txt; do
    [ -f "$notice" ] || continue
    cp "$notice" "$payload/licenses/"
    notices=$((notices + 1))
done
if [ "$notices" -eq 0 ]; then
    echo "packaging: no third-party notices under $fw/runtime/licenses" >&2
    exit 1
fi
if [ ! -f "$build_dir/bios/OpenBIOS.LICENSE" ]; then
    echo "packaging: missing $build_dir/bios/OpenBIOS.LICENSE" >&2
    exit 1
fi
cp "$build_dir/bios/OpenBIOS.LICENSE" "$payload/licenses/"
cp "$root/packaging/release/game.toml" "$payload/game.toml"
cp "$root/game_options.toml" "$payload/game_options.toml"
cp "$root/packaging/release/input.ini" "$payload/input.ini"
cp "$root/packaging/release/START_HERE.txt" "$payload/START_HERE.txt"
cp "$root/packaging/linux/README.md" "$payload/APPIMAGE_README.md"
cp "$root/LICENSE" "$root/README.md" "$payload/"

ln -s "../share/$payload_name/assets" "$appdir/usr/bin/assets"

psx_product_verify_no_toolchain_payload "$payload"

if command -v magick >/dev/null 2>&1; then
    image_tool=magick
elif command -v convert >/dev/null 2>&1; then
    image_tool=convert
else
    echo "ImageMagick is required to create the AppImage icon." >&2
    exit 1
fi
"$image_tool" "$root/launcher_assets/img/boxart.tga" \
    -resize 240x240 -background transparent -gravity center -extent 256x256 \
    "$appdir/io.github.plinkr.Wild9Recomp.png"
ln -s io.github.plinkr.Wild9Recomp.png "$appdir/.DirIcon"

mkdir -p "$tools_dir"
fetch_tool() {
    url=$1
    sha=$2
    dest=$3
    if [ ! -f "$dest" ] || \
       [ "$(sha256sum "$dest" | awk '{print $1}')" != "$sha" ]; then
        curl -fL --retry 3 "$url" -o "$dest.tmp"
        printf '%s  %s\n' "$sha" "$dest.tmp" | sha256sum -c -
        mv "$dest.tmp" "$dest"
    fi
    chmod 0755 "$dest"
}

linuxdeploy=$tools_dir/linuxdeploy-x86_64.AppImage
appimagetool=$tools_dir/appimagetool-x86_64.AppImage
fetch_tool "$linuxdeploy_url" "$linuxdeploy_sha" "$linuxdeploy"
fetch_tool "$appimagetool_url" "$appimagetool_sha" "$appimagetool"

export NO_STRIP=1
"$linuxdeploy" --appimage-extract-and-run \
    --appdir "$appdir" \
    --executable "$appdir/usr/bin/Wild9_Recompiled" \
    --desktop-file "$appdir/io.github.plinkr.Wild9Recomp.desktop" \
    --icon-file "$appdir/io.github.plinkr.Wild9Recomp.png"

if [ -d "$appdir/usr/lib" ]; then
    keep=$build_dir/appdir-keep.txt
    env -u LD_LIBRARY_PATH ldd "$appdir/usr/bin/Wild9_Recompiled" \
        | awk '{ for (i = 1; i <= NF; i++) if ($i ~ /^\//) print $i }' \
        | while read -r p; do readlink -f "$p" 2>/dev/null || true; done \
        | sort -u > "$keep"
    pruned=0
    for f in "$appdir"/usr/lib/*; do
        [ -e "$f" ] || continue
        real=$(readlink -f "$f")
        if ! grep -qxF "$real" "$keep"; then
            echo "  prune unreachable bundled lib: $(basename "$f")"
            rm -f "$f"
            pruned=$((pruned + 1))
        fi
    done
    echo "  pruned $pruned unreachable libraries from usr/lib"
    if env -u LD_LIBRARY_PATH ldd "$appdir/usr/bin/Wild9_Recompiled" \
            | grep -q "not found"; then
        echo "binary has unresolved libraries without LD_LIBRARY_PATH" >&2
        env -u LD_LIBRARY_PATH ldd "$appdir/usr/bin/Wild9_Recompiled" \
            | grep "not found" >&2
        exit 1
    fi
fi

rm -f -- "$output"
mkdir -p -- "$(dirname -- "$output")"
ARCH=x86_64 "$appimagetool" --appimage-extract-and-run "$appdir" "$output"
chmod 0755 "$output"

(cd "$(dirname -- "$output")" && sha256sum "$(basename -- "$output")") > "$output.sha256"
cat "$output.sha256"
