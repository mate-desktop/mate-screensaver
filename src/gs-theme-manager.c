/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2006 William Jon McCann <mccann@jhu.edu>
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
#include <stdio.h>

#include <string.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#include <glib-object.h>
#include <matemenu-tree.h>

#include "gs-theme-manager.h"
#include "gs-debug.h"

static void     gs_theme_manager_finalize   (GObject             *object);

struct _GSThemeInfo
{
	char  *name;
	char  *exec;
	char  *file_id;
	guint  refcount;
};

struct GSThemeManagerPrivate
{
	MateMenuTree *menu_tree;
};

G_DEFINE_TYPE_WITH_PRIVATE (GSThemeManager, gs_theme_manager, G_TYPE_OBJECT)

static gpointer theme_manager_object = NULL;

static const char *known_engine_locations [] =
{
	SAVERDIR,
#ifdef XSCREENSAVER_HACK_DIR
	XSCREENSAVER_HACK_DIR,
#endif
	LIBEXECDIR "/xscreensaver",
	"/usr/libexec/xscreensaver",
	"/usr/lib/xscreensaver",
	NULL
};

/* Returns the full path to the queried command */
static char *
find_command (const char *command)
{
	int i;

	if (g_path_is_absolute (command))
	{
		char *dirname;

		dirname = g_path_get_dirname (command);
		for (i = 0; known_engine_locations [i]; i++)
		{
			if (strcmp (dirname, known_engine_locations [i]) == 0)
			{
				if (g_file_test (command, G_FILE_TEST_IS_EXECUTABLE)
				        && ! g_file_test (command, G_FILE_TEST_IS_DIR))
				{
					g_free (dirname);
					return g_strdup (command);
				}
			}
		}
		g_free (dirname);
	}
	else
	{
		for (i = 0; known_engine_locations [i]; i++)
		{
			char *path;

			path = g_build_filename (known_engine_locations [i], command, NULL);

			if (g_file_test (path, G_FILE_TEST_IS_EXECUTABLE)
			        && ! g_file_test (path, G_FILE_TEST_IS_DIR))
			{
				return path;
			}

			g_free (path);
		}
	}

	return NULL;
}

static gboolean
check_command (const char *command)
{
	char *path;
	char **argv;

	g_return_val_if_fail (command != NULL, FALSE);

	g_shell_parse_argv (command, NULL, &argv, NULL);
	path = find_command (argv [0]);
	g_strfreev (argv);

	if (path != NULL)
	{
		g_free (path);
		return TRUE;
	}

	return FALSE;
}

static void
add_known_engine_locations_to_path (void)
{
	static gboolean already_added;
	int      i;
	GString *str;

	/* We only want to add the items to the path once */
	if (already_added)
	{
		return;
	}

	already_added = TRUE;

	/* TODO: set a default PATH ? */

	str = g_string_new (g_getenv ("PATH"));
	for (i = 0; known_engine_locations [i]; i++)
	{
		/* TODO: check that permissions are safe */
		if (g_file_test (known_engine_locations [i], G_FILE_TEST_IS_DIR))
		{
			g_string_append_printf (str, ":%s", known_engine_locations [i]);
		}
	}

	g_setenv ("PATH", str->str, TRUE);
	g_string_free (str, TRUE);
}

GSThemeInfo *
gs_theme_info_ref (GSThemeInfo *info)
{
	g_return_val_if_fail (info != NULL, NULL);
	g_return_val_if_fail (info->refcount > 0, NULL);

	info->refcount++;

	return info;
}

void
gs_theme_info_unref (GSThemeInfo *info)
{
	g_return_if_fail (info != NULL);
	g_return_if_fail (info->refcount > 0);

	if (--info->refcount == 0)
	{
		g_free (info->name);
		g_free (info->exec);
		g_free (info->file_id);

		g_free (info);
	}
}

const char *
gs_theme_info_get_id (GSThemeInfo *info)
{
	g_return_val_if_fail (info != NULL, NULL);

	return info->file_id;
}

const char *
gs_theme_info_get_name (GSThemeInfo *info)
{
	g_return_val_if_fail (info != NULL, NULL);

	return info->name;
}

const char *
gs_theme_info_get_exec (GSThemeInfo *info)
{
	const char *exec;

	g_return_val_if_fail (info != NULL, NULL);

	if (check_command (info->exec))
	{
		exec = info->exec;
	}
	else
	{
		exec = NULL;
	}

	return exec;
}

static GSThemeInfo *
gs_theme_info_new_from_matemenu_tree_entry (MateMenuTreeEntry *entry)
{
	GSThemeInfo *info;
	const char     *str;
	char           *pos;
	GDesktopAppInfo *ginfo;

	info = g_new0 (GSThemeInfo, 1);
	ginfo = matemenu_tree_entry_get_app_info (entry);

	info->refcount = 1;
	info->name     = g_strdup (g_app_info_get_name(G_APP_INFO(ginfo)));
	info->exec     = g_strdup (g_app_info_get_commandline(G_APP_INFO(ginfo)));

	/* remove the .desktop suffix */
	str = matemenu_tree_entry_get_desktop_file_id (entry);
	pos = g_strrstr (str, ".desktop");
	if (pos)
	{
		info->file_id = g_strndup (str, pos - str);
	}
	else
	{
		info->file_id  = g_strdup (str);
	}

	return info;
}

