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
 * Ported from mate-media's gvc-player-widget.c
 *
 */

#include "config.h"

#include <string.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "gs-media-widget.h"
#include "gs-mpris-manager.h"

#define COVER_SIZE   160
#define WIDGET_WIDTH 200
#define SEEK_STEP    5.0 /* seconds per scroll step */

static void gs_media_widget_finalize (GObject *object);

struct GSMediaWidgetPrivate
{
	GSMprisManager    *manager;
	GSMprisPlayer     *player;

	GSMediaWidgetParts parts;
	char             **allowed_players;

	GtkWidget         *player_label;
	GtkWidget         *cover;
	GtkWidget         *info_box;
	GtkWidget         *title_label;
	GtkWidget         *artist_label;
	GtkWidget         *album_label;
	GtkWidget         *progress_box;
	GtkWidget         *progress_bar;
	GtkWidget         *seek_scale;
	GtkWidget         *time_label;
	GtkWidget         *controls;
	GtkWidget         *prev_button;
	GtkWidget         *play_button;
	GtkWidget         *next_button;

	GCancellable      *cover_cancellable;
	guint              position_timer_id;

	gint64             position;     /* last known, in microseconds */
	gboolean           seek_dragging;
	gboolean           updating_seek;
};

enum
{
    ACTIVITY,
    LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE_WITH_PRIVATE (GSMediaWidget, gs_media_widget, GTK_TYPE_BOX)

static void update_player (GSMediaWidget *widget);

static char *
format_time (gint64 seconds)
{
	if (seconds < 0)
	{
		seconds = 0;
	}

	return g_strdup_printf ("%" G_GINT64_FORMAT ":%02" G_GINT64_FORMAT,
	                        seconds / 60, seconds % 60);
}

static gboolean
has_part (GSMediaWidget     *widget,
          GSMediaWidgetParts part)
{
	return (widget->priv->parts & part) != 0;
}

static void
set_label_text (GtkWidget  *label,
                const char *text)
{
	gtk_label_set_text (GTK_LABEL (label), text);
	gtk_widget_set_visible (label, text != NULL && text[0] != '\0');
}

/* Cover art */

static void
cancel_cover_load (GSMediaWidget *widget)
{
	if (widget->priv->cover_cancellable != NULL)
	{
		g_cancellable_cancel (widget->priv->cover_cancellable);
		g_clear_object (&widget->priv->cover_cancellable);
	}
}

static void
set_cover_fallback (GSMediaWidget *widget)
{
	gtk_image_set_from_icon_name (GTK_IMAGE (widget->priv->cover),
	                              "media-optical",
	                              GTK_ICON_SIZE_DIALOG);
	gtk_image_set_pixel_size (GTK_IMAGE (widget->priv->cover), COVER_SIZE);
}

static void
on_cover_pixbuf_ready (GObject      *source_object,
                       GAsyncResult *res,
                       gpointer      user_data)
{
	GSMediaWidget *widget;
	GdkPixbuf     *pixbuf;
	GError        *error = NULL;

	pixbuf = gdk_pixbuf_new_from_stream_finish (res, &error);
	if (pixbuf == NULL)
	{
		if (! g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
		{
			set_cover_fallback (GS_MEDIA_WIDGET (user_data));
		}
		g_error_free (error);
		return;
	}

	widget = GS_MEDIA_WIDGET (user_data);
	gtk_image_set_from_pixbuf (GTK_IMAGE (widget->priv->cover), pixbuf);
	g_object_unref (pixbuf);
}

static void
load_cover_from_stream (GSMediaWidget *widget,
                        GInputStream  *stream)
{
	gdk_pixbuf_new_from_stream_at_scale_async (stream,
	                                           COVER_SIZE,
	                                           COVER_SIZE,
	                                           TRUE,
	                                           widget->priv->cover_cancellable,
	                                           on_cover_pixbuf_ready,
	                                           widget);
}

static void
on_cover_file_read_ready (GObject      *source_object,
                          GAsyncResult *res,
                          gpointer      user_data)
{
	GFileInputStream *stream;
	GError           *error = NULL;

	stream = g_file_read_finish (G_FILE (source_object), res, &error);
	if (stream == NULL)
	{
		if (! g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
		{
			set_cover_fallback (GS_MEDIA_WIDGET (user_data));
		}
		g_error_free (error);
		return;
	}

	load_cover_from_stream (GS_MEDIA_WIDGET (user_data), G_INPUT_STREAM (stream));
	g_object_unref (stream);
}

/* The cancellable is cancelled on every reload and on destroy, so the
 * callbacks above never run against a stale track or a dead widget. */
static void
update_cover (GSMediaWidget *widget)
{
	const char *art_url;

	cancel_cover_load (widget);

	if (! has_part (widget, GS_MEDIA_WIDGET_PART_ALBUM_ART))
	{
		gtk_widget_hide (widget->priv->cover);
		return;
	}
	gtk_widget_show (widget->priv->cover);

	art_url = gs_mpris_player_get_art_url (widget->priv->player);
	if (art_url[0] == '\0')
	{
		set_cover_fallback (widget);
		return;
	}

	widget->priv->cover_cancellable = g_cancellable_new ();

	if (g_str_has_prefix (art_url, "data:image/"))
	{
		const char *comma;

		comma = strchr (art_url, ',');
		if (comma != NULL)
		{
			guchar       *decoded;
			gsize         len;
			GInputStream *stream;

			decoded = g_base64_decode (comma + 1, &len);
			stream = g_memory_input_stream_new_from_data (decoded, len, g_free);
			load_cover_from_stream (widget, stream);
			g_object_unref (stream);
			return;
		}
	}
	else
	{
		GFile *file;

		file = g_file_new_for_uri (art_url);
		g_file_read_async (file,
		                   G_PRIORITY_DEFAULT,
		                   widget->priv->cover_cancellable,
		                   on_cover_file_read_ready,
		                   widget);
		g_object_unref (file);
		return;
	}

	set_cover_fallback (widget);
}

/* Progress */

static void
set_time_label (GSMediaWidget *widget,
                gint64         seconds)
{
	char *elapsed;
	char *total;
	char *text;

	elapsed = format_time (seconds);
	total = format_time (gs_mpris_player_get_length (widget->priv->player) / G_USEC_PER_SEC);
	text = g_strdup_printf ("%s / %s", elapsed, total);
	gtk_label_set_text (GTK_LABEL (widget->priv->time_label), text);
	g_free (elapsed);
	g_free (total);
	g_free (text);
}

static void
set_position (GSMediaWidget *widget,
              gint64         position)
{
	gint64 length;
	gint64 seconds;

	widget->priv->position = position;

	/* don't yank the slider out from under the user */
	if (widget->priv->seek_dragging)
	{
		return;
	}

	length = gs_mpris_player_get_length (widget->priv->player) / G_USEC_PER_SEC;
	seconds = CLAMP (position / G_USEC_PER_SEC, 0, length);

	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (widget->priv->progress_bar),
	                               length > 0 ? (gdouble) seconds / length : 0.0);

	widget->priv->updating_seek = TRUE;
	gtk_range_set_value (GTK_RANGE (widget->priv->seek_scale), seconds);
	widget->priv->updating_seek = FALSE;

	set_time_label (widget, seconds);
}

static void
on_position_ready (GSMprisPlayer *player,
                   gint64         position,
                   GError        *error,
                   gpointer       user_data)
{
	GSMediaWidget *widget = GS_MEDIA_WIDGET (user_data);

	if (error == NULL && widget->priv->player == player)
	{
		set_position (widget, position);
	}

	g_object_unref (widget);
}

static void
request_position (GSMediaWidget *widget)
{
	gs_mpris_player_get_position_async (widget->priv->player,
	                                    on_position_ready,
	                                    g_object_ref (widget));
}

static gboolean
position_timer_cb (GSMediaWidget *widget)
{
	request_position (widget);

	return G_SOURCE_CONTINUE;
}

static void
remove_position_timer (GSMediaWidget *widget)
{
	if (widget->priv->position_timer_id != 0)
	{
		g_source_remove (widget->priv->position_timer_id);
		widget->priv->position_timer_id = 0;
	}
}

static void
update_progress (GSMediaWidget *widget)
{
	gboolean visible;
	gboolean seek;
	gint64   length;

	remove_position_timer (widget);

	length = gs_mpris_player_get_length (widget->priv->player) / G_USEC_PER_SEC;
	visible = has_part (widget, GS_MEDIA_WIDGET_PART_PROGRESS) && length > 0;
	gtk_widget_set_visible (widget->priv->progress_box, visible);
	if (! visible)
	{
		return;
	}

	seek = has_part (widget, GS_MEDIA_WIDGET_PART_SEEK);
	gtk_widget_set_visible (widget->priv->progress_bar, ! seek);
	gtk_widget_set_visible (widget->priv->seek_scale, seek);
	gtk_widget_set_sensitive (widget->priv->seek_scale,
	                          gs_mpris_player_get_can_seek (widget->priv->player));

	widget->priv->updating_seek = TRUE;
	gtk_range_set_range (GTK_RANGE (widget->priv->seek_scale), 0, length);
	widget->priv->updating_seek = FALSE;

	request_position (widget);

	if (gs_mpris_player_is_playing (widget->priv->player))
	{
		widget->priv->position_timer_id = g_timeout_add_seconds (1,
		                                                         (GSourceFunc) position_timer_cb,
		                                                         widget);
	}
}

/* Player state */

static void
update_metadata (GSMediaWidget *widget)
{
	const char *title;

	set_label_text (widget->priv->player_label,
	                gs_mpris_player_get_identity (widget->priv->player));
	if (! has_part (widget, GS_MEDIA_WIDGET_PART_PLAYER_NAME))
	{
		gtk_widget_hide (widget->priv->player_label);
	}

	title = gs_mpris_player_get_title (widget->priv->player);
	set_label_text (widget->priv->title_label, title[0] != '\0' ? title : _("Unknown Title"));
	set_label_text (widget->priv->artist_label, gs_mpris_player_get_artist (widget->priv->player));
	set_label_text (widget->priv->album_label, gs_mpris_player_get_album (widget->priv->player));
	gtk_widget_set_visible (widget->priv->info_box,
	                        has_part (widget, GS_MEDIA_WIDGET_PART_TRACK_INFO));

	update_cover (widget);
	update_progress (widget);
	set_position (widget, 0);
}

static void
update_status (GSMediaWidget *widget)
{
	gboolean    playing;
	const char *icon_name;

	playing = gs_mpris_player_is_playing (widget->priv->player);
	icon_name = playing ? "media-playback-pause-symbolic" : "media-playback-start-symbolic";

	gtk_button_set_image (GTK_BUTTON (widget->priv->play_button),
	                      gtk_image_new_from_icon_name (icon_name, GTK_ICON_SIZE_BUTTON));
	gtk_widget_set_tooltip_text (widget->priv->play_button,
	                             playing ? _("Pause") : _("Play"));

	update_progress (widget);
}

static void
update_capabilities (GSMediaWidget *widget)
{
	GSMprisPlayer *player = widget->priv->player;
	gboolean       play_pause;
	gboolean       skip;

	play_pause = has_part (widget, GS_MEDIA_WIDGET_PART_PLAY_PAUSE);
	skip = has_part (widget, GS_MEDIA_WIDGET_PART_SKIP);

	gtk_widget_set_visible (widget->priv->controls, play_pause || skip);
	gtk_widget_set_visible (widget->priv->play_button, play_pause);
	gtk_widget_set_visible (widget->priv->prev_button, skip);
	gtk_widget_set_visible (widget->priv->next_button, skip);

	gtk_widget_set_sensitive (widget->priv->prev_button,
	                          gs_mpris_player_get_can_go_previous (player));
	gtk_widget_set_sensitive (widget->priv->next_button,
	                          gs_mpris_player_get_can_go_next (player));
	gtk_widget_set_sensitive (widget->priv->play_button,
	                          gs_mpris_player_get_can_play (player) ||
	                          gs_mpris_player_get_can_pause (player));
	gtk_widget_set_sensitive (widget->priv->seek_scale,
	                          gs_mpris_player_get_can_seek (player));
}

static void
on_player_metadata_changed (GSMprisPlayer *player,
                            GSMediaWidget *widget)
{
	update_metadata (widget);
}

static void
on_player_capabilities_changed (GSMprisPlayer *player,
                                GSMediaWidget *widget)
{
	update_capabilities (widget);
}

static void
set_player (GSMediaWidget *widget,
            GSMprisPlayer *player)
{
	if (widget->priv->player == player)
	{
		if (player != NULL)
		{
			update_status (widget);
		}
		return;
	}

	cancel_cover_load (widget);
	remove_position_timer (widget);

	if (widget->priv->player != NULL)
	{
		g_signal_handlers_disconnect_by_func (widget->priv->player, on_player_metadata_changed, widget);
		g_signal_handlers_disconnect_by_func (widget->priv->player, on_player_capabilities_changed, widget);
		g_clear_object (&widget->priv->player);
	}

	if (player == NULL)
	{
		gtk_widget_hide (GTK_WIDGET (widget));
		return;
	}

	widget->priv->player = g_object_ref (player);
	g_signal_connect (player, "metadata-changed",
	                  G_CALLBACK (on_player_metadata_changed), widget);
	g_signal_connect (player, "capabilities-changed",
	                  G_CALLBACK (on_player_capabilities_changed), widget);

	update_metadata (widget);
	update_status (widget);
	update_capabilities (widget);

	gtk_widget_show (GTK_WIDGET (widget));
}

/* Player selection */

static gboolean
is_player_allowed (GSMediaWidget *widget,
                   GSMprisPlayer *player)
{
	if (! gs_mpris_player_is_ready (player))
	{
		return FALSE;
	}

	if (gs_mpris_player_is_playing (player))
	{
		return TRUE;
	}

	if (widget->priv->allowed_players != NULL)
	{
		return g_strv_contains ((const char * const *) widget->priv->allowed_players,
		                        gs_mpris_player_get_bus_name (player));
	}

	return ! gs_mpris_player_is_stopped (player);
}

/* Prefer a playing player, and otherwise stick with the one already shown
 * so that two paused players don't make the widget jump around. */
static void
update_player (GSMediaWidget *widget)
{
	GSMprisPlayer *current = widget->priv->player;
	GSMprisPlayer *best = NULL;
	GList         *players;
	GList         *l;

	if (widget->priv->manager == NULL)
	{
		return;
	}

	players = gs_mpris_manager_get_players (widget->priv->manager);
	for (l = players; l != NULL; l = l->next)
	{
		GSMprisPlayer *player = l->data;

		if (! is_player_allowed (widget, player))
		{
			continue;
		}

		if (gs_mpris_player_is_playing (player))
		{
			best = player;
			break;
		}

		if (best == NULL)
		{
			best = player;
		}
	}
	g_list_free (players);

	if (current != NULL &&
	    is_player_allowed (widget, current) &&
	    (best == NULL || gs_mpris_player_is_playing (current) || ! gs_mpris_player_is_playing (best)))
	{
		best = current;
	}

	/* Once shown, keep a player allowed: pausing it from the lock screen
	 * must not make it disappear before it can be resumed. */
	if (best != NULL && widget->priv->allowed_players != NULL &&
	    ! g_strv_contains ((const char * const *) widget->priv->allowed_players,
	                       gs_mpris_player_get_bus_name (best)))
	{
		guint n = g_strv_length (widget->priv->allowed_players);

		widget->priv->allowed_players = g_renew (char *, widget->priv->allowed_players, n + 2);
		widget->priv->allowed_players[n] = g_strdup (gs_mpris_player_get_bus_name (best));
		widget->priv->allowed_players[n + 1] = NULL;
	}

	set_player (widget, best);
}

static void
on_player_status_changed (GSMprisPlayer *player,
                          GSMediaWidget *widget)
{
	update_player (widget);
}

static void
on_manager_player_added (GSMprisManager *manager,
                         GSMprisPlayer  *player,
                         GSMediaWidget  *widget)
{
	g_signal_connect (player, "status-changed",
	                  G_CALLBACK (on_player_status_changed), widget);
	update_player (widget);
}

static void
on_manager_player_removed (GSMprisManager *manager,
                           const char     *bus_name,
                           GSMediaWidget  *widget)
{
	if (widget->priv->player != NULL &&
	    g_strcmp0 (gs_mpris_player_get_bus_name (widget->priv->player), bus_name) == 0)
	{
		set_player (widget, NULL);
	}

	update_player (widget);
}

/* Controls */

static void
on_prev_clicked (GtkButton     *button,
                 GSMediaWidget *widget)
{
	g_signal_emit (widget, signals [ACTIVITY], 0);
	gs_mpris_player_previous (widget->priv->player);
}

static void
on_play_clicked (GtkButton     *button,
                 GSMediaWidget *widget)
{
	g_signal_emit (widget, signals [ACTIVITY], 0);
	gs_mpris_player_play_pause (widget->priv->player);
}

static void
on_next_clicked (GtkButton     *button,
                 GSMediaWidget *widget)
{
	g_signal_emit (widget, signals [ACTIVITY], 0);
	gs_mpris_player_next (widget->priv->player);
}

static void
seek_to (GSMediaWidget *widget,
         gdouble        seconds)
{
	gint64 position = (gint64) seconds * G_USEC_PER_SEC;

	g_signal_emit (widget, signals [ACTIVITY], 0);
	gs_mpris_player_seek_to (widget->priv->player, position, widget->priv->position);
	widget->priv->position = position;
}

static gboolean
on_seek_button_press (GtkWidget      *scale,
                      GdkEventButton *event,
                      GSMediaWidget  *widget)
{
	widget->priv->seek_dragging = TRUE;

	return FALSE;
}

/* the seek is only sent once the slider is let go of */
static gboolean
on_seek_button_release (GtkWidget      *scale,
                        GdkEventButton *event,
                        GSMediaWidget  *widget)
{
	widget->priv->seek_dragging = FALSE;

	if (widget->priv->player != NULL)
	{
		seek_to (widget, gtk_range_get_value (GTK_RANGE (scale)));
	}

	return FALSE;
}

static void
on_seek_value_changed (GtkRange      *scale,
                       GSMediaWidget *widget)
{
	if (widget->priv->updating_seek || widget->priv->player == NULL)
	{
		return;
	}

	set_time_label (widget, (gint64) gtk_range_get_value (scale));
}

static gboolean
on_seek_scroll_event (GtkWidget      *scale,
                      GdkEventScroll *event,
                      GSMediaWidget  *widget)
{
	GtkAdjustment *adjustment;
	gdouble        delta = 0.0;
	gdouble        value;

	if (widget->priv->player == NULL ||
	    ! gs_mpris_player_get_can_seek (widget->priv->player))
	{
		return TRUE;
	}

	switch (event->direction)
	{
	case GDK_SCROLL_UP:
	case GDK_SCROLL_RIGHT:
		delta = SEEK_STEP;
		break;
	case GDK_SCROLL_DOWN:
	case GDK_SCROLL_LEFT:
		delta = -SEEK_STEP;
		break;
	case GDK_SCROLL_SMOOTH:
		delta = (event->delta_y != 0.0 ? -event->delta_y : event->delta_x) * SEEK_STEP;
		break;
	default:
		break;
	}

	if (delta == 0.0)
	{
		return TRUE;
	}

	adjustment = gtk_range_get_adjustment (GTK_RANGE (scale));
	value = CLAMP (gtk_range_get_value (GTK_RANGE (scale)) + delta,
	               gtk_adjustment_get_lower (adjustment),
	               gtk_adjustment_get_upper (adjustment));
	gtk_range_set_value (GTK_RANGE (scale), value);
	seek_to (widget, value);

	return TRUE;
}

/* Buttons never take focus, so typing keeps going to the password entry */
static GtkWidget *
new_icon_button (const char *icon_name,
                 const char *tooltip)
{
	GtkWidget *button;

	button = gtk_button_new_from_icon_name (icon_name, GTK_ICON_SIZE_BUTTON);
	gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);
	gtk_widget_set_can_focus (button, FALSE);
	gtk_widget_set_tooltip_text (button, tooltip);

	return button;
}

static GtkWidget *
new_info_label (void)
{
	GtkWidget *label;

	label = gtk_label_new (NULL);
	gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
	gtk_label_set_line_wrap_mode (GTK_LABEL (label), PANGO_WRAP_WORD_CHAR);
	gtk_label_set_lines (GTK_LABEL (label), 2);
	gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
	gtk_label_set_max_width_chars (GTK_LABEL (label), 24);
	gtk_label_set_justify (GTK_LABEL (label), GTK_JUSTIFY_CENTER);

	return label;
}

static void
gs_media_widget_destroy (GtkWidget *object)
{
	GSMediaWidget *widget = GS_MEDIA_WIDGET (object);

	cancel_cover_load (widget);
	remove_position_timer (widget);

	if (widget->priv->manager != NULL)
	{
		GList *players;
		GList *l;

		players = gs_mpris_manager_get_players (widget->priv->manager);
		for (l = players; l != NULL; l = l->next)
		{
			g_signal_handlers_disconnect_by_data (l->data, widget);
		}
		g_list_free (players);

		g_signal_handlers_disconnect_by_data (widget->priv->manager, widget);
		g_clear_object (&widget->priv->manager);
	}

	if (widget->priv->player != NULL)
	{
		g_signal_handlers_disconnect_by_data (widget->priv->player, widget);
		g_clear_object (&widget->priv->player);
	}

	GTK_WIDGET_CLASS (gs_media_widget_parent_class)->destroy (object);
}

static void
gs_media_widget_class_init (GSMediaWidgetClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->finalize = gs_media_widget_finalize;
	widget_class->destroy = gs_media_widget_destroy;

	signals [ACTIVITY] =
	    g_signal_new ("activity",
	                  G_TYPE_FROM_CLASS (klass),
	                  G_SIGNAL_RUN_LAST,
	                  G_STRUCT_OFFSET (GSMediaWidgetClass, activity),
	                  NULL, NULL,
	                  g_cclosure_marshal_VOID__VOID,
	                  G_TYPE_NONE, 0);
}

static void
gs_media_widget_init (GSMediaWidget *widget)
{
	GtkStyleContext *context;
	PangoAttrList   *attrs;

	widget->priv = gs_media_widget_get_instance_private (widget);
	widget->priv->parts = GS_MEDIA_WIDGET_PART_ALL;

	/* only shown once there is a player to show */
	gtk_widget_set_no_show_all (GTK_WIDGET (widget), TRUE);
	gtk_orientable_set_orientation (GTK_ORIENTABLE (widget), GTK_ORIENTATION_VERTICAL);
	gtk_box_set_spacing (GTK_BOX (widget), 6);
	gtk_widget_set_size_request (GTK_WIDGET (widget), WIDGET_WIDTH, -1);
	gtk_widget_set_valign (GTK_WIDGET (widget), GTK_ALIGN_CENTER);

	context = gtk_widget_get_style_context (GTK_WIDGET (widget));
	gtk_style_context_add_class (context, "media-controls");

	widget->priv->player_label = gtk_label_new (NULL);
	gtk_label_set_ellipsize (GTK_LABEL (widget->priv->player_label), PANGO_ELLIPSIZE_END);
	gtk_label_set_max_width_chars (GTK_LABEL (widget->priv->player_label), 24);
	gtk_style_context_add_class (gtk_widget_get_style_context (widget->priv->player_label),
	                             GTK_STYLE_CLASS_DIM_LABEL);
	gtk_box_pack_start (GTK_BOX (widget), widget->priv->player_label, FALSE, FALSE, 0);

	widget->priv->cover = gtk_image_new ();
	gtk_widget_set_size_request (widget->priv->cover, COVER_SIZE, COVER_SIZE);
	gtk_widget_set_halign (widget->priv->cover, GTK_ALIGN_CENTER);
	set_cover_fallback (widget);
	gtk_box_pack_start (GTK_BOX (widget), widget->priv->cover, FALSE, FALSE, 0);

	widget->priv->info_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
	widget->priv->title_label = new_info_label ();
	widget->priv->artist_label = new_info_label ();
	widget->priv->album_label = new_info_label ();
	gtk_style_context_add_class (gtk_widget_get_style_context (widget->priv->album_label),
	                             GTK_STYLE_CLASS_DIM_LABEL);
	attrs = pango_attr_list_new ();
	pango_attr_list_insert (attrs, pango_attr_weight_new (PANGO_WEIGHT_BOLD));
	gtk_label_set_attributes (GTK_LABEL (widget->priv->title_label), attrs);
	pango_attr_list_unref (attrs);
	gtk_box_pack_start (GTK_BOX (widget->priv->info_box), widget->priv->title_label, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (widget->priv->info_box), widget->priv->artist_label, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (widget->priv->info_box), widget->priv->album_label, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (widget), widget->priv->info_box, FALSE, FALSE, 0);

	widget->priv->progress_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	widget->priv->progress_bar = gtk_progress_bar_new ();
	gtk_widget_set_valign (widget->priv->progress_bar, GTK_ALIGN_CENTER);
	widget->priv->time_label = gtk_label_new ("0:00 / 0:00");
	gtk_style_context_add_class (gtk_widget_get_style_context (widget->priv->time_label),
	                             GTK_STYLE_CLASS_DIM_LABEL);
	widget->priv->seek_scale = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL, 0, 1, 1);
	gtk_scale_set_draw_value (GTK_SCALE (widget->priv->seek_scale), FALSE);
	/* keep keyboard focus on the password entry */
	gtk_widget_set_can_focus (widget->priv->seek_scale, FALSE);
	gtk_widget_set_tooltip_text (widget->priv->seek_scale, _("Seek"));
	gtk_box_pack_start (GTK_BOX (widget->priv->progress_box), widget->priv->progress_bar, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (widget->priv->progress_box), widget->priv->seek_scale, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (widget->priv->progress_box), widget->priv->time_label, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (widget), widget->priv->progress_box, FALSE, FALSE, 0);

