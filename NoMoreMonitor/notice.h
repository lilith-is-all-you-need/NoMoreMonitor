#pragma once
#include <Windows.h>
#include <shellapi.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

extern const wchar_t* notice_class_name;
extern const wchar_t* notice_window_name;
#define WM_IPC_STATE (WM_APP + 1)
extern HWND hwnd;

extern bool g_use_osd;

bool notice_window_init();
void send_balloon_notification(const wchar_t* title, const wchar_t* text);
void notice_cleanup_tray(void);