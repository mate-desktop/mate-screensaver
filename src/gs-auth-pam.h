/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (c) 2019 Paul Wolneykien <manowar@altlinux.org>
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
 */

#ifndef __GS_AUTH_PAM_H
#define __GS_AUTH_PAM_H

#include "gs-auth.h"

G_BEGIN_DECLS

static inline GSAuthMessageStyle
pam_style_to_gs_style (int pam_style)
{
        GSAuthMessageStyle style;

        switch (pam_style)
        {
        case PAM_PROMPT_ECHO_ON:
                style = GS_AUTH_MESSAGE_PROMPT_ECHO_ON;
                break;
        case PAM_PROMPT_ECHO_OFF:
                style = GS_AUTH_MESSAGE_PROMPT_ECHO_OFF;
                break;
        case PAM_ERROR_MSG:
                style = GS_AUTH_MESSAGE_ERROR_MSG;
                break;
        default /* PAM_TEXT_INFO */:
                style = GS_AUTH_MESSAGE_TEXT_INFO;
        }

        return style;
}

G_END_DECLS

#endif /* __GS_AUTH_PAM_H */
