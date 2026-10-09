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

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <gdk/gdk.h>
#include <gtk/gtk.h>

#ifdef ENABLE_X11
#include <gdk/gdkx.h>
#endif
#ifdef ENABLE_WAYLAND
#include <gdk/gdkwayland.h>
#endif

#include "gs-grab.h"
#include "gs-debug.h"

#define SLEEPTIMEOUT 100000

typedef struct
{
	GdkWindow  *grab_window;
	GdkDisplay *grab_display;
	guint       no_pointer_grab : 1;
	guint       hide_cursor : 1;

#ifdef ENABLE_X11
	GtkWidget  *invisible;
#endif
} GSGrabPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GSGrab, gs_grab, G_TYPE_OBJECT)

#define GRAB_GET_PRIVATE(o) ((GSGrabPrivate *) gs_grab_get_instance_private (GS_GRAB (o)))

void
gs_grab_reset (GSGrab *grab)
{
	GSGrabPrivate *priv;

	g_return_if_fail (GS_IS_GRAB (grab));

	priv = GRAB_GET_PRIVATE (grab);

	if (priv->grab_window != NULL)
	{
		g_object_remove_weak_pointer (G_OBJECT (priv->grab_window),
		                              (gpointer *) &priv->grab_window);
	}
	priv->grab_window = NULL;
	priv->grab_display = NULL;
}

#ifdef ENABLE_X11

static void gs_grab_nuke_focus (GdkDisplay *display);

static const char *
grab_string (int status)
{
	switch (status)
	{
	case GDK_GRAB_SUCCESS:
		return "GrabSuccess";
	case GDK_GRAB_ALREADY_GRABBED:
		return "AlreadyGrabbed";
	case GDK_GRAB_INVALID_TIME:
		return "GrabInvalidTime";
	case GDK_GRAB_NOT_VIEWABLE:
		return "GrabNotViewable";
	case GDK_GRAB_FROZEN:
		return "GrabFrozen";
	case GDK_GRAB_FAILED:
		return "GrabFailed";
	default:
	{
		static char foo [255];
		sprintf (foo, "unknown status: %d", status);
		return foo;
	}
	}
}

static void
xorg_lock_smasher_set_active (GSGrab  *grab,
                              gboolean active)
{
}

static void
prepare_window_grab_cb (GdkSeat   *seat,
                        GdkWindow *window,
                        gpointer   user_data)
{
	gdk_window_show_unraised (window);
}

static int
gs_grab_get (GSGrab     *grab,
             GdkWindow  *window,
             GdkDisplay *display,
             gboolean    no_pointer_grab,
             gboolean    hide_cursor)
{
	GSGrabPrivate *priv = GRAB_GET_PRIVATE (grab);
	GdkGrabStatus status;
	GdkSeat      *seat;
	GdkSeatCapabilities caps;
	GdkCursor    *cursor;

	g_return_val_if_fail (window != NULL, FALSE);
	g_return_val_if_fail (display != NULL, FALSE);

	cursor = gdk_cursor_new_for_display (display, GDK_BLANK_CURSOR);

	gs_debug ("Grabbing devices for window=%X", (guint32) GDK_WINDOW_XID (window));

	seat = gdk_display_get_default_seat (display);
	if (!no_pointer_grab)
		caps = GDK_SEAT_CAPABILITY_ALL;
	else
		caps = GDK_SEAT_CAPABILITY_KEYBOARD;

	status = gdk_seat_grab (seat, window,
	                        caps, TRUE,
	                        (hide_cursor ? cursor : NULL),
	                        NULL,
	                        prepare_window_grab_cb,
	                        NULL);

	/* make it release grabbed pointer if requested and if any;
	   time between grabbing and ungrabbing is minimal as grab was already
	   completed once */
	if (status == GDK_GRAB_SUCCESS && no_pointer_grab &&
	    gdk_display_device_is_grabbed (display, gdk_seat_get_pointer (seat)))
	{
		gs_grab_release (grab, FALSE);
		gs_debug ("Regrabbing keyboard");
		status = gdk_seat_grab (seat, window,
		                        caps, TRUE,
		                        (hide_cursor ? cursor : NULL),
		                        NULL, NULL, NULL);
	}

	if (status == GDK_GRAB_SUCCESS)
	{
		if (priv->grab_window != NULL)
		{
			g_object_remove_weak_pointer (G_OBJECT (priv->grab_window),
			                              (gpointer *) &priv->grab_window);
		}
		priv->grab_window = window;

		g_object_add_weak_pointer (G_OBJECT (priv->grab_window),
		                           (gpointer *) &priv->grab_window);

		priv->grab_display = display;
		priv->no_pointer_grab = (no_pointer_grab != FALSE);
		priv->hide_cursor = (hide_cursor != FALSE);
	}

	g_object_unref (G_OBJECT (cursor));

	return status;
}

