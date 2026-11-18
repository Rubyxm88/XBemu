/*
 * xemu: one-time XBL Dashboard install, the parts that talk to QEMU
 * (the rest is in ui/xui/dash-install.cc)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/atomic.h"
#include "block/accounting.h"
#include "qemu/bswap.h"
#include "system/block-backend.h"
#include "system/reset.h"

#include "xemu-dashinstall.h"
#include "xemu-settings.h"
#include "xemu-xbonline.h"
#include "xemu-notifications.h"

#define HDD_NAME "ide0-hd0"

static int resets;
static bool reset_hooked;
static bool swap_pending;

static bool swap_dashboards(void);
static bool oxos_settings_pending;
static void write_oxos_settings(void);

static void on_reset(void *opaque)
{
    qatomic_inc(&resets);
    if (swap_pending) {
        swap_pending = false;
        swap_dashboards();
    }
    if (oxos_settings_pending) {
        oxos_settings_pending = false;
        write_oxos_settings();
    }
}

static BlockBackend *hdd(void)
{
    BlockBackend *blk = blk_by_name(HDD_NAME);
    return blk && blk_is_available(blk) ? blk : NULL;
}

uint64_t xemu_dashinstall_hdd_written(void)
{
    BlockBackend *blk = hdd();
    return blk ? blk_get_stats(blk)->nr_bytes[BLOCK_ACCT_WRITE] : 0;
}

void xemu_dashinstall_hdd_flush(void)
{
    BlockBackend *blk = hdd();
    if (blk) {
        blk_flush(blk);
    }
}

int xemu_dashinstall_resets(void)
{
    if (!reset_hooked) {
        reset_hooked = true;
        qemu_register_reset(on_reset, NULL);
    }
    return qatomic_read(&resets);
}

/* --- Switching between the XBL Dashboard and the one it replaced --- */

/* System partition (C:) */
#define C_OFFSET 0x8CA80000ULL
#define C_SIZE 0x1F400000ULL
#define ENTRY_SIZE 64
#define NAME_MAX_LEN 42
#define ATTR_DIRECTORY 0x10

/* Where a file's directory entry is, by its name, in the root of C: */
typedef struct DashEntry {
    const char *name;
    int64_t offset;  /* of its 64-byte entry on the disk, 0 if not found */
    uint8_t entry[ENTRY_SIZE];
} DashEntry;

static bool find_entries(BlockBackend *blk, DashEntry *want, int n)
{
    uint8_t hdr[16];
    if (blk_pread(blk, C_OFFSET, sizeof(hdr), hdr, 0) < 0 ||
        memcmp(hdr, "FATX", 4)) {
        return false;
    }
    uint32_t cluster_size = ldl_le_p(hdr + 8) * 512;
    if (cluster_size < 512 || cluster_size > 128 * 1024) {
        return false;
    }
    uint32_t clusters = C_SIZE / cluster_size;
    bool wide = clusters >= 0xfff0;
    uint64_t fat = C_OFFSET + 0x1000;
    uint64_t data = fat + ((clusters * (wide ? 4ULL : 2ULL) + 0xfff) & ~0xfffULL);

    uint8_t *buf = g_malloc(cluster_size);
    uint32_t dir = 1;  /* the root directory */
    bool done = false;
    for (int hops = 0; dir && !done && hops < 256; hops++) {
        uint64_t at = data + (uint64_t)(dir - 1) * cluster_size;
        if (blk_pread(blk, at, cluster_size, buf, 0) < 0) {
            break;
        }
        for (uint32_t i = 0; i < cluster_size; i += ENTRY_SIZE) {
            const uint8_t *e = buf + i;
            if (e[0] == 0x00 || e[0] == 0xff) {
                done = true;  /* end of the directory */
                break;
            }
            if (e[0] > NAME_MAX_LEN || (e[1] & ATTR_DIRECTORY)) {
                continue;  /* deleted (0xe5) or a directory */
            }
            for (int k = 0; k < n; k++) {
                if (!want[k].offset && e[0] == strlen(want[k].name) &&
                    !g_ascii_strncasecmp((const char *)e + 2, want[k].name, e[0])) {
                    want[k].offset = at + i;
                    memcpy(want[k].entry, e, ENTRY_SIZE);
                }
            }
        }
        uint8_t next[4];
        if (blk_pread(blk, fat + (uint64_t)dir * (wide ? 4 : 2), wide ? 4 : 2,
                      next, 0) < 0) {
            break;
        }
        dir = wide ? ldl_le_p(next) : lduw_le_p(next);
        if (dir == 0 || dir >= (wide ? 0xfffffff0 : 0xfff0) || dir > clusters) {
            dir = 0;
        }
    }
    g_free(buf);
    return true;
}

