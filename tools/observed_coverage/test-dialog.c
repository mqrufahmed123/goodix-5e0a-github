/* SPDX-License-Identifier: GPL-3.0-or-later
 * Compile the actual dialog and guide. No sensor or system fprintd calls.
 */
#define g_dbus_proxy_call mock_coverage_call
#define g_dbus_proxy_call_finish mock_coverage_call_finish
#define cc_fprintd_device_call_enroll_start mock_enroll_start
#define cc_fingerprint_manager_get_devices mock_get_devices
#define cc_fingerprint_manager_update_state mock_update_state
#include CC_FINGERPRINT_GUIDE_SOURCE
#include CC_FINGERPRINT_DIALOG_SOURCE

static GVariant *mock_coverage_payload;
static gboolean mock_coverage_unknown;
static guint mock_coverage_calls;

void
mock_coverage_call (GDBusProxy *proxy, const gchar *method, GVariant *parameters,
                    GDBusCallFlags flags, gint timeout, GCancellable *cancel,
                    GAsyncReadyCallback callback, gpointer data)
{
  GTask *task = g_task_new (proxy, cancel, callback, data);
  g_assert_cmpstr (method, ==, "GetEnrollmentCoverage");
  mock_coverage_calls++;
  if (mock_coverage_unknown)
    g_task_return_error (task, g_dbus_error_new_for_dbus_error ("org.freedesktop.DBus.Error.UnknownMethod", "Method unavailable"));
  else
    g_task_return_pointer (task, g_variant_ref_sink (g_variant_new ("(@a{sv})", g_variant_ref (mock_coverage_payload))), (GDestroyNotify) g_variant_unref);
  g_object_unref (task);
}

GVariant *
mock_coverage_call_finish (GDBusProxy *proxy, GAsyncResult *result, GError **error)
{
  return g_task_propagate_pointer (G_TASK (result), error);
}

static guint start_calls;
extern GResource *cc_system_get_resource (void);

void mock_enroll_start (CcFprintdDevice *proxy, const gchar *finger, GCancellable *cancel,
                        GAsyncReadyCallback callback, gpointer data)
{ start_calls++; }
void mock_get_devices (CcFingerprintManager *manager, GCancellable *cancel,
                       GAsyncReadyCallback callback, gpointer data) {}
void mock_update_state (CcFingerprintManager *manager, CcFingerprintStateUpdated callback, gpointer data) {}

static CcFingerprintDialog *
new_dialog (const char *name, int stages)
{
  CcFingerprintDialog *self;
  start_calls = 0;
  self = g_object_ref_sink (cc_fingerprint_dialog_new (NULL));
  self->device = cc_fprintd_device_skeleton_new ();
  cc_fprintd_device_set_name (self->device, name);
  cc_fprintd_device_set_scan_type (self->device, "press");
  cc_fprintd_device_set_num_enroll_stages (self->device, stages);
  self->dialog_state = DIALOG_STATE_DEVICE_CLAIMED;
  enroll_finger (self, "right-index-finger");
  remove_dialog_state (self, DIALOG_STATE_DEVICE_ENROLL_STARTING);
  g_assert_cmpuint (start_calls, ==, 1);
  return self;
}

static void
free_dialog (CcFingerprintDialog *self)
{
  coverage_stop (self);
  self->dialog_state = DIALOG_STATE_NONE;
  if (gtk_widget_get_root (GTK_WIDGET (self)))
    cc_fingerprint_dialog_close_attempt (ADW_DIALOG (self));
  else
    {
      g_clear_handle_id (&self->enroll_stage_passed_id, g_source_remove);
      g_clear_object (&self->device);
      g_clear_object (&self->cancellable);
      g_clear_object (&self->manager);
    }
  g_object_unref (self);
}

