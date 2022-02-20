/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2004-2006 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2012-2021 MATE Developers
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
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <sys/utsname.h>

#include <glib/gprintf.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>
#include <gdk/gdkkeysyms.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <gtk/gtkx.h>
#include <gio/gio.h>

#define MATE_DESKTOP_USE_UNSTABLE_API
#include <libmate-desktop/mate-desktop-utils.h>

#ifdef WITH_KBD_LAYOUT_INDICATOR
#include <libmatekbd/matekbd-indicator.h>
#endif

#ifdef WITH_LIBNOTIFY
#include <libnotify/notify.h>
#endif

#include "gs-lock-plug.h"

#include "gs-debug.h"

#define GSETTINGS_SCHEMA "org.mate.screensaver"

#define KEY_LOCK_DIALOG_THEME "lock-dialog-theme"

#define KEY_LOCK_DIALOG_T_FMT "lock-dialog-time-format"
#define KEY_LOCK_DIALOG_D_FMT "lock-dialog-date-format"

#define MDM_FLEXISERVER_COMMAND "mdmflexiserver"
#define MDM_FLEXISERVER_ARGS    "--startnew Standard"

#define GDM_FLEXISERVER_COMMAND "gdmflexiserver"
#define GDM_FLEXISERVER_ARGS    "--startnew Standard"

/* same as SMS ;) */
#define NOTE_BUFFER_MAX_CHARS 160

enum
{
    AUTH_PAGE = 0,
};

#define FACE_ICON_SIZE 48
#define DIALOG_TIMEOUT_MSEC 60000

static void gs_lock_plug_finalize   (GObject         *object);

struct GSLockPlugPrivate
{
	GtkWidget   *vbox;
	GtkWidget   *auth_action_area;

	GtkWidget   *notebook;
	GtkWidget   *auth_face_image;
	GtkWidget   *auth_time_label;
	GtkWidget   *auth_date_label;
	GtkWidget   *auth_realname_label;
	GtkWidget   *auth_username_label;
	GtkWidget   *auth_prompt_label;
	GtkWidget   *auth_prompt_entry;
	GtkWidget   *auth_prompt_box;
	GtkWidget   *auth_capslock_label;
	GtkWidget   *auth_message_label;
	GtkWidget   *status_message_label;

	GtkWidget   *auth_unlock_button;
	GtkWidget   *auth_switch_button;
	GtkWidget   *auth_cancel_button;
	GtkWidget   *auth_logout_button;
	GtkWidget   *auth_note_button;
	GtkWidget   *note_tab;
	GtkWidget   *note_tab_label;
	GtkWidget   *note_text_view;
	GtkWidget   *note_ok_button;
	GtkWidget   *note_cancel_button;

	GtkWidget   *auth_prompt_kbd_layout_indicator;

	gboolean     caps_lock_on;
	gboolean     switch_enabled;
	gboolean     leave_note_enabled;
	gboolean     logout_enabled;
	char        *logout_command;
	char        *status_message;

	guint        timeout;

	guint        datetime_timeout_id;
	guint        cancel_timeout_id;
	guint        auth_check_idle_id;
	guint        response_idle_id;

	GList       *key_events;
};

typedef struct _ResponseData ResponseData;

struct _ResponseData
{
	gint response_id;
};

enum
{
    RESPONSE,
    CLOSE,
    LAST_SIGNAL
};

enum
{
    PROP_0,
    PROP_LOGOUT_ENABLED,
    PROP_LOGOUT_COMMAND,
    PROP_SWITCH_ENABLED,
    PROP_STATUS_MESSAGE
};

static guint lock_plug_signals [LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE_WITH_PRIVATE (GSLockPlug, gs_lock_plug, GTK_TYPE_PLUG)

static void
gs_lock_plug_style_set (GtkWidget *widget,
                        GtkStyle  *previous_style)
{
	GSLockPlug *plug;

	if (GTK_WIDGET_CLASS (gs_lock_plug_parent_class)->style_set)
	{
		GTK_WIDGET_CLASS (gs_lock_plug_parent_class)->style_set (widget, previous_style);
	}

	plug = GS_LOCK_PLUG (widget);

	if (! plug->priv->vbox)
	{
		return;
	}

	gtk_container_set_border_width (GTK_CONTAINER (plug->priv->vbox), 12);
	gtk_box_set_spacing (GTK_BOX (plug->priv->vbox), 12);

	gtk_container_set_border_width (GTK_CONTAINER (plug->priv->auth_action_area), 0);
	gtk_box_set_spacing (GTK_BOX (plug->priv->auth_action_area), 5);
}

static gboolean
process_is_running (const char * name)
{
        int num_processes;
        gchar *command = g_strdup_printf ("pidof %s | wc -l", name);
        FILE *fp = popen(command, "r");

        if (fscanf(fp, "%d", &num_processes) != 1)
                num_processes = 0;

        pclose(fp);
        g_free (command);

        if (num_processes > 0) {
                return TRUE;
        } else {
                return FALSE;
        }
}

static void
do_user_switch (GSLockPlug *plug)
{
	GError  *error;
	gboolean res;
	char    *command;

	if (process_is_running ("mdm"))
	{
		/* MDM */
		command = g_strdup_printf ("%s %s",
								   MDM_FLEXISERVER_COMMAND,
								   MDM_FLEXISERVER_ARGS);

		error = NULL;
		res = mate_gdk_spawn_command_line_on_screen (gdk_screen_get_default (),
												command,
												&error);

		g_free (command);

		if (! res)
		{
			gs_debug ("Unable to start MDM greeter: %s", error->message);
			g_error_free (error);
		}
	}
	else if (process_is_running ("gdm") || process_is_running("gdm3") || process_is_running("gdm-binary"))
	{
		/* GDM */
		command = g_strdup_printf ("%s %s",
								   GDM_FLEXISERVER_COMMAND,
								   GDM_FLEXISERVER_ARGS);

		error = NULL;
		res = mate_gdk_spawn_command_line_on_screen (gdk_screen_get_default (),
												command,
												&error);

		g_free (command);

		if (! res) {
			gs_debug ("Unable to start GDM greeter: %s", error->message);
			g_error_free (error);
		}
	}
	else if (g_getenv ("XDG_SEAT_PATH") != NULL)
	{
		/* LightDM */
		GDBusProxyFlags flags = G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START;
		GDBusProxy *proxy = NULL;

		error = NULL;
		proxy = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM,
											  flags,
											  NULL,
											  "org.freedesktop.DisplayManager",
											  g_getenv ("XDG_SEAT_PATH"),
											  "org.freedesktop.DisplayManager.Seat",
											  NULL,
											  &error);
		if (proxy != NULL) {
			g_dbus_proxy_call_sync (proxy,
									"SwitchToGreeter",
									g_variant_new ("()"),
									G_DBUS_CALL_FLAGS_NONE,
									-1,
									NULL,
									NULL);
			g_object_unref (proxy);
		}
		else {
			gs_debug ("Unable to start LightDM greeter: %s", error->message);
			g_error_free (error);
		}
	}

}

static void
set_status_text (GSLockPlug *plug,
                 const char *text)
{
	if (plug->priv->auth_message_label != NULL)
	{
		gtk_label_set_text (GTK_LABEL (plug->priv->auth_message_label), text);
	}
}

static void
date_time_update (GSLockPlug *plug)
{
	GDateTime *datetime;
	gchar *time;
	gchar *date;
	gchar *tfmt;
	gchar *dfmt;

	gchar *str;

	GSettings *settings;

	settings = g_settings_new (GSETTINGS_SCHEMA);
	tfmt = g_settings_get_string (settings, KEY_LOCK_DIALOG_T_FMT);
	dfmt = g_settings_get_string (settings, KEY_LOCK_DIALOG_D_FMT);
	g_object_unref (settings);

	/* Time/Date formating https://developer.gnome.org/glib/stable/glib-GDateTime.html#g-date-time-format */

	datetime = g_date_time_new_now_local ();
	if (g_strcmp0(tfmt, "locale") == 0)
	{
		// Use locale default format
		time = g_date_time_format (datetime, "%X");
	}
	else
	{
		// Apply user defined format
		time = g_date_time_format (datetime, tfmt);
	}

	if (g_strcmp0(dfmt, "locale") == 0)
	{
		// Use locale default format
		date = g_date_time_format (datetime, _("%A, %B %e"));
	}
	else
	{
		// Apply user defined format
		date = g_date_time_format (datetime, dfmt);
	}

	str = g_strdup_printf ("<span size=\"xx-large\" weight=\"ultrabold\">%s</span>", time);
	gtk_label_set_text (GTK_LABEL (plug->priv->auth_time_label), str);
	gtk_label_set_use_markup (GTK_LABEL (plug->priv->auth_time_label), TRUE);
	g_free (str);

	str = g_strdup_printf ("<span size=\"large\">%s</span>", date);
	gtk_label_set_markup (GTK_LABEL (plug->priv->auth_date_label), str);
	gtk_label_set_use_markup (GTK_LABEL (plug->priv->auth_date_label), TRUE);
	g_free (str);

	g_free (time);
	g_free (date);
	g_free (tfmt);
	g_free (dfmt);
	g_date_time_unref (datetime);
}

