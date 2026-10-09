## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

NULL =

saverdir = $(pkglibexecdir)
themesdir = $(pkgdatadir)/themes

AM_CPPFLAGS =							\
	-I.							\
	-I$(srcdir)						\
	-DMATEMENU_I_KNOW_THIS_IS_UNSTABLE			\
	$(MATE_SCREENSAVER_CFLAGS)				\
	$(MATE_SCREENSAVER_DIALOG_CFLAGS)			\
	$(MATE_SCREENSAVER_CAPPLET_CFLAGS)			\
	$(DISABLE_DEPRECATED_CFLAGS)				\
	-DPREFIX=\""$(prefix)"\"				\
	-DBINDIR=\""$(bindir)"\"				\
	-DLIBDIR=\""$(libdir)"\"				\
	-DLIBEXECDIR=\""$(libexecdir)"\"			\
	-DDATADIR=\""$(datadir)"\"				\
	-DSYSCONFDIR=\""$(sysconfdir)"\"			\
	-DMATELOCALEDIR=\""$(datadir)/locale"\"			\
	-DPKGLIBEXECDIR=\""$(pkglibexecdir)"\"			\
	-DSAVERDIR=\""$(saverdir)"\"				\
	-DTHEMESDIR=\""$(themesdir)"\"				\
	-DGTKBUILDERDIR=\"$(pkgdatadir)\"			\
	-DMATE_SCREENSAVER_GL_HELPER_PATH=\""$(libexecdir)/mate-screensaver-gl-helper"\" \
	-DPAM_SERVICE_NAME=\""mate-screensaver"\"		\
	$(WARN_CFLAGS)						\
	$(AUTH_CFLAGS)						\
	$(DEBUG_CFLAGS)						\
	$(DBUS_CFLAGS)						\
	$(LIBMATEKBDUI_CFLAGS)					\
	$(LIBNOTIFY_CFLAGS)					\
	$(SYSTEMD_CFLAGS)					\
	$(LIBELOGIND_CFLAGS)					\
	$(WAYLAND_CLIENT_CFLAGS)				\
	$(LIBWLEMBED_CFLAGS)					\
	$(LIBWLEMBED_GTK3_CFLAGS)				\
	$(GDK_WAYLAND_CFLAGS)					\
	$(GTK_LAYER_SHELL_CFLAGS)				\
	$(NULL)

bin_PROGRAMS = \
	mate-screensaver		\
	mate-screensaver-command	\
	mate-screensaver-preferences	\
	$(NULL)

bin_SCRIPTS = \
	mate-screensaver-configure	\
	$(NULL)

libexec_PROGRAMS = \
	mate-screensaver-dialog	\
	$(NULL)

if ENABLE_X11
libexec_PROGRAMS += \
	mate-screensaver-gl-helper	\
	$(NULL)
endif

noinst_PROGRAMS = 	\
	test-passwd	\
	$(NULL)

if ENABLE_X11
noinst_PROGRAMS +=	\
	test-fade	\
	test-watcher	\
	test-window	\
	$(NULL)
endif

desktopdir = $(sysconfdir)/xdg/autostart
desktop_in_files = mate-screensaver.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)
$(desktop_DATA): $(desktop_in_files)
	$(AM_V_GEN) $(MSGFMT) --desktop --keyword= --keyword=Name --keyword=Comment --keyword=Keywords --template $< -d $(top_srcdir)/po -o $@

mate_screensaver_command_SOURCES = 	\
	mate-screensaver-command.c	\
	$(NULL)

mate_screensaver_command_LDADD =	\
	$(MATE_SCREENSAVER_COMMAND_LIBS)	\
	$(NULL)

AUTH_SOURCES = 			\
	gs-auth.h		\
	gs-auth-pam.h		\
	gs-auth-@AUTH_SCHEME@.c	\
	$(NULL)

if ENABLE_X11
test_fade_SOURCES = 			\
	test-fade.c 			\
	gs-fade.c	 		\
	gs-fade.h	 		\
	gs-debug.c			\
	gs-debug.h			\
	$(NULL)
endif

