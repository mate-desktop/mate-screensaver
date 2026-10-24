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
 * Ported from mate-media's gvc-mpris-player.c
 *
 */

#include "config.h"

#include <string.h>

#include "gs-mpris-player.h"

#define MPRIS_PREFIX          "org.mpris.MediaPlayer2."
#define MPRIS_OBJECT_PATH     "/org/mpris/MediaPlayer2"
#define MPRIS_SERVER_IFACE    "org.mpris.MediaPlayer2"
#define MPRIS_PLAYER_IFACE    "org.mpris.MediaPlayer2.Player"
#define DBUS_PROPERTIES_IFACE "org.freedesktop.DBus.Properties"

static void gs_mpris_player_finalize (GObject *object);

struct GSMprisPlayerPrivate
{
	char       *bus_name;
	GDBusProxy *server_proxy;
	GDBusProxy *player_proxy;
	guint       pending_proxies;
	gboolean    ready;

	char       *identity;
	char       *playback_status;
	char       *title;
	char       *artist;
	char       *album;
	char       *art_url;
	char       *track_id;
	gint64      length;

	gboolean    can_control;
	gboolean    can_seek;
	gboolean    can_play;
	gboolean    can_pause;
	gboolean    can_go_next;
	gboolean    can_go_previous;
};