static void
test_stage_retry_and_complete (void)
{
  CcFingerprintDialog *self = new_dialog ("Goodix TLS Fingerprint Sensor 5e0a", 12);
  g_assert_true (gtk_widget_get_visible (GTK_WIDGET (self->enroll_guide)));
  g_assert_false (gtk_widget_get_visible (GTK_WIDGET (self->enroll_result_image)));
  g_assert_cmpuint (self->enroll_guide->accepted, ==, 0);
  g_assert_cmpstr (gtk_label_get_text (self->enroll_guide->title), ==, "Center");
  handle_enroll_signal (self, "enroll-stage-passed", FALSE);
  g_assert_cmpuint (self->enroll_guide->accepted, ==, 1);
  g_assert_cmpstr (gtk_label_get_text (self->enroll_guide->title), ==, "Tip");
  handle_enroll_signal (self, "enroll-retry-scan", FALSE);
  g_assert_cmpuint (self->enroll_guide->accepted, ==, 1);
  g_assert_cmpstr (gtk_label_get_text (self->enroll_guide->title), ==, "Tip");
  g_assert_nonnull (strstr (gtk_label_get_text (self->enroll_guide->instruction), "retry this position"));
  /* fprintd sends the last capture as enroll-completed, not stage-passed. */
  for (guint i = 1; i < 11; i++)
    handle_enroll_signal (self, "enroll-stage-passed", FALSE);
  g_assert_cmpuint (self->enroll_guide->accepted, ==, 11);
  handle_enroll_signal (self, "enroll-completed", TRUE);
  g_assert_cmpuint (self->enroll_guide->accepted, ==, 12);
  g_assert_true (self->enroll_guide->complete);
  g_assert_cmpstr (gtk_label_get_text (self->enroll_guide->title), ==, "Enrollment complete");
  g_assert_true (gtk_widget_get_sensitive (GTK_WIDGET (self->done_button)));
  g_assert_false (gtk_widget_get_sensitive (GTK_WIDGET (self->cancel_button)));
  free_dialog (self);
}

static void
test_duplicate_check_and_twelve_captures (void)
{
  /* The live device advertises 13: one duplicate check and 12 captures. */
  CcFingerprintDialog *self = new_dialog ("Goodix TLS Fingerprint Sensor 5e0a", 13);
  g_assert_true (gtk_widget_get_visible (GTK_WIDGET (self->enroll_guide)));
  g_assert_false (gtk_widget_get_visible (GTK_WIDGET (self->enroll_result_image)));
  g_assert_cmpstr (gtk_label_get_text (self->enroll_guide->title), ==, "Check finger");
  handle_enroll_signal (self, "enroll-retry-scan", FALSE);
  g_assert_cmpuint (self->enroll_guide->accepted, ==, 0);
  g_assert_cmpstr (gtk_label_get_text (self->enroll_guide->title), ==, "Check finger");
  handle_enroll_signal (self, "enroll-stage-passed", FALSE);
  g_assert_cmpuint (self->enroll_guide->accepted, ==, 0);
  g_assert_cmpstr (gtk_label_get_text (self->enroll_guide->title), ==, "Center");
  handle_enroll_signal (self, "enroll-retry-scan", FALSE);
  g_assert_cmpuint (self->enroll_guide->accepted, ==, 0);
  for (guint i = 1; i <= 11; i++)
    {
      handle_enroll_signal (self, "enroll-stage-passed", FALSE);
      g_assert_cmpuint (self->enroll_guide->accepted, ==, i);
    }
  handle_enroll_signal (self, "enroll-completed", TRUE);
  g_assert_cmpuint (self->enroll_guide->accepted, ==, 12);
  g_assert_cmpstr (gtk_label_get_text (self->enroll_guide->count), ==, "12 of 12 touches accepted");
  g_assert_true (self->enroll_guide->complete);
  self->dialog_state = DIALOG_STATE_DEVICE_CLAIMED;
  enroll_finger (self, "right-index-finger");
  g_assert_cmpstr (gtk_label_get_text (self->enroll_guide->title), ==, "Check finger");
  handle_enroll_signal (self, "enroll-duplicate", TRUE);
  g_assert_cmpuint (self->enroll_guide->accepted, ==, 0);
  g_assert_true (self->enroll_guide->failed);
  g_assert_cmpstr (gtk_label_get_text (self->enroll_result_message), ==, "Fingerprint is duplicate");
  free_dialog (self);
}

