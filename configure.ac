AC_INIT([fwup], [0.5.1-dev], [fhunleth@troodon-software.com])
AC_CONFIG_SRCDIR([src/fwup_metadata.c])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE([-Wall -Werror foreign subdir-objects])

# Which platform?
AC_CANONICAL_HOST

# Checks for programs
AC_PROG_CC
AC_PROG_LIBTOOL

# Checks for header files
AC_CHECK_HEADERS([fcntl.h inttypes.h malloc.h stddef.h stdint.h stdlib.h string.h unistd.h archive.h confuse.h])

# Checks for typedefs, structures, and compiler characteristics
AC_TYPE_INT64_T
AC_TYPE_OFF_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T
AC_SYS_LARGEFILE
AC_PROG_CC_C99

# Checks for library functions
AC_FUNC_MALLOC
AC_CHECK_FUNCS([memset setenv strdup strtoul open_memstream umount])
AM_CONDITIONAL([HAS_OPEN_MEMSTREAM], [test x$ac_vc_func_open_memstream = x"yes"])

AC_CHECK_LIB([confuse], [cfg_init], [], [ AC_MSG_ERROR([Could not find libconfuse]) ])
AC_CHECK_LIB([archive], [archive_read_new], [], [ AC_MSG_ERROR([Could not find libarchive]) ])
AC_CHECK_LIB([sodium], [crypto_generichash_init], [], [ AC_MSG_ERROR([Could not find libsodium]) ])

# Platform-specific additions
AS_CASE([$host_os],
        [darwin*], [LIBS="$LIBS -framework CoreFoundation -framework DiskArbitration"]
        )

AC_CONFIG_FILES([Makefile src/Makefile tests/Makefile])

AC_OUTPUT
