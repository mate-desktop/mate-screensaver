dnl -*- mode: m4 -*-

AC_PREREQ(2.60)
AC_INIT([mate-screensaver],
        [1.28.0],
        [https://mate-desktop.org/])

AC_CONFIG_SRCDIR(src/mate-screensaver.c)
AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE([1.9 no-dist-gzip dist-xz check-news])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_CONFIG_HEADERS(config.h)

AM_MAINTAINER_MODE
MATE_DEBUG_CHECK([no])
MATE_COMPILE_WARNINGS

AC_PROG_CC
AM_PROG_CC_C_O
AC_STDC_HEADERS
AC_PROG_LIBTOOL
AC_CANONICAL_HOST

AC_HEADER_STDC

AC_SUBST(VERSION)

# Save flags to aclocal
ACLOCAL="$ACLOCAL $ACLOCAL_FLAGS"

AM_GNU_GETTEXT_VERSION([0.19.8])
AM_GNU_GETTEXT([external])

GETTEXT_PACKAGE=mate-screensaver
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE, "$GETTEXT_PACKAGE", [Name of default gettext domain])

dnl
dnl Enable gsettings schema macros
dnl

GLIB_GSETTINGS

# Dependencies

DBUS_REQUIRED_VERSION=0.30
GLIB_REQUIRED_VERSION=2.58.0
GTK_REQUIRED_VERSION=3.22.0
X11_REQUIRED_VERSION=1.0
LIBMATE_MENU_REQUIRED_VERSION=1.21.0
MATE_DESKTOP_REQUIRED_VERSION=1.17.0
LIBMATEKBDUI_REQUIRED_VERSION=1.17.0

AC_CHECK_HEADERS(unistd.h)
AC_CHECK_HEADERS(crypt.h sys/select.h)
AC_CHECK_FUNCS(select fcntl uname nice setpriority getcwd getwd putenv sbrk)
AC_CHECK_FUNCS(sigaction syslog realpath setrlimit)
AC_CHECK_FUNCS(getresuid)
AC_TYPE_UID_T

AC_CHECK_FUNCS([setresuid setenv unsetenv clearenv])
PKG_CHECK_MODULES(MATE_SCREENSAVER,
        x11 >= $X11_REQUIRED_VERSION
        xscrnsaver
        gtk+-3.0 >= $GTK_REQUIRED_VERSION
        dbus-glib-1 >= $DBUS_REQUIRED_VERSION
        gio-2.0 >= $GLIB_REQUIRED_VERSION
        mate-desktop-2.0 >= $MATE_DESKTOP_REQUIRED_VERSION
        libmate-menu >= $LIBMATE_MENU_REQUIRED_VERSION)
AC_SUBST(MATE_SCREENSAVER_CFLAGS)
AC_SUBST(MATE_SCREENSAVER_LIBS)

PKG_CHECK_MODULES(MATE_SCREENSAVER_DIALOG,
        gio-2.0 >= $GLIB_REQUIRED_VERSION
        gthread-2.0
        gtk+-3.0 >= $GTK_REQUIRED_VERSION
        mate-desktop-2.0 >= $MATE_DESKTOP_REQUIRED_VERSION)
AC_SUBST(MATE_SCREENSAVER_DIALOG_CFLAGS)
AC_SUBST(MATE_SCREENSAVER_DIALOG_LIBS)

PKG_CHECK_MODULES(MATE_SCREENSAVER_CAPPLET,
        gio-2.0 >= $GLIB_REQUIRED_VERSION
        gtk+-3.0 >= $GTK_REQUIRED_VERSION
        mate-desktop-2.0 >= $MATE_DESKTOP_REQUIRED_VERSION
        libmate-menu >= $LIBMATE_MENU_REQUIRED_VERSION)
AC_SUBST(MATE_SCREENSAVER_CAPPLET_CFLAGS)
AC_SUBST(MATE_SCREENSAVER_CAPPLET_LIBS)

PKG_CHECK_MODULES(MATE_SCREENSAVER_COMMAND,
        gobject-2.0 >= $GLIB_REQUIRED_VERSION
        dbus-glib-1 >= $DBUS_REQUIRED_VERSION)
AC_SUBST(MATE_SCREENSAVER_COMMAND_CFLAGS)
AC_SUBST(MATE_SCREENSAVER_COMMAND_LIBS)

AC_PATH_XTRA

ALL_X_LIBS="$X_LIBS $X_PRE_LIBS -lXext -lX11 $X_EXTRA_LIBS"
SAVER_LIBS="$ALL_X_LIBS"

AC_PATH_PROG(GLIB_GENMARSHAL, glib-genmarshal)

# GLIB_COMPILE_RESOURCES

AC_ARG_VAR([GLIB_COMPILE_RESOURCES],[glib-compile-resources bin])
AC_PATH_PROG([GLIB_COMPILE_RESOURCES],[glib-compile-resources],[])
if test -z "$GLIB_COMPILE_RESOURCES"; then
  AC_MSG_ERROR([glib-compile-resources not found])
fi

# XMLLINT

AC_ARG_VAR([XMLLINT],[xmllint bin])
AC_PATH_PROG([XMLLINT],[xmllint],[])
if test -z "$XMLLINT"; then
  AC_MSG_ERROR([xmllint not found])
fi

# Solaris requires libresolv for daemon()
case "$host" in
	*-*-solaris*)
		AC_CHECK_LIB(resolv, daemon, [MATE_SCREENSAVER_LIBS="$MATE_SCREENSAVER_LIBS -lresolv"])
	;;
esac

# Optional dependencies for the theme engines

SAVER_MODULES="gthread-2.0 gtk+-3.0 >= $GTK_REQUIRED_VERSION gobject-2.0 >= $GLIB_REQUIRED_VERSION"

PKG_CHECK_MODULES(MATE_SCREENSAVER_SAVER, $SAVER_MODULES)
AC_SUBST(MATE_SCREENSAVER_SAVER_CFLAGS)
AC_SUBST(MATE_SCREENSAVER_SAVER_LIBS)

