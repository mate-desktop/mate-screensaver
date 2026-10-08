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
 */

#include "config.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <string.h>

#include <gtk/gtk.h>

#ifdef ENABLE_WAYLAND
#include <gdk/gdkwayland.h>
#include <wayland-client.h>
#include <libwlembed-gtk3/libwlembed-gtk3.h>
#include "ext-session-lock-client.h"
#endif

#include "gs-window.h"
#include "gs-window-private.h"
#include "gs-manager.h"
#include "gs-debug.h"

#ifdef ENABLE_WAYLAND

WleEmbeddedCompositor *gs_wayland_compositor = NULL;
struct ext_session_lock_v1 *gs_wayland_session_lock = NULL;

#define MATE_SCREENSAVER_DIALOG_PATH LIBEXECDIR "/mate-screensaver-dialog"

/* How long to wait for the lock surface to get mapped and focused
 * before bringing up the unlock dialog anyway (200ms per try) */
#define MAX_DIALOG_DEFER_TRIES 25

/* Minimum pointer travel (pixels) between events before motion
 * counts as real user activity */
#define MOTION_ACTIVITY_THRESHOLD 4

/* Upper bound for key events cached while the saver is running,
 * to be replayed into the unlock dialog once it appears */
#define MAX_QUEUED_EVENTS 16

typedef struct GSWindowWaylandPrivate GSWindowWaylandPrivate;

struct GSWindowWaylandPrivate
{
	struct ext_session_lock_surface_v1 *lock_surface;

	GtkWidget                          *lock_box;
	GtkWidget                          *lock_socket;

	GPid                                lock_pid;
	gint                                lock_watch_id;
	guint                               lock_child_watch_id;
	gint                                dialog_response;
	gboolean                            dialog_quit_requested;

	guint                               watchdog_timer_id;
	guint                               popup_dialog_idle_id;
	guint                               popup_dialog_retry_id;
	guint                               deactivated_idle_id;
	guint                               dialog_defer_count;
	GTimer                             *timer;

	GtkWidget                          *vbox;

	/* filtering stationary-cursor pointer events */
	gboolean                            have_last_motion;
	gdouble                             last_motion_x;
	gdouble                             last_motion_y;

	/* key events typed while only the saver was running,
	   replayed into the unlock dialog when it appears */
	GList                              *key_events;
};

typedef struct
{
	GSWindow parent;
} GSWindowWayland;

typedef struct
{
	GSWindowClass parent_class;
} GSWindowWaylandClass;

G_DEFINE_TYPE_WITH_PRIVATE (GSWindowWayland, gs_window_wayland, GS_TYPE_WINDOW)

#define GS_WINDOW_WAYLAND(o)      (G_TYPE_CHECK_INSTANCE_CAST ((o), GS_TYPE_WINDOW_WAYLAND, GSWindowWayland))
#define GS_IS_WINDOW_WAYLAND(o)   (G_TYPE_CHECK_INSTANCE_TYPE ((o), GS_TYPE_WINDOW_WAYLAND))
#define GS_TYPE_WINDOW_WAYLAND    (gs_window_wayland_get_type ())
#define GS_WINDOW_WAYLAND_GET_PRIVATE(o) ((GSWindowWaylandPrivate *)gs_window_wayland_get_instance_private (GS_WINDOW_WAYLAND (o)))

static void set_invisible_cursor (GdkWindow *window,
                                  gboolean   invisible);
static void gs_window_wayland_finalize (GObject *object);

static void popdown_dialog (GSWindow *window);

static gboolean popup_dialog_idle (gpointer data);

static void remove_key_events (GSWindow *window);

static void lock_plug_added_cb (GSWindow *window);

static void lock_socket_show_cb (GtkWidget *widget,
                                 GSWindow  *window);

enum
{
	DIALOG_RESPONSE_CANCEL,
	DIALOG_RESPONSE_OK
};

static void
set_invisible_cursor (GdkWindow *window,
                      gboolean   invisible)
{
	GdkDisplay *display;
	GdkCursor  *cursor = NULL;

	if (window == NULL)
	{
		return;
	}

	if (invisible)
	{
		display = gdk_window_get_display (window);
		cursor = gdk_cursor_new_for_display (display, GDK_BLANK_CURSOR);
	}

	gdk_window_set_cursor (window, cursor);

	if (cursor)
	{
		g_object_unref (cursor);
	}
}

static void
wayland_window_set_dialog_up (GSWindow *window,
                              gboolean  dialog_up)
{
	if (window->priv->dialog_up == dialog_up)
	{
		return;
	}

	window->priv->dialog_up = (dialog_up != FALSE);
	g_object_notify (G_OBJECT (window), "dialog-up");
}

static gboolean
emit_deactivated_idle (gpointer data)
{
	GSWindow             *window = GS_WINDOW (data);
	GSWindowWaylandPrivate *priv;

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (window);
	priv->deactivated_idle_id = 0;

	g_signal_emit (window, gs_window_signals [GS_WINDOW_SIGNAL_DEACTIVATED], 0);

	return FALSE;
}

