# ASCII-Grid-Runner-Game-in-C-on-a-RISC-V-Virtual-Microcontroller

## Overview

**ASCII Grid Runner** is a university project in which I designed a **RISC-V virtual microcontroller** in C and built a **simple ASCII-based game** that runs on top of it.  
The project combines CPU architecture concepts (instruction decoding, ALU, registers, memory, branching) with a terminal-based interactive game.

---

## Features

- RISC-V virtual CPU (fetch–decode–execute cycle)
- Memory-mapped ASCII display (10×10 grid)
- Interactive game with keyboard controls
- Multithreaded design (CPU, display, input)

---

## Controls

- `W` `A` `S` `D` — Move player  
- `Q` — Quit

---

## Build & Run

### Requirements
- GCC or Clang
- POSIX system (Linux / macOS)
- pthread support

### Compile
```bash
gcc -Wall -Wextra -pthread -o ascii_grid_runner project.c
