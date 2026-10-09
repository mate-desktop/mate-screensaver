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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * Authors: William Jon McCann <mccann@jhu.edu>
 *
 */

#include "config.h"

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>

#ifdef ENABLE_X11
#include <gdk/gdkx.h>
#include <gtk/gtkx.h>
#ifdef HAVE_SHAPE_EXT
#include <X11/extensions/shape.h>
#endif
#endif

#ifdef ENABLE_WAYLAND
#include <gdk/gdkwayland.h>
#include <wayland-client.h>
#include <libwlembed-gtk3/libwlembed-gtk3.h>
#endif

#include "gs-window.h"
#include "gs-window-private.h"
#include "gs-marshal.h"
#include "subprocs.h"
#include "gs-debug.h"

#ifdef ENABLE_WAYLAND
/* The embedded compositor used to host saver themes and the unlock dialog
 * on Wayland. Set by GSManager before windows are created. */
WleEmbeddedCompositor *gs_wayland_compositor = NULL;
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

enum
{
	DIALOG_RESPONSE_CANCEL,
	DIALOG_RESPONSE_OK
};

#define MAX_QUEUED_EVENTS 16
#define MATE_SCREENSAVER_DIALOG_PATH LIBEXECDIR "/mate-screensaver-dialog"

#ifdef ENABLE_WAYLAND
/* How long to wait for the lock surface to get mapped and focused
 * before bringing up the unlock dialog anyway (200ms per try) */
#define MAX_DIALOG_DEFER_TRIES 25

/* Minimum pointer travel (pixels) between events before motion
 * counts as real user activity */
#define MOTION_ACTIVITY_THRESHOLD 4
#endif

static gboolean popup_dialog_idle (gpointer data);
static void gs_window_dialog_finish (GSWindow *window);
static void remove_command_watches (GSWindow *window);
static void remove_key_events (GSWindow *window);
static void popdown_dialog (GSWindow *window);
static void popup_dialog (GSWindow *window);
static void remove_watchdog_timer (GSWindow *window);

#ifdef ENABLE_X11
static void create_info_bar (GSWindow *window);
#endif

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

/* ------------------------------------------------------------------ */
/* Widget vfuncs and helpers                                          */
/* ------------------------------------------------------------------ */

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

#ifdef ENABLE_X11
/* derived from tomboy */
static void
gs_window_override_user_time (GSWindow *window)
{
	guint32 ev_time = gtk_get_current_event_time ();

	if (ev_time == 0)
	{
		gint ev_mask = gtk_widget_get_events (GTK_WIDGET (window));
		if (!(ev_mask & GDK_PROPERTY_CHANGE_MASK))
		{
			gtk_widget_add_events (GTK_WIDGET (window),
			                       GDK_PROPERTY_CHANGE_MASK);
		}

		/*
		 * NOTE: Last resort for D-BUS or other non-interactive
		 *       openings.  Causes roundtrip to server.  Lame.
		 */
		ev_time = gdk_x11_get_server_time (gtk_widget_get_window (GTK_WIDGET (window)));
	}

	gdk_x11_window_set_user_time (gtk_widget_get_window (GTK_WIDGET (window)), ev_time);
}

static cairo_region_t *
get_outside_region (GSWindow *window)
{
	GdkDisplay *display;
	int         i;
	int         num_monitors;
	cairo_region_t *region;

	display = gtk_widget_get_display (GTK_WIDGET (window));

	region = cairo_region_create ();

	num_monitors = gdk_display_get_n_monitors (display);
	for (i = 0; i < num_monitors; i++)
	{
		GdkMonitor *mon = gdk_display_get_monitor (display, i);

		if (mon != window->priv->monitor)
		{
			GdkRectangle geometry;
			cairo_rectangle_int_t rectangle;

			gdk_monitor_get_geometry (mon, &geometry);
			rectangle.x = geometry.x;
			rectangle.y = geometry.y;
			rectangle.width = geometry.width;
			rectangle.height = geometry.height;
			cairo_region_union_rectangle (region, &rectangle);
		}
		else
		{
			break;
		}
	}

	return region;
}

static void
update_geometry (GSWindow *window)
{
	GdkRectangle geometry;
	cairo_region_t *outside_region;
	cairo_region_t *monitor_region;

	outside_region = get_outside_region (window);

	gdk_monitor_get_geometry (window->priv->monitor, &geometry);
	gs_debug ("got geometry for monitor: x=%d y=%d w=%d h=%d",
	          geometry.x,
	          geometry.y,
	          geometry.width,
	          geometry.height);
	monitor_region = cairo_region_create_rectangle ((const cairo_rectangle_int_t *)&geometry);
	cairo_region_subtract (monitor_region, outside_region);
	cairo_region_destroy (outside_region);

	cairo_region_get_extents (monitor_region, (cairo_rectangle_int_t *)&geometry);
	cairo_region_destroy (monitor_region);

	gs_debug ("using geometry for monitor: x=%d y=%d w=%d h=%d",
	          geometry.x,
	          geometry.y,
	          geometry.width,
	          geometry.height);

	window->priv->geometry.x = geometry.x;
	window->priv->geometry.y = geometry.y;
	window->priv->geometry.width = geometry.width;
	window->priv->geometry.height = geometry.height;
}

static void
monitor_geometry_notify (GdkMonitor *monitor,
                         GParamSpec *pspec,
                         GSWindow   *window)
{
	gs_debug ("Got monitor geometry notify signal");
	gtk_widget_queue_resize (GTK_WIDGET (window));
}

/* copied from panel-toplevel.c */
static void
gs_window_move_resize_window (GSWindow *window,
                              gboolean  move,
                              gboolean  resize)
{
	GtkWidget *widget;
	GdkWindow *gdkwindow;

	widget = GTK_WIDGET (window);
	gdkwindow = gtk_widget_get_window (widget);

	g_assert (gtk_widget_get_realized (widget));

	gs_debug ("Move and/or resize window: x=%d y=%d w=%d h=%d",
	          window->priv->geometry.x,
	          window->priv->geometry.y,
	          window->priv->geometry.width,
	          window->priv->geometry.height);

	if (move && resize)
	{
		gdk_window_move_resize (gdkwindow,
		                        window->priv->geometry.x,
		                        window->priv->geometry.y,
		                        window->priv->geometry.width,
		                        window->priv->geometry.height);
	}
	else if (move)
	{
		gdk_window_move (gdkwindow,
		                 window->priv->geometry.x,
		                 window->priv->geometry.y);
	}
	else if (resize)
	{
		gdk_window_resize (gdkwindow,
		                   window->priv->geometry.width,
		                   window->priv->geometry.height);
	}
}

/* copied from gdk */
extern char **environ;

static gchar **
spawn_make_environment_for_display (GdkDisplay *display,
                                    gchar     **envp)
{
	gchar **retval = NULL;
	const gchar *display_name;
	gint    display_index = -1;
	gint    i, env_len;

	g_return_val_if_fail (GDK_IS_DISPLAY (display), NULL);

	if (envp == NULL)
		envp = environ;

	for (env_len = 0; envp[env_len]; env_len++)
		if (strncmp (envp[env_len], "DISPLAY", strlen ("DISPLAY")) == 0)
			display_index = env_len;

	retval = g_new (char *, env_len + 1);
	retval[env_len] = NULL;

	display_name = gdk_display_get_name (display);

	for (i = 0; i < env_len; i++)
		if (i == display_index)
			retval[i] = g_strconcat ("DISPLAY=", display_name, NULL);
		else
			retval[i] = g_strdup (envp[i]);

	g_assert (i == env_len);

	return retval;
}

static gboolean
spawn_command_line_on_display_sync (GdkDisplay  *display,
                                    const gchar  *command_line,
                                    char        **standard_output,
                                    char        **standard_error,
                                    int          *exit_status,
                                    GError      **error)
{
	char     **argv = NULL;
	char     **envp = NULL;
	gboolean   retval;

	g_return_val_if_fail (command_line != NULL, FALSE);

	if (! g_shell_parse_argv (command_line, NULL, &argv, error))
	{
		return FALSE;
	}

	envp = spawn_make_environment_for_display (display, NULL);

	retval = g_spawn_sync (NULL,
	                       argv,
	                       envp,
	                       G_SPAWN_SEARCH_PATH,
	                       NULL,
	                       NULL,
	                       standard_output,
	                       standard_error,
	                       exit_status,
	                       error);

	g_strfreev (argv);
	g_strfreev (envp);

	return retval;
}

