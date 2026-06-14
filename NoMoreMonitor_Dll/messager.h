#pragma once
#include <Windows.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

enum {
	status_waiting = 0,
	status_start = 1,
	status_watching = 2,
	status_stop = 3,
	status_error = -1,
	status_log = 4,
	status_info = 5
};

struct log_data {
	wchar_t log_buffer[1024];
	int curr_state;
	int pot_error;
	int min_delay_seconds;
	int max_delay_seconds;
};

extern HANDLE event;
extern HANDLE mapping;
extern void* ptr_mem;

bool messager_init();
void msg_pass(const wchar_t* text, int curr_status, int err_code);
void messager_uninit();