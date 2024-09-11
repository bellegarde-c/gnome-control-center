/*
 * Copyright (C) 2024 Cédric Bellegarde <cedric.bellegarde@adishatz.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <glib/gi18n.h>

#include "cc-waydroid-app-row.h"


const char* ROOT_APPLICATIONS[] = {
  "com.android.documentsui",
  "com.android.contacts",
  "com.android.camera2",
  "org.lineageos.recorder",
  "com.android.gallery3d",
  "org.lineageos.jelly",
  "org.lineageos.eleven",
  "org.lineageos.etar",
  "com.android.settings",
  "com.android.calculator2",
  "com.android.deskclock",
  "com.android.traceur"
};

struct _CcAppRow {
  AdwExpanderRow parent_instance;

  GtkWidget       *remove_application_button;
  GtkWidget       *show_in_launcher_switch;
  GtkImage        *icon;

  GKeyFile        *desktop_file;
  char            *filename;
};

G_DEFINE_TYPE (CcAppRow, cc_app_row, ADW_TYPE_EXPANDER_ROW)

static gboolean
is_root_application (CcAppRow *self, const gchar *filename)
{
  guint root_count = sizeof (ROOT_APPLICATIONS) / sizeof (char*);

  for (unsigned int i = 0; i < root_count; i++) {
    if (g_strrstr (filename, ROOT_APPLICATIONS[i]) != NULL)
      return TRUE;
  }
  return FALSE;
}

static void
show_in_launcher_active_cb (CcAppRow *self)
{
  g_autoptr (GError) error = NULL;
  gboolean active = gtk_switch_get_active (GTK_SWITCH (self->show_in_launcher_switch));

  g_key_file_set_boolean (self->desktop_file,
                          G_KEY_FILE_DESKTOP_GROUP,
                          G_KEY_FILE_DESKTOP_KEY_NO_DISPLAY,
                          !active);
  g_key_file_save_to_file (self->desktop_file, self->filename, &error);

  if (error != NULL) {
      g_warning ("%s", error->message);
  }
}

static void
remove_application_dialog_cb (AdwDialog *dialog,
                              gchar* response,
                              gpointer   user_data)
{
  if (g_strcmp0 (response, "remove") == 0) {
    CcAppRow *self = CC_APP_ROW (user_data);
    g_autofree gchar *command = NULL;
    GString *exec = g_string_new (g_key_file_get_string (
                                     self->desktop_file,
                                     G_KEY_FILE_DESKTOP_GROUP,
                                     G_KEY_FILE_DESKTOP_KEY_EXEC,
                                     NULL));
    g_string_replace (exec, "launch", "remove", 0);
    command = g_string_free_and_steal (exec);

    g_spawn_command_line_async(command, NULL);

    adw_expander_row_set_expanded (ADW_EXPANDER_ROW (user_data), FALSE);
    gtk_widget_set_sensitive (GTK_WIDGET (user_data), FALSE);
    gtk_widget_set_opacity (GTK_WIDGET (user_data), 0.5);
  }
}

static void
remove_application_clicked_cb (CcAppRow *self)
{
  AdwDialog *dialog = adw_alert_dialog_new (_("Remove application?"), NULL);
  g_autofree char *app_name = g_key_file_get_string (self->desktop_file,
                                                     G_KEY_FILE_DESKTOP_GROUP,
                                                     G_KEY_FILE_DESKTOP_KEY_NAME,
                                                     NULL);

  adw_alert_dialog_format_body (ADW_ALERT_DIALOG (dialog),
                                _("This will remove %s"),
                                app_name);

  adw_alert_dialog_add_responses (ADW_ALERT_DIALOG (dialog),
                                  "cancel",  _("_Cancel"),
                                  "remove", _("_Remove"),
                                  NULL);

  adw_alert_dialog_set_response_appearance (ADW_ALERT_DIALOG (dialog),
                                            "remove",
                                            ADW_RESPONSE_DESTRUCTIVE);

  adw_alert_dialog_set_default_response (ADW_ALERT_DIALOG (dialog), "cancel");
  adw_alert_dialog_set_close_response (ADW_ALERT_DIALOG (dialog), "cancel");

  g_signal_connect (dialog,
                    "response",
                    G_CALLBACK (remove_application_dialog_cb),
                    self);

  adw_dialog_present (dialog, NULL);
}

static void
cc_app_row_dispose (GObject *object)
{
  G_OBJECT_CLASS (cc_app_row_parent_class)->dispose (object);
}

static void
cc_app_row_finalize (GObject *object)
{
  CcAppRow *self = CC_APP_ROW (object);

  g_key_file_free (self->desktop_file);
  g_free (self->filename);

  G_OBJECT_CLASS (cc_app_row_parent_class)->finalize (object);
}

static void
cc_app_row_class_init (CcAppRowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = cc_app_row_dispose;
  object_class->finalize = cc_app_row_finalize;

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/control-center/waydroid/cc-waydroid-app-row.ui");

  gtk_widget_class_bind_template_child (widget_class, CcAppRow, icon);
  gtk_widget_class_bind_template_child (widget_class, CcAppRow, remove_application_button);
  gtk_widget_class_bind_template_child (widget_class, CcAppRow, show_in_launcher_switch);

  gtk_widget_class_bind_template_callback (widget_class,
                                           show_in_launcher_active_cb);
  gtk_widget_class_bind_template_callback (widget_class,
                                           remove_application_clicked_cb);
}

static void
cc_app_row_init (CcAppRow *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

GtkWidget*
cc_app_row_new (GDesktopAppInfo *app_info)
{
  CcAppRow *self;
  g_autoptr (GKeyFile) desktop_file = g_key_file_new ();
  g_autofree char *name = NULL;
  g_autofree char *icon_name = NULL;
  g_autofree char *no_display = NULL;
  const char *filename = g_desktop_app_info_get_filename (app_info);

  g_return_val_if_fail (filename != NULL, NULL);

  g_return_val_if_fail (g_key_file_load_from_file (desktop_file,
                                                   filename,
                                                   G_KEY_FILE_KEEP_COMMENTS |
                                                   G_KEY_FILE_KEEP_TRANSLATIONS,
                                                   NULL), NULL);

  name = g_key_file_get_string (desktop_file,
                                G_KEY_FILE_DESKTOP_GROUP,
                                G_KEY_FILE_DESKTOP_KEY_NAME,
                                NULL);
  g_return_val_if_fail (name != NULL, NULL);

  icon_name = g_key_file_get_string (desktop_file,
                                     G_KEY_FILE_DESKTOP_GROUP,
                                     G_KEY_FILE_DESKTOP_KEY_ICON,
                                     NULL);
  g_return_val_if_fail (icon_name != NULL, NULL);

  no_display = g_key_file_get_string (desktop_file,
                                      G_KEY_FILE_DESKTOP_GROUP,
                                      G_KEY_FILE_DESKTOP_KEY_NO_DISPLAY,
                                      NULL);
  g_return_val_if_fail (name != NULL, NULL);

  self = CC_APP_ROW (g_object_new (CC_TYPE_APP_ROW, NULL));
  self->desktop_file = g_key_file_ref (desktop_file);
  self->filename = g_strdup (filename);

  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (self), name);
  gtk_image_set_from_file (self->icon, icon_name);

  if (is_root_application (self, filename)) {
    gtk_widget_set_sensitive (self->remove_application_button, FALSE);
  }

  g_signal_handlers_block_by_func(self->show_in_launcher_switch,
                                  show_in_launcher_active_cb,
                                  self);

  gtk_switch_set_active (GTK_SWITCH (self->show_in_launcher_switch),
                         g_strcmp0 (no_display, "true") != 0);

  g_signal_handlers_unblock_by_func(self->show_in_launcher_switch,
                                    show_in_launcher_active_cb,
                                    self);

  return GTK_WIDGET (self);
}