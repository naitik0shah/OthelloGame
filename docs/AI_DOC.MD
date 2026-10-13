Below is a **code-free, implementation-oriented specification** you can use as the master instruction set for the project. It is deliberately detailed because your grading includes how well each member can explain the features, implementation decisions, and use of GenAI.

# Othello Game — GUI Project Specification

## 1. Project Objective

Develop a complete **Othello/Reversi board game with a graphical user interface using C language only**.

The project must:

* Implement the complete rules of Othello.
* Provide an interactive GUI.
* Support two human players.
* Provide clear game-state feedback.
* Detect legal and illegal moves.
* Flip captured pieces correctly.
* Detect passes and game termination.
* Calculate and display scores.
* Provide restart/new-game functionality.
* Maintain a clean separation between game logic, GUI, input handling, and supporting components.
* Compile and run on **Linux, Windows, and macOS**.
* Be sufficiently modular that different team members can independently explain and demonstrate their components.

**No code should be generated at the planning/specification stage.**

---

# 2. Technology Constraint

## Mandatory language

The implementation must use:

> **C only**

Do not implement the project in:

* C++
* Java
* Python
* JavaScript/TypeScript
* C#
* Rust
* etc.

C libraries/frameworks may be used where appropriate for GUI, graphics, input, and platform abstraction, but the actual application logic must be written in C.

## Cross-platform requirement

The project must work on:

* Linux
* Windows
* macOS

The team must explicitly document:

1. Required compiler.
2. Required libraries.
3. Required library versions, if applicable.
4. Build procedure for each OS.
5. Any OS-specific configuration.
6. Any differences in keyboard/mouse/input handling.
7. How the project avoids platform-specific assumptions.

A preferred design is to use a **cross-platform C-compatible GUI/graphics library** rather than writing separate GUI implementations for each operating system.

---

# 3. Core Game Rules

The implementation must follow standard Othello/Reversi rules.

## Board

Use an **8 × 8 board**.

Each cell has exactly one of three states:

* Empty
* Black
* White

The board must initially contain the standard four center pieces:

* Two black pieces.
* Two white pieces.
* Arranged diagonally in the standard starting configuration.

The exact initial configuration must be documented in the report.

---

# 4. Game Model

The game should be represented using an appropriate data structure.

A recommended conceptual model is:

```text
Board
 └── 8 × 8 cells
       ├── EMPTY
       ├── BLACK
       └── WHITE
```

The implementation should also maintain game-state information such as:

* Current player.
* Number of black pieces.
* Number of white pieces.
* Number of empty cells.
* Whether the game is active.
* Whether the previous player passed.
* Whether the game has ended.

Do not duplicate information unnecessarily.

For example, if the number of black pieces can reliably be calculated from the board, the team should consciously decide whether maintaining a separate counter is worthwhile.

If counters are maintained, the team must explain:

* Why they exist.
* When they are updated.
* How consistency is guaranteed.

---

# 5. Recommended Software Architecture

The project should be divided into logical modules rather than placing everything in one large C file.

A possible architecture is:

```text
                ┌─────────────────────┐
                │     GUI / View      │
                └──────────┬──────────┘
                           │
                           ▼
                ┌─────────────────────┐
                │ Input / Controller  │
                └──────────┬──────────┘
                           │
                           ▼
                ┌─────────────────────┐
                │    Game Engine      │
                └──────────┬──────────┘
                           │
                           ▼
                ┌─────────────────────┐
                │   Board / Rules     │
                └─────────────────────┘
```

Additional modules can handle:

* Menu/state management.
* Scoreboard.
* Save/load.
* Configuration.
* Audio.
* Testing/debugging.

The architecture should make it possible to test the game logic without depending entirely on the GUI.

---

# 6. Minimum Five Non-Trivial Components

The assignment requires at least **five different non-trivial components**.

For a team of 9–12 students, a strong decomposition would be the following.

## Component 1 — Board & Game-State Management

Responsible for:

* Board representation.
* Board initialization.
* Resetting the board.
* Current-player management.
* Game-state management.
* Piece counting.
* Detecting full-board conditions.

### Members should be able to explain