void
gs_lock_plug_set_sensitive (GSLockPlug *plug,
                            gboolean    sensitive)
{
	g_return_if_fail (GS_IS_LOCK_PLUG (plug));

	gtk_widget_set_sensitive (plug->priv->auth_prompt_entry, sensitive);
	gtk_widget_set_sensitive (plug->priv->auth_action_area, sensitive);
}

static void
remove_datetime_timeout (GSLockPlug *plug)
{
	if (plug->priv->datetime_timeout_id > 0)
	{
		g_source_remove (plug->priv->datetime_timeout_id);
		plug->priv->datetime_timeout_id = 0;
	}
}

static void
remove_cancel_timeout (GSLockPlug *plug)
{
	if (plug->priv->cancel_timeout_id > 0)
	{
		g_source_remove (plug->priv->cancel_timeout_id);
		plug->priv->cancel_timeout_id = 0;
	}
}

static void
remove_response_idle (GSLockPlug *plug)
{
	if (plug->priv->response_idle_id > 0)
	{
		g_source_remove (plug->priv->response_idle_id);
		plug->priv->response_idle_id = 0;
	}
}

static void
gs_lock_plug_response (GSLockPlug *plug,
                       gint        response_id)
{
	int new_response;

	new_response = response_id;

	g_return_if_fail (GS_IS_LOCK_PLUG (plug));

	/* Act only on response IDs we recognize */
	if (!(response_id == GS_LOCK_PLUG_RESPONSE_OK
	        || response_id == GS_LOCK_PLUG_RESPONSE_CANCEL))
	{
		return;
	}

	remove_cancel_timeout (plug);
	remove_response_idle (plug);

	if (response_id == GS_LOCK_PLUG_RESPONSE_CANCEL)
	{
		gtk_entry_set_text (GTK_ENTRY (plug->priv->auth_prompt_entry), "");
	}

	g_signal_emit (plug,
	               lock_plug_signals [RESPONSE],
	               0,
	               new_response);
}

static gboolean
response_cancel_idle_cb (gpointer user_data)
{
	GSLockPlug *plug = user_data;

	plug->priv->response_idle_id = 0;

	gs_lock_plug_response (plug, GS_LOCK_PLUG_RESPONSE_CANCEL);

	return FALSE;
}

static gboolean
dialog_timed_out (gpointer user_data)
{
	GSLockPlug *plug = user_data;

	gs_lock_plug_set_sensitive (plug, FALSE);
	set_status_text (plug, _("Time has expired."));

	if (plug->priv->response_idle_id != 0)
	{
		g_warning ("Response idle ID already set but shouldn't be");
	}

	remove_response_idle (plug);

	plug->priv->response_idle_id = g_timeout_add (2000,
	                                              response_cancel_idle_cb,
	                                              plug);
	return FALSE;
}

static void
capslock_update (GSLockPlug *plug,
                 gboolean    is_on)
{

	plug->priv->caps_lock_on = is_on;

	if (plug->priv->auth_capslock_label == NULL)
	{
		return;
	}

	if (is_on)
	{
		gtk_label_set_text (GTK_LABEL (plug->priv->auth_capslock_label),
		                    _("You have the Caps Lock key on."));
	}
	else
	{
		gtk_label_set_text (GTK_LABEL (plug->priv->auth_capslock_label),
		                    "");
	}
}

static gboolean
is_capslock_on (void)
{
	GdkKeymap *keymap;
	gboolean res;

	res = FALSE;

	keymap = gdk_keymap_get_for_display (gdk_display_get_default ());
	if (keymap != NULL) {
		res = gdk_keymap_get_caps_lock_state (keymap);
	}

	return res;
}

static void
restart_cancel_timeout (GSLockPlug *plug)
{
	remove_cancel_timeout (plug);

	plug->priv->cancel_timeout_id = g_timeout_add (plug->priv->timeout,
	                                               dialog_timed_out,
	                                               plug);
}

void
gs_lock_plug_get_text (GSLockPlug *plug,
                       char      **text)
{
	const char *typed_text;
	char       *null_text;
	char       *local_text;

	typed_text = gtk_entry_get_text (GTK_ENTRY (plug->priv->auth_prompt_entry));
	local_text = g_locale_from_utf8 (typed_text, strlen (typed_text), NULL, NULL, NULL);

	null_text = g_strnfill (strlen (typed_text) + 1, '\b');
	gtk_entry_set_text (GTK_ENTRY (plug->priv->auth_prompt_entry), null_text);
	gtk_entry_set_text (GTK_ENTRY (plug->priv->auth_prompt_entry), "");
	g_free (null_text);

	if (text != NULL)
	{
		*text = local_text;
	}
	else
	{
		g_free (local_text);
	}
}

typedef struct
{
	GSLockPlug *plug;
	gint response_id;
	GMainLoop *loop;
	gboolean destroyed;
} RunInfo;

static void
shutdown_loop (RunInfo *ri)
{
	if (g_main_loop_is_running (ri->loop))
		g_main_loop_quit (ri->loop);
}

static void
run_unmap_handler (GSLockPlug *plug,
                   gpointer data)
{
	RunInfo *ri = data;

	shutdown_loop (ri);
}

static void
run_response_handler (GSLockPlug *plug,
                      gint response_id,
                      gpointer data)
{
	RunInfo *ri;

	ri = data;

	ri->response_id = response_id;

	shutdown_loop (ri);
}

static gint
run_delete_handler (GSLockPlug *plug,
                    GdkEventAny *event,
                    gpointer data)
{
	RunInfo *ri = data;

	shutdown_loop (ri);

	return TRUE; /* Do not destroy */
}

static void
run_destroy_handler (GSLockPlug *plug,
                     gpointer data)
{
	RunInfo *ri = data;

	/* shutdown_loop will be called by run_unmap_handler */
	ri->destroyed = TRUE;
}

static void
run_keymap_handler (GdkKeymap *keymap,
                    GSLockPlug *plug)
{
	capslock_update (plug, is_capslock_on ());
}

/* adapted from GTK+ gtkdialog.c */
int
gs_lock_plug_run (GSLockPlug *plug)
{
	RunInfo ri = { NULL, GTK_RESPONSE_NONE, NULL, FALSE };
	gboolean was_modal;
	gulong response_handler;
	gulong unmap_handler;
	gulong destroy_handler;
	gulong delete_handler;
	gulong keymap_handler;
	GdkKeymap *keymap;

	g_return_val_if_fail (GS_IS_LOCK_PLUG (plug), -1);

	g_object_ref (plug);

	was_modal = gtk_window_get_modal (GTK_WINDOW (plug));
	if (!was_modal)
	{
		gtk_window_set_modal (GTK_WINDOW (plug), TRUE);
	}

	if (!gtk_widget_get_visible (GTK_WIDGET (plug)))
	{
		gtk_widget_show (GTK_WIDGET (plug));
	}

	keymap = gdk_keymap_get_for_display (gtk_widget_get_display (GTK_WIDGET (plug)));

	keymap_handler =
	    g_signal_connect (keymap,
	                      "state-changed",
	                      G_CALLBACK (run_keymap_handler),
	                      plug);

	response_handler =
	    g_signal_connect (plug,
	                      "response",
	                      G_CALLBACK (run_response_handler),
	                      &ri);

	unmap_handler =
	    g_signal_connect (plug,
	                      "unmap",
	                      G_CALLBACK (run_unmap_handler),
	                      &ri);

	delete_handler =
	    g_signal_connect (plug,
	                      "delete_event",
	                      G_CALLBACK (run_delete_handler),
	                      &ri);

	destroy_handler =
	    g_signal_connect (plug,
	                      "destroy",
	                      G_CALLBACK (run_destroy_handler),
	                      &ri);

	ri.loop = g_main_loop_new (NULL, FALSE);

	g_main_loop_run (ri.loop);

	g_main_loop_unref (ri.loop);

	ri.loop = NULL;

	if (!ri.destroyed)
	{
		if (! was_modal)
		{
			gtk_window_set_modal (GTK_WINDOW (plug), FALSE);
		}

		g_signal_handler_disconnect (plug, response_handler);
		g_signal_handler_disconnect (plug, unmap_handler);
		g_signal_handler_disconnect (plug, delete_handler);
		g_signal_handler_disconnect (plug, destroy_handler);
		g_signal_handler_disconnect (keymap, keymap_handler);
	}

	g_object_unref (plug);

	return ri.response_id;
}

