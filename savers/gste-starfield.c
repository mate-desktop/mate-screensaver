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

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <glib.h>
#include <gtk/gtk.h>

#include "gs-theme-engine.h"
#include "gste-starfield.h"

static void     gste_starfield_finalize    (GObject             *object);
static void     draw_frame                 (GSTEStarfield       *sf,
                                            cairo_t *cr);

typedef struct _point
{
	gdouble x, y, z;
} point;

struct GSTEStarfieldPrivate
{
	guint         timeout_id;
	int64_t       timestamp;
	unsigned int  count;
	double        speed;
	double        current_speed;
	double        acceleration;
	guint         delay;
	double        size;
	point        *stars;
};

enum
{
	PROP_0,
	PROP_COUNT,
	PROP_SPEED,
	PROP_ACCEL,
	PROP_DELAY,
	PROP_SIZE
};

static GObjectClass *parent_class = NULL;

G_DEFINE_TYPE_WITH_PRIVATE (GSTEStarfield, gste_starfield, GS_TYPE_THEME_ENGINE)

#define Z_NEAR 0.0
#define Z_FAR 1.0
#define XY_CLIP_LIMIT 0.5

#define DEFAULT_ACCELERATION 1.0
#define MAX_ACCELERATION 10.0
#define MIN_ACCELERATION 0.1

#define DEFAULT_SPEED 3.0
#define MAX_SPEED 10.0
#define MIN_SPEED 1.0
#define SPEED_FACTOR 0.25

#define DEFAULT_COUNT 200
#define MAX_COUNT 500
#define MIN_COUNT 1

#define DEFAULT_DELAY 15
#define MAX_DELAY 33
#define MIN_DELAY 5

#define DEFAULT_SIZE 8
#define MAX_SIZE 20
#define MIN_SIZE 1
#define SIZE_RATIO 0.001

