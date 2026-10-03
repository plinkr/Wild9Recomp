cmake_minimum_required(VERSION 3.20)
set(PSXRECOMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/psxrecomp" CACHE PATH
    "Path to psxrecomp")
if(NOT EXISTS "${PSXRECOMP_ROOT}/runtime/runtime.cmake")
    message(FATAL_ERROR
        "PSXRecomp runtime is missing: ${PSXRECOMP_ROOT}/runtime/runtime.cmake\n"
        "This project expects the psxrecomp framework checkout at ${PSXRECOMP_ROOT}.\n"
        "Run: git submodule update --init --recursive\n"
        "If psxrecomp/ is not a submodule, set -DPSXRECOMP_ROOT=<path-to-psxrecomp>.")
endif()

project(Wild9Recomp LANGUAGES C CXX)

option(WILD9_SETUP_WIZARD
    "Compile the first-run setup wizard + local codegen host into psx-runtime" ON)

if(WILD9_SETUP_WIZARD)
    set(PSX_SETUP_WIZARD ON CACHE BOOL
        "Advertise first-run setup wizard + Generate & rebuild in recomp-ui" FORCE)
    set(WILD9_SETUP_WIZARD_ARGS
        ENABLE_SETUP_WIZARD
        CODEGEN_SETUP_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/codegen_setup.c")
else()
    set(PSX_SETUP_WIZARD OFF CACHE BOOL
        "Advertise first-run setup wizard + Generate & rebuild in recomp-ui" FORCE)
    set(WILD9_SETUP_WIZARD_ARGS)
endif()
include("${PSXRECOMP_ROOT}/runtime/runtime.cmake")

set(PSX_PRELOADED_MODS "${CMAKE_CURRENT_SOURCE_DIR}/mods/preloaded")
if(NOT IS_DIRECTORY "${PSX_PRELOADED_MODS}")
    set(PSX_PRELOADED_MODS "NONE")
endif()

file(GLOB WILD9_MOD_PLUGIN_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mods/*.c")

psxrecomp_add_game_runtime(psx-runtime
    PGXP
    PRELOADED_MODS_DIR "${PSX_PRELOADED_MODS}"
    ${WILD9_SETUP_WIZARD_ARGS}
    WINDOW_TITLE "Wild9 Recompiled"
    DEFAULT_GAME_CONFIG_PATH "game.toml"
    MAX_PLAYERS 1
    LAUNCHER_BOXART "${CMAKE_CURRENT_SOURCE_DIR}/launcher_assets/img/boxart.tga"
    APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/psxrecomp.ico"
    GEN_MARKER "generated/SLUS_004.25_dispatch.c"
    GEN_FULL_GLOB "generated/SLUS_004.25_full_*.c"
)

if(NOT WILD9_SETUP_WIZARD)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/generated/SLUS_004.25_dispatch.c")
        message(FATAL_ERROR
            "WILD9_SETUP_WIZARD=OFF requires generated/ to be present, but "
            "generated/SLUS_004.25_dispatch.c is missing.\n"
            "Generate the game C with tools/regen.sh first, or configure with "
            "-DWILD9_SETUP_WIZARD=ON to build the setup host instead.")
    endif()
    if(PSXRECOMP_FORCE_SETUP_HOST)
        message(FATAL_ERROR
            "WILD9_SETUP_WIZARD=OFF and PSXRECOMP_FORCE_SETUP_HOST=ON are "
            "mutually exclusive: the first builds a product binary, the "
            "second deliberately omits the game C and needs the wizard.")
    endif()
    file(GLOB _wild9_full_c "${CMAKE_CURRENT_SOURCE_DIR}/generated/SLUS_004.25_full_*.c")
    list(LENGTH _wild9_full_c _wild9_full_n)
    if(_wild9_full_n LESS 1)
        message(FATAL_ERROR
            "WILD9_SETUP_WIZARD=OFF found no generated/SLUS_004.25_full_*.c. "
            "Run tools/regen.sh to regenerate the game C before packaging.")
    endif()
    message(STATUS
        "Wild9: product build - ${_wild9_full_n} generated game C file(s) "
        "linked, first-run setup wizard and codegen host excluded")
endif()

target_sources(psx-runtime PRIVATE ${WILD9_MOD_PLUGIN_SOURCES})

foreach(_PSX_RUNTIME_CONFIG game.toml game_options.toml)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_PSX_RUNTIME_CONFIG}")
        add_custom_command(TARGET psx-runtime POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/${_PSX_RUNTIME_CONFIG}"
                "$<TARGET_FILE_DIR:psx-runtime>/${_PSX_RUNTIME_CONFIG}"
            COMMENT "Staging ${_PSX_RUNTIME_CONFIG}"
            VERBATIM)
    endif()
endforeach()
