#include "WinInputCommon.h"

#define ID_CONNECT 201
#define ID_CLIP_PUSH 202
#define ID_CLIP_PULL 203
#define WM_REMOTE_KEY (WM_APP + 1)

HWND window, host_field, port_field, secret_field, status_field, connect_button;
SOCKET connection = INVALID_SOCKET;
HHOOK keyboard_hook = 0;
std::wstring secret;
bool pressed[256] = {};
WORD pressed_scan[256] = {};
bool pressed_extended[256] = {};

void status(const wchar_t* text) { SetWindowTextW(status_field, text); }

void drop_connection(const wchar_t* reason) {
    if (connection != INVALID_SOCKET) { closesocket(connection); connection = INVALID_SOCKET; }
    if (keyboard_hook) { UnhookWindowsHookEx(keyboard_hook); keyboard_hook = 0; }
    memset(pressed, 0, sizeof(pressed));
    EnableWindow(host_field, TRUE); EnableWindow(port_field, TRUE);
    EnableWindow(secret_field, TRUE); EnableWindow(connect_button, TRUE);
    status(reason);
}

bool send_frame(const char* command, const std::string& value) {
    if (connection == INVALID_SOCKET) return false;
    if (write_frame(connection, utf8(secret), command, value)) return true;
    drop_connection(L"连接断开，请重新连接");
    return false;
}

bool send_event(WORD vk, WORD scan, bool extended, bool down) {
    char event[48];
    sprintf_s(event, "%u,%u,%u,%u", vk, scan, extended ? 1 : 0, down ? 1 : 0);
    return send_frame("event", event);
}

bool send_event(const std::string& value) { return send_frame("clip_put", value); }

void release_keys() {
    for (int vk = 1; vk < 256; ++vk) {
        if (pressed[vk]) {
            pressed[vk] = false;
            send_event((WORD)vk, pressed_scan[vk], pressed_extended[vk], false);
        }
    }
}

LRESULT CALLBACK keyboard(int code, WPARAM message, LPARAM value) {
    if (code == HC_ACTION && connection != INVALID_SOCKET && GetForegroundWindow() == window) {
        const KBDLLHOOKSTRUCT* event = (const KBDLLHOOKSTRUCT*)value;
        if (event->vkCode > 0 && event->vkCode < 256 && !(event->flags & LLKHF_INJECTED)) {
            bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
            bool up = message == WM_KEYUP || message == WM_SYSKEYUP;
            if (down || up) {
                WORD vk = (WORD)event->vkCode;
                pressed[vk] = down;
                pressed_scan[vk] = (WORD)event->scanCode;
                pressed_extended[vk] = (event->flags & LLKHF_EXTENDED) != 0;
                LPARAM data = vk | ((LPARAM)(event->scanCode & 0x1ff) << 8)
                    | ((LPARAM)((event->flags & LLKHF_EXTENDED) != 0) << 17)
                    | ((LPARAM)up << 18);
                if (PostMessageW(window, WM_REMOTE_KEY, 0, data)) return 1;
            }
        }
    }
    return CallNextHookEx(keyboard_hook, code, message, value);
}

