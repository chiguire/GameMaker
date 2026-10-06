/* Borland findfirst/findnext over the Win32 API. Names come back DOS-style: 8.3 short names, upper case.
 * (Kept apart from dosplat.c because <windows.h> clashes with raylib's symbol names.) */
#include "dosplat.h"
#include <ctype.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>

static void fill_ffblk(struct ffblk *f, const WIN32_FIND_DATAA *d)
{
    const char *name = d->cAlternateFileName[0] ? d->cAlternateFileName : d->cFileName;   /* short name if any */
    size_t n = strlen(name);
    if (n > sizeof f->ff_name - 1) n = sizeof f->ff_name - 1;
    for (size_t i = 0; i < n; i++) f->ff_name[i] = (char)toupper((unsigned char)name[i]);
    f->ff_name[n] = 0;
    f->ff_attrib = (char)((d->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ? 0x10 : 0) |
                          (d->dwFileAttributes & FILE_ATTRIBUTE_READONLY ? 0x01 : 0) |
                          (d->dwFileAttributes & FILE_ATTRIBUTE_HIDDEN ? 0x02 : 0) |
                          (d->dwFileAttributes & FILE_ATTRIBUTE_SYSTEM ? 0x04 : 0) |
                          (d->dwFileAttributes & FILE_ATTRIBUTE_ARCHIVE ? 0x20 : 0));
    f->ff_fsize = (int32_t)d->nFileSizeLow;
}

/* DOS rule: an entry matches if its attributes are within (normal files | requested attributes) */
static int attr_ok(const struct ffblk *f, int16_t want)
{
    return ((unsigned char)f->ff_attrib & ~(unsigned)(want | 0x20 | 0x01)) == 0;
}

static int16_t last_attrib;

int16_t gm_findfirst(const char *pattern, struct ffblk *f, int16_t attrib)
{
    WIN32_FIND_DATAA d;
    HANDLE h = FindFirstFileA(pattern, &d);
    if (h == INVALID_HANDLE_VALUE) return -1;
    f->ff_handle = (intptr_t)h;
    last_attrib = attrib;
    for (;;) {
        fill_ffblk(f, &d);
        if (attr_ok(f, attrib)) return 0;
        if (!FindNextFileA(h, &d)) { FindClose(h); f->ff_handle = 0; return -1; }
    }
}

int16_t gm_findnext(struct ffblk *f)
{
    HANDLE h = (HANDLE)f->ff_handle;
    WIN32_FIND_DATAA d;
    if (!h) return -1;
    while (FindNextFileA(h, &d)) {
        fill_ffblk(f, &d);
        if (attr_ok(f, last_attrib)) return 0;
    }
    FindClose(h);
    f->ff_handle = 0;
    return -1;
}
#else
int16_t gm_findfirst(const char *pattern, struct ffblk *f, int16_t attrib) { (void)pattern; (void)f; (void)attrib; return -1; }
int16_t gm_findnext(struct ffblk *f) { (void)f; return -1; }
#endif