static void
add_emit_deactivated_idle (GSWindow *window)
{
	GSWindowWaylandPrivate *priv;

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (window);

	if (priv->deactivated_idle_id != 0)
	{
		return;
	}

	priv->deactivated_idle_id = g_idle_add (emit_deactivated_idle, window);
}

static void
remove_command_watches (GSWindow *window)
{
	GSWindowWaylandPrivate *priv;

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (window);

	if (priv->lock_watch_id != 0)
	{
		g_source_remove (priv->lock_watch_id);
		priv->lock_watch_id = 0;
	}
}

static void
reap_dialog_child (GSWindow *window)
{
	GSWindowWaylandPrivate *priv;
	int                     status;
	int                     i;
	pid_t                   ret;

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (window);

	if (priv->lock_child_watch_id != 0)
	{
		g_source_remove (priv->lock_child_watch_id);
		priv->lock_child_watch_id = 0;
	}

	if (priv->lock_pid == 0)
	{
		return;
	}

	kill (priv->lock_pid, SIGTERM);

	for (i = 0; i < 200; i++)
	{
		ret = waitpid (priv->lock_pid, &status, WNOHANG);
		if (ret != 0)
		{
			break;
		}
		g_usleep (10000);
	}

	if (ret == 0)
	{
		kill (priv->lock_pid, SIGKILL);
		waitpid (priv->lock_pid, &status, 0);
	}

	g_spawn_close_pid (priv->lock_pid);
	priv->lock_pid = 0;
}

static void
gs_window_dialog_finish (GSWindow *window)
{
	remove_command_watches (window);
	reap_dialog_child (window);
}

static void
maybe_kill_dialog (GSWindow *window)
{
	GSWindowWaylandPrivate *priv;

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (window);

	if (priv->lock_pid != 0 && !priv->dialog_quit_requested)
	{
		gs_debug ("Sending TERM to dialog process %d", priv->lock_pid);
		kill (priv->lock_pid, SIGTERM);
	}
}

static gboolean
lock_command_watch (GIOChannel   *source,
                    GIOCondition  condition,
                    GSWindow     *window)
{
	GSWindowWaylandPrivate *priv;
	gboolean                finished = FALSE;

	g_return_val_if_fail (GS_IS_WINDOW (window), FALSE);

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (window);

	if (condition & G_IO_IN)
	{
		GIOStatus status;
		GError   *error = NULL;
		char     *line;

		line = NULL;
		status = g_io_channel_read_line (source, &line, NULL, NULL, &error);

		switch (status)
		{
		case G_IO_STATUS_NORMAL:
			gs_debug ("command output: %s", line);

			if (strstr (line, "RESPONSE=") != NULL)
			{
				if (strstr (line, "RESPONSE=OK") != NULL)
				{
					gs_debug ("Got OK response");
					priv->dialog_response = DIALOG_RESPONSE_OK;
				}
				else
				{
					gs_debug ("Got CANCEL response");
					priv->dialog_response = DIALOG_RESPONSE_CANCEL;
				}
				finished = TRUE;
			}
			else if (strstr (line, "NOTICE=") != NULL)
			{
				if (strstr (line, "NOTICE=AUTH FAILED") != NULL)
				{
					gs_debug ("Auth failed");
				}
			}
			else if (strstr (line, "REQUEST QUIT") != NULL)
			{
				gs_debug ("Dialog requested quit");
				priv->dialog_quit_requested = TRUE;
				maybe_kill_dialog (window);
			}
			break;
		case G_IO_STATUS_EOF:
			gs_debug ("Got EOF from dialog");
			finished = TRUE;
			break;
		default:
			break;
		}

		g_free (line);
	}
	else if (condition & (G_IO_HUP | G_IO_ERR))
	{
		gs_debug ("Got HUP/ERR from dialog");
		finished = TRUE;
	}

	if (finished)
	{
		gint response;

		response = priv->dialog_response;

		popdown_dialog (window);

		if (response == DIALOG_RESPONSE_OK)
		{
			add_emit_deactivated_idle (window);
		}

		return FALSE;
	}

	return TRUE;
}

static void
remove_popup_dialog_sources (GSWindow *window)
{
	GSWindowWaylandPrivate *priv;

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (window);

	if (priv->popup_dialog_idle_id != 0)
	{
		g_source_remove (priv->popup_dialog_idle_id);
		priv->popup_dialog_idle_id = 0;
	}

	if (priv->popup_dialog_retry_id != 0)
	{
		g_source_remove (priv->popup_dialog_retry_id);
		priv->popup_dialog_retry_id = 0;
	}

	priv->dialog_defer_count = 0;
}

static void
destroy_lock_box (GSWindow *window)
{
	GSWindowWaylandPrivate *priv;

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (window);

	/* drop any keys still queued for a dialog that is going away */
	remove_key_events (window);

	if (priv->lock_box != NULL)
	{
		gtk_widget_destroy (priv->lock_box);
		priv->lock_box = NULL;
	}

	priv->lock_socket = NULL;
}

