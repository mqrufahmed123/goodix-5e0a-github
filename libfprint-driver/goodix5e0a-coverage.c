/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Conservative, advisory registration of actually observed ridge texture.
 * This is not a fingerprint matcher, anatomical classifier, or coverage
 * certificate. Uncertain frames never change the map. */
#include "goodix5e0a-coverage.h"

#include <math.h>
#ifdef GOODIX_COVERAGE_DEBUG
#include <stdio.h>
#endif
#include <stdlib.h>
#include <string.h>

#define W GOODIX_COVERAGE_WIDTH
#define H GOODIX_COVERAGE_HEIGHT
#define N (W * H)
#define MAP 512
#define GRID GOODIX_COVERAGE_GRID_SIZE
#define ANCHORS 12
#define CANDIDATES 24
#define PI 3.14159265358979323846
#define MIN_OVERLAP .44
#define MIN_SCORE .90
#define MIN_MARGIN .045
#define MIN_NOVEL .08

typedef struct {
  float value[N];
  uint8_t mask[N];
  unsigned int valid;
  double x, y, angle;
  unsigned int depth;
} Image;

typedef struct {
  double dx, dy, angle, score, overlap;
  unsigned int anchor;
} Candidate;

struct GoodixCoverage {
  GoodixCoverageResult result;
  Image images[ANCHORS];
  unsigned int anchors;
  uint8_t observed[MAP * MAP];
};

static void
erase (void *data, size_t length)
{
  volatile unsigned char *p = data;
  while (length--)
    *p++ = 0;
}

const char *
goodix_coverage_status_name (GoodixCoverageStatus status)
{
  static const char *const names[] = {
    "initial", "added", "repeated", "unaligned", "low-quality", "map-full"
  };
  return (unsigned int) status < sizeof names / sizeof names[0] ? names[status] : "unaligned";
}

GoodixCoverage *
goodix_coverage_new (void)
{
  GoodixCoverage *self = calloc (1, sizeof *self);
  if (self)
    goodix_coverage_reset (self);
  return self;
}

void
goodix_coverage_reset (GoodixCoverage *self)
{
  if (!self)
    return;
  erase (self, sizeof *self);
  self->result.status = GOODIX_COVERAGE_UNALIGNED;
  self->result.x = self->result.y = GOODIX_COVERAGE_MAP_ORIGIN;
}

void
goodix_coverage_free (GoodixCoverage *self)
{
  if (self) {
    erase (self, sizeof *self);
    free (self);
  }
}

const GoodixCoverageResult *
goodix_coverage_result (const GoodixCoverage *self)
{
  return self ? &self->result : NULL;
}

static double
rect (const double *integral, int x0, int y0, int x1, int y1)
{
  return integral[y1 * (W + 1) + x1] - integral[y0 * (W + 1) + x1]
         - integral[y1 * (W + 1) + x0] + integral[y0 * (W + 1) + x0];
}

/* A3x3 low pass suppresses sensor noise; removing the9x9 local mean makes
 * registration insensitive to ordinary pressure/brightness changes. Local
 * gradient coherence excludes flat areas and predominantly unstructured noise.
 * The six-pixel border is never credited as observed texture. */
