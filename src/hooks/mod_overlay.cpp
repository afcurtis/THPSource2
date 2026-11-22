#include "mod_overlay.h"

#include "hook_runtime.h"

#include <stdlib.h>
#include <string.h>

/* Mods are folders with a mod.ini; their Data folder mirrors the game's data
 * directory. Where they are looked for, in this order of precedence:
 *   OPENTONY_MODS     ';'-separated folders (a mod, or a folder of mods); "none" turns mods off
 *   opentony.ini      [mods] path = same list
 *   default           <game directory>\Mods
 * Within a folder of mods they load in alphabetical order of their names, and a
 * file in a later mod replaces the same file of an earlier one. */

#ifndef INVALID_FILE_ATTRIBUTES /* newer than the VC6 SDK */
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#endif

#define OT_MODS_MAX 64u
#define OT_MOD_DEPTH_MAX 12u

static OtOverlayIndex g_index;
static int g_trace = 0;
static unsigned g_mods = 0;
static char g_loaded[OT_MODS_MAX][MAX_PATH];

static int directory_exists(const char *path) {
    const DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static int file_exists(const char *path) {
    const DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static void join(char *out, unsigned size, const char *directory, const char *name) {
    unsigned length = (unsigned)lstrlenA(directory);
    if (length + 1 >= size) {
        out[0] = 0;
        return;
    }
    memcpy(out, directory, length);
    if (length != 0 && out[length - 1] != '\\' && out[length - 1] != '/') {
        out[length++] = '\\';
    }
    if (length + (unsigned)lstrlenA(name) + 1 > size) {
        out[0] = 0;
        return;
    }
    lstrcpyA(out + length, name);
}

/* Indexes every file below `directory`; `relative` is the path below the mod's Data folder. */
static unsigned scan_directory(const char *directory, const char *relative, unsigned mod, unsigned depth) {
    char pattern[MAX_PATH];
    WIN32_FIND_DATAA found;
    HANDLE handle;
    unsigned files = 0;

    if (depth > OT_MOD_DEPTH_MAX) {
        return 0;
    }
    join(pattern, sizeof(pattern), directory, "*");
    handle = FindFirstFileA(pattern, &found);
    if (handle == INVALID_HANDLE_VALUE) {
        return 0;
    }
    do {
        char path[MAX_PATH];
        char below[MAX_PATH];
        if (lstrcmpA(found.cFileName, ".") == 0 || lstrcmpA(found.cFileName, "..") == 0) {
            continue;
        }
        join(path, sizeof(path), directory, found.cFileName);
        join(below, sizeof(below), relative, found.cFileName);
        if (path[0] == 0 || below[0] == 0) {
            ot_log("mods: path too long below %s: %s", directory, found.cFileName);
            continue;
        }
        if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            files += scan_directory(path, below, mod, depth + 1u);
        } else {
            char key[OT_OVERLAY_KEY_MAX];
            if (ot_overlay_make_key(below, key, sizeof(key)) == 0) {
                ot_log("mods: ignoring %s (not a usable game path)", path);
                continue;
            }
            if (ot_overlay_add(&g_index, key, path, mod) >= 0) {
                ++files;
            }
        }
    } while (FindNextFileA(handle, &found));
    FindClose(handle);
    return files;
}

static int already_loaded(const char *folder) {
    unsigned i;
    for (i = 0; i < g_mods; ++i) {
        if (lstrcmpiA(g_loaded[i], folder) == 0) {
            return 1;
        }
    }
    return 0;
}

static void load_mod(const char *folder) {
    char ini[MAX_PATH];
    char data[MAX_PATH];
    char name[128];
    char value[16];
    unsigned files;
    unsigned mod;

    if (already_loaded(folder)) {
        return;
    }
    if (g_mods >= OT_MODS_MAX) {
        ot_log("mods: more than %u mods; ignoring %s", OT_MODS_MAX, folder);
        return;
    }
    join(ini, sizeof(ini), folder, "mod.ini");
    GetPrivateProfileStringA("Mod", "Enabled", "1", value, sizeof(value), ini);
    if (value[0] == '0') {
        ot_log("mods: %s is disabled (Enabled = 0)", folder);
        return;
    }
    GetPrivateProfileStringA("Mod", "Name", "", name, sizeof(name), ini);
    if (name[0] == 0) {
        const char *slash = strrchr(folder, '\\');
        lstrcpynA(name, slash != 0 ? slash + 1 : folder, sizeof(name));
    }
    mod = g_mods++;
    lstrcpynA(g_loaded[mod], folder, MAX_PATH);
    join(data, sizeof(data), folder, "Data");
    files = directory_exists(data) ? scan_directory(data, "", mod, 0) : 0;
    ot_log("mods: loaded '%s' (%s): %u files", name, folder, files);
}

static int compare_names(const void *a, const void *b) {
    return lstrcmpiA((const char *)a, (const char *)b);
}

/* `root` is a mod (it has a mod.ini) or a folder of mods. */
static void load_root(const char *root) {
    char ini[MAX_PATH];
    char pattern[MAX_PATH];
    WIN32_FIND_DATAA found;
    HANDLE handle;
    static char names[OT_MODS_MAX][MAX_PATH];
    unsigned count = 0;
    unsigned i;

    if (!directory_exists(root)) {
        return;
    }
    join(ini, sizeof(ini), root, "mod.ini");
    if (file_exists(ini)) {
        load_mod(root);
        return;
    }
    join(pattern, sizeof(pattern), root, "*");
    handle = FindFirstFileA(pattern, &found);
    if (handle == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 || found.cFileName[0] == '.') {
            continue;
        }
        if (lstrcmpiA(found.cFileName, "Music") == 0) {
            continue;   /* Mods\Music is the custom soundtrack's folder (standalone_music.cpp), not a mod */
        }
        if (count < OT_MODS_MAX) {
            lstrcpynA(names[count++], found.cFileName, MAX_PATH);
        }
    } while (FindNextFileA(handle, &found));
    FindClose(handle);
    qsort(names, count, MAX_PATH, compare_names);
    for (i = 0; i < count; ++i) {
        char folder[MAX_PATH];
        join(folder, sizeof(folder), root, names[i]);
        join(ini, sizeof(ini), folder, "mod.ini");
        if (file_exists(ini)) {
            load_mod(folder);
        } else {
            ot_log("mods: %s has no mod.ini; skipped", folder);
        }
    }
}

