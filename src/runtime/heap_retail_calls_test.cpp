// Replays retail forced calls (Phase 4.14b) through the native heap (heap.cpp: mem.cpp's zones, free lists and wrappers):
//
//   opentony_heap_retail_calls_test FIXTURE.txt [FIXTURE.txt ...]
//
// A fixture (scripts/make_career_calls_fixture.py, from re/gdb/probes/frontend_calls.py with the group `mem` of mem_specs.py) holds the retail memory regions the ported code touches (the heap
// globals and the scratch slab the probe built two zones in) and, for every case, the state, the arguments and what the retail function did; see retail_calls_replay.hpp for the format.
//
// `Bang` (0x0046ee20) is not replayed: it prints through the PSX stub and then spins for ever, so a forced call never returns. Its one reachable effect, the print, is checked here directly.
#include "heap_kit.hpp"
#include "retail_calls_replay.hpp"

#include <memory>

using namespace opentony::runtime;
using namespace opentony::runtime::replay;
using namespace opentony::runtime::heap;

namespace {

struct Ctx {
    RetailMemory mem;
    RetailServices svc;
    HeapKit heap;
    explicit Ctx(RetailMemory memory) : mem(std::move(memory)), heap(mem, svc) {}
    Ctx(const Ctx&) = delete;
    Ctx& operator=(const Ctx&) = delete;
};

using HeapAdapter = Adapter<Ctx>;

// A(expr): the function returns expr. V(expr): it returns nothing the callers use. `a` are the stack arguments, `self` is `this`.
#define A(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; return U(expr); }
#define V(expr) [](Ctx& c, const Args& a, std::uint32_t self) -> std::uint32_t { (void)a; (void)c; (void)self; expr; return 0; }
#define G c.heap

const std::map<std::uint32_t, HeapAdapter>& adapters() {
    static const std::map<std::uint32_t, HeapAdapter> table = {
        {0x0046e7f0, {"AddToFreeList", 0, false, V(G.add_to_free_list(a[0], a[1]))}},
        {0x0046e8c0, {"Mem_Init", 0, false, V(G.init())}},
        {0x0046e9c0, {"Mem_NewX", 0xffffffff, false, A(G.new_x(a[0], a[1], a[2], a[3]))}},
        {0x0046ee40, {"Mem_NewTop", 0xffffffff, false, A(G.new_top(a[0]))}},
        {0x0046f000, {"Mem_DeleteX", 0, false, V(G.delete_x(a[0]))}},
        {0x0046f0e0, {"Mem_ShrinkX", 0, false, V(G.shrink_x(a[0], a[1]))}},
        {0x0046f250, {"Mem_GetBlockSizeX", 0xffffffff, false, A(G.block_size_x(a[0]))}},
        {0x0046f2c0, {"Mem_GetFree", 0xffffffff, false, A(G.free_bytes(static_cast<std::int8_t>(a[0] & 0xff)))}},
        {0x0046f2e0, {"Mem_Copy", 0xffffffff, false, A(G.copy(a[0], a[1], S(a[2])))}},
        {0x0046f3b0, {"Mem_CopyBytes", 0xffffffff, false, A(G.copy_bytes(a[0], a[1], S(a[2])))}},
        {0x0046f3e0, {"Mem_Swap", 0, false, V(G.swap(a[0], a[1], S(a[2])))}},
        {0x0046f460, {"Mem_SuperDebugUsage", 0xffffffff, false, A(G.super_debug_usage())}},
        {0x0046f470, {"Mem_NoCheck", 0, false, V(G.no_check())}},
        {0x0046f480, {"SuperMem_CheckForLeaks", 0, false, V(G.check_for_leaks())}},
        {0x0046f490, {"FileIO_AllocLoadedBuffer", 0xffffffff, false, A(G.alloc_loaded_buffer(a[0], a[1], a[2], a[3]))}},
        {0x0046f4d0, {"Mem_Delete", 0, false, V(G.release(a[0]))}},
        {0x0046f4e0, {"Mem_Shrink", 0, false, V(G.shrink(a[0], a[1]))}},
        {0x0046f510, {"Mem_GetBlockSize", 0xffffffff, false, A(G.block_size(a[0]))}},
    };
    return table;
}

// Bang: the print of the three numbers through the PSX stub (the retail function then spins)
bool bang_reports_the_print() {
    RetailMemory memory;
    RetailServices services;
    HeapKit kit(memory, services);
    kit.bang(0x80, 0, 0);
    const auto& calls = services.calls();
    return calls.size() == 1 && calls[0].name == "engine_004e8730" && calls[0].args == std::vector<std::uint32_t>{0x00534920, 0x80, 0, 0};
}

}  // namespace

int main(int argc, char** argv) {
    if (!bang_reports_the_print()) {
        std::cerr << "Bang does not report the print\n";
        return 1;
    }
    Replay<Ctx> replay(adapters(), [](RetailMemory memory) { return std::make_unique<Ctx>(std::move(memory)); });
    return replay.run_files(argc, argv, "heap retail forced calls");
}
