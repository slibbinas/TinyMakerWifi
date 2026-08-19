#!/usr/bin/env bash
#
# libslic3r SLA grandine -> WebAssembly.
#
# Sis skriptas pakartoja VISA build'a nuo tuscios masinos: parsisiunčia
# PrusaSlicer saltinius ir priklausomybes, susikompiliuoja NLopt bei qhull i
# WASM, tada surenka `sla.wasm` su musu tiltu (bridge.cpp).
#
# Naudojimas:
#   bash wasm/build.sh              # viskas nuo pradziu (pirma karta ~40 min)
#   bash wasm/build.sh link         # tik perlinkuoti (kai pakeistas bridge.cpp)
#   WORK=/kitas/kelias bash wasm/build.sh
#
# Reikia: git, python (su pip), bash. cmake ir ninja idiegiami per pip.
#
set -euo pipefail

WORK="${WORK:-/c/PIO-build}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="${OUT:-$WORK/wasm-build}"
PRUSA_TAG="${PRUSA_TAG:-version_2.9.6}"
JOBS="${JOBS:-8}"

say() { printf '\n=== %s ===\n' "$*"; }

# ---------------------------------------------------------------- irankiai
setup_tools() {
  say "irankiai"
  python -m pip install --quiet cmake ninja
  PYSCRIPTS="$(python -c 'import sysconfig,os;print(os.path.join(sysconfig.get_paths()["data"],"Scripts"))' 2>/dev/null || true)"
  [ -d "$PYSCRIPTS" ] && export PATH="$PATH:$PYSCRIPTS"

  if [ ! -x "$WORK/emsdk/upstream/emscripten/emcc" ] && [ ! -f "$WORK/emsdk/upstream/emscripten/emcc.py" ]; then
    git clone --depth 1 https://github.com/emscripten-core/emsdk.git "$WORK/emsdk"
    (cd "$WORK/emsdk" && ./emsdk install latest && ./emsdk activate latest)
  fi
  export PATH="$WORK/emsdk/upstream/emscripten:$PATH"
  em++ --version | head -1
}

# ----------------------------------------------------------- saltiniai
fetch() {
  say "saltiniai"
  if [ ! -d "$WORK/prusa-src/src/libslic3r" ]; then
    git clone --depth 1 --branch "$PRUSA_TAG" --filter=blob:none --sparse \
      https://github.com/prusa3d/PrusaSlicer.git "$WORK/prusa-src"
    (cd "$WORK/prusa-src" && git sparse-checkout set --skip-checks src deps cmake bundled_deps CMakeLists.txt)
  fi
  [ -d "$WORK/eigen/Eigen" ]  || git clone --depth 1 --branch 3.4.0 https://gitlab.com/libeigen/eigen.git "$WORK/eigen"
  [ -d "$WORK/cereal/include" ] || git clone --depth 1 --branch v1.3.2 https://github.com/USCiLab/cereal.git "$WORK/cereal"
  [ -d "$WORK/nlopt/src" ]    || git clone --depth 1 --branch v2.7.1 https://github.com/stevengj/nlopt.git "$WORK/nlopt"
  [ -d "$WORK/qhull/src" ]    || git clone --depth 1 --branch v8.0.2 https://github.com/qhull/qhull.git "$WORK/qhull"

  if [ ! -f "$WORK/cgal/include/CGAL/version.h" ]; then
    curl -sL "https://github.com/CGAL/cgal/releases/download/v5.6.1/CGAL-5.6.1-library.tar.xz" -o "$WORK/cgal.tar.xz"
    mkdir -p "$WORK/cgal" && tar -xf "$WORK/cgal.tar.xz" --strip-components=1 -C "$WORK/cgal" && rm "$WORK/cgal.tar.xz"
  fi

  mkdir -p "$WORK/wasm-inc/nanosvg" "$WORK/wasm-inc/nlohmann" "$WORK/wasm-gen"
  for f in nanosvg.h nanosvgrast.h; do
    [ -f "$WORK/wasm-inc/nanosvg/$f" ] || curl -sL "https://raw.githubusercontent.com/memononen/nanosvg/master/src/$f" -o "$WORK/wasm-inc/nanosvg/$f"
  done
  for f in json.hpp json_fwd.hpp; do
    [ -f "$WORK/wasm-inc/nlohmann/$f" ] || curl -sL "https://raw.githubusercontent.com/nlohmann/json/develop/single_include/nlohmann/$f" -o "$WORK/wasm-inc/nlohmann/$f"
  done

  # CMake ji generuoja is version.inc; mums uztenka keturiu eiluciu.
  cat > "$WORK/wasm-gen/libslic3r_version.h" <<'EOF'
#ifndef __SLIC3R_VERSION_H
#define __SLIC3R_VERSION_H
#define SLIC3R_APP_NAME "PrusaSlicer"
#define SLIC3R_APP_KEY "PrusaSlicer"
#define SLIC3R_VERSION "2.9.6"
#define SLIC3R_BUILD_ID "PrusaSlicer-2.9.6+WASM"
#endif
EOF
}

