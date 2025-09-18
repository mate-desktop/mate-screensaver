/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2004-2006 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2012-2021 MATE Developers
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
 * Authors: William Jon McCann <mccann@jhu.edu>
 *          Rodrigo Moya <rodrigo@novell.com>
 *
 */

#include "config.h"

#define _GNU_SOURCE
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>          /* For uid_t, gid_t */

#include <glib/gi18n.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#include <gio/gio.h>

#define MATE_DESKTOP_USE_UNSTABLE_API
#include <libmate-desktop/mate-desktop-utils.h>
#include <libmate-desktop/mate-desktop-thumbnail.h>

#include "gs-debug.h"

#include "copy-theme-dialog.h"

#include "gs-theme-manager.h"
#include "gs-job.h"
#include "gs-prefs.h" /* for GS_MODE enum */

#define LOCKDOWN_SETTINGS_SCHEMA "org.mate.lockdown"
#define KEY_LOCK_DISABLE "disable-lock-screen"

#define SESSION_SETTINGS_SCHEMA "org.mate.session"
#define KEY_IDLE_DELAY "idle-delay"

#define GSETTINGS_SCHEMA "org.mate.screensaver"
#define KEY_LOCK "lock-enabled"
#define KEY_IDLE_ACTIVATION_ENABLED "idle-activation-enabled"
#define KEY_MODE "mode"
#define KEY_LOCK_DELAY "lock-delay"
#define KEY_CYCLE_DELAY "cycle-delay"
#define KEY_THEMES "themes"

#define GPM_COMMAND "mate-power-preferences"

enum
{
    NAME_COLUMN = 0,
    ID_COLUMN,
    N_COLUMNS
};

/* Drag and drop info */
enum
{
    TARGET_URI_LIST,
    TARGET_NS_URL
};

static GtkTargetEntry drop_types [] =
{
	{ "text/uri-list", 0, TARGET_URI_LIST },
	{ "_NETSCAPE_URL", 0, TARGET_NS_URL }
};

static GtkBuilder     *builder = NULL;
static GSThemeManager *theme_manager = NULL;
static GSJob          *job = NULL;
static GSettings      *screensaver_settings = NULL;
static GSettings      *session_settings = NULL;
static GSettings      *lockdown_settings = NULL;
static MateDesktopThumbnailFactory *thumb_factory = NULL;

static gdouble
config_get_activate_delay (gboolean *is_writable)
{
	gint delay;

	if (is_writable)
	{
		*is_writable = g_settings_is_writable (session_settings,
		               KEY_IDLE_DELAY);
	}

	if ((delay = g_settings_get_int (session_settings, KEY_IDLE_DELAY)) < 1)
	{
		return 1.0;
	}

	return (gdouble) delay;
}

static void
config_set_activate_delay (gint32 timeout)
{
	g_settings_set_int (session_settings, KEY_IDLE_DELAY, timeout);
}

static gdouble
config_get_lock_delay (gboolean *is_writable)
{
	gint delay;

	if (is_writable)
	{
		*is_writable = g_settings_is_writable (screensaver_settings,
		               KEY_LOCK_DELAY);
	}

	if ((delay = g_settings_get_int (screensaver_settings, KEY_LOCK_DELAY)) < 1)
	{
		return 0.0;
	}

	return (gdouble) delay;
}

static void
config_set_lock_delay (gint32 timeout)
{
	g_settings_set_int (screensaver_settings, KEY_LOCK_DELAY, timeout);
}

static int
config_get_mode (gboolean *is_writable)
{
	int mode;

	if (is_writable)
	{
		*is_writable = g_settings_is_writable (screensaver_settings,
		               KEY_MODE);
	}

	mode = g_settings_get_enum (screensaver_settings, KEY_MODE);

	return mode;
}

static void
config_set_mode (int mode)
{
	g_settings_set_enum (screensaver_settings, KEY_MODE, mode);
}

static char *
config_get_theme (gboolean *is_writable)
{
	char *name;
	int   mode;

	if (is_writable)
	{
		gboolean can_write_theme;
		gboolean can_write_mode;

		can_write_theme = g_settings_is_writable (screensaver_settings,
		                                          KEY_THEMES);
		can_write_mode = g_settings_is_writable (screensaver_settings,
		                                         KEY_MODE);
		*is_writable = can_write_theme && can_write_mode;
	}

	mode = config_get_mode (NULL);

	name = NULL;
	if (mode == GS_MODE_BLANK_ONLY)
	{
		name = g_strdup ("__blank-only");
	}
	else if (mode == GS_MODE_RANDOM)
	{
		name = g_strdup ("__random");
	}
	else
	{
		gchar **strv;
		strv = g_settings_get_strv (screensaver_settings,
	                                KEY_THEMES);
		if (strv != NULL) {
			name = g_strdup (strv[0]);
		}
		else
		{
			/* TODO: handle error */
			/* default to blank */
			name = g_strdup ("__blank-only");
		}

		g_strfreev (strv);
	}

	return name;
}

static gchar **
get_all_theme_ids (void)
{
	gchar **ids = NULL;
	GSList *entries;
	GSList *l;
        guint idx = 0;

	entries = gs_theme_manager_get_info_list (theme_manager);
        ids = g_new0 (gchar *, g_slist_length (entries) + 1);
	for (l = entries; l; l = l->next)
	{
		GSThemeInfo *info = l->data;

		ids[idx++] = g_strdup (gs_theme_info_get_id (info));
		gs_theme_info_unref (info);
	}
	g_slist_free (entries);

	return ids;
}

static void
config_set_theme (const char *theme_id)
{
	gchar **strv = NULL;
	int     mode;

	if (theme_id && strcmp (theme_id, "__blank-only") == 0)
	{
		mode = GS_MODE_BLANK_ONLY;
	}
	else if (theme_id && strcmp (theme_id, "__random") == 0)
	{
		mode = GS_MODE_RANDOM;

		/* set the themes key to contain all available screensavers */
		strv = get_all_theme_ids ();
	}
	else
	{
		mode = GS_MODE_SINGLE;
		strv = g_strsplit (theme_id, "%%%", 1);
	}

	config_set_mode (mode);

	g_settings_set_strv (screensaver_settings,
	                     KEY_THEMES,
	                     (const gchar * const*) strv);

	g_strfreev (strv);

}

