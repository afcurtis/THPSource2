"""Cross-reference external evidence onto the canonical THPS2 executable.

The pipeline is deliberately split into small stages whose outputs are reviewable
files under ``re/xref/``:

1. ``features``      - export normalized per-function fingerprints from Ghidra
2. ``match-builds``  - map a variant executable's addresses onto the canonical build
3. ``units``         - attribute functions to Neversoft source files via assert paths
4. ``community``     - harvest names/addresses from pinned community hook projects
5. ``psx``           - match PSX demo symbols onto canonical functions
6. ``apply``         - merge the evidence into ``re/symbols/imported.yml``
7. ``coverage``      - report named/total functions per subsystem
"""
