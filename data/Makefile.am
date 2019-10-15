SUBDIRS =			\
	images

NULL =

man_MANS = 				\
	mate-screensaver.1		\
	mate-screensaver-command.1	\
	mate-screensaver-preferences.1	\
	$(NULL)

desktopdir = $(datadir)/applications
desktop_in_files = mate-screensaver-preferences.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

$(desktop_DATA): $(desktop_in_files)
	$(AM_V_GEN) $(MSGFMT) --desktop --template $< -d $(top_srcdir)/po -o $@

directorydir = $(datadir)/desktop-directories
directory_in_files =                            \
	mate-screensaver.directory.desktop.in		\
	$(NULL)
directory_DATA = $(directory_in_files:.directory.desktop.in=.directory)

$(directory_DATA): $(directory_in_files)
	$(AM_V_GEN) $(MSGFMT) --desktop --template $< -d $(top_srcdir)/po -o $@

menudir = $(sysconfdir)/xdg/menus
menu_DATA =					\
	mate-screensavers.menu			\
	$(NULL)

gtkbuilderdir = $(pkgdatadir)
gtkbuilder_DATA = 					\
	lock-dialog-default.ui		\
	$(NULL)

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = mate-screensaver.pc

dbussessionservicedir = $(DBUS_SESSION_SERVICE_DIR)
dbussessionservice_DATA = org.mate.ScreenSaver.service

@GSETTINGS_RULES@

gsettingsschema_in_files = org.mate.screensaver.gschema.xml.in
gsettings_SCHEMAS = $(gsettingsschema_in_files:.xml.in=.xml)

EXTRA_DIST = 				\
	$(gsettingsschema_in_files)	\
	$(desktop_in_files)		\
	$(directory_in_files)		\
	$(gtkbuilder_DATA)		\
	$(man_MANS)			\
	mate-screensaver		\
	mate-screensaver.pc.in		\
	mate-screensaver-preferences.ui \
	org.mate.screensaver.gresource.xml \
	org.mate.ScreenSaver.service.in \
	xscreensaver-config.xsl		\
	migrate-xscreensaver-config.sh	\
	$(NULL)

DISTCLEANFILES = 			\
	$(gsettings_SCHEMAS)	\
	$(desktop_DATA)			\
	$(directory_DATA)		\
	$(NULL)

MAINTAINERCLEANFILES =			\
	*~				\
	Makefile.in     \
    $(gsettings_SCHEMAS:.xml=.valid)

install-data-hook:
	@system=`uname`; \
	if test -f /usr/include/security/pam_appl.h; then \
	  if test '!' -d $(DESTDIR)$(PAM_PREFIX)/pam.d; then \
		$(mkinstalldirs) $(DESTDIR)$(PAM_PREFIX)/pam.d; \
		chmod 755 $(DESTDIR)$(PAM_PREFIX)/pam.d; \
	   fi; \
	   if test $$system = Linux; then \
		if test '!' -f $(DESTDIR)$(PAM_PREFIX)/pam.d/mate-screensaver; then \
		     $(INSTALL_DATA) mate-screensaver $(DESTDIR)$(PAM_PREFIX)/pam.d/mate-screensaver; \
		fi; \
		if test "x$(PAM_PREFIX)" != "x/etc/pam.d"; then \
	             echo "***" ; \
	             echo "***  Warning: mate-screensaver has been compiled with support for" ; \
	             echo "***           Pluggable Authentication Modules (PAM).  However, you may need to" ; \
	             echo "***           manually install $(PAM_PREFIX)/pam.d/mate-screensaver" ; \
	             echo "***           to /etc/pam.d/mate-screensaver.  Otherwise, unlocking might" ; \
	             echo "***           not work." ; \
	             echo "***" ; \
	             echo "***           Note: If you are using SuSE/Novell you may have to modify this file." ; \
	             echo "***" ; \
		fi; \
	   fi; \
	   if test $$system = SunOS; then \
	        echo "***" ; \
	        echo "***  Warning: mate-screensaver has been compiled with support for" ; \
	        echo "***           Pluggable Authentication Modules (PAM).  However, you" ; \
	        echo "***           need to manually add authentication for mate-screensaver in" ; \
	        echo "***           $(PAM_PREFIX)/pam.conf or /etc/pam.conf." ; \
	        echo "***           Otherwise, unlocking might not work." ; \
	        echo "***" ; \
	   fi; \
	fi
