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

#ifndef __GS_MEDIA_WIDGET_H
#define __GS_MEDIA_WIDGET_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GS_TYPE_MEDIA_WIDGET         (gs_media_widget_get_type ())
#define GS_MEDIA_WIDGET(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GS_TYPE_MEDIA_WIDGET, GSMediaWidget))
#define GS_MEDIA_WIDGET_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST ((k), GS_TYPE_MEDIA_WIDGET, GSMediaWidgetClass))
#define GS_IS_MEDIA_WIDGET(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GS_TYPE_MEDIA_WIDGET))
#define GS_IS_MEDIA_WIDGET_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GS_TYPE_MEDIA_WIDGET))
#define GS_MEDIA_WIDGET_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GS_TYPE_MEDIA_WIDGET, GSMediaWidgetClass))

typedef enum
{
	GS_MEDIA_WIDGET_PART_PLAYER_NAME = 1 << 0,
	GS_MEDIA_WIDGET_PART_ALBUM_ART   = 1 << 1,
	GS_MEDIA_WIDGET_PART_TRACK_INFO  = 1 << 2,
	GS_MEDIA_WIDGET_PART_PROGRESS    = 1 << 3,
	GS_MEDIA_WIDGET_PART_SEEK        = 1 << 4, /* progress is a seek slider */
	GS_MEDIA_WIDGET_PART_PLAY_PAUSE  = 1 << 5,
	GS_MEDIA_WIDGET_PART_SKIP        = 1 << 6, /* previous and next */
	GS_MEDIA_WIDGET_PART_ALL         = (1 << 7) - 1
} GSMediaWidgetParts;

typedef struct GSMediaWidgetPrivate GSMediaWidgetPrivate;

typedef struct
{
	GtkBox                parent;
	GSMediaWidgetPrivate *priv;
} GSMediaWidget;

typedef struct
{
	GtkBoxClass           parent_class;

	void (* activity) (GSMediaWidget *widget);
} GSMediaWidgetClass;

GType       gs_media_widget_get_type            (void);
GtkWidget * gs_media_widget_new                 (void);

void        gs_media_widget_set_parts           (GSMediaWidget      *widget,
                                                 GSMediaWidgetParts  parts);

/* NULL shows any player that is playing or paused.  Otherwise only players
 * that are playing right now or whose bus name is listed are shown. */
void        gs_media_widget_set_allowed_players (GSMediaWidget      *widget,
                                                 const char * const *bus_names);

G_END_DECLS

#endif /* __GS_MEDIA_WIDGET_H */