static int
prepare (Image *image, const uint8_t *frame, size_t stride)
{
  float smooth[N];
  double sums[(W + 1) * (H + 1)] = { 0 };
  double squares[(W + 1) * (H + 1)] = { 0 };
  double xx[(W + 1) * (H + 1)] = { 0 };
  double yy[(W + 1) * (H + 1)] = { 0 };
  double xy[(W + 1) * (H + 1)] = { 0 };
  uint8_t rawmask[N] = { 0 };
  double coherence_sum = 0;
  unsigned int quality_samples = 0;
  unsigned int blocks[9] = { 0 };
  int x, y, i, j, goodblocks = 0;

  memset (image, 0, sizeof *image);
  for (y = 0; y < H; y++)
    for (x = 0; x < W; x++) {
      double value = 0;
      for (j = -1; j <= 1; j++)
        for (i = -1; i <= 1; i++) {
          int sx = x + i < 0 ? 0 : x + i >= W ? W - 1 : x + i;
          int sy = y + j < 0 ? 0 : y + j >= H ? H - 1 : y + j;
          value += frame[(size_t) sy * stride + sx] * (i ? 1 : 2) * (j ? 1 : 2);
        }
      smooth[y * W + x] = (float) (value / 16.0);
    }

  for (y = 0; y < H; y++)
    for (x = 0; x < W; x++) {
      int p = (y + 1) * (W + 1) + x + 1;
      double v = smooth[y * W + x];
      double gx = x && x < W - 1 ? smooth[y * W + x + 1] - smooth[y * W + x - 1] : 0;
      double gy = y && y < H - 1 ? smooth[(y + 1) * W + x] - smooth[(y - 1) * W + x] : 0;
#define ADD_INTEGRAL(a, v) a[p] = (v) + a[p - 1] + a[p - (W + 1)] - a[p - (W + 2)]
      ADD_INTEGRAL (sums, v);
      ADD_INTEGRAL (squares, v * v);
      ADD_INTEGRAL (xx, gx * gx);
      ADD_INTEGRAL (yy, gy * gy);
      ADD_INTEGRAL (xy, gx * gy);
#undef ADD_INTEGRAL
    }

  for (y = 5; y < H - 5; y++)
    for (x = 5; x < W - 5; x++) {
      double mean = rect (sums, x - 4, y - 4, x + 5, y + 5) / 81;
      double variance = rect (squares, x - 4, y - 4, x + 5, y + 5) / 81 - mean * mean;
      double a = rect (xx, x - 4, y - 4, x + 5, y + 5);
      double b = rect (yy, x - 4, y - 4, x + 5, y + 5);
      double c = rect (xy, x - 4, y - 4, x + 5, y + 5);
      double coherence = sqrt ((a - b) * (a - b) + 4 * c * c) / (a + b + 1e-9);
      image->value[y * W + x] = (float) (smooth[y * W + x] - mean);
      if (variance >= 20 && a + b >= 810) {
        coherence_sum += coherence;
        quality_samples++;
        rawmask[y * W + x] = coherence >= .30;
      }
    }

  /* Erosion avoids counting uncertain mask boundaries and resampling fringes. */
  for (y = 6; y < H - 6; y++)
    for (x = 6; x < W - 6; x++) {
      int valid = 1;
      for (j = -1; j <= 1 && valid; j++)
        for (i = -1; i <= 1; i++)
          if (!rawmask[(y + j) * W + x + i]) {
            valid = 0;
            break;
          }
      if (valid) {
        image->mask[y * W + x] = 1;
        image->valid++;
        blocks[(y * 3 / H) * 3 + x * 3 / W]++;
      }
    }
  for (i = 0; i < 9; i++)
    goodblocks += blocks[i] >= 65;
  erase (smooth, sizeof smooth);
  erase (sums, sizeof sums);
  erase (squares, sizeof squares);
  erase (xx, sizeof xx);
  erase (yy, sizeof yy);
  erase (xy, sizeof xy);
  erase (rawmask, sizeof rawmask);
  return image->valid >= 1250 && goodblocks >= 6 && quality_samples && coherence_sum / quality_samples >= .52;
}

typedef struct {
  double a, b, aa, bb, ab;
  unsigned int count;
} Moments;

static double
correlation (const Moments *m)
{
  double va, vb;
  if (m->count < 12)
    return -1;
  va = m->aa - m->a * m->a / m->count;
  vb = m->bb - m->b * m->b / m->count;
  if (va <= 1e-6 || vb <= 1e-6)
    return -1;
  return (m->ab - m->a * m->b / m->count) / sqrt (va * vb);
}