static cairo_surface_t *
surface_from_pixbuf (GdkPixbuf *pixbuf)
{
	cairo_surface_t *surface;
	cairo_t         *cr;

	surface = cairo_image_surface_create (gdk_pixbuf_get_has_alpha (pixbuf) ?
	                                      CAIRO_FORMAT_ARGB32 : CAIRO_FORMAT_RGB24,
	                                      gdk_pixbuf_get_width (pixbuf),
	                                      gdk_pixbuf_get_height (pixbuf));
	cr = cairo_create (surface);
	gdk_cairo_set_source_pixbuf (cr, pixbuf, 0, 0);
	cairo_paint (cr);
	cairo_destroy (cr);

	return surface;
}

static void
rounded_rectangle (cairo_t *cr,
                   gdouble  aspect,
                   gdouble  x,
                   gdouble  y,
                   gdouble  corner_radius,
                   gdouble  width,
                   gdouble  height)
{
	gdouble radius;
	gdouble degrees;

	radius = corner_radius / aspect;
	degrees = G_PI / 180.0;

	cairo_new_sub_path (cr);
	cairo_arc (cr,
	           x + width - radius,
	           y + radius,
	           radius,
	           -90 * degrees,
	           0 * degrees);
	cairo_arc (cr,
	           x + width - radius,
	           y + height - radius,
	           radius,
	           0 * degrees,
	           90 * degrees);
	cairo_arc (cr,
	           x + radius,
	           y + height - radius,
	           radius,
	           90 * degrees,
	           180 * degrees);
	cairo_arc (cr,
	           x + radius,
	           y + radius,
	           radius,
	           180 * degrees,
	           270 * degrees);
	cairo_close_path (cr);
}

/* copied from gnome-screensaver 3.x */

/**
 * go_cairo_convert_data_to_pixbuf:
 * @src: a pointer to pixel data in cairo format
 * @dst: a pointer to pixel data in pixbuf format
 * @width: image width
 * @height: image height
 * @rowstride: data rowstride
 *
 * Converts the pixel data stored in @src in CAIRO_FORMAT_ARGB32 cairo format
 * to GDK_COLORSPACE_RGB pixbuf format and move them
 * to @dst. If @src == @dst, pixel are converted in place.
 **/

static void
go_cairo_convert_data_to_pixbuf (unsigned char *dst,
                                 unsigned char const *src,
                                 int width,
                                 int height,
                                 int rowstride)
{
	int i,j;
	unsigned int t;
	unsigned char a, b, c;

	g_return_if_fail (dst != NULL);

#define MULT(d,c,a,t) G_STMT_START { t = (a)? c * 255 / a: 0; d = t;} G_STMT_END

	if (src == dst || src == NULL) {
		for (i = 0; i < height; i++) {
			for (j = 0; j < width; j++) {
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
				MULT(a, dst[2], dst[3], t);
				MULT(b, dst[1], dst[3], t);
				MULT(c, dst[0], dst[3], t);
				dst[0] = a;
				dst[1] = b;
				dst[2] = c;
#else
				MULT(a, dst[1], dst[0], t);
				MULT(b, dst[2], dst[0], t);
				MULT(c, dst[3], dst[0], t);
				dst[3] = dst[0];
				dst[0] = a;
				dst[1] = b;
				dst[2] = c;
#endif
					dst += 4;
			}
			dst += rowstride - width * 4;
		}
	} else {
		for (i = 0; i < height; i++) {
			for (j = 0; j < width; j++) {
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
				MULT(dst[0], src[2], src[3], t);
				MULT(dst[1], src[1], src[3], t);
				MULT(dst[2], src[0], src[3], t);
				dst[3] = src[3];
#else
				MULT(dst[0], src[1], src[0], t);
				MULT(dst[1], src[2], src[0], t);
				MULT(dst[2], src[3], src[0], t);
				dst[3] = src[0];
#endif
				src += 4;
				dst += 4;
			}
			src += rowstride - width * 4;
			dst += rowstride - width * 4;
		}
	}
#undef MULT
}

static void
cairo_to_pixbuf (guint8    *src_data,
                 GdkPixbuf *dst_pixbuf)
{
	unsigned char *src;
	unsigned char *dst;
	guint          w;
	guint          h;
	guint          rowstride;

	w = gdk_pixbuf_get_width (dst_pixbuf);
	h = gdk_pixbuf_get_height (dst_pixbuf);
	rowstride = gdk_pixbuf_get_rowstride (dst_pixbuf);

	dst = gdk_pixbuf_get_pixels (dst_pixbuf);
	src = src_data;

	go_cairo_convert_data_to_pixbuf (dst, src, w, h, rowstride);
}

static GdkPixbuf *
frame_pixbuf (GdkPixbuf *source)
{
	GdkPixbuf       *dest;
	cairo_t         *cr;
	cairo_surface_t *surface;
	guint            w;
	guint            h;
	guint            rowstride;
	int              frame_width;
	double           radius;
	guint8          *data;

	frame_width = 5;

	w = gdk_pixbuf_get_width (source) + frame_width * 2;
	h = gdk_pixbuf_get_height (source) + frame_width * 2;
	radius = w / 10;

	dest = gdk_pixbuf_new (GDK_COLORSPACE_RGB,
						   TRUE,
						   8,
						   w,
						   h);
	rowstride = gdk_pixbuf_get_rowstride (dest);

	data = g_new0 (guint8, h * rowstride);

	surface = cairo_image_surface_create_for_data (data,
												   CAIRO_FORMAT_ARGB32,
												   w,
												   h,
												   rowstride);
	cr = cairo_create (surface);
	cairo_surface_destroy (surface);

	/* set up image */
	cairo_rectangle (cr, 0, 0, w, h);
	cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 0.0);
	cairo_fill (cr);

	rounded_rectangle (cr,
					   1.0,
					   frame_width + 0.5,
					   frame_width + 0.5,
					   radius,
					   w - frame_width * 2 - 1,
					   h - frame_width * 2 - 1);
	cairo_set_source_rgba (cr, 0.5, 0.5, 0.5, 0.3);
	cairo_fill_preserve (cr);

	surface = surface_from_pixbuf (source);
	cairo_set_source_surface (cr, surface, frame_width, frame_width);
	cairo_fill (cr);
	cairo_surface_destroy (surface);

	cairo_to_pixbuf (data, dest);

	cairo_destroy (cr);
	g_free (data);

	return dest;
}

/* end copied from gdm-user.c */

static void
image_set_from_pixbuf (GtkImage  *image,
                       GdkPixbuf *source)
{
	GdkPixbuf *pixbuf;

	pixbuf = frame_pixbuf (source);
	gtk_image_set_from_pixbuf (image, pixbuf);
	g_object_unref (pixbuf);
}

static gboolean
check_user_file (const gchar *filename,
                 uid_t        user,
                 gssize       max_file_size,
                 gboolean     relax_group,
                 gboolean     relax_other)
{
	struct stat fileinfo;

	if (max_file_size < 0)
	{
		max_file_size = G_MAXSIZE;
	}

	/* Exists/Readable? */
	if (g_stat (filename, &fileinfo) < 0)
	{
		return FALSE;
	}

	/* Is a regular file */
	if (G_UNLIKELY (!S_ISREG (fileinfo.st_mode)))
	{
		return FALSE;
	}

	/* Owned by user? */
	if (G_UNLIKELY (fileinfo.st_uid != user))
	{
		return FALSE;
	}

	/* Group not writable or relax_group? */
	if (G_UNLIKELY ((fileinfo.st_mode & S_IWGRP) == S_IWGRP && !relax_group))
	{
		return FALSE;
	}

	/* Other not writable or relax_other? */
	if (G_UNLIKELY ((fileinfo.st_mode & S_IWOTH) == S_IWOTH && !relax_other))
	{
		return FALSE;
	}

	/* Size is kosher? */
	if (G_UNLIKELY (fileinfo.st_size > max_file_size))
	{
		return FALSE;
	}

	return TRUE;
}

static gboolean
set_face_image (GSLockPlug *plug)
{
	GdkPixbuf    *pixbuf;
	const char   *homedir;
	char         *path;
	int           icon_size = 96;
	gsize         user_max_file = 65536;
	uid_t         uid;

	homedir = g_get_home_dir ();
	uid = getuid ();

	path = g_build_filename (homedir, ".face", NULL);

	pixbuf = NULL;
	if (check_user_file (path, uid, user_max_file, 0, 0))
	{
		pixbuf = gdk_pixbuf_new_from_file_at_size (path,
		         icon_size,
		         icon_size,
		         NULL);
	}

	g_free (path);

	if (pixbuf == NULL)
	{
		return FALSE;
	}

	image_set_from_pixbuf (GTK_IMAGE (plug->priv->auth_face_image), pixbuf);

	g_object_unref (pixbuf);

	return TRUE;
}

#if !GTK_CHECK_VERSION (3, 23, 0)
static void
gs_lock_plug_get_preferred_width (GtkWidget *widget, gint *minimum_width, gint *natural_width)
{
    gint scale;

    GTK_WIDGET_CLASS (gs_lock_plug_parent_class)->get_preferred_width (widget, minimum_width, natural_width);

    scale = gtk_widget_get_scale_factor (widget);
    *minimum_width /= scale;
    *natural_width /= scale;
}

