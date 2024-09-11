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

#define CC_TYPE_FDROID_ROW (cc_fdroid_row_get_type())
G_DECLARE_FINAL_TYPE (CcFdroidRow, cc_fdroid_row, CC, FDROID_ROW, AdwExpanderRow)

GtkWidget* cc_fdroid_row_new                    (void);

G_END_DECLS
