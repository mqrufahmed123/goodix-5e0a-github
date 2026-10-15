/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Entirely synthetic fixtures: never reads, stores, or reconstructs biometrics. */
#include "goodix5e0a-coverage.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define PI 3.14159265358979323846
extern unsigned int goodix_coverage_test_anchor_count (const GoodixCoverage *self);
extern void goodix_coverage_test_fill_anchor_bank (GoodixCoverage *self);
static unsigned int random_state = 0x97182345;
static double noise (void) {
  random_state ^= random_state << 13;
  random_state ^= random_state >> 17;
  random_state ^= random_state << 5;
  return (random_state & 65535) / 65535.0 - .5;
}

static double texture (double x, double y, int id) {
  double phase = .74 * x + .18 * y + .0016 * x * y + .0018 * y * y
                 + 2.0 * sin (.041 * x) * cos (.052 * y)
                 + .65 * sin (.13 * y + .08 * x)
                 + atan2(y - 9, x - 12) - atan2(y + 22, x + 18)
                 + atan2(y - 31, x + 8)
                 + atan2(y - 45, x - 35) - atan2(y - 40, x - 15)
                 + atan2(y + 10, x - 45);
  if (id)
    phase = .22 * x - .83 * y + .0029 * x * x + 3.2 * cos (.071 * y) * sin (.039 * x);
  return 125 + 65 * sin (phase) + 15 * sin (2 * phase + .4)
         + 9 * cos (.23 * x + .19 * y) * sin (.16 * y - .09 * x);
}

static void frame (uint8_t out[64 * 80], double x, double y, double angle,
                   int id, double gain, double offset, double jitter) {
  double c = cos (angle * PI / 180), s = sin (angle * PI / 180);
  int ix, iy;
  for (iy = 0; iy < 80; iy++)
    for (ix = 0; ix < 64; ix++) {
      double wx = x + c * (ix - 31.5) - s * (iy - 39.5);
      double wy = y + s * (ix - 31.5) + c * (iy - 39.5);
      double value = gain * texture (wx, wy, id) + offset + jitter * noise ();
      out[iy * 64 + ix] = (uint8_t) fmax (0, fmin (255, value));
    }
}

static void report (const char *name, const GoodixCoverageResult *r) {
  printf ("%s: %s pose %.2f %.2f %.2f score %.4f margin %.4f overlap %.3f cells %u +%u novel %.3f\n",
          name, goodix_coverage_status_name (r->status), r->x - 256, r->y - 256,
          r->angle, r->score, r->margin, r->overlap, r->observed_cells, r->new_cells, r->novel_fraction);
  fflush (stdout);
}

static void test_repetition (void) {
  uint8_t image[64 * 80];
  GoodixCoverage *tracker = goodix_coverage_new ();
  const GoodixCoverageResult *r;
  unsigned int original, i;
  assert (tracker);
  frame (image, 0, 0, 0, 0, 1, 0, 0);
  r = goodix_coverage_observe (tracker, image, 64);
  report ("initial", r);
  assert (r->status == GOODIX_COVERAGE_INITIAL && r->observed_cells >= 100);
  original = r->observed_cells;
  for (i = 0; i < 3; i++) {
    frame (image, 0, 0, 0, 0, i == 1 ? .82 : 1, i == 1 ? 19 : 0, i == 2 ? 8 : 0);
    r = goodix_coverage_observe (tracker, image, 64);
    report ("duplicate", r);
    assert (r->status == GOODIX_COVERAGE_REPEATED);
    assert (r->observed_cells == original && r->new_cells == 0);
    assert (hypot (r->x - 256, r->y - 256) < 1.6 && fabs (r->angle) < 1.6);
  }
  goodix_coverage_free (tracker);
}

static void test_translation_rotation (void) {
  uint8_t image[64 * 80];
  GoodixCoverage *tracker = goodix_coverage_new ();
  const GoodixCoverageResult *r;
  const double poses[][3] = { { 0, 0, 0 }, { 14, 3, 0 }, { 22, 12, 7.5 },
                              { 9, 19, -6 }, { -5, 13, -9 }, { 0, 0, 0 } };
  unsigned int i, first = 0, previous = 0;
  for (i = 0; i < sizeof poses / sizeof poses[0]; i++) {
    frame (image, poses[i][0], poses[i][1], poses[i][2], 0, 1, 0, 2);
    r = goodix_coverage_observe (tracker, image, 64);
    report ("translation/rotation", r);
    assert (r->status == GOODIX_COVERAGE_INITIAL || r->status == GOODIX_COVERAGE_ADDED || r->status == GOODIX_COVERAGE_REPEATED);
    assert (hypot (r->x - 256 - poses[i][0], r->y - 256 - poses[i][1]) < 2.0);
    assert (fabs (r->angle - poses[i][2]) < 2.0);
    assert (r->observed_cells >= previous);
    if (!i)
      first = r->observed_cells;
    previous = r->observed_cells;
  }
  assert (r->observed_cells >= first * 1.5);
  assert (r->status == GOODIX_COVERAGE_REPEATED);
  goodix_coverage_free (tracker);
}