enum
{
    READY,
    METADATA_CHANGED,
    STATUS_CHANGED,
    CAPABILITIES_CHANGED,
    LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE_WITH_PRIVATE (GSMprisPlayer, gs_mpris_player, G_TYPE_OBJECT)

static char *
variant_dup_string (GVariant *variant)
{
	if (variant == NULL)
	{
		return g_strdup ("");
	}

	if (g_variant_is_of_type (variant, G_VARIANT_TYPE_STRING) ||
	    g_variant_is_of_type (variant, G_VARIANT_TYPE_OBJECT_PATH))
	{
		return g_variant_dup_string (variant, NULL);
	}

	if (g_variant_is_of_type (variant, G_VARIANT_TYPE_STRING_ARRAY))
	{
		const char **strv;
		char        *joined;

		strv = g_variant_get_strv (variant, NULL);
		joined = g_strjoinv (", ", (char **) strv);
		g_free (strv);
		return joined;
	}

	return g_strdup ("");
}

static gboolean
get_cached_boolean (GDBusProxy *proxy,
                    const char *name)
{
	GVariant *value;
	gboolean  result = FALSE;

	value = g_dbus_proxy_get_cached_property (proxy, name);
	if (value != NULL)
	{
		if (g_variant_is_of_type (value, G_VARIANT_TYPE_BOOLEAN))
		{
			result = g_variant_get_boolean (value);
		}
		g_variant_unref (value);
	}

	return result;
}

static char *
get_cached_string (GDBusProxy *proxy,
                   const char *name)
{
	GVariant *value;
	char     *result = NULL;

	value = g_dbus_proxy_get_cached_property (proxy, name);
	if (value != NULL)
	{
		result = variant_dup_string (value);
		g_variant_unref (value);
	}

	return result;
}

static void
update_metadata (GSMprisPlayer *player,
                 GVariant      *metadata)
{
	GVariantIter iter;
	const char  *key;
	GVariant    *value;

	if (metadata == NULL || !g_variant_is_of_type (metadata, G_VARIANT_TYPE_VARDICT))
	{
		return;
	}

	g_free (player->priv->title);
	g_free (player->priv->artist);
	g_free (player->priv->album);
	g_free (player->priv->art_url);
	g_clear_pointer (&player->priv->track_id, g_free);
	player->priv->title = g_strdup ("");
	player->priv->artist = g_strdup ("");
	player->priv->album = g_strdup ("");
	player->priv->art_url = g_strdup ("");
	player->priv->length = 0;

	g_variant_iter_init (&iter, metadata);
	while (g_variant_iter_next (&iter, "{&sv}", &key, &value))
	{
		if (g_str_equal (key, "xesam:title"))
		{
			g_free (player->priv->title);
			player->priv->title = variant_dup_string (value);
		}
		else if (g_str_equal (key, "xesam:artist"))
		{
			g_free (player->priv->artist);
			player->priv->artist = variant_dup_string (value);
		}
		else if (g_str_equal (key, "xesam:album"))
		{
			g_free (player->priv->album);
			player->priv->album = variant_dup_string (value);
		}
		else if (g_str_equal (key, "mpris:artUrl"))
		{
			g_free (player->priv->art_url);
			player->priv->art_url = variant_dup_string (value);
		}
		else if (g_str_equal (key, "mpris:trackid") &&
		         g_variant_is_of_type (value, G_VARIANT_TYPE_OBJECT_PATH))
		{
			g_free (player->priv->track_id);
			player->priv->track_id = g_variant_dup_string (value, NULL);
		}
		else if (g_str_equal (key, "mpris:length"))
		{
			/* the spec says int64, but some players send uint64 */
			if (g_variant_is_of_type (value, G_VARIANT_TYPE_INT64))
			{
				player->priv->length = g_variant_get_int64 (value);
			}
			else if (g_variant_is_of_type (value, G_VARIANT_TYPE_UINT64))
			{
				player->priv->length = (gint64) g_variant_get_uint64 (value);
			}
		}

		g_variant_unref (value);
	}
}

static void
update_capabilities (GSMprisPlayer *player)
{
	GDBusProxy *proxy = player->priv->player_proxy;

	if (proxy == NULL)
	{
		return;
	}

	player->priv->can_control = get_cached_boolean (proxy, "CanControl");
	player->priv->can_play = get_cached_boolean (proxy, "CanPlay");
	player->priv->can_pause = get_cached_boolean (proxy, "CanPause");
	player->priv->can_go_next = get_cached_boolean (proxy, "CanGoNext");
	player->priv->can_go_previous = get_cached_boolean (proxy, "CanGoPrevious");
	player->priv->can_seek = get_cached_boolean (proxy, "CanSeek");
}

static void
read_initial_state (GSMprisPlayer *player)
{
	GVariant *metadata;
	char     *value;

	value = get_cached_string (player->priv->server_proxy, "Identity");
	g_free (player->priv->identity);
	if (value != NULL && value[0] != '\0')
	{
		player->priv->identity = value;
	}
	else
	{
		/* fall back to a prettified bus name, e.g. "Vlc" */
		char *name;

		g_free (value);
		name = g_strdup (player->priv->bus_name + strlen (MPRIS_PREFIX));
		if (name[0] != '\0')
		{
			name[0] = g_ascii_toupper (name[0]);
		}
		player->priv->identity = name;
	}

	g_free (player->priv->playback_status);
	player->priv->playback_status = get_cached_string (player->priv->player_proxy, "PlaybackStatus");
	if (player->priv->playback_status == NULL || player->priv->playback_status[0] == '\0')
	{
		g_free (player->priv->playback_status);
		player->priv->playback_status = g_strdup ("Stopped");
	}

	metadata = g_dbus_proxy_get_cached_property (player->priv->player_proxy, "Metadata");
	update_metadata (player, metadata);
	if (metadata != NULL)
	{
		g_variant_unref (metadata);
	}

	update_capabilities (player);
}

static void
on_proxy_properties_changed (GDBusProxy    *proxy,
                             GVariant      *changed_properties,
                             const char   **invalidated_properties,
                             GSMprisPlayer *player)
{
	gboolean     metadata_changed = FALSE;
	gboolean     status_changed = FALSE;
	gboolean     capabilities_changed = FALSE;
	gboolean     player_iface;
	GVariantIter iter;
	const char  *key;
	GVariant    *value;

	player_iface = (proxy == player->priv->player_proxy);

	g_variant_iter_init (&iter, changed_properties);
	while (g_variant_iter_next (&iter, "{&sv}", &key, &value))
	{
		if (player_iface && g_str_equal (key, "Metadata"))
		{
			update_metadata (player, value);
			metadata_changed = TRUE;
		}
		else if (player_iface && g_str_equal (key, "PlaybackStatus"))
		{
			g_free (player->priv->playback_status);
			player->priv->playback_status = variant_dup_string (value);
			status_changed = TRUE;
		}
		else if (!player_iface && g_str_equal (key, "Identity"))
		{
			g_free (player->priv->identity);
			player->priv->identity = variant_dup_string (value);
			metadata_changed = TRUE;
		}
		else if (player_iface && g_str_has_prefix (key, "Can"))
		{
			capabilities_changed = TRUE;
		}

		g_variant_unref (value);
	}

	if (! player->priv->ready)
	{
		return;
	}

	if (capabilities_changed)
	{
		update_capabilities (player);
		g_signal_emit (player, signals [CAPABILITIES_CHANGED], 0);
	}
	if (metadata_changed)
	{
		g_signal_emit (player, signals [METADATA_CHANGED], 0);
	}
	if (status_changed)
	{
		g_signal_emit (player, signals [STATUS_CHANGED], 0);
	}
}

static void
on_proxy_ready (GObject      *source_object,
                GAsyncResult *res,
                gpointer      user_data)
{
	GSMprisPlayer *player = GS_MPRIS_PLAYER (user_data);
	GDBusProxy    *proxy;
	GError        *error = NULL;

	proxy = g_dbus_proxy_new_for_bus_finish (res, &error);
	if (proxy == NULL)
	{
		g_debug ("Unable to create MPRIS proxy for %s: %s",
		         player->priv->bus_name,
		         error->message);
		g_error_free (error);
	}
	else
	{
		if (g_str_equal (g_dbus_proxy_get_interface_name (proxy), MPRIS_SERVER_IFACE))
		{
			player->priv->server_proxy = proxy;
		}
		else
		{
			player->priv->player_proxy = proxy;
		}

		g_signal_connect (proxy,
		                  "g-properties-changed",
		                  G_CALLBACK (on_proxy_properties_changed),
		                  player);
	}

	player->priv->pending_proxies--;

	if (player->priv->pending_proxies == 0 &&
	    player->priv->server_proxy != NULL &&
	    player->priv->player_proxy != NULL)
	{
		read_initial_state (player);
		player->priv->ready = TRUE;
		g_signal_emit (player, signals [READY], 0);
	}

	g_object_unref (player);
}

static void
player_call (GSMprisPlayer *player,
             const char    *method,
             GVariant      *parameters)
{
	if (player->priv->player_proxy == NULL)
	{
		if (parameters != NULL)
		{
			g_variant_unref (g_variant_ref_sink (parameters));
		}
		return;
	}

	g_dbus_proxy_call (player->priv->player_proxy,
	                   method,
	                   parameters,
	                   G_DBUS_CALL_FLAGS_NONE,
	                   -1,
	                   NULL,
	                   NULL,
	                   NULL);
}

static void
gs_mpris_player_dispose (GObject *object)
{
	GSMprisPlayer *player = GS_MPRIS_PLAYER (object);

	if (player->priv->server_proxy != NULL)
	{
		g_signal_handlers_disconnect_by_data (player->priv->server_proxy, player);
		g_clear_object (&player->priv->server_proxy);
	}
	if (player->priv->player_proxy != NULL)
	{
		g_signal_handlers_disconnect_by_data (player->priv->player_proxy, player);
		g_clear_object (&player->priv->player_proxy);
	}

	G_OBJECT_CLASS (gs_mpris_player_parent_class)->dispose (object);
}

static void
gs_mpris_player_class_init (GSMprisPlayerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = gs_mpris_player_dispose;
	object_class->finalize = gs_mpris_player_finalize;

	signals [READY] =
	    g_signal_new ("ready",
	                  G_TYPE_FROM_CLASS (klass),
	                  G_SIGNAL_RUN_LAST,
	                  0, NULL, NULL, NULL,
	                  G_TYPE_NONE, 0);
	signals [METADATA_CHANGED] =
	    g_signal_new ("metadata-changed",
	                  G_TYPE_FROM_CLASS (klass),
	                  G_SIGNAL_RUN_LAST,
	                  0, NULL, NULL, NULL,
	                  G_TYPE_NONE, 0);
	signals [STATUS_CHANGED] =
	    g_signal_new ("status-changed",
	                  G_TYPE_FROM_CLASS (klass),
	                  G_SIGNAL_RUN_LAST,
	                  0, NULL, NULL, NULL,
	                  G_TYPE_NONE, 0);
	signals [CAPABILITIES_CHANGED] =
	    g_signal_new ("capabilities-changed",
	                  G_TYPE_FROM_CLASS (klass),
	                  G_SIGNAL_RUN_LAST,
	                  0, NULL, NULL, NULL,
	                  G_TYPE_NONE, 0);
}

static void
gs_mpris_player_init (GSMprisPlayer *player)
{
	player->priv = gs_mpris_player_get_instance_private (player);
	player->priv->pending_proxies = 2;
	player->priv->playback_status = g_strdup ("Stopped");
	player->priv->title = g_strdup ("");
	player->priv->artist = g_strdup ("");
	player->priv->album = g_strdup ("");
	player->priv->art_url = g_strdup ("");
}

static void
gs_mpris_player_finalize (GObject *object)
{
	GSMprisPlayer *player = GS_MPRIS_PLAYER (object);

	g_free (player->priv->bus_name);
	g_free (player->priv->identity);
	g_free (player->priv->playback_status);
	g_free (player->priv->title);
	g_free (player->priv->artist);
	g_free (player->priv->album);
	g_free (player->priv->art_url);
	g_free (player->priv->track_id);

	G_OBJECT_CLASS (gs_mpris_player_parent_class)->finalize (object);
}

GSMprisPlayer *
gs_mpris_player_new (const char *bus_name)
{
	GSMprisPlayer *player;

	g_return_val_if_fail (bus_name != NULL, NULL);

	player = g_object_new (GS_TYPE_MPRIS_PLAYER, NULL);
	player->priv->bus_name = g_strdup (bus_name);

	g_dbus_proxy_new_for_bus (G_BUS_TYPE_SESSION,
	                          G_DBUS_PROXY_FLAGS_NONE,
	                          NULL,
	                          bus_name,
	                          MPRIS_OBJECT_PATH,
	                          MPRIS_SERVER_IFACE,
	                          NULL,
	                          on_proxy_ready,
	                          g_object_ref (player));
	g_dbus_proxy_new_for_bus (G_BUS_TYPE_SESSION,
	                          G_DBUS_PROXY_FLAGS_NONE,
	                          NULL,
	                          bus_name,
	                          MPRIS_OBJECT_PATH,
	                          MPRIS_PLAYER_IFACE,
	                          NULL,
	                          on_proxy_ready,
	                          g_object_ref (player));

	return player;
}

const char *
gs_mpris_player_get_bus_name (GSMprisPlayer *player)
{
	g_return_val_if_fail (GS_IS_MPRIS_PLAYER (player), NULL);

	return player->priv->bus_name;
}

gboolean
gs_mpris_player_is_ready (GSMprisPlayer *player)
{
	g_return_val_if_fail (GS_IS_MPRIS_PLAYER (player), FALSE);

	return player->priv->ready;
}

gboolean
gs_mpris_player_is_playing (GSMprisPlayer *player)
{
	g_return_val_if_fail (GS_IS_MPRIS_PLAYER (player), FALSE);

	return g_strcmp0 (player->priv->playback_status, "Playing") == 0;
}

gboolean
gs_mpris_player_is_stopped (GSMprisPlayer *player)
{
	g_return_val_if_fail (GS_IS_MPRIS_PLAYER (player), TRUE);

	return g_strcmp0 (player->priv->playback_status, "Stopped") == 0;
}

const char *
gs_mpris_player_get_identity (GSMprisPlayer *player)
{
	return player->priv->identity ? player->priv->identity : "";
}

const char *
gs_mpris_player_get_title (GSMprisPlayer *player)
{
	return player->priv->title ? player->priv->title : "";
}

const char *
gs_mpris_player_get_artist (GSMprisPlayer *player)
{
	return player->priv->artist ? player->priv->artist : "";
}

const char *
gs_mpris_player_get_album (GSMprisPlayer *player)
{
	return player->priv->album ? player->priv->album : "";
}

const char *
gs_mpris_player_get_art_url (GSMprisPlayer *player)
{
	return player->priv->art_url ? player->priv->art_url : "";
}

gint64
gs_mpris_player_get_length (GSMprisPlayer *player)
{
	return player->priv->length;
}

gboolean
gs_mpris_player_get_can_control (GSMprisPlayer *player)
{
	return player->priv->can_control;
}

gboolean
gs_mpris_player_get_can_go_next (GSMprisPlayer *player)
{
	return player->priv->can_control && player->priv->can_go_next;
}

gboolean
gs_mpris_player_get_can_go_previous (GSMprisPlayer *player)
{
	return player->priv->can_control && player->priv->can_go_previous;
}

gboolean
gs_mpris_player_get_can_seek (GSMprisPlayer *player)
{
	return player->priv->can_control && player->priv->can_seek;
}

gboolean
gs_mpris_player_get_can_play (GSMprisPlayer *player)
{
	return player->priv->can_control && player->priv->can_play;
}

gboolean
gs_mpris_player_get_can_pause (GSMprisPlayer *player)
{
	return player->priv->can_control && player->priv->can_pause;
}

void
gs_mpris_player_play_pause (GSMprisPlayer *player)
{
	g_return_if_fail (GS_IS_MPRIS_PLAYER (player));

	player_call (player, "PlayPause", NULL);
}

void
gs_mpris_player_next (GSMprisPlayer *player)
{
	g_return_if_fail (GS_IS_MPRIS_PLAYER (player));

	if (gs_mpris_player_get_can_go_next (player))
	{
		player_call (player, "Next", NULL);
	}
}

void
gs_mpris_player_previous (GSMprisPlayer *player)
{
	g_return_if_fail (GS_IS_MPRIS_PLAYER (player));

	if (gs_mpris_player_get_can_go_previous (player))
	{
		player_call (player, "Previous", NULL);
	}
}

void
gs_mpris_player_seek_to (GSMprisPlayer *player,
                         gint64         position,
                         gint64         current_position)
{
	g_return_if_fail (GS_IS_MPRIS_PLAYER (player));

	if (! gs_mpris_player_get_can_seek (player))
	{
		return;
	}

	if (player->priv->track_id != NULL &&
	    g_variant_is_object_path (player->priv->track_id))
	{
		player_call (player, "SetPosition",
		             g_variant_new ("(ox)", player->priv->track_id, position));
	}
	else
	{
		player_call (player, "Seek",
		             g_variant_new ("(x)", position - current_position));
	}
}

typedef struct
{
	GSMprisPlayer          *player;
	GSMprisPositionCallback callback;
	gpointer                user_data;
} PositionData;

static void
on_get_position_ready (GObject      *source_object,
                       GAsyncResult *res,
                       gpointer      user_data)
{
	PositionData *data = user_data;
	GVariant     *result;
	GError       *error = NULL;
	gint64        position = 0;

	result = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source_object), res, &error);
	if (result != NULL)
	{
		GVariant *value;

		g_variant_get (result, "(v)", &value);
		if (g_variant_is_of_type (value, G_VARIANT_TYPE_INT64))
		{
			position = g_variant_get_int64 (value);
		}
		g_variant_unref (value);
		g_variant_unref (result);
	}

	if (data->callback != NULL)
	{
		data->callback (data->player, position, error, data->user_data);
	}

	if (error != NULL)
	{
		g_error_free (error);
	}
	g_object_unref (data->player);
	g_free (data);
}

/* Position is not covered by PropertiesChanged, so it has to be polled */
void
gs_mpris_player_get_position_async (GSMprisPlayer          *player,
                                    GSMprisPositionCallback callback,
                                    gpointer                user_data)
{
	PositionData *data;

	g_return_if_fail (GS_IS_MPRIS_PLAYER (player));

	if (player->priv->player_proxy == NULL)
	{
		return;
	}

	data = g_new0 (PositionData, 1);
	data->player = g_object_ref (player);
	data->callback = callback;
	data->user_data = user_data;

	g_dbus_connection_call (g_dbus_proxy_get_connection (player->priv->player_proxy),
	                        player->priv->bus_name,
	                        MPRIS_OBJECT_PATH,
	                        DBUS_PROPERTIES_IFACE,
	                        "Get",
	                        g_variant_new ("(ss)", MPRIS_PLAYER_IFACE, "Position"),
	                        G_VARIANT_TYPE ("(v)"),
	                        G_DBUS_CALL_FLAGS_NONE,
	                        -1,
	                        NULL,
	                        on_get_position_ready,
	                        data);
}
