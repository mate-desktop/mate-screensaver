/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2004-2008 William Jon McCann <mccann@jhu.edu>
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

#include <time.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>

#include <gio/gio.h>

#define MATE_DESKTOP_USE_UNSTABLE_API
#include <libmate-desktop/mate-bg.h>

#include "gs-prefs.h"        /* for GSSaverMode */

#include "gs-manager.h"
#include "gs-window.h"
#include "gs-theme-manager.h"
#include "gs-job.h"
#include "gs-grab.h"
#include "gs-fade.h"
#include "gs-debug.h"

static void gs_manager_finalize   (GObject        *object);

struct GSManagerPrivate
{
	GSList      *windows;
	GHashTable  *jobs;

	GSThemeManager *theme_manager;
	MateBG        *bg;

	/* Policy */
	glong        lock_timeout;
	glong        cycle_timeout;
	glong        logout_timeout;

	guint        lock_enabled : 1;
	guint        logout_enabled : 1;
	guint        keyboard_enabled : 1;
	guint        user_switch_enabled : 1;
	guint        throttled : 1;

	char        *logout_command;
	char        *keyboard_command;

	char        *status_message;

	/* State */
	guint        active : 1;
	guint        lock_active : 1;

	guint        fading : 1;
	guint        dialog_up : 1;

	time_t       activate_time;

	guint        lock_timeout_id;
	guint        cycle_timeout_id;

	GSList      *themes;
	GSSaverMode  saver_mode;
	GSGrab      *grab;
	GSFade      *fade;
	guint        unfade_idle_id;
};

enum
{
    ACTIVATED,
    DEACTIVATED,
    AUTH_REQUEST_BEGIN,
    AUTH_REQUEST_END,
    LAST_SIGNAL
};

enum
{
    PROP_0,
    PROP_LOCK_ENABLED,
    PROP_LOGOUT_ENABLED,
    PROP_USER_SWITCH_ENABLED,
    PROP_KEYBOARD_ENABLED,
    PROP_LOCK_TIMEOUT,
    PROP_CYCLE_TIMEOUT,
    PROP_LOGOUT_TIMEOUT,
    PROP_LOGOUT_COMMAND,
    PROP_KEYBOARD_COMMAND,
    PROP_STATUS_MESSAGE,
    PROP_ACTIVE,
    PROP_THROTTLED,
};

#define FADE_TIMEOUT 250