* Why the selected board representation was used.
* How coordinates are represented.
* How an individual cell is accessed.
* How initialization works.
* How player turns are represented.
* How state changes propagate through the program.

---

# 7. Component 2 — Move Validation & Othello Rule Engine

This is one of the most important components.

Responsible for determining whether a move is legal.

A move is legal if:

1. The selected cell is empty.
2. Placing the current player's piece there would capture at least one opponent piece.
3. Capture must occur in at least one of the eight possible directions.

The eight directions are:

```text
NW  N  NE
 W  .   E
SW  S  SE
```

The implementation should systematically inspect each direction.

For each direction:

1. Start from the selected cell.
2. Move one cell in that direction.
3. The first cell should contain an opponent piece.
4. Continue in the same direction while opponent pieces continue.
5. Eventually encounter the current player's piece.
6. If this happens, the opponent pieces between the two player's pieces are capturable.

If an empty cell or board boundary is encountered before reaching the current player's piece, that direction does not produce a capture.

### Required functions/concepts

The design should conceptually support operations such as:

* Check whether a move is legal.
* Determine which directions result in captures.
* Determine which pieces will be flipped.
* Find all legal moves for a player.

The exact function names should be chosen by the team during implementation.

### Explanation requirement

Every student responsible for this component should be able to explain:

* Why there are eight directions.
* How boundaries are handled.
* How a valid capture is identified.
* Why an adjacent opponent piece alone is insufficient.
* How multiple directions are handled.
* How corner and edge cases are handled.

---

# 8. Component 3 — Move Execution & Piece Flipping

Move validation and move execution should be logically distinguishable.

Once a legal move has been identified:

1. Place the current player's piece.
2. Identify all captured pieces.
3. Flip every captured opponent piece.
4. Update scores/state.
5. Switch the current player if appropriate.
6. Refresh the GUI.

Example concept:

```text
Before:

. . . . .
. W W W .
. . B . .
. . . . .

Black plays at the appropriate position.

After:

. . . . .
. B B B .
. . B . .
. . . . .
```

The implementation must correctly support:

* One-direction captures.
* Multiple-direction captures.
* Long capture chains.
* Captures at edges.
* Captures near corners.
* Simultaneous captures in several directions.

### Important design principle

Do not write eight separate pieces of almost-identical logic if a direction-based algorithm can handle all eight directions.

The team should be prepared to explain the algorithmic reason for its implementation.

---

# 9. Component 4 — GUI & Rendering

The GUI must make the game visually understandable.

At minimum, it should display:

* Othello board.
* Grid lines.
* Black pieces.
* White pieces.
* Empty cells.
* Current player.
* Black score.
* White score.
* Game status.
* New Game/Restart control.
* Exit control.

A stronger GUI can additionally display:

* Highlighted legal moves.
* Last move.
* Captured pieces.
* Turn indicator.
* Game-over dialog.
* Winner announcement.
* Help/rules screen.

## GUI requirements

The interface should clearly distinguish:

```text
BOARD
┌───┬───┬───┬───┬───┬───┬───┬───┐
│   │   │   │   │   │   │   │   │
├───┼───┼───┼───┼───┼───┼───┼───┤
│   │   │   │   │   │   │   │   │
├───┼───┼───┼───┼───┼───┼───┼───┤
│   │   │   │   │   │   │   │   │
...
└───┴───┴───┴───┴───┴───┴───┴───┘

Black: XX
White: XX

Current Turn: Black
```

The GUI must not contain the core Othello rules directly wherever possible.

Instead:

```text
Mouse Click
     ↓
GUI converts screen coordinate → board coordinate
     ↓
Game Engine validates move
     ↓
Game Engine updates board
     ↓
GUI redraws board
```

This separation is important for both maintainability and presentation.

---

# 10. Component 5 — Input Handling & Game Controller

This component connects the GUI to the game engine.

It should handle:

* Mouse clicks.
* Button clicks.
* Menu actions.
* Restart requests.
* Exit requests.
* Potential keyboard shortcuts.

For a board click, the controller must convert:

```text
Screen coordinates
        ↓
Board coordinates
        ↓
Requested move
```

