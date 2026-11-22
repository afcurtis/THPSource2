#ifndef OPENTONY_HOOKS_ORACLE_SCRIPT_H
#define OPENTONY_HOOKS_ORACLE_SCRIPT_H

#include <stdint.h>

/* Function-level oracle (Phase 5.4a, 7b continued, part 3). A script names calls of retail functions with arguments built in retail memory, and dumps what they
 * leave behind in a canonical text form; the same script runs in the hooked retail process (the real functions, the session probe's
 * OPENTONY_SESSION_SCRIPT) and in the program with no retail image (the natives, opentony_spike's OT_ORACLE_SCRIPT), and the two outputs are compared
 * (tests/test_texture_oracle.py). Both are 32-bit Windows processes in which a retail address is an address, so memory is read and written through pointers; what
 * differs is how a function is called and how a surface is read, which is the backend.
 *
 * One command per line, `#` starts a comment, arguments are separated by spaces. An expression is a number (decimal, or hex with 0x), a name, or `[expression]` (the
 * dword at an address), followed by any number of `+n` / `-n`:
 *
 *   set32|set16|set8 ADDR VALUE      write
 *   or32 ADDR VALUE                  set bits of the dword at ADDR
 *   alloc NAME SIZE                 a block from the game's allocator (operator new, 0x005002fe), zeroed; NAME is its address
 *   bytes ADDR HEX                   write the bytes
 *   fill ADDR VALUE SIZE             write a byte repeatedly
 *   pattern ADDR SIZE SEED           write a pseudo-random byte sequence (a fixed generator, so both sides agree)
 *   call [NAME=]ADDR c|t|s ARG...    call the function at ADDR (c: cdecl, t: thiscall with the first argument in ECX, s: stdcall); NAME takes the answer
 *   let NAME EXPR                    name a value (read now: a later change of the memory does not change the name)
 *   val LABEL EXPR                   print a value
 *   maskval LABEL EXPR MASK          print a value with the bits outside MASK cleared (a word whose other bits are stack leftovers)
 *   same LABEL EXPR EXPR            print 1 when two values are equal (a pointer compared with a name), else 0
 *   rec LABEL ADDR [WORD,...]        print the eleven dwords of a texture record; the words listed are pointers and print as P (non-null) or 0
 *   mem LABEL ADDR SIZE              print bytes in hex
 *   surf LABEL SURFACE               print a surface's size and its rows, packed (no pitch padding)
 *   tick                             one delivery of the simulation timer callback (see OtOracleBackend::tick)
 *   flush                            write what has been printed so far to the output file now
 *   echo TEXT
 */

struct OtOracleBackend {
    void *context;
    /* Call the retail function at `address`. `count` words are its arguments (for a thiscall the first is the object). `convention`: 0 cdecl, 1 thiscall, 2 stdcall. */
    uint32_t (*call)(void *context, uint32_t address, int convention, const uint32_t *args, uint32_t count);
    /* The surface named by `surface` (the value a texture record keeps in word 0): its size, bytes per pixel and rows packed to width * bytes per pixel in `pixels`.
     * 0 when there is no such surface or `capacity` is too small. */
    int (*surface)(void *context, uint32_t surface, uint32_t *width, uint32_t *height, uint32_t *bytes_per_pixel, unsigned char *pixels, uint32_t capacity);
    /* One delivery of the simulation timer's callback (SimulationTimeTimerCallback, 0x004dace0, with the game's TimerState at 0x006a05a0). In the retail process the
     * lockstep clock owns that function, so a delivery is the clock's own; the program with no retail image calls the native. May be null. */
    void (*tick)(void *context);
};

/* Runs the script and writes what it printed to `output_path`. 0 when it ran to the end; otherwise the number of the line that failed (the output says why). */
int ot_oracle_run(const char *script_path, const char *output_path, const OtOracleBackend *backend);

#endif /* OPENTONY_HOOKS_ORACLE_SCRIPT_H */
