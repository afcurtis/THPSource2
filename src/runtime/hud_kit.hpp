#pragma once

// The HUD panel natives as a Kit object (Phase 5.1 batch e): `panel.` in a Kit hook row.
#include "hud_panel.hpp"
#include "retail_memory.hpp"
#include "retail_services.hpp"

namespace opentony::runtime::hudkit {

struct HudKit {
    HudKit(RetailMemory& memory, RetailServices& services) : panel(memory, services) {}
    HudKit(const HudKit&) = delete;
    HudKit& operator=(const HudKit&) = delete;

    hud::Panel panel;
};

}  // namespace opentony::runtime::hudkit
