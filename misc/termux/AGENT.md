# Agent guide — building BRL-CAD under Termux

This file is written for **AI agents and automation**. It is a deterministic,
idempotent, verifiable version of `../../TERMUX.md`. Prefer reading this file when
executing, and `TERMUX.md` when explaining to a human.

## Goal

Produce a working BRL-CAD build on a 64-bit Android/Termux host:

* `bin/mged -c` can create and list geometry.
* `bin/rt` renders a frame.
* `make` returns exit code 0.

## Invariants (never violate)

1. **Never pass `--sysroot`** (or any `-isysroot`) to the compiler. It breaks
   libc++ header lookup and the very first bext C++ file fails with
   `fatal error: 'algorithm' file not found`.
2. Target the device's API level explicitly: `--target=aarch64-linux-android36`
   (adjust to the host's API level if different).
3. Always `unset CFLAGS CXXFLAGS SYSROOT` before configuring and re-export only the
   `--target=...` value.
4. Never run `make` without `LD_LIBRARY_PATH=$BRLCAD_BUILD/lib` when executing freshly
   built binaries — Termux has no `ldconfig`.
5. Patch application must be idempotent-checkable: use `git apply --reverse --check`
   to confirm a patch is already applied before attempting to apply it again.

## Variables

```bash
export BRLCAD_SRC=$HOME/brlcad/brlcad
export BEXT_SRC=$HOME/brlcad/bext
export BRLCAD_BUILD=$BRLCAD_SRC/build
export BEXT_BUILD=$BRLCAD_BUILD/bext_build
export BEXT_OUT=$BRLCAD_BUILD/bext_output
export P=$BRLCAD_SRC/misc/termux/patches
```

## Step 1 — prerequisites

```bash
pkg install -y clang cmake make ninja git python pkg-config
pkg install -y tcl tk xorgproto libx11 libxext libxmu libxi libxrender libsm libice libglvnd mesa
pkg install -y libandroid-shmem
pkg install -y freetype libpng libjpeg-turbo libtiff libexpat libsqlite zlib zstd
```

Verify:

```bash
for c in clang cmake make git pkg-config; do command -v "$c" || exit 1; done
test -e "$PREFIX/lib/libandroid-shmem.so" || exit 1
test -e "$PREFIX/lib/libfreetype.so"     || exit 1
```

## Step 2 — sources

```bash
mkdir -p "$HOME/brlcad" && cd "$HOME/brlcad"
[ -d brlcad ] || git clone --branch termux https://github.com/chainreaction/brlcad.git
[ -d bext ]   || git clone https://github.com/BRL-CAD/bext.git
# if the clone already exists, just make sure we are on the porting branch:
git -C brlcad checkout termux
git -C bext submodule update --init --recursive
```

> The BRL-CAD-side fixes are already committed on the `termux` branch.
> The optional `patches/01-main-brlcad.patch` exists only to carry them to another
> checkout.

## Step 3 — apply bext patches (idempotent)

```bash
apply() {  # apply <repo-dir> <patch>
  local d="$1" p="$2"
  if git -C "$d" apply --reverse --check "$p" 2>/dev/null; then
    echo "already applied: $p"
  else
    git -C "$d" apply "$p"
    echo "applied: $p"
  fi
}

apply "$BEXT_SRC"                          "$P/02-bext-drivers.patch"
apply "$BEXT_SRC/geogram/geogram"          "$P/10-geogram_geogram.patch"
apply "$BEXT_SRC/opennurbs/opennurbs"      "$P/11-opennurbs_opennurbs.patch"
apply "$BEXT_SRC/poissonrecon/PoissonRecon" "$P/12-poissonrecon_PoissonRecon.patch"
apply "$BEXT_SRC/stepcode/stepcode"        "$P/13-stepcode_stepcode.patch"
apply "$BEXT_SRC/utahrle/utahrle"          "$P/14-utahrle_utahrle.patch"

# tinygltf: BRL-CAD now uses the v3 API; the pinned submodule is v2.
# Move the submodule to the current 'release' head (idempotent).
if ! grep -q 'tiny_gltf_v3.h' "$BEXT_SRC/tinygltf/tinygltf/CMakeLists.txt" 2>/dev/null; then
  git -C "$BEXT_SRC/tinygltf/tinygltf" fetch origin release
  git -C "$BEXT_SRC/tinygltf/tinygltf" checkout origin/release
fi

BEXT_SRC="$BEXT_SRC" BEXT_BUILD="$BEXT_BUILD" bash "$BRLCAD_SRC/misc/termux/bext-extra-edits.sh"
```

Note: the `bext-extra-edits.sh` submodule edits are **not** mirrored in the patch
files. Run the script; it is idempotent.

## Step 4 — build bext

```bash
unset CFLAGS CXXFLAGS SYSROOT
export CFLAGS="--target=aarch64-linux-android36"
export CXXFLAGS="$CFLAGS"

cmake -S "$BEXT_SRC" -B "$BEXT_BUILD" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX="$BEXT_OUT"

BEXT_SRC="$BEXT_SRC" BEXT_BUILD="$BEXT_BUILD" bash "$BRLCAD_SRC/misc/termux/bext-extra-edits.sh"

# converge: repeat until exit code 0
until cmake --build "$BEXT_BUILD" --parallel 6 -k 0; do
  echo "bext build not clean yet; re-running"
done
```

Success check:

```bash
test -d "$BEXT_OUT/install/lib" || exit 1
```

## Step 5 — configure + build BRL-CAD

Use exactly the flags in `TERMUX.md` §5 (the long `cmake` invocation). Then:

```bash
make -j6
test "${PIPESTATUS[0]:-$?}" -eq 0 || exit 1
```

## Step 6 — verify

The repo ships an asserted end-to-end smoke test. It must exit 0 and print
`SMOKE TEST PASSED`:

```bash
BRLCAD_BUILD="$BRLCAD_BUILD" bash "$BRLCAD_SRC/misc/termux/smoke-test.sh"
```

It builds two CSG models — `demo.r = box - ball` (a cube with a visible
hemispherical dimple) and `demo.r = box - cyl` (a cube with a cylindrical
through-hole) — renders one 64×64 frame of each, and exports both ASCII and
binary STL, checking the triangle counts, the ASCII framing and the binary STL
byte length (`84 + triangles*50`).  Expected: `dimple: 182 triangles`,
`through-hole: 112 triangles`.

Minimal manual equivalent if the script is unavailable:

```bash
export LD_LIBRARY_PATH="$BRLCAD_BUILD/lib:$LD_LIBRARY_PATH"
test -x "$BRLCAD_BUILD/bin/mged" || exit 1
test -x "$BRLCAD_BUILD/bin/rt"   || exit 1

out=$(printf 'make sph sph\nls\nquit\n' | "$BRLCAD_BUILD/bin/mged" -c "${TMPDIR:-$HOME/tmp}/agent_t.g")
echo "$out" | grep -q 'sph' || exit 1

"$BRLCAD_BUILD/bin/rt" -s 64 -p 0 -o "${TMPDIR:-$HOME/tmp}/agent_t.pix" \
    "${TMPDIR:-$HOME/tmp}/agent_t.g" sph >"${TMPDIR:-$HOME/tmp}/agent_rt.log" 2>&1
test -s "${TMPDIR:-$HOME/tmp}/agent_t.pix" || exit 1
grep -q 'rays' "${TMPDIR:-$HOME/tmp}/agent_rt.log" || exit 1
```

## Failure → remediation table

| Observation | Action |
|---|---|
| `fatal error: 'algorithm' file not found` | a `--sysroot` leaked in; `unset CFLAGS CXXFLAGS SYSROOT`, wipe the CMake cache, reconfigure |
| build produces no output for minutes, processes in `S`/`futex_wait`, 0 CPU | the `bu_semaphore` alignment fix is missing — verify `src/libbu/semaphore.c` contains `__attribute__((aligned(8)))` on `struct bu_semaphores` |
| `undefined reference to shmget` / `shmat` | `libandroid-shmem` not linked: check `src/libbu/CMakeLists.txt` and the `-landroid-shmem` linker flags |
| any `-msse*`/`__SSE__` error | a bext submodule patch was not applied |
| `Cannot find -lfreetype` / undefined freetype symbols | add `-lfreetype` to linker flags (`opennurbs` patch + BRL-CAD configure) |
| `undefined symbol: FT_Done_Face` / other `FT_*` linking a static OpenNURBS consumer | `misc/CMake/FindOPENNURBS.cmake` must add FreeType to `OPENNURBS::OPENNURBS-static` (`INTERFACE_LINK_LIBRARIES`) |
| `undefined symbol: modf` (or another math symbol) linking a tool | `src/libbu/CMakeLists.txt` must expose `m` via `PUBLIC_LIBS ${BU_PUBLIC_LIBS}`; the error means it is only in `BU_PRIVATE_LIBS` |
| `fatal error: 'tiny_gltf_v3.h' file not found` | update the `tinygltf` submodule to `release` head (Step 3) and ensure `02-bext-drivers.patch` applied; rebuild the `TINYGLTF_BLD-install` target after removing `bext_build/tinygltf/TINYGLTF_BLD-prefix` |
| `fuzz_*` / sanitizer link errors | `regress/fuzz` must stay disabled |
| binary runs but `error while loading shared libraries` | set `LD_LIBRARY_PATH` |

## Idempotency contract

A second full run of Steps 1–6 on an already-built tree must be a no-op:
patches report "already applied", `bext-extra-edits.sh` changes nothing, and
`make` returns 0 immediately. If that is not true, treat it as a bug in this
procedure.
