/* Offline lifecycle coverage. Compile the current driver, not a copied model.
 * Transport and core completion hooks are replaced; libfprint's SSM and the
 * Goodix base generation/clean-state helpers remain real. No USB calls occur.
 * The driver TU is selected at build time: dev builds default to the repo
 * layout, the Nix lane overrides GOODIX_5E0A_C_INCLUDE to the patched
 * libfprint tree copy (kept identical by the patch-sync test).
 */
#ifndef GOODIX_5E0A_C_INCLUDE
#define GOODIX_5E0A_C_INCLUDE "../../libfprint-driver/goodix5e0a.c"
#endif
#define goodix_tls_is_alive mock_tls_is_alive
#define goodix_tls_init mock_tls_init
#define goodix_shutdown_tls mock_shutdown_tls
#define goodix_start_read_loop mock_start_read_loop
#define goodix_stop_read_loop mock_stop_read_loop
#define goodix_send_nop mock_send_nop
#define goodix_send_query_firmware_version mock_send_query_firmware_version
#define goodix_send_preset_psk_read_5e0a mock_send_preset_psk_read_5e0a
#define goodix_send_upload_config_mcu mock_send_upload_config_mcu
#define goodix_send_enable_chip mock_send_enable_chip
#define goodix_send_protocol mock_send_protocol
#define fpi_device_suspend_complete mock_suspend_complete
#define fpi_device_resume_complete mock_resume_complete
#define fpi_device_action_error mock_action_error
#define fpi_device_get_current_action mock_current_action
#define goodix_milan_enroll_add_image mock_enroll_add_image
#define goodix_milan_enroll_start mock_enroll_start
#define goodix_milan_enroll_finish mock_enroll_finish
#define goodix_dev_deinit mock_dev_deinit
#define fpi_device_close_complete mock_close_complete
#define goodix_coverage_free mock_coverage_free
#define fpi_device_enroll_progress mock_enroll_progress
/* This driver derives from FpDevice, despite its legacy image-device casts. */
#include "fp-image-device.h"
#undef FP_IMAGE_DEVICE
#define FP_IMAGE_DEVICE(dev) ((FpImageDevice *) (dev))
#include GOODIX_5E0A_C_INCLUDE
#undef goodix_coverage_free

/* Forward to the real tracker, while observing that sensitive storage is
 * released on each lifecycle boundary. The driver itself is not copied. */
void goodix_coverage_free (GoodixCoverage *self);

static gboolean tls_alive, reading;
static guint tls_inits, shutdowns, probes, configs, enables, suspends, resumes, errors;
static guint scan_commands;
static GoodixCallbackInfo *scan_command;
static GoodixNoneCallback tls_cb, enable_cb;
static gpointer tls_data, enable_data;
static GoodixCallbackInfo *probe;
static FpiDeviceAction current_action;
static int enroll_result, native_enrolled;
static guint enroll_reports, enroll_retries;
static guint coverage_frees, native_finishes, closes;

void mock_coverage_free (GoodixCoverage *coverage)
{
  coverage_frees++;
  goodix_coverage_free (coverage);
}
void *mock_enroll_start (int *max_images)
{
  if (max_images)
    *max_images = 12;
  return &native_enrolled;
}
void mock_enroll_finish (void *context)
{
  g_assert_true (context == &native_enrolled);
  native_finishes++;
}
gboolean mock_dev_deinit (FpDevice *dev, GError **error)
{
  return TRUE;
}
void mock_close_complete (FpDevice *dev, GError *error)
{
  g_assert_no_error (error);
  closes++;
}

FpiDeviceAction mock_current_action (FpDevice *dev) { return current_action; }
int mock_enroll_add_image (void *ctx, const uint8_t *pixels, int width, int height,
                           int *count, int *pct)
{
  *count = native_enrolled;
  *pct = native_enrolled * 100 / 12;
  return enroll_result;
}
void mock_enroll_progress (FpDevice *dev, gint completed, FpPrint *print, GError *error)
{
  enroll_reports++;
  if (error)
    {
      g_assert_error (error, FP_DEVICE_RETRY, FP_DEVICE_RETRY_CENTER_FINGER);
      enroll_retries++;
      g_error_free (error);
    }
}

