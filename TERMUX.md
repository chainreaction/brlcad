# BRL-CAD on Termux (Android / aarch64)

A complete, reproducible recipe for building **BRL-CAD** and its bundled external
dependencies (**bext**) inside **Termux** on a 64-bit Android device — no root, no
proot, no Linux container.

> **Fork:** https://github.com/chainreaction/brlcad — branch **`termux`**.
>
> This `termux` branch is a community/porting branch based on the tested revision.
> It already contains the BRL-CAD-side porting fixes, so a plain checkout +
> `cmake` + `make` is enough for the CAD tree itself. The `bext` side lives in a
> separate repository and is handled with the patches under `misc/termux/patches/`.

* **For users:** read this file top to bottom.
* **For AI agents / automation:** read `misc/termux/AGENT.md` — it is a
  deterministic, checkable version of this guide.

---

## 0. Tested configuration

| | |
|---|---|
| Device | Snapdragon 8 Elite (Adreno 830), Android 15 (API 36) |
| Termux prefix | `/data/data/com.termux/files/usr` |
| Compiler | clang 21.1.8 / libc++ (bionic) |
| CMake | 4.4.4 |
| BRL-CAD | 7.46.x (validated on `7929a747ad`) |
| Build type | `Release`, `make -j6` |

Result: build reaches **100 %**; `mged`, `rt` and all converters run headless, the
example `.g` databases are generated, and the Tcl/Tk GUI libraries are linked
(the GUI itself needs an X server — see §8).

Wall-clock: roughly **1–3 h** on a phone-class SoC, depending on throttling.
`-j6` is the recommended parallelism; `-j8` only pushes the SoC into thermal
throttling.

---

## 1. Install Termux packages

```bash
pkg update
pkg install clang cmake make ninja git python pkg-config

# Tcl/Tk + X11/GL headers and libraries used by libdm / mged
pkg install tcl tk xorgproto libx11 libxext libxmu libxi libxrender \
            libsm libice libglvnd mesa

# bionic shim: <sys/shm.h> redirects shmget/shmat to libandroid-shmem
pkg install libandroid-shmem

# image / text / compression deps
pkg install freetype libpng libjpeg-turbo libtiff libexpat libsqlite zlib zstd

# optional — GPU / Vulkan diagnostics (see §8)
pkg install mesa-vulkan-icd-freedreno vulkan-tools clinfo
```

If the X11 packages come from a separate repository, enable it first:

```bash
pkg install x11-repo
```

---

## 2. Get the sources

```bash
mkdir -p ~/brlcad && cd ~/brlcad

# BRL-CAD — use this termux branch
git clone --branch termux https://github.com/chainreaction/brlcad.git

# bext — separate repo with its own submodules
git clone https://github.com/BRL-CAD/bext.git
git -C bext submodule update --init --recursive
```

Recommended environment (the guide uses these names):

```bash
export BRLCAD_SRC=$HOME/brlcad/brlcad
export BEXT_SRC=$HOME/brlcad/bext
export BRLCAD_BUILD=$BRLCAD_SRC/build
export BEXT_BUILD=$BRLCAD_BUILD/bext_build
export BEXT_OUT=$BRLCAD_BUILD/bext_output
```

---

## 3. Patch bext

The BRL-CAD tree on this branch is already patched. **All remaining changes are on
the bext side.** Apply them in order:

```bash
P=$BRLCAD_SRC/misc/termux/patches

# 3a. bext driver CMakeLists (assetimport, geogram, opencv, opennurbs, tinygltf)
git -C "$BEXT_SRC" apply "$P/02-bext-drivers.patch"

# 3b. bext submodules
git -C "$BEXT_SRC/geogram/geogram"           apply "$P/10-geogram_geogram.patch"
git -C "$BEXT_SRC/opennurbs/opennurbs"       apply "$P/11-opennurbs_opennurbs.patch"
git -C "$BEXT_SRC/poissonrecon/PoissonRecon" apply "$P/12-poissonrecon_PoissonRecon.patch"
git -C "$BEXT_SRC/stepcode/stepcode"         apply "$P/13-stepcode_stepcode.patch"
git -C "$BEXT_SRC/utahrle/utahrle"           apply "$P/14-utahrle_utahrle.patch"

# 3c. tinygltf: newer BRL-CAD uses the v3 API (tiny_gltf_v3.h), but the pinned
#     bext submodule is still v2.  Move it to the current 'release' branch head.
git -C "$BEXT_SRC/tinygltf/tinygltf" fetch origin release
git -C "$BEXT_SRC/tinygltf/tinygltf" checkout origin/release

# 3d. edits that ExternalProject only makes in its build-tree copies
bash "$BRLCAD_SRC/misc/termux/bext-extra-edits.sh"
```

