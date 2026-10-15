#include <glib.h>
#include <math.h>
#include <stdio.h>
#include <time.h>
#include "goodix5e0a_image.h"
#include "reference-image.h"

static guint32 rng = 0x79234ab1;
static guint32 next (void) { rng ^= rng << 13; rng ^= rng >> 17; return rng ^= rng << 5; }
static guint8 reference[64*80], optimized[64*80];
static guint16 pixels[64*80];
static volatile guint checksum;
static double now (void) {
  struct timespec t;
  clock_gettime (CLOCK_MONOTONIC, &t);
  return t.tv_sec + t.tv_nsec / 1e9;
}
int main (void)
{
  for (guint test = 0; test < 160; test++)
    {
      for (guint i = 0; i < 64*80; i++)
        switch (test % 8)
          {
          case 0: pixels[i] = 0; break;
          case 1: pixels[i] = 100; break;
          case 2: pixels[i] = next () & 4095; break;
          case 3: pixels[i] = next () & 65535; break;
          case 4: pixels[i] = (i % 2) ? 65535 : 0; break;
          case 5: pixels[i] = (i % 64) * 40 + (i / 64) * 12; break;
          case 6: pixels[i] = (i < 63) ? 100 : 0; break;
          default: pixels[i] = (i < 64) ? 100 : 0; break;
          }
      Goodix5e0aImageSettings s = {3 + 2*(next () % 15), .25f, 100};
      const float gains[] = {.1f, .25f, 1.5f, 8.f};
      const int mids[] = {16,100,128,240};
      s.gain = gains[next () % 4]; s.midpoint = mids[next () % 4];
      gboolean a = goodix5e0a_normalize_reference (pixels, reference, &s);
      gboolean b = goodix5e0a_normalize_configured (pixels, optimized, &s);
      g_assert_cmpint (a, ==, b);
      g_assert_cmpmem (reference, sizeof reference, optimized, sizeof optimized);
    }
  for (guint i=0; i<64*80; i++) pixels[i] = next() & 4095;
  Goodix5e0aImageSettings s = {13,.25f,100};
  for (int i=0;i<10;i++) {
    goodix5e0a_normalize_reference (pixels, reference, &s);
    goodix5e0a_normalize_configured (pixels, optimized, &s);
  }
  const int iterations=400;
  double start=now();
  for (int i=0;i<iterations;i++) {
    goodix5e0a_normalize_reference (pixels, reference, &s); checksum += reference[i%5120];
  }
  double old_us=(now()-start)*1e6/iterations;
  start=now();
  for (int i=0;i<iterations;i++) {
    goodix5e0a_normalize_configured (pixels, optimized, &s); checksum += optimized[i%5120];
  }
  double new_us=(now()-start)*1e6/iterations;
  printf ("{\"equivalent_cases\":160,\"reference_us\":%.2f,\"optimized_us\":%.2f,\"speedup\":%.2f}\n",
          old_us,new_us,old_us/new_us);
}
