/* New Nintendo 3DS libctru platform services for PS2RC. */
#include <3ds.h>
#include <errno.h>
#include <limits.h>
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "hle.h"
#include "hle_plat.h"
#include "hle_memcard.h"
#include "hle_3ds_padmap.h"

#define HLE_3DS_ROOT "sdmc:/3ds/ps2rc-sa/"
#define HLE_3DS_MAX_VFILES 512
#define HLE_3DS_MAX_OPEN 8
#define HLE_3DS_PATH_MAX 384
#define HLE_3DS_STACK_PAINT 0x57AC57ACu

typedef struct {
    char path[HLE_3DS_PATH_MAX];
    FILE *fp;
    int64_t size;
    int64_t pos;
    uint32_t used;
    int valid;
} Hle3dsFile;

static Hle3dsFile vfiles[HLE_3DS_MAX_VFILES];
static unsigned vfile_count, vfile_open, vfile_tick;
static uint64_t pace_shift_us, clock_start_us;
static FILE *session_log;
static LightLock disc_lock;
static int disc_lock_ready;
static char data_root[HLE_3DS_PATH_MAX] = HLE_3DS_ROOT;
const char *hle_mc_host_dir; /* no memory card is mounted unless configured explicitly */

static uint64_t now_ticks(void) { return svcGetSystemTick(); }

static uint64_t ticks_to_us(uint64_t ticks)
{
    const uint64_t hz = SYSCLOCK_ARM11;
    return (ticks / hz) * 1000000u + (ticks % hz) * 1000000u / hz;
}

uint32_t hle_3ds_platform_us(void) { return (uint32_t)ticks_to_us(now_ticks()); }

int posix_memalign(void **result, size_t alignment, size_t size)
{
    if (!result || alignment < sizeof(void *) || (alignment & (alignment - 1u)) || alignment % sizeof(void *))
        return EINVAL;
    void *p = memalign(alignment, size);
    if (!p) return ENOMEM;
    *result = p;
    return 0;
}

static int resolve_path(const char *input, char *output, size_t output_size)
{
    if (!input || !*input || !output || output_size == 0) return 0;
    if (strstr(input, ":/") || input[0] == '/') {
        if (snprintf(output, output_size, "%s", input) >= (int)output_size) return 0;
    } else if (snprintf(output, output_size, "%s%s", data_root, input) >= (int)output_size) {
        return 0;
    }
    for (char *p = output; *p; ++p) if (*p == '\\') *p = '/';
    return 1;
}

static int file_open_slot(unsigned index)
{
    Hle3dsFile *f = &vfiles[index];
    if (f->fp) { f->used = ++vfile_tick; return 1; }
    if (vfile_open >= HLE_3DS_MAX_OPEN) {
        unsigned oldest = HLE_3DS_MAX_VFILES;
        uint32_t stamp = UINT32_MAX;
        for (unsigned i = 0; i < vfile_count; ++i)
            if (vfiles[i].fp && vfiles[i].used < stamp) { oldest = i; stamp = vfiles[i].used; }
        if (oldest == HLE_3DS_MAX_VFILES) return 0;
        fclose(vfiles[oldest].fp);
        vfiles[oldest].fp = NULL;
        vfile_open--;
    }
    f->fp = fopen(f->path, "rb");
    if (!f->fp) return 0;
    f->used = ++vfile_tick;
    f->pos = -1;
    vfile_open++;
    return 1;
}

int hle_plat_open(const char *path)
{
    char resolved[HLE_3DS_PATH_MAX];
    if (!resolve_path(path, resolved, sizeof(resolved))) return -1;
    for (unsigned i = 0; i < vfile_count; ++i)
        if (!strcmp(vfiles[i].path, resolved)) return file_open_slot(i) ? (int)i : -1;
    if (vfile_count >= HLE_3DS_MAX_VFILES) return -1;
    unsigned i = vfile_count;
    Hle3dsFile *f = &vfiles[i];
    memset(f, 0, sizeof(*f));
    if (snprintf(f->path, sizeof(f->path), "%s", resolved) >= (int)sizeof(f->path)) return -1;
    f->size = -1;
    f->pos = -1;
    f->valid = 1;
    if (!file_open_slot(i)) { memset(f, 0, sizeof(*f)); return -1; }
    if (fseek(f->fp, 0, SEEK_END) != 0) return -1;
    long size = ftell(f->fp);
    if (size < 0 || fseek(f->fp, 0, SEEK_SET) != 0) return -1;
    f->size = size;
    f->pos = 0;
    vfile_count++;
    return (int)i;
}

