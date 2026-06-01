#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")

bool injector(HANDLE proc, wchar_t* dll_path) {
    SIZE_T buf_size = (wcslen(dll_path) + 1) * sizeof(wchar_t);
    LPVOID ptr_rmt_buf = VirtualAllocEx(proc, NULL, buf_size,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!ptr_rmt_buf) {
        wprintf(L"[!] VirtualAllocEx failed: %lu\n", GetLastError());
        return false;
    }

    if (!WriteProcessMemory(proc, ptr_rmt_buf, dll_path, buf_size, NULL)) {
        wprintf(L"[!] WriteProcessMemory failed: %lu\n", GetLastError());
        VirtualFreeEx(proc, ptr_rmt_buf, 0, MEM_RELEASE);
        return false;
    }

    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    if (!k32) {
        VirtualFreeEx(proc, ptr_rmt_buf, 0, MEM_RELEASE);
        return false;
    }

    FARPROC pLoadLibraryW = GetProcAddress(k32, "LoadLibraryW");
    if (!pLoadLibraryW) {
        VirtualFreeEx(proc, ptr_rmt_buf, 0, MEM_RELEASE);
        return false;
    }

    HANDLE hThread = CreateRemoteThread(proc, NULL, 0,
        (LPTHREAD_START_ROUTINE)pLoadLibraryW, ptr_rmt_buf, 0, NULL);
    if (hThread == NULL) {
        wprintf(L"[!] CreateRemoteThread failed: %lu\n", GetLastError());
        VirtualFreeEx(proc, ptr_rmt_buf, 0, MEM_RELEASE);
        return false;
    }
    DWORD exitCode = 0;
    DWORD wait_res = WaitForSingleObject(hThread, 5000);
    if (wait_res == WAIT_TIMEOUT) {
        wprintf(L"[!] Remote thread timed out (possible deadlock in target).\n");
        VirtualFreeEx(proc, ptr_rmt_buf, 0, MEM_RELEASE);
        CloseHandle(hThread);
        return false;
    }
    GetExitCodeThread(hThread, &exitCode);
    if (exitCode == 0) {
        wprintf(L"[!] LoadLibraryW returned NULL in remote process\n");
        VirtualFreeEx(proc, ptr_rmt_buf, 0, MEM_RELEASE);
        CloseHandle(hThread);
        return false;
    }
    VirtualFreeEx(proc, ptr_rmt_buf, 0, MEM_RELEASE);
    CloseHandle(hThread);
    return true;
}

bool ejector(HANDLE proc, const wchar_t* dll_name) {
    HMODULE hMods[1024];
    DWORD cbNeeded;

    if (EnumProcessModulesEx(proc, hMods, sizeof(hMods), &cbNeeded, LIST_MODULES_ALL)) {
        int count = cbNeeded / sizeof(HMODULE);
        for (int i = 0; i < count; i++) {
            wchar_t szModName[MAX_PATH];
            if (GetModuleBaseNameW(proc, hMods[i], szModName, sizeof(szModName) / sizeof(wchar_t))) {
                if (_wcsicmp(szModName, dll_name) == 0) {

                    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
                    if (!k32) return false;

                    FARPROC pFreeLibrary = GetProcAddress(k32, "FreeLibrary");
                    if (!pFreeLibrary) return false;

                    HANDLE hThread = CreateRemoteThread(proc, NULL, 0,
                        (LPTHREAD_START_ROUTINE)pFreeLibrary, (LPVOID)hMods[i], 0, NULL);

                    if (hThread != NULL) {
                        WaitForSingleObject(hThread, 5000);
                        CloseHandle(hThread);
                        return true;
                    }
                    return false;
                }
            }
        }
    }
    return false; 
}