static void
test_disconnection (void)
{
  CcFingerprintDialog *self = new_dialog ("Goodix TLS Fingerprint Sensor 5e0a", 12);
  handle_enroll_signal (self, "enroll-stage-passed", FALSE);
  handle_enroll_signal (self, "enroll-disconnected", TRUE);
  g_assert_cmpuint (self->enroll_guide->accepted, ==, 1);
  g_assert_true (self->enroll_guide->failed);
  g_assert_cmpstr (gtk_label_get_text (self->enroll_guide->title), ==, "Enrollment stopped");
  free_dialog (self);
}

static void
test_other_devices_and_restart (void)
{
  CcFingerprintDialog *self = new_dialog ("Different fingerprint reader", 5);
  g_assert_false (gtk_widget_get_visible (GTK_WIDGET (self->enroll_guide)));
  g_assert_true (gtk_widget_get_visible (GTK_WIDGET (self->enroll_result_image)));
  self->dialog_state = DIALOG_STATE_DEVICE_CLAIMED;
  cc_fprintd_device_set_name (self->device, "Goodix TLS Fingerprint Sensor 5e0a");
  cc_fprintd_device_set_num_enroll_stages (self->device, 12);
  enroll_finger (self, "right-index-finger");
  g_assert_true (gtk_widget_get_visible (GTK_WIDGET (self->enroll_guide)));
  handle_enroll_signal (self, "enroll-stage-passed", FALSE);
  self->dialog_state = DIALOG_STATE_DEVICE_CLAIMED;
  enroll_finger (self, "right-index-finger");
  g_assert_cmpuint (self->enroll_guide->accepted, ==, 0);
  g_assert_false (self->enroll_guide->complete);
  g_assert_false (self->enroll_guide->failed);
  self->dialog_state = DIALOG_STATE_DEVICE_CLAIMED;
  cc_fprintd_device_set_num_enroll_stages (self->device, 16);
  enroll_finger (self, "right-index-finger");
  g_assert_false (gtk_widget_get_visible (GTK_WIDGET (self->enroll_guide)));
  free_dialog (self);
}

static void
test_reduced_motion_and_cleanup (void)
{
  GtkSettings *settings = gtk_settings_get_default ();
  gboolean original;
  g_object_get (settings, "gtk-enable-animations", &original, NULL);
  g_object_set (settings, "gtk-enable-animations", FALSE, NULL);
  GtkWindow *window = GTK_WINDOW (g_object_ref_sink (gtk_window_new ()));
  CcFingerprintGuide *guide = CC_FINGERPRINT_GUIDE (g_object_ref_sink (cc_fingerprint_guide_new ()));
  gtk_window_set_child (window, GTK_WIDGET (guide));
  gtk_window_present (window);
  while (g_main_context_iteration (NULL, FALSE));
  g_assert_cmpuint (guide->tick_id, ==, 0);
  g_object_set (settings, "gtk-enable-animations", TRUE, NULL);
  g_assert_cmpuint (guide->tick_id, >, 0);
  cc_fingerprint_guide_set_state (guide, 12, 12, TRUE, FALSE, FALSE);
  g_assert_cmpuint (guide->tick_id, ==, 0);
  cc_fingerprint_guide_set_state (guide, 0, 12, FALSE, FALSE, FALSE);
  g_assert_cmpuint (guide->tick_id, >, 0);
  gtk_window_destroy (window);
  g_assert_cmpuint (guide->tick_id, ==, 0);
  g_object_unref (window);
  g_object_unref (guide);
  g_object_set (settings, "gtk-enable-animations", original, NULL);
}

/* Synthetic occupancy fixtures contain no captured fingerprint data. */
static GVariant *
coverage_fixture (guint64 session, guint frame, const char *state,
                  guint aligned, guint columns, guint extra_columns)
{
  GVariantBuilder values;
  guint8 grid[128 * 128] = { 0 };
  guint observed = (columns + extra_columns) * 20;
  for (guint y = 50; y < 70; y++)
    for (guint x = 50; x < 50 + columns + extra_columns; x++)
      grid[y * 128 + x] = 1;
  g_variant_builder_init (&values, G_VARIANT_TYPE_VARDICT);
#define ADD(key, value) g_variant_builder_add (&values, "{sv}", key, value)
  ADD ("version", g_variant_new_uint32 (1));
  ADD ("session", g_variant_new_uint64 (session));
  ADD ("frame", g_variant_new_uint32 (frame));
  ADD ("state", g_variant_new_string (state));
  ADD ("aligned", g_variant_new_uint32 (aligned));
  ADD ("observed-cells", g_variant_new_uint32 (observed));
  ADD ("new-cells", g_variant_new_uint32 (extra_columns * 20));
  ADD ("grid-size", g_variant_new_uint32 (128));
  ADD ("cell-pixels", g_variant_new_uint32 (4));
  ADD ("grid", g_variant_new_fixed_array (G_VARIANT_TYPE_BYTE, grid, sizeof grid, 1));
#undef ADD
  return g_variant_ref_sink (g_variant_builder_end (&values));
}