static gboolean
config_get_enabled (gboolean *is_writable)
{
	int enabled;

	if (is_writable)
	{
		*is_writable = g_settings_is_writable (screensaver_settings,
		               KEY_LOCK);
	}

	enabled = g_settings_get_boolean (screensaver_settings, KEY_IDLE_ACTIVATION_ENABLED);

	return enabled;
}

static void
config_set_enabled (gboolean enabled)
{
	g_settings_set_boolean (screensaver_settings, KEY_IDLE_ACTIVATION_ENABLED, enabled);
}

static gboolean
config_get_lock (gboolean *is_writable)
{
	gboolean lock;

	if (is_writable)
	{
		*is_writable = g_settings_is_writable (screensaver_settings,
		               KEY_LOCK);
	}

	lock = g_settings_get_boolean (screensaver_settings, KEY_LOCK);

	return lock;
}

static gboolean
config_get_lock_disabled (void)
{
	return g_settings_get_boolean (lockdown_settings, KEY_LOCK_DISABLE);
}

static void
config_set_lock (gboolean lock)
{
	g_settings_set_boolean (screensaver_settings, KEY_LOCK, lock);
}

static void
job_set_theme (const char *theme)
{
	GSThemeInfo *info;
	const char  *command;

	command = NULL;

	info = gs_theme_manager_lookup_theme_info (theme_manager, theme);
	if (info != NULL)
	{
		command = gs_theme_info_get_exec (info);
	}

	gs_job_set_command (job, command);

	if (info != NULL)
	{
		gs_theme_info_unref (info);
	}
}

static gboolean
preview_on_draw (GtkWidget *widget,
                 cairo_t   *cr,
                 gpointer   data)
{
	if (job == NULL || !gs_job_is_running (job))
	{
		cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
		cairo_set_source_rgb (cr, 0, 0, 0);
		cairo_paint (cr);
	}

	return FALSE;
}

static void
preview_set_theme (GtkWidget  *widget,
                   const char *theme,
                   const char *name)
{
	GtkWidget *label;
	char      *markup;

	if (job != NULL)
	{
		gs_job_stop (job);
	}

	gtk_widget_queue_draw (widget);

	label = GTK_WIDGET (gtk_builder_get_object (builder, "fullscreen_preview_theme_label"));
	markup = g_markup_printf_escaped ("<i>%s</i>", name);
	gtk_label_set_markup (GTK_LABEL (label), markup);
	g_free (markup);

	if ((theme && strcmp (theme, "__blank-only") == 0))
	{

	}
	else if (theme && strcmp (theme, "__random") == 0)
	{
		gchar **themes;

		themes = get_all_theme_ids ();
		if (themes != NULL)
		{
			gint32  i;

			i = g_random_int_range (0, g_strv_length (themes));
                        job_set_theme (themes[i]);
                        g_strfreev (themes);

			gs_job_start (job);
		}
	}
	else
	{
		job_set_theme (theme);
		gs_job_start (job);
	}
}

static void
help_display (void)
{
	GError *error;

	error = NULL;
	gtk_show_uri_on_window (NULL,
                                "help:mate-user-guide/prefs-screensaver",
                                GDK_CURRENT_TIME,
                                &error);

	if (error != NULL)
	{
		GtkWidget *d;

		d = gtk_message_dialog_new (NULL,
		                            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		                            GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
		                            "%s", error->message);
		gtk_dialog_run (GTK_DIALOG (d));
		gtk_widget_destroy (d);
		g_error_free (error);
	}

}

static void
response_cb (GtkWidget *widget,
             int        response_id)
{

	if (response_id == GTK_RESPONSE_HELP)
	{
		help_display ();
	}
	else if (response_id == GTK_RESPONSE_REJECT)
	{
		GError  *error;
		gboolean res;

		error = NULL;

		res = mate_gdk_spawn_command_line_on_screen (gdk_screen_get_default (),
		                                        GPM_COMMAND,
		                                        &error);
		if (! res)
		{
			g_warning ("Unable to start power management preferences: %s", error->message);
			g_error_free (error);
		}
	}
	else
	{
		gtk_widget_destroy (widget);
		gtk_main_quit ();
	}
}

static GSList *
get_theme_info_list (void)
{
	return gs_theme_manager_get_info_list (theme_manager);
}

static void
populate_model (GtkTreeStore *store)
{
	GtkTreeIter iter;
	GSList     *themes        = NULL;
	GSList     *l;

	gtk_tree_store_append (store, &iter, NULL);
	gtk_tree_store_set (store, &iter,
	                    NAME_COLUMN, _("Blank screen"),
	                    ID_COLUMN, "__blank-only",
	                    -1);

	gtk_tree_store_append (store, &iter, NULL);
	gtk_tree_store_set (store, &iter,
	                    NAME_COLUMN, _("Random"),
	                    ID_COLUMN, "__random",
	                    -1);

	gtk_tree_store_append (store, &iter, NULL);
	gtk_tree_store_set (store, &iter,
	                    NAME_COLUMN, NULL,
	                    ID_COLUMN, "__separator",
	                    -1);

	themes = get_theme_info_list ();

	if (themes == NULL)
	{
		return;
	}

	for (l = themes; l; l = l->next)
	{
		const char  *name;
		const char  *id;
		GSThemeInfo *info = l->data;

		if (info == NULL)
		{
			continue;
		}

		name = gs_theme_info_get_name (info);
		id = gs_theme_info_get_id (info);

		gtk_tree_store_append (store, &iter, NULL);
		gtk_tree_store_set (store, &iter,
		                    NAME_COLUMN, name,
		                    ID_COLUMN, id,
		                    -1);

		gs_theme_info_unref (info);
	}

	g_slist_free (themes);
}

static void
tree_selection_previous (GtkTreeSelection *selection)
{
	GtkTreeIter   iter;
	GtkTreeModel *model;
	GtkTreePath  *path;

	if (! gtk_tree_selection_get_selected (selection, &model, &iter))
	{
		return;
	}

	path = gtk_tree_model_get_path (model, &iter);
	if (gtk_tree_path_prev (path))
	{
		gtk_tree_selection_select_path (selection, path);
	}
}