For example, if the board occupies a rectangular region of the window, the implementation should mathematically determine:

```text
column = (mouse_x - board_x) / cell_width
row    = (mouse_y - board_y) / cell_height
```

The exact formula will depend on the GUI layout.

### Important

The controller must reject clicks that:

* Are outside the board.
* Select an occupied cell.
* Represent an illegal Othello move.

---

# 11. Component 6 — Turn, Pass & Game-End Management

This is a separate non-trivial feature and can be assigned to additional students.

The program must correctly handle situations where the current player has **no legal move**.

A player with no legal move must pass.

The program should:

1. Detect that the current player has no legal moves.
2. Inform the player.
3. Pass the turn to the opponent.
4. Check whether the opponent has any legal moves.

If **both players have no legal moves**, the game ends.

The game also ends when the board is completely occupied.

The implementation must not assume:

```text
Board full = only possible game-ending condition
```

because a game can end before all 64 cells are occupied.

---

# 12. Component 7 — Scoring & Result System

The scoring component should calculate:

```text
Black score = number of black pieces
White score = number of white pieces
```

The final result must clearly indicate:

* Black wins.
* White wins.
* Draw.

The GUI should display the score during gameplay and the final result when the game ends.

The report should explain whether scores are:

1. Calculated by scanning the board, or
2. Maintained incrementally.

The team must justify the design choice.

---

# 13. Optional Advanced Component — Save/Load

A useful additional feature is game persistence.

The user could:

* Save a game.
* Load a saved game.

The saved state should contain sufficient information to reconstruct the game, such as:

* Board.
* Current player.
* Relevant game-state information.

If implemented, the team must explain:

* File format.
* Serialization.
* Deserialization.
* Error handling.
* Invalid/corrupted save files.
* Cross-platform file handling.

This is an excellent feature for demonstrating file I/O in C.

---

# 14. Optional Advanced Component — Move History

A move-history system could record:

```text
Move 1: Black → D3
Move 2: White → C3
Move 3: Black → C4
...
```

It can be used to display:

* Move number.
* Player.
* Board coordinate.

An advanced version could support:

* Undo.
* Redo.

If undo/redo is implemented, the team must explain the underlying data structure and state management.

---

# 15. Optional Advanced Component — AI Opponent

An AI opponent may be implemented as an extension, but it should **not replace the fundamental two-player implementation**.

Possible difficulty levels:

### Easy

Choose a legal move randomly.

### Intermediate

Prefer moves according to simple heuristics, such as:

* Corners.
* Number of pieces captured.
* Avoiding dangerous squares.

### Advanced

Use a game-tree search algorithm such as:

* Minimax.
* Alpha-beta pruning.

If AI is implemented, it becomes a substantial additional component.

Students responsible for it must be able to explain:

* State representation.
* Legal move generation.
* Evaluation function.
* Search depth.
* Minimax.
* Alpha-beta pruning, if used.
* Computational complexity.
* Why the chosen depth is practical.

---

# 16. Optional Advanced Component — Sound & Visual Feedback

Possible additions:

* Move sound.
* Invalid-move sound.
* Game-over sound.
* Button interaction effects.
* Piece placement animation.
* Flip animation.

These should remain secondary to correctness.

---

# 17. Required Game Flow

The complete game should follow approximately this flow:

```text
PROGRAM START
     ↓
Initialize GUI
     ↓
Initialize game
     ↓
Create standard Othello board
     ↓
Set current player
     ↓
Display board
     ↓
Check legal moves
     ↓
Wait for input
     ↓
User selects cell
     ↓
Convert GUI coordinate → board coordinate
     ↓
Validate move
     │
     ├── INVALID
     │      ↓
     │   Show feedback
     │      ↓
     │   Wait again
     │
     └── VALID
            ↓
       Place piece
            ↓
       Flip captured pieces
            ↓
       Update score/state
            ↓
       Check game end
            │
            ├── YES → Show result
            │
            └── NO
                 ↓
          Switch player
                 ↓
          Check legal moves
                 │
                 ├── Moves exist → Continue
                 │
                 └── No moves
                         ↓
                       Pass
                         ↓
                   Check opponent
                         ↓
                    Continue/end
```

