#include <stdio.h>
#include <Windows.h>
#include <string.h>
#include <stdlib.h>
#include "notice.h"
#include "messager.h"
#include "injector.h"
#include "resource.h"
#include "config.h"
#include "osd_render.h"
#include <tchar.h>

#include <psapi.h>
#pragma comment(lib, "psapi.lib")

void set_console_icon() {
    HWND hConsole = GetConsoleWindow();
    if (hConsole != NULL) {
        HINSTANCE hInstance = GetModuleHandle(NULL);
        HICON hIconBig = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON1));

        HICON hIconSmall = (HICON)LoadImageW(
            hInstance,
            MAKEINTRESOURCEW(IDI_ICON1),
            IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON),
            GetSystemMetrics(SM_CYSMICON),
            LR_DEFAULTCOLOR
        );

        SendMessage(hConsole, WM_SETICON, ICON_BIG, (LPARAM)hIconBig);
        SendMessage(hConsole, WM_SETICON, ICON_SMALL, (LPARAM)hIconSmall);
    }
}

void SetupLogWindow() {
    HWND hwndConsole = GetConsoleWindow();
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hwndConsole == NULL || hOut == INVALID_HANDLE_VALUE) {
        printf("[!]can't get the console handle\n");
        return;
    }

    RECT workArea;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);

    int windowWidth = (workArea.right - workArea.left) / 5;
    int windowHeight = (workArea.bottom - workArea.top) / 5;

    /* 先把屏幕缓冲区按字体大小缩到目标行列，否则 SetWindowPos 缩不动窗口（只会出滚动条） */
    CONSOLE_FONT_INFO cfi = { 0 };
    if (GetCurrentConsoleFont(hOut, FALSE, &cfi)) {
        COORD fs = GetConsoleFontSize(hOut, cfi.nFont);
        if (fs.X > 0 && fs.Y > 0) {
            int cols = windowWidth / fs.X;
            int rows = windowHeight / fs.Y;
            if (cols < 10) cols = 10;
            if (rows < 5) rows = 5;
            COORD sz = { (SHORT)cols, (SHORT)rows };
            SetConsoleScreenBufferSize(hOut, sz);
        }
    }

    /* 放到工作区右上角 */
    int x = workArea.right - windowWidth;
    int y = workArea.top;
    SetWindowPos(hwndConsole,
        HWND_NOTOPMOST,
        x,
        y,
        windowWidth,
        windowHeight,
        SWP_SHOWWINDOW);
}

/* 应用控制台颜色（"背景前景" 十六进制，如 "0E" = 黑底黄字） */
static void setup_console_color(void) {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return;

    int vals[2] = { 0, 7 };   /* 默认黑底、浅灰字 */
    size_t n = wcslen(g_config.console_color);
    for (size_t i = 0; i < n && i < 2; i++) {
        wchar_t ch = g_config.console_color[i];
        int v = -1;
        if (ch >= L'0' && ch <= L'9') v = ch - L'0';
        else if (ch >= L'a' && ch <= L'f') v = ch - L'a' + 10;
        else if (ch >= L'A' && ch <= L'F') v = ch - L'A' + 10;
        if (v >= 0) vals[i] = v;
    }
    WORD attr = (WORD)((vals[0] << 4) | vals[1]);
    SetConsoleTextAttribute(hOut, attr);
}

BOOL WINAPI console_ctrl_handler(DWORD signal) {
    switch (signal) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:   
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        notice_cleanup_tray();
        messager_uninit();
        osd_shutdown();
        return TRUE;
    }
    return FALSE;
}


DWORD FindProcessByEnum(const TCHAR* processName)
{
    DWORD pids[1024], cbNeeded;
    if (!EnumProcesses(pids, sizeof(pids), &cbNeeded)) return 0;

    DWORD count = cbNeeded / sizeof(DWORD);
    for (DWORD i = 0; i < count; i++) {
        if (pids[i] == 0) continue;
        HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pids[i]);
        if (hProc) {
            TCHAR name[MAX_PATH] = { 0 };
            if (GetModuleBaseName(hProc, NULL, name, MAX_PATH)) {
                if (_tcsicmp(name, processName) == 0) {
                    CloseHandle(hProc);
                    return pids[i];
                }
            }
            CloseHandle(hProc);
        }
    }
    return 0;
}