static void
tree_selection_next (GtkTreeSelection *selection)
{
	GtkTreeIter   iter;
	GtkTreeModel *model;
	GtkTreePath  *path;

	if (! gtk_tree_selection_get_selected (selection, &model, &iter))
	{
		return;
	}

	path = gtk_tree_model_get_path (model, &iter);
	gtk_tree_path_next (path);
	gtk_tree_selection_select_path (selection, path);
}

static void
tree_selection_changed_cb (GtkTreeSelection *selection,
                           GtkWidget        *preview)
{
	GtkTreeIter   iter;
	GtkTreeModel *model;
	char         *theme;
	char         *name;

	if (! gtk_tree_selection_get_selected (selection, &model, &iter))
	{
		return;
	}

	gtk_tree_model_get (model, &iter, ID_COLUMN, &theme, NAME_COLUMN, &name, -1);

	if (theme == NULL)
	{
		g_free (name);
		return;
	}

	preview_set_theme (preview, theme, name);
	config_set_theme (theme);

	g_free (theme);
	g_free (name);
}

static void
activate_delay_value_changed_cb (GtkRange *range,
                                 gpointer  user_data)
{
	gdouble value;

	value = gtk_range_get_value (range);
	config_set_activate_delay ((gint32)value);
}

static void
lock_delay_value_changed_cb (GtkRange *range,
                             gpointer  user_data)
{
	gdouble value;

	value = gtk_range_get_value (range);
	config_set_lock_delay ((gint32)value);
}

static int
compare_theme_names (char *name_a,
                     char *name_b,
                     char *id_a,
                     char *id_b)
{

	if (id_a == NULL)
	{
		return 1;
	}
	else if (id_b == NULL)
	{
		return -1;
	}

	if (strcmp (id_a, "__blank-only") == 0)
	{
		return -1;
	}
	else if (strcmp (id_b, "__blank-only") == 0)
	{
		return 1;
	}
	else if (strcmp (id_a, "__random") == 0)
	{
		return -1;
	}
	else if (strcmp (id_b, "__random") == 0)
	{
		return 1;
	}
	else if (strcmp (id_a, "__separator") == 0)
	{
		return -1;
	}
	else if (strcmp (id_b, "__separator") == 0)
	{
		return 1;
	}

	if (name_a == NULL)
	{
		return 1;
	}
	else if (name_b == NULL)
	{
		return -1;
	}

	return g_utf8_collate (name_a, name_b);
}

static int
compare_theme  (GtkTreeModel *model,
                GtkTreeIter  *a,
                GtkTreeIter  *b,
                gpointer      user_data)
{
	char *name_a;
	char *name_b;
	char *id_a;
	char *id_b;
	int   result;

	gtk_tree_model_get (model, a, NAME_COLUMN, &name_a, -1);
	gtk_tree_model_get (model, b, NAME_COLUMN, &name_b, -1);
	gtk_tree_model_get (model, a, ID_COLUMN, &id_a, -1);
	gtk_tree_model_get (model, b, ID_COLUMN, &id_b, -1);

	result = compare_theme_names (name_a, name_b, id_a, id_b);

	g_free (name_a);
	g_free (name_b);
	g_free (id_a);
	g_free (id_b);

	return result;
}

static gboolean
separator_func (GtkTreeModel *model,
                GtkTreeIter  *iter,
                gpointer      data)
{
	int       column = GPOINTER_TO_INT (data);
	gboolean  res = FALSE;
	char     *text;

	gtk_tree_model_get (model, iter, column, &text, -1);

	if (text != NULL && strcmp (text, "__separator") == 0)
	{
		res = TRUE;
	}

	g_free (text);

	return res;
}

static void
setup_treeview (GtkWidget *tree,
                GtkWidget *preview)
{
	GtkTreeStore      *store;
	GtkTreeViewColumn *column;
	GtkCellRenderer   *renderer;
	GtkTreeSelection  *select;

	store = gtk_tree_store_new (N_COLUMNS,
	                            G_TYPE_STRING,
	                            G_TYPE_STRING);
	populate_model (store);

	gtk_tree_view_set_model (GTK_TREE_VIEW (tree),
	                         GTK_TREE_MODEL (store));

	g_object_unref (store);

	g_object_set (tree, "show-expanders", FALSE, NULL);

	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes ("Name", renderer,
	         "text", NAME_COLUMN,
	         NULL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (tree), column);

	gtk_tree_view_column_set_sort_column_id (column, NAME_COLUMN);
	gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (store),
	                                 NAME_COLUMN,
	                                 compare_theme,
	                                 NULL, NULL);
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (store),
	                                      NAME_COLUMN,
	                                      GTK_SORT_ASCENDING);

	gtk_tree_view_set_row_separator_func (GTK_TREE_VIEW (tree),
	                                      separator_func,
	                                      GINT_TO_POINTER (ID_COLUMN),
	                                      NULL);

	select = gtk_tree_view_get_selection (GTK_TREE_VIEW (tree));
	gtk_tree_selection_set_mode (select, GTK_SELECTION_SINGLE);
	g_signal_connect (select, "changed",
	                  G_CALLBACK (tree_selection_changed_cb),
	                  preview);

}

static void
setup_treeview_selection (GtkWidget *tree)
{
	char         *theme;
	GtkTreeModel *model;
	GtkTreeIter   iter;
	GtkTreePath  *path = NULL;
	gboolean      is_writable;

	theme = config_get_theme (&is_writable);

	if (! is_writable)
	{
		gtk_widget_set_sensitive (tree, FALSE);
	}

	model = gtk_tree_view_get_model (GTK_TREE_VIEW (tree));

	if (theme && gtk_tree_model_get_iter_first (model, &iter))
	{

		do
		{
			char *id;
			gboolean found;

			gtk_tree_model_get (model, &iter,
			                    ID_COLUMN, &id, -1);
			found = (id && strcmp (id, theme) == 0);
			g_free (id);

			if (found)
			{
				path = gtk_tree_model_get_path (model, &iter);
				break;
			}

		}
		while (gtk_tree_model_iter_next (model, &iter));
	}

	if (path)
	{
		gtk_tree_view_set_cursor (GTK_TREE_VIEW (tree),
		                          path,
		                          NULL,
		                          FALSE);

		gtk_tree_path_free (path);
	}

	g_free (theme);
}

