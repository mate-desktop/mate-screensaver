/*
 * gs-watcher.c
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
 *
 * Authors:
 *     Marcus Johnson <marcusl@littlesvr.ca>
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include <gdk/gdk.h>

#ifdef ENABLE_WAYLAND
#include <gdk/gdkwayland.h>
#include "ext-idle-notify-client.h"
#endif

#ifdef ENABLE_X11
#include <gdk/gdkx.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#endif

#include "gs-watcher.h"
#include "gs-watcher-private.h"
#include "gs-debug.h"

#define GS_WATCHER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_WATCHER, GSWatcherPrivate))

enum
{
    PROP_0,
    PROP_ENABLED,
    PROP_ACTIVE,
    PROP_STATUS_MESSAGE,
};

static void gs_watcher_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec);
static void gs_watcher_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec);
static void gs_watcher_finalize (GObject *object);
static void gs_watcher_activate_monitoring (GSWatcher *watcher,
                                            guint      timeout_ms);
static void gs_watcher_deactivate_monitoring (GSWatcher *watcher);

static gboolean gs_boolean_accumulator (GSignalInvocationHint *ihint,
                                        GValue                *return_accu,
                                        const GValue          *handler_return,
                                        gpointer               dummy);

G_DEFINE_TYPE (GSWatcher, gs_watcher, G_TYPE_OBJECT)

static guint signals[GS_WATCHER_N_SIGNALS] = { 0, };

#ifdef ENABLE_X11

static void remove_watchdog_timer (GSWatcher *watcher);
static gboolean watchdog_timer (GSWatcher *watcher);

static void
remove_idle_id (GSWatcher *watcher)
{
    GSWatcherPrivate *priv = watcher->priv;

    if (priv->idle_id > 0)
    {
        g_source_remove (priv->idle_id);
        priv->idle_id = 0;
    }
}

static void
add_watchdog_timer (GSWatcher *watcher,
                    guint      timeout)
{
    GSWatcherPrivate *priv = watcher->priv;

    priv->watchdog_timer_id = g_timeout_add (timeout,
                                             (GSourceFunc)watchdog_timer,
                                             watcher);
}

static void
remove_watchdog_timer (GSWatcher *watcher)
{
    GSWatcherPrivate *priv = watcher->priv;

    if (priv->watchdog_timer_id != 0)
    {
        g_source_remove (priv->watchdog_timer_id);
        priv->watchdog_timer_id = 0;
    }
}

static void
on_idle_timeout (GSWatcher *watcher)
{
    GSWatcherPrivate *priv = watcher->priv;
    gboolean res;

    res = _gs_watcher_set_session_idle (watcher, TRUE);

    _gs_watcher_set_session_idle_notice (watcher, FALSE);

    /* try again if we failed i guess */
    if (res)
    {
        priv->idle_id = 0;
    }
}

static void
set_status (GSWatcher *watcher,
            guint      status)
{
    GSWatcherPrivate *priv = watcher->priv;
    gboolean is_idle;

    if (! priv->active)
    {
        gs_debug ("GSWatcher: not active, ignoring status changes");
        /* no change in idleness */
        return;
    }

    is_idle = (status == 3);

    if (!is_idle && !priv->idle_notice)
    {
        return;
    }

    if (is_idle)
    {
        _gs_watcher_set_session_idle_notice (watcher, is_idle);
        /* queue an activation */
        if (priv->idle_id > 0)
        {
            g_source_remove (priv->idle_id);
        }
        priv->idle_id = g_timeout_add (priv->delta_notice_timeout,
                                       (GSourceFunc)on_idle_timeout,
                                       watcher);
    }
    /* cancel notice too */
    else
    {
        remove_idle_id (watcher);
        _gs_watcher_set_session_idle (watcher, FALSE);
        _gs_watcher_set_session_idle_notice (watcher, FALSE);
    }
}