/* These mocks retain completions so tests can deliver an orphan after teardown. */
gboolean mock_tls_is_alive (FpDevice *dev) { return tls_alive; }
void mock_tls_init (FpDevice *dev, GoodixNoneCallback cb, gpointer data)
{
  g_assert_false (tls_alive);
  tls_alive = TRUE;
  tls_inits++;
  tls_cb = cb;
  tls_data = data;
}
gboolean mock_shutdown_tls (FpDevice *dev, GError **error)
{
  shutdowns++;
  tls_alive = FALSE;
  return TRUE;
}
void mock_start_read_loop (FpDevice *dev) { reading = TRUE; }
void mock_stop_read_loop (FpDevice *dev) { reading = FALSE; }
void mock_send_nop (FpDevice *dev, GoodixNoneCallback cb, gpointer data)
{ cb (dev, data, NULL); }
void mock_send_query_firmware_version (FpDevice *dev, GoodixFirmwareVersionCallback cb, gpointer data)
{ cb (dev, GOODIX_5E0A_FIRMWARE_VERSION, data, NULL); }
void mock_send_preset_psk_read_5e0a (FpDevice *dev, guint32 flags, guint32 len,
                                    guint32 offset, GoodixPresetPskReadCallback cb, gpointer data)
{ cb (dev, TRUE, flags, NULL, 0, data, NULL); }
void mock_send_upload_config_mcu (FpDevice *dev, guint8 *config, guint16 len,
                                  GDestroyNotify free_func, GoodixSuccessCallback cb, gpointer data)
{
  configs++;
  cb (dev, TRUE, data, NULL);
}
void mock_send_enable_chip (FpDevice *dev, gboolean enabled, GoodixNoneCallback cb, gpointer data)
{
  enables++;
  enable_cb = cb;
  enable_data = data;
}
void mock_send_protocol (FpDevice *dev, guint8 cmd, const guint8 *payload, guint16 len,
                         GDestroyNotify free_func, gboolean checksum, guint timeout,
                         gboolean reply, GoodixCmdCallback cb, gpointer data)
{
  g_assert_cmpuint (cmd, ==, GOODIX_CMD_QUERY_MCU_STATE);
  if (FPI_DEVICE_GOODIXTLS5E0A (dev)->scan_ssm)
    {
      g_assert_null (scan_command);
      scan_command = data;
      scan_commands++;
    }
  else
    {
      g_assert_null (probe);
      probe = data;
      probes++;
    }
}
void mock_suspend_complete (FpDevice *dev, GError *error)
{
  g_assert_error (error, FP_DEVICE_ERROR, FP_DEVICE_ERROR_NOT_SUPPORTED);
  g_error_free (error);
  suspends++;
}
void mock_resume_complete (FpDevice *dev, GError *error)
{ g_assert_no_error (error); resumes++; }
void mock_action_error (FpDevice *dev, GError *error)
{ g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED); g_error_free (error); errors++; }

static FpDevice *new_device (void)
{
  tls_alive = reading = FALSE;
  tls_inits = shutdowns = probes = configs = enables = suspends = resumes = errors = 0;
  scan_commands = 0;
  current_action = FPI_DEVICE_ACTION_NONE;
  enroll_result = native_enrolled = enroll_reports = enroll_retries = 0;
  coverage_frees = native_finishes = closes = 0;
  g_clear_pointer (&probe, g_free);
  g_clear_pointer (&scan_command, g_free);
  FpDeviceClass *klass = g_type_class_ref (fpi_device_goodixtls5e0a_get_type ());
  klass->type = FP_DEVICE_TYPE_VIRTUAL;
  FpDevice *dev = g_object_new (fpi_device_goodixtls5e0a_get_type (), NULL);
  g_type_class_unref (klass);
  return dev;
}

static void test_park_suspend_resume (void)
{
  g_autoptr(FpDevice) dev = new_device ();
  FpiDeviceGoodixTls5e0a *self = FPI_DEVICE_GOODIXTLS5E0A (dev);
  tls_alive = TRUE;
  reading = TRUE;
  self->warm_ok = TRUE;
  goodix5e0a_deactivate ((FpImageDevice *) dev);
  g_assert_true (self->tls_parked);
  g_assert_true (tls_alive);
  g_assert_false (reading);
  g_assert_true (goodix_session_is_clean (dev));
  guint parked_gen = goodix_activation_gen_get (dev);

  goodix5e0a_suspend (dev);
  g_assert_false (self->tls_parked);
  g_assert_false (self->warm_ok);
  g_assert_false (tls_alive);
  g_assert_false (reading);
  g_assert_false (goodix_session_is_clean (dev));
  g_assert_cmpuint (goodix_activation_gen_get (dev), >, parked_gen);
  g_assert_cmpuint (suspends, ==, 1);
  goodix5e0a_resume (dev);
  g_assert_cmpuint (resumes, ==, 1);
  g_assert_false (tls_alive);

  dev_activate ((FpImageDevice *) dev);
  g_assert_cmpuint (tls_inits, ==, 1);
  g_assert_cmpuint (probes, ==, 0);
  g_assert_false (self->warm_attempted);
  g_assert_true (reading);
  tls_cb (dev, tls_data, NULL);
  g_assert_cmpuint (configs, ==, 1);
  g_assert_cmpuint (enables, ==, 1);
  enable_cb (dev, enable_data, NULL);
  g_assert_nonnull (self->scan_ssm);
  g_assert_cmpuint (scan_commands, ==, 1);
  goodix5e0a_suspend (dev);
  g_clear_pointer (&scan_command, g_free);
}

static void test_late_tls_after_cancel (void)
{
  g_autoptr(FpDevice) dev = new_device ();
  FpiDeviceGoodixTls5e0a *self = FPI_DEVICE_GOODIXTLS5E0A (dev);
  dev_activate ((FpImageDevice *) dev);
  GoodixNoneCallback old_cb = tls_cb;
  gpointer old_data = tls_data;
  dev_cancel (dev);
  g_assert_cmpuint (errors, ==, 1);
  g_assert_false (tls_alive);
  goodix5e0a_suspend (dev);
  goodix5e0a_resume (dev);
  dev_activate ((FpImageDevice *) dev);
  g_assert_cmpuint (tls_inits, ==, 2);
  old_cb (dev, old_data, NULL);
  old_cb (dev, old_data, g_error_new_literal (G_IO_ERROR, G_IO_ERROR_CANCELLED, "late TLS"));
  g_assert_cmpuint (configs, ==, 0);
  g_assert_cmpuint (enables, ==, 0);
  g_assert_cmpuint (errors, ==, 1);
  g_assert_null (self->scan_ssm);
  tls_cb (dev, tls_data, NULL);
  g_assert_cmpuint (configs, ==, 1);
  goodix5e0a_suspend (dev);
}