# Find out where the session service file goes
# The sad sed hack is recomended by section 27.10 of the automake manual.
DBUS_SESSION_SERVICE_DIR=`pkg-config --variable session_bus_services_dir dbus-1 | sed -e 's,/usr/share,${datarootdir},g'`
AC_SUBST(DBUS_SESSION_SERVICE_DIR)

# Determine PAM prefix

withval=""
AC_ARG_WITH(pam-prefix,
[  --with-pam-prefix=<prefix>   specify where pam files go],[
if test x$withval != x; then
   AC_MSG_RESULT("PAM files will be installed in prefix ${withval}.")
fi])
if test x$withval != x; then
	PAM_PREFIX_UNEXPANDED="$withval"
else
	PAM_PREFIX_UNEXPANDED="$sysconfdir"
fi
PAM_PREFIX=`eval echo $PAM_PREFIX_UNEXPANDED`
AC_SUBST(PAM_PREFIX)

# Desktop entry handling

PKG_CHECK_MODULES(LIB_MATE_MENU,
        gtk+-3.0 >= $GTK_REQUIRED_VERSION)

AC_SUBST(LIB_MATE_MENU_CFLAGS)
AC_SUBST(LIB_MATE_MENU_LIBS)

dnl ---------------------------------------------------------------------------
dnl - Where should we put documentation ?
dnl ---------------------------------------------------------------------------
AC_ARG_WITH(doc-dir,
              [AS_HELP_STRING([--with-doc-dir=<dir>],
              [directory to install documentation])])
if ! test -z "$with_doc_dir"; then
   DOCDIR="$with_doc_dir/mate-screensaver-$VERSION"
else
   DOCDIR="$datadir/doc/mate-screensaver-$VERSION"
fi
AC_SUBST(DOCDIR)

dnl ---------------------------------------------------------------------------
dnl - DocBook Documentation
dnl ---------------------------------------------------------------------------
AC_ARG_ENABLE(docbook-docs, [  --enable-docbook-docs   build documentation (requires xmlto)],enable_docbook_docs=$enableval,enable_docbook_docs=no)
AC_PATH_PROG(XMLTO, xmlto, no)
AC_MSG_CHECKING([whether to build DocBook documentation])
if test x$XMLTO = xno ; then
    have_docbook=no
else
    have_docbook=yes
fi
if test x$enable_docbook_docs = xauto ; then
    if test x$have_docbook = xno ; then
        enable_docbook_docs=no
    else
        enable_docbook_docs=yes
    fi
fi
if test x$enable_docbook_docs = xyes; then
    if test x$have_docbook = xno; then
	AC_MSG_ERROR([Building DocBook docs explicitly required, but DocBook not found])
    fi
fi
AM_CONDITIONAL(DOCBOOK_DOCS_ENABLED, test x$enable_docbook_docs = xyes)
AC_MSG_RESULT(yes)

dnl ---------------------------------------------------------------------------
dnl - Some utility functions to make checking for X things easier.
dnl ---------------------------------------------------------------------------

# Like AC_CHECK_HEADER, but it uses the already-computed -I directories.
#
AC_DEFUN([AC_CHECK_X_HEADER], [
  ac_save_CPPFLAGS="$CPPFLAGS"
  if test \! -z "$includedir" ; then
    CPPFLAGS="$CPPFLAGS -I$includedir"
  fi
  CPPFLAGS="$CPPFLAGS $X_CFLAGS"
  AC_CHECK_HEADER([$1],[$2],[$3],[$4])
  CPPFLAGS="$ac_save_CPPFLAGS"])

# Like AC_TRY_COMPILE, but it uses the already-computed -I directories.
#
AC_DEFUN([AC_TRY_X_COMPILE], [
  ac_save_CPPFLAGS="$CPPFLAGS"
  if test \! -z "$includedir" ; then
    CPPFLAGS="$CPPFLAGS -I$includedir"
  fi
  CPPFLAGS="$CPPFLAGS $X_CFLAGS"
  AC_TRY_COMPILE([$1], [$2], [$3], [$4])
  CPPFLAGS="$ac_save_CPPFLAGS"])


# Like AC_CHECK_LIB, but it uses the already-computed -I and -L directories.
# Use this sparingly; it probably doesn't work very well on X programs.
#
AC_DEFUN([AC_CHECK_X_LIB], [
  ac_save_CPPFLAGS="$CPPFLAGS"
  ac_save_LDFLAGS="$LDFLAGS"
#  ac_save_LIBS="$LIBS"

  if test \! -z "$includedir" ; then
    CPPFLAGS="$CPPFLAGS -I$includedir"
  fi
  # note: $X_CFLAGS includes $x_includes
  CPPFLAGS="$CPPFLAGS $X_CFLAGS"

  if test \! -z "$libdir" ; then
    LDFLAGS="$LDFLAGS -L$libdir"
  fi
  # note: $X_LIBS includes $x_libraries
  LDFLAGS="$LDFLAGS $ALL_X_LIBS"

  AC_CHECK_LIB([$1], [$2], [$3], [$4], [$5])
  CPPFLAGS="$ac_save_CPPFLAGS"
  LDFLAGS="$ac_save_LDFLAGS"
#  LIBS="$ac_save_LIBS"
  ])


