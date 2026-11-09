/* SPDX-License-Identifier: GPL-3.0-or-later */
/* sf_parallel.h — host-side parallel runner for the ps8bf filter.
 *
 * The MSVC .8bf build compiles the engine *without* OpenMP on purpose: MSVC's
 * /openmp would add an import on vcomp140.dll, and the filter must import only
 * KERNEL32/USER32/GDI32. This file provides the same parallelism through plain
 * Win32 threads (CreateThread lives in kernel32) by plugging into the shared
 * chain's optional parallel-exec hook (sp_chain_set_parallel).
 *
 * The threaded work is element-wise per-pixel processing, so splitting a pixel
 * range into contiguous chunks and running them concurrently is bit-identical
 * to the serial loop — verified by the Linux harness, which links this same
 * chunking code through a pthread backend.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Install the runner into sp_chain (idempotent). Called once per filter
 * invocation from PluginMain. Honours SPEKTRAFILM_THREADS=<n> to cap the
 * worker count; SPEKTRAFILM_THREADS=1 forces the serial path. */
void sf_parallel_install(void);

/* Number of worker threads the runner will use (>=1). */
int sf_parallel_workers(void);

#ifdef __cplusplus
}
#endif