static void test_late_probe_after_suspend (void)
{
  g_autoptr(FpDevice) dev = new_device ();
  FpiDeviceGoodixTls5e0a *self = FPI_DEVICE_GOODIXTLS5E0A (dev);
  tls_alive = self->warm_ok = TRUE;
  goodix5e0a_deactivate ((FpImageDevice *) dev);
  dev_activate ((FpImageDevice *) dev);
  g_assert_cmpuint (probes, ==, 1);
  g_assert_nonnull (probe);
  goodix5e0a_suspend (dev);
  goodix5e0a_resume (dev);
  dev_activate ((FpImageDevice *) dev);
  ((GoodixNoneCallback) probe->callback) (dev, probe->user_data, NULL);
  ((GoodixNoneCallback) probe->callback) (dev, probe->user_data,
      g_error_new_literal (G_IO_ERROR, G_IO_ERROR_TIMED_OUT, "late probe"));
  g_assert_cmpuint (tls_inits, ==, 1);
  g_assert_cmpuint (enables, ==, 0);
  g_assert_cmpuint (errors, ==, 0);
  g_assert_true (tls_alive);
  goodix5e0a_suspend (dev);
  g_clear_pointer (&probe, g_free);
}

static void test_active_scan_cancel (void)
{
  g_autoptr(FpDevice) dev = new_device ();
  FpiDeviceGoodixTls5e0a *self = FPI_DEVICE_GOODIXTLS5E0A (dev);
  dev_activate ((FpImageDevice *) dev);
  tls_cb (dev, tls_data, NULL);
  enable_cb (dev, enable_data, NULL);
  FpiSsm *old_ssm = self->scan_ssm;
  g_assert_nonnull (old_ssm);
  self->down_timeout = g_timeout_source_new (60000);
  GSource *timeout = g_source_ref (self->down_timeout);
  g_source_unref (self->down_timeout);
  self->frame_count = self->best_frame_no = 2;
  self->retry_guard = self->session_started = TRUE;
  guint old_scan_gen = self->scan_gen;
  dev_cancel (dev);
  g_assert_null (self->scan_ssm);
  g_assert_null (self->down_timeout);
  g_assert_true (g_source_is_destroyed (timeout));
  g_source_unref (timeout);
  g_assert_false (tls_alive);
  g_assert_false (self->tls_parked);
  g_assert_false (self->session_started);
  g_assert_false (self->retry_guard);
  g_assert_cmpuint (self->frame_count, ==, 0);
  g_assert_cmpuint (self->best_frame_no, ==, 0);
  g_assert_cmpuint (self->scan_gen, >, old_scan_gen);
  /* The timer guard only compares the orphan pointer, never dereferences it. */
  goodix5e0a_on_down_poll_timeout (dev, old_ssm);
  g_assert_cmpuint (scan_commands, ==, 1);
  g_assert_cmpuint (errors, ==, 1);
  goodix5e0a_suspend (dev);
  goodix5e0a_resume (dev);
  dev_activate ((FpImageDevice *) dev);
  g_assert_cmpuint (tls_inits, ==, 2);
  goodix5e0a_suspend (dev);
  g_clear_pointer (&scan_command, g_free);
}

/* Exercise the public libfprint dispatch too. Virtual type keeps the wakeup
 * path off USB/sysfs. Documents the observed dispatch: idle suspend/resume
 * never reaches the driver hooks, and a stale parked session can only be
 * reused after its health probe succeeds.
 */
static void test_public_idle_suspend (void)
{
  g_autoptr(FpDevice) dev = new_device ();
  FpiDeviceGoodixTls5e0a *self = FPI_DEVICE_GOODIXTLS5E0A (dev);
  tls_alive = self->warm_ok = TRUE;
  goodix5e0a_deactivate ((FpImageDevice *) dev);
  g_assert_true (self->tls_parked);
  g_autoptr(GError) error = NULL;
  g_assert_true (fp_device_suspend_sync (dev, NULL, &error));
  g_assert_no_error (error);
  g_assert_true (fp_device_resume_sync (dev, NULL, &error));
  g_assert_no_error (error);
  /* Observed dispatch (reproducer test_idle_suspend_dispatch_c.c): with no
   * interactive action libfprint completes suspend/resume itself and never
   * calls the driver hooks. Parked TLS therefore survives the cycle. What
   * still protects the claim is the park health probe: dev_activate may not
   * send enable_chip, config, or a full-ladder TLS init while a suspended
   * park is only "candidate fresh". */
  g_assert_cmpuint (suspends, ==, 0);
  g_assert_cmpuint (shutdowns, ==, 0);
  g_assert_true (self->tls_parked);
  g_assert_true (tls_alive);
  dev_activate ((FpImageDevice *) dev);
  g_assert_cmpuint (probes, ==, 1);
  g_assert_cmpuint (configs, ==, 0);
  g_assert_cmpuint (enables, ==, 0);
  g_assert_false (self->tls_parked);
  goodix5e0a_suspend (dev);
}

