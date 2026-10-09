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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#ifdef ENABLE_WAYLAND

#include <gdk/gdk.h>
#include <gdk/gdkwayland.h>

#include "ext-session-lock-client.h"

#include "gs-debug.h"
#include "gs-window.h"
#include "gs-session-lock-manager.h"

typedef struct
{
	struct ext_session_lock_surface_v1 *surface;
	struct wl_output                   *output;
} GSLockSurface;

struct _GSSessionLockManager
{
	GObject __parent__;

	struct wl_registry                 *wl_registry;
	struct ext_session_lock_manager_v1 *wl_manager;
	struct ext_session_lock_v1         *wl_lock;
	GHashTable                         *lock_surfaces;
	gboolean                            locked;
	gboolean                            finished;
};

enum
{
	SIGNAL_LOCKED = 0,
	SIGNAL_FINISHED,
	N_SIGNALS
};

static guint signals[N_SIGNALS] = { 0, };

G_DEFINE_TYPE (GSSessionLockManager, gs_session_lock_manager, G_TYPE_OBJECT)

static void
on_registry_global (void               *data,
                    struct wl_registry *registry,
                    uint32_t            id,
                    const char         *interface,
                    uint32_t            version)
{
	GSSessionLockManager *manager = data;

	if (g_strcmp0 (interface, ext_session_lock_manager_v1_interface.name) == 0)
	{
		manager->wl_manager = wl_registry_bind (manager->wl_registry,
		                                        id,
		                                        &ext_session_lock_manager_v1_interface,
		                                        MIN ((uint32_t) ext_session_lock_manager_v1_interface.version, version));
	}
}

static void
on_registry_global_remove (void               *data,
                           struct wl_registry *registry,
                           uint32_t            id)
{
}

static const struct wl_registry_listener registry_listener =
{
	.global        = on_registry_global,
	.global_remove = on_registry_global_remove,
};

static void
on_lock_locked (void                        *data,
                struct ext_session_lock_v1 *lock)
{
	GSSessionLockManager *manager = data;

	gs_debug ("Session lock confirmed by compositor");
	manager->locked = TRUE;
	g_signal_emit (manager, signals[SIGNAL_LOCKED], 0);
}

static void
on_lock_finished (void                        *data,
                  struct ext_session_lock_v1 *lock)
{
	GSSessionLockManager *manager = data;

	gs_debug ("Session lock finished by compositor");

	/* The compositor has already destroyed all lock surfaces on its side,
	 * so we must NOT send a destroy request for them: just drop our
	 * references. */
	manager->finished = TRUE;
	g_hash_table_remove_all (manager->lock_surfaces);

	/* Protocol: use unlock_and_destroy if the locked event was received,
	 * otherwise destroy. */
	if (manager->locked)
	{
		ext_session_lock_v1_unlock_and_destroy (manager->wl_lock);
	}
	else
	{
		ext_session_lock_v1_destroy (manager->wl_lock);
	}

	manager->wl_lock = NULL;
	manager->locked = FALSE;

	g_signal_emit (manager, signals[SIGNAL_FINISHED], 0);
}

static const struct ext_session_lock_v1_listener lock_listener =
{
	.locked   = on_lock_locked,
	.finished = on_lock_finished,
};

static void
on_surface_configure (void                               *data,
                      struct ext_session_lock_surface_v1 *surface,
                      uint32_t                            serial,
                      uint32_t                            width,
                      uint32_t                            height)
{
	GSWindow  *window = data;
	GdkWindow *gdk_window;

	/* Size the window before acknowledging the configure, and make sure
	 * the GdkWindow still exists (the window may be going away). */
	gdk_window = gtk_widget_get_window (GTK_WIDGET (window));
	if (gdk_window != NULL)
	{
		gdk_window_move_resize (gdk_window, 0, 0, width, height);
	}

	ext_session_lock_surface_v1_ack_configure (surface, serial);

	gs_debug ("Lock surface configured: %ux%u", width, height);
}

static const struct ext_session_lock_surface_v1_listener surface_listener =
{
	.configure = on_surface_configure,
};

static gboolean
has_surface_for_output (GSSessionLockManager *manager,
                        struct wl_output     *output)
{
	GHashTableIter iter;
	gpointer       value;

	if (output == NULL)
	{
		return FALSE;
	}

	g_hash_table_iter_init (&iter, manager->lock_surfaces);
	while (g_hash_table_iter_next (&iter, NULL, &value))
	{
		GSLockSurface *lock_surface = value;

		if (lock_surface->output == output)
		{
			return TRUE;
		}
	}

	return FALSE;
}