static gboolean
x11_grab_window (GSGrab     *grab,
                 GdkWindow  *window,
                 GdkDisplay *display,
                 gboolean    no_pointer_grab,
                 gboolean    hide_cursor)
{
	gboolean    status = FALSE;
	int         i;
	int         retries = 12;

	for (i = 0; i < retries; i++)
	{
		status = gs_grab_get (grab, window, display,
		                      no_pointer_grab, hide_cursor);
		if (status == GDK_GRAB_SUCCESS)
		{
			break;
		}
		else if (i == (int) (retries / 2))
		{
			gs_grab_nuke_focus (display);
		}

		g_usleep (G_USEC_PER_SEC);
	}

	if (status != GDK_GRAB_SUCCESS)
	{
		gs_debug ("Couldn't grab devices!  (%s)",
		          grab_string (status));
		return FALSE;
	}

	return TRUE;
}

static void
gs_grab_nuke_focus (GdkDisplay *display)
{
	Window focus = 0;
	int    rev = 0;

	gs_debug ("Nuking focus");

	gdk_x11_display_error_trap_push (display);

	XGetInputFocus (GDK_DISPLAY_XDISPLAY (display), &focus, &rev);
	XSetInputFocus (GDK_DISPLAY_XDISPLAY (display), None,
	                RevertToNone, CurrentTime);

	gdk_x11_display_error_trap_pop_ignored (display);
}

static gboolean
gs_grab_move (GSGrab     *grab,
              GdkWindow  *window,
              GdkDisplay *display,
              gboolean    no_pointer_grab,
              gboolean    hide_cursor)
{
	GSGrabPrivate *priv = GRAB_GET_PRIVATE (grab);
	int         result;
	GdkWindow  *old_window;
	GdkDisplay *old_display;
	gboolean    old_hide_cursor;

	if (priv->grab_window == window &&
	    priv->no_pointer_grab == no_pointer_grab)
	{
		gs_debug ("Window %X is already grabbed, skipping",
		          (guint32) GDK_WINDOW_XID (priv->grab_window));
		return TRUE;
	}

	if (priv->grab_window != NULL)
	{
		gs_debug ("Moving devices grab from %X to %X",
		          (guint32) GDK_WINDOW_XID (priv->grab_window),
		          (guint32) GDK_WINDOW_XID (window));
	}
	else
	{
		gs_debug ("Getting devices grab on %X",
		          (guint32) GDK_WINDOW_XID (window));
	}

	gs_debug ("*** doing X server grab");
	gdk_x11_display_grab (display);

	old_window = priv->grab_window;
	old_display = priv->grab_display;
	old_hide_cursor = priv->hide_cursor;

	if (old_window)
	{
		gs_grab_release (grab, FALSE);
	}

	result = gs_grab_get (grab, window, display,
	                      no_pointer_grab, hide_cursor);

	if (result != GDK_GRAB_SUCCESS)
	{
		g_usleep (G_USEC_PER_SEC);
		result = gs_grab_get (grab, window, display,
		                      no_pointer_grab, hide_cursor);
	}

	if ((result != GDK_GRAB_SUCCESS) && old_window)
	{
		int old_result;

		gs_debug ("Could not grab devices for new window. Resuming previous grab.");
		old_result = gs_grab_get (grab, old_window, old_display,
		                          no_pointer_grab, old_hide_cursor);
		if (old_result != GDK_GRAB_SUCCESS)
			gs_debug ("Could not grab devices for old window");
	}

	gs_debug ("*** releasing X server grab");
	gdk_x11_display_ungrab (display);
	gdk_display_flush (display);

	return (result == GDK_GRAB_SUCCESS);
}

