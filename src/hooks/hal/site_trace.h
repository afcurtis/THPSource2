#ifndef OPENTONY_HOOKS_HAL_SITE_TRACE_H
#define OPENTONY_HOOKS_HAL_SITE_TRACE_H

/* Dynamic call-site coverage tracer (docs/HAL.md; re/notes/platform-boundary.md,
 * "Residual risk"): an independent, runtime cross-check of the static
 * boundary audit's `re/config/hal_boundary.yml` inventory (874 sites, 3.0).
 *
 * The static audit is Ghidra decompilation plus COM type propagation, checked
 * against instruction-level vtable evidence (0 contradictions, 3.0). This
 * tracer instead watches the *running* retail process: it plants a one-shot
 * software breakpoint (0xCC) at every one of the audit's known call-site
 * addresses (for the canonical build only), and records which ones actually
 * execute during a real capture. It cannot discover a call site the audit
 * failed to list (that would need patching every COM vtable slot regardless
 * of address, a much larger effort deferred in docs/phases/PHASE_3_SUMMARY.md
 * with its reasoning); what it proves is that the listed sites are not an
 * artifact of static analysis: they are the addresses retail's own code
 * actually reaches.
 *
 * Entirely inert unless OPENTONY_HAL_TRACE_SITES names a sites file (one
 * build id line, "canonical", then one hex call-site address per line,
 * written by `tony hal trace sites`); every existing run, test and gate is
 * unaffected. Hits are appended to "<sites file>.hits" as they happen, so a
 * crash or a killed process still leaves partial coverage data. */

#ifdef __cplusplus
extern "C" {
#endif

/* Called once from ot_hooks_attach, after the build is identified. `build`
 * is the ot_builds[] index (0 = canonical); patching only ever happens when
 * this is 0, matching the sites file's recorded build and the addresses
 * re/config/hal_boundary.yml carries in its first (canonical) column. */
void ot_site_trace_start(int build);

/* Called once from ot_hooks_detach. Closes the hits log; does not need to
 * remove any remaining breakpoints (the process is exiting either way). */
void ot_site_trace_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENTONY_HOOKS_HAL_SITE_TRACE_H */
