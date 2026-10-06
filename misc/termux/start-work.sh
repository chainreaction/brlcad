#!/data/data/com.termux/files/usr/bin/bash
# ---------------------------------------------------------------------------
# start-work.sh — start (or re-attach to) a persistent BRL-CAD work session.
#
# Creates one tmux session with three windows:
#
#   agent  a shell for an agent TUI (e.g. pi) or general editing
#   mged   an interactive mged text console (opens a database if given)
#   build  a shell in the build directory, its output piped to a log file
#
# Usage:
#   BRLCAD_BUILD=/path/to/build [BRLCAD_DB=model.g] bash misc/termux/start-work.sh
#
# Environment:
#   SESSION       tmux session name                (default: brlcad)
#   BRLCAD_BUILD  build dir containing bin/        (default: ${BRLCAD_SRC:-$HOME/brlcad/brlcad}/build)
#   BRLCAD_DB     geometry database to open in mged (optional)
#   START_AGENT   1 launches the agent TUI          (default: 0, plain shell)
#   AGENT_CMD     agent command                     (default: pi)
#   LOG_DIR       directory for the build log       (default: $HOME/brlcad-logs)
# ---------------------------------------------------------------------------
set -euo pipefail

: "${SESSION:=brlcad}"
: "${BRLCAD_BUILD:=${BRLCAD_SRC:-$HOME/brlcad/brlcad}/build}"
: "${START_AGENT:=0}"
: "${AGENT_CMD:=pi}"
: "${LOG_DIR:=$HOME/brlcad-logs}"

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
tmux new-session -d -s "$SESSION" -n agent -c "$PWD"
tmux setenv -g LD_LIBRARY_PATH "$LD_LIBRARY_PATH"
tmux setenv -g BRLCAD_BUILD "$BRLCAD_BUILD"
if [ "$START_AGENT" = "1" ]; then
    tmux send-keys -t "$SESSION:agent" "$AGENT_CMD" C-m
fi

# window 2: mged text console
tmux new-window -t "$SESSION" -n mged -c "$BRLCAD_BUILD"
if [ -n "${BRLCAD_DB:-}" ]; then
    tmux send-keys -t "$SESSION:mged" "'$BRLCAD_BUILD/bin/mged' -c '$BRLCAD_DB'" C-m
else
    tmux send-keys -t "$SESSION:mged" \
        "echo 'start mged with: $BRLCAD_BUILD/bin/mged -c <db.g>'" C-m
fi

# window 3: build shell, every keystroke/output also appended to the log
tmux new-window -t "$SESSION" -n build -c "$BRLCAD_BUILD"
tmux pipe-pane -t "$SESSION:build" -o "cat >> '$BUILD_LOG'"
tmux send-keys -t "$SESSION:build" \
    "echo 'build dir: $BRLCAD_BUILD'; echo 'log: $BUILD_LOG'; echo 'build: make -j6 (see TERMUX.md 6.3 for detached builds)'" C-m

tmux select-window -t "$SESSION:agent"
exec tmux attach -t "$SESSION"
