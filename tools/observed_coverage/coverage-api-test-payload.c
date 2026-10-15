/* SPDX-License-Identifier: GPL-3.0-or-later
 * TEST ONLY: inject a synthetic, non-biometric payload for the new D-Bus method.
 * Never install or preload this library in the real system daemon.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <glib-object.h>
#include <string.h>

gpointer
g_object_get_data (GObject *object, const gchar *key)
{
  typedef gpointer (*RealGetData) (GObject *, const gchar *);
  static RealGetData real_get_data;
  static gsize payload_initialized;
  static GVariant *payload;

  if (!real_get_data)
    real_get_data = (RealGetData) dlsym (RTLD_NEXT, "g_object_get_data");

  if (key && strcmp (key, "goodix-5e0a-coverage-v1") == 0)
    {
      if (g_once_init_enter (&payload_initialized))
        {
          GVariantBuilder builder;
          g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
          g_variant_builder_add (&builder, "{sv}", "test-only-sentinel",
                                 g_variant_new_string ("synthetic coarse map; no fingerprint data"));
          payload = g_variant_ref_sink (g_variant_builder_end (&builder));
          g_once_init_leave (&payload_initialized, 1);
        }
      return payload;
    }
  return real_get_data (object, key);
}
