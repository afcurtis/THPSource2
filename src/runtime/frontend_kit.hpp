#pragma once

// The front-end text and window-base natives as one Kit object (Phase 5.1 batch e): the font tools, the message strip, the window base and the text effects (the members of the
// replay test's context: `fonts.`, `mess.`, `window.`, `effects.` in a Kit hook row).
#include "frontend_effects.hpp"
#include "frontend_mess.hpp"
#include "frontend_text.hpp"
#include "frontend_window.hpp"
#include "retail_memory.hpp"
#include "retail_services.hpp"

namespace opentony::runtime::frontendkit {

struct FrontendKit {
    FrontendKit(RetailMemory& memory, RetailServices& services)
        : fonts(memory, services), mess(memory, services, fonts), window(memory, services, fonts), effects(memory, services, fonts) {}
    FrontendKit(const FrontendKit&) = delete;
    FrontendKit& operator=(const FrontendKit&) = delete;

    frontend::FontTools fonts;
    frontend::Mess mess;
    frontend::WindowBase window;
    frontend::TextEffects effects;
};

}  // namespace opentony::runtime::frontendkit