static void
add_moment (Moments *m, double a, double b)
{
  m->a += a;
  m->b += b;
  m->aa += a * a;
  m->bb += b * b;
  m->ab += a * b;
  m->count++;
}

/* Compare only mutually textured samples. The optional block check requires
 * agreement in at least three quadrants, spanning both axes; a narrow matching
 * ridge strip cannot establish a2D pose. */
static double
compare (const Image *query, const Image *anchor, double dx, double dy,
         double angle, int step, int blocks_required, double *overlap)
{
  Moments moments = { 0 };
  Moments blocks[4] = { { 0 } };
  unsigned int possible = 0;
  double theta = angle * PI / 180, c = cos (theta), s = sin (theta);
  int x, y, good = 0, rows = 0, columns = 0;
  int minx = W, miny = H, maxx = 0, maxy = 0;

  for (y = 6; y < H - 6; y += step)
    for (x = 6; x < W - 6; x += step) {
      double ax, ay, fx, fy, a, b;
      int ix, iy, p;
      if (!query->mask[y * W + x])
        continue;
      possible++;
      ax = c * (x - (W - 1) / 2.0) - s * (y - (H - 1) / 2.0) + dx + (W - 1) / 2.0;
      ay = s * (x - (W - 1) / 2.0) + c * (y - (H - 1) / 2.0) + dy + (H - 1) / 2.0;
      if (ax < 5 || ay < 5 || ax >= W - 6 || ay >= H - 6)
        continue;
      ix = (int) ax;
      iy = (int) ay;
      p = iy * W + ix;
      if (!anchor->mask[p] || !anchor->mask[p + 1] || !anchor->mask[p + W] || !anchor->mask[p + W + 1])
        continue;
      fx = ax - ix;
      fy = ay - iy;
      a = query->value[y * W + x];
      b = (1 - fy) * ((1 - fx) * anchor->value[p] + fx * anchor->value[p + 1])
          + fy * ((1 - fx) * anchor->value[p + W] + fx * anchor->value[p + W + 1]);
      add_moment (&moments, a, b);
      if (blocks_required) {
        if (x < minx) minx = x;
        if (x > maxx) maxx = x;
        if (y < miny) miny = y;
        if (y > maxy) maxy = y;
      }
    }
  *overlap = possible ? (double) moments.count / possible : 0;
  if (*overlap < MIN_OVERLAP)
    return -1;
  if (blocks_required) {
    /* Split the actual overlap, not the entire sensor: an offset patch may
     * overlap only the upper sensor half while still containing a substantial
     * independently textured2D region. */
    if (maxx - minx < 25 || maxy - miny < 29)
      return -1;
    for (y = 6; y < H - 6; y += step)
      for (x = 6; x < W - 6; x += step) {
        double ax, ay, fx, fy, a, b;
        int ix, iy, p, q;
        if (!query->mask[y * W + x]) continue;
        ax = c * (x - (W - 1) / 2.0) - s * (y - (H - 1) / 2.0) + dx + (W - 1) / 2.0;
        ay = s * (x - (W - 1) / 2.0) + c * (y - (H - 1) / 2.0) + dy + (H - 1) / 2.0;
        if (ax < 5 || ay < 5 || ax >= W - 6 || ay >= H - 6) continue;
        ix = (int) ax;
        iy = (int) ay;
        p = iy * W + ix;
        if (!anchor->mask[p] || !anchor->mask[p + 1] || !anchor->mask[p + W] || !anchor->mask[p + W + 1]) continue;
        fx = ax - ix;
        fy = ay - iy;
        a = query->value[y * W + x];
        b = (1 - fy) * ((1 - fx) * anchor->value[p] + fx * anchor->value[p + 1])
            + fy * ((1 - fx) * anchor->value[p + W] + fx * anchor->value[p + W + 1]);
        q = (y * 2 >= miny + maxy ? 2 : 0) + (x * 2 >= minx + maxx);
        add_moment (&blocks[q], a, b);
      }
    for (x = 0; x < 4; x++)
      if (blocks[x].count >= (unsigned int) (step == 1 ? 95 : 24) && correlation (&blocks[x]) >= .65) {
        good++;
        rows |= 1 << (x / 2);
        columns |= 1 << (x % 2);
      }
    if (good < 3 || rows != 3 || columns != 3)
      return -1;
  }
  return correlation (&moments);
}

