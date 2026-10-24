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
 * Ported from mate-media's gvc-mpris-manager.c
 *
 */

#include "config.h"

#include <string.h>

#include "gs-mpris-manager.h"

#define DBUS_NAME    "org.freedesktop.DBus"
#define DBUS_PATH    "/org/freedesktop/DBus"
#define DBUS_IFACE   "org.freedesktop.DBus"
#define MPRIS_PREFIX "org.mpris.MediaPlayer2."

struct GSMprisManagerPrivate
{
	GDBusConnection *connection;
	GCancellable    *cancellable;
	GHashTable      *players;
	guint            name_owner_changed_id;
};

enum
{
    PLAYER_ADDED,
    PLAYER_REMOVED,
    LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE_WITH_PRIVATE (GSMprisManager, gs_mpris_manager, G_TYPE_OBJECT)

static gboolean
is_mpris_name (const char *name)
{
	return name != NULL && g_str_has_prefix (name, MPRIS_PREFIX);
}

static void
on_player_ready (GSMprisPlayer  *player,
                 GSMprisManager *manager)
{
	g_signal_emit (manager, signals [PLAYER_ADDED], 0, player);
}

static void
manager_add_player (GSMprisManager *manager,
                    const char     *bus_name)
{
	GSMprisPlayer *player;

	if (manager->priv->players == NULL ||
	    g_hash_table_contains (manager->priv->players, bus_name))
	{
		return;
	}

	player = gs_mpris_player_new (bus_name);
	g_signal_connect (player, "ready", G_CALLBACK (on_player_ready), manager);
	g_hash_table_insert (manager->priv->players, g_strdup (bus_name), player);
}

static void
manager_remove_player (GSMprisManager *manager,
                       const char     *bus_name)
{
	GSMprisPlayer *player;
	gboolean       was_ready;

	if (manager->priv->players == NULL)
	{
		return;
	}

	player = g_hash_table_lookup (manager->priv->players, bus_name);
	if (player == NULL)
	{
		return;
	}

	/* a pending proxy may keep the player alive; don't let it report in later */
	g_signal_handlers_disconnect_by_data (player, manager);
	was_ready = gs_mpris_player_is_ready (player);

	g_hash_table_remove (manager->priv->players, bus_name);

	if (was_ready)
	{
		g_signal_emit (manager, signals [PLAYER_REMOVED], 0, bus_name);
	}
}

static void
on_list_names_ready (GObject      *source_object,
                     GAsyncResult *res,
                     gpointer      user_data)
{
	GSMprisManager *manager;
	GVariant       *result;
	GError         *error = NULL;
	char          **names = NULL;
	int             i;

	result = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source_object), res, &error);
	if (result == NULL)
	{
		if (! g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
		{
			g_debug ("Unable to list MPRIS players: %s", error->message);
		}
		g_error_free (error);
		return;
	}

	manager = GS_MPRIS_MANAGER (user_data);

	g_variant_get (result, "(^as)", &names);
	for (i = 0; names != NULL && names[i] != NULL; i++)
	{
		if (is_mpris_name (names[i]))
		{
			manager_add_player (manager, names[i]);
		}
	}
	g_strfreev (names);
	g_variant_unref (result);
}

static void
on_name_owner_changed (GDBusConnection *connection,
                       const char      *sender_name,
                       const char      *object_path,
                       const char      *interface_name,
                       const char      *signal_name,
                       GVariant        *parameters,
                       gpointer         user_data)
{
	GSMprisManager *manager = GS_MPRIS_MANAGER (user_data);
	const char     *name;
	const char     *old_owner;
	const char     *new_owner;

	g_variant_get (parameters, "(&s&s&s)", &name, &old_owner, &new_owner);

	if (! is_mpris_name (name))
	{
		return;
	}

	if (old_owner[0] != '\0')
	{
		manager_remove_player (manager, name);
	}
	if (new_owner[0] != '\0')
	{
		manager_add_player (manager, name);
	}
}