static void
test_measured_map_and_repeated_area (void)
{
  g_autoptr(CcFingerprintGuide) guide = g_object_ref_sink (CC_FINGERPRINT_GUIDE (cc_fingerprint_guide_new ()));
  g_autoptr(GVariant) pending = coverage_fixture (101, 0, "pending", 0, 0, 0);
  g_assert_true (cc_fingerprint_guide_apply_coverage (guide, pending));
  g_assert_cmpuint (guide->observed_cells, ==, 0);
  g_autoptr(GVariant) first = coverage_fixture (101, 1, "initial", 1, 0, 16);
  g_assert_true (cc_fingerprint_guide_apply_coverage (guide, first));
  g_assert_cmpuint (guide->observed_cells, ==, 320);
  /* A repeated accepted native impression advances count, never area. */
  cc_fingerprint_guide_set_state (guide, 2, 12, FALSE, FALSE, FALSE);
  g_autoptr(GVariant) repeated = coverage_fixture (101, 2, "repeated", 2, 16, 0);
  g_assert_true (cc_fingerprint_guide_apply_coverage (guide, repeated));
  g_assert_cmpuint (guide->accepted, ==, 2);
  g_assert_cmpuint (guide->observed_cells, ==, 320);
  g_assert_nonnull (strstr (gtk_label_get_text (guide->coverage_status), "Repeated area"));
  g_assert_true (cc_fingerprint_guide_apply_coverage (guide, repeated));
  g_autoptr(GVariant) added = coverage_fixture (101, 3, "added", 3, 16, 8);
  g_assert_true (cc_fingerprint_guide_apply_coverage (guide, added));
  g_assert_cmpuint (guide->observed_cells, ==, 480);
  g_assert_cmpuint (guide->new_cells, ==, 160);
}

static void
test_map_rejects_stale_and_mutated_frames (void)
{
  g_autoptr(CcFingerprintGuide) guide = g_object_ref_sink (CC_FINGERPRINT_GUIDE (cc_fingerprint_guide_new ()));
  g_autoptr(GVariant) first = coverage_fixture (201, 2, "added", 2, 0, 20);
  g_assert_true (cc_fingerprint_guide_apply_coverage (guide, first));
  g_autoptr(GVariant) older = coverage_fixture (201, 1, "initial", 1, 0, 16);
  g_assert_false (cc_fingerprint_guide_apply_coverage (guide, older));
  g_autoptr(GVariant) other = coverage_fixture (202, 3, "added", 3, 20, 4);
  g_assert_false (cc_fingerprint_guide_apply_coverage (guide, other));
  g_autoptr(GVariant) mutated = coverage_fixture (201, 2, "added", 2, 0, 24);
  g_assert_false (cc_fingerprint_guide_apply_coverage (guide, mutated));
  g_autoptr(GVariant) shrunk = coverage_fixture (201, 3, "added", 3, 0, 16);
  g_assert_false (cc_fingerprint_guide_apply_coverage (guide, shrunk));
  g_assert_cmpuint (guide->coverage_frame, ==, 2);
  g_assert_cmpuint (guide->observed_cells, ==, 400);
  g_autoptr(GVariant) invalid = coverage_fixture (201, 13, "added", 3, 20, 4);
  g_assert_false (cc_fingerprint_guide_apply_coverage (guide, invalid));
  g_autoptr(GVariant) wrong_state = coverage_fixture (201, 3, "whole-finger", 3, 20, 4);
  g_assert_false (cc_fingerprint_guide_apply_coverage (guide, wrong_state));
}

