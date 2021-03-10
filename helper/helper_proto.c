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

#include "config.h"

#include <stdlib.h>
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include "helper_proto.h"

static ssize_t
read_all (int fd, void *buf, size_t count)
{
    ssize_t rd, t_rd = 0;

    if (0 == count)
        return 0;

    while (t_rd < count)
    {
        rd = read (fd, buf + t_rd, count - t_rd);
        if (0 == rd)
            break;
        if (rd < 0)
            return rd;
        t_rd += rd;
    }

    return t_rd;
}

ssize_t
read_msg (int fd, char *buf, size_t length)
{
    size_t msg_len;
    ssize_t rd;

    rd = read_all (fd, &msg_len, sizeof msg_len);
    if (rd < 0)
        return HELPER_IO_ERR;
    if (rd > 0 && rd != sizeof msg_len)
        return HELPER_LENGTH_READ_ERR;

    if (msg_len >= length)
        return HELPER_TOO_LONG_ERR;

    if (msg_len > 0)
    {
        rd = read_all (fd, buf, msg_len);
        if (rd < 0)
            return HELPER_IO_ERR;
        if (rd != msg_len)
            return HELPER_MSG_READ_ERR;
    }
    else
        rd = 0;
    buf[rd] = '\0';

    return rd;
}

int
read_prompt (int fd, char *buf, size_t *length)
{
    int msg_type, rd;

    rd = read_all (fd, &msg_type, sizeof msg_type);
    if (0 == rd)
        return 0;
    if (rd < 0)
        return HELPER_IO_ERR;
    if (rd > 0 && rd != sizeof msg_type)
        return HELPER_TYPE_READ_ERR;

    rd = read_msg (fd, buf, *length);
    if (rd < 0)
        return rd;

    *length = rd;
    return msg_type;
}

static ssize_t
write_all (int fd, const void *buf, size_t count)
{
    ssize_t wt, t_wt = 0;

    if (0 == count)
        return 0;

    while (t_wt < count)
    {
        wt = write (fd, buf + t_wt, count - t_wt);
        if (0 == wt)
            break;
        if (wt < 0)
            return wt;
        t_wt += wt;
    }

    return t_wt;
}

ssize_t
write_msg (int fd, const void *buf, size_t length)
{
    ssize_t wt;

    wt = write_all (fd, &length, sizeof length);
    if (wt < 0)
        return HELPER_IO_ERR;
    if (wt > 0 && wt != sizeof length)
        return HELPER_LENGTH_WRITE_ERR;

    if (length > 0)
    {
        wt = write_all (fd, buf, length);
        if (wt < 0)
            return HELPER_IO_ERR;
        if (wt != length)
            return HELPER_MSG_WRITE_ERR;
    }
    else
        wt = 0;

    return wt;
}

int
write_prompt (int fd, int msg_type, const void *buf, size_t length)
{
    ssize_t wt;

    wt = write_all (fd, &msg_type, sizeof msg_type);
    if (wt < 0)
        return HELPER_IO_ERR;
    if (wt > 0 && wt != sizeof msg_type)
        return HELPER_TYPE_WRITE_ERR;

    wt = write_msg (fd, buf, length);

    return wt;
}
