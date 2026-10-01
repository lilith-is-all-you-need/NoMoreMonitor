#define _CRT_SECURE_NO_WARNINGS
#include "position_panel.h"
#include "notice.h"

/* 九宫格单选按钮 ID 与位置索引 0~8 连续对应 */
#define IDC_POS_BASE 2101
#define IDC_OFFSET_X 2110
#define IDC_OFFSET_Y 2111

static AppConfig* s_cfg = NULL;
static AppConfig  s_orig;
static BOOL       s_ready = FALSE;

static void ApplyLive(HWND hdlg) {
    if (!s_cfg) return;

    int pos = 4;
    for (int i = 0; i < 9; i++) {
        if (IsDlgButtonChecked(hdlg, IDC_POS_BASE + i) == BST_CHECKED) {
            pos = i;
            break;
        }
    }
    s_cfg->text_position = pos;

    BOOL ok;
    int ox = GetDlgItemInt(hdlg, IDC_OFFSET_X, &ok, TRUE);
    if (ok) s_cfg->text_offset_x = ox;
    int oy = GetDlgItemInt(hdlg, IDC_OFFSET_Y, &ok, TRUE);
    if (ok) s_cfg->text_offset_y = oy;

    notice_refresh_osd();
}

static void Restore(void) {
    if (!s_cfg) return;
    *s_cfg = s_orig;
    notice_refresh_osd();
}