static void
test_coverage_reset_fallback_and_uncertainty (void)
{
  g_autoptr(CcFingerprintGuide) guide = g_object_ref_sink (CC_FINGERPRINT_GUIDE (cc_fingerprint_guide_new ()));
  g_autoptr(GVariant) first = coverage_fixture (301, 1, "initial", 1, 0, 16);
  g_assert_true (cc_fingerprint_guide_apply_coverage (guide, first));
  g_autoptr(GVariant) unaligned = coverage_fixture (301, 2, "unaligned", 1, 16, 0);
  g_assert_true (cc_fingerprint_guide_apply_coverage (guide, unaligned));
  g_assert_cmpuint (guide->observed_cells, ==, 320);
  g_assert_nonnull (strstr (gtk_label_get_text (guide->coverage_status), "Cannot align"));
  cc_fingerprint_guide_set_coverage_unavailable (guide);
  g_assert_false (guide->coverage_available);
  g_assert_cmpuint (guide->observed_cells, ==, 0);
  g_assert_nonnull (strstr (gtk_label_get_text (guide->coverage_status), "cannot check"));
  cc_fingerprint_guide_reset_coverage (guide);
  g_assert_cmpuint (guide->coverage_session, ==, 0);
  g_assert_cmpuint (guide->coverage_frame, ==, 0);
  g_assert_cmpuint (guide->grid[50 * 128 + 50], ==, 0);
  g_autoptr(GVariant) next = coverage_fixture (302, 1, "initial", 1, 0, 16);
  g_assert_true (cc_fingerprint_guide_apply_coverage (guide, next));
}

static void
test_dialog_coverage_generation_and_previous_session (void)
{
  CcFingerprintDialog *self = new_dialog ("Goodix TLS Fingerprint Sensor 5e0a", 13);
  self->coverage_active = TRUE;
  guint generation = self->coverage_generation;
  g_autoptr(GVariant) first = coverage_fixture (401, 1, "initial", 1, 0, 16);
  g_assert_false (coverage_apply_reply (self, first, generation - 1));
  g_assert_cmpuint (self->enroll_guide->observed_cells, ==, 0);
  g_assert_true (coverage_apply_reply (self, first, generation));
  g_assert_cmpuint (self->coverage_last_session, ==, 401);
  self->dialog_state = DIALOG_STATE_DEVICE_CLAIMED;
  enroll_finger (self, "right-index-finger");
  self->coverage_active = TRUE;
  g_assert_false (coverage_apply_reply (self, first, self->coverage_generation));
  g_assert_false (coverage_apply_reply (self, first, generation));
  g_assert_cmpuint (self->enroll_guide->observed_cells, ==, 0);
  g_autoptr(GVariant) next = coverage_fixture (402, 1, "initial", 1, 0, 16);
  g_assert_true (coverage_apply_reply (self, next, self->coverage_generation));
  coverage_stop (self);
  g_assert_false (coverage_apply_reply (self, next, self->coverage_generation));
  free_dialog (self);
}

static void
prepare_mock_transport (CcFingerprintDialog *self)
{
  /* Uninitialized proxy, only used by the mocked transport; no bus messages. */
  g_clear_object (&self->device);
  self->device = (CcFprintdDevice *) g_object_new (G_TYPE_DBUS_PROXY, NULL);
  self->coverage_active = TRUE;
  self->coverage_cancellable = g_cancellable_new ();
  mock_coverage_calls = 0;
  mock_coverage_unknown = FALSE;
}

static void
drain_coverage_callback (CcFingerprintDialog *self)
{
  while (self->coverage_pending)
    g_main_context_iteration (NULL, TRUE);
}

