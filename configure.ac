AC_INIT([cdebootstrap],m4_esyscmd(dpkg-parsechangelog | perl -ne 'print $1 if m/^Version: (.*)$/;'))
AM_INIT_AUTOMAKE([foreign no-define])
AC_CONFIG_HEADERS([config.h])
AM_MAINTAINER_MODE

AC_CANONICAL_TARGET

AC_PROG_CC
AC_PROG_RANLIB

AC_ARG_WITH(debian-architecture,
	AS_HELP_STRING(--with-debian-architecture=NAME,which debian architecture to use),
	[DEB_ARCH="$withval"])

if test -z "$DEB_ARCH"; then
	AC_PATH_PROG(DPKG_ARCHITECTURE,dpkg-architecture)

	if test -n "$DPKG_ARCHITECTURE"; then
		target_alias="$target_cpu-$target_os"
		DEB_ARCH=$($DPKG_ARCHITECTURE -t$target_alias -qDEB_HOST_ARCH)
	fi
fi

if test -z "$DEB_ARCH"; then
	AC_MSG_WARN([can't determine default debian architecture])
fi

AC_MSG_CHECKING(for debian architecture)
AC_DEFINE_UNQUOTED([DEB_ARCH],["$DEB_ARCH"],[Debian architecture])
AC_MSG_RESULT($DEB_ARCH)

AC_ARG_WITH(frontend,
	AS_HELP_STRING(--with-frontend=NAME,which frontend to use),
	[FRONTEND=$withval],[FRONTEND=standalone])

AC_MSG_CHECKING(for frontend)
if test "$FRONTEND" = "debian-installer"; then
	FRONTEND_LIBS="-ldebconfclient"
elif test "$FRONTEND" = "standalone"; then
	FRONTEND_LIBS=""
else
	AC_MSG_ERROR([unknown frontend $FRONTEND], 1)
fi
AC_MSG_RESULT($FRONTEND)

AC_SUBST(FRONTEND)
AC_SUBST(FRONTEND_LIBS)

AC_MSG_CHECKING(for configdir)
AC_ARG_WITH(configdir,
	AS_HELP_STRING(--with-configdir=DIR,a),
	[
CONFIGDIR="$withval"
if test "${CONFIGDIR:0:7}" = "BINARY/"; then
	configdir="\${bindir}${CONFIGDIR:6}"
	CONFIGDIR="${CONFIGDIR:6}"
	CONFIGDIR_BINARY=1
	AC_MSG_RESULT([path of binary/$CONFIGDIR])
else
	configdir="$CONFIGDIR"
	AC_MSG_RESULT([$CONFIGDIR])
fi
],[
CONFIGDIR="\${datadir}/$PACKAGE_NAME"
configdir="$CONFIGDIR"
AC_MSG_RESULT(default)
])

AC_SUBST([configdir])
AC_SUBST([CONFIGDIR])
AM_CONDITIONAL([CONFIGDIR_BINARY], [test "$CONFIGDIR_BINARY"])

AH_VERBATIM([_BSD_SOURCE],
[/* Enable BSD extensions on systems that have them.  */
#ifndef _BSD_SOURCE
# undef _BSD_SOURCE
#endif])dnl
AC_BEFORE([$0], [AC_COMPILE_IFELSE])dnl
AC_BEFORE([$0], [AC_RUN_IFELSE])dnl
AC_DEFINE([_BSD_SOURCE])

AH_VERBATIM([_XOPEN_SOURCE],
[/* Enable XOPEN extensions on systems that have them.  */
#ifndef _XOPEN_SOURCE
# undef _XOPEN_SOURCE
#endif])dnl
AC_BEFORE([$0], [AC_COMPILE_IFELSE])dnl
AC_BEFORE([$0], [AC_RUN_IFELSE])dnl
AC_DEFINE([_XOPEN_SOURCE])

AC_SYS_LARGEFILE

if test "$GCC" = yes; then
	CFLAGS="$CFLAGS -std=gnu99"
fi

AC_CONFIG_FILES([
Makefile
config/Makefile
helper/Makefile
helper/cdebootstrap-helper-apt/Makefile
helper/cdebootstrap-helper-makedev/Makefile
helper/cdebootstrap-helper-rc.d/Makefile
include/Makefile
src/Makefile
src/frontend/Makefile
src/frontend/debian-installer/Makefile
src/frontend/standalone/Makefile
])

AC_OUTPUT
