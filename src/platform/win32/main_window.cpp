#include "branchnotes/platform/main_window.hpp"
#include "branchnotes/storage/local_note_store.hpp"

#include <commctrl.h>
#include <commdlg.h>
#include <richedit.h>
#include <windowsx.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <memory>
#include <optional>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {

using branchnotes::storage::LocalNoteStore;
using branchnotes::storage::NoteInfo;

constexpr wchar_t kWindowClassName[] = L"BranchNotesMainWindow";
constexpr wchar_t kPromptClassName[] = L"BranchNotesPromptWindow";
constexpr wchar_t kMapCanvasClassName[] = L"BranchNotesMindMapCanvas";
constexpr wchar_t kWindowTitle[] = L"枝記 Notes";
constexpr wchar_t kAllNotes[] = L"全部筆記";
constexpr int kSidebarWidth = 232;
constexpr int kListWidth = 316;
constexpr int kHeaderHeight = 68;
constexpr int kSearchControl = 100;
constexpr int kRichEditControl = 101;
constexpr int kMapCanvasControl = 102;
constexpr int kCategoryRename = 2001;
constexpr int kCategoryDelete = 2002;
constexpr int kNoteRename = 2011;
constexpr int kNoteDelete = 2012;
constexpr int kMoveCategoryFirst = 2100;
constexpr int kPromptEdit = 3001;
constexpr int kPromptAccept = IDOK;
constexpr int kPromptCancel = IDCANCEL;
constexpr UINT_PTR kPomodoroTimer = 1;
constexpr UINT_PTR kAutosaveTimer = 2;

enum class DetailMode { Editor, MindMap };

struct MapNode {
    int id{};
    int x = 500;
    int y = 500;
    std::wstring text;
};

struct MapEdge { int from{}; int to{}; };

struct MapCanvasState {
    std::vector<MapNode> nodes;
    std::vector<MapEdge> edges;
    int worldWidth = 2400;
    int worldHeight = 1800;
    int scrollX{};
    int scrollY{};
    int selectedId = -1;
    int draggingId = -1;
    POINT dragOffset{};
    bool dirty{};
    HDC backBufferDc{};
    HBITMAP backBufferBitmap{};
    HGDIOBJ previousBitmap{};
    int backBufferWidth{};
    int backBufferHeight{};
    HBRUSH gridBrush{};
    HBITMAP gridPatternBitmap{};
    COLORREF gridPanelColor{};
    COLORREF gridDotColor{};
};

struct Palette {
    COLORREF canvas = RGB(244, 242, 236);
    COLORREF panel = RGB(255, 254, 250);
    COLORREF side = RGB(250, 249, 245);
    COLORREF ink = RGB(45, 50, 48);
    COLORREF muted = RGB(132, 137, 132);
    COLORREF line = RGB(231, 229, 222);
    COLORREF accent = RGB(96, 120, 99);
    COLORREF soft = RGB(232, 238, 229);
    COLORREF field = RGB(243, 244, 239);
};

const Palette kPalettes[] = {
    {RGB(244,242,236), RGB(255,254,250), RGB(250,249,245), RGB(45,50,48), RGB(132,137,132), RGB(231,229,222), RGB(96,120,99), RGB(232,238,229), RGB(243,244,239)},
    {RGB(235,243,237), RGB(252,254,251), RGB(244,249,245), RGB(43,57,48), RGB(117,138,123), RGB(220,232,222), RGB(77,119,89), RGB(225,239,228), RGB(242,248,243)},
    {RGB(246,237,226), RGB(255,251,245), RGB(251,245,237), RGB(57,48,39), RGB(142,126,108), RGB(235,222,205), RGB(157,111,67), RGB(244,231,214), RGB(249,243,235)},
    {RGB(239,238,250), RGB(253,252,255), RGB(248,247,253), RGB(49,47,66), RGB(128,125,157), RGB(227,225,243), RGB(105,99,161), RGB(235,233,249), RGB(247,246,253)},
    {RGB(235,240,244), RGB(252,253,255), RGB(245,248,251), RGB(43,52,61), RGB(119,132,146), RGB(220,229,236), RGB(76,108,135), RGB(227,237,245), RGB(241,246,250)}
};

constexpr int kPaletteCount = static_cast<int>(sizeof(kPalettes) / sizeof(kPalettes[0]));

struct PromptContext {
    HWND owner{};
    HWND edit{};
    std::wstring title;
    std::wstring label;
    std::wstring initial;
    std::wstring value;
    bool accepted{};
};

HINSTANCE g_instance{};
HMODULE g_richEditModule{};
HWND g_window{};
HWND g_search{};
HWND g_richEdit{};
HWND g_mapCanvas{};
HFONT g_font{};
HFONT g_titleFont{};
HBRUSH g_fieldBrush{};
Palette g_colors;
LocalNoteStore g_store;
std::vector<std::wstring> g_categories;
std::vector<NoteInfo> g_notes;
std::vector<std::wstring> g_moveTargets;
std::wstring g_selectedCategory{kAllNotes};
std::wstring g_searchText;
std::optional<NoteInfo> g_selectedNote;
MapCanvasState g_mapState;
DetailMode g_detailMode = DetailMode::Editor;
int g_themeIndex{};
int g_timerSeconds = 25 * 60;
int g_timerPresetMinutes = 25;
bool g_timerRunning{};
bool g_loadingDocument{};
std::wstring g_saveStatus = L"已自動儲存";
int g_categoryScroll{};
int g_noteScroll{};

void ShowError(const std::wstring& message);
std::wstring CurrentTimerText();
std::string ToUtf8(const std::wstring& value);
std::optional<std::wstring> PromptForText(const std::wstring& title,
    const std::wstring& label, const std::wstring& initial = {});
void Layout(HWND window);
void UpdateMapScrollbars(HWND canvas);

RECT MakeRect(int left, int top, int right, int bottom) {
    return RECT{left, top, right, bottom};
}

RECT SidebarRect(HWND hwnd) { RECT r{}; GetClientRect(hwnd, &r); return MakeRect(0, 0, kSidebarWidth, r.bottom); }
RECT ListRect(HWND hwnd) { RECT r{}; GetClientRect(hwnd, &r); return MakeRect(kSidebarWidth, 0, kSidebarWidth + kListWidth, r.bottom); }

void Fill(HDC dc, RECT rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

void RoundBox(HDC dc, RECT rect, COLORREF color, COLORREF border, int radius = 12) {
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void DrawTextLine(HDC dc, const std::wstring& text, RECT rect, COLORREF color,
                  HFONT font, UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS) {
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    HGDIOBJ oldFont = SelectObject(dc, font);
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rect, flags | DT_NOPREFIX);
    SelectObject(dc, oldFont);
}

void DrawTextAt(HDC dc, const std::wstring& text, int x, int y, int width,
                COLORREF color, HFONT font) {
    DrawTextLine(dc, text, MakeRect(x, y, x + width, y + 25), color, font);
}

void DrawRule(HDC dc, int x1, int y1, int x2, int y2) {
    HPEN pen = CreatePen(PS_SOLID, 1, g_colors.line);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    MoveToEx(dc, x1, y1, nullptr);
    LineTo(dc, x2, y2);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

void RefreshData(bool keepSelection = true, bool redrawAll = true) {
    const std::wstring selectedPath = keepSelection && g_selectedNote
        ? g_selectedNote->path.wstring() : L"";
    g_categories = g_store.ListCategories();
    if (g_selectedCategory != kAllNotes &&
        std::find(g_categories.begin(), g_categories.end(), g_selectedCategory) == g_categories.end()) {
        g_selectedCategory = kAllNotes;
    }
    g_notes = g_store.ListNotes(g_selectedCategory, g_searchText);
    g_selectedNote.reset();
    if (!selectedPath.empty()) {
        auto it = std::find_if(g_notes.begin(), g_notes.end(), [&](const NoteInfo& note) {
            return note.path.wstring() == selectedPath;
        });
        if (it != g_notes.end()) g_selectedNote = *it;
    }
    if (!g_selectedNote && !g_notes.empty()) g_selectedNote = g_notes.front();
    if (g_window && redrawAll) InvalidateRect(g_window, nullptr, TRUE);
}

std::wstring ReadControlText(HWND control) {
    const int length = GetWindowTextLengthW(control);
    std::wstring value(static_cast<size_t>(length) + 1, L'\0');
    if (length > 0) GetWindowTextW(control, value.data(), length + 1);
    value.resize(static_cast<size_t>(length));
    return value;
}

void InvalidateSaveStatus() {
    if (!g_window) return;
    const int left = kSidebarWidth + kListWidth + 46;
    RECT status = MakeRect(left, 104, left + 224, 136);
    InvalidateRect(g_window, &status, FALSE);
}

void InvalidateTimerDisplay() {
    if (!g_window) return;
    RECT client{}; GetClientRect(g_window, &client);
    RECT timer = MakeRect(client.right - 470, 10, client.right - 6, 56);
    InvalidateRect(g_window, &timer, FALSE);
}

void ApplyTheme(int index, bool persist) {
    g_themeIndex = std::clamp(index, 0, kPaletteCount - 1);
    g_colors = kPalettes[g_themeIndex];
    if (g_fieldBrush) DeleteObject(g_fieldBrush);
    g_fieldBrush = CreateSolidBrush(g_colors.field);
    if (g_richEdit) {
        SendMessageW(g_richEdit, EM_SETBKGNDCOLOR, 0, g_colors.panel);
        CHARFORMAT2W format{};
        format.cbSize = sizeof(format);
        format.dwMask = CFM_COLOR;
        format.crTextColor = g_colors.ink;
        SendMessageW(g_richEdit, EM_SETCHARFORMAT, SCF_DEFAULT,
                     reinterpret_cast<LPARAM>(&format));
    }
    if (persist) {
        std::wstring error;
        g_store.SaveThemeIndex(g_themeIndex, error);
    }
    if (g_window) InvalidateRect(g_window, nullptr, TRUE);
    if (g_mapCanvas) InvalidateRect(g_mapCanvas, nullptr, TRUE);
}

struct RichStreamContext {
    const std::vector<char>* input{};
    std::vector<char>* output{};
    size_t position{};
};

DWORD CALLBACK RichStreamCallback(DWORD_PTR cookie, LPBYTE buffer, LONG requested,
                                  LONG* transferred) {
    auto* context = reinterpret_cast<RichStreamContext*>(cookie);
    *transferred = 0;
    if (context->input) {
        const size_t remaining = context->input->size() - context->position;
        const size_t amount = std::min(remaining, static_cast<size_t>(requested));
        if (amount) std::memcpy(buffer, context->input->data() + context->position, amount);
        context->position += amount;
        *transferred = static_cast<LONG>(amount);
    } else if (context->output) {
        context->output->insert(context->output->end(), buffer, buffer + requested);
        *transferred = requested;
    }
    return 0;
}

bool StreamRtfIntoEditor(const std::vector<char>& data) {
    if (!g_richEdit) return false;
    RichStreamContext context{&data, nullptr, 0};
    EDITSTREAM stream{};
    stream.dwCookie = reinterpret_cast<DWORD_PTR>(&context);
    stream.pfnCallback = RichStreamCallback;
    g_loadingDocument = true;
    const LRESULT read = SendMessageW(g_richEdit, EM_STREAMIN, SF_RTF,
                                      reinterpret_cast<LPARAM>(&stream));
    g_loadingDocument = false;
    return read >= 0 && stream.dwError == 0;
}

bool StreamRtfFromEditor(std::vector<char>& data) {
    if (!g_richEdit) return false;
    data.clear();
    RichStreamContext context{nullptr, &data, 0};
    EDITSTREAM stream{};
    stream.dwCookie = reinterpret_cast<DWORD_PTR>(&context);
    stream.pfnCallback = RichStreamCallback;
    SendMessageW(g_richEdit, EM_STREAMOUT, SF_RTF,
                 reinterpret_cast<LPARAM>(&stream));
    return stream.dwError == 0 && !data.empty();
}

bool StreamPlainTextFromEditor(std::wstring& text) {
    if (!g_richEdit) return false;
    std::vector<char> bytes;
    RichStreamContext context{nullptr, &bytes, 0};
    EDITSTREAM stream{};
    stream.dwCookie = reinterpret_cast<DWORD_PTR>(&context);
    stream.pfnCallback = RichStreamCallback;
    SendMessageW(g_richEdit, EM_STREAMOUT, SF_TEXT | SF_UNICODE,
                 reinterpret_cast<LPARAM>(&stream));
    if (stream.dwError != 0 || bytes.size() % sizeof(wchar_t) != 0) return false;
    if (bytes.empty()) { text.clear(); return true; }
    text.assign(reinterpret_cast<const wchar_t*>(bytes.data()), bytes.size() / sizeof(wchar_t));
    while (!text.empty() && text.back() == L'\0') text.pop_back();
    return true;
}

bool MarkdownFromEditor(std::string& markdown) {
    std::wstring text;
    if (!StreamPlainTextFromEditor(text)) return false;
    CHARRANGE original{};
    SendMessageW(g_richEdit, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&original));
    std::wstring result;
    size_t position = 0;
    int number = 0;
    while (position < text.size()) {
        size_t end = text.find_first_of(L"\r\n", position);
        if (end == std::wstring::npos) end = text.size();
        const std::wstring line = text.substr(position, end - position);
        PARAFORMAT2 paragraph{};
        paragraph.cbSize = sizeof(paragraph);
        CHARFORMAT2W character{};
        character.cbSize = sizeof(character);
        const LONG selectionEnd = static_cast<LONG>(std::max(position + (line.empty() ? 0u : 1u), end));
        CHARRANGE range{static_cast<LONG>(position), selectionEnd};
        SendMessageW(g_richEdit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&range));
        SendMessageW(g_richEdit, EM_GETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&paragraph));
        SendMessageW(g_richEdit, EM_GETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&character));

        if (!line.empty()) {
            if ((character.dwMask & CFM_SIZE) && character.yHeight >= 560) result += L"# ";
            else if ((character.dwMask & CFM_SIZE) && character.yHeight >= 400) result += L"## ";
            if ((paragraph.dwMask & PFM_NUMBERING) && paragraph.wNumbering == PFN_BULLET) {
                result += L"- "; number = 0;
            } else if ((paragraph.dwMask & PFM_NUMBERING) && paragraph.wNumbering == PFN_ARABIC) {
                result += std::to_wstring(++number) + L". ";
            } else number = 0;
            result += line;
        }
        result += L"\n";
        if (end == text.size()) break;
        position = end + 1;
        if (text[end] == L'\r' && position < text.size() && text[position] == L'\n') ++position;
    }
    SendMessageW(g_richEdit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&original));
    markdown = ToUtf8(result);
    return true;
}