test_fade_LDADD =			\
	$(MATE_SCREENSAVER_LIBS)	\
	$(X11_LDADD)			\
	$(WAYLAND_LDADD)		\
	$(NULL)

test_passwd_SOURCES = 			\
	test-passwd.c 			\
	$(AUTH_SOURCES)			\
	setuid.c			\
	setuid.h			\
	subprocs.c			\
	subprocs.h			\
	$(NULL)

test_passwd_LDADD =			\
	$(MATE_SCREENSAVER_DIALOG_LIBS)\
	$(AUTH_LIBS)			\
	$(NULL)

if HAVE_PASSWD_HELPER
test_passwd_LDADD +=	\
	../helper/libhelper-proto.a
endif

if ENABLE_X11
X11_TEST_WATCHER_SOURCES =		\
	test-watcher.c			\
	gs-watcher.c			\
	gs-watcher.h			\
	gs-watcher-private.h		\
	gs-marshal.c 			\
	gs-marshal.h 			\
	gs-debug.c			\
	gs-debug.h			\
	$(NULL)

X11_TEST_WINDOW_SOURCES =		\
	test-window.c			\
	gs-window.c			\
	gs-window.h			\
	gs-window-private.h		\
	gs-grab.c			\
	gs-grab.h			\
	gs-marshal.c 			\
	gs-marshal.h			\
	gs-debug.c			\
	gs-debug.h			\
	subprocs.c			\
	subprocs.h			\
	$(NULL)
endif

test_watcher_SOURCES = $(X11_TEST_WATCHER_SOURCES) $(WAYLAND_TEST_WATCHER_SOURCES)
test_window_SOURCES = $(X11_TEST_WINDOW_SOURCES) $(WAYLAND_TEST_WINDOW_SOURCES)

nodist_test_watcher_SOURCES = $(WAYLAND_GEN_SOURCES)
nodist_test_window_SOURCES = $(WAYLAND_GEN_SOURCES)

test_watcher_LDADD =			\
	$(MATE_SCREENSAVER_LIBS)	\
	$(X11_LDADD)			\
	$(WAYLAND_LDADD)		\
	$(NULL)

test_window_LDADD =			\
	$(MATE_SCREENSAVER_LIBS)	\
	$(X11_LDADD)			\
	$(WAYLAND_LDADD)		\
	$(NULL)

mate_screensaver_dialog_SOURCES = 	\
	mate-screensaver-dialog.c	\
	gs-lock-plug.c			\
	gs-lock-plug.h			\
	gs-debug.c			\
	gs-debug.h			\
	setuid.c			\
	setuid.h			\
	subprocs.c			\
	subprocs.h			\
	$(AUTH_SOURCES)			\
	$(NULL)

EXTRA_mate_screensaver_dialog_SOURCES = \
	gs-auth-pam.c		\
	gs-auth-helper.c	\
	gs-auth-pwent.c		\
	gs-auth-bsdauth.c	\
	$(NULL)

mate_screensaver_dialog_LDADD =	\
	$(MATE_SCREENSAVER_DIALOG_LIBS)\
	$(X11_LDADD)			\
	$(WAYLAND_LDADD)		\
	$(AUTH_LIBS)			\
	$(LIBMATEKBDUI_LIBS)		\
	$(LIBNOTIFY_LIBS)		\
	$(NULL)

if HAVE_PASSWD_HELPER
mate_screensaver_dialog_LDADD +=	\
	../helper/libhelper-proto.a
../helper/libhelper-proto.a:
	$(MAKE) -C ../helper
endif

BUILT_SOURCES = 		\
	gs-marshal.c 		\
	gs-marshal.h		\
	$(NULL)

CLEANFILES =			\
	$(NULL)

gs-marshal.c: gs-marshal.list
	echo "#include \"gs-marshal.h\"" > $@ && \
	@GLIB_GENMARSHAL@ $< --prefix=gs_marshal --body >> $@

gs-marshal.h: gs-marshal.list
	@GLIB_GENMARSHAL@ $< --prefix=gs_marshal --header > $@

if ENABLE_WAYLAND
BUILT_SOURCES +=			\
	ext-idle-notify-client.c	\
	ext-idle-notify-client.h	\
	ext-session-lock-client.c	\
	ext-session-lock-client.h	\
	$(NULL)