static void
popdown_dialog (GSWindow *window)
{
	GSWindowWaylandPrivate *priv;
	GdkWindow              *gdkwin;

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (window);

	gs_window_dialog_finish (window);

	remove_popup_dialog_sources (window);

	destroy_lock_box (window);

	if (window->priv->drawing_area != NULL)
	{
		gtk_widget_show (window->priv->drawing_area);
	}

	gdkwin = gtk_widget_get_window (GTK_WIDGET (window));
	if (gdkwin != NULL)
	{
		set_invisible_cursor (gdkwin, TRUE);
	}

	priv->dialog_quit_requested = FALSE;

	wayland_window_set_dialog_up (window, FALSE);
}

/* just for debugging */
static gboolean
error_watch (GIOChannel   *source,
             GIOCondition  condition,
             gpointer      data)
{
	gboolean finished = FALSE;

	if (condition & G_IO_IN)
	{
		GIOStatus status;
		GError   *error = NULL;
		char     *line;

		line = NULL;
		status = g_io_channel_read_line (source, &line, NULL, NULL, &error);

		switch (status)
		{
		case G_IO_STATUS_NORMAL:
			gs_debug ("command error output: %s", line);
			break;
		case G_IO_STATUS_EOF:
			finished = TRUE;
			break;
		case G_IO_STATUS_ERROR:
			finished = TRUE;
			gs_debug ("Error reading from child: %s\n", error->message);
			g_error_free (error);
			return FALSE;
		case G_IO_STATUS_AGAIN:
		default:
			break;
		}
		g_free (line);
	}
	else if (condition & G_IO_HUP)
	{
		finished = TRUE;
	}

	if (finished)
	{
		return FALSE;
	}

	return TRUE;
}

static void
dialog_child_watch_cb (GPid     pid,
                       gint     status,
                       gpointer data)
{
	GSWindow               *window = GS_WINDOW (data);
	GSWindowWaylandPrivate *priv;

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (window);

	/* GLib has already reaped the child in this callback. */
	if (priv->lock_pid == pid)
	{
		priv->lock_pid = 0;
	}
	priv->lock_child_watch_id = 0;
}

static gboolean
spawn_on_window (GSWindow     *window,
                 const char   *command,
                 const char   *embedding_token,
                 GPid         *child_pid,
                 GIOFunc       watch_func,
                 gpointer      watch_data,
                 gint         *watch_id)
{
	gboolean               result;
	GError                *error;
	char                 **argv;
	char                 **envp;
	GIOChannel            *channel;
	WleEmbeddedCompositor *compositor;
	int                    standard_output;
	int                    standard_error;
	gint                   id;

	g_return_val_if_fail (GS_IS_WINDOW (window), FALSE);
	g_return_val_if_fail (command != NULL, FALSE);
	g_return_val_if_fail (child_pid != NULL, FALSE);

	gs_window_dialog_finish (window);

	error = NULL;

	if (! g_shell_parse_argv (command, NULL, &argv, &error))
	{
		gs_debug ("Could not parse command: %s", error->message);
		g_error_free (error);
		return FALSE;
	}

	envp = g_get_environ ();

	compositor = gs_wayland_compositor;

	if (embedding_token != NULL && compositor != NULL &&
	    GDK_IS_WAYLAND_DISPLAY (gdk_display_get_default ()))
	{
		/* Spawn the child against the embedded compositor so its
		 * windows can be embedded into our lock surface. The real
		 * display stays reachable via WAYLAND_PARENT_DISPLAY. */
		gs_debug ("spawn_on_window: spawning '%s' on embedded compositor '%s'",
		          command, wle_embedded_compositor_get_socket_name (compositor));

		envp = g_environ_setenv (envp, "XSCREENSAVER_WINDOW", embedding_token, TRUE);

		result = wle_embedded_compositor_spawn_with_pipes (compositor,
		                                                   NULL,
		                                                   argv,
		                                                   envp,
		                                                   G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
		                                                   NULL,
		                                                   NULL,
		                                                   child_pid,
		                                                   NULL,
		                                                   &standard_output,
		                                                   &standard_error,
		                                                   &error);
	}
	else
	{
		result = g_spawn_async_with_pipes (NULL,
		                                   argv,
		                                   envp,
		                                   G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH,
		                                   NULL,
		                                   NULL,
		                                   child_pid,
		                                   NULL,
		                                   &standard_output,
		                                   &standard_error,
		                                   &error);
	}

	if (! result)
	{
		gs_debug ("Could not start command: %s", error->message);
		g_error_free (error);
		g_strfreev (argv);
		g_strfreev (envp);
		return FALSE;
	}

	{
		GSWindowWaylandPrivate *priv;

		priv = GS_WINDOW_WAYLAND_GET_PRIVATE (window);
		priv->lock_child_watch_id = g_child_watch_add (*child_pid,
		                                               dialog_child_watch_cb,
		                                               window);
	}

	/* output channel */
	channel = g_io_channel_unix_new (standard_output);
	g_io_channel_set_close_on_unref (channel, TRUE);
	g_io_channel_set_flags (channel,
	                        g_io_channel_get_flags (channel) | G_IO_FLAG_NONBLOCK,
	                        NULL);
	id = g_io_add_watch (channel,
	                     G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL,
	                     watch_func,
	                     watch_data);
	if (watch_id != NULL)
	{
		*watch_id = id;
	}
	g_io_channel_unref (channel);

	/* error channel */
	channel = g_io_channel_unix_new (standard_error);
	g_io_channel_set_close_on_unref (channel, TRUE);
	g_io_channel_set_flags (channel,
	                        g_io_channel_get_flags (channel) | G_IO_FLAG_NONBLOCK,
	                        NULL);
	g_io_add_watch (channel,
	                G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL,
	                error_watch,
	                NULL);
	g_io_channel_unref (channel);

	g_strfreev (argv);
	g_strfreev (envp);

	return TRUE;
}

