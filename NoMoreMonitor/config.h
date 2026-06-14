#pragma once
#include <Windows.h>
#include <stdbool.h>

typedef struct {
	int min_delay;        // 最小延迟秒数 (default: 3)
	int max_delay;        // 最大延迟秒数 (default: 8)
	bool use_osd;         // 是否使用全屏OSD (default: true)
} AppConfig;

bool config_load(const wchar_t* ini_path, AppConfig* config);
bool config_save(const wchar_t* ini_path, const AppConfig* config);
void config_set_defaults(AppConfig* config);
