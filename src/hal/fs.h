#ifndef OPENTONY_HAL_FS_H
#define OPENTONY_HAL_FS_H

/* File-system service (docs/HAL.md, "File system").
 *
 * Retail file I/O never leaves the game's static C runtime (fopen/_open on
 * paths such as "data\\skater.bmp"); only start-up path discovery (PCinit,
 * CD_DriveScan), directory listing (WINMAIN_FindFiles) and the settings file
 * (WINMAIN_LoadSetting/SaveSetting, GetPrivateProfileString) call Win32. The
 * hook DLL therefore keeps retail file I/O and fs is not a [hal] service; this
 * interface is what the standalone build implements.
 *
 * Paths are UTF-8, relative to a root, and use either separator; lookups are
 * case-insensitive even on case-sensitive file systems, because the game
 * spells the same file several ways. */

#include "hal.h"

#ifdef __cplusplus
extern "C" {
#endif

enum OtHalFsRoot {
    OT_HAL_FS_INSTALL = 0, /* the installed game files (retail: the exe's directory) */
    OT_HAL_FS_MEDIA = 1,   /* files retail reads from the CD (movies, music); may equal INSTALL */
    OT_HAL_FS_SAVE = 2,    /* writable: saves, screenshots, logs */
    OT_HAL_FS_CONFIG = 3,  /* writable: settings */
    OT_HAL_FS_ROOT_COUNT = 4
};

enum OtHalFileMode {
    OT_HAL_FILE_READ = 1,
    OT_HAL_FILE_WRITE = 2, /* create or truncate */
    OT_HAL_FILE_APPEND = 3
};

enum OtHalSeek {
    OT_HAL_SEEK_SET = 0,
    OT_HAL_SEEK_CUR = 1,
    OT_HAL_SEEK_END = 2
};

enum OtHalFsEntryFlags {
    OT_HAL_FS_DIRECTORY = 1u << 0
};

typedef struct OtHalFsEntry {
    char name[260]; /* UTF-8 file name, no directory */
    uint32_t flags; /* OtHalFsEntryFlags */
    uint64_t size;
} OtHalFsEntry;

/* Called once per match; return 0 to stop the listing. */
typedef int(OT_HAL_CALL *OtHalFsVisit)(void *user, const OtHalFsEntry *entry);

typedef struct OtHalFsBackend {
    const char *name;
    /* The real path of `relative` under `root`, matched case-insensitively
     * one component at a time; for a file that does not exist yet the last
     * component is kept as given. Returns the length written (without the
     * terminator), or 0 when it does not fit or a directory is missing. */
    uint32_t(OT_HAL_CALL *resolve)(uint32_t root, const char *relative, char *out, uint32_t capacity);

    OtHalHandle(OT_HAL_CALL *open)(uint32_t root, const char *relative, uint32_t mode);
    uint32_t(OT_HAL_CALL *read)(OtHalHandle file, void *data, uint32_t size);
    uint32_t(OT_HAL_CALL *write)(OtHalHandle file, const void *data, uint32_t size);
    int64_t(OT_HAL_CALL *seek)(OtHalHandle file, int64_t offset, uint32_t whence); /* new position or -1 */
    int64_t(OT_HAL_CALL *size)(OtHalHandle file);
    void(OT_HAL_CALL *close)(OtHalHandle file);

    /* Lists `directory` (relative to `root`) entries matching `pattern`
     * ("*" and "?" wildcards, case-insensitive). Returns the number visited. */
    uint32_t(OT_HAL_CALL *list)(uint32_t root, const char *directory, const char *pattern, OtHalFsVisit visit, void *user);

    /* Settings (retail: a [section] key=value INI file). load returns the
     * length copied, or copies `fallback` when the key is missing. */
    uint32_t(OT_HAL_CALL *load_setting)(const char *section, const char *key, const char *fallback, char *out, uint32_t capacity);
    int(OT_HAL_CALL *save_setting)(const char *section, const char *key, const char *value); /* 1 on success */
} OtHalFsBackend;

#ifdef __cplusplus
}
#endif

#endif /* OPENTONY_HAL_FS_H */
