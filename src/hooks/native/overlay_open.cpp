// FileIO_BackendOpen (0x004e75b0) with the loose-file overlay (Phase 5.2).
//
// Retail's mode-0 open resolves a path in ALL.PKR (a hit becomes a type-1 slot: a
// malloc'd memory image of the entry) and falls back to a CRT stream on a miss. The
// overlay answers first: when a mod has a file for the path, this adapter makes the
// same type-1 slot out of the mod file, so seek, read and close are retail's own.
// Everything else runs retail's code through the trampoline.
//
// The slot arrays and the allocator are the canonical build's (the hook row lists no
// other build); the layout is in re/evidence/functions/fileio-runtime.md, "The backend,
// exactly".

#include "hook_runtime.h"
#include "hook_types.h"
#include "mod_overlay.h"

#include <string.h>

#ifndef INVALID_FILE_SIZE /* newer than the VC6 SDK */
#define INVALID_FILE_SIZE ((DWORD)0xFFFFFFFFu)
#endif

namespace {

typedef int(__cdecl *OpenFunction)(const char *path, int mode);
typedef int(__cdecl *FindSlotFunction)(void);
typedef void *(__cdecl *MallocFunction)(unsigned size);
typedef void(__cdecl *FreeFunction)(void *block);

const UINT_PTR kFindFreeSlot = 0x004e78e0; // FileIO_FindFreeBackendSlot: lowest free of ten, or -1
const UINT_PTR kMalloc = 0x00501bb4;       // the executable's CRT malloc / free: close frees the buffer
const UINT_PTR kFree = 0x00501b6c;
unsigned char *const kSlotType = reinterpret_cast<unsigned char *>(0x006a6d10); // 0 free, 1 memory image, 2 CRT stream
unsigned long *const kSlotSize = reinterpret_cast<unsigned long *>(0x006a6d44);
unsigned long *const kSlotPosition = reinterpret_cast<unsigned long *>(0x006a716c);
unsigned char **const kSlotBuffer = reinterpret_cast<unsigned char **>(0x006a7194);

OpenFunction g_retail = 0;
LONG g_requests = 0;
LONG g_replaced = 0;

OpenFunction retail_open() {
    if (g_retail == 0) {
        unsigned index;
        for (index = 0; index < ot_hook_count(); ++index) {
            const OtHookState *hook = ot_hook_state(index);
            if (lstrcmpA(hook->spec->name, "FileIO_BackendOpen") == 0) {
                g_retail = reinterpret_cast<OpenFunction>(hook->trampoline);
                break;
            }
        }
    }
    return g_retail;
}

// Reads the whole mod file into a block of the executable's own heap. 0 on failure.
unsigned char *read_file(const char *path, unsigned long *size) {
    unsigned char *block = 0;
    DWORD read = 0;
    const HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        return 0;
    }
    const DWORD length = GetFileSize(file, 0);
    if (length != INVALID_FILE_SIZE) {
        block = static_cast<unsigned char *>(reinterpret_cast<MallocFunction>(kMalloc)(length != 0 ? length : 1u));
        if (block != 0 && !ReadFile(file, block, length, &read, 0)) {
            read = 0;
        }
        if (block != 0 && read != length) {
            reinterpret_cast<FreeFunction>(kFree)(block);
            block = 0;
        }
        *size = length;
    }
    CloseHandle(file);
    return block;
}

int __cdecl ot_FileIO_BackendOpen(const char *path, int mode) {
    const OpenFunction retail = retail_open();
    char key[OT_OVERLAY_KEY_MAX];
    const OtOverlayEntry *entry;

    if (mode != 0 || retail == 0) {
        return retail != 0 ? retail(path, mode) : -1;
    }
    InterlockedIncrement(&g_requests);
    entry = ot_overlay_lookup(path, key, sizeof(key));
    if (entry != 0) {
        const int slot = reinterpret_cast<FindSlotFunction>(kFindFreeSlot)();
        unsigned long size = 0;
        unsigned char *block;
        if (slot < 0) {
            return -1; // retail: a full table fails before anything is looked up
        }
        block = read_file(entry->path, &size);
        if (block != 0) {
            kSlotBuffer[slot] = block;
            kSlotPosition[slot] = 0;
            kSlotSize[slot] = size;
            kSlotType[slot] = 1;
            InterlockedIncrement(&g_replaced);
            if (ot_overlay_trace() != 0) {
                ot_log("mods: replaced %s <- %s (%lu bytes, slot %d)", key, entry->path, size, slot);
            }
            return slot;
        }
        ot_log("mods: cannot read %s for %s; the game's own file is used", entry->path, key);
    } else if (ot_overlay_trace() >= 2 && key[0] != 0) {
        ot_log("mods: open %s (not replaced)", key);
    }
    return retail(path, mode);
}

} // namespace

OT_REGISTER_NATIVE(FileIO_BackendOpen, ot_FileIO_BackendOpen)
