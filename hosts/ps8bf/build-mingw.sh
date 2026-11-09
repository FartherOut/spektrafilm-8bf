#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# build-mingw.sh — cross-compile the spektrafilm .8bf from Linux/CachyOS with
# mingw-w64, as a *validation* build (the shipping build is MSVC + static CRT;
# see build.ps1 / CMakeLists.txt). The resulting DLL imports the mingw runtime
# instead of being CRT-static, but exercises the same sources, the same x64
# FilterRecord ABI and the same embedded PiPL/RCDATA resources.
#
# Requirements (Arch/CachyOS):
#   pacman -S mingw-w64-gcc
# Usage:
#   ./build-mingw.sh            -> build/spektrafilm.8bf
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
core="$repo/spektra-core"
gen="$here/build/gen"
out="$here/build"
mkdir -p "$out" "$gen"

CC=x86_64-w64-mingw32-gcc
RC=x86_64-w64-mingw32-windres

# --- generate embedded assets + PiPL ---------------------------------------
# The embedded pack is derived, never hand-edited: the pinned submodule pack
# plus the archived Hanatos2025 bandpass. sync_pack.py is the only writer of
# data/pack, and it stops the build when an upstream tree is missing instead of
# falling back to the pristine pack — that fallback built fine and silently
# changed how the 20 film profiles that ship a bandpass render.
python3 "$repo/tools/sync_pack.py"
pack="$core/data/pack"

python3 "$repo/tools/gen_assets.py" --win "$pack" "$gen"
python3 "$here/tools/gen_pipl.py" "$here/res/pipl.bin"

INC="-I$here/pi -I$here/src -I$here/res -I$gen
     -I$here/third_party/imgui -I$here/third_party/imgui/backends
     -I$core/third_party/darktable -I$core/sim_port -I$core/shims
     -I$core/shims/portability -I$core/shims/portability/glib-2.0
     -I$core/shims/portability/json-glib"

# --- sources ----------------------------------------------------------------
# NOTE: on Windows the real CRT supplies the printf/strtod family. stdio_asset.c
# *is* linked so the engine's file reads resolve against the RCDATA pack
# (assets_win.c). It provides sf_asset_* and the engine TUs below get the CRT
# names mapped onto them (ASSET_DEFS); host TUs keep the real CRT stdio.
SRC="
 $core/third_party/darktable/spektra_core.c
 $core/third_party/darktable/spektra_sim.c
 $core/shims/dt_stubs.c
 $core/shims/portability/glib_mini.c
 $core/shims/portability/json_mini.c
 $core/shims/portability/stdio_asset.c
 $core/sim_port/sp_chain.c
 $core/sim_port/sp_color.c
 $core/sim_port/sp_params.c
 $here/src/plugin_main.c
 $here/src/params.c
 $here/src/preset.c
 $here/src/icc_profile.c
 $here/src/pix_convert.c
 $here/src/filter_loop.c
 $here/src/sf_gui.c
 $here/src/sf_parallel.c
 $here/src/sf_preview.c
 $here/src/assets_win.c
"

# Dear ImGui (vendored) + the C++ adapter TUs. Compiled with g++.
IMGUI="$here/third_party/imgui"
CXX_SRC="
 $IMGUI/imgui.cpp
 $IMGUI/imgui_draw.cpp
 $IMGUI/imgui_tables.cpp
 $IMGUI/imgui_widgets.cpp
 $IMGUI/backends/imgui_impl_win32.cpp
 $IMGUI/backends/imgui_impl_dx11.cpp
 $here/src/sf_d3d11.cpp
 $here/src/sf_gui_imgui.cpp
"

# ImGui config: drop obsolete API + demo, disable the Win32 shell/IME helpers
# (which would otherwise statically import shell32/imm32), and keep math operators.
IMGUI_DEFS="-DIMGUI_DISABLE_OBSOLETE_FUNCTIONS -DIMGUI_DISABLE_DEMO_WINDOWS \
            -DIMGUI_DEFINE_MATH_OPERATORS -DIMGUI_DISABLE_WIN32_DEFAULT_IME_FUNCTIONS \
            -DIMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS -DNOMINMAX"

# Engine TUs (under spektra-core) read the embedded pack through the sf_asset_*
# shim; map the CRT stdio names onto them for those files only, so host TUs
# (filter_loop.c's transcript log, the GUI dialog) keep the real CRT stdio.
ASSET_DEFS="-Dfopen=sf_asset_fopen -Dfread=sf_asset_fread -Dfclose=sf_asset_fclose \
            -Dfeof=sf_asset_feof -Dftell=sf_asset_ftell -Dfseek=sf_asset_fseek"

# --- resources --------------------------------------------------------------
$RC "$here/res/resource.rc" -O coff -o "$out/resource.o" -I"$here/res"
$RC "$gen/pack.rc"          -O coff -o "$out/pack.o"

