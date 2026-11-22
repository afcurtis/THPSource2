"""Mark the four object list functions of ob.cpp trace-validated in re/native/functions.yml (milestone 10e)."""
import re

p = (__import__("os").path.expanduser("~/THPSource2") + "/re/native/functions.yml")
text = open(p).read()
for addr in ("0x004801d0", "0x004801f0", "0x004802f0", "0x004803b0"):
    m = re.search(r"  - address: %s\n(.*?)(?=\n  - address: |\Z)" % addr, text, re.S)
    assert m, addr
    block = m.group(0)
    new = block.replace("    status: tested\n", "    status: trace-validated\n", 1)
    new = new.replace("      - src/trg/script_world.cpp\n", "      - src/trg/script_world.cpp\n      - src/runtime/world_object.cpp\n", 1)
    new = new.replace("      - src/trg/world_objects_test.cpp\n", "      - src/trg/world_objects_test.cpp\n      - src/runtime/world_trig_nodes_trace_test.cpp\n", 1)
    new = new.replace(
        "      - re/evidence/functions/object-script-vm.md\n",
        "      - re/evidence/functions/object-script-vm.md\n"
        "      - re/evidence/fixtures/object-lists-hangar.txt\n"
        "      - re/evidence/fixtures/object-lists-warehouse.txt\n"
        "      - re/evidence/fixtures/object-lists-levels.txt\n", 1)
    suffix = (" WorldKit (world_object.cpp, milestone 10e) is the retail-memory form the standalone runs, from the disassembly; replayed against 880 top-level retail calls of the 13 level loads "
              "(437 PushHead, 192 Remove, 83 SuspendToList with their virtual slot 1, 42 UnSuspend; the Hangar and Warehouse loads hold no suspend) word for word over the object, its neighbours, "
              "the list heads and the vtable, 0 mismatches.")
    new = re.sub(r'(    notes: ")(.*)(")\n', lambda mm: mm.group(1) + mm.group(2) + suffix + '"\n', new, count=1)
    assert new != block
    text = text.replace(block, new)
open(p, "w").write(text)
print("ok")
