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
| `scripts/` | generators and other scripts, committed to git |
| `logs/` | session and build logs, `notes.md` |

### Cautions

- It is a live interactive session: everything typed appears there.
- Prefer read-only commands; confirm before destructive edits (deleting or
  overwriting objects).
- Match panes strictly on `#{pane_current_command}`. A failed match leaves
  `-t` empty, and `capture-pane -t ""` then dumps the agent's own screen
  instead of failing.

### Scripting mged with Tcl

Tcl in mged is complete; only the interactive prompt mangles it. Every line
read at `mged>` passes through the legacy object-name globber
(`glob_compat_mode`, on by default), which escapes `[` — the character is a
class in glob patterns:

| Typed at `mged>` | Result |
|---|---|
| `set a [expr 1+1]` | `wrong # args: should be "set varName ?newValue?"` |
| `puts [expr 2*3]` | `can not find channel named "[expr"` |
| `set glob_compat_mode 0` first | `2` — full Tcl |

A file loaded with `source` goes straight to `Tcl_EvalFile` and is unaffected,
so **write scripts into the workspace and `source` them**; never inline
`[...]` at the prompt. Note that `glob_compat_mode 0` also disables `*` and
`[...]` as object-name patterns (`erase *` stops working) — escape instead
(`\[...\]`) or set it back.

Worth knowing before it costs time:

- An uncaught error aborts the rest of a sourced file; wrap risky parts in `catch`.
- BOT attributes are `V` and `F`; `get <bot> vertices` fails.
- `in {*}$toks` works, so a Tcl generator can build geometry with no `eval`
  and no temporary file.
- `/tmp` is not writable; use `$TMPDIR` or the workspace for temporaries.

### Rendering

Render from the live `mged`, not from a bare `rt`: standalone `rt` does not
frame the object, which can end up as a couple of percent of the image.

```tcl
draw <object>
ae 35 25; autoview; zoom 1.85
rt -R -s 800 -o renders/x.png
```

A `.pix` output is a **headerless** raw RGB raster, so `pix-png` has to guess
its size. `rt` writes a real PNG when the output name ends in `.png`; prefer
that, and use `-s N` if a `.pix` is unavoidable.

### If no mged window exists

Tell the user instead of silently starting a new process.

## Keep the workspace self-contained

The machine can take the session down without warning. Anything worth keeping
must already be in this directory:

- generators and scripts in `scripts/`, committed to git;
- findings and decisions appended to `logs/notes.md` as they happen;
- nothing that would have to be re-derived from a chat transcript after a
  restart.