static void
world_pose (const GoodixCoverage *self, const Candidate *candidate,
            double *x, double *y, double *angle)
{
  const Image *a = &self->images[candidate->anchor];
  double c = cos (a->angle * PI / 180), s = sin (a->angle * PI / 180);
  *x = a->x + c * candidate->dx - s * candidate->dy;
  *y = a->y + s * candidate->dx + c * candidate->dy;
  *angle = a->angle + candidate->angle;
}

static double
pose_distance (const GoodixCoverage *self, const Candidate *a, const Candidate *b)
{
  double ax, ay, aa, bx, by, ba;
  world_pose (self, a, &ax, &ay, &aa);
  world_pose (self, b, &bx, &by, &ba);
  return hypot (ax - bx, ay - by) + fabs (aa - ba) * PI / 180 * 30;
}

static void
retain (const GoodixCoverage *self, Candidate candidates[CANDIDATES],
        unsigned int *count, Candidate candidate, double separation)
{
  unsigned int i, lowest = 0;
  if (candidate.score < .30)
    return;
  for (i = 0; i < *count; i++) {
    if (pose_distance (self, &candidate, &candidates[i]) < separation) {
      if (candidate.score + .025 * candidate.overlap > candidates[i].score + .025 * candidates[i].overlap)
        candidates[i] = candidate;
      return;
    }
    if (candidates[i].score + .025 * candidates[i].overlap < candidates[lowest].score + .025 * candidates[lowest].overlap)
      lowest = i;
  }
  if (*count < CANDIDATES)
    candidates[(*count)++] = candidate;
  else if (candidate.score + .025 * candidate.overlap > candidates[lowest].score + .025 * candidates[lowest].overlap)
    candidates[lowest] = candidate;
}

static Candidate
refine (const GoodixCoverage *self, const Image *query, Candidate candidate,
        double range, double angle_range, double delta, int step)
{
  Candidate best = candidate, current;
  double x, y, angle;
  best.score = compare (query, &self->images[candidate.anchor], candidate.dx,
                        candidate.dy, candidate.angle, step, 0, &best.overlap);
  for (angle = candidate.angle - angle_range; angle <= candidate.angle + angle_range + 1e-6; angle += delta)
    for (y = candidate.dy - range; y <= candidate.dy + range + 1e-6; y += 1)
      for (x = candidate.dx - range; x <= candidate.dx + range + 1e-6; x += 1) {
        current = candidate;
        current.dx = x;
        current.dy = y;
        current.angle = angle;
        current.score = compare (query, &self->images[candidate.anchor], x, y, angle,
                                 step, 0, &current.overlap);
        if (current.score > best.score)
          best = current;
      }
  return best;
}

static int
periodic (const Image *query)
{
  int x, y;
  double overlap;
  /* A near-perfect nontrivial self-translation cannot locate new area safely.
   * Typical curved or branching real ridges differ well before this threshold. */
  for (y = -16; y <= 16; y++)
    for (x = -16; x <= 16; x++)
      if (x * x + y * y >= 36 &&
          compare (query, query, x, y, 0, 3, 0, &overlap) > .985)
        return 1;
  return 0;
}

