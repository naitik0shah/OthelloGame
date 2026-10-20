# Othello Game in C (C17 Standard)

An interactive, terminal-based implementation of the classic board game **Othello (Reversi)** written in C17. Built as a collaborative project by a team of 10 engineering students, featuring Vi-like command modes, dynamic move validation, full move replay, and game statistics.

- **Repository**: [https://github.com/naitik0shah/OthelloGame](https://github.com/naitik0shah/OthelloGame)
- **Language**: C (C17 Standard)
- **Build System**: GNU Make

---

## Technical Prerequisites & Setup Instructions

### 1. Prerequisites by Operating System

#### **Linux (Ubuntu / Debian / Fedora)**

Install `gcc`, `make`, and `git`:

```bash
# Ubuntu/Debian
sudo apt update
sudo apt install build-essential git

# Fedora
sudo dnf groupinstall "Development Tools"
```

#### **macOS**

Install Xcode Command Line Tools and Git:

```bash
xcode-select --install
```

*Note: Xcode provides `clang` which natively supports standard C17 flags (`-std=c17`).*

#### **Windows**

We recommend using **MinGW-w64** via **MSYS2** or **VS Code with GCC**.

1. Download and install [MSYS2](https://www.msys2.org/).
2. Open the MSYS2 UCRT64 terminal and run:

   ```bash
   pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-make git
   ```

3. Add `C:\msys64\ucrt64\bin` to your Windows environment `PATH`.
4. (Optional) In VS Code, install the **C/C++ Extension Pack**.

---

## Building and Running

### Clone the Repository

```bash
git clone https://github.com/naitik0shah/OthelloGame.git
cd OthelloGame
```

### Compilation Commands

```bash
# Build the project executable
make

# Run the game
make run

# Clean build artifacts
make clean

# Run tests (if configured)
make test
```

*Note on Windows using MinGW:* Use `mingw32-make` if `make` is not aliased.

---

## Controls & Command Reference

The application operates in two primary input modes: **Play Mode** and **Command Mode** (accessed via `:`).

### Command Mode Shortcuts (Prefix `:` + Enter)

| Command | Description |
| :--- | :--- |
| `:n` | Start a new game (prompts interactively for player names and color selection). |
| `:n -p1="Alice" -p2="Bob" -w=p1` | Quick-start new game with parameters (`-w` sets white player). |
| `:l -f="<path>"` | Load game state from file. |
| `:s -f="<path>"` | Save current game state to file. |
| `:s <file1> <file2> ...` | Aggregate statistics summary table from save files. |
| `:v -f="<path>"` | View/replay a recorded game step-by-step. |
| `:h` | Display help screen with command listings. |
| `:q` | Quit application. |

### Gameplay Controls

- **Coordinate Entry**: Type `e3` or `E3` to move selection cursor directly, clicking on the cell also selects the clicked cell.
- **Enter Key**: Place piece at targeted square.
- **Valid Move Markers**: `*` indicates a legal move square (in Standard Display Mode).
- **Replay Controls (View Mode)**: Use `/` for forward step and `\` (or `p`) for backward step.
