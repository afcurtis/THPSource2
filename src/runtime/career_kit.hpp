#pragma once

// The career natives as one Kit object (Phase 5.1 batch e): the career save state and the competition state share the retail memory and services (and the career reaches the skater
// manager), so a Kit hook reaches them as `career.` and `comp.` (the members of the replay test's context).
#include "career.hpp"
#include "competition.hpp"
#include "retail_memory.hpp"
#include "retail_services.hpp"
#include "skater_manager.hpp"

namespace opentony::runtime::careerkit {

struct CareerKit {
    CareerKit(RetailMemory& memory, RetailServices& services) : sk(memory, services), career(memory, services, sk), comp(memory, services) {}
    CareerKit(const CareerKit&) = delete;
    CareerKit& operator=(const CareerKit&) = delete;

    skater::SkaterManager sk;
    career::Career career;
    competition::Competition comp;
};

}  // namespace opentony::runtime::careerkit