/* Test the actual normalizer included above, with independently calculated
 * values: an affine ramp has zero interior residual and clipped edge means. */
static void test_frame_normalization (void)
{
  GoodixTls5xxPix raw[64 * 80] = {0};
  guint8 normalized[64 * 80];
  float low, high;

  g_assert_false (goodix5e0a_normalize_raw_frame (raw, normalized, NULL, NULL));
  for (guint i = 0; i < 63; i++)
    raw[i] = 1000;
  g_assert_false (goodix5e0a_normalize_raw_frame (raw, normalized, NULL, NULL));
  raw[63] = 1000;
  g_assert_true (goodix5e0a_normalize_raw_frame (raw, normalized, NULL, NULL));

  for (int y = 0; y < 80; y++)
    for (int x = 0; x < 64; x++)
      raw[y * 64 + x] = 1000 + 6 * x + 12 * y;
  g_assert_true (goodix5e0a_normalize_raw_frame (raw, normalized, &low, &high));
  g_assert_cmpfloat (low, ==, -9.0f);
  g_assert_cmpfloat (high, ==, 9.0f);
  for (int y = 0; y < 80; y++)
    for (int x = 0; x < 64; x++)
      {
        float res = (x == 0 ? -3.0f : x == 63 ? 3.0f : 0.0f)
                  + (y == 0 ? -6.0f : y == 79 ? 6.0f : 0.0f);
        int expected = (int) roundf (GOODIX_5E0A_NORMALIZE_MIDPOINT + res * GOODIX_5E0A_CONTRAST_GAIN);
        g_assert_cmpuint (normalized[y * 64 + x], ==, CLAMP (expected, 0, 255));
      }

  for (guint i = 0; i < G_N_ELEMENTS (raw); i++)
    raw[i] = 1000;
  g_assert_false (goodix5e0a_normalize_raw_frame (raw, normalized, &low, &high));
  g_assert_cmpfloat (low, ==, 0.0f);
  g_assert_cmpfloat (high, ==, 0.0f);
  for (int delta = -900; delta <= 900; delta += 1800)
    {
      raw[40 * 64 + 32] = 1000 + delta;
      g_assert_true (goodix5e0a_normalize_raw_frame (raw, normalized, NULL, NULL));
      g_assert_cmpuint (normalized[40 * 64 + 32], ==, delta < 0 ? 0 : 255);
      int expected = (int) roundf (GOODIX_5E0A_NORMALIZE_MIDPOINT - (delta / 9.0f) * GOODIX_5E0A_CONTRAST_GAIN);
      g_assert_cmpuint (normalized[40 * 64 + 31], ==, CLAMP (expected, 0, 255));
      g_assert_cmpuint (normalized[0], ==, 128);
    }
}

static guint probe_handler_calls, probe_complete_calls;
static GQuark probe_complete_domain;
static gint probe_complete_code;

static void probe_handler (FpiSsm *ssm, FpDevice *dev)
{
  probe_handler_calls++;
}

static void probe_complete (FpiSsm *ssm, FpDevice *dev, GError *error)
{
  probe_complete_calls++;
  if (error)
    {
      probe_complete_domain = error->domain;
      probe_complete_code = error->code;
      g_error_free (error);
    }
  else
    {
      probe_complete_domain = 0;
      probe_complete_code = -1;
    }
}

/* Real-priv teardown: this harness mocks send_protocol, so priv is never
 * armed and goodix_reset_state is a no-op in the cases above. Arm a real
 * priv waiter with goodix_read_tls (no USB needed) whose completion is a
 * live scan callback, then tear down mid-command. A correct teardown
 * abandons the SSM first: park is ineligible and exactly one completion
 * (deactivate_complete / suspend_complete) is reported, never a
 * session_error alongside it. */
static void test_deactivate_mid_command_real_priv (void)
{
  g_autoptr(FpDevice) dev = new_device ();
  FpiDeviceGoodixTls5e0a *self = FPI_DEVICE_GOODIXTLS5E0A (dev);
  dev_activate ((FpImageDevice *) dev);
  tls_cb (dev, tls_data, NULL);
  enable_cb (dev, enable_data, NULL);
  g_assert_nonnull (self->scan_ssm);
  g_assert_true (tls_alive);
  g_assert_true (self->warm_ok);
  reading = TRUE;

  /* FDT_DOWN wait armed at the priv layer with the live scan SSM. */
  goodix_read_tls (dev, goodix5e0a_on_fdt_down_reply, self->scan_ssm);
  goodix5e0a_deactivate ((FpImageDevice *) dev);
  g_assert_null (self->scan_ssm);
  g_assert_false (self->tls_parked);
  g_assert_false (reading);
  g_assert_false (goodix_session_is_clean (dev));
  g_assert_cmpuint (shutdowns, ==, 1);
  g_assert_cmpuint (errors, ==, 0);
}

