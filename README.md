# BranchNotes

BranchNotes is a local-first Windows notes application built with C++20, the Win32 API, and CMake. It does not use Qt.

## Requirements

- Windows 10 or newer
- CMake 3.25 or newer
- Visual Studio C++ Build Tools (MSVC and Windows SDK)
- VS Code with the recommended CMake Tools and C/C++ extensions

## Build in VS Code

Open this folder in VS Code. CMake Tools uses the `windows-msvc` configure preset and the `windows-msvc-debug` build preset. Use the CMake status bar to configure, build, and launch `BranchNotes`.

## Build from a Visual Studio Developer PowerShell

```powershell
& "C:\Program Files\CMake\bin\cmake.exe" --preset windows-msvc
& "C:\Program Files\CMake\bin\cmake.exe" --build --preset windows-msvc-release
```

The executable is written to `build/deliverable/Release/BranchNotes-Map-Exact-Clip.exe`.

## Source layout

- `src/main.cpp` — Windows application entry point.
- `src/platform/win32/` — Win32 window and message-loop implementation.
- `include/branchnotes/` — application-facing headers.
- `prototypes/` — isolated interface experiments, not compiled into the application.

## Features in this build

- Create, rename, delete, and move note categories and notes; search note titles.
- Edit notes with the Windows Rich Edit control. The toolbar provides headings, bold, italic, bulleted and numbered lists, and a 3 × 3 table. Changes are automatically saved as RTF.
- Choose from five background color themes; the selected theme is remembered.
- Use the timer by choosing 5, 10, 15, 20, 25, or 30 minutes, then clicking **開始**. The button becomes **暫停** while it runs; **重設** restores the selected duration. It starts only when you press the button.
- Switch to **心智圖** to add nodes, drag them, double-click to edit, use Ctrl-click to connect the selected node to another node, or select a node and press Delete. Scrollbars and the mouse wheel pan the canvas; dragging near an edge expands the workspace. Mind maps and the viewport are saved with their notes.
- Import TXT, Markdown, and RTF files. TXT/Markdown supports UTF-8 and UTF-16 input. Imports are copied into the selected category.
- Export the selected note as RTF, TXT, or Markdown. RTF retains formatting; Markdown converts headings, bullet lists, and numbered lists; TXT contains plain text.

## Data and distribution

Notes and mind maps are stored locally under `%LOCALAPPDATA%/BranchNotes/Notes`. Deleting a note moves its note and mind-map files to the Windows Recycle Bin. The Release build uses the static MSVC runtime, so the executable does not require a separate Visual C++ runtime installation. It still relies on Windows system components, including Rich Edit.

The current executable is `build/deliverable/Release/BranchNotes-Map-Exact-Clip.exe`. This is a portable single executable for the current Windows machine; a signed installer, automatic updates, and cloud sync are outside this implementation step.
