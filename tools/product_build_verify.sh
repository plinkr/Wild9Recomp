#!/bin/sh
set -eu

psx_product_verify_symbol() {
    _ps_exe=$1
    _ps_syms=$2
    _ps_class=$3
    _ps_what=$4
    shift 4
    for _ps_sym in "$@"; do
        if ! grep -qE "[${_ps_class}] ${_ps_sym}\$" "$_ps_syms"; then
            echo "  FAIL: ${_ps_exe} does not define ${_ps_sym}." >&2
            echo "        Expected evidence: ${_ps_what}" >&2
            return 1
        fi
    done
}

psx_product_verify_game_code() {
    _pg_exe=$1
    _pg_syms=$2
    psx_product_verify_symbol "$_pg_exe" "$_pg_syms" 'TtWw' \
        'the recompiled game code is linked in. Without it the launcher opens' \
        func_80010000 func_8005F7C8 || return 1
    echo "  game code verified: $(grep -cE '[TtWw] func_80' "$_pg_syms") linked game functions"
}

psx_product_verify_bios_backend() {
    psx_product_verify_symbol "$1" "$2" 'A-Za-z' \
        'a recompiled BIOS backend is registered. The packaged config ships' \
        OpenBIOS_psx_bios_backend
}

psx_product_verify_absent() {
    _pa_exe=$1
    shift
    for _pa_marker in "$@"; do
        if grep -qa "$_pa_marker" "$_pa_exe"; then
            echo "  FAIL: ${_pa_exe} still contains '${_pa_marker}'." >&2
            echo "        The first-run setup wizard is linked in, so the launcher" >&2
            echo "        will demand a cmake/clang toolchain before a player can" >&2
            echo "        play a build that already contains the game." >&2
            return 1
        fi
    done
}

psx_product_verify_no_toolchain_payload() {
    _pt_payload=$1
    for _pt_dir in toolchain overlay_toolchain cache; do
        if [ -e "$_pt_payload/$_pt_dir" ]; then
            echo "  FAIL: payload ships ${_pt_dir}/." >&2
            echo "        A no-compile package must not contain anything a player" >&2
            echo "        could read as \"you still need to build something\"." >&2
            return 1
        fi
    done
}

psx_verify_product_build() {
    _pv_exe=$1
    _pv_log=$2

    if [ ! -x "$_pv_exe" ]; then
        echo "  FAIL: no executable at ${_pv_exe}." >&2
        return 1
    fi

    if [ -z "${_pv_log:-}" ]; then
        echo "  FAIL: no configure log given; the product configure is unconfirmed." >&2
        return 1
    fi
    if [ ! -f "$_pv_log" ] || ! grep -q 'product build' "$_pv_log"; then
        echo "  FAIL: ${_pv_log} does not report the Wild9 product build." >&2
        echo "        -DWILD9_SETUP_WIZARD=OFF did not take effect." >&2
        return 1
    fi

    if ! command -v nm >/dev/null 2>&1; then
        echo "  FAIL: nm not found; cannot verify the product build." >&2
        return 1
    fi

    _pv_syms="${_pv_exe}.product-build.syms"
    if ! nm "$_pv_exe" > "$_pv_syms" 2>"${_pv_exe}.nm.err"; then
        echo "  FAIL: nm could not read ${_pv_exe}:" >&2
        cat "${_pv_exe}.nm.err" >&2
        echo "        Refusing to package an unverified build." >&2
        return 1
    fi

    psx_product_verify_game_code "$_pv_exe" "$_pv_syms" || return 1
    psx_product_verify_bios_backend "$_pv_exe" "$_pv_syms" || return 1

    psx_product_verify_absent "$_pv_exe" \
        WILD9RECOMP_PROJECT_ROOT \
        WILD9RECOMP_BUILD_DIR \
        WILD9RECOMP_FORCE_SETUP \
        RetroPortingToolKit/RetroPorting-Toolchains \
        || return 1

    echo "  product build verified: game C + OpenBIOS linked, setup host absent"
    return 0
}