static guint         signals [LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE_WITH_PRIVATE (GSManager, gs_manager, G_TYPE_OBJECT)

static void
manager_add_job_for_window (GSManager *manager,
                            GSWindow  *window,
                            GSJob     *job)
{
	if (manager->priv->jobs == NULL)
	{
		return;
	}

	g_hash_table_insert (manager->priv->jobs, window, job);
}

static const char *
select_theme (GSManager *manager)
{
	const char *theme = NULL;

	g_return_val_if_fail (manager != NULL, NULL);
	g_return_val_if_fail (GS_IS_MANAGER (manager), NULL);

	if (manager->priv->saver_mode == GS_MODE_BLANK_ONLY)
	{
		return NULL;
	}

	if (manager->priv->themes)
	{
		int number = 0;

		if (manager->priv->saver_mode == GS_MODE_RANDOM)
		{
			g_random_set_seed (time (NULL));
			number = g_random_int_range (0, g_slist_length (manager->priv->themes));
		}
		theme = g_slist_nth_data (manager->priv->themes, number);
	}

	return theme;
}

static GSJob *
lookup_job_for_window (GSManager *manager,
                       GSWindow  *window)
{
	GSJob *job;

	if (manager->priv->jobs == NULL)
	{
		return NULL;
	}

	job = g_hash_table_lookup (manager->priv->jobs, window);

	return job;
}

static void
manager_maybe_stop_job_for_window (GSManager *manager,
                                   GSWindow  *window)
{
	GSJob *job;

	job = lookup_job_for_window (manager, window);

	if (job == NULL)
	{
		gs_debug ("Job not found for window");
		return;
	}

	gs_job_stop (job);
}

static void
manager_maybe_start_job_for_window (GSManager *manager,
                                    GSWindow  *window)
{
	GSJob *job;

	job = lookup_job_for_window (manager, window);

	if (job == NULL)
	{
		gs_debug ("Job not found for window");
		return;
	}

	if (! manager->priv->dialog_up)
	{
		if (! manager->priv->throttled)
		{
			if (! gs_job_is_running (job))
			{
				if (! gs_window_is_obscured (window))
				{
					gs_debug ("Starting job for window");
					gs_job_start (job);
				}
				else
				{
					gs_debug ("Window is obscured deferring start of job");
				}
			}
			else
			{
				gs_debug ("Not starting job because job is running");
			}
		}
		else
		{
			gs_debug ("Not starting job because throttled");
		}
	}
	else
	{
		gs_debug ("Not starting job because dialog is up");
	}
}

static void
manager_select_theme_for_job (GSManager *manager,
                              GSJob     *job)
{
	const char *theme;

	theme = select_theme (manager);

	if (theme != NULL)
	{
		GSThemeInfo    *info;
		const char     *command;

		command = NULL;

		info = gs_theme_manager_lookup_theme_info (manager->priv->theme_manager, theme);
		if (info != NULL)
		{
			command = gs_theme_info_get_exec (info);
		}
		else
		{
			gs_debug ("Could not find information for theme: %s",
			          theme);
		}

		gs_job_set_command (job, command);

		if (info != NULL)
		{
			gs_theme_info_unref (info);
		}
	}
	else
	{
		gs_job_set_command (job, NULL);
	}
}

static void
cycle_job (GSWindow  *window,
           GSJob     *job,
           GSManager *manager)
{
	gs_job_stop (job);
	manager_select_theme_for_job (manager, job);
	manager_maybe_start_job_for_window (manager, window);
}

static void
manager_cycle_jobs (GSManager *manager)
{
	if (manager->priv->jobs != NULL)
	{
		g_hash_table_foreach (manager->priv->jobs, (GHFunc) cycle_job, manager);
	}
}

static void
throttle_job (GSWindow  *window,
              GSJob     *job,
              GSManager *manager)
{
	if (manager->priv->throttled)
	{
		gs_job_stop (job);
	}
	else
	{
		manager_maybe_start_job_for_window (manager, window);
	}
}

static void
manager_throttle_jobs (GSManager *manager)
{
	if (manager->priv->jobs != NULL)
	{
		g_hash_table_foreach (manager->priv->jobs, (GHFunc) throttle_job, manager);
	}
}

static void
resume_job (GSWindow  *window,
            GSJob     *job,
            GSManager *manager)
{
	if (gs_job_is_running (job))
	{
		gs_job_suspend (job, FALSE);
	}
	else
	{
		manager_maybe_start_job_for_window (manager, window);
	}
}

static void
manager_resume_jobs (GSManager *manager)
{
	if (manager->priv->jobs != NULL)
	{
		g_hash_table_foreach (manager->priv->jobs, (GHFunc) resume_job, manager);
	}
}

static void
suspend_job (GSWindow  *window,
             GSJob     *job,
             GSManager *manager)
{
	gs_job_suspend (job, TRUE);
}

static void
manager_suspend_jobs (GSManager *manager)
{
	if (manager->priv->jobs != NULL)
	{
		g_hash_table_foreach (manager->priv->jobs, (GHFunc) suspend_job, manager);
	}
}

static void
manager_stop_jobs (GSManager *manager)
{
	if (manager->priv->jobs != NULL)
	{
		g_hash_table_destroy (manager->priv->jobs);

	}
	manager->priv->jobs = NULL;
}

void
gs_manager_set_mode (GSManager  *manager,
                     GSSaverMode mode)
{
	g_return_if_fail (GS_IS_MANAGER (manager));

	manager->priv->saver_mode = mode;
}

static void
free_themes (GSManager *manager)
{
	if (manager->priv->themes)
	{
		g_slist_free_full (manager->priv->themes, g_free);
	}
}

void
gs_manager_set_themes (GSManager *manager,
                       GSList    *themes)
{
	GSList *l;

	g_return_if_fail (GS_IS_MANAGER (manager));

	free_themes (manager);
	manager->priv->themes = NULL;

	for (l = themes; l; l = l->next)
	{
		manager->priv->themes = g_slist_append (manager->priv->themes, g_strdup (l->data));
	}
}

void
gs_manager_set_throttled (GSManager *manager,
                          gboolean   throttled)
{
	g_return_if_fail (GS_IS_MANAGER (manager));

	if (manager->priv->throttled != throttled)
	{
		GSList *l;

		manager->priv->throttled = (throttled != FALSE);

		if (! manager->priv->dialog_up)
		{

			manager_throttle_jobs (manager);

			for (l = manager->priv->windows; l; l = l->next)
			{
				gs_window_clear (l->data);
			}
		}
	}
}

void
gs_manager_get_lock_active (GSManager *manager,
                            gboolean  *lock_active)
{
	if (lock_active != NULL)
	{
		*lock_active = FALSE;
	}

	g_return_if_fail (GS_IS_MANAGER (manager));

	if (lock_active != NULL)
	{
		*lock_active = manager->priv->lock_active;
	}
}

void
gs_manager_set_lock_active (GSManager *manager,
                            gboolean   lock_active)
{
	g_return_if_fail (GS_IS_MANAGER (manager));

	gs_debug ("Setting lock active: %d", lock_active);

	if (manager->priv->lock_active != lock_active)
	{
		GSList *l;

		manager->priv->lock_active = (lock_active != FALSE);
		for (l = manager->priv->windows; l; l = l->next)
		{
			gs_window_set_lock_enabled (l->data, lock_active);
		}
	}
}

void
gs_manager_get_lock_enabled (GSManager *manager,
                             gboolean  *lock_enabled)
{
	if (lock_enabled != NULL)
	{
		*lock_enabled = FALSE;
	}

	g_return_if_fail (GS_IS_MANAGER (manager));

	if (lock_enabled != NULL)
	{
		*lock_enabled = manager->priv->lock_enabled;
	}
}

void
gs_manager_set_lock_enabled (GSManager *manager,
                             gboolean   lock_enabled)
{
	g_return_if_fail (GS_IS_MANAGER (manager));

	if (manager->priv->lock_enabled != lock_enabled)
	{
		manager->priv->lock_enabled = (lock_enabled != FALSE);
	}
}

void
gs_manager_set_logout_enabled (GSManager *manager,
                               gboolean   logout_enabled)
{
	g_return_if_fail (GS_IS_MANAGER (manager));

	if (manager->priv->logout_enabled != logout_enabled)
	{
		GSList *l;

		manager->priv->logout_enabled = (logout_enabled != FALSE);
		for (l = manager->priv->windows; l; l = l->next)
		{
			gs_window_set_logout_enabled (l->data, logout_enabled);
		}
	}
}

void
gs_manager_set_keyboard_enabled (GSManager *manager,
                                 gboolean   enabled)
{
	g_return_if_fail (GS_IS_MANAGER (manager));

	if (manager->priv->keyboard_enabled != enabled)
	{
		GSList *l;

		manager->priv->keyboard_enabled = (enabled != FALSE);
		for (l = manager->priv->windows; l; l = l->next)
		{
			gs_window_set_keyboard_enabled (l->data, enabled);
		}
	}
}

void
gs_manager_set_user_switch_enabled (GSManager *manager,
                                    gboolean   user_switch_enabled)
{
	g_return_if_fail (GS_IS_MANAGER (manager));

	if (manager->priv->user_switch_enabled != user_switch_enabled)
	{
		GSList *l;

		manager->priv->user_switch_enabled = (user_switch_enabled != FALSE);
		for (l = manager->priv->windows; l; l = l->next)
		{
			gs_window_set_user_switch_enabled (l->data, user_switch_enabled);
		}
	}
}

static gboolean
activate_lock_timeout (GSManager *manager)
{
	if (manager->priv->lock_enabled)
	{
		gs_manager_set_lock_active (manager, TRUE);
	}

	manager->priv->lock_timeout_id = 0;

	return FALSE;
}

static void
remove_lock_timer (GSManager *manager)
{
	if (manager->priv->lock_timeout_id != 0)
	{
		g_source_remove (manager->priv->lock_timeout_id);
		manager->priv->lock_timeout_id = 0;
	}
}

static void
add_lock_timer (GSManager *manager,
                glong      timeout)
{
	manager->priv->lock_timeout_id = g_timeout_add (timeout,
	                                 (GSourceFunc)activate_lock_timeout,
	                                 manager);
}

void
gs_manager_set_lock_timeout (GSManager *manager,
                             glong      lock_timeout)
{
	g_return_if_fail (GS_IS_MANAGER (manager));

	if (manager->priv->lock_timeout != lock_timeout)
	{

		manager->priv->lock_timeout = lock_timeout;

		if (manager->priv->active
		        && ! manager->priv->lock_active
		        && (lock_timeout >= 0))
		{

			glong elapsed = (time (NULL) - manager->priv->activate_time) * 1000;

			remove_lock_timer (manager);

			if (elapsed >= lock_timeout)
			{
				activate_lock_timeout (manager);
			}
			else
			{
				add_lock_timer (manager, lock_timeout - elapsed);
			}
		}
	}
}

void
gs_manager_set_logout_timeout (GSManager *manager,
                               glong      logout_timeout)
{
	g_return_if_fail (GS_IS_MANAGER (manager));

	if (manager->priv->logout_timeout != logout_timeout)
	{
		GSList *l;

		manager->priv->logout_timeout = logout_timeout;
		for (l = manager->priv->windows; l; l = l->next)
		{
			gs_window_set_logout_timeout (l->data, logout_timeout);
		}
	}
}

void
gs_manager_set_logout_command (GSManager  *manager,
                               const char *command)
{
	GSList *l;

	g_return_if_fail (GS_IS_MANAGER (manager));

	g_free (manager->priv->logout_command);

	if (command)
	{
		manager->priv->logout_command = g_strdup (command);
	}
	else
	{
		manager->priv->logout_command = NULL;
	}

	for (l = manager->priv->windows; l; l = l->next)
	{
		gs_window_set_logout_command (l->data, manager->priv->logout_command);
	}
}

void
gs_manager_set_keyboard_command (GSManager  *manager,
                                 const char *command)
{
	GSList *l;

	g_return_if_fail (GS_IS_MANAGER (manager));

	g_free (manager->priv->keyboard_command);

	if (command)
	{
		manager->priv->keyboard_command = g_strdup (command);
	}
	else
	{
		manager->priv->keyboard_command = NULL;
	}

	for (l = manager->priv->windows; l; l = l->next)
	{
		gs_window_set_keyboard_command (l->data, manager->priv->keyboard_command);
	}
}

void
gs_manager_set_status_message (GSManager  *manager,
                               const char *status_message)
{
	GSList *l;

	g_return_if_fail (GS_IS_MANAGER (manager));

	g_free (manager->priv->status_message);

	manager->priv->status_message = g_strdup (status_message);

	for (l = manager->priv->windows; l; l = l->next)
	{
		gs_window_set_status_message (l->data, manager->priv->status_message);
	}
}

gboolean
gs_manager_cycle (GSManager *manager)
{
	g_return_val_if_fail (manager != NULL, FALSE);
	g_return_val_if_fail (GS_IS_MANAGER (manager), FALSE);

	gs_debug ("cycling jobs");

	if (! manager->priv->active)
	{
		return FALSE;
	}

	if (manager->priv->dialog_up)
	{
		return FALSE;
	}

	if (manager->priv->throttled)
	{
		return FALSE;
	}

	manager_cycle_jobs (manager);

	return TRUE;
}

static gboolean
cycle_timeout (GSManager *manager)
{
	g_return_val_if_fail (manager != NULL, FALSE);
	g_return_val_if_fail (GS_IS_MANAGER (manager), FALSE);

	if (! manager->priv->dialog_up)
	{
		gs_manager_cycle (manager);
	}

	return TRUE;
}

static void
remove_cycle_timer (GSManager *manager)
{
	if (manager->priv->cycle_timeout_id != 0)
	{
		g_source_remove (manager->priv->cycle_timeout_id);
		manager->priv->cycle_timeout_id = 0;
	}
}

static void
add_cycle_timer (GSManager *manager,
                 glong      timeout)
{
	manager->priv->cycle_timeout_id = g_timeout_add (timeout,
	                                  (GSourceFunc)cycle_timeout,
	                                  manager);
}

void
gs_manager_set_cycle_timeout (GSManager *manager,
                              glong      cycle_timeout)
{
	g_return_if_fail (GS_IS_MANAGER (manager));

	if (manager->priv->cycle_timeout != cycle_timeout)
	{

		manager->priv->cycle_timeout = cycle_timeout;

		if (manager->priv->active && (cycle_timeout >= 0))
		{
			glong timeout;
			glong elapsed = (time (NULL) - manager->priv->activate_time) * 1000;

			remove_cycle_timer (manager);

			if (elapsed >= cycle_timeout)
			{
				timeout = 0;
			}
			else
			{
				timeout = cycle_timeout - elapsed;
			}

			add_cycle_timer (manager, timeout);

		}
	}
}

static void
gs_manager_set_property (GObject            *object,
                         guint               prop_id,
                         const GValue       *value,
                         GParamSpec         *pspec)
{
	GSManager *self;

	self = GS_MANAGER (object);

	switch (prop_id)
	{
	case PROP_THROTTLED:
		gs_manager_set_throttled (self, g_value_get_boolean (value));
		break;
	case PROP_LOCK_ENABLED:
		gs_manager_set_lock_enabled (self, g_value_get_boolean (value));
		break;
	case PROP_LOCK_TIMEOUT:
		gs_manager_set_lock_timeout (self, g_value_get_long (value));
		break;
	case PROP_LOGOUT_ENABLED:
		gs_manager_set_logout_enabled (self, g_value_get_boolean (value));
		break;
	case PROP_KEYBOARD_ENABLED:
		gs_manager_set_keyboard_enabled (self, g_value_get_boolean (value));
		break;
	case PROP_USER_SWITCH_ENABLED:
		gs_manager_set_user_switch_enabled (self, g_value_get_boolean (value));
		break;
	case PROP_LOGOUT_TIMEOUT:
		gs_manager_set_logout_timeout (self, g_value_get_long (value));
		break;
	case PROP_LOGOUT_COMMAND:
		gs_manager_set_logout_command (self, g_value_get_string (value));
		break;
	case PROP_KEYBOARD_COMMAND:
		gs_manager_set_keyboard_command (self, g_value_get_string (value));
		break;
	case PROP_STATUS_MESSAGE:
		gs_manager_set_status_message (self, g_value_get_string (value));
		break;
	case PROP_CYCLE_TIMEOUT:
		gs_manager_set_cycle_timeout (self, g_value_get_long (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_manager_get_property (GObject            *object,
                         guint               prop_id,
                         GValue             *value,
                         GParamSpec         *pspec)
{
	GSManager *self;

	self = GS_MANAGER (object);

	switch (prop_id)
	{
	case PROP_THROTTLED:
		g_value_set_boolean (value, self->priv->throttled);
		break;
	case PROP_LOCK_ENABLED:
		g_value_set_boolean (value, self->priv->lock_enabled);
		break;
	case PROP_LOCK_TIMEOUT:
		g_value_set_long (value, self->priv->lock_timeout);
		break;
	case PROP_LOGOUT_ENABLED:
		g_value_set_boolean (value, self->priv->logout_enabled);
		break;
	case PROP_KEYBOARD_ENABLED:
		g_value_set_boolean (value, self->priv->keyboard_enabled);
		break;
	case PROP_USER_SWITCH_ENABLED:
		g_value_set_boolean (value, self->priv->user_switch_enabled);
		break;
	case PROP_LOGOUT_TIMEOUT:
		g_value_set_long (value, self->priv->logout_timeout);
		break;
	case PROP_LOGOUT_COMMAND:
		g_value_set_string (value, self->priv->logout_command);
		break;
	case PROP_KEYBOARD_COMMAND:
		g_value_set_string (value, self->priv->keyboard_command);
		break;
	case PROP_STATUS_MESSAGE:
		g_value_set_string (value, self->priv->status_message);
		break;
	case PROP_CYCLE_TIMEOUT:
		g_value_set_long (value, self->priv->cycle_timeout);
		break;
	case PROP_ACTIVE:
		g_value_set_boolean (value, self->priv->active);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_manager_class_init (GSManagerClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize     = gs_manager_finalize;
	object_class->get_property = gs_manager_get_property;
	object_class->set_property = gs_manager_set_property;

	signals [ACTIVATED] =
	    g_signal_new ("activated",
	                  G_TYPE_FROM_CLASS (object_class),
	                  G_SIGNAL_RUN_LAST,
	                  G_STRUCT_OFFSET (GSManagerClass, activated),
	                  NULL,
	                  NULL,
	                  g_cclosure_marshal_VOID__VOID,
	                  G_TYPE_NONE,
	                  0);
	signals [DEACTIVATED] =
	    g_signal_new ("deactivated",
	                  G_TYPE_FROM_CLASS (object_class),
	                  G_SIGNAL_RUN_LAST,
	                  G_STRUCT_OFFSET (GSManagerClass, deactivated),
	                  NULL,
	                  NULL,
	                  g_cclosure_marshal_VOID__VOID,
	                  G_TYPE_NONE,
	                  0);
	signals [AUTH_REQUEST_BEGIN] =
	    g_signal_new ("auth-request-begin",
	                  G_TYPE_FROM_CLASS (object_class),
	                  G_SIGNAL_RUN_LAST,
	                  G_STRUCT_OFFSET (GSManagerClass, auth_request_begin),
	                  NULL,
	                  NULL,
	                  g_cclosure_marshal_VOID__VOID,
	                  G_TYPE_NONE,
	                  0);
	signals [AUTH_REQUEST_END] =
	    g_signal_new ("auth-request-end",
	                  G_TYPE_FROM_CLASS (object_class),
	                  G_SIGNAL_RUN_LAST,
	                  G_STRUCT_OFFSET (GSManagerClass, auth_request_end),
	                  NULL,
	                  NULL,
	                  g_cclosure_marshal_VOID__VOID,
	                  G_TYPE_NONE,
	                  0);

	g_object_class_install_property (object_class,
	                                 PROP_ACTIVE,
	                                 g_param_spec_boolean ("active",
	                                         NULL,
	                                         NULL,
	                                         FALSE,
	                                         G_PARAM_READABLE));
	g_object_class_install_property (object_class,
	                                 PROP_LOCK_ENABLED,
	                                 g_param_spec_boolean ("lock-enabled",
	                                         NULL,
	                                         NULL,
	                                         FALSE,
	                                         G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
	                                 PROP_LOCK_TIMEOUT,
	                                 g_param_spec_long ("lock-timeout",
	                                         NULL,
	                                         NULL,
	                                         -1,
	                                         G_MAXLONG,
	                                         0,
	                                         G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
	                                 PROP_LOGOUT_ENABLED,
	                                 g_param_spec_boolean ("logout-enabled",
	                                         NULL,
	                                         NULL,
	                                         FALSE,
	                                         G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
	                                 PROP_USER_SWITCH_ENABLED,
	                                 g_param_spec_boolean ("user-switch-enabled",
	                                         NULL,
	                                         NULL,
	                                         FALSE,
	                                         G_PARAM_READWRITE));

	g_object_class_install_property (object_class,
	                                 PROP_LOGOUT_TIMEOUT,
	                                 g_param_spec_long ("logout-timeout",
	                                         NULL,
	                                         NULL,
	                                         -1,
	                                         G_MAXLONG,
	                                         0,
	                                         G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
	                                 PROP_LOGOUT_COMMAND,
	                                 g_param_spec_string ("logout-command",
	                                         NULL,
	                                         NULL,
	                                         NULL,
	                                         G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
	                                 PROP_CYCLE_TIMEOUT,
	                                 g_param_spec_long ("cycle-timeout",
	                                         NULL,
	                                         NULL,
	                                         10000,
	                                         G_MAXLONG,
	                                         300000,
	                                         G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
	                                 PROP_THROTTLED,
	                                 g_param_spec_boolean ("throttled",
	                                         NULL,
	                                         NULL,
	                                         TRUE,
	                                         G_PARAM_READWRITE));
}

static void
on_bg_changed (MateBG   *bg,
               GSManager *manager)
{
	gs_debug ("background changed");
}

static void
gs_manager_init (GSManager *manager)
{
	manager->priv = gs_manager_get_instance_private (manager);

	manager->priv->fade = gs_fade_new ();
	manager->priv->grab = gs_grab_new ();
	manager->priv->theme_manager = gs_theme_manager_new ();

	manager->priv->bg = mate_bg_new ();

	g_signal_connect (manager->priv->bg,
					  "changed",
					  G_CALLBACK (on_bg_changed),
					  manager);

	mate_bg_load_from_preferences (manager->priv->bg);
	GSettings *settings = g_settings_new ("org.mate.screensaver");
	char *filename= g_settings_get_string (settings, "picture-filename");
	if (g_file_test (filename, G_FILE_TEST_EXISTS)) {
		mate_bg_set_filename (manager->priv->bg, filename);
	}
	g_free (filename);
	g_object_unref (settings);
}

static void
remove_timers (GSManager *manager)
{
	remove_lock_timer (manager);
	remove_cycle_timer (manager);
}

static void
remove_unfade_idle (GSManager *manager)
{
	if (manager->priv->unfade_idle_id > 0)
	{
		g_source_remove (manager->priv->unfade_idle_id);
		manager->priv->unfade_idle_id = 0;
	}
}

static gboolean
window_deactivated_idle (gpointer data)
{
	GSManager *manager = data;

	g_return_val_if_fail (manager != NULL, FALSE);
	g_return_val_if_fail (GS_IS_MANAGER (manager), FALSE);

	/* don't deactivate directly but only emit a signal
	   so that we let the parent deactivate */
	g_signal_emit (manager, signals [DEACTIVATED], 0);

	return FALSE;
}

static void
window_deactivated_cb (GSWindow  *window,
                       GSManager *manager)
{
	g_return_if_fail (manager != NULL);
	g_return_if_fail (GS_IS_MANAGER (manager));

	g_idle_add (window_deactivated_idle, manager);
}

static GSWindow *
find_window_at_pointer (GSManager *manager)
{
	GdkDisplay *display;
	GdkDevice  *device;
	GdkMonitor *monitor;
	int         x, y;
	GSWindow   *window;
	GSList     *l;

	display = gdk_display_get_default ();

	device = gdk_seat_get_pointer (gdk_display_get_default_seat (display));
	gdk_device_get_position (device, NULL, &x, &y);
	monitor = gdk_display_get_monitor_at_point (display, x, y);

	/* Find the gs-window that is on that monitor */
	window = NULL;
	for (l = manager->priv->windows; l; l = l->next)
	{
		GSWindow *win = GS_WINDOW (l->data);
		if (gs_window_get_display (win) == display &&
		    gs_window_get_monitor (win) == monitor)
		{
			window = win;
		}
	}

	if (window == NULL)
	{
		gs_debug ("WARNING: Could not find the GSWindow for display %s",
		          gdk_display_get_name (display));
		/* take the first one */
		window = manager->priv->windows->data;
	}
	else
	{
		gs_debug ("Requesting unlock for display %s",
		          gdk_display_get_name (display));
	}

	return window;
}

void
gs_manager_show_message (GSManager  *manager,
                         const char *summary,
                         const char *body,
                         const char *icon)
{
	GSWindow *window;

	g_return_if_fail (GS_IS_MANAGER (manager));

	/* Find the GSWindow that contains the pointer */
	window = find_window_at_pointer (manager);
	gs_window_show_message (window, summary, body, icon);

	gs_manager_request_unlock (manager);
}

static gboolean
manager_maybe_grab_window (GSManager *manager,
                           GSWindow  *window)
{
	GdkDisplay *display;
	GdkDevice  *device;
	GdkMonitor *monitor;
	int         x, y;
	gboolean    grabbed;

	display = gdk_display_get_default ();
	device = gdk_seat_get_pointer (gdk_display_get_default_seat (display));
	gdk_device_get_position (device, NULL, &x, &y);
	monitor = gdk_display_get_monitor_at_point (display, x, y);

	gdk_display_flush (display);
	grabbed = FALSE;
	if (gs_window_get_display (window) == display &&
	    gs_window_get_monitor (window) == monitor)
	{
		gs_debug ("Initiate grab move to %p", window);
		gs_grab_move_to_window (manager->priv->grab,
		                        gs_window_get_gdk_window (window),
		                        gs_window_get_display (window),
		                        FALSE, FALSE);
		grabbed = TRUE;
	}

	return grabbed;
}

static void
window_grab_broken_cb (GSWindow           *window,
                       GdkEventGrabBroken *event,
                       GSManager          *manager)
{
	GdkDisplay *display;
	GdkSeat    *seat;
	GdkDevice  *device;

	display = gdk_window_get_display (gs_window_get_gdk_window (window));
	seat = gdk_display_get_default_seat (display);

	if (event->keyboard)
	{
		gs_debug ("KEYBOARD GRAB BROKEN!");
		device = gdk_seat_get_pointer (seat);
		if (!gdk_display_device_is_grabbed (display, device))
			gs_grab_reset (manager->priv->grab);
	}
	else
	{
		gs_debug ("POINTER GRAB BROKEN!");
		device = gdk_seat_get_keyboard (seat);
		if (!gdk_display_device_is_grabbed (display, device))
			gs_grab_reset (manager->priv->grab);
	}
}

static gboolean
unfade_idle (GSManager *manager)
{
	gs_debug ("resetting fade");
	gs_fade_reset (manager->priv->fade);
	manager->priv->unfade_idle_id = 0;
	return FALSE;
}

static void
add_unfade_idle (GSManager *manager)
{
	remove_unfade_idle (manager);
	manager->priv->unfade_idle_id = g_timeout_add (500, (GSourceFunc)unfade_idle, manager);
}

static gboolean
window_map_event_cb (GSWindow  *window,
                     GdkEvent  *event,
                     GSManager *manager)
{
	gs_debug ("Handling window map_event event");

	manager_maybe_grab_window (manager, window);

	manager_maybe_start_job_for_window (manager, window);

	return FALSE;
}

static void
window_map_cb (GSWindow  *window,
               GSManager *manager)
{
	gs_debug ("Handling window map event");
}

static void
window_unmap_cb (GSWindow  *window,
                 GSManager *manager)
{
	gs_debug ("window unmapped!");
}

static void
apply_background_to_window (GSManager *manager,
                            GSWindow  *window)
{
	GSettings       *settings;
	char            *filename;
	cairo_surface_t *surface;
	int              width;
	int              height;

	mate_bg_load_from_preferences(manager->priv->bg);

	settings = g_settings_new ("org.mate.screensaver");
	filename = g_settings_get_string (settings, "picture-filename");

	if (g_file_test (filename, G_FILE_TEST_EXISTS)) {
		mate_bg_set_filename (manager->priv->bg, filename);
	}
	g_free (filename);
	g_object_unref (settings);

	if (manager->priv->bg == NULL)
	{
		gs_debug ("No background available");
		gs_window_set_background_surface (window, NULL);
	}

	gtk_widget_get_preferred_width (GTK_WIDGET (window), &width, NULL);
	gtk_widget_get_preferred_height (GTK_WIDGET (window), &height, NULL);
	gs_debug ("Creating background w:%d h:%d", width, height);
	surface = mate_bg_create_surface (manager->priv->bg,
	                                  gs_window_get_gdk_window (window),
	                                  width,
	                                  height,
	                                  FALSE);
	gs_window_set_background_surface (window, surface);
	cairo_surface_destroy (surface);
}

static void
manager_show_window (GSManager *manager,
                     GSWindow  *window)
{
	GSJob *job;

	apply_background_to_window (manager, window);

	job = gs_job_new_for_widget (gs_window_get_drawing_area (window));

	manager_select_theme_for_job (manager, job);
	manager_add_job_for_window (manager, window, job);

	manager->priv->activate_time = time (NULL);

	if (manager->priv->lock_timeout >= 0)
	{
		remove_lock_timer (manager);
		add_lock_timer (manager, manager->priv->lock_timeout);
	}

	if (manager->priv->cycle_timeout >= 10000)
	{
		remove_cycle_timer (manager);
		add_cycle_timer (manager, manager->priv->cycle_timeout);
	}

	add_unfade_idle (manager);

	/* FIXME: only emit signal once */
	g_signal_emit (manager, signals [ACTIVATED], 0);
}

static void
window_show_cb (GSWindow  *window,
                GSManager *manager)
{

	g_return_if_fail (manager != NULL);
	g_return_if_fail (GS_IS_MANAGER (manager));
	g_return_if_fail (window != NULL);
	g_return_if_fail (GS_IS_WINDOW (window));

	gs_debug ("Handling window show");
	manager_show_window (manager, window);
}

static void
maybe_set_window_throttle (GSManager *manager,
                           GSWindow  *window,
                           gboolean   throttled)
{
	if (throttled)
	{
		manager_maybe_stop_job_for_window (manager, window);
	}
	else
	{
		manager_maybe_start_job_for_window (manager, window);
	}
}

static void
window_obscured_cb (GSWindow   *window,
                    GParamSpec *pspec,
                    GSManager  *manager)
{
	gboolean obscured;

	obscured = gs_window_is_obscured (window);
	gs_debug ("Handling window obscured: %s", obscured ? "obscured" : "unobscured");

	maybe_set_window_throttle (manager, window, obscured);

	if (! obscured)
	{
		gs_manager_request_unlock (manager);
	}
}

static void
handle_window_dialog_up (GSManager *manager,
                         GSWindow  *window)
{
	GSList *l;

	g_return_if_fail (manager != NULL);
	g_return_if_fail (GS_IS_MANAGER (manager));

	gs_debug ("Handling dialog up");

	g_signal_emit (manager, signals [AUTH_REQUEST_BEGIN], 0);

	manager->priv->dialog_up = TRUE;
	/* make all other windows insensitive to not get events */
	for (l = manager->priv->windows; l; l = l->next)
	{
		if (l->data != window)
		{
			gtk_widget_set_sensitive (GTK_WIDGET (l->data), FALSE);
		}
	}

	/* move devices grab so that dialog can be used;
	   release the pointer grab while dialog is up so that
	   the dialog can be used. We'll regrab it when the dialog goes down */
	gs_debug ("Initiate pointer-less grab move to %p", window);
	gs_grab_move_to_window (manager->priv->grab,
	                        gs_window_get_gdk_window (window),
	                        gs_window_get_display (window),
	                        TRUE, FALSE);

	if (! manager->priv->throttled)
	{
		gs_debug ("Suspending jobs");

		manager_suspend_jobs (manager);
	}
}

static void
handle_window_dialog_down (GSManager *manager,
                           GSWindow  *window)
{
	GSList *l;

	g_return_if_fail (manager != NULL);
	g_return_if_fail (GS_IS_MANAGER (manager));

	gs_debug ("Handling dialog down");

	/* regrab pointer */
	gs_grab_move_to_window (manager->priv->grab,
	                        gs_window_get_gdk_window (window),
	                        gs_window_get_display (window),
	                        FALSE, FALSE);

	/* make all windows sensitive to get events */
	for (l = manager->priv->windows; l; l = l->next)
	{
		gtk_widget_set_sensitive (GTK_WIDGET (l->data), TRUE);
	}

	manager->priv->dialog_up = FALSE;

	if (! manager->priv->throttled)
	{
		manager_resume_jobs (manager);
	}

	g_signal_emit (manager, signals [AUTH_REQUEST_END], 0);
}

static void
window_dialog_up_changed_cb (GSWindow   *window,
                             GParamSpec *pspec,
                             GSManager  *manager)
{
	gboolean up;

	up = gs_window_is_dialog_up (window);
	gs_debug ("Handling window dialog up changed: %s", up ? "up" : "down");
	if (up)
	{
		handle_window_dialog_up (manager, window);
	}
	else
	{
		handle_window_dialog_down (manager, window);
	}
}

static gboolean
window_activity_cb (GSWindow  *window,
                    GSManager *manager)
{
	gboolean handled;

	handled = gs_manager_request_unlock (manager);

	return handled;
}

static void
disconnect_window_signals (GSManager *manager,
                           GSWindow  *window)
{
	g_signal_handlers_disconnect_by_func (window, window_deactivated_cb, manager);
	g_signal_handlers_disconnect_by_func (window, window_activity_cb, manager);
	g_signal_handlers_disconnect_by_func (window, window_show_cb, manager);
	g_signal_handlers_disconnect_by_func (window, window_map_cb, manager);
	g_signal_handlers_disconnect_by_func (window, window_map_event_cb, manager);
	g_signal_handlers_disconnect_by_func (window, window_obscured_cb, manager);
	g_signal_handlers_disconnect_by_func (window, window_dialog_up_changed_cb, manager);
	g_signal_handlers_disconnect_by_func (window, window_unmap_cb, manager);
	g_signal_handlers_disconnect_by_func (window, window_grab_broken_cb, manager);
}

static void
window_destroyed_cb (GtkWindow *window,
                     GSManager *manager)
{
	disconnect_window_signals (manager, GS_WINDOW (window));
}

static void
connect_window_signals (GSManager *manager,
                        GSWindow  *window)
{
	g_signal_connect_object (window, "destroy",
	                         G_CALLBACK (window_destroyed_cb), manager, 0);
	g_signal_connect_object (window, "activity",
	                         G_CALLBACK (window_activity_cb), manager, 0);
	g_signal_connect_object (window, "deactivated",
	                         G_CALLBACK (window_deactivated_cb), manager, 0);
	g_signal_connect_object (window, "show",
	                         G_CALLBACK (window_show_cb), manager, G_CONNECT_AFTER);
	g_signal_connect_object (window, "map",
	                         G_CALLBACK (window_map_cb), manager, G_CONNECT_AFTER);
	g_signal_connect_object (window, "map_event",
	                         G_CALLBACK (window_map_event_cb), manager, G_CONNECT_AFTER);
	g_signal_connect_object (window, "notify::obscured",
	                         G_CALLBACK (window_obscured_cb), manager, G_CONNECT_AFTER);
	g_signal_connect_object (window, "notify::dialog-up",
	                         G_CALLBACK (window_dialog_up_changed_cb), manager, 0);
	g_signal_connect_object (window, "unmap",
	                         G_CALLBACK (window_unmap_cb), manager, G_CONNECT_AFTER);
	g_signal_connect_object (window, "grab_broken_event",
	                         G_CALLBACK (window_grab_broken_cb), manager, G_CONNECT_AFTER);
}

static void
gs_manager_create_window_for_monitor (GSManager  *manager,
                                      GdkMonitor *monitor)
{
	GSWindow    *window;
	GdkRectangle rect;

	gdk_monitor_get_geometry (monitor, &rect);

	gs_debug ("Creating a window [%d,%d] (%dx%d)",
	          rect.x, rect.y, rect.width, rect.height);

	window = gs_window_new (monitor, manager->priv->lock_active);

	gs_window_set_user_switch_enabled (window, manager->priv->user_switch_enabled);
	gs_window_set_logout_enabled (window, manager->priv->logout_enabled);
	gs_window_set_logout_timeout (window, manager->priv->logout_timeout);
	gs_window_set_logout_command (window, manager->priv->logout_command);
	gs_window_set_keyboard_enabled (window, manager->priv->keyboard_enabled);
	gs_window_set_keyboard_command (window, manager->priv->keyboard_command);
	gs_window_set_status_message (window, manager->priv->status_message);

	connect_window_signals (manager, window);

	manager->priv->windows = g_slist_append (manager->priv->windows, window);

	if (manager->priv->active && !manager->priv->fading)
	{
		gtk_widget_show (GTK_WIDGET (window));
	}
}

static void
on_display_monitor_added (GdkDisplay *display,
                          GdkMonitor *monitor,
                          GSManager  *manager)
{
	GSList     *l;
	int         n_monitors;

	n_monitors = gdk_display_get_n_monitors (display);

	gs_debug ("Monitor added on display %s, now there are %d",
	          gdk_display_get_name (display), n_monitors);

	/* Tear down the unlock dialog in case we want to move it
	 * to the new monitor
	 */
	l = manager->priv->windows;
	while (l != NULL)
	{
		gs_window_cancel_unlock_request (GS_WINDOW (l->data));
		l = l->next;
	}

	/* add a new window */
	gs_manager_create_window_for_monitor (manager, monitor);

	/* and put unlock dialog up whereever it's supposed to be */
	gs_manager_request_unlock (manager);
}

static void
on_display_monitor_removed (GdkDisplay *display,
                            GdkMonitor *monitor,
                            GSManager  *manager)
{
	GSList     *l;
	int         n_monitors;

	n_monitors = gdk_display_get_n_monitors (display);

	gs_debug ("Monitor removed on display %s, now there are %d",
	          gdk_display_get_name (display), n_monitors);

	gdk_x11_grab_server ();

	/* remove the now extra window */
	l = manager->priv->windows;
	while (l != NULL)
	{
		GdkDisplay *this_display;
		GdkMonitor *this_monitor;
		GSList     *next = l->next;

		this_display = gs_window_get_display (GS_WINDOW (l->data));
		this_monitor = gs_window_get_monitor (GS_WINDOW (l->data));
		if (this_display == display && this_monitor == monitor)
		{
			manager_maybe_stop_job_for_window (manager,
			                                   GS_WINDOW (l->data));
			g_hash_table_remove (manager->priv->jobs, l->data);
			gs_window_destroy (GS_WINDOW (l->data));
			manager->priv->windows = g_slist_delete_link (manager->priv->windows, l);
		}
		l = next;
	}

	gdk_display_flush (display);
	gdk_x11_ungrab_server ();
}

static void
gs_manager_destroy_windows (GSManager *manager)
{
	GdkDisplay  *display;
	GSList      *l;

	g_return_if_fail (manager != NULL);
	g_return_if_fail (GS_IS_MANAGER (manager));

	if (manager->priv->windows == NULL)
	{
		return;
	}

	display = gdk_display_get_default ();

	g_signal_handlers_disconnect_by_func (display,
	                                      on_display_monitor_removed,
	                                      manager);
	g_signal_handlers_disconnect_by_func (display,
	                                      on_display_monitor_added,
	                                      manager);

	for (l = manager->priv->windows; l; l = l->next)
	{
		gs_window_destroy (l->data);
	}
	g_slist_free (manager->priv->windows);
	manager->priv->windows = NULL;
}

static void
gs_manager_finalize (GObject *object)
{
	GSManager *manager;

	g_return_if_fail (object != NULL);
	g_return_if_fail (GS_IS_MANAGER (object));

	manager = GS_MANAGER (object);

	g_return_if_fail (manager->priv != NULL);

	if (manager->priv->bg != NULL)
	{
		g_object_unref (manager->priv->bg);
	}

	free_themes (manager);
	g_free (manager->priv->logout_command);
	g_free (manager->priv->keyboard_command);
	g_free (manager->priv->status_message);

	remove_unfade_idle (manager);
	remove_timers (manager);

	gs_grab_release (manager->priv->grab, TRUE);

	manager_stop_jobs (manager);

	gs_manager_destroy_windows (manager);

	manager->priv->active = FALSE;
	manager->priv->activate_time = 0;
	manager->priv->lock_enabled = FALSE;

	g_object_unref (manager->priv->fade);
	g_object_unref (manager->priv->grab);
	g_object_unref (manager->priv->theme_manager);

	G_OBJECT_CLASS (gs_manager_parent_class)->finalize (object);
}

static void
gs_manager_create_windows_for_display (GSManager  *manager,
                                       GdkDisplay *display)
{
	int n_monitors;
	int i;

	g_return_if_fail (manager != NULL);
	g_return_if_fail (GS_IS_MANAGER (manager));
	g_return_if_fail (GDK_IS_DISPLAY (display));

	g_object_ref (manager);
	g_object_ref (display);

	n_monitors = gdk_display_get_n_monitors (display);

	gs_debug ("Creating %d windows for display %s",
	          n_monitors, gdk_display_get_name (display));

	for (i = 0; i < n_monitors; i++)
	{
		GdkMonitor *mon = gdk_display_get_monitor (display, i);
		gs_manager_create_window_for_monitor (manager, mon);
	}

	g_object_unref (display);
	g_object_unref (manager);
}

static void
gs_manager_create_windows (GSManager *manager)
{
	GdkDisplay  *display;

	g_return_if_fail (manager != NULL);
	g_return_if_fail (GS_IS_MANAGER (manager));

	g_assert (manager->priv->windows == NULL);

	display = gdk_display_get_default ();
	g_signal_connect (display, "monitor-added",
	                  G_CALLBACK (on_display_monitor_added),
	                  manager);
	g_signal_connect (display, "monitor-removed",
	                  G_CALLBACK (on_display_monitor_removed),
	                  manager);

	gs_manager_create_windows_for_display (manager, display);
}

GSManager *
gs_manager_new (void)
{
	GObject *manager;

	manager = g_object_new (GS_TYPE_MANAGER, NULL);

	return GS_MANAGER (manager);
}

static void
show_windows (GSList *windows)
{
	GSList *l;

	for (l = windows; l; l = l->next)
	{
		gtk_widget_show (GTK_WIDGET (l->data));
	}
}

static void
remove_job (GSJob *job)
{
	if (job == NULL)
	{
		return;
	}

	gs_job_stop (job);
	g_object_unref (job);
}

static void
fade_done_cb (GSFade    *fade,
              GSManager *manager)
{
	gs_debug ("fade completed, showing windows");
	show_windows (manager->priv->windows);
	manager->priv->fading = FALSE;
}

static gboolean
gs_manager_activate (GSManager *manager)
{
	gboolean    do_fade;
	gboolean    res;

	g_return_val_if_fail (manager != NULL, FALSE);
	g_return_val_if_fail (GS_IS_MANAGER (manager), FALSE);

	if (manager->priv->active)
	{
		gs_debug ("Trying to activate manager when already active");
		return FALSE;
	}

	res = gs_grab_grab_root (manager->priv->grab, FALSE, FALSE);
	if (! res)
	{
		return FALSE;
	}

	if (manager->priv->windows == NULL)
	{
		gs_manager_create_windows (GS_MANAGER (manager));
	}

	manager->priv->jobs = g_hash_table_new_full (g_direct_hash,
	                      g_direct_equal,
	                      NULL,
	                      (GDestroyNotify)remove_job);

	manager->priv->active = TRUE;

	/* fade to black and show windows */
	do_fade = FALSE;
	if (do_fade)
	{
		manager->priv->fading = TRUE;
		gs_debug ("fading out");
		gs_fade_async (manager->priv->fade,
		               FADE_TIMEOUT,
		               (GSFadeDoneFunc)fade_done_cb,
		               manager);

		while (manager->priv->fading)
		{
			gtk_main_iteration ();
		}
	}
	else
	{
		show_windows (manager->priv->windows);
	}

	return TRUE;
}

static gboolean
gs_manager_deactivate (GSManager *manager)
{
	g_return_val_if_fail (manager != NULL, FALSE);
	g_return_val_if_fail (GS_IS_MANAGER (manager), FALSE);

	if (! manager->priv->active)
	{
		gs_debug ("Trying to deactivate a screensaver that is not active");
		return FALSE;
	}

	remove_unfade_idle (manager);
	gs_fade_reset (manager->priv->fade);
	remove_timers (manager);

	gs_grab_release (manager->priv->grab, TRUE);

	manager_stop_jobs (manager);

	gs_manager_destroy_windows (manager);

	/* reset state */
	manager->priv->active = FALSE;
	manager->priv->activate_time = 0;
	manager->priv->lock_active = FALSE;
	manager->priv->dialog_up = FALSE;
	manager->priv->fading = FALSE;

	return TRUE;
}

gboolean
gs_manager_set_active (GSManager *manager,
                       gboolean   active)
{
	gboolean res;

	if (active)
	{
		res = gs_manager_activate (manager);
	}
	else
	{
		res = gs_manager_deactivate (manager);
	}

	return res;
}

gboolean
gs_manager_get_active (GSManager *manager)
{
	g_return_val_if_fail (manager != NULL, FALSE);
	g_return_val_if_fail (GS_IS_MANAGER (manager), FALSE);

	return manager->priv->active;
}

gboolean
gs_manager_request_unlock (GSManager *manager)
{
	GSWindow *window;

	g_return_val_if_fail (manager != NULL, FALSE);
	g_return_val_if_fail (GS_IS_MANAGER (manager), FALSE);

	if (! manager->priv->active)
	{
		gs_debug ("Request unlock but manager is not active");
		return FALSE;
	}

	if (manager->priv->dialog_up)
	{
		gs_debug ("Request unlock but dialog is already up");
		return FALSE;
	}

	if (manager->priv->fading)
	{
		gs_debug ("Request unlock so finishing fade");
		gs_fade_finish (manager->priv->fade);
	}

	if (manager->priv->windows == NULL)
	{
		gs_debug ("We don't have any windows!");
		return FALSE;
	}

	/* Find the GSWindow that contains the pointer */
	window = find_window_at_pointer (manager);
        apply_background_to_window (manager, window);
	gs_window_request_unlock (window);

	return TRUE;
}

void
gs_manager_cancel_unlock_request (GSManager *manager)
{
	GSList *l;
	for (l = manager->priv->windows; l; l = l->next)
	{
		gs_window_cancel_unlock_request (l->data);
	}
}
