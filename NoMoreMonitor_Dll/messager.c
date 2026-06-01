#include "messager.h"

void* ptr_mem;
HANDLE mapping;
HANDLE event;
HANDLE mutex;   // 新增

bool messager_init(void) {
    event = OpenEventW(EVENT_MODIFY_STATE, FALSE, L"Local\\LilithLogEvent");
    if (!event) return false;

    mutex = OpenMutexW(SYNCHRONIZE, FALSE, L"Local\\LilithMutex");
    if (!mutex) {
        CloseHandle(event); event = NULL;
        return false;
    }

    mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, L"Local\\LilithSharedMem");
    if (!mapping) {
        CloseHandle(event); event = NULL;
        CloseHandle(mutex); mutex = NULL;
        return false;
    }

    ptr_mem = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0,
        sizeof(struct log_data));
    if (!ptr_mem) {
        CloseHandle(event);   event = NULL;
        CloseHandle(mutex);   mutex = NULL;
        CloseHandle(mapping); mapping = NULL;
        return false;
    }
    return true;
}

void msg_pass(const wchar_t* text, int curr_status, int err_code) {
    if (!ptr_mem || !mutex || !event || !text) return;

    // 跨进程加锁
    if (WaitForSingleObject(mutex, 2000) != WAIT_OBJECT_0) return;

    struct log_data pack = { 0 };
    pack.curr_state = curr_status;
    pack.pot_error = err_code;
    wcsncpy_s(pack.log_buffer, 1024, text, _TRUNCATE);
    memcpy(ptr_mem, &pack, sizeof(struct log_data));

    ReleaseMutex(mutex);
    SetEvent(event);   // 通知 host
}

void messager_uninit(void) {
    if (ptr_mem) { UnmapViewOfFile(ptr_mem); ptr_mem = NULL; }
    if (mapping) { CloseHandle(mapping);     mapping = NULL; }
    if (mutex) { CloseHandle(mutex);       mutex = NULL; }
    if (event) { CloseHandle(event);       event = NULL; }
}