static int
register_image (GoodixCoverage *self, const Image *query, Candidate *best,
                double *margin)
{
  Candidate coarse[CANDIDATES], fine[CANDIDATES], candidate;
  unsigned int count = 0, fcount = 0, i, j, winner = 0;
  double x, y, angle, competitor = 0, overlap;

  for (j = 0; j < self->anchors; j++)
    for (angle = -24; angle <= 24; angle += 3)
      for (y = -44; y <= 44; y += 2)
        for (x = -36; x <= 36; x += 2) {
          candidate.dx = x;
          candidate.dy = y;
          candidate.angle = angle;
          candidate.anchor = j;
          candidate.score = compare (query, &self->images[j], x, y, angle, 4, 0, &candidate.overlap);
          retain (self, coarse, &count, candidate, 4);
        }
  if (!count)
    return 0;
  for (i = 0; i < count; i++) {
    candidate = refine (self, query, coarse[i], 3, 3, 1.5, 2);
    candidate = refine (self, query, candidate, 1, 1.5, .75, 2);
    retain (self, fine, &fcount, candidate, 3);
  }
  if (!fcount)
    return 0;
  for (i = 0; i < fcount; i++) {
    fine[i].score = compare (query, &self->images[fine[i].anchor], fine[i].dx,
                             fine[i].dy, fine[i].angle, 1, 1, &fine[i].overlap);
    if (fine[i].score + .025 * fine[i].overlap > fine[winner].score + .025 * fine[winner].overlap)
      winner = i;
  }
  *best = fine[winner];
  if (best->score < MIN_SCORE)
    return 0;
  for (i = 0; i < fcount; i++)
    if (pose_distance (self, best, &fine[i]) >= 6 && fine[i].score > competitor)
      competitor = fine[i].score;

  /* Explicitly revisit one-or-more-ridge translations near the winning pose;
   * the coarse shortlist alone is insufficient for ambiguity estimation. */
  for (y = -18; y <= 18; y += 3)
    for (x = -18; x <= 18; x += 3)
      if (x * x + y * y >= 36) {
        candidate = *best;
        candidate.dx += x;
        candidate.dy += y;
        candidate.score = compare (query, &self->images[best->anchor], candidate.dx,
                                   candidate.dy, candidate.angle, 2, 0, &overlap);
        if (candidate.score > competitor && candidate.score >= .60) {
          candidate = refine (self, query, candidate, 1, .75, .75, 2);
          if (pose_distance (self, best, &candidate) >= 6 && candidate.score > competitor)
            competitor = candidate.score;
        }
      }
  *margin = best->score - competitor;
#ifdef GOODIX_COVERAGE_DEBUG
  fprintf(stderr, "best dx %.3f dy %.3f ang %.3f score %.5f competitor %.5f\n", best->dx,best->dy,best->angle,best->score,competitor);
#endif
  if (*margin < MIN_MARGIN)
    return 0;

  /* Independent reverse comparison must agree at the inverse pose. Its local
   * optimum may move at most1.6px/1.5degrees due to bilinear resampling. */
  {
    double theta = best->angle * PI / 180, c = cos (theta), s = sin (theta);
    double ix = -c * best->dx - s * best->dy;
    double iy = s * best->dx - c * best->dy;
    double inverse_score, inverse_overlap, maximum;
    inverse_score = compare (&self->images[best->anchor], query, ix, iy,
                             -best->angle, 2, 0, &inverse_overlap);
    maximum = inverse_score;
    for (y = -2; y <= 2; y++)
      for (x = -2; x <= 2; x++) {
        double score = compare (&self->images[best->anchor], query, ix + x, iy + y,
                                -best->angle, 2, 0, &overlap);
        if (score > maximum) {
          maximum = score;
          if (x * x + y * y > 2 && score > inverse_score + .025)
            return 0;
        }
      }
    if (inverse_score < .72 || inverse_overlap < MIN_OVERLAP)
      return 0;
  }

  /* Every other sufficiently overlapping anchor must support the same world
   * pose. This closes loops and rejects mutually inconsistent mosaic branches.
   * Distant anchors with insufficient shared texture provide no evidence. */
  {
    double wx, wy, wa;
    world_pose (self, best, &wx, &wy, &wa);
    for (i = 0; i < self->anchors; i++) {
      const Image *anchor = &self->images[i];
      double c = cos (anchor->angle * PI / 180), s = sin (anchor->angle * PI / 180);
      double dx = c * (wx - anchor->x) + s * (wy - anchor->y);
      double dy = -s * (wx - anchor->x) + c * (wy - anchor->y);
      double score = compare (query, anchor, dx, dy, wa - anchor->angle, 2, 0, &overlap);
      if (i != best->anchor && overlap >= .52 && score < .67)
        return 0;
    }
  }
  return 1;
}