static void
on_presence_status_changed (DBusGProxy *presence_proxy,
                            guint       status,
                            GSWatcher  *watcher)
{
    set_status (watcher, status);
}

static void
on_presence_status_text_changed (DBusGProxy *presence_proxy,
                                 const char *status_text,
                                 GSWatcher  *watcher)
{
    g_object_set (watcher, "status-message", status_text, NULL);
}

static gboolean
connect_presence_watcher (GSWatcher *watcher)
{
    GSWatcherPrivate *priv = watcher->priv;
    DBusGConnection  *bus;
    GError           *error;
    gboolean          ret;

    ret = FALSE;

    error = NULL;
    bus = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
    if (bus == NULL)
    {
        g_warning ("Unable to get session bus: %s", error->message);
        g_error_free (error);
        goto done;
    }

    error = NULL;
    priv->presence_proxy = dbus_g_proxy_new_for_name_owner (bus,
                                "org.gnome.SessionManager",
                                "/org/gnome/SessionManager/Presence",
                                "org.gnome.SessionManager.Presence",
                                &error);
    if (priv->presence_proxy != NULL)
    {
        DBusGProxy *proxy;

        dbus_g_proxy_add_signal (priv->presence_proxy,
                                 "StatusChanged",
                                 G_TYPE_UINT,
                                 G_TYPE_INVALID);
        dbus_g_proxy_connect_signal (priv->presence_proxy,
                                     "StatusChanged",
                                     G_CALLBACK (on_presence_status_changed),
                                     watcher,
                                     NULL);
        dbus_g_proxy_add_signal (priv->presence_proxy,
                                 "StatusTextChanged",
                                 G_TYPE_STRING,
                                 G_TYPE_INVALID);
        dbus_g_proxy_connect_signal (priv->presence_proxy,
                                     "StatusTextChanged",
                                     G_CALLBACK (on_presence_status_text_changed),
                                     watcher,
                                     NULL);

        proxy = dbus_g_proxy_new_from_proxy (priv->presence_proxy,
                                             "org.freedesktop.DBus.Properties",
                                             "/org/gnome/SessionManager/Presence");
        if (proxy != NULL)
        {
            guint       status;
            const char *status_text;
            GValue      value = { 0, };

            status = 0;
            status_text = NULL;

            error = NULL;
            dbus_g_proxy_call (proxy,
                               "Get",
                               &error,
                               G_TYPE_STRING, "org.gnome.SessionManager.Presence",
                               G_TYPE_STRING, "status",
                               G_TYPE_INVALID,
                               G_TYPE_VALUE, &value,
                               G_TYPE_INVALID);

            if (error != NULL)
            {
                g_warning ("Couldn't get presence status: %s", error->message);
                g_error_free (error);
                goto done;
            }
            else
            {
                status = g_value_get_uint (&value);
            }

            g_value_unset (&value);

            error = NULL;
            dbus_g_proxy_call (proxy,
                               "Get",
                               &error,
                               G_TYPE_STRING, "org.gnome.SessionManager.Presence",
                               G_TYPE_STRING, "status-text",
                               G_TYPE_INVALID,
                               G_TYPE_VALUE, &value,
                               G_TYPE_INVALID);

            if (error != NULL)
            {
                g_warning ("Couldn't get presence status text: %s", error->message);
                g_error_free (error);
            }
            else
            {
                status_text = g_value_get_string (&value);
            }

            set_status (watcher, status);
            g_object_set (watcher, "status-message", status_text, NULL);
        }
    }
    else
    {
        g_warning ("Failed to get session presence proxy: %s", error->message);
        g_error_free (error);
        goto done;
    }

    ret = TRUE;

done:
    return ret;
}