static void test_rejection (void) {
  uint8_t image[64 * 80], grid[128 * 128];
  GoodixCoverage *tracker = goodix_coverage_new ();
  const GoodixCoverageResult *r;
  unsigned int saved, i;
  frame (image, 0, 0, 0, 0, 1, 0, 0);
  r = goodix_coverage_observe (tracker, image, 64);
  saved = r->observed_cells;
  memcpy (grid, r->grid, sizeof grid);
  frame (image, 0, 0, 0, 1, 1, 0, 0);
  r = goodix_coverage_observe (tracker, image, 64);
  report ("unrelated", r);
  assert (r->status == GOODIX_COVERAGE_UNALIGNED);
  assert (r->observed_cells == saved && !memcmp (grid, r->grid, sizeof grid));
  frame (image, 180, -190, 0, 0, 1, 0, 0);
  r = goodix_coverage_observe (tracker, image, 64);
  report ("nonoverlap", r);
  assert (r->status == GOODIX_COVERAGE_UNALIGNED);
  assert (r->observed_cells == saved && !memcmp (grid, r->grid, sizeof grid));
  memset (image, 127, sizeof image);
  r = goodix_coverage_observe (tracker, image, 64);
  report ("flat", r);
  assert (r->status == GOODIX_COVERAGE_LOW_QUALITY && r->observed_cells == saved);
  for (i = 0; i < sizeof image; i++)
    image[i] = (uint8_t) (127 + 90 * noise ());
  r = goodix_coverage_observe (tracker, image, 64);
  report ("unstructured noise", r);
  assert (r->status == GOODIX_COVERAGE_LOW_QUALITY && r->observed_cells == saved);
  goodix_coverage_reset (tracker);
  r = goodix_coverage_result (tracker);
  assert (r->frame_count == 0 && r->observed_cells == 0 && r->aligned_count == 0);
  for (i = 0; i < sizeof image; i++)
    image[i] = (uint8_t) (127 + 70 * sin ((i % 64) * PI / 4));
  r = goodix_coverage_observe (tracker, image, 64);
  report ("periodic ambiguous", r);
  assert (r->status == GOODIX_COVERAGE_LOW_QUALITY && r->observed_cells == 0);
  goodix_coverage_free (tracker);
}

static void test_rotated_same_region (void) {
  uint8_t image[64 * 80], original[64 * 80];
  GoodixCoverage *tracker = goodix_coverage_new ();
  const GoodixCoverageResult *r;
  unsigned int cells, pass;
  int x, y;
  frame (original, 0, 0, 0, 0, 1, 0, 0);
  r = goodix_coverage_observe (tracker, original, 64);
  cells = r->observed_cells;
  for (pass = 0; pass < 2; pass++) {
    double angle = pass ? -9 : 9;
    double c = cos (angle * PI / 180), s = sin (angle * PI / 180);
    for (y = 0; y < 80; y++)
      for (x = 0; x < 64; x++) {
        double sx = c * (x - 31.5) - s * (y - 39.5) + 31.5;
        double sy = s * (x - 31.5) + c * (y - 39.5) + 39.5;
        int ix = (int) floor (sx), iy = (int) floor (sy);
        double fx = sx - ix, fy = sy - iy;
        image[y * 64 + x] = ix >= 0 && iy >= 0 && ix < 63 && iy < 79 ?
          (uint8_t) ((1 - fy) * ((1 - fx) * original[iy * 64 + ix] + fx * original[iy * 64 + ix + 1])
                     + fy * ((1 - fx) * original[(iy + 1) * 64 + ix] + fx * original[(iy + 1) * 64 + ix + 1])) : 127;
      }
    r = goodix_coverage_observe (tracker, image, 64);
    report ("rotated same source crop", r);
    assert (r->status == GOODIX_COVERAGE_REPEATED);
    assert (r->observed_cells == cells && r->new_cells == 0);
    assert (hypot (r->x - 256, r->y - 256) < 1.6 && fabs (r->angle - angle) < 1.6);
  }
  goodix_coverage_free (tracker);
}