### What each patch fixes

**bext drivers** (`02-bext-drivers.patch`)

| File | Change | Why |
|---|---|---|
| `assetimport/CMakeLists.txt` | `-DASSIMP_WARNINGS_AS_ERRORS=OFF` | host clang 21 turns warnings into errors |
| `geogram/CMakeLists.txt` | `-DVORPALINE_PLATFORM=Linux64-nonx86-clang-dynamic` | arm64, no SSE |
| `opencv/CMakeLists.txt` | `-DBUILD_ANDROID_PROJECTS=OFF -DBUILD_ANDROID_EXAMPLES=OFF` | they try to pull in the NDK |
| `opennurbs/CMakeLists.txt` | add `-lfreetype` to linker flags | freetype symbols not propagated |
| `tinygltf/CMakeLists.txt` + `tinygltf.patch` | build tinygltf v3 as a library and export `tinygltf::tinygltf` | upstream switched to the v3 API |

**bext submodules**

| Submodule | Change | Why |
|---|---|---|
| `geogram` | remove `-msse3` from `cmake/platforms/Linux-clang.cmake` | x86-only flag on aarch64 |
| `opennurbs` | disable `CreateFaceWithAndroidNdk` path | NDK-only, not available in Termux |
| `poissonrecon` | guard `#include <sys/timeb.h>` to Windows | header absent on bionic |
| `stepcode` | cast `strncpy` args in `judySArray.h` / `judyS2Array.h` | `_buff` is `unsigned char*` |
| `utahrle` | add `#include <strings.h>` | `bzero`/`bcopy` need it on bionic |

`bext-extra-edits.sh` additionally handles three things that only exist in the
copies `ExternalProject_Add(URL <submodule-dir>)` creates:
`geogram/.../android_utils.cpp` (disabled), `geogram/.../PoissonRecon/Geometry.cpp`
(`__ANDROID__` branches off) and Mesa's `osmesa/.../main/imports.c` (`<strings.h>`).
The script is idempotent and also fixes the submodule sources, so a fresh bext
configure reproduces the state automatically.

---

## 4. Build bext

bext is meant to be built **separately**; BRL-CAD then consumes the result via
`BRLCAD_EXT_DIR`.

```bash
unset CFLAGS CXXFLAGS SYSROOT          # never let --sysroot leak in, see §7
export CFLAGS="--target=aarch64-linux-android36"
export CXXFLAGS="$CFLAGS"

cmake -S "$BEXT_SRC" -B "$BEXT_BUILD" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX="$BEXT_OUT"

# make sure the copy-only edits are present in the freshly configured tree
bash "$BRLCAD_SRC/misc/termux/bext-extra-edits.sh"

# deps are built as ExternalProjects; a single failure aborts the rest,
# so re-run / use -k until it converges
cmake --build "$BEXT_BUILD" --parallel 6
```

Everything lands in `$BEXT_OUT/install`.

---

## 5. Configure and build BRL-CAD

The most important pitfall: **do not let `--sysroot=/data/data/com.termux/files/usr`
into the flags.** It collapses clang's libc++ search paths and you get
`fatal error: 'algorithm' file not found` even for trivial C++ — this breaks the
bext build immediately. Termux's clang already knows its own include/lib dirs.