---

# 18. Coordinate-System Design

The team must explicitly define two coordinate systems.

## GUI coordinates

Usually:

```text
(x, y)
```

where:

* `x` increases horizontally.
* `y` increases vertically.

## Board coordinates

For example:

```text
(row, column)
```

where:

```text
row    = 0...7
column = 0...7
```

The conversion between them must be clearly defined.

This prevents a common class of bugs where the visual board and internal board representation disagree.

---

# 19. Boundary Handling

Every direction-based operation must safely handle:

* Top edge.
* Bottom edge.
* Left edge.
* Right edge.
* Corners.

The implementation must never access memory outside the board.

For an 8 × 8 array:

```text
valid row    → 0 through 7
valid column → 0 through 7
```

The team must explain how boundary checking is performed **before** accessing the next board cell.

---

# 20. Legal-Move Highlighting

This is recommended for a polished GUI.

At the beginning of a turn, the program can identify all legal moves and visually mark them.

For example:

```text
● = existing piece
· = legal move
```

The GUI could use:

* Small circles.
* Transparent markers.
* Highlighted cells.

This feature also provides an excellent demonstration of the move-generation algorithm.

---

# 21. Invalid-Move Handling

Invalid input must not modify the board.

Examples:

* Clicking an occupied cell.
* Clicking an empty cell that captures nothing.
* Clicking outside the board.

The program should provide clear feedback such as:

```text
Invalid move.
Select a legal position.
```

The exact wording can be chosen by the team.

---

# 22. Game-End Handling

When the game ends, the GUI should display:

* Final black score.
* Final white score.
* Winner/draw.
* Option to start a new game.
* Option to exit.

Example:

```text
GAME OVER

Black: 34
White: 30

Black wins!

[New Game] [Exit]
```

No further board moves should be accepted until a new game is started.

---

# 23. Error Handling

The project must deliberately handle errors rather than assuming everything succeeds.

Examples:

### GUI initialization failure

Display an appropriate error and terminate cleanly.

### File operation failure

If save/load exists:

```text
Unable to save game.
```

rather than silently failing.

### Invalid input

Reject it without corrupting game state.

### Unexpected state

The program should avoid crashes caused by invalid indexes or malformed data.

---

# 24. Memory Management

Because the project is written in C, memory management must be considered explicitly.

The team must document:

* Which memory is allocated dynamically.
* Why dynamic allocation is necessary.
* Who owns allocated memory.
* Where memory is freed.
* How memory leaks are prevented.

If dynamic allocation is unnecessary for a particular structure, using fixed-size structures may be preferable.

For an 8 × 8 board, the team should be prepared to explain why a statically allocated board may be sufficient.

---

# 25. Modularity Requirements

Avoid creating one enormous file such as:

```text
main.c
```

containing the entire project.

A better conceptual organization could be:

```text
project/
│
├── src/
│   ├── main.c
│   ├── board.c
│   ├── rules.c
│   ├── game.c
│   ├── input.c
│   ├── gui.c
│   ├── score.c
│   └── ...
│
├── include/
│   ├── board.h
│   ├── rules.h
│   ├── game.h
│   ├── input.h
│   ├── gui.h
│   └── ...
│
├── assets/
│   └── ...
│
├── tests/
│   └── ...
│
├── docs/
│   └── ...
│
└── README
```

The exact structure can differ, but responsibilities should remain clearly separated.

---

# 26. Header-File Design

Each module should expose only what other modules actually need.

For example conceptually:

```text
board.h
    ↓
Board-related structures/functions

rules.h
    ↓
Move-validation functions

game.h
    ↓
Game-state functions

gui.h
    ↓
Rendering functions

input.h
    ↓
Input/controller functions
```

Avoid unnecessary global variables.

Students should be able to explain:

* What belongs in a `.h` file.
* What belongs in a `.c` file.
* Why a particular function is exposed publicly.
* Why another function remains internal to a module.

---

# 27. Global Variables

Global mutable state should be minimized.

If global state is used, the report must explain:

* Why it was necessary.
* What data is global.
* Which modules modify it.
* What risks it introduces.

Prefer passing state explicitly where practical.