static void
reload_themes (void)
{
	GtkWidget    *treeview;
	GtkTreeModel *model;

	treeview = GTK_WIDGET (gtk_builder_get_object (builder, "savers_treeview"));
	model = gtk_tree_view_get_model (GTK_TREE_VIEW (treeview));
	gtk_tree_store_clear (GTK_TREE_STORE (model));
	populate_model (GTK_TREE_STORE (model));

	gtk_tree_view_set_model (GTK_TREE_VIEW (treeview),
	                         GTK_TREE_MODEL (model));
}

static void
theme_copy_complete_cb (GtkWidget *dialog, gpointer user_data)
{
	reload_themes ();
	gtk_widget_destroy (dialog);
}

static void
theme_installer_run (GtkWidget *prefs_dialog, GList *files)
{
	GtkWidget *copy_dialog;

	copy_dialog = copy_theme_dialog_new (files);
	g_list_free_full (files, g_object_unref);

	gtk_window_set_transient_for (GTK_WINDOW (copy_dialog),
	                              GTK_WINDOW (prefs_dialog));
	gtk_window_set_icon_name (GTK_WINDOW (copy_dialog),
	                          "preferences-desktop-screensaver");

	g_signal_connect (copy_dialog, "complete",
	                  G_CALLBACK (theme_copy_complete_cb), NULL);

	copy_theme_dialog_begin (COPY_THEME_DIALOG (copy_dialog));
}

/* Callback issued during drag movements */
static gboolean
drag_motion_cb (GtkWidget      *widget,
                GdkDragContext *context,
                int             x,
                int             y,
                guint           time,
                gpointer        data)
{
	return FALSE;
}

/* Callback issued during drag leaves */
static void
drag_leave_cb (GtkWidget      *widget,
               GdkDragContext *context,
               guint           time,
               gpointer        data)
{
	gtk_widget_queue_draw (widget);
}

/* GIO has no version of mate_vfs_uri_list_parse(), so copy from MateVFS
 * and re-work to create GFiles.
**/
static GList *
uri_list_parse (const gchar *uri_list)
{
	const gchar *p, *q;
	gchar *retval;
	GFile *file;
	GList *result = NULL;

	g_return_val_if_fail (uri_list != NULL, NULL);

	p = uri_list;

	/* We don't actually try to validate the URI according to RFC
	 * 2396, or even check for allowed characters - we just ignore
	 * comments and trim whitespace off the ends.  We also
	 * allow LF delimination as well as the specified CRLF.
	 */
	while (p != NULL)
	{
		if (*p != '#')
		{
			while (g_ascii_isspace (*p))
				p++;

			q = p;
			while ((*q != '\0')
			        && (*q != '\n')
			        && (*q != '\r'))
				q++;

			if (q > p)
			{
				q--;
				while (q > p
				        && g_ascii_isspace (*q))
					q--;

				retval = g_malloc (q - p + 2);
				strncpy (retval, p, q - p + 1);
				retval[q - p + 1] = '\0';

				file = g_file_new_for_uri (retval);

				g_free (retval);

				if (file != NULL)
					result = g_list_prepend (result, file);
			}
		}
		p = strchr (p, '\n');
		if (p != NULL)
			p++;
	}

	return g_list_reverse (result);
}

/* Callback issued on actual drops. Attempts to load the file dropped. */
static void
drag_data_received_cb (GtkWidget        *widget,
                       GdkDragContext   *context,
                       int               x,
                       int               y,
                       GtkSelectionData *selection_data,
                       guint             info,
                       guint             time,
                       gpointer          data)
{
	GList     *files;

	if (!(info == TARGET_URI_LIST || info == TARGET_NS_URL))
		return;

	files = uri_list_parse ((char *) gtk_selection_data_get_data (selection_data));
	if (files != NULL)
	{
		GtkWidget *prefs_dialog;

		prefs_dialog = GTK_WIDGET (gtk_builder_get_object (builder, "prefs_dialog"));
		theme_installer_run (prefs_dialog, files);
	}
}

/* Adapted from totem_time_to_string_text */
static char *
time_to_string_text (long time)
{
	char  *secs, *mins, *hours, *string;
	int    sec, min, hour;

	sec = time % 60;
	time = time - sec;
	min = (time % (60 * 60)) / 60;
	time = time - (min * 60);
	hour = time / (60 * 60);

	hours = g_strdup_printf (ngettext ("%d hour",
	                                   "%d hours", hour), hour);

	mins = g_strdup_printf (ngettext ("%d minute",
	                                  "%d minutes", min), min);

	secs = g_strdup_printf (ngettext ("%d second",
	                                  "%d seconds", sec), sec);

	if (hour > 0)
	{
		if (sec > 0)
			/* hour:minutes:seconds */
			string = g_strdup_printf (_("%s %s %s"), hours, mins, secs);
		else if (min > 0)
			/* hour:minutes */
			string = g_strdup_printf (_("%s %s"), hours, mins);
		else
			/* hour */
			string = g_strdup_printf (_("%s"), hours);
	}
	else if (min > 0)
	{
		if (sec > 0)
		{
			/* minutes:seconds */
			string = g_strdup_printf (_("%s %s"), mins, secs);
		}
		else
		{
			/* minutes */
			string = g_strdup_printf (_("%s"), mins);
		}
	}
	else
	{
		/* seconds */
		string = g_strdup_printf (_("%s"), secs);
	}

	g_free (hours);
	g_free (mins);
	g_free (secs);

	return string;
}