/* Project only eroded textured-mask pixels. A4px cell requires12 actually
 * observed pixel positions. Small apparent gains are withheld entirely, so
 * pressure/noise/mask fluctuations cannot slowly fill the canvas. */
static int
update_map (GoodixCoverage *self, const Image *image, double x, double y,
            double angle, int initial, int allow_growth)
{
  uint8_t projected[MAP * MAP] = { 0 };
  uint8_t counts[GRID * GRID] = { 0 };
  unsigned int supported = 0, novel = 0, pixels = 0;
  double c = cos (angle * PI / 180), s = sin (angle * PI / 180);
  int ix, iy;

  for (iy = 6; iy < H - 6; iy++)
    for (ix = 6; ix < W - 6; ix++) {
      double wx, wy;
      int mx, my;
      if (!image->mask[iy * W + ix])
        continue;
      wx = x + c * (ix - (W - 1) / 2.0) - s * (iy - (H - 1) / 2.0);
      wy = y + s * (ix - (W - 1) / 2.0) + c * (iy - (H - 1) / 2.0);
      mx = (int) floor (wx + .5);
      my = (int) floor (wy + .5);
      if (mx < 0 || my < 0 || mx >= MAP || my >= MAP) {
        erase (projected, sizeof projected);
        erase (counts, sizeof counts);
        return -1;
      }
      projected[my * MAP + mx] = 1;
    }
  for (iy = 0; iy < MAP; iy++)
    for (ix = 0; ix < MAP; ix++)
      if (projected[iy * MAP + ix]) {
        unsigned int cell = (iy / 4) * GRID + ix / 4;
        pixels++;
        counts[cell]++;
      }
  for (ix = 0; ix < GRID * GRID; ix++)
    if (counts[ix] >= 12) {
      supported++;
      novel += self->result.grid[ix] == 0;
    }
  self->result.novel_fraction = supported ? (double) novel / supported : 0;
  if (!pixels || !supported || (!initial && (novel < 8 || self->result.novel_fraction < MIN_NOVEL))) {
    erase (projected, sizeof projected);
    erase (counts, sizeof counts);
    return 0;
  }
  if (!allow_growth) {
    erase (projected, sizeof projected);
    erase (counts, sizeof counts);
    return -2;
  }
  for (iy = 0; iy < MAP; iy++)
    for (ix = 0; ix < MAP; ix++)
      if (projected[iy * MAP + ix])
        self->observed[iy * MAP + ix] = 1;
  memset (counts, 0, sizeof counts);
  for (iy = 0; iy < MAP; iy++)
    for (ix = 0; ix < MAP; ix++)
      if (self->observed[iy * MAP + ix])
        counts[(iy / 4) * GRID + ix / 4]++;
  self->result.new_cells = 0;
  for (ix = 0; ix < GRID * GRID; ix++)
    if (counts[ix] >= 12 && !self->result.grid[ix]) {
      self->result.grid[ix] = 1;
      self->result.new_cells++;
      self->result.observed_cells++;
    }
  erase (projected, sizeof projected);
  erase (counts, sizeof counts);
  return 1;
}