bool SaveCurrentDocument() {
    if (!g_selectedNote || !g_richEdit || g_loadingDocument) return true;
    std::vector<char> data;
    std::wstring error;
    if (!StreamRtfFromEditor(data) ||
        !g_store.WriteNoteContents(g_selectedNote->category, g_selectedNote->title,
                                   data, error)) {
        g_saveStatus = error.empty() ? L"儲存失敗" : error;
        InvalidateSaveStatus();
        return false;
    }
    g_saveStatus = L"已自動儲存";
    InvalidateSaveStatus();
    return true;
}

void ScheduleAutosave() {
    if (!g_selectedNote || g_loadingDocument) return;
    KillTimer(g_window, kAutosaveTimer);
    SetTimer(g_window, kAutosaveTimer, 700, nullptr);
    g_saveStatus = L"編輯中 · 即將儲存";
    InvalidateSaveStatus();
}

std::string ToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) return {};
    std::string result(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        result.data(), length, nullptr, nullptr);
    return result;
}

std::wstring FromUtf8(const std::string& value) {
    if (value.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) return {};
    std::wstring result(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), length);
    return result;
}

std::wstring DecodeImportedText(const std::vector<char>& bytes) {
    if (bytes.empty()) return {};
    const auto* data = reinterpret_cast<const unsigned char*>(bytes.data());
    size_t offset = 0;
    if (bytes.size() >= 2 && data[0] == 0xFF && data[1] == 0xFE) {
        offset = 2;
        std::wstring result;
        for (size_t i = offset; i + 1 < bytes.size(); i += 2)
            result.push_back(static_cast<wchar_t>(data[i] | (data[i + 1] << 8)));
        return result;
    }
    if (bytes.size() >= 2 && data[0] == 0xFE && data[1] == 0xFF) {
        offset = 2;
        std::wstring result;
        for (size_t i = offset; i + 1 < bytes.size(); i += 2)
            result.push_back(static_cast<wchar_t>((data[i] << 8) | data[i + 1]));
        return result;
    }
    if (bytes.size() >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF)
        offset = 3;
    std::string utf8(bytes.begin() + static_cast<std::ptrdiff_t>(offset), bytes.end());
    std::wstring decoded = FromUtf8(utf8);
    if (!decoded.empty() || utf8.empty()) return decoded;
    const int length = MultiByteToWideChar(CP_ACP, 0, utf8.data(),
        static_cast<int>(utf8.size()), nullptr, 0);
    if (length <= 0) return {};
    decoded.resize(static_cast<size_t>(length));
    MultiByteToWideChar(CP_ACP, 0, utf8.data(), static_cast<int>(utf8.size()),
        decoded.data(), length);
    return decoded;
}

std::vector<char> TextToRtf(const std::wstring& text) {
    std::string rtf = "{\\rtf1\\ansi\\deff0\\uc1{\\fonttbl{\\f0 Segoe UI;}}\\viewkind4\\pard\\f0\\fs22 ";
    for (size_t i = 0; i < text.size(); ++i) {
        const wchar_t ch = text[i];
        if (ch == L'\r') continue;
        if (ch == L'\n') { rtf += "\\par\n"; continue; }
        if (ch == L'\t') { rtf += "\\tab "; continue; }
        if (ch == L'\\' || ch == L'{' || ch == L'}') { rtf.push_back('\\'); rtf.push_back(static_cast<char>(ch)); continue; }
        if (ch >= 0x20 && ch <= 0x7E) { rtf.push_back(static_cast<char>(ch)); continue; }
        const short signedCode = static_cast<short>(ch);
        rtf += "\\u" + std::to_string(signedCode) + "?";
    }
    rtf += "\\par}";
    return std::vector<char>(rtf.begin(), rtf.end());
}

std::wstring EscapeMapField(const std::wstring& value) {
    std::wstring result;
    for (wchar_t ch : value) {
        if (ch == L'\\') result += L"\\\\";
        else if (ch == L'\t') result += L"\\t";
        else if (ch == L'\r') continue;
        else if (ch == L'\n') result += L"\\n";
        else result.push_back(ch);
    }
    return result;
}

std::wstring UnescapeMapField(const std::wstring& value) {
    std::wstring result;
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == L'\\' && i + 1 < value.size()) {
            const wchar_t next = value[++i];
            if (next == L't') result.push_back(L'\t');
            else if (next == L'n') result.push_back(L'\n');
            else result.push_back(next);
        } else result.push_back(value[i]);
    }
    return result;
}

void UpdateMapScrollbars(HWND canvas) {
    if (!canvas) return;
    RECT client{}; GetClientRect(canvas, &client);
    SCROLLINFO horizontal{sizeof(horizontal), SIF_RANGE | SIF_PAGE | SIF_POS};
    horizontal.nMin = 0;
    horizontal.nMax = std::max(0, g_mapState.worldWidth - 1);
    horizontal.nPage = static_cast<UINT>(std::max(1L, client.right));
    const int maxX = std::max(0, g_mapState.worldWidth - static_cast<int>(horizontal.nPage));
    g_mapState.scrollX = std::clamp(g_mapState.scrollX, 0, maxX);
    horizontal.nPos = g_mapState.scrollX;
    SetScrollInfo(canvas, SB_HORZ, &horizontal, TRUE);

    SCROLLINFO vertical{sizeof(vertical), SIF_RANGE | SIF_PAGE | SIF_POS};
    vertical.nMin = 0;
    vertical.nMax = std::max(0, g_mapState.worldHeight - 1);
    vertical.nPage = static_cast<UINT>(std::max(1L, client.bottom));
    const int maxY = std::max(0, g_mapState.worldHeight - static_cast<int>(vertical.nPage));
    g_mapState.scrollY = std::clamp(g_mapState.scrollY, 0, maxY);
    vertical.nPos = g_mapState.scrollY;
    SetScrollInfo(canvas, SB_VERT, &vertical, TRUE);
}

void DestroyMapBackBuffer() {
    if (g_mapState.backBufferDc && g_mapState.backBufferBitmap) {
        SelectObject(g_mapState.backBufferDc, g_mapState.previousBitmap);
        DeleteObject(g_mapState.backBufferBitmap);
    }
    if (g_mapState.backBufferDc) DeleteDC(g_mapState.backBufferDc);
    g_mapState.backBufferDc = nullptr;
    g_mapState.backBufferBitmap = nullptr;
    g_mapState.previousBitmap = nullptr;
    g_mapState.backBufferWidth = 0;
    g_mapState.backBufferHeight = 0;
    if (g_mapState.gridBrush) DeleteObject(g_mapState.gridBrush);
    if (g_mapState.gridPatternBitmap) DeleteObject(g_mapState.gridPatternBitmap);
    g_mapState.gridBrush = nullptr;
    g_mapState.gridPatternBitmap = nullptr;
}

void LoadMindMap() {
    DestroyMapBackBuffer();
    g_mapState = {};
    if (!g_selectedNote) { UpdateMapScrollbars(g_mapCanvas); return; }
    std::vector<char> bytes;
    if (!g_store.ReadMindMap(g_selectedNote->category, g_selectedNote->title, bytes)) {
        UpdateMapScrollbars(g_mapCanvas);
        return;
    }
    const std::wstring source = FromUtf8(std::string(bytes.begin(), bytes.end()));
    std::wistringstream lines(source);
    std::wstring line;
    bool hasWorldSize = false;
    while (std::getline(lines, line)) {
        std::vector<std::wstring> fields;
        size_t start = 0;
        for (size_t i = 0; i <= line.size(); ++i) {
            if (i == line.size() || line[i] == L'\t') {
                fields.emplace_back(line.substr(start, i - start));
                start = i + 1;
            }
        }
        try {
            if (fields.size() == 5 && fields[0] == L"N") {
                g_mapState.nodes.push_back({std::stoi(fields[1]), std::clamp(std::stoi(fields[2]), 0, 50000),
                    std::clamp(std::stoi(fields[3]), 0, 50000), UnescapeMapField(fields[4])});
            } else if (fields.size() == 3 && fields[0] == L"E") {
                g_mapState.edges.push_back({std::stoi(fields[1]), std::stoi(fields[2])});
            } else if (fields.size() == 3 && fields[0] == L"S") {
                g_mapState.worldWidth = std::clamp(std::stoi(fields[1]), 1000, 50000);
                g_mapState.worldHeight = std::clamp(std::stoi(fields[2]), 800, 50000);
                hasWorldSize = true;
            } else if (fields.size() == 3 && fields[0] == L"V") {
                g_mapState.scrollX = std::clamp(std::stoi(fields[1]), 0, 50000);
                g_mapState.scrollY = std::clamp(std::stoi(fields[2]), 0, 50000);
            }
        } catch (...) { }
    }
    RECT client{}; if (g_mapCanvas) GetClientRect(g_mapCanvas, &client);
    if (!hasWorldSize) {
        g_mapState.worldWidth = std::max(2400, static_cast<int>(client.right) * 3);
        g_mapState.worldHeight = std::max(1800, static_cast<int>(client.bottom) * 3);
        for (auto& node : g_mapState.nodes) {
            const int nodeWidth = std::clamp(static_cast<int>(node.text.size()) * 9 + 30, 104, 224);
            node.x = 38 + std::max(0, static_cast<int>(client.right) - 76 - nodeWidth) * node.x / 1000;
            node.y = 38 + std::max(0, static_cast<int>(client.bottom) - 116) * node.y / 1000;
        }
    }
    g_mapState.worldWidth = std::max(g_mapState.worldWidth, static_cast<int>(client.right) + 1);
    g_mapState.worldHeight = std::max(g_mapState.worldHeight, static_cast<int>(client.bottom) + 1);
    UpdateMapScrollbars(g_mapCanvas);
}