static char *
format_value_callback_time (GtkScale *scale,
                            gdouble   value)
{
	gchar *time_str, *big_time_str;
	GtkAdjustment *adj;
	gdouble lower, range, delta;
	gint pad_size;

	/* get the value representation as a string */
	time_str = time_to_string_text ((long) (value * 60.0));

	/* Now, adjust the string so the representation for the bounds are the
	 * longest ones, and try and adjust the length as smoothly as possible.
	 * The issue here is that GTK is using the lower and upper value
	 * representations to compute the largest expected value's bounding box,
	 * so those need to be bigger than anything else we might represent,
	 * otherwise layout gets messed up (wraps and overflows).  To achieve this,
	 * we pad the values near each bound so its length is at least the same as
	 * the biggest actual value.  We cannot really do anything perfect here
	 * because what matters is the pango layout size for the largest value, but
	 * we don't have access to enough information to create one matching what
	 * GTK will actually use, and even so it'd be trial-and-error until the
	 * layout is big enough.  So the silly assumptions below are probably good
	 * enough. */
	adj = gtk_range_get_adjustment (GTK_RANGE (scale));
	lower = gtk_adjustment_get_lower (adj);
	range = gtk_adjustment_get_upper (adj) - lower;
	delta = range / 2 - (value - lower);
	/* the largest (character-wise) time string we expect */
	big_time_str = time_to_string_text (7199 /* 1:59:59 */);
	pad_size = ((g_utf8_strlen (big_time_str, -1) * (ABS (delta) / range)) -
	            g_utf8_strlen (time_str, -1));
	g_free (big_time_str);
	if (pad_size > 0)
	{
		/* pad string with EM SPACE (U+2003) */
		GString *padded = g_string_new (NULL);

		/* adjust pad side in RTL locales that aren't actually translated, as
		 * a properly translated one would have text drawn RTL already */
		if (gtk_widget_get_direction (GTK_WIDGET (scale)) == GTK_TEXT_DIR_RTL)
		{
			const gchar *msg_plural = "%d minutes";
			if (ngettext ("%d minute", msg_plural, 2) == msg_plural)
				delta *= -1;
		}

		if (delta < 0)
		{
			for (gint i = 0; i < pad_size; i++)
				g_string_append_unichar (padded, 0x2003);
			g_string_append (padded, time_str);
		}
		else
		{
			g_string_append (padded, time_str);
			for (gint i = 0; i < pad_size; i++)
				g_string_append_unichar (padded, 0x2003);
		}
		g_free (time_str);
		time_str = g_string_free (padded, FALSE);
	}

	return time_str;
}

static void
lock_checkbox_toggled (GtkToggleButton *button, gpointer user_data)
{
	config_set_lock (gtk_toggle_button_get_active (button));
}

static void
enabled_checkbox_toggled (GtkToggleButton *button, gpointer user_data)
{
	config_set_enabled (gtk_toggle_button_get_active (button));
}

static void
picture_filename_changed (GtkFileChooserButton *button, gpointer user_data)
{
	char *picture_filename;

	picture_filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (button));
	g_settings_set_string (screensaver_settings, "picture-filename", picture_filename);
	g_free (picture_filename);
}

static void
ui_disable_lock (gboolean disable)
{
	GtkWidget *widget;

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "lock_checkbox"));
	gtk_widget_set_sensitive (widget, !disable);
	if (disable)
	{
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), FALSE);
	}
}

static void
ui_set_lock (gboolean enabled)
{
	GtkWidget *widget;
	gboolean   active;
	gboolean   lock_disabled;

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "lock_checkbox"));

	active = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
	if (active != enabled)
	{
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), enabled);
	}
	lock_disabled = config_get_lock_disabled ();
	ui_disable_lock (lock_disabled);
}

static void
ui_set_enabled (gboolean enabled)
{
	GtkWidget *widget;
	gboolean   active;
	gboolean   is_writable;
	gboolean   lock_disabled;

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "enable_checkbox"));
	active = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
	if (active != enabled)
	{
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), enabled);
	}

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "lock_checkbox"));
	config_get_lock (&is_writable);
	if (is_writable)
	{
		gtk_widget_set_sensitive (widget, enabled);
	}
	lock_disabled = config_get_lock_disabled ();
	ui_disable_lock(lock_disabled);
}

static void
ui_set_delay (const char *name, gdouble delay)
{
	GtkWidget *widget;

	widget = GTK_WIDGET (gtk_builder_get_object (builder, name));
	gtk_range_set_value (GTK_RANGE (widget), delay);
}

static void
key_changed_cb (GSettings *settings, const gchar *key, gpointer data)
{
	if (strcmp (key, KEY_IDLE_ACTIVATION_ENABLED) == 0)
	{
			gboolean enabled;

			enabled = g_settings_get_boolean (settings, key);

			ui_set_enabled (enabled);
	}
	else if (strcmp (key, KEY_LOCK) == 0)
	{
		        gboolean enabled;

			enabled = g_settings_get_boolean (settings, key);

			ui_set_lock (enabled);
	}
	else if (strcmp (key, KEY_LOCK_DISABLE) == 0)
	{
		        gboolean disabled;

			disabled = g_settings_get_boolean (settings, key);

			ui_disable_lock (disabled);
	}
	else if (strcmp (key, KEY_THEMES) == 0)
	{
		        GtkWidget *treeview;

			treeview = GTK_WIDGET (gtk_builder_get_object (builder, "savers_treeview"));
			setup_treeview_selection (treeview);
	}
	else if (strcmp (key, KEY_IDLE_DELAY) == 0)
	{
			int delay;

			delay = g_settings_get_int (settings, key);
			ui_set_delay ("activate_delay_hscale", (gdouble) delay);
	}
	else if (strcmp (key, KEY_LOCK_DELAY) == 0)
	{
		int delay;

		delay = g_settings_get_int (settings, key);
		ui_set_delay ("lock_delay_hscale", (gdouble) delay);
	}
	else
	{
		/*g_warning ("Config key not handled: %s", key);*/
	}
}

static void
fullscreen_preview_previous_cb (GtkWidget *fullscreen_preview_window,
                                gpointer   user_data)
{
	GtkWidget        *treeview;
	GtkTreeSelection *selection;

	treeview = GTK_WIDGET (gtk_builder_get_object (builder, "savers_treeview"));
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview));
	tree_selection_previous (selection);
}

static void
fullscreen_preview_next_cb (GtkWidget *fullscreen_preview_window,
                            gpointer   user_data)
{
	GtkWidget        *treeview;
	GtkTreeSelection *selection;

	treeview = GTK_WIDGET (gtk_builder_get_object (builder, "savers_treeview"));
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview));
	tree_selection_next (selection);
}