static void
gs_lock_plug_get_preferred_height_for_width (GtkWidget *widget, gint width, gint *minimum_height, gint *natural_height)
{
    gint scale;

    GTK_WIDGET_CLASS (gs_lock_plug_parent_class)->get_preferred_height_for_width (widget, width, minimum_height, natural_height);

    scale = gtk_widget_get_scale_factor (widget);
    *minimum_height /= scale;
    *natural_height /= scale;
}
#endif

static void
gs_lock_plug_show (GtkWidget *widget)
{
	GSLockPlug *plug = GS_LOCK_PLUG (widget);

	gs_profile_start (NULL);

	gs_profile_start ("parent");
	if (GTK_WIDGET_CLASS (gs_lock_plug_parent_class)->show)
	{
		GTK_WIDGET_CLASS (gs_lock_plug_parent_class)->show (widget);
	}

	gs_profile_end ("parent");

	if (plug->priv->auth_face_image)
	{
		set_face_image (plug);
	}

	capslock_update (plug, is_capslock_on ());

	restart_cancel_timeout (plug);

	gs_profile_end (NULL);
}

static void
gs_lock_plug_hide (GtkWidget *widget)
{
	if (GTK_WIDGET_CLASS (gs_lock_plug_parent_class)->hide)
	{
		GTK_WIDGET_CLASS (gs_lock_plug_parent_class)->hide (widget);
	}
}

static void
queue_key_event (GSLockPlug  *plug,
                 GdkEventKey *event)
{
	GdkEvent *saved_event;

	saved_event = gdk_event_copy ((GdkEvent *)event);
	plug->priv->key_events = g_list_prepend (plug->priv->key_events,
	                         saved_event);
}

static void
forward_key_events (GSLockPlug *plug)
{
	plug->priv->key_events = g_list_reverse (plug->priv->key_events);
	while (plug->priv->key_events != NULL)
	{
		GdkEventKey *event = plug->priv->key_events->data;

		gtk_window_propagate_key_event (GTK_WINDOW (plug), event);

		gdk_event_free ((GdkEvent *)event);

		plug->priv->key_events = g_list_delete_link (plug->priv->key_events,
		                         plug->priv->key_events);
	}
}

static void
gs_lock_plug_set_logout_enabled (GSLockPlug *plug,
                                 gboolean    logout_enabled)
{
	g_return_if_fail (GS_LOCK_PLUG (plug));

	if (plug->priv->logout_enabled == logout_enabled)
	{
		return;
	}

	plug->priv->logout_enabled = logout_enabled;
	g_object_notify (G_OBJECT (plug), "logout-enabled");

	if (plug->priv->auth_logout_button == NULL)
	{
		return;
	}

	if (logout_enabled)
	{
		gtk_widget_show (plug->priv->auth_logout_button);
	}
	else
	{
		gtk_widget_hide (plug->priv->auth_logout_button);
	}
}

static void
gs_lock_plug_set_logout_command (GSLockPlug *plug,
                                 const char *command)
{
	g_return_if_fail (GS_LOCK_PLUG (plug));

	g_free (plug->priv->logout_command);

	if (command)
	{
		plug->priv->logout_command = g_strdup (command);
	}
	else
	{
		plug->priv->logout_command = NULL;
	}
}

static void
gs_lock_plug_set_status_message (GSLockPlug *plug,
                                 const char *status_message)
{
	g_return_if_fail (GS_LOCK_PLUG (plug));

	g_free (plug->priv->status_message);
	plug->priv->status_message = g_strdup (status_message);

	if (plug->priv->status_message_label)
	{
		if (plug->priv->status_message)
		{
			gtk_label_set_text (GTK_LABEL (plug->priv->status_message_label),
			                    plug->priv->status_message);
			gtk_widget_show (plug->priv->status_message_label);
		}
		else
		{
			gtk_widget_hide (plug->priv->status_message_label);
		}
	}
}