static INT_PTR CALLBACK PositionDlgProc(HWND hdlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        s_orig = *s_cfg;
        s_ready = FALSE;

        CheckRadioButton(hdlg, IDC_POS_BASE, IDC_POS_BASE + 8,
                         IDC_POS_BASE + (s_cfg->text_position >= 0 && s_cfg->text_position <= 8 ? s_cfg->text_position : 4));
        SetDlgItemInt(hdlg, IDC_OFFSET_X, (UINT)s_cfg->text_offset_x, TRUE);
        SetDlgItemInt(hdlg, IDC_OFFSET_Y, (UINT)s_cfg->text_offset_y, TRUE);

        s_ready = TRUE;
        return TRUE;
    }

    case WM_COMMAND: {
        WORD id = LOWORD(wp);
        WORD code = HIWORD(wp);

        if (s_ready && code == EN_CHANGE && (id == IDC_OFFSET_X || id == IDC_OFFSET_Y)) {
            ApplyLive(hdlg);
            return TRUE;
        }
        if (s_ready && code == BN_CLICKED && id >= IDC_POS_BASE && id <= IDC_POS_BASE + 8) {
            ApplyLive(hdlg);
            return TRUE;
        }

        switch (id) {
        case IDOK:
            ApplyLive(hdlg);
            config_save(s_cfg, NULL);   /* 持久化到 INI */
            EndDialog(hdlg, IDOK);
            return TRUE;
        case IDCANCEL:
            Restore();
            EndDialog(hdlg, IDCANCEL);
            return TRUE;
        }
        break;
    }

    case WM_CLOSE:
        Restore();
        EndDialog(hdlg, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

/* ---------- 内存对话框模板构建器（参照 LilithTimer settings.c） ---------- */
typedef struct {
    DLGTEMPLATE* dlg;
    WCHAR* p;
    int count;
} DlgBuilder;

static void DlgAlign(DlgBuilder* b) {
    while ((ULONG_PTR)b->p % 4) b->p++;
}

static void DlgBegin(DlgBuilder* b, BYTE* buf, short cx, short cy, const WCHAR* title) {
    b->dlg = (DLGTEMPLATE*)buf;
    b->dlg->style = DS_SETFONT | DS_FIXEDSYS | WS_POPUP | WS_CAPTION | WS_SYSMENU;
    b->dlg->dwExtendedStyle = 0;
    b->dlg->cdit = 0;
    b->dlg->x = 0; b->dlg->y = 0;
    b->dlg->cx = cx; b->dlg->cy = cy;
    b->count = 0;

    b->p = (WCHAR*)(b->dlg + 1);
    *b->p++ = 0;                 /* 菜单：无 */
    *b->p++ = 0;                 /* 窗口类：默认 */
    wcscpy(b->p, title);
    b->p += wcslen(title) + 1;
    *(WORD*)b->p = 9; b->p += 1; /* 字号 9pt */
    wcscpy(b->p, L"Microsoft YaHei UI");
    b->p += wcslen(L"Microsoft YaHei UI") + 1;
}

static void DlgAddAtom(DlgBuilder* b, WORD atom, DWORD style, DWORD exStyle,
                       int x, int y, int cx, int cy, int id, const WCHAR* text) {
    DLGITEMTEMPLATE* item;
    DlgAlign(b);
    item = (DLGITEMTEMPLATE*)b->p;
    item->style = style;
    item->dwExtendedStyle = exStyle;
    item->x = (short)x; item->y = (short)y;
    item->cx = (short)cx; item->cy = (short)cy;
    item->id = (WORD)id;

    b->p = (WCHAR*)(item + 1);
    *b->p++ = 0xFFFF;
    *b->p++ = atom;
    if (text) {
        wcscpy(b->p, text);
        b->p += wcslen(text) + 1;
    } else {
        *b->p++ = 0;
    }
    *b->p++ = 0;
    b->count++;
}

static DLGTEMPLATE* DlgEnd(DlgBuilder* b) {
    b->dlg->cdit = (WORD)b->count;
    return b->dlg;
}

#define ATOM_BUTTON 0x0080
#define ATOM_EDIT   0x0081
#define ATOM_STATIC 0x0082

static DLGTEMPLATE* CreateTemplate(void) {
    static BYTE buffer[4096];
    DlgBuilder b;
    int i;

    static const LPCWSTR labels[9] = {
        L"左上", L"上", L"右上", L"左", L"中", L"右", L"左下", L"下", L"右下"
    };
    static const int rx[9] = { 12, 74, 136, 12, 74, 136, 12, 74, 136 };
    static const int ry[9] = { 12, 12, 12, 34, 34, 34, 56, 56, 56 };

    DlgBegin(&b, buffer, 250, 142, L"文字位置");

    DlgAddAtom(&b, ATOM_BUTTON, WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 0,
               6, 4, 238, 78, 0xFFFF, L"九宫格");

    for (i = 0; i < 9; i++) {
        DlgAddAtom(&b, ATOM_BUTTON,
                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON |
                   (i == 0 ? WS_GROUP : 0), 0,
                   rx[i], ry[i], 50, 14, IDC_POS_BASE + i, labels[i]);
    }

    DlgAddAtom(&b, ATOM_STATIC, WS_CHILD | WS_VISIBLE, 0,
               12, 88, 50, 12, 0xFFFF, L"X 偏移:");
    DlgAddAtom(&b, ATOM_EDIT, WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_NUMBER, 0,
               64, 86, 52, 14, IDC_OFFSET_X, NULL);

    DlgAddAtom(&b, ATOM_STATIC, WS_CHILD | WS_VISIBLE, 0,
               132, 88, 50, 12, 0xFFFF, L"Y 偏移:");
    DlgAddAtom(&b, ATOM_EDIT, WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_NUMBER, 0,
               184, 86, 52, 14, IDC_OFFSET_Y, NULL);

    DlgAddAtom(&b, ATOM_STATIC, WS_CHILD | WS_VISIBLE, 0,
               12, 106, 224, 12, 0xFFFF, L"偏移为像素，可为负（实时预览）");

    DlgAddAtom(&b, ATOM_BUTTON, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 0,
               112, 120, 56, 18, IDOK, L"确定");
    DlgAddAtom(&b, ATOM_BUTTON, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
               178, 120, 56, 18, IDCANCEL, L"取消");

    return DlgEnd(&b);
}

void position_panel_show(HINSTANCE hInstance, AppConfig* cfg) {
    if (!cfg) return;
    s_cfg = cfg;
    DialogBoxIndirectParamW(hInstance, CreateTemplate(), NULL, PositionDlgProc, 0);
    s_cfg = NULL;
}