static void
test_async_final_fetch_and_unknown_method (void)
{
  CcFingerprintDialog *self = new_dialog ("Goodix TLS Fingerprint Sensor 5e0a", 13);
  prepare_mock_transport (self);
  g_autoptr(GVariant) before_final = coverage_fixture (501, 11, "added", 11, 0, 20);
  mock_coverage_payload = before_final;
  coverage_poll (self);
  self->coverage_final = TRUE;
  drain_coverage_callback (self);
  g_assert_true (self->coverage_active);
  g_assert_cmpuint (self->enroll_guide->coverage_frame, ==, 11);
  g_autoptr(GVariant) final = coverage_fixture (501, 12, "repeated", 12, 20, 0);
  mock_coverage_payload = final;
  coverage_poll (self);
  drain_coverage_callback (self);
  g_assert_false (self->coverage_active);
  g_assert_null (self->coverage_cancellable);
  g_assert_cmpuint (self->enroll_guide->coverage_frame, ==, 12);
  g_assert_cmpuint (mock_coverage_calls, ==, 2);
  free_dialog (self);

  self = new_dialog ("Goodix TLS Fingerprint Sensor 5e0a", 13);
  prepare_mock_transport (self);
  mock_coverage_unknown = TRUE;
  coverage_poll (self);
  drain_coverage_callback (self);
  g_assert_false (self->coverage_active);
  g_assert_false (self->enroll_guide->coverage_available);
  g_assert_nonnull (strstr (gtk_label_get_text (self->enroll_guide->coverage_status), "unavailable"));
  free_dialog (self);
  mock_coverage_unknown = FALSE;
  mock_coverage_payload = NULL;
}

static void
test_async_duplicate_check_without_native_session (void)
{
  CcFingerprintDialog *self = new_dialog ("Goodix TLS Fingerprint Sensor 5e0a", 13);
  prepare_mock_transport (self);
  GVariantBuilder empty_builder;
  g_variant_builder_init (&empty_builder, G_VARIANT_TYPE_VARDICT);
  g_autoptr(GVariant) empty = g_variant_ref_sink (g_variant_builder_end (&empty_builder));
  mock_coverage_payload = empty;
  coverage_poll (self);
  drain_coverage_callback (self);
  /* fprintd's duplicate check has no native enrollment observer yet. The
   * method exists, so an empty snapshot must keep polling without fallback. */
  g_assert_true (self->coverage_active);
  g_assert_false (self->enroll_guide->coverage_available);
  g_assert_cmpuint (self->enroll_guide->coverage_session, ==, 0);
  g_assert_cmpstr (gtk_label_get_text (self->enroll_guide->title), ==, "Check finger");
  g_assert_nonnull (strstr (gtk_label_get_text (self->enroll_guide->coverage_status), "Waiting"));
  g_autoptr(GVariant) pending = coverage_fixture (701, 0, "pending", 0, 0, 0);
  mock_coverage_payload = pending;
  coverage_poll (self);
  drain_coverage_callback (self);
  g_assert_true (self->coverage_active);
  g_assert_true (self->enroll_guide->coverage_available);
  g_assert_cmpuint (self->enroll_guide->coverage_session, ==, 701);
  g_assert_cmpuint (self->enroll_guide->observed_cells, ==, 0);
  g_autoptr(GVariant) first = coverage_fixture (701, 1, "initial", 1, 0, 16);
  mock_coverage_payload = first;
  coverage_poll (self);
  drain_coverage_callback (self);
  g_assert_true (self->coverage_active);
  g_assert_cmpuint (self->enroll_guide->observed_cells, ==, 320);
  g_assert_cmpuint (mock_coverage_calls, ==, 3);
  free_dialog (self);
  mock_coverage_payload = NULL;
}

static void
test_async_cancelled_generation (void)
{
  CcFingerprintDialog *self = new_dialog ("Goodix TLS Fingerprint Sensor 5e0a", 13);
  prepare_mock_transport (self);
  g_autoptr(GVariant) first = coverage_fixture (601, 1, "initial", 1, 0, 16);
  mock_coverage_payload = first;
  coverage_poll (self);
  coverage_stop (self);
  self->coverage_active = TRUE;
  self->coverage_pending = TRUE;
  /* The old callback must neither draw its map nor clear a new request. */
  while (g_main_context_iteration (NULL, FALSE));
  g_assert_true (self->coverage_pending);
  g_assert_cmpuint (self->enroll_guide->observed_cells, ==, 0);
  coverage_stop (self);
  free_dialog (self);
  mock_coverage_payload = NULL;
}

