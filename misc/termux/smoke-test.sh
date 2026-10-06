#!/data/data/com.termux/files/usr/bin/bash
# ---------------------------------------------------------------------------
# BRL-CAD Termux smoke test
#
# Creates a small CSG model (cube with a spherical cavity), renders one frame,
# and exports it to ASCII and binary STL.  Every step is asserted, so a
# non-zero exit means the build is not functional.
#
# Usage:
#   BRLCAD_BUILD=/path/to/build  bash misc/termux/smoke-test.sh
#
# Environment:
#   BRLCAD_BUILD  build directory containing bin/ and lib/ (default:
#                 ${BRLCAD_SRC:-$HOME/brlcad/brlcad}/build)
#   BRLCAD_BIN    override bin directory
#   BRLCAD_LIB    override lib directory
# ---------------------------------------------------------------------------
set -euo pipefail

: "${BRLCAD_BUILD:=${BRLCAD_SRC:-$HOME/brlcad/brlcad}/build}"
: "${BRLCAD_BIN:=$BRLCAD_BUILD/bin}"
: "${BRLCAD_LIB:=$BRLCAD_BUILD/lib}"

WORK=$(mktemp -d "${TMPDIR:-$HOME/tmp}/brlcad-smoke.XXXXXX")
export LD_LIBRARY_PATH="$BRLCAD_LIB:${LD_LIBRARY_PATH:-}"

fail() { echo "SMOKE TEST FAILED: $*" >&2; exit 1; }
note() { echo "  $*"; }

echo "BRL-CAD smoke test"
echo "  build : $BRLCAD_BUILD"
echo "  work  : $WORK"

for exe in mged rt g-stl; do
    [ -x "$BRLCAD_BIN/$exe" ] || fail "$exe not found at $BRLCAD_BIN/$exe"
done

# --- 1. build a model: region demo.r = box - ball (visible dimple) --------
echo "[1/4] creating model (box with a spherical dimple)"
printf '%s\n' \
    'make -o 0 0 500 -s 600 ball sph' \
    'make box rpp' \
    'r demo.r u box - ball' \
    'ls' \
    'quit' | "$BRLCAD_BIN/mged" -c "$WORK/demo.g" > "$WORK/mged.log" 2>&1 \
    || fail "mged exited non-zero"
grep -q 'demo.r/R' "$WORK/mged.log" || fail "region demo.r was not created"
note "model: $WORK/demo.g"

# --- 2. render one frame ---------------------------------------------------
echo "[2/4] rendering one 64x64 frame"
"$BRLCAD_BIN/rt" -s 64 -p 0 -o "$WORK/demo.pix" "$WORK/demo.g" demo.r \
    > "$WORK/rt.log" 2>&1 || fail "rt exited non-zero"
[ -s "$WORK/demo.pix" ] || fail "rt produced no image"
grep -q 'rays' "$WORK/rt.log" || fail "rt reported no rays"
note "image: $WORK/demo.pix ($(stat -c%s "$WORK/demo.pix") bytes)"

# --- 3. ASCII STL ----------------------------------------------------------
echo "[3/4] exporting ASCII STL"
"$BRLCAD_BIN/g-stl" -o "$WORK/demo.stl" "$WORK/demo.g" demo.r \
    > "$WORK/stl.log" 2>&1 || fail "g-stl (ASCII) exited non-zero"
facets=$(grep -c 'facet normal' "$WORK/demo.stl" || true)
[ "${facets:-0}" -gt 0 ] || fail "ASCII STL contains no facets"
head -1 "$WORK/demo.stl" | grep -q '^solid ' || fail "ASCII STL has no solid header"
tail -1 "$WORK/demo.stl" | grep -q '^endsolid ' || fail "ASCII STL has no endsolid trailer"
note "ASCII STL: $WORK/demo.stl ($facets facets)"

# --- 4. binary STL ---------------------------------------------------------
echo "[4/4] exporting binary STL"
"$BRLCAD_BIN/g-stl" -b -o "$WORK/demo_bin.stl" "$WORK/demo.g" demo.r \
    > "$WORK/stlb.log" 2>&1 || fail "g-stl (binary) exited non-zero"
tris=$(od -An -tu4 -j80 -N4 "$WORK/demo_bin.stl" | tr -d ' ')
size=$(stat -c%s "$WORK/demo_bin.stl")
[ "$size" -eq "$((84 + tris * 50))" ] \
    || fail "binary STL size mismatch (header says $tris triangles, file is $size bytes)"
[ "$tris" -gt 0 ] || fail "binary STL contains no triangles"
note "binary STL: $WORK/demo_bin.stl ($tris triangles, $size bytes)"

echo
echo "SMOKE TEST PASSED"
echo "  ASCII STL : $WORK/demo.stl"
echo "  binary STL: $WORK/demo_bin.stl"
