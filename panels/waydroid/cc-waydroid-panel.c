/*
 * Copyright (C) 2024 Cédric Bellegarde <cedric.bellegarde@adishatz.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "cc-waydroid-panel.h"
#include "cc-waydroid-resources.h"
#include "cc-waydroid-app-row.h"
#include "cc-waydroid-fdroid-row.h"
#include "cc-util.h"

#include <adwaita.h>
#include <gio/gdesktopappinfo.h>
#include <glib/gi18n.h>
#include <unistd.h>
#include <packagekit-glib2/packagekit.h>

#define WAYDROID_DBUS_NAME          "id.waydro.Container"
#define WAYDROID_DBUS_PATH          "/ContainerManager"
#define WAYDROID_DBUS_INTERFACE     "id.waydro.ContainerManager"

struct IOWatchProp {
  CcWaydroidPanel *panel;
  gchar           *prop;
};

struct _CcWaydroidPanel {
  CcPanel           parent;

  GDBusProxy       *waydroid_proxy;

  GtkWidget        *stack;

  GtkWidget        *install_box;
  GtkWidget        *install_status_page;
  GtkWidget        *install_waydroid_button;
  GtkWidget        *install_waydroid_spinner;
  GtkWidget        *enable_waydroid_switch;
  GtkWidget        *setting_uevent_switch;
  GtkWidget        *setting_suspend_switch;
  GtkWidget        *setting_shared_folder_switch;
  GtkWidget        *android_applications;

  GCancellable     *cancellable;
};

enum {
  PACKAGE_STATE_NONE,
  PACKAGE_STATE_GAPPS,
  PACKAGE_STATE_VANILLA
} PackageState;

G_DEFINE_TYPE (CcWaydroidPanel, cc_waydroid_panel, CC_TYPE_PANEL)

static void
waydroid_resolved_cb (GObject      *object,
                      GAsyncResult *res,
                      gpointer      user_data);
static void
waydroid_get_session_cb (GObject      *object,
                         GAsyncResult *res,
                         gpointer      data);
static void
setting_uevent_active_cb (CcWaydroidPanel *self);
static void
setting_suspend_active_cb (CcWaydroidPanel *self);

static char*
get_lcd_density (void)
{
  char *value = NULL;
  const char *env;
  g_autofree char *error = NULL;
  int exit_status = 0;

  env = g_getenv ("GRID_UNIT_PX");
  if (env == NULL)
    g_spawn_command_line_sync("sh -c \"getprop ro.sf.lcd_density\"",
                              &value,
                              NULL,
                              &exit_status,
                              NULL);
  else
    value = g_strdup (env);

  if (exit_status == 0)
    return value;
  else
    return g_strdup ("");
}

static GVariant*
get_waydroid_session (void)
{
  GVariantBuilder builder;

  g_variant_builder_init (&builder, G_VARIANT_TYPE("a{ss}"));

  g_autofree char *uid = g_strdup_printf ("%d", getuid ());
  g_autofree char *gid = g_strdup_printf ("%d", getgid ());
  g_autofree char *pid = g_strdup_printf ("%d", getpid ());
  g_autofree char *pulse_runtime_path = g_strdup_printf ("%s/pulse", g_get_user_runtime_dir ());
  g_autofree char *waydroid_data = g_strdup_printf ("%s/.local/share/waydroid/data", g_get_home_dir ());
  g_autofree char *lcd_density = get_lcd_density ();

  g_variant_builder_add (&builder, "{ss}", "user_name", g_get_user_name ());
  g_variant_builder_add (&builder, "{ss}", "user_id", uid);
  g_variant_builder_add (&builder, "{ss}", "group_id", gid);
  g_variant_builder_add (&builder, "{ss}", "host_user", g_get_home_dir ());
  g_variant_builder_add (&builder, "{ss}", "pid", pid);
  g_variant_builder_add (&builder, "{ss}", "state", "STOPPED");
  g_variant_builder_add (&builder, "{ss}", "xdg_data_home", g_get_user_data_dir ());
  g_variant_builder_add (&builder, "{ss}", "xdg_runtime_dir", g_get_user_runtime_dir ());
  g_variant_builder_add (&builder, "{ss}", "wayland_display", "wayland-0");
  g_variant_builder_add (&builder, "{ss}", "pulse_runtime_path", pulse_runtime_path);
  g_variant_builder_add (&builder, "{ss}", "lcd_density", lcd_density);
  g_variant_builder_add (&builder, "{ss}", "background_start", "true");
  g_variant_builder_add (&builder, "{ss}", "waydroid_data", waydroid_data);

  return g_variant_new ("a{ss}", &builder);
}

static void
set_status_page (CcWaydroidPanel *self,
                 gboolean         installed)
{
  gtk_widget_set_sensitive (self->install_box, !installed);

  if (installed) {
    gtk_widget_set_opacity (self->install_box, 0.0);
     adw_status_page_set_title (ADW_STATUS_PAGE (self->install_status_page),
                                _("Waydroid service needs to be enabled"));
  } else {
    gtk_widget_set_opacity (self->install_box, 1.0);
    adw_status_page_set_title (ADW_STATUS_PAGE (self->install_status_page),
                               _("Waydroid package needs to be installed"));
  }
}

static void
handle_waydroid_package (CcWaydroidPanel *self,
                         GPtrArray *packages)
{
  gboolean installed = FALSE;

  for (unsigned int i = 0; i < packages->len; i++) {
    PkPackage *package = packages->pdata[i];
    PkInfoEnum info = pk_package_get_info (package);

    if ((info & PK_INFO_ENUM_INSTALLED) == PK_INFO_ENUM_INSTALLED) {
      installed = TRUE;
    } else {
      gtk_widget_set_name (self->install_waydroid_button,
                           pk_package_get_id (package));
    }
  }

  set_status_page (self, installed);
}

static int
sort_applications (gconstpointer a,
                   gconstpointer b)
{
  GDesktopAppInfo *app_info_a = G_DESKTOP_APP_INFO (a);
  GDesktopAppInfo *app_info_b = G_DESKTOP_APP_INFO (b);
  g_autofree char *name_a = g_desktop_app_info_get_string (app_info_a, "Name");
  g_autofree char *name_b = g_desktop_app_info_get_string (app_info_b, "Name");

  return strcmp (name_a, name_b);
}

static void
add_application (GDesktopAppInfo *app_info, gpointer user_data)
{
  CcWaydroidPanel *self = user_data;
  GtkWidget *row = cc_app_row_new (app_info);

  if (row != NULL)
    adw_preferences_group_add (ADW_PREFERENCES_GROUP (self->android_applications),
                               row);
}

static void
check_available_apps (CcWaydroidPanel *self)
{
  gchar ***apps = g_desktop_app_info_search ("waydroid");
  GList *to_sort = NULL;
  unsigned int i = 0;
  gboolean fdroid_installed = FALSE;

  while (apps[i] != NULL) {
    for (unsigned int h = 0; h < g_strv_length (apps[i]); h++) {
      GDesktopAppInfo *app_info = g_desktop_app_info_new (apps[i][h]);
      g_autofree char *exec = g_desktop_app_info_get_string (app_info, "Exec");
      if (g_strrstr (exec, "waydroid app launch ") != NULL) {
        to_sort = g_list_insert_sorted (to_sort, app_info, sort_applications);
      }
      if (g_strrstr (exec, "org.fdroid.fdroid") != NULL) {
        fdroid_installed = TRUE;
      }
    }
    g_strfreev (apps[i]);
    i += 1;
  }

  if (!fdroid_installed) {
      GtkWidget *fdroid_row = cc_fdroid_row_new ();
      adw_preferences_group_add (ADW_PREFERENCES_GROUP (self->android_applications),
                                 fdroid_row);
  }

  g_list_foreach (to_sort, (GFunc) add_application, self);
  g_list_free_full (to_sort, g_object_unref);

  g_free (apps);
}

static void
check_waydroid_installed (CcWaydroidPanel *self)
{
  g_autoptr (PkTask) task = NULL;
  gchar **values = NULL;

  task = pk_task_new ();

  values = g_new0 (char *, 2);
  values[0] = g_strdup ("waydroid");
  values[1] = NULL;
  pk_task_resolve_async (task,
                         PK_FILTER_ENUM_NONE,
                         values,
                         self->cancellable,
                         NULL,
                         NULL,
                         waydroid_resolved_cb,
                         self);
}

static void
check_waydroid_running (CcWaydroidPanel *self)
{
  g_dbus_proxy_call (self->waydroid_proxy,
                     "GetSession",
                     NULL,
                     G_DBUS_CALL_FLAGS_NONE,
                     -1,
                     self->cancellable,
                     waydroid_get_session_cb,
                     self);
}

static gboolean
get_prop_watch (GIOChannel   *source,
                GIOCondition  condition,
                gpointer      data)
{
  struct IOWatchProp *io_watch_prop = data;
  g_autofree char *line = NULL;
  gsize  size;

  g_io_channel_read_line (source, &line, &size, NULL, NULL);

  if (g_strcmp0 (io_watch_prop->prop, "uevent") == 0) {
    g_signal_handlers_block_by_func(io_watch_prop->panel->setting_uevent_switch,
                                    setting_uevent_active_cb,
                                    io_watch_prop->panel);
    gtk_switch_set_active (GTK_SWITCH (io_watch_prop->panel->setting_uevent_switch),
                           g_strrstr (line, "true") != NULL);
    g_signal_handlers_unblock_by_func(io_watch_prop->panel->setting_uevent_switch,
                                      setting_uevent_active_cb,
                                      io_watch_prop->panel);
  } else if (g_strcmp0 (io_watch_prop->prop, "suspend") == 0) {
    g_signal_handlers_block_by_func(io_watch_prop->panel->setting_suspend_switch,
                                    setting_suspend_active_cb,
                                    io_watch_prop->panel);
    gtk_switch_set_active (GTK_SWITCH (io_watch_prop->panel->setting_suspend_switch),
                           g_strrstr (line, "true") != NULL);
    g_signal_handlers_unblock_by_func(io_watch_prop->panel->setting_suspend_switch,
                                      setting_suspend_active_cb,
                                      io_watch_prop->panel);
  }

  g_free (io_watch_prop);

  return FALSE;
}

static void
get_android_prop (CcWaydroidPanel *self,
                  char            *prop)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (GIOChannel) stdout;
  gchar *argv[5];
  gchar *envp[1];
  int uevent_stdout;
  struct IOWatchProp *io_watch_prop;

  argv[0] = "/usr/bin/waydroid2";
  argv[1] = "prop";
  argv[2] = "get";
  argv[3] = prop;
  argv[4] = NULL;

  envp[0] = NULL;

  if (!g_spawn_async_with_pipes (NULL,
                                 argv,
                                 envp,
                                 G_SPAWN_DO_NOT_REAP_CHILD,
                                 NULL,
                                 NULL,
                                 NULL,
                                 NULL,
                                 &uevent_stdout,
                                 NULL,
                                 &error)) {
    g_warning ("Can't read property: %s, %s", prop, error->message);
    return;
  }

  stdout = g_io_channel_unix_new (uevent_stdout);
  g_io_channel_set_close_on_unref (stdout, TRUE);

  io_watch_prop = g_malloc (sizeof (struct IOWatchProp));
  io_watch_prop->panel = self;
  io_watch_prop->prop = prop;

  g_io_add_watch (stdout,
                  G_IO_IN | G_IO_PRI,
                  (GIOFunc) get_prop_watch, io_watch_prop);
}

static void
set_android_prop (CcWaydroidPanel *self,
                  char            *prop,
                  char            *value)
{
  g_autoptr (GError) error = NULL;
  gchar *argv[6];
  gchar *envp[1];

  argv[0] = "/usr/bin/waydroid2";
  argv[1] = "prop";
  argv[2] = "set";
  argv[3] = prop;
  argv[4] = value;
  argv[5] = NULL;

  envp[0] = NULL;

  if (!g_spawn_async_with_pipes (NULL,
                                 argv,
                                 envp,
                                 G_SPAWN_DO_NOT_REAP_CHILD,
                                 NULL,
                                 NULL,
                                 NULL,
                                 NULL,
                                 NULL,
                                 NULL,
                                 &error)) {
    g_warning ("Can't write property: %s, %s", prop, error->message);
  }
}

static void
waydroid_installed_cb (GObject      *object,
                       GAsyncResult *res,
                       gpointer      user_data)
{
  CcWaydroidPanel *self = CC_WAYDROID_PANEL (user_data);
  g_autoptr (PkResults) results = NULL;
  g_autoptr (GError) error = NULL;

  gtk_spinner_stop (GTK_SPINNER (self->install_waydroid_spinner));

  results = pk_client_generic_finish (PK_CLIENT (object), res, &error);
  if (error != NULL) {
    g_warning ("Can't install waydroid: %s", error->message);
    set_status_page (self, FALSE);
  } else {
    gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "configure");
    gtk_widget_set_sensitive (self->enable_waydroid_switch, TRUE);
    set_status_page (self, TRUE);
    check_waydroid_running (self);
  }
}

static void
waydroid_resolved_cb (GObject      *object,
                      GAsyncResult *res,
                      gpointer      user_data)
{
  CcWaydroidPanel *self = CC_WAYDROID_PANEL (user_data);
  g_autoptr (PkResults) results = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr (PkError) error_code = NULL;
  g_autoptr (GPtrArray) packages = NULL;

  results = pk_task_generic_finish (PK_TASK (object), res, &error);
  if (error != NULL) {
    g_warning ("Can't contact PackageKit");
    return;
  }

  error_code = pk_results_get_error_code (results);
  if (error_code != NULL) {
    g_warning ("Can't find waydroid in packages");
    return;
  }

  packages = pk_results_get_package_array (results);
  handle_waydroid_package (self, packages);
}

static void
install_waydroid_clicked_cb (CcWaydroidPanel *self)
{
  g_autoptr (PkClient) client = NULL;
  gchar **values = NULL;

  gtk_widget_set_sensitive (self->install_waydroid_button, FALSE);

  client = pk_client_new ();

  values = g_new0 (char *, 2);
  values[0] = g_strdup (gtk_widget_get_name (self->install_waydroid_button));
  values[1] = NULL;

  gtk_spinner_start (GTK_SPINNER (self->install_waydroid_spinner));

  pk_client_install_packages_async (client,
                                    PK_FILTER_ENUM_NONE,
                                    values,
                                    self->cancellable,
                                    NULL,
                                    NULL,
                                    waydroid_installed_cb,
                                    self);
}

static void
waydroid_state_changed_cb (GObject   *object,
                           GAsyncResult *res,
                           gpointer      data)
{
  CcWaydroidPanel *self = CC_WAYDROID_PANEL (data);
  GDBusProxy *proxy = G_DBUS_PROXY (object);
  g_autoptr (GVariant) result = NULL;
  g_autoptr (GError) error = NULL;

  result = g_dbus_proxy_call_finish (proxy, res, &error);

  if (error != NULL) {
    gtk_switch_set_active (GTK_SWITCH (self->enable_waydroid_switch),
                           !gtk_switch_get_active (GTK_SWITCH (self->enable_waydroid_switch)));
  }
  check_waydroid_running (self);
}

static void
enable_waydroid_active_cb (CcWaydroidPanel *self)
{
  gboolean active = gtk_switch_get_active (GTK_SWITCH (self->enable_waydroid_switch));
  GVariant *variant;
  const char *method = active ? "Stop" : "Start";

  if (active)
    variant = g_variant_new ("(b)", TRUE);
  else
    variant = g_variant_new ("(v)", get_waydroid_session ());

  g_dbus_proxy_call (self->waydroid_proxy,
                     method,
                     variant,
                     G_DBUS_CALL_FLAGS_NONE,
                     -1,
                     self->cancellable,
                     waydroid_state_changed_cb,
                     self);
}

static void
setting_uevent_active_cb (CcWaydroidPanel *self)
{
  g_autofree char *active =  g_strdup_printf("%b",
                                             gtk_switch_get_active (GTK_SWITCH (self->setting_uevent_switch)));


  set_android_prop (self, "uevent", active);
}

static void
setting_suspend_active_cb (CcWaydroidPanel *self)
{
  g_autofree char *active =  g_strdup_printf("%b",
                                             gtk_switch_get_active (GTK_SWITCH (self->setting_suspend_switch)));

  set_android_prop (self, "suspend", active);
}

static void
setting_shared_folder_active_cb (CcWaydroidPanel *self)
{
  g_autofree char *dirname = g_build_filename (g_get_user_config_dir (),
                                               "Droidian",
                                               NULL);
  g_autofree char *filename = g_build_filename (dirname,
                                                "waydroid_shared_folder",
                                                NULL);
  g_autoptr (GFile) directory = g_file_new_for_path (dirname);
  g_autoptr (GFile) file = g_file_new_for_path (filename);
  g_autoptr (GError) error = NULL;
  gboolean active = gtk_switch_get_active (GTK_SWITCH (self->setting_shared_folder_switch));

  if (active) {
    g_autoptr (GFileOutputStream) stream = NULL;

    g_file_make_directory_with_parents (directory, self->cancellable, NULL);
    if (error == NULL) {
      stream = g_file_create (file,
                              G_FILE_CREATE_PRIVATE,
                              self->cancellable,
                              &error);
    }
  } else {
    g_file_delete (file, self->cancellable, &error);
  }

  if (error != NULL)
    g_warning ("%s", error->message);
}

static void
waydroid_bus_cb (GObject  *object,
                 GAsyncResult* res,
                 gpointer  user_data)
{
  CcWaydroidPanel *self = CC_WAYDROID_PANEL (user_data);
  g_autoptr (GError) error = NULL;

  self->waydroid_proxy = g_dbus_proxy_new_for_bus_finish (res, &error);

  if (error != NULL) {
    g_warning ("Can't enable Waydroid bus proxy");
    return;
  }

  check_waydroid_running (self);
}

static void
waydroid_get_session_cb (GObject      *object,
                         GAsyncResult *res,
                         gpointer      data)
{
  CcWaydroidPanel *self = CC_WAYDROID_PANEL (data);
  GDBusProxy *proxy = G_DBUS_PROXY (object);
  g_autoptr (GVariant) result = NULL;
  g_autoptr (GError) error = NULL;

  g_signal_handlers_block_by_func(self->enable_waydroid_switch,
                                  enable_waydroid_active_cb,
                                  self);

  result = g_dbus_proxy_call_finish (proxy, res, &error);

  result = g_variant_new ("b", TRUE);
  if (FALSE && error != NULL) {
    g_warning ("Can't get waydroid session state: %s", error->message);
    gtk_widget_set_sensitive (self->enable_waydroid_switch, FALSE);
    gtk_switch_set_active (GTK_SWITCH (self->enable_waydroid_switch), FALSE);
    gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "install");
    return;
  }

  gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "configure");
  gtk_widget_set_sensitive (self->enable_waydroid_switch, TRUE);
  gtk_switch_set_active (GTK_SWITCH (self->enable_waydroid_switch),
                         g_variant_get_size (result) > 0);

  g_signal_handlers_unblock_by_func(self->enable_waydroid_switch,
                                    enable_waydroid_active_cb,
                                    self);

  get_android_prop (self, "uevent");
  get_android_prop (self, "suspend");
}

static void
cc_waydroid_panel_dispose (GObject *object)
{
  CcWaydroidPanel *self = CC_WAYDROID_PANEL (object);

  g_clear_object (&self->waydroid_proxy);

  G_OBJECT_CLASS (cc_waydroid_panel_parent_class)->dispose (object);
}

static void
cc_waydroid_panel_finalize (GObject *object)
{
  G_OBJECT_CLASS (cc_waydroid_panel_parent_class)->finalize (object);
}

static void
cc_waydroid_panel_class_init (CcWaydroidPanelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = cc_waydroid_panel_dispose;
  object_class->finalize = cc_waydroid_panel_finalize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/control-center/waydroid/cc-waydroid-panel.ui");

  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        install_box);
  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        install_status_page);
  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        enable_waydroid_switch);
  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        install_waydroid_button);
  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        install_waydroid_spinner);
  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        stack);
  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        setting_uevent_switch);
  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        setting_shared_folder_switch);
  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        setting_suspend_switch);
  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        android_applications);

  gtk_widget_class_bind_template_callback (widget_class,
                                           install_waydroid_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class,
                                           enable_waydroid_active_cb);
  gtk_widget_class_bind_template_callback (widget_class,
                                           setting_uevent_active_cb);
  gtk_widget_class_bind_template_callback (widget_class,
                                           setting_suspend_active_cb);
  gtk_widget_class_bind_template_callback (widget_class,
                                           setting_shared_folder_active_cb);
}

static void
cc_waydroid_panel_init (CcWaydroidPanel *self)
{
  g_resources_register (cc_waydroid_get_resource ());
  gtk_widget_init_template (GTK_WIDGET (self));

  self->cancellable = g_cancellable_new ();

  g_dbus_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
                            0,
                            NULL,
                            WAYDROID_DBUS_NAME,
                            WAYDROID_DBUS_PATH,
                            WAYDROID_DBUS_INTERFACE,
                            self->cancellable,
                            waydroid_bus_cb,
                            self);

  check_waydroid_installed (self);
  check_available_apps (self);

  //gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "install");
  gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "configure");
}

CcWaydroidPanel *
cc_waydroid_panel_new (void)
{
  return CC_WAYDROID_PANEL (g_object_new (CC_TYPE_WAYDROID_PANEL, NULL));
}