static GdkVisual *
get_best_visual_for_display (GdkDisplay *display)
{
	GdkScreen    *screen;
	char         *std_output;
	int           exit_status;
	GError       *error;
	unsigned long v;
	char          c;
	GdkVisual    *visual;
	gboolean      res;

	visual = NULL;
	screen = gdk_display_get_default_screen (display);

	error = NULL;
	std_output = NULL;
	res = spawn_command_line_on_display_sync (display,
	        MATE_SCREENSAVER_GL_HELPER_PATH,
	        &std_output,
	        NULL,
	        &exit_status,
	        &error);
	if (! res)
	{
		gs_debug ("Could not run command '%s': %s",
		          MATE_SCREENSAVER_GL_HELPER_PATH, error->message);
		g_error_free (error);
		goto out;
	}

	if (1 == sscanf (std_output, "0x%lx %c", &v, &c))
	{
		if (v != 0)
		{
			VisualID      visual_id;

			visual_id = (VisualID) v;
			visual = gdk_x11_screen_lookup_visual (screen, visual_id);

			gs_debug ("Found best GL visual for display %s: 0x%x",
			          gdk_display_get_name (display),
			          (unsigned int) visual_id);
		}
	}
out:
	g_free (std_output);

	return g_object_ref (visual);
}

static void
widget_set_best_visual (GtkWidget *widget)
{
	GdkVisual *visual;

	g_return_if_fail (widget != NULL);

	visual = get_best_visual_for_display (gtk_widget_get_display (widget));
	if (visual != NULL)
	{
		gtk_widget_set_visual (widget, visual);
		g_object_unref (visual);
	}
}

static void
gs_window_raise (GSWindow *window)
{
	GdkWindow *win;

	g_return_if_fail (GS_IS_WINDOW (window));

	gs_debug ("Raising screensaver window");

	win = gtk_widget_get_window (GTK_WIDGET (window));

	gdk_window_raise (win);
}

static gboolean
x11_window_is_ours (Window window)
{
	GdkWindow *gwindow;
	gboolean   ret;

	ret = FALSE;

	gwindow = gdk_x11_window_lookup_for_display (gdk_display_get_default (), window);
	if (gwindow && (window != GDK_ROOT_WINDOW ()))
	{
		ret = TRUE;
	}

	return ret;
}

#ifdef HAVE_SHAPE_EXT
static void
unshape_window (GSWindow *window)
{
	gdk_window_shape_combine_region (gtk_widget_get_window (GTK_WIDGET (window)),
	                                 NULL,
	                                 0,
	                                 0);
}
#endif

static void
gs_window_xevent (GSWindow  *window,
                  GdkXEvent *xevent)
{
	XEvent *ev;

	ev = xevent;

	/* MapNotify is used to tell us when new windows are mapped.
	   ConfigureNofify is used to tell us when windows are raised. */
	switch (ev->xany.type)
	{
	case MapNotify:
	{
		XMapEvent *xme = &ev->xmap;

		if (! x11_window_is_ours (xme->window))
		{
			gs_window_raise (window);
		}
		else
		{
			gs_debug ("not raising our windows");
		}

		break;
	}
	case ConfigureNotify:
	{
		XConfigureEvent *xce = &ev->xconfigure;

		if (! x11_window_is_ours (xce->window))
		{
			gs_window_raise (window);
		}
		else
		{
			gs_debug ("not raising our windows");
		}

		break;
	}
	default:
		/* extension events */
#ifdef HAVE_SHAPE_EXT
		if (ev->xany.type == (window->priv->shape_event_base + ShapeNotify))
		{
			/*XShapeEvent *xse = (XShapeEvent *) ev;*/
			unshape_window (window);
			gs_debug ("Window was reshaped!");
		}
#endif

		break;
	}
}

static GdkFilterReturn
xevent_filter (GdkXEvent *xevent,
               GdkEvent  *event,
               GSWindow  *window)
{
	gs_window_xevent (window, xevent);

	return GDK_FILTER_CONTINUE;
}

static void
select_popup_events (void)
{
	XWindowAttributes attr;
	unsigned long     events;
	GdkDisplay *display;

	display = gdk_display_get_default ();

	gdk_x11_display_error_trap_push (display);

	memset (&attr, 0, sizeof (attr));
	XGetWindowAttributes (GDK_DISPLAY_XDISPLAY (display), GDK_ROOT_WINDOW (), &attr);

	events = SubstructureNotifyMask | attr.your_event_mask;
	XSelectInput (GDK_DISPLAY_XDISPLAY (display), GDK_ROOT_WINDOW (), events);

	gdk_x11_display_error_trap_pop_ignored (display);
}

static void
window_select_shape_events (GSWindow *window)
{
#ifdef HAVE_SHAPE_EXT
	unsigned long events;
	int           shape_error_base;
	GdkDisplay *display;

	display = gtk_widget_get_display (GTK_WIDGET (window));

	gdk_x11_display_error_trap_push (display);

	if (XShapeQueryExtension (GDK_DISPLAY_XDISPLAY (display), &window->priv->shape_event_base, &shape_error_base))
	{
		events = ShapeNotifyMask;
		XShapeSelectInput (GDK_DISPLAY_XDISPLAY (display), GDK_WINDOW_XID (gtk_widget_get_window (GTK_WIDGET (window))), events);
	}

	gdk_x11_display_error_trap_pop_ignored (display);
#endif
}
#endif /* ENABLE_X11 */

