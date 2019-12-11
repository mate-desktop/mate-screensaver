/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 8; tab-width: 8 -*-
 *
 * Copyright (C) 2005 William Jon McCann <mccann@jhu.edu>
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <glib.h>
#include <gtk/gtk.h>

#include "gs-theme-engine.h"
#include "gs-theme-engine-marshal.h"

static void     gs_theme_engine_finalize   (GObject            *object);

struct GSThemeEnginePrivate
{
	gpointer reserved;
};

static GObjectClass *parent_class = NULL;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (GSThemeEngine, gs_theme_engine, GTK_TYPE_DRAWING_AREA)

void
_gs_theme_engine_profile_log (const char *func,
                              const char *note,
                              const char *format,
                              ...)
{
	va_list args;
	char   *str;
	char   *formatted;

	va_start (args, format);
	formatted = g_strdup_vprintf (format, args);
	va_end (args);

	if (func != NULL)
	{
		str = g_strdup_printf ("MARK: %s %s: %s %s", g_get_prgname(), func, note ? note : "", formatted);
	}
	else
	{
		str = g_strdup_printf ("MARK: %s: %s %s", g_get_prgname(), note ? note : "", formatted);
	}

	g_free (formatted);

	access (str, F_OK);
	g_free (str);
}

static void
gs_theme_engine_set_property (GObject            *object,
                              guint               prop_id,
                              const GValue       *value,
                              GParamSpec         *pspec)
{
	GS_THEME_ENGINE (object);

	switch (prop_id)
	{
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_theme_engine_get_property (GObject            *object,
                              guint               prop_id,
                              GValue             *value,
                              GParamSpec         *pspec)
{
	GS_THEME_ENGINE (object);

	switch (prop_id)
	{
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static gboolean
gs_theme_engine_real_draw (GtkWidget *widget,
                           cairo_t   *cr)
{
	cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
	cairo_set_source_rgb (cr, 0, 0, 0);
	cairo_paint (cr);

	return FALSE;
}

static void
gs_theme_engine_class_init (GSThemeEngineClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = gs_theme_engine_finalize;
	object_class->get_property = gs_theme_engine_get_property;
	object_class->set_property = gs_theme_engine_set_property;

	widget_class->draw = gs_theme_engine_real_draw;
}

static void
gs_theme_engine_init (GSThemeEngine *engine)
{
	engine->priv = gs_theme_engine_get_instance_private (engine);
}

static void
gs_theme_engine_finalize (GObject *object)
{
	GSThemeEngine *engine;

	g_return_if_fail (object != NULL);
	g_return_if_fail (GS_IS_THEME_ENGINE (object));

	engine = GS_THEME_ENGINE (object);

	g_return_if_fail (engine->priv != NULL);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

void
gs_theme_engine_get_window_size (GSThemeEngine *engine,
                                 int           *width,
                                 int           *height)
{
	if (width != NULL)
	{
		*width = 0;
	}
	if (height != NULL)
	{
		*height = 0;
	}

	g_return_if_fail (GS_IS_THEME_ENGINE (engine));

	if (! gtk_widget_get_visible (GTK_WIDGET (engine)))
	{
		return;
	}

	gdk_window_get_geometry (gtk_widget_get_window (GTK_WIDGET (engine)),
	                         NULL,
	                         NULL,
	                         width,
	                         height);
}

GdkWindow *
gs_theme_engine_get_window (GSThemeEngine *engine)
{
	g_return_val_if_fail (GS_IS_THEME_ENGINE (engine), NULL);

	return gtk_widget_get_window (GTK_WIDGET (engine));
}