static void
popup_dialog (GSWindow *window)
{
	GSWindowWaylandPrivate *priv;
	WleEmbeddedCompositor  *compositor;
	gboolean                result;
	GString                *command;
	const char             *token;

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (window);

	gs_debug ("Popping up dialog");

	compositor = gs_wayland_compositor;
	if (compositor == NULL)
	{
		gs_debug ("No embedded compositor available");
		return;
	}

	if (priv->lock_socket != NULL)
	{
		gs_debug ("Dialog is already up");
		return;
	}

	/* The compositor only routes keyboard events to the lock surface
	 * once it is mapped and has keyboard focus. Wait a little rather
	 * than dropping the request entirely. */
	if (! gtk_widget_get_mapped (GTK_WIDGET (window)) ||
	    ! gtk_window_is_active (GTK_WINDOW (window)))
	{
		if (priv->dialog_defer_count < MAX_DIALOG_DEFER_TRIES)
		{
			priv->dialog_defer_count++;
			gs_debug ("Window not ready for dialog, retrying (%d/%d)",
			          priv->dialog_defer_count, MAX_DIALOG_DEFER_TRIES);
			if (priv->popup_dialog_retry_id != 0)
			{
				g_source_remove (priv->popup_dialog_retry_id);
			}
			priv->popup_dialog_retry_id = g_timeout_add (200, popup_dialog_idle, window);
			return;
		}

		gs_debug ("Window still not focused after %d tries, bringing up dialog anyway",
		          priv->dialog_defer_count);
	}

	priv->dialog_defer_count = 0;

	command = g_string_new (MATE_SCREENSAVER_DIALOG_PATH);

	if (window->priv->logout_enabled)
	{
		command = g_string_append (command, " --enable-logout");
		g_string_append_printf (command, " --logout-command='%s'", window->priv->logout_command);
	}

	if (window->priv->status_message)
	{
		char *quoted;

		quoted = g_shell_quote (window->priv->status_message);
		g_string_append_printf (command, " --status-message=%s", quoted);
		g_free (quoted);
	}

	if (window->priv->user_switch_enabled)
	{
		command = g_string_append (command, " --enable-switch");
	}

	if (gs_debug_enabled ())
	{
		command = g_string_append (command, " --verbose");
	}

	priv->lock_socket = wle_gtk_socket_new (compositor);
	if (priv->lock_socket == NULL)
	{
		gs_debug ("Failed to create WleGtkSocket for dialog");
		g_string_free (command, TRUE);
		return;
	}

	priv->lock_box = gtk_grid_new ();
	gtk_widget_set_halign (priv->lock_box, GTK_ALIGN_CENTER);
	gtk_widget_set_valign (priv->lock_box, GTK_ALIGN_CENTER);
	gtk_widget_show (priv->lock_box);
	gtk_box_pack_start (GTK_BOX (priv->vbox), priv->lock_box, TRUE, TRUE, 0);
	gtk_container_add (GTK_CONTAINER (priv->lock_box), priv->lock_socket);

	/* Show the socket only once the dialog's plug has actually embedded,
	 * like xfce4-screensaver does; the "show" handler below then runs at
	 * the right time to focus the entry and replay queued keys. */
	g_signal_connect_swapped (priv->lock_socket,
	                          "plug-added",
	                          G_CALLBACK (lock_plug_added_cb),
	                          window);
	g_signal_connect (priv->lock_socket,
	                  "show",
	                  G_CALLBACK (lock_socket_show_cb),
	                  window);

	token = wle_gtk_socket_get_embedding_token (WLE_GTK_SOCKET (priv->lock_socket));
	gs_debug ("Dialog embedding token: %s", token != NULL ? token : "(null)");

	if (window->priv->drawing_area != NULL)
	{
		gtk_widget_hide (window->priv->drawing_area);
	}

	gtk_widget_queue_draw (GTK_WIDGET (window));
	set_invisible_cursor (gtk_widget_get_window (GTK_WIDGET (window)), FALSE);

	priv->dialog_quit_requested = FALSE;
	priv->dialog_response = DIALOG_RESPONSE_CANCEL;

	result = spawn_on_window (window,
	                          command->str,
	                          token,
	                          &priv->lock_pid,
	                          (GIOFunc) lock_command_watch,
	                          window,
	                          &priv->lock_watch_id);
	if (! result)
	{
		gs_debug ("Could not start command: %s", command->str);

		destroy_lock_box (window);

		if (window->priv->drawing_area != NULL)
		{
			gtk_widget_show (window->priv->drawing_area);
		}

		set_invisible_cursor (gtk_widget_get_window (GTK_WIDGET (window)), TRUE);
	}

	g_string_free (command, TRUE);
}

