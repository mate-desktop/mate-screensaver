/* Part of xscreensaver.
 * Copyright (c) 2019 Paul Wolneykien <manowar@altlinux.org>
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation.  No representations are made about the suitability of this
 * software for any purpose.  It is provided "as is" without express or
 * implied warranty.
 */

/* Provides functions for two-way communication between the screensaver
 * and the helper program. The idea of helper program is to be able to
 * run mate-screensaver-dialog without any setuid bits.
 */

#ifndef __HELPER_PROTO_H
#define __HELPER_PROTO_H

#include "config.h"

#include <stdlib.h>
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#define HELPER_IO_ERR           -1

#define HELPER_LENGTH_READ_ERR  -2
#define HELPER_TOO_LONG_ERR     -3
#define HELPER_MSG_READ_ERR     -4
#define HELPER_TYPE_READ_ERR    -5

ssize_t read_msg (int fd, char *buf, size_t length);
int read_prompt (int fd, char *buf, size_t *length);

#define HELPER_LENGTH_WRITE_ERR -6
#define HELPER_MSG_WRITE_ERR    -7
#define HELPER_TYPE_WRITE_ERR   -8

ssize_t write_msg (int fd, const void *buf, size_t length);
int write_prompt (int fd, int msg_type, const void *buf, size_t length);

#endif /* __HELPER_PROTO_H */
