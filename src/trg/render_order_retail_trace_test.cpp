// Replays a retail ordering-table trace (see scripts/make_render_order_fixture.py): every polygon
// link M3D_ClipAndBucketPolygon made since the table was cleared is applied to RetailOrderingTable,
// which must hand each polygon the same word 0 the retail table did, and its far-to-near draw
// order must equal the chain D3D_DrawPolygonList walked from the address its caller passed.
//
//   opentony_render_order_retail_trace_test FIXTURE.txt [--mutate]
//
// --mutate swaps two expected links and must make the replay fail.
#include "render_model_submit.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace opentony::trg;

namespace {

int failures = 0;
void fail(long frame, const std::string& message) {
    if (++failures <= 20) std::cerr << "frame " << frame << ": " << message << "\n";
}

struct Frame {
    std::uint32_t base{}, start{};
    std::size_t size{};
    struct Link { std::uint32_t poly, bucket, next; };
    std::vector<Link> links;
    std::vector<std::pair<std::uint32_t, bool>> nodes;
};

std::uint32_t hex(const std::string& s) { return static_cast<std::uint32_t>(std::stoul(s, nullptr, 16)); }

long polygons_checked = 0, frames_checked = 0;

void replay(long index, Frame& frame, bool mutate) {
    RetailOrderingTable table(frame.base, frame.size - RetailOrderingTable::kBucketEntryOffset);
    if (frame.start != table.entry_address(frame.size - 1)) {
        fail(index, "the caller did not pass the last ordering-table entry");
    }
    if (mutate && frame.links.size() > 1) {
        std::swap(frame.links.front().bucket, frame.links.back().bucket);
    }
    std::map<std::uint32_t, std::uint32_t> next;
    for (const auto& link : frame.links) {
        const std::uint32_t got = table.prepend(link.bucket, link.poly);
        next[link.poly] = got;
        ++polygons_checked;
        if (got != link.next) {
            std::ostringstream os;
            os << "polygon 0x" << std::hex << link.poly << " bucket 0x" << link.bucket
               << " got word 0 0x" << got << ", retail 0x" << link.next;
            fail(index, os.str());
        }
    }
    const auto order = table.draw_order([&](std::uint32_t poly) { return next.at(poly); });
    std::vector<std::uint32_t> expected;
    std::uint32_t last_entry = 0xffffffffU;
    for (const auto& [node, is_polygon] : frame.nodes) {
        if (!is_polygon) {
            // Entries must be visited in strictly descending order.
            if (node >= last_entry) fail(index, "entry visited out of descending order");
            last_entry = node;
        } else if (next.count(node)) {
            expected.push_back(node);
        }
    }
    if (order != expected) {
        std::ostringstream os;
        os << "draw order differs: native " << order.size() << " polygons, retail " << expected.size();
        for (std::size_t i = 0; i < std::min(order.size(), expected.size()); ++i) {
            if (order[i] != expected[i]) {
                os << ", first difference at " << i << " (native 0x" << std::hex << order[i]
                   << " retail 0x" << expected[i] << ")";
                break;
            }
        }
        fail(index, os.str());
    }
    ++frames_checked;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: " << argv[0] << " FIXTURE.txt [--mutate]\n";
        return 2;
    }
    const bool mutate = argc > 2 && std::string(argv[2]) == "--mutate";
    std::ifstream fixture(argv[1]);
    if (!fixture) {
        std::cerr << "cannot open fixture\n";
        return 2;
    }
    std::string line;
    Frame frame;
    long index = 0;
    while (std::getline(fixture, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream in(line);
        std::string tag, a, b, c;
        in >> tag >> a >> b >> c;
        if (tag == "D") {
            frame = Frame{};
            ++index;
            frame.base = hex(a);
            frame.size = std::stoul(b);
            frame.start = hex(c);
        } else if (tag == "L") {
            frame.links.push_back({hex(a), hex(b), hex(c)});
        } else if (tag == "N") {
            frame.nodes.emplace_back(hex(a), b == "1");
        } else if (tag == "Z") {
            replay(index, frame, mutate);
        } else {
            std::cerr << "unknown record " << tag << "\n";
            return 2;
        }
    }
    std::cout << "frames " << frames_checked << ", polygons " << polygons_checked << "\n";
    if (frames_checked == 0) return 2;
    return failures == 0 ? 0 : 1;
}
