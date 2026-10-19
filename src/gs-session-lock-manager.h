/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012-2026 MATE Developers
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __GS_SESSION_LOCK_MANAGER_H
#define __GS_SESSION_LOCK_MANAGER_H

#include <glib-object.h>

#include "gs-window.h"

G_BEGIN_DECLS

#define GS_TYPE_SESSION_LOCK_MANAGER (gs_session_lock_manager_get_type ())

G_DECLARE_FINAL_TYPE (GSSessionLockManager, gs_session_lock_manager, GS, SESSION_LOCK_MANAGER, GObject)

GSSessionLockManager *gs_session_lock_manager_new          (void);
gboolean              gs_session_lock_manager_lock         (GSSessionLockManager *manager);
void                  gs_session_lock_manager_unlock       (GSSessionLockManager *manager);
void                  gs_session_lock_manager_add_window   (GSSessionLockManager *manager,
                                                            GSWindow             *window);
void                  gs_session_lock_manager_remove_window(GSSessionLockManager *manager,
                                                            GSWindow             *window);

G_END_DECLS

#endif /* __GS_SESSION_LOCK_MANAGER_H */
