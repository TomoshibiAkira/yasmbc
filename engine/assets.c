/* Load generated files under assets/. The playable binary never opens an iNES ROM. */
#include "assets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#elif defined(__DJGPP__)
#include <crt0.h>
#include <unistd.h>
#else
#include <unistd.h>
#endif

#include "audio-data.h"
#include "audio.h"
#include "enemy/enemy-data.h"
#include "enemy/enemy.h"
#include "level/level.h"
#include "player/player.h"
#include "screen/routine/colors.h"
#include "screen/routine/title.h"
#include "screen/routine/hud.h"
#include "screen/routine/intermediate.h"
#include "score.h"
#include "sprite-offsets.h"
#include "spr-object.h"
#include "title-menu.h"
#include "game-mode/core.h"
#include "misc.h"
#include "collision.h"
#include "scroll.h"

#include <stdint.h>

#define ASSETS_MAX 512
#define ASSETS_PATH_MAX 512
#define PACK_MAGIC "SMBPAK1"
#define PACK_NAME_LEN 96

typedef struct {
    char key[96];
    uint8_t *data;
    size_t len;
} AssetEntry;

typedef struct {
    char name[PACK_NAME_LEN];
    uint32_t offset;
    uint32_t size;
} __attribute__((packed)) PackEntry;

static AssetEntry s_entries[ASSETS_MAX];
static int s_count;
static char s_root[ASSETS_PATH_MAX];
static char s_argv0[ASSETS_PATH_MAX];
static FILE *s_pack;
static PackEntry s_pack_dir[ASSETS_MAX];
static int s_pack_count;

const uint8_t *smb_music_rom;
unsigned int smb_music_rom_len;

void Assets_SetArgv0(const char *argv0)
{
    if (!argv0) {
        s_argv0[0] = 0;
        return;
    }
    snprintf(s_argv0, sizeof(s_argv0), "%s", argv0);
}

static int path_dirname(char *dst, size_t dst_size, const char *path) {
    const char *slash = strrchr(path, '/');
    const char *bslash = strrchr(path, '\\');
    if (bslash && (!slash || bslash > slash))
        slash = bslash;
    if (!slash || slash == path)
        return -1;
    {
        size_t n = (size_t)(slash - path);
        if (n + 1 >= dst_size)
            return -1;
        memcpy(dst, path, n);
        dst[n] = 0;
    }
    return 0;
}

#if !defined(DOS) && !defined(__DJGPP__)
static int try_root(const char *root) {
    char path[ASSETS_PATH_MAX];
    FILE *f;

    snprintf(path, sizeof(path), "%s/tiles.chr", root);
    f = fopen(path, "rb");
    if (!f)
        return -1;
    fclose(f);
    snprintf(s_root, sizeof(s_root), "%s", root);
    return 0;
}
#endif

static FILE *open_asset(const char *relative_path) {
    char path[ASSETS_PATH_MAX * 2];

    snprintf(path, sizeof(path), "%s/%s", s_root, relative_path);
    return fopen(path, "rb");
}

static int cache_bytes(const char *relative_path, uint8_t *buf, size_t size,
                       size_t *length)
{
    strncpy(s_entries[s_count].key, relative_path, sizeof(s_entries[s_count].key) - 1);
    s_entries[s_count].key[sizeof(s_entries[s_count].key) - 1] = 0;
    s_entries[s_count].data = buf;
    s_entries[s_count].len = size;
    if (length)
        *length = size;
    s_count++;
    return 0;
}

static const uint8_t *load_from_pack(const char *relative_path, size_t *length)
{
    int i;
    uint8_t *buf;
    size_t nread;

    for (i = 0; i < s_pack_count; i++) {
        if (strcmp(s_pack_dir[i].name, relative_path) == 0)
            break;
    }
    if (i == s_pack_count) {
        fprintf(stderr, "Assets_Load: missing %s in pack\n", relative_path);
        return NULL;
    }
    if (fseek(s_pack, (long)s_pack_dir[i].offset, SEEK_SET) != 0)
        return NULL;
    buf = (uint8_t *)malloc((size_t)s_pack_dir[i].size + 1);
    if (!buf)
        return NULL;
    nread = fread(buf, 1, s_pack_dir[i].size, s_pack);
    if (nread != s_pack_dir[i].size) {
        free(buf);
        return NULL;
    }
    buf[s_pack_dir[i].size] = 0;
    cache_bytes(relative_path, buf, s_pack_dir[i].size, length);
    return buf;
}

