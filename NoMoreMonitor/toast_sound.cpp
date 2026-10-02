#include "toast_sound.h"
#include <mmsystem.h>
#include <mfapi.h>
#include <mfobjects.h>
#include <mferror.h>
#include <mfplay.h>
#include <wchar.h>

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "ole32.lib")

#define TOAST_MCI_ALIAS L"nmm_toast_snd"

typedef HRESULT (WINAPI *PFN_MFCreateMediaPlayer)(
    LPCWSTR pszURL, HWND hwnd, IMFPMediaPlayerCallback* pCallback, IMFPMediaPlayer** ppPlayer);

static bool s_mf_started = false;
static IMFPMediaPlayer* s_player = NULL;
static PFN_MFCreateMediaPlayer s_create_player = NULL;
static bool s_create_player_tried = false;

/*
 * 强制释放 wave/MCI/MF 播放资源。
 * 音量合成器静音/取消静音后，PlaySound/MCI 容易留下占用或错误状态，
 * 导致后续播放全部失败。每次播放前和停止时都做一次彻底清理。
 */
static void purge_wave(void) {
    /* SND_PURGE：停止本任务全部声音并释放 wave 设备 */
    PlaySoundW(NULL, NULL, SND_PURGE | SND_NOSTOP);
    PlaySoundW(NULL, NULL, 0);

    /* 用一次极短的 waveOut open/close 重置 wave mapper 分配 */
    WAVEFORMATEX fmt;
    ZeroMemory(&fmt, sizeof(fmt));
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 1;
    fmt.nSamplesPerSec = 8000;
    fmt.nAvgBytesPerSec = 8000;
    fmt.nBlockAlign = 1;
    fmt.wBitsPerSample = 8;
    HWAVEOUT hwo = NULL;
    if (waveOutOpen(&hwo, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR) {
        waveOutReset(hwo);
        waveOutClose(hwo);
    }
}

static void mci_force_close(void) {
    /* 无论标志位如何都尝试 stop/close，避免残留设备占用 */
    mciSendStringW(L"stop " TOAST_MCI_ALIAS, NULL, 0, NULL);
    mciSendStringW(L"close " TOAST_MCI_ALIAS, NULL, 0, NULL);
}

static void mf_shutdown_player(void) {
    if (s_player) {
        /* 先 Stop 再 Shutdown，静音中断的会话才能干净释放 */
        s_player->Stop();
        s_player->Shutdown();
        s_player->Release();
        s_player = NULL;
    }
}

void toast_sound_stop(void) {
    mf_shutdown_player();
    mci_force_close();
    purge_wave();
}

static void play_system_sound(void) {
    toast_sound_stop();
    if (PlaySoundW(L"SystemNotification", NULL, SND_ALIAS | SND_ASYNC | SND_NODEFAULT)) return;
    if (PlaySoundW(L"SystemAsterisk", NULL, SND_ALIAS | SND_ASYNC | SND_NODEFAULT)) return;
    if (PlaySoundW(L"Windows Notify System Generic", NULL, SND_ALIAS | SND_ASYNC | SND_NODEFAULT)) return;
    MessageBeep(MB_ICONINFORMATION);
}

static bool has_ext(const wchar_t* path, const wchar_t* ext) {
    size_t n = wcslen(path), m = wcslen(ext);
    if (n < m) return false;
    return _wcsicmp(path + n - m, ext) == 0;
}

static bool is_wav(const wchar_t* path) {
    return has_ext(path, L".wav") || has_ext(path, L".wave");
}

static bool mf_ensure(void) {
    if (s_mf_started) return true;
    HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
    if (FAILED(hr)) return false;
    s_mf_started = true;
    return true;
}

static PFN_MFCreateMediaPlayer get_mf_create(void) {
    if (s_create_player_tried) return s_create_player;
    s_create_player_tried = true;
    HMODULE dll = LoadLibraryW(L"mfplay.dll");
    if (dll) {
        s_create_player = (PFN_MFCreateMediaPlayer)GetProcAddress(dll, "MFCreateMediaPlayer");
    }
    return s_create_player;
}

/* Media Foundation：wav/mp3/flac/aac/wma 等 */
static bool play_via_mf(const wchar_t* path) {
    PFN_MFCreateMediaPlayer create = get_mf_create();
    if (!create || !mf_ensure()) return false;

    mf_shutdown_player();

    HRESULT hr = create(NULL, NULL, NULL, &s_player);
    if (FAILED(hr) || !s_player) {
        s_player = NULL;
        return false;
    }

    hr = s_player->CreateMediaItemFromURL(path, TRUE, NULL, NULL);
    if (FAILED(hr)) {
        mf_shutdown_player();
        return false;
    }
    return true;
}

/* MCI 兜底 */
static bool play_via_mci(const wchar_t* path) {
    mci_force_close();

    wchar_t cmd[1024];
    swprintf_s(cmd, L"open \"%s\" alias " TOAST_MCI_ALIAS, path);
    MCIERROR err = mciSendStringW(cmd, NULL, 0, NULL);
    if (err) {
        swprintf_s(cmd, L"open \"%s\" type mpegvideo alias " TOAST_MCI_ALIAS, path);
        err = mciSendStringW(cmd, NULL, 0, NULL);
    }
    if (err) {
        swprintf_s(cmd, L"open \"%s\" type waveaudio alias " TOAST_MCI_ALIAS, path);
        err = mciSendStringW(cmd, NULL, 0, NULL);
    }
    if (err) return false;

    err = mciSendStringW(L"play " TOAST_MCI_ALIAS, NULL, 0, NULL);
    if (err) {
        mci_force_close();
        return false;
    }
    return true;
}

static bool play_wav(const wchar_t* path) {
    if (PlaySoundW(path, NULL, SND_FILENAME | SND_ASYNC | SND_NODEFAULT)) {
        return true;
    }
    /* 失败时清理后再试一次（静音切换后 wave 设备偶发占用） */
    purge_wave();
    return PlaySoundW(path, NULL, SND_FILENAME | SND_ASYNC | SND_NODEFAULT) != FALSE;
}

bool toast_sound_play(const wchar_t* path) {
    /* 每次播放前彻底释放上一段，避免静音/取消静音后卡死 */
    toast_sound_stop();

    if (!path || path[0] == L'\0') {
        play_system_sound();
        return true;
    }

    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
        return false;
    }

    if (is_wav(path) && play_wav(path)) {
        return true;
    }

    if (play_via_mf(path)) {
        return true;
    }

    if (play_via_mci(path)) {
        return true;
    }

    /* 三路都失败：再清一次设备，给下一次试听留干净状态 */
    toast_sound_stop();
    return false;
}
