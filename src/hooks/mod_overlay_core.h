#ifndef OPENTONY_HOOKS_MOD_OVERLAY_CORE_H
#define OPENTONY_HOOKS_MOD_OVERLAY_CORE_H

/* The pure half of the loose-file overlay (Phase 5.2): how a path the game
 * opens becomes a lookup key, and the table of mod files by key. No operating
 * system calls, no standard library beyond <stdlib.h> and <string.h>, so the
 * VC6-CRT hook DLL and the Linux unit test (mod_overlay_core_test.cpp) compile
 * the same code. The Win32 half (finding mods, reading files) is mod_overlay.cpp.
 *
 * Keys. The game opens `data\SKWARE_T.TRG`, `data\logos\x.bmp`, `data\sfx\y.wav`:
 * the base directory `data\` plus a sub directory plus a name. A key is that
 * path below `data`, lower case, with '/' separators: `skware_t.trg`,
 * `logos/x.bmp`. A mod's `Data` folder mirrors it (`<mod>\Data\logos\X.BMP`).
 * Only the exact path answers: the game asks for optional variants of a file in
 * other directories (`NEWBMP\s2bgd0.bmp` beside `s2bgd0.bmp`), and a file that
 * answered by name alone would stand in for those too. */

#include <stdlib.h>
#include <string.h>

#define OT_OVERLAY_KEY_MAX 160u
#define OT_OVERLAY_PATH_MAX 520u

typedef struct OtOverlayEntry {
    char key[OT_OVERLAY_KEY_MAX];
    char path[OT_OVERLAY_PATH_MAX]; /* the mod file on disk */
    unsigned mod;                   /* index of the mod that supplied it (later mods win) */
} OtOverlayEntry;

typedef struct OtOverlayIndex {
    OtOverlayEntry *entries;
    unsigned count;
    unsigned capacity;
} OtOverlayIndex;

static inline char ot_overlay_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

/* Normalises a path into a key. Returns the key length, or 0 when the path is
 * empty, too long, absolute, or climbs out of its directory with "..". A
 * leading "data/" (the game's base directory) is dropped; "./" is ignored. */
static inline unsigned ot_overlay_make_key(const char *path, char *key, unsigned size) {
    unsigned length = 0;
    unsigned segment = 0; /* start of the current segment in `key` */
    int base_dropped = 0;
    const char *p = path;
    if (path == 0 || size == 0) {
        return 0;
    }
    /* A drive letter or a leading separator is an absolute path: never a game-relative name. */
    if (p[0] != 0 && p[1] == ':') {
        return 0;
    }
    if (p[0] == '\\' || p[0] == '/') {
        return 0;
    }
    for (; *p != 0; ++p) {
        char c = *p == '\\' ? '/' : ot_overlay_lower(*p);
        if (c == '/') {
            const unsigned segment_length = length - segment;
            if (segment_length == 0) { /* "//": collapse */
                continue;
            }
            if (segment_length == 1 && key[segment] == '.') { /* "./" */
                length = segment;
                continue;
            }
            if (segment_length == 2 && key[segment] == '.' && key[segment + 1] == '.') {
                return 0;
            }
            if (!base_dropped && segment == 0 && segment_length == 4 && memcmp(key, "data", 4) == 0) { /* the base directory */
                base_dropped = 1;
                length = 0;
                segment = 0;
                continue;
            }
        }
        if (length + 1 >= size) {
            return 0;
        }
        key[length++] = c;
        if (c == '/') {
            segment = length;
        }
    }
    {
        const unsigned segment_length = length - segment;
        if (segment_length == 0) { /* empty, or ends in a separator */
            return 0;
        }
        if ((segment_length == 1 && key[segment] == '.') ||
            (segment_length == 2 && key[segment] == '.' && key[segment + 1] == '.')) {
            return 0;
        }
    }
    key[length] = 0;
    return length;
}

static inline void ot_overlay_free(OtOverlayIndex *index) {
    free(index->entries);
    index->entries = 0;
    index->count = 0;
    index->capacity = 0;
}

/* Adds (or replaces, when a later mod has the same key) one file. `key` must
 * come from ot_overlay_make_key. Returns 1 when the key is new, 0 when it
 * replaced an earlier mod's file, -1 on failure. */
static inline int ot_overlay_add(OtOverlayIndex *index, const char *key, const char *path, unsigned mod) {
    unsigned i;
    OtOverlayEntry *entry;
    if (strlen(key) >= OT_OVERLAY_KEY_MAX || strlen(path) >= OT_OVERLAY_PATH_MAX) {
        return -1;
    }
    for (i = 0; i < index->count; ++i) {
        if (strcmp(index->entries[i].key, key) == 0) {
            strcpy(index->entries[i].path, path);
            index->entries[i].mod = mod;
            return 0;
        }
    }
    if (index->count == index->capacity) {
        const unsigned capacity = index->capacity != 0 ? index->capacity * 2u : 256u;
        OtOverlayEntry *grown = (OtOverlayEntry *)realloc(index->entries, capacity * sizeof(OtOverlayEntry));
        if (grown == 0) {
            return -1;
        }
        index->entries = grown;
        index->capacity = capacity;
    }
    entry = &index->entries[index->count++];
    strcpy(entry->key, key);
    strcpy(entry->path, path);
    entry->mod = mod;
    return 1;
}

/* The mod file that answers a request key (the exact key), or 0. */
static inline const OtOverlayEntry *ot_overlay_find(const OtOverlayIndex *index, const char *key) {
    unsigned i;
    for (i = 0; i < index->count; ++i) {
        if (strcmp(index->entries[i].key, key) == 0) {
            return &index->entries[i];
        }
    }
    return 0;
}

#endif /* OPENTONY_HOOKS_MOD_OVERLAY_CORE_H */