static void
fullscreen_preview_cancelled_cb (GtkWidget *button,
                                 gpointer   user_data)
{

	GtkWidget *fullscreen_preview_area;
	GtkWidget *fullscreen_preview_window;
	GtkWidget *preview_area;
	GtkWidget *dialog;

	preview_area = GTK_WIDGET (gtk_builder_get_object (builder, "preview_area"));
	gs_job_set_widget (job, preview_area);

	fullscreen_preview_area = GTK_WIDGET (gtk_builder_get_object (builder, "fullscreen_preview_area"));
	gtk_widget_queue_draw (fullscreen_preview_area);

	fullscreen_preview_window = GTK_WIDGET (gtk_builder_get_object (builder, "fullscreen_preview_window"));
	gtk_widget_hide (fullscreen_preview_window);

	dialog = GTK_WIDGET (gtk_builder_get_object (builder, "prefs_dialog"));
	gtk_widget_show (dialog);
	gtk_window_present (GTK_WINDOW (dialog));
}

static void
fullscreen_preview_start_cb (GtkWidget *widget,
                             gpointer   user_data)
{
	GtkWidget *fullscreen_preview_area;
	GtkWidget *fullscreen_preview_window;
	GtkWidget *dialog;

	dialog = GTK_WIDGET (gtk_builder_get_object (builder, "prefs_dialog"));
	gtk_widget_hide (dialog);

	fullscreen_preview_window = GTK_WIDGET (gtk_builder_get_object (builder, "fullscreen_preview_window"));

	gtk_window_fullscreen (GTK_WINDOW (fullscreen_preview_window));
	gtk_window_set_keep_above (GTK_WINDOW (fullscreen_preview_window), TRUE);

	gtk_widget_show (fullscreen_preview_window);
	gtk_widget_grab_focus (fullscreen_preview_window);

	fullscreen_preview_area = GTK_WIDGET (gtk_builder_get_object (builder, "fullscreen_preview_area"));
	gtk_widget_queue_draw (fullscreen_preview_area);
	gs_job_set_widget (job, fullscreen_preview_area);
}

static void
constrain_list_size (GtkWidget      *widget,
                     GtkAllocation  *allocation,
                     GtkWidget      *to_size)
{
	GtkRequisition req;
	int            max_height;

	/* constrain height to be the tree height up to a max */
	max_height = (HeightOfScreen (gdk_x11_screen_get_xscreen (gtk_widget_get_screen (widget)))) / 4;

	gtk_widget_get_preferred_size (to_size, &req, NULL);
	allocation->height = MIN (req.height, max_height);
}

static void
setup_list_size_constraint (GtkWidget *widget,
                            GtkWidget *to_size)
{
	g_signal_connect (widget, "size-allocate",
	                  G_CALLBACK (constrain_list_size), to_size);
}

static gboolean
check_is_root_user (void)
{
#ifndef G_OS_WIN32
	uid_t ruid, euid, suid; /* Real, effective and saved user ID's */
	gid_t rgid, egid, sgid; /* Real, effective and saved group ID's */

#ifdef HAVE_GETRESUID
	if (getresuid (&ruid, &euid, &suid) != 0 ||
	        getresgid (&rgid, &egid, &sgid) != 0)
#endif /* HAVE_GETRESUID */
	{
		suid = ruid = getuid ();
		sgid = rgid = getgid ();
		euid = geteuid ();
		egid = getegid ();
	}

	if (ruid == 0)
	{
		return TRUE;
	}

#endif
	return FALSE;
}

static void
setup_for_root_user (void)
{
	GtkWidget *lock_checkbox;
	GtkWidget *label;

	lock_checkbox = GTK_WIDGET (gtk_builder_get_object (builder, "lock_checkbox"));
	label = GTK_WIDGET (gtk_builder_get_object (builder, "root_warning_label"));

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (lock_checkbox), FALSE);
	gtk_widget_set_sensitive (lock_checkbox, FALSE);

	gtk_widget_show (label);
}

/* copied from gs-window-x11.c */
#ifndef _GNU_SOURCE
extern char **environ;
#endif

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

	return visual;
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

static gboolean
setup_treeview_idle (gpointer data)
{
	(void)data;			/* remove warning unused parameter ‘data’ */
	GtkWidget *preview;
	GtkWidget *treeview;

	preview  = GTK_WIDGET (gtk_builder_get_object (builder, "preview_area"));
	treeview = GTK_WIDGET (gtk_builder_get_object (builder, "savers_treeview"));

	setup_treeview (treeview, preview);
	setup_treeview_selection (treeview);

	return FALSE;
}

static gboolean
is_program_in_path (const char *program)
{
	char *tmp = g_find_program_in_path (program);
	if (tmp != NULL)
	{
		g_free (tmp);
		return TRUE;
	}
	else
	{
		return FALSE;
	}
}

