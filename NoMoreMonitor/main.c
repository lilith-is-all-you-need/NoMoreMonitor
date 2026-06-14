#include <stdio.h>
#include <Windows.h>
#include <string.h>
#include "notice.h"
#include "messager.h"
#include "injector.h"
#include "resource.h"
#include "config.h"
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
    if (hwndConsole == NULL) {
        printf("[!]can't get the console handle\n");
        return;
    }

    RECT workArea;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);

    int windowWidth = (workArea.right - workArea.left) / 5;
    int windowHeight = (workArea.bottom - workArea.top) / 5;

    SetWindowPos(hwndConsole,
        HWND_NOTOPMOST,
        workArea.left,
        workArea.top,
        windowWidth,
        windowHeight,
        SWP_SHOWWINDOW);

    system("color E");
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
    SetupLogWindow();
    SetConsoleCtrlHandler(console_ctrl_handler, TRUE);
    SetConsoleTitleW(L"[Lilith]NoMoreMonitor --dev:YDS inspiration from LHX");

    // 加载配置文件
    AppConfig g_config;
    wchar_t ini_path[MAX_PATH];
    GetModuleFileNameW(NULL, ini_path, MAX_PATH);
    wchar_t* last_slash = wcsrchr(ini_path, L'\\');
    if (last_slash) {
        wcscpy_s(last_slash + 1, MAX_PATH - (last_slash - ini_path) - 1, L"NoMoreMonitor.ini");
    }

    bool config_exists = config_load(ini_path, &g_config);

    // 如果配置文件不存在，弹出MessageBox并保存用户选择
    if (!config_exists) {
        int choice = MessageBoxW(NULL, L"是否以巨幅文本+通知的形式来提示（若选否则只以通知形式）",
                                 L"Lilith is all you need", MB_YESNO | MB_ICONQUESTION);
        g_config.use_osd = (choice == IDYES);

        // 保存配置，下次不再询问
        config_save(ini_path, &g_config);
    }

    // 应用配置
    g_use_osd = g_config.use_osd;
    if (g_use_osd) {
        wprintf(L"[-]mode: text with notice\n");
    }
    else {
        wprintf(L"[-]mode: notice only\n");
    }

    if (!messager_init()) { wprintf(L"[!]messager init failed\n"); goto MAIN_FAILED; }

    // 将延迟配置写入shared memory
    if (WaitForSingleObject(mutex, 2000) == WAIT_OBJECT_0) {
        struct log_data* shared = (struct log_data*)ptr_buf;
        shared->min_delay_seconds = g_config.min_delay;
        shared->max_delay_seconds = g_config.max_delay;
        ReleaseMutex(mutex);
        wprintf(L"[*]Config synced: delay=%d-%ds\n", g_config.min_delay, g_config.max_delay);
    }

    if (!notice_window_init()) { wprintf(L"[!]notice init failed\n"); messager_uninit(); goto MAIN_FAILED; }
    while (1) {
        wprintf(L"[-]Waiting for media_capture.exe...\n");
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

        wprintf(L"[*]Target appeared\n");
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

        wprintf(L"[-]running...\n");
        bool process_alive = true;
        while (process_alive) {
            DWORD wait_res = MsgWaitForMultipleObjects(1, &target_process, FALSE, INFINITE, QS_ALLINPUT);

            if (wait_res == WAIT_OBJECT_0) {
                wprintf(L"[*]Target process closed. Restarting monitor...\n");
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
    wprintf(L"[*]mission completed!\n");
    messager_uninit();
    system("pause");
    return 0;

MAIN_FAILED:
    messager_uninit();
    system("pause");
    return -1;
}