/* A directory entry with another name; the rest (clusters, size, dates) kept */
static void rename_entry(uint8_t *e, const char *name)
{
    size_t len = strlen(name);
    e[0] = len;
    memset(e + 2, 0xff, NAME_MAX_LEN);
    memcpy(e + 2, name, len);
}

/* Runs while the console is stopped for its restart */
static bool swap_dashboards(void)
{
    BlockBackend *blk = hdd();
    const char *fail = NULL;
    DashEntry e[2] = { { XEMU_XBONLINE_DASH_FILE },
                       { XEMU_XBONLINE_DASH_BACKUP_FILE } };

    if (!blk) {
        fail = "no hard disk";
    } else {
        blk_drain(blk);
        if (!find_entries(blk, e, 2)) {
            fail = "C: could not be read";
        } else if (!e[0].offset || !e[1].offset) {
            fail = "the backup dashboard is missing";
        }
    }
    if (!fail) {
        rename_entry(e[0].entry, XEMU_XBONLINE_DASH_BACKUP_FILE);
        rename_entry(e[1].entry, XEMU_XBONLINE_DASH_FILE);
        if (blk_pwrite(blk, e[0].offset, ENTRY_SIZE, e[0].entry, 0) < 0 ||
            blk_pwrite(blk, e[1].offset, ENTRY_SIZE, e[1].entry, 0) < 0 ||
            blk_flush(blk) < 0) {
            fail = "writing C: failed";
        }
    }

    xemu_xbonline_probe_hdd(g_config.sys.files.hdd_path);
    int now = xemu_xbonline_main_dashboard();
    fprintf(stderr, "dash switch: %s, dashboard now %d, backup %d\n",
            fail ? fail : "swapped", now, xemu_xbonline_backup_dashboard());
    if (fail) {
        char *msg = g_strdup_printf("The dashboard was not switched (%s).", fail);
        xemu_queue_error_message(msg);
        g_free(msg);
        return false;
    }
    xemu_queue_notification(now == XBONLINE_DASH_XBL ?
                                "Switched to the XBL Dashboard" :
                                "Switched to the original dashboard. "
                                "The XBL Dashboard is kept as its backup.");
    return true;
}

void xemu_dashinstall_switch_dashboard(void)
{
    xemu_dashinstall_resets();  /* hooks the restart */
    swap_pending = true;
}

/* --- C:\OXOS\oxos_settings.ini for OXOS v0.4.13 and newer --- */

/*
 * Up to v0.4.12 OXOS looked for the dashboard in a fixed list that ended
 * with C:\Dashboard\default.xbe, where the XBL Dashboard installer puts it
 * on an XBemu console. From v0.4.13 OXOS tries only the dashboards that
 * C:\OXOS\oxos_settings.ini lists, then C:\xboxdash.xbe, so an XBemu
 * console stopped at "No Dashboard Found" (code 13). When that file is
 * missing it is written, before the BIOS reads the hard disk, with the old
 * list in the old order. A file that is there is never changed.
 */

static const char oxos_settings_text[] =
    "; C:\\OXOS\\oxos_settings.ini: OXOS's own settings.\r\n"
    "; Written by XBemu because it was missing: the dashboards in the order\r\n"
    "; OXOS v0.4.12 and older looked for them. XBemu never changes this\r\n"
    "; file once it is here.\r\n"
    "\r\n"
    "[dashboard]\r\n"
    "dashboard_1 = C:\\evoxdash.xbe\r\n"
    "dashboard_2 = E:\\evoxdash.xbe\r\n"
    "dashboard_3 = C:\\xboxdash.xbe\r\n"
    "dashboard_4 = C:\\Dashboard\\default.xbe\r\n";

#define OXOS_DIR_NAME "OXOS"
#define OXOS_SETTINGS_NAME "oxos_settings.ini"

/* The C: partition's layout */
typedef struct FatxGeo {
    uint32_t cluster_size;
    uint32_t clusters;
    bool wide;          /* 32-bit FAT entries */
    uint64_t fat;       /* offset of the FAT */
    uint64_t data;      /* offset of cluster 1 */
} FatxGeo;

static bool fatx_geo(BlockBackend *blk, FatxGeo *g)
{
    uint8_t hdr[16];
    if (blk_pread(blk, C_OFFSET, sizeof(hdr), hdr, 0) < 0 ||
        memcmp(hdr, "FATX", 4)) {
        return false;
    }
    g->cluster_size = ldl_le_p(hdr + 8) * 512;
    if (g->cluster_size < 512 || g->cluster_size > 128 * 1024) {
        return false;
    }
    g->clusters = C_SIZE / g->cluster_size;
    g->wide = g->clusters >= 0xfff0;
    g->fat = C_OFFSET + 0x1000;
    g->data = g->fat +
              (((uint64_t)g->clusters * (g->wide ? 4 : 2) + 0xfff) & ~0xfffULL);
    return true;
}

