# zunekit

Shared components for Zune programs on AROS, built on the standard Zune and
MUI classes that AROS already ships. Each component is compiled into the
program that uses it from one source here; none of them holds application
logic.

Components:

- `zk_table`: a multi-column table on NList.mcc with batched appending,
  multiple selection, Amiga+A and dragging selected rows out to a Wanderer
  drawer window. See [usage](docs/usage.md).
- `zk_steps`: a slider over a few named steps (compression levels and the
  like) on the standard Numeric class; mouse, keys and wheel choose the same
  steps.
- `zk_icon`: a Workbench icon's image (a program's own icon in its window),
  drawn by icon.library with its alpha channel.
- `zk_drop`: the place in a window where files are dropped: a dashed border,
  an arrow and two lines of text.

## Start here

- [Documentation](docs/README.md): what each component does and how to use it.
- [Development](docs/development.md): building and testing.

## Project boundaries

`src/` holds the components, `tests/` programs that exercise them. Fixes that
belong in Zune itself are not made here: they go to AROS as separate patches.
Licence: MIT ([LICENSE](LICENSE)).