static gboolean
popup_dialog_idle (gpointer data)
{
	GSWindow               *window = data;
	GSWindowWaylandPrivate *priv;
	GSource                *source;
	guint                   id;

	popup_dialog (window);

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (window);
	source = g_main_current_source ();
	if (source != NULL)
	{
		id = g_source_get_id (source);
		if (priv->popup_dialog_idle_id == id)
		{
			priv->popup_dialog_idle_id = 0;
		}
		if (priv->popup_dialog_retry_id == id)
		{
			priv->popup_dialog_retry_id = 0;
		}
	}

	return FALSE;
}

static void
add_popup_dialog_idle (GSWindow *window)
{
	GSWindowWaylandPrivate *priv;

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (window);

	if (priv->popup_dialog_idle_id != 0)
	{
		return;
	}

	priv->popup_dialog_idle_id = g_idle_add (popup_dialog_idle, window);
}

static void
destroy_lock_surface (GSWindow *window)
{
	GSWindowWaylandPrivate *priv;

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (window);

	if (priv->lock_surface != NULL)
	{
		ext_session_lock_surface_v1_destroy (priv->lock_surface);
		priv->lock_surface = NULL;
	}
}

static gboolean
watchdog_timer_cb (gpointer data)
{
	GSWindow  *window = GS_WINDOW (data);
	GtkWidget *widget = GTK_WIDGET (window);
	GdkWindow *gdkwindow;

	gdkwindow = gtk_widget_get_window (widget);
	if (gdkwindow != NULL)
	{
		gdk_window_focus (gdkwindow, GDK_CURRENT_TIME);
	}

	return G_SOURCE_CONTINUE;
}

static void
remove_watchdog_timer (GSWindow *window)
{
	GSWindowWaylandPrivate *priv;

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (window);

	if (priv->watchdog_timer_id != 0)
	{
		g_source_remove (priv->watchdog_timer_id);
		priv->watchdog_timer_id = 0;
	}
}

static void
add_watchdog_timer (GSWindow *window,
                    guint     timeout)
{
	GSWindowWaylandPrivate *priv;

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (window);

	priv->watchdog_timer_id = g_timeout_add (timeout,
	                                        watchdog_timer_cb,
	                                        window);
}

static void
lock_surface_handle_configure (void                               *data,
                               struct ext_session_lock_surface_v1 *surface,
                               uint32_t                            serial,
                               uint32_t                            width,
                               uint32_t                            height)
{
	GSWindow *window = GS_WINDOW (data);

	ext_session_lock_surface_v1_ack_configure (surface, serial);

	gdk_window_move_resize (gtk_widget_get_window (GTK_WIDGET (window)),
	                        0, 0, width, height);

	gs_debug ("Lock surface configured: %ux%u", width, height);
}

static const struct ext_session_lock_surface_v1_listener lock_surface_listener =
{
	lock_surface_handle_configure,
};

static void
attach_lock_surface (GSWindow *window)
{
	GSWindowWaylandPrivate     *priv;
	GdkWindow                  *gdk_window;
	GdkMonitor                 *monitor;
	struct wl_surface          *wl_surface;
	struct wl_output           *wl_output;
	struct ext_session_lock_v1 *session_lock;

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (window);

	if (priv->lock_surface != NULL)
	{
		return;
	}

	session_lock = gs_wayland_session_lock;
	if (session_lock == NULL)
	{
		gs_debug ("No session lock available from manager");
		return;
	}

	gdk_window = gtk_widget_get_window (GTK_WIDGET (window));
	if (gdk_window == NULL)
	{
		gs_debug ("No GDK window available");
		return;
	}

	/* The GdkWindow must be pristine here: we attach during realize,
	 * before GTK ever maps the surface or attaches a buffer, exactly
	 * like xfce4-screensaver does. No recreation dance needed. */
	gdk_wayland_window_set_use_custom_surface (gdk_window);

	wl_surface = gdk_wayland_window_get_wl_surface (gdk_window);
	if (wl_surface == NULL)
	{
		gs_debug ("Failed to get Wayland surface");
		return;
	}

	monitor = gs_window_get_monitor (window);
	if (monitor != NULL)
	{
		wl_output = gdk_wayland_monitor_get_wl_output (monitor);
	}
	else
	{
		wl_output = NULL;
	}

	priv->lock_surface = ext_session_lock_v1_get_lock_surface (session_lock,
	                                                           wl_surface,
	                                                           wl_output);
	if (priv->lock_surface != NULL)
	{
		ext_session_lock_surface_v1_add_listener (priv->lock_surface,
		                                          &lock_surface_listener,
		                                          window);
		wl_display_roundtrip (gdk_wayland_display_get_wl_display (gdk_display_get_default ()));
		gs_debug ("Lock surface created for output");
	}
	else
	{
		gs_debug ("Failed to create lock surface");
	}
}

void
gs_window_wayland_create_lock_surface (GSWindow *window)
{
	g_return_if_fail (GS_IS_WINDOW_WAYLAND (window));

	attach_lock_surface (window);
}

