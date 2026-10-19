# This file will be processed with automake-1.7 to create Makefile.in

AUTOMAKE_OPTIONS = 1.7
ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}

NULL =

DISTCHECK_CONFIGURE_FLAGS = \
	--without-systemd \
	--enable-compile-warnings=no \
	CFLAGS='-Wno-deprecated-declarations'

SUBDIRS =
if BUILTIN_HELPER
SUBDIRS += helper
endif

SUBDIRS +=	\
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

if ENABLE_BUNDLED_LIBWLEMBED

# The bundled libwlembed submodule is built and staged at configure time (see
# configure.ac), so there is nothing to build during "make". The hook below
# only needs to copy it from the stage directory into the install prefix.

install-exec-hook:
	@if test "x@LIBWLEMBED_USE_BUNDLED@" = "xyes"; then \
		echo "Installing bundled libwlembed..."; \
		$(MESON) install -C $(LIBWLEMBED_SUBMODULE_BUILDDIR) \
			--destdir "$(DESTDIR)" --no-rebuild; \
	fi

distclean-local:
	rm -rf $(LIBWLEMBED_SUBMODULE_BUILDDIR)

# Do not ship the submodule build directory in tarballs.
dist-hook:
	rm -rf $(distdir)/submodules/libwlembed/build

endif

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