CLEANFILES +=				\
	ext-idle-notify-client.c	\
	ext-idle-notify-client.h	\
	ext-session-lock-client.c	\
	ext-session-lock-client.h	\
	$(NULL)

ext-idle-notify-client.c: $(WAYLAND_PROTOCOLS_DATADIR)/staging/ext-idle-notify/ext-idle-notify-v1.xml
	$(AM_V_GEN) $(WAYLAND_SCANNER) private-code $< $@

ext-idle-notify-client.h: $(WAYLAND_PROTOCOLS_DATADIR)/staging/ext-idle-notify/ext-idle-notify-v1.xml
	$(AM_V_GEN) $(WAYLAND_SCANNER) client-header $< $@

ext-session-lock-client.c: $(WAYLAND_PROTOCOLS_DATADIR)/staging/ext-session-lock/ext-session-lock-v1.xml
	$(AM_V_GEN) $(WAYLAND_SCANNER) private-code $< $@

ext-session-lock-client.h: $(WAYLAND_PROTOCOLS_DATADIR)/staging/ext-session-lock/ext-session-lock-v1.xml
	$(AM_V_GEN) $(WAYLAND_SCANNER) client-header $< $@
endif

# The bundled libwlembed submodule is built and staged at configure time, so
# there is nothing to build here. The staged headers/libraries are already
# referenced through LIBWLEMBED_CFLAGS/LIBWLEMBED_LIBS.

if ENABLE_X11
X11_SOURCES =			\
	$(NULL)

X11_LDADD =			\
	$(SAVER_LIBS)		\
	$(NULL)
else
X11_SOURCES = $(NULL)
X11_LDADD = $(NULL)
endif

if ENABLE_WAYLAND
WAYLAND_SOURCES =		\
	gs-session-lock-manager.c	\
	gs-session-lock-manager.h	\
	$(NULL)

WAYLAND_GEN_SOURCES =		\
	ext-idle-notify-client.c	\
	ext-session-lock-client.c	\
	$(NULL)

WAYLAND_LDADD =			\
	$(WAYLAND_CLIENT_LIBS)	\
	$(LIBWLEMBED_LIBS)	\
	$(LIBWLEMBED_GTK3_LIBS)	\
	$(GTK_LAYER_SHELL_LIBS)	\
	$(NULL)

WAYLAND_TEST_WATCHER_SOURCES =	\
	$(NULL)

WAYLAND_TEST_WINDOW_SOURCES =	\
	$(NULL)
else
WAYLAND_SOURCES = $(NULL)
WAYLAND_GEN_SOURCES = $(NULL)
WAYLAND_LDADD = $(NULL)
WAYLAND_TEST_WATCHER_SOURCES = $(NULL)
WAYLAND_TEST_WINDOW_SOURCES = $(NULL)
endif

mate_screensaver_SOURCES =	\
	mate-screensaver.c	\
	mate-screensaver.h	\
	gs-monitor.c		\
	gs-monitor.h		\
	gs-listener-dbus.c	\
	gs-listener-dbus.h	\
	gs-manager.c		\
	gs-manager.h		\
	gs-window.c		\
	gs-window.h		\
	gs-watcher.c		\
	gs-watcher.h		\
	gs-watcher-private.h	\
	gs-window-private.h	\
	gs-grab.c		\
	gs-grab.h		\
	gs-prefs.c		\
	gs-prefs.h		\
	gs-theme-manager.c	\
	gs-theme-manager.h	\
	gs-job.c		\
	gs-job.h		\
	gs-debug.c		\
	gs-debug.h		\
	subprocs.c		\
	subprocs.h		\
	gs-fade.c		\
	gs-fade.h		\
	$(X11_SOURCES)		\
	$(WAYLAND_SOURCES)	\
	$(NULL)

nodist_mate_screensaver_SOURCES =	\
	gs-marshal.c		\
	$(NULL)

if ENABLE_WAYLAND
nodist_mate_screensaver_SOURCES +=	\
	ext-idle-notify-client.c	\
	ext-session-lock-client.c	\
	$(NULL)
