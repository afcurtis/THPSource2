#pragma once

// The front-end screens as one Kit object (Phase 5.4a, continued): the window base, the menu toolkit and every screen built on them, the members of the
// replay tests' contexts (`options.`, `front.`, ... in a native row). Unlike the tests' contexts this one installs no dispatch lambdas and no loop seams:
// a virtual call of the screens reports `vcall_<slot target>` and the standalone resolver finds the target's native by address, which is the one dispatch
// policy a program with no retail code has (the tests' lambdas route the same calls to the same methods, by name instead of by address).
#include "career.hpp"
#include "career_select.hpp"
#include "create_color.hpp"
#include "create_palette.hpp"
#include "create_screens.hpp"
#include "create_spool.hpp"
#include "create_textures.hpp"
#include "credits.hpp"
#include "equip_select.hpp"
#include "frontend_text.hpp"
#include "frontend_window.hpp"
#include "front.hpp"
#include "goals.hpp"
#include "handicap.hpp"
#include "level_select.hpp"
#include "mainmenu.hpp"
#include "menu_toolkit.hpp"
#include "mmu_screens.hpp"
#include "multi_select.hpp"
#include "netmenu.hpp"
#include "options.hpp"
#include "player_select.hpp"
#include "retail_memory.hpp"
#include "retail_services.hpp"
#include "shell_helpers.hpp"
#include "skater_manager.hpp"
#include "skater_window_helpers.hpp"
#include "skater_windows.hpp"
#include "trick_screens.hpp"

namespace opentony::runtime::screenskit {

struct ScreensKit {
    ScreensKit(RetailMemory& memory, RetailServices& services)
        : fonts(memory, services), window(memory, services, fonts), menu(memory, services, fonts, window), skaters(memory, services),
          color(memory, services), spool(memory, services), textures(memory, services), palette(memory, services, color, textures),
          career_data(memory, services, skaters),
          options(memory, services, fonts, window, menu, skaters), front(memory, services, fonts, window, menu, skaters),
          shell_helpers(memory, services, fonts, window, menu, skaters), net_menu(memory, services, fonts, window, menu, skaters),
          main_menu(memory, services, fonts, window, menu), credits(memory, services, fonts, window, menu),
          equip(memory, services, fonts, window, menu, skaters), player_select(memory, services, fonts, window, menu, skaters),
          goals(memory, services, fonts, window, menu, skaters), career_select(memory, services, fonts, window, menu, skaters),
          handicap(memory, services, fonts, window, menu, skaters), multi_select(memory, services, fonts, window, menu, skaters),
          level_select(memory, services, fonts, window, menu, skaters), skater_helpers(memory, services, fonts, window, menu, color),
          skater_windows(memory, services, fonts, window, menu, skater_helpers, skaters, textures, palette, career_data),
          create_screens(memory, services, fonts, window, menu, skaters, spool, skater_helpers),
          trick_screens(memory, services, fonts, window, menu, skaters, skater_helpers, create_screens),
          mmu_screens(memory, services, fonts, window, menu, skaters, create_screens, spool) {}
    ScreensKit(const ScreensKit&) = delete;
    ScreensKit& operator=(const ScreensKit&) = delete;

    // the base every screen is built on
    frontend::FontTools fonts;
    frontend::WindowBase window;
    frontend::MenuToolkit menu;
    skater::SkaterManager skaters;
    // what the skater and create screens share
    create::CreateColor color;
    create::CreateSpool spool;
    create::TextureSets textures;
    create::CreatePalette palette;
    career::Career career_data;
    // the screens
    frontend::OptionsScreen options;
    frontend::FrontScreens front;
    frontend::ShellHelpers shell_helpers;
    frontend::NetMenu net_menu;
    frontend::MainMenu main_menu;
    frontend::Credits credits;
    frontend::EquipSelect equip;
    frontend::PlayerSelect player_select;
    frontend::GoalScreens goals;
    frontend::CareerSelect career_select;
    frontend::HandicapScreen handicap;
    frontend::MultiSelect multi_select;
    frontend::LevelSelect level_select;
    frontend::SkaterWindowHelpers skater_helpers;
    frontend::SkaterWindows skater_windows;
    frontend::CreateScreens create_screens;
    frontend::TrickScreens trick_screens;
    frontend::MmuScreens mmu_screens;
};

}  // namespace opentony::runtime::screenskit
