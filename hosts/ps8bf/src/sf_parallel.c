/* SPDX-License-Identifier: GPL-3.0-or-later */
/* sf_parallel.c — see sf_parallel.h.
 *
 * One job = one contiguous pixel range handed to an sp_range_fn. For each job
 * we split [0, npix) into one range per worker, run range 0 on the calling
 * thread, the rest on freshly created threads, and join. Ranges are contiguous
 * and disjoint and the work is element-wise, so the output is byte-identical to
 * serial regardless of how many threads run.
 *
 * Create/join per stage is deliberate: a stage covers the whole image (millions
 * of pixels), so thread setup is noise next to the work, and there is no pool
 * state to tear down at DLL unload. Thread creation failure degrades to running
 * that range inline, never to incorrect output.
 */
#include <stdlib.h>

#include "spektra_sim.h"
#include "spektra_core.h" /* sf_set_loop_runner + sf_loop_fn */
#include "sp_chain.h"
#include "sf_parallel.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

#define SF_PARALLEL_MAX_THREADS 64

/* Below this many pixels the per-stage thread setup outweighs the gain. */
#define SF_PARALLEL_MIN_PIXELS ((size_t)1 << 16)

/* Engine loop items are rows/blocks (heavy), so a much lower floor is fine. */
#define SF_PARALLEL_MIN_ITEMS ((size_t)8)

typedef struct
{
  sp_range_fn fn;
  void *ctx;
  size_t start;
  size_t count;
} sf_job_t;

#ifdef _WIN32
static DWORD WINAPI sf_worker_main(LPVOID arg)
{
  sf_job_t *j = (sf_job_t *)arg;
  j->fn(j->ctx, j->start, j->count);
  return 0;
}
#else
static void *sf_worker_main(void *arg)
{
  sf_job_t *j = (sf_job_t *)arg;
  j->fn(j->ctx, j->start, j->count);
  return NULL;
}
#endif

/* Cached logical CPU count, capped and overridable for testing. */
int sf_parallel_workers(void)
{
  static int cached = 0;
  if(cached > 0) return cached;

  int n = 0;
  const char *env = getenv("SPEKTRAFILM_THREADS");
  if(env && env[0])
  {
    n = atoi(env);
    if(n < 1) n = 1;
  }
  if(n <= 0)
  {
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    n = (int)si.dwNumberOfProcessors;
#else
    long p = sysconf(_SC_NPROCESSORS_ONLN);
    n = (p > 0) ? (int)p : 1;
#endif
  }
  if(n < 1) n = 1;
  if(n > SF_PARALLEL_MAX_THREADS) n = SF_PARALLEL_MAX_THREADS;
  cached = n;
  return cached;
}

static void sf_parallel_run_min(sp_range_fn fn, void *ctx, size_t npix, size_t min_items)
{
  const int workers = sf_parallel_workers();
  if(workers <= 1 || npix < min_items)
  {
    fn(ctx, 0, npix);
    return;
  }

  int n = workers;
  if((size_t)n > npix) n = (int)npix;

  sf_job_t jobs[SF_PARALLEL_MAX_THREADS];
  const size_t base = npix / (size_t)n;
  const size_t rem = npix % (size_t)n;
  size_t start = 0;
  for(int i = 0; i < n; i++)
  {
    const size_t count = base + ((size_t)i < rem ? 1u : 0u);
    jobs[i].fn = fn;
    jobs[i].ctx = ctx;
    jobs[i].start = start;
    jobs[i].count = count;
    start += count;
  }

#ifdef _WIN32
  HANDLE th[SF_PARALLEL_MAX_THREADS];
  int nth = 0;
  for(int i = 1; i < n; i++)
  {
    HANDLE h = CreateThread(NULL, 0, sf_worker_main, &jobs[i], 0, NULL);
    if(h) th[nth++] = h;
    else jobs[i].fn(jobs[i].ctx, jobs[i].start, jobs[i].count); /* inline fallback */
  }
  jobs[0].fn(jobs[0].ctx, jobs[0].start, jobs[0].count);
  if(nth > 0) WaitForMultipleObjects((DWORD)nth, th, TRUE, INFINITE);
  for(int i = 0; i < nth; i++) CloseHandle(th[i]);
#else
  pthread_t th[SF_PARALLEL_MAX_THREADS];
  int nth = 0;
  for(int i = 1; i < n; i++)
  {
    if(pthread_create(&th[nth], NULL, sf_worker_main, &jobs[i]) == 0) nth++;
    else jobs[i].fn(jobs[i].ctx, jobs[i].start, jobs[i].count); /* inline fallback */
  }
  jobs[0].fn(jobs[0].ctx, jobs[0].start, jobs[0].count);
  for(int i = 0; i < nth; i++) pthread_join(th[i], NULL);
#endif
}

/* Adapt the engine's per-index loop hook (sf_set_loop_runner) to the range
   splitter above: each worker runs fn(ctx, i) for i in its sub-range. */
typedef struct
{
  sf_loop_fn fn;
  void *ctx;
} sf_loop_tramp;

static void sf_loop_range(void *v, size_t start, size_t count)
{
  const sf_loop_tramp *t = (const sf_loop_tramp *)v;
  const size_t end = start + count;
  for(size_t i = start; i < end; i++) t->fn(t->ctx, i);
}

static void sf_parallel_exec_impl(sp_range_fn fn, void *ctx, size_t npix)
{
  sf_parallel_run_min(fn, ctx, npix, SF_PARALLEL_MIN_PIXELS);
}

static void sf_parallel_loop_impl(sf_loop_fn fn, void *ctx, size_t n)
{
  sf_loop_tramp t = { fn, ctx };
  sf_parallel_run_min(sf_loop_range, &t, n, SF_PARALLEL_MIN_ITEMS);
}

void sf_parallel_install(void)
{
  sp_chain_set_parallel(sf_parallel_exec_impl);
  sf_set_loop_runner(sf_parallel_loop_impl);
}
