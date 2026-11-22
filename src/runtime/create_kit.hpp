#pragma once

// The create-a-skater data natives as one Kit object (Phase 5.1 batch e): colours, texture sets, custom skater records, parts, the spool, the palette and the stitcher share the retail
// memory and services (the members are the replay test's context members, so a Kit hook reaches them as `color.`, `textures.`, `customs.`, ...).
#include "create_color.hpp"
#include "create_palette.hpp"
#include "create_parts.hpp"
#include "create_records.hpp"
#include "create_spool.hpp"
#include "create_stitch.hpp"
#include "create_textures.hpp"
#include "retail_memory.hpp"
#include "retail_services.hpp"
#include "skater_manager.hpp"

namespace opentony::runtime::createkit {

struct CreateKit {
    CreateKit(RetailMemory& memory, RetailServices& services)
        : color(memory, services),
          textures(memory, services),
          skaters(memory, services),
          customs(memory, services, skaters, textures),
          parts(memory, services),
          spool(memory, services),
          palette(memory, services, color, textures),
          stitch(memory, services) {}
    CreateKit(const CreateKit&) = delete;
    CreateKit& operator=(const CreateKit&) = delete;

    create::CreateColor color;
    create::TextureSets textures;
    skater::SkaterManager skaters;
    create::CustomSkaters customs;
    create::CreateParts parts;
    create::CreateSpool spool;
    create::CreatePalette palette;
    create::CreateStitch stitch;
};

}  // namespace opentony::runtime::createkit