const GoodixCoverageResult *
goodix_coverage_observe (GoodixCoverage *self, const uint8_t *frame, size_t stride)
{
  Image image;
  Candidate best;
  double x, y, angle, margin = 0;
  int mapped;
  unsigned int depth = 0, i;

  if (!self)
    return NULL;
  self->result.frame_count++;
  self->result.new_cells = 0;
  self->result.score = self->result.margin = self->result.overlap = self->result.novel_fraction = 0;
  if (!frame || stride < W || stride > SIZE_MAX / (H - 1) || !prepare (&image, frame, stride)) {
    self->result.status = GOODIX_COVERAGE_LOW_QUALITY;
    erase (&image, sizeof image);
    return &self->result;
  }
  if (!self->anchors) {
    if (periodic (&image)) {
      self->result.status = GOODIX_COVERAGE_LOW_QUALITY;
      erase (&image, sizeof image);
      return &self->result;
    }
    x = y = GOODIX_COVERAGE_MAP_ORIGIN;
    angle = 0;
    self->result.score = self->result.margin = self->result.overlap = 1;
  } else {
    memset (&best, 0, sizeof best);
    if (!register_image (self, &image, &best, &margin)) {
      self->result.status = GOODIX_COVERAGE_UNALIGNED;
      self->result.score = best.score;
      self->result.margin = margin;
      self->result.overlap = best.overlap;
      erase (&image, sizeof image);
      return &self->result;
    }
    world_pose (self, &best, &x, &y, &angle);
    self->result.score = best.score;
    self->result.margin = margin;
    self->result.overlap = best.overlap;
  }
  /* Limit unchecked chain drift. Growth needs a strongly matching parent at
   * depth0 or1 from the original patch. A second-generation patch cannot
   * extend the map again. Repeats remain observable without adding anything.
   * This deliberately withholds distant skin rather than presenting an
   * increasingly uncertain chain as measured coverage. */
  if (self->anchors) {
    depth = ANCHORS + 1;
    for (i = 0; i < self->anchors; i++) {
      const Image *a = &self->images[i];
      double c = cos (a->angle * PI / 180), s = sin (a->angle * PI / 180);
      double dx = c * (x - a->x) + s * (y - a->y);
      double dy = -s * (x - a->x) + c * (y - a->y);
      double overlap = 0;
      double score = compare (&image, a, dx, dy, angle - a->angle, 1, 1, &overlap);
      if (overlap >= MIN_OVERLAP && score >= .90 && a->depth + 1 < depth)
        depth = a->depth + 1;
    }
  }
  /* Keeping a fixed anchor bank bounds memory and prevents an unvalidated
   * forgetting policy. Additional repeats remain reportable without banking. */
  mapped = update_map (self, &image, x, y, angle, self->anchors == 0, depth <= 2);
  if (mapped < 0) {
    self->result.status = mapped == -1 ? GOODIX_COVERAGE_MAP_FULL : GOODIX_COVERAGE_UNALIGNED;
    erase (&image, sizeof image);
    return &self->result;
  }
  self->result.x = x;
  self->result.y = y;
  self->result.angle = angle;
  self->result.aligned_count++;
  self->result.status = self->anchors == 0 ? GOODIX_COVERAGE_INITIAL :
                        mapped ? GOODIX_COVERAGE_ADDED : GOODIX_COVERAGE_REPEATED;
  if (mapped && self->anchors < ANCHORS) {
    image.x = x;
    image.y = y;
    image.angle = angle;
    image.depth = depth;
    self->images[self->anchors++] = image;
  }
  erase (&image, sizeof image);
  return &self->result;
}

#ifdef GOODIX_COVERAGE_TESTING
/* White-box bound check for the standalone synthetic harness only. */
unsigned int
goodix_coverage_test_anchor_count (const GoodixCoverage *self)
{
  return self->anchors;
}

void
goodix_coverage_test_fill_anchor_bank (GoodixCoverage *self)
{
  unsigned int i;
  if (!self->anchors)
    return;
  for (i = self->anchors; i < ANCHORS; i++)
    self->images[i] = self->images[0];
  self->anchors = ANCHORS;
}
#endif
