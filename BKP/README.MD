# Othello Game in C (C17 Standard)

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
