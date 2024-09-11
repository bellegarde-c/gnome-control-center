/*
 * Copyright (C) 2024 Cédric Bellegarde <cedric.bellegarde@adishatz.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <glib/gi18n.h>

#include "cc-waydroid-fdroid-row.h"

#define FDROID_URL "https://f-droid.org/F-Droid.apk"

struct _CcFdroidRow {
  AdwExpanderRow     parent_instance;

  GtkWidget         *install_row;
  GtkWidget         *install_application_button;
  GtkWidget         *spinner;

  GCancellable      *cancellable;
};

G_DEFINE_TYPE (CcFdroidRow, cc_fdroid_row, ADW_TYPE_EXPANDER_ROW)

static void
show_fdroid_error (CcFdroidRow *self,
                   gchar       *error)
{
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (self->install_row),
                                 error);
  gtk_spinner_stop (GTK_SPINNER (self->spinner));
  gtk_widget_set_sensitive (self->install_application_button, TRUE);
}

static void
install_child_exit (GPid     pid,
                    gint     wait_status,
                    gpointer user_data)
{
  CcFdroidRow *self = CC_FDROID_ROW (user_data);
  g_autoptr (GFile) file = g_file_new_for_path ("/tmp/fdroid.apk");

  gtk_spinner_stop (GTK_SPINNER (self->spinner));
  gtk_button_set_label (GTK_BUTTON (self->install_application_button),
                        _("Installed"));

  g_file_delete_async (file, 0, self->cancellable, NULL, NULL);
}

static void
install_fdroid (CcFdroidRow *self)
{
  g_autoptr (GError) error = NULL;
  gchar *argv[5];
  gchar *envp[1];
  GPid child_pid;

  argv[0] = "/usr/bin/waydroid2";
  argv[1] = "app";
  argv[2] = "install";
  argv[3] = "/tmp/fdroid.apk";
  argv[4] = NULL;

  envp[0] = NULL;

  if (!g_spawn_async_with_pipes (NULL,
                                 argv,
                                 envp,
                                 G_SPAWN_DO_NOT_REAP_CHILD,
                                 NULL,
                                 NULL,
                                 &child_pid,
                                 NULL,
                                 NULL,
                                 NULL,
                                 &error)) {
    show_fdroid_error (self, error->message);
    return;
  }

  g_child_watch_add(child_pid, install_child_exit, self);
}

static void
fdroid_downloaded_cb (GObject      *source_object,
                      GAsyncResult *res,
                      gpointer      user_data)
{
  CcFdroidRow *self = CC_FDROID_ROW (user_data);
  g_autoptr (GError) error = NULL;
  gboolean success;

  success = g_file_copy_finish (G_FILE (source_object),
                                res,
                                &error);

  if (!success) {
    show_fdroid_error (self, error->message);
    return;
  }

  install_fdroid (self);
}

static void
install_application_clicked_cb (CcFdroidRow *self)
{
  g_autoptr (GFile) input_file  = g_file_new_for_uri (FDROID_URL);
  g_autoptr (GFile) output_file = g_file_new_for_path ("/tmp/fdroid.apk");

  gtk_spinner_start (GTK_SPINNER (self->spinner));
  gtk_widget_set_sensitive (self->install_application_button, FALSE);

  g_file_copy_async (input_file,
                     output_file,
                     G_FILE_COPY_OVERWRITE,
                     0,
                     self->cancellable,
                     NULL,
                     NULL,
                     fdroid_downloaded_cb,
                     self);
}

static void
cc_fdroid_row_dispose (GObject *object)
{
  G_OBJECT_CLASS (cc_fdroid_row_parent_class)->dispose (object);
}

static void
cc_fdroid_row_finalize (GObject *object)
{
  G_OBJECT_CLASS (cc_fdroid_row_parent_class)->finalize (object);
}

static void
cc_fdroid_row_class_init (CcFdroidRowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = cc_fdroid_row_dispose;
  object_class->finalize = cc_fdroid_row_finalize;

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/control-center/waydroid/cc-waydroid-fdroid-row.ui");

  gtk_widget_class_bind_template_child (widget_class, CcFdroidRow, install_row);
  gtk_widget_class_bind_template_child (widget_class, CcFdroidRow, install_application_button);
  gtk_widget_class_bind_template_child (widget_class, CcFdroidRow, spinner);

  gtk_widget_class_bind_template_callback (widget_class,
                                           install_application_clicked_cb);
}

static void
cc_fdroid_row_init (CcFdroidRow *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  self->cancellable = g_cancellable_new ();
}

GtkWidget*
cc_fdroid_row_new (void)
{
  GtkWidget *self;

  self = g_object_new (CC_TYPE_FDROID_ROW, NULL);

  return self;
}