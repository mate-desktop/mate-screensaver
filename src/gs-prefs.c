/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2004-2006 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2012-2021 MATE Developers
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
 * Authors: William Jon McCann <mccann@jhu.edu>
 *
 */

#include "config.h"

#include <string.h>

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

#include "gs-prefs.h"

static void gs_prefs_finalize   (GObject      *object);

#define LOCKDOWN_SETTINGS_SCHEMA "org.mate.lockdown"
#define KEY_LOCK_DISABLE "disable-lock-screen"
#define KEY_USER_SWITCH_DISABLE "disable-user-switching"

#define SESSION_SETTINGS_SCHEMA "org.mate.session"
#define KEY_IDLE_DELAY "idle-delay"

#define GSETTINGS_SCHEMA "org.mate.screensaver"
#define KEY_IDLE_ACTIVATION_ENABLED "idle-activation-enabled"
#define KEY_LOCK_ENABLED "lock-enabled"
#define KEY_MODE "mode"
#define KEY_POWER_DELAY "power-management-delay"
#define KEY_LOCK_DELAY "lock-delay"
#define KEY_CYCLE_DELAY "cycle-delay"
#define KEY_THEMES "themes"
#define KEY_USER_SWITCH_ENABLED "user-switch-enabled"
#define KEY_LOGOUT_ENABLED "logout-enabled"
#define KEY_LOGOUT_DELAY "logout-delay"
#define KEY_LOGOUT_COMMAND "logout-command"
#define KEY_KEYBOARD_ENABLED "embedded-keyboard-enabled"
#define KEY_KEYBOARD_COMMAND "embedded-keyboard-command"
#define KEY_STATUS_MESSAGE_ENABLED "status-message-enabled"

#define _gs_prefs_set_idle_activation_enabled(x,y) ((x)->idle_activation_enabled = ((y) != FALSE))
#define _gs_prefs_set_lock_enabled(x,y) ((x)->lock_enabled = ((y) != FALSE))
#define _gs_prefs_set_lock_disabled(x,y) ((x)->lock_disabled = ((y) != FALSE))
#define _gs_prefs_set_user_switch_disabled(x,y) ((x)->user_switch_disabled = ((y) != FALSE))
#define _gs_prefs_set_keyboard_enabled(x,y) ((x)->keyboard_enabled = ((y) != FALSE))
#define _gs_prefs_set_status_message_enabled(x,y) ((x)->status_message_enabled = ((y) != FALSE))
#define _gs_prefs_set_logout_enabled(x,y) ((x)->logout_enabled = ((y) != FALSE))
#define _gs_prefs_set_user_switch_enabled(x,y) ((x)->user_switch_enabled = ((y) != FALSE))

struct GSPrefsPrivate
{
	GSettings *settings;
	GSettings *lockdown_settings;
	GSettings *session_settings;
};

enum
{
    CHANGED,
    LAST_SIGNAL
};

enum
{
    PROP_0
};