---

# 28. Testing Requirements

The project should include systematic testing rather than only testing the final GUI manually.

## Unit-level tests

At minimum, test:

### Board initialization

Verify the four starting pieces.

### Legal move detection

Test:

* Horizontal capture.
* Vertical capture.
* Diagonal capture.
* Multiple-direction capture.
* Illegal move.
* Occupied-cell move.

### Boundary conditions

Test:

* Corners.
* Top edge.
* Bottom edge.
* Left edge.
* Right edge.

### Piece flipping

Verify that exactly the correct pieces are flipped.

### Turn management

Verify:

* Normal turn switching.
* Pass.
* Consecutive no-move conditions.

### Scoring

Verify scores after known board configurations.

### Game termination

Test:

* Full board.
* No legal moves for both players.

---

# 29. Testing Strategy

For every important function, document:

```text
Input
Expected output
Actual output
Pass/Fail
```

A test case should ideally be reproducible.

For example:

```text
Test: Horizontal capture

Initial board:
...

Move:
Black at E4

Expected:
Pieces B4, C4, D4 become Black

Result:
PASS
```

The report should contain representative test cases rather than merely stating:

> "The program was tested and works."

---

# 30. Cross-Platform Testing

The team should actually test the program on the required operating systems where possible.

Document:

| Platform | Compiler | Build Method | Result |
| -------- | -------- | ------------ | ------ |
| Linux    | ...      | ...          | Pass   |
| Windows  | ...      | ...          | Pass   |
| macOS    | ...      | ...          | Pass   |

Do not claim successful testing on a platform that was not actually tested.

If a platform could not be tested, state that honestly and document the compatibility design.

---

# 31. Build System

The project should have a reproducible build process.

A suitable approach could involve a cross-platform build system or clearly documented platform-specific build commands.

The project should ideally provide:

```text
README
    ↓
Prerequisites
    ↓
Installation
    ↓
Build
    ↓
Run
    ↓
Test
```

A new user should be able to understand how to build the project without asking the team for undocumented steps.

---

# 32. GUI Library Documentation

The final report must explicitly identify the GUI/graphics library used.

Explain:

1. Why the library was selected.
2. How it works with C.
3. Supported operating systems.
4. How the project initializes it.
5. How windows are created.
6. How drawing is performed.
7. How mouse/keyboard events are received.
8. How the application loop works.
9. How the project shuts the library down.

Do not simply write:

> "We used library X because it is easy."

Explain the technical reasoning.

---

# 33. Event Loop Explanation

Every GUI team member should understand the event loop.

Conceptually:

```text
Initialize GUI
       ↓
while application is running:
       ↓
    receive events
       ↓
    process events
       ↓
    update game state
       ↓
    render current state
       ↓
repeat
```

Be prepared to explain:

* What an event is.
* How mouse events are detected.
* How the board is redrawn.
* How the program remains responsive.
* What happens between user actions.

---

# 34. Rendering Strategy

The GUI should render from the current game state.

Conceptually:

```text
Game State
     ↓
Board contents
     ↓
Renderer
     ↓
Visual board
```

Avoid making the GUI the authoritative source of game state.

The internal board should determine what is drawn.

This makes the game easier to test and prevents visual state from becoming inconsistent with logical state.

---

# 35. Separation of Concerns

A critical architectural principle:

> **The game engine should not need to know how the board looks.**

For example, the rules module should not need to know:

* Window size.
* Pixel coordinates.
* Button positions.
* Colors.
* Fonts.

Likewise, the GUI should not independently implement Othello rules.

The GUI asks the game engine:

```text
"Is this move legal?"
```

rather than determining legality itself.

---

# 36. Team Component Allocation

For 9–12 students, a possible allocation is:

| Component                     | Approx. Responsibility |
| ----------------------------- | ---------------------- |
| Board/data structures         | 2 students             |
| Move validation/rules         | 2 students             |
| Piece flipping/game mechanics | 2 students             |
| GUI/rendering                 | 2 students             |
| Input/controller              | 1–2 students           |
| Game state/score/pass/end     | 1–2 students           |
| Testing/integration           | Shared responsibility  |