static void
gs_window_wayland_request_unlock (GSWindow *window)
{
	GSWindowWaylandPrivate *priv;

	g_return_if_fail (GS_IS_WINDOW_WAYLAND (window));

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (window);

	if (! gtk_widget_get_mapped (GTK_WIDGET (window)))
	{
		return;
	}

	if (priv->lock_watch_id != 0)
	{
		return;
	}

	if (! window->priv->lock_enabled)
	{
		add_emit_deactivated_idle (window);
		return;
	}

	gs_debug ("Wayland request unlock");

	add_popup_dialog_idle (window);

	wayland_window_set_dialog_up (window, TRUE);
}

static void
gs_window_wayland_cancel_unlock_request (GSWindow *window)
{
	g_return_if_fail (GS_IS_WINDOW_WAYLAND (window));

	destroy_lock_surface (window);

	wayland_window_set_dialog_up (window, FALSE);
}

static gboolean
on_drawing_area_draw (GtkWidget *widget,
                      cairo_t   *cr)
{
	cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
	cairo_set_source_rgb (cr, 0, 0, 0);
	cairo_paint (cr);

	return FALSE;
}

static void
gs_window_real_show (GSWindow *window)
{
	GtkWidget              *widget = GTK_WIDGET (window);
	GSWindowWaylandPrivate *priv;

	if (GTK_WIDGET_CLASS (gs_window_wayland_parent_class)->show)
	{
		GTK_WIDGET_CLASS (gs_window_wayland_parent_class)->show (widget);
	}

	gs_window_clear (window);

	set_invisible_cursor (gtk_widget_get_window (widget), TRUE);

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (window);

	/* start each activation with a fresh motion baseline */
	priv->have_last_motion = FALSE;

	if (priv->timer != NULL)
	{
		g_timer_destroy (priv->timer);
	}
	priv->timer = g_timer_new ();

	remove_watchdog_timer (window);
	add_watchdog_timer (window, 30000);
}

static void
gs_window_real_hide (GtkWidget *widget)
{
	GSWindow                *window = GS_WINDOW (widget);

	remove_watchdog_timer (window);

	popdown_dialog (window);

	destroy_lock_surface (window);

	if (GTK_WIDGET_CLASS (gs_window_wayland_parent_class)->hide)
	{
		GTK_WIDGET_CLASS (gs_window_wayland_parent_class)->hide (widget);
	}
}

static gboolean
gs_window_real_draw (GtkWidget *widget,
                     cairo_t   *cr)
{
	GSWindow        *window = GS_WINDOW (widget);
	cairo_surface_t *bg_surface = window->priv->background_surface;

	cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
	if (gs_window_is_dialog_up (window) && bg_surface != NULL)
	{
		cairo_set_source_surface (cr, bg_surface, 0, 0);
	}
	else
	{
		cairo_set_source_rgb (cr, 0, 0, 0);
	}
	cairo_paint (cr);

	return FALSE;
}

static void
gs_window_real_realize (GtkWidget *widget)
{
	if (GTK_WIDGET_CLASS (gs_window_wayland_parent_class)->realize)
	{
		GTK_WIDGET_CLASS (gs_window_wayland_parent_class)->realize (widget);
	}

	gtk_widget_set_app_paintable (widget, TRUE);

	/* Attach the ext-session-lock surface role right away, while the
	 * underlying wl_surface is still pristine (unmapped, no buffer).
	 * This is how the window is meant to live its whole life. */
	if (gs_wayland_session_lock != NULL)
	{
		attach_lock_surface (GS_WINDOW (widget));
	}

	gs_debug ("Wayland window realized");
}

static void
gs_window_wayland_real_destroy (GSWindow *window)
{
	GSWindowWaylandPrivate *priv;

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (window);

	remove_watchdog_timer (window);

	if (priv->deactivated_idle_id != 0)
	{
		g_source_remove (priv->deactivated_idle_id);
		priv->deactivated_idle_id = 0;
	}

	popdown_dialog (window);

	destroy_lock_surface (window);

	window->priv->drawing_area = NULL;
	priv->vbox = NULL;
}

static void
gs_window_real_unrealize (GtkWidget *widget)
{
	remove_watchdog_timer (GS_WINDOW (widget));

	destroy_lock_surface (GS_WINDOW (widget));

	if (GTK_WIDGET_CLASS (gs_window_wayland_parent_class)->unrealize)
	{
		GTK_WIDGET_CLASS (gs_window_wayland_parent_class)->unrealize (widget);
	}
}

static void
maybe_handle_activity (GSWindow *window)
{
	GSWindowWaylandPrivate *priv;
	gboolean                handled;

	handled = FALSE;

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (window);

	/* if the unlock dialog is already up, don't re-trigger */
	if (priv->lock_socket != NULL)
	{
		return;
	}

	if (! gtk_widget_get_sensitive (GTK_WIDGET (window)))
	{
		return;
	}

	g_signal_emit (window, gs_window_signals [GS_WINDOW_SIGNAL_ACTIVITY], 0, &handled);
}

