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

#include <gdk/gdk.h>
#include <gtk/gtk.h>

#include "gs-window.h"
#include "gs-window-private.h"
#include "gs-marshal.h"
#include "gs-debug.h"

#ifdef ENABLE_X11
#include <gdk/gdkx.h>
extern GSWindow *gs_window_x11_new (GdkMonitor *monitor,
                                    gboolean    lock_enabled);
#endif

#ifdef ENABLE_WAYLAND
#include <gdk/gdkwayland.h>
extern GSWindow *gs_window_wayland_new (GdkMonitor *monitor,
                                        gboolean    lock_enabled);
#endif

enum
{
	PROP_0,
	PROP_MONITOR,
	PROP_LOCK_ENABLED,
	PROP_LOGOUT_ENABLED,
	PROP_KEYBOARD_ENABLED,
	PROP_USER_SWITCH_ENABLED,
	PROP_LOGOUT_TIMEOUT,
	PROP_LOGOUT_COMMAND,
	PROP_KEYBOARD_COMMAND,
	PROP_STATUS_MESSAGE,
	PROP_OBSCURED,
	PROP_DIALOG_UP,
	N_PROPERTIES
};

static GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };

G_DEFINE_TYPE_WITH_PRIVATE (GSWindow, gs_window, GTK_TYPE_WINDOW)

guint gs_window_signals [GS_WINDOW_N_SIGNALS] = { 0, };

static gboolean
gs_window_real_activity (GSWindow *window)
{
	return FALSE;
}

static void
gs_window_real_deactivated (GSWindow *window)
{
}

static void
gs_window_real_dialog_up (GSWindow *window)
{
}

static void
gs_window_real_dialog_down (GSWindow *window)
{
}

static void
gs_window_real_request_unlock (GSWindow *window)
{
}

static void
gs_window_real_cancel_unlock_request (GSWindow *window)
{
}

static void
gs_window_real_show (GSWindow *window)
{
	GtkWidget *widget;

	g_return_if_fail (GS_IS_WINDOW (window));

	widget = GTK_WIDGET (window);

	if (GTK_WIDGET_CLASS (gs_window_parent_class)->show)
	{
		GTK_WIDGET_CLASS (gs_window_parent_class)->show (widget);
	}
}

static void
gs_window_real_destroy (GSWindow *window)
{
}

static void
gs_window_real_create_lock_surface (GSWindow *window)
{
}

static void
gs_window_real_forget_lock_surface (GSWindow *window)
{
}

