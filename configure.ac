dnl configure.ac for pc

AC_PREREQ([2.69])
AC_INIT([pc], [0.5], [pen@lysator.liu.se], [pc], [https://github.com/ptrrkssn/pc])

AC_CONFIG_SRCDIR([pc.c])
AC_CONFIG_HEADERS([config.h])

AC_CONFIG_AUX_DIR([build-aux])

AC_PROG_CC
AC_PROG_LN_S
AC_PROG_INSTALL
AC_PROG_MAKE_SET

# Checks for header files.
AC_CHECK_HEADERS([sys/vnode.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_UID_T
AC_TYPE_MODE_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT32_T

# Checks for library functions.
AC_FUNC_ERROR_AT_LINE
AC_FUNC_LSTAT_FOLLOWS_SLASHED_SYMLINK
AC_FUNC_MALLOC
AC_FUNC_REALLOC

AC_CHECK_FUNCS([lchmod utimensat lutimes attropen])

AC_SEARCH_LIBS([socket], [socket])


AC_ARG_WITH([aio],
 AS_HELP_STRING([--without-aio], [Don't build support for asynchronous I/O]))

if test "x$with_aio" != "xno"; then
   AC_CHECK_HEADERS([aio.h])
   AC_CHECK_FUNCS([aio_read aio_waitcomplete])
fi


AC_ARG_WITH([acls],
 AS_HELP_STRING([--without-acls], [Don't build support for ACLs]))

if test "x$with_acls" != "xno"; then
dnl   Solaris acl stuff we don't currently use
dnl   AC_SEARCH_LIBS([acl_get], [sec])
   AC_SEARCH_LIBS([acl_get_file], [acl])
   AC_CHECK_HEADERS([sys/acl.h])
   AC_CHECK_FUNCS([acl_get_file acl_set_file acl_set_link_np acl_get_link_np acl])
fi


AC_ARG_WITH([attrs],
  AS_HELP_STRING([--without-attrs], [Don't build support for attributes]))

if test "x$with_attrs" != "xno"; then
   AC_CHECK_HEADERS([sys/extattr.h sys/xattr.h])
   AC_CHECK_FUNCS([extattr_get_link extattr_set_link extattr_delete_link getxattr lgetxattr setxattr lsetxattr attropen])
fi


AC_ARG_WITH([fflags],
 AS_HELP_STRING([--without-fflags], [Don't build support for file flags]))

if test "x$with_fflags" != "xno"; then
   AC_CHECK_FUNCS([chflags lchflags getattrat])
fi


AC_ARG_WITH([zlib],
  AS_HELP_STRING([--without-zlib], [Don't build support for zlib-provided digests]))

AC_ARG_WITH([nettle],
  AS_HELP_STRING([--without-nettle], [Don't build support for Nettle-provided digests]))

AC_ARG_WITH([md],
  AS_HELP_STRING([--without-md], [Don't build support for MD-provided digests]))

AC_ARG_WITH([openssl],
  AS_HELP_STRING([--without-openssl], [Don't build support for OpenSSL-provided digests]))


if test "x$with_zlib" != "xno"; then
   have_zlib_lib=no
   have_zlib_h=no
   
   AC_SEARCH_LIBS([crc32_z], [z], [have_zlib_lib=yes], [
     AC_SEARCH_LIBS([adler32_z], [z], [have_zlib_lib=yes])])
     
   if test "x$have_zlib_lib" = "xyes"; then
     AC_CHECK_HEADERS([zlib.h], [have_zlib_h=yes])
   fi
   
   if test "x$have_zlib_h" = "xyes"; then
     AC_CHECK_FUNCS([adler32_z crc32_z])
   fi
fi


if test "x$with_nettle" != "xno"; then
   have_nettle_lib=no
   have_nettle_h=no
   
   AC_SEARCH_LIBS([nettle_sha3_512_init], [nettle], [have_nettle_lib=yes], [
     AC_SEARCH_LIBS([nettle_sha512_init], [nettle], [have_nettle_lib=yes], [
       AC_SEARCH_LIBS([nettle_md5_init], [nettle], [have_nettle_lib=yes])])])

   if test "x$have_nettle_lib" = "xyes"; then
     AC_CHECK_HEADERS([nettle/md5.h nettle/sha2.h nettle/sha3.h], [have_nettle_h=yes])
   fi
   
   if test "x$have_nettle_h" = "xyes"; then
     AC_CHECK_FUNCS([nettle_sha3_256_init nettle_sha3_512_init])
     AC_CHECK_FUNCS([nettle_sha256_init nettle_sha512_init])
     AC_CHECK_FUNCS([nettle_md5_init])

     dnl No need for OpenSSL if these are found
     AC_CHECK_FUNCS([nettle_md5_init nettle_sha256_init nettle_sha512_init], [with_openssl=no])
   fi
fi


if test "x$with_md" != "xno"; then
   have_libmd_lib=no
   have_libmd_h=no
   
   AC_SEARCH_LIBS([SHA512_Init], [md], [have_libmd_lib=yes], [
     AC_SEARCH_LIBS([MD5Init], [md], [have_libmd_lib=yes])])
     
   if test "x$have_libmd_lib" = "xyes"; then
     AC_CHECK_HEADERS([md5.h skein.h sha256.h sha512.h], [have_libmd_h=yes])
   fi
   
   if test "x$have_libmd_h" = "xyes"; then
     AC_CHECK_FUNCS([MD5Init SKEIN256_Init SKEIN1024_Init])
     
     dnl No need for OpenSSL if these are found
     AC_CHECK_FUNCS([SHA256_Init SHA512_Init], [with_openssl=no])
   fi
fi


if test "x$with_openssl" != "xno"; then
   have_openssl_lib=no
   have_openssl_h=no
   
   AC_SEARCH_LIBS([SHA512_Init], [crypto], [have_openssl_lib=yes], [
     AC_SEARCH_LIBS([MD5_Init], [crypto], [have_openssl_lib=yes])])

   if test "x$have_openssl_lib" = "xyes"; then
     AC_CHECK_HEADERS([openssl/md5.h openssl/sha.h], [have_openssl_h=yes])
   fi

   if test "x$have_openssl_h" = "xyes"; then
     unset ac_cv_func_SHA256_Init
     unset ac_cv_func_SHA512_Init
     AC_CHECK_FUNCS([MD5_Init SHA256_Init SHA512_Init])
   fi
fi

AC_OUTPUT([Makefile])