void SaveMindMap() {
    if (!g_selectedNote) return;
    const bool contentChanged = g_mapState.dirty;
    std::wstring content = L"S\t" + std::to_wstring(g_mapState.worldWidth) + L"\t" +
        std::to_wstring(g_mapState.worldHeight) + L"\nV\t" +
        std::to_wstring(g_mapState.scrollX) + L"\t" + std::to_wstring(g_mapState.scrollY) + L"\n";
    for (const auto& node : g_mapState.nodes)
        content += L"N\t" + std::to_wstring(node.id) + L"\t" + std::to_wstring(node.x) + L"\t" +
                   std::to_wstring(node.y) + L"\t" + EscapeMapField(node.text) + L"\n";
    for (const auto& edge : g_mapState.edges)
        content += L"E\t" + std::to_wstring(edge.from) + L"\t" + std::to_wstring(edge.to) + L"\n";
    const std::string utf8 = ToUtf8(content);
    std::wstring error;
    if (!g_store.WriteMindMap(g_selectedNote->category, g_selectedNote->title,
        std::vector<char>(utf8.begin(), utf8.end()), error)) ShowError(error);
    g_mapState.dirty = false;
    if (contentChanged) {
        g_saveStatus = L"心智圖已儲存";
        InvalidateSaveStatus();
    }
}

void LoadSelectedDocument() {
    KillTimer(g_window, kAutosaveTimer);
    if (!g_selectedNote) {
        g_loadingDocument = true;
        if (g_richEdit) SetWindowTextW(g_richEdit, L"");
        g_loadingDocument = false;
        DestroyMapBackBuffer();
        g_mapState = {};
        g_saveStatus = L"選取一篇筆記開始編輯";
        return;
    }
    std::vector<char> data;
    std::wstring error;
    if (g_store.ReadNoteContents(g_selectedNote->category, g_selectedNote->title, data, error)) {
        if (!StreamRtfIntoEditor(data)) {
            g_saveStatus = L"無法解析 RTF 內容";
            ShowError(g_saveStatus);
        } else g_saveStatus = L"已自動儲存";
    } else {
        g_saveStatus = error;
        ShowError(error);
    }
    LoadMindMap();
    if (g_mapCanvas) InvalidateRect(g_mapCanvas, nullptr, FALSE);
}

RECT MapNodeRect(const MapNode& node, RECT client) {
    const int width = std::clamp(static_cast<int>(node.text.size()) * 9 + 30, 104, 224);
    (void)client;
    const int x = node.x - g_mapState.scrollX;
    const int y = node.y - g_mapState.scrollY;
    return MakeRect(x, y, x + width, y + 40);
}

RECT MapEdgeDamageRect(const RECT& first, const RECT& second) {
    const int firstX = (first.left + first.right) / 2;
    const int firstY = (first.top + first.bottom) / 2;
    const int secondX = (second.left + second.right) / 2;
    const int secondY = (second.top + second.bottom) / 2;
    return MakeRect(std::min(firstX, secondX) - 5, std::min(firstY, secondY) - 5,
                    std::max(firstX, secondX) + 6, std::max(firstY, secondY) + 6);
}

bool EnsureMapGridBrush(HDC reference) {
    if (g_mapState.gridBrush && g_mapState.gridPanelColor == g_colors.panel &&
        g_mapState.gridDotColor == g_colors.line) return true;
    if (g_mapState.gridBrush) DeleteObject(g_mapState.gridBrush);
    if (g_mapState.gridPatternBitmap) DeleteObject(g_mapState.gridPatternBitmap);
    g_mapState.gridBrush = nullptr;
    g_mapState.gridPatternBitmap = nullptr;

    HBITMAP pattern = CreateCompatibleBitmap(reference, 24, 24);
    if (!pattern) return false;
    HDC patternDc = CreateCompatibleDC(reference);
    if (!patternDc) { DeleteObject(pattern); return false; }
    HGDIOBJ oldBitmap = SelectObject(patternDc, pattern);
    RECT tile = MakeRect(0, 0, 24, 24);
    Fill(patternDc, tile, g_colors.panel);
    SetPixelV(patternDc, 18, 18, g_colors.line);
    HBRUSH brush = CreatePatternBrush(pattern);
    SelectObject(patternDc, oldBitmap);
    DeleteDC(patternDc);
    if (!brush) { DeleteObject(pattern); return false; }
    g_mapState.gridPatternBitmap = pattern;
    g_mapState.gridBrush = brush;
    g_mapState.gridPanelColor = g_colors.panel;
    g_mapState.gridDotColor = g_colors.line;
    return true;
}

void AddMapDamageRect(HRGN region, RECT bounds, const RECT& client) {
    InflateRect(&bounds, 3, 3);
    RECT clipped{};
    if (!IntersectRect(&clipped, &bounds, &client)) return;
    HRGN part = CreateRectRgnIndirect(&clipped);
    if (!part) return;
    CombineRgn(region, region, part, RGN_OR);
    DeleteObject(part);
}

int FindMapNodeAt(POINT point, RECT client) {
    for (auto it = g_mapState.nodes.rbegin(); it != g_mapState.nodes.rend(); ++it) {
        RECT bounds = MapNodeRect(*it, client);
        if (PtInRect(&bounds, point)) return it->id;
    }
    return -1;
}

MapNode* FindMapNode(int id) {
    auto it = std::find_if(g_mapState.nodes.begin(), g_mapState.nodes.end(),
        [id](const MapNode& node) { return node.id == id; });
    return it == g_mapState.nodes.end() ? nullptr : &*it;
}

