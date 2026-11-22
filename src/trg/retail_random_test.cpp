// Rnd (0x004c9340) against a retail capture: the Warehouse platform trace
// recorded the generator state at the entry of a bounce and every draw it made
// (114 draws across the trace agree with this model; this pins the first).
#include "retail_random.hpp"

#include "tests/test_check.hpp"

int main() {
    opentony::trg::RetailRandom random({2630746775U, 1340977373U, 778356218U});
    CHECK(random.next(4096) == 3365);
    CHECK(random.next(2) == 0);
    CHECK(random.next(2) == 1);

    // The result is always inside the requested range.
    opentony::trg::RetailRandom sweep({1U, 2U, 3U});
    for (int index = 0; index < 1000; ++index) {
        const int value = sweep.next(7);
        CHECK(value >= 0 && value < 7);
    }
    return 0;
}
