#include "messager.h"
#include "notice.h"
#include "config.h"
#include "spy_data.h"

HANDLE event;
HANDLE mapping;
HANDLE mutex;
HANDLE quit_event;
void* ptr_buf;
HANDLE msg_thread;

/* spy 会话记录：status_watching 起点 → status_stop 终点 */
static SYSTEMTIME g_spy_start;
static bool g_spy_recording = false;

static void spy_session_stop(void) {
    if (!g_spy_recording) return;

    SYSTEMTIME end;
    GetLocalTime(&end);

    FILETIME fs, fe;
    SystemTimeToFileTime(&g_spy_start, &fs);
    SystemTimeToFileTime(&end, &fe);
    ULARGE_INTEGER a, b;
    a.LowPart = fs.dwLowDateTime; a.HighPart = fs.dwHighDateTime;
    b.LowPart = fe.dwLowDateTime; b.HighPart = fe.dwHighDateTime;
    ULONGLONG delta = (b.QuadPart >= a.QuadPart) ? (b.QuadPart - a.QuadPart) : 0;

    SpySession s;
    s.start = g_spy_start;
    s.end = end;
    s.duration_seconds = (DWORD)(delta / 10000000ULL);
    spy_data_append(&s);

    g_spy_recording = false;
}

DWORD WINAPI lililth_thread_proc(LPVOID lpParameter) {
    HANDLE waits[2] = { quit_event, event }; 
    for (;;) {
        DWORD r = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
        if (r == WAIT_OBJECT_0) {

            break;
        }
        if (r != WAIT_OBJECT_0 + 1) {
            
            break;
        }


        struct log_data curr_pack = { 0 };
        if (WaitForSingleObject(mutex, 2000) == WAIT_OBJECT_0) {
            memcpy(&curr_pack, ptr_buf, sizeof(struct log_data));
            ReleaseMutex(mutex);
        }
        else {
            continue; 
        }

        switch (curr_pack.curr_state) {
        case status_error:
            con_printf(L"[!]Log from Dll:%s Err code:%d\n",
                curr_pack.log_buffer, curr_pack.pot_error);
            break;
        case status_waiting:
            g_spy_recording = false;
            break;
        case status_watching:
            GetLocalTime(&g_spy_start);
            g_spy_recording = true;
            if (hwnd) PostMessage(hwnd, WM_IPC_STATE, status_watching, 0);
            break;
        case status_start:
            con_printf(L"[-]%s\n", g_config.console_spy_start);
            if (hwnd) PostMessage(hwnd, WM_IPC_STATE, status_start, 0);
            break;
        case status_stop:
            con_printf(L"[-]%s\n", g_config.console_spy_stop);
            spy_session_stop();
            if (hwnd) PostMessage(hwnd, WM_IPC_STATE, status_stop, 0);
            break;
        case status_log:
            con_printf(L"[*]Log:%s\n", curr_pack.log_buffer);

            if (hwnd) {
                SendMessage(hwnd, WM_IPC_STATE, status_log, (LPARAM)curr_pack.log_buffer);
            }
            break;
        case status_info:
            con_printf(L"[*]Info:%s\n", curr_pack.log_buffer);
            break;
        default:
            break;
        }
    }
    return 0;
}


bool messager_init(void) {
    SECURITY_DESCRIPTOR sd;
    InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
    SetSecurityDescriptorDacl(&sd, TRUE, NULL, FALSE); 

    SECURITY_ATTRIBUTES sa = { 0 };
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = &sd;
    sa.bInheritHandle = FALSE;

    event = CreateEventW(NULL, FALSE, FALSE, L"Local\\LilithLogEvent");
    if (!event) { con_printf(L"[!]can't create event (%lu)\n", GetLastError()); goto fail; }

    mutex = CreateMutexW(NULL, FALSE, L"Local\\LilithMutex");
    if (!mutex) { con_printf(L"[!]can't create mutex (%lu)\n", GetLastError()); goto fail; }

    mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
        0, sizeof(struct log_data), L"Local\\LilithSharedMem");
    if (!mapping) { con_printf(L"[!]can't create file mapping (%lu)\n", GetLastError()); goto fail; }

    ptr_buf = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(struct log_data));
    if (!ptr_buf) { con_printf(L"[!]can't map view (%lu)\n", GetLastError()); goto fail; }
    ZeroMemory(ptr_buf, sizeof(struct log_data));

    quit_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!quit_event) { con_printf(L"[!]can't create quit_event (%lu)\n", GetLastError()); goto fail; }

    msg_thread = CreateThread(NULL, 0, lililth_thread_proc, NULL, 0, NULL);
    if (!msg_thread) { con_printf(L"[!]can't create messager thread (%lu)\n", GetLastError()); goto fail; }

    return true;

fail:
    messager_uninit();
    return false;
}


void messager_uninit(void) {
    if (quit_event) SetEvent(quit_event);

    if (msg_thread) {
        if (WaitForSingleObject(msg_thread, 3000) == WAIT_TIMEOUT) {
            TerminateThread(msg_thread, 0);
        }
        CloseHandle(msg_thread);
        msg_thread = NULL;
    }

    if (ptr_buf) { UnmapViewOfFile(ptr_buf); ptr_buf = NULL; }
    if (mapping) { CloseHandle(mapping);     mapping = NULL; }
    if (mutex) { CloseHandle(mutex);       mutex = NULL; }
    if (event) { CloseHandle(event);       event = NULL; }
    if (quit_event) { CloseHandle(quit_event);  quit_event = NULL; }
}