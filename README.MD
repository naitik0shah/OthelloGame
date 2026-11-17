# Othello (C17)

## Pre-requisites

Make sure everything is set to path & env variables are configured properly

- Git
- C Compiler
- CMake App + CLI
- VS-Code installed with extensions
  - <https://marketplace.visualstudio.com/items?itemName=ms-vscode.cpptools-extension-pack>
  - <https://marketplace.visualstudio.com/items?itemName=llvm-vs-code-extensions.vscode-clangd>
  - <https://marketplace.visualstudio.com/items?itemName=ms-vscode.makefile-tools>

## Setup

If you have never downloaded the repo
`git clone https://github.com/naitik0shah/OthelloGame`

If you have downloaded the repo then
`git fetch`
`git pull`

## Build & Run

`cmake -S . -B build`  
`cmake --build build`
`.\build\othello.exe`