# Usage: HANDLE_X_PATH_ARG([variable_name],
#                          [--command-line-option],
#                          [descriptive string])
#
# All of the --with options take three forms:
#
#   --with-foo (or --with-foo=yes)
#   --without-foo (or --with-foo=no)
#   --with-foo=/DIR
#
# This function, HANDLE_X_PATH_ARG, deals with the /DIR case.  When it sees
# a directory (string beginning with a slash) it checks to see whether
# /DIR/include and /DIR/lib exist, and adds them to $X_CFLAGS and $X_LIBS
# as appropriate.
#
AC_DEFUN([HANDLE_X_PATH_ARG], [
   case "$[$1]" in
    yes) ;;
    no)  ;;

    /*)
     AC_MSG_CHECKING([for [$3] headers])
     d=$[$1]/include
     if test -d $d; then
       X_CFLAGS="-I$d $X_CFLAGS"
       AC_MSG_RESULT($d)
     else
       AC_MSG_RESULT(not found ($d: no such directory))
     fi

     AC_MSG_CHECKING([for [$3] libs])
     d=$[$1]/lib
     if test -d $d; then
       X_LIBS="-L$d $X_LIBS"
       AC_MSG_RESULT($d)
     else
       AC_MSG_RESULT(not found ($d: no such directory))
     fi

     # replace the directory string with "yes".
     [$1]_req="yes"
     [$1]=$[$1]_req
     ;;

    *)
     echo ""
     echo "error: argument to [$2] must be \"yes\", \"no\", or a directory."
     echo "       If it is a directory, then \`DIR/include' will be added to"
     echo "       the -I list, and \`DIR/lib' will be added to the -L list."
     exit 1
     ;;
   esac
  ])

dnl ---------------------------------------------------------------------------
dnl - Check for shaped window extension
dnl ---------------------------------------------------------------------------

have_shape=no
AC_CHECK_X_HEADER(X11/extensions/shape.h, [have_shape=yes],,
                    [#include <X11/Xlib.h>])
if test "$have_shape" = yes; then
  AC_DEFINE(HAVE_SHAPE_EXT, 1, [Define if shape extension is available])
fi

dnl ---------------------------------------------------------------------------
dnl - Check for the MIT-SCREEN-SAVER server extension.
dnl ---------------------------------------------------------------------------

have_mit=no
with_mit_req=unspecified
AC_ARG_WITH(mit-ext,
[  --with-mit-ext          Include support for the MIT-SCREEN-SAVER extension.],
  [with_mit="$withval"; with_mit_req="$withval"],[with_mit=yes])

HANDLE_X_PATH_ARG(with_mit, --with-mit-ext, MIT-SCREEN-SAVER)

if test "$with_mit" = yes; then
  AC_CHECK_X_HEADER(X11/extensions/scrnsaver.h, [have_mit=yes],,
                    [#include <X11/Xlib.h>])

  # Now check to see if it's really in the library; XF86Free-3.3 ships
  # scrnsaver.h, but doesn't include the code in libXext.a
  if test "$have_mit" = yes; then
    AC_CHECK_X_LIB(Xext, XScreenSaverRegister, [true], [have_mit=no], -lm)

    if test "$have_mit" = no; then
      # Looks like XF86Free-3.3 actually puts it in XExExt instead
      # of in Xext.
      AC_CHECK_X_LIB(XExExt, XScreenSaverRegister,
                     [have_mit=yes; SAVER_LIBS="$SAVER_LIBS -lXExExt"],
                     [true], -lX11 -lXext -lm)
    fi

    if test "$have_mit" = no; then
      # Looks like some versions of XFree86 (whichever version
      # it is that comes with RedHat Linux 2.0 -- I can't find a version
      # number) put this in Xss instead of Xext.
      AC_CHECK_X_LIB(Xss, XScreenSaverRegister,
                     [have_mit=yes; SAVER_LIBS="$SAVER_LIBS -lXss"],
                     [true], -lX11 -lXext -lm)
    fi

  if test "$have_mit" = yes; then
    AC_DEFINE(HAVE_MIT_SAVER_EXTENSION, 1, [Define if the MIT screensaver extension is available])
  fi

  fi

elif test "$with_mit" != no; then
  echo "error: must be yes or no: --with-mit-ext=$with_mit"
  exit 1
fi

dnl ---------------------------------------------------------------------------
dnl - Check for the XF86VMODE server extension (for gamma fading.)
dnl ---------------------------------------------------------------------------

have_xf86vmode=no
have_xf86gamma=no
have_xf86gamma_ramp=no
with_xf86gamma_req=unspecified
AC_ARG_WITH(xf86gamma-ext,
[  --with-xf86gamma-ext    Include support for XFree86 gamma fading.],
  [with_xf86gamma="$withval"; with_xf86gamma_req="$withval"],
  [with_xf86gamma=yes])

HANDLE_X_PATH_ARG(with_xf86gamma, --with-xf86gamma-ext, xf86gamma)

if test "$with_xf86gamma" = yes; then

  # first check for xf86vmode.h, if we haven't already
  if test "$have_xf86vmode" = yes; then
    have_xf86gamma=yes
  else
    AC_CHECK_X_HEADER(X11/extensions/xf86vmode.h, [have_xf86gamma=yes],,
                      [#include <X11/Xlib.h>])
  fi

  # if that succeeded, then check for the -lXxf86vm
  if test "$have_xf86gamma" = yes; then
    have_xf86gamma=no
    AC_CHECK_X_LIB(Xxf86vm, XF86VidModeSetGamma,
                  [have_xf86gamma=yes],
                   [true], -lXext -lX11)
  fi

  # check for the Ramp versions of the functions too.
  if test "$have_xf86gamma" = yes; then
    have_xf86gamma_ramp=no
    AC_CHECK_X_LIB(Xxf86vm, XF86VidModeSetGammaRamp,
                  [have_xf86gamma_ramp=yes],
                   [true], -lXext -lX11)
  fi

  # if those tests succeeded, then we've really got the functions.
  if test "$have_xf86gamma" = yes; then
    AC_DEFINE(HAVE_XF86VMODE_GAMMA, 1, [Define if XF86VMODE Gamma is available])
  fi

  if test "$have_xf86gamma_ramp" = yes; then
    AC_DEFINE(HAVE_XF86VMODE_GAMMA_RAMP, 1, [Define if XF86VMODE Gamma Ramp is available])
  fi

  # pull in the lib, if we haven't already
  if test "$have_xf86gamma" = yes -a "$have_xf86vmode" = no; then
    SAVER_LIBS="$SAVER_LIBS -lXxf86vm"
  fi

elif test "$with_xf86gamma" != no; then
  echo "error: must be yes or no: --with-xf86gamma-ext=$with_xf86vmode"
  exit 1
fi

dnl ---------------------------------------------------------------------------
dnl - The --enable-locking option
dnl ---------------------------------------------------------------------------

AC_ARG_ENABLE(locking,[
Screen locking options:

  --enable-locking        Compile in support for locking the display.
  --disable-locking       Do not allow locking at all.],
  [enable_locking="$enableval"],[enable_locking=yes])
if test "$enable_locking" = yes; then
  true
elif test "$enable_locking" = no; then
  AC_DEFINE(NO_LOCKING, 1, [Define if screen locking support is disabled])
else
  echo "error: must be yes or no: --enable-locking=$enable_locking"
  exit 1
fi

# Check whether to use a xscreensaver hacks configuration directory
AC_MSG_CHECKING([whether to use xscreensaver hacks configuration])
AC_ARG_WITH(xscreensaverdir, AS_HELP_STRING([--with-xscreensaverdir=dir], [Use xscreensaver hack configs found in directory],))
xscreensaverdir=
if test "x$with_xscreensaverdir" = "xyes"; then
  if test -d /usr/share/xscreensaver/config; then
    xscreensaverdir=/usr/share/xscreensaver/config
  elif test -d /etc/xscreensaver; then
    xscreensaverdir=/etc/xscreensaver
  fi
elif test "x$with_xscreensaverdir" = "x" -o "x$with_xscreensaverdir" = "xno"; then
  xscreensaverdir=
else
  xscreensaverdir="$with_xscreensaverdir"
fi

if test "x$xscreensaverdir" != "x" ; then
  AC_DEFINE_UNQUOTED(XSCREENSAVER_CONFIG_DIR, "$xscreensaverdir", [Define to the directory containing XScreensaver configuration files])
  AC_MSG_RESULT([$xscreensaverdir])
else
  AC_MSG_RESULT([no])
fi

# Path to xscreensaver hack executables
AC_ARG_WITH(xscreensaverhackdir, AS_HELP_STRING([--with-xscreensaverhackdir=dir], [Look for xscreensaver hack executables in directory],))
AC_MSG_CHECKING([for location of xscreensaver hack executables])
xscreensaverhackdir=
if test "x$with_xscreensaverhackdir" = "xyes" -o "x$with_xscreensaverhackdir" = "x"; then
  if test -d /usr/X11R6/lib/xscreensaver; then
    xscreensaverhackdir=/usr/X11R6/lib/xscreensaver
  elif test -d $prefix/libexec/xscreensaver; then
    xscreensaverhackdir=$prefix/libexec/xscreensaver
  elif test -d /usr/libexec/xscreensaver; then
    xscreensaverhackdir=/usr/libexec/xscreensaver
  elif test -d /usr/lib/xscreensaver; then
    xscreensaverhackdir=/usr/lib/xscreensaver
  elif test -d /usr/lib64/xscreensaver; then
    xscreensaverhackdir=/usr/lib64/xscreensaver
  fi
elif test "x$with_xscreensaverhackdir" != "xno"; then
  xscreensaverhackdir="$with_xscreensaverhackdir"
fi

if test "x$xscreensaverhackdir" != "x" ; then
  AC_DEFINE_UNQUOTED(XSCREENSAVER_HACK_DIR, "$xscreensaverhackdir", [Define to the directory containing XScreensaver hack executables])
  AC_MSG_RESULT([$xscreensaverhackdir])
else
  AC_MSG_RESULT([no])
  AC_MSG_WARN([could not find directory containing xscreensaver hacks])
fi

dnl ---------------------------------------------------------------------------
dnl - Check for GL
dnl ---------------------------------------------------------------------------

AC_ARG_WITH(libgl, [  --with-libgl	  build with GL support])
have_libgl=no
GL_LIBS=""

if test x$with_libgl != xno; then
  AC_CHECK_X_HEADER(GL/gl.h, have_gl=yes, have_gl=no)
  if test "$have_gl" = yes ; then
    AC_CHECK_X_HEADER(GL/glx.h, have_gl=yes, have_gl=no,
                      [#include <GL/gl.h>])
  fi

  if test "$have_gl" = yes ; then
    AC_CHECK_X_LIB(GL, glXChooseVisual, have_libgl=yes, have_libgl=no, -lm)
  fi
fi

if test "x$have_libgl" = "xyes"; then
    AC_DEFINE(HAVE_LIBGL, 1, [Define if libgl is available])
    GL_LIBS="-lGL $GL_LIBS"
fi

dnl ---------------------------------------------------------------------------
dnl - Check for bsd_auth(3) (OpenBSD)
dnl ---------------------------------------------------------------------------
have_bsdauth=no
with_bsdauth_req=unspecified
NEED_SETUID=no

case "$host" in
  *-openbsd*)
    with_bsdauth=yes
    AUTH_SCHEME=bsdauth
    NEED_SETUID=no
    if test "x$enable_locking" = "xyes"; then
      with_bsdauth_req=yes
      NEED_SETUID=yes
    fi
esac

if test "$with_bsdauth" = yes ; then
  AC_CACHE_CHECK([for BSD Authentication], ac_cv_bsdauth,
    [AC_TRY_X_COMPILE([#include <stdlib.h>
                       #include <unistd.h>
                       #include <sys/types.h>
                       #include <bsd_auth.h>],
      [int ok = auth_userokay("x", 0, "x", "x");],
      [ac_cv_bsdauth=yes],
      [ac_cv_bsdauth=no])])
  if test "$ac_cv_bsdauth" = yes; then
    have_bsdauth=yes
  fi
fi

if test "$have_bsdauth" = yes; then
  AC_DEFINE(HAVE_BSDAUTH, 1, [Define to 1 if using bsd_auth(3) authentication])
fi

AC_SUBST(NEED_SETUID)

dnl ---------------------------------------------------------------------------
dnl - Check for PAM
dnl ---------------------------------------------------------------------------

have_pam=no
AC_ARG_ENABLE(pam, AS_HELP_STRING([--enable-pam],
                   [Enable PAM support @<:@default=auto@:>@],
                   ),,enable_pam=auto)

if test "x$enable_locking" = "xyes" -a "x$enable_pam" != "xno" -a "x$have_bsdauth" = "xno"; then
AC_CHECK_LIB(pam, pam_start, have_pam=yes)
fi
if test "x$have_pam" = "xyes"; then

    AUTH_LIBS="${AUTH_LIBS} -lpam"
    AC_DEFINE(HAVE_PAM, 1, [Define if PAM support is included])

    # On Linux, sigtimedwait() is in libc; on Solaris, it's in librt.
    have_timedwait=no
    AC_CHECK_LIB(c, sigtimedwait, [have_timedwait=yes])
    if test "$have_timedwait" = no ; then
      AC_CHECK_LIB(rt, sigtimedwait, [AUTH_LIBS="${AUTH_LIBS} -lrt"])
    fi

    AC_MSG_CHECKING(how to call pam_strerror)
    AC_CACHE_VAL(ac_cv_pam_strerror_args,
     [AC_TRY_COMPILE([#include <stdio.h>
                      #include <stdlib.h>
                      #include <security/pam_appl.h>],
                     [pam_handle_t *pamh = 0;
                      char *s = pam_strerror(pamh, PAM_SUCCESS);],
                     [ac_pam_strerror_args=2],
                     [AC_TRY_COMPILE([#include <stdio.h>
                                      #include <stdlib.h>
                                      #include <security/pam_appl.h>],
                                     [char *s =
                                       pam_strerror(PAM_SUCCESS);],
                                     [ac_pam_strerror_args=1],
                                     [ac_pam_strerror_args=0])])
      ac_cv_pam_strerror_args=$ac_pam_strerror_args])
    ac_pam_strerror_args=$ac_cv_pam_strerror_args
    if test "$ac_pam_strerror_args" = 1 ; then
      AC_MSG_RESULT(one argument)
    elif test "$ac_pam_strerror_args" = 2 ; then
      AC_DEFINE(PAM_STRERROR_TWO_ARGS, 1, [Define if pam_strerror takes two arguments])
      AC_MSG_RESULT(two arguments)
    else
      AC_MSG_RESULT(unknown)
    fi

fi
AM_CONDITIONAL(HAVE_PAM, test x$have_pam = xyes)
AC_SUBST(HAVE_PAM)


#       Check for the nine billion variants of shadow passwords...

need_setuid=no

have_shadow=no
have_shadow_enhanced=no
have_shadow_adjunct=no
have_shadow_hpux=no
have_passwd_helper=no

with_shadow_req=unspecified

AC_ARG_WITH(shadow,
[  --with-shadow           Include support for shadow password authentication.],
  [with_shadow="$withval"; with_shadow_req="$withval"],[with_shadow=yes])

HANDLE_X_PATH_ARG(with_shadow, --with-shadow, shadow password)

if test "$enable_locking" = no ; then
  with_shadow_req=no
  with_shadow=no
fi


#       Check for Sun "adjunct" passwords.

if test "$with_shadow" = yes ; then
  AC_CACHE_CHECK([for Sun-style shadow passwords], ac_cv_sun_adjunct,
                 [AC_TRY_X_COMPILE([#include <stdlib.h>
                                    #include <unistd.h>
                                    #include <sys/types.h>
                                    #include <sys/label.h>
                                    #include <sys/audit.h>
                                    #include <pwdadj.h>],
                      [struct passwd_adjunct *p = getpwanam("nobody");
                       const char *pw = p->pwa_passwd;],
                      [ac_cv_sun_adjunct=yes],
                      [ac_cv_sun_adjunct=no])])
  if test "$ac_cv_sun_adjunct" = yes; then
    have_shadow_adjunct=yes
    have_shadow=yes
    need_setuid=yes
  fi
fi


#       Check for DEC and SCO so-called "enhanced" security.

if test "$with_shadow" = yes ; then
  AC_CACHE_CHECK([for DEC-style shadow passwords], ac_cv_enhanced_passwd,
                 [AC_TRY_X_COMPILE([#include <stdlib.h>
                                    #include <unistd.h>
                                    #include <sys/types.h>
                                    #include <pwd.h>
                                    #include <sys/security.h>
                                    #include <prot.h>],
                      [struct pr_passwd *p;
                       const char *pw;
                       set_auth_parameters(0, 0);
                       check_auth_parameters();
                       p = getprpwnam("nobody");
                       pw = p->ufld.fd_encrypt;],
                      [ac_cv_enhanced_passwd=yes],
                      [ac_cv_enhanced_passwd=no])])
  if test $ac_cv_enhanced_passwd = yes; then
    have_shadow_enhanced=yes
    have_shadow=yes
    need_setuid=yes

    # On SCO, getprpwnam() is in -lprot (which uses nap() from -lx)
    # (I'm told it needs -lcurses too, but I don't understand why.)
    # But on DEC, it's in -lsecurity.
    #
    AC_CHECK_LIB(prot, getprpwnam,
                 [AUTH_LIBS="$AUTH_LIBS -lprot -lcurses -lx"],
                 [AC_CHECK_LIB(security, getprpwnam,
                               [AUTH_LIBS="$AUTH_LIBS -lsecurity"])],
                 [-lx])
  fi
fi

#       Check for HP's entry in the "Not Invented Here" Sweepstakes.

if test "$with_shadow" = yes ; then
  AC_CACHE_CHECK([for HP-style shadow passwords], ac_cv_hpux_passwd,
                 [AC_TRY_X_COMPILE([#include <stdlib.h>
                                    #include <unistd.h>
                                    #include <sys/types.h>
                                    #include <pwd.h>
                                    #include <hpsecurity.h>
                                    #include <prot.h>],
                      [struct s_passwd *p = getspwnam("nobody");
                       const char *pw = p->pw_passwd;],
                      [ac_cv_hpux_passwd=yes],
                      [ac_cv_hpux_passwd=no])])
  if test "$ac_cv_hpux_passwd" = yes; then
    have_shadow_hpux=yes
    have_shadow=yes
    need_setuid=yes

    # on HPUX, bigcrypt is in -lsec
    AC_CHECK_LIB(sec, bigcrypt, [AUTH_LIBS="$AUTH_LIBS -lsec"])
  fi
fi


#       Check for FreeBSD-style shadow passwords.
#
#       On FreeBSD, getpwnam() and friends work just like on non-shadow-
#       password systems -- except you only get stuff in the pw_passwd field
#       if the running program is setuid.  So, guess that we've got this
#       lossage to contend with if /etc/master.passwd exists, and default to
#       a setuid installation.

if test "$with_shadow" = yes ; then
  AC_CACHE_CHECK([for FreeBSD-style shadow passwords], ac_cv_master_passwd,
                 [if test -f /etc/master.passwd ; then
                    ac_cv_master_passwd=yes
                  else
                    ac_cv_master_passwd=no
                  fi])
  if test "$ac_cv_master_passwd" = yes; then
    need_setuid=yes
  fi
fi


#       Check for "traditional" shadow passwords.

if test "$with_shadow" = yes ; then
  AC_CACHE_CHECK([for generic shadow passwords], ac_cv_shadow,
                 [AC_TRY_X_COMPILE([#include <stdlib.h>
                                    #include <unistd.h>
                                    #include <sys/types.h>
                                    #include <pwd.h>
                                    #include <shadow.h>],
                      [struct spwd *p = getspnam("nobody");
                       const char *pw = p->sp_pwdp;],
                      [ac_cv_shadow=yes],
                      [ac_cv_shadow=no])])
  if test "$ac_cv_shadow" = yes; then
    have_shadow=yes
    need_setuid=yes

    # On some systems (UnixWare 2.1), getspnam() is in -lgen instead of -lc.
    have_getspnam=no
    AC_CHECK_LIB(c, getspnam, [have_getspnam=yes])
    if test "$have_getspnam" = no ; then
      AC_CHECK_LIB(gen, getspnam,
                   [have_getspnam=yes; AUTH_LIBS="$AUTH_LIBS -lgen"])
    fi
  fi
fi


#       Check for other libraries needed for non-shadow passwords.

if test "$enable_locking" = yes ; then

  # On some systems (UnixWare 2.1), crypt() is in -lcrypt instead of -lc.
  have_crypt=no
  AC_CHECK_LIB(c, crypt, [have_crypt=yes])
  if test "$have_crypt" = no ; then
    AC_CHECK_LIB(crypt, crypt,
                 [have_crypt=yes; AUTH_LIBS="${AUTH_LIBS} -lcrypt"])
  fi
fi

# Most of the above shadow mechanisms will have set need_setuid to yes,
# if they were found.  But, on some systems, we need setuid even when
# using plain old vanilla passwords.
#
if test "$enable_locking" = yes ; then
  case "$host" in
    *-hpux* | *-aix* | *-netbsd* | *-freebsd* | *-openbsd* )
      need_setuid=yes
    ;;
  esac
fi

if test "$have_shadow_adjunct" = yes ; then
  AC_DEFINE(HAVE_ADJUNCT_PASSWD, 1, [Define if system uses adjunct shadow passwords])
elif test "$have_shadow_enhanced" = yes ; then
  AC_DEFINE(HAVE_ENHANCED_PASSWD, 1, [Define if system uses enhanced shadow passwords])
elif test "$have_shadow_hpux" = yes ; then
  AC_DEFINE(HAVE_HPUX_PASSWD, 1, [Define if system uses HPUX shadow passwords])
elif test "$have_shadow" = yes ; then
  AC_DEFINE(HAVE_SHADOW_PASSWD, 1, [Define if system uses traditional shadow passwords])
fi

#       Check for external password helper
#       On SuSE, instead of having xscreensaver be a setuid program, they
#       fork an external program that takes the password on stdin, and
#       returns true if that password is a valid one.  Then only that
#       smaller program needs to be setuid.
#
#       (Note that this external program is not a GUI: the GUI is still
#       all in xscreensaver itself; the external program just does auth.)

have_passwd_helper=no
builtin_helper=no

AC_ARG_WITH(passwd-helper,
[  --with-passwd-helper    Include support for an external password
                          verification helper program.],
  [with_passwd_helper="$withval"],[with_passwd_helper=no])
# no HANDLE_X_PATH_ARG for this one

if test "$enable_locking" = no ; then
  with_passwd_helper=no
  builtin_helper=no
fi

case "$with_passwd_helper" in
  ""|no) : ;;
  /*)
    AC_DEFINE_UNQUOTED(PASSWD_HELPER_PROGRAM, "$with_passwd_helper", [Full pathname of password helper application])
    builtin_helper=no
    have_passwd_helper=yes;;
  *)
    echo "error: --with-passwd-helper needs full pathname of helper (not '$with_passwd_helper')." >&2
    exit 1
esac

if test "$need_setuid" = yes -a "$have_pam" != yes ; then
  NEED_SETUID=yes
else
  NEED_SETUID=no
fi
AC_SUBST(NEED_SETUID)

dnl ---------------------------------------------------------------------------
dnl  Authentication scheme
dnl ---------------------------------------------------------------------------

AC_ARG_ENABLE(authentication-scheme,
  [  --enable-authentication-scheme=[auto/pam/helper/pwent/bsdauth]  Choose a specific
                          authentication scheme [default=auto]],,
  enable_authentication_scheme=auto)

AUTH_SCHEME="auth-pam"

if test x$enable_authentication_scheme = xpam -a x$have_pam = xno ; then
	AC_MSG_ERROR(PAM support requested but not available)
fi
if test x$enable_authentication_scheme = xhelper -a x$have_passwd_helper = xno ; then
    builtin_helper=yes
    have_passwd_helper=yes
    AC_DEFINE_UNQUOTED(PASSWD_HELPER_PROGRAM, [PKGLIBEXECDIR "/mate-screensaver-pam-helper"], [Full pathname of password helper application])
fi
if test x$enable_authentication_scheme = xbsdauth -a x$have_bsdauth = xno ; then
	AC_MSG_ERROR(bsd_auth(3) support requested but not available)
fi

if test x$enable_authentication_scheme = xpam ; then
        AUTH_SCHEME="pam"
elif test x$enable_authentication_scheme = xhelper ; then
        AUTH_SCHEME="helper"
elif test x$enable_authentication_scheme = xpwent ; then
        AUTH_SCHEME="pwent"
elif test x$enable_authentication_scheme = xbsdauth ; then
	AUTH_SCHEME="bsdauth"
elif test x$enable_authentication_scheme = xauto ; then
	if test x$have_pam != xno ; then
		AUTH_SCHEME="pam"
	elif test x$have_passwd_helper != xno ; then
		AUTH_SCHEME="helper"
	elif test x$have_bsdauth != xno ; then
		AUTH_SCHEME="bsdauth"
	else
		AUTH_SCHEME="pwent"
        fi
else
	AC_MSG_ERROR(Unknown authentication scheme)
fi

AC_SUBST(AUTH_SCHEME)
AM_CONDITIONAL(HAVE_PASSWD_HELPER, test x$have_passwd_helper = xyes)
AM_CONDITIONAL(BUILTIN_HELPER, test x$builtin_helper = xyes)
AC_SUBST(HAVE_PASSWD_HELPER)

dnl ---------------------------------------------------------------------------
dnl ConsoleKit
dnl ---------------------------------------------------------------------------

AC_ARG_WITH(console-kit,
            AS_HELP_STRING([--with-console-kit],
            [Add ConsoleKit support]),,
            with_console_kit=auto)

use_console_kit=no
if test "x$with_console_kit" != "xno" ; then
    use_console_kit=yes
    AC_DEFINE(WITH_CONSOLE_KIT, 1, [ConsoleKit support])
fi
AM_CONDITIONAL(WITH_CONSOLE_KIT, test x$use_console_kit = xyes)
AC_SUBST(WITH_CONSOLE_KIT)

dnl ---------------------------------------------------------------------------
dnl systemd
dnl ---------------------------------------------------------------------------

AC_ARG_WITH(systemd,
            AS_HELP_STRING([--with-systemd],
            [Add systemd support]),
            [with_systemd=$withval], [with_systemd=auto])

PKG_CHECK_MODULES(SYSTEMD, [libsystemd], [have_systemd=yes],
                  [PKG_CHECK_MODULES(SYSTEMD, [libsystemd-login],
                  [have_systemd=yes], [have_systemd=no])])

if test "x$with_systemd" = "xauto" ; then
        if test x$have_systemd = xno ; then
                use_systemd=no
        else
                use_systemd=yes
        fi
else
	use_systemd=$with_systemd
fi

if test "x$use_systemd" = "xyes"; then
        if test "x$have_systemd" = "xno"; then
                AC_MSG_ERROR([Systemd support explicitly required, but systemd not found])
        fi

        AC_DEFINE(WITH_SYSTEMD, 1, [systemd support])
fi
AC_SUBST(SYSTEMD_CFLAGS)
AC_SUBST(SYSTEMD_LIBS)

dnl ====================================================================
dnl Check for elogind
dnl
dnl elogind is not officially supported, but since it's a subset of
dnl systemd, it should work out of the box
dnl ====================================================================

AC_ARG_WITH(elogind,
            AS_HELP_STRING([--with-elogind],
            [Use libelogind instead of libsystemd-login]),
            [with_elogind=$withval], [with_elogind=no])

use_elogind=no
if test "x$with_elogind" = "xyes"; then
    PKG_CHECK_MODULES(LIBELOGIND,[libelogind], [use_elogind=yes], [use_elogind=no])

    if test "x$use_elogind" = "xyes"; then
        AC_DEFINE([WITH_ELOGIND], 1, [elogind support])
        AC_DEFINE([WITH_SYSTEMD], 1, [Define to 1 to reduce ifdefs since elogind is a drop-in replacement for systemd])
        AC_SUBST(LIBELOGIND_CFLAGS)
        AC_SUBST(LIBELOGIND_LIBS)
    fi
fi
AM_CONDITIONAL(WITH_ELOGIND, [test "x$use_elogind" = "xyes"])
AC_SUBST(WITH_ELOGIND)

dnl ---------------------------------------------------------------------------
dnl libmatekbd
dnl ---------------------------------------------------------------------------

have_libmatekbdui=no
AC_ARG_WITH(kbd-layout-indicator,[  --without-kbd-layout-indicator         disable keyboard layout indicator],
            [with_kbd_layout_indicator="$withval"],[with_kbd_layout_indicator=yes])
if test x$with_kbd_layout_indicator != xno; then
  PKG_CHECK_MODULES(LIBMATEKBDUI, libmatekbdui >= $LIBMATEKBDUI_REQUIRED_VERSION, have_libmatekbdui=yes, have_libmatekbdui=no)
fi
if test "x$have_libmatekbdui" = "xyes"; then
  AC_SUBST(LIBMATEKBDUI_CFLAGS)
  AC_SUBST(LIBMATEKBDUI_LIBS)
  AC_DEFINE(WITH_KBD_LAYOUT_INDICATOR, 1, [Define if keyboard layout indicator should be built])
fi

dnl ---------------------------------------------------------------------------
dnl libnotify
dnl ---------------------------------------------------------------------------

have_libnotify=no
AC_ARG_WITH(libnotify,[  --without-libnotify         disable libnotify support])
if test x$with_libnotify != xno; then
	PKG_CHECK_MODULES(LIBNOTIFY, libnotify > 0.7.0, have_libnotify=yes, have_libnotify=no)
fi
if test "x$have_libnotify" = "xyes"; then
  AC_SUBST(LIBNOTIFY_CFLAGS)
  AC_SUBST(LIBNOTIFY_LIBS)
  AC_DEFINE(WITH_LIBNOTIFY, 1, [Define for libnotify support])
fi

dnl ---------------------------------------------------------------------------
dnl glib headers for pam-helper
dnl ---------------------------------------------------------------------------

PKG_CHECK_MODULES(PAM_HELPER,
	glib-2.0 >= $GLIB_REQUIRED_VERSION)
AC_SUBST(PAM_HELPER_CFLAGS)

dnl ---------------------------------------------------------------------------
dnl Finish
dnl ---------------------------------------------------------------------------

AC_SUBST(AUTH_LIBS)
AC_SUBST(SAVER_LIBS)
AC_SUBST(GL_LIBS)


REAL_PREFIX=
if test "x$prefix" = "xNONE"; then
  REAL_PREFIX=$ac_default_prefix
else
  REAL_PREFIX=$prefix
fi

## temporarily change prefix and exec_prefix
old_prefix=$prefix
prefix=$REAL_PREFIX

if test "x$exec_prefix" = xNONE ; then
   REAL_EXEC_PREFIX=$REAL_PREFIX
else
   REAL_EXEC_PREFIX=$exec_prefix
fi
old_exec_prefix=$exec_prefix
exec_prefix=$REAL_EXEC_PREFIX

## eval everything
LOCALSTATEDIR_TMP="$localstatedir"
EXPANDED_LOCALSTATEDIR=`eval echo $LOCALSTATEDIR_TMP`
AC_SUBST(EXPANDED_LOCALSTATEDIR)

SYSCONFDIR_TMP="$sysconfdir"
EXPANDED_SYSCONFDIR=`eval echo $SYSCONFDIR_TMP`
AC_SUBST(EXPANDED_SYSCONFDIR)

BINDIR_TMP="$bindir"
EXPANDED_BINDIR=`eval echo $BINDIR_TMP`
AC_SUBST(EXPANDED_BINDIR)

LIBDIR_TMP="$libdir"
EXPANDED_LIBDIR=`eval echo $LIBDIR_TMP`
AC_SUBST(EXPANDED_LIBDIR)

DATADIR_TMP="$datadir"
EXPANDED_DATADIR=`eval echo $DATADIR_TMP`
AC_SUBST(EXPANDED_DATADIR)

## put prefix and exec_prefix back
prefix=$old_prefix
exec_prefix=$old_exec_prefix

#
# Enable Debug Log
#
if test "$ax_enable_debug" = "yes"; then
  DEBUG_CFLAGS="-DG_ENABLE_DEBUG"
else
  if test "x$ax_enable_debug" = "xno"; then
    DEBUG_CFLAGS="-DG_DISABLE_ASSERT -DG_DISABLE_CHECKS"
  else
    DEBUG_CFLAGS=""
  fi
fi
AC_SUBST(DEBUG_CFLAGS)

# Flags

AC_SUBST(CFLAGS)
AC_SUBST(CPPFLAGS)
AC_SUBST(LDFLAGS)

# Saver engine stuff
SLIDESHOW_COSMOS_DIR="${EXPANDED_DATADIR}/backgrounds/cosmos"

AC_SUBST(SLIDESHOW_COSMOS_DIR)

FLOATERS_FOOT_LOGO_IMAGE="${EXPANDED_DATADIR}/pixmaps/mate-logo-white.svg"
AC_SUBST(FLOATERS_FOOT_LOGO_IMAGE)
FLOATERS_FOOT_GNOME_LOGO_IMAGE="${EXPANDED_DATADIR}/pixmaps/gnome-logo-white.svg"
AC_SUBST(FLOATERS_FOOT_GNOME_LOGO_IMAGE)

# Menu stuff

MATE_SCREENSAVER_THEMES_DIR="${EXPANDED_DATADIR}/applications/screensavers"
AC_SUBST(MATE_SCREENSAVER_THEMES_DIR)

# Special directories
# Used for .pc.in file

privlibexecdir='${libexecdir}'/mate-screensaver
AC_SUBST(privlibexecdir)

privdatadir='${datadir}'/mate-screensaver
AC_SUBST(privdatadir)

themesdir='${datadir}'/applications/screensavers
AC_SUBST(themesdir)


# Files

AC_CONFIG_FILES([
Makefile
po/Makefile.in
src/Makefile
src/mate-screensaver.desktop.in
data/Makefile
data/mate-screensavers.menu
data/mate-screensaver.pc
data/org.mate.ScreenSaver.service
data/images/Makefile
data/images/cosmos/Makefile
savers/Makefile
doc/Makefile
doc/mate-screensaver.xml
helper/Makefile
])
AC_OUTPUT

echo "
Configure summary:

    ${PACKAGE_STRING}
    `echo $PACKAGE_STRING | sed "s/./=/g"`

    prefix .......................: ${prefix}
    exec_prefix ..................: ${exec_prefix}
    libdir .......................: ${EXPANDED_LIBDIR}
    bindir .......................: ${EXPANDED_BINDIR}
    sysconfdir ...................: ${EXPANDED_SYSCONFDIR}
    localstatedir ................: ${EXPANDED_LOCALSTATEDIR}
    datadir ......................: ${EXPANDED_DATADIR}
    PAM prefix ...................: ${PAM_PREFIX}
    source code location .........: ${srcdir}
    compiler .....................: ${CC}
    cflags .......................: ${CFLAGS}
    Warning flags ................: ${WARN_CFLAGS}
    Base libs ....................: ${MATE_SCREENSAVER_LIBS}
    Extension libs ...............: ${SAVER_LIBS}
    Maintainer mode ..............: ${USE_MAINTAINER_MODE}
    Docs enabled .................: ${enable_docbook_docs}

    GL ...........................: ${have_libgl}

    Screen locking enabled .......: ${enable_locking}
    Show keyboard indicator ......: ${with_kbd_layout_indicator}
    systemd support ..............: ${use_systemd}
    elogind support ..............: ${use_elogind}
    ConsoleKit support ...........: ${use_console_kit}
    libnotify support ............: ${have_libnotify}
    PAM support ..................: ${have_pam}
    bsd_auth(3) support ..........: ${have_bsdauth}
    Have shadow passwords ........: ${have_shadow}
    Have adjunct shadow ..........: ${have_shadow_adjunct}
    Have enhanced shadow .........: ${have_shadow_enhanced}
    Have HPUX shadow .............: ${have_shadow_hpux}
    Have password helper .........: ${have_passwd_helper}
    Authentication scheme ........: ${AUTH_SCHEME}"

if test "x$need_setuid" = "xyes" -a "x$have_pam" != "xyes" ; then
echo \
"    Need setuid dialog ...........: yes
"
else
echo \
"    Need setuid dialog ...........: no
"
fi


echo ""
