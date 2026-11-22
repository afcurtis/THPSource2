"""Stubs shared by the Phase 4.10a frontend groups (re/gdb/probes/frontend_calls.py).

Unlike the create-a-skater probe this does not stub the font's width function (0x0044a4d0): in 4.10a it is a function
under test.  Only outside services are stubbed: the assertion routine, printf, the random numbers, the allocator and
Mem_Delete.  Allocations come from the slab's arena (Env.alloc_base / Env.alloc_size, set by a group's seed).
"""

from __future__ import annotations


def install_stubs(fc, env):
    import os

    from create_specs import SPECS

    # Phase 4.14b: the allocator wrappers are functions under test in the heap group, where a stub on one would intercept the forced call itself
    selected = [g for g in os.environ.get("P410_GROUPS", "").split(",") if g]
    under_test = {s.address for s in SPECS if s.group in selected}

    def stub(address, *args, **kw):
        if address not in under_test:
            fc.stub(address, *args, **kw)

    stub(0x004011E0, 2, "assert", when=lambda a: (a[0] & 0xFF) == 0, keep=slice(1, 2))
    stub(0x004CF850, 1, "printf_log")
    stub(0x004C9340, 1, "rnd", result=env.rnd)
    stub(0x0040B500, 1, "rand", result=env.rnd)
    stub(0x0046F490, 4, "alloc", result=env.alloc)
    stub(0x0046F4D0, 1, "mem_delete")
