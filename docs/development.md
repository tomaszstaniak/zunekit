---
type: guide
updated: 2026-10-21
---

# Development

## Supported scope

| Target | Status |
|---|---|
| AROS x86_64 ABIv11 | built; the test program and the first user (Unarc) run on the ABIv11 2026.09 base and AROS One 1.3 |
| AROS mainline (ABIv1) | not built yet |

## Setup and commands

Copy `local.env.example` to `local.env` and set the toolchain and SDK paths,
then:

    scripts/build-abiv11.sh     # build/abiv11-x86_64/zk-table-test, zk-steps-test

`zk-table-test [rows] [LOG file]` opens a window with a table; the line
under it, and the log file when given, show what the table reported
(selection, Amiga+A, drag start and end).

`zk-steps-test [LOG file]` opens a window with a step slider (2, 3 or 4
steps, chosen with the cycle gadget above it) and a disabled one; the line
under them, and the log file, show each value the slider reported.

A program uses a component by compiling its file from `src/` with its own
sources, at a pinned revision of this repository.

Machine values belong in ignored `local.env`; `scripts/env.sh` loads it
without overriding variables already set, and resolves relative paths from
this directory.