static void
gste_starfield_set_property (GObject            *object,
                             guint               prop_id,
                             const GValue       *value,
                             GParamSpec         *pspec)
{
	GSTEStarfield *self;

	self = GSTE_STARFIELD (object);

	switch (prop_id)
	{
	case PROP_COUNT:
		self->priv->count = g_value_get_int (value);
		break;
	case PROP_SPEED:
		self->priv->speed = g_value_get_double (value);
		break;
	case PROP_ACCEL:
		self->priv->acceleration = g_value_get_double (value);
		break;
	case PROP_DELAY:
		self->priv->delay = g_value_get_int (value);
		break;
	case PROP_SIZE:
		self->priv->size = g_value_get_double (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gste_starfield_get_property (GObject            *object,
                             guint               prop_id,
                             GValue             *value,
                             GParamSpec         *pspec)
{
	GSTEStarfield *self;

	self = GSTE_STARFIELD (object);

	switch (prop_id)
	{
	case PROP_COUNT:
		g_value_set_int (value, self->priv->count);
		break;
	case PROP_SPEED:
		g_value_set_double (value, self->priv->speed);
		break;
	case PROP_ACCEL:
		g_value_set_double (value, self->priv->acceleration);
		break;
	case PROP_DELAY:
		g_value_set_int (value, self->priv->delay);
		break;
	case PROP_SIZE:
		g_value_set_double (value, self->priv->size);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
setup_stars (GSTEStarfield *sf)
{
	int i;

	if (sf->priv->stars)
	{
		g_free (sf->priv->stars);
	}
	sf->priv->stars = g_new(point, sf->priv->count);

	for (i = 0; i < sf->priv->count; ++i)
	{
		point *s = &sf->priv->stars[i];
		s->z = -cbrt(g_random_double() - 1); /* icdf for beta(1, 3) distribution */
		s->x = s->z * g_random_double_range(-XY_CLIP_LIMIT, XY_CLIP_LIMIT);
		s->y = s->z * g_random_double_range(-XY_CLIP_LIMIT, XY_CLIP_LIMIT);
	}
	sf->priv->timestamp = g_get_monotonic_time ();
	sf->priv->current_speed = 0.0;
}

static double
get_elapsed_time (GSTEStarfield *sf)
{
	int64_t prev_timestamp = sf->priv->timestamp;

	sf->priv->timestamp = g_get_monotonic_time ();

	int64_t elapsed = sf->priv->timestamp - prev_timestamp;

	return (double)elapsed / 1e6;
}

static gboolean
draw_iter (GSTEStarfield *sf)
{
	gtk_widget_queue_draw (GTK_WIDGET (sf));
	return TRUE;
}

static void
gste_starfield_real_show (GtkWidget *widget)
{
	GSTEStarfield *sf = GSTE_STARFIELD (widget);

	/* start */
	setup_stars (sf);

	if (sf->priv->timeout_id > 0)
	{
		g_source_remove (sf->priv->timeout_id);
	}
	sf->priv->timeout_id = g_timeout_add (sf->priv->delay,
	                                      (GSourceFunc)draw_iter,
	                                      sf);

	if (GTK_WIDGET_CLASS (parent_class)->show)
	{
		GTK_WIDGET_CLASS (parent_class)->show (widget);
	}
}

static gboolean
gste_starfield_real_draw (GtkWidget *widget,
                          cairo_t   *cr)
{
	if (GTK_WIDGET_CLASS (parent_class)->draw) {
		GTK_WIDGET_CLASS (parent_class)->draw (widget, cr);
	}

	draw_frame (GSTE_STARFIELD (widget), cr);

	return TRUE;
}

static gboolean
gste_starfield_real_configure (GtkWidget         *widget,
                               GdkEventConfigure *event)
{
	GSTEStarfield *sf = GSTE_STARFIELD (widget);
	gboolean       handled = FALSE;

	/* resize */

	/* just reset everything */
	setup_stars (sf);

	/* schedule a redraw */
	gtk_widget_queue_draw (widget);

	if (GTK_WIDGET_CLASS (parent_class)->configure_event)
	{
		handled = GTK_WIDGET_CLASS (parent_class)->configure_event (widget, event);
	}

	return handled;
}

static void
gste_starfield_class_init (GSTEStarfieldClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = gste_starfield_finalize;
	object_class->get_property = gste_starfield_get_property;
	object_class->set_property = gste_starfield_set_property;

	widget_class->show = gste_starfield_real_show;
	widget_class->draw = gste_starfield_real_draw;
	widget_class->configure_event = gste_starfield_real_configure;

	g_object_class_install_property (object_class,
	                                 PROP_COUNT,
	                                 g_param_spec_int ("count",
	                                         NULL,
	                                         NULL,
	                                         MIN_COUNT,
	                                         MAX_COUNT,
	                                         DEFAULT_COUNT,
	                                         G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
	                                 PROP_SPEED,
	                                 g_param_spec_double ("speed",
	                                         NULL,
	                                         NULL,
	                                         MIN_SPEED,
	                                         MAX_SPEED,
	                                         DEFAULT_SPEED,
	                                         G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
	                                 PROP_ACCEL,
	                                 g_param_spec_double ("acceleration",
	                                         NULL,
	                                         NULL,
	                                         MIN_ACCELERATION,
	                                         MAX_ACCELERATION,
	                                         DEFAULT_ACCELERATION,
	                                         G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
	                                 PROP_DELAY,
	                                 g_param_spec_int ("delay",
	                                         NULL,
	                                         NULL,
	                                         MIN_DELAY,
	                                         MAX_DELAY,
	                                         DEFAULT_DELAY,
	                                         G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
	                                 PROP_SIZE,
	                                 g_param_spec_double ("size",
	                                         NULL,
	                                         NULL,
	                                         MIN_SIZE,
	                                         MAX_SIZE,
	                                         DEFAULT_SIZE,
	                                         G_PARAM_READWRITE));
}

static void
draw_frame (GSTEStarfield *sf,
            cairo_t       *cr)
{
	int i;
	int window_width;
	int window_height;
	GdkWindow *window;

	window = gs_theme_engine_get_window (GS_THEME_ENGINE (sf));

	if (window == NULL)
	{
		return;
	}

	gs_theme_engine_get_window_size (GS_THEME_ENGINE (sf),
									 &window_width,
									 &window_height);

	double elapsed = get_elapsed_time(sf);
	double speed = sf->priv->current_speed;
	double step = SPEED_FACTOR * speed * elapsed;
	double max_radius = fmax(sf->priv->size * SIZE_RATIO * window_height, 1);

	cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_antialias (cr, CAIRO_ANTIALIAS_FAST);

	for (i = 0; i < sf->priv->count; ++i)
	{
		point *s = &sf->priv->stars[i];

		s->z -= step;

		if (s->z <= Z_NEAR ||
		    s->x > s->z *  XY_CLIP_LIMIT ||
		    s->x < s->z * -XY_CLIP_LIMIT ||
		    s->y > s->z *  XY_CLIP_LIMIT ||
		    s->y < s->z * -XY_CLIP_LIMIT)
		{
			s->z = Z_FAR - g_random_double_range(0, fmin(step, Z_FAR - Z_NEAR));
			s->x = s->z * g_random_double_range(-XY_CLIP_LIMIT, XY_CLIP_LIMIT);
			s->y = s->z * g_random_double_range(-XY_CLIP_LIMIT, XY_CLIP_LIMIT);
		}

		double x = (s->x / s->z + XY_CLIP_LIMIT) * window_width;
		double y = (s->y / s->z + XY_CLIP_LIMIT) * window_height;
		double illum = 1.0 / (1.0 + s->z);
		illum *= illum;
		double radius = max_radius * (1.0 - s->z);

		cairo_set_source_rgb (cr, illum, illum, illum);
		cairo_set_line_width (cr, radius);
		cairo_move_to (cr, x, y);
		cairo_line_to (cr, x, y);
		cairo_stroke (cr);
	}
	if (speed != sf->priv->speed)
	{
		speed += sf->priv->acceleration * elapsed;
		sf->priv->current_speed = fmin(speed, sf->priv->speed);
	}
}

static void
gste_starfield_init (GSTEStarfield *sf)
{
	sf->priv = gste_starfield_get_instance_private (sf);

	sf->priv->count = DEFAULT_COUNT;
	sf->priv->speed = DEFAULT_SPEED;
	sf->priv->acceleration = DEFAULT_ACCELERATION;
	sf->priv->delay = DEFAULT_DELAY;
	sf->priv->size = DEFAULT_SIZE;
}

static void
gste_starfield_finalize (GObject *object)
{
	GSTEStarfield *sf;

	g_return_if_fail (object != NULL);
	g_return_if_fail (GSTE_IS_STARFIELD (object));

	sf = GSTE_STARFIELD (object);

	g_return_if_fail (sf->priv != NULL);

	if (sf->priv->timeout_id > 0)
	{
		g_source_remove (sf->priv->timeout_id);
		sf->priv->timeout_id = 0;
	}

	g_free (sf->priv->stars);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}
