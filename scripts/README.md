# Workflow scripts

Bash. Run from any directory; each script finds the project itself.

| Script | Does |
|---|---|
| `env.sh` | Sourced by the others. Loads `../local.env` without overriding variables already set; relative values resolve against the project root. |
| `build-abiv11.sh` | The test program for ABIv11 x86_64, with a check for unresolved symbols. |
| `build-tests.sh` | The toolbar, image view, drag-out and table test programs for `v1`, `v11` or `pi`, with warnings as errors and a check for unresolved symbols. |
| `test-host.sh` | Builds and runs the host tests of the components' pure logic. |