static guint         signals [LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE_WITH_PRIVATE (GSPrefs, gs_prefs, G_TYPE_OBJECT)

static void
gs_prefs_set_property (GObject            *object,
                       guint               prop_id,
                       const GValue       *value,
                       GParamSpec         *pspec)
{
	switch (prop_id)
	{
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_prefs_get_property (GObject            *object,
                       guint               prop_id,
                       GValue             *value,
                       GParamSpec         *pspec)
{
	switch (prop_id)
	{
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_prefs_class_init (GSPrefsClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize     = gs_prefs_finalize;
	object_class->get_property = gs_prefs_get_property;
	object_class->set_property = gs_prefs_set_property;

	signals [CHANGED] =
	    g_signal_new ("changed",
	                  G_TYPE_FROM_CLASS (object_class),
	                  G_SIGNAL_RUN_LAST,
	                  G_STRUCT_OFFSET (GSPrefsClass, changed),
	                  NULL,
	                  NULL,
	                  g_cclosure_marshal_VOID__VOID,
	                  G_TYPE_NONE,
	                  0);
}

static void
_gs_prefs_set_timeout (GSPrefs *prefs,
                       int      value)
{
	if (value < 1)
		value = 10;

	/* pick a reasonable large number for the
	   upper bound */
	if (value > 480)
		value = 480;

	prefs->timeout = value * 60000;
}

static void
_gs_prefs_set_power_timeout (GSPrefs *prefs,
                             int      value)
{
	if (value < 1)
		value = 60;

	/* pick a reasonable large number for the
	   upper bound */
	if (value > 480)
		value = 480;

	/* this value is in seconds - others are in minutes */
	prefs->power_timeout = value * 1000;
}

static void
_gs_prefs_set_lock_timeout (GSPrefs *prefs,
                            int      value)
{
	if (value < 0)
		value = 0;

	/* pick a reasonable large number for the
	   upper bound */
	if (value > 480)
		value = 480;

	prefs->lock_timeout = value * 60000;
}

static void
_gs_prefs_set_cycle_timeout (GSPrefs *prefs,
                             int      value)
{
	if (value < 1)
		value = 1;

	/* pick a reasonable large number for the
	   upper bound */
	if (value > 480)
		value = 480;

	prefs->cycle = value * 60000;
}

static void
_gs_prefs_set_mode (GSPrefs    *prefs,
                    gint         mode)
{
	prefs->mode = mode;
}

static void
_gs_prefs_set_themes (GSPrefs *prefs,
                      gchar **values)
{
	guint i;
	if (prefs->themes)
	{
		g_slist_free_full (prefs->themes, g_free);
	}

	/* take ownership of the list */
	prefs->themes = NULL;
        for (i=0; values[i] != NULL; i++)
		prefs->themes = g_slist_append (prefs->themes, g_strdup (values[i]));
}

static void
_gs_prefs_set_keyboard_command (GSPrefs    *prefs,
                                const char *value)
{
	g_free (prefs->keyboard_command);
	prefs->keyboard_command = NULL;

	if (value)
	{
		/* FIXME: check command */

		prefs->keyboard_command = g_strdup (value);
	}
}

static void
_gs_prefs_set_logout_command (GSPrefs    *prefs,
                              const char *value)
{
	g_free (prefs->logout_command);
	prefs->logout_command = NULL;

	if (value)
	{
		/* FIXME: check command */

		prefs->logout_command = g_strdup (value);
	}
}

static void
_gs_prefs_set_logout_timeout (GSPrefs *prefs,
                              int      value)
{
	if (value < 0)
		value = 0;

	/* pick a reasonable large number for the
	   upper bound */
	if (value > 480)
		value = 480;

	prefs->logout_timeout = value * 60000;
}

static void
gs_prefs_load_from_settings (GSPrefs *prefs)
{
	glong      value;
	gboolean   bvalue;
	char      *string;
	gchar    **strv;
	gint       mode;

	bvalue = g_settings_get_boolean (prefs->priv->settings, KEY_IDLE_ACTIVATION_ENABLED);
	_gs_prefs_set_idle_activation_enabled (prefs, bvalue);

	bvalue = g_settings_get_boolean (prefs->priv->settings, KEY_LOCK_ENABLED);
	_gs_prefs_set_lock_enabled (prefs, bvalue);

	bvalue = g_settings_get_boolean (prefs->priv->lockdown_settings, KEY_LOCK_DISABLE);
	_gs_prefs_set_lock_disabled (prefs, bvalue);

	bvalue = g_settings_get_boolean (prefs->priv->lockdown_settings, KEY_USER_SWITCH_DISABLE);
	_gs_prefs_set_user_switch_disabled (prefs, bvalue);

	value = g_settings_get_int (prefs->priv->session_settings, KEY_IDLE_DELAY);
	_gs_prefs_set_timeout (prefs, value);

	value = g_settings_get_int (prefs->priv->settings, KEY_POWER_DELAY);
	_gs_prefs_set_power_timeout (prefs, value);

	value = g_settings_get_int (prefs->priv->settings, KEY_LOCK_DELAY);
	_gs_prefs_set_lock_timeout (prefs, value);

	value = g_settings_get_int (prefs->priv->settings, KEY_CYCLE_DELAY);
	_gs_prefs_set_cycle_timeout (prefs, value);

	mode = g_settings_get_enum (prefs->priv->settings, KEY_MODE);
	_gs_prefs_set_mode (prefs, mode);

	strv = g_settings_get_strv (prefs->priv->settings, KEY_THEMES);
	_gs_prefs_set_themes (prefs, strv);
	g_strfreev (strv);

	/* Embedded keyboard options */

	bvalue = g_settings_get_boolean (prefs->priv->settings, KEY_KEYBOARD_ENABLED);
	_gs_prefs_set_keyboard_enabled (prefs, bvalue);

	string = g_settings_get_string (prefs->priv->settings, KEY_KEYBOARD_COMMAND);
	_gs_prefs_set_keyboard_command (prefs, string);
	g_free (string);

	bvalue = g_settings_get_boolean (prefs->priv->settings, KEY_STATUS_MESSAGE_ENABLED);
	_gs_prefs_set_status_message_enabled (prefs, bvalue);

	/* Logout options */

	bvalue = g_settings_get_boolean (prefs->priv->settings, KEY_LOGOUT_ENABLED);
	_gs_prefs_set_logout_enabled (prefs, bvalue);

	string = g_settings_get_string (prefs->priv->settings, KEY_LOGOUT_COMMAND);
	_gs_prefs_set_logout_command (prefs, string);
	g_free (string);

	value = g_settings_get_int (prefs->priv->settings, KEY_LOGOUT_DELAY);
	_gs_prefs_set_logout_timeout (prefs, value);

	/* User switching options */

	bvalue = g_settings_get_boolean (prefs->priv->settings, KEY_USER_SWITCH_ENABLED);
	_gs_prefs_set_user_switch_enabled (prefs, bvalue);
}

static void
key_changed_cb (GSettings *settings,
		gchar *key,
		GSPrefs *prefs)
{
	if (strcmp (key, KEY_MODE) == 0)
	{
		gint mode;

		mode = g_settings_get_enum (settings, key);
		_gs_prefs_set_mode (prefs, mode);

	}
	else if (strcmp (key, KEY_THEMES) == 0)
	{
		gchar **strv = NULL;

		strv = g_settings_get_strv (settings, key);
		_gs_prefs_set_themes (prefs, strv);
		g_strfreev (strv);

	}
	else if (strcmp (key, KEY_IDLE_DELAY) == 0)
	{
		int delay;

		delay = g_settings_get_int (settings, key);
		_gs_prefs_set_timeout (prefs, delay);

	}
	else if (strcmp (key, KEY_POWER_DELAY) == 0)
	{
		int delay;

		delay = g_settings_get_int (settings, key);
		_gs_prefs_set_power_timeout (prefs, delay);

	}
	else if (strcmp (key, KEY_LOCK_DELAY) == 0)
	{
		int delay;

		delay = g_settings_get_int (settings, key);
		_gs_prefs_set_lock_timeout (prefs, delay);
	}
	else if (strcmp (key, KEY_IDLE_ACTIVATION_ENABLED) == 0)
	{
		gboolean enabled;

		enabled = g_settings_get_boolean (settings, key);
		_gs_prefs_set_idle_activation_enabled (prefs, enabled);

	}
	else if (strcmp (key, KEY_LOCK_ENABLED) == 0)
	{
		gboolean enabled;

		enabled = g_settings_get_boolean (settings, key);
		_gs_prefs_set_lock_enabled (prefs, enabled);

	}
	else if (strcmp (key, KEY_LOCK_DISABLE) == 0)
	{
		gboolean disabled;

		disabled = g_settings_get_boolean (settings, key);
		_gs_prefs_set_lock_disabled (prefs, disabled);

	}
	else if (strcmp (key, KEY_USER_SWITCH_DISABLE) == 0)
	{
		gboolean disabled;

		disabled = g_settings_get_boolean (settings, key);
		_gs_prefs_set_user_switch_disabled (prefs, disabled);

	}
	else if (strcmp (key, KEY_CYCLE_DELAY) == 0)
	{
		int delay;

		delay = g_settings_get_int (settings, key);
		_gs_prefs_set_cycle_timeout (prefs, delay);

	}
	else if (strcmp (key, KEY_KEYBOARD_ENABLED) == 0)
	{
		gboolean enabled;

		enabled = g_settings_get_boolean (settings, key);
		_gs_prefs_set_keyboard_enabled (prefs, enabled);

	}
	else if (strcmp (key, KEY_KEYBOARD_COMMAND) == 0)
	{
		char *command;

		command = g_settings_get_string (settings, key);
		_gs_prefs_set_keyboard_command (prefs, command);
		g_free (command);

	}
	else if (strcmp (key, KEY_STATUS_MESSAGE_ENABLED) == 0)
	{
		gboolean enabled;

		enabled = g_settings_get_boolean (settings, key);
		_gs_prefs_set_status_message_enabled (prefs, enabled);

	}
	else if (strcmp (key, KEY_LOGOUT_ENABLED) == 0)
	{
		gboolean enabled;

		enabled = g_settings_get_boolean (settings, key);
		_gs_prefs_set_logout_enabled (prefs, enabled);

	}
	else if (strcmp (key, KEY_LOGOUT_DELAY) == 0)
	{
		int delay;

		delay = g_settings_get_int (settings, key);
		_gs_prefs_set_logout_timeout (prefs, delay);

	}
	else if (strcmp (key, KEY_LOGOUT_COMMAND) == 0)
	{
		char *command;
		command = g_settings_get_string (settings, key);
		_gs_prefs_set_logout_command (prefs, command);
		g_free (command);
	}
	else if (strcmp (key, KEY_USER_SWITCH_ENABLED) == 0)
	{
		gboolean enabled;

		enabled = g_settings_get_boolean (settings, key);
		_gs_prefs_set_user_switch_enabled (prefs, enabled);

	}
	else
	{
		g_warning ("Config key not handled: %s", key);
	}

	g_signal_emit (prefs, signals [CHANGED], 0);
}

static void
gs_prefs_init (GSPrefs *prefs)
{
	prefs->priv = gs_prefs_get_instance_private (prefs);

	prefs->priv->settings = g_settings_new (GSETTINGS_SCHEMA);
	g_signal_connect (prefs->priv->settings,
			  "changed",
			  G_CALLBACK (key_changed_cb),
			  prefs);
	prefs->priv->lockdown_settings = g_settings_new (LOCKDOWN_SETTINGS_SCHEMA);
	g_signal_connect (prefs->priv->lockdown_settings,
			  "changed",
			  G_CALLBACK (key_changed_cb),
			  prefs);
	prefs->priv->session_settings = g_settings_new (SESSION_SETTINGS_SCHEMA);
	g_signal_connect (prefs->priv->session_settings,
			  "changed::" KEY_IDLE_DELAY,
			  G_CALLBACK (key_changed_cb),
			  prefs);

	prefs->idle_activation_enabled = TRUE;
	prefs->lock_enabled            = TRUE;
	prefs->lock_disabled           = FALSE;
	prefs->logout_enabled          = FALSE;
	prefs->user_switch_enabled     = FALSE;

	prefs->timeout                 = 600000;
	prefs->power_timeout           = 60000;
	prefs->lock_timeout            = 0;
	prefs->logout_timeout          = 14400000;
	prefs->cycle                   = 600000;

	prefs->mode                    = GS_MODE_SINGLE;

	gs_prefs_load_from_settings (prefs);
}

static void
gs_prefs_finalize (GObject *object)
{
	GSPrefs *prefs;

	g_return_if_fail (object != NULL);
	g_return_if_fail (GS_IS_PREFS (object));

	prefs = GS_PREFS (object);

	g_return_if_fail (prefs->priv != NULL);

	if (prefs->priv->settings)
	{
		g_object_unref (prefs->priv->settings);
		prefs->priv->settings = NULL;
	}

	if (prefs->priv->lockdown_settings) {
		g_object_unref (prefs->priv->lockdown_settings);
		prefs->priv->lockdown_settings = NULL;
	}

	if (prefs->priv->session_settings) {
		g_object_unref (prefs->priv->session_settings);
		prefs->priv->session_settings = NULL;
	}

	if (prefs->themes)
	{
		g_slist_free_full (prefs->themes, g_free);
	}

	g_free (prefs->logout_command);
	g_free (prefs->keyboard_command);

	G_OBJECT_CLASS (gs_prefs_parent_class)->finalize (object);
}

GSPrefs *
gs_prefs_new (void)
{
	GObject *prefs;

	prefs = g_object_new (GS_TYPE_PREFS, NULL);

	return GS_PREFS (prefs);
}
