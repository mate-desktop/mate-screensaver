/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 8; tab-width: 8 -*-
 *
 * Copyright (C) 2025 Kane Scipioni <k.scipioni@gmail.com>
 * Copyright (C) 2012-2025 MATE Developers
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>

#include <glib/gi18n.h>

#include "gs-theme-window.h"
#include "gs-theme-engine.h"
#include "gste-starfield.h"

int
main (int argc, char **argv)
{
	GSThemeEngine *engine;
	GtkWidget     *window;
	GError        *error;
	gboolean       ret;
	gint           count = 0;
	gdouble        speed = 0.0;
	gdouble        acceleration = 0.0;
	gint           delay = 0;
	gdouble        size = 0.0;
	GOptionEntry  entries [] =
	{
		{
			"count", 'c', 0, G_OPTION_ARG_INT, &count,
			N_("Number of stars [1-500]"), N_("NUM")
		},
		{
			"speed", 's', 0, G_OPTION_ARG_DOUBLE, &speed,
			N_("Speed of camera [1.0-10.0]"), N_("RATE")
		},
		{
			"acceleration", 'a', 0, G_OPTION_ARG_DOUBLE, &acceleration,
			N_("Acceleration of camera [0.1-10.0]"), N_("ACCEL")
		},
		{
			"delay", 'd', 0, G_OPTION_ARG_INT, &delay,
			N_("Time between frames [5-33]"), N_("MSEC")
		},
		{
			"size", 'r', 0, G_OPTION_ARG_DOUBLE, &size,
			N_("Max radius of stars [1-20]"), N_("RADIUS")
		},
		{ NULL, 0, 0, G_OPTION_ARG_NONE, NULL, NULL, NULL }
	};

	bindtextdomain (GETTEXT_PACKAGE, MATELOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	error = NULL;

	ret = gtk_init_with_args (&argc, &argv,
	                          NULL,
	                          entries,
	                          NULL,
	                          &error);
	if (!ret)
	{
		g_printerr (_("%s. See --help for usage information.\n"),
		            error->message);
		g_error_free (error);
		exit (1);
	}

	window = gs_theme_window_new ();
	g_signal_connect (window, "delete-event",
	                  G_CALLBACK (gtk_main_quit),
	                  NULL);

	g_set_prgname ("starfield");

	engine = g_object_new (GSTE_TYPE_STARFIELD, NULL);

	if (count)
	{
		g_object_set (engine, "count", count, NULL);
	}

	if (speed)
	{
		g_object_set (engine, "speed", speed, NULL);
	}

	if (acceleration)
	{
		g_object_set (engine, "acceleration", acceleration, NULL);
	}

	if (delay)
	{
		g_object_set (engine, "delay", delay, NULL);
	}

	if (size)
	{
		g_object_set (engine, "size", size, NULL);
	}

	gtk_container_add (GTK_CONTAINER (window), GTK_WIDGET (engine));

	gtk_widget_show (GTK_WIDGET (engine));

	gtk_window_set_default_size (GTK_WINDOW (window), 640, 480);
	gtk_widget_show (window);

	gtk_main ();

	return 0;
}
