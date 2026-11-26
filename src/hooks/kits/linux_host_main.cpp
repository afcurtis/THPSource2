#include "linux_boot_host.hpp"
#include "linux_guest_memory.hpp"
#include "retail_memory.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>

int main(int argc, char** argv) {
    const bool initialize = argc == 3 && std::strcmp(argv[1], "--check-initializers") == 0;
    const bool boot = argc == 4 && std::strcmp(argv[1], "--check-platform-startup") == 0;
    const bool try_boot = argc == 4 && std::strcmp(argv[1], "--try-boot") == 0;
    const bool static_data = argc == 3 && std::strcmp(argv[1], "--check-static-data") == 0;
    if (!initialize && !boot && !try_boot && !static_data) {
        std::fprintf(stderr, "usage: opentony_linux_host < --check-static-data | --check-initializers > <thawk2_static.img>\n"
                             "       opentony_linux_host < --check-platform-startup | --try-boot > <thawk2_static.img> <game-directory>\n");
        return 64;
    }

    try {
        std::ifstream stream(argv[2], std::ios::binary);
        if (!stream) {
            throw std::runtime_error("cannot open static-data image");
        }
        const std::vector<std::uint8_t> image((std::istreambuf_iterator<char>(stream)), {});
        opentony::kitdll::LinuxGuestMemory arena;
        const auto copied = arena.load_static_data(image);

        // Exercise the same guest addresses and heap allocator the ported Kits use.
        opentony::runtime::RetailMemory memory;
        memory.make_live(nullptr, false, arena.base());
        const auto address = arena.heap().allocate(sizeof(std::uint32_t));
        if (address == 0) {
            throw std::runtime_error("guest heap could not allocate a startup block");
        }
        constexpr std::uint32_t marker = 0x12345678;
        memory.set32(address, marker);
        const auto actual = memory.u32(address);
        arena.heap().release(address);
        if (actual != marker || memory.u32(opentony::kitdll::arena::kImageBase + 0x1000) != 0) {
            throw std::runtime_error("guest memory startup check failed");
        }

        std::printf("Linux host: loaded %zu static-data bytes; guest heap ready\n", copied);
        if (boot || try_boot) {
            opentony::kitdll::run_linux_initializers(arena, argv[3], try_boot);
        } else if (initialize) {
            opentony::kitdll::run_linux_initializers(arena);
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Linux host: %s: %s\n", argv[2], error.what());
        return 1;
    }
}