# ------------------------------------------------- priklausomybes i WASM
build_deps() {
  say "NLopt -> WASM"
  if [ ! -f "$WORK/nlopt/build-wasm/libnlopt.a" ]; then
    # NLopt 2.7.1 skriptai reikalauja CMake < 3.5 suderinamumo, kurio 4.x nebeturi.
    sed -i 's/cmake_minimum_required *( *VERSION *[0-9.]*/cmake_minimum_required(VERSION 3.10/' "$WORK/nlopt/cmake/"*.cmake
    emcmake cmake -S "$WORK/nlopt" -B "$WORK/nlopt/build-wasm" -G Ninja \
      -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
      -DNLOPT_PYTHON=OFF -DNLOPT_OCTAVE=OFF -DNLOPT_MATLAB=OFF -DNLOPT_GUILE=OFF -DNLOPT_SWIG=OFF -DNLOPT_TESTS=OFF
    cmake --build "$WORK/nlopt/build-wasm"
  fi

  say "qhull -> WASM"
  if [ ! -f "$WORK/qhull/build-wasm/libqhullcpp.a" ]; then
    emcmake cmake -S "$WORK/qhull" -B "$WORK/qhull/build-wasm" -G Ninja \
      -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF
    cmake --build "$WORK/qhull/build-wasm" --target qhullcpp qhullstatic_r
  fi
}

# ------------------------------------------------------------- vėliavos
flags() {
  SRC="$WORK/prusa-src/src"
  BD="$WORK/prusa-src/bundled_deps"
  INC="-I$SRC -I$SRC/libslic3r -I$SRC/clipper"
  INC="$INC -I$HERE/shims"                       # pakaitalai eina PIRMI
  INC="$INC -I$WORK/eigen -I$WORK/cereal/include -I$WORK/cgal/include -I$WORK/wasm-inc -I$WORK/wasm-gen"
  INC="$INC -I$WORK/nlopt/src/api -I$WORK/nlopt/build-wasm -I$WORK/qhull/src -I$WORK/qhull/src/libqhull_r"
  for d in "" semver admesh libigl ankerl fast_float tcbspan int128 miniz localesutils agg imgui libnest2d qoi stb_image; do
    INC="$INC -I$BD/$d"
  done
  INC="$INC -I$BD/glu-libtess/include"
  DEFS="-DNDEBUG -DCGAL_DISABLE_GMP=1 -DCGAL_DISABLE_ROUNDING_MATH_CHECK=1 -DCGAL_HAS_NO_THREADS=1"
  CXXFLAGS="-std=c++17 -O2 $DEFS"
  export INC CXXFLAGS SRC BD
}

# ---------------------------------------------------------- kompiliavimas
compile() {
  say "libslic3r -> WASM objektai"
  mkdir -p "$OUT/obj"
  cat > "$OUT/one.sh" <<'EOF'
f="$1"; n=$(echo "$f" | tr '/' '_' | sed 's/.cpp$//;s/.c$//')
o="$OUT/obj/$n.o"
[ -f "$o" ] && exit 0
case "$f" in
  *.c) emcc -O2 -DNDEBUG -include limits.h -c "$f" -o "$o" $INC 2> "$OUT/obj/$n.log" ;;
  *)   em++ $CXXFLAGS -c "$f" -o "$o" $INC --use-port=boost_headers 2> "$OUT/obj/$n.log" ;;
esac || { echo "NEPAVYKO $f :: $(grep -m1 -E 'error:' "$OUT/obj/$n.log" | head -c 120)"; exit 1; }
EOF
  export OUT
  ( cd "$SRC" && grep -v '^\s*#' "$HERE/sources.txt" | tr -d '\r' | xargs -P "$JOBS" -I{} bash "$OUT/one.sh" {} ) || true

  # bundled deps, kuriu nera sourceslist'e
  ( cd "$SRC" && for f in "$BD"/admesh/admesh/*.cpp "$BD"/localesutils/LocalesUtils.cpp; do
      n="x_$(basename "$f" .cpp)"; [ -f "$OUT/obj/$n.o" ] || em++ $CXXFLAGS -c "$f" -o "$OUT/obj/$n.o" $INC --use-port=boost_headers 2>/dev/null || true
    done )
  # glu-libtess: priorityq.c pats itraukia priorityq-heap.c, tad pastarojo NEKOMPILIUOJAM
  for f in "$BD"/glu-libtess/src/{dict,geom,memalloc,mesh,normal,priorityq,render,sweep,tess,tessmono}.c "$BD"/semver/semver.c; do
    n="c_$(basename "$f" .c)"; [ -f "$OUT/obj/$n.o" ] || emcc -O2 -DNDEBUG -include limits.h -c "$f" -o "$OUT/obj/$n.o" $INC 2>/dev/null || true
  done

  echo "objektu: $(ls "$OUT"/obj/*.o | wc -l)"
}

# --------------------------------------------------------------- linkas
link() {
  say "tiltas ir linkas"
  ( cd "$SRC" && em++ $CXXFLAGS -c "$HERE/bridge.cpp" -o "$OUT/obj/bridge.o" $INC --use-port=boost_headers )
  em++ -std=c++17 -O2 "$OUT"/obj/*.o \
    "$WORK/nlopt/build-wasm/libnlopt.a" \
    "$WORK/qhull/build-wasm/libqhullcpp.a" "$WORK/qhull/build-wasm/libqhullstatic_r.a" \
    -o "$OUT/sla.js" -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1 -sNODERAWFS=1 --use-port=boost_headers
  ls -la "$OUT/sla.wasm"
  echo "Patikra:  node $OUT/sla.js <model.stl> 0.05"
}

case "${1:-all}" in
  link)  setup_tools; flags; link ;;
  deps)  setup_tools; fetch; build_deps ;;
  *)     setup_tools; fetch; build_deps; flags; compile; link ;;
esac
