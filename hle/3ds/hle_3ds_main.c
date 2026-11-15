/* PS2RC 3DS application entry: load local ELF data and start the shared HLE scheduler. */
#include <3ds.h>
#include <limits.h>
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hle.h"
#include "hle_cfg.h"
#include "hle_plat.h"
#include "hle_prof.h"
#include "hle_3ds_gfx.h"

#define HLE_3DS_ROOT "sdmc:/3ds/ps2rc-sa/"

static void wait_start(void)
{
    for (;;) {
        if (!aptMainLoop()) break;
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
}

static int load_elf_data(const char *path, uint32_t *entry)
{
    FILE *file = fopen(path, "rb");
    if (!file) { printf("Cannot open %s\n", path); return 0; }
    uint8_t eh[52];
    if (fread(eh, 1, sizeof(eh), file) != sizeof(eh) || memcmp(eh, "\x7f" "ELF", 4) ||
        eh[4] != 1 || eh[5] != 1) {
        fclose(file);
        printf("Invalid ELF header: %s\n", path);
        return 0;
    }
    uint32_t e, phoff;
    uint16_t phentsize, phnum;
    memcpy(&e, eh + 24, sizeof(e));
    memcpy(&phoff, eh + 28, sizeof(phoff));
    memcpy(&phentsize, eh + 42, sizeof(phentsize));
    memcpy(&phnum, eh + 44, sizeof(phnum));
    if (phentsize < 32 || (uint64_t)phoff + (uint64_t)phentsize * phnum > LONG_MAX) {
        fclose(file);
        printf("Invalid ELF program-header table\n");
        return 0;
    }
    for (uint32_t i = 0; i < phnum; ++i) {
        uint32_t ph[8];
        const long where = (long)phoff + (long)i * phentsize;
        if (fseek(file, where, SEEK_SET) || fread(ph, sizeof(uint32_t), 8, file) != 8) {
            fclose(file);
            printf("ELF program-header read failed (%lu)\n", (unsigned long)i);
            return 0;
        }
        if (ph[0] != 1 || ph[4] == 0) continue;
        const uint64_t end = (uint64_t)ph[2] + ph[5];
        if (end > hle_ram_end || ph[2] > hle_ram_end || end < ph[2] || ph[4] > ph[5]) {
            fclose(file);
            printf("ELF segment outside guest RAM: %08lx + %08lx\n",
                   (unsigned long)ph[2], (unsigned long)ph[5]);
            return 0;
        }
        uint32_t skip = 0;
        if (ph[2] <= HLE_CODE_LO && (uint64_t)ph[2] + ph[4] > HLE_CODE_HI)
            skip = HLE_CODE_HI - ph[2]; /* translated code stays in ARM memory, not the guest RAM window */
        if (skip > ph[4] || (uint64_t)ph[1] + skip + (ph[4] - skip) > LONG_MAX ||
            fseek(file, (long)(ph[1] + skip), SEEK_SET) ||
            fread(hle_ram + ph[2] + skip, 1, ph[4] - skip, file) != ph[4] - skip) {
            fclose(file);
            printf("ELF segment load failed at %08lx\n", (unsigned long)ph[2]);
            return 0;
        }
        LOG("loaded ELF segment %08lx filesz %08lx memsz %08lx\n",
            (unsigned long)ph[2], (unsigned long)ph[4], (unsigned long)ph[5]);
    }
    fclose(file);
    *entry = e;
    return 1;
}

int main(void)
{
    gfxInitDefault();
    consoleInit(GFX_BOTTOM, NULL);
    printf("PS2RC New 3DS runtime\n");
    printf("PS2RC translated ARM code; game data stays on SD\n\n");
    if (R_FAILED(fsInit())) {
        printf("FATAL: SD filesystem service unavailable\n");
        wait_start();
        gfxExit();
        return 1;
    }
    if (!hle_3ds_gfx_init()) {
        printf("FATAL: Citro3D startup failed\n");
        wait_start();
        fsExit();
        gfxExit();
        return 1;
    }
    hle_3ds_gfx_present();

    hle_ram = memalign(64, HLE_RAM_SIZE);
    if (!hle_ram) {
        printf("FATAL: could not reserve %u MiB guest RAM\n", HLE_RAM_SIZE >> 20);
        wait_start();
        hle_3ds_gfx_shutdown();
        fsExit();
        gfxExit();
        return 1;
    }
    memset(hle_ram, 0, HLE_RAM_SIZE);
    hle_ram_end = HLE_PACK_RAM_END_PSP;
    if (hle_ram_end > HLE_RAM_SIZE) hle_ram_end = HLE_RAM_SIZE;

    char elf_path[256], disc_dir[256], map_path[256], meta_path[256];
    snprintf(elf_path, sizeof(elf_path), "%sdisc/%s", HLE_3DS_ROOT, HLE_PACK_BOOT_ELF);
    snprintf(disc_dir, sizeof(disc_dir), "%sdisc", HLE_3DS_ROOT);
    snprintf(map_path, sizeof(map_path), "%sdiscmap.txt", HLE_3DS_ROOT);
    snprintf(meta_path, sizeof(meta_path), "%siso_meta.bin", HLE_3DS_ROOT);
    uint32_t entry = 0;
    printf("Loading %s\n", HLE_PACK_BOOT_ELF);
    if (!load_elf_data(elf_path, &entry)) {
        printf("FATAL: game ELF load failed\n");
        hle_plat_log_flush();
        wait_start();
        free(hle_ram);
        hle_3ds_gfx_shutdown();
        fsExit();
        gfxExit();
        return 2;
    }
    hle_elf_path = HLE_PACK_BOOT_ELF;
    if (!hle_disc_init(disc_dir, map_path, meta_path)) {
        printf("FATAL: disc map unavailable: %s\n", map_path);
        hle_plat_log_flush();
        wait_start();
        free(hle_ram);
        hle_3ds_gfx_shutdown();
        fsExit();
        gfxExit();
        return 3;
    }

    printf("ELF entry %08lx; guest RAM %lu MiB\n", (unsigned long)entry,
           (unsigned long)(hle_ram_end >> 20));
    printf("Starting PS2RC HLE kernel and scheduler...\n");
    hle_set_vcallms_mode(VCALLMS_HLE);
#ifdef HLE_3DS_PROF
    hle_pz_opt_on = 1; hle_pz_opt_summary_s = 20;
#endif
    hle_pz_init();
    hle_boot_init(entry);
    hle_plat_log_flush();
    hle_sched_run();
    LOG("scheduler returned\n");
    hle_exit(0);
    return 0;
}