static void
on_bus_ready (GObject      *source_object,
              GAsyncResult *res,
              gpointer      user_data)
{
	GSMprisManager  *manager;
	GDBusConnection *connection;
	GError          *error = NULL;

	connection = g_bus_get_finish (res, &error);
	if (connection == NULL)
	{
		if (! g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
		{
			g_warning ("Unable to connect to the session bus for MPRIS: %s", error->message);
		}
		g_error_free (error);
		return;
	}

	manager = GS_MPRIS_MANAGER (user_data);
	manager->priv->connection = connection;

	manager->priv->name_owner_changed_id =
	    g_dbus_connection_signal_subscribe (connection,
	                                        DBUS_NAME,
	                                        DBUS_IFACE,
	                                        "NameOwnerChanged",
	                                        DBUS_PATH,
	                                        NULL,
	                                        G_DBUS_SIGNAL_FLAGS_NONE,
	                                        on_name_owner_changed,
	                                        manager,
	                                        NULL);

	g_dbus_connection_call (connection,
	                        DBUS_NAME,
	                        DBUS_PATH,
	                        DBUS_IFACE,
	                        "ListNames",
	                        NULL,
	                        G_VARIANT_TYPE ("(as)"),
	                        G_DBUS_CALL_FLAGS_NONE,
	                        -1,
	                        manager->priv->cancellable,
	                        on_list_names_ready,
	                        manager);
}

static void
gs_mpris_manager_dispose (GObject *object)
{
	GSMprisManager *manager = GS_MPRIS_MANAGER (object);

	if (manager->priv->cancellable != NULL)
	{
		g_cancellable_cancel (manager->priv->cancellable);
		g_clear_object (&manager->priv->cancellable);
	}

	if (manager->priv->connection != NULL &&
	    manager->priv->name_owner_changed_id != 0)
	{
		g_dbus_connection_signal_unsubscribe (manager->priv->connection,
		                                      manager->priv->name_owner_changed_id);
		manager->priv->name_owner_changed_id = 0;
	}
	g_clear_object (&manager->priv->connection);

	if (manager->priv->players != NULL)
	{
		GHashTableIter iter;
		gpointer       value;

		g_hash_table_iter_init (&iter, manager->priv->players);
		while (g_hash_table_iter_next (&iter, NULL, &value))
		{
			g_signal_handlers_disconnect_by_data (value, manager);
		}
		g_clear_pointer (&manager->priv->players, g_hash_table_unref);
	}

	G_OBJECT_CLASS (gs_mpris_manager_parent_class)->dispose (object);
}

static void
gs_mpris_manager_class_init (GSMprisManagerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = gs_mpris_manager_dispose;

	signals [PLAYER_ADDED] =
	    g_signal_new ("player-added",
	                  G_TYPE_FROM_CLASS (klass),
	                  G_SIGNAL_RUN_LAST,
	                  0, NULL, NULL, NULL,
	                  G_TYPE_NONE, 1,
	                  GS_TYPE_MPRIS_PLAYER);
	signals [PLAYER_REMOVED] =
	    g_signal_new ("player-removed",
	                  G_TYPE_FROM_CLASS (klass),
	                  G_SIGNAL_RUN_LAST,
	                  0, NULL, NULL, NULL,
	                  G_TYPE_NONE, 1,
	                  G_TYPE_STRING);
}

static void
gs_mpris_manager_init (GSMprisManager *manager)
{
	manager->priv = gs_mpris_manager_get_instance_private (manager);
	manager->priv->cancellable = g_cancellable_new ();
	manager->priv->players = g_hash_table_new_full (g_str_hash,
	                                                g_str_equal,
	                                                g_free,
	                                                g_object_unref);
}

GSMprisManager *
gs_mpris_manager_new (void)
{
	GSMprisManager *manager;

	manager = g_object_new (GS_TYPE_MPRIS_MANAGER, NULL);

	g_bus_get (G_BUS_TYPE_SESSION,
	           manager->priv->cancellable,
	           on_bus_ready,
	           manager);

	return manager;
}

GList *
gs_mpris_manager_get_players (GSMprisManager *manager)
{
	GHashTableIter iter;
	gpointer       value;
	GList         *players = NULL;

	g_return_val_if_fail (GS_IS_MPRIS_MANAGER (manager), NULL);

	if (manager->priv->players == NULL)
	{
		return NULL;
	}

	g_hash_table_iter_init (&iter, manager->priv->players);
	while (g_hash_table_iter_next (&iter, NULL, &value))
	{
		if (gs_mpris_player_is_ready (value))
		{
			players = g_list_prepend (players, value);
		}
	}

	return players;
}

char **
gs_mpris_manager_get_playing_players (GSMprisManager *manager)
{
	GPtrArray *names;
	GList     *players;
	GList     *l;

	g_return_val_if_fail (GS_IS_MPRIS_MANAGER (manager), NULL);

	names = g_ptr_array_new ();

	players = gs_mpris_manager_get_players (manager);
	for (l = players; l != NULL; l = l->next)
	{
		if (gs_mpris_player_is_playing (l->data))
		{
			g_ptr_array_add (names, g_strdup (gs_mpris_player_get_bus_name (l->data)));
		}
	}
	g_list_free (players);

	g_ptr_array_add (names, NULL);

	return (char **) g_ptr_array_free (names, FALSE);
}