/* Same armed-priv setup through suspend: one suspend_complete, no error. */
static void test_suspend_mid_command_real_priv (void)
{
  g_autoptr(FpDevice) dev = new_device ();
  FpiDeviceGoodixTls5e0a *self = FPI_DEVICE_GOODIXTLS5E0A (dev);
  dev_activate ((FpImageDevice *) dev);
  tls_cb (dev, tls_data, NULL);
  enable_cb (dev, enable_data, NULL);
  g_assert_nonnull (self->scan_ssm);

  goodix_read_tls (dev, goodix5e0a_on_fdt_down_reply, self->scan_ssm);
  goodix5e0a_suspend (dev);
  g_assert_null (self->scan_ssm);
  g_assert_false (self->tls_parked);
  g_assert_cmpuint (suspends, ==, 1);
  g_assert_cmpuint (errors, ==, 0);
  goodix5e0a_resume (dev);
  g_assert_cmpuint (resumes, ==, 1);
}

/* step_cb on teardown CANCELLED must fail the SSM, never advance it;
 * non-CANCELLED transients stay tolerant and advance. */
static void test_step_cb_cancelled_fails_fast (void)
{
  g_autoptr(FpDevice) dev = new_device ();
  FpiDeviceGoodixTls5e0a *self = FPI_DEVICE_GOODIXTLS5E0A (dev);

  probe_handler_calls = probe_complete_calls = 0;
  FpiSsm *probe = fpi_ssm_new (dev, probe_handler, 3);
  fpi_ssm_start (probe, probe_complete);
  g_assert_cmpuint (probe_handler_calls, ==, 1);
  self->scan_ssm = probe;
  goodix5e0a_step_cb (dev, probe,
                      g_error_new_literal (G_IO_ERROR, G_IO_ERROR_CANCELLED,
                                           "Command cancelled by state reset"));
  g_assert_cmpuint (probe_handler_calls, ==, 1);
  g_assert_cmpuint (probe_complete_calls, ==, 1);
  g_assert_cmpuint (probe_complete_domain, ==, G_IO_ERROR);
  g_assert_cmpint (probe_complete_code, ==, G_IO_ERROR_CANCELLED);
  self->scan_ssm = NULL; /* mark_failed auto-freed the SSM */

  probe_handler_calls = probe_complete_calls = 0;
  probe = fpi_ssm_new (dev, probe_handler, 3);
  fpi_ssm_start (probe, probe_complete);
  self->scan_ssm = probe;
  goodix5e0a_step_cb (dev, probe,
                      g_error_new_literal (G_IO_ERROR, G_IO_ERROR_TIMED_OUT,
                                           "transient timeout"));
  g_assert_cmpuint (probe_handler_calls, ==, 2);
  g_assert_cmpuint (probe_complete_calls, ==, 0);
  fpi_ssm_free (probe);
  self->scan_ssm = NULL;
}

/* fprintd may keep a claim open across attempts. A completed authentication
 * must park just as a clean close does, and the next attempt must still probe
 * that connection and wait for finger release. Exercise both auth actions. */
static void test_completed_auth_reuses_tls (gconstpointer identify)
{
  g_autoptr(FpDevice) dev = new_device ();
  FpiDeviceGoodixTls5e0a *self = FPI_DEVICE_GOODIXTLS5E0A (dev);
  self->is_identify = GPOINTER_TO_INT (identify);
  self->is_verify = !self->is_identify;
  dev_activate ((FpImageDevice *) dev);
  tls_cb (dev, tls_data, NULL);
  enable_cb (dev, enable_data, NULL);
  for (guint i = 0; i < 3; i++)
    {
      g_assert_nonnull (self->scan_ssm);
      self->retry_guard = TRUE;
      self->retry_guard_mono = g_get_monotonic_time ();
      gint64 guard_at = self->retry_guard_mono;
      g_clear_pointer (&scan_command, g_free);
      fpi_ssm_mark_completed (self->scan_ssm);
      g_assert_null (self->scan_ssm);
      g_assert_true (self->tls_parked);
      g_assert_true (tls_alive);
      g_assert_false (reading);
      g_assert_true (goodix_session_is_clean (dev));
      g_assert_true (self->retry_guard);
      g_assert_cmpint (self->retry_guard_mono, ==, guard_at);
      dev_activate ((FpImageDevice *) dev);
      g_assert_cmpuint (tls_inits, ==, 1);
      g_assert_cmpuint (shutdowns, ==, 0);
      g_assert_cmpuint (probes, ==, i + 1);
      g_assert_nonnull (probe);
      g_assert_null (self->scan_ssm); /* no capture before health reply */
      ((GoodixNoneCallback) probe->callback) (dev, probe->user_data, NULL);
      g_clear_pointer (&probe, g_free);
      enable_cb (dev, enable_data, NULL);
      g_assert_true (self->retry_guard);
    }
  dev_cancel (dev);
  g_assert_false (self->tls_parked);
  g_assert_false (tls_alive);
  g_assert_cmpuint (errors, ==, 1);
  g_clear_pointer (&scan_command, g_free);
}

