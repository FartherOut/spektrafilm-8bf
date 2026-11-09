# SPDX-License-Identifier: GPL-3.0-or-later
# CMakeLists.txt — spektrafilm-8bf (shipping build: MSVC /MT, x64).
#
# Canonical build for the Windows filter. The shipping target links the static
# CRT (/MT) and imports only the standard Win32 DLLs (kernel32/user32/gdi32 for
# the ImGui dialog's windowing + DPI); shell32 is not imported and nothing
# Adobe-related is linked. A cross-compiled *validation* build is available from
# Linux/CachyOS via hosts/ps8bf/build-mingw.sh.
#
# Configure + build (from a "x64 Native Tools Command Prompt" or after vcvars;
# build.ps1 auto-detects the newest installed VS 2022/2026 and does this for
# you):
#   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake -B build -G "Visual Studio 18 2026" -A x64   # needs CMake >= 4.2
#   cmake --build build
# Or just run build.ps1, which does the above and stages dist/spektrafilm.8bf.
#
# Requires Python 3 (generates the embedded pack resources and the PiPL).
cmake_minimum_required(VERSION 3.20)
# The project is spektrafilm-8bf (named after the repository, the way
# spektrafilm-ofx and spektrafilm-rs are named after theirs). The build *target*
# and the artifact keep the bare lowercase product name, so this still produces
# spektrafilm.8bf: a host loads "spektrafilm", not "spektrafilm-8bf".
project(spektrafilm-8bf C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(REPO "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(CORE     "${REPO}/spektra-core")
# The data pack the binary embeds is derived, never hand-edited: the pinned
# darktable-spektrafilm submodule pack plus the archived Hanatos2025 bandpass
# from the OFX tree, materialised by tools/sync_pack.py into
# spektra-core/data/pack (git-ignored, and a plain directory tree, so the build
# copies cleanly across filesystems and OSes). There is deliberately no fallback
# to the pristine submodule pack: that configures and builds without complaint
# and silently changes how the 20 film profiles that ship a bandpass render.
set(PACKDIR  "${CORE}/data/pack")
set(GEN      "${CMAKE_CURRENT_BINARY_DIR}/gen")

find_package(Python3 COMPONENTS Interpreter REQUIRED)

file(GLOB SF_PACK_SRCS "${CORE}/third_party/darktable-spektrafilm/packs/*/pack.json")
# Deliberately uncaptured: what the derivation did is part of the build report,
# so it streams into the build output instead of being held for the failure path.
execute_process(
  COMMAND ${Python3_EXECUTABLE} "${REPO}/tools/sync_pack.py"
  RESULT_VARIABLE SF_SYNC_RC)
if(NOT SF_SYNC_RC EQUAL 0)
  message(FATAL_ERROR "tools/sync_pack.py could not derive the data pack (see its output above)")
endif()

# --- generated: embedded data pack (RCDATA) + asset id table + PiPL ---------
add_custom_command(
  OUTPUT "${GEN}/asset_ids.h" "${GEN}/pack.rc" "${GEN}/pack_list.h"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${GEN}"
  # Re-sync before generating: a no-op while the stamp holds, and it re-derives
  # the pack when the submodule moves without needing a fresh configure.
  COMMAND ${Python3_EXECUTABLE} "${REPO}/tools/sync_pack.py"
  COMMAND ${Python3_EXECUTABLE} "${REPO}/tools/gen_assets.py" --win
          "${PACKDIR}" "${GEN}"
  DEPENDS "${REPO}/tools/gen_assets.py" "${REPO}/tools/sync_pack.py"
          "${REPO}/tools/merge_bandpass_hanatos2025.py" ${SF_PACK_SRCS}
  COMMENT "gen_assets --win: embedded data pack")
add_custom_target(sf_assets DEPENDS "${GEN}/asset_ids.h" "${GEN}/pack.rc"
                  "${GEN}/pack_list.h")

add_custom_command(
  OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/res/pipl.bin"
  COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_pipl.py"
          "${CMAKE_CURRENT_SOURCE_DIR}/res/pipl.bin"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_pipl.py"
  COMMENT "gen_pipl: PiPL resource")
add_custom_target(sf_pipl DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/res/pipl.bin")

# --- sources ----------------------------------------------------------------
# The real CRT supplies the printf/strtod family on Windows. stdio_asset.c is
# linked so the engine's file reads resolve against the RCDATA pack served by
# assets_win.c. It exposes sf_asset_* entry points (not CRT-named ones, which
# would collide with the static /MT CRT: LNK2005); the engine TUs have the CRT
# names mapped onto them below. Host TUs (filter_loop.c's transcript log and the
# dialogs) keep the real CRT stdio.
set(SF_CORE
  "${CORE}/third_party/darktable/spektra_core.c"
  "${CORE}/third_party/darktable/spektra_sim.c"
  "${CORE}/shims/dt_stubs.c"
  "${CORE}/shims/portability/glib_mini.c"
  "${CORE}/shims/portability/json_mini.c"
  "${CORE}/shims/portability/stdio_asset.c"
  "${CORE}/sim_port/sp_chain.c"
  "${CORE}/sim_port/sp_color.c"
  "${CORE}/sim_port/sp_params.c")

# Route the engine's fopen/fread/... through sf_asset_* (stdio_asset.c) in the
# plugin build. Applied per-source so the host TUs are untouched.
set_source_files_properties(${SF_CORE}
  PROPERTIES COMPILE_DEFINITIONS
  "fopen=sf_asset_fopen;fread=sf_asset_fread;fclose=sf_asset_fclose;feof=sf_asset_feof;ftell=sf_asset_ftell;fseek=sf_asset_fseek")
set(SF_HOST
  src/plugin_main.c
  src/params.c
  src/preset.c
  src/icc_profile.c
  src/pix_convert.c
  src/filter_loop.c
  src/sf_gui.c
  src/sf_parallel.c
  src/sf_preview.c
  src/assets_win.c
  src/sf_d3d11.cpp
  src/sf_gui_imgui.cpp)

# Dear ImGui (vendored, pinned, MIT) — core + the official Win32/DX11 backends.
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")
set(SF_IMGUI
  "${IMGUI_DIR}/imgui.cpp"
  "${IMGUI_DIR}/imgui_draw.cpp"
  "${IMGUI_DIR}/imgui_tables.cpp"
  "${IMGUI_DIR}/imgui_widgets.cpp"
  "${IMGUI_DIR}/backends/imgui_impl_win32.cpp"
  "${IMGUI_DIR}/backends/imgui_impl_dx11.cpp")
set(SF_RES
  res/resource.rc
  "${GEN}/pack.rc")

# A MODULE library with no prefix and a .8bf suffix => spektrafilm.8bf.
add_library(spektrafilm MODULE ${SF_CORE} ${SF_HOST} ${SF_IMGUI} ${SF_RES} res/plugin.def)

target_include_directories(spektrafilm PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/pi"
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${CMAKE_CURRENT_SOURCE_DIR}/res"
  "${IMGUI_DIR}"
  "${IMGUI_DIR}/backends"
  "${GEN}"
  "${CORE}/third_party/darktable"
  "${CORE}/sim_port"
  "${CORE}/shims"
  "${CORE}/shims/portability"
  "${CORE}/shims/portability/glib-2.0"
  "${CORE}/shims/portability/json-glib")

add_dependencies(spektrafilm sf_assets sf_pipl)

set_target_properties(spektrafilm PROPERTIES
  PREFIX "" SUFFIX ".8bf" OUTPUT_NAME "spektrafilm")

# MSVC's <math.h> hides M_PI / M_SQRT2 / M_SQRT1_2 / M_LOG2E unless this is
# defined *before* the header is first included. A command-line /D satisfies
# that; harmless on mingw/clang.
target_compile_definitions(spektrafilm PRIVATE _USE_MATH_DEFINES)

# ---------------------------------------------------------------------------
# Dear ImGui (vendored under third_party/imgui) is the only parameter-dialog
# backend; the original Win32 dialog was retired in phase B6. sf_gui.c runs it
# and reports if the D3D11 device cannot be created.
# ---------------------------------------------------------------------------
target_compile_definitions(spektrafilm PRIVATE
  IMGUI_DISABLE_OBSOLETE_FUNCTIONS
  IMGUI_DISABLE_DEMO_WINDOWS
  IMGUI_DEFINE_MATH_OPERATORS
  # Avoid static imm32.dll / shell32.dll imports pulled in by ImGui's default
  # Win32 shell + IME helpers (import-list invariant, D8).
  IMGUI_DISABLE_WIN32_DEFAULT_IME_FUNCTIONS
  IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS
  NOMINMAX)

# The parameter-dialog backend is Dear ImGui + Direct3D 11 (no runtime switch;
# the Win32 dialog was retired in phase B6). If no D3D11/WARP device can be
# created, sf_gui.c reports the failure and cancels.

# The DX11 backend calls D3DCompile(); redirect it to our dynamically loaded
# shim so it never links d3dcompiler.lib (keeps d3dcompiler_47.dll dynamic).
set_source_files_properties(
  "${IMGUI_DIR}/backends/imgui_impl_dx11.cpp"
  PROPERTIES COMPILE_DEFINITIONS "D3DCompile=sf_d3d_compile_shim")

# No warning suppressions on any route, and /W3 is MSVC's default level: whatever
# the compiler reports is printed and counted in the build report. What it reports
# today is one -Wunknown-pragmas / C4068 notice per translation unit that includes
# the vendored grain.h -- five of them in the plugin (spektra_core.c, spektra_sim.c,
# sp_chain.c, sf_parallel.c and the ImGui dialog) -- for its `#pragma STDC
# FP_CONTRACT` (and the `#pragma omp` / `#pragma GCC optimize` in spektra_core.c):
# directives GCC and MSVC do not implement. That code is kept unmodified, so the
# notices stay in the report instead of being silenced there. Nothing else is
# expected; a new code in the report means new diagnostics, and build.ps1 breaks
# them out per code.
set(SF_GCC_WARN -Wall)

if(MSVC)
  # Static CRT; only kernel32/user32/gdi32 imported.
  set_property(TARGET spektrafilm PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
  target_compile_options(spektrafilm PRIVATE
    /W3 /O2
    /D_CRT_SECURE_NO_WARNINGS /D_CRT_NONSTDC_NO_DEPRECATE
    $<$<COMPILE_LANGUAGE:CXX>:/EHsc>)
  target_link_libraries(spektrafilm PRIVATE user32 gdi32)
elseif(MINGW)
  target_compile_options(spektrafilm PRIVATE -O2 ${SF_GCC_WARN}
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>)
  # Keep the C++ runtime out of the validation build's import table.
  target_link_options(spektrafilm PRIVATE -static)
  target_link_libraries(spektrafilm PRIVATE user32 gdi32)
else()
  target_compile_options(spektrafilm PRIVATE -O2 ${SF_GCC_WARN}
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>)
  target_link_libraries(spektrafilm PRIVATE user32 gdi32)
endif()
