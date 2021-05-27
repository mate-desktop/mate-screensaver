/*
 * Set*id helper program for PAM authentication.
 *
 * It is supposed to be called from mate-screensaver
 * in order to communicate with Linux PAM as a privileged proxy.
 * The conversation messages from the PAM stack is transmitted to
 * mate-screensaver dialog via stdout and the received user replies
 * read from stdin are sent back to PAM.
 *
 * Copyright (C) 2002 SuSE Linux AG
 * Copyright (c) 2021 Paul Wolneykien <manowar@altlinux.org>.
 *
 * Based on the helper written by okir@suse.de, loosely based on
 * unix_chkpwd by Andrew Morgan.
 */

#include <security/pam_appl.h>
#include <security/_pam_macros.h>

#include <sys/types.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <pwd.h>
#include <signal.h>
#include <fcntl.h>
#include <ctype.h>
#include <limits.h>

#include "helper_proto.h"
#include "gs-auth-pam.h"

#define MAXLEN 1024

enum {
    UNIX_PASSED = 0,
    UNIX_FAILED = 1
};

static char * program_name;

/*
 * Log error messages
 */
static void
_log_err(int err, const char *format,...)
{
    va_list args;

    va_start(args, format);
    openlog(program_name, LOG_CONS | LOG_PID, LOG_AUTH);
    vsyslog(err, format, args);
    va_end(args);
    closelog();
}

static void
su_sighandler(int sig)
{
    if (sig > 0) {
        _log_err(LOG_NOTICE, "caught signal %d.", sig);
        exit(sig);
    }
}

/*
 * Setup signal handlers
 */
static void
setup_signals(void)
{
    struct sigaction action;

    memset((void *) &action, 0, sizeof(action));
    action.sa_handler = su_sighandler;
    action.sa_flags = SA_RESETHAND;
    sigaction(SIGILL, &action, NULL);
    sigaction(SIGTRAP, &action, NULL);
    sigaction(SIGBUS, &action, NULL);
    sigaction(SIGSEGV, &action, NULL);
    action.sa_handler = SIG_IGN;
    action.sa_flags = 0;
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGHUP, &action, NULL);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGQUIT, &action, NULL);
    sigaction(SIGALRM, &action, NULL);
}

static int
_converse(int num_msg, const struct pam_message **msg,
          struct pam_response **resp, void *appdata_ptr)
{
    struct pam_response *reply;
    char buf[MAXLEN];
    int num;
    int ret = PAM_SUCCESS;

    if (!(reply = malloc(sizeof(*reply) * num_msg)))
        return PAM_CONV_ERR;

    for (num = 0; num < num_msg; num++) {
        ssize_t wt, rd;
        size_t msg_len = strlen(msg[num]->msg);
        wt = write_prompt (STDOUT_FILENO,
                   pam_style_to_gs_style (msg[num]->msg_style),
                   msg[num]->msg, msg_len);
        if (wt < 0 || wt != msg_len) {
            _log_err(LOG_ERR, "error writing promt");
            ret = PAM_CONV_ERR;
            break;
        }

        rd = read_msg (STDIN_FILENO, buf, sizeof (buf));
        if (rd < 0) {
            _log_err(LOG_ERR, "error reading reply");
            ret = PAM_CONV_ERR;
            break;
        }

        reply[num].resp = malloc (rd + 1);
        if (!reply[num].resp)
            ret = PAM_BUF_ERR;
        else {
            reply[num].resp_retcode = 0;
            memcpy (reply[num].resp, buf, rd);
            reply[num].resp[rd] = '\0';
        }
    }

    if (ret != PAM_SUCCESS) {
        for (num = 0; num < num_msg; num++)
            free (reply[num].resp);
    } else
        *resp = reply;

    return ret;
}

static int
_authenticate(const char *service, const char *user)
{
    struct pam_conv conv = { _converse, NULL };
    pam_handle_t *pamh;
    int err;

    err = pam_start(service, user, &conv, &pamh);
    if (err != PAM_SUCCESS) {
        _log_err(LOG_ERR, "pam_start(%s, %s) failed (errno %d)",
                service, user, err);
        return UNIX_FAILED;
    }

    err = pam_authenticate(pamh, 0);
    if (err != PAM_SUCCESS)
        _log_err(LOG_ERR, "pam_authenticate(%s, %s): %s",
                service, user,
                pam_strerror(pamh, err));

    if (err == PAM_SUCCESS)
    {
        int err2 = pam_setcred(pamh, PAM_REFRESH_CRED);
        if (err2 != PAM_SUCCESS)
            _log_err(LOG_ERR, "pam_setcred(%s, %s): %s",
                     service, user,
                     pam_strerror(pamh, err2));
        /*
         * ignore errors on refresh credentials.
         * If this did not work we use the old once.
         */
    }

    pam_end(pamh, err);

    if (err != PAM_SUCCESS)
        return UNIX_FAILED;
    return UNIX_PASSED;
}

static char *
getuidname(uid_t uid)
{
    struct passwd *pw;
    static char username[32];

    pw = getpwuid(uid);
    if (pw == NULL)
        return NULL;

    strncpy(username, pw->pw_name, sizeof(username));
    username[sizeof(username) - 1] = '\0';

    endpwent();
    return username;
}

static int
sane_pam_service(const char *name)
{
    const char *sp;
    char path[PATH_MAX];

    if (strlen(name) > 32)
        return 0;
    for (sp = name; *sp; sp++) {
        if (!isalnum(*sp) && *sp != '_' && *sp != '-')
            return 0;
    }

    snprintf(path, sizeof(path), "/etc/pam.d/%s", name);
    return access(path, R_OK) == 0;
}

int
main(int argc, char *argv[])
{
    const char *program_name;
    char *service, *user;
    int fd;
    uid_t uid;

    uid = getuid();

    /*
     * Make sure standard file descriptors are connected.
     */
    while ((fd = open("/dev/null", O_RDWR)) <= 2)
        ;
    close(fd);

    /*
     * Get the program name
     */
    if ((program_name = strrchr(argv[0], '/')) != NULL)
        program_name++;
    else
        program_name = argv[0];

    /*
     * Catch or ignore as many signal as possible.
     */
    setup_signals();

    /*
     * Check argument list
     */
    if (argc < 2 || argc > 3) {
        _log_err(LOG_NOTICE, "Bad number of arguments (%d)", argc);
        return UNIX_FAILED;
    }

    /*
     * Get the service name and do some sanity checks on it
     */
    service = argv[1];
    if (!sane_pam_service(service)) {
        _log_err(LOG_ERR, "Illegal service name '%s'", service);
        return UNIX_FAILED;
    }

    /*
     * Discourage users messing around (fat chance)
     */
    if (isatty(STDIN_FILENO) && uid != 0) {
        _log_err(LOG_NOTICE,
            "Inappropriate use of Unix helper binary [UID=%d]",
             uid);
        fprintf(stderr,
            "This binary is not designed for running in this way\n"
            "-- the system administrator has been informed\n");
        sleep(10); /* this should discourage/annoy the user */
        return UNIX_FAILED;
    }

    /*
     * determine the caller's user name
     */
    user = getuidname(uid);
    if (argc == 3 && strcmp(user, argv[2])) {
        user = argv[2];
        /* Discourage use of this program as a
         * password cracker */
        if (uid != 0)
            sleep(5);
    }
    return _authenticate(service, user);
}