static void test_failed_scan_does_not_park (void)
{
  g_autoptr(FpDevice) dev = new_device ();
  FpiDeviceGoodixTls5e0a *self = FPI_DEVICE_GOODIXTLS5E0A (dev);
  self->is_verify = TRUE;
  dev_activate ((FpImageDevice *) dev);
  tls_cb (dev, tls_data, NULL);
  enable_cb (dev, enable_data, NULL);
  self->retry_guard = TRUE;
  g_clear_pointer (&scan_command, g_free);
  g_test_expect_message (G_LOG_DOMAIN, G_LOG_LEVEL_CRITICAL, "*failed to scan: scan failed*");
  fpi_ssm_mark_failed (self->scan_ssm,
                       g_error_new_literal (G_IO_ERROR, G_IO_ERROR_CANCELLED, "scan failed"));
  g_test_assert_expected_messages ();
  g_assert_null (self->scan_ssm);
  g_assert_false (self->tls_parked);
  g_assert_false (self->warm_ok);
  g_assert_false (self->retry_guard);
  g_assert_false (goodix_session_is_clean (dev));
  g_assert_cmpuint (errors, ==, 1);
  goodix5e0a_deactivate ((FpImageDevice *) dev);
  g_assert_false (tls_alive);
}

static void test_touch_completion_lift_policy (gconstpointer matched)
{
  g_autoptr(FpDevice) dev = new_device ();
  FpiDeviceGoodixTls5e0a *self = FPI_DEVICE_GOODIXTLS5E0A (dev);
  self->is_verify = TRUE;
  dev_activate ((FpImageDevice *) dev);
  tls_cb (dev, tls_data, NULL);
  enable_cb (dev, enable_data, NULL);
  self->auth_matched = GPOINTER_TO_INT (matched);
  g_clear_pointer (&scan_command, g_free);
  goodix5e0a_finish_touch (dev, self->scan_ssm);
  g_assert_true (self->tls_parked);
  g_assert_cmpint (self->retry_guard, ==, !GPOINTER_TO_INT (matched));
  if (matched)
    g_assert_cmpint (self->retry_guard_mono, ==, 0);
  else
    g_assert_cmpint (self->retry_guard_mono, >, 0);
  dev_activate ((FpImageDevice *) dev);
  g_assert_false (self->auth_matched); /* never carry a prior match into a new action */
  g_assert_cmpuint (probes, ==, 1);
  goodix5e0a_suspend (dev);
  g_clear_pointer (&probe, g_free);
}

/* Exercise the real delivery path against an engine that can succeed without
 * adding a new impression. UI progress must follow native additions. */
static void test_enrollment_requires_native_addition (void)
{
  g_autoptr(FpDevice) dev = new_device ();
  FpiDeviceGoodixTls5e0a *self = FPI_DEVICE_GOODIXTLS5E0A (dev);
  current_action = FPI_DEVICE_ACTION_ENROLL;
  self->milan_enrol_ctx = &native_enrolled;
  self->coverage = goodix_coverage_new ();
  self->coverage_session = 12345;
  goodix5e0a_publish_coverage (self);
  g_autoptr(GVariant) pending = g_variant_ref (g_object_get_data (G_OBJECT (dev), GOODIX_COVERAGE_DATA_KEY));
  goodix5e0a_deliver_frame (dev); /* success, but count still zero */
  g_assert_cmpuint (self->enroll_stage, ==, 0);
  g_assert_cmpuint (enroll_retries, ==, 1);
  g_assert_cmpuint (goodix_coverage_result (self->coverage)->frame_count, ==, 0);
  g_assert_true (g_variant_equal (pending, g_object_get_data (G_OBJECT (dev), GOODIX_COVERAGE_DATA_KEY)));
  native_enrolled = 1;
  goodix5e0a_deliver_frame (dev);
  g_assert_cmpuint (self->enroll_stage, ==, 1);
  g_assert_cmpuint (goodix_coverage_result (self->coverage)->frame_count, ==, 1);
  g_autoptr(GVariant) accepted = g_variant_ref (g_object_get_data (G_OBJECT (dev), GOODIX_COVERAGE_DATA_KEY));
  g_assert_false (g_variant_equal (pending, accepted));
  goodix5e0a_deliver_frame (dev); /* duplicate count */
  g_assert_cmpuint (self->enroll_stage, ==, 1);
  g_assert_cmpuint (enroll_retries, ==, 2);
  g_assert_cmpuint (goodix_coverage_result (self->coverage)->frame_count, ==, 1);
  g_assert_true (g_variant_equal (accepted, g_object_get_data (G_OBJECT (dev), GOODIX_COVERAGE_DATA_KEY)));
  native_enrolled = 2;
  enroll_result = 131;
  goodix5e0a_deliver_frame (dev); /* rejected despite changed count */
  g_assert_cmpuint (self->enroll_stage, ==, 1);
  g_assert_cmpuint (goodix_coverage_result (self->coverage)->frame_count, ==, 1);
  g_assert_true (g_variant_equal (accepted, g_object_get_data (G_OBJECT (dev), GOODIX_COVERAGE_DATA_KEY)));
  enroll_result = 0;
  goodix5e0a_deliver_frame (dev);
  g_assert_cmpuint (self->enroll_stage, ==, 2);
  g_assert_cmpuint (goodix_coverage_result (self->coverage)->frame_count, ==, 2);
  g_assert_cmpuint (enroll_reports, ==, 5);
  g_assert_cmpuint (enroll_retries, ==, 3);
  self->milan_enrol_ctx = NULL;
}

