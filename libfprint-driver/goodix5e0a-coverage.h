/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef GOODIX5E0A_COVERAGE_H
#define GOODIX5E0A_COVERAGE_H

#include <stddef.h>
#include <stdint.h>

#define GOODIX_COVERAGE_WIDTH 64
#define GOODIX_COVERAGE_HEIGHT 80
#define GOODIX_COVERAGE_GRID_SIZE 128
#define GOODIX_COVERAGE_CELL_PIXELS 4
#define GOODIX_COVERAGE_MAP_ORIGIN 256

typedef enum {
  GOODIX_COVERAGE_INITIAL,
  GOODIX_COVERAGE_ADDED,
  GOODIX_COVERAGE_REPEATED,
  GOODIX_COVERAGE_UNALIGNED,
  GOODIX_COVERAGE_LOW_QUALITY,
  GOODIX_COVERAGE_MAP_FULL
} GoodixCoverageStatus;

typedef struct {
  GoodixCoverageStatus status;
  /* Image-center pose in the bounded512px map; angle is degrees. Invalid
   * registrations leave the last trusted pose here. These are relative image
   * coordinates, never an anatomical label or a whole-finger percentage. */
  double x, y, angle;
  double score, margin, overlap, novel_fraction;
  unsigned int frame_count, aligned_count, observed_cells, new_cells;
  uint8_t grid[GOODIX_COVERAGE_GRID_SIZE * GOODIX_COVERAGE_GRID_SIZE];
} GoodixCoverageResult;

typedef struct GoodixCoverage GoodixCoverage;

GoodixCoverage *goodix_coverage_new (void);
void goodix_coverage_reset (GoodixCoverage *self);
void goodix_coverage_free (GoodixCoverage *self);
const GoodixCoverageResult *goodix_coverage_result (const GoodixCoverage *self);
/* Input is an accepted, normalized64x80 grayscale frame with stride>=64.
 * All storage is in RAM. No image is exported or written to disk. Returns an
 * internal result pointer valid until the next observe/reset/free operation.
 * Registration is advisory and must not alter biometric match decisions. */
const GoodixCoverageResult *goodix_coverage_observe (GoodixCoverage *self,
                                                  const uint8_t *frame,
                                                  size_t stride);
const char *goodix_coverage_status_name (GoodixCoverageStatus status);

#endif
