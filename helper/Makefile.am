## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

noinst_LIBRARIES = libhelper-proto.a

libhelper_proto_a_CFLAGS =	\
	$(PAM_HELPER_CFLAGS) \
	-I$(top_srcdir)/src

libhelper_proto_a_SOURCES =	\
	helper_proto.h			\
	helper_proto.c

pkglibexec_PROGRAMS = mate-screensaver-pam-helper

mate_screensaver_pam_helper_CFLAGS =	\
	$(PAM_HELPER_CFLAGS) \
	-I$(top_srcdir)/src

mate_screensaver_pam_helper_SOURCES =	\
	pam-helper.c

mate_screensaver_pam_helper_LDADD =	\
	libhelper-proto.a

mate_screensaver_pam_helper_LDFLAGS =	\
	$(AUTH_LIBS)