static uint64_t cluster_offset(const FatxGeo *g, uint32_t cluster)
{
    return g->data + (uint64_t)(cluster - 1) * g->cluster_size;
}

static bool fat_get(BlockBackend *blk, const FatxGeo *g, uint32_t cluster,
                    uint32_t *value)
{
    uint8_t b[4];
    if (blk_pread(blk, g->fat + (uint64_t)cluster * (g->wide ? 4 : 2),
                  g->wide ? 4 : 2, b, 0) < 0) {
        return false;
    }
    *value = g->wide ? ldl_le_p(b) : lduw_le_p(b);
    return true;
}

/* Marks a cluster as the last one of its chain */
static bool fat_set_end(BlockBackend *blk, const FatxGeo *g, uint32_t cluster)
{
    uint8_t b[4];
    if (g->wide) {
        stl_le_p(b, 0xffffffff);
    } else {
        stw_le_p(b, 0xffff);
    }
    return blk_pwrite(blk, g->fat + (uint64_t)cluster * (g->wide ? 4 : 2),
                      g->wide ? 4 : 2, b, 0) >= 0;
}

/* n free clusters (FAT entry 0), lowest first */
static bool fat_find_free(BlockBackend *blk, const FatxGeo *g,
                          uint32_t *out, int n)
{
    const uint32_t width = g->wide ? 4 : 2;
    const uint32_t per_chunk = 0x4000;
    uint8_t *buf = g_malloc(per_chunk * width);
    int found = 0;
    for (uint64_t first = 0; first <= g->clusters && found < n;
         first += per_chunk) {
        uint32_t count = MIN(per_chunk, g->clusters + 1 - first);
        if (blk_pread(blk, g->fat + first * width, count * width, buf, 0) < 0) {
            break;
        }
        for (uint32_t i = 0; i < count && found < n; i++) {
            uint32_t c = first + i;
            uint32_t v = g->wide ? ldl_le_p(buf + i * 4) :
                                   lduw_le_p(buf + i * 2);
            /* Cluster 1 is the root directory */
            if (c >= 2 && v == 0) {
                out[found++] = c;
            }
        }
    }
    g_free(buf);
    return found == n;
}

/*
 * In the directory starting at cluster dir: the entry named name
 * (*found_at, its 64 bytes in found), and the first slot a new entry can
 * take (*free_at: a deleted entry or the end of the directory; 0 when its
 * clusters are full). False if the disk could not be read.
 */
static bool dir_scan(BlockBackend *blk, const FatxGeo *g, uint32_t dir,
                     const char *name, int64_t *found_at, uint8_t *found,
                     int64_t *free_at)
{
    uint8_t *buf = g_malloc(g->cluster_size);
    bool ok = true, done = false;
    *found_at = 0;
    *free_at = 0;
    for (int hops = 0; dir && !done && hops < 256; hops++) {
        uint64_t at = cluster_offset(g, dir);
        if (blk_pread(blk, at, g->cluster_size, buf, 0) < 0) {
            ok = false;
            break;
        }
        for (uint32_t i = 0; i < g->cluster_size; i += ENTRY_SIZE) {
            const uint8_t *e = buf + i;
            if (e[0] == 0x00 || e[0] == 0xff) {
                if (!*free_at) {
                    *free_at = at + i;
                }
                done = true;  /* the end of the directory */
                break;
            }
            if (e[0] == 0xe5) {
                if (!*free_at) {
                    *free_at = at + i;  /* deleted */
                }
                continue;
            }
            if (!*found_at && e[0] <= NAME_MAX_LEN &&
                e[0] == strlen(name) &&
                !g_ascii_strncasecmp((const char *)e + 2, name, e[0])) {
                *found_at = at + i;
                memcpy(found, e, ENTRY_SIZE);
            }
        }
        if (!done) {
            uint32_t next;
            if (!fat_get(blk, g, dir, &next)) {
                ok = false;
                break;
            }
            dir = next >= 2 && next <= g->clusters ? next : 0;
        }
    }
    g_free(buf);
    return ok;
}

/* A new 64-byte directory entry, dated now */
static void make_entry(uint8_t *e, const char *name, uint8_t attr,
                       uint32_t cluster, uint32_t size)
{
    GDateTime *now = g_date_time_new_now_local();
    int year = MAX(g_date_time_get_year(now) - 2000, 0);
    uint16_t date = (year << 9) | (g_date_time_get_month(now) << 5) |
                    g_date_time_get_day_of_month(now);
    uint16_t time = (g_date_time_get_hour(now) << 11) |
                    (g_date_time_get_minute(now) << 5) |
                    (g_date_time_get_second(now) / 2);
    g_date_time_unref(now);

    memset(e, 0xff, ENTRY_SIZE);
    e[0] = strlen(name);
    e[1] = attr;
    memcpy(e + 2, name, strlen(name));
    stl_le_p(e + 44, cluster);
    stl_le_p(e + 48, size);
    for (int k = 52; k < ENTRY_SIZE; k += 4) {  /* created, written, read */
        stw_le_p(e + k, time);
        stw_le_p(e + k + 2, date);
    }
}

