/*
 * Copyright (C) 2024 Cédric Bellegarde <cedric.bellegarde@adishatz.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <adwaita.h>
#include <shell/cc-panel.h>
#include <gio/gdesktopappinfo.h>

G_BEGIN_DECLS

#define CC_TYPE_APP_ROW (cc_app_row_get_type())
G_DECLARE_FINAL_TYPE (CcAppRow, cc_app_row, CC, APP_ROW, AdwExpanderRow)

GtkWidget* cc_app_row_new                    (GDesktopAppInfo *app_info);

G_END_DECLS
