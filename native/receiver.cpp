#include "WinInputCommon.h"

#define ID_START 101
#define WM_APP_CLIP_READ (WM_APP + 1)
#define WM_APP_CLIP_WRITE (WM_APP + 2)
HWND hPort, hSecret, hStatus, hStart, main_window;
SOCKET ls = INVALID_SOCKET;
HANDLE worker = 0;
std::wstring secret;
CRITICAL_SECTION clip_lock;
HANDLE clip_done;
std::string clip_payload;
int clip_result = -1;

void status_text(const wchar_t* text) { SetWindowTextW(hStatus, text); }

DWORD WINAPI serve(void*) {
    for (;;) {
        SOCKET c = accept(ls, 0, 0);
        if (c == INVALID_SOCKET) break;
        std::string frame, rs, cmd, value;
        bool held[256] = {};
        WORD scans[256] = {};
        bool extended_keys[256] = {};
        while (read_frame(c, frame)) {
            if (!split_frame(frame, rs, cmd, value) || wide(rs) != secret) break;
            if (cmd == "text") send_text(wide(value));
            else if (cmd == "key") send_key(wide(value));
            else if (cmd == "event") {
                WORD vk, scan; bool extended, down;
                if (!parse_key_event(value, vk, scan, extended, down)) break;
                if (inject_key_event(vk, scan, extended, down)) {
                    held[vk] = down;
                    scans[vk] = scan;
                    extended_keys[vk] = extended;
                }
            }
            else if (cmd == "clip_get") {
                EnterCriticalSection(&clip_lock);
                clip_payload.clear();
                clip_result = -1;
                LeaveCriticalSection(&clip_lock);
                ResetEvent(clip_done);
                PostMessageW(main_window, WM_APP_CLIP_READ, 0, 0);
                if (WaitForSingleObject(clip_done, 5000) != WAIT_OBJECT_0) break;
                EnterCriticalSection(&clip_lock);
                bool ok = clip_result >= 0;
                std::string reply = clip_payload;
                LeaveCriticalSection(&clip_lock);
                if (!ok) { if (!write_frame(c, rs, "clip_fail", "接收端无法读取剪贴板")) break; }
                else if (reply.empty()) { if (!write_frame(c, rs, "clip_fail", "接收端剪贴板为空或不支持")) break; }
                else if (!write_frame(c, rs, "clip_put", reply)) break;
            }
            else if (cmd == "clip_put") {
                if (value.empty() || value.size() > MAX_CLIP) break;
                EnterCriticalSection(&clip_lock);
                clip_payload = value;
                clip_result = -1;
                LeaveCriticalSection(&clip_lock);
                ResetEvent(clip_done);
                PostMessageW(main_window, WM_APP_CLIP_WRITE, 0, 0);
                if (WaitForSingleObject(clip_done, 5000) != WAIT_OBJECT_0) break;
                EnterCriticalSection(&clip_lock);
                int result = clip_result;
                LeaveCriticalSection(&clip_lock);
                if (!write_frame(c, rs, "clip_ack", std::to_string(result))) break;
            }
        }
        for (int vk = 1; vk < 256; ++vk)
            if (held[vk]) inject_key_event((WORD)vk, scans[vk], extended_keys[vk], false);
        closesocket(c);
    }
    return 0;
}

