#include <windows.h>
#include <windowsx.h>
#include <string>

namespace {
constexpr int kSidebar = 210;
constexpr int kTimer = 1001;
constexpr int kNewNote = 1003;
constexpr COLORREF kInk = RGB(42, 47, 48);
constexpr COLORREF kMuted = RGB(126, 132, 132);
constexpr COLORREF kLine = RGB(229, 230, 224);
constexpr COLORREF kGreen = RGB(95, 121, 99);
constexpr COLORREF kPaper = RGB(255, 254, 250);

HWND g_window{};
HWND g_title{};
HWND g_body{};
HWND g_timerButton{};
HWND g_newNoteButton{};
bool g_running{};
bool g_mint{};
int g_seconds = 25 * 60;

void Text(HDC dc, int x, int y, const wchar_t* value, COLORREF color = kInk,
          int size = 15, bool bold = false) {
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    HFONT font = CreateFontW(-size, 0, 0, 0, bold ? FW_SEMIBOLD : FW_NORMAL,
                             FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    auto old = SelectObject(dc, font);
    TextOutW(dc, x, y, value, static_cast<int>(wcslen(value)));
    SelectObject(dc, old);
    DeleteObject(font);
}

void Fill(HDC dc, RECT r, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &r, brush);
    DeleteObject(brush);
}

void Line(HDC dc, int x1, int y1, int x2, int y2) {
    HPEN pen = CreatePen(PS_SOLID, 1, kLine);
    auto old = SelectObject(dc, pen);
    MoveToEx(dc, x1, y1, nullptr);
    LineTo(dc, x2, y2);
    SelectObject(dc, old);
    DeleteObject(pen);
}

void UpdateTimerLabel() {
    wchar_t text[64];
    swprintf_s(text, L"%02d:%02d", g_seconds / 60, g_seconds % 60);
    SetWindowTextW(g_timerButton, g_running ? L"Ⅱ  暫停" : L"▶  開始");
    InvalidateRect(g_window, nullptr, FALSE);
}

void Layout(HWND hwnd) {
    RECT r{};
    GetClientRect(hwnd, &r);
    const int width = r.right;
    const int height = r.bottom;
    MoveWindow(g_title, kSidebar + 48, 158,
               width - kSidebar - 48 - 315, 42, TRUE);
    MoveWindow(g_body, kSidebar + 48, 282,
               width - kSidebar - 48 - 315, height - 340, TRUE);
    MoveWindow(g_timerButton, width - 164, 190, 116, 31, TRUE);
    MoveWindow(g_newNoteButton, 145, 280, 40, 30, TRUE);
}

void Paint(HWND hwnd) {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(hwnd, &ps);
    RECT client{};
    GetClientRect(hwnd, &client);
    const int width = client.right;
    const int height = client.bottom;
    const COLORREF bg = g_mint ? RGB(235, 243, 237) : RGB(245, 243, 237);
    Fill(dc, client, bg);
    Fill(dc, RECT{0, 0, kSidebar, height}, RGB(251, 250, 246));
    Fill(dc, RECT{kSidebar, 0, width, height}, kPaper);
    Fill(dc, RECT{width - 292, 0, width, height}, RGB(251, 250, 246));
    Line(dc, kSidebar, 0, kSidebar, height);
    Line(dc, width - 292, 0, width - 292, height);
    Line(dc, kSidebar, 62, width, 62);

    Text(dc, 24, 22, L"枝記", kGreen, 22, true);
    Text(dc, 87, 28, L"NOTES", kMuted, 10, true);
    Text(dc, 24, 95, L"⌕  搜尋筆記…", kMuted, 13);
    Text(dc, 24, 145, L"▤  所有筆記", kGreen, 14, true);
    Text(dc, 24, 183, L"◷  最近編輯", kMuted, 14);
    Text(dc, 24, 221, L"☆  收藏", kMuted, 14);
    Text(dc, 24, 285, L"我的分類", kMuted, 11, true);
    Text(dc, 28, 322, L"●  學習筆記", kGreen, 14, true);
    Text(dc, 28, 360, L"●  工作專案", kMuted, 14);
    Text(dc, 28, 398, L"●  生活靈感", kMuted, 14);
    Text(dc, 28, 436, L"●  閱讀摘錄", kMuted, 14);
    Text(dc, kSidebar + 32, 23, L"學習筆記   /   專注力與時間管理", kMuted, 13);

    Text(dc, kSidebar + 48, 98, L"學習筆記  ·  時間管理", kGreen, 12, true);
    Text(dc, kSidebar + 48, 223, L"最近編輯：今天 10:42     ·     自動儲存 ●", kMuted, 12);
    Line(dc, kSidebar + 48, 260, width - 338, 260);
    Text(dc, kSidebar + 48, 278, L"Aa    H₂    ☷    ▦    ☑", kMuted, 13, true);
    Text(dc, width - 270, 96, L"工作台", kInk, 16, true);
    Text(dc, width - 270, 132, L"專注計時器", kMuted, 12);
    wchar_t timer[32];
    swprintf_s(timer, L"%02d:%02d", g_seconds / 60, g_seconds % 60);
    Text(dc, width - 270, 161, timer, kInk, 29, true);
    Text(dc, width - 270, 216, L"筆記背景", kInk, 14, true);
    Text(dc, width - 270, 245, L"選擇閱讀氛圍", kMuted, 11);
    const COLORREF colors[] = {RGB(245,243,237), RGB(225,239,229), RGB(242,226,210), RGB(229,228,246)};
    for (int i = 0; i < 4; ++i) {
        RECT swatch{width - 270 + i * 39, 270, width - 242 + i * 39, 298};
        HBRUSH brush = CreateSolidBrush(colors[i]);
        HPEN pen = CreatePen(PS_SOLID, g_mint && i == 1 ? 2 : 1,
                             g_mint && i == 1 ? kGreen : RGB(214, 216, 210));
        auto oldBrush = SelectObject(dc, brush);
        auto oldPen = SelectObject(dc, pen);
        RoundRect(dc, swatch.left, swatch.top, swatch.right, swatch.bottom, 8, 8);
        SelectObject(dc, oldBrush); SelectObject(dc, oldPen);
        DeleteObject(brush); DeleteObject(pen);
    }
    Text(dc, width - 270, 311, L"關聯筆記", kInk, 14, true);
    Text(dc, width - 270, 343, L"◷  番茄工作法入門", kMuted, 12);
    Text(dc, width - 270, 374, L"▤  每週學習回顧", kMuted, 12);
    Text(dc, width - 270, 405, L"☆  建立好習慣", kMuted, 12);

    Text(dc, kSidebar + 48, height - 204, L"想法地圖", kInk, 16, true);
    RECT map{kSidebar + 48, height - 170, width - 338, height - 35};
    Fill(dc, map, RGB(250, 250, 247));
    FrameRect(dc, &map, reinterpret_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
    const int cx = (map.left + map.right) / 2;
    const int cy = (map.top + map.bottom) / 2;
    HPEN edge = CreatePen(PS_SOLID, 2, RGB(184, 196, 180));
    auto oldPen = SelectObject(dc, edge);
    MoveToEx(dc, cx, cy, nullptr); LineTo(dc, map.left + 75, map.top + 28);
    MoveToEx(dc, cx, cy, nullptr); LineTo(dc, map.right - 75, map.top + 28);
    MoveToEx(dc, cx, cy, nullptr); LineTo(dc, map.left + 75, map.bottom - 25);
    MoveToEx(dc, cx, cy, nullptr); LineTo(dc, map.right - 75, map.bottom - 25);
    SelectObject(dc, oldPen); DeleteObject(edge);
    Text(dc, cx - 27, cy - 10, L"專注力", kGreen, 14, true);
    Text(dc, map.left + 22, map.top + 15, L"環境整理", kMuted, 12);
    Text(dc, map.right - 102, map.top + 15, L"時間區塊", kMuted, 12);
    Text(dc, map.left + 25, map.bottom - 39, L"休息節奏", kMuted, 12);
    Text(dc, map.right - 100, map.bottom - 39, L"每週回顧", kMuted, 12);
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_window = hwnd;
        g_title = CreateWindowExW(0, L"EDIT", L"專注力與時間管理",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd,
            nullptr, GetModuleHandleW(nullptr), nullptr);
        SendMessageW(g_title, WM_SETFONT,
            reinterpret_cast<WPARAM>(CreateFontW(-29,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,
                DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
                DEFAULT_PITCH,L"Segoe UI")), TRUE);
        g_body = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
            L"把注意力放回重要的事\r\n\r\n專注不是把所有時間塞滿，而是有意識地選擇此刻要投入的事情。\r\n\r\n✦  給自己一個清楚的起點：只做 25 分鐘，休息 5 分鐘。\r\n\r\n本週行動清單\r\n• 每天早上選出今天最重要的 3 件事\r\n• 工作時關閉不必要的通知\r\n• 每完成一段專注時間，就短暫離開座位\r\n\r\n一週專注紀錄\r\n週一     3 × 25 分鐘     讀完第 2 章     完成\r\n週二     2 × 25 分鐘     整理課堂摘要   完成",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL,
            0, 0, 0, 0, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        SendMessageW(g_body, WM_SETFONT,
            reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        g_timerButton = CreateWindowExW(0, L"BUTTON", L"▶  開始專注",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_FLAT, 0, 0, 0, 0,
            hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTimer)), GetModuleHandleW(nullptr), nullptr);
        g_newNoteButton = CreateWindowExW(0, L"BUTTON", L"＋",
            WS_CHILD | WS_VISIBLE | BS_FLAT, 0, 0, 0, 0,
            hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kNewNote)),
            GetModuleHandleW(nullptr), nullptr);
        Layout(hwnd);
        return 0;
    }
    case WM_SIZE: Layout(hwnd); return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == kTimer) {
            g_running = !g_running;
            if (g_running) SetTimer(hwnd, 1, 1000, nullptr);
            else KillTimer(hwnd, 1);
            UpdateTimerLabel();
        } else if (LOWORD(wp) == kNewNote) {
            SetWindowTextW(g_title, L"未命名筆記");
            MessageBoxW(hwnd, L"這是界面样稿。筆記建立和保存功能尚未接入。",
                        L"新筆記（Demo）", MB_OK | MB_ICONINFORMATION);
        }
        return 0;
    case WM_LBUTTONDOWN: {
        const int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        RECT r{}; GetClientRect(hwnd, &r);
        const int swatchY = 270, firstX = r.right - 270;
        if (y >= swatchY && y <= swatchY + 28 && x >= firstX && x < firstX + 145) {
            const int choice = (x - firstX) / 39;
            g_mint = choice == 1;
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        }
        break;
    }
    case WM_TIMER:
        if (g_seconds > 0) --g_seconds;
        else { KillTimer(hwnd, 1); g_running = false; MessageBeep(MB_ICONASTERISK); }
        UpdateTimerLabel();
        return 0;
    case WM_PAINT: Paint(hwnd); return 0;
    case WM_DESTROY: KillTimer(hwnd, 1); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    const wchar_t className[] = L"BranchNotesMockup";
    WNDCLASSW wc{};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = className;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    if (!RegisterClassW(&wc)) return 1;
    HWND hwnd = CreateWindowExW(0, className, L"枝記 Notes — 最小界面方案",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1200, 820,
        nullptr, nullptr, instance, nullptr);
    if (!hwnd) return 2;
    ShowWindow(hwnd, showCommand);
    UpdateWindow(hwnd);
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}
