#include "config.h"
#include <stdio.h>

void config_set_defaults(AppConfig* config) {
	config->min_delay = 3;
	config->max_delay = 8;
	config->use_osd = true;
}

bool config_load(const wchar_t* ini_path, AppConfig* config) {
	config_set_defaults(config);

	// 检查文件是否存在
	if (GetFileAttributesW(ini_path) == INVALID_FILE_ATTRIBUTES) {
		wprintf(L"[*]Config file not found, using defaults\n");
		return false; // 返回false表示需要保存配置
	}

	// 读取配置
	config->min_delay = GetPrivateProfileIntW(L"Delay", L"MinSeconds", 3, ini_path);
	config->max_delay = GetPrivateProfileIntW(L"Delay", L"MaxSeconds", 8, ini_path);
	config->use_osd = GetPrivateProfileIntW(L"Display", L"UseOSD", 1, ini_path) != 0;

	// 验证范围
	if (config->min_delay < 1) config->min_delay = 1;
	if (config->max_delay < config->min_delay) config->max_delay = config->min_delay;
	if (config->max_delay > 30) config->max_delay = 30;

	wprintf(L"[*]Config loaded: delay=%d-%ds, OSD=%s\n",
			config->min_delay, config->max_delay, config->use_osd ? L"on" : L"off");

	return true; // 成功加载
}

bool config_save(const wchar_t* ini_path, const AppConfig* config) {
	wchar_t buf[32];

	swprintf_s(buf, 32, L"%d", config->min_delay);
	if (!WritePrivateProfileStringW(L"Delay", L"MinSeconds", buf, ini_path)) {
		wprintf(L"[!]Failed to save MinSeconds\n");
		return false;
	}

	swprintf_s(buf, 32, L"%d", config->max_delay);
	if (!WritePrivateProfileStringW(L"Delay", L"MaxSeconds", buf, ini_path)) {
		wprintf(L"[!]Failed to save MaxSeconds\n");
		return false;
	}

	swprintf_s(buf, 32, L"%d", config->use_osd ? 1 : 0);
	if (!WritePrivateProfileStringW(L"Display", L"UseOSD", buf, ini_path)) {
		wprintf(L"[!]Failed to save UseOSD\n");
		return false;
	}

	wprintf(L"[*]Config saved to %s\n", ini_path);
	return true;
}