These are **responsibility groups**, not isolated silos.

Every team member must understand the complete integrated system.

---

# 37. Individual Student Responsibility

Because students are graded individually, each student should maintain a record containing:

### Their component

* Purpose.
* Requirements.
* Design.
* Data structures.
* Algorithms.
* Functions.
* Integration points.
* Tests.
* Known limitations.

### Their actual contribution

Record:

* Code written personally.
* Code modified.
* Tests created.
* Documentation created.
* Integration work.
* Debugging performed.

This will make the final presentation much easier.

---

# 38. Integration Responsibility

The project must not become:

```text
Component A works
Component B works
Component C works
but
A + B + C do not work together
```

Integration should be planned from the beginning.

Define interfaces between components.

For example:

```text
GUI
 ↓
Input
 ↓
Game Controller
 ↓
Rules
 ↓
Board State
 ↓
Rules
 ↓
Updated Board
 ↓
GUI
```

Every interface should have a clearly understood contract.

---

# 39. API/Function Contracts

Before implementation, each major function should have a documented purpose.

For each function define:

```text
Function:
Purpose:
Inputs:
Output:
Side effects:
Preconditions:
Postconditions:
Possible errors:
```

This is particularly useful during team integration.

---

# 40. Algorithmic Complexity

Students should understand the complexity of their important algorithms.

For example, legal move checking involves examining up to eight directions.

The team should be able to discuss:

* Worst-case work for checking one move.
* Work required to find all legal moves.
* Cost of scanning the board.
* Cost of counting pieces.
* Cost of AI search, if implemented.

Do not simply quote Big-O notation without explaining what it means for the actual Othello board.

---

# 41. Data-Structure Justification

The report must explain why the selected data structures were used.

Possible choices include:

* 2D array for board.
* Struct for game state.
* Enum for cell/player states.
* Array/list for legal moves.
* Stack/history structure for undo.
* File representation for saved games.

For each important structure:

```text
What problem does it solve?
Why was it selected?
What alternatives were considered?
What are its advantages?
What are its limitations?
```

---

# 42. Security & Robustness

Although this is a game, basic defensive programming is expected.

The implementation should avoid:

* Out-of-bounds array access.
* Invalid pointer use.
* Use-after-free.
* Memory leaks.
* Unsafe file handling.
* Unchecked assumptions about input.

If external files are used, validate their contents before using them.

---

# 43. Code Quality Requirements

The final code should have:

* Meaningful variable names.
* Meaningful function names.
* Consistent formatting.
* Comments explaining non-obvious algorithms.
* Minimal unnecessary duplication.
* Modular files.
* No dead code.
* No unexplained magic numbers where constants would be clearer.

Comments should explain **why**, not merely repeat what the code obviously does.

Bad:

```text
// increment i
```

Better:

```text
// Move to the next cell in the current search direction.
```

---

# 44. Documentation Requirements

The final documentation should explain the complete project.

Recommended report structure:

## 1. Introduction

* Problem statement.
* Objective.
* Othello overview.

## 2. Requirements

* Functional requirements.
* Non-functional requirements.
* Cross-platform requirements.

## 3. Technology Stack

* C compiler.
* GUI library.
* Supporting libraries.
* Build tools.

## 4. Architecture

* Overall architecture.
* Module diagram.
* Data flow.
* Component responsibilities.

## 5. Game Logic

* Board representation.
* Move validation.
* Capture detection.
* Piece flipping.
* Turn management.
* Pass rules.
* End-game rules.
* Scoring.

## 6. GUI

* Window design.
* Rendering.
* Input handling.
* Coordinate conversion.
* Event loop.

## 7. Advanced Features

Document any:

* AI.
* Save/load.
* Undo/redo.
* Move history.
* Sound.
* Animations.

## 8. Testing

* Test methodology.
* Test cases.
* Results.
* Edge cases.

## 9. Cross-Platform Support

* Linux.
* Windows.
* macOS.

## 10. Team Contributions

Document each student's contribution separately.

## 11. GenAI Usage

This section is **mandatory and should be treated as a major project component**, given the assignment instructions.

## 12. Limitations & Future Work

Document what the current version does not implement.