/* Runs while the console is stopped for its first reset */
static void write_oxos_settings(void)
{
    BlockBackend *blk = hdd();
    FatxGeo g;
    const char *what = NULL, *fail = NULL;

    if (!blk) {
        fprintf(stderr, "oxos settings: no hard disk\n");
        return;
    }
    blk_drain(blk);
    if (!fatx_geo(blk, &g)) {
        fprintf(stderr, "oxos settings: C: is not FATX, left alone\n");
        return;
    }

    int64_t dir_at = 0, file_at = 0, root_free = 0, dir_free = 0;
    uint8_t dir_entry[ENTRY_SIZE], file_entry[ENTRY_SIZE];
    uint32_t dir = 0;
    if (!dir_scan(blk, &g, 1, OXOS_DIR_NAME, &dir_at, dir_entry,
                  &root_free)) {
        fail = "C: could not be read";
    } else if (dir_at) {
        dir = ldl_le_p(dir_entry + 44);
        if (!(dir_entry[1] & ATTR_DIRECTORY)) {
            fail = "C:\\OXOS is a file";
        } else if (dir < 2 || dir > g.clusters) {
            fail = "C:\\OXOS is damaged";
        } else if (!dir_scan(blk, &g, dir, OXOS_SETTINGS_NAME, &file_at,
                             file_entry, &dir_free)) {
            fail = "C:\\OXOS could not be read";
        } else if (file_at) {
            fprintf(stderr, "oxos settings: C:\\OXOS\\" OXOS_SETTINGS_NAME
                            " is there, left alone\n");
            return;
        } else if (!dir_free) {
            fail = "C:\\OXOS is full";
        }
    } else if (!root_free) {
        fail = "the root of C: is full";
    }

    size_t len = sizeof(oxos_settings_text) - 1;
    uint32_t free_cl[2];
    if (!fail && len > g.cluster_size) {
        fail = "the file is bigger than a cluster";
    }
    if (!fail && !fat_find_free(blk, &g, free_cl, dir ? 1 : 2)) {
        fail = "C: is full";
    }
    if (!fail) {
        /* The file's data first, the entries that point to it last */
        uint32_t file_cl = free_cl[0];
        uint8_t *buf = g_malloc(g.cluster_size);
        memset(buf, 0, g.cluster_size);
        memcpy(buf, oxos_settings_text, len);
        bool ok = blk_pwrite(blk, cluster_offset(&g, file_cl), g.cluster_size,
                             buf, 0) >= 0 &&
                  fat_set_end(blk, &g, file_cl);
        make_entry(file_entry, OXOS_SETTINGS_NAME, 0, file_cl, len);
        if (ok && !dir) {
            /* A new C:\OXOS holding just the file */
            dir = free_cl[1];
            memset(buf, 0xff, g.cluster_size);
            memcpy(buf, file_entry, ENTRY_SIZE);
            make_entry(dir_entry, OXOS_DIR_NAME, ATTR_DIRECTORY, dir, 0);
            ok = blk_pwrite(blk, cluster_offset(&g, dir), g.cluster_size, buf,
                            0) >= 0 &&
                 fat_set_end(blk, &g, dir) &&
                 blk_pwrite(blk, root_free, ENTRY_SIZE, dir_entry, 0) >= 0;
            what = "C:\\OXOS\\" OXOS_SETTINGS_NAME " (and the folder)";
        } else if (ok) {
            ok = blk_pwrite(blk, dir_free, ENTRY_SIZE, file_entry, 0) >= 0;
            what = "C:\\OXOS\\" OXOS_SETTINGS_NAME;
        }
        g_free(buf);
        if (!ok || blk_flush(blk) < 0) {
            fail = "writing C: failed";
        }
    }
    if (fail) {
        fprintf(stderr, "oxos settings: not written (%s)\n", fail);
    } else {
        fprintf(stderr, "oxos settings: wrote %s, %zu bytes\n", what, len);
    }
}

void xemu_dashinstall_oxos_settings(const char *flashrom_path)
{
    char *base = g_path_get_basename(flashrom_path ? flashrom_path : "");
    int v[3];
    bool oxos = sscanf(base, "oxos-v%d.%d.%d", &v[0], &v[1], &v[2]) == 3;
    g_free(base);
    if (!oxos || (v[0] == 0 && (v[1] < 4 || (v[1] == 4 && v[2] < 13)))) {
        return;  /* that BIOS finds the dashboard without the file */
    }
    xemu_dashinstall_resets();  /* hooks the first reset */
    oxos_settings_pending = true;
}