static void
on_window_realize (GtkWidget            *widget,
                   GSSessionLockManager *manager)
{
	GSWindow                            *window = GS_WINDOW (widget);
	GdkWindow                           *gdk_window;
	GdkMonitor                          *monitor;
	struct wl_surface                   *wl_surface;
	struct wl_output                    *wl_output = NULL;
	struct ext_session_lock_surface_v1  *surface;
	GSLockSurface                       *lock_surface;

	if (manager->wl_lock == NULL)
	{
		return;
	}

	/* Idempotent: realize may fire more than once for a window. */
	if (g_hash_table_lookup (manager->lock_surfaces, window) != NULL)
	{
		return;
	}

	gdk_window = gtk_widget_get_window (widget);
	if (gdk_window == NULL)
	{
		gs_debug ("No GDK window available for lock surface");
		return;
	}

	monitor = gs_window_get_monitor (window);
	if (monitor != NULL)
	{
		wl_output = gdk_wayland_monitor_get_wl_output (monitor);
	}

	/* Never create two lock surfaces for the same output. */
	if (has_surface_for_output (manager, wl_output))
	{
		gs_debug ("Lock surface already exists for this output");
		return;
	}

	gdk_wayland_window_set_use_custom_surface (gdk_window);

	wl_surface = gdk_wayland_window_get_wl_surface (gdk_window);
	if (wl_surface == NULL)
	{
		gs_debug ("Failed to get Wayland surface for lock surface");
		return;
	}

	surface = ext_session_lock_v1_get_lock_surface (manager->wl_lock, wl_surface, wl_output);
	if (surface == NULL)
	{
		gs_debug ("Failed to create lock surface");
		return;
	}

	ext_session_lock_surface_v1_add_listener (surface, &surface_listener, window);

	lock_surface = g_new0 (GSLockSurface, 1);
	lock_surface->surface = surface;
	lock_surface->output = wl_output;
	g_hash_table_insert (manager->lock_surfaces, window, lock_surface);

	wl_display_roundtrip (gdk_wayland_display_get_wl_display (gdk_display_get_default ()));

	gs_debug ("Lock surface created for monitor %s",
	          monitor != NULL ? gdk_monitor_get_model (monitor) : "(none)");
}

static void
gs_session_lock_manager_init (GSSessionLockManager *manager)
{
	struct wl_display *wl_display;

	wl_display = gdk_wayland_display_get_wl_display (gdk_display_get_default ());
	if (wl_display == NULL)
	{
		g_warning ("No Wayland display available");
		return;
	}

	manager->wl_registry = wl_display_get_registry (wl_display);
	manager->lock_surfaces = g_hash_table_new_full (g_direct_hash,
	                                                g_direct_equal,
	                                                NULL,
	                                                (GDestroyNotify) g_free);
	wl_registry_add_listener (manager->wl_registry, &registry_listener, manager);
	wl_display_roundtrip (wl_display);

	if (manager->wl_manager == NULL)
	{
		g_warning ("ext-session-lock-v1 protocol unsupported: "
		           "mate-screensaver will not be able to lock the session");
	}
}

static void
gs_session_lock_manager_finalize (GObject *object)
{
	GSSessionLockManager *manager = GS_SESSION_LOCK_MANAGER (object);

	if (manager->wl_lock != NULL)
	{
		gs_session_lock_manager_unlock (manager);
	}

	if (manager->lock_surfaces != NULL)
	{
		g_hash_table_destroy (manager->lock_surfaces);
		manager->lock_surfaces = NULL;
	}

	if (manager->wl_manager != NULL)
	{
		ext_session_lock_manager_v1_destroy (manager->wl_manager);
		manager->wl_manager = NULL;
	}

	if (manager->wl_registry != NULL)
	{
		wl_registry_destroy (manager->wl_registry);
		manager->wl_registry = NULL;
	}

	G_OBJECT_CLASS (gs_session_lock_manager_parent_class)->finalize (object);
}

static void
gs_session_lock_manager_class_init (GSSessionLockManagerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = gs_session_lock_manager_finalize;

	signals[SIGNAL_LOCKED] =
	    g_signal_new ("locked",
	                  G_TYPE_FROM_CLASS (klass),
	                  G_SIGNAL_RUN_LAST,
	                  0, NULL, NULL,
	                  g_cclosure_marshal_VOID__VOID,
	                  G_TYPE_NONE, 0);

	signals[SIGNAL_FINISHED] =
	    g_signal_new ("finished",
	                  G_TYPE_FROM_CLASS (klass),
	                  G_SIGNAL_RUN_LAST,
	                  0, NULL, NULL,
	                  g_cclosure_marshal_VOID__VOID,
	                  G_TYPE_NONE, 0);
}

