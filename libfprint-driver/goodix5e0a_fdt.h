/* Optional per-unit FDT calibration. Values change volatile thresholds only. */
#pragma once
#include <glib.h>
#include <math.h>

typedef struct {
  gint window;
  gfloat gain;
  gint midpoint;
} Goodix5e0aImageSettings;

static inline gboolean
goodix5e0a_load_image_override (const char *path, guint8 payload[10],
                               Goodix5e0aImageSettings *settings, GError **error)
{
  g_autoptr(GKeyFile) file = g_key_file_new ();
  g_autoptr(GError) local_error = NULL;
  if (!g_key_file_load_from_file (file, path, G_KEY_FILE_NONE, &local_error))
    {
      if (g_error_matches (local_error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
        return TRUE;
      g_propagate_error (error, g_steal_pointer (&local_error));
      return FALSE;
    }
  if (!g_key_file_has_group (file, "Image"))
    return TRUE;
  gsize count = 0;
  g_autofree gint *dac = g_key_file_get_integer_list (file, "Image", "DAC", &count, error);
  if (!dac)
    return FALSE;
  if (count != 4)
    {
      g_set_error_literal (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE,
                           "Image calibration requires four DAC values");
      return FALSE;
    }
  for (guint i = 0; i < 4; i++)
    if (dac[i] < 1 || dac[i] > 255)
      {
        g_set_error_literal (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE,
                             "Image DAC values must be between 1 and 255");
        return FALSE;
      }
  Goodix5e0aImageSettings proposed = *settings;
  if (g_key_file_has_key (file, "Image", "Window", NULL))
    proposed.window = g_key_file_get_integer (file, "Image", "Window", &local_error);
  if (!local_error && g_key_file_has_key (file, "Image", "Gain", NULL))
    proposed.gain = g_key_file_get_double (file, "Image", "Gain", &local_error);
  if (!local_error && g_key_file_has_key (file, "Image", "Midpoint", NULL))
    proposed.midpoint = g_key_file_get_integer (file, "Image", "Midpoint", &local_error);
  if (local_error)
    {
      g_propagate_error (error, g_steal_pointer (&local_error));
      return FALSE;
    }
  if (proposed.window < 3 || proposed.window > 31 || proposed.window % 2 == 0 ||
      !isfinite (proposed.gain) || proposed.gain < 0.1f || proposed.gain > 8.f ||
      proposed.midpoint < 16 || proposed.midpoint > 240)
    {
      g_set_error_literal (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE,
                           "Invalid image normalization settings");
      return FALSE;
    }
  for (guint i = 0; i < 4; i++)
    {
      payload[2 + 2*i] = dac[i];
      payload[3 + 2*i] = 0;
    }
  *settings = proposed;
  return TRUE;
}

static inline gboolean
goodix5e0a_has_contact (const guint8 *data, guint16 len)
{
  if (!data || len < 16 || data[2] == 0xff || (data[2] & 0x3f) == 0)
    return FALSE;
  guint32 energy = 0;
  for (guint i = 4; i + 1 < len; i += 2)
    energy += (guint32) data[i] | ((guint32) data[i + 1] << 8);
  return energy > 0;
}

static inline gboolean
goodix5e0a_load_fdt_override (const char *path, guint8 down[35], guint8 up[35], GError **error)
{
  g_autoptr(GKeyFile) file = g_key_file_new ();
  g_autoptr(GError) local_error = NULL;
  if (!g_key_file_load_from_file (file, path, G_KEY_FILE_NONE, &local_error))
    {
      if (g_error_matches (local_error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
        return TRUE;
      g_propagate_error (error, g_steal_pointer (&local_error));
      return FALSE;
    }
  gsize down_count = 0, up_count = 0;
  g_autofree gint *d = g_key_file_get_integer_list (file, "FDT", "Down", &down_count, error);
  if (!d)
    return FALSE;
  g_autofree gint *u = g_key_file_get_integer_list (file, "FDT", "Up", &up_count, error);
  if (!u)
    return FALSE;
  if (down_count != 6 || up_count != 6)
    {
      g_set_error_literal (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE,
                           "FDT calibration requires six Down and six Up values");
      return FALSE;
    }
  for (guint i = 0; i < 6; i++)
    if (d[i] < 1 || d[i] > 254 || u[i] <= d[i] || u[i] > 254)
      {
        g_set_error_literal (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE,
                             "FDT calibration must satisfy 1 <= Down < Up <= 254");
        return FALSE;
      }
  g_autofree gint *dac = NULL;
  if (g_key_file_has_key (file, "FDT", "DAC", NULL))
    {
      gsize count = 0;
      dac = g_key_file_get_integer_list (file, "FDT", "DAC", &count, error);
      if (!dac)
        return FALSE;
      gboolean valid = count == 4;
      for (gsize i = 0; i < count; i++)
        valid &= dac[i] >= 1 && dac[i] <= 255;
      if (!valid)
        {
          g_set_error_literal (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE,
                               "FDT DAC requires four values between 1 and 255");
          return FALSE;
        }
    }
  /* Validate the entire file before changing either command. */
  for (guint i = 0; i < 6; i++)
    {
      down[10 + 2*i] = up[10 + 2*i] = 0x80;
      down[11 + 2*i] = d[i];
      up[11 + 2*i] = u[i];
    }
  if (dac)
    for (guint i = 0; i < 4; i++)
      {
        down[2 + 2*i] = up[2 + 2*i] = down[26 + 2*i] = dac[i];
        down[3 + 2*i] = up[3 + 2*i] = down[27 + 2*i] = 0;
      }
  g_message ("5e0a loaded per-unit FDT calibration from %s", path);
  return TRUE;
}
