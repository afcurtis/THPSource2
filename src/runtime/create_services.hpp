#pragma once

namespace opentony::runtime::create {

// Names of the outside services the create-a-skater code reaches (RetailServices). A port reports each
// reach as a named call, in order; a replay scripts the values a service returns.
constexpr const char* kAlloc = "alloc";            // FileIO_AllocLoadedBuffer(size, 1, 1, 0) -> address (scripted)
constexpr const char* kFileOpen = "file_open";     // FileIO_OpenLoad(name, 1) -> address of the file image (scripted)
constexpr const char* kMemDelete = "mem_delete";   // Mem_Delete(block)
constexpr const char* kPrintf = "printf_log";      // printf_log(format, ...): the format's address is reported
constexpr const char* kRnd = "rnd";                // Rnd(limit) -> value (scripted)
constexpr const char* kTrickInit = "trick_init";   // TrickConfig_InitializeCharacter(character, trick manager)

}  // namespace opentony::runtime::create
