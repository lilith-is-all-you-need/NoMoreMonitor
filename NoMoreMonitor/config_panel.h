#pragma once
#include <windows.h>
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 打开配置面板(模态)。cfg 指向运行时全局配置(g_config)：
 * 面板编辑一份副本，"保存" 时写回 INI 并同步到 *cfg。
 */
void config_panel_show(HINSTANCE hInstance, AppConfig* cfg);

#ifdef __cplusplus
}
#endif