static void
update_picture_filename_preview (GtkFileChooser *file_chooser,
                                 gpointer        data)
{
	GtkWidget *preview;
	char *uri;
	gboolean have_preview = FALSE;

	preview = GTK_WIDGET (data);
	uri = gtk_file_chooser_get_preview_uri (file_chooser);
	if (uri) {
		GFile     *file;
		GFileInfo *file_info;
		time_t     mtime;
		guint64    mtime_aux;
		char      *thumb_path;

		file = g_file_new_for_uri (uri);
		file_info = g_file_query_info (file,
		                               G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE","G_FILE_ATTRIBUTE_TIME_MODIFIED,
		                               G_FILE_QUERY_INFO_NONE,
		                               NULL, NULL);
		g_object_unref (file);

		if (file_info == NULL)
			goto no_file_info;

		mtime_aux = g_file_info_get_attribute_uint64 (file_info,
		                                              G_FILE_ATTRIBUTE_TIME_MODIFIED);
		mtime = (time_t)mtime_aux;
		if ((thumb_path = mate_desktop_thumbnail_factory_lookup (thumb_factory, uri, mtime)) != NULL) {
			/* try to load thumbnail from cache */
			gtk_image_set_from_file (GTK_IMAGE (preview), thumb_path);
			have_preview = TRUE;
			g_free (thumb_path);
		} else {
			/* try to generate thumbnail from wallpaper & store to cache */
			const char *content_type = g_file_info_get_content_type (file_info);
			if (content_type != NULL) {
				char *mime_type = g_content_type_get_mime_type (content_type);
				if (mime_type != NULL) {
					/* can generate thumbnail and did not fail previously */
					if (mate_desktop_thumbnail_factory_can_thumbnail (thumb_factory, uri, mime_type, mtime)) {
						GdkPixbuf *pixbuf;
						pixbuf =  mate_desktop_thumbnail_factory_generate_thumbnail (thumb_factory,
				                                                                             uri, mime_type);
						if (pixbuf != NULL) {
							gtk_image_set_from_pixbuf (GTK_IMAGE (preview), pixbuf);
							mate_desktop_thumbnail_factory_save_thumbnail (thumb_factory,
							                                               pixbuf, uri, mtime);
							have_preview = TRUE;
							g_object_unref (pixbuf);
						} else {
							mate_desktop_thumbnail_factory_create_failed_thumbnail (thumb_factory,
							                                                        uri, mtime);
						}
					}
					g_free (mime_type);
				}
			}
		}
		g_object_unref (file_info);

no_file_info:
		g_free (uri);
	}
	gtk_file_chooser_set_preview_widget_active (file_chooser, have_preview);
}