/* Figuring out what the appropriate XSetScreenSaver() parameters are
   (one wouldn't expect this to be rocket science.)
*/
static void
disable_builtin_screensaver (gboolean unblank_screen)
{
    int current_server_timeout, current_server_interval;
    int current_prefer_blank,   current_allow_exp;
    int desired_server_timeout, desired_server_interval;
    int desired_prefer_blank,   desired_allow_exp;

    XGetScreenSaver (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()),
                     &current_server_timeout,
                     &current_server_interval,
                     &current_prefer_blank,
                     &current_allow_exp);

    desired_server_timeout  = current_server_timeout;
    desired_server_interval = current_server_interval;
    desired_prefer_blank    = current_prefer_blank;
    desired_allow_exp       = current_allow_exp;

    desired_server_interval = 0;
    /* I suspect (but am not sure) that DontAllowExposures might have
       something to do with powering off the monitor as well, at least
       on some systems that don't support XDPMS?  Who know... */
    desired_allow_exp = AllowExposures;
    /* When we're not using an extension, set the server-side timeout to 0,
       so that the server never gets involved with screen blanking, and we
       do it all ourselves.  (However, when we *are* using an extension,
       we tell the server when to notify us, and rather than blanking the
       screen, the server will send us an X event telling us to blank.)
    */
    desired_server_timeout = 0;

    if (desired_server_timeout     != current_server_timeout
            || desired_server_interval != current_server_interval
            || desired_prefer_blank    != current_prefer_blank
            || desired_allow_exp       != current_allow_exp)
    {
        gs_debug ("disabling server builtin screensaver:"
                  " (xset s %d %d; xset s %s; xset s %s)",
                  desired_server_timeout,
                  desired_server_interval,
                  (desired_prefer_blank ? "blank" : "noblank"),
                  (desired_allow_exp ? "expose" : "noexpose"));

        XSetScreenSaver (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()),
                         desired_server_timeout,
                         desired_server_interval,
                         desired_prefer_blank,
                         desired_allow_exp);

        XSync (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), FALSE);
    }

    if (unblank_screen)
    {
        /* Turn off the server builtin saver if it is now running. */
        XForceScreenSaver (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), ScreenSaverReset);
    }
}

/* This timer goes off every few minutes, whether the user is idle or not,
   to try and clean up anything that has gone wrong.

   It calls disable_builtin_screensaver() so that if xset has been used,
   or some other program (like xlock) has messed with the XSetScreenSaver()
   settings, they will be set back to sensible values (if a server extension
   is in use, messing with xlock can cause the screensaver to never get a wakeup
   event, and could cause monitor power-saving to occur, and all manner of
   heinousness.)

 */
static gboolean
watchdog_timer (GSWatcher *watcher)
{
    disable_builtin_screensaver (FALSE);
    return TRUE;
}

#endif /* ENABLE_X11 */

#ifdef ENABLE_WAYLAND

static void
on_activation_idled (void                            *data,
                     struct ext_idle_notification_v1 *notification)
{
    GSWatcher *watcher = GS_WATCHER (data);

    gs_debug ("Wayland: activation idle notification fired");

    _gs_watcher_set_session_idle_notice (watcher, FALSE);
    _gs_watcher_set_session_idle (watcher, TRUE);
}

static void
on_activation_resumed (void                            *data,
                       struct ext_idle_notification_v1 *notification)
{
    GSWatcher *watcher = GS_WATCHER (data);

    gs_debug ("Wayland: activation resumed");

    _gs_watcher_set_session_idle (watcher, FALSE);
    _gs_watcher_set_session_idle_notice (watcher, FALSE);
}

static struct ext_idle_notification_v1_listener activation_listener =
{
    .idled = on_activation_idled,
    .resumed = on_activation_resumed,
};

static void
on_lock_notice_idled (void                            *data,
                      struct ext_idle_notification_v1 *notification)
{
    GSWatcher *watcher = GS_WATCHER (data);

    gs_debug ("Wayland: lock notice idle notification fired");

    _gs_watcher_set_session_idle_notice (watcher, TRUE);
}

static void
on_lock_notice_resumed (void                            *data,
                        struct ext_idle_notification_v1 *notification)
{
    GSWatcher *watcher = GS_WATCHER (data);

    gs_debug ("Wayland: lock notice resumed");