## 13. References

Include:

* Library documentation.
* Tutorials.
* Articles.
* Books.
* Official documentation.
* Other online resources.

---

# 45. Mandatory GenAI Documentation

The assignment explicitly requires reporting **all code generated using GenAI and your own code**.

Therefore, maintain a GenAI log from the **first prompt onward**.

Do not attempt to reconstruct it at the end.

A recommended format is:

| ID     | Date | Category       | Prompt | Tool    | Output Used? | How Used            | Student Modification |
| ------ | ---- | -------------- | ------ | ------- | ------------ | ------------------- | -------------------- |
| AI-001 | ...  | Planning       | ...    | ChatGPT | Yes          | Architecture        | ...                  |
| AI-002 | ...  | Learning       | ...    | ChatGPT | Yes          | Explained algorithm | ...                  |
| AI-003 | ...  | Implementation | ...    | ChatGPT | Partially    | Generated function  | ...                  |

---

# 46. GenAI Prompt Categories

Categorize prompts such as:

### A. Planning

Prompts used for:

* Architecture.
* Feature selection.
* Module decomposition.

### B. Learning

Prompts used to understand:

* Othello rules.
* C concepts.
* GUI concepts.
* Algorithms.

### C. Implementation

Prompts that request:

* Code.
* Function implementation.
* Debugging.

### D. Debugging

Prompts involving:

* Compiler errors.
* Runtime errors.
* Logic errors.
* GUI problems.

### E. Testing

Prompts used to develop:

* Test cases.
* Edge cases.
* Validation strategies.

### F. Documentation

Prompts used for:

* Report structure.
* Explanations.
* Documentation improvement.

### G. Code Review

Prompts used to:

* Identify bugs.
* Improve architecture.
* Analyze complexity.

---

# 47. GenAI Usage Must Be Traceable

For every AI-generated contribution, the team should be able to answer:

> "Which prompt resulted in this part of the project?"

And:

> "What did the student understand/change after receiving the AI output?"

Do not simply paste generated code into the project and record:

> "Used ChatGPT."

Instead document the process.

Example structure:

```text
Prompt
  ↓
AI response
  ↓
Student analysis
  ↓
Student modification
  ↓
Testing
  ↓
Final implementation
```

---

# 48. Own Code vs AI-Generated Code

Maintain a clear distinction between:

### Student-written code

Code designed and written independently.

### AI-assisted code

Code where AI provided:

* Suggestions.
* Partial implementation.
* Debugging assistance.
* Refactoring ideas.

### AI-generated code

Code substantially produced by an AI tool.

The report should identify these distinctions honestly.

---

# 49. Understanding Requirement

Every student must understand the code they present.

A student should **not** rely on:

> "ChatGPT generated this."

During evaluation, be prepared to explain:

* What the function does.
* Why it exists.
* Inputs.
* Outputs.
* Algorithm.
* Edge cases.
* Complexity.
* Interaction with other modules.
* Why the implementation is correct.

If a student cannot explain AI-generated code, it should not be treated as understood project work.

---

# 50. Presentation Preparation

Each student should prepare a short explanation of their component.

Recommended structure:

### 1. Problem

What problem does your component solve?

### 2. Design

How did you structure it?

### 3. Data structures

What data does it use?

### 4. Algorithm

How does it work step by step?

### 5. Integration

How does it communicate with other components?

### 6. Edge cases

What unusual situations did you handle?

### 7. Testing

How did you verify it?

### 8. GenAI

How did AI assist you, if applicable?

### 9. Limitations

What limitations remain?

---

# 51. Demonstration Requirements

The final demonstration should show more than simply:

> "The game runs."

Demonstrate:

1. Program startup.
2. Initial board.
3. Legal move.
4. Piece flipping.
5. Multiple-direction flipping.
6. Invalid move.
7. Score update.
8. Turn switching.
9. Pass condition.
10. Game termination.
11. New game.
12. Any advanced features.
13. Cross-platform execution, where possible.

---

# 52. Edge Cases That Must Be Demonstrated or Tested

At minimum:

* Corner move.
* Edge move.
* Diagonal capture.
* Multiple-direction capture.
* Long capture sequence.
* Illegal move.
* Occupied cell.
* No legal moves.
* Player pass.
* Both players unable to move.
* Full board.
* Draw.
* One-player-dominant final board.

These cases are particularly valuable during evaluation because they demonstrate that the implementation follows the rules rather than merely working for the initial configuration.

---

# 53. Definition of "Complete"

The project should not be considered complete until:

* [ ] C implementation is complete.
* [ ] GUI is functional.
* [ ] Standard Othello rules work.
* [ ] Legal moves work.
* [ ] Captures work.
* [ ] Multiple-direction captures work.
* [ ] Scores work.
* [ ] Turns work.
* [ ] Passes work.
* [ ] Game termination works.
* [ ] Restart works.
* [ ] Invalid input is handled.
* [ ] GUI and game engine are integrated.
* [ ] Tests have been performed.
* [ ] Documentation is complete.
* [ ] Individual contributions are documented.
* [ ] GenAI prompts have been recorded.
* [ ] AI-generated code has been identified.
* [ ] References have been documented.
* [ ] Cross-platform build/run instructions exist.

---

# 54. Recommended Development Order

Do **not** begin with the GUI.

Use this sequence:

### Phase 1 — Requirements

Finalize:

* Rules.
* Features.
* Components.
* Team responsibilities.

### Phase 2 — Architecture

Define:

* Data structures.
* Modules.
* APIs.
* Interfaces.

### Phase 3 — Core Board

Implement and test:

* Board.
* Initialization.
* Reset.
* Cell access.

### Phase 4 — Rules

Implement and test:

* Direction checking.
* Legal move detection.
* Capture detection.
* Piece flipping.

### Phase 5 — Game State

Implement:

* Turns.
* Scores.
* Passes.
* End conditions.

### Phase 6 — GUI

Implement:

* Window.
* Board.
* Pieces.
* Score.
* Status.

### Phase 7 — Input

Connect:

```text
Mouse → Board coordinate → Game engine
```

### Phase 8 — Integration

Connect all modules.

### Phase 9 — Testing

Perform systematic tests.

### Phase 10 — Advanced Features

Only after the core game is stable.

### Phase 11 — Documentation

Document:

* Architecture.
* Implementation.
* Testing.
* Contributions.
* GenAI.

### Phase 12 — Presentation

Every member practices explaining their own component **without relying on the report or AI-generated explanation**.

---

# 55. Final Architectural Goal

The finished system should conceptually look like:

```text
                    ┌───────────────────┐
                    │       GUI         │
                    │ Board / Buttons   │
                    └─────────┬─────────┘
                              │
                              ▼
                    ┌───────────────────┐
                    │ Input Controller  │
                    └─────────┬─────────┘
                              │
                              ▼
                    ┌───────────────────┐
                    │   Game Manager    │
                    │ Turn / Pass / End │
                    └─────────┬─────────┘
                              │
                 ┌────────────┴────────────┐
                 ▼                         ▼
        ┌─────────────────┐       ┌─────────────────┐
        │  Rules Engine   │       │ Score Manager   │
        │ Legal Moves     │       │ Piece Counts    │
        │ Captures        │       │ Result          │
        └────────┬────────┘       └────────┬────────┘
                 │                         │
                 └────────────┬────────────┘
                              ▼
                    ┌───────────────────┐
                    │  Board / State    │
                    │    8 × 8 board    │
                    └───────────────────┘
```

The key principle is:

> **The board state is the source of truth; the rules engine determines what is legal; the game manager controls progression; the controller handles user actions; and the GUI displays the resulting state.**

This architecture gives the team a clean way to demonstrate both **software engineering** and **Othello-specific algorithmic understanding**, while keeping the project manageable for a 9–12 person team.

### Immediate next step

Before writing any C code, freeze these five artifacts:

1. **Component allocation table** — who owns which subsystem.
2. **Architecture/module diagram** — how the subsystems communicate.
3. **Data-structure specification** — board, player, game state, moves, etc.
4. **Function/API specification** — what each module exposes.
5. **GenAI prompt log** — start recording prompts from this point onward.

That will give you a stable foundation before implementation begins.
