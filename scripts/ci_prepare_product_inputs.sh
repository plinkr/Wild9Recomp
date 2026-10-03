#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
recompiler_build=${1:-build-recompiler}
fw=$root/psxrecomp

if [ -f "$root/generated/SLUS_004.25_dispatch.c" ]; then
    echo "  generated/ already present ($(find "$root/generated" -name '*.c' | wc -l | tr -d ' ') C files)"
    exit 0
fi

set +e
python3 "$root/scripts/fetch_generated_bundle.py"
bundle_status=$?
set -e
if [ "$bundle_status" -eq 2 ]; then
    echo "ci_prepare_product_inputs: no generated/ bundle available." >&2
    echo "  A product build cannot proceed without the recompiled game C." >&2
    exit 2
elif [ "$bundle_status" -ne 0 ]; then
    echo "ci_prepare_product_inputs: the bundle failed verification." >&2
    exit 1
fi

psx_emitter_present() {
    [ -x "$1/psxrecomp-bios" ] || [ -x "$1/psxrecomp-bios.exe" ]
}

case "$recompiler_build" in
    /*) bios_build=$recompiler_build ;;
    *) bios_build=$root/$recompiler_build ;;
esac

if [ -f "$fw/bios/openbios.bin" ] && [ ! -f "$fw/generated/OpenBIOS_dispatch.c" ]; then
    echo "  regenerating the OpenBIOS backend"
    if psx_emitter_present "$bios_build"; then
        (cd "$fw" && PSXRECOMP_BIOS_BUILD="$bios_build" tools/regen_bios.sh \
            --config bios/OpenBIOS.toml)
    else
        echo "  no psxrecomp-bios in $bios_build; building the recompiler"
        cmake -S "$fw/recompiler" -B "$fw/recompiler/build" -G Ninja \
            -DCMAKE_BUILD_TYPE=Release
        cmake --build "$fw/recompiler/build" --target psxrecomp-bios \
            -j "${BUILD_JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"
        (cd "$fw" && tools/regen_bios.sh --config bios/OpenBIOS.toml)
    fi
fi

if [ ! -f "$root/generated/SLUS_004.25_dispatch.c" ]; then
    echo "ci_prepare_product_inputs: generated/ is in place but the marker is not." >&2
    echo "  expected generated/SLUS_004.25_dispatch.c (see GEN_MARKER in CMakeLists.txt)" >&2
    exit 1
fi
echo "  generated/ ready"