endif

if ENABLE_X11
mate_screensaver_gl_helper_LDADD =	\
	$(MATE_SCREENSAVER_LIBS)	\
	$(GL_LIBS)			\
	$(NULL)

mate_screensaver_gl_helper_SOURCES =	\
	mate-screensaver-gl-helper.c	\
	gs-visual-gl.c			\
	gs-visual-gl.h			\
	$(NULL)
endif

mate_screensaver_LDADD =		\
	$(MATE_SCREENSAVER_LIBS)	\
	$(X11_LDADD)			\
	$(WAYLAND_LDADD)		\
	$(SYSTEMD_LIBS)			\
	$(LIBELOGIND_LIBS)		\
	$(NULL)

mate_screensaver_CPPFLAGS =			\
	$(AM_CPPFLAGS)				\
	$(WAYLAND_CLIENT_CFLAGS)		\
	$(LIBWLEMBED_CFLAGS)			\
	$(GDK_WAYLAND_CFLAGS)			\
	$(GTK_LAYER_SHELL_CFLAGS)		\
	$(NULL)

mate_screensaver_LDFLAGS = -export-dynamic

mate-screensaver-preferences-resources.h mate-screensaver-preferences-resources.c: $(srcdir)/../data/org.mate.screensaver.gresource.xml Makefile $(shell $(GLIB_COMPILE_RESOURCES) --generate-dependencies --sourcedir $(srcdir)/../data $(srcdir)/../data/org.mate.screensaver.gresource.xml)
	$(AM_V_GEN) XMLLINT=$(XMLLINT) $(GLIB_COMPILE_RESOURCES) --target $@ --sourcedir $(srcdir)/../data --generate --c-name screensaver $<

nodist_mate_screensaver_preferences_SOURCES = \
	mate-screensaver-preferences-resources.h \
	mate-screensaver-preferences-resources.c \
	$(NULL)
mate_screensaver_preferences_SOURCES =	\
	mate-screensaver-preferences.c	\
	copy-theme-dialog.c		\
	copy-theme-dialog.h		\
	gs-theme-manager.c		\
	gs-theme-manager.h		\
	gs-job.c			\
	gs-job.h			\
	gs-debug.c			\
	gs-debug.h			\
	subprocs.c			\
	subprocs.h			\
	$(NULL)

mate_screensaver_preferences_LDADD =	\
	$(MATE_SCREENSAVER_CAPPLET_LIBS)	\
	$(WAYLAND_LDADD)			\
	$(NULL)

EXTRA_DIST =				\
	debug-screensaver.sh		\
	gs-marshal.list			\
	mate-screensaver.desktop.in	\
	mate-screensaver-configure	\
	$(NULL)

CLEANFILES +=				\
	$(desktop_DATA) 		\
	mate-screensaver.desktop.in	\
	mate-screensaver-preferences-resources.h \
	mate-screensaver-preferences-resources.c \
	$(BUILT_SOURCES)

MAINTAINERCLEANFILES =			\
	*~				\
	Makefile.in

install-exec-hook:
	@if [ "x@NEED_SETUID@" = "xyes" ]; then \
	    echo "***" ; \
	    echo "***  Warning: mate-screensaver has been compiled with support for" ; \
	    if [ "x@AUTH_SCHEME@" = "xbsdauth" ]; then \
		echo "***           bsd_auth(3) and must be installed as a setuid root" ; \
	    else \
	        echo "***           shadow passwords. If your system actually uses shadow" ; \
	        echo "***           passwords then it must be installed as a setuid root" ; \
	    fi; \
	    echo "***           program in order for locking to work. To do this, you" ; \
	    echo "***           must run:" ; \
	    echo "***" ; \
	    echo "***           chown root $(DESTDIR)$(libexecdir)/mate-screensaver-dialog" ; \
	    echo "***           chmod +s $(DESTDIR)$(libexecdir)/mate-screensaver-dialog" ; \
	    echo "***" ; \
	    echo "***           For now, it will be installed non-setuid, which" ; \
	    echo "***           means that locking might not work." ; \
	    echo "***" ; \
	fi

-include $(top_srcdir)/git.mk