#endif /* ENABLE_X11 */

#ifdef ENABLE_WAYLAND

static gboolean
wayland_grab_window (GSGrab     *grab,
                     GdkWindow  *window,
                     GdkDisplay *display,
                     gboolean    no_pointer_grab,
                     gboolean    hide_cursor)
{
	GSGrabPrivate *priv = GRAB_GET_PRIVATE (grab);

	g_return_val_if_fail (window != NULL, FALSE);
	g_return_val_if_fail (display != NULL, FALSE);

	gs_debug ("Wayland: storing window reference (compositor handles grab)");

	if (priv->grab_window != NULL)
	{
		g_object_remove_weak_pointer (G_OBJECT (priv->grab_window),
		                              (gpointer *) &priv->grab_window);
	}

	priv->grab_window = window;
	g_object_add_weak_pointer (G_OBJECT (priv->grab_window),
	                           (gpointer *) &priv->grab_window);

	priv->grab_display = display;
	priv->no_pointer_grab = (no_pointer_grab != FALSE);
	priv->hide_cursor = (hide_cursor != FALSE);

	return TRUE;
}

#endif /* ENABLE_WAYLAND */

void
gs_grab_release (GSGrab  *grab,
                 gboolean flush)
{
	GdkDisplay *display;

	g_return_if_fail (GS_IS_GRAB (grab));

	display = gdk_display_get_default ();

#ifdef ENABLE_X11
	if (GDK_IS_X11_DISPLAY (display))
	{
		GdkSeat *seat = gdk_display_get_default_seat (display);

		gs_debug ("Ungrabbing devices");
		gdk_seat_ungrab (seat);
	}
#endif
#ifdef ENABLE_WAYLAND
	if (GDK_IS_WAYLAND_DISPLAY (display))
	{
		gs_debug ("Releasing Wayland grab (compositor handles input)");
	}
#endif

	gs_grab_reset (grab);

	if (flush)
	{
#ifdef ENABLE_X11
		if (GDK_IS_X11_DISPLAY (display))
		{
			xorg_lock_smasher_set_active (grab, TRUE);
			gdk_display_sync (display);
		}
#endif
		gdk_display_flush (display);
	}
}

gboolean
gs_grab_grab_window (GSGrab     *grab,
                     GdkWindow  *window,
                     GdkDisplay *display,
                     gboolean    no_pointer_grab,
                     gboolean    hide_cursor)
{
	g_return_val_if_fail (GS_IS_GRAB (grab), FALSE);

#ifdef ENABLE_WAYLAND
	if (GDK_IS_WAYLAND_DISPLAY (display))
	{
		return wayland_grab_window (grab, window, display,
		                            no_pointer_grab, hide_cursor);
	}
#endif
#ifdef ENABLE_X11
	if (GDK_IS_X11_DISPLAY (display))
	{
		return x11_grab_window (grab, window, display,
		                        no_pointer_grab, hide_cursor);
	}
#endif

	g_warning ("No grab handler available for this display");
	return FALSE;
}

gboolean
gs_grab_grab_root (GSGrab  *grab,
                   gboolean no_pointer_grab,
                   gboolean hide_cursor)
{
	GdkDisplay *display;
	GdkWindow  *root;
	GdkScreen  *screen;
	GdkDevice  *device;
	gboolean    res;

	gs_debug ("Grabbing the root window");

	display = gdk_display_get_default ();
	device = gdk_seat_get_pointer (gdk_display_get_default_seat (display));
	gdk_device_get_position (device, &screen, NULL, NULL);
	root = gdk_screen_get_root_window (screen);

	res = gs_grab_grab_window (grab, root, display,
	                           no_pointer_grab, hide_cursor);

	return res;
}