static void
queue_key_event (GSWindow      *window,
                 GdkEventKey   *event)
{
	GSWindowWaylandPrivate *priv;

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (window);

	/* Eat the first return, enter, escape, or space */
	if (priv->key_events == NULL
	        && (event->keyval == GDK_KEY_Return
	            || event->keyval == GDK_KEY_KP_Enter
	            || event->keyval == GDK_KEY_Escape
	            || event->keyval == GDK_KEY_space))
	{
		return;
	}

	/* Don't queue keys that may cause focus navigation in the dialog */
	if (g_list_length (priv->key_events) < MAX_QUEUED_EVENTS
	        && event->keyval != GDK_KEY_Tab
	        && event->keyval != GDK_KEY_Up
	        && event->keyval != GDK_KEY_Down)
	{
		priv->key_events = g_list_prepend (priv->key_events,
		                                   gdk_event_copy ((GdkEvent *) event));
	}
}

static void
forward_key_events (GSWindow *window)
{
	GSWindowWaylandPrivate *priv;

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (window);

	priv->key_events = g_list_reverse (priv->key_events);

	while (priv->key_events != NULL)
	{
		GdkEventKey *event = priv->key_events->data;

		gtk_window_propagate_key_event (GTK_WINDOW (window), event);

		gdk_event_free ((GdkEvent *) event);
		priv->key_events = g_list_delete_link (priv->key_events,
		                                       priv->key_events);
	}
}

static void
remove_key_events (GSWindow *window)
{
	GSWindowWaylandPrivate *priv;

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (window);

	while (priv->key_events != NULL)
	{
		gdk_event_free ((GdkEvent *) priv->key_events->data);
		priv->key_events = g_list_delete_link (priv->key_events,
		                                       priv->key_events);
	}
}

static void
lock_plug_added_cb (GSWindow *window)
{
	GSWindowWaylandPrivate *priv;

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (window);

	gtk_widget_show (priv->lock_socket);
}

static void
lock_socket_show_cb (GtkWidget *widget,
                     GSWindow  *window)
{
	GSWindowWaylandPrivate *priv;

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (window);

	gtk_widget_child_focus (priv->lock_socket, GTK_DIR_TAB_FORWARD);

	/* send queued events to the dialog */
	forward_key_events (window);
}

static gboolean
gs_window_real_key_press_event (GtkWidget   *widget,
                                GdkEventKey *event)
{
	GSWindow               *window = GS_WINDOW (widget);

	/* Ignore brightness keys: adjusting the screen brightness while
	 * idle should not bring up the unlock dialog */
	if (event->hardware_keycode == 101 || event->hardware_keycode == 212
	        || (gdk_keyval_name (event->keyval) != NULL
	            && g_str_has_prefix (gdk_keyval_name (event->keyval), "XF86MonBrightness")))
	{
		gs_debug ("Ignoring brightness key");
		return TRUE;
	}

	maybe_handle_activity (window);

	queue_key_event (window, event);

	if (GTK_WIDGET_CLASS (gs_window_wayland_parent_class)->key_press_event)
	{
		GTK_WIDGET_CLASS (gs_window_wayland_parent_class)->key_press_event (widget, event);
	}

	return TRUE;
}

static gboolean
gs_window_real_button_press_event (GtkWidget      *widget,
                                   GdkEventButton *event)
{
	GSWindow *window = GS_WINDOW (widget);

	maybe_handle_activity (window);

	return FALSE;
}

static gboolean
gs_window_real_scroll_event (GtkWidget      *widget,
                             GdkEventScroll *event)
{
	GSWindow *window = GS_WINDOW (widget);

	maybe_handle_activity (window);

	return FALSE;
}

static gboolean
gs_window_real_motion_notify_event (GtkWidget      *widget,
                                    GdkEventMotion *event)
{
	GSWindow               *window = GS_WINDOW (widget);
	GSWindowWaylandPrivate *priv;
	gdouble                 dx, dy;

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (window);

	/* Wayland re-delivers pointer events with unchanged coordinates when
	 * surfaces are reconfigured underneath a stationary cursor (e.g. while
	 * the embedded saver settles its subsurface offsets after mapping).
	 * Only treat real movement as user activity, like X11 did. */
	if (priv->have_last_motion)
	{
		dx = event->x - priv->last_motion_x;
		dy = event->y - priv->last_motion_y;

		if ((dx < 0 ? -dx : dx) < MOTION_ACTIVITY_THRESHOLD &&
		        (dy < 0 ? -dy : dy) < MOTION_ACTIVITY_THRESHOLD)
		{
			return FALSE;
		}

		gs_debug ("Pointer moved to (%.1f,%.1f) from (%.1f,%.1f), treating as activity",
		          event->x, event->y, priv->last_motion_x, priv->last_motion_y);
	}

	priv->have_last_motion = TRUE;
	priv->last_motion_x = event->x;
	priv->last_motion_y = event->y;

	maybe_handle_activity (window);

	return FALSE;
}