static GSThemeInfo *
find_info_for_id (MateMenuTree  *tree,
                  const char *id)
{
	GSThemeInfo     *info;
	MateMenuTreeDirectory *root;
	MateMenuTreeIter *iter;
	MateMenuTreeItemType type;

	root = matemenu_tree_get_root_directory (tree);
	if (root == NULL)
	{
		return NULL;
	}

	info = NULL;
	iter = matemenu_tree_directory_iter (root);
	while ((type = matemenu_tree_iter_next (iter)) != MATEMENU_TREE_ITEM_INVALID) {
		if (info == NULL && type == MATEMENU_TREE_ITEM_ENTRY) {
			MateMenuTreeEntry *entry;
			const char     *file_id;

			entry = matemenu_tree_iter_get_entry(iter);
			file_id = matemenu_tree_entry_get_desktop_file_id (entry);
			if (file_id && id && strcmp (file_id, id) == 0)
			{
				info = gs_theme_info_new_from_matemenu_tree_entry (entry);
			}
			matemenu_tree_item_unref (entry);
		}
	}
	matemenu_tree_iter_unref (iter);
	matemenu_tree_item_unref (root);
	return info;
}

GSThemeInfo *
gs_theme_manager_lookup_theme_info (GSThemeManager *theme_manager,
                                    const char     *name)
{
	GSThemeInfo *info;
	char        *id;

	g_return_val_if_fail (GS_IS_THEME_MANAGER (theme_manager), NULL);
	g_return_val_if_fail (name != NULL, NULL);

	id = g_strdup_printf ("%s.desktop", name);
	info = find_info_for_id (theme_manager->priv->menu_tree, id);
	g_free (id);

	return info;
}

static void
theme_prepend_entry (GSList         **parent_list,
                     MateMenuTreeEntry  *entry,
                     const char      *filename)
{
	GSThemeInfo *info;

	info = gs_theme_info_new_from_matemenu_tree_entry (entry);

	*parent_list = g_slist_prepend (*parent_list, info);
}

static void
make_theme_list (GSList             **parent_list,
                 MateMenuTreeDirectory  *directory,
                 const char          *filename)
{
	MateMenuTreeIter *iter;
	MateMenuTreeItemType type;

	iter = matemenu_tree_directory_iter (directory);
	while ((type = matemenu_tree_iter_next (iter)) != MATEMENU_TREE_ITEM_INVALID) {
		if (type == MATEMENU_TREE_ITEM_ENTRY) {
			MateMenuTreeEntry *item;
			item = matemenu_tree_iter_get_entry (iter);
			theme_prepend_entry (parent_list, (MateMenuTreeEntry*)item, filename);
			matemenu_tree_item_unref (item);
		}
	}
	matemenu_tree_iter_unref (iter);
	*parent_list = g_slist_reverse (*parent_list);
}

GSList *
gs_theme_manager_get_info_list (GSThemeManager *theme_manager)
{
	GSList             *l = NULL;
	MateMenuTreeDirectory *root;

	g_return_val_if_fail (GS_IS_THEME_MANAGER (theme_manager), NULL);

	root = matemenu_tree_get_root_directory (theme_manager->priv->menu_tree);

	if (root != NULL)
	{
		make_theme_list (&l, root, "mate-screensavers.menu");
		matemenu_tree_item_unref (root);
	}

	return l;
}

static void
gs_theme_manager_class_init (GSThemeManagerClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = gs_theme_manager_finalize;
}

static MateMenuTree *
get_themes_tree (void)
{
	MateMenuTree *themes_tree = NULL;
	GError *error = NULL;

	/* we only need to add the locations to the path once
	   and since this is only run once we'll do it here */
	add_known_engine_locations_to_path ();

	themes_tree = matemenu_tree_new ("mate-screensavers.menu", MATEMENU_TREE_FLAGS_NONE);
	if (!matemenu_tree_load_sync (themes_tree, &error)) {
		g_debug("Load matemenu tree got error: %s\n", error->message);
		g_error_free(error);
		g_object_unref(themes_tree);
		return NULL;
	}

	return themes_tree;
}

static void
on_applications_changed (MateMenuTree *menu_tree)
{
	GError *error = NULL;

	if (!matemenu_tree_load_sync (menu_tree, &error)) {
		g_debug ("Load matemenu tree got error: %s\n", error->message);
		g_error_free (error);
	}
}

static void
gs_theme_manager_init (GSThemeManager *theme_manager)
{
	theme_manager->priv = gs_theme_manager_get_instance_private (theme_manager);

	theme_manager->priv->menu_tree = get_themes_tree ();
	g_signal_connect (theme_manager->priv->menu_tree, "changed",
	                  G_CALLBACK (on_applications_changed),
	                  NULL);
}

static void
gs_theme_manager_finalize (GObject *object)
{
	GSThemeManager *theme_manager;

	g_return_if_fail (object != NULL);
	g_return_if_fail (GS_IS_THEME_MANAGER (object));

	theme_manager = GS_THEME_MANAGER (object);

	g_return_if_fail (theme_manager->priv != NULL);

	if (theme_manager->priv->menu_tree != NULL)
	{
		g_object_unref(theme_manager->priv->menu_tree);
	}

	G_OBJECT_CLASS (gs_theme_manager_parent_class)->finalize (object);
}

GSThemeManager *
gs_theme_manager_new (void)
{
	if (theme_manager_object)
	{
		g_object_ref (theme_manager_object);
	}
	else
	{
		theme_manager_object = g_object_new (GS_TYPE_THEME_MANAGER, NULL);
		g_object_add_weak_pointer (theme_manager_object,
		                           (gpointer *) &theme_manager_object);
	}

	return GS_THEME_MANAGER (theme_manager_object);
}