int main() {
    SetConsoleCtrlHandler(console_ctrl_handler, TRUE);
    config_load(&g_config, NULL);
    {
        /* 首次运行生成一份默认配置文件，方便用户手动编辑 */
        wchar_t cfg_path[MAX_PATH] = { 0 };
        config_get_path(cfg_path, MAX_PATH, NULL);
        if (GetFileAttributesW(cfg_path) == INVALID_FILE_ATTRIBUTES) {
            config_save(&g_config, NULL);
        }
    }
    SetConsoleTitleW(g_config.console_title);

    /* 控制台：显示/隐藏 + 左上角缩放 + 颜色 */
    if (g_config.show_console) {
        SetupLogWindow();
        setup_console_color();
    }
    else {
        HWND hc = GetConsoleWindow();
        if (hc) ShowWindow(hc, SW_HIDE);
    }

    int choice = MessageBoxW(NULL, L"是否以巨幅文本+通知的形式来提示（若选否则只以通知形式）", L"Lilith is all you need", MB_YESNO | MB_ICONQUESTION);
    if (choice == IDYES) {
        g_use_osd = true;
        wprintf(L"[-]%s\n", g_config.console_mode_on);
    }
    else {
        g_use_osd = false;
        wprintf(L"[-]%s\n", g_config.console_mode_off);
    }
    if (!messager_init()) { wprintf(L"[!]messager init failed\n"); goto MAIN_FAILED; }
    if (!notice_window_init()) { wprintf(L"[!]notice init failed\n"); messager_uninit(); goto MAIN_FAILED; }
    while (1) {
        wprintf(L"[-]%s\n", g_config.console_waiting);
        DWORD pid = 0;
        while ((pid = FindProcessByEnum(L"media_capture.exe")) == 0) {
            MSG msg;
            while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    wprintf(L"[!]Window closed before target process started.\n");
                    goto EXIT_CLEANUP;
                }
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            Sleep(50);
        }

        wprintf(L"[*]%s\n", g_config.console_target_appeared);
        HANDLE target_process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
        if (!target_process) {
            wprintf(L"[!]OpenProcess failed\n");
            goto MAIN_FAILED;
        }
        WaitForInputIdle(target_process, 5000);
        wchar_t full_path[MAX_PATH] = { 0 };
        if (GetModuleFileNameW(NULL, full_path, MAX_PATH) == 0) {
            wprintf(L"[!]GetModuleFileNameW failed: %lu\n", GetLastError());
            CloseHandle(target_process);
            goto MAIN_FAILED;
        }

        wchar_t* last_slash = wcsrchr(full_path, L'\\');
        if (last_slash) {
            size_t remaining_space = MAX_PATH - (last_slash - full_path) - 1;
            wcscpy_s(last_slash + 1, remaining_space, L"NoMoreMonitor_Dll.dll");
        }
        else {
            wprintf(L"[!]Invalid execution path\n");
            CloseHandle(target_process);
            goto MAIN_FAILED;
        }

        int eject_retries = 10;
        while (ejector(target_process, L"NoMoreMonitor_Dll.dll") && eject_retries-- > 0) {
            wprintf(L"[*]Found existing DLL in target process, ejecting...\n");
            Sleep(100);
        }

        if (!injector(target_process, full_path)) {
            wprintf(L"[!] inject failed\n");
            CloseHandle(target_process);
            continue;
        }

        wprintf(L"[-]%s\n", g_config.console_running);
        bool process_alive = true;
        while (process_alive) {
            DWORD wait_res = MsgWaitForMultipleObjects(1, &target_process, FALSE, INFINITE, QS_ALLINPUT);

            if (wait_res == WAIT_OBJECT_0) {
                wprintf(L"[*]%s\n", g_config.console_target_closed);
                PostMessage(hwnd, WM_IPC_STATE, status_waiting, 0);
                process_alive = false;
            }
            else if (wait_res == WAIT_OBJECT_0 + 1) {
                MSG msg;
                while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
                    if (msg.message == WM_QUIT) {
                        CloseHandle(target_process);
                        goto EXIT_CLEANUP;
                    }
                    TranslateMessage(&msg);
                    DispatchMessage(&msg);
                }
            }
        }

        CloseHandle(target_process);
    }

EXIT_CLEANUP:
    wprintf(L"[*]%s\n", g_config.console_mission_completed);
    messager_uninit();
    osd_shutdown();
    system("pause");
    return 0;

MAIN_FAILED:
    messager_uninit();
    osd_shutdown();
    system("pause");
    return -1;
}