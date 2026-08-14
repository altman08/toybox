/* mhz.c - measure CPU clock frequency
 *
 * Copyright 2001-2016 Willy Tarreau <w@1wt.eu>
 *
 * No standard.

USE_MHZ(NEWTOY(mhz, "cit", TOYFLAG_USR|TOYFLAG_BIN))

config MHZ
  bool "mhz"
  default n
  help
    usage: mhz [-ci] [-t] [LINES [HEAT [COUNT]]]

    Measure CPU clock frequency.

    -c  Show CPU MHz only
    -i  Report integral (integer) frequencies only
    -t  Show TSC MHz only (x86/x86_64)

    LINES  Number of measurements (default 1)
    HEAT   Pre-heat time in microseconds (default 0)
    COUNT  Calibration value, higher is slower but more accurate (default auto)
*/

#define FOR_mhz
#include "toys.h"

#if defined(__i386__) || defined(__x86_64__)
#define HAVE_RDTSC 1
#endif

GLOBALS(
  unsigned int count;
  long runs;
)

// Return current time in microseconds using monotonic clock.
static unsigned long long mhz_now(void)
{
  struct timespec tv;

  clock_gettime(CLOCK_MONOTONIC, &tv);
  return (unsigned long long)tv.tv_sec * 1000000ULL + tv.tv_nsec / 1000ULL;
}

#ifdef HAVE_RDTSC
// Read the CPU timestamp counter on x86/x86_64.
static unsigned long long mhz_rdtsc(void)
{
  unsigned int lo, hi;

  asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
  return lo + ((unsigned long long)hi << 32);
}
#else
#define mhz_rdtsc() 0ULL
#endif

// Prevent the compiler from reordering or eliminating a variable.
#define dont_move(var) do { asm volatile("" : "=r"(var) : "0"(var)); } while (0)

// Individual read-after-write XOR steps (each depends on the previous).
#define run1cycle_ae()  do { a ^= e; dont_move(a); } while (0)
#define run1cycle_ba()  do { b ^= a; dont_move(b); } while (0)
#define run1cycle_cb()  do { c ^= b; dont_move(c); } while (0)
#define run1cycle_dc()  do { d ^= c; dont_move(d); } while (0)
#define run1cycle_ed()  do { e ^= d; dont_move(e); } while (0)

#define run5cycles() do { \
  run1cycle_ae(); \
  run1cycle_ba(); \
  run1cycle_cb(); \
  run1cycle_dc(); \
  run1cycle_ed(); \
} while (0)

#define run10cycles()  do { run5cycles();  run5cycles();  } while (0)

#define run100cycles() do { \
  run10cycles(); run10cycles(); run10cycles(); run10cycles(); run10cycles(); \
  run10cycles(); run10cycles(); run10cycles(); run10cycles(); run10cycles(); \
} while (0)

// 50 serially-dependent XOR operations per iteration.  The CPU cannot
// execute them out of order, so each iteration costs at least 50 cycles.
static __attribute__((noinline, aligned(64))) void loop50(unsigned int n)
{
  unsigned int a = 0, b = 0, c = 0, d = 0, e = 0;

  do {
    run10cycles(); run10cycles(); run10cycles(); run10cycles(); run10cycles();
  } while (__builtin_expect(--n, 1));
}

// 250 serially-dependent XOR operations per iteration.  Kept small enough
// to fit in a 1 kB L1 cache on 32-bit instruction sets.
static __attribute__((noinline, aligned(64))) void loop250(unsigned int n)
{
  unsigned int a = 0, b = 0, c = 0, d = 0, e = 0;

  do {
    run10cycles(); run10cycles(); run10cycles(); run10cycles(); run10cycles();
    run100cycles(); run100cycles();
  } while (__builtin_expect(--n, 1));
}

// Run one measurement, return the final count so the next call can reuse it.
static unsigned int mhz_run_once(unsigned int count)
{
  long long tsc_begin;
  long long tsc50 __attribute__((unused));
  long long tsc250 __attribute__((unused));
  long long us_begin, us50, us250, usdiff;
  int retries = 24;
  unsigned int i;
  char mhz[20];

  for (;;) {
    // Pick the fastest of 5 back-to-back runs of the 50-cycle loop.
    us50 = LLONG_MAX;
    for (i = 0; i < 5; i++) {
      us_begin = mhz_now();
      tsc_begin = mhz_rdtsc();
      loop50(count);
      tsc50 = mhz_rdtsc() - tsc_begin;
      usdiff = mhz_now() - us_begin;
      if (usdiff < us50) us50 = usdiff;
    }

    if (us50 < 20000 && retries) {
      // Need at least 20 ms; double count below 10 ms, else +25 %.
      count = (us50 < 10000) ? count * 2 : count * 5 / 4;
      retries--;
      continue;
    }

    // Pick the fastest of 5 back-to-back runs of the 250-cycle loop.
    us250 = LLONG_MAX;
    for (i = 0; i < 5; i++) {
      us_begin = mhz_now();
      tsc_begin = mhz_rdtsc();
      loop250(count);
      tsc250 = mhz_rdtsc() - tsc_begin;
      usdiff = mhz_now() - us_begin;
      if (usdiff < us250) us250 = usdiff;
    }

    // Valid measurement: the two loops must differ.
    if (us250 != us50) break;

    if (!retries--) break;
    count *= 2;
  }

  // Compute MHz from the difference: loop250 - loop50 = 200 cycles per count.
  if (FLAG(i))
    snprintf(mhz, sizeof(mhz), "%.0f",
             count * 200.0 / (us250 - us50) + 0.5);
  else
    snprintf(mhz, sizeof(mhz), "%.3f",
             count * 200.0 / (us250 - us50));

  if (!FLAG(c) && !FLAG(t)) {
    xprintf("count=%u us50=%lld us250=%lld diff=%lld cpu_MHz=%s",
            count, us50, us250, us250 - us50, mhz);
  } else if (FLAG(c)) {
    xprintf("%s\n", mhz);
    return count;
  }

#ifdef HAVE_RDTSC
  if (FLAG(i))
    snprintf(mhz, sizeof(mhz), "%.0f",
             (tsc250 - tsc50) / (float)(us250 - us50) + 0.5);
  else
    snprintf(mhz, sizeof(mhz), "%.3f",
             (tsc250 - tsc50) / (float)(us250 - us50));

  if (!FLAG(t)) {
    xprintf(" tsc50=%lld tsc250=%lld diff=%lld rdtsc_MHz=%s",
            tsc50, tsc250, (tsc250 - tsc50) / count, mhz);
  } else {
    xprintf("%s\n", mhz);
    return count;
  }
#endif

  xputc('\n');
  return count;
}

// Spin for <delay> microseconds to let the CPU reach its rated frequency.
static void mhz_preheat(long delay)
{
  unsigned long long start = mhz_now();

  while (mhz_now() - start < (unsigned long long)delay);
}

void mhz_main(void)
{
  long runs = 1;
  unsigned int count = 1000;

  if (*toys.optargs) runs = atolx(toys.optargs[0]);
  if (toys.optargs[0] && toys.optargs[1]) mhz_preheat(atolx(toys.optargs[1]));
  if (toys.optargs[0] && toys.optargs[1] && toys.optargs[2]) {
    long v = atolx(toys.optargs[2]);

    if (v > 0) count = (unsigned int)v;
  }

  while (runs--) count = mhz_run_once(count);
}
