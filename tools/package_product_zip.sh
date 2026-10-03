#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=${BUILD_DIR:-"$root/build-product"}
stage=$build_dir/stage
fw=$root/psxrecomp

if [ -z "${ARTIFACT:-}" ]; then
    if [ "${OS:-}" = "Windows_NT" ]; then artifact=windows-x64; else artifact=linux-x64; fi
else
    artifact=$ARTIFACT
fi
version=${RELEASE_VERSION:-$(tr -d " \t\r\n" < "$root/VERSION")}
version=${version#v}
[ -n "$version" ] || { echo "VERSION is empty" >&2; exit 1; }

output=${OUTPUT:-"$root/dist/wild9-$version-$artifact.zip"}
bios_build=${PSXRECOMP_BIOS_BUILD:-build-recompiler}

. "$fw/tools/release_overlay_stage.sh"
psx_release_stage_init "$fw"

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

bios_root=$root/$bios_build
if ! psx_emitters_present "$bios_root"; then
    bios_root=$fw/$bios_build
fi
if ! psx_emitters_present "$bios_root"; then
    bios_root=$fw/recompiler/build
fi
if ! psx_emitters_present "$bios_root"; then
    cmake -S "$fw/recompiler" -B "$fw/recompiler/build" -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build "$fw/recompiler/build" --target psxrecomp-game psxrecomp-bios \
        -j "${BUILD_JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"
    bios_root=$fw/recompiler/build
fi

if [ -f "$fw/bios/openbios.bin" ] && [ ! -f "$fw/generated/OpenBIOS_dispatch.c" ]; then
    (cd "$fw" && PSXRECOMP_BIOS_BUILD="${bios_root#"$fw"/}" tools/regen_bios.sh \
        --config bios/OpenBIOS.toml)
fi

if [ "${SKIP_BUILD:-0}" != 1 ]; then
    extra=""
    if [ "${CCACHE:-0}" = 1 ] && command -v ccache >/dev/null 2>&1; then
        extra="-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache"
    fi
    # EXTRA_CMAKE_ARGS is intentionally word-split: callers pass whole
    # -Dfoo=bar tokens (e.g. the macOS deployment target).
    extra_args=${EXTRA_CMAKE_ARGS:-}
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
    if [ "${OS:-}" = "Windows_NT" ] || [ -n "${MSYSTEM:-}" ]; then
        unset PSXRECOMP_TOOLCHAIN_DIR TOOLCHAIN_DIR BPE_TOOLCHAIN_DIR RETCOMM_TOOLCHAIN_DIR || true
        unset CMAKE_PREFIX_PATH SDL3_DIR ZLIB_ROOT || true
    fi
    if cmake -S "$root" -B "$build_dir" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DPSX_DEBUG_TOOLS=OFF \
        -DPSX_SDL_BACKEND=SDL3 \
        -DWILD9_SETUP_WIZARD=OFF \
        -DPSXRECOMP_FORCE_SETUP_HOST=OFF \
        -DPSXRECOMP_REQUIRE_GAME_C=ON \
        $extra $extra_args $sdl_args > "$build_dir-configure.log" 2>&1; then
        cat "$build_dir-configure.log"
    else
        configure_status=$?
        cat "$build_dir-configure.log" >&2
        echo "packaging: cmake configure failed (exit $configure_status)." >&2
        exit "$configure_status"
    fi
else
    if [ ! -d "$build_dir" ]; then
        echo "packaging: SKIP_BUILD=1 but $build_dir does not exist." >&2
        exit 1
    fi
fi

cmake --build "$build_dir" --target psx-runtime \
    -j "${BUILD_JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"

. "$root/tools/product_build_verify.sh"

# MinGW names the target Wild9_Recompiled.exe; everything else drops the suffix.
# Check for .exe explicitly first so native tools receive the exact file path.
if [ -f "$build_dir/Wild9_Recompiled.exe" ] || [ "${OS:-}" = "Windows_NT" ]; then
    exe=$build_dir/Wild9_Recompiled.exe
else
    exe=$build_dir/Wild9_Recompiled
fi

psx_verify_product_build "$exe" \
                        "$build_dir-configure.log"

case "$stage" in
    "$build_dir"/*) ;;
    *) echo "Refusing unsafe stage path: $stage" >&2; exit 1 ;;
esac
rm -rf -- "$stage"
mkdir -p -- "$stage"

staged_exe=$stage/$(basename -- "$exe")
install -m 0755 "$exe" "$staged_exe"

cp -a "$build_dir/assets" "$stage/assets"
cp -a "$build_dir/bios" "$stage/bios"
mkdir -p "$stage/saves"
psx_add_mod_catalog --build-path "$build_dir" --stage "$stage" \
                    --runtime-target psx-runtime

mkdir -p "$stage/licenses"
notices=0
for notice in "$fw"/runtime/licenses/*-NOTICES.txt; do
    [ -f "$notice" ] || continue
    cp "$notice" "$stage/licenses/"
    notices=$((notices + 1))
done
if [ "$notices" -eq 0 ]; then
    echo "packaging: no third-party notices under $fw/runtime/licenses" >&2
    exit 1
fi
if [ -f "$build_dir/bios/OpenBIOS.LICENSE" ]; then
    cp "$build_dir/bios/OpenBIOS.LICENSE" "$stage/licenses/"
else
    echo "packaging: missing $build_dir/bios/OpenBIOS.LICENSE" >&2
    exit 1
fi

cp "$root/packaging/release/game.toml" "$stage/game.toml"
cp "$root/game_options.toml" "$stage/game_options.toml"
cp "$root/packaging/release/input.ini" "$stage/input.ini"
cp "$root/packaging/release/START_HERE.txt" "$stage/START_HERE.txt"
if [ "${OS:-}" = "Windows_NT" ]; then
    cp "$root/packaging/release/README-WINDOWS.txt" "$stage/README.txt"
else
    cp "$root/packaging/release/README-POSIX.txt" "$stage/README.txt"
fi
cp "$root/LICENSE" "$stage/LICENSE"

psx_product_verify_no_toolchain_payload "$stage"

if [ -n "${OS:-}" ] && [ "${OS:-}" = "Windows_NT" ]; then
    staged_dlls=""
    for dll in "$build_dir"/*.dll "$build_dir"/*.DLL; do
        [ -f "$dll" ] || continue
        cp -f "$dll" "$stage/"
        base=$(basename "$dll")
        staged_dlls="$staged_dlls $base"
        echo "  staged sibling DLL $base"
    done

    if command -v objdump >/dev/null 2>&1; then
        imports=$(objdump -p "$staged_exe" | awk '/DLL Name:/ {print $3}' | sort -u)
        missing=""
        count=0
        for dll in $imports; do
            [ -n "$dll" ] || continue
            count=$((count + 1))
            if [ -f "$stage/$dll" ]; then
                continue
            fi
            dll_lower=$(printf '%s' "$dll" | tr '[:upper:]' '[:lower:]')
            case "$dll_lower" in
                kernel32.dll|kernelbase.dll|user32.dll|gdi32.dll|gdiplus.dll|advapi32.dll|\
                shell32.dll|shcore.dll|ole32.dll|oleaut32.dll|oleacc.dll|ntdll.dll|ws2_32.dll|\
                msvcrt.dll|ucrtbase.dll|vcruntime140.dll|vcruntime140_1.dll|vcruntime*.dll|\
                msvcp140.dll|msvcp*.dll|api-ms-win-crt-*.dll|api-ms-win-core-*.dll|\
                api-ms-*.dll|ext-ms-*.dll|\
                comctl32.dll|comdlg32.dll|imm32.dll|setupapi.dll|crypt32.dll|\
                wintrust.dll|secur32.dll|bcrypt.dll|ncrypt.dll|shlwapi.dll|winmm.dll|\
                version.dll|mswsock.dll|dwmapi.dll|uxtheme.dll|hid.dll|\
                dbghelp.dll|psapi.dll|iphlpapi.dll|nsi.dll|dnsapi.dll|netapi32.dll|\
                userenv.dll|wtsapi32.dll|cabinet.dll|msimg32.dll|\
                opengl32.dll|glu32.dll|d2d1.dll|dwrite.dll|dcomp.dll|dxgi.dll|\
                d3d*.dll|d3dcompiler_*.dll|windowscodecs.dll|propsys.dll|\
                rpcrt4.dll|dinput8.dll|xinput*.dll|cfgmgr32.dll|powrprof.dll|\
                avrt.dll|mf*.dll|audioses.dll)
                    ;;
                *) missing="$missing $dll" ;;
            esac
        done
        if [ -n "$missing" ]; then
            echo "packaging: exe imports DLL(s) that are neither Windows built-ins" >&2
            echo "  nor shipped in this zip. Players would get ERROR 0xc000007b:" >&2
            for dll in $missing; do echo "    $dll" >&2; done
            exit 1
        fi
        echo "  self-containment ok (${count} imported DLLs, all system or staged${staged_dlls:+; staged:$staged_dlls})"
    else
        echo "packaging: objdump not found; cannot verify DLL self-containment" >&2
        echo "  Install mingw-w64-x86_64-binutils, or the zip may not run on a" >&2
        echo "  clean machine." >&2
        exit 1
    fi

    sign_sh=$fw/tools/ci/sign_windows.sh
    if [ -f "$sign_sh" ]; then
        bash "$sign_sh" "$stage"
    else
        echo "packaging: $sign_sh missing; Windows binaries ship unsigned" >&2
    fi
fi

rm -f -- "$output"
mkdir -p -- "$(dirname -- "$output")"
if command -v zip >/dev/null 2>&1; then
    find "$stage" -exec touch -c {} + 2>/dev/null || find "$stage" -exec touch {} +
    (cd "$stage" && zip -r -q -9 "$output" .)
    chmod 0644 "$output"
else
    python3 "$fw/tools/create_release_zip.py" --source "$stage" --output "$output"
    chmod 0644 "$output"
fi

if command -v sha256sum >/dev/null 2>&1; then
    (cd "$(dirname -- "$output")" && sha256sum "$(basename -- "$output")") >"$output.sha256"
else
    # macOS has shasum, not the GNU coreutils sha256sum.
    (cd "$(dirname -- "$output")" && shasum -a 256 "$(basename -- "$output")") >"$output.sha256"
fi
cat "$output.sha256"
du -h "$output"
