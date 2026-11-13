# Architecture

- `common.h` / `common.c`: shared types, constants, geometry and text helpers.
- `validation_capture.c`: move validation, piece flipping, turn switching and game state.
- `ui_cli_platform.c`: terminal input/platform handling and board rendering.
- `storage.c`: save/load and persistence format.
- `stats.c`: completed-game statistics.
- `misc.c`: command parsing, application flow, prompts, help and replay.
- `main.c`: program entry point only.

Task ownership boundaries are marked with `// ===== ... TASK` comments where applicable.
