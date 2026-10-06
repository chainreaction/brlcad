# Workspace rules

This directory is a BRL-CAD work session scaffolded by `brlcad-tui`. It runs in
a tmux session with three windows: `agent`, `mged`, and `build`.

## Route everything through the running mged

By default, every request that touches geometry, models, rendering, export, or
BRL-CAD tooling must go through the **already-running `mged`** in the `mged`
window. Do not spawn a new `mged`, `rt`, `g2asc`, etc. unless that window is
gone or the user explicitly asks for a separate process.

### Locate the pane

```bash
tmux list-panes -a -F '#{pane_id} #{pane_current_command}'
```

Pick the pane whose command is `mged`. Resolve it each time; never hardcode a
`%N` pane id.

### Send commands and read output

```bash
tmux send-keys -t <pane> -l '<command>'   # literal text
tmux send-keys -t <pane> Enter            # submit
tmux capture-pane -t <pane> -p            # read the screen
tmux capture-pane -t <pane> -p -S -200    # read scrollback
```

### Layout

| Path | Holds |
|---|---|
| `models/` | geometry databases (`*.g`) |
| `exports/` | STL / STEP and other exports |
| `renders/` | raytraced images |
| `logs/` | session and build logs |

### Cautions

- It is a live interactive session: everything typed appears there.
- Prefer read-only commands; confirm before destructive edits (deleting or
  overwriting objects).
- The interactive Tcl in this build does **not** perform `[...]` command
  substitution (`$var` and `{...}` work). Avoid square brackets, or use mged
  commands directly.

### If no mged window exists

Tell the user instead of silently starting a new process.
