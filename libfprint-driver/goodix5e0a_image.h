#pragma once
#include <string.h>
#include "goodix5e0a_fdt.h"

/* Subtract a local mean while retaining ridges wider than three pixels.
 * Settings are bounded and validated by the calibration file loader. */
static inline gboolean
goodix5e0a_normalize_configured (const guint16 *pixels, guint8 *output,
                                const Goodix5e0aImageSettings *settings)
{
  const gint width = 64, height = 80, radius = settings->window / 2;
  guint active = 0;
  for (guint i = 0; i < 64*80; i++)
    active += pixels[i] > 30;
  if (active < 64)
    {
      memset (output, 0, 64*80);
      return FALSE;
    }
  /* An integral image gives the same integer window sums as the reference
   * nested loops, while processing every pixel a constant number of times. */
  guint32 integral[81][65] = {{0}};
  for (gint y = 0; y < height; y++)
    {
      guint32 row_sum = 0;
      for (gint x = 0; x < width; x++)
        {
          row_sum += pixels[y*width+x];
          integral[y+1][x+1] = integral[y][x+1] + row_sum;
        }
    }
  for (gint y = 0; y < height; y++)
    for (gint x = 0; x < width; x++)
      {
        gint left = MAX (0, x-radius), right = MIN (width, x+radius+1);
        gint top = MAX (0, y-radius), bottom = MIN (height, y+radius+1);
        guint32 sum = integral[bottom][right] - integral[top][right]
                      - integral[bottom][left] + integral[top][left];
        guint32 count = (right-left) * (bottom-top);
        float residual = pixels[y*width+x] - (float) sum / count;
        gint value = (gint) roundf (settings->midpoint + residual * settings->gain);
        output[y*width+x] = CLAMP (value, 0, 255);
      }
  return TRUE;
}