gboolean
gs_grab_grab_offscreen (GSGrab  *grab,
                        gboolean no_pointer_grab,
                        gboolean hide_cursor)
{
	gs_debug ("Grabbing an offscreen window");

#ifdef ENABLE_X11
	if (GDK_IS_X11_DISPLAY (gdk_display_get_default ()))
	{
		GSGrabPrivate *priv = GRAB_GET_PRIVATE (grab);
		GtkWidget  *invisible = priv->invisible;
		GdkWindow  *window;
		GdkDisplay *display;
		GdkScreen  *screen;

		window = gtk_widget_get_window (GTK_WIDGET (invisible));
		screen = gtk_invisible_get_screen (GTK_INVISIBLE (invisible));
		display = gdk_screen_get_display (screen);

		return gs_grab_grab_window (grab, window, display,
		                            no_pointer_grab, hide_cursor);
	}
#endif
#ifdef ENABLE_WAYLAND
	if (GDK_IS_WAYLAND_DISPLAY (gdk_display_get_default ()))
	{
		GtkWidget  *invisible;
		GdkWindow  *window;
		GdkDisplay *display;
		GdkScreen  *screen;
		gboolean    res;

		invisible = gtk_invisible_new ();
		gtk_widget_show (invisible);

		window = gtk_widget_get_window (invisible);
		screen = gtk_invisible_get_screen (GTK_INVISIBLE (invisible));
		display = gdk_screen_get_display (screen);

		res = gs_grab_grab_window (grab, window, display,
		                           no_pointer_grab, hide_cursor);

		gtk_widget_destroy (invisible);

		return res;
	}
#endif

	g_warning ("No grab handler available for this display");
	return FALSE;
}

void
gs_grab_move_to_window (GSGrab     *grab,
                        GdkWindow  *window,
                        GdkDisplay *display,
                        gboolean    no_pointer_grab,
                        gboolean    hide_cursor)
{
	g_return_if_fail (GS_IS_GRAB (grab));

#ifdef ENABLE_WAYLAND
	if (GDK_IS_WAYLAND_DISPLAY (display))
	{
		GSGrabPrivate *priv = GRAB_GET_PRIVATE (grab);

		gs_debug ("Wayland: moving grab to new window");

		if (priv->grab_window == window &&
		    priv->no_pointer_grab == no_pointer_grab)
		{
			gs_debug ("Window is already grabbed, skipping");
			return;
		}

		wayland_grab_window (grab, window, display,
		                     no_pointer_grab, hide_cursor);
		return;
	}
#endif
#ifdef ENABLE_X11
	if (GDK_IS_X11_DISPLAY (display))
	{
		gboolean result = FALSE;

		xorg_lock_smasher_set_active (grab, FALSE);

		while (!result)
		{
			result = gs_grab_move (grab, window, display,
			                       no_pointer_grab, hide_cursor);
			gdk_display_flush (display);
		}
		return;
	}
#endif

	g_warning ("No grab handler available for this display");
}

static void
gs_grab_finalize (GObject *object)
{
#ifdef ENABLE_X11
	GSGrab *grab = GS_GRAB (object);
	GSGrabPrivate *priv = GRAB_GET_PRIVATE (grab);

	if (priv->invisible != NULL)
	{
		gtk_widget_destroy (priv->invisible);
		priv->invisible = NULL;
	}
#endif

	G_OBJECT_CLASS (gs_grab_parent_class)->finalize (object);
}

static void
gs_grab_class_init (GSGrabClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = gs_grab_finalize;
}

static void
gs_grab_init (GSGrab *grab)
{
	GSGrabPrivate *priv = GRAB_GET_PRIVATE (grab);

	priv->no_pointer_grab = FALSE;
	priv->hide_cursor = FALSE;

#ifdef ENABLE_X11
	priv->invisible = gtk_invisible_new ();
	gtk_widget_show (priv->invisible);
#endif
}

GSGrab *
gs_grab_new (void)
{
	return g_object_new (GS_TYPE_GRAB, NULL);
}