LRESULT CALLBACK proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_REMOTE_KEY) {
        if (connection == INVALID_SOCKET) return 0;
        send_event((WORD)(lParam & 0xff), (WORD)((lParam >> 8) & 0x1ff),
            ((lParam >> 17) & 1) != 0, ((lParam >> 18) & 1) == 0);
        return 0;
    }
    if (message == WM_ACTIVATE && LOWORD(wParam) == WA_INACTIVE) {
        MSG pending;
        while (PeekMessageW(&pending, hwnd, WM_REMOTE_KEY, WM_REMOTE_KEY, PM_REMOVE))
            send_event((WORD)(pending.lParam & 0xff), (WORD)((pending.lParam >> 8) & 0x1ff),
                ((pending.lParam >> 17) & 1) != 0, ((pending.lParam >> 18) & 1) == 0);
        release_keys();
        return 0;
    }
    if (message == WM_COMMAND && LOWORD(wParam) == ID_CLIP_PUSH && HIWORD(wParam) == BN_CLICKED) {
        bool skipped = false;
        std::string data = clip_serialize(skipped);
        if (data.empty()) { status(L"本地剪贴板为空或没有可同步的格式"); return 0; }
        if (!send_event(data)) return 0;
        std::string frame, from, command, value;
        if (!read_frame(connection, frame) || !split_frame(frame, from, command, value)) { drop_connection(L"接收端无响应，连接已断开"); return 0; }
        if (command == "clip_ack") status((std::wstring(L"已推送，接收端写入 ") + std::to_wstring(atoi(value.c_str())) + L" 个格式" + (skipped ? L"（部分格式被跳过）" : L"")).c_str());
        else if (command == "clip_fail") status(wide(value).c_str());
        else status(L"接收端返回异常响应");
        return 0;
    }
    if (message == WM_COMMAND && LOWORD(wParam) == ID_CLIP_PULL && HIWORD(wParam) == BN_CLICKED) {
        if (connection == INVALID_SOCKET) return 0;
        int timeout = 8000;
        setsockopt(connection, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
        if (!write_frame(connection, utf8(secret), "clip_get", "")) { drop_connection(L"请求失败，连接已断开"); return 0; }
        std::string frame, from, command, value;
        if (!read_frame(connection, frame) || !split_frame(frame, from, command, value)) { drop_connection(L"接收端无响应，连接已断开"); return 0; }
        timeout = 500;
        setsockopt(connection, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
        if (command == "clip_put") {
            if (clip_deserialize(value) < 0) status(L"写入本地剪贴板失败");
            else status(L"已拉取接收端剪贴板");
        } else if (command == "clip_fail") {
            status(wide(value).c_str());
        } else {
            status(L"接收端返回异常响应");
        }
        return 0;
    }
    if (message == WM_COMMAND && LOWORD(wParam) == ID_CONNECT && HIWORD(wParam) == BN_CLICKED) {
        wchar_t host[128], port_text[16], key[256];
        GetWindowTextW(host_field, host, 128);
        GetWindowTextW(port_field, port_text, 16);
        GetWindowTextW(secret_field, key, 256);
        secret = key;
        int port = _wtoi(port_text);
        if (secret.size() < 16 || port < 1 || port > 65535) {
            MessageBoxW(hwnd, L"密钥至少16字符，端口必须是1到65535", L"错误", MB_ICONERROR);
            return 0;
        }
        sockaddr_in address = {};
        address.sin_family = AF_INET;
        address.sin_port = htons((u_short)port);
        if (InetPtonW(AF_INET, host, &address.sin_addr) != 1) {
            MessageBoxW(hwnd, L"请填写有效的IPv4地址", L"错误", MB_ICONERROR);
            return 0;
        }
        connection = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (connection == INVALID_SOCKET || connect(connection, (sockaddr*)&address, sizeof(address)) == SOCKET_ERROR) {
            int error = WSAGetLastError();
            if (connection != INVALID_SOCKET) closesocket(connection);
            connection = INVALID_SOCKET;
            wchar_t text[128]; wsprintfW(text, L"连接失败，错误码：%d", error);
            MessageBoxW(hwnd, text, L"错误", MB_ICONERROR);
            return 0;
        }
        int timeout = 500;
        setsockopt(connection, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
        keyboard_hook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboard, GetModuleHandleW(0), 0);
        if (!keyboard_hook) {
            closesocket(connection); connection = INVALID_SOCKET;
            MessageBoxW(hwnd, L"键盘监听安装失败", L"错误", MB_ICONERROR);
            return 0;
        }
        EnableWindow(host_field, FALSE); EnableWindow(port_field, FALSE);
        EnableWindow(secret_field, FALSE); EnableWindow(connect_button, FALSE);
        status(L"已连接：仅本窗口在前台时转发按键；鼠标关闭窗口退出");
        SetFocus(hwnd);
        return 0;
    }
    if (message == WM_DESTROY) {
        if (keyboard_hook) { UnhookWindowsHookEx(keyboard_hook); keyboard_hook = 0; }
        release_keys();
        if (connection != INVALID_SOCKET) closesocket(connection);
        WSACleanup(); PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int show) {
    WSADATA data = {};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return 1;
    WNDCLASSW wc = {};
    wc.hInstance = instance; wc.lpfnWndProc = proc;
    wc.lpszClassName = L"WinInputSender";
    wc.hCursor = LoadCursorW(0, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    if (!RegisterClassW(&wc)) return 1;
    window = CreateWindowW(wc.lpszClassName, L"WinInput 发送端", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 590, 230, 0, 0, instance, 0);
    if (!window) return 1;
    CreateWindowW(L"STATIC", L"接收端IP", WS_CHILD | WS_VISIBLE, 20, 20, 75, 24, window, 0, instance, 0);
    host_field = CreateWindowW(L"EDIT", L"192.168.0.102", WS_CHILD | WS_VISIBLE | WS_BORDER,
        100, 16, 170, 28, window, 0, instance, 0);
    CreateWindowW(L"STATIC", L"端口", WS_CHILD | WS_VISIBLE, 290, 20, 45, 24, window, 0, instance, 0);
    port_field = CreateWindowW(L"EDIT", L"18765", WS_CHILD | WS_VISIBLE | WS_BORDER,
        340, 16, 85, 28, window, 0, instance, 0);
    CreateWindowW(L"STATIC", L"密钥", WS_CHILD | WS_VISIBLE, 20, 58, 75, 24, window, 0, instance, 0);
    secret_field = CreateWindowW(L"EDIT", L"1234567890123456", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_PASSWORD,
        100, 54, 325, 28, window, 0, instance, 0);
    connect_button = CreateWindowW(L"BUTTON", L"连接", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        450, 16, 95, 28, window, (HMENU)(INT_PTR)ID_CONNECT, instance, 0);
    CreateWindowW(L"BUTTON", L"推送剪贴板", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        450, 54, 95, 28, window, (HMENU)(INT_PTR)ID_CLIP_PUSH, instance, 0);
    CreateWindowW(L"BUTTON", L"拉取剪贴板", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        450, 92, 95, 28, window, (HMENU)(INT_PTR)ID_CLIP_PULL, instance, 0);
    status_field = CreateWindowW(L"STATIC", L"未连接", WS_CHILD | WS_VISIBLE,
        20, 135, 545, 30, window, 0, instance, 0);
    ShowWindow(window, show ? show : SW_SHOW); UpdateWindow(window);
    MSG msg; int result;
    while ((result = GetMessageW(&msg, 0, 0, 0)) > 0) {
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
    return result < 0 ? 1 : 0;
}