static gboolean
gs_window_real_draw (GtkWidget *widget,
                     cairo_t   *cr)
{
	GSWindow *window = GS_WINDOW (widget);
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

/* every so often we should raise the window in case
   another window has somehow gotten on top */
static gboolean
watchdog_timer (gpointer data)
{
	GSWindow  *window = GS_WINDOW (data);
	GtkWidget *widget = GTK_WIDGET (window);
	GdkWindow *gdkwindow;

	gdkwindow = gtk_widget_get_window (widget);
	if (gdkwindow != NULL)
	{
		gdk_window_focus (gdkwindow, GDK_CURRENT_TIME);
	}

	return TRUE;
}

static void
remove_watchdog_timer (GSWindow *window)
{
	if (window->priv->watchdog_timer_id != 0)
	{
		g_source_remove (window->priv->watchdog_timer_id);
		window->priv->watchdog_timer_id = 0;
	}
}

static void
add_watchdog_timer (GSWindow *window,
                    guint     timeout)
{
	window->priv->watchdog_timer_id = g_timeout_add (timeout,
	                                                 (GSourceFunc)watchdog_timer,
	                                                 window);
}

static gboolean
emit_deactivated_idle (gpointer data)
{
	GSWindow *window = GS_WINDOW (data);

	window->priv->deactivated_idle_id = 0;

	g_signal_emit (window, gs_window_signals [GS_WINDOW_SIGNAL_DEACTIVATED], 0);

	return FALSE;
}

static void
add_emit_deactivated_idle (GSWindow *window)
{
	if (window->priv->deactivated_idle_id != 0)
	{
		return;
	}

	window->priv->deactivated_idle_id = g_idle_add (emit_deactivated_idle, window);
}

static void
remove_popup_dialog_sources (GSWindow *window)
{
	if (window->priv->popup_dialog_idle_id != 0)
	{
		g_source_remove (window->priv->popup_dialog_idle_id);
		window->priv->popup_dialog_idle_id = 0;
	}

#ifdef ENABLE_WAYLAND
	if (window->priv->popup_dialog_retry_id != 0)
	{
		g_source_remove (window->priv->popup_dialog_retry_id);
		window->priv->popup_dialog_retry_id = 0;
	}

	window->priv->dialog_defer_count = 0;
#endif
}

static void
add_popup_dialog_idle (GSWindow *window)
{
	if (window->priv->popup_dialog_idle_id != 0)
	{
		return;
	}

	window->priv->popup_dialog_idle_id = g_idle_add (popup_dialog_idle, window);
}

static void
window_set_dialog_up (GSWindow *window,
                      gboolean  dialog_up)
{
	if (window->priv->dialog_up == dialog_up)
	{
		return;
	}

	window->priv->dialog_up = (dialog_up != FALSE);
	g_object_notify (G_OBJECT (window), "dialog-up");
}

static void
gs_window_real_show (GtkWidget *widget)
{
	GSWindow *window = GS_WINDOW (widget);

	if (GTK_WIDGET_CLASS (gs_window_parent_class)->show)
	{
		GTK_WIDGET_CLASS (gs_window_parent_class)->show (widget);
	}

	gs_window_clear (window);

	set_invisible_cursor (gtk_widget_get_window (widget), TRUE);

	if (window->priv->timer)
	{
		g_timer_destroy (window->priv->timer);
	}
	window->priv->timer = g_timer_new ();

	remove_watchdog_timer (window);
	add_watchdog_timer (window, 30000);

#ifdef ENABLE_WAYLAND
	if (GDK_IS_WAYLAND_DISPLAY (gdk_display_get_default ()))
	{
		/* start each activation with a fresh motion baseline */
		window->priv->have_last_motion = FALSE;
		return;
	}
#endif

#ifdef ENABLE_X11
	if (GDK_IS_X11_DISPLAY (gdk_display_get_default ()))
	{
		select_popup_events ();
		window_select_shape_events (window);
		gdk_window_add_filter (NULL, (GdkFilterFunc)xevent_filter, window);
	}
#endif
}

static void
gs_window_real_hide (GtkWidget *widget)
{
	GSWindow *window = GS_WINDOW (widget);

#ifdef ENABLE_X11
	if (GDK_IS_X11_DISPLAY (gdk_display_get_default ()))
	{
		gdk_window_remove_filter (NULL, (GdkFilterFunc)xevent_filter, window);
	}
#endif

	remove_watchdog_timer (window);

#ifdef ENABLE_WAYLAND
	if (GDK_IS_WAYLAND_DISPLAY (gdk_display_get_default ()))
	{
		popdown_dialog (window);
	}
#endif

	if (GTK_WIDGET_CLASS (gs_window_parent_class)->hide)
	{
		GTK_WIDGET_CLASS (gs_window_parent_class)->hide (widget);
	}
}

static void
gs_window_real_realize (GtkWidget *widget)
{
#ifdef ENABLE_X11
	if (GDK_IS_X11_DISPLAY (gdk_display_get_default ()))
	{
		widget_set_best_visual (widget);
	}
#endif

	if (GTK_WIDGET_CLASS (gs_window_parent_class)->realize)
	{
		GTK_WIDGET_CLASS (gs_window_parent_class)->realize (widget);
	}

#ifdef ENABLE_X11
	if (GDK_IS_X11_DISPLAY (gdk_display_get_default ()))
	{
		GSWindow   *window = GS_WINDOW (widget);
		GdkMonitor *monitor = window->priv->monitor;

		gs_window_override_user_time (window);

		gs_window_move_resize_window (window, TRUE, TRUE);

		if (monitor != NULL)
		{
			g_signal_connect (monitor,
			                  "notify::geometry",
			                  G_CALLBACK (monitor_geometry_notify),
			                  widget);
		}

		return;
	}
#endif

#ifdef ENABLE_WAYLAND
	if (GDK_IS_WAYLAND_DISPLAY (gdk_display_get_default ()))
	{
		gtk_widget_set_app_paintable (widget, TRUE);
		gs_debug ("Wayland window realized");
	}
#endif
}

static void
gs_window_real_unrealize (GtkWidget *widget)
{
	GSWindow *window = GS_WINDOW (widget);

#ifdef ENABLE_X11
	if (GDK_IS_X11_DISPLAY (gdk_display_get_default ()))
	{
		GdkMonitor *monitor = window->priv->monitor;

		if (monitor != NULL)
		{
			g_signal_handlers_disconnect_by_func (monitor,
			                                      monitor_geometry_notify,
			                                      widget);
		}
	}
#endif

	remove_watchdog_timer (window);

	if (GTK_WIDGET_CLASS (gs_window_parent_class)->unrealize)
	{
		GTK_WIDGET_CLASS (gs_window_parent_class)->unrealize (widget);
	}
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
forward_key_events (GSWindow *window)
{
	window->priv->key_events = g_list_reverse (window->priv->key_events);

	while (window->priv->key_events != NULL)
	{
		GdkEventKey *event = window->priv->key_events->data;

		gtk_window_propagate_key_event (GTK_WINDOW (window), event);

		gdk_event_free ((GdkEvent *)event);
		window->priv->key_events = g_list_delete_link (window->priv->key_events,
		                           window->priv->key_events);
	}
}

static void
remove_key_events (GSWindow *window)
{
	window->priv->key_events = g_list_reverse (window->priv->key_events);

	while (window->priv->key_events)
	{
		GdkEventKey *event = window->priv->key_events->data;

		gdk_event_free ((GdkEvent *)event);
		window->priv->key_events = g_list_delete_link (window->priv->key_events,
		                           window->priv->key_events);
	}
}

/* Shared by the X11 and Wayland unlock dialogs: the lock socket is only
 * shown once the embedded dialog's plug has been added, and queued key
 * events are replayed when it becomes visible. */
static void
lock_plug_added (GtkWidget *widget,
                 GSWindow  *window)
{
	gtk_widget_show (widget);
}

static void
lock_socket_show (GtkWidget *widget,
                  GSWindow  *window)
{
	gtk_widget_child_focus (window->priv->lock_socket, GTK_DIR_TAB_FORWARD);

	/* send queued events to the dialog */
	forward_key_events (window);
}

/* adapted from gspawn.c */
#ifdef ENABLE_X11
static int
wait_on_child (int pid)
{
	int status;

wait_again:
	if (waitpid (pid, &status, 0) < 0)
	{
		if (errno == EINTR)
		{
			goto wait_again;
		}
		else if (errno == ECHILD)
		{
			; /* do nothing, child already reaped */
		}
		else
		{
			gs_debug ("waitpid () should not fail in 'GSWindow'");
		}
	}

	return status;
}
#endif /* ENABLE_X11 */

#ifdef ENABLE_X11
static gboolean
lock_plug_removed (GtkWidget *widget,
                   GSWindow  *window)
{
	gtk_widget_hide (widget);
	gtk_container_remove (GTK_CONTAINER (window->priv->vbox), GTK_WIDGET (window->priv->lock_box));
	window->priv->lock_box = NULL;

	return TRUE;
}

static void
keyboard_plug_added (GtkWidget *widget,
                     GSWindow  *window)
{
	gtk_widget_show (widget);
}

static gboolean
keyboard_plug_removed (GtkWidget *widget,
                       GSWindow  *window)
{
	gtk_widget_hide (widget);
	gtk_container_remove (GTK_CONTAINER (window->priv->vbox), GTK_WIDGET (window->priv->keyboard_socket));

	return TRUE;
}

static void
keyboard_socket_destroyed (GtkWidget *widget,
                           GSWindow  *window)
{
	g_signal_handlers_disconnect_by_func (widget, keyboard_socket_destroyed, window);
	g_signal_handlers_disconnect_by_func (widget, keyboard_plug_added, window);
	g_signal_handlers_disconnect_by_func (widget, keyboard_plug_removed, window);

	window->priv->keyboard_socket = NULL;
}

static void
lock_socket_destroyed (GtkWidget *widget,
                       GSWindow  *window)
{
	g_signal_handlers_disconnect_by_func (widget, lock_socket_show, window);
	g_signal_handlers_disconnect_by_func (widget, lock_socket_destroyed, window);
	g_signal_handlers_disconnect_by_func (widget, lock_plug_added, window);
	g_signal_handlers_disconnect_by_func (widget, lock_plug_removed, window);

	window->priv->lock_socket = NULL;
}

static void
create_keyboard_socket (GSWindow *window,
                        guint32   id)
{
	int height;

	height = (HeightOfScreen (gdk_x11_screen_get_xscreen (gtk_widget_get_screen (GTK_WIDGET (window))))) / 4;

	window->priv->keyboard_socket = gtk_socket_new ();
	gtk_widget_set_size_request (window->priv->keyboard_socket, -1, height);

	g_signal_connect (window->priv->keyboard_socket, "destroy",
	                  G_CALLBACK (keyboard_socket_destroyed), window);
	g_signal_connect (window->priv->keyboard_socket, "plug_added",
	                  G_CALLBACK (keyboard_plug_added), window);
	g_signal_connect (window->priv->keyboard_socket, "plug_removed",
	                  G_CALLBACK (keyboard_plug_removed), window);
	gtk_box_pack_start (GTK_BOX (window->priv->vbox), window->priv->keyboard_socket, FALSE, FALSE, 0);
	gtk_socket_add_id (GTK_SOCKET (window->priv->keyboard_socket), id);
}

static void
kill_keyboard_command (GSWindow *window)
{
	if (window->priv->keyboard_pid > 0)
	{
		signal_pid (window->priv->keyboard_pid, SIGTERM);
	}
}

static void
kill_dialog_command (GSWindow *window)
{
	/* If a dialog is up we need to signal it
	   and wait on it */
	if (window->priv->lock_pid > 0)
	{
		signal_pid (window->priv->lock_pid, SIGTERM);
	}
}

static void
keyboard_command_finish (GSWindow *window)
{
	g_return_if_fail (GS_IS_WINDOW (window));

	/* send a signal just in case */
	kill_keyboard_command (window);

	gs_debug ("Keyboard finished");

	if (window->priv->keyboard_pid > 0)
	{
		wait_on_child (window->priv->keyboard_pid);

		g_spawn_close_pid (window->priv->keyboard_pid);
		window->priv->keyboard_pid = 0;
	}
}

static gboolean
keyboard_command_watch (GIOChannel   *source,
                        GIOCondition  condition,
                        GSWindow     *window)
{
	gboolean finished = FALSE;

	g_return_val_if_fail (GS_IS_WINDOW (window), FALSE);

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
		{
			guint32 id;
			char    c;
			gs_debug ("keyboard command output: %s", line);
			if (1 == sscanf (line, " %" G_GUINT32_FORMAT " %c", &id, &c))
			{
				create_keyboard_socket (window, id);
			}
		}
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
		window->priv->keyboard_watch_id = 0;
		keyboard_command_finish (window);
		return FALSE;
	}

	return TRUE;
}

static gboolean
x11_spawn_on_window (GSWindow *window,
                     char     *command,
                     int      *pid,
                     GIOFunc   watch_func,
                     gpointer  user_data,
                     gint     *watch_id)
{
	int         argc;
	char      **argv;
	char      **envp;
	GError     *error;
	gboolean    result;
	GIOChannel *channel;
	int         standard_output;
	int         standard_error;
	int         child_pid;
	int         id;

	error = NULL;
	if (! g_shell_parse_argv (command, &argc, &argv, &error))
	{
		gs_debug ("Could not parse command: %s", error->message);
		g_error_free (error);
		return FALSE;
	}

	error = NULL;
	envp = spawn_make_environment_for_display (gtk_widget_get_display (GTK_WIDGET (window)), NULL);
	result = g_spawn_async_with_pipes (NULL,
	                                   argv,
	                                   envp,
	                                   G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH,
	                                   NULL,
	                                   NULL,
	                                   &child_pid,
	                                   NULL,
	                                   &standard_output,
	                                   &standard_error,
	                                   &error);

	if (! result)
	{
		gs_debug ("Could not start command '%s': %s", command, error->message);
		g_error_free (error);
		g_strfreev (argv);
		g_strfreev (envp);
		return FALSE;
	}

	if (pid != NULL)
	{
		*pid = child_pid;
	}
	else
	{
		g_spawn_close_pid (child_pid);
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
	                     user_data);
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
	id = g_io_add_watch (channel,
	                     G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL,
	                     error_watch,
	                     NULL);
	g_io_channel_unref (channel);

	g_strfreev (argv);
	g_strfreev (envp);

	return result;
}

static void
embed_keyboard (GSWindow *window)
{
	gboolean res;

	if (! window->priv->keyboard_enabled
	        || window->priv->keyboard_command == NULL)
		return;

	gs_debug ("Adding embedded keyboard widget");

	/* FIXME: verify command is safe */

	gs_debug ("Running command: %s", window->priv->keyboard_command);

	res = x11_spawn_on_window (window,
	                           window->priv->keyboard_command,
	                           &window->priv->keyboard_pid,
	                           (GIOFunc)keyboard_command_watch,
	                           window,
	                           &window->priv->keyboard_watch_id);
	if (! res)
	{
		gs_debug ("Could not start command: %s", window->priv->keyboard_command);
	}
}

static void
create_lock_socket (GSWindow *window,
                    guint32   id)
{
	window->priv->lock_socket = gtk_socket_new ();
	window->priv->lock_box = gtk_grid_new ();
	gtk_widget_set_halign (GTK_WIDGET (window->priv->lock_box),
	                       GTK_ALIGN_CENTER);
	gtk_widget_set_valign (GTK_WIDGET (window->priv->lock_box),
	                       GTK_ALIGN_CENTER);
	gtk_widget_show (window->priv->lock_box);
	gtk_box_pack_start (GTK_BOX (window->priv->vbox), window->priv->lock_box, TRUE, TRUE, 0);

	gtk_container_add (GTK_CONTAINER (window->priv->lock_box), window->priv->lock_socket);

	g_signal_connect (window->priv->lock_socket, "show",
	                  G_CALLBACK (lock_socket_show), window);
	g_signal_connect (window->priv->lock_socket, "destroy",
	                  G_CALLBACK (lock_socket_destroyed), window);
	g_signal_connect (window->priv->lock_socket, "plug_added",
	                  G_CALLBACK (lock_plug_added), window);
	g_signal_connect (window->priv->lock_socket, "plug_removed",
	                  G_CALLBACK (lock_plug_removed), window);

	gtk_socket_add_id (GTK_SOCKET (window->priv->lock_socket), id);

	if (window->priv->keyboard_enabled)
	{
		embed_keyboard (window);
	}
}

static void
shake_dialog (GSWindow *window)
{
	int   i;
	guint start, end;

	window->priv->dialog_shake_in_progress = TRUE;

	for (i = 0; i < 8; i++)
	{
		if (i % 2 == 0)
		{
			start = 30;
			end = 0;
		}
		else
		{
			start = 0;
			end = 30;
		}

		if (! window->priv->lock_box)
		{
			break;
		}

		gtk_widget_set_margin_start (GTK_WIDGET (window->priv->lock_box),
		                             start);
		gtk_widget_set_margin_end (GTK_WIDGET (window->priv->lock_box),
		                           end);

		while (gtk_events_pending ())
		{
			gtk_main_iteration ();
		}

		g_usleep (10000);
	}

	window->priv->dialog_shake_in_progress = FALSE;
}

static void
window_set_obscured (GSWindow *window,
                     gboolean  obscured)
{
	if (window->priv->obscured == obscured)
	{
		return;
	}

	window->priv->obscured = (obscured != FALSE);
	g_object_notify (G_OBJECT (window), "obscured");
}

static gboolean
gs_window_real_visibility_notify_event (GtkWidget          *widget,
                                        GdkEventVisibility *event)
{
	switch (event->state)
	{
	case GDK_VISIBILITY_FULLY_OBSCURED:
		window_set_obscured (GS_WINDOW (widget), TRUE);
		break;
	case GDK_VISIBILITY_PARTIAL:
		break;
	case GDK_VISIBILITY_UNOBSCURED:
		window_set_obscured (GS_WINDOW (widget), FALSE);
		break;
	default:
		break;
	}

	return FALSE;
}
#endif /* ENABLE_X11 */

#ifdef ENABLE_X11
static gboolean
is_logout_enabled (GSWindow *window)
{
	double elapsed;

	if (! window->priv->logout_enabled)
	{
		return FALSE;
	}

	if (! window->priv->logout_command)
	{
		return FALSE;
	}

	elapsed = g_timer_elapsed (window->priv->timer, NULL);

	if (window->priv->logout_timeout < (elapsed * 1000))
	{
		return TRUE;
	}

	return FALSE;
}

static gboolean
is_user_switch_enabled (GSWindow *window)
{
	return window->priv->user_switch_enabled;
}
#endif /* ENABLE_X11 */

static void
maybe_kill_dialog (GSWindow *window)
{
#ifdef ENABLE_WAYLAND
	if (GDK_IS_WAYLAND_DISPLAY (gdk_display_get_default ()))
	{
		if (window->priv->lock_pid != 0 && !window->priv->dialog_quit_requested)
		{
			gs_debug ("Sending TERM to dialog process %d", window->priv->lock_pid);
			kill (window->priv->lock_pid, SIGTERM);
		}
		return;
	}
#endif

#ifdef ENABLE_X11
	if (!window->priv->dialog_shake_in_progress
	        && window->priv->dialog_quit_requested
	        && window->priv->lock_pid > 0)
	{
		kill (window->priv->lock_pid, SIGTERM);
	}
#endif
}

static gboolean
lock_command_watch (GIOChannel   *source,
                    GIOCondition  condition,
                    GSWindow     *window)
{
	gboolean finished = FALSE;

	g_return_val_if_fail (GS_IS_WINDOW (window), FALSE);

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

			if (strstr (line, "WINDOW ID=") != NULL)
			{
#ifdef ENABLE_X11
				if (GDK_IS_X11_DISPLAY (gdk_display_get_default ()))
				{
					guint32 id;
					char    c;
					if (1 == sscanf (line, " WINDOW ID= %" G_GUINT32_FORMAT " %c", &id, &c))
					{
						create_lock_socket (window, id);
					}
				}
#endif
			}
			else if (strstr (line, "NOTICE=") != NULL)
			{
				if (strstr (line, "NOTICE=AUTH FAILED") != NULL)
				{
#ifdef ENABLE_X11
					if (GDK_IS_X11_DISPLAY (gdk_display_get_default ()))
					{
						shake_dialog (window);
					}
#endif
					gs_debug ("Auth failed");
				}
			}
			else if (strstr (line, "RESPONSE=") != NULL)
			{
				if (strstr (line, "RESPONSE=OK") != NULL)
				{
					gs_debug ("Got OK response");
					window->priv->dialog_response = DIALOG_RESPONSE_OK;
				}
				else
				{
					gs_debug ("Got CANCEL response");
					window->priv->dialog_response = DIALOG_RESPONSE_CANCEL;
				}
				finished = TRUE;
			}
			else if (strstr (line, "REQUEST QUIT") != NULL)
			{
				gs_debug ("Got request for quit");
				window->priv->dialog_quit_requested = TRUE;
				maybe_kill_dialog (window);
			}
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
	else if (condition & (G_IO_HUP | G_IO_ERR))
	{
		finished = TRUE;
	}

	if (finished)
	{
		gint response = window->priv->dialog_response;

		popdown_dialog (window);

		if (response == DIALOG_RESPONSE_OK)
		{
			add_emit_deactivated_idle (window);
		}

		window->priv->lock_watch_id = 0;

		return FALSE;
	}

	return TRUE;
}

static void
remove_command_watches (GSWindow *window)
{
	if (window->priv->lock_watch_id != 0)
	{
		g_source_remove (window->priv->lock_watch_id);
		window->priv->lock_watch_id = 0;
	}
#ifdef ENABLE_X11
	if (window->priv->keyboard_watch_id != 0)
	{
		g_source_remove (window->priv->keyboard_watch_id);
		window->priv->keyboard_watch_id = 0;
	}
#endif
}

#ifdef ENABLE_X11
static void
x11_popdown_dialog (GSWindow *window)
{
	gs_window_dialog_finish (window);

	gtk_widget_show (window->priv->drawing_area);

	gs_window_clear (window);
	set_invisible_cursor (gtk_widget_get_window (GTK_WIDGET (window)), TRUE);

	window_set_dialog_up (window, FALSE);

	/* reset the pointer positions */
	window->priv->last_x = -1;
	window->priv->last_y = -1;

	if (window->priv->lock_box != NULL)
	{
		gtk_container_remove (GTK_CONTAINER (window->priv->vbox), GTK_WIDGET (window->priv->lock_box));
		window->priv->lock_box = NULL;
	}

	remove_popup_dialog_sources (window);
	remove_command_watches (window);
}

static void
x11_popup_dialog (GSWindow *window)
{
	gboolean  result;
	GString  *command;

	gs_debug ("Popping up dialog");

	command = g_string_new (MATE_SCREENSAVER_DIALOG_PATH);

	if (is_logout_enabled (window))
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

	if (is_user_switch_enabled (window))
	{
		command = g_string_append (command, " --enable-switch");
	}

	if (gs_debug_enabled ())
	{
		command = g_string_append (command, " --verbose");
	}

	gtk_widget_hide (window->priv->drawing_area);

	gtk_widget_queue_draw (GTK_WIDGET (window));
	set_invisible_cursor (gtk_widget_get_window (GTK_WIDGET (window)), FALSE);

	window->priv->dialog_quit_requested = FALSE;
	window->priv->dialog_shake_in_progress = FALSE;

	result = x11_spawn_on_window (window,
	                              command->str,
	                              &window->priv->lock_pid,
	                              (GIOFunc)lock_command_watch,
	                              window,
	                              &window->priv->lock_watch_id);
	if (! result)
	{
		gs_debug ("Could not start command: %s", command->str);
	}

	g_string_free (command, TRUE);
}
#endif /* ENABLE_X11 */

#ifdef ENABLE_WAYLAND
static void
reap_dialog_child (GSWindow *window)
{
	int   status;
	int   i;
	pid_t ret;

	if (window->priv->lock_child_watch_id != 0)
	{
		g_source_remove (window->priv->lock_child_watch_id);
		window->priv->lock_child_watch_id = 0;
	}

	if (window->priv->lock_pid == 0)
	{
		return;
	}

	kill (window->priv->lock_pid, SIGTERM);

	ret = 0;
	for (i = 0; i < 200; i++)
	{
		ret = waitpid (window->priv->lock_pid, &status, WNOHANG);
		if (ret != 0)
		{
			break;
		}
		g_usleep (10000);
	}

	if (ret == 0)
	{
		kill (window->priv->lock_pid, SIGKILL);
		waitpid (window->priv->lock_pid, &status, 0);
	}

	g_spawn_close_pid (window->priv->lock_pid);
	window->priv->lock_pid = 0;
}

static void
dialog_child_watch_cb (GPid     pid,
                       gint     status,
                       gpointer data)
{
	GSWindow *window = GS_WINDOW (data);

	/* GLib has already reaped the child in this callback. */
	if (window->priv->lock_pid == pid)
	{
		window->priv->lock_pid = 0;
	}
	window->priv->lock_child_watch_id = 0;
}

static gboolean
wayland_spawn_on_window (GSWindow     *window,
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

	window->priv->lock_child_watch_id = g_child_watch_add (*child_pid,
	                                                       dialog_child_watch_cb,
	                                                       window);

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
wayland_popup_dialog (GSWindow *window)
{
	WleEmbeddedCompositor *compositor;
	gboolean               result;
	GString               *command;
	const char            *token;

	gs_debug ("Popping up dialog");

	compositor = gs_wayland_compositor;
	if (compositor == NULL)
	{
		gs_debug ("No embedded compositor available");
		return;
	}

	if (window->priv->lock_socket != NULL)
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
		if (window->priv->dialog_defer_count < MAX_DIALOG_DEFER_TRIES)
		{
			window->priv->dialog_defer_count++;
			gs_debug ("Window not ready for dialog, retrying (%d/%d)",
			          window->priv->dialog_defer_count, MAX_DIALOG_DEFER_TRIES);
			if (window->priv->popup_dialog_retry_id != 0)
			{
				g_source_remove (window->priv->popup_dialog_retry_id);
			}
			window->priv->popup_dialog_retry_id = g_timeout_add (200, popup_dialog_idle, window);
			return;
		}

		gs_debug ("Window still not focused after %d tries, bringing up dialog anyway",
		          window->priv->dialog_defer_count);
	}

	window->priv->dialog_defer_count = 0;

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

	window->priv->lock_socket = wle_gtk_socket_new (compositor);
	if (window->priv->lock_socket == NULL)
	{
		gs_debug ("Failed to create WleGtkSocket for dialog");
		g_string_free (command, TRUE);
		return;
	}

	window->priv->lock_box = gtk_grid_new ();
	gtk_widget_set_halign (window->priv->lock_box, GTK_ALIGN_CENTER);
	gtk_widget_set_valign (window->priv->lock_box, GTK_ALIGN_CENTER);
	gtk_widget_show (window->priv->lock_box);
	gtk_box_pack_start (GTK_BOX (window->priv->vbox), window->priv->lock_box, TRUE, TRUE, 0);
	gtk_container_add (GTK_CONTAINER (window->priv->lock_box), window->priv->lock_socket);

	/* Show the socket only once the dialog's plug has actually embedded,
	 * like xfce4-screensaver does; the "show" handler below then runs at
	 * the right time to focus the entry and replay queued keys. */
	g_signal_connect (window->priv->lock_socket,
	                  "plug-added",
	                  G_CALLBACK (lock_plug_added),
	                  window);
	g_signal_connect (window->priv->lock_socket,
	                  "show",
	                  G_CALLBACK (lock_socket_show),
	                  window);

	token = wle_gtk_socket_get_embedding_token (WLE_GTK_SOCKET (window->priv->lock_socket));
	gs_debug ("Dialog embedding token: %s", token != NULL ? token : "(null)");

	if (window->priv->drawing_area != NULL)
	{
		gtk_widget_hide (window->priv->drawing_area);
	}

	gtk_widget_queue_draw (GTK_WIDGET (window));
	set_invisible_cursor (gtk_widget_get_window (GTK_WIDGET (window)), FALSE);

	window->priv->dialog_quit_requested = FALSE;
	window->priv->dialog_response = DIALOG_RESPONSE_CANCEL;

	result = wayland_spawn_on_window (window,
	                                  command->str,
	                                  token,
	                                  &window->priv->lock_pid,
	                                  (GIOFunc) lock_command_watch,
	                                  window,
	                                  &window->priv->lock_watch_id);
	if (! result)
	{
		gs_debug ("Could not start command: %s", command->str);

		window->priv->lock_socket = NULL;
		if (window->priv->lock_box != NULL)
		{
			gtk_widget_destroy (window->priv->lock_box);
			window->priv->lock_box = NULL;
		}

		if (window->priv->drawing_area != NULL)
		{
			gtk_widget_show (window->priv->drawing_area);
		}

		set_invisible_cursor (gtk_widget_get_window (GTK_WIDGET (window)), TRUE);
	}

	g_string_free (command, TRUE);
}

static void
wayland_popdown_dialog (GSWindow *window)
{
	GdkWindow *gdkwin;

	gs_window_dialog_finish (window);

	remove_popup_dialog_sources (window);

	if (window->priv->lock_box != NULL)
	{
		gtk_widget_destroy (window->priv->lock_box);
		window->priv->lock_box = NULL;
	}

	window->priv->lock_socket = NULL;

	if (window->priv->drawing_area != NULL)
	{
		gtk_widget_show (window->priv->drawing_area);
	}

	gdkwin = gtk_widget_get_window (GTK_WIDGET (window));
	if (gdkwin != NULL)
	{
		set_invisible_cursor (gdkwin, TRUE);
	}

	window->priv->dialog_quit_requested = FALSE;

	window_set_dialog_up (window, FALSE);
}
#endif /* ENABLE_WAYLAND */

static void
popup_dialog (GSWindow *window)
{
#ifdef ENABLE_WAYLAND
	if (GDK_IS_WAYLAND_DISPLAY (gdk_display_get_default ()))
	{
		wayland_popup_dialog (window);
		return;
	}
#endif

#ifdef ENABLE_X11
	if (GDK_IS_X11_DISPLAY (gdk_display_get_default ()))
	{
		x11_popup_dialog (window);
	}
#endif
}

static void
popdown_dialog (GSWindow *window)
{
#ifdef ENABLE_WAYLAND
	if (GDK_IS_WAYLAND_DISPLAY (gdk_display_get_default ()))
	{
		wayland_popdown_dialog (window);
		return;
	}
#endif

#ifdef ENABLE_X11
	if (GDK_IS_X11_DISPLAY (gdk_display_get_default ()))
	{
		x11_popdown_dialog (window);
	}
#endif
}

static gboolean
popup_dialog_idle (gpointer data)
{
	GSWindow *window = data;
	GSource  *source;
	guint     id;

	popup_dialog (window);

	source = g_main_current_source ();
	if (source != NULL)
	{
		id = g_source_get_id (source);
		if (window->priv->popup_dialog_idle_id == id)
		{
			window->priv->popup_dialog_idle_id = 0;
		}
#ifdef ENABLE_WAYLAND
		if (window->priv->popup_dialog_retry_id == id)
		{
			window->priv->popup_dialog_retry_id = 0;
		}
#endif
	}
	else
	{
		window->priv->popup_dialog_idle_id = 0;
	}

	return FALSE;
}

static void
gs_window_dialog_finish (GSWindow *window)
{
	g_return_if_fail (GS_IS_WINDOW (window));

	gs_debug ("Dialog finished");

#ifdef ENABLE_WAYLAND
	if (GDK_IS_WAYLAND_DISPLAY (gdk_display_get_default ()))
	{
		remove_command_watches (window);
		reap_dialog_child (window);
		return;
	}
#endif

#ifdef ENABLE_X11
	/* make sure we finish the keyboard thing too */
	keyboard_command_finish (window);

	/* send a signal just in case */
	kill_dialog_command (window);

	if (window->priv->lock_pid > 0)
	{
		wait_on_child (window->priv->lock_pid);

		g_spawn_close_pid (window->priv->lock_pid);
		window->priv->lock_pid = 0;
	}

	/* remove events for the case were we failed to show socket */
	remove_key_events (window);
#endif
}

static gboolean
queue_key_event (GSWindow    *window,
                 GdkEventKey *event)
{
	/* Eat the first return, enter, escape, or space */
	if (window->priv->key_events == NULL
	        && (event->keyval == GDK_KEY_Return
	            || event->keyval == GDK_KEY_KP_Enter
	            || event->keyval == GDK_KEY_Escape
	            || event->keyval == GDK_KEY_space))
	{
		return FALSE;
	}

	/* Only cache MAX_QUEUED_EVENTS key events.  If there are any more than this then
	   something is wrong */
	/* Don't queue keys that may cause focus navigation in the dialog */
	if (g_list_length (window->priv->key_events) < MAX_QUEUED_EVENTS
	        && event->keyval != GDK_KEY_Tab
	        && event->keyval != GDK_KEY_Up
	        && event->keyval != GDK_KEY_Down)
	{
		window->priv->key_events = g_list_prepend (window->priv->key_events,
		                           gdk_event_copy ((GdkEvent *)event));
	}

	return TRUE;
}

static gboolean
maybe_handle_activity (GSWindow *window)
{
	gboolean handled;

	handled = FALSE;

	/* if we already have a socket then don't bother */
	if (! window->priv->lock_socket
	        && gtk_widget_get_sensitive (GTK_WIDGET (window)))
	{
		g_signal_emit (window, gs_window_signals [GS_WINDOW_SIGNAL_ACTIVITY], 0, &handled);
	}

	return handled;
}

static gboolean
gs_window_real_key_press_event (GtkWidget   *widget,
                                GdkEventKey *event)
{
	GSWindow *window = GS_WINDOW (widget);

	/* Ignore brightness keys */
	if (event->hardware_keycode == 101 || event->hardware_keycode == 212
	        || (gdk_keyval_name (event->keyval) != NULL
	            && g_str_has_prefix (gdk_keyval_name (event->keyval), "XF86MonBrightness")))
	{
		gs_debug ("Ignoring brightness keys");
		return TRUE;
	}

	maybe_handle_activity (window);

	queue_key_event (window, event);

	if (GTK_WIDGET_CLASS (gs_window_parent_class)->key_press_event)
	{
		GTK_WIDGET_CLASS (gs_window_parent_class)->key_press_event (widget, event);
	}

	return TRUE;
}

static gboolean
gs_window_real_motion_notify_event (GtkWidget      *widget,
                                    GdkEventMotion *event)
{
	GSWindow *window = GS_WINDOW (widget);

#ifdef ENABLE_WAYLAND
	if (GDK_IS_WAYLAND_DISPLAY (gdk_display_get_default ()))
	{
		gdouble dx, dy;

		/* Wayland re-delivers pointer events with unchanged coordinates when
		 * surfaces are reconfigured underneath a stationary cursor (e.g. while
		 * the embedded saver settles its subsurface offsets after mapping).
		 * Only treat real movement as user activity, like X11 did. */
		if (window->priv->have_last_motion)
		{
			dx = event->x - window->priv->last_motion_x;
			dy = event->y - window->priv->last_motion_y;

			if ((dx < 0 ? -dx : dx) < MOTION_ACTIVITY_THRESHOLD &&
			        (dy < 0 ? -dy : dy) < MOTION_ACTIVITY_THRESHOLD)
			{
				return FALSE;
			}

			gs_debug ("Pointer moved to (%.1f,%.1f) from (%.1f,%.1f), treating as activity",
			          event->x, event->y, window->priv->last_motion_x, window->priv->last_motion_y);
		}

		window->priv->have_last_motion = TRUE;
		window->priv->last_motion_x = event->x;
		window->priv->last_motion_y = event->y;

		maybe_handle_activity (window);

		return FALSE;
	}
#endif

#ifdef ENABLE_X11
	if (GDK_IS_X11_DISPLAY (gdk_display_get_default ()))
	{
		gdouble     distance;
		gdouble     min_distance;
		gdouble     min_percentage = 0.1;
		GdkDisplay *display;
		GdkScreen  *screen;

		display = gs_window_get_display (window);
		screen = gdk_display_get_default_screen (display);
		min_distance = WidthOfScreen (gdk_x11_screen_get_xscreen (screen)) * min_percentage;

		/* if the last position was not set then don't detect motion */
		if (window->priv->last_x < 0 || window->priv->last_y < 0)
		{
			window->priv->last_x = event->x;
			window->priv->last_y = event->y;

			return FALSE;
		}

		/* just an approximate distance */
		distance = MAX (ABS (window->priv->last_x - event->x),
		                ABS (window->priv->last_y - event->y));

		if (distance > min_distance)
		{
			maybe_handle_activity (window);

			window->priv->last_x = -1;
			window->priv->last_y = -1;
		}
	}
#endif

	return FALSE;
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

#ifdef ENABLE_X11
static void
gs_window_real_size_request (GtkWidget      *widget,
                             GtkRequisition *requisition)
{
	GSWindow      *window;
	GtkBin        *bin;
	GtkWidget     *child;
	GdkRectangle   old_geometry;
	int            position_changed = FALSE;
	int            size_changed = FALSE;

	window = GS_WINDOW (widget);
	bin = GTK_BIN (widget);
	child = gtk_bin_get_child (bin);

	if (child && gtk_widget_get_visible (child))
	{
		gtk_widget_get_preferred_size (child, requisition, NULL);
	}

	old_geometry = window->priv->geometry;

	update_geometry (window);

	requisition->width  = window->priv->geometry.width;
	requisition->height = window->priv->geometry.height;

	if (! gtk_widget_get_realized (widget))
	{
		return;
	}

	if (old_geometry.width  != window->priv->geometry.width ||
	        old_geometry.height != window->priv->geometry.height)
	{
		size_changed = TRUE;
	}

	if (old_geometry.x != window->priv->geometry.x ||
	        old_geometry.y != window->priv->geometry.y)
	{
		position_changed = TRUE;
	}

	gs_window_move_resize_window (window, position_changed, size_changed);
}

static void
gs_window_real_get_preferred_width (GtkWidget *widget,
                                    gint      *minimal_width,
                                    gint      *natural_width)
{
	GtkRequisition requisition;
	gs_window_real_size_request (widget, &requisition);
	*minimal_width = *natural_width = requisition.width;
}

static void
gs_window_real_get_preferred_height (GtkWidget *widget,
                                     gint      *minimal_height,
                                     gint      *natural_height)
{
	GtkRequisition requisition;
	gs_window_real_size_request (widget, &requisition);
	*minimal_height = *natural_height = requisition.height;
}

static gboolean
gs_window_real_grab_broken (GtkWidget          *widget,
                            GdkEventGrabBroken *event)
{
	if (event->grab_window != NULL)
	{
		gs_debug ("Grab broken on window %X %s, new grab on window %X",
		          (guint32) GDK_WINDOW_XID (event->window),
		          event->keyboard ? "keyboard" : "pointer",
		          (guint32) GDK_WINDOW_XID (event->grab_window));
	}
	else
	{
		gs_debug ("Grab broken on window %X %s, new grab is outside application",
		          (guint32) GDK_WINDOW_XID (event->window),
		          event->keyboard ? "keyboard" : "pointer");
	}

	return FALSE;
}
#endif /* ENABLE_X11 */

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

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
		window_set_dialog_up (self, g_value_get_boolean (value));
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

#ifdef ENABLE_X11
static void
create_info_bar (GSWindow *window)
{
	window->priv->info_bar = gtk_info_bar_new ();
	gtk_widget_set_no_show_all (window->priv->info_bar, TRUE);
	gtk_box_pack_end (GTK_BOX (window->priv->vbox), window->priv->info_bar, FALSE, FALSE, 0);
}
#endif

static void
gs_window_finalize (GObject *object)
{
	GSWindow *window;

	g_return_if_fail (object != NULL);
	g_return_if_fail (GS_IS_WINDOW (object));

	window = GS_WINDOW (object);

	g_return_if_fail (window->priv != NULL);

#ifdef ENABLE_WAYLAND
	if (GDK_IS_WAYLAND_DISPLAY (gdk_display_get_default ()))
	{
		remove_watchdog_timer (window);

		if (window->priv->deactivated_idle_id != 0)
		{
			g_source_remove (window->priv->deactivated_idle_id);
			window->priv->deactivated_idle_id = 0;
		}

		remove_key_events (window);

		if (window->priv->timer != NULL)
		{
			g_timer_destroy (window->priv->timer);
			window->priv->timer = NULL;
		}

		popdown_dialog (window);

		window->priv->drawing_area = NULL;
		window->priv->vbox = NULL;
	}
#endif

#ifdef ENABLE_X11
	if (GDK_IS_X11_DISPLAY (gdk_display_get_default ()))
	{
		if (window->priv->info_bar_timer_id > 0)
		{
			g_source_remove (window->priv->info_bar_timer_id);
			window->priv->info_bar_timer_id = 0;
		}

		remove_watchdog_timer (window);
		remove_popup_dialog_sources (window);

		if (window->priv->timer)
		{
			g_timer_destroy (window->priv->timer);
			window->priv->timer = NULL;
		}

		remove_key_events (window);

		remove_command_watches (window);

		gs_window_dialog_finish (window);
	}
#endif

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
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->set_property = gs_window_set_property;
	object_class->get_property = gs_window_get_property;
	object_class->finalize     = gs_window_finalize;

	klass->activity    = gs_window_real_activity;
	klass->deactivated = gs_window_real_deactivated;
	klass->dialog_up   = gs_window_real_dialog_up;
	klass->dialog_down = gs_window_real_dialog_down;

	widget_class->show                = gs_window_real_show;
	widget_class->hide                = gs_window_real_hide;
	widget_class->draw                = gs_window_real_draw;
	widget_class->realize             = gs_window_real_realize;
	widget_class->unrealize           = gs_window_real_unrealize;
	widget_class->key_press_event     = gs_window_real_key_press_event;
	widget_class->motion_notify_event = gs_window_real_motion_notify_event;
	widget_class->button_press_event  = gs_window_real_button_press_event;
	widget_class->scroll_event        = gs_window_real_scroll_event;
#ifdef ENABLE_X11
	widget_class->get_preferred_width  = gs_window_real_get_preferred_width;
	widget_class->get_preferred_height = gs_window_real_get_preferred_height;
	widget_class->grab_broken_event   = gs_window_real_grab_broken;
	widget_class->visibility_notify_event = gs_window_real_visibility_notify_event;
#endif

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
	window->priv = gs_window_get_instance_private (window);

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

	window->priv->vbox = NULL;
	window->priv->lock_box = NULL;
	window->priv->lock_socket = NULL;
	window->priv->popup_dialog_idle_id = 0;
	window->priv->watchdog_timer_id = 0;
	window->priv->deactivated_idle_id = 0;
	window->priv->lock_pid = 0;
	window->priv->lock_watch_id = 0;
	window->priv->dialog_response = DIALOG_RESPONSE_CANCEL;
	window->priv->dialog_quit_requested = FALSE;
	window->priv->key_events = NULL;
	window->priv->timer = NULL;

#ifdef ENABLE_X11
	window->priv->geometry.x      = -1;
	window->priv->geometry.y      = -1;
	window->priv->geometry.width  = -1;
	window->priv->geometry.height = -1;

	window->priv->keyboard_socket = NULL;
	window->priv->info_bar = NULL;
	window->priv->info_content = NULL;
	window->priv->info_bar_timer_id = 0;
	window->priv->dialog_shake_in_progress = FALSE;
	window->priv->keyboard_pid = 0;
	window->priv->keyboard_watch_id = 0;
	window->priv->last_x = -1;
	window->priv->last_y = -1;
#ifdef HAVE_SHAPE_EXT
	window->priv->shape_event_base = 0;
#endif
#endif

#ifdef ENABLE_WAYLAND
	window->priv->lock_child_watch_id = 0;
	window->priv->popup_dialog_retry_id = 0;
	window->priv->dialog_defer_count = 0;
	window->priv->have_last_motion = FALSE;
	window->priv->last_motion_x = 0;
	window->priv->last_motion_y = 0;
#endif

	gtk_window_set_decorated (GTK_WINDOW (window), FALSE);

	gtk_window_set_skip_taskbar_hint (GTK_WINDOW (window), TRUE);
	gtk_window_set_skip_pager_hint (GTK_WINDOW (window), TRUE);

	gtk_window_set_keep_above (GTK_WINDOW (window), TRUE);

	gtk_window_fullscreen (GTK_WINDOW (window));

	gtk_widget_set_events (GTK_WIDGET (window),
	                       gtk_widget_get_events (GTK_WIDGET (window))
	                       | GDK_POINTER_MOTION_MASK
	                       | GDK_BUTTON_PRESS_MASK
	                       | GDK_BUTTON_RELEASE_MASK
	                       | GDK_KEY_PRESS_MASK
	                       | GDK_KEY_RELEASE_MASK
	                       | GDK_EXPOSURE_MASK
	                       | GDK_VISIBILITY_NOTIFY_MASK
	                       | GDK_ENTER_NOTIFY_MASK
	                       | GDK_LEAVE_NOTIFY_MASK);

	window->priv->vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
	gtk_widget_show (window->priv->vbox);
	gtk_container_add (GTK_CONTAINER (window), window->priv->vbox);

#ifdef ENABLE_WAYLAND
	if (GDK_IS_WAYLAND_DISPLAY (gdk_display_get_default ()))
	{
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
		gtk_box_pack_start (GTK_BOX (window->priv->vbox),
		                    window->priv->drawing_area, TRUE, TRUE, 0);
		return;
	}
#endif

#ifdef ENABLE_X11
	if (GDK_IS_X11_DISPLAY (gdk_display_get_default ()))
	{
		window->priv->drawing_area = gtk_socket_new ();
		gtk_widget_show (window->priv->drawing_area);
		gtk_widget_set_app_paintable (window->priv->drawing_area, TRUE);
		gtk_box_pack_start (GTK_BOX (window->priv->vbox),
		                    window->priv->drawing_area, TRUE, TRUE, 0);
		g_signal_connect (window->priv->drawing_area,
		                  "draw",
		                  G_CALLBACK (on_drawing_area_draw),
		                  NULL);
		g_signal_connect (window->priv->drawing_area,
		                  "plug-removed",
		                  G_CALLBACK (gtk_true),
		                  NULL);
		create_info_bar (window);
	}
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

	gtk_widget_show (GTK_WIDGET (window));
}

void
gs_window_destroy (GSWindow *window)
{
	g_return_if_fail (GS_IS_WINDOW (window));

#ifdef ENABLE_WAYLAND
	if (GDK_IS_WAYLAND_DISPLAY (gdk_display_get_default ()))
	{
		remove_watchdog_timer (window);

		if (window->priv->deactivated_idle_id != 0)
		{
			g_source_remove (window->priv->deactivated_idle_id);
			window->priv->deactivated_idle_id = 0;
		}

		popdown_dialog (window);

		window->priv->drawing_area = NULL;
		window->priv->vbox = NULL;
	}
#endif

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

#ifdef ENABLE_WAYLAND
	if (GDK_IS_WAYLAND_DISPLAY (gdk_display_get_default ()))
	{
		if (! gtk_widget_get_mapped (GTK_WIDGET (window)))
		{
			return;
		}

		if (window->priv->lock_watch_id != 0)
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

		window_set_dialog_up (window, TRUE);
		return;
	}
#endif

#ifdef ENABLE_X11
	if (GDK_IS_X11_DISPLAY (gdk_display_get_default ()))
	{
		if (! gtk_widget_get_mapped (GTK_WIDGET (window)))
		{
			return;
		}

		if (window->priv->lock_watch_id != 0)
		{
			return;
		}

		if (! window->priv->lock_enabled)
		{
			add_emit_deactivated_idle (window);
			return;
		}

		if (window->priv->popup_dialog_idle_id == 0)
		{
			add_popup_dialog_idle (window);
		}

		window_set_dialog_up (window, TRUE);
	}
#endif
}

void
gs_window_cancel_unlock_request (GSWindow *window)
{
	g_return_if_fail (GS_IS_WINDOW (window));

#ifdef ENABLE_WAYLAND
	if (GDK_IS_WAYLAND_DISPLAY (gdk_display_get_default ()))
	{
		/* Do NOT destroy the lock surface here: it must stay alive for as
		 * long as the session is locked. This only cancels the unlock dialog. */
		window_set_dialog_up (window, FALSE);
		return;
	}
#endif

#ifdef ENABLE_X11
	if (GDK_IS_X11_DISPLAY (gdk_display_get_default ()))
	{
		popdown_dialog (window);
	}
#endif
}

GSWindow *
gs_window_new (GdkMonitor *monitor,
               gboolean    lock_enabled)
{
	GObject    *result;
	GdkDisplay *display;

	g_return_val_if_fail (GDK_IS_MONITOR (monitor), NULL);

	display = gdk_monitor_get_display (monitor);

#ifdef ENABLE_X11
	if (GDK_IS_X11_DISPLAY (display))
	{
		GdkScreen *screen = gdk_display_get_default_screen (display);

		result = g_object_new (GS_TYPE_WINDOW,
		                       "type", GTK_WINDOW_POPUP,
		                       "screen", screen,
		                       "monitor", monitor,
		                       "lock-enabled", lock_enabled,
		                       "app-paintable", TRUE,
		                       NULL);

		return GS_WINDOW (result);
	}
#endif

#ifdef ENABLE_WAYLAND
	if (GDK_IS_WAYLAND_DISPLAY (display))
	{
		result = g_object_new (GS_TYPE_WINDOW,
		                       "type", GTK_WINDOW_POPUP,
		                       "monitor", monitor,
		                       "lock-enabled", lock_enabled,
		                       "app-paintable", TRUE,
		                       NULL);

		return GS_WINDOW (result);
	}
#endif

	g_warning ("No supported display type found");
	return NULL;
}
