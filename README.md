# BranchNotes / 枝記 Notes

**English** | [繁體中文](#繁體中文)

BranchNotes is a local-first Windows note-taking application built with C++20, the Win32 API, and CMake. It does not use Qt.

## Features

- Create, rename, delete, move, categorize, and search notes.
- Rich-text editing with headings, bold, italic, bulleted and numbered lists, and 3 × 3 tables. Notes are saved automatically as RTF.
- Five remembered background themes.
- A manual-start timer with 5, 10, 15, 20, 25, and 30 minute presets.
- Mind maps with draggable, editable nodes and Ctrl-click connections. Pan with scrollbars or the mouse wheel; the workspace can expand as you drag near its edges. Maps and viewport positions are saved with their notes.
- Import TXT, Markdown, and RTF files. TXT and Markdown imports support UTF-8 and UTF-16.
- Export notes to RTF, TXT, or Markdown.

## Run the portable build

Extract `release/BranchNotes-0.1.0-win64.zip` and run `BranchNotes.exe`. No Qt or separate Visual C++ runtime installation is required. The application uses Windows system components, including Rich Edit.

The standalone build is at `build/deliverable/Release/BranchNotes.exe`.

## Build requirements

- Windows 10 or newer
- CMake 3.25 or newer
- Visual Studio C++ Build Tools with MSVC and the Windows SDK
- VS Code with the CMake Tools and C/C++ extensions

Recommended VS Code extensions are listed in `.vscode/extensions.json`.

### Build in VS Code

Open this folder in VS Code. CMake Tools uses the `windows-msvc` configure preset and the `windows-msvc-debug` build preset. Use the CMake status bar to configure, build, and launch `BranchNotes`.

### Build a Release from Visual Studio Developer PowerShell

```powershell
& "C:\Program Files\CMake\bin\cmake.exe" --preset windows-msvc
& "C:\Program Files\CMake\bin\cmake.exe" --build --preset windows-msvc-release
```

The executable is written to `build/deliverable/Release/BranchNotes.exe`.

### Create the portable ZIP

```powershell
& "C:\Program Files\CMake\bin\cmake.exe" --build --preset windows-msvc-release
& "C:\Program Files\CMake\bin\cpack.exe" --config build/CPackConfig.cmake -C Release -B release
```

The package is written to `release/BranchNotes-0.1.0-win64.zip` and contains `BranchNotes.exe` and `使用说明.txt`.

## License

BranchNotes is distributed under the [MIT License](LICENSE).

## Data and backups

Notes and mind maps are stored locally under `%LOCALAPPDATA%\BranchNotes\Notes`. To back up your data, close the application and copy that folder. Deleting a note moves its files to the Windows Recycle Bin.

## Source layout

- `src/main.cpp` — Windows application entry point.
- `src/platform/win32/` — Win32 window and message-loop implementation.
- `include/branchnotes/` — application-facing headers.
- `prototypes/` — interface experiments not compiled into the application.

---

<a id="繁體中文"></a>
## 繁體中文

BranchNotes 是一款以 C++20、Win32 API 和 CMake 開發的 Windows 本機筆記軟體，不使用 Qt。

### 功能

- 建立、重新命名、刪除、移動、分類及搜尋筆記。
- 支援標題、粗體、斜體、項目符號清單、編號清單和 3 × 3 表格；筆記會自動儲存為 RTF。
- 提供五種背景主題，並記住選擇的主題。
- 計時器採手動開始，提供 5、10、15、20、25 和 30 分鐘選項。
- 心智圖支援新增、拖曳和編輯節點，以及按住 Ctrl 點選節點建立連線。可用捲軸或滑鼠滾輪移動畫布；拖曳至邊緣時工作區會擴展。心智圖和畫布位置會與筆記一併儲存。
- 匯入 TXT、Markdown 和 RTF 檔案；TXT 和 Markdown 支援 UTF-8、UTF-16。
- 匯出為 RTF、TXT 或 Markdown。

### 執行便攜版

解壓縮 `release/BranchNotes-0.1.0-win64.zip`，再執行 `BranchNotes.exe`。不需要安裝 Qt 或另外安裝 Visual C++ 執行階段；程式會使用 Windows 系統元件，包括 Rich Edit。

單獨的執行檔位於 `build/deliverable/Release/BranchNotes.exe`。

### 開發環境需求

- Windows 10 或更新版本
- CMake 3.25 或更新版本
- Visual Studio C++ Build Tools（MSVC 和 Windows SDK）
- VS Code，以及 CMake Tools 和 C/C++ 擴充套件

建議安裝的 VS Code 擴充套件列於 `.vscode/extensions.json`。

### 在 VS Code 中建置

使用 VS Code 開啟專案資料夾。CMake Tools 會使用 `windows-msvc` 設定預設和 `windows-msvc-debug` 建置預設；可透過狀態列進行設定、建置和啟動。

### 使用 Visual Studio Developer PowerShell 建置 Release

```powershell
& "C:\Program Files\CMake\bin\cmake.exe" --preset windows-msvc
& "C:\Program Files\CMake\bin\cmake.exe" --build --preset windows-msvc-release
```

執行檔會輸出至 `build/deliverable/Release/BranchNotes.exe`。

### 建立便攜 ZIP

```powershell
& "C:\Program Files\CMake\bin\cmake.exe" --build --preset windows-msvc-release
& "C:\Program Files\CMake\bin\cpack.exe" --config build/CPackConfig.cmake -C Release -B release
```

套件會輸出至 `release/BranchNotes-0.1.0-win64.zip`，內含 `BranchNotes.exe` 和 `使用说明.txt`。

### 授權

BranchNotes 採用 [MIT 授權](LICENSE)。

### 資料與備份

筆記和心智圖儲存在 `%LOCALAPPDATA%\BranchNotes\Notes`。備份前請先關閉程式，再複製該資料夾。刪除筆記時，相關檔案會移至 Windows 資源回收筒。

### 原始碼目錄

- `src/main.cpp` — Windows 程式進入點。
- `src/platform/win32/` — Win32 視窗和訊息迴圈。
- `include/branchnotes/` — 程式介面標頭檔。
- `prototypes/` — 不會編入正式程式的介面實驗版本。

