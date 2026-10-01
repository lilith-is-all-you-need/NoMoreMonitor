#include <Windows.h>
#include <stdbool.h>
#include <stdio.h>
#include "MinHook.h"
#include "messager.h"
#include "..\NoMoreMonitor\config.h"


typedef int(__fastcall* pfn_ds_capture_start)(void* ptr_this, void* edx); // ordinal:76
typedef void(__fastcall* pfn_ds_capture_stop)(void* ptr_this, void* edx); // ordinal:86

typedef bool(__fastcall* pfn_mf_capture_start)(void* ptr_this, void* edx); // ordinal:77
typedef void(__fastcall* pfn_mf_capture_stop)(void* ptr_this, void* edx); // ordinal:87

pfn_ds_capture_start orig_ds_capture_start;
pfn_ds_capture_stop  orig_ds_capture_stop;
pfn_mf_capture_start orig_mf_capture_start;
pfn_mf_capture_stop  orig_mf_capture_stop;


void SafeBufferDelay(DWORD delayMs) {
	ULONGLONG endTick = GetTickCount64() + delayMs;

	while (GetTickCount64() < endTick) {
		DWORD waitResult = MsgWaitForMultipleObjects(0, NULL, FALSE, 50, QS_ALLINPUT);
		if (waitResult == WAIT_OBJECT_0) {
			MSG msg;
			while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
		}
	}
}

static bool is_gui_thread(void) {
	GUITHREADINFO gti = { sizeof(gti) };
	return GetGUIThreadInfo(GetCurrentThreadId(), &gti) != FALSE;
}

void smart_delay(DWORD ms) {
	if (is_gui_thread()) {
		SafeBufferDelay(ms); 
	}
	else {
		Sleep(ms);
	}
}

bool is_init = false;

static volatile LONG g_in_hook = 0;
static volatile LONG g_in_mf_hook = 0; 

void countdown_delay(int seconds, const wchar_t* fmt) {
	wchar_t msg_buf[256];
	for (int i = seconds; i > 0; i--) {
		swprintf_s(msg_buf, _countof(msg_buf), fmt, i);
		msg_pass(msg_buf, status_log, 0);
		smart_delay(1000); // 每次延时 1 秒
	}
}

/* 从与 DLL 同目录的 NoMoreMonitor.ini 读取最新配置 */
static void dll_load_config(AppConfig* cfg) {
	HMODULE self = GetModuleHandleW(L"NoMoreMonitor_Dll.dll");
	config_load(cfg, self);
}

int __fastcall hooked_ds_capture_start(void* ptr_this, void* edx) {
	if (InterlockedCompareExchange(&g_in_hook, 1, 0) != 0) {
		return orig_ds_capture_start(ptr_this, edx);
	}
	AppConfig cfg;
	dll_load_config(&cfg);

	msg_pass(L"DS capture start!", status_start, 0);
	Sleep(50);

	countdown_delay(cfg.pause_seconds, cfg.countdown_text);

	msg_pass(L"watching", status_watching, 0);
	int ret = orig_ds_capture_start(ptr_this, edx);
	InterlockedExchange(&g_in_hook, 0);
	return ret;
}

bool __fastcall hooked_mf_capture_start(void* ptr_this, void* edx) {
	if (InterlockedCompareExchange(&g_in_mf_hook, 1, 0) != 0) {
		return orig_mf_capture_start(ptr_this, edx);
	}

	AppConfig cfg;
	dll_load_config(&cfg);

	msg_pass(L"MF capture start!", status_start, 0);

	Sleep(50);

	countdown_delay(cfg.pause_seconds, cfg.countdown_text);

	msg_pass(L"watching", status_watching, 0);

	bool ret = orig_mf_capture_start(ptr_this, edx);
	InterlockedExchange(&g_in_mf_hook, 0);

	return ret;
}
void __fastcall hooked_ds_capture_stop(void* ptr_this, void* edx) {
	msg_pass(L"we are safe now... temporarily", status_stop, 0);
	orig_ds_capture_stop(ptr_this, edx);
}

void __fastcall hooked_mf_capture_stop(void* ptr_this, void* edx) {
	msg_pass(L"we are safe now... temporarily", status_stop, 0);
	orig_mf_capture_stop(ptr_this, edx);
}

bool init_hook() {
	HANDLE target_dll = GetModuleHandleW(L"media_framework_device.dll");
	if (target_dll == NULL) {
		msg_pass(L"target dll isn't exist", status_error, -1);
		return false;
	}
	if (MH_Initialize() != MH_OK) {
		msg_pass(L"can't init the MinHook lib", status_error, -1);
		return false;
	}
	int ret = 0;
	ret = MH_CreateHook(GetProcAddress(target_dll, MAKEINTRESOURCEA(76)), hooked_ds_capture_start, (LPVOID*)&orig_ds_capture_start);
	if (ret != MH_OK) {
		msg_pass(L"can't create a hook on ord:76", status_error, -1);
		MH_Uninitialize();
		return false;
	}
	ret = MH_CreateHook(GetProcAddress(target_dll, MAKEINTRESOURCEA(86)), hooked_ds_capture_stop, (LPVOID*)&orig_ds_capture_stop);
	if (ret != MH_OK) {
		msg_pass(L"can't create a hook on ord:86", status_error, -1);
		MH_Uninitialize();
		return false;
	}
	ret = MH_CreateHook(GetProcAddress(target_dll, MAKEINTRESOURCEA(77)), hooked_mf_capture_start, (LPVOID*)&orig_mf_capture_start);
	if (ret != MH_OK) {
		msg_pass(L"can't create a hook on ord:77", status_error, -1);
		MH_Uninitialize();
		return false;
	}
	ret = MH_CreateHook(GetProcAddress(target_dll, MAKEINTRESOURCEA(87)), hooked_mf_capture_stop, (LPVOID*)&orig_mf_capture_stop);
	if (ret != MH_OK) {
		msg_pass(L"can't create a hook on ord:87", status_error, -1);
		MH_Uninitialize();
		return false;
	}
	is_init = true;
	return true;
}

bool enable_hook() {
	if (is_init == false) {
		return false;
	}
	if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
		msg_pass(L"can't enable hooks", status_error, -1);
		return false;
	}
	return true;
}

bool disable_hook() {
	if (is_init == false) {
		return false;
	}
	if (MH_DisableHook(MH_ALL_HOOKS) != MH_OK) {
		msg_pass(L"can't disable hooks", status_error, -1);
		return false;
	}
	return true;
}

bool uninit_hook() {
	if (is_init == false) {
		return false;
	}
	if (disable_hook() != true) {
		return false;
	}
	if (MH_Uninitialize() != MH_OK) {
		msg_pass(L"can't uninit MH",status_error,-1);
		return false;
	}
	is_init = false;
	return true;
}