void PaintMapCanvas(HWND canvas) {
    HRGN updateRegion = CreateRectRgn(0, 0, 0, 0);
    const int updateRegionType = updateRegion ? GetUpdateRgn(canvas, updateRegion, FALSE) : ERROR;
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(canvas, &paint);
    RECT client{}; GetClientRect(canvas, &client);
    const int bufferWidth = std::max(1L, client.right);
    const int bufferHeight = std::max(1L, client.bottom);
    if (!g_mapState.backBufferDc) g_mapState.backBufferDc = CreateCompatibleDC(dc);
    if (g_mapState.backBufferDc &&
        (bufferWidth != g_mapState.backBufferWidth || bufferHeight != g_mapState.backBufferHeight)) {
        if (g_mapState.backBufferBitmap) {
            SelectObject(g_mapState.backBufferDc, g_mapState.previousBitmap);
            DeleteObject(g_mapState.backBufferBitmap);
        }
        g_mapState.backBufferBitmap = CreateCompatibleBitmap(dc, bufferWidth, bufferHeight);
        if (g_mapState.backBufferBitmap) {
            g_mapState.previousBitmap = SelectObject(g_mapState.backBufferDc, g_mapState.backBufferBitmap);
            g_mapState.backBufferWidth = bufferWidth;
            g_mapState.backBufferHeight = bufferHeight;
        } else {
            DeleteDC(g_mapState.backBufferDc);
            g_mapState.backBufferDc = nullptr;
        }
    }
    HDC drawDc = g_mapState.backBufferDc ? g_mapState.backBufferDc : dc;
    const int drawState = SaveDC(drawDc);
    if (drawDc != dc && updateRegion && updateRegionType != ERROR) {
        if (SelectClipRgn(drawDc, updateRegion) == ERROR)
            IntersectClipRect(drawDc, paint.rcPaint.left, paint.rcPaint.top,
                              paint.rcPaint.right, paint.rcPaint.bottom);
    } else {
        IntersectClipRect(drawDc, paint.rcPaint.left, paint.rcPaint.top,
                          paint.rcPaint.right, paint.rcPaint.bottom);
    }
    if (EnsureMapGridBrush(drawDc)) {
        HGDIOBJ oldBrush = SelectObject(drawDc, g_mapState.gridBrush);
        POINT oldOrigin{};
        SetBrushOrgEx(drawDc, -g_mapState.scrollX, -g_mapState.scrollY, &oldOrigin);
        PatBlt(drawDc, paint.rcPaint.left, paint.rcPaint.top,
            paint.rcPaint.right - paint.rcPaint.left,
            paint.rcPaint.bottom - paint.rcPaint.top, PATCOPY);
        SetBrushOrgEx(drawDc, oldOrigin.x, oldOrigin.y, nullptr);
        SelectObject(drawDc, oldBrush);
    } else {
        Fill(drawDc, paint.rcPaint, g_colors.panel);
    }

    for (const auto& edge : g_mapState.edges) {
        const MapNode* from = FindMapNode(edge.from);
        const MapNode* to = FindMapNode(edge.to);
        if (!from || !to) continue;
        RECT a = MapNodeRect(*from, client), b = MapNodeRect(*to, client);
        HPEN pen = CreatePen(PS_SOLID, 2, g_colors.accent);
        HGDIOBJ old = SelectObject(drawDc, pen);
        MoveToEx(drawDc, (a.left + a.right) / 2, (a.top + a.bottom) / 2, nullptr);
        LineTo(drawDc, (b.left + b.right) / 2, (b.top + b.bottom) / 2);
        SelectObject(drawDc, old); DeleteObject(pen);
    }

    if (g_mapState.nodes.empty()) {
        DrawTextLine(drawDc, L"還沒有節點；按上方「＋ 節點」建立第一個想法。",
            MakeRect(30, 30, client.right - 30, 66), g_colors.muted, g_font,
            DT_LEFT | DT_TOP | DT_WORDBREAK);
    }
    for (const auto& node : g_mapState.nodes) {
        RECT bounds = MapNodeRect(node, client);
        const bool selected = node.id == g_mapState.selectedId;
        RoundBox(drawDc, bounds, selected ? g_colors.soft : g_colors.panel,
                 selected ? g_colors.accent : g_colors.line, 12);
        DrawTextLine(drawDc, node.text, MakeRect(bounds.left + 10, bounds.top,
            bounds.right - 10, bounds.bottom), g_colors.ink, g_font,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    if (drawState) RestoreDC(drawDc, drawState);
    if (drawDc != dc && paint.rcPaint.right > paint.rcPaint.left && paint.rcPaint.bottom > paint.rcPaint.top) {
        BitBlt(dc, paint.rcPaint.left, paint.rcPaint.top,
            paint.rcPaint.right - paint.rcPaint.left, paint.rcPaint.bottom - paint.rcPaint.top,
            drawDc, paint.rcPaint.left, paint.rcPaint.top, SRCCOPY);
    }
    EndPaint(canvas, &paint);
    if (updateRegion) DeleteObject(updateRegion);
}

void DeleteSelectedMapNode() {
    if (g_mapState.selectedId < 0) return;
    const int id = g_mapState.selectedId;
    std::erase_if(g_mapState.nodes, [id](const MapNode& node) { return node.id == id; });
    std::erase_if(g_mapState.edges, [id](const MapEdge& edge) { return edge.from == id || edge.to == id; });
    g_mapState.selectedId = -1;
    g_mapState.dirty = true;
    SaveMindMap();
    InvalidateRect(g_mapCanvas, nullptr, FALSE);
}

void ScrollMapTo(HWND canvas, int x, int y) {
    RECT client{}; GetClientRect(canvas, &client);
    g_mapState.scrollX = std::clamp(x, 0, std::max(0, g_mapState.worldWidth - static_cast<int>(client.right)));
    g_mapState.scrollY = std::clamp(y, 0, std::max(0, g_mapState.worldHeight - static_cast<int>(client.bottom)));
    UpdateMapScrollbars(canvas);
    InvalidateRect(canvas, nullptr, FALSE);
    SaveMindMap();
}

void HandleMapScroll(HWND canvas, int bar, UINT command) {
    RECT client{}; GetClientRect(canvas, &client);
    SCROLLINFO info{sizeof(info), SIF_ALL};
    GetScrollInfo(canvas, bar, &info);
    const bool horizontal = bar == SB_HORZ;
    int position = horizontal ? g_mapState.scrollX : g_mapState.scrollY;
    const int page = horizontal ? static_cast<int>(client.right) : static_cast<int>(client.bottom);
    switch (command) {
    case SB_LINELEFT: position -= 48; break;
    case SB_LINERIGHT: position += 48; break;
    case SB_PAGELEFT: position -= std::max(80, page - 48); break;
    case SB_PAGERIGHT: position += std::max(80, page - 48); break;
    case SB_THUMBPOSITION:
    case SB_THUMBTRACK: position = info.nTrackPos; break;
    case SB_LEFT: position = 0; break;
    case SB_RIGHT: position = horizontal ? g_mapState.worldWidth : g_mapState.worldHeight; break;
    default: return;
    }
    if (horizontal) ScrollMapTo(canvas, position, g_mapState.scrollY);
    else ScrollMapTo(canvas, g_mapState.scrollX, position);
}

void GrowMapForDrag(int& x, int& y, int nodeWidth) {
    constexpr int kGrowth = 900;
    constexpr int kMaximumWorld = 50000;
    bool changed = false;
    if (x < 24 && g_mapState.scrollX == 0 && g_mapState.worldWidth < kMaximumWorld) {
        const int amount = std::min(kGrowth, kMaximumWorld - g_mapState.worldWidth);
        for (auto& node : g_mapState.nodes) node.x += amount;
        x += amount;
        g_mapState.scrollX += amount;
        g_mapState.worldWidth += amount;
        changed = true;
    }
    if (y < 24 && g_mapState.scrollY == 0 && g_mapState.worldHeight < kMaximumWorld) {
        const int amount = std::min(kGrowth, kMaximumWorld - g_mapState.worldHeight);
        for (auto& node : g_mapState.nodes) node.y += amount;
        y += amount;
        g_mapState.scrollY += amount;
        g_mapState.worldHeight += amount;
        changed = true;
    }
    while (x + nodeWidth + 24 >= g_mapState.worldWidth && g_mapState.worldWidth < kMaximumWorld) {
        g_mapState.worldWidth = std::min(kMaximumWorld, g_mapState.worldWidth + kGrowth);
        changed = true;
    }
    while (y + 64 >= g_mapState.worldHeight && g_mapState.worldHeight < kMaximumWorld) {
        g_mapState.worldHeight = std::min(kMaximumWorld, g_mapState.worldHeight + kGrowth);
        changed = true;
    }
    if (changed) {
        g_mapState.dirty = true;
        UpdateMapScrollbars(g_mapCanvas);
    }
}

LRESULT CALLBACK MapCanvasProcedure(HWND canvas, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_PAINT:
        PaintMapCanvas(canvas);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        UpdateMapScrollbars(canvas);
        InvalidateRect(canvas, nullptr, FALSE);
        return 0;
    case WM_HSCROLL:
        HandleMapScroll(canvas, SB_HORZ, LOWORD(wParam));
        return 0;
    case WM_VSCROLL:
        HandleMapScroll(canvas, SB_VERT, LOWORD(wParam));
        return 0;
    case WM_MOUSEWHEEL: {
        const int delta = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
        if (GetKeyState(VK_SHIFT) & 0x8000)
            ScrollMapTo(canvas, g_mapState.scrollX - delta * 72, g_mapState.scrollY);
        else
            ScrollMapTo(canvas, g_mapState.scrollX, g_mapState.scrollY - delta * 72);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        SetFocus(canvas);
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        RECT client{}; GetClientRect(canvas, &client);
        const int hit = FindMapNodeAt(point, client);
        if (hit >= 0 && (GetKeyState(VK_CONTROL) & 0x8000) &&
            g_mapState.selectedId >= 0 && g_mapState.selectedId != hit) {
            const bool exists = std::any_of(g_mapState.edges.begin(), g_mapState.edges.end(), [&](const MapEdge& edge) {
                return (edge.from == hit && edge.to == g_mapState.selectedId) ||
                       (edge.from == g_mapState.selectedId && edge.to == hit);
            });
            if (!exists) {
                g_mapState.edges.push_back({g_mapState.selectedId, hit});
                g_mapState.dirty = true;
                SaveMindMap();
            }
            g_mapState.selectedId = hit;
            InvalidateRect(canvas, nullptr, FALSE);
            return 0;
        }
        g_mapState.selectedId = hit;
        g_mapState.draggingId = hit;
        if (auto* node = FindMapNode(hit)) {
            RECT bounds = MapNodeRect(*node, client);
            g_mapState.dragOffset = {point.x - bounds.left, point.y - bounds.top};
            SetCapture(canvas);
        }
        InvalidateRect(canvas, nullptr, FALSE);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (g_mapState.draggingId >= 0 && (wParam & MK_LBUTTON)) {
            auto* node = FindMapNode(g_mapState.draggingId);
            if (node) {
                RECT client{}; GetClientRect(canvas, &client);
                const RECT oldBounds = MapNodeRect(*node, client);
                std::vector<RECT> oldEdgeDamage;
                for (const auto& edge : g_mapState.edges) {
                    const int otherId = edge.from == node->id ? edge.to : edge.to == node->id ? edge.from : -1;
                    if (const auto* other = FindMapNode(otherId))
                        oldEdgeDamage.push_back(MapEdgeDamageRect(oldBounds, MapNodeRect(*other, client)));
                }
                int x = GET_X_LPARAM(lParam) + g_mapState.scrollX - g_mapState.dragOffset.x;
                int y = GET_Y_LPARAM(lParam) + g_mapState.scrollY - g_mapState.dragOffset.y;
                const int nodeWidth = std::clamp(static_cast<int>(node->text.size()) * 9 + 30, 104, 224);
                GrowMapForDrag(x, y, nodeWidth);
                node->x = std::clamp(x, 0, std::max(0, g_mapState.worldWidth - nodeWidth));
                node->y = std::clamp(y, 0, std::max(0, g_mapState.worldHeight - 40));
                RECT movedNode = MapNodeRect(*node, client);
                HRGN damage = CreateRectRgn(0, 0, 0, 0);
                if (damage) {
                    AddMapDamageRect(damage, oldBounds, client);
                    AddMapDamageRect(damage, movedNode, client);
                    for (const RECT& edgeDamage : oldEdgeDamage)
                        AddMapDamageRect(damage, edgeDamage, client);
                    for (const auto& edge : g_mapState.edges) {
                        const int otherId = edge.from == node->id ? edge.to : edge.to == node->id ? edge.from : -1;
                        if (const auto* other = FindMapNode(otherId)) {
                            AddMapDamageRect(damage, MapEdgeDamageRect(movedNode, MapNodeRect(*other, client)), client);
                        }
                    }
                    g_mapState.dirty = true;
                    InvalidateRgn(canvas, damage, FALSE);
                    DeleteObject(damage);
                } else {
                    InvalidateRect(canvas, nullptr, FALSE);
                }
            }
        }
        return 0;
    case WM_LBUTTONUP:
        if (GetCapture() == canvas) ReleaseCapture();
        if (g_mapState.draggingId >= 0 && g_mapState.dirty) SaveMindMap();
        g_mapState.draggingId = -1;
        return 0;
    case WM_LBUTTONDBLCLK: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        RECT client{}; GetClientRect(canvas, &client);
        const int hit = FindMapNodeAt(point, client);
        auto* node = FindMapNode(hit);
        if (node) {
            auto value = PromptForText(L"編輯節點", L"節點文字", node->text);
            if (value && !value->empty()) {
                node->text = *value;
                SaveMindMap();
                InvalidateRect(canvas, nullptr, FALSE);
            }
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (wParam == VK_DELETE || wParam == VK_BACK) { DeleteSelectedMapNode(); return 0; }
        break;
    case WM_NCDESTROY:
        DestroyMapBackBuffer();
        break;
    }
    return DefWindowProcW(canvas, message, wParam, lParam);
}

void ShowError(const std::wstring& message) {
    MessageBoxW(g_window, message.c_str(), L"枝記 Notes", MB_OK | MB_ICONWARNING);
}

LRESULT CALLBACK PromptProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* context = reinterpret_cast<PromptContext*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        context = static_cast<PromptContext*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(context));
    }
    switch (message) {
    case WM_CREATE: {
        RECT client{}; GetClientRect(window, &client);
        CreateWindowExW(0, L"STATIC", context->label.c_str(), WS_CHILD | WS_VISIBLE,
            24, 22, client.right - 48, 24, window, nullptr, g_instance, nullptr);
        context->edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", context->initial.c_str(),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            24, 52, client.right - 48, 36, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPromptEdit)), g_instance, nullptr);
        SendMessageW(context->edit, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
        HWND cancel = CreateWindowExW(0, L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            client.right - 204, client.bottom - 53, 80, 32, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPromptCancel)), g_instance, nullptr);
        HWND accept = CreateWindowExW(0, L"BUTTON", L"確定", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW | BS_DEFPUSHBUTTON,
            client.right - 112, client.bottom - 53, 88, 32, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPromptAccept)), g_instance, nullptr);
        SendMessageW(cancel, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
        SendMessageW(accept, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
        SetFocus(context->edit);
        SendMessageW(context->edit, EM_SETSEL, 0, -1);
        return 0;
    }
    case WM_DRAWITEM: {
        auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        const bool primary = item->CtlID == kPromptAccept;
        const bool pressed = (item->itemState & ODS_SELECTED) != 0;
        COLORREF fill = primary ? (pressed ? RGB(77, 101, 81) : g_colors.accent)
                                : (pressed ? g_colors.soft : g_colors.panel);
        COLORREF border = primary ? fill : g_colors.line;
        RoundBox(item->hDC, item->rcItem, fill, border, 10);
        std::wstring label = item->CtlID == kPromptAccept ? L"確定" : L"取消";
        SetBkMode(item->hDC, TRANSPARENT);
        SetTextColor(item->hDC, primary ? RGB(255, 255, 255) : g_colors.ink);
        HGDIOBJ oldFont = SelectObject(item->hDC, g_font);
        DrawTextW(item->hDC, label.c_str(), -1, &item->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(item->hDC, oldFont);
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == kPromptAccept || LOWORD(wParam) == kPromptCancel) {
            if (LOWORD(wParam) == kPromptAccept) {
                context->value = ReadControlText(context->edit);
                context->accepted = true;
            }
            DestroyWindow(window);
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

std::optional<std::wstring> PromptForText(const std::wstring& title,
                                          const std::wstring& label,
                                          const std::wstring& initial) {
    PromptContext context{g_window, nullptr, title, label, initial, {}, false};
    HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
        kPromptClassName, title.c_str(), WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 420, 190, g_window, nullptr, g_instance, &context);
    if (!dialog) return std::nullopt;
    RECT ownerRect{}; GetWindowRect(g_window, &ownerRect);
    RECT dialogRect{}; GetWindowRect(dialog, &dialogRect);
    SetWindowPos(dialog, nullptr,
        ownerRect.left + ((ownerRect.right - ownerRect.left) - (dialogRect.right - dialogRect.left)) / 2,
        ownerRect.top + ((ownerRect.bottom - ownerRect.top) - (dialogRect.bottom - dialogRect.top)) / 2,
        0, 0, SWP_NOSIZE | SWP_NOZORDER);
    EnableWindow(g_window, FALSE);
    ShowWindow(dialog, SW_SHOW);
    UpdateWindow(dialog);
    MSG message{};
    while (IsWindow(dialog) && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dialog, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    EnableWindow(g_window, TRUE);
    SetForegroundWindow(g_window);
    if (!context.accepted) return std::nullopt;
    return context.value;
}

int CategoryTop() { return 224 - g_categoryScroll; }
int NoteTop() { return 139 - g_noteScroll; }

void AddMapNode() {
    if (!g_selectedNote) { ShowError(L"請先建立或選取一篇筆記。"); return; }
    auto value = PromptForText(L"新增心智圖節點", L"想法或關鍵字");
    if (!value || value->empty()) return;
    int nextId = 1;
    for (const auto& node : g_mapState.nodes) nextId = std::max(nextId, node.id + 1);
    const int index = static_cast<int>(g_mapState.nodes.size());
    RECT client{}; GetClientRect(g_mapCanvas, &client);
    const int nodeX = g_mapState.scrollX + std::max(40, (static_cast<int>(client.right) - 160) / 2) + (index % 3) * 230;
    const int nodeY = g_mapState.scrollY + std::max(40, (static_cast<int>(client.bottom) - 80) / 2) + (index / 3) * 90;
    const int nodeWidth = std::clamp(static_cast<int>(value->size()) * 9 + 30, 104, 224);
    g_mapState.worldWidth = std::max(g_mapState.worldWidth, nodeX + nodeWidth + 240);
    g_mapState.worldHeight = std::max(g_mapState.worldHeight, nodeY + 160);
    g_mapState.nodes.push_back({nextId, nodeX, nodeY, *value});
    g_mapState.selectedId = nextId;
    g_mapState.dirty = true;
    UpdateMapScrollbars(g_mapCanvas);
    SaveMindMap();
    InvalidateRect(g_mapCanvas, nullptr, FALSE);
}

void ApplyFormatting(int action) {
    if (!g_richEdit || !g_selectedNote) return;
    SetFocus(g_richEdit);
    if (action == 0 || action == 1 || action == 2 || action == 3) {
        CHARFORMAT2W format{};
        format.cbSize = sizeof(format);
        if (action <= 1) {
            SendMessageW(g_richEdit, EM_GETCHARFORMAT, SCF_SELECTION,
                         reinterpret_cast<LPARAM>(&format));
            format.dwMask = action == 0 ? CFM_BOLD : CFM_ITALIC;
            const DWORD effect = action == 0 ? CFE_BOLD : CFE_ITALIC;
            format.dwEffects = (format.dwEffects & effect) ? 0 : effect;
        } else {
            format.dwMask = CFM_SIZE | CFM_BOLD | CFM_FACE;
            format.dwEffects = CFE_BOLD;
            format.yHeight = action == 2 ? 640 : 440;
            wcscpy_s(format.szFaceName, L"Segoe UI");
        }
        SendMessageW(g_richEdit, EM_SETCHARFORMAT, SCF_SELECTION,
                     reinterpret_cast<LPARAM>(&format));
    } else if (action == 4 || action == 5) {
        PARAFORMAT2 format{};
        format.cbSize = sizeof(format);
        format.dwMask = PFM_NUMBERING | PFM_NUMBERINGSTYLE | PFM_OFFSET | PFM_OFFSETINDENT;
        format.wNumbering = action == 4 ? PFN_BULLET : PFN_ARABIC;
        format.wNumberingStyle = action == 4 ? PFNS_PLAIN : PFNS_PERIOD;
        format.dxOffset = 220;
        format.dxStartIndent = 440;
        SendMessageW(g_richEdit, EM_SETPARAFORMAT, 0,
                     reinterpret_cast<LPARAM>(&format));
    } else if (action == 6) {
        TABLEROWPARMS row{};
        row.cbRow = sizeof(row);
        row.cbCell = sizeof(TABLECELLPARMS);
        row.cCell = 3;
        row.cRow = 3;
        row.dxCellMargin = 110;
        row.cpStartRow = -1;
        TABLECELLPARMS cells[3]{};
        for (int i = 0; i < 3; ++i) {
            cells[i].dxWidth = 1900 * (i + 1);
            cells[i].dxBrdrLeft = cells[i].dxBrdrRight = 8;
            cells[i].dyBrdrTop = cells[i].dyBrdrBottom = 8;
            cells[i].crBrdrLeft = cells[i].crBrdrRight = RGB(200, 205, 198);
            cells[i].crBrdrTop = cells[i].crBrdrBottom = RGB(200, 205, 198);
            cells[i].crBackPat = g_colors.panel;
        }
        const LRESULT result = SendMessageW(g_richEdit, EM_INSERTTABLE,
            reinterpret_cast<WPARAM>(&row), reinterpret_cast<LPARAM>(cells));
        if (result != S_OK) ShowError(L"目前無法在這個插入位置建立表格。請把游標放在一般文字段落後再試一次。");
    }
    ScheduleAutosave();
}

std::wstring SafeImportedTitle(std::wstring title) {
    for (wchar_t& ch : title) {
        if (ch < 32 || std::wstring_view(L"<>:\"/\\|?*").find(ch) != std::wstring_view::npos)
            ch = L'-';
    }
    while (!title.empty() && (title.back() == L'.' || title.back() == L' ')) title.pop_back();
    if (title.size() > 72) title.resize(72);
    if (title.empty() || title == L"全部筆記") title = L"匯入筆記";
    static constexpr const wchar_t* reserved[] = {
        L"CON", L"PRN", L"AUX", L"NUL", L"COM1", L"COM2", L"COM3", L"COM4",
        L"COM5", L"COM6", L"COM7", L"COM8", L"COM9", L"LPT1", L"LPT2",
        L"LPT3", L"LPT4", L"LPT5", L"LPT6", L"LPT7", L"LPT8", L"LPT9"
    };
    for (const auto* name : reserved) {
        if (_wcsicmp(title.c_str(), name) == 0) { title = L"匯入-" + title; break; }
    }
    return title;
}

void ImportNote() {
    OPENFILENAMEW dialog{};
    wchar_t fileName[32768]{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = g_window;
    dialog.lpstrFilter = L"支援的筆記檔案 (*.txt;*.md;*.rtf)\0*.txt;*.md;*.rtf\0文字檔 (*.txt)\0*.txt\0Markdown (*.md)\0*.md\0RTF 文件 (*.rtf)\0*.rtf\0所有檔案 (*.*)\0*.*\0\0";
    dialog.lpstrFile = fileName;
    dialog.nMaxFile = static_cast<DWORD>(std::size(fileName));
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    dialog.lpstrTitle = L"匯入筆記";
    if (!GetOpenFileNameW(&dialog)) return;

    std::ifstream input(fileName, std::ios::binary);
    if (!input) { ShowError(L"無法讀取所選檔案。"); return; }
    std::vector<char> source(std::istreambuf_iterator<char>(input), {});
    if (source.size() > 32u * 1024u * 1024u) { ShowError(L"檔案太大，首版匯入上限為 32 MB。"); return; }
    if (!input.eof() && input.fail()) { ShowError(L"讀取檔案時發生錯誤。"); return; }

    const std::filesystem::path sourcePath(fileName);
    const std::wstring extension = sourcePath.extension().wstring();
    std::vector<char> rtf;
    if (_wcsicmp(extension.c_str(), L".rtf") == 0) {
        const std::string prefix(source.begin(), source.begin() + static_cast<std::ptrdiff_t>(std::min<size_t>(source.size(), 16)));
        if (prefix.find("{\\rtf") == std::string::npos) { ShowError(L"這個 RTF 檔案格式無法辨識。"); return; }
        rtf = std::move(source);
    } else if (_wcsicmp(extension.c_str(), L".txt") == 0 || _wcsicmp(extension.c_str(), L".md") == 0) {
        const std::wstring decoded = DecodeImportedText(source);
        if (decoded.empty() && !source.empty()) { ShowError(L"無法解碼檔案文字。請使用 UTF-8 或 UTF-16 編碼。"); return; }
        rtf = TextToRtf(decoded);
    } else {
        ShowError(L"目前支援 TXT、Markdown 和 RTF 檔案。"); return;
    }

    if (!SaveCurrentDocument()) return;
    const std::wstring category = g_selectedCategory == kAllNotes ? g_store.DefaultCategory() : g_selectedCategory;
    const auto existing = g_store.ListNotes(category);
    std::wstring desired = SafeImportedTitle(sourcePath.stem().wstring());
    auto titleExists = [&](const std::wstring& title) {
        return std::any_of(existing.begin(), existing.end(), [&](const NoteInfo& item) { return _wcsicmp(item.title.c_str(), title.c_str()) == 0; });
    };
    const std::wstring base = desired;
    int suffix = 2;
    while (titleExists(desired)) desired = base + L" (" + std::to_wstring(suffix++) + L")";

    std::wstring createdTitle, error;
    if (!g_store.CreateNote(category, createdTitle, error)) { ShowError(error); return; }
    if (createdTitle != desired && !g_store.RenameNote(category, createdTitle, desired, error)) {
        std::wstring ignored;
        g_store.DeleteNote(category, createdTitle, ignored);
        ShowError(error);
        return;
    }
    if (!g_store.WriteNoteContents(category, desired, rtf, error)) {
        std::wstring ignored;
        g_store.DeleteNote(category, desired, ignored);
        ShowError(error);
        return;
    }
    g_selectedCategory = category;
    g_searchText.clear();
    SetWindowTextW(g_search, L"");
    RefreshData(false);
    auto it = std::find_if(g_notes.begin(), g_notes.end(), [&](const NoteInfo& item) { return item.title == desired; });
    if (it != g_notes.end()) g_selectedNote = *it;
    LoadSelectedDocument();
    InvalidateRect(g_window, nullptr, TRUE);
}

void ExportNote() {
    if (!g_selectedNote) { ShowError(L"請先選取一篇筆記，再匯出。 "); return; }
    if (!SaveCurrentDocument()) return;

    OPENFILENAMEW dialog{};
    wchar_t fileName[32768]{};
    wcscpy_s(fileName, g_selectedNote->title.c_str());
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = g_window;
    dialog.lpstrFilter = L"RTF 文件（保留格式） (*.rtf)\0*.rtf\0純文字 (*.txt)\0*.txt\0Markdown (*.md)\0*.md\0\0";
    dialog.lpstrFile = fileName;
    dialog.nMaxFile = static_cast<DWORD>(std::size(fileName));
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_OVERWRITEPROMPT;
    dialog.lpstrTitle = L"匯出筆記";
    if (!GetSaveFileNameW(&dialog)) return;

    std::filesystem::path destination(fileName);
    if (destination.extension().empty()) {
        const wchar_t* extension = dialog.nFilterIndex == 2 ? L".txt" :
                                   dialog.nFilterIndex == 3 ? L".md" : L".rtf";
        destination += extension;
    }
    const std::wstring extension = destination.extension().wstring();
    std::vector<char> bytes;
    if (_wcsicmp(extension.c_str(), L".rtf") == 0) {
        if (!StreamRtfFromEditor(bytes)) { ShowError(L"無法讀取筆記的 RTF 內容。 "); return; }
    } else if (_wcsicmp(extension.c_str(), L".txt") == 0 || _wcsicmp(extension.c_str(), L".md") == 0) {
        std::string utf8;
        if (_wcsicmp(extension.c_str(), L".md") == 0) {
            if (!MarkdownFromEditor(utf8)) { ShowError(L"無法轉換筆記內容。 "); return; }
        } else {
            std::wstring plainText;
            if (!StreamPlainTextFromEditor(plainText)) { ShowError(L"無法讀取筆記文字。 "); return; }
            utf8 = ToUtf8(plainText);
        }
        constexpr unsigned char utf8Bom[] = {0xEF, 0xBB, 0xBF};
        bytes.assign(reinterpret_cast<const char*>(utf8Bom),
                     reinterpret_cast<const char*>(utf8Bom) + sizeof(utf8Bom));
        bytes.insert(bytes.end(), utf8.begin(), utf8.end());
    } else {
        ShowError(L"請使用 .rtf、.txt 或 .md 副檔名匯出。 ");
        return;
    }

    std::ofstream output(destination, std::ios::binary | std::ios::trunc);
    if (!output || (!bytes.empty() && !output.write(bytes.data(), static_cast<std::streamsize>(bytes.size())))) {
        ShowError(L"無法寫入匯出檔案，請確認資料夾權限與磁碟空間。 ");
        return;
    }
    MessageBoxW(g_window, (L"已匯出至：\n" + destination.wstring()).c_str(), L"枝記 Notes", MB_OK | MB_ICONINFORMATION);
}

std::wstring CurrentTimerText() {
    wchar_t text[32]{};
    swprintf_s(text, L"%02d:%02d", g_timerSeconds / 60, g_timerSeconds % 60);
    return text;
}

void ToggleTimer() {
    if (g_timerRunning) {
        KillTimer(g_window, kPomodoroTimer);
        g_timerRunning = false;
    } else {
        if (g_timerSeconds <= 0) g_timerSeconds = g_timerPresetMinutes * 60;
        SetTimer(g_window, kPomodoroTimer, 1000, nullptr);
        g_timerRunning = true;
    }
    InvalidateTimerDisplay();
}

void ResetTimer() {
    KillTimer(g_window, kPomodoroTimer);
    g_timerRunning = false;
    g_timerSeconds = g_timerPresetMinutes * 60;
    InvalidateTimerDisplay();
}

void SelectTimerPreset(int minutes) {
    static constexpr int presets[] = {5, 10, 15, 20, 25, 30};
    if (std::find(std::begin(presets), std::end(presets), minutes) == std::end(presets)) return;
    KillTimer(g_window, kPomodoroTimer);
    g_timerRunning = false;
    g_timerPresetMinutes = minutes;
    g_timerSeconds = minutes * 60;
    InvalidateTimerDisplay();
}

void DrawMain(HWND hwnd) {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(hwnd, &paint);
    RECT client{}; GetClientRect(hwnd, &client);
    const int width = client.right;
    const int height = client.bottom;
    Fill(dc, client, g_colors.canvas);
    Fill(dc, SidebarRect(hwnd), g_colors.side);
    Fill(dc, ListRect(hwnd), g_colors.panel);
    Fill(dc, MakeRect(kSidebarWidth + kListWidth, 0, width, height), g_colors.panel);
    DrawRule(dc, kSidebarWidth, 0, kSidebarWidth, height);
    DrawRule(dc, kSidebarWidth + kListWidth, 0, kSidebarWidth + kListWidth, height);
    DrawRule(dc, 0, kHeaderHeight, width, kHeaderHeight);
    DrawTextAt(dc, L"枝記", 22, 18, 62, g_colors.accent, g_titleFont);
    DrawTextAt(dc, L"NOTES", 82, 25, 58, g_colors.muted, g_font);
    DrawTextAt(dc, L"本機筆記工作區", 22, 48, 190, g_colors.muted, g_font);
    DrawTextAt(dc, L"全部筆記", 22, 134, 146, g_selectedCategory == kAllNotes ? g_colors.accent : g_colors.ink, g_font);
    const auto allCount = g_store.ListNotes(kAllNotes).size();
    wchar_t countText[24]{}; swprintf_s(countText, L"%zu", allCount);
    DrawTextLine(dc, countText, MakeRect(166, 134, 210, 159), g_colors.muted, g_font, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    DrawTextAt(dc, L"我的分類", 22, 184, 112, g_colors.muted, g_font);
    RECT addCategory = MakeRect(184, 181, 216, 210);
    RoundBox(dc, addCategory, g_colors.panel, g_colors.line, 9);
    DrawTextLine(dc, L"+", MakeRect(addCategory.left, addCategory.top, addCategory.right, addCategory.bottom), g_colors.accent, g_titleFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    const int catTop = CategoryTop();
    for (size_t i = 0; i < g_categories.size(); ++i) {
        const int y = catTop + static_cast<int>(i) * 38;
        RECT row = MakeRect(12, y, kSidebarWidth - 12, y + 34);
        if (y + 34 < 216 || y > height - 136) continue;
        const bool selected = g_categories[i] == g_selectedCategory;
        if (selected) RoundBox(dc, row, g_colors.soft, g_colors.soft, 9);
        HBRUSH dotBrush = CreateSolidBrush(selected ? g_colors.accent : RGB(199, 168, 128));
        HGDIOBJ oldBrush = SelectObject(dc, dotBrush);
        HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
        Ellipse(dc, 25, y + 13, 33, y + 21);
        SelectObject(dc, oldBrush); SelectObject(dc, oldPen); DeleteObject(dotBrush);
        DrawTextAt(dc, g_categories[i], 42, y + 4, 116, selected ? g_colors.accent : g_colors.ink, g_font);
        wchar_t noteCount[20]{};
        swprintf_s(noteCount, L"%zu", g_store.ListNotes(g_categories[i]).size());
        DrawTextLine(dc, noteCount, MakeRect(164, y + 4, 204, y + 29), g_colors.muted, g_font, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
    DrawTextAt(dc, L"背景色調", 20, height - 112, 90, g_colors.muted, g_font);
    const COLORREF swatches[kPaletteCount] = {RGB(205,198,179), RGB(162,194,166), RGB(210,169,126), RGB(176,169,217), RGB(155,183,203)};
    for (int i = 0; i < kPaletteCount; ++i) {
        const int x = 22 + i * 37;
        RECT swatch = MakeRect(x, height - 82, x + 25, height - 57);
        RoundBox(dc, swatch, swatches[i], i == g_themeIndex ? g_colors.accent : g_colors.line, 9);
        if (i == g_themeIndex) {
            HPEN pen = CreatePen(PS_SOLID, 2, g_colors.accent);
            HGDIOBJ old = SelectObject(dc, pen);
            HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
            RoundRect(dc, swatch.left - 3, swatch.top - 3, swatch.right + 3, swatch.bottom + 3, 11, 11);
            SelectObject(dc, oldBrush); SelectObject(dc, old); DeleteObject(pen);
        }
    }
    DrawTextAt(dc, L"資料只儲存在本機", 20, height - 35, 190, g_colors.muted, g_font);
    DrawTextAt(dc, L"筆記", kSidebarWidth + 22, 20, 150, g_colors.ink, g_titleFont);
    const std::wstring listSubTitle = g_selectedCategory == kAllNotes ? L"所有分類" : g_selectedCategory;
    DrawTextAt(dc, listSubTitle, kSidebarWidth + 22, 48, kListWidth - 40, g_colors.muted, g_font);
    RECT addNote = MakeRect(kSidebarWidth + kListWidth - 49, 15, kSidebarWidth + kListWidth - 17, 47);
    RoundBox(dc, addNote, g_colors.soft, g_colors.soft, 10);
    DrawTextLine(dc, L"+", addNote, g_colors.accent, g_titleFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    const int listTop = NoteTop();
    if (g_notes.empty()) {
        DrawTextAt(dc, g_searchText.empty() ? L"這裡還沒有筆記" : L"沒有符合搜尋的筆記",
                   kSidebarWidth + 22, 147, kListWidth - 38, g_colors.muted, g_font);
        DrawTextAt(dc, L"按右上角 + 建立一篇", kSidebarWidth + 22, 175, kListWidth - 38, g_colors.muted, g_font);
    }
    for (size_t i = 0; i < g_notes.size(); ++i) {
        const auto& note = g_notes[i];
        const int y = listTop + static_cast<int>(i) * 82;
        if (y + 74 < 88 || y > height) continue;
        RECT row = MakeRect(kSidebarWidth + 10, y, kSidebarWidth + kListWidth - 10, y + 74);
        const bool selected = g_selectedNote && g_selectedNote->path == note.path;
        if (selected) RoundBox(dc, row, g_colors.soft, g_colors.soft, 11);
        DrawTextAt(dc, note.title, row.left + 13, y + 11, kListWidth - 52,
                   selected ? g_colors.accent : g_colors.ink, g_font);
        const std::wstring categoryLabel = note.category + L"   ·   本機筆記";
        DrawTextAt(dc, categoryLabel, row.left + 13, y + 39, kListWidth - 50, g_colors.muted, g_font);
    }
    const int detailLeft = kSidebarWidth + kListWidth + 46;
    const int detailWidth = std::max(160, width - detailLeft - 46);
    const int timerLeft = width - 468;
    DrawTextAt(dc, L"枝記  /  " + (g_selectedNote ? g_selectedNote->category : listSubTitle),
               detailLeft, 21, std::max(40, timerLeft - detailLeft - 16), g_colors.muted, g_font);
    RECT timer = MakeRect(timerLeft, 12, timerLeft + 74, 54);
    RoundBox(dc, timer, g_colors.soft, g_colors.soft, 10);
    DrawTextLine(dc, CurrentTimerText(), timer, g_colors.accent, g_titleFont,
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    static constexpr int presets[] = {5, 10, 15, 20, 25, 30};
    for (int i = 0; i < 6; ++i) {
        RECT option = MakeRect(timerLeft + 82 + i * 38, 12, timerLeft + 116 + i * 38, 54);
        const bool selected = presets[i] == g_timerPresetMinutes;
        RoundBox(dc, option, selected ? g_colors.accent : g_colors.panel,
                 selected ? g_colors.accent : g_colors.line, 9);
        DrawTextLine(dc, std::to_wstring(presets[i]) + L"分", option,
            selected ? g_colors.panel : g_colors.ink, g_font, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    RECT timerStart = MakeRect(timerLeft + 310, 12, timerLeft + 390, 54);
    RoundBox(dc, timerStart, g_colors.accent, g_colors.accent, 10);
    DrawTextLine(dc, g_timerRunning ? L"暫停" : L"開始", timerStart, g_colors.panel, g_font,
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    RECT timerReset = MakeRect(timerLeft + 398, 12, timerLeft + 460, 54);
    RoundBox(dc, timerReset, g_colors.panel, g_colors.line, 10);
    DrawTextLine(dc, L"重設", timerReset, g_colors.muted, g_font, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DrawRule(dc, detailLeft, 63, width - 40, 63);
    if (g_selectedNote) {
        DrawTextAt(dc, g_selectedNote->title, detailLeft, 77, std::max(120, detailWidth - 340), g_colors.ink, g_titleFont);
        DrawTextAt(dc, g_saveStatus, detailLeft, 108, 220, g_colors.muted, g_font);
    } else {
        DrawTextAt(dc, L"選取或建立一篇筆記", detailLeft, 78, detailWidth, g_colors.ink, g_titleFont);
    }
    const int tabY = 142;
    const wchar_t* tabs[] = {L"正文", L"心智圖"};
    for (int i = 0; i < 2; ++i) {
        RECT tab = MakeRect(detailLeft + i * 94, tabY, detailLeft + i * 94 + 84, tabY + 32);
        const bool active = (i == 0) == (g_detailMode == DetailMode::Editor);
        if (active) RoundBox(dc, tab, g_colors.soft, g_colors.soft, 9);
        DrawTextLine(dc, tabs[i], tab, active ? g_colors.accent : g_colors.muted, g_font,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    const int actionsY = 181;
    auto chip = [&](int x, int w, const wchar_t* label, bool active = false) {
        RECT r = MakeRect(x, actionsY, x + w, actionsY + 31);
        RoundBox(dc, r, active ? g_colors.soft : g_colors.panel, g_colors.line, 8);
        DrawTextLine(dc, label, MakeRect(r.left + 4, r.top, r.right - 4, r.bottom),
            active ? g_colors.accent : g_colors.ink, g_font, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    };
    chip(width - 205, 78, L"匯出…");
    chip(width - 115, 78, L"匯入…");
    if (g_detailMode == DetailMode::Editor) {
        chip(detailLeft, 48, L"標題"); chip(detailLeft + 54, 48, L"小標");
        chip(detailLeft + 108, 36, L"粗體"); chip(detailLeft + 150, 36, L"斜體");
        chip(detailLeft + 192, 68, L"• 清單"); chip(detailLeft + 266, 68, L"1. 清單");
        chip(detailLeft + 340, 58, L"表格");
    } else {
        chip(detailLeft, 84, L"＋ 節點"); chip(detailLeft + 92, 84, L"刪除節點");
        RECT help = MakeRect(detailLeft, 220, width - 42, 260);
        RoundBox(dc, help, g_colors.soft, g_colors.soft, 9);
        DrawTextLine(dc,
            L"拖曳節點移動；選取後按住 Ctrl，再點另一節點連線；雙擊編輯。使用滾輪與捲軸平移，拖到邊緣可擴展畫布。",
            MakeRect(help.left + 12, help.top + 2, help.right - 12, help.bottom - 2),
            g_colors.ink, g_font, DT_LEFT | DT_VCENTER | DT_WORDBREAK);
    }
    EndPaint(hwnd, &paint);
}

std::optional<size_t> CategoryAt(int y, int height) {
    const int top = CategoryTop();
    if (y < top || y >= height - 132) return std::nullopt;
    const int index = (y - top) / 38;
    if (index < 0 || static_cast<size_t>(index) >= g_categories.size()) return std::nullopt;
    return static_cast<size_t>(index);
}

std::optional<size_t> NoteAt(int y, int height) {
    const int top = NoteTop();
    if (y < top || y >= height) return std::nullopt;
    const int index = (y - top) / 82;
    if (index < 0 || static_cast<size_t>(index) >= g_notes.size()) return std::nullopt;
    return static_cast<size_t>(index);
}

void AddCategory() {
    if (!SaveCurrentDocument()) return;
    auto value = PromptForText(L"新增分類", L"輸入分類名稱");
    if (!value) return;
    std::wstring error;
    if (!g_store.CreateCategory(*value, error)) { ShowError(error); return; }
    g_selectedCategory = *value;
    g_searchText.clear();
    SetWindowTextW(g_search, L"");
    RefreshData(false);
    LoadSelectedDocument();
    Layout(g_window);
}

void RenameCategory(const std::wstring& oldName) {
    if (!SaveCurrentDocument()) return;
    auto value = PromptForText(L"重新命名分類", L"分類名稱", oldName);
    if (!value) return;
    std::wstring error;
    if (!g_store.RenameCategory(oldName, *value, error)) { ShowError(error); return; }
    if (g_selectedCategory == oldName) g_selectedCategory = *value;
    RefreshData();
    LoadSelectedDocument();
    Layout(g_window);
}

void DeleteCategory(const std::wstring& name) {
    if (MessageBoxW(g_window,
        (L"刪除「" + name + L"」分類？其中的筆記會移到「" + g_store.DefaultCategory() + L"」。").c_str(),
        L"刪除分類", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return;
    if (!SaveCurrentDocument()) return;
    std::wstring error;
    if (!g_store.DeleteCategory(name, error)) { ShowError(error); return; }
    if (g_selectedCategory == name) g_selectedCategory = kAllNotes;
    RefreshData(false);
    LoadSelectedDocument();
    Layout(g_window);
}

void AddNote() {
    if (!SaveCurrentDocument()) return;
    std::wstring category = g_selectedCategory == kAllNotes ? g_store.DefaultCategory() : g_selectedCategory;
    std::wstring title, error;
    if (!g_store.CreateNote(category, title, error)) { ShowError(error); return; }
    g_selectedCategory = category;
    g_searchText.clear();
    SetWindowTextW(g_search, L"");
    RefreshData(false);
    auto it = std::find_if(g_notes.begin(), g_notes.end(), [&](const NoteInfo& note) { return note.title == title; });
    if (it != g_notes.end()) g_selectedNote = *it;
    LoadSelectedDocument();
    Layout(g_window);
    InvalidateRect(g_window, nullptr, TRUE);
}

void RenameNote(const NoteInfo& note) {
    if (!SaveCurrentDocument()) return;
    auto value = PromptForText(L"重新命名筆記", L"筆記標題", note.title);
    if (!value) return;
    std::wstring error;
    if (!g_store.RenameNote(note.category, note.title, *value, error)) { ShowError(error); return; }
    g_searchText.clear();
    SetWindowTextW(g_search, L"");
    RefreshData(false);
    auto it = std::find_if(g_notes.begin(), g_notes.end(), [&](const NoteInfo& item) { return item.title == *value; });
    if (it != g_notes.end()) g_selectedNote = *it;
    LoadSelectedDocument();
    Layout(g_window);
    InvalidateRect(g_window, nullptr, TRUE);
}

void DeleteNote(const NoteInfo& note) {
    if (MessageBoxW(g_window, (L"將「" + note.title + L"」移到資源回收筒？").c_str(),
                    L"刪除筆記", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return;
    if (!SaveCurrentDocument()) return;
    std::wstring error;
    if (!g_store.DeleteNote(note.category, note.title, error)) { ShowError(error); return; }
    RefreshData(false);
    LoadSelectedDocument();
    Layout(g_window);
}

void MoveNote(const NoteInfo& note, const std::wstring& target) {
    if (!SaveCurrentDocument()) return;
    std::wstring error;
    if (!g_store.MoveNote(note.category, note.title, target, error)) { ShowError(error); return; }
    g_selectedCategory = target;
    RefreshData(false);
    auto it = std::find_if(g_notes.begin(), g_notes.end(), [&](const NoteInfo& item) { return item.title == note.title; });
    if (it != g_notes.end()) g_selectedNote = *it;
    LoadSelectedDocument();
    Layout(g_window);
    InvalidateRect(g_window, nullptr, TRUE);
}

void ShowCategoryMenu(const std::wstring& category, POINT screenPoint) {
    HMENU menu = CreatePopupMenu();
    const UINT state = category == g_store.DefaultCategory() ? MF_STRING | MF_GRAYED : MF_STRING;
    AppendMenuW(menu, state, kCategoryRename, L"重新命名分類");
    AppendMenuW(menu, state, kCategoryDelete, L"刪除分類…");
    const UINT command = TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD,
                                         screenPoint.x, screenPoint.y, 0, g_window, nullptr);
    DestroyMenu(menu);
    if (command == kCategoryRename) RenameCategory(category);
    else if (command == kCategoryDelete) DeleteCategory(category);
}

void ShowNoteMenu(const NoteInfo& note, POINT screenPoint) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kNoteRename, L"重新命名");
    AppendMenuW(menu, MF_STRING, kNoteDelete, L"移至資源回收筒");
    HMENU moveMenu = CreatePopupMenu();
    g_moveTargets.clear();
    for (const auto& category : g_categories) {
        if (category == note.category) continue;
        g_moveTargets.push_back(category);
        AppendMenuW(moveMenu, MF_STRING,
                    kMoveCategoryFirst + static_cast<UINT>(g_moveTargets.size() - 1), category.c_str());
    }
    if (!g_moveTargets.empty()) AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(moveMenu), L"移至分類");
    else DestroyMenu(moveMenu);
    const UINT command = TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD,
                                         screenPoint.x, screenPoint.y, 0, g_window, nullptr);
    DestroyMenu(menu);
    if (command == kNoteRename) RenameNote(note);
    else if (command == kNoteDelete) DeleteNote(note);
    else if (command >= kMoveCategoryFirst && command < kMoveCategoryFirst + g_moveTargets.size())
        MoveNote(note, g_moveTargets[command - kMoveCategoryFirst]);
}

void Layout(HWND window) {
    RECT client{}; GetClientRect(window, &client);
    const int listSearchWidth = kSidebarWidth - 42;
    if (g_search) MoveWindow(g_search, 18, 80, listSearchWidth, 34, TRUE);
    const int left = kSidebarWidth + kListWidth + 46;
    const int top = 222;
    const int width = std::max(160, static_cast<int>(client.right) - left - 46);
    const int editorHeight = std::max(100, static_cast<int>(client.bottom) - top - 30);
    if (g_richEdit) {
        MoveWindow(g_richEdit, left, top, width, editorHeight, TRUE);
        ShowWindow(g_richEdit, g_detailMode == DetailMode::Editor ? SW_SHOW : SW_HIDE);
        EnableWindow(g_richEdit, g_selectedNote.has_value());
    }
    if (g_mapCanvas) {
        const int mapTop = top + 48;
        const int mapHeight = std::max(100, static_cast<int>(client.bottom) - mapTop - 30);
        MoveWindow(g_mapCanvas, left, mapTop, width, mapHeight, TRUE);
        UpdateMapScrollbars(g_mapCanvas);
        ShowWindow(g_mapCanvas, g_detailMode == DetailMode::MindMap ? SW_SHOW : SW_HIDE);
        EnableWindow(g_mapCanvas, g_selectedNote.has_value());
    }
}

LRESULT CALLBACK MainWindowProcedure(HWND window, UINT message,
                                     WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        g_window = window;
        g_font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        g_titleFont = CreateFontW(-22, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        std::wstring error;
        if (!g_store.Initialize(error)) {
            MessageBoxW(window, error.c_str(), L"枝記 Notes", MB_OK | MB_ICONERROR);
            return -1;
        }
        ApplyTheme(g_store.LoadThemeIndex(), false);
        g_search = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            18, 80, 190, 34, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSearchControl)), g_instance, nullptr);
        SendMessageW(g_search, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
        SendMessageW(g_search, EM_SETCUEBANNER, TRUE,
                     reinterpret_cast<LPARAM>(L"搜尋筆記標題…"));
        g_richEdit = CreateWindowExW(WS_EX_CLIENTEDGE, MSFTEDIT_CLASS, L"",
            WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL |
            ES_WANTRETURN | ES_NOHIDESEL,
            594, 210, 500, 500, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRichEditControl)), g_instance, nullptr);
        if (!g_richEdit) {
            MessageBoxW(window, L"無法建立 Windows 內建的文字編輯器。", L"枝記 Notes", MB_OK | MB_ICONERROR);
            return -1;
        }
        SendMessageW(g_richEdit, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
        SendMessageW(g_richEdit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELONG(18, 18));
        SendMessageW(g_richEdit, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_SELCHANGE);
        CHARFORMAT2W base{}; base.cbSize = sizeof(base); base.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR;
        base.yHeight = 220; base.crTextColor = g_colors.ink; wcscpy_s(base.szFaceName, L"Segoe UI");
        SendMessageW(g_richEdit, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&base));
        g_mapCanvas = CreateWindowExW(WS_EX_CLIENTEDGE, kMapCanvasClassName, L"",
            WS_CHILD | WS_TABSTOP | WS_HSCROLL | WS_VSCROLL, 594, 210, 500, 500, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kMapCanvasControl)), g_instance, nullptr);
        RefreshData(false);
        LoadSelectedDocument();
        Layout(window);
        return 0;
    }
    case WM_SIZE:
        Layout(window);
        InvalidateRect(window, nullptr, TRUE);
        return 0;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize.x = 1260;
        info->ptMinTrackSize.y = 640;
        return 0;
    }
    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetBkColor(dc, g_colors.field);
        SetTextColor(dc, g_colors.ink);
        return reinterpret_cast<LRESULT>(g_fieldBrush);
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, g_colors.muted);
        return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == kSearchControl && HIWORD(wParam) == EN_CHANGE) {
            const auto oldPath = g_selectedNote ? g_selectedNote->path : std::filesystem::path{};
            if (!SaveCurrentDocument()) return 0;
            g_searchText = ReadControlText(g_search);
            g_noteScroll = 0;
            RefreshData(false, false);
            const bool selectionChanged = !g_selectedNote || g_selectedNote->path != oldPath;
            if (selectionChanged) LoadSelectedDocument();
            RECT client{}; GetClientRect(window, &client);
            RECT noteList = MakeRect(kSidebarWidth, kHeaderHeight, kSidebarWidth + kListWidth, client.bottom);
            InvalidateRect(window, &noteList, FALSE);
            if (selectionChanged) {
                const int detailLeft = kSidebarWidth + kListWidth + 46;
                RECT detailHeader = MakeRect(detailLeft, 68, client.right, 136);
                InvalidateRect(window, &detailHeader, FALSE);
            }
        } else if (LOWORD(wParam) == kRichEditControl && HIWORD(wParam) == EN_CHANGE) {
            ScheduleAutosave();
        }
        return 0;
    case WM_LBUTTONDOWN: {
        const int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        RECT client{}; GetClientRect(window, &client);
        if (x < kSidebarWidth) {
            if (y >= client.bottom - 90 && y <= client.bottom - 48) {
                const int index = (x - 22) / 37;
                if (index >= 0 && index < kPaletteCount && x >= 22 && x < 22 + kPaletteCount * 37)
                    ApplyTheme(index, true);
                return 0;
            }
            if (y >= 181 && y <= 211 && x >= 184) { AddCategory(); return 0; }
            if (y >= 130 && y <= 166) {
                if (!SaveCurrentDocument()) return 0;
                g_selectedCategory = kAllNotes;
                RefreshData(false);
                LoadSelectedDocument(); Layout(window);
                return 0;
            }
            if (auto index = CategoryAt(y, client.bottom)) {
                if (!SaveCurrentDocument()) return 0;
                g_selectedCategory = g_categories[*index];
                g_noteScroll = 0;
                RefreshData(false);
                LoadSelectedDocument(); Layout(window);
                return 0;
            }
        } else if (x < kSidebarWidth + kListWidth) {
            if (y >= 12 && y <= 52 && x >= kSidebarWidth + kListWidth - 56) {
                AddNote(); return 0;
            }
            if (auto index = NoteAt(y, client.bottom)) {
                if (!SaveCurrentDocument()) return 0;
                g_selectedNote = g_notes[*index];
                LoadSelectedDocument(); Layout(window);
                InvalidateRect(window, nullptr, TRUE);
                return 0;
            }
        } else {
            const int detailLeft = kSidebarWidth + kListWidth + 46;
            const int timerLeft = client.right - 468;
            if (y >= 8 && y <= 60 && x >= timerLeft && x <= timerLeft + 460) {
                if (x >= timerLeft + 82 && x < timerLeft + 310) {
                    const int index = (x - timerLeft - 82) / 38;
                    static constexpr int presets[] = {5, 10, 15, 20, 25, 30};
                    if (index >= 0 && index < 6) SelectTimerPreset(presets[index]);
                } else if (x >= timerLeft + 310 && x < timerLeft + 390) ToggleTimer();
                else if (x >= timerLeft + 398 && x <= timerLeft + 460) ResetTimer();
                return 0;
            }
            if (y >= 142 && y <= 174 && x >= detailLeft && x <= detailLeft + 180) {
                if (!SaveCurrentDocument()) return 0;
                g_detailMode = x < detailLeft + 90 ? DetailMode::Editor : DetailMode::MindMap;
                Layout(window); InvalidateRect(window, nullptr, FALSE); return 0;
            }
            if (y >= 181 && y <= 213 && x >= client.right - 210 && x < client.right - 126) { ExportNote(); return 0; }
            if (y >= 181 && y <= 213 && x >= client.right - 120) { ImportNote(); return 0; }
            if (y >= 181 && y <= 213 && x >= detailLeft && g_detailMode == DetailMode::Editor) {
                if (x >= detailLeft && x < detailLeft + 48) ApplyFormatting(2);
                else if (x < detailLeft + 102) ApplyFormatting(3);
                else if (x < detailLeft + 146) ApplyFormatting(0);
                else if (x < detailLeft + 188) ApplyFormatting(1);
                else if (x < detailLeft + 262) ApplyFormatting(4);
                else if (x < detailLeft + 336) ApplyFormatting(5);
                else if (x < detailLeft + 400) ApplyFormatting(6);
                return 0;
            }
            if (y >= 181 && y <= 213 && x >= detailLeft && g_detailMode == DetailMode::MindMap) {
                if (x >= detailLeft && x < detailLeft + 88) AddMapNode();
                else if (x >= detailLeft + 92 && x < detailLeft + 180) DeleteSelectedMapNode();
                return 0;
            }
        }
        break;
    }
    case WM_RBUTTONUP: {
        const int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        POINT screen{ x, y };
        ClientToScreen(window, &screen);
        RECT client{}; GetClientRect(window, &client);
        if (x < kSidebarWidth) {
            if (auto index = CategoryAt(y, client.bottom)) ShowCategoryMenu(g_categories[*index], screen);
        } else if (x < kSidebarWidth + kListWidth) {
            if (auto index = NoteAt(y, client.bottom)) ShowNoteMenu(g_notes[*index], screen);
        }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(window, &point);
        const int amount = GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? -3 : 3;
        RECT client{}; GetClientRect(window, &client);
        if (point.x < kSidebarWidth) {
            const int available = static_cast<int>(client.bottom) - 365;
            const int maxScroll = std::max(0, static_cast<int>(g_categories.size()) * 38 - available);
            g_categoryScroll = std::clamp(g_categoryScroll + amount * 38, 0, maxScroll);
        } else if (point.x < kSidebarWidth + kListWidth) {
            const int available = static_cast<int>(client.bottom) - 139;
            const int maxScroll = std::max(0, static_cast<int>(g_notes.size()) * 82 - available);
            g_noteScroll = std::clamp(g_noteScroll + amount * 82, 0, maxScroll);
        } else if (g_detailMode == DetailMode::MindMap && g_mapCanvas) {
            if (GetKeyState(VK_SHIFT) & 0x8000)
                ScrollMapTo(g_mapCanvas, g_mapState.scrollX + amount * 24, g_mapState.scrollY);
            else
                ScrollMapTo(g_mapCanvas, g_mapState.scrollX, g_mapState.scrollY + amount * 24);
            return 0;
        }
        InvalidateRect(window, nullptr, TRUE);
        return 0;
    }
    case WM_TIMER:
        if (wParam == kAutosaveTimer) {
            KillTimer(window, kAutosaveTimer);
            SaveCurrentDocument();
            return 0;
        }
        if (wParam == kPomodoroTimer) {
            if (--g_timerSeconds <= 0) {
                KillTimer(window, kPomodoroTimer);
                g_timerRunning = false;
                g_timerSeconds = 0;
                MessageBeep(MB_ICONASTERISK);
            }
            InvalidateTimerDisplay();
            return 0;
        }
        break;
    case WM_CLOSE:
        KillTimer(window, kPomodoroTimer);
        KillTimer(window, kAutosaveTimer);
        if (!SaveCurrentDocument() && MessageBoxW(window,
            L"最後一次儲存失敗。仍要關閉並放棄未儲存的變更嗎？", L"枝記 Notes",
            MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return 0;
        DestroyWindow(window);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        DrawMain(window);
        return 0;
    case WM_DESTROY:
        if (g_font) DeleteObject(g_font);
        if (g_titleFont) DeleteObject(g_titleFont);
        if (g_fieldBrush) DeleteObject(g_fieldBrush);
        if (g_richEditModule) FreeLibrary(g_richEditModule);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

namespace branchnotes::platform {

int RunMainWindow(HINSTANCE instance, int showCommand) {
    g_instance = instance;
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    g_richEditModule = LoadLibraryW(L"Msftedit.dll");
    if (!g_richEditModule) return static_cast<int>(GetLastError());
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = MainWindowProcedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = kWindowClassName;
    if (!RegisterClassExW(&windowClass)) return static_cast<int>(GetLastError());

    WNDCLASSEXW promptClass{};
    promptClass.cbSize = sizeof(promptClass);
    promptClass.lpfnWndProc = PromptProcedure;
    promptClass.hInstance = instance;
    promptClass.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
    promptClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    promptClass.lpszClassName = kPromptClassName;
    if (!RegisterClassExW(&promptClass)) return static_cast<int>(GetLastError());

    WNDCLASSEXW mapClass{};
    mapClass.cbSize = sizeof(mapClass);
    mapClass.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    mapClass.lpfnWndProc = MapCanvasProcedure;
    mapClass.hInstance = instance;
    mapClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    mapClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    mapClass.lpszClassName = kMapCanvasClassName;
    if (!RegisterClassExW(&mapClass)) return static_cast<int>(GetLastError());

    HWND window = CreateWindowExW(WS_CLIPCHILDREN, kWindowClassName, kWindowTitle,
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1220, 820,
        nullptr, nullptr, instance, nullptr);
    if (!window) return static_cast<int>(GetLastError());
    ShowWindow(window, showCommand);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

} // namespace branchnotes::platform
