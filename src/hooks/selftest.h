#ifndef OPENTONY_HOOK_SELFTEST_H
#define OPENTONY_HOOK_SELFTEST_H

/* Differential self-test run inside the game process (OPENTONY_SELFTEST=1),
 * before WinMain: for every installed hook, the retail code (through its
 * trampoline), the native entry point, and the patched retail entry with the
 * toggle on and off must produce identical memory and return values.
 * Writes <dir>opentony.selftest and returns the number of failures. */
int ot_selftest_run(const char *dir);

#endif /* OPENTONY_HOOK_SELFTEST_H */
