dnl configure.ac for pc

AC_PREREQ([2.69])
AC_INIT([pc], [0.3], [pen@lysator.liu.se], [pc], [https://github.com/ptrrkssn/pc])

AC_CONFIG_SRCDIR([pc.c])
AC_CONFIG_HEADERS([config.h])

AC_CONFIG_AUX_DIR([build-aux])

AC_PROG_CC
AC_PROG_LN_S
AC_PROG_INSTALL
AC_PROG_MAKE_SET

# Checks for header files.
AC_CHECK_HEADERS([sys/vnode.h sys/extattr.h sys/xattr.h sys/acl.h aio.h zlib.h md5.h skein.h sha256.h sha384.h sha512.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_UID_T
AC_TYPE_MODE_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T

# Checks for library functions.
AC_FUNC_ERROR_AT_LINE
AC_FUNC_LSTAT_FOLLOWS_SLASHED_SYMLINK
AC_FUNC_MALLOC
AC_FUNC_REALLOC

AC_CHECK_FUNCS([extattr_get_file getxattr acl_get_file acl aio_read])

AC_SEARCH_LIBS([crc32_z], [z])
AC_SEARCH_LIBS([adler32_z], [z])

AC_SEARCH_LIBS([MD5Init], [md])
AC_SEARCH_LIBS([SKEIN256_Init], [md])
AC_SEARCH_LIBS([SHA256_Init], [md])
AC_SEARCH_LIBS([SHA384_Init], [md])
AC_SEARCH_LIBS([SHA512_Init], [md])

AC_OUTPUT([Makefile])
