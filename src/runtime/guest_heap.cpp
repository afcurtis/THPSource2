#include "guest_heap.hpp"

#include <cstdio>
#include <iterator>
#include <stdexcept>
#include <string>

namespace opentony::runtime::guest {
namespace {

std::string hex(std::uint32_t value) {
    char text[16];
    std::snprintf(text, sizeof text, "%08x", value);
    return text;
}

}  // namespace

GuestHeap::GuestHeap(std::uint32_t base, std::uint32_t size, Commit commit)
    : base_(base), size_(size), commit_(std::move(commit)), top_(base), committed_(commit_ ? base : std::uint64_t{base} + size) {
    if (base % kAlign != 0 || size % kAlign != 0 || base == 0 || std::uint64_t{base} + size > 0x100000000ull) {
        throw std::invalid_argument("guest heap: range " + hex(base) + " +" + hex(size) + " is not an aligned range of guest memory above 0");
    }
    peak_top_ = base;
}

std::uint32_t GuestHeap::allocate(std::uint32_t size) {
    const std::uint64_t usable = (std::uint64_t{size == 0 ? 1u : size} + kAlign - 1) / kAlign * kAlign;
    const std::uint64_t span = usable + kGap;
    if (span > size_) {
        return 0;
    }
    auto taken = static_cast<std::uint32_t>(span);
    std::uint32_t start = take_free(taken);
    if (start == 0) {
        start = take_top(taken);
        if (start == 0) {
            return 0;
        }
    }
    const std::uint32_t address = start + kGap;
    used_.emplace(address, taken);
    ++allocations_;
    live_bytes_ += taken - kGap;
    return address;
}

// First fit by address. A remainder too small to hold a block (less than kGap + kAlign) goes with the block (`span` grows).
std::uint32_t GuestHeap::take_free(std::uint32_t& span) {
    for (auto it = free_.begin(); it != free_.end(); ++it) {
        if (it->second < span) {
            continue;
        }
        const std::uint32_t start = it->first;
        const std::uint32_t rest = it->second - span;
        free_.erase(it);
        if (rest >= kGap + kAlign) {
            free_.emplace(start + span, rest);
        } else {
            span += rest;
        }
        return start;
    }
    return 0;
}

std::uint32_t GuestHeap::take_top(std::uint32_t span) {
    const std::uint64_t end = top_ + span;
    if (end > std::uint64_t{base_} + size_) {
        return 0;
    }
    if (end > committed_) {
        std::uint64_t grow = (end - committed_ + kCommitGranule - 1) / kCommitGranule * kCommitGranule;
        if (committed_ + grow > std::uint64_t{base_} + size_) {
            grow = std::uint64_t{base_} + size_ - committed_;
        }
        if (!commit_(static_cast<std::uint32_t>(committed_), static_cast<std::uint32_t>(grow))) {
            return 0;
        }
        committed_ += grow;
    }
    const auto start = static_cast<std::uint32_t>(top_);
    top_ = end;
    if (top_ > peak_top_) {
        peak_top_ = static_cast<std::uint32_t>(top_);
    }
    return start;
}

void GuestHeap::release(std::uint32_t address) {
    if (address == 0) {
        return;
    }
    const auto it = used_.find(address);
    if (it == used_.end()) {
        throw std::invalid_argument("guest heap: free of " + hex(address) + ", which is not a live block");
    }
    const std::uint32_t span = it->second;
    used_.erase(it);
    ++releases_;
    live_bytes_ -= span - kGap;
    give_back(address - kGap, span);
}

void GuestHeap::give_back(std::uint32_t start, std::uint32_t span) {
    auto next = free_.lower_bound(start);
    if (next != free_.end() && next->first == start + span) {
        span += next->second;
        next = free_.erase(next);
    }
    if (next != free_.begin()) {
        auto previous = std::prev(next);
        if (previous->first + previous->second == start) {
            start = previous->first;
            span += previous->second;
            free_.erase(previous);
        }
    }
    if (std::uint64_t{start} + span == top_) {
        top_ = start;   // the top span goes back to the top (its pages stay committed)
        return;
    }
    free_.emplace(start, span);
}

std::uint32_t GuestHeap::size_of(std::uint32_t address) const noexcept {
    const auto it = used_.find(address);
    return it == used_.end() ? 0 : it->second - kGap;
}

GuestHeap::Stats GuestHeap::stats() const noexcept {
    Stats s;
    s.allocations = allocations_;
    s.releases = releases_;
    s.live_blocks = static_cast<std::uint32_t>(used_.size());
    s.live_bytes = live_bytes_;
    s.top = static_cast<std::uint32_t>(top_);
    s.peak_top = peak_top_;
    s.committed_end = static_cast<std::uint32_t>(committed_);
    s.free_spans = static_cast<std::uint32_t>(free_.size());
    return s;
}

}  // namespace opentony::runtime::guest
