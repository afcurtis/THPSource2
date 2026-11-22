#pragma once

// The player spool natives as one Kit object (Phase 5.1 batch d): the manager (spooler.cpp), the skater spool, the PRE manager and the file loader, which share the
// retail memory and services but not an object, so a Kit hook reaches them as `spooler.`, `skater.`, `pre.` and `files.`.
#include "file_loader.hpp"
#include "player_spooler.hpp"
#include "pre_manager.hpp"
#include "retail_memory.hpp"
#include "retail_services.hpp"
#include "skater_spool.hpp"

namespace opentony::runtime::pspoolkit {

struct PlayerSpoolKit {
    PlayerSpoolKit(RetailMemory& memory, RetailServices& services)
        : spooler(memory, services), skater(memory, services, spooler), pre(memory, services), files(memory, services) {}
    PlayerSpoolKit(const PlayerSpoolKit&) = delete;
    PlayerSpoolKit& operator=(const PlayerSpoolKit&) = delete;

    pspool::PlayerSpooler spooler;
    skaterspool::SkaterSpool skater;
    pre::PreManager pre;
    fileio::FileLoader files;
};

}  // namespace opentony::runtime::pspoolkit