```bash
cd "$BRLCAD_BUILD"
unset CFLAGS CXXFLAGS SYSROOT
export CFLAGS="--target=aarch64-linux-android36"
export CXXFLAGS="$CFLAGS"

cmake "$BRLCAD_SRC" \
  -DCMAKE_BUILD_TYPE=Release \
  -DBRLCAD_EXT_DIR="$BEXT_OUT" \
  -DBRLCAD_ENABLE_STRICT=OFF \
  -DBRLCAD_BUNDLED_LIBS=AUTO \
  -DBRLCAD_ENABLE_X11=ON \
  -DBRLCAD_ENABLE_OPENGL=ON \
  -DBRLCAD_ENABLE_TCL=ON \
  -DBRLCAD_ENABLE_TK=ON \
  -DXOPENGL_INCLUDE_DIR_GL=$PREFIX/include \
  -DXOPENGL_INCLUDE_DIR_GLX=$PREFIX/include \
  -DXOPENGL_gl_LIBRARY=$PREFIX/lib/libOpenGL.so \
  -DXOPENGL_gldispatch_LIBRARY=$PREFIX/lib/libGLdispatch.so \
  -DXOPENGL_glu_LIBRARY=$PREFIX/lib/libGLU.so \
  "-DCMAKE_EXE_LINKER_FLAGS=-landroid-shmem -lfreetype" \
  "-DCMAKE_SHARED_LINKER_FLAGS=-landroid-shmem -lfreetype" \
  -DBRLCAD_ENABLE_QT=OFF \
  -DBRLCAD_ENABLE_GDAL=OFF \
  -DBRLCAD_ENABLE_OPENMESH=OFF \
  -DBRLCAD_ENABLE_STEP=OFF \
  -DBRLCAD_ENABLE_OPENVDB=OFF \
  -DBRLCAD_ENABLE_OSG=OFF \
  -DBRLCAD_ENABLE_APPLESEED=OFF \
  -DBRLCAD_ENABLE_OPENCL=OFF \
  -DBRLCAD_ENABLE_MANPAGES=OFF \
  -DBRLCAD_ENABLE_HTML_MANPAGES=OFF

make -j6            # or: cmake --build . --parallel 6
```

Optional install (default prefix is `/usr/brlcad/rel-7.42.1`; pass
`-DCMAKE_INSTALL_PREFIX=$PREFIX` if you prefer):

```bash
make install
```

### Smoke test

One command exercises the whole stack — model creation, ray tracing and STL
export — on **two** CSG models:

```bash
BRLCAD_BUILD="$BRLCAD_BUILD" bash "$BRLCAD_SRC/misc/termux/smoke-test.sh"
```

The script (for users and agents alike) is fully asserted and exits non-zero on
any failure.  For each model it:

1. **Builds** the region `demo.r` with `mged`.
2. **Renders** one 64×64 frame with `rt` and requires rays to be traced.
3. **Exports ASCII STL** with `g-stl` and checks the `solid` / `endsolid`
   framing and the `facet normal` count.
4. **Exports binary STL** with `g-stl -b`, reads the triangle count from byte
   offset 80 and asserts `file size == 84 + triangles * 50` (also that it
   matches the ASCII facet count).

The two models:

| Case | Region | Construction | Triangles |
|---|---|---|---|
| `dimple` | `demo.r = box - ball` | cube 1000³ mm minus a sphere of radius 300 mm centred on the top face (`make -o 0 0 500 -s 600 ball sph`) — a **visible** hemispherical dimple | 182 |
| `through-hole` | `demo.r = box - cyl` | cube 1000³ mm minus an RCC base `(0,0,-500)`, height `(0,0,1000)`, radius 200 mm (`in cyl rcc …`) — a hole all the way through | 112 |

> A sphere that is merely *inscribed* in the cube (radius 500) leaves an
> entirely internal cavity: the STL is geometrically correct, but from the
> outside you only see a plain cube.  Move the cut tool so the cut opens up
> (a face dimple) or goes all the way through (through-hole).

Expected tail:

```
SMOKE TEST PASSED
  dimple       : 182 triangles
  through-hole : 112 triangles
```

If you only want a quick manual check without STL export:

```bash
export LD_LIBRARY_PATH="$BRLCAD_BUILD/lib:$LD_LIBRARY_PATH"
printf 'make sph sph\nls\nquit\n' | "$BRLCAD_BUILD/bin/mged" -c "$HOME/tmp/t.g"
"$BRLCAD_BUILD/bin/rt" -s 64 -p 0 -o "$HOME/tmp/t.pix" "$HOME/tmp/t.g" sph
```