static void load_list(const char *list) {
    char root[MAX_PATH];
    unsigned length = 0;
    for (;; ++list) {
        if (*list == 0 || *list == ';') {
            root[length] = 0;
            if (length != 0) {
                load_root(root);
            }
            length = 0;
            if (*list == 0) {
                break;
            }
        } else if (length + 1 < sizeof(root)) {
            root[length++] = *list;
        }
    }
}

unsigned ot_overlay_configure(const char *dll_dir, const char *ini) {
    char list[1024];
    char value[16];
    DWORD length;

    length = GetEnvironmentVariableA("OPENTONY_MODS", list, sizeof(list));
    if (length != 0 && length < sizeof(list)) {
        if (lstrcmpiA(list, "none") == 0) {
            ot_log("mods: OPENTONY_MODS=none");
            return 0;
        }
        ot_log("mods: OPENTONY_MODS=%s", list);
    } else if (ini != 0 && GetPrivateProfileStringA("mods", "path", "", list, sizeof(list), ini) != 0) {
        ot_log("mods: [mods] path=%s", list);
    } else {
        join(list, sizeof(list), dll_dir, "Mods");
    }
    if (GetEnvironmentVariableA("OPENTONY_MOD_TRACE", value, sizeof(value)) != 0 && value[0] != 0) {
        g_trace = (lstrcmpiA(value, "all") == 0 || value[0] == '2') ? 2 : (value[0] == '0' ? 0 : 1);
    } else if (ini != 0) {
        GetPrivateProfileStringA("mods", "trace", "0", value, sizeof(value), ini);
        g_trace = (lstrcmpiA(value, "all") == 0 || value[0] == '2') ? 2 : (value[0] == '1' ? 1 : 0);
    }
    load_list(list);
    ot_log("mods: %u mods, %u files, trace %d", g_mods, g_index.count, g_trace);
    return g_index.count;
}

const OtOverlayEntry *ot_overlay_lookup(const char *request, char *key, unsigned key_size) {
    if (g_index.count == 0 || ot_overlay_make_key(request, key, key_size) == 0) {
        key[0] = 0;
        return 0;
    }
    return ot_overlay_find(&g_index, key);
}

int ot_overlay_trace(void) {
    return g_trace;
}

unsigned ot_overlay_file_count(void) {
    return g_index.count;
}