static void
gs_window_set_property (GObject      *object,
                        guint         prop_id,
                        const GValue *value,
                        GParamSpec   *pspec)
{
	GSWindow *self;

	g_return_if_fail (GS_IS_WINDOW (object));

	self = GS_WINDOW (object);

	switch (prop_id)
	{
	case PROP_MONITOR:
		gs_window_set_monitor (self, g_value_get_object (value));
		break;
	case PROP_LOCK_ENABLED:
		gs_window_set_lock_enabled (self, g_value_get_boolean (value));
		break;
	case PROP_LOGOUT_ENABLED:
		gs_window_set_logout_enabled (self, g_value_get_boolean (value));
		break;
	case PROP_KEYBOARD_ENABLED:
		gs_window_set_keyboard_enabled (self, g_value_get_boolean (value));
		break;
	case PROP_USER_SWITCH_ENABLED:
		gs_window_set_user_switch_enabled (self, g_value_get_boolean (value));
		break;
	case PROP_LOGOUT_TIMEOUT:
		gs_window_set_logout_timeout (self, g_value_get_long (value));
		break;
	case PROP_LOGOUT_COMMAND:
		gs_window_set_logout_command (self, g_value_get_string (value));
		break;
	case PROP_KEYBOARD_COMMAND:
		gs_window_set_keyboard_command (self, g_value_get_string (value));
		break;
	case PROP_STATUS_MESSAGE:
		gs_window_set_status_message (self, g_value_get_string (value));
		break;
	case PROP_OBSCURED:
		{
			gboolean v = g_value_get_boolean (value);
			if (self->priv->obscured != v)
			{
				self->priv->obscured = (v != FALSE);
				g_object_notify_by_pspec (object, obj_properties[PROP_OBSCURED]);
			}
		}
		break;
	case PROP_DIALOG_UP:
		{
			gboolean v = g_value_get_boolean (value);
			if (self->priv->dialog_up != v)
			{
				self->priv->dialog_up = (v != FALSE);
				g_object_notify_by_pspec (object, obj_properties[PROP_DIALOG_UP]);
			}
		}
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_window_get_property (GObject    *object,
                        guint       prop_id,
                        GValue     *value,
                        GParamSpec *pspec)
{
	GSWindow *self;

	g_return_if_fail (GS_IS_WINDOW (object));

	self = GS_WINDOW (object);

	switch (prop_id)
	{
	case PROP_MONITOR:
		g_value_set_object (value, self->priv->monitor);
		break;
	case PROP_LOCK_ENABLED:
		g_value_set_boolean (value, self->priv->lock_enabled);
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
	case PROP_OBSCURED:
		g_value_set_boolean (value, self->priv->obscured);
		break;
	case PROP_DIALOG_UP:
		g_value_set_boolean (value, self->priv->dialog_up);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_window_finalize (GObject *object)
{
	GSWindow *window;

	g_return_if_fail (object != NULL);
	g_return_if_fail (GS_IS_WINDOW (object));

	window = GS_WINDOW (object);

	g_return_if_fail (window->priv != NULL);

	g_free (window->priv->logout_command);
	window->priv->logout_command = NULL;

	g_free (window->priv->keyboard_command);
	window->priv->keyboard_command = NULL;

	g_free (window->priv->status_message);
	window->priv->status_message = NULL;

	if (window->priv->monitor)
	{
		g_object_unref (window->priv->monitor);
		window->priv->monitor = NULL;
	}

	if (window->priv->background_surface)
	{
		cairo_surface_destroy (window->priv->background_surface);
		window->priv->background_surface = NULL;
	}

	G_OBJECT_CLASS (gs_window_parent_class)->finalize (object);
}

static void
gs_window_class_init (GSWindowClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);

	object_class->set_property = gs_window_set_property;
	object_class->get_property = gs_window_get_property;
	object_class->finalize     = gs_window_finalize;

	klass->activity    = gs_window_real_activity;
	klass->deactivated = gs_window_real_deactivated;
	klass->dialog_up   = gs_window_real_dialog_up;
	klass->dialog_down = gs_window_real_dialog_down;
	klass->request_unlock = gs_window_real_request_unlock;
	klass->cancel_unlock_request = gs_window_real_cancel_unlock_request;
	klass->real_show   = gs_window_real_show;
	klass->real_destroy = gs_window_real_destroy;
	klass->create_lock_surface = gs_window_real_create_lock_surface;
	klass->forget_lock_surface = gs_window_real_forget_lock_surface;

	/* signals */

	gs_window_signals [GS_WINDOW_SIGNAL_ACTIVITY] =
	    g_signal_new ("activity",
	                  G_TYPE_FROM_CLASS (klass),
	                  G_SIGNAL_RUN_LAST,
	                  G_STRUCT_OFFSET (GSWindowClass, activity),
	                  g_signal_accumulator_true_handled,
	                  NULL,
	                  gs_marshal_BOOLEAN__VOID,
	                  G_TYPE_BOOLEAN, 0);

	gs_window_signals [GS_WINDOW_SIGNAL_DEACTIVATED] =
	    g_signal_new ("deactivated",
	                  G_TYPE_FROM_CLASS (klass),
	                  G_SIGNAL_RUN_LAST,
	                  G_STRUCT_OFFSET (GSWindowClass, deactivated),
	                  NULL, NULL,
	                  g_cclosure_marshal_VOID__VOID,
	                  G_TYPE_NONE, 0);

	gs_window_signals [GS_WINDOW_SIGNAL_DIALOG_UP] =
	    g_signal_new ("dialog-up",
	                  G_TYPE_FROM_CLASS (klass),
	                  G_SIGNAL_RUN_LAST,
	                  G_STRUCT_OFFSET (GSWindowClass, dialog_up),
	                  NULL, NULL,
	                  g_cclosure_marshal_VOID__VOID,
	                  G_TYPE_NONE, 0);

	gs_window_signals [GS_WINDOW_SIGNAL_DIALOG_DOWN] =
	    g_signal_new ("dialog-down",
	                  G_TYPE_FROM_CLASS (klass),
	                  G_SIGNAL_RUN_LAST,
	                  G_STRUCT_OFFSET (GSWindowClass, dialog_down),
	                  NULL, NULL,
	                  g_cclosure_marshal_VOID__VOID,
	                  G_TYPE_NONE, 0);

	/* properties */

	obj_properties[PROP_MONITOR] =
	    g_param_spec_object ("monitor",
	                        "Monitor",
	                        "The monitor this window is on",
	                        GDK_TYPE_MONITOR,
	                        G_PARAM_READWRITE |
	                        G_PARAM_STATIC_STRINGS);

	obj_properties[PROP_LOCK_ENABLED] =
	    g_param_spec_boolean ("lock-enabled",
	                         "Lock Enabled",
	                         "Whether the screen should be locked",
	                         FALSE,
	                         G_PARAM_READWRITE |
	                         G_PARAM_STATIC_STRINGS);

	obj_properties[PROP_LOGOUT_ENABLED] =
	    g_param_spec_boolean ("logout-enabled",
	                         "Logout Enabled",
	                         "Whether the logout button should be shown",
	                         FALSE,
	                         G_PARAM_READWRITE |
	                         G_PARAM_STATIC_STRINGS);

	obj_properties[PROP_KEYBOARD_ENABLED] =
	    g_param_spec_boolean ("keyboard-enabled",
	                         "Keyboard Enabled",
	                         "Whether the embedded keyboard should be shown",
	                         FALSE,
	                         G_PARAM_READWRITE |
	                         G_PARAM_STATIC_STRINGS);

	obj_properties[PROP_USER_SWITCH_ENABLED] =
	    g_param_spec_boolean ("user-switch-enabled",
	                         "User Switch Enabled",
	                         "Whether the user switch button should be shown",
	                         FALSE,
	                         G_PARAM_READWRITE |
	                         G_PARAM_STATIC_STRINGS);

	obj_properties[PROP_LOGOUT_TIMEOUT] =
	    g_param_spec_long ("logout-timeout",
	                      "Logout Timeout",
	                      "Seconds of inactivity before the logout button appears",
	                      0,
	                      G_MAXLONG,
	                      0,
	                      G_PARAM_READWRITE |
	                      G_PARAM_STATIC_STRINGS);

	obj_properties[PROP_LOGOUT_COMMAND] =
	    g_param_spec_string ("logout-command",
	                        "Logout Command",
	                        "The command to run when the logout button is pressed",
	                        NULL,
	                        G_PARAM_READWRITE |
	                        G_PARAM_STATIC_STRINGS);

	obj_properties[PROP_KEYBOARD_COMMAND] =
	    g_param_spec_string ("keyboard-command",
	                        "Keyboard Command",
	                        "The command to use for the embedded keyboard",
	                        NULL,
	                        G_PARAM_READWRITE |
	                        G_PARAM_STATIC_STRINGS);

	obj_properties[PROP_STATUS_MESSAGE] =
	    g_param_spec_string ("status-message",
	                        "Status Message",
	                        "A message to display on the lock screen",
	                        NULL,
	                        G_PARAM_READWRITE |
	                        G_PARAM_STATIC_STRINGS);

	obj_properties[PROP_OBSCURED] =
	    g_param_spec_boolean ("obscured",
	                         "Obscured",
	                         "Whether the window is fully obscured by another window",
	                         FALSE,
	                         G_PARAM_READABLE |
	                         G_PARAM_STATIC_STRINGS);

	obj_properties[PROP_DIALOG_UP] =
	    g_param_spec_boolean ("dialog-up",
	                         "Dialog Up",
	                         "Whether the unlock dialog is currently shown",
	                         FALSE,
	                         G_PARAM_READABLE |
	                         G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPERTIES, obj_properties);
}

static void
gs_window_init (GSWindow *window)
{
	window->priv = G_TYPE_INSTANCE_GET_PRIVATE (window, GS_TYPE_WINDOW, GSWindowPrivate);

	window->priv->monitor = NULL;
	window->priv->lock_enabled = FALSE;
	window->priv->logout_enabled = FALSE;
	window->priv->keyboard_enabled = FALSE;
	window->priv->user_switch_enabled = FALSE;
	window->priv->keyboard_command = NULL;
	window->priv->logout_command = NULL;
	window->priv->logout_timeout = 0;
	window->priv->status_message = NULL;

	window->priv->obscured = FALSE;
	window->priv->dialog_up = FALSE;
	window->priv->drawing_area = NULL;
	window->priv->background_surface = NULL;

#ifdef ENABLE_X11
	window->priv->vbox = NULL;
	window->priv->lock_box = NULL;
	window->priv->lock_socket = NULL;
	window->priv->keyboard_socket = NULL;
	window->priv->info_bar = NULL;
	window->priv->info_content = NULL;
	window->priv->popup_dialog_idle_id = 0;
	window->priv->dialog_map_signal_id = 0;
	window->priv->dialog_unmap_signal_id = 0;
	window->priv->dialog_response_signal_id = 0;
	window->priv->watchdog_timer_id = 0;
	window->priv->info_bar_timer_id = 0;
	window->priv->lock_pid = 0;
	window->priv->lock_watch_id = 0;
	window->priv->dialog_response = 0;
	window->priv->dialog_quit_requested = FALSE;
	window->priv->dialog_shake_in_progress = FALSE;
	window->priv->keyboard_pid = 0;
	window->priv->keyboard_watch_id = 0;
	window->priv->key_events = NULL;
	window->priv->last_x = -1;
	window->priv->last_y = -1;
	window->priv->timer = NULL;
#endif
}

void
gs_window_set_monitor (GSWindow   *window,
                       GdkMonitor *monitor)
{
	GdkMonitor *old;

	g_return_if_fail (GS_IS_WINDOW (window));
	g_return_if_fail (GDK_IS_MONITOR (monitor));

	old = window->priv->monitor;

	if (old == monitor)
	{
		return;
	}

	if (window->priv->monitor)
	{
		g_object_unref (window->priv->monitor);
	}

	window->priv->monitor = g_object_ref (monitor);

	g_object_notify (G_OBJECT (window), "monitor");
}

GdkMonitor *
gs_window_get_monitor (GSWindow *window)
{
	g_return_val_if_fail (GS_IS_WINDOW (window), NULL);

	return window->priv->monitor;
}

void
gs_window_set_lock_enabled (GSWindow  *window,
                            gboolean   lock_enabled)
{
	g_return_if_fail (GS_IS_WINDOW (window));

	if (window->priv->lock_enabled != lock_enabled)
	{
		window->priv->lock_enabled = (lock_enabled != FALSE);
		g_object_notify (G_OBJECT (window), "lock-enabled");
	}
}

void
gs_window_set_logout_enabled (GSWindow  *window,
                              gboolean   logout_enabled)
{
	g_return_if_fail (GS_IS_WINDOW (window));

	if (window->priv->logout_enabled != logout_enabled)
	{
		window->priv->logout_enabled = (logout_enabled != FALSE);
		g_object_notify (G_OBJECT (window), "logout-enabled");
	}
}

void
gs_window_set_keyboard_enabled (GSWindow  *window,
                                gboolean   enabled)
{
	g_return_if_fail (GS_IS_WINDOW (window));

	if (window->priv->keyboard_enabled != enabled)
	{
		window->priv->keyboard_enabled = (enabled != FALSE);
		g_object_notify (G_OBJECT (window), "keyboard-enabled");
	}
}

void
gs_window_set_keyboard_command (GSWindow   *window,
                                const char *command)
{
	char *copy;

	g_return_if_fail (GS_IS_WINDOW (window));

	copy = g_strdup (command);

	if (g_strcmp0 (window->priv->keyboard_command, copy) == 0)
	{
		g_free (copy);
		return;
	}

	g_free (window->priv->keyboard_command);
	window->priv->keyboard_command = copy;

	g_object_notify (G_OBJECT (window), "keyboard-command");
}

void
gs_window_set_user_switch_enabled (GSWindow  *window,
                                   gboolean   user_switch_enabled)
{
	g_return_if_fail (GS_IS_WINDOW (window));

	if (window->priv->user_switch_enabled != user_switch_enabled)
	{
		window->priv->user_switch_enabled = (user_switch_enabled != FALSE);
		g_object_notify (G_OBJECT (window), "user-switch-enabled");
	}
}

void
gs_window_set_logout_timeout (GSWindow  *window,
                              glong      timeout)
{
	g_return_if_fail (GS_IS_WINDOW (window));

	if (window->priv->logout_timeout != timeout)
	{
		window->priv->logout_timeout = timeout;
		g_object_notify (G_OBJECT (window), "logout-timeout");
	}
}

void
gs_window_set_logout_command (GSWindow   *window,
                              const char *command)
{
	char *copy;

	g_return_if_fail (GS_IS_WINDOW (window));

	copy = g_strdup (command);

	if (g_strcmp0 (window->priv->logout_command, copy) == 0)
	{
		g_free (copy);
		return;
	}

	g_free (window->priv->logout_command);
	window->priv->logout_command = copy;

	g_object_notify (G_OBJECT (window), "logout-command");
}

void
gs_window_set_status_message (GSWindow   *window,
                              const char *status_message)
{
	char *copy;

	g_return_if_fail (GS_IS_WINDOW (window));

	copy = g_strdup (status_message);

	if (g_strcmp0 (window->priv->status_message, copy) == 0)
	{
		g_free (copy);
		return;
	}

	g_free (window->priv->status_message);
	window->priv->status_message = copy;

	g_object_notify (G_OBJECT (window), "status-message");
}

gboolean
gs_window_is_obscured (GSWindow *window)
{
	g_return_val_if_fail (GS_IS_WINDOW (window), FALSE);

	return window->priv->obscured;
}

gboolean
gs_window_is_dialog_up (GSWindow *window)
{
	g_return_val_if_fail (GS_IS_WINDOW (window), FALSE);

	return window->priv->dialog_up;
}

GdkDisplay *
gs_window_get_display (GSWindow *window)
{
	g_return_val_if_fail (GS_IS_WINDOW (window), NULL);

	return gtk_widget_get_display (GTK_WIDGET (window));
}

void
gs_window_show (GSWindow *window)
{
	g_return_if_fail (GS_IS_WINDOW (window));

	if (GS_WINDOW_GET_CLASS (window)->real_show)
	{
		GS_WINDOW_GET_CLASS (window)->real_show (window);
	}
}

void
gs_window_destroy (GSWindow *window)
{
	g_return_if_fail (GS_IS_WINDOW (window));

	if (GS_WINDOW_GET_CLASS (window)->real_destroy)
	{
		GS_WINDOW_GET_CLASS (window)->real_destroy (window);
	}

	gtk_widget_destroy (GTK_WIDGET (window));
}

GdkWindow *
gs_window_get_gdk_window (GSWindow *window)
{
	g_return_val_if_fail (GS_IS_WINDOW (window), NULL);

	return gtk_widget_get_window (GTK_WIDGET (window));
}

GtkWidget *
gs_window_get_drawing_area (GSWindow *window)
{
	g_return_val_if_fail (GS_IS_WINDOW (window), NULL);

	return window->priv->drawing_area;
}

void
gs_window_clear (GSWindow *window)
{
	GdkWindow *gdkwindow;

	g_return_if_fail (GS_IS_WINDOW (window));

	gdkwindow = gtk_widget_get_window (GTK_WIDGET (window));
	if (gdkwindow != NULL)
	{
		cairo_t *cr;

		cr = gdk_cairo_create (gdkwindow);
		cairo_set_operator (cr, CAIRO_OPERATOR_OVER);

		if (gs_window_is_dialog_up (window) && window->priv->background_surface != NULL)
		{
			cairo_set_source_surface (cr, window->priv->background_surface, 0, 0);
		}
		else
		{
			cairo_set_source_rgb (cr, 0, 0, 0);
		}

		cairo_paint (cr);
		cairo_destroy (cr);
	}
}

void
gs_window_set_background_surface (GSWindow        *window,
                                  cairo_surface_t *surface)
{
	g_return_if_fail (GS_IS_WINDOW (window));

	if (window->priv->background_surface != NULL)
	{
		cairo_surface_destroy (window->priv->background_surface);
	}

	window->priv->background_surface = surface;

	if (window->priv->background_surface != NULL)
	{
		cairo_surface_reference (window->priv->background_surface);
	}

	gtk_widget_queue_draw (GTK_WIDGET (window));
}

void
gs_window_show_message (GSWindow   *window,
                        const char *summary,
                        const char *body,
                        const char *icon)
{
	g_return_if_fail (GS_IS_WINDOW (window));
}

void
gs_window_request_unlock (GSWindow *window)
{
	g_return_if_fail (GS_IS_WINDOW (window));

	if (GS_WINDOW_GET_CLASS (window)->request_unlock)
	{
		GS_WINDOW_GET_CLASS (window)->request_unlock (window);
	}
}

void
gs_window_cancel_unlock_request (GSWindow *window)
{
	g_return_if_fail (GS_IS_WINDOW (window));

	if (GS_WINDOW_GET_CLASS (window)->cancel_unlock_request)
	{
		GS_WINDOW_GET_CLASS (window)->cancel_unlock_request (window);
	}
}

void
gs_window_create_lock_surface (GSWindow *window)
{
	g_return_if_fail (GS_IS_WINDOW (window));

	if (GS_WINDOW_GET_CLASS (window)->create_lock_surface)
	{
		GS_WINDOW_GET_CLASS (window)->create_lock_surface (window);
	}
}

void
gs_window_forget_lock_surface (GSWindow *window)
{
	g_return_if_fail (GS_IS_WINDOW (window));

	if (GS_WINDOW_GET_CLASS (window)->forget_lock_surface)
	{
		GS_WINDOW_GET_CLASS (window)->forget_lock_surface (window);
	}
}

GSWindow *
gs_window_new (GdkMonitor *monitor,
               gboolean    lock_enabled)
{
	GdkDisplay *display;

	g_return_val_if_fail (GDK_IS_MONITOR (monitor), NULL);

	display = gdk_monitor_get_display (monitor);

#ifdef ENABLE_WAYLAND
	if (GDK_IS_WAYLAND_DISPLAY (display))
	{
		return gs_window_wayland_new (monitor, lock_enabled);
	}
#endif

#ifdef ENABLE_X11
	if (GDK_IS_X11_DISPLAY (display))
	{
		return gs_window_x11_new (monitor, lock_enabled);
	}
#endif

	g_warning ("No supported display type found");
	return NULL;
}