Expected: `mged` prints `sph`; `rt` reports `4096 rays` and writes a non-empty
`t.pix`. Use a writable path such as `$HOME/tmp` (or Termux's `$TMPDIR`) —
`/tmp` is not a normal writable directory on Termux.

---

## 6. Command-line / TUI workflow

BRL-CAD is fully usable on Termux with **no GUI at all** — this is the most
reliable and lowest-power way to use it on a phone.  The GUI (§8) is optional.
The recommended setup composes three layers:

| Layer | Tool | Role |
|---|---|---|
| Substrate | `tmux` + `termux-wake-lock` | keeps sessions alive across app restarts |
| Domain | `mged -c` / `mged -p`, `bwish` | geometry work in text / Tcl |
| Automation | an agent TUI (e.g. `pi`) or shell scripts | edits, builds, diagnosis |

### 6.1 Persistent session

```bash
pkg install tmux termux-api    # termux-api provides termux-wake-lock
termux-wake-lock               # stop Android from killing the background server
tmux new -s brlcad
```

A ready-made version of this layout ships in the branch:

```bash
# one-time: adopt the bundled config, then restart the tmux server
cp "$BRLCAD_SRC/misc/termux/tmux.conf" ~/.tmux.conf

# start (or re-attach to) the work session
BRLCAD_BUILD="$BRLCAD_BUILD" [BRLCAD_DB=model.g] [START_AGENT=1] \
  bash "$BRLCAD_SRC/misc/termux/start-work.sh"
```

For a one-command launcher, install the bundled shim once and then just call
`brlcad-tui` (it auto-detects the checkout and the configured build directory):

```bash
bash "$BRLCAD_SRC/misc/termux/brlcad-tui" --install   # symlink into $PREFIX/bin

brlcad-tui                    # start / re-attach the work session
brlcad-tui model.g            # ... and open model.g in the mged window
```

`start-work.sh` acquires the wake lock, creates the `agent` / `mged` / `build`
windows, pipes the build window to `~/brlcad-logs/<session>-build.log`, and
attaches; re-running it just re-attaches.  Release the lock when you are done
with `termux-wake-unlock`.

Create windows manually with `Ctrl-b c` and switch with `Ctrl-b 0/1/2`:

| Window | Command | Purpose |
|---|---|---|
| `mged` | `mged -c model.g` | interactive Tcl/mged console |
| `build` | `make -j6 2>&1 \| tee make.log` | long build + log |
| `agent` | `pi` | automation / code editing |

Detach with `Ctrl-b d`; reconnect with `tmux attach -t brlcad`.  If the Termux
app itself is restarted, the tmux server and its windows survive — far more
robust than a foreground process.

### 6.2 The mged text interface

* `mged -c db.g` — classic text-only mode: a Tcl REPL with all mged commands.
* `mged -p db.g` — **pipe mode**: emits `CMD_DONE` sentinels after each command,
  so a script or an agent can drive mged deterministically (a text "API").
* `--rcfile FILE`, `--set VAR=VALUE`, `--rset ...` — reproducible startup; or a
  `~/.mgedrc` for personal defaults.
* `bwish`, or `tclsh` with the BRL-CAD Tcl packages, for standalone scripts.

Example batch run (no GUI):

```bash
printf 'make sph sph\nr demo.r u sph\nquit\n' | mged -c demo.g
```

### 6.3 Long builds that survive a crash

A background build must not depend on the foreground TUI staying alive
(sessions *do* die occasionally).  Run it detached and write a sentinel when it
finishes:

```bash
setsid bash -c 'make -j6 > make.log 2>&1; echo MAKE_EXIT=$? >> make.log' &
# later, even after a crash or reattach:
grep -q MAKE_EXIT make.log && tail -3 make.log || echo "still running"
```

Use `timeout` around individual steps so a hung tool cannot stall the job
forever, and check `make.log` for errors before assuming success.

### 6.4 Running an agent TUI (pi) inside tmux

tmux can report `Shift+Enter`, `Ctrl+Enter` and `Enter` as the same key.  Enable
extended keys so the agent can distinguish them (see the agent's own `tmux`
docs):

```tmux
set -g extended-keys on
set -g extended-keys-format csi-u
```

Then restart the tmux server.  On Android, clipboard-text integration needs the
Termux:API app plus `pkg install termux-api`; shared storage needs
`termux-setup-storage`.

### 6.5 When to use what

| Need | Use |
|---|---|
| move/edit geometry, export STL | `mged -c` (interactive) or `mged -p` (scripts) |
| run a full build / smoke test | detached `setsid` job + sentinel log |
| edit sources, manage the port | the agent TUI, inside tmux |
| a 3D viewport | Termux:X11 GUI (§8) — optional |

---

## 7. Pitfall → cause → fix

| Symptom | Cause | Fix |
|---|---|---|
| `'algorithm' file not found` in bext | `--sysroot=$PREFIX` in `CFLAGS`/`CXXFLAGS` | `unset CFLAGS CXXFLAGS SYSROOT`; use `--target=aarch64-linux-android36` |
| Build **hangs forever** at `Generating … .g`, 0 % CPU | bionic `pthread_mutex_t` is 4-byte aligned → `struct bu_semaphores` is 4-aligned → `BU_CKMAG()` false `mis-aligned` error → `bu_bomb()` → recursive futex deadlock | `src/libbu/semaphore.c` forces 8-byte alignment (**already in this branch**) |
| `undefined reference to shmget/shmat` | bionic has no SysV shared memory | link `libandroid-shmem` (**already in this branch** + linker flags) |
| `fuzz_*` link errors, `make all` fails | fuzzer runtime not available for this clang/bionic | `regress/fuzz` disabled (**already in this branch**) |
| assimp / geogram / stepcode / utahrle / opennurbs compile errors | newer stricter clang, x86-only flags, missing `strings.h`, NDK-only paths | bext patches in `misc/termux/patches/` |
| `mged`/`rt` cannot find `.so` at runtime | Termux has no `ldconfig` | `export LD_LIBRARY_PATH=$BRLCAD_BUILD/lib` (or install and use its lib dir) |
| `undefined symbol: modf` (or any math symbol) linking a tool | upstream moved `M_LIBRARY` from public `BU_LIBS` to private `BU_PRIVATE_LIBS`, and bionic keeps libm separate (unlike glibc ≥ 2.34) | `src/libbu/CMakeLists.txt` exposes `m` via `PUBLIC_LIBS` (**already in this branch**) |
| `undefined symbol: FT_*` linking a static OpenNURBS consumer | static `libopennurbsStatic.a` references FreeType, which is not implicit on Android | `misc/CMake/FindOPENNURBS.cmake` adds FreeType to `OPENNURBS::OPENNURBS-static` deps (**already in this branch**) |
| `fatal error: 'tiny_gltf_v3.h' file not found` | BRL-CAD moved to tinygltf v3; the pinned bext submodule is v2 | update the `tinygltf` submodule to `release` head (§3c) + apply `02-bext-drivers.patch` |

---

## 8. GPU / OpenGL / Vulkan (optional)

Hardware acceleration on a stock, un-rooted Android is the hardest part. Findings
on the test device:

* **Vendor OpenCL does not load.** `/vendor/lib64/libOpenCL_adreno.so` is blocked by
  the Android linker namespace (`/vendor/lib64` is not in `permitted_paths`), and a
  symlink from `$PREFIX/lib` does **not** help — the linker resolves the real path.
  The only workaround is `LD_LIBRARY_PATH=/vendor/lib64`, which the namespace check
  honours. This is why BRL-CAD is configured with `-DBRLCAD_ENABLE_OPENCL=OFF`.
* **Mesa Turnip provides real Vulkan.** `/dev/kgsl-3d0` is world-writable (`0666`)
  and Mesa's freedreno/Turnip driver talks to the GPU through kgsl UAPI directly:
  ```bash
  pkg install mesa-vulkan-icd-freedreno vulkan-tools
  vulkaninfo --summary      # -> Adreno (TM) 830, driverName = turnip Mesa driver
  ```
* **Zink provides hardware OpenGL/GLES over Vulkan:**
  ```bash
  EGL_PLATFORM=surfaceless GALLIUM_DRIVER=zink <app>
  # GL_RENDERER = zink Vulkan ... (Adreno (TM) 830 (MESA_TURNIP))
  # GL_VERSION  = OpenGL ES 3.2 Mesa ...
  ```
* The `mged` **GUI** still needs an X server (Termux:X11 / VNC) plus a GL stack.
  The CLI (`mged -c`, `rt`) is fully headless and needs none of it.

---

## 9. Files in this branch

```
TERMUX.md                         this guide (users)
misc/termux/AGENT.md              deterministic guide (agents/automation)
misc/termux/smoke-test.sh         asserted model → render → STL smoke test
misc/termux/tmux.conf             persistent-session tmux config (extended keys)
misc/termux/start-work.sh         wake-lock + agent/mged/build tmux session
misc/termux/brlcad-tui            one-command launcher (installs into $PREFIX/bin)
misc/termux/bext-extra-edits.sh   copy-only bext edits (idempotent)
misc/termux/patches/              bext patches (01 is the BRL-CAD tree, for other checkouts)
```

These are deliberately **Android-specific workarounds** and are not intended for
upstreaming; use them on a local checkout / this branch.
