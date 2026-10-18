# Workflow scripts

Bash. Run from any directory; each script finds the project itself.

| Script | Does |
|---|---|
| `env.sh` | Sourced by the others. Loads `../local.env` without overriding variables already set; relative values resolve against the project root. |
| `build-abiv11.sh` | The test program for ABIv11 x86_64, with a check for unresolved symbols. |