    _gs_watcher_set_session_idle (watcher, FALSE);
    _gs_watcher_set_session_idle_notice (watcher, FALSE);
}

static struct ext_idle_notification_v1_listener lock_notice_listener =
{
    .idled = on_lock_notice_idled,
    .resumed = on_lock_notice_resumed,
};

static struct ext_idle_notifier_v1 *
bind_idle_notifier (struct wl_registry *registry,
                    guint32             name)
{
    struct ext_idle_notifier_v1 *notifier;

    notifier = wl_registry_bind (registry,
                                 name,
                                 &ext_idle_notifier_v1_interface,
                                 1);

    return notifier;
}

static void
remove_idle_notification (GSWatcher                         *watcher,
                          struct ext_idle_notification_v1 **notification)
{
    if (*notification != NULL)
    {
        ext_idle_notification_v1_destroy (*notification);
        *notification = NULL;
    }
}

static void
remove_idle_notifier (GSWatcher *watcher)
{
    GSWatcherPrivate *priv = watcher->priv;

    if (priv->idle_notifier != NULL)
    {
        ext_idle_notifier_v1_destroy (priv->idle_notifier);
        priv->idle_notifier = NULL;
    }
}

static void
remove_registry (GSWatcher *watcher)
{
    GSWatcherPrivate *priv = watcher->priv;

    if (priv->registry != NULL)
    {
        wl_registry_destroy (priv->registry);
        priv->registry = NULL;
    }
}

static void
on_registry_global (void                *data,
                    struct wl_registry *registry,
                    guint32             name,
                    const char         *interface,
                    guint32             version)
{
    GSWatcher        *watcher = GS_WATCHER (data);
    GSWatcherPrivate *priv = watcher->priv;

    if (strcmp (interface, "ext_idle_notifier_v1") == 0)
    {
        priv->idle_notifier = bind_idle_notifier (registry, name);
    }
}

static const struct wl_registry_listener registry_listener =
{
    .global = on_registry_global,
};

static struct ext_idle_notification_v1 *
create_idle_notification (GSWatcher *watcher,
                          guint      timeout_ms,
                          struct ext_idle_notification_v1_listener *listener)
{
    GSWatcherPrivate *priv = watcher->priv;
    GdkDisplay       *gdk_display;
    struct wl_display *display;
    struct wl_seat    *seat;
    struct ext_idle_notification_v1 *notification;

    if (priv->idle_notifier == NULL)
    {
        gs_debug ("Wayland: idle notifier not available");
        return NULL;
    }

    gdk_display = gdk_display_get_default ();
    display = gdk_wayland_display_get_wl_display (gdk_display);
    if (display == NULL)
    {
        gs_debug ("Wayland: could not get Wayland display");
        return NULL;
    }

    seat = gdk_wayland_seat_get_wl_seat (gdk_display_get_default_seat (gdk_display));
    if (seat == NULL)
    {
        gs_debug ("Wayland: could not get Wayland seat");
        return NULL;
    }

    notification = ext_idle_notifier_v1_get_idle_notification (priv->idle_notifier,
                                                               timeout_ms,
                                                               seat);

    if (notification == NULL)
    {
        gs_debug ("Wayland: could not create idle notification for %u ms", timeout_ms);
        return NULL;
    }

    ext_idle_notification_v1_add_listener (notification,
                                           listener,
                                           watcher);

    gs_debug ("Wayland: idle notification created with timeout %u ms", timeout_ms);

    return notification;
}