	widget->priv->controls = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 2);
	gtk_widget_set_halign (widget->priv->controls, GTK_ALIGN_CENTER);
	widget->priv->prev_button = new_icon_button ("media-skip-backward-symbolic", _("Previous"));
	widget->priv->play_button = new_icon_button ("media-playback-start-symbolic", _("Play"));
	widget->priv->next_button = new_icon_button ("media-skip-forward-symbolic", _("Next"));
	gtk_box_pack_start (GTK_BOX (widget->priv->controls), widget->priv->prev_button, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (widget->priv->controls), widget->priv->play_button, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (widget->priv->controls), widget->priv->next_button, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (widget), widget->priv->controls, FALSE, FALSE, 0);

	g_signal_connect (widget->priv->prev_button, "clicked", G_CALLBACK (on_prev_clicked), widget);
	g_signal_connect (widget->priv->play_button, "clicked", G_CALLBACK (on_play_clicked), widget);
	g_signal_connect (widget->priv->next_button, "clicked", G_CALLBACK (on_next_clicked), widget);
	g_signal_connect (widget->priv->seek_scale, "button-press-event", G_CALLBACK (on_seek_button_press), widget);
	g_signal_connect (widget->priv->seek_scale, "button-release-event", G_CALLBACK (on_seek_button_release), widget);
	g_signal_connect (widget->priv->seek_scale, "value-changed", G_CALLBACK (on_seek_value_changed), widget);
	g_signal_connect (widget->priv->seek_scale, "scroll-event", G_CALLBACK (on_seek_scroll_event), widget);

	/* the widget itself is no-show-all, so show its contents explicitly */
	gtk_widget_show_all (widget->priv->info_box);
	gtk_widget_show_all (widget->priv->progress_box);
	gtk_widget_show_all (widget->priv->controls);
	gtk_widget_show (widget->priv->player_label);
	gtk_widget_show (widget->priv->cover);

	widget->priv->manager = gs_mpris_manager_new ();
	g_signal_connect (widget->priv->manager, "player-added",
	                  G_CALLBACK (on_manager_player_added), widget);
	g_signal_connect (widget->priv->manager, "player-removed",
	                  G_CALLBACK (on_manager_player_removed), widget);
}

static void
gs_media_widget_finalize (GObject *object)
{
	GSMediaWidget *widget = GS_MEDIA_WIDGET (object);

	g_strfreev (widget->priv->allowed_players);

	G_OBJECT_CLASS (gs_media_widget_parent_class)->finalize (object);
}

GtkWidget *
gs_media_widget_new (void)
{
	return g_object_new (GS_TYPE_MEDIA_WIDGET, NULL);
}

void
gs_media_widget_set_parts (GSMediaWidget     *widget,
                           GSMediaWidgetParts parts)
{
	g_return_if_fail (GS_IS_MEDIA_WIDGET (widget));

	if (widget->priv->parts == parts)
	{
		return;
	}

	widget->priv->parts = parts;

	if (widget->priv->player != NULL)
	{
		update_metadata (widget);
		update_capabilities (widget);
	}
}

void
gs_media_widget_set_allowed_players (GSMediaWidget      *widget,
                                     const char * const *bus_names)
{
	g_return_if_fail (GS_IS_MEDIA_WIDGET (widget));

	g_strfreev (widget->priv->allowed_players);
	widget->priv->allowed_players = g_strdupv ((char **) bus_names);

	update_player (widget);
}
