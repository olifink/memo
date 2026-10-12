# memo

A lightweight, zero-dependency, POSIX-compliant full-screen text editor inspired by Jef Raskin's **Canon Cat** (1987) and *The Humane Interface*.

## Key Concepts

* **Modeless Text Flow**: At all times, typing inserts text into the document.
* **The Two-Part Cursor**:
  * **The Pencil** (blinking cursor): Visual insertion point for new text.
  * **The Eraser** (solid highlight): Exact character or span that will be deleted when `ERASE` (`Backspace` or `Delete`) is pressed.
  * **Fundamental Cursor Rule**: Type at the pencil, erase at the eraser.
* **Dedicated LEAP Quasimodes**:
  * **Hold `Right-ALT`** + type: **LEAP Forward** (incremental teleportation toward document end).
  * **Hold `Left-ALT`** + type: **LEAP Backward** (incremental teleportation toward document start).
  * **Release `ALT`**: Commits cursor destination and coalesces pencil and eraser.
  * **Hold `Left-ALT` + `Right-ALT`**: **LEAP Both** (selects the span between endpoints).
* **Local Micro-Navigation**: Standard arrow keys (`Up`, `Down`, `Left`, `Right`) move the cursor locally and collapse selections.
* **Modeless Manipulation**:
  * `Alt-M`: **MOVE** selected span directly to current cursor position (no clipboard needed).
  * `Alt-C`: **COPY** selected span to current cursor position.
  * `Alt-U` / `Ctrl-Z`: **UNDO** previous transaction.

## Building & Testing

Requires a POSIX C99 compiler (`gcc` or `clang`) and `make`.

```bash
# Build memo
make

# Run automated test suite
make test

# Clean artifacts
make clean
```

## Usage

```bash
./memo [FILE]
```

Press `Ctrl-S` to save, and `Ctrl-Q` to quit.
