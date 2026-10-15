#pragma once
#include <string.h>
#include "goodix5e0a_fdt.h"

/* Subtract a local mean while retaining ridges wider than three pixels.
 * Settings are bounded and validated by the calibration file loader. */
static inline gboolean
goodix5e0a_normalize_reference (const guint16 *pixels, guint8 *output,
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
  for (gint y = 0; y < height; y++)
    for (gint x = 0; x < width; x++)
      {
        guint32 sum = 0, count = 0;
        for (gint yy = MAX (0, y-radius); yy <= MIN (height-1, y+radius); yy++)
          for (gint xx = MAX (0, x-radius); xx <= MIN (width-1, x+radius); xx++)
            { sum += pixels[yy*width+xx]; count++; }
        float residual = pixels[y*width+x] - (float) sum / count;
        gint value = (gint) roundf (settings->midpoint + residual * settings->gain);
        output[y*width+x] = CLAMP (value, 0, 255);
      }
  return TRUE;
}
