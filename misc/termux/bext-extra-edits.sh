#!/data/data/com.termux/files/usr/bin/bash
# ---------------------------------------------------------------------------
# bext-extra-edits.sh
#
# A few bext fixes cannot be expressed as ordinary patches because they apply
# to the *copies* that CMake's ExternalProject_Add(URL <submodule-dir>) makes
# under  <build>/<dep>/<DEP>_BLD-prefix/src/<DEP>_BLD .  Applying to the
# submodule sources as well makes a fresh `cmake -S bext -B bext_build`
# reproduce them automatically; applying to the copies covers a build that is
# already configured.
#
# Idempotent: safe to run as many times as you like.
# ---------------------------------------------------------------------------
set -u

: "${BEXT_SRC:=$HOME/brlcad/bext}"
: "${BEXT_BUILD:=$HOME/brlcad/build/bext_build}"
ROOTS=()
[ -d "$BEXT_SRC" ]   && ROOTS+=("$BEXT_SRC")
[ -d "$BEXT_BUILD" ] && ROOTS+=("$BEXT_BUILD")

if [ "${#ROOTS[@]}" -eq 0 ]; then
    echo "bext-extra-edits: neither BEXT_SRC ($BEXT_SRC) nor BEXT_BUILD ($BEXT_BUILD) exists" >&2
    exit 1
fi

echo "bext-extra-edits: roots = ${ROOTS[*]}"

# --- 1. geogram: stop compiling android_utils.cpp -------------------------
# It pulls in NDK-only headers.  The non-x86 VORPALINE platform does not need it.
while IFS= read -r f; do
    [ -n "$f" ] || continue
    if [ -f "$f" ]; then
        mv "$f" "$f.off"
        echo "  disabled: $f"
    fi
done < <(find "${ROOTS[@]}" -type f -path '*/src/lib/geogram/basic/android_utils.cpp' 2>/dev/null)

# --- 2. geogram: disable the __ANDROID__ special-casing in PoissonRecon -----
while IFS= read -r f; do
    [ -n "$f" ] || continue
    # two known spots; regex keeps us independent of exact line numbers
    perl -0pi -e 's/^#ifdef __ANDROID__$/#if 0/m;
                  s/^#elif defined\(__ANDROID__\)\s*$/#elif 0/m' "$f"
    echo "  patched Geometry.cpp: $f"
done < <(find "${ROOTS[@]}" -type f -path '*/src/lib/geogram/third_party/PoissonRecon/Geometry.cpp' 2>/dev/null)

# --- 3. Mesa/osmesa: bzero()/ffs() need <strings.h> on bionic ---------------
while IFS= read -r f; do
    [ -n "$f" ] || continue
    if ! grep -q '#include <strings.h>' "$f"; then
        sed -i '/#include "imports.h"/a #include <strings.h>' "$f"
    fi
    echo "  patched imports.c:   $f"
done < <(find "${ROOTS[@]}" -type f -path '*/src/main/imports.c' 2>/dev/null)

echo "bext-extra-edits: done"
