#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include "goodix5e0a_fdt.h"
#include "goodix5e0a_image.h"

int main (void)
{
  /* Actual untouched reply: nonzero baseline must not become contact. */
  guint8 air[16] = {0x80,0,0,0,0xc4,0,0xf6,0,0xb5,0,0xdd,0,0xc0,0,0xdb,0};
  g_assert_false (goodix5e0a_has_contact (air, sizeof air));
  /* Actual calibrated contact reply from the hardware experiment. */
  guint8 touch[16] = {2,0,0x27,0,0x28,0,0x43,0,0x39,0,0x64,0,0x57,0,0x4e,0};
  g_assert_true (goodix5e0a_has_contact (touch, sizeof touch));
  g_assert_false (goodix5e0a_has_contact (touch, 4));
  touch[2] = 0xff;
  g_assert_false (goodix5e0a_has_contact (touch, sizeof touch));
  g_assert_false (goodix5e0a_has_contact (NULL, 16));
  g_autofree gchar *dir = g_dir_make_tmp ("goodix-fdt-test-XXXXXX", NULL);
  g_autofree gchar *path = g_build_filename (dir, "calibration.conf", NULL);
  guint8 down[35], up[35];
  memset (down, 0x11, sizeof down);
  memset (up, 0x22, sizeof up);
  g_autoptr(GError) error = NULL;
  g_assert_true (goodix5e0a_load_fdt_override (path, down, up, &error));
  const char *invalid[] = {
    "[FDT]\nDown=50;72;\nUp=55;77;55;64;66;69;\n",
    "[FDT]\nDown=50;72;50;59;61;64;\nUp=55;77;55;64;66;999;\n",
    "[FDT]\nDown=50;72;50;59;61;64;\nUp=55;77;55;64;66;64;\n",
    "[FDT]\nDown=50;72;50;59;61;-1;\nUp=55;77;55;64;66;69;\n",
    "[FDT]\nDown=50;72;50;59;61;64;\n",
    "[FDT]\nDown=50;72;50;59;61;64;\nUp=55;77;55;64;66;69;\nDAC=167;168;166;999;\n",
  };
  for (guint i = 0; i < G_N_ELEMENTS (invalid); i++)
    {
      g_assert_true (g_file_set_contents (path, invalid[i], -1, &error));
      g_assert_false (goodix5e0a_load_fdt_override (path, down, up, &error));
      g_assert_nonnull (error);
      for (guint j = 0; j < 35; j++)
        { g_assert_cmpint (down[j], ==, 0x11); g_assert_cmpint (up[j], ==, 0x22); }
      g_clear_error (&error);
    }
  const char *valid = "[FDT]\nDown=50;72;50;59;61;64;\nUp=55;77;55;64;66;69;\nDAC=167;168;166;167;\n";
  g_assert_true (g_file_set_contents (path, valid, -1, &error));
  g_assert_true (goodix5e0a_load_fdt_override (path, down, up, &error));
  g_assert_no_error (error);
  g_assert_cmpint (down[11], ==, 50);
  g_assert_cmpint (up[21], ==, 69);
  g_assert_cmpint(down[2], ==, 167);
  g_assert_cmpint(down[26], ==, 167);
  g_assert_cmpint(up[2], ==, 167);
  g_assert_cmpint(up[26], ==, 0x22);
  for (guint j = 0; j < 35; j++)
    if (j < 2 || (j >= 22 && j < 26) || j == 34)
      { g_assert_cmpint (down[j], ==, 0x11); g_assert_cmpint (up[j], ==, 0x22); }
  Goodix5e0aImageSettings settings = {3, 1.5f, 128};
  guint8 image[10] = {5,0,176,0,178,0,176,0,177,0};
  const char *bad_image = "[Image]\nDAC=167;168;166;999;\n";
  g_assert_true (g_file_set_contents (path, bad_image, -1, &error));
  g_assert_false (goodix5e0a_load_image_override (path, image, &settings, &error));
  g_assert_cmpint (image[2], ==, 176);
  g_assert_cmpint (image[8], ==, 177);
  g_clear_error (&error);
  const char *bad_settings[] = {
    "[Image]\nDAC=167;168;166;167;\nWindow=12\n",
    "[Image]\nDAC=167;168;166;167;\nGain=nan\n",
    "[Image]\nDAC=167;168;166;167;\nMidpoint=300\n",
  };
  for (guint i = 0; i < G_N_ELEMENTS (bad_settings); i++)
    {
      g_assert_true (g_file_set_contents (path, bad_settings[i], -1, &error));
      g_assert_false (goodix5e0a_load_image_override (path, image, &settings, &error));
      g_assert_cmpint (settings.window, ==, 3);
      g_assert_cmpfloat (settings.gain, ==, 1.5f);
      g_assert_cmpint (image[2], ==, 176);
      g_clear_error (&error);
    }
  const char *valid_image = "[Image]\nDAC=167;168;166;167;\nWindow=13\nGain=0.25\nMidpoint=100\n";
  g_assert_true (g_file_set_contents (path, valid_image, -1, &error));
  g_assert_true (goodix5e0a_load_image_override (path, image, &settings, &error));
  g_assert_cmpint (image[0], ==, 5);
  g_assert_cmpint (image[2], ==, 167);
  g_assert_cmpint (image[4], ==, 168);
  g_assert_cmpint (image[6], ==, 166);
  g_assert_cmpint (image[8], ==, 167);
  g_assert_cmpint (settings.window, ==, 13);
  g_assert_cmpfloat (settings.gain, ==, .25f);
  g_assert_cmpint (settings.midpoint, ==, 100);
  guint16 pixels[64*80];
  guint8 output[64*80];
  for (guint i = 0; i < 64*80; i++) pixels[i] = 100;
  g_assert_true (goodix5e0a_normalize_configured (pixels, output, &settings));
  for (guint i = 0; i < 64*80; i++) g_assert_cmpint (output[i], ==, 100);
  memset(pixels, 0, sizeof pixels);
  g_assert_false (goodix5e0a_normalize_configured (pixels, output, &settings));
  g_remove (path);
  g_rmdir (dir);
  g_print ("FDT contact fixtures and calibration validation passed.\n");
  return 0;
}