static void
wayland_activate_monitoring (GSWatcher *watcher,
                             guint      timeout_ms)
{
    GSWatcherPrivate *priv = watcher->priv;
    struct wl_display *display;

    gs_debug ("Wayland: activating idle monitoring");

    priv->timeout_ms = timeout_ms;

    display = gdk_wayland_display_get_wl_display (gdk_display_get_default ());
    if (display == NULL)
    {
        gs_debug ("Wayland: could not get Wayland display for activation");
        return;
    }

    priv->registry = wl_display_get_registry (display);
    if (priv->registry == NULL)
    {
        gs_debug ("Wayland: could not get Wayland registry");
        return;
    }

    wl_registry_add_listener (priv->registry, &registry_listener, watcher);
    wl_display_roundtrip (display);

    remove_idle_notification (watcher, &priv->idle_notification);
    remove_idle_notification (watcher, &priv->lock_notification);

    priv->idle_notification = create_idle_notification (watcher,
                                                        timeout_ms,
                                                        &activation_listener);

    if (priv->lock_delay_ms > 0)
    {
        priv->lock_notification = create_idle_notification (watcher,
                                                            timeout_ms + priv->lock_delay_ms,
                                                            &lock_notice_listener);
    }
}

static void
wayland_deactivate_monitoring (GSWatcher *watcher)
{
    GSWatcherPrivate *priv = watcher->priv;

    gs_debug ("Wayland: deactivating idle monitoring");

    remove_idle_notification (watcher, &priv->idle_notification);
    remove_idle_notification (watcher, &priv->lock_notification);
    remove_idle_notifier (watcher);
    remove_registry (watcher);
}

#endif /* ENABLE_WAYLAND */

static void
gs_watcher_activate_monitoring (GSWatcher *watcher,
                                guint      timeout_ms)
{
#ifdef ENABLE_WAYLAND
    if (GDK_IS_WAYLAND_DISPLAY (gdk_display_get_default ()))
    {
        wayland_activate_monitoring (watcher, timeout_ms);
        return;
    }
#endif
#ifdef ENABLE_X11
    if (GDK_IS_X11_DISPLAY (gdk_display_get_default ()))
    {
        gs_debug ("X11: activating idle monitoring");

        disable_builtin_screensaver (TRUE);
        add_watchdog_timer (watcher, 600000);
        return;
    }
#endif
}

static void
gs_watcher_deactivate_monitoring (GSWatcher *watcher)
{
#ifdef ENABLE_WAYLAND
    if (GDK_IS_WAYLAND_DISPLAY (gdk_display_get_default ()))
    {
        wayland_deactivate_monitoring (watcher);
        return;
    }
#endif
#ifdef ENABLE_X11
    if (GDK_IS_X11_DISPLAY (gdk_display_get_default ()))
    {
        gs_debug ("X11: deactivating idle monitoring");

        remove_idle_id (watcher);
        remove_watchdog_timer (watcher);
        return;
    }
#endif
}

static void
gs_watcher_class_init (GSWatcherClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->set_property = gs_watcher_set_property;
    object_class->get_property = gs_watcher_get_property;
    object_class->finalize = gs_watcher_finalize;

    g_object_class_install_property (object_class,
                                     PROP_ENABLED,
                                     g_param_spec_boolean ("enabled",
                                                           NULL,
                                                           NULL,
                                                           TRUE,
                                                           G_PARAM_READWRITE |
                                                           G_PARAM_STATIC_STRINGS));

    g_object_class_install_property (object_class,
                                     PROP_ACTIVE,
                                     g_param_spec_boolean ("active",
                                                           NULL,
                                                           NULL,
                                                           FALSE,
                                                           G_PARAM_READWRITE |
                                                           G_PARAM_STATIC_STRINGS));

    g_object_class_install_property (object_class,
                                     PROP_STATUS_MESSAGE,
                                     g_param_spec_string ("status-message",
                                                          NULL,
                                                          NULL,
                                                          NULL,
                                                          G_PARAM_READABLE |
                                                          G_PARAM_STATIC_STRINGS));

    signals[GS_WATCHER_SIGNAL_IDLE_CHANGED] =
        g_signal_new ("idle-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      gs_boolean_accumulator, NULL,
                      NULL,
                      G_TYPE_BOOLEAN, 1,
                      G_TYPE_BOOLEAN);

    signals[GS_WATCHER_SIGNAL_IDLE_NOTICE_CHANGED] =
        g_signal_new ("idle-notice-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      gs_boolean_accumulator, NULL,
                      NULL,
                      G_TYPE_BOOLEAN, 1,
                      G_TYPE_BOOLEAN);

    g_type_class_add_private (klass, sizeof (GSWatcherPrivate));
}