static GtkWindow *preview_window;
static CcFingerprintDialog *preview_dialog;
static GMainLoop *preview_loop;
static const char *preview_path;
static gboolean
save_preview (gpointer data)
{
  GtkWidget *widget = GTK_WIDGET (preview_window);
  GdkPaintable *paintable = gtk_widget_paintable_new (widget);
  GtkSnapshot *snapshot = gtk_snapshot_new ();
  int width = gtk_widget_get_width (widget), height = gtk_widget_get_height (widget);
  gdk_paintable_snapshot (paintable, snapshot, width, height);
  GskRenderNode *node = gtk_snapshot_free_to_node (snapshot);
  GskRenderer *renderer = gtk_native_get_renderer (GTK_NATIVE (widget));
  graphene_rect_t bounds = GRAPHENE_RECT_INIT (0, 0, width, height);
  GdkTexture *texture = gsk_renderer_render_texture (renderer, node, &bounds);
  g_assert_true (gdk_texture_save_to_png (texture, preview_path));
  g_object_unref (texture);
  gsk_render_node_unref (node);
  g_object_unref (paintable);
  g_main_loop_quit (preview_loop);
  return G_SOURCE_REMOVE;
}

int main (int argc, char **argv)
{
  gtk_init ();
  adw_init ();
  g_resources_register (cc_system_get_resource ());
  GtkCssProvider *css = gtk_css_provider_new ();
  gtk_css_provider_load_from_resource (css, "/org/gnome/control-center/system/users/users.css");
  gtk_style_context_add_provider_for_display (gdk_display_get_default (), GTK_STYLE_PROVIDER (css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref (css);
  if (argc == 3 && g_str_equal (argv[1], "--snapshot"))
    {
      preview_path = argv[2];
      preview_window = GTK_WINDOW (adw_window_new ());
      gtk_window_set_title (preview_window, "Fingerprint enrollment — simulated UI preview");
      gtk_window_set_default_size (preview_window, 720, 780);
      gtk_window_present (preview_window);
      preview_dialog = new_dialog ("Goodix TLS Fingerprint Sensor 5e0a", 13);
      handle_enroll_signal (preview_dialog, "enroll-stage-passed", FALSE);
      handle_enroll_signal (preview_dialog, "enroll-stage-passed", FALSE);
      cc_fingerprint_guide_set_state (preview_dialog->enroll_guide, 3, 12, FALSE, FALSE, FALSE);
      g_autoptr(GVariant) simulated_map = coverage_fixture (900, 3, "added", 3, 16, 8);
      cc_fingerprint_guide_apply_coverage (preview_dialog->enroll_guide, simulated_map);
      adw_dialog_present (ADW_DIALOG (preview_dialog), GTK_WIDGET (preview_window));
      preview_loop = g_main_loop_new (NULL, FALSE);
      g_timeout_add (900, save_preview, NULL);
      g_main_loop_run (preview_loop);
      free_dialog (preview_dialog);
      gtk_window_destroy (preview_window);
      g_main_loop_unref (preview_loop);
      return 0;
    }
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/fingerprint-guide/stage-retry-complete", test_stage_retry_and_complete);
  g_test_add_func ("/fingerprint-guide/duplicate-check-and-twelve-captures", test_duplicate_check_and_twelve_captures);
  g_test_add_func ("/fingerprint-guide/disconnection", test_disconnection);
  g_test_add_func ("/fingerprint-guide/other-reader-and-restart", test_other_devices_and_restart);
  g_test_add_func ("/fingerprint-guide/reduced-motion-and-cleanup", test_reduced_motion_and_cleanup);
  g_test_add_func ("/fingerprint-guide/measured-map-and-repeat", test_measured_map_and_repeated_area);
  g_test_add_func ("/fingerprint-guide/reject-stale-mutated-map", test_map_rejects_stale_and_mutated_frames);
  g_test_add_func ("/fingerprint-guide/reset-fallback-uncertainty", test_coverage_reset_fallback_and_uncertainty);
  g_test_add_func ("/fingerprint-guide/dialog-coverage-generations", test_dialog_coverage_generation_and_previous_session);
  g_test_add_func ("/fingerprint-guide/async-final-fetch-fallback", test_async_final_fetch_and_unknown_method);
  g_test_add_func ("/fingerprint-guide/async-cancelled-generation", test_async_cancelled_generation);
  g_test_add_func ("/fingerprint-guide/async-duplicate-check-empty-snapshot", test_async_duplicate_check_without_native_session);
  return g_test_run ();
}
