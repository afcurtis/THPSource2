// Milestone 11b: the guest heap (guest_heap.hpp), the standalone's C runtime heap inside guest memory.
#include "guest_heap.hpp"
#include "tests/test_check.hpp"

#include <cstdint>
#include <iostream>
#include <random>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

using opentony::runtime::guest::GuestHeap;

constexpr std::uint32_t kBase = 0x10000000;
constexpr std::uint32_t kGap = GuestHeap::kGap;

template <typename F>
bool throws_invalid(F f) {
    try {
        f();
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

void addresses_are_aligned_gapped_and_ordered() {
    GuestHeap heap(kBase, 0x100000);
    const std::uint32_t a = heap.allocate(1);
    const std::uint32_t b = heap.allocate(16);
    const std::uint32_t c = heap.allocate(17);
    const std::uint32_t z = heap.allocate(0);
    CHECK(a == kBase + kGap);
    CHECK(b == a + 16 + kGap);
    CHECK(c == b + 16 + kGap);
    CHECK(z == c + 32 + kGap);
    CHECK(heap.size_of(a) == 16 && heap.size_of(c) == 32 && heap.size_of(z) == 16);
    CHECK(heap.size_of(a + 4) == 0 && !heap.owns(a + 4) && heap.owns(b));
    for (std::uint32_t p : {a, b, c, z}) {
        CHECK(p % GuestHeap::kAlign == 0);
    }
}

void freed_spans_are_reused_first_fit_and_coalesce() {
    GuestHeap heap(kBase, 0x100000);
    const std::uint32_t a = heap.allocate(64);
    const std::uint32_t b = heap.allocate(64);
    const std::uint32_t c = heap.allocate(64);
    const std::uint32_t d = heap.allocate(64);
    heap.release(b);
    // a smaller block takes the freed span's start and leaves the rest free
    const std::uint32_t e = heap.allocate(16);
    CHECK(e == b);
    const std::uint32_t f = heap.allocate(16);
    CHECK(f == b + 16 + kGap);
    heap.release(e);
    heap.release(f);
    heap.release(c);
    // b, c and the remainder are one span again: a block of b's and c's bytes fits there
    CHECK(heap.stats().free_spans == 1);
    const std::uint32_t g = heap.allocate(64 + kGap + 64);
    CHECK(g == b);
    CHECK(heap.allocate(16) > d);   // nothing free below d is left
    (void)a;
}

void a_top_span_goes_back_to_the_top() {
    GuestHeap heap(kBase, 0x100000);
    const std::uint32_t a = heap.allocate(100);
    const std::uint32_t b = heap.allocate(100);
    heap.release(b);
    CHECK(heap.stats().top == b - kGap && heap.stats().free_spans == 0);
    heap.release(a);
    CHECK(heap.stats().top == kBase && heap.stats().live_blocks == 0 && heap.stats().live_bytes == 0);
    CHECK(heap.allocate(8) == a);
}

void a_small_remainder_goes_with_the_block() {
    GuestHeap heap(kBase, 0x100000);
    const std::uint32_t a = heap.allocate(48);
    const std::uint32_t guard = heap.allocate(16);
    heap.release(a);
    const std::uint32_t b = heap.allocate(32);   // the span has 48 + gap: 16 bytes would be left, too few for a block
    CHECK(b == a && heap.size_of(b) == 48 && heap.stats().free_spans == 0);
    heap.release(b);
    CHECK(heap.stats().free_spans == 1);
    (void)guard;
}

void bad_frees_throw_and_zero_is_ignored() {
    GuestHeap heap(kBase, 0x100000);
    const std::uint32_t a = heap.allocate(32);
    heap.release(0);
    CHECK(throws_invalid([&] { heap.release(a + 16); }));
    heap.release(a);
    CHECK(throws_invalid([&] { heap.release(a); }));
    CHECK(throws_invalid([&] { heap.release(0x12345670); }));
    CHECK(throws_invalid([] { GuestHeap bad(0, 0x1000); }));
    CHECK(throws_invalid([] { GuestHeap bad(0x10000008, 0x1000); }));
    CHECK(throws_invalid([] { GuestHeap bad(0xfffff000, 0x2000); }));
}

void commit_is_asked_in_granules_and_full_is_zero() {
    std::vector<std::pair<std::uint32_t, std::uint32_t>> commits;
    std::uint32_t limit = kBase + 3 * GuestHeap::kCommitGranule;
    GuestHeap heap(kBase, 0x01000000, [&](std::uint32_t address, std::uint32_t size) {
        if (address + size > limit) {
            return false;
        }
        commits.emplace_back(address, size);
        return true;
    });
    CHECK(heap.allocate(16) != 0);
    CHECK(commits.size() == 1 && commits[0] == std::make_pair(kBase, GuestHeap::kCommitGranule));
    CHECK(heap.allocate(GuestHeap::kCommitGranule) != 0);   // crosses into the second granule
    CHECK(commits.size() == 2 && commits[1].first == kBase + GuestHeap::kCommitGranule && commits[1].second == GuestHeap::kCommitGranule);
    CHECK(heap.allocate(2 * GuestHeap::kCommitGranule) == 0);   // the host refuses the pages
    CHECK(heap.allocate(0x100) != 0);                           // a smaller block still fits the committed pages
    GuestHeap small(kBase, 0x1000);
    CHECK(small.allocate(0x1000) == 0);                       // larger than the range with its gap
    CHECK(small.allocate(0x1000 - kGap) == kBase + kGap);
    CHECK(small.allocate(1) == 0);
}

// The heap against a model: random allocations and frees never overlap, stay in range, and the same sequence gives the same addresses.
void random_sequences_never_overlap_and_repeat() {
    auto run = [](unsigned seed) {
        GuestHeap heap(kBase, 0x00400000);
        std::mt19937 random(seed);
        std::vector<std::pair<std::uint32_t, std::uint32_t>> live;
        std::vector<std::uint32_t> trace;
        for (int step = 0; step < 20000; ++step) {
            if (live.empty() || random() % 3 != 0) {
                const std::uint32_t size = random() % 4 == 0 ? random() % 0x4000 : random() % 0x100;
                const std::uint32_t at = heap.allocate(size);
                if (at == 0) {
                    continue;
                }
                CHECK(at >= kBase + kGap && at + size <= kBase + 0x00400000 && at % 16 == 0 && heap.size_of(at) >= size);
                for (const auto& [other, other_size] : live) {
                    CHECK(at + heap.size_of(at) + kGap <= other || other + other_size + kGap <= at);
                }
                live.emplace_back(at, heap.size_of(at));
                trace.push_back(at);
            } else {
                const std::size_t k = random() % live.size();
                heap.release(live[k].first);
                live.erase(live.begin() + static_cast<std::ptrdiff_t>(k));
            }
        }
        std::uint32_t bytes = 0;
        for (const auto& item : live) {
            bytes += item.second;
        }
        CHECK(heap.stats().live_blocks == live.size() && heap.stats().live_bytes == bytes);
        for (const auto& item : live) {
            heap.release(item.first);
        }
        CHECK(heap.stats().top == kBase && heap.stats().free_spans == 0);
        return trace;
    };
    CHECK(run(7) == run(7));
    CHECK(run(7) != run(8));
}

}  // namespace

int main() {
    addresses_are_aligned_gapped_and_ordered();
    freed_spans_are_reused_first_fit_and_coalesce();
    a_top_span_goes_back_to_the_top();
    a_small_remainder_goes_with_the_block();
    bad_frees_throw_and_zero_is_ignored();
    commit_is_asked_in_granules_and_full_is_zero();
    random_sequences_never_overlap_and_repeat();
    std::cout << "guest heap: ok\n";
    return 0;
}