int64_t hle_plat_pread(int handle, uint64_t offset, void *dst, uint32_t size)
{
    if (handle < 0 || (unsigned)handle >= vfile_count || !dst || offset > (uint64_t)LONG_MAX) return -1;
    Hle3dsFile *f = &vfiles[handle];
    if (!f->valid || !file_open_slot((unsigned)handle)) return -1;
    if (f->pos != (int64_t)offset && fseek(f->fp, (long)offset, SEEK_SET) != 0) return -1;
    size_t got = fread(dst, 1, size, f->fp);
    if (ferror(f->fp)) { clearerr(f->fp); f->pos = -1; return -1; }
    f->pos = (int64_t)offset + (int64_t)got;
    return (int64_t)got;
}

int64_t hle_plat_size(int handle)
{
    if (handle < 0 || (unsigned)handle >= vfile_count) return -1;
    return vfiles[handle].valid ? vfiles[handle].size : -1;
}

const char *hle_plat_disc_root(void) { return data_root; }

void hle_plat_log_write(const char *text, size_t size)
{
    if (!session_log) {
        char path[HLE_3DS_PATH_MAX];
        if (resolve_path("session.log", path, sizeof(path))) session_log = fopen(path, "wb");
    }
    if (session_log) fwrite(text, 1, size, session_log);
}

void hle_plat_log_flush(void)
{
    if (session_log) fflush(session_log);
}

void hle_plat_milestone(const char *line)
{
    hle_plat_log_write(line, strlen(line));
    hle_plat_log_write("\n", 1);
    hle_plat_log_flush();
    printf("%s\n", line);
}

void hle_plat_result_line(const char *line) { hle_plat_milestone(line); }

int hle_plat_shot(const char *tag, char *why, unsigned why_size)
{
    (void)tag;
    if (why && why_size) snprintf(why, why_size, "3DS screenshot capture is not wired yet");
    return -1;
}

uint16_t hle_plat_pad_read(uint8_t sticks[4])
{
    hidScanInput();
    const u32 keys = hidKeysHeld();
    const uint16_t buttons = hle_3ds_pad_map_buttons(keys);
    circlePosition circle = {0, 0}, cstick = {0, 0};
    hidCircleRead(&circle);
    irrstCstickRead(&cstick);
    hle_3ds_pad_map_sticks(circle.dx, circle.dy, cstick.dx, cstick.dy, sticks);
    return buttons;
}

void hle_plat_backtrace(void) {}
void hle_plat_note_mmio(void *ra, uint32_t address) { (void)ra; (void)address; }
const char *hle_plat_fn_name(void *pc) { (void)pc; return NULL; }
int hle_plat_hw_totals(uint64_t total[20], uint64_t *us) { (void)total; (void)us; return 0; }
int hle_plat_hw_door(uint32_t raw[20]) { (void)raw; return 0; }

void *hle_plat_prof_alloc(size_t size) { return memalign(64, (size + 63u) & ~(size_t)63u); }
void *hle_plat_big_alloc(size_t size)
{
    void *p = memalign(64, (size + 63u) & ~(size_t)63u);
    if (p) memset(p, 0, size);
    return p;
}
void *hle_plat_stack_alloc(size_t size)
{
    void *p = memalign(64, (size + 63u) & ~(size_t)63u);
    if (!p) return NULL;
    uint32_t *w = p;
    for (size_t i = 0; i < size / sizeof(*w); ++i) w[i] = HLE_3DS_STACK_PAINT;
    return p;
}

double hle_host_seconds(void)
{
    uint64_t us = ticks_to_us(now_ticks());
    if (!clock_start_us) clock_start_us = us;
    return (double)(us - clock_start_us) / 1000000.0;
}

