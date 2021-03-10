/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * written by Olaf Kirch <okir@suse.de>
 * xscreensaver, Copyright (c) 1993-2004 Jamie Zawinski <jwz@jwz.org>
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation.  No representations are made about the suitability of this
 * software for any purpose.  It is provided "as is" without express or
 * implied warranty.
 */

/* The idea here is to be able to run mate-screensaver-dialog without any setuid bits.
 * Password verification happens through an external program that you feed
 * your password to on stdin.  The external command is invoked with a user
 * name argument.
 *
 * The external helper does whatever authentication is necessary.  Currently,
 * SuSE uses "unix2_chkpwd", which is a variation of "unix_chkpwd" from the
 * PAM distribution.
 *
 * Normally, the password helper should just authenticate the calling user
 * (i.e. based on the caller's real uid).  This is in order to prevent
 * brute-forcing passwords in a shadow environment.  A less restrictive
 * approach would be to allow verifying other passwords as well, but always
 * with a 2 second delay or so.  (Not sure what SuSE's "unix2_chkpwd"
 * currently does.)
 *                         -- Olaf Kirch <okir@suse.de>, 16-Dec-2003
 */

#include "config.h"

#include <stdlib.h>
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <pwd.h>
#include <errno.h>
#include <sys/wait.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "gs-auth.h"
#include "subprocs.h"

#include "../helper/helper_proto.h"
#define MAXLEN 1024

static gboolean verbose_enabled = FALSE;

GQuark
gs_auth_error_quark (void)
{
	static GQuark quark = 0;
	if (! quark)
	{
		quark = g_quark_from_static_string ("gs_auth_error");
	}

	return quark;
}

void
gs_auth_set_verbose (gboolean enabled)
{
	verbose_enabled = enabled;
}

gboolean
gs_auth_get_verbose (void)
{
	return verbose_enabled;
}

static gboolean
ext_run (const char *user,
         GSAuthMessageFunc func,
         gpointer   data)
{
        int pfd[2], r_pfd[2], status;
	pid_t pid;
        gboolean verbose = gs_auth_get_verbose ();

        if (pipe (pfd) < 0 || pipe (r_pfd) < 0)
	{
                return FALSE;
	}

	if (verbose)
	{
		g_message ("ext_run (%s, %s)",
		           PASSWD_HELPER_PROGRAM, user);
	}

	block_sigchld ();

	if ((pid = fork ()) < 0)
	{
                close (pfd [0]);
                close (pfd [1]);
                close (r_pfd [0]);
                close (r_pfd [1]);
		return FALSE;
	}

	if (pid == 0)
	{
		close (pfd [1]);
                close (r_pfd [0]);
		if (pfd [0] != 0)
		{
			dup2 (pfd [0], 0);
		}
                if (r_pfd [1] != 1)
                {
                        dup2 (r_pfd [1], 1);
                }

		/* Helper is invoked as helper service-name [user] */
		execlp (PASSWD_HELPER_PROGRAM, PASSWD_HELPER_PROGRAM, "mate-screensaver", user, NULL);
		if (verbose)
		{
			g_message ("%s: %s", PASSWD_HELPER_PROGRAM, g_strerror (errno));
		}

		exit (1);
	}

	close (pfd [0]);
        close (r_pfd [1]);

        gboolean ret = FALSE;
        while (waitpid (pid, &status, WNOHANG) == 0)
        {
                int msg_type;
                char buf[MAXLEN];
                unsigned int msg_len = MAXLEN;

                msg_type = read_prompt (r_pfd [0], buf, &msg_len);
                if (0 == msg_type) continue;
                if (msg_type < 0)
                {
                        g_message ("Error reading prompt (%d)", msg_type);
                        ret = FALSE;
                        goto exit;
                }

                char *input = NULL;
                func (msg_type, buf, &input, data);

                unsigned int input_len = input ? strlen (input) : 0;
                ssize_t wt;

                wt = write_msg (pfd [1], input, input_len);
                if (wt < 0)
		{
                        g_message ("Error writing prompt reply (%d)", wt);
                        ret = FALSE;
                        goto exit;
		}
	}

        close (pfd [1]);
        close (r_pfd [0]);
	unblock_sigchld ();

	if (! WIFEXITED (status) || WEXITSTATUS (status) != 0)
	{
                ret = FALSE;
	}
        else
                ret = TRUE;

  exit:
        return ret;
}

gboolean
gs_auth_verify_user (const char       *username,
                     const char       *display,
                     GSAuthMessageFunc func,
                     gpointer          data,
                     GError          **error)
{
        return ext_run (username, func, data);
}

gboolean
gs_auth_init (void)
{
	return TRUE;
}

gboolean
gs_auth_priv_init (void)
{
	/* Make sure the passwd helper exists */
	if (g_access (PASSWD_HELPER_PROGRAM, X_OK) < 0)
	{
		g_warning ("%s does not exist. "
		           "password authentication via "
		           "external helper will not work.",
		           PASSWD_HELPER_PROGRAM);
		return FALSE;
	}

	return TRUE;
}
