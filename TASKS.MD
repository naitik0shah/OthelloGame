# Team Task Allocation & Roadmap

## 1. Module Division & Team Assignments

### Module 1: CLI Interface & Input Mode Management

**Members**: Akshar, Ojas

- [ ] Implement string command parser for prefix commands (`:n`, `:l`, `:s`, `:v`, `:h`, `:q`).
- [ ] Parse flag arguments (`-p1=...`, `-p2=...`, `-w=...`, `-f=...`).
- [ ] Create interactive input screens when flags are omitted.
- [ ] Handle keyboard cursor positioning (e.g., `h3`/`H3` direct cursor jumps).
- [ ] Build ASCII 8x8 Board renderer with column headers (A-H) and row numbers (1-8).
- [ ] Render side panel displaying Move History and Turn Status.
- [ ] Integrate ANSI escape codes for red error messages and highlighted text.

### Module 2: Move Validation & Valid Move Hinting

**Members**: Aniruddh, Maulik, Naitik

- [ ] Implement `validate_move()` logic for a targeted cell.
- [ ] Implement `get_all_valid_moves()` loop to populate valid board matrix `*` hints.
- [ ] Handle zero-valid-move edge cases (turn auto-skip signaling or game ended).
- [ ] Implement `/` (forward) and `\` (backward) replay mode step navigation for `:v`.

### Module 3: Piece Capturing & Board State Flip Logic

**Members**: Aarav, Nishi, Prem

- [ ] Implement directional ray-casting algorithms (8 directions: N, NE, E, SE, S, SW, W, NW).
- [ ] Implement piece capturing using ray-casting.
- [ ] Implement piece flipping along validated rays upon piece placement.
- [ ] Update `history` array with move positions and active player.
- [ ] Implement game-over condition checker (board full or both players blocked).
- [ ] Handle turn state transitions and piece counters (Black score vs White score).

### Module 4: Game Persistence, Statistics & Utilities

**Members**: Manthan, Yug

- [ ] Implement file serialization (`save_game`) to write `GameState` to file.
- [ ] Implement file deserialization (`load_game`) to restore `GameState` from file.
- [ ] Build statistics summary generator (`:s file1 file2...`) printing win/loss ratios and high scores.
- [ ] Validate file paths and error-handling for missing/corrupted files.

---

## 2. Project Milestone Roadmap

```text
Phase 1: Common Headers & Setup (Days 1 - 2)
 └── Set up repository, makefile, and common types (othello_types.h).

Phase 2: Core Logic & Validation (Days 3 - 5)
 └── Modules 2 & 3 complete ray-casting validation and flipping logic.

Phase 3: CLI Parser & TUI Rendering (Days 6 - 8)
 └── Modules 1 & 4 implement terminal UI, command parsing, and board display.

Phase 4: Save/Load & Stats Aggregation (Days 9 - 10)
 └── Module 5 completes persistence engine and stats summary generator.

Phase 5: Integration, Replay Mode & Testing (Days 11 - 12)
 └── Full integration, edge case testing, and project documentation review.
```