const uint8_t *Assets_Load(const char *relative_path, size_t *length) {
    int i;
    FILE *f;
    uint8_t *buf;
    long size;
    size_t nread;

    if (length)
        *length = 0;
    for (i = 0; i < s_count; i++) {
        if (strcmp(s_entries[i].key, relative_path) == 0) {
            if (length)
                *length = s_entries[i].len;
            return s_entries[i].data;
        }
    }
    if (s_count >= ASSETS_MAX) {
        fprintf(stderr, "Assets_Load: cache full (%s)\n", relative_path);
        return NULL;
    }

    if (s_pack)
        return load_from_pack(relative_path, length);

    f = open_asset(relative_path);
    if (!f) {
        fprintf(stderr, "Assets_Load: missing %s/%s\n", s_root, relative_path);
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    size = ftell(f);
    if (size < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    buf = (uint8_t *)malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    nread = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (nread != (size_t)size) {
        free(buf);
        return NULL;
    }
    buf[size] = 0;
    cache_bytes(relative_path, buf, (size_t)size, length);
    return buf;
}

int Assets_Copy(const char *relative_path, void *dst, size_t expected_len) {
    size_t n = 0;
    const uint8_t *src = Assets_Load(relative_path, &n);
    if (!src || n != expected_len)
        return -1;
    memcpy(dst, src, expected_len);
    return 0;
}

static int try_pack(const char *path)
{
    FILE *f;
    char magic[8];
    uint32_t count;
    int i;

    f = fopen(path, "rb");
    if (!f)
        return -1;
    if (fread(magic, 1, 8, f) != 8 || memcmp(magic, PACK_MAGIC, 7) != 0) {
        fprintf(stderr, "try_pack: '%s' is not a valid pack (bad magic)\n", path);
        fclose(f);
        return -1;
    }
    if (fread(&count, 4, 1, f) != 1 || count == 0 || count > ASSETS_MAX) {
        fprintf(stderr, "try_pack: '%s' has %u entries (ASSETS_MAX=%d)\n",
                path, (unsigned)count, ASSETS_MAX);
        fclose(f);
        return -1;
    }
    for (i = 0; i < (int)count; i++) {
        if (fread(&s_pack_dir[i], sizeof(PackEntry), 1, f) != 1) {
            fprintf(stderr, "try_pack: '%s' failed reading entry %d\n", path, i);
            fclose(f);
            return -1;
        }
        s_pack_dir[i].name[PACK_NAME_LEN - 1] = 0;
    }
    s_pack = f;
    s_pack_count = (int)count;
    snprintf(s_root, sizeof(s_root), "%s", path);
    printf("Assets: opened pack '%s' (%d entries)\n", path, s_pack_count);
    return 0;
}

static int choose_root(void) {
    char dir[ASSETS_PATH_MAX];
    char beside[ASSETS_PATH_MAX * 2];

#if defined(DOS) || defined(__DJGPP__)
    if (try_pack("ASSETS.DAT") == 0 || try_pack("assets.dat") == 0)
        return 0;
    if (s_argv0[0] && path_dirname(dir, sizeof(dir), s_argv0) == 0) {
        snprintf(beside, sizeof(beside), "%s/ASSETS.DAT", dir);
        if (try_pack(beside) == 0)
            return 0;
        snprintf(beside, sizeof(beside), "%s/assets.dat", dir);
        if (try_pack(beside) == 0)
            return 0;
        snprintf(beside, sizeof(beside), "%s\\ASSETS.DAT", dir);
        if (try_pack(beside) == 0)
            return 0;
        snprintf(beside, sizeof(beside), "%s\\assets.dat", dir);
        if (try_pack(beside) == 0)
            return 0;
    }
#if defined(__DJGPP__)
    if (__dos_argv0 && __dos_argv0[0] && path_dirname(dir, sizeof(dir), __dos_argv0) == 0) {
        snprintf(beside, sizeof(beside), "%s/ASSETS.DAT", dir);
        if (try_pack(beside) == 0)
            return 0;
        snprintf(beside, sizeof(beside), "%s/assets.dat", dir);
        if (try_pack(beside) == 0)
            return 0;
        snprintf(beside, sizeof(beside), "%s\\ASSETS.DAT", dir);
        if (try_pack(beside) == 0)
            return 0;
        snprintf(beside, sizeof(beside), "%s\\assets.dat", dir);
        if (try_pack(beside) == 0)
            return 0;
    }
#endif
    fprintf(stderr, "Assets_Init: ASSETS.DAT not found next to SMB2.EXE\n");
    return -1;
#else
    {
        char exe[ASSETS_PATH_MAX];
        static const char *cwd_roots[] = {"assets", "c-port/assets"};
        size_t i;

        for (i = 0; i < sizeof(cwd_roots) / sizeof(cwd_roots[0]); i++) {
            if (try_root(cwd_roots[i]) == 0)
                return 0;
        }

#ifdef _WIN32
        if (GetModuleFileNameA(NULL, exe, sizeof(exe)) != 0 &&
            path_dirname(dir, sizeof(dir), exe) == 0) {
            snprintf(beside, sizeof(beside), "%s/assets", dir);
            if (try_root(beside) == 0)
                return 0;
        }
#else
        {
            ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
            if (n > 0) {
                exe[n] = 0;
                if (path_dirname(dir, sizeof(dir), exe) == 0) {
                    snprintf(beside, sizeof(beside), "%s/assets", dir);
                    if (try_root(beside) == 0)
                        return 0;
                }
            }
        }
#endif
        if (try_pack("ASSETS.DAT") == 0 || try_pack("assets.dat") == 0)
            return 0;
        if (s_argv0[0] && path_dirname(dir, sizeof(dir), s_argv0) == 0) {
            snprintf(beside, sizeof(beside), "%s/ASSETS.DAT", dir);
            if (try_pack(beside) == 0)
                return 0;
            snprintf(beside, sizeof(beside), "%s/assets.dat", dir);
            if (try_pack(beside) == 0)
                return 0;
        }
        fprintf(stderr,
                "Assets_Init: extracted assets not found. Run: make extract ROM=path.nes\n");
        return -1;
    }
#endif
}

int Assets_Init(void) {
    size_t music_len = 0;

    if (choose_root() != 0)
        return -1;

    smb_music_rom = Assets_Load("audio/music_data.bin", &music_len);
    smb_music_rom_len = (unsigned int)music_len;
    if (!smb_music_rom || music_len != 1773) {
        fprintf(stderr, "Assets_Init: audio/music_data.bin must be 1773 bytes\n");
        return -1;
    }
    if (EnemyData_Load() != 0)
        return -1;
    if (Level_LoadRomTables() != 0)
        return -1;
    if (Player_LoadRomTables() != 0)
        return -1;
    if (Title_LoadRom() != 0)
        return -1;
    if (Colors_LoadRom() != 0)
        return -1;
    if (Score_LoadRomTables() != 0)
        return -1;
    if (SpriteOffsets_LoadRom() != 0)
        return -1;
    if (SprObject_LoadRom() != 0)
        return -1;
    if (TitleMenu_LoadRom() != 0)
        return -1;
    if (Hud_LoadRom() != 0)
        return -1;
    if (Intermediate_LoadRom() != 0)
        return -1;
    if (GameMode_LoadRom() != 0)
        return -1;
    if (Audio_LoadRomTables() != 0)
        return -1;
    if (Misc_LoadRom() != 0)
        return -1;
    if (Collision_LoadRom() != 0)
        return -1;
    if (Enemy_LoadRomTables() != 0)
        return -1;
    if (Scroll_LoadRom() != 0)
        return -1;
    return 0;
}