/* A deterministic invented ridge texture, never a captured fingerprint. */
static void coverage_fixture (guint8 pixels[64 * 80])
{
  for (guint y = 0; y < 80; y++)
    for (guint x = 0; x < 64; x++)
      {
        double xx = (double) x - 31.5, yy = (double) y - 39.5;
        double phase = .74 * xx + .18 * yy + .0016 * xx * yy + .0018 * yy * yy
                       + 2 * sin (.041 * xx) * cos (.052 * yy)
                       + .65 * sin (.13 * yy + .08 * xx)
                       + atan2 (yy - 9, xx - 12) - atan2 (yy + 22, xx + 18)
                       + atan2 (yy - 31, xx + 8);
        pixels[y * 64 + x] = (guint8) CLAMP (125 + 65 * sin (phase) + 15 * sin (2 * phase + .4)
                                            + 9 * cos (.23 * xx + .19 * yy) * sin (.16 * yy - .09 * xx),
                                            0, 255);
      }
}

static void seed_coverage (FpDevice *dev)
{
  FpiDeviceGoodixTls5e0a *self = FPI_DEVICE_GOODIXTLS5E0A (dev);
  guint8 pixels[64 * 80];
  g_assert_null (self->coverage);
  self->coverage = goodix_coverage_new ();
  self->coverage_session = 24680;
  self->enroll_stage = 1;
  coverage_fixture (pixels);
  const GoodixCoverageResult *result = goodix_coverage_observe (self->coverage, pixels, 64);
  g_assert_cmpuint (result->frame_count, ==, 1);
  g_assert_cmpuint (result->observed_cells, >, 0);
  goodix5e0a_publish_coverage (self);
  g_assert_nonnull (g_object_get_data (G_OBJECT (dev), GOODIX_COVERAGE_DATA_KEY));
}

static void assert_coverage_cleared (FpDevice *dev)
{
  FpiDeviceGoodixTls5e0a *self = FPI_DEVICE_GOODIXTLS5E0A (dev);
  g_assert_null (self->coverage);
  g_assert_cmpuint (self->coverage_session, ==, 0);
  g_assert_null (g_object_get_data (G_OBJECT (dev), GOODIX_COVERAGE_DATA_KEY));
}

static void test_coverage_snapshot_is_coarse_only (void)
{
  g_autoptr(FpDevice) dev = new_device ();
  seed_coverage (dev);
  GVariant *snapshot = g_object_get_data (G_OBJECT (dev), GOODIX_COVERAGE_DATA_KEY);
  const char *allowed[] = { "version", "session", "frame", "state", "aligned", "observed-cells",
                           "new-cells", "grid-size", "cell-pixels", "grid", "x", "y", "angle",
                           "score", "margin", "overlap", "novel-fraction", NULL };
  GVariantIter iter;
  const char *key;
  GVariant *value;
  g_assert_true (g_variant_is_of_type (snapshot, G_VARIANT_TYPE_VARDICT));
  g_variant_iter_init (&iter, snapshot);
  while (g_variant_iter_next (&iter, "{&sv}", &key, &value))
    {
      g_assert_true (g_strv_contains (allowed, key));
      if (g_variant_is_container (value))
        g_assert_cmpstr (key, ==, "grid");
      g_variant_unref (value);
    }
  g_autoptr(GVariant) grid = g_variant_lookup_value (snapshot, "grid", G_VARIANT_TYPE_BYTESTRING);
  g_assert_nonnull (grid);
  gsize length;
  const guint8 *cells = g_variant_get_fixed_array (grid, &length, sizeof (guint8));
  g_assert_cmpuint (length, ==, GOODIX_COVERAGE_GRID_SIZE * GOODIX_COVERAGE_GRID_SIZE);
  for (gsize i = 0; i < length; i++)
    g_assert_cmpuint (cells[i], <=, 1);
}

static void test_coverage_reset_and_new_enrollment (void)
{
  g_autoptr(FpDevice) dev = new_device ();
  FpiDeviceGoodixTls5e0a *self = FPI_DEVICE_GOODIXTLS5E0A (dev);
  seed_coverage (dev);
  goodix_coverage_reset (self->coverage);
  goodix5e0a_publish_coverage (self);
  const GoodixCoverageResult *result = goodix_coverage_result (self->coverage);
  g_assert_cmpuint (result->frame_count, ==, 0);
  g_assert_cmpuint (result->observed_cells, ==, 0);
  for (guint i = 0; i < sizeof result->grid; i++)
    g_assert_cmpuint (result->grid[i], ==, 0);
  goodix5e0a_clear_coverage (self);
  seed_coverage (dev);
  guint freed_before = coverage_frees;
  current_action = FPI_DEVICE_ACTION_ENROLL;
  dev_enroll (dev); /* Real new-session entry point, with mocked Milan start. */
  g_assert_cmpuint (coverage_frees, ==, freed_before + 1);
  g_assert_cmpuint (self->enroll_stage, ==, 0);
  g_assert_cmpuint (self->coverage_session, !=, 24680);
  result = goodix_coverage_result (self->coverage);
  g_assert_cmpuint (result->frame_count, ==, 0);
  g_assert_cmpuint (result->observed_cells, ==, 0);
  for (guint i = 0; i < sizeof result->grid; i++)
    g_assert_cmpuint (result->grid[i], ==, 0);
  dev_cancel (dev);
  assert_coverage_cleared (dev);
  g_assert_cmpuint (native_finishes, ==, 1);
}