static void
init_capplet (void)
{
	GtkWidget *dialog;
	GtkWidget *preview;
	GtkWidget *treeview;
	GtkWidget *list_scroller;
	GtkWidget *activate_delay_hscale;
	GtkWidget *lock_delay_hscale;
	GtkWidget *label;
	GtkWidget *enabled_checkbox;
	GtkWidget *lock_checkbox;
	GtkWidget *root_warning_label;
	GtkWidget *preview_button;
	GtkWidget *gpm_button;
	GtkWidget *fullscreen_preview_window;
	GtkWidget *fullscreen_preview_area;
	GtkWidget *fullscreen_preview_previous;
	GtkWidget *fullscreen_preview_next;
	GtkWidget *fullscreen_preview_close;
	GtkWidget *picture_filename;
	GtkWidget *picture_filename_preview;
	gdouble    activate_delay;
	gboolean   enabled;
	gboolean   is_writable;
	GError    *error=NULL;
	gint       mode;

	builder = gtk_builder_new();
	if (!gtk_builder_add_from_resource (builder, "/org/mate/screensaver/preferences.ui", &error))
	{
		g_warning("Couldn't load builder resource: %s", error->message);
		g_error_free(error);
	}

	if (builder == NULL)
	{

		dialog = gtk_message_dialog_new (NULL,
		                                 0, GTK_MESSAGE_ERROR,
		                                 GTK_BUTTONS_OK,
		                                 _("Could not load the main interface"));
		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
		        _("Please make sure that the screensaver is properly installed"));

		gtk_dialog_set_default_response (GTK_DIALOG (dialog),
		                                 GTK_RESPONSE_OK);
		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (dialog);
		exit (1);
	}

	preview            = GTK_WIDGET (gtk_builder_get_object (builder, "preview_area"));
	dialog             = GTK_WIDGET (gtk_builder_get_object (builder, "prefs_dialog"));
	treeview           = GTK_WIDGET (gtk_builder_get_object (builder, "savers_treeview"));
	list_scroller      = GTK_WIDGET (gtk_builder_get_object (builder, "themes_scrolled_window"));
	activate_delay_hscale = GTK_WIDGET (gtk_builder_get_object (builder, "activate_delay_hscale"));
	lock_delay_hscale  = GTK_WIDGET (gtk_builder_get_object (builder, "lock_delay_hscale"));
	enabled_checkbox   = GTK_WIDGET (gtk_builder_get_object (builder, "enable_checkbox"));
	lock_checkbox      = GTK_WIDGET (gtk_builder_get_object (builder, "lock_checkbox"));
	root_warning_label = GTK_WIDGET (gtk_builder_get_object (builder, "root_warning_label"));
	preview_button     = GTK_WIDGET (gtk_builder_get_object (builder, "preview_button"));
	gpm_button         = GTK_WIDGET (gtk_builder_get_object (builder, "gpm_button"));
	fullscreen_preview_window = GTK_WIDGET (gtk_builder_get_object (builder, "fullscreen_preview_window"));
	fullscreen_preview_area = GTK_WIDGET (gtk_builder_get_object (builder, "fullscreen_preview_area"));
	fullscreen_preview_close = GTK_WIDGET (gtk_builder_get_object (builder, "fullscreen_preview_close"));
	fullscreen_preview_previous = GTK_WIDGET (gtk_builder_get_object (builder, "fullscreen_preview_previous_button"));
	fullscreen_preview_next = GTK_WIDGET (gtk_builder_get_object (builder, "fullscreen_preview_next_button"));
	picture_filename = GTK_WIDGET (gtk_builder_get_object (builder, "picture_filename"));

	label              = GTK_WIDGET (gtk_builder_get_object (builder, "activate_delay_label"));
	gtk_label_set_mnemonic_widget (GTK_LABEL (label), activate_delay_hscale);
	label              = GTK_WIDGET (gtk_builder_get_object (builder, "lock_delay_label"));
	gtk_label_set_mnemonic_widget (GTK_LABEL (label), lock_delay_hscale);
	label              = GTK_WIDGET (gtk_builder_get_object (builder, "savers_label"));
	gtk_label_set_mnemonic_widget (GTK_LABEL (label), treeview);

	gtk_widget_set_no_show_all (root_warning_label, TRUE);
	widget_set_best_visual (preview);

	if (! is_program_in_path (GPM_COMMAND))
	{
		gtk_widget_set_no_show_all (gpm_button, TRUE);
		gtk_widget_hide (gpm_button);
	}

	screensaver_settings = g_settings_new (GSETTINGS_SCHEMA);
	g_signal_connect (screensaver_settings,
	                  "changed",
	                  G_CALLBACK (key_changed_cb),
	                  NULL);

	session_settings = g_settings_new (SESSION_SETTINGS_SCHEMA);
	g_signal_connect (session_settings,
	                  "changed::" KEY_IDLE_DELAY,
	                  G_CALLBACK (key_changed_cb),
	                  NULL);

	lockdown_settings = g_settings_new (LOCKDOWN_SETTINGS_SCHEMA);
	g_signal_connect (lockdown_settings,
	                  "changed::" KEY_LOCK_DISABLE,
	                  G_CALLBACK (key_changed_cb),
	                  NULL);

	activate_delay = config_get_activate_delay (&is_writable);
	ui_set_delay ("activate_delay_hscale", activate_delay);
	if (! is_writable)
	{
		gtk_widget_set_sensitive (activate_delay_hscale, FALSE);
	}
	g_signal_connect (activate_delay_hscale, "format-value",
	                  G_CALLBACK (format_value_callback_time), NULL);

	activate_delay = config_get_lock_delay (&is_writable);
	ui_set_delay ("lock_delay_hscale", activate_delay);
	if (! is_writable)
	{
		gtk_widget_set_sensitive (lock_delay_hscale, FALSE);
	}
	g_signal_connect (lock_delay_hscale, "format-value",
	                  G_CALLBACK (format_value_callback_time), NULL);

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (lock_checkbox), config_get_lock (&is_writable));
	if (! is_writable)
	{
		gtk_widget_set_sensitive (lock_checkbox, FALSE);
	}
	g_signal_connect (lock_checkbox, "toggled",
	                  G_CALLBACK (lock_checkbox_toggled), NULL);

	char *path = g_settings_get_string (screensaver_settings, "picture-filename");
	gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (picture_filename), path);
	g_free (path);
	gtk_file_filter_add_pixbuf_formats (GTK_FILE_FILTER (gtk_builder_get_object (builder, "picture_filefilter")));
	picture_filename_preview = gtk_image_new ();
	gtk_file_chooser_set_preview_widget (GTK_FILE_CHOOSER (picture_filename), picture_filename_preview);
	thumb_factory = mate_desktop_thumbnail_factory_new (MATE_DESKTOP_THUMBNAIL_SIZE_LARGE);
	g_signal_connect (picture_filename, "update-preview",
	                  G_CALLBACK (update_picture_filename_preview), picture_filename_preview);
	g_signal_connect (picture_filename, "selection-changed",
	                  G_CALLBACK (picture_filename_changed), NULL);

	enabled = config_get_enabled (&is_writable);
	ui_set_enabled (enabled);
	if (! is_writable)
	{
		gtk_widget_set_sensitive (enabled_checkbox, FALSE);
	}
	g_signal_connect (enabled_checkbox, "toggled",
	                  G_CALLBACK (enabled_checkbox_toggled), NULL);

	setup_list_size_constraint (list_scroller, treeview);
	gtk_widget_set_size_request (preview, 480, 300);
	gtk_window_set_icon_name (GTK_WINDOW (dialog), "preferences-desktop-screensaver");
	gtk_window_set_icon_name (GTK_WINDOW (fullscreen_preview_window), "screensaver");

	g_signal_connect (fullscreen_preview_area,
	                  "draw", G_CALLBACK (preview_on_draw),
	                  NULL);

	gtk_drag_dest_set (dialog, GTK_DEST_DEFAULT_ALL,
	                   drop_types, G_N_ELEMENTS (drop_types),
	                   GDK_ACTION_COPY | GDK_ACTION_LINK | GDK_ACTION_MOVE);

	g_signal_connect (dialog, "drag-motion",
	                  G_CALLBACK (drag_motion_cb), NULL);
	g_signal_connect (dialog, "drag-leave",
	                  G_CALLBACK (drag_leave_cb), NULL);
	g_signal_connect (dialog, "drag-data-received",
	                  G_CALLBACK (drag_data_received_cb), NULL);

	gtk_widget_show_all (dialog);

	/* Update list of themes if using random screensaver */
	mode = g_settings_get_enum (screensaver_settings, KEY_MODE);
	if (mode == GS_MODE_RANDOM) {
		gchar **list;
		list = get_all_theme_ids ();
		g_settings_set_strv (screensaver_settings, KEY_THEMES, (const gchar * const*) list);
		g_strfreev (list);
	}

	g_signal_connect (preview, "draw", G_CALLBACK (preview_on_draw), NULL);
	gs_job_set_widget (job, preview);

	if (check_is_root_user ())
	{
		setup_for_root_user ();
	}

	g_signal_connect (activate_delay_hscale, "value-changed",
	                  G_CALLBACK (activate_delay_value_changed_cb), NULL);

	g_signal_connect (lock_delay_hscale, "value-changed",
	                  G_CALLBACK (lock_delay_value_changed_cb), NULL);

	g_signal_connect (dialog, "response",
	                  G_CALLBACK (response_cb), NULL);

	g_signal_connect (preview_button, "clicked",
	                  G_CALLBACK (fullscreen_preview_start_cb),
	                  treeview);

	g_signal_connect (fullscreen_preview_close, "clicked",
	                  G_CALLBACK (fullscreen_preview_cancelled_cb), NULL);
	g_signal_connect (fullscreen_preview_previous, "clicked",
	                  G_CALLBACK (fullscreen_preview_previous_cb), NULL);
	g_signal_connect (fullscreen_preview_next, "clicked",
	                  G_CALLBACK (fullscreen_preview_next_cb), NULL);

	g_idle_add (setup_treeview_idle, NULL);
}

static void
finalize_capplet (void)
{
	g_object_unref (screensaver_settings);
	g_object_unref (session_settings);
	g_object_unref (lockdown_settings);
	g_object_unref (thumb_factory);
}

int
main (int    argc,
      char **argv)
{

#ifdef ENABLE_NLS
	bindtextdomain (GETTEXT_PACKAGE, MATELOCALEDIR);
# ifdef HAVE_BIND_TEXTDOMAIN_CODESET
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
# endif
	textdomain (GETTEXT_PACKAGE);
#endif

	gtk_init (&argc, &argv);

	job = gs_job_new ();
	theme_manager = gs_theme_manager_new ();

	init_capplet ();

	gtk_main ();

	finalize_capplet ();

	g_object_unref (theme_manager);
	g_object_unref (job);

	return 0;
}
