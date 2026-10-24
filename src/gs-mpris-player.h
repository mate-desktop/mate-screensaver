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

#ifndef __GS_MPRIS_PLAYER_H
#define __GS_MPRIS_PLAYER_H

#include <gio/gio.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define GS_TYPE_MPRIS_PLAYER         (gs_mpris_player_get_type ())
#define GS_MPRIS_PLAYER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GS_TYPE_MPRIS_PLAYER, GSMprisPlayer))
#define GS_MPRIS_PLAYER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST ((k), GS_TYPE_MPRIS_PLAYER, GSMprisPlayerClass))
#define GS_IS_MPRIS_PLAYER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GS_TYPE_MPRIS_PLAYER))
#define GS_IS_MPRIS_PLAYER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GS_TYPE_MPRIS_PLAYER))
#define GS_MPRIS_PLAYER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GS_TYPE_MPRIS_PLAYER, GSMprisPlayerClass))

typedef struct GSMprisPlayerPrivate GSMprisPlayerPrivate;

typedef struct
{
	GObject               parent;
	GSMprisPlayerPrivate *priv;
} GSMprisPlayer;

typedef struct
{
	GObjectClass          parent_class;
} GSMprisPlayerClass;

typedef void (*GSMprisPositionCallback) (GSMprisPlayer *player,
                                         gint64         position,
                                         GError        *error,
                                         gpointer       user_data);

GType           gs_mpris_player_get_type            (void);
GSMprisPlayer * gs_mpris_player_new                 (const char *bus_name);

const char *    gs_mpris_player_get_bus_name        (GSMprisPlayer *player);
gboolean        gs_mpris_player_is_ready            (GSMprisPlayer *player);
gboolean        gs_mpris_player_is_playing          (GSMprisPlayer *player);
gboolean        gs_mpris_player_is_stopped          (GSMprisPlayer *player);

const char *    gs_mpris_player_get_identity        (GSMprisPlayer *player);
const char *    gs_mpris_player_get_title           (GSMprisPlayer *player);
const char *    gs_mpris_player_get_artist          (GSMprisPlayer *player);
const char *    gs_mpris_player_get_album           (GSMprisPlayer *player);
const char *    gs_mpris_player_get_art_url         (GSMprisPlayer *player);
gint64          gs_mpris_player_get_length          (GSMprisPlayer *player);

gboolean        gs_mpris_player_get_can_control     (GSMprisPlayer *player);
gboolean        gs_mpris_player_get_can_go_next     (GSMprisPlayer *player);
gboolean        gs_mpris_player_get_can_go_previous (GSMprisPlayer *player);
gboolean        gs_mpris_player_get_can_seek        (GSMprisPlayer *player);
gboolean        gs_mpris_player_get_can_play        (GSMprisPlayer *player);
gboolean        gs_mpris_player_get_can_pause       (GSMprisPlayer *player);

void            gs_mpris_player_play_pause          (GSMprisPlayer *player);
void            gs_mpris_player_next                (GSMprisPlayer *player);
void            gs_mpris_player_previous            (GSMprisPlayer *player);
void            gs_mpris_player_seek_to             (GSMprisPlayer *player,
                                                     gint64         position,
                                                     gint64         current_position);
void            gs_mpris_player_get_position_async  (GSMprisPlayer          *player,
                                                     GSMprisPositionCallback callback,
                                                     gpointer                user_data);

G_END_DECLS

#endif /* __GS_MPRIS_PLAYER_H */