static void
gs_watcher_init (GSWatcher *watcher)
{
    GSWatcherPrivate *priv;

    _gs_watcher_init_priv (watcher);

    priv = watcher->priv;

#ifdef ENABLE_X11
    priv->presence_proxy = NULL;
    priv->watchdog_timer_id = 0;
    priv->idle_id = 0;
    priv->delta_notice_timeout = 10000;
#endif

#ifdef ENABLE_WAYLAND
    priv->idle_notification = NULL;
    priv->lock_notification = NULL;
    priv->idle_notifier = NULL;
    priv->registry = NULL;
    priv->timeout_ms = 0;
    priv->lock_delay_ms = 0;
#endif

#ifdef ENABLE_X11
    if (GDK_IS_X11_DISPLAY (gdk_display_get_default ()))
    {
        connect_presence_watcher (watcher);
    }
#endif
}

void
_gs_watcher_init_priv (GSWatcher *watcher)
{
    GSWatcherPrivate *priv;

    g_return_if_fail (GS_IS_WATCHER (watcher));

    priv = GS_WATCHER_GET_PRIVATE (watcher);
    watcher->priv = priv;

    priv->enabled = TRUE;
    priv->active = FALSE;
    priv->idle_notice = FALSE;
    priv->idle = FALSE;
    priv->status_message = NULL;
}

static gboolean
gs_boolean_accumulator (GSignalInvocationHint *ihint,
                        GValue                *return_accu,
                        const GValue          *handler_return,
                        gpointer               dummy)
{
    gboolean continue_emission;
    gboolean signal_return;

    signal_return = g_value_get_boolean (return_accu);
    continue_emission = !g_value_get_boolean (handler_return);

    g_value_set_boolean (return_accu, signal_return || g_value_get_boolean (handler_return));

    return continue_emission;
}

gboolean
_gs_watcher_set_session_idle (GSWatcher *watcher,
                              gboolean   is_idle)
{
    GSWatcherPrivate *priv;
    gboolean          handled;

    g_return_val_if_fail (GS_IS_WATCHER (watcher), FALSE);

    priv = watcher->priv;

    if (priv->idle == is_idle)
        return TRUE;

    priv->idle = is_idle;

    handled = FALSE;
    g_signal_emit (watcher, signals[GS_WATCHER_SIGNAL_IDLE_CHANGED], 0,
                   is_idle, &handled);

    return TRUE;
}

gboolean
_gs_watcher_set_session_idle_notice (GSWatcher *watcher,
                                     gboolean   in_effect)
{
    GSWatcherPrivate *priv;
    gboolean          handled;

    g_return_val_if_fail (GS_IS_WATCHER (watcher), FALSE);

    priv = watcher->priv;

    if (priv->idle_notice == in_effect)
        return TRUE;

    priv->idle_notice = in_effect;

    handled = FALSE;
    g_signal_emit (watcher, signals[GS_WATCHER_SIGNAL_IDLE_NOTICE_CHANGED], 0,
                   in_effect, &handled);

    return TRUE;
}

gboolean
gs_watcher_set_enabled (GSWatcher *watcher,
                        gboolean   enabled)
{
    GSWatcherPrivate *priv;

    g_return_val_if_fail (GS_IS_WATCHER (watcher), FALSE);

    priv = watcher->priv;

    if (priv->enabled == enabled)
        return TRUE;

    priv->enabled = enabled;

    g_object_notify (G_OBJECT (watcher), "enabled");

    return TRUE;
}

gboolean
gs_watcher_get_enabled (GSWatcher *watcher)
{
    g_return_val_if_fail (GS_IS_WATCHER (watcher), FALSE);

    return watcher->priv->enabled;
}

