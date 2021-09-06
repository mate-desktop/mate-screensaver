/* Part of mate-screensaver.
 *
 * Copyright (c) 2019-2021 Paul Wolneykien <manowar@altlinux.org>
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