static void benchmark (void) {
  uint8_t image[64 * 80], saved_grid[128 * 128];
  GoodixCoverage *tracker = goodix_coverage_new ();
  const GoodixCoverageResult *r = NULL;
  unsigned int i;
  double maximum = 0, start = (double) clock () / CLOCKS_PER_SEC;
  for (i = 0; i < 20; i++) {
    double step_start;
    double px = (i / 4) % 2 ? (3 - i % 4) * 15 : (i % 4) * 15;
    frame (image, px, (i / 4) * 18, (i % 3) * 4.5, 0, 1, 0, 2);
    memcpy (saved_grid, goodix_coverage_result (tracker)->grid, sizeof saved_grid);
    step_start = (double) clock () / CLOCKS_PER_SEC;
    r = goodix_coverage_observe (tracker, image, 64);
    if ((double) clock () / CLOCKS_PER_SEC - step_start > maximum)
      maximum = (double) clock () / CLOCKS_PER_SEC - step_start;
    report (i < 12 ? "benchmark" : "anchor-bank stress", r);
    if (r->status == GOODIX_COVERAGE_ADDED || r->status == GOODIX_COVERAGE_REPEATED) {
      assert (hypot (r->x - 256 - px, r->y - 256 - (i / 4) * 18) < 3);
      assert (fabs (r->angle - (i % 3) * 4.5) < 3);
    }
    if (r->status != GOODIX_COVERAGE_ADDED && r->status != GOODIX_COVERAGE_INITIAL) {
      assert (r->new_cells == 0 && !memcmp (saved_grid, r->grid, sizeof saved_grid));
    }
    /* This high-correlation frontier capture needs a third registration hop.
     * It must remain unmapped even though the local match itself is strong. */
    if (i == 4 || i == 10 || i == 11) {
      assert (r->score > .98 && r->margin > .10);
      assert (r->status == GOODIX_COVERAGE_UNALIGNED && r->new_cells == 0);
    }
    assert (goodix_coverage_test_anchor_count (tracker) <= 12);
    if (i == 11)
      printf ("Twelve-frame synthetic CPU benchmark: total %.3fs, maximum frame %.3fs; %u aligned of %u.\n",
              (double) clock () / CLOCKS_PER_SEC - start, maximum, r->aligned_count, r->frame_count);
  }
  printf ("Twenty-frame bank stress: total %.3fs, maximum frame %.3fs; %u anchors (limit12).\n",
          (double) clock () / CLOCKS_PER_SEC - start, maximum, goodix_coverage_test_anchor_count (tracker));
  assert (r->frame_count == 20 && r->aligned_count >= 6);
  /* Conservative two-hop growth can fill the trusted neighborhood before
   * filling all12 slots. The bank never exceeds its fixed upper bound. */
  assert (goodix_coverage_test_anchor_count (tracker) <= 12);
  /* Exercise the worst fixed-bank search cost without weakening the two-hop
   * growth gate merely to manufacture12 naturally banked frames. These are
   * synthetic duplicate anchors seeded through a test-only entry point. */
  goodix_coverage_reset (tracker);
  frame (image, 0, 0, 0, 0, 1, 0, 0);
  goodix_coverage_observe (tracker, image, 64);
  goodix_coverage_test_fill_anchor_bank (tracker);
  start = (double) clock () / CLOCKS_PER_SEC;
  r = goodix_coverage_observe (tracker, image, 64);
  printf ("Twelve-anchor search stress (synthetic test-only bank seed): %.3fs.\n",
          (double) clock () / CLOCKS_PER_SEC - start);
  assert (goodix_coverage_test_anchor_count (tracker) == 12);
  assert (r->status == GOODIX_COVERAGE_REPEATED && r->new_cells == 0);
  goodix_coverage_free (tracker);
}

static void test_invalid_input (void) {
  uint8_t image[64 * 80] = { 0 };
  GoodixCoverage *tracker = goodix_coverage_new ();
  assert (!goodix_coverage_observe (NULL, image, 64));
  assert (!goodix_coverage_result (NULL));
  assert (goodix_coverage_observe (tracker, NULL, 64)->status == GOODIX_COVERAGE_LOW_QUALITY);
  assert (goodix_coverage_observe (tracker, image, 63)->status == GOODIX_COVERAGE_LOW_QUALITY);
  assert (goodix_coverage_observe (tracker, image, SIZE_MAX)->status == GOODIX_COVERAGE_LOW_QUALITY);
  goodix_coverage_reset (NULL);
  goodix_coverage_free (NULL);
  goodix_coverage_free (tracker);
}

int main (void) {
  test_invalid_input ();
  test_repetition ();
  test_translation_rotation ();
  test_rejection ();
  test_rotated_same_region ();
  benchmark ();
  puts ("All synthetic coverage tracker tests passed.");
  return 0;
}