LRESULT CALLBACK proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_APP_CLIP_READ) {
        bool skipped = false;
        std::string data = clip_serialize(skipped);
        EnterCriticalSection(&clip_lock);
        clip_payload = data;
        clip_result = skipped ? 1 : (data.empty() ? -1 : 0);
        LeaveCriticalSection(&clip_lock);
        SetEvent(clip_done);
        return 0;
    }
    if (message == WM_APP_CLIP_WRITE) {
        EnterCriticalSection(&clip_lock);
        std::string data = clip_payload;
        LeaveCriticalSection(&clip_lock);
        int result = clip_deserialize(data);
        EnterCriticalSection(&clip_lock);
        clip_result = result;
        LeaveCriticalSection(&clip_lock);
        SetEvent(clip_done);
        return 0;
    }
    if (message == WM_COMMAND && LOWORD(wParam) == ID_START) {
        wchar_t port_text[16] = {}, secret_text[256] = {};
        GetWindowTextW(hPort, port_text, 16);
        GetWindowTextW(hSecret, secret_text, 256);
        secret = secret_text;
        int port = _wtoi(port_text);
        if (secret.size() < 16 || port < 1 || port > 65535) {
            MessageBoxW(window, L"密钥至少16字符，端口必须在1到65535之间", L"错误", MB_OK | MB_ICONERROR);
            return 0;
        }
        sockaddr_in address = {};
        address.sin_family = AF_INET;
        address.sin_port = htons((u_short)port);
        address.sin_addr.s_addr = INADDR_ANY;
        ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (ls == INVALID_SOCKET || bind(ls, (sockaddr*)&address, sizeof(address)) == SOCKET_ERROR || listen(ls, 5) == SOCKET_ERROR) {
            MessageBoxW(window, L"监听失败，请检查端口是否被占用", L"错误", MB_OK | MB_ICONERROR);
            if (ls != INVALID_SOCKET) { closesocket(ls); ls = INVALID_SOCKET; }
            return 0;
        }
        worker = CreateThread(0, 0, serve, 0, 0, 0);
        if (!worker) {
            closesocket(ls); ls = INVALID_SOCKET;
            MessageBoxW(window, L"无法创建监听线程", L"错误", MB_OK | MB_ICONERROR);
            return 0;
        }
        EnableWindow(hStart, FALSE);
        status_text(L"监听中");
        return 0;
    }
    if (message == WM_DESTROY) {
        if (ls != INVALID_SOCKET) { closesocket(ls); ls = INVALID_SOCKET; }
        if (worker) { SetEvent(clip_done); WaitForSingleObject(worker, 2000); CloseHandle(worker); worker = 0; }
        WSACleanup();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int show) {
    WSADATA data = {};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        MessageBoxW(0, L"WSAStartup失败", L"WinInput", MB_OK | MB_ICONERROR);
        return 1;
    }
    InitializeCriticalSection(&clip_lock);
    clip_done = CreateEventW(0, TRUE, FALSE, 0);
    main_window = 0;

    HINSTANCE instance = GetModuleHandleW(0);
    const wchar_t* class_name = L"WinInputReceiverClass";
    WNDCLASSW wc = {};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.hInstance = instance;
    wc.lpfnWndProc = proc;
    wc.lpszClassName = class_name;
    wc.hCursor = LoadCursorW(0, IDC_ARROW);
    wc.hIcon = LoadIconW(0, IDI_APPLICATION);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        MessageBoxW(0, L"窗口类注册失败", L"WinInput", MB_OK | MB_ICONERROR);
        WSACleanup();
        return 1;
    }

    HWND window = CreateWindowExW(0, class_name, L"WinInput 原生接收端", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 460, 240, 0, 0, instance, 0);
    if (!window) {
        wchar_t message[128];
        wsprintfW(message, L"CreateWindowExW失败，错误码：%lu", GetLastError());
        MessageBoxW(0, message, L"WinInput", MB_OK | MB_ICONERROR);
        WSACleanup();
        return 1;
    }
    main_window = window;

    CreateWindowExW(0, L"STATIC", L"监听地址：0.0.0.0", WS_CHILD | WS_VISIBLE,
        20, 25, 180, 24, window, 0, instance, 0);
    CreateWindowExW(0, L"STATIC", L"端口", WS_CHILD | WS_VISIBLE,
        20, 65, 80, 24, window, 0, instance, 0);
    hPort = CreateWindowExW(0, L"EDIT", L"18765", WS_CHILD | WS_VISIBLE | WS_BORDER,
        110, 60, 300, 28, window, 0, instance, 0);
    CreateWindowExW(0, L"STATIC", L"共享密钥", WS_CHILD | WS_VISIBLE,
        20, 105, 80, 24, window, 0, instance, 0);
    hSecret = CreateWindowExW(0, L"EDIT", L"1234567890123456", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_PASSWORD,
        110, 100, 300, 28, window, 0, instance, 0);
    hStart = CreateWindowExW(0, L"BUTTON", L"启动", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        110, 140, 100, 30, window, (HMENU)(INT_PTR)ID_START, instance, 0);
    hStatus = CreateWindowExW(0, L"STATIC", L"未启动", WS_CHILD | WS_VISIBLE,
        230, 145, 180, 24, window, 0, instance, 0);

    ShowWindow(window, show ? show : SW_SHOW);
    UpdateWindow(window);
    MSG message;
    int result;
    while ((result = GetMessageW(&message, 0, 0, 0)) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (clip_done) CloseHandle(clip_done);
    DeleteCriticalSection(&clip_lock);
    return result < 0 ? 1 : 0;
}