static void
gs_window_wayland_class_init (GSWindowWaylandClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
	GSWindowClass  *window_class = GS_WINDOW_CLASS (klass);

	object_class->finalize = gs_window_wayland_finalize;

	window_class->real_show = gs_window_real_show;
	window_class->real_destroy = gs_window_wayland_real_destroy;
	window_class->request_unlock = gs_window_wayland_request_unlock;
	window_class->cancel_unlock_request = gs_window_wayland_cancel_unlock_request;
	window_class->create_lock_surface = gs_window_wayland_create_lock_surface;

	widget_class->hide                = gs_window_real_hide;
	widget_class->draw                = gs_window_real_draw;
	widget_class->realize             = gs_window_real_realize;
	widget_class->unrealize           = gs_window_real_unrealize;
	widget_class->key_press_event     = gs_window_real_key_press_event;
	widget_class->button_press_event  = gs_window_real_button_press_event;
	widget_class->scroll_event        = gs_window_real_scroll_event;
	widget_class->motion_notify_event = gs_window_real_motion_notify_event;
}

static void
gs_window_wayland_init (GSWindowWayland *wayland)
{
	GSWindow               *window = GS_WINDOW (wayland);
	GSWindowWaylandPrivate *priv;

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (wayland);

	priv->lock_surface = NULL;
	priv->lock_box = NULL;
	priv->lock_socket = NULL;
	priv->lock_pid = 0;
	priv->lock_watch_id = 0;
	priv->dialog_response = DIALOG_RESPONSE_CANCEL;
	priv->dialog_quit_requested = FALSE;
	priv->watchdog_timer_id = 0;
	priv->popup_dialog_idle_id = 0;
	priv->popup_dialog_retry_id = 0;
	priv->dialog_defer_count = 0;
	priv->key_events = NULL;
	priv->timer = NULL;
	priv->vbox = NULL;

	gtk_window_set_decorated (GTK_WINDOW (wayland), FALSE);

	gtk_window_set_skip_taskbar_hint (GTK_WINDOW (wayland), TRUE);
	gtk_window_set_skip_pager_hint (GTK_WINDOW (wayland), TRUE);

	gtk_window_set_keep_above (GTK_WINDOW (wayland), TRUE);

	gtk_window_fullscreen (GTK_WINDOW (wayland));

	gtk_widget_set_events (GTK_WIDGET (wayland),
	                       gtk_widget_get_events (GTK_WIDGET (wayland))
	                       | GDK_POINTER_MOTION_MASK
	                       | GDK_BUTTON_PRESS_MASK
	                       | GDK_BUTTON_RELEASE_MASK
	                       | GDK_KEY_PRESS_MASK
	                       | GDK_KEY_RELEASE_MASK
	                       | GDK_EXPOSURE_MASK
	                       | GDK_ENTER_NOTIFY_MASK
	                       | GDK_LEAVE_NOTIFY_MASK);

	priv->vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
	gtk_widget_show (priv->vbox);
	gtk_container_add (GTK_CONTAINER (wayland), priv->vbox);

	/* Saver themes embed into a WleGtkSocket through the embedded
	 * compositor; fall back to a plain drawing area if the embedded
	 * compositor is unavailable. */
	if (gs_wayland_compositor != NULL)
	{
		window->priv->drawing_area = wle_gtk_socket_new (gs_wayland_compositor);
	}

	if (window->priv->drawing_area == NULL)
	{
		window->priv->drawing_area = gtk_drawing_area_new ();
		gtk_widget_set_app_paintable (window->priv->drawing_area, TRUE);
		g_signal_connect (window->priv->drawing_area,
		                  "draw",
		                  G_CALLBACK (on_drawing_area_draw),
		                  NULL);
		gs_debug ("Using plain drawing area for saver themes");
	}
	else
	{
		gs_debug ("Using WleGtkSocket for saver themes");
	}

	gtk_widget_show (window->priv->drawing_area);
	gtk_box_pack_start (GTK_BOX (priv->vbox),
	                    window->priv->drawing_area, TRUE, TRUE, 0);
}

static void
gs_window_wayland_finalize (GObject *object)
{
	GSWindow                *window;
	GSWindowWaylandPrivate  *priv;

	g_return_if_fail (object != NULL);
	g_return_if_fail (GS_IS_WINDOW (object));

	window = GS_WINDOW (object);

	g_return_if_fail (window->priv != NULL);

	priv = GS_WINDOW_WAYLAND_GET_PRIVATE (window);

	remove_watchdog_timer (window);

	if (priv->deactivated_idle_id != 0)
	{
		g_source_remove (priv->deactivated_idle_id);
		priv->deactivated_idle_id = 0;
	}

	remove_key_events (window);

	if (priv->timer != NULL)
	{
		g_timer_destroy (priv->timer);
		priv->timer = NULL;
	}

	popdown_dialog (window);

	destroy_lock_surface (window);

	/* Note: common fields (logout_command, keyboard_command, status_message)
	 * are freed by the base class finalize */

	G_OBJECT_CLASS (gs_window_wayland_parent_class)->finalize (object);
}

GSWindow *
gs_window_wayland_new (GdkMonitor *monitor,
                      gboolean   lock_enabled)
{
	GObject *result;

	result = g_object_new (GS_TYPE_WINDOW_WAYLAND,
	                       "type", GTK_WINDOW_POPUP,
	                       "monitor", monitor,
	                       "lock-enabled", lock_enabled,
	                       "app-paintable", TRUE,
	                       NULL);

	return GS_WINDOW (result);
}

#endif /* ENABLE_WAYLAND */
