#pragma once

// The park generator as one Kit object (Phase 5.4a, continued): the generated-object tools and the generator that uses them (`gen.`, `lg.` in a native row,
// the members of the replay test's context).
#include "gen_objs.hpp"
#include "level_gen.hpp"
#include "retail_memory.hpp"
#include "retail_services.hpp"

namespace opentony::runtime::levelgenkit {

struct LevelGenKit {
    LevelGenKit(RetailMemory& memory, RetailServices& services) : gen(memory, services), lg(memory, services, gen) {}
    LevelGenKit(const LevelGenKit&) = delete;
    LevelGenKit& operator=(const LevelGenKit&) = delete;

    genobjs::GenObjs gen;
    levelgen::LevelGen lg;
};

}  // namespace opentony::runtime::levelgenkit
