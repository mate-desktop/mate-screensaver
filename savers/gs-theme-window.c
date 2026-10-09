/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 8; tab-width: 8 -*-
 *
 * gs-theme-window.c - special toplevel for screensavers
 *
 * Copyright (C) 2005 Ray Strode <rstrode@redhat.com>
 * Copyright (C) 2012-2021 MATE Developers
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Originally written by: Ray Strode <rstrode@redhat.com>
 */

#include "config.h"

#include <errno.h>
#include <stdlib.h>

#include <glib.h>
#include <glib-object.h>
#include <glib/gi18n.h>

#ifdef ENABLE_X11
#include <gdk/gdkx.h>
#include <gtk/gtkx.h>
#endif
#ifdef ENABLE_WAYLAND
#include <gdk/gdkwayland.h>
#include <libwlembed-gtk3/libwlembed-gtk3.h>
#endif
#include <gtk/gtk.h>

#include "gs-theme-window.h"

static void gs_theme_window_finalize     (GObject *object);

static GObjectClass   *parent_class = NULL;

G_DEFINE_TYPE (GSThemeWindow, gs_theme_window, GTK_TYPE_WINDOW)

static void
gs_theme_window_class_init (GSThemeWindowClass *klass)
{
	GObjectClass   *object_class;

	object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = gs_theme_window_finalize;
}

static void
gs_theme_window_init (GSThemeWindow *window)
{
	gtk_widget_set_app_paintable (GTK_WIDGET (window), TRUE);
}

static void
gs_theme_window_finalize (GObject *object)
{
	GObjectClass  *parent_class;

	GS_THEME_WINDOW (object);

	parent_class = G_OBJECT_CLASS (gs_theme_window_parent_class);

	if (parent_class->finalize != NULL)
		parent_class->finalize (object);
}

GtkWidget *
gs_theme_window_new (void)
{
	const char *preview_xid;

	preview_xid = g_getenv ("XSCREENSAVER_WINDOW");

#ifdef ENABLE_X11
	if (GDK_IS_X11_DISPLAY (gdk_display_get_default ()) &&
	    preview_xid != NULL && preview_xid[0] != '\0')
	{
		char  *end;
		gulong remote_xwindow;

		errno = 0;
		remote_xwindow = strtoul (preview_xid, &end, 0);

		if ((remote_xwindow != 0) &&
		    (end != NULL) && (*end == '\0') &&
		    (errno != ERANGE))
		{
			GtkWidget *window;

			window = gtk_plug_new_for_display (gdk_display_get_default (),
			                                   remote_xwindow);
			gtk_widget_set_app_paintable (window, TRUE);

			g_debug ("gs_theme_window_new: created GtkPlug for window 0x%lX",
			         remote_xwindow);

			return window;
		}
	}
#endif
#ifdef ENABLE_WAYLAND
	if (GDK_IS_WAYLAND_DISPLAY (gdk_display_get_default ()))
	{
		if (preview_xid != NULL && preview_xid[0] != '\0')
		{
			GtkWidget *window;

			window = wle_gtk_plug_new (preview_xid);
			gtk_widget_set_app_paintable (window, TRUE);

			g_debug ("gs_theme_window_new: created WleGtkPlug for token '%s'",
			         preview_xid);

			return window;
		}
	}
#endif

	{
		GSThemeWindow *window;

		window = g_object_new (GS_TYPE_THEME_WINDOW,
		                       "type", GTK_WINDOW_TOPLEVEL,
		                       NULL);

		g_debug ("gs_theme_window_new: falling back to GSThemeWindow toplevel");

		return GTK_WIDGET (window);
	}
}