static void (*saved_parent_finalize) (GObject *object);
static guint parent_finalize_checks;

static void check_parent_finalize (GObject *object)
{
  assert_coverage_cleared (FP_DEVICE (object));
  parent_finalize_checks++;
  saved_parent_finalize (object);
}

static void test_coverage_lifecycle_cleanup (gconstpointer action)
{
  FpDevice *dev = new_device ();
  FpiDeviceGoodixTls5e0a *self = FPI_DEVICE_GOODIXTLS5E0A (dev);
  seed_coverage (dev);
  const char *name = action;
  if (g_str_equal (name, "cancel"))
    dev_cancel (dev);
  else if (g_str_equal (name, "suspend"))
    goodix5e0a_suspend (dev);
  else if (g_str_equal (name, "scan-error"))
    {
      g_test_expect_message (G_LOG_DOMAIN, G_LOG_LEVEL_CRITICAL, "*failed to scan: coverage test failure*");
      goodix5e0a_scan_complete (NULL, dev,
                               g_error_new_literal (G_IO_ERROR, G_IO_ERROR_CANCELLED, "coverage test failure"));
      g_test_assert_expected_messages ();
    }
  else if (g_str_equal (name, "close"))
    {
      dev_close (dev);
      g_assert_cmpuint (closes, ==, 1);
    }
  else if (g_str_equal (name, "finalize"))
    {
      /* Observe state at the actual parent-finalize boundary while object
       * storage is still valid, rather than reading freed memory. */
      GObjectClass *parent = G_OBJECT_CLASS (fpi_device_goodixtls5e0a_parent_class);
      saved_parent_finalize = parent->finalize;
      parent_finalize_checks = 0;
      parent->finalize = check_parent_finalize;
      g_object_unref (dev);
      parent->finalize = saved_parent_finalize;
      g_assert_cmpuint (parent_finalize_checks, ==, 1);
      g_assert_cmpuint (coverage_frees, ==, 1);
      return;
    }
  else
    g_assert_not_reached ();
  assert_coverage_cleared (dev);
  g_assert_cmpuint (coverage_frees, ==, 1);
  g_assert_null (self->milan_enrol_ctx);
  g_object_unref (dev);
  g_assert_cmpuint (coverage_frees, ==, 1); /* No double free at finalization. */
}

int main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/goodix/lifecycle/park-suspend-resume", test_park_suspend_resume);
  g_test_add_func ("/goodix/lifecycle/late-tls-after-cancel", test_late_tls_after_cancel);
  g_test_add_func ("/goodix/lifecycle/late-probe-after-suspend", test_late_probe_after_suspend);
  g_test_add_func ("/goodix/lifecycle/active-scan-cancel", test_active_scan_cancel);
  g_test_add_func ("/goodix/lifecycle/public-idle-suspend", test_public_idle_suspend);
  g_test_add_func ("/goodix/frame/normalization", test_frame_normalization);
  g_test_add_func ("/goodix/lifecycle/deactivate-mid-command-real-priv", test_deactivate_mid_command_real_priv);
  g_test_add_func ("/goodix/lifecycle/suspend-mid-command-real-priv", test_suspend_mid_command_real_priv);
  g_test_add_func ("/goodix/lifecycle/step-cb-cancelled-fails-fast", test_step_cb_cancelled_fails_fast);
  g_test_add_data_func ("/goodix/lifecycle/completed-verify-reuses-tls", GINT_TO_POINTER (FALSE), test_completed_auth_reuses_tls);
  g_test_add_data_func ("/goodix/lifecycle/completed-identify-reuses-tls", GINT_TO_POINTER (TRUE), test_completed_auth_reuses_tls);
  g_test_add_func ("/goodix/lifecycle/failed-scan-does-not-park", test_failed_scan_does_not_park);
  g_test_add_data_func ("/goodix/lifecycle/match-no-lift-guard", GINT_TO_POINTER (TRUE), test_touch_completion_lift_policy);
  g_test_add_data_func ("/goodix/lifecycle/no-match-keeps-lift-guard", GINT_TO_POINTER (FALSE), test_touch_completion_lift_policy);
  g_test_add_func ("/goodix/lifecycle/enrollment-requires-native-addition", test_enrollment_requires_native_addition);
  g_test_add_func ("/goodix/coverage/snapshot-is-coarse-only", test_coverage_snapshot_is_coarse_only);
  g_test_add_func ("/goodix/coverage/reset-and-new-enrollment", test_coverage_reset_and_new_enrollment);
  g_test_add_data_func ("/goodix/coverage/cleanup-cancel", "cancel", test_coverage_lifecycle_cleanup);
  g_test_add_data_func ("/goodix/coverage/cleanup-suspend", "suspend", test_coverage_lifecycle_cleanup);
  g_test_add_data_func ("/goodix/coverage/cleanup-scan-error", "scan-error", test_coverage_lifecycle_cleanup);
  g_test_add_data_func ("/goodix/coverage/cleanup-close", "close", test_coverage_lifecycle_cleanup);
  g_test_add_data_func ("/goodix/coverage/cleanup-finalize", "finalize", test_coverage_lifecycle_cleanup);
  return g_test_run ();
}