uint64_t hle_plat_pace_now_us(void)
{
    return ticks_to_us(now_ticks()) - pace_shift_us;
}

void hle_plat_pace_adjust(int64_t us)
{
    pace_shift_us = (uint64_t)((int64_t)pace_shift_us + us);
}

void hle_plat_pace(uint64_t virtual_time)
{
    uint64_t due_us = virtual_time / EE_HZ * 1000000u + virtual_time % EE_HZ * 1000000u / EE_HZ;
    for (;;) {
        uint64_t now = hle_plat_pace_now_us();
        if (now >= due_us) return;
        uint64_t wait_us = due_us - now;
        if (wait_us > 1000000u) wait_us = 1000000u;
        svcSleepThread((s64)(wait_us * 1000u));
    }
}

int hle_plat_write_file(const char *name, const void *data, size_t size)
{
    char path[HLE_3DS_PATH_MAX];
    if (!resolve_path(name, path, sizeof(path))) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    const size_t wrote = fwrite(data, 1, size, f);
    const int close_ok = fclose(f) == 0;
    return wrote == size && close_ok ? 0 : -1;
}

void hle_plat_disc_lock_create(void)
{
    LightLock_Init(&disc_lock);
    disc_lock_ready = 1;
}
void hle_plat_disc_lock(void) { if (disc_lock_ready) LightLock_Lock(&disc_lock); }
void hle_plat_disc_unlock(void) { if (disc_lock_ready) LightLock_Unlock(&disc_lock); }

void hle_plat_exit(int code)
{
    hle_plat_log_flush();
    printf("\nPS2RC runtime exited with code %d. Press START to close.\n", code);
    for (;;) {
        if (!aptMainLoop()) break;
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
    exit(code);
}

int hle_plat_mc_config(char *root, size_t size)
{
    (void)root; (void)size;
    return 0; /* saving remains disabled unless a memory-card path is deliberately configured */
}

void hle_plat_mc_now(HleMcTime *out)
{
    time_t now = time(NULL);
    struct tm t;
    gmtime_r(&now, &t);
    out->sec = (uint8_t)t.tm_sec; out->min = (uint8_t)t.tm_min; out->hour = (uint8_t)t.tm_hour;
    out->day = (uint8_t)t.tm_mday; out->month = (uint8_t)(t.tm_mon + 1); out->year = (uint16_t)(t.tm_year + 1900);
}
void hle_plat_mc_set_mtime(const char *path, const HleMcTime *t) { (void)path; (void)t; }
int hle_plat_mc_list(const char *dir, HleMcListFn fn, void *ctx) { (void)dir; (void)fn; (void)ctx; return -1; }
int hle_plat_mc_exists(const char *path) { (void)path; return 0; }
void hle_plat_mc_release(void) {}
int hle_plat_mc_mkdir(const char *path) { (void)path; return -1; }
int hle_plat_mc_rmdir(const char *path) { (void)path; return -1; }
int hle_plat_mc_remove(const char *path) { (void)path; return -1; }
int hle_plat_mc_rename(const char *from, const char *to) { (void)from; (void)to; return -1; }
int64_t hle_plat_mc_read(const char *path, uint32_t offset, void *dst, uint32_t size) { (void)path; (void)offset; (void)dst; (void)size; return -1; }
int64_t hle_plat_mc_write(const char *path, uint32_t offset, const void *src, uint32_t size, int create) { (void)path; (void)offset; (void)src; (void)size; (void)create; return -1; }

/* libctru/newlib has no timegm. Kept for the shared memory-card implementation if enabled later. */
time_t timegm(struct tm *t)
{
    static const int mdays[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int year = t->tm_year + 1900;
    int64_t days = (int64_t)(year - 1970) * 365;
    for (int y = 1970; y < year; ++y) if ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ++days;
    for (int m = 0; m < t->tm_mon; ++m) days += mdays[m] + (m == 1 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0));
    days += t->tm_mday - 1;
    return (time_t)((((days * 24 + t->tm_hour) * 60 + t->tm_min) * 60) + t->tm_sec);
}