static void
gs_lock_plug_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
	GSLockPlug *self;

	self = GS_LOCK_PLUG (object);

	switch (prop_id)
	{
	case PROP_LOGOUT_ENABLED:
		g_value_set_boolean (value, self->priv->logout_enabled);
		break;
	case PROP_LOGOUT_COMMAND:
		g_value_set_string (value, self->priv->logout_command);
		break;
	case PROP_SWITCH_ENABLED:
		g_value_set_boolean (value, self->priv->switch_enabled);
		break;
	case PROP_STATUS_MESSAGE:
		g_value_set_string (value, self->priv->status_message);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_lock_plug_set_switch_enabled (GSLockPlug *plug,
                                 gboolean    switch_enabled)
{
	g_return_if_fail (GS_LOCK_PLUG (plug));

	if (plug->priv->switch_enabled == switch_enabled)
	{
		return;
	}

	plug->priv->switch_enabled = switch_enabled;
	g_object_notify (G_OBJECT (plug), "switch-enabled");

	if (plug->priv->auth_switch_button == NULL)
	{
		return;
	}

	if (switch_enabled)
	{
		if (process_is_running ("mdm"))
		{
			/* MDM  */
			gtk_widget_show (plug->priv->auth_switch_button);
		}
		else if (process_is_running ("gdm") || process_is_running("gdm3") || process_is_running("gdm-binary"))
		{
			/* GDM */
			gtk_widget_show (plug->priv->auth_switch_button);
		}
		else if (g_getenv ("XDG_SEAT_PATH") != NULL)
		{
			/* LightDM */
			gtk_widget_show (plug->priv->auth_switch_button);
		}
		else
		{
			gs_debug ("Warning: Unknown DM for switch button");
			gtk_widget_hide (plug->priv->auth_switch_button);
		}
	}
	else
	{
		gtk_widget_hide (plug->priv->auth_switch_button);
	}
}

static void
gs_lock_plug_set_property (GObject            *object,
                           guint               prop_id,
                           const GValue       *value,
                           GParamSpec         *pspec)
{
	GSLockPlug *self;

	self = GS_LOCK_PLUG (object);

	switch (prop_id)
	{
	case PROP_LOGOUT_ENABLED:
		gs_lock_plug_set_logout_enabled (self, g_value_get_boolean (value));
		break;
	case PROP_LOGOUT_COMMAND:
		gs_lock_plug_set_logout_command (self, g_value_get_string (value));
		break;
	case PROP_STATUS_MESSAGE:
		gs_lock_plug_set_status_message (self, g_value_get_string (value));
		break;
	case PROP_SWITCH_ENABLED:
		gs_lock_plug_set_switch_enabled (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_lock_plug_close (GSLockPlug *plug)
{
	/* Synthesize delete_event to close dialog. */

	GtkWidget *widget = GTK_WIDGET (plug);
	GdkEvent  *event;

	event = gdk_event_new (GDK_DELETE);
	event->any.window = g_object_ref (gtk_widget_get_window(widget));
	event->any.send_event = TRUE;

	gtk_main_do_event (event);
	gdk_event_free (event);
}

static void
gs_lock_plug_class_init (GSLockPlugClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
	GtkBindingSet  *binding_set;

	object_class->finalize     = gs_lock_plug_finalize;
	object_class->get_property = gs_lock_plug_get_property;
	object_class->set_property = gs_lock_plug_set_property;

	widget_class->style_set                      = gs_lock_plug_style_set;
	widget_class->show                           = gs_lock_plug_show;
	widget_class->hide                           = gs_lock_plug_hide;
#if !GTK_CHECK_VERSION (3, 23, 0)
	widget_class->get_preferred_width            = gs_lock_plug_get_preferred_width;
	widget_class->get_preferred_height_for_width = gs_lock_plug_get_preferred_height_for_width;
#endif

	klass->close = gs_lock_plug_close;

	lock_plug_signals [RESPONSE] = g_signal_new ("response",
	                               G_OBJECT_CLASS_TYPE (klass),
	                               G_SIGNAL_RUN_LAST,
	                               G_STRUCT_OFFSET (GSLockPlugClass, response),
	                               NULL, NULL,
	                               g_cclosure_marshal_VOID__INT,
	                               G_TYPE_NONE, 1,
	                               G_TYPE_INT);
	lock_plug_signals [CLOSE] = g_signal_new ("close",
	                            G_OBJECT_CLASS_TYPE (klass),
	                            G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
	                            G_STRUCT_OFFSET (GSLockPlugClass, close),
	                            NULL, NULL,
	                            g_cclosure_marshal_VOID__VOID,
	                            G_TYPE_NONE, 0);

	g_object_class_install_property (object_class,
	                                 PROP_LOGOUT_ENABLED,
	                                 g_param_spec_boolean ("logout-enabled",
	                                         NULL,
	                                         NULL,
	                                         FALSE,
	                                         G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
	                                 PROP_LOGOUT_COMMAND,
	                                 g_param_spec_string ("logout-command",
	                                         NULL,
	                                         NULL,
	                                         NULL,
	                                         G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
	                                 PROP_STATUS_MESSAGE,
	                                 g_param_spec_string ("status-message",
	                                         NULL,
	                                         NULL,
	                                         NULL,
	                                         G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
	                                 PROP_SWITCH_ENABLED,
	                                 g_param_spec_boolean ("switch-enabled",
	                                         NULL,
	                                         NULL,
	                                         FALSE,
	                                         G_PARAM_READWRITE));

	binding_set = gtk_binding_set_by_class (klass);

	gtk_binding_entry_add_signal (binding_set, GDK_KEY_Escape, 0,
	                              "close", 0);
}

static void
clear_clipboards (GSLockPlug *plug)
{
	GtkClipboard *clipboard;

	clipboard = gtk_widget_get_clipboard (GTK_WIDGET (plug), GDK_SELECTION_PRIMARY);
	gtk_clipboard_clear (clipboard);
	gtk_clipboard_set_text (clipboard, "", -1);
	clipboard = gtk_widget_get_clipboard (GTK_WIDGET (plug), GDK_SELECTION_CLIPBOARD);
	gtk_clipboard_clear (clipboard);
	gtk_clipboard_set_text (clipboard, "", -1);
}

static void
take_note (GtkButton  *button,
           GSLockPlug *plug)
{
	int page;

	page = gtk_notebook_page_num (GTK_NOTEBOOK (plug->priv->notebook), plug->priv->note_tab);
	gtk_notebook_set_current_page (GTK_NOTEBOOK (plug->priv->notebook), page);
	/* this counts as activity so restart the timer */
	restart_cancel_timeout (plug);
}

static void
submit_note (GtkButton  *button,
             GSLockPlug *plug)
{
#ifdef WITH_LIBNOTIFY
	char               *text;
	char                summary[128];
	char               *escaped_text;
	GtkTextBuffer      *buffer;
	GtkTextIter         start, end;
	time_t              t;
	struct tm          *tmp;
	NotifyNotification *note;

	gtk_notebook_set_current_page (GTK_NOTEBOOK (plug->priv->notebook), AUTH_PAGE);
	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (plug->priv->note_text_view));
	gtk_text_buffer_get_bounds (buffer, &start, &end);
	text = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
	gtk_text_buffer_set_text (buffer, "", 0);
	escaped_text = g_markup_escape_text (text, -1);

	t = time (NULL);
	tmp = localtime (&t);
	strftime (summary, 128, "%X", tmp);

	note = notify_notification_new (summary, escaped_text, NULL);
	notify_notification_set_timeout (note, NOTIFY_EXPIRES_NEVER);
	notify_notification_show (note, NULL);
	g_object_unref (note);

	g_free (text);
	g_free (escaped_text);

	gs_lock_plug_response (plug, GS_LOCK_PLUG_RESPONSE_CANCEL);
#endif /* WITH_LIBNOTIFY */
}

static void
cancel_note (GtkButton  *button,
             GSLockPlug *plug)
{
	GtkTextBuffer *buffer;

	gtk_notebook_set_current_page (GTK_NOTEBOOK (plug->priv->notebook), AUTH_PAGE);
	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (plug->priv->note_text_view));
	gtk_text_buffer_set_text (buffer, "", 0);

	/* this counts as activity so restart the timer */
	restart_cancel_timeout (plug);

	gtk_window_set_default (GTK_WINDOW (plug), plug->priv->auth_unlock_button);

	clear_clipboards (plug);
}

static void
logout_button_clicked (GtkButton  *button,
                       GSLockPlug *plug)
{
	char   **argv  = NULL;
	GError  *error = NULL;
	gboolean res;

	if (! plug->priv->logout_command)
	{
		return;
	}

	res = g_shell_parse_argv (plug->priv->logout_command, NULL, &argv, &error);

	if (! res)
	{
		g_warning ("Could not parse logout command: %s", error->message);
		g_error_free (error);
		return;
	}

	g_spawn_async (g_get_home_dir (),
	               argv,
	               NULL,
	               G_SPAWN_SEARCH_PATH,
	               NULL,
	               NULL,
	               NULL,
	               &error);

	g_strfreev (argv);

	if (error)
	{
		g_warning ("Could not run logout command: %s", error->message);
		g_error_free (error);
	}
}

void
gs_lock_plug_set_busy (GSLockPlug *plug)
{
	GdkDisplay *display;
	GdkCursor *cursor;
	GtkWidget *top_level;

	top_level = gtk_widget_get_toplevel (GTK_WIDGET (plug));

	display = gtk_widget_get_display (GTK_WIDGET (plug));
	cursor = gdk_cursor_new_for_display (display, GDK_WATCH);

	gdk_window_set_cursor (gtk_widget_get_window (top_level), cursor);
	g_object_unref (cursor);
}

void
gs_lock_plug_set_ready (GSLockPlug *plug)
{
	GdkDisplay *display;
	GdkCursor *cursor;
	GtkWidget *top_level;

	top_level = gtk_widget_get_toplevel (GTK_WIDGET (plug));

	display = gtk_widget_get_display (GTK_WIDGET (plug));
	cursor = gdk_cursor_new_for_display (display, GDK_LEFT_PTR);
	gdk_window_set_cursor (gtk_widget_get_window (top_level), cursor);
	g_object_unref (cursor);
}

void
gs_lock_plug_enable_prompt (GSLockPlug *plug,
                            const char *message,
                            gboolean    visible)
{
	g_return_if_fail (GS_IS_LOCK_PLUG (plug));

	gs_debug ("Setting prompt to: %s", message);

	gtk_widget_set_sensitive (plug->priv->auth_unlock_button, TRUE);
	gtk_widget_show (plug->priv->auth_unlock_button);
	gtk_widget_grab_default (plug->priv->auth_unlock_button);
	gtk_label_set_text (GTK_LABEL (plug->priv->auth_prompt_label), message);
	gtk_widget_show (plug->priv->auth_prompt_label);
	gtk_entry_set_visibility (GTK_ENTRY (plug->priv->auth_prompt_entry), visible);
	gtk_widget_set_sensitive (plug->priv->auth_prompt_entry, TRUE);
	gtk_widget_show (plug->priv->auth_prompt_entry);

	if (! gtk_widget_has_focus (plug->priv->auth_prompt_entry))
	{
		gtk_widget_grab_focus (plug->priv->auth_prompt_entry);
	}

	/* were there any key events sent to the plug while the
	 * entry wasnt ready? If so, forward them along
	 */
	forward_key_events (plug);

	restart_cancel_timeout (plug);
}

void
gs_lock_plug_disable_prompt (GSLockPlug *plug)
{
	g_return_if_fail (GS_IS_LOCK_PLUG (plug));

	/* gtk_widget_hide (plug->priv->auth_prompt_entry); */
	/* gtk_widget_hide (plug->priv->auth_prompt_label); */
	gtk_widget_set_sensitive (plug->priv->auth_unlock_button, FALSE);
	gtk_widget_set_sensitive (plug->priv->auth_prompt_entry, FALSE);
	/* gtk_widget_hide (plug->priv->auth_unlock_button); */

	gtk_widget_grab_default (plug->priv->auth_cancel_button);
}

void
gs_lock_plug_show_message (GSLockPlug *plug,
                           const char *message)
{
	g_return_if_fail (GS_IS_LOCK_PLUG (plug));

	set_status_text (plug, message ? message : "");
}

/* button press handler used to inhibit popup menu */
static gint
entry_button_press (GtkWidget      *widget,
                    GdkEventButton *event)
{
	if (event->button == 3 && event->type == GDK_BUTTON_PRESS)
	{
		return TRUE;
	}

	return FALSE;
}

static gint
entry_key_press (GtkWidget   *widget,
                 GdkEventKey *event,
                 GSLockPlug  *plug)
{
	restart_cancel_timeout (plug);

	/* if the input widget is visible and ready for input
	 * then just carry on as usual
	 */
	if (gtk_widget_get_visible (plug->priv->auth_prompt_entry) &&
	        gtk_widget_is_sensitive (plug->priv->auth_prompt_entry))
	{
		return FALSE;
	}

	if (strcmp (event->string, "") == 0)
	{
		return FALSE;
	}

	queue_key_event (plug, event);

	return TRUE;
}

/* adapted from gtk_dialog_add_button */
static GtkWidget *
gs_lock_plug_add_button (GSLockPlug  *plug,
                         GtkWidget   *action_area,
                         const gchar *button_text)
{
	GtkWidget *button;

	g_return_val_if_fail (GS_IS_LOCK_PLUG (plug), NULL);
	g_return_val_if_fail (button_text != NULL, NULL);

	button = GTK_WIDGET (g_object_new (GTK_TYPE_BUTTON,
	                                   "label", button_text,
	                                   "use-stock", TRUE,
	                                   "use-underline", TRUE,
	                                   NULL));

	gtk_widget_set_can_default (button, TRUE);

	gtk_widget_show (button);

	gtk_box_pack_end (GTK_BOX (action_area),
	                  button,
	                  FALSE, TRUE, 0);

	return button;
}

static char *
get_user_display_name (void)
{
	const char *name;
	char       *utf8_name;

	name = g_get_real_name ();

	if (name == NULL || g_strcmp0 (name, "") == 0 ||
	    g_strcmp0 (name, "Unknown") == 0)
	{
		name = g_get_user_name ();
	}

	utf8_name = NULL;

	if (name != NULL)
	{
		utf8_name = g_locale_to_utf8 (name, -1, NULL, NULL, NULL);
	}

	return utf8_name;
}

static char *
get_user_name (void)
{
	const char *name;
	char       *utf8_name;

	name = g_get_user_name ();

	utf8_name = NULL;
	if (name != NULL)
	{
		utf8_name = g_locale_to_utf8 (name, -1, NULL, NULL, NULL);
	}

	return utf8_name;
}

static void
create_page_one_buttons (GSLockPlug *plug)
{

	gs_profile_start ("page one buttons");

	plug->priv->auth_switch_button =  gs_lock_plug_add_button (GS_LOCK_PLUG (plug),
	                                  plug->priv->auth_action_area,
	                                  _("S_witch User..."));
	gtk_button_box_set_child_secondary (GTK_BUTTON_BOX (plug->priv->auth_action_area),
	                                    plug->priv->auth_switch_button,
	                                    TRUE);
	gtk_widget_set_focus_on_click (GTK_WIDGET (plug->priv->auth_switch_button), FALSE);
	gtk_widget_set_no_show_all (plug->priv->auth_switch_button, TRUE);

	plug->priv->auth_logout_button =  gs_lock_plug_add_button (GS_LOCK_PLUG (plug),
	                                  plug->priv->auth_action_area,
	                                  _("Log _Out"));
	gtk_widget_set_focus_on_click (GTK_WIDGET (plug->priv->auth_logout_button), FALSE);
	gtk_widget_set_no_show_all (plug->priv->auth_logout_button, TRUE);

	plug->priv->auth_cancel_button =  gs_lock_plug_add_button (GS_LOCK_PLUG (plug),
	                                  plug->priv->auth_action_area,
	                                  "gtk-cancel");
	gtk_widget_set_focus_on_click (GTK_WIDGET (plug->priv->auth_cancel_button), FALSE);

	plug->priv->auth_unlock_button =  gs_lock_plug_add_button (GS_LOCK_PLUG (plug),
	                                  plug->priv->auth_action_area,
	                                  _("_Unlock"));
	gtk_widget_set_focus_on_click (GTK_WIDGET (plug->priv->auth_unlock_button), FALSE);

	gtk_window_set_default (GTK_WINDOW (plug), plug->priv->auth_unlock_button);

	gs_profile_end ("page one buttons");
}

/* adapted from MDM */
static char *
expand_string (const char *text)
{
	GString       *str;
	const char    *p;
	char          *username;
	glong          i;
	glong          n_chars;
	struct utsname name;

	str = g_string_sized_new (strlen (text));

	p = text;
	n_chars = g_utf8_strlen (text, -1);
	i = 0;

	while (i < n_chars)
	{
		gunichar ch;

		ch = g_utf8_get_char (p);

		/* Backslash commands */
		if (ch == '\\')
		{
			p = g_utf8_next_char (p);
			i++;
			ch = g_utf8_get_char (p);

			if (i >= n_chars || ch == '\0')
			{
				g_warning ("Unescaped \\ at end of text\n");
				goto bail;
			}
			else if (ch == 'n')
			{
				g_string_append_unichar (str, '\n');
			}
			else
			{
				g_string_append_unichar (str, ch);
			}
		}
		else if (ch == '%')
		{
			p = g_utf8_next_char (p);
			i++;
			ch = g_utf8_get_char (p);

			if (i >= n_chars || ch == '\0')
			{
				g_warning ("Unescaped %% at end of text\n");
				goto bail;
			}

			switch (ch)
			{
			case '%':
				g_string_append (str, "%");
				break;
			case 'c':
				/* clock */
				break;
			case 'd':
				/* display */
				g_string_append (str, g_getenv ("DISPLAY"));
				break;
			case 'h':
				/* hostname */
				g_string_append (str, g_get_host_name ());
				break;
			case 'm':
				/* machine name */
				uname (&name);
				g_string_append (str, name.machine);
				break;
			case 'n':
				/* nodename */
				uname (&name);
				g_string_append (str, name.nodename);
				break;
			case 'r':
				/* release */
				uname (&name);
				g_string_append (str, name.release);
				break;
			case 'R':
				/* Real name */
				username = get_user_display_name ();
				g_string_append (str, username);
				g_free (username);
				break;
			case 's':
				/* system name */
				uname (&name);
				g_string_append (str, name.sysname);
				break;
			case 'U':
				/* Username */
				username = get_user_name ();
				g_string_append (str, username);
				g_free (username);
				break;
			default:
				if (ch < 127)
				{
					g_warning ("unknown escape code %%%c in text\n", (char)ch);
				}
				else
				{
					g_warning ("unknown escape code %%(U%x) in text\n", (int)ch);
				}
			}
		}
		else
		{
			g_string_append_unichar (str, ch);
		}
		p = g_utf8_next_char (p);
		i++;
	}

bail:

	return g_string_free (str, FALSE);
}

static void
expand_string_for_label (GtkWidget *label)
{
	const char *template;
	char       *str;

	template = gtk_label_get_label (GTK_LABEL (label));
	str = expand_string (template);
	gtk_label_set_label (GTK_LABEL (label), str);
	g_free (str);
}

static void
create_page_one (GSLockPlug *plug)
{
	GtkWidget            *vbox;
	GtkWidget            *vbox2;
	GtkWidget            *hbox;
	char                 *str;

	gs_profile_start ("page one");

	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
	gtk_widget_set_halign (GTK_WIDGET (vbox),
	                       GTK_ALIGN_CENTER);
	gtk_widget_set_valign (GTK_WIDGET (vbox),
	                       GTK_ALIGN_CENTER);
	gtk_notebook_append_page (GTK_NOTEBOOK (plug->priv->notebook), vbox, NULL);

	vbox2 = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	gtk_box_pack_start (GTK_BOX (vbox), vbox2, FALSE, FALSE, 0);

	str = g_strdup ("<span size=\"xx-large\" weight=\"ultrabold\">%s</span>");
	plug->priv->auth_time_label = gtk_label_new (str);
	g_free (str);
	gtk_label_set_xalign (GTK_LABEL (plug->priv->auth_time_label), 0.5);
	gtk_label_set_yalign (GTK_LABEL (plug->priv->auth_time_label), 0.5);
	gtk_label_set_use_markup (GTK_LABEL (plug->priv->auth_time_label), TRUE);
	gtk_box_pack_start (GTK_BOX (vbox2), plug->priv->auth_time_label, FALSE, FALSE, 0);

	str = g_strdup ("<span size=\"large\">%s</span>");
	plug->priv->auth_date_label = gtk_label_new (str);
	g_free (str);
	gtk_label_set_xalign (GTK_LABEL (plug->priv->auth_date_label), 0.5);
	gtk_label_set_yalign (GTK_LABEL (plug->priv->auth_date_label), 0.5);
	gtk_label_set_use_markup (GTK_LABEL (plug->priv->auth_date_label), TRUE);
	gtk_box_pack_start (GTK_BOX (vbox2), plug->priv->auth_date_label, FALSE, FALSE, 0);

	plug->priv->auth_face_image = gtk_image_new ();
	gtk_box_pack_start (GTK_BOX (vbox), plug->priv->auth_face_image, TRUE, TRUE, 0);
	gtk_widget_set_halign (plug->priv->auth_face_image, GTK_ALIGN_CENTER);
	gtk_widget_set_valign (plug->priv->auth_face_image, GTK_ALIGN_END);

	vbox2 = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	gtk_box_pack_start (GTK_BOX (vbox), vbox2, FALSE, FALSE, 0);

	str = g_strdup ("<span size=\"x-large\">%R</span>");
	plug->priv->auth_realname_label = gtk_label_new (str);
	g_free (str);
	expand_string_for_label (plug->priv->auth_realname_label);
	gtk_label_set_xalign (GTK_LABEL (plug->priv->auth_realname_label), 0.5);
	gtk_label_set_yalign (GTK_LABEL (plug->priv->auth_realname_label), 0.5);
	gtk_label_set_use_markup (GTK_LABEL (plug->priv->auth_realname_label), TRUE);
	gtk_box_pack_start (GTK_BOX (vbox2), plug->priv->auth_realname_label, FALSE, FALSE, 0);

	/* To translators: This expands to USERNAME on HOSTNAME */
	str = g_strdup_printf ("<span size=\"small\">%s</span>", _("%U on %h"));
	plug->priv->auth_username_label = gtk_label_new (str);
	g_free (str);
	expand_string_for_label (plug->priv->auth_username_label);
	gtk_label_set_xalign (GTK_LABEL (plug->priv->auth_realname_label), 0.5);
	gtk_label_set_yalign (GTK_LABEL (plug->priv->auth_realname_label), 0.5);
	gtk_label_set_use_markup (GTK_LABEL (plug->priv->auth_username_label), TRUE);
	gtk_box_pack_start (GTK_BOX (vbox2), plug->priv->auth_username_label, FALSE, FALSE, 0);

	vbox2 = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	gtk_box_pack_start (GTK_BOX (vbox), vbox2, TRUE, TRUE, 0);

	hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_box_pack_start (GTK_BOX (vbox2), hbox, FALSE, FALSE, 0);

	plug->priv->auth_prompt_label = gtk_label_new_with_mnemonic (_("_Password:"));
	gtk_label_set_xalign (GTK_LABEL (plug->priv->auth_prompt_label), 0.0);
	gtk_label_set_yalign (GTK_LABEL (plug->priv->auth_prompt_label), 0.5);
	gtk_box_pack_start (GTK_BOX (hbox), plug->priv->auth_prompt_label, FALSE, FALSE, 0);

	plug->priv->auth_prompt_entry = gtk_entry_new ();
	gtk_box_pack_start (GTK_BOX (hbox), plug->priv->auth_prompt_entry, TRUE, TRUE, 0);

	gtk_label_set_mnemonic_widget (GTK_LABEL (plug->priv->auth_prompt_label),
	                               plug->priv->auth_prompt_entry);

	plug->priv->auth_capslock_label = gtk_label_new ("");
	gtk_label_set_xalign (GTK_LABEL (plug->priv->auth_capslock_label), 0.5);
	gtk_label_set_yalign (GTK_LABEL (plug->priv->auth_capslock_label), 0.5);
	gtk_box_pack_start (GTK_BOX (vbox2), plug->priv->auth_capslock_label, FALSE, FALSE, 0);

	/* Status text */

	plug->priv->auth_message_label = gtk_label_new (NULL);
	gtk_box_pack_start (GTK_BOX (vbox), plug->priv->auth_message_label,
	                    FALSE, FALSE, 0);
	/* Buttons */
	plug->priv->auth_action_area = gtk_button_box_new (GTK_ORIENTATION_HORIZONTAL);

	gtk_button_box_set_layout (GTK_BUTTON_BOX (plug->priv->auth_action_area),
	                           GTK_BUTTONBOX_END);

	gtk_box_pack_end (GTK_BOX (vbox), plug->priv->auth_action_area,
	                  FALSE, TRUE, 0);
	gtk_widget_show (plug->priv->auth_action_area);

	create_page_one_buttons (plug);

	gs_profile_end ("page one");
}

static void
unlock_button_clicked (GtkButton  *button,
                       GSLockPlug *plug)
{
	gs_lock_plug_response (plug, GS_LOCK_PLUG_RESPONSE_OK);
}

static void
cancel_button_clicked (GtkButton  *button,
                       GSLockPlug *plug)
{
	gs_lock_plug_response (plug, GS_LOCK_PLUG_RESPONSE_CANCEL);
}

static void
switch_user_button_clicked (GtkButton  *button,
                            GSLockPlug *plug)
{

	remove_response_idle (plug);

	gs_lock_plug_set_sensitive (plug, FALSE);

	plug->priv->response_idle_id = g_timeout_add (2000,
	                                              response_cancel_idle_cb,
	                                              plug);

	gs_lock_plug_set_busy (plug);
	do_user_switch (plug);
}

static char *
get_dialog_theme_name (GSLockPlug *plug)
{
	char        *name;
	GSettings *settings;

	settings = g_settings_new (GSETTINGS_SCHEMA);
	name = g_settings_get_string (settings, KEY_LOCK_DIALOG_THEME);
	g_object_unref (settings);

	return name;
}

static gboolean
load_theme (GSLockPlug *plug)
{
	char       *theme;
	char       *filename;
	char       *gtkbuilder;
	char       *css;
	GtkBuilder *builder;
	GtkWidget  *lock_dialog;
	GError     *error=NULL;

	theme = get_dialog_theme_name (plug);
	if (theme == NULL)
	{
		return FALSE;
	}

	filename = g_strdup_printf ("lock-dialog-%s.ui", theme);
	gtkbuilder = g_build_filename (GTKBUILDERDIR, filename, NULL);
	g_free (filename);
	if (! g_file_test (gtkbuilder, G_FILE_TEST_IS_REGULAR))
	{
		g_free (gtkbuilder);
		g_free (theme);
		return FALSE;
	}

	filename = g_strdup_printf ("lock-dialog-%s.css", theme);
	g_free (theme);
	css = g_build_filename (GTKBUILDERDIR, filename, NULL);
	g_free (filename);
	if (g_file_test (css, G_FILE_TEST_IS_REGULAR))
	{
		static GtkCssProvider *style_provider = NULL;

		if (style_provider == NULL)
		{
			style_provider = gtk_css_provider_new ();

			gtk_style_context_add_provider_for_screen (gdk_screen_get_default(),
			                                           GTK_STYLE_PROVIDER (style_provider),
			                                           GTK_STYLE_PROVIDER_PRIORITY_USER);
		}

		gtk_css_provider_load_from_path (style_provider, css, NULL);
	}
	g_free (css);

	builder = gtk_builder_new();
	if (!gtk_builder_add_from_file (builder,gtkbuilder,&error))
	{
		g_warning ("Couldn't load builder file '%s': %s", gtkbuilder, error->message);
		g_error_free(error);
		g_free (gtkbuilder);
		return FALSE;
	}
	g_free (gtkbuilder);

	lock_dialog = GTK_WIDGET (gtk_builder_get_object(builder, "lock-dialog"));
	gtk_container_add (GTK_CONTAINER (plug), lock_dialog);

	plug->priv->vbox = NULL;
	plug->priv->notebook = GTK_WIDGET (gtk_builder_get_object(builder, "notebook"));

	plug->priv->auth_face_image = GTK_WIDGET (gtk_builder_get_object(builder, "auth-face-image"));
	plug->priv->auth_action_area = GTK_WIDGET (gtk_builder_get_object(builder, "auth-action-area"));
	plug->priv->auth_time_label = GTK_WIDGET (gtk_builder_get_object(builder, "auth-time-label"));
	plug->priv->auth_date_label = GTK_WIDGET (gtk_builder_get_object(builder, "auth-date-label"));
	plug->priv->auth_realname_label = GTK_WIDGET (gtk_builder_get_object(builder, "auth-realname-label"));
	plug->priv->auth_username_label = GTK_WIDGET (gtk_builder_get_object(builder, "auth-username-label"));
	plug->priv->auth_prompt_label = GTK_WIDGET (gtk_builder_get_object(builder, "auth-prompt-label"));
	plug->priv->auth_prompt_entry = GTK_WIDGET (gtk_builder_get_object(builder, "auth-prompt-entry"));
	plug->priv->auth_prompt_box = GTK_WIDGET (gtk_builder_get_object(builder, "auth-prompt-box"));
	plug->priv->auth_capslock_label = GTK_WIDGET (gtk_builder_get_object(builder, "auth-capslock-label"));
	plug->priv->auth_message_label = GTK_WIDGET (gtk_builder_get_object(builder, "auth-status-label"));
	plug->priv->auth_unlock_button = GTK_WIDGET (gtk_builder_get_object(builder, "auth-unlock-button"));
	plug->priv->auth_cancel_button = GTK_WIDGET (gtk_builder_get_object(builder, "auth-cancel-button"));
	plug->priv->auth_logout_button = GTK_WIDGET (gtk_builder_get_object(builder, "auth-logout-button"));
	plug->priv->auth_switch_button = GTK_WIDGET (gtk_builder_get_object(builder, "auth-switch-button"));
	plug->priv->auth_note_button = GTK_WIDGET (gtk_builder_get_object(builder, "auth-note-button"));
	plug->priv->note_tab = GTK_WIDGET (gtk_builder_get_object(builder, "note-tab"));
	plug->priv->note_tab_label = GTK_WIDGET (gtk_builder_get_object(builder, "note-tab-label"));
	plug->priv->note_ok_button = GTK_WIDGET (gtk_builder_get_object(builder, "note-ok-button"));
	plug->priv->note_text_view = GTK_WIDGET (gtk_builder_get_object(builder, "note-text-view"));
	plug->priv->note_cancel_button = GTK_WIDGET (gtk_builder_get_object(builder, "note-cancel-button"));

	/* Placeholder for the keyboard indicator */
	plug->priv->auth_prompt_kbd_layout_indicator = GTK_WIDGET (gtk_builder_get_object(builder, "auth-prompt-kbd-layout-indicator"));
	if (plug->priv->auth_logout_button != NULL)
	{
		gtk_widget_set_no_show_all (plug->priv->auth_logout_button, TRUE);
	}
	if (plug->priv->auth_switch_button != NULL)
	{
		gtk_widget_set_no_show_all (plug->priv->auth_switch_button, TRUE);
	}
	if (plug->priv->auth_note_button != NULL)
	{
		gtk_widget_set_no_show_all (plug->priv->auth_note_button, TRUE);
	}

	date_time_update (plug);
	gtk_widget_show_all (lock_dialog);

	plug->priv->status_message_label = GTK_WIDGET (gtk_builder_get_object(builder, "status-message-label"));

	return TRUE;
}

static int
delete_handler (GSLockPlug  *plug,
                GdkEventAny *event,
                gpointer     data)
{
	gs_lock_plug_response (plug, GS_LOCK_PLUG_RESPONSE_CANCEL);

	return TRUE; /* Do not destroy */
}

static void
on_note_text_buffer_changed (GtkTextBuffer *buffer,
                             GSLockPlug    *plug)
{
	int len;

	len = gtk_text_buffer_get_char_count (buffer);
	gtk_widget_set_sensitive (plug->priv->note_ok_button, len <= NOTE_BUFFER_MAX_CHARS);
}

static void
entry_icon_clicked (GtkEntry       *entry,
                    gint            position,
                    GdkEventButton *event,
                    gpointer        data)
{
	(void)data;

	if ((position == GTK_ENTRY_ICON_SECONDARY) && (event->type == GDK_DOUBLE_BUTTON_PRESS))
	{
		if (g_strcmp0 (gtk_entry_get_icon_name (entry, position), "emblem-readonly"))
		{
			gtk_entry_set_icon_from_icon_name (entry, position, "emblem-readonly");
			gtk_entry_set_icon_tooltip_text (entry, position, _("Show password"));
			gtk_entry_set_visibility (entry, FALSE);
		}
		else
		{
			gtk_entry_set_icon_from_icon_name (entry, position, "emblem-unreadable");
			gtk_entry_set_icon_tooltip_text (entry, position, _("Hide password"));
			gtk_entry_set_visibility (entry, TRUE);
		}
	}
}
static void
gs_lock_plug_init (GSLockPlug *plug)
{
	gs_profile_start (NULL);

	plug->priv = gs_lock_plug_get_instance_private (plug);

	clear_clipboards (plug);

#ifdef WITH_LIBNOTIFY
	notify_init ("mate-screensaver-dialog");
	plug->priv->leave_note_enabled = TRUE;
#else
	plug->priv->leave_note_enabled = FALSE;
#endif

	GtkStyleContext *context;

	context = gtk_widget_get_style_context (GTK_WIDGET (plug));
	gtk_style_context_add_class (context, "lock-dialog");

	if (! load_theme (plug))
	{
		gs_debug ("Unable to load theme!");

		plug->priv->vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
		gtk_container_add (GTK_CONTAINER (plug), plug->priv->vbox);

		/* Notebook */

		plug->priv->notebook = gtk_notebook_new ();
		gtk_notebook_set_show_tabs (GTK_NOTEBOOK (plug->priv->notebook), FALSE);
		gtk_notebook_set_show_border (GTK_NOTEBOOK (plug->priv->notebook), FALSE);
		gtk_box_pack_start (GTK_BOX (plug->priv->vbox), plug->priv->notebook, TRUE, TRUE, 0);

		/* Page 1 */

		create_page_one (plug);

		date_time_update (plug);
		gtk_widget_show_all (plug->priv->vbox);
	}
	plug->priv->datetime_timeout_id = g_timeout_add_seconds (1, G_SOURCE_FUNC (date_time_update), plug);

	if (plug->priv->note_text_view != NULL)
	{
		GtkTextBuffer *buffer;
		buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (plug->priv->note_text_view));
		g_signal_connect (buffer, "changed", G_CALLBACK (on_note_text_buffer_changed), plug);
	}

	/* Layout indicator */
#ifdef WITH_KBD_LAYOUT_INDICATOR
	if (plug->priv->auth_prompt_kbd_layout_indicator != NULL)
	{
		XklEngine *engine;

		engine = xkl_engine_get_instance (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()));
		if (xkl_engine_get_num_groups (engine) > 1)
		{
			GtkWidget *layout_indicator;

			layout_indicator = matekbd_indicator_new ();
			matekbd_indicator_set_parent_tooltips (MATEKBD_INDICATOR (layout_indicator), TRUE);
			gtk_box_pack_start (GTK_BOX (plug->priv->auth_prompt_kbd_layout_indicator),
			                    layout_indicator,
			                    FALSE,
			                    FALSE,
			                    6);

			gtk_widget_show_all (layout_indicator);
			gtk_widget_show (plug->priv->auth_prompt_kbd_layout_indicator);
		}
		else
		{
			gtk_widget_hide (plug->priv->auth_prompt_kbd_layout_indicator);
		}

		g_object_unref (engine);
	}
#endif

	if (plug->priv->auth_note_button != NULL)
	{
		if (plug->priv->leave_note_enabled)
		{
			gtk_widget_show_all (plug->priv->auth_note_button);
		}
		else
		{
			gtk_widget_hide (plug->priv->auth_note_button);
		}
	}
	if (plug->priv->auth_switch_button != NULL)
	{
		if (plug->priv->switch_enabled)
		{
			gtk_widget_show_all (plug->priv->auth_switch_button);
		}
		else
		{
			gtk_widget_hide (plug->priv->auth_switch_button);
		}
	}

	gtk_widget_grab_default (plug->priv->auth_unlock_button);

	if (plug->priv->auth_username_label != NULL)
	{
		expand_string_for_label (plug->priv->auth_username_label);
	}

	if (plug->priv->auth_realname_label != NULL)
	{
		expand_string_for_label (plug->priv->auth_realname_label);
	}

	if (! plug->priv->logout_enabled || ! plug->priv->logout_command)
	{
		if (plug->priv->auth_logout_button != NULL)
		{
			gtk_widget_hide (plug->priv->auth_logout_button);
		}
	}

	plug->priv->timeout = DIALOG_TIMEOUT_MSEC;

	g_signal_connect (plug, "key_press_event",
	                  G_CALLBACK (entry_key_press), plug);

	/* button press handler used to inhibit popup menu */
	g_signal_connect (plug->priv->auth_prompt_entry, "button_press_event",
	                  G_CALLBACK (entry_button_press), NULL);
	gtk_entry_set_activates_default (GTK_ENTRY (plug->priv->auth_prompt_entry), TRUE);
	gtk_entry_set_visibility (GTK_ENTRY (plug->priv->auth_prompt_entry), FALSE);

	/* show/hide password */
	g_signal_connect (plug->priv->auth_prompt_entry, "icon-press",
	                  G_CALLBACK (entry_icon_clicked), NULL);

	g_signal_connect (plug->priv->auth_unlock_button, "clicked",
	                  G_CALLBACK (unlock_button_clicked), plug);

	g_signal_connect (plug->priv->auth_cancel_button, "clicked",
	                  G_CALLBACK (cancel_button_clicked), plug);

	if (plug->priv->status_message_label)
	{
		if (plug->priv->status_message)
		{
			gtk_label_set_text (GTK_LABEL (plug->priv->status_message_label),
			                    plug->priv->status_message);
		}
		else
		{
			gtk_widget_hide (plug->priv->status_message_label);
		}
	}

	if (plug->priv->auth_switch_button != NULL)
	{
		g_signal_connect (plug->priv->auth_switch_button, "clicked",
		                  G_CALLBACK (switch_user_button_clicked), plug);
	}

	if (plug->priv->auth_note_button != NULL)
	{
		g_signal_connect (plug->priv->auth_note_button, "clicked",
		                  G_CALLBACK (take_note), plug);
		g_signal_connect (plug->priv->note_ok_button, "clicked",
		                  G_CALLBACK (submit_note), plug);
		g_signal_connect (plug->priv->note_cancel_button, "clicked",
		                  G_CALLBACK (cancel_note), plug);
	}

	if (plug->priv->note_tab_label != NULL)
	{
		expand_string_for_label (plug->priv->note_tab_label);
	}

	if (plug->priv->auth_logout_button != NULL)
	{
		g_signal_connect (plug->priv->auth_logout_button, "clicked",
		                  G_CALLBACK (logout_button_clicked), plug);
	}

	g_signal_connect (plug, "delete_event", G_CALLBACK (delete_handler), NULL);

	gs_profile_end (NULL);
}

static void
gs_lock_plug_finalize (GObject *object)
{
	GSLockPlug *plug;

	g_return_if_fail (object != NULL);
	g_return_if_fail (GS_IS_LOCK_PLUG (object));

	plug = GS_LOCK_PLUG (object);

	g_return_if_fail (plug->priv != NULL);

	g_free (plug->priv->logout_command);

	remove_response_idle (plug);
	remove_cancel_timeout (plug);
	remove_datetime_timeout (plug);
#ifdef WITH_LIBNOTIFY
	notify_uninit ();
#endif

	G_OBJECT_CLASS (gs_lock_plug_parent_class)->finalize (object);
}

GtkWidget *
gs_lock_plug_new (void)
{
	GtkWidget *result;

	result = g_object_new (GS_TYPE_LOCK_PLUG, NULL);

	gtk_window_set_focus_on_map (GTK_WINDOW (result), TRUE);

	return result;
}