GSSessionLockManager *
gs_session_lock_manager_new (void)
{
	GSSessionLockManager *manager;

	manager = g_object_new (GS_TYPE_SESSION_LOCK_MANAGER, NULL);

	if (manager->wl_manager == NULL)
	{
		g_clear_object (&manager);
	}

	return manager;
}

gboolean
gs_session_lock_manager_lock (GSSessionLockManager *manager)
{
	g_return_val_if_fail (GS_IS_SESSION_LOCK_MANAGER (manager), FALSE);
	g_return_val_if_fail (manager->wl_lock == NULL, FALSE);

	if (manager->wl_manager == NULL)
	{
		return FALSE;
	}

	manager->finished = FALSE;
	manager->locked = FALSE;

	gs_debug ("Locking session");
	manager->wl_lock = ext_session_lock_manager_v1_lock (manager->wl_manager);
	if (manager->wl_lock == NULL)
	{
		return FALSE;
	}

	ext_session_lock_v1_add_listener (manager->wl_lock, &lock_listener, manager);
	wl_display_roundtrip (gdk_wayland_display_get_wl_display (gdk_display_get_default ()));

	return manager->wl_lock != NULL;
}

void
gs_session_lock_manager_unlock (GSSessionLockManager *manager)
{
	g_return_if_fail (GS_IS_SESSION_LOCK_MANAGER (manager));

	if (manager->wl_lock == NULL)
	{
		return;
	}

	gs_debug ("Unlocking session");

	/* Destroy our surfaces first: once unlock_and_destroy is sent the
	 * compositor destroys them server-side and a later destroy request
	 * would be a fatal error. Only do this if the compositor has not
	 * already finished the lock (in which case the surfaces are gone). */
	if (!manager->finished)
	{
		GHashTableIter iter;
		gpointer       value;

		g_hash_table_iter_init (&iter, manager->lock_surfaces);
		while (g_hash_table_iter_next (&iter, NULL, &value))
		{
			GSLockSurface *lock_surface = value;

			if (lock_surface->surface != NULL)
			{
				ext_session_lock_surface_v1_destroy (lock_surface->surface);
				lock_surface->surface = NULL;
			}
		}
	}

	g_hash_table_remove_all (manager->lock_surfaces);

	if (manager->locked)
	{
		ext_session_lock_v1_unlock_and_destroy (manager->wl_lock);
	}
	else
	{
		ext_session_lock_v1_destroy (manager->wl_lock);
	}

	wl_display_roundtrip (gdk_wayland_display_get_wl_display (gdk_display_get_default ()));

	manager->wl_lock = NULL;
	manager->locked = FALSE;
	manager->finished = FALSE;
}

void
gs_session_lock_manager_add_window (GSSessionLockManager *manager,
                                    GSWindow             *window)
{
	const gchar *model;

	g_return_if_fail (GS_IS_SESSION_LOCK_MANAGER (manager));
	g_return_if_fail (GS_IS_WINDOW (window));

	if (manager->wl_lock == NULL)
	{
		return;
	}

	g_return_if_fail (!gtk_widget_get_realized (GTK_WIDGET (window)));

	model = gdk_monitor_get_model (gs_window_get_monitor (window));
	gs_debug ("Adding window for monitor %s", model != NULL ? model : "(none)");

	g_signal_connect (window, "realize", G_CALLBACK (on_window_realize), manager);
	g_object_set_data_full (G_OBJECT (window), "monitor-model", g_strdup (model), g_free);
}

void
gs_session_lock_manager_remove_window (GSSessionLockManager *manager,
                                       GSWindow             *window)
{
	GSLockSurface *lock_surface;

	g_return_if_fail (GS_IS_SESSION_LOCK_MANAGER (manager));
	g_return_if_fail (GS_IS_WINDOW (window));

	if (manager->wl_lock == NULL)
	{
		return;
	}

	g_signal_handlers_disconnect_by_func (window, on_window_realize, manager);

	gs_debug ("Removing window for monitor %s",
	          g_object_get_data (G_OBJECT (window), "monitor-model"));

	lock_surface = g_hash_table_lookup (manager->lock_surfaces, window);
	if (lock_surface != NULL && lock_surface->surface != NULL && !manager->finished)
	{
		ext_session_lock_surface_v1_destroy (lock_surface->surface);
		lock_surface->surface = NULL;
	}

	/* g_hash_table_remove frees the GSLockSurface (value destroy = g_free). */
	g_hash_table_remove (manager->lock_surfaces, window);
}

#endif /* ENABLE_WAYLAND */
