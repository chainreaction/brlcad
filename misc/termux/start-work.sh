#!/data/data/com.termux/files/usr/bin/bash
# ---------------------------------------------------------------------------
# start-work.sh — start (or re-attach to) a persistent BRL-CAD work session.
#
# Creates one tmux session rooted in a workspace directory that holds every
# artifact of the session, with three windows:
#
#   agent  a shell for an agent TUI (e.g. pi) or general editing
#   mged   an interactive mged text console (opens a database if given)
#   build  a shell in the workspace, its output piped to a log file
#
# Usually you do not call this directly -- `brlcad-tui` creates the workspace
# and sets the variables below.  To drive it by hand:
#
#   PROJECT_DIR=/path/to/workspace BRLCAD_BUILD=/path/to/build \
#     bash misc/termux/start-work.sh
#
# Environment:
#   SESSION       tmux session name                (default: brlcad)
#   PROJECT_DIR   workspace / window working dir   (default: $PWD)
#   BRLCAD_BUILD  build dir containing bin/        (default: ${BRLCAD_SRC:-$HOME/brlcad/brlcad}/build)
#   BRLCAD_DB     geometry database to open in mged (optional)
#   START_AGENT   1 launches the agent TUI          (default: 0, plain shell)
#   AGENT_CMD     agent command                     (default: pi)
#   LOG_DIR       directory for the build log       (default: $PROJECT_DIR/logs)
# ---------------------------------------------------------------------------
set -euo pipefail

: "${SESSION:=brlcad}"
: "${PROJECT_DIR:=$PWD}"
: "${BRLCAD_BUILD:=${BRLCAD_SRC:-$HOME/brlcad/brlcad}/build}"
: "${START_AGENT:=0}"
: "${AGENT_CMD:=pi}"
: "${LOG_DIR:=$PROJECT_DIR/logs}"

PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
export LD_LIBRARY_PATH="$BRLCAD_BUILD/lib:$PREFIX/lib:${LD_LIBRARY_PATH:-}"

command -v tmux >/dev/null || { echo "tmux not found: pkg install tmux" >&2; exit 1; }

# Keep Android from killing the tmux server while work is in progress.
if command -v termux-wake-lock >/dev/null; then
    termux-wake-lock || true
fi

# Re-attach if the session is already around.
if tmux has-session -t "$SESSION" >/dev/null 2>&1; then
    exec tmux attach -t "$SESSION"
fi

mkdir -p "$LOG_DIR"
BUILD_LOG="$LOG_DIR/$SESSION-build.log"

# window 1: agent / editor.  This also starts the tmux server; it inherits our
# exported environment.  Publish the paths too, so a pre-existing server picks
# them up for the remaining windows.
tmux new-session -d -s "$SESSION" -n agent -c "$PROJECT_DIR"
tmux setenv -g LD_LIBRARY_PATH "$LD_LIBRARY_PATH"
tmux setenv -g BRLCAD_BUILD "$BRLCAD_BUILD"
tmux setenv -g PROJECT_DIR "$PROJECT_DIR"
if [ "$START_AGENT" = "1" ]; then
    tmux send-keys -t "$SESSION:agent" "$AGENT_CMD" C-m
fi

# window 2: mged text console
tmux new-window -t "$SESSION" -n mged -c "$PROJECT_DIR"
if [ -n "${BRLCAD_DB:-}" ]; then
    tmux send-keys -t "$SESSION:mged" "'$BRLCAD_BUILD/bin/mged' -c -a nu '$BRLCAD_DB'" C-m
else
    tmux send-keys -t "$SESSION:mged" \
        "echo 'start mged with: $BRLCAD_BUILD/bin/mged -c -a nu <db.g>'" C-m
fi

# window 3: workspace shell.  Every keystroke and all output is appended to the
# log, and the window opens with a coloured "how to work here" hint.
print_build_hint() {
    local hint="$LOG_DIR/.build-hint"
    {
        printf '\n'
        printf '  \033[1;36m══ BUILD ══\033[0m  \033[36mworkspace shell\033[0m\n'
        printf '  \033[2m────────────────────────────────────────────\033[0m\n'
        printf '  \033[1;33mworkspace\033[0m  %s\n' "$PROJECT_DIR"
        printf '  \033[1;33mlog      \033[0m  %s\n' "$BUILD_LOG"
        printf '             \033[2m(everything here is also appended to the log)\033[0m\n'
        printf '\n'
        printf '  \033[1;33mTypical work\033[0m\n'
        if [ -f "$PROJECT_DIR/Makefile" ] || [ -f "$PROJECT_DIR/makefile" ] || [ -f "$PROJECT_DIR/GNUmakefile" ]; then
            printf '    \033[32mmake\033[0m                    build / render / export this project\n'
            printf '    \033[32mmake check\033[0m              verify the exported STL is printable\n'
        fi
        printf '    \033[32m$BRLCAD_BUILD/bin/mged -c -a nu %s\033[0m\n' "${BRLCAD_DB:-<db.g>}"
        printf '        \033[2minspect geometry; the mged window already has it open\033[0m\n'
        printf '    \033[32m$BRLCAD_BUILD/bin/rt -s 512 -o renders/x.png %s <object>\033[0m\n' "${BRLCAD_DB:-<db.g>}"
        printf '    \033[32m$BRLCAD_BUILD/bin/g-stl -o exports/x.stl %s <object>\033[0m\n' "${BRLCAD_DB:-<db.g>}"
        printf '\n'
        printf '  \033[1;33mtmux\033[0m\n'
        printf '    \033[35mCtrl-b 0\033[0m agent   \033[35mCtrl-b 1\033[0m mged   \033[35mCtrl-b 2\033[0m build\n'
        printf '    \033[35mCtrl-b d\033[0m detach — the session keeps running\n'
        printf '\n'
    } > "$hint"
    # Clear the screen first so the echoed command does not clutter the hint.
    tmux send-keys -t "$SESSION:build" "printf '\\033[H\\033[2J'; cat '$hint'; rm -f '$hint'" C-m
}

tmux new-window -t "$SESSION" -n build -c "$PROJECT_DIR"
tmux pipe-pane -t "$SESSION:build" -o "cat >> '$BUILD_LOG'"
print_build_hint

tmux select-window -t "$SESSION:agent"
exec tmux attach -t "$SESSION"
