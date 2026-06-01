#include <Windows.h>
#include "messager.h"
#include "hook.h"


DWORD WINAPI init_thread_proc(LPVOID p) {
    messager_init();

    HMODULE mod = NULL;
    for (int i = 0; i < 100; i++) { 
        mod = GetModuleHandleW(L"media_framework_device.dll");
        if (mod) break;
        Sleep(100);
    }
    if (!mod) {
        msg_pass(L"target dll never loaded, give up", status_error, -1);
        return 1;
    }
    msg_pass(L"(From Dll)attatched the target process", status_info, 0);
    init_hook();
    enable_hook();
    return 0;
}


BOOL APIENTRY DllMain(HMODULE hModule,
    DWORD  ul_reason_for_call,
    LPVOID lpReserved
)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH: {
        DisableThreadLibraryCalls(hModule);
        //MessageBoxW(NULL, L"attach the target process", L"NoMoreMonitor_Dll", MB_ICONINFORMATION | MB_SYSTEMMODAL);
        HANDLE h = CreateThread(NULL, 0, init_thread_proc, NULL, 0, NULL);
        if (h) CloseHandle(h);
        break;
    }
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break;
    case DLL_PROCESS_DETACH:
        //MessageBoxW(NULL, L"detach the target process", L"NoMoreMonitor_Dll", MB_ICONINFORMATION | MB_SYSTEMMODAL);
        if (lpReserved != NULL) {
            break;
        }
        msg_pass(L"(From Dll)detatched the target process",status_info,0);
        //disable_hook();
        //Sleep(50);
       // msg_pass(L"reset the shared mem flag...",status_waiting,0);
        uninit_hook();       
        messager_uninit();
        break;

        return TRUE;
    }
    return TRUE;
}