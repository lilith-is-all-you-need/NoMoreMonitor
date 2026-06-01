#pragma once
#include <Windows.h>
bool injector(HANDLE proc, wchar_t* dll_path);
bool ejector(HANDLE proc, const wchar_t* dll_name);