# --- compile + link ---------------------------------------------------------
# -municode not needed (ANSI Win32). kernel32 comes from the CRT; user32 (window
# APIs) and gdi32 (ImGui's Win32 backend queries the display DPI through a GDI
# DC) are linked. shell32 is deliberately not used by the slice. comdlg32 was
# only the retired native dialog's colour picker, so it is no longer imported.
# The GPU stack (d3d11/dxgi/d3dcompiler_47) is LoadLibrary()'d at runtime, so no
# additional import libs are needed (decision D8).
# -static keeps the C++ runtime (libstdc++/libgcc/libwinpthread) out of the
# import table, matching the intent of the shipping /MT build.
CXX=x86_64-w64-mingw32-g++
# No suppressions: whatever -Wall reports is printed. The recurring notices are
# the #pragma omp / GCC optimize / STDC FP_CONTRACT directives in the vendored
# darktable headers (one -Wunknown-pragmas line per file that includes grain.h);
# that code is kept unmodified, so the notice stays in the report.
WARNFLAGS="-Wall"
CXXFLAGS="-m64 -O2 -std=gnu++17 $WARNFLAGS -fno-exceptions -fno-rtti $IMGUI_DEFS"

# A compile's diagnostics go to a log so they can be shown for failures *and*
# for warnings: printing them only when the compile fails lets a build quietly
# accumulate warnings. Nothing is filtered; the total and the per-option
# breakdown land in the report so a new class of diagnostic cannot hide.
warn_total=0
warn_codes="$out/warn-codes.txt"
: > "$warn_codes"
show_diagnostics() { # <source path> <diagnostics log>
  if [ -s "$2" ]; then
    echo "diagnostics from ${1#"$repo"/}:"
    cat "$2"
    warn_total=$((warn_total + $(grep -c "warning:" "$2" || true)))
    grep -o '\[-W[a-z0-9-]*\]' "$2" | sed 's/\[-\(.*\)\]/-\1/' >> "$warn_codes" || true
  fi
}

cc_obj=""
n_c=0
for f in $SRC; do
  o="$out/$(basename "${f%.c}").o"
  extra=""
  case "$f" in "$core"/*) extra="$ASSET_DEFS" ;; esac
  if ! $CC -m64 -O2 -std=gnu11 $WARNFLAGS $extra $INC -c "$f" -o "$o" 2>"$out/compile.log"; then
    echo "FAILED: $f"; cat "$out/compile.log"; exit 1
  fi
  show_diagnostics "$f" "$out/compile.log"
  cc_obj="$cc_obj $o"
  n_c=$((n_c + 1))
done

cxx_obj=""
n_cxx=0
for f in $CXX_SRC; do
  o="$out/$(basename "${f%.cpp}").o"
  # imgui_impl_dx11.cpp must call our dynamically-loaded D3DCompile shim.
  extra=""
  case "$f" in
    */imgui_impl_dx11.cpp) extra="-DD3DCompile=sf_d3d_compile_shim" ;;
  esac
  if ! $CXX $CXXFLAGS $extra $INC -c "$f" -o "$o" 2>"$out/compile.log"; then
    echo "FAILED: $f"; cat "$out/compile.log"; exit 1
  fi
  show_diagnostics "$f" "$out/compile.log"
  cxx_obj="$cxx_obj $o"
  n_cxx=$((n_cxx + 1))
done

rm -f "$out/compile.log"
$CXX -m64 -shared -static -o "$out/spektrafilm.8bf" \
    $cc_obj $cxx_obj "$out/resource.o" "$out/pack.o" \
    "$here/res/plugin.def" \
    -luser32 -lgdi32

# --- report ------------------------------------------------------------------
# What went in and what came out, every build: the derived pack's provenance, the
# diagnostics count, and the artifact's identity. sha256 names *this file*, not
# this run: the linker stamps a TimeDateStamp into the PE header, so two builds of
# identical sources hash differently. Compare the counts and sizes between runs,
# and the hash between two copies of one artifact.
pack_info=$(python3 - "$core/data/pack.stamp" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
print(f"{d['src_files']} files, {d['band_profiles']} bandpass profiles, "
      f"src pack {d['src'][:12]}, bands {d['bands'][:12]}")
PY
)
warn_by=""
if [ -s "$warn_codes" ]; then
  warn_by=$(sort "$warn_codes" | uniq -c |
           awk '{a[NR]=$2" x"$1} END {for (i=1;i<=NR;i++) printf "%s%s", (i>1?", ":""), a[i]}')
fi
echo
echo "== build report =="
printf "  %-12s %s\n" "compiler"   "$($CC --version | head -1)"
printf "  %-12s %s\n" "C++"        "$($CXX --version | head -1)"
printf "  %-12s %s\n" "sources"    "$n_c C, $n_cxx C++ (+ 2 generated resources)"
printf "  %-12s %s\n" "warnings"   "$warn_total${warn_by:+  ($warn_by)}"
printf "  %-12s %s\n" "pack"       "$pack_info"
printf "  %-12s %s (%s bytes)\n" "artifact" "$out/spektrafilm.8bf" \
       "$(stat -c%s "$out/spektrafilm.8bf")"
printf "  %-12s %s\n" "sha256 file" "$(sha256sum "$out/spektrafilm.8bf" | cut -d' ' -f1)"
printf "  %-12s %s\n" "imports"    "$(x86_64-w64-mingw32-objdump -p "$out/spektrafilm.8bf" \
                                    | awk '/DLL Name:/{printf "%s ", $3}')"
# The api-ms-win-crt-* entries above are expected here and only here: this route
# links the mingw runtime. The shipping MSVC build is /MT and imports nothing
# else, which build.ps1 enforces.
printf "  %-12s %s\n" "entry point" "$(x86_64-w64-mingw32-nm -g --defined-only "$out/spektrafilm.8bf" \
                                    | awk '$3=="PluginMain"{print $2" "$3; exit}')"
