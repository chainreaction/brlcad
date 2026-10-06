#!/data/data/com.termux/files/usr/bin/bash
# ---------------------------------------------------------------------------
# BRL-CAD Termux smoke test
#
# Builds two small CSG models, renders one frame each, and exports both to
# ASCII and binary STL.  Every step is asserted, so a non-zero exit means the
# build is not functional.
#
#   dimple        demo.r = box - ball   (cube with a hemispherical dimple)
#   through-hole  demo.r = box - cyl    (cube drilled all the way through)
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

# Create a database from mged commands on stdin; the region must be demo.r.
build_model() {
    local db="$1"
    "$BRLCAD_BIN/mged" -c "$db" > "$db.mged.log" 2>&1 || fail "mged failed for $(basename "$db")"
    grep -q 'demo.r/R' "$db.mged.log" || fail "region demo.r not created in $(basename "$db")"
}

# Render one 64x64 frame of demo.r and assert that rays were traced.
render_model() {
    local db="$1" pix="$2"
    "$BRLCAD_BIN/rt" -s 64 -p 0 -o "$pix" "$db" demo.r > "$db.rt.log" 2>&1 \
        || fail "rt failed for $(basename "$db")"
    [ -s "$pix" ] || fail "rt produced no image for $(basename "$db")"
    grep -q 'rays' "$db.rt.log" || fail "rt reported no rays for $(basename "$db")"
}

# Export demo.r to ASCII and binary STL, validate both, print the triangle count.
check_stl() {
    local db="$1" base="$2" facets tris size
    "$BRLCAD_BIN/g-stl" -o "$base.stl" "$db" demo.r >> "$db.stl.log" 2>&1 \
        || fail "g-stl (ASCII) failed for $(basename "$db")"
    facets=$(grep -c 'facet normal' "$base.stl" || true)
    [ "${facets:-0}" -gt 0 ] || fail "ASCII STL has no facets: $base.stl"
    head -1 "$base.stl" | grep -q '^solid '    || fail "ASCII STL has no solid header: $base.stl"
    tail -1 "$base.stl" | grep -q '^endsolid ' || fail "ASCII STL has no endsolid trailer: $base.stl"

    "$BRLCAD_BIN/g-stl" -b -o "$base.bin.stl" "$db" demo.r >> "$db.stl.log" 2>&1 \
        || fail "g-stl (binary) failed for $(basename "$db")"
    tris=$(od -An -tu4 -j80 -N4 "$base.bin.stl" | tr -d ' ')
    size=$(stat -c%s "$base.bin.stl")
    [ "$tris" -gt 0 ] || fail "binary STL has no triangles: $base.bin.stl"
    [ "$size" -eq "$((84 + tris * 50))" ] \
        || fail "binary STL size mismatch for $base (header says $tris triangles, file is $size bytes)"
    [ "$facets" -eq "$tris" ] \
        || fail "ASCII/binary triangle count mismatch for $base ($facets vs $tris)"
    echo "$tris"
}

# --- case 1: cube with a visible hemispherical dimple ----------------------
echo "[1/2] dimple model (box - ball)"
build_model "$WORK/dimple.g" <<'EOF'
make -o 0 0 500 -s 600 ball sph
make box rpp
r demo.r u box - ball
ls
quit
EOF
render_model "$WORK/dimple.g" "$WORK/dimple.pix"
dimple_tris=$(check_stl "$WORK/dimple.g" "$WORK/dimple")
note "dimple: $dimple_tris triangles ($WORK/dimple.stl, $WORK/dimple.bin.stl)"

# --- case 2: cube with a cylindrical through-hole --------------------------
echo "[2/2] through-hole model (box - cyl)"
build_model "$WORK/hole.g" <<'EOF'
make box rpp
in cyl rcc 0 0 -500 0 0 1000 200
r demo.r u box - cyl
ls
quit
EOF
render_model "$WORK/hole.g" "$WORK/hole.pix"
hole_tris=$(check_stl "$WORK/hole.g" "$WORK/hole")
note "through-hole: $hole_tris triangles ($WORK/hole.stl, $WORK/hole.bin.stl)"

echo
echo "SMOKE TEST PASSED"
note "dimple       : $dimple_tris triangles"
note "through-hole : $hole_tris triangles"
