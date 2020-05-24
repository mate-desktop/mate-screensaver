# This file will be processed with automake-1.7 to create Makefile.in

AUTOMAKE_OPTIONS = 1.7
ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}

NULL =

DISTCHECK_CONFIGURE_FLAGS = \
	--without-systemd \
	--enable-compile-warnings=no \
	CFLAGS='-Wno-deprecated-declarations'

SUBDIRS =			\
	po 			\
	src			\
	savers			\
	data			\
	doc			\
	$(NULL)

EXTRA_DIST =			\
	autogen.sh		\
	COPYING			\
	COPYING.LIB		\
	AUTHORS			\
	INSTALL			\
	README			\
	ChangeLog		\
	its			\
	NEWS			\
	$(NULL)

DISTCLEANFILES =		\
	$(NULL)

MAINTAINERCLEANFILES =		\
	*~			\
	compile			\
	configure		\
	INSTALL			\
	COPYING			\
	install-sh		\
	missing			\
	mkinstalldirs		\
	config.guess		\
	ltmain.sh		\
	config.sub		\
	depcomp			\
	Makefile.in		\
	config.h.*		\
	aclocal.m4		\
	acinclude.m4		\
	$(NULL)

# Build ChangeLog from GIT  history
ChangeLog:
	$(AM_V_GEN) if test -d $(top_srcdir)/.git; then \
		GIT_DIR="$(top_srcdir)/.git" git log --stat > $@; \
	fi

dist: ChangeLog

.PHONY: ChangeLog

-include $(top_srcdir)/git.mk
