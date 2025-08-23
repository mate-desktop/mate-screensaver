/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2025 Kane Scipioni <k.scipioni@gmail.com>
 * Copyright (C) 2012-2025 MATE Developers
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
 *
 */

#ifndef __GSTE_STARFIELD_H
#define __GSTE_STARFIELD_H

#include <glib.h>
#include <gdk/gdk.h>
#include "gs-theme-engine.h"

G_BEGIN_DECLS

#define GSTE_TYPE_STARFIELD         (gste_starfield_get_type ())
#define GSTE_STARFIELD(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GSTE_TYPE_STARFIELD, GSTEStarfield))
#define GSTE_STARFIELD_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GSTE_TYPE_STARFIELD, GSTEStarfieldClass))
#define GSTE_IS_STARFIELD(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GSTE_TYPE_STARFIELD))
#define GSTE_IS_STARFIELD_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GSTE_TYPE_STARFIELD))
#define GSTE_STARFIELD_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GSTE_TYPE_STARFIELD, GSTEStarfieldClass))

typedef struct GSTEStarfieldPrivate GSTEStarfieldPrivate;

typedef struct
{
	GSThemeEngine          parent;
	GSTEStarfieldPrivate  *priv;
} GSTEStarfield;

typedef struct
{
	GSThemeEngineClass     parent_class;
} GSTEStarfieldClass;

GType           gste_starfield_get_type         (void);
GSThemeEngine  *gste_starfield_new              (void);

G_END_DECLS

#endif /* __GSTE_STARFIELD_H */
