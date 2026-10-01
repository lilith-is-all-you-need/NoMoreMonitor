#include "img_loader.h"
#include <stdlib.h>

#include "stb_image.h"
#include "stb_image_resize2.h"

unsigned char* img_load_file(const wchar_t* path, int* out_w, int* out_h) {
    if (!path || path[0] == L'\0') return NULL;

    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return NULL;

    DWORD size = GetFileSize(f, NULL);
    if (size == 0 || size == INVALID_FILE_SIZE || size > (256u << 20)) {
        CloseHandle(f);
        return NULL;
    }

    unsigned char* buf = (unsigned char*)malloc(size);
    if (!buf) { CloseHandle(f); return NULL; }

    DWORD read = 0;
    BOOL ok = ReadFile(f, buf, size, &read, NULL);
    CloseHandle(f);
    if (!ok || read != size) { free(buf); return NULL; }

    int w = 0, h = 0, ch = 0;
    unsigned char* px = stbi_load_from_memory(buf, (int)size, &w, &h, &ch, 4);
    free(buf);
    if (!px) return NULL;

    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
    return px;
}

unsigned char* img_scale_rgba(unsigned char* px, int* w, int* h, int new_w, int new_h) {
    if (!px || !w || !h || new_w < 1 || new_h < 1) return px;

    unsigned char* out = (unsigned char*)malloc((size_t)new_w * new_h * 4);
    if (!out) return px;

    stbir_resize(px, *w, *h, (*w) * 4,
                 out, new_w, new_h, new_w * 4,
                 STBIR_RGBA, STBIR_TYPE_UINT8, STBIR_EDGE_CLAMP, STBIR_FILTER_CATMULLROM);

    stbi_image_free(px);
    *w = new_w;
    *h = new_h;
    return out;
}

void img_free(unsigned char* px) {
    stbi_image_free(px);
}
