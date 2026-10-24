/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2026 MATE Developers
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#ifndef __GS_MPRIS_MANAGER_H
#define __GS_MPRIS_MANAGER_H

#include <gio/gio.h>
#include <glib-object.h>

#include "gs-mpris-player.h"

G_BEGIN_DECLS

#define GS_TYPE_MPRIS_MANAGER         (gs_mpris_manager_get_type ())
#define GS_MPRIS_MANAGER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GS_TYPE_MPRIS_MANAGER, GSMprisManager))
#define GS_MPRIS_MANAGER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST ((k), GS_TYPE_MPRIS_MANAGER, GSMprisManagerClass))
#define GS_IS_MPRIS_MANAGER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GS_TYPE_MPRIS_MANAGER))
#define GS_IS_MPRIS_MANAGER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GS_TYPE_MPRIS_MANAGER))
#define GS_MPRIS_MANAGER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GS_TYPE_MPRIS_MANAGER, GSMprisManagerClass))

typedef struct GSMprisManagerPrivate GSMprisManagerPrivate;

typedef struct
{
	GObject                parent;
	GSMprisManagerPrivate *priv;
} GSMprisManager;

typedef struct
{
	GObjectClass           parent_class;
} GSMprisManagerClass;

GType            gs_mpris_manager_get_type            (void);
GSMprisManager * gs_mpris_manager_new                 (void);

/* ready players only; free the list, not the players */
GList *          gs_mpris_manager_get_players         (GSMprisManager *manager);
/* bus names of the players currently in the "Playing" state */
char **          gs_mpris_manager_get_playing_players (GSMprisManager *manager);

G_END_DECLS

#endif /* __GS_MPRIS_MANAGER_H */
