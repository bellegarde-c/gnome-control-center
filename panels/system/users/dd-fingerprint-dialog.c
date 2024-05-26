/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2020 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Authors: Cedric Bellegarde <cedric.bellegarde@adishatz.org>
 */

#include <glib/gi18n.h>
#include <cairo/cairo.h>

#include "dd-fingerprint-dialog.h"

#include "cc-list-row.h"

#include "config.h"

struct _DdFingerprintDialog
{
  AdwDialog parent_instance;
};

G_DEFINE_TYPE (DdFingerprintDialog, dd_fingerprint_dialog, GTK_TYPE_WINDOW)

static void
dd_fingerprint_dialog_constructed (GObject *object)
{
    gtk_window_maximize (GTK_WINDOW (object));
    G_OBJECT_CLASS (dd_fingerprint_dialog_parent_class)->constructed (object);
}

static void
dd_fingerprint_dialog_dispose (GObject *object)
{
    G_OBJECT_CLASS (dd_fingerprint_dialog_parent_class)->dispose (object);
}

static void
dd_fingerprint_dialog_class_init (DdFingerprintDialogClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_Escape, 0, "window.close", NULL);

  gtk_widget_class_set_template_from_resource (widget_class,
    "/org/gnome/control-center/system/users/dd-fingerprint-dialog.ui");

  object_class->constructed = dd_fingerprint_dialog_constructed;
  object_class->dispose = dd_fingerprint_dialog_dispose;
}

static void
dd_fingerprint_dialog_init (DdFingerprintDialog *self)
{
    gtk_widget_init_template (GTK_WIDGET (self));
}

DdFingerprintDialog *
dd_fingerprint_dialog_new ()
{
  return g_object_new (DD_TYPE_FINGERPRINT_DIALOG, NULL);
}