gboolean
gs_watcher_set_active (GSWatcher *watcher,
                       gboolean   active)
{
    GSWatcherPrivate *priv;

    g_return_val_if_fail (GS_IS_WATCHER (watcher), FALSE);

    priv = watcher->priv;

    if (priv->active == active)
        return TRUE;

    priv->active = active;

    if (active)
    {
        gs_watcher_activate_monitoring (watcher, priv->idle_timeout_ms);
    }
    else
    {
        gs_watcher_deactivate_monitoring (watcher);
    }

    g_object_notify (G_OBJECT (watcher), "active");

    return TRUE;
}

gboolean
gs_watcher_get_active (GSWatcher *watcher)
{
    g_return_val_if_fail (GS_IS_WATCHER (watcher), FALSE);

    return watcher->priv->active;
}

void
gs_watcher_set_idle_timeout (GSWatcher *watcher,
                             guint      timeout_ms)
{
    g_return_if_fail (GS_IS_WATCHER (watcher));

    watcher->priv->idle_timeout_ms = timeout_ms;
}

GSWatcher *
gs_watcher_new (void)
{
#ifdef ENABLE_WAYLAND
    if (GDK_IS_WAYLAND_DISPLAY (gdk_display_get_default ()))
    {
        return g_object_new (GS_TYPE_WATCHER, NULL);
    }
#endif

#ifdef ENABLE_X11
    if (GDK_IS_X11_DISPLAY (gdk_display_get_default ()))
    {
        return g_object_new (GS_TYPE_WATCHER, NULL);
    }
#endif

    g_critical ("No idle watcher backend matches the current display; "
                "the screensaver will not run. "
#ifdef ENABLE_WAYLAND
                "ENABLE_WAYLAND is defined, "
#else
                "ENABLE_WAYLAND is not defined, "
#endif
#ifdef ENABLE_X11
                "ENABLE_X11 is defined, "
#else
                "ENABLE_X11 is not defined, "
#endif
                "and GDK reports a \'%s\' display.",
                gdk_display_get_default () != NULL
                ? gdk_display_get_name (gdk_display_get_default ())
                : "(no default display)");

    return NULL;
}

static void
gs_watcher_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
    GSWatcher *watcher = GS_WATCHER (object);

    switch (prop_id)
    {
    case PROP_ENABLED:
        gs_watcher_set_enabled (watcher, g_value_get_boolean (value));
        break;
    case PROP_ACTIVE:
        gs_watcher_set_active (watcher, g_value_get_boolean (value));
        break;
    case PROP_STATUS_MESSAGE:
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
gs_watcher_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
    GSWatcher *watcher = GS_WATCHER (object);
    GSWatcherPrivate *priv = watcher->priv;

    switch (prop_id)
    {
    case PROP_ENABLED:
        g_value_set_boolean (value, priv->enabled);
        break;
    case PROP_ACTIVE:
        g_value_set_boolean (value, priv->active);
        break;
    case PROP_STATUS_MESSAGE:
        g_value_set_string (value, priv->status_message);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
gs_watcher_finalize (GObject *object)
{
    GSWatcher *watcher = GS_WATCHER (object);
    GSWatcherPrivate *priv = watcher->priv;

#ifdef ENABLE_X11
    remove_idle_id (watcher);
    remove_watchdog_timer (watcher);

    if (priv->presence_proxy != NULL)
    {
        g_object_unref (priv->presence_proxy);
        priv->presence_proxy = NULL;
    }
#endif

#ifdef ENABLE_WAYLAND
    remove_idle_notification (watcher, &priv->idle_notification);
    remove_idle_notification (watcher, &priv->lock_notification);
    remove_idle_notifier (watcher);
    remove_registry (watcher);
#endif

    g_free (priv->status_message);
    priv->status_message = NULL;

    G_OBJECT_CLASS (gs_watcher_parent_class)->finalize (object);
}
