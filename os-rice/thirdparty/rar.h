/*
 * rar.h -- libarchive 3.8.1's RAR readers, amalgamated into one
 * single-header library for os-rice.
 *
 * Upstream (https://libarchive.org/, Tim Kientzle et al) is BSD-2-Clause;
 * the licence is reproduced in the file comments below, where upstream put
 * it. This carries the decode side of RAR only: the RAR 1.5-3.x reader, the
 * RAR 5 reader, libarchive's PPMd7 and the BLAKE2s pair RAR5 checksums with.
 *
 * Vendored the way thirdparty/yaml.h and thirdparty/bearssl.h are: one file
 * in the source tree, no submodule, no package to install, and the same
 * stb-style split -- including it plainly gets the declarations, and exactly
 * one translation unit defines the code:
 *
 *   #define RAR_IMPLEMENTATION
 *   #include "../thirdparty/rar.h"
 *
 * That unit is lib/rar.c. Everywhere else just includes it.
 *
 * The readers are not standalone: they call into libarchive's read stack
 * (__archive_read_ahead, archive_set_error, the archive_entry setters, a
 * few archive_string helpers). lib/rar_shim.c implements that surface --
 * around 40 functions -- against the upstream headers this file carries
 * verbatim, so nothing here is a retyped copy of an upstream struct.
 *
 * All C89, like the rest of this tree, with one borrowed extension: the
 * fixed-width types in <stdint.h>, which every compiler this builds with
 * provides in C89 mode.
 *
 * Regenerating: see amalgamate_rar.py's docstring.
 *
 * ---- upstream licence ----
 *
 * Copyright (c) 2003-2007 Tim Kientzle
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE DAMAGE.
 */

/* This is what archive_platform.h would have read out of a configure-built
 * config.h, written out here because that header #errors without one. Only
 * what the RAR readers and the headers under them look at is listed; the
 * rest of libarchive's several hundred HAVE_* macros belong to files this
 * amalgamation does not carry.
 *
 * HAVE_ZLIB_H stays off on purpose: archive_crc32.h then supplies its own
 * crc32(), so the RAR readers need no compression library at all. */
#define __LIBARCHIVE_BUILD 1
#define HAVE_LIMITS_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_ERRNO_H 1
#define HAVE_STDINT_H 1
#define HAVE_WCHAR_H 1

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

/* ssize_t and mode_t: POSIX puts them in <sys/types.h>, and the RAR readers
 * use both spellings raw. MSVC's ucrt has no ssize_t at all, and hides
 * mode_t behind the non-standard-names switch <sys/stat.h> only honours when
 * _CRT_INTERNAL_NONSTDC_NAMES is on -- newer cl builds leave it off -- so
 * supply both there. The underscored _mode_t is always declared. */
#include <sys/types.h>
#include <sys/stat.h>
#if defined(_MSC_VER)
#if !defined(_SSIZE_T_DEFINED)
#define _SSIZE_T_DEFINED
#ifdef _WIN64
typedef __int64 ssize_t;
#else
typedef int ssize_t;
#endif
#endif
#if !defined(_CRT_INTERNAL_NONSTDC_NAMES) || !_CRT_INTERNAL_NONSTDC_NAMES
typedef _mode_t mode_t;
#endif
#endif

/* archive_platform.h's tail: the error codes archive.h only lists as
 * comments, and the attribute spellings the sources use. EILSEQ over
 * upstream's BSD-only EFTYPE, which is the fallback it picks anyway. */
#define ARCHIVE_ERRNO_FILE_FORMAT EILSEQ
#define ARCHIVE_ERRNO_PROGRAMMER EINVAL
#define ARCHIVE_ERRNO_MISC (-1)
#if defined(__GNUC__) && (__GNUC__ >= 7)
#define __LA_FALLTHROUGH __attribute__((fallthrough))
#else
#define __LA_FALLTHROUGH
#endif
#define __LA_LIBC_CC
#define la_stat(path, stref) stat(path, stref)


/* ==== archive.h ==== */
/*-
 * Copyright (c) 2003-2010 Tim Kientzle
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef ARCHIVE_H_INCLUDED
#define	ARCHIVE_H_INCLUDED

/*
 * The version number is expressed as a single integer that makes it
 * easy to compare versions at build time: for version a.b.c, the
 * version number is printf("%d%03d%03d",a,b,c).  For example, if you
 * know your application requires version 2.12.108 or later, you can
 * assert that ARCHIVE_VERSION_NUMBER >= 2012108.
 */
/* Note: Compiler will complain if this does not match archive_entry.h! */
#define	ARCHIVE_VERSION_NUMBER 3008001

#include <sys/stat.h>
#include <stddef.h>  /* for wchar_t */
#include <stdio.h> /* For FILE * */
#if ARCHIVE_VERSION_NUMBER < 4000000
/* time_t is slated to be removed from public includes in 4.0 */
#include <time.h> /* For time_t */
#endif

/*
 * Note: archive.h is for use outside of libarchive; the configuration
 * headers (config.h, archive_platform.h, etc.) are purely internal.
 * Do NOT use HAVE_XXX configuration macros to control the behavior of
 * this header!  If you must conditionalize, use predefined compiler and/or
 * platform macros.
 */
#if defined(__BORLANDC__) && __BORLANDC__ >= 0x560
# include <stdint.h>
#elif !defined(__WATCOMC__) && !defined(_MSC_VER) && !defined(__INTERIX) && !defined(__BORLANDC__) && !defined(_SCO_DS) && !defined(__osf__) && !defined(__CLANG_INTTYPES_H)
# include <inttypes.h>
#endif

/* Get appropriate definitions of 64-bit integer */
#if !defined(__LA_INT64_T_DEFINED)
/* Older code relied on the __LA_INT64_T macro; after 4.0 we'll switch to the typedef exclusively. */
# if ARCHIVE_VERSION_NUMBER < 4000000
#define __LA_INT64_T la_int64_t
# endif
#define __LA_INT64_T_DEFINED
# if defined(_WIN32) && !defined(__CYGWIN__) && !defined(__WATCOMC__)
typedef __int64 la_int64_t;
typedef unsigned __int64 la_uint64_t;
# else
# include <unistd.h>  /* ssize_t */
#  if defined(_SCO_DS) || defined(__osf__)
typedef long long la_int64_t;
typedef unsigned long long la_uint64_t;
#  else
typedef int64_t la_int64_t;
typedef uint64_t la_uint64_t;
#  endif
# endif
#endif

/* The la_ssize_t should match the type used in 'struct stat' */
#if !defined(__LA_SSIZE_T_DEFINED)
/* Older code relied on the __LA_SSIZE_T macro; after 4.0 we'll switch to the typedef exclusively. */
# if ARCHIVE_VERSION_NUMBER < 4000000
#define __LA_SSIZE_T la_ssize_t
# endif
#define __LA_SSIZE_T_DEFINED
# if defined(_WIN32) && !defined(__CYGWIN__) && !defined(__WATCOMC__)
#  if defined(_SSIZE_T_DEFINED) || defined(_SSIZE_T_)
typedef ssize_t la_ssize_t;
#  elif defined(_WIN64)
typedef __int64 la_ssize_t;
#  else
typedef long la_ssize_t;
#  endif
# else
# include <unistd.h>  /* ssize_t */
typedef ssize_t la_ssize_t;
# endif
#endif

#if ARCHIVE_VERSION_NUMBER < 4000000
/* Use the platform types for time_t */
#define __LA_TIME_T time_t
#else
/* Use 64-bits integer types for time_t */
#define __LA_TIME_T la_int64_t
#endif

#if ARCHIVE_VERSION_NUMBER < 4000000
/* Use the platform types for dev_t */
#define __LA_DEV_T dev_t
#else
/* Use 64-bits integer types for dev_t */
#define __LA_DEV_T la_int64_t
#endif

/* Large file support for Android */
#if defined(__LIBARCHIVE_BUILD) && defined(__ANDROID__)

#endif

/*
 * On Windows, define LIBARCHIVE_STATIC if you're building or using a
 * .lib.  The default here assumes you're building a DLL.  Only
 * libarchive source should ever define __LIBARCHIVE_BUILD.
 */
#if ((defined __WIN32__) || (defined _WIN32) || defined(__CYGWIN__)) && (!defined LIBARCHIVE_STATIC)
# ifdef __LIBARCHIVE_BUILD
#  ifdef __GNUC__
#   define __LA_DECL	__attribute__((dllexport)) extern
#  else
#   define __LA_DECL	__declspec(dllexport)
#  endif
# else
#  ifdef __GNUC__
#   define __LA_DECL
#  else
#   define __LA_DECL	__declspec(dllimport)
#  endif
# endif
#elif defined __LIBARCHIVE_ENABLE_VISIBILITY
#  define __LA_DECL __attribute__((visibility("default")))
#else
/* Static libraries or non-Windows needs no special declaration. */
# define __LA_DECL
#endif

#if defined(__GNUC__) && __GNUC__ >= 3 && !defined(__MINGW32__)
#define	__LA_PRINTF(fmtarg, firstvararg) \
	__attribute__((__format__ (__printf__, fmtarg, firstvararg)))
#else
#define	__LA_PRINTF(fmtarg, firstvararg)	/* nothing */
#endif

#if defined(__GNUC__) && (__GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 1))
# define __LA_DEPRECATED __attribute__((deprecated))
#else
# define __LA_DEPRECATED
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The version number is provided as both a macro and a function.
 * The macro identifies the installed header; the function identifies
 * the library version (which may not be the same if you're using a
 * dynamically-linked version of the library).  Of course, if the
 * header and library are very different, you should expect some
 * strangeness.  Don't do that.
 */
__LA_DECL int		archive_version_number(void);

/*
 * Textual name/version of the library, useful for version displays.
 */
#define	ARCHIVE_VERSION_ONLY_STRING "3.8.1"
#define	ARCHIVE_VERSION_STRING "libarchive " ARCHIVE_VERSION_ONLY_STRING
__LA_DECL const char *	archive_version_string(void);

/*
 * Detailed textual name/version of the library and its dependencies.
 * This has the form:
 *    "libarchive x.y.z zlib/a.b.c liblzma/d.e.f ... etc ..."
 * the list of libraries described here will vary depending on how
 * libarchive was compiled.
 */
__LA_DECL const char *	archive_version_details(void);

/*
 * Returns NULL if libarchive was compiled without the associated library.
 * Otherwise, returns the version number that libarchive was compiled
 * against.
 */
__LA_DECL const char *  archive_zlib_version(void);
__LA_DECL const char *  archive_liblzma_version(void);
__LA_DECL const char *  archive_bzlib_version(void);
__LA_DECL const char *  archive_liblz4_version(void);
__LA_DECL const char *  archive_libzstd_version(void);
__LA_DECL const char *  archive_liblzo2_version(void);
__LA_DECL const char *  archive_libexpat_version(void);
__LA_DECL const char *  archive_libbsdxml_version(void);
__LA_DECL const char *  archive_libxml2_version(void);
__LA_DECL const char *  archive_mbedtls_version(void);
__LA_DECL const char *  archive_nettle_version(void);
__LA_DECL const char *  archive_openssl_version(void);
__LA_DECL const char *  archive_libmd_version(void);
__LA_DECL const char *  archive_commoncrypto_version(void);
__LA_DECL const char *  archive_cng_version(void);
__LA_DECL const char *  archive_wincrypt_version(void);
__LA_DECL const char *  archive_librichacl_version(void);
__LA_DECL const char *  archive_libacl_version(void);
__LA_DECL const char *  archive_libattr_version(void);
__LA_DECL const char *  archive_libiconv_version(void);
__LA_DECL const char *  archive_libpcre_version(void);
__LA_DECL const char *  archive_libpcre2_version(void);

/* Declare our basic types. */
struct archive;
struct archive_entry;

/*
 * Error codes: Use archive_errno() and archive_error_string()
 * to retrieve details.  Unless specified otherwise, all functions
 * that return 'int' use these codes.
 */
#define	ARCHIVE_EOF	  1	/* Found end of archive. */
#define	ARCHIVE_OK	  0	/* Operation was successful. */
#define	ARCHIVE_RETRY	(-10)	/* Retry might succeed. */
#define	ARCHIVE_WARN	(-20)	/* Partial success. */
/* For example, if write_header "fails", then you can't push data. */
#define	ARCHIVE_FAILED	(-25)	/* Current operation cannot complete. */
/* But if write_header is "fatal," then this archive is dead and useless. */
#define	ARCHIVE_FATAL	(-30)	/* No more operations are possible. */

/*
 * As far as possible, archive_errno returns standard platform errno codes.
 * Of course, the details vary by platform, so the actual definitions
 * here are stored in "archive_platform.h".  The symbols are listed here
 * for reference; as a rule, clients should not need to know the exact
 * platform-dependent error code.
 */
/* Unrecognized or invalid file format. */
/* #define	ARCHIVE_ERRNO_FILE_FORMAT */
/* Illegal usage of the library. */
/* #define	ARCHIVE_ERRNO_PROGRAMMER_ERROR */
/* Unknown or unclassified error. */
/* #define	ARCHIVE_ERRNO_MISC */

/*
 * Callbacks are invoked to automatically read/skip/write/open/close the
 * archive. You can provide your own for complex tasks (like breaking
 * archives across multiple tapes) or use standard ones built into the
 * library.
 */

/* Returns pointer and size of next block of data from archive. */
typedef la_ssize_t	archive_read_callback(struct archive *,
			    void *_client_data, const void **_buffer);

/* Skips at most request bytes from archive and returns the skipped amount.
 * This may skip fewer bytes than requested; it may even skip zero bytes.
 * If you do skip fewer bytes than requested, libarchive will invoke your
 * read callback and discard data as necessary to make up the full skip.
 */
typedef la_int64_t	archive_skip_callback(struct archive *,
			    void *_client_data, la_int64_t request);

/* Seeks to specified location in the file and returns the position.
 * Whence values are SEEK_SET, SEEK_CUR, SEEK_END from stdio.h.
 * Return ARCHIVE_FATAL if the seek fails for any reason.
 */
typedef la_int64_t	archive_seek_callback(struct archive *,
    void *_client_data, la_int64_t offset, int whence);

/* Returns size actually written, zero on EOF, -1 on error. */
typedef la_ssize_t	archive_write_callback(struct archive *,
			    void *_client_data,
			    const void *_buffer, size_t _length);

typedef int	archive_open_callback(struct archive *, void *_client_data);

typedef int	archive_close_callback(struct archive *, void *_client_data);

typedef int	archive_free_callback(struct archive *, void *_client_data);

/* Switches from one client data object to the next/prev client data object.
 * This is useful for reading from different data blocks such as a set of files
 * that make up one large file.
 */
typedef int archive_switch_callback(struct archive *, void *_client_data1,
			    void *_client_data2);

/*
 * Returns a passphrase used for encryption or decryption, NULL on nothing
 * to do and give it up.
 */
typedef const char *archive_passphrase_callback(struct archive *,
			    void *_client_data);

/*
 * Codes to identify various stream filters.
 */
#define	ARCHIVE_FILTER_NONE	0
#define	ARCHIVE_FILTER_GZIP	1
#define	ARCHIVE_FILTER_BZIP2	2
#define	ARCHIVE_FILTER_COMPRESS	3
#define	ARCHIVE_FILTER_PROGRAM	4
#define	ARCHIVE_FILTER_LZMA	5
#define	ARCHIVE_FILTER_XZ	6
#define	ARCHIVE_FILTER_UU	7
#define	ARCHIVE_FILTER_RPM	8
#define	ARCHIVE_FILTER_LZIP	9
#define	ARCHIVE_FILTER_LRZIP	10
#define	ARCHIVE_FILTER_LZOP	11
#define	ARCHIVE_FILTER_GRZIP	12
#define	ARCHIVE_FILTER_LZ4	13
#define	ARCHIVE_FILTER_ZSTD	14

#if ARCHIVE_VERSION_NUMBER < 4000000
#define	ARCHIVE_COMPRESSION_NONE	ARCHIVE_FILTER_NONE
#define	ARCHIVE_COMPRESSION_GZIP	ARCHIVE_FILTER_GZIP
#define	ARCHIVE_COMPRESSION_BZIP2	ARCHIVE_FILTER_BZIP2
#define	ARCHIVE_COMPRESSION_COMPRESS	ARCHIVE_FILTER_COMPRESS
#define	ARCHIVE_COMPRESSION_PROGRAM	ARCHIVE_FILTER_PROGRAM
#define	ARCHIVE_COMPRESSION_LZMA	ARCHIVE_FILTER_LZMA
#define	ARCHIVE_COMPRESSION_XZ		ARCHIVE_FILTER_XZ
#define	ARCHIVE_COMPRESSION_UU		ARCHIVE_FILTER_UU
#define	ARCHIVE_COMPRESSION_RPM		ARCHIVE_FILTER_RPM
#define	ARCHIVE_COMPRESSION_LZIP	ARCHIVE_FILTER_LZIP
#define	ARCHIVE_COMPRESSION_LRZIP	ARCHIVE_FILTER_LRZIP
#endif

/*
 * Codes returned by archive_format.
 *
 * Top 16 bits identifies the format family (e.g., "tar"); lower
 * 16 bits indicate the variant.  This is updated by read_next_header.
 * Note that the lower 16 bits will often vary from entry to entry.
 * In some cases, this variation occurs as libarchive learns more about
 * the archive (for example, later entries might utilize extensions that
 * weren't necessary earlier in the archive; in this case, libarchive
 * will change the format code to indicate the extended format that
 * was used).  In other cases, it's because different tools have
 * modified the archive and so different parts of the archive
 * actually have slightly different formats.  (Both tar and cpio store
 * format codes in each entry, so it is quite possible for each
 * entry to be in a different format.)
 */
#define	ARCHIVE_FORMAT_BASE_MASK		0xff0000
#define	ARCHIVE_FORMAT_CPIO			0x10000
#define	ARCHIVE_FORMAT_CPIO_POSIX		(ARCHIVE_FORMAT_CPIO | 1)
#define	ARCHIVE_FORMAT_CPIO_BIN_LE		(ARCHIVE_FORMAT_CPIO | 2)
#define	ARCHIVE_FORMAT_CPIO_BIN_BE		(ARCHIVE_FORMAT_CPIO | 3)
#define	ARCHIVE_FORMAT_CPIO_SVR4_NOCRC		(ARCHIVE_FORMAT_CPIO | 4)
#define	ARCHIVE_FORMAT_CPIO_SVR4_CRC		(ARCHIVE_FORMAT_CPIO | 5)
#define	ARCHIVE_FORMAT_CPIO_AFIO_LARGE		(ARCHIVE_FORMAT_CPIO | 6)
#define	ARCHIVE_FORMAT_CPIO_PWB			(ARCHIVE_FORMAT_CPIO | 7)
#define	ARCHIVE_FORMAT_SHAR			0x20000
#define	ARCHIVE_FORMAT_SHAR_BASE		(ARCHIVE_FORMAT_SHAR | 1)
#define	ARCHIVE_FORMAT_SHAR_DUMP		(ARCHIVE_FORMAT_SHAR | 2)
#define	ARCHIVE_FORMAT_TAR			0x30000
#define	ARCHIVE_FORMAT_TAR_USTAR		(ARCHIVE_FORMAT_TAR | 1)
#define	ARCHIVE_FORMAT_TAR_PAX_INTERCHANGE	(ARCHIVE_FORMAT_TAR | 2)
#define	ARCHIVE_FORMAT_TAR_PAX_RESTRICTED	(ARCHIVE_FORMAT_TAR | 3)
#define	ARCHIVE_FORMAT_TAR_GNUTAR		(ARCHIVE_FORMAT_TAR | 4)
#define	ARCHIVE_FORMAT_ISO9660			0x40000
#define	ARCHIVE_FORMAT_ISO9660_ROCKRIDGE	(ARCHIVE_FORMAT_ISO9660 | 1)
#define	ARCHIVE_FORMAT_ZIP			0x50000
#define	ARCHIVE_FORMAT_EMPTY			0x60000
#define	ARCHIVE_FORMAT_AR			0x70000
#define	ARCHIVE_FORMAT_AR_GNU			(ARCHIVE_FORMAT_AR | 1)
#define	ARCHIVE_FORMAT_AR_BSD			(ARCHIVE_FORMAT_AR | 2)
#define	ARCHIVE_FORMAT_MTREE			0x80000
#define	ARCHIVE_FORMAT_RAW			0x90000
#define	ARCHIVE_FORMAT_XAR			0xA0000
#define	ARCHIVE_FORMAT_LHA			0xB0000
#define	ARCHIVE_FORMAT_CAB			0xC0000
#define	ARCHIVE_FORMAT_RAR			0xD0000
#define	ARCHIVE_FORMAT_7ZIP			0xE0000
#define	ARCHIVE_FORMAT_WARC			0xF0000
#define	ARCHIVE_FORMAT_RAR_V5			0x100000

/*
 * Codes returned by archive_read_format_capabilities().
 *
 * This list can be extended with values between 0 and 0xffff.
 * The original purpose of this list was to let different archive
 * format readers expose their general capabilities in terms of
 * encryption.
 */
#define ARCHIVE_READ_FORMAT_CAPS_NONE (0) /* no special capabilities */
#define ARCHIVE_READ_FORMAT_CAPS_ENCRYPT_DATA (1<<0)  /* reader can detect encrypted data */
#define ARCHIVE_READ_FORMAT_CAPS_ENCRYPT_METADATA (1<<1)  /* reader can detect encryptable metadata (pathname, mtime, etc.) */

/*
 * Codes returned by archive_read_has_encrypted_entries().
 *
 * In case the archive does not support encryption detection at all
 * ARCHIVE_READ_FORMAT_ENCRYPTION_UNSUPPORTED is returned. If the reader
 * for some other reason (e.g. not enough bytes read) cannot say if
 * there are encrypted entries, ARCHIVE_READ_FORMAT_ENCRYPTION_DONT_KNOW
 * is returned.
 */
#define ARCHIVE_READ_FORMAT_ENCRYPTION_UNSUPPORTED -2
#define ARCHIVE_READ_FORMAT_ENCRYPTION_DONT_KNOW -1

/*-
 * Basic outline for reading an archive:
 *   1) Ask archive_read_new for an archive reader object.
 *   2) Update any global properties as appropriate.
 *      In particular, you'll certainly want to call appropriate
 *      archive_read_support_XXX functions.
 *   3) Call archive_read_open_XXX to open the archive
 *   4) Repeatedly call archive_read_next_header to get information about
 *      successive archive entries.  Call archive_read_data to extract
 *      data for entries of interest.
 *   5) Call archive_read_free to end processing.
 */
__LA_DECL struct archive	*archive_read_new(void);

/*
 * The archive_read_support_XXX calls enable auto-detect for this
 * archive handle.  They also link in the necessary support code.
 * For example, if you don't want bzlib linked in, don't invoke
 * support_compression_bzip2().  The "all" functions provide the
 * obvious shorthand.
 */

#if ARCHIVE_VERSION_NUMBER < 4000000
__LA_DECL int archive_read_support_compression_all(struct archive *)
		__LA_DEPRECATED;
__LA_DECL int archive_read_support_compression_bzip2(struct archive *)
		__LA_DEPRECATED;
__LA_DECL int archive_read_support_compression_compress(struct archive *)
		__LA_DEPRECATED;
__LA_DECL int archive_read_support_compression_gzip(struct archive *)
		__LA_DEPRECATED;
__LA_DECL int archive_read_support_compression_lzip(struct archive *)
		__LA_DEPRECATED;
__LA_DECL int archive_read_support_compression_lzma(struct archive *)
		__LA_DEPRECATED;
__LA_DECL int archive_read_support_compression_none(struct archive *)
		__LA_DEPRECATED;
__LA_DECL int archive_read_support_compression_program(struct archive *,
		     const char *command) __LA_DEPRECATED;
__LA_DECL int archive_read_support_compression_program_signature
		(struct archive *, const char *,
		 const void * /* match */, size_t) __LA_DEPRECATED;

__LA_DECL int archive_read_support_compression_rpm(struct archive *)
		__LA_DEPRECATED;
__LA_DECL int archive_read_support_compression_uu(struct archive *)
		__LA_DEPRECATED;
__LA_DECL int archive_read_support_compression_xz(struct archive *)
		__LA_DEPRECATED;
#endif

__LA_DECL int archive_read_support_filter_all(struct archive *);
__LA_DECL int archive_read_support_filter_by_code(struct archive *, int);
__LA_DECL int archive_read_support_filter_bzip2(struct archive *);
__LA_DECL int archive_read_support_filter_compress(struct archive *);
__LA_DECL int archive_read_support_filter_gzip(struct archive *);
__LA_DECL int archive_read_support_filter_grzip(struct archive *);
__LA_DECL int archive_read_support_filter_lrzip(struct archive *);
__LA_DECL int archive_read_support_filter_lz4(struct archive *);
__LA_DECL int archive_read_support_filter_lzip(struct archive *);
__LA_DECL int archive_read_support_filter_lzma(struct archive *);
__LA_DECL int archive_read_support_filter_lzop(struct archive *);
__LA_DECL int archive_read_support_filter_none(struct archive *);
__LA_DECL int archive_read_support_filter_program(struct archive *,
		     const char *command);
__LA_DECL int archive_read_support_filter_program_signature
		(struct archive *, const char * /* cmd */,
				    const void * /* match */, size_t);
__LA_DECL int archive_read_support_filter_rpm(struct archive *);
__LA_DECL int archive_read_support_filter_uu(struct archive *);
__LA_DECL int archive_read_support_filter_xz(struct archive *);
__LA_DECL int archive_read_support_filter_zstd(struct archive *);

__LA_DECL int archive_read_support_format_7zip(struct archive *);
__LA_DECL int archive_read_support_format_all(struct archive *);
__LA_DECL int archive_read_support_format_ar(struct archive *);
__LA_DECL int archive_read_support_format_by_code(struct archive *, int);
__LA_DECL int archive_read_support_format_cab(struct archive *);
__LA_DECL int archive_read_support_format_cpio(struct archive *);
__LA_DECL int archive_read_support_format_empty(struct archive *);
/* archive_read_support_format_gnutar() is an alias for historical reasons
 * of archive_read_support_format_tar(). */
__LA_DECL int archive_read_support_format_gnutar(struct archive *);
__LA_DECL int archive_read_support_format_iso9660(struct archive *);
__LA_DECL int archive_read_support_format_lha(struct archive *);
__LA_DECL int archive_read_support_format_mtree(struct archive *);
__LA_DECL int archive_read_support_format_rar(struct archive *);
__LA_DECL int archive_read_support_format_rar5(struct archive *);
__LA_DECL int archive_read_support_format_raw(struct archive *);
__LA_DECL int archive_read_support_format_tar(struct archive *);
__LA_DECL int archive_read_support_format_warc(struct archive *);
__LA_DECL int archive_read_support_format_xar(struct archive *);
/* archive_read_support_format_zip() enables both streamable and seekable
 * zip readers. */
__LA_DECL int archive_read_support_format_zip(struct archive *);
/* Reads Zip archives as stream from beginning to end.  Doesn't
 * correctly handle SFX ZIP files or ZIP archives that have been modified
 * in-place. */
__LA_DECL int archive_read_support_format_zip_streamable(struct archive *);
/* Reads starting from central directory; requires seekable input. */
__LA_DECL int archive_read_support_format_zip_seekable(struct archive *);

/* Functions to manually set the format and filters to be used. This is
 * useful to bypass the bidding process when the format and filters to use
 * is known in advance.
 */
__LA_DECL int archive_read_set_format(struct archive *, int);
__LA_DECL int archive_read_append_filter(struct archive *, int);
__LA_DECL int archive_read_append_filter_program(struct archive *,
    const char *);
__LA_DECL int archive_read_append_filter_program_signature
    (struct archive *, const char *, const void * /* match */, size_t);

/* Set various callbacks. */
__LA_DECL int archive_read_set_open_callback(struct archive *,
    archive_open_callback *);
__LA_DECL int archive_read_set_read_callback(struct archive *,
    archive_read_callback *);
__LA_DECL int archive_read_set_seek_callback(struct archive *,
    archive_seek_callback *);
__LA_DECL int archive_read_set_skip_callback(struct archive *,
    archive_skip_callback *);
__LA_DECL int archive_read_set_close_callback(struct archive *,
    archive_close_callback *);
/* Callback used to switch between one data object to the next */
__LA_DECL int archive_read_set_switch_callback(struct archive *,
    archive_switch_callback *);

/* This sets the first data object. */
__LA_DECL int archive_read_set_callback_data(struct archive *, void *);
/* This sets data object at specified index */
__LA_DECL int archive_read_set_callback_data2(struct archive *, void *,
    unsigned int);
/* This adds a data object at the specified index. */
__LA_DECL int archive_read_add_callback_data(struct archive *, void *,
    unsigned int);
/* This appends a data object to the end of list */
__LA_DECL int archive_read_append_callback_data(struct archive *, void *);
/* This prepends a data object to the beginning of list */
__LA_DECL int archive_read_prepend_callback_data(struct archive *, void *);

/* Opening freezes the callbacks. */
__LA_DECL int archive_read_open1(struct archive *);

/* Convenience wrappers around the above. */
__LA_DECL int archive_read_open(struct archive *, void *_client_data,
		     archive_open_callback *, archive_read_callback *,
		     archive_close_callback *);
__LA_DECL int archive_read_open2(struct archive *, void *_client_data,
		     archive_open_callback *, archive_read_callback *,
		     archive_skip_callback *, archive_close_callback *);

/*
 * A variety of shortcuts that invoke archive_read_open() with
 * canned callbacks suitable for common situations.  The ones that
 * accept a block size handle tape blocking correctly.
 */
/* Use this if you know the filename.  Note: NULL indicates stdin. */
__LA_DECL int archive_read_open_filename(struct archive *,
		     const char *_filename, size_t _block_size);
/* Use this for reading multivolume files by filenames.
 * NOTE: Must be NULL terminated. Sorting is NOT done. */
__LA_DECL int archive_read_open_filenames(struct archive *,
		     const char **_filenames, size_t _block_size);
__LA_DECL int archive_read_open_filename_w(struct archive *,
		     const wchar_t *_filename, size_t _block_size);
#if defined(_WIN32) && !defined(__CYGWIN__)
__LA_DECL int archive_read_open_filenames_w(struct archive *,
		     const wchar_t **_filenames, size_t _block_size);
#endif
/* archive_read_open_file() is a deprecated synonym for ..._open_filename(). */
__LA_DECL int archive_read_open_file(struct archive *,
		     const char *_filename, size_t _block_size) __LA_DEPRECATED;
/* Read an archive that's stored in memory. */
__LA_DECL int archive_read_open_memory(struct archive *,
		     const void * buff, size_t size);
/* A more involved version that is only used for internal testing. */
__LA_DECL int archive_read_open_memory2(struct archive *a, const void *buff,
		     size_t size, size_t read_size);
/* Read an archive that's already open, using the file descriptor. */
__LA_DECL int archive_read_open_fd(struct archive *, int _fd,
		     size_t _block_size);
/* Read an archive that's already open, using a FILE *. */
/* Note: DO NOT use this with tape drives. */
__LA_DECL int archive_read_open_FILE(struct archive *, FILE *_file);

/* Parses and returns next entry header. */
__LA_DECL int archive_read_next_header(struct archive *,
		     struct archive_entry **);

/* Parses and returns next entry header using the archive_entry passed in */
__LA_DECL int archive_read_next_header2(struct archive *,
		     struct archive_entry *);

/*
 * Retrieve the byte offset in UNCOMPRESSED data where last-read
 * header started.
 */
__LA_DECL la_int64_t		 archive_read_header_position(struct archive *);

/*
 * Returns 1 if the archive contains at least one encrypted entry.
 * If the archive format not support encryption at all
 * ARCHIVE_READ_FORMAT_ENCRYPTION_UNSUPPORTED is returned.
 * If for any other reason (e.g. not enough data read so far)
 * we cannot say whether there are encrypted entries, then
 * ARCHIVE_READ_FORMAT_ENCRYPTION_DONT_KNOW is returned.
 * In general, this function will return values below zero when the
 * reader is uncertain or totally incapable of encryption support.
 * When this function returns 0 you can be sure that the reader
 * supports encryption detection but no encrypted entries have
 * been found yet.
 *
 * NOTE: If the metadata/header of an archive is also encrypted, you
 * cannot rely on the number of encrypted entries. That is why this
 * function does not return the number of encrypted entries but#
 * just shows that there are some.
 */
__LA_DECL int	archive_read_has_encrypted_entries(struct archive *);

/*
 * Returns a bitmask of capabilities that are supported by the archive format reader.
 * If the reader has no special capabilities, ARCHIVE_READ_FORMAT_CAPS_NONE is returned.
 */
__LA_DECL int		 archive_read_format_capabilities(struct archive *);

/* Read data from the body of an entry.  Similar to read(2). */
__LA_DECL la_ssize_t		 archive_read_data(struct archive *,
				    void *, size_t);

/* Seek within the body of an entry.  Similar to lseek(2). */
__LA_DECL la_int64_t archive_seek_data(struct archive *, la_int64_t, int);

/*
 * A zero-copy version of archive_read_data that also exposes the file offset
 * of each returned block.  Note that the client has no way to specify
 * the desired size of the block.  The API does guarantee that offsets will
 * be strictly increasing and that returned blocks will not overlap.
 */
__LA_DECL int archive_read_data_block(struct archive *a,
		    const void **buff, size_t *size, la_int64_t *offset);

/*-
 * Some convenience functions that are built on archive_read_data:
 *  'skip': skips entire entry
 *  'into_buffer': writes data into memory buffer that you provide
 *  'into_fd': writes data to specified filedes
 */
__LA_DECL int archive_read_data_skip(struct archive *);
__LA_DECL int archive_read_data_into_fd(struct archive *, int fd);

/*
 * Set read options.
 */
/* Apply option to the format only. */
__LA_DECL int archive_read_set_format_option(struct archive *_a,
			    const char *m, const char *o,
			    const char *v);
/* Apply option to the filter only. */
__LA_DECL int archive_read_set_filter_option(struct archive *_a,
			    const char *m, const char *o,
			    const char *v);
/* Apply option to both the format and the filter. */
__LA_DECL int archive_read_set_option(struct archive *_a,
			    const char *m, const char *o,
			    const char *v);
/* Apply option string to both the format and the filter. */
__LA_DECL int archive_read_set_options(struct archive *_a,
			    const char *opts);

/*
 * Add a decryption passphrase.
 */
__LA_DECL int archive_read_add_passphrase(struct archive *, const char *);
__LA_DECL int archive_read_set_passphrase_callback(struct archive *,
			    void *client_data, archive_passphrase_callback *);


/*-
 * Convenience function to recreate the current entry (whose header
 * has just been read) on disk.
 *
 * This does quite a bit more than just copy data to disk. It also:
 *  - Creates intermediate directories as required.
 *  - Manages directory permissions:  non-writable directories will
 *    be initially created with write permission enabled; when the
 *    archive is closed, dir permissions are edited to the values specified
 *    in the archive.
 *  - Checks hardlinks:  hardlinks will not be extracted unless the
 *    linked-to file was also extracted within the same session. (TODO)
 */

/* The "flags" argument selects optional behavior, 'OR' the flags you want. */

/* Default: Do not try to set owner/group. */
#define	ARCHIVE_EXTRACT_OWNER			(0x0001)
/* Default: Do obey umask, do not restore SUID/SGID/SVTX bits. */
#define	ARCHIVE_EXTRACT_PERM			(0x0002)
/* Default: Do not restore mtime/atime. */
#define	ARCHIVE_EXTRACT_TIME			(0x0004)
/* Default: Replace existing files. */
#define	ARCHIVE_EXTRACT_NO_OVERWRITE 		(0x0008)
/* Default: Try create first, unlink only if create fails with EEXIST. */
#define	ARCHIVE_EXTRACT_UNLINK			(0x0010)
/* Default: Do not restore ACLs. */
#define	ARCHIVE_EXTRACT_ACL			(0x0020)
/* Default: Do not restore fflags. */
#define	ARCHIVE_EXTRACT_FFLAGS			(0x0040)
/* Default: Do not restore xattrs. */
#define	ARCHIVE_EXTRACT_XATTR 			(0x0080)
/* Default: Do not try to guard against extracts redirected by symlinks. */
/* Note: With ARCHIVE_EXTRACT_UNLINK, will remove any intermediate symlink. */
#define	ARCHIVE_EXTRACT_SECURE_SYMLINKS		(0x0100)
/* Default: Do not reject entries with '..' as path elements. */
#define	ARCHIVE_EXTRACT_SECURE_NODOTDOT		(0x0200)
/* Default: Create parent directories as needed. */
#define	ARCHIVE_EXTRACT_NO_AUTODIR		(0x0400)
/* Default: Overwrite files, even if one on disk is newer. */
#define	ARCHIVE_EXTRACT_NO_OVERWRITE_NEWER	(0x0800)
/* Detect blocks of 0 and write holes instead. */
#define	ARCHIVE_EXTRACT_SPARSE			(0x1000)
/* Default: Do not restore Mac extended metadata. */
/* This has no effect except on Mac OS. */
#define	ARCHIVE_EXTRACT_MAC_METADATA		(0x2000)
/* Default: Use HFS+ compression if it was compressed. */
/* This has no effect except on Mac OS v10.6 or later. */
#define	ARCHIVE_EXTRACT_NO_HFS_COMPRESSION	(0x4000)
/* Default: Do not use HFS+ compression if it was not compressed. */
/* This has no effect except on Mac OS v10.6 or later. */
#define	ARCHIVE_EXTRACT_HFS_COMPRESSION_FORCED	(0x8000)
/* Default: Do not reject entries with absolute paths */
#define ARCHIVE_EXTRACT_SECURE_NOABSOLUTEPATHS (0x10000)
/* Default: Do not clear no-change flags when unlinking object */
#define	ARCHIVE_EXTRACT_CLEAR_NOCHANGE_FFLAGS	(0x20000)
/* Default: Do not extract atomically (using rename) */
#define	ARCHIVE_EXTRACT_SAFE_WRITES		(0x40000)

__LA_DECL int archive_read_extract(struct archive *, struct archive_entry *,
		     int flags);
__LA_DECL int archive_read_extract2(struct archive *, struct archive_entry *,
		     struct archive * /* dest */);
__LA_DECL void	 archive_read_extract_set_progress_callback(struct archive *,
		     void (*_progress_func)(void *), void *_user_data);

/* Record the dev/ino of a file that will not be written.  This is
 * generally set to the dev/ino of the archive being read. */
__LA_DECL void		archive_read_extract_set_skip_file(struct archive *,
		     la_int64_t, la_int64_t);

/* Close the file and release most resources. */
__LA_DECL int		 archive_read_close(struct archive *);
/* Release all resources and destroy the object. */
/* Note that archive_read_free will call archive_read_close for you. */
__LA_DECL int		 archive_read_free(struct archive *);
#if ARCHIVE_VERSION_NUMBER < 4000000
/* Synonym for archive_read_free() for backwards compatibility. */
__LA_DECL int		 archive_read_finish(struct archive *) __LA_DEPRECATED;
#endif

/*-
 * To create an archive:
 *   1) Ask archive_write_new for an archive writer object.
 *   2) Set any global properties.  In particular, you should set
 *      the compression and format to use.
 *   3) Call archive_write_open to open the file (most people
 *       will use archive_write_open_file or archive_write_open_fd,
 *       which provide convenient canned I/O callbacks for you).
 *   4) For each entry:
 *      - construct an appropriate struct archive_entry structure
 *      - archive_write_header to write the header
 *      - archive_write_data to write the entry data
 *   5) archive_write_close to close the output
 *   6) archive_write_free to cleanup the writer and release resources
 */
__LA_DECL struct archive	*archive_write_new(void);
__LA_DECL int archive_write_set_bytes_per_block(struct archive *,
		     int bytes_per_block);
__LA_DECL int archive_write_get_bytes_per_block(struct archive *);
/* XXX This is badly misnamed; suggestions appreciated. XXX */
__LA_DECL int archive_write_set_bytes_in_last_block(struct archive *,
		     int bytes_in_last_block);
__LA_DECL int archive_write_get_bytes_in_last_block(struct archive *);

/* The dev/ino of a file that won't be archived.  This is used
 * to avoid recursively adding an archive to itself. */
__LA_DECL int archive_write_set_skip_file(struct archive *,
    la_int64_t, la_int64_t);

#if ARCHIVE_VERSION_NUMBER < 4000000
__LA_DECL int archive_write_set_compression_bzip2(struct archive *)
		__LA_DEPRECATED;
__LA_DECL int archive_write_set_compression_compress(struct archive *)
		__LA_DEPRECATED;
__LA_DECL int archive_write_set_compression_gzip(struct archive *)
		__LA_DEPRECATED;
__LA_DECL int archive_write_set_compression_lzip(struct archive *)
		__LA_DEPRECATED;
__LA_DECL int archive_write_set_compression_lzma(struct archive *)
		__LA_DEPRECATED;
__LA_DECL int archive_write_set_compression_none(struct archive *)
		__LA_DEPRECATED;
__LA_DECL int archive_write_set_compression_program(struct archive *,
		     const char *cmd) __LA_DEPRECATED;
__LA_DECL int archive_write_set_compression_xz(struct archive *)
		__LA_DEPRECATED;
#endif

/* A convenience function to set the filter based on the code. */
__LA_DECL int archive_write_add_filter(struct archive *, int filter_code);
__LA_DECL int archive_write_add_filter_by_name(struct archive *,
		     const char *name);
__LA_DECL int archive_write_add_filter_b64encode(struct archive *);
__LA_DECL int archive_write_add_filter_bzip2(struct archive *);
__LA_DECL int archive_write_add_filter_compress(struct archive *);
__LA_DECL int archive_write_add_filter_grzip(struct archive *);
__LA_DECL int archive_write_add_filter_gzip(struct archive *);
__LA_DECL int archive_write_add_filter_lrzip(struct archive *);
__LA_DECL int archive_write_add_filter_lz4(struct archive *);
__LA_DECL int archive_write_add_filter_lzip(struct archive *);
__LA_DECL int archive_write_add_filter_lzma(struct archive *);
__LA_DECL int archive_write_add_filter_lzop(struct archive *);
__LA_DECL int archive_write_add_filter_none(struct archive *);
__LA_DECL int archive_write_add_filter_program(struct archive *,
		     const char *cmd);
__LA_DECL int archive_write_add_filter_uuencode(struct archive *);
__LA_DECL int archive_write_add_filter_xz(struct archive *);
__LA_DECL int archive_write_add_filter_zstd(struct archive *);


/* A convenience function to set the format based on the code or name. */
__LA_DECL int archive_write_set_format(struct archive *, int format_code);
__LA_DECL int archive_write_set_format_by_name(struct archive *,
		     const char *name);
/* To minimize link pollution, use one or more of the following. */
__LA_DECL int archive_write_set_format_7zip(struct archive *);
__LA_DECL int archive_write_set_format_ar_bsd(struct archive *);
__LA_DECL int archive_write_set_format_ar_svr4(struct archive *);
__LA_DECL int archive_write_set_format_cpio(struct archive *);
__LA_DECL int archive_write_set_format_cpio_bin(struct archive *);
__LA_DECL int archive_write_set_format_cpio_newc(struct archive *);
__LA_DECL int archive_write_set_format_cpio_odc(struct archive *);
__LA_DECL int archive_write_set_format_cpio_pwb(struct archive *);
__LA_DECL int archive_write_set_format_gnutar(struct archive *);
__LA_DECL int archive_write_set_format_iso9660(struct archive *);
__LA_DECL int archive_write_set_format_mtree(struct archive *);
__LA_DECL int archive_write_set_format_mtree_classic(struct archive *);
/* TODO: int archive_write_set_format_old_tar(struct archive *); */
__LA_DECL int archive_write_set_format_pax(struct archive *);
__LA_DECL int archive_write_set_format_pax_restricted(struct archive *);
__LA_DECL int archive_write_set_format_raw(struct archive *);
__LA_DECL int archive_write_set_format_shar(struct archive *);
__LA_DECL int archive_write_set_format_shar_dump(struct archive *);
__LA_DECL int archive_write_set_format_ustar(struct archive *);
__LA_DECL int archive_write_set_format_v7tar(struct archive *);
__LA_DECL int archive_write_set_format_warc(struct archive *);
__LA_DECL int archive_write_set_format_xar(struct archive *);
__LA_DECL int archive_write_set_format_zip(struct archive *);
__LA_DECL int archive_write_set_format_filter_by_ext(struct archive *a, const char *filename);
__LA_DECL int archive_write_set_format_filter_by_ext_def(struct archive *a, const char *filename, const char * def_ext);
__LA_DECL int archive_write_zip_set_compression_deflate(struct archive *);
__LA_DECL int archive_write_zip_set_compression_store(struct archive *);
__LA_DECL int archive_write_zip_set_compression_lzma(struct archive *);
__LA_DECL int archive_write_zip_set_compression_xz(struct archive *);
__LA_DECL int archive_write_zip_set_compression_bzip2(struct archive *);
__LA_DECL int archive_write_zip_set_compression_zstd(struct archive *);
/* Deprecated; use archive_write_open2 instead */
__LA_DECL int archive_write_open(struct archive *, void *,
		     archive_open_callback *, archive_write_callback *,
		     archive_close_callback *);
__LA_DECL int archive_write_open2(struct archive *, void *,
		     archive_open_callback *, archive_write_callback *,
		     archive_close_callback *, archive_free_callback *);
__LA_DECL int archive_write_open_fd(struct archive *, int _fd);
__LA_DECL int archive_write_open_filename(struct archive *, const char *_file);
__LA_DECL int archive_write_open_filename_w(struct archive *,
		     const wchar_t *_file);
/* A deprecated synonym for archive_write_open_filename() */
__LA_DECL int archive_write_open_file(struct archive *, const char *_file)
		__LA_DEPRECATED;
__LA_DECL int archive_write_open_FILE(struct archive *, FILE *);
/* _buffSize is the size of the buffer, _used refers to a variable that
 * will be updated after each write into the buffer. */
__LA_DECL int archive_write_open_memory(struct archive *,
			void *_buffer, size_t _buffSize, size_t *_used);

/*
 * Note that the library will truncate writes beyond the size provided
 * to archive_write_header or pad if the provided data is short.
 */
__LA_DECL int archive_write_header(struct archive *,
		     struct archive_entry *);
__LA_DECL la_ssize_t	archive_write_data(struct archive *,
			    const void *, size_t);

/* This interface is currently only available for archive_write_disk handles.  */
__LA_DECL la_ssize_t	 archive_write_data_block(struct archive *,
				    const void *, size_t, la_int64_t);

__LA_DECL int		 archive_write_finish_entry(struct archive *);
__LA_DECL int		 archive_write_close(struct archive *);
/* Marks the archive as FATAL so that a subsequent free() operation
 * won't try to close() cleanly.  Provides a fast abort capability
 * when the client discovers that things have gone wrong. */
__LA_DECL int            archive_write_fail(struct archive *);
/* This can fail if the archive wasn't already closed, in which case
 * archive_write_free() will implicitly call archive_write_close(). */
__LA_DECL int		 archive_write_free(struct archive *);
#if ARCHIVE_VERSION_NUMBER < 4000000
/* Synonym for archive_write_free() for backwards compatibility. */
__LA_DECL int		 archive_write_finish(struct archive *) __LA_DEPRECATED;
#endif

/*
 * Set write options.
 */
/* Apply option to the format only. */
__LA_DECL int archive_write_set_format_option(struct archive *_a,
			    const char *m, const char *o,
			    const char *v);
/* Apply option to the filter only. */
__LA_DECL int archive_write_set_filter_option(struct archive *_a,
			    const char *m, const char *o,
			    const char *v);
/* Apply option to both the format and the filter. */
__LA_DECL int archive_write_set_option(struct archive *_a,
			    const char *m, const char *o,
			    const char *v);
/* Apply option string to both the format and the filter. */
__LA_DECL int archive_write_set_options(struct archive *_a,
			    const char *opts);

/*
 * Set an encryption passphrase.
 */
__LA_DECL int archive_write_set_passphrase(struct archive *_a, const char *p);
__LA_DECL int archive_write_set_passphrase_callback(struct archive *,
			    void *client_data, archive_passphrase_callback *);

/*-
 * ARCHIVE_WRITE_DISK API
 *
 * To create objects on disk:
 *   1) Ask archive_write_disk_new for a new archive_write_disk object.
 *   2) Set any global properties.  In particular, you probably
 *      want to set the options.
 *   3) For each entry:
 *      - construct an appropriate struct archive_entry structure
 *      - archive_write_header to create the file/dir/etc on disk
 *      - archive_write_data to write the entry data
 *   4) archive_write_free to cleanup the writer and release resources
 *
 * In particular, you can use this in conjunction with archive_read()
 * to pull entries out of an archive and create them on disk.
 */
__LA_DECL struct archive	*archive_write_disk_new(void);
/* This file will not be overwritten. */
__LA_DECL int archive_write_disk_set_skip_file(struct archive *,
    la_int64_t, la_int64_t);
/* Set flags to control how the next item gets created.
 * This accepts a bitmask of ARCHIVE_EXTRACT_XXX flags defined above. */
__LA_DECL int		 archive_write_disk_set_options(struct archive *,
		     int flags);
/*
 * The lookup functions are given uname/uid (or gname/gid) pairs and
 * return a uid (gid) suitable for this system.  These are used for
 * restoring ownership and for setting ACLs.  The default functions
 * are naive, they just return the uid/gid.  These are small, so reasonable
 * for applications that don't need to preserve ownership; they
 * are probably also appropriate for applications that are doing
 * same-system backup and restore.
 */
/*
 * The "standard" lookup functions use common system calls to lookup
 * the uname/gname, falling back to the uid/gid if the names can't be
 * found.  They cache lookups and are reasonably fast, but can be very
 * large, so they are not used unless you ask for them.  In
 * particular, these match the specifications of POSIX "pax" and old
 * POSIX "tar".
 */
__LA_DECL int	 archive_write_disk_set_standard_lookup(struct archive *);
/*
 * If neither the default (naive) nor the standard (big) functions suit
 * your needs, you can write your own and register them.  Be sure to
 * include a cleanup function if you have allocated private data.
 */
__LA_DECL int archive_write_disk_set_group_lookup(struct archive *,
    void * /* private_data */,
    la_int64_t (*)(void *, const char *, la_int64_t),
    void (* /* cleanup */)(void *));
__LA_DECL int archive_write_disk_set_user_lookup(struct archive *,
    void * /* private_data */,
    la_int64_t (*)(void *, const char *, la_int64_t),
    void (* /* cleanup */)(void *));
__LA_DECL la_int64_t archive_write_disk_gid(struct archive *, const char *, la_int64_t);
__LA_DECL la_int64_t archive_write_disk_uid(struct archive *, const char *, la_int64_t);

/*
 * ARCHIVE_READ_DISK API
 *
 * This is still evolving and somewhat experimental.
 */
__LA_DECL struct archive *archive_read_disk_new(void);
/* The names for symlink modes here correspond to an old BSD
 * command-line argument convention: -L, -P, -H */
/* Follow all symlinks. */
__LA_DECL int archive_read_disk_set_symlink_logical(struct archive *);
/* Follow no symlinks. */
__LA_DECL int archive_read_disk_set_symlink_physical(struct archive *);
/* Follow symlink initially, then not. */
__LA_DECL int archive_read_disk_set_symlink_hybrid(struct archive *);
/* TODO: Handle Linux stat32/stat64 ugliness. <sigh> */
__LA_DECL int archive_read_disk_entry_from_file(struct archive *,
    struct archive_entry *, int /* fd */, const struct stat *);
/* Look up gname for gid or uname for uid. */
/* Default implementations are very, very stupid. */
__LA_DECL const char *archive_read_disk_gname(struct archive *, la_int64_t);
__LA_DECL const char *archive_read_disk_uname(struct archive *, la_int64_t);
/* "Standard" implementation uses getpwuid_r, getgrgid_r and caches the
 * results for performance. */
__LA_DECL int	archive_read_disk_set_standard_lookup(struct archive *);
/* You can install your own lookups if you like. */
__LA_DECL int	archive_read_disk_set_gname_lookup(struct archive *,
    void * /* private_data */,
    const char *(* /* lookup_fn */)(void *, la_int64_t),
    void (* /* cleanup_fn */)(void *));
__LA_DECL int	archive_read_disk_set_uname_lookup(struct archive *,
    void * /* private_data */,
    const char *(* /* lookup_fn */)(void *, la_int64_t),
    void (* /* cleanup_fn */)(void *));
/* Start traversal. */
__LA_DECL int	archive_read_disk_open(struct archive *, const char *);
__LA_DECL int	archive_read_disk_open_w(struct archive *, const wchar_t *);
/*
 * Request that current entry be visited.  If you invoke it on every
 * directory, you'll get a physical traversal.  This is ignored if the
 * current entry isn't a directory or a link to a directory.  So, if
 * you invoke this on every returned path, you'll get a full logical
 * traversal.
 */
__LA_DECL int	archive_read_disk_descend(struct archive *);
__LA_DECL int	archive_read_disk_can_descend(struct archive *);
__LA_DECL int	archive_read_disk_current_filesystem(struct archive *);
__LA_DECL int	archive_read_disk_current_filesystem_is_synthetic(struct archive *);
__LA_DECL int	archive_read_disk_current_filesystem_is_remote(struct archive *);
/* Request that the access time of the entry visited by traversal be restored. */
__LA_DECL int  archive_read_disk_set_atime_restored(struct archive *);
/*
 * Set behavior. The "flags" argument selects optional behavior.
 */
/* Request that the access time of the entry visited by traversal be restored.
 * This is the same as archive_read_disk_set_atime_restored. */
#define	ARCHIVE_READDISK_RESTORE_ATIME		(0x0001)
/* Default: Do not skip an entry which has nodump flags. */
#define	ARCHIVE_READDISK_HONOR_NODUMP		(0x0002)
/* Default: Skip a mac resource fork file whose prefix is "._" because of
 * using copyfile. */
#define	ARCHIVE_READDISK_MAC_COPYFILE		(0x0004)
/* Default: Traverse mount points. */
#define	ARCHIVE_READDISK_NO_TRAVERSE_MOUNTS	(0x0008)
/* Default: Xattrs are read from disk. */
#define	ARCHIVE_READDISK_NO_XATTR		(0x0010)
/* Default: ACLs are read from disk. */
#define	ARCHIVE_READDISK_NO_ACL			(0x0020)
/* Default: File flags are read from disk. */
#define	ARCHIVE_READDISK_NO_FFLAGS		(0x0040)
/* Default: Sparse file information is read from disk. */
#define	ARCHIVE_READDISK_NO_SPARSE		(0x0080)

__LA_DECL int  archive_read_disk_set_behavior(struct archive *,
		    int flags);

/*
 * Set archive_match object that will be used in archive_read_disk to
 * know whether an entry should be skipped. The callback function
 * _excluded_func will be invoked when an entry is skipped by the result
 * of archive_match.
 */
__LA_DECL int	archive_read_disk_set_matching(struct archive *,
		    struct archive *_matching, void (*_excluded_func)
		    (struct archive *, void *, struct archive_entry *),
		    void *_client_data);
__LA_DECL int	archive_read_disk_set_metadata_filter_callback(struct archive *,
		    int (*_metadata_filter_func)(struct archive *, void *,
		    	struct archive_entry *), void *_client_data);

/* Simplified cleanup interface;
 * This calls archive_read_free() or archive_write_free() as needed. */
__LA_DECL int	archive_free(struct archive *);

/*
 * Accessor functions to read/set various information in
 * the struct archive object:
 */

/* Number of filters in the current filter pipeline. */
/* Filter #0 is the one closest to the format, -1 is a synonym for the
 * last filter, which is always the pseudo-filter that wraps the
 * client callbacks. */
__LA_DECL int		 archive_filter_count(struct archive *);
__LA_DECL la_int64_t	 archive_filter_bytes(struct archive *, int);
__LA_DECL int		 archive_filter_code(struct archive *, int);
__LA_DECL const char *	 archive_filter_name(struct archive *, int);

#if ARCHIVE_VERSION_NUMBER < 4000000
/* These don't properly handle multiple filters, so are deprecated and
 * will eventually be removed. */
/* As of libarchive 3.0, this is an alias for archive_filter_bytes(a, -1); */
__LA_DECL la_int64_t	 archive_position_compressed(struct archive *)
				__LA_DEPRECATED;
/* As of libarchive 3.0, this is an alias for archive_filter_bytes(a, 0); */
__LA_DECL la_int64_t	 archive_position_uncompressed(struct archive *)
				__LA_DEPRECATED;
/* As of libarchive 3.0, this is an alias for archive_filter_name(a, 0); */
__LA_DECL const char	*archive_compression_name(struct archive *)
				__LA_DEPRECATED;
/* As of libarchive 3.0, this is an alias for archive_filter_code(a, 0); */
__LA_DECL int		 archive_compression(struct archive *)
				__LA_DEPRECATED;
#endif

/* Parses a date string relative to the current time.
 * NOTE: This is not intended for general date parsing, and the resulting timestamp should only be used for libarchive. */
__LA_DECL time_t	archive_parse_date(time_t now, const char *datestr);

__LA_DECL int		 archive_errno(struct archive *);
__LA_DECL const char	*archive_error_string(struct archive *);
__LA_DECL const char	*archive_format_name(struct archive *);
__LA_DECL int		 archive_format(struct archive *);
__LA_DECL void		 archive_clear_error(struct archive *);
__LA_DECL void		 archive_set_error(struct archive *, int _err,
			    const char *fmt, ...) __LA_PRINTF(3, 4);
__LA_DECL void		 archive_copy_error(struct archive *dest,
			    struct archive *src);
__LA_DECL int		 archive_file_count(struct archive *);

/*
 * ARCHIVE_MATCH API
 */
__LA_DECL struct archive *archive_match_new(void);
__LA_DECL int	archive_match_free(struct archive *);

/*
 * Test if archive_entry is excluded.
 * This is a convenience function. This is the same as calling all
 * archive_match_path_excluded, archive_match_time_excluded
 * and archive_match_owner_excluded.
 */
__LA_DECL int	archive_match_excluded(struct archive *,
		    struct archive_entry *);

/*
 * Test if pathname is excluded. The conditions are set by following functions.
 */
__LA_DECL int	archive_match_path_excluded(struct archive *,
		    struct archive_entry *);
/* Control recursive inclusion of directory content when directory is included. Default on. */
__LA_DECL int	archive_match_set_inclusion_recursion(struct archive *, int);
/* Add exclusion pathname pattern. */
__LA_DECL int	archive_match_exclude_pattern(struct archive *, const char *);
__LA_DECL int	archive_match_exclude_pattern_w(struct archive *,
		    const wchar_t *);
/* Add exclusion pathname pattern from file. */
__LA_DECL int	archive_match_exclude_pattern_from_file(struct archive *,
		    const char *, int _nullSeparator);
__LA_DECL int	archive_match_exclude_pattern_from_file_w(struct archive *,
		    const wchar_t *, int _nullSeparator);
/* Add inclusion pathname pattern. */
__LA_DECL int	archive_match_include_pattern(struct archive *, const char *);
__LA_DECL int	archive_match_include_pattern_w(struct archive *,
		    const wchar_t *);
/* Add inclusion pathname pattern from file. */
__LA_DECL int	archive_match_include_pattern_from_file(struct archive *,
		    const char *, int _nullSeparator);
__LA_DECL int	archive_match_include_pattern_from_file_w(struct archive *,
		    const wchar_t *, int _nullSeparator);
/*
 * How to get statistic information for inclusion patterns.
 */
/* Return the amount number of unmatched inclusion patterns. */
__LA_DECL int	archive_match_path_unmatched_inclusions(struct archive *);
/* Return the pattern of unmatched inclusion with ARCHIVE_OK.
 * Return ARCHIVE_EOF if there is no inclusion pattern. */
__LA_DECL int	archive_match_path_unmatched_inclusions_next(
		    struct archive *, const char **);
__LA_DECL int	archive_match_path_unmatched_inclusions_next_w(
		    struct archive *, const wchar_t **);

/*
 * Test if a file is excluded by its time stamp.
 * The conditions are set by following functions.
 */
__LA_DECL int	archive_match_time_excluded(struct archive *,
		    struct archive_entry *);

/*
 * Flags to tell a matching type of time stamps. These are used for
 * following functions.
 */
/* Time flag: mtime to be tested. */
#define ARCHIVE_MATCH_MTIME	(0x0100)
/* Time flag: ctime to be tested. */
#define ARCHIVE_MATCH_CTIME	(0x0200)
/* Comparison flag: Match the time if it is newer than. */
#define ARCHIVE_MATCH_NEWER	(0x0001)
/* Comparison flag: Match the time if it is older than. */
#define ARCHIVE_MATCH_OLDER	(0x0002)
/* Comparison flag: Match the time if it is equal to. */
#define ARCHIVE_MATCH_EQUAL	(0x0010)
/* Set inclusion time. */
__LA_DECL int	archive_match_include_time(struct archive *, int _flag,
		    time_t _sec, long _nsec);
/* Set inclusion time by a date string. */
__LA_DECL int	archive_match_include_date(struct archive *, int _flag,
		    const char *_datestr);
__LA_DECL int	archive_match_include_date_w(struct archive *, int _flag,
		    const wchar_t *_datestr);
/* Set inclusion time by a particular file. */
__LA_DECL int	archive_match_include_file_time(struct archive *,
		    int _flag, const char *_pathname);
__LA_DECL int	archive_match_include_file_time_w(struct archive *,
		    int _flag, const wchar_t *_pathname);
/* Add exclusion entry. */
__LA_DECL int	archive_match_exclude_entry(struct archive *,
		    int _flag, struct archive_entry *);

/*
 * Test if a file is excluded by its uid ,gid, uname or gname.
 * The conditions are set by following functions.
 */
__LA_DECL int	archive_match_owner_excluded(struct archive *,
		    struct archive_entry *);
/* Add inclusion uid, gid, uname and gname. */
__LA_DECL int	archive_match_include_uid(struct archive *, la_int64_t);
__LA_DECL int	archive_match_include_gid(struct archive *, la_int64_t);
__LA_DECL int	archive_match_include_uname(struct archive *, const char *);
__LA_DECL int	archive_match_include_uname_w(struct archive *,
		    const wchar_t *);
__LA_DECL int	archive_match_include_gname(struct archive *, const char *);
__LA_DECL int	archive_match_include_gname_w(struct archive *,
		    const wchar_t *);

/* Utility functions */
#if ARCHIVE_VERSION_NUMBER < 4000000
/* Convenience function to sort a NULL terminated list of strings */
__LA_DECL int archive_utility_string_sort(char **);
#endif

#ifdef __cplusplus
}
#endif

/* These are meaningless outside of this header. */
#undef __LA_DECL

#endif /* !ARCHIVE_H_INCLUDED */

/* ==== archive_entry.h ==== */
/*-
 * Copyright (c) 2003-2008 Tim Kientzle
 * Copyright (c) 2016 Martin Matuska
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef ARCHIVE_ENTRY_H_INCLUDED
#define	ARCHIVE_ENTRY_H_INCLUDED

/* Note: Compiler will complain if this does not match archive.h! */
#define	ARCHIVE_VERSION_NUMBER 3008001

/*
 * Note: archive_entry.h is for use outside of libarchive; the
 * configuration headers (config.h, archive_platform.h, etc.) are
 * purely internal.  Do NOT use HAVE_XXX configuration macros to
 * control the behavior of this header!  If you must conditionalize,
 * use predefined compiler and/or platform macros.
 */

#include <sys/types.h>
#include <stddef.h>  /* for wchar_t */
#include <stdint.h>  /* for C99 int64_t, etc. */
#if ARCHIVE_VERSION_NUMBER < 4000000
/* time_t is slated to be removed from public includes in 4.0 */
#include <time.h>
#endif

#if defined(_WIN32) && !defined(__CYGWIN__)
#include <windows.h>
#endif

/* Get a suitable 64-bit integer type. */
#if !defined(__LA_INT64_T_DEFINED)
# if ARCHIVE_VERSION_NUMBER < 4000000
#define __LA_INT64_T la_int64_t
# endif
#define __LA_INT64_T_DEFINED
# if defined(_WIN32) && !defined(__CYGWIN__) && !defined(__WATCOMC__)
typedef __int64 la_int64_t;
typedef unsigned __int64 la_uint64_t;
# else
#include <unistd.h>
#  if defined(_SCO_DS) || defined(__osf__)
typedef long long la_int64_t;
typedef unsigned long long la_uint64_t;
#  else
typedef int64_t la_int64_t;
typedef uint64_t la_uint64_t;
#  endif
# endif
#endif

/* The la_ssize_t should match the type used in 'struct stat' */
#if !defined(__LA_SSIZE_T_DEFINED)
/* Older code relied on the __LA_SSIZE_T macro; after 4.0 we'll switch to the typedef exclusively. */
# if ARCHIVE_VERSION_NUMBER < 4000000
#define __LA_SSIZE_T la_ssize_t
# endif
#define __LA_SSIZE_T_DEFINED
# if defined(_WIN32) && !defined(__CYGWIN__) && !defined(__WATCOMC__)
#  if defined(_SSIZE_T_DEFINED) || defined(_SSIZE_T_)
typedef ssize_t la_ssize_t;
#  elif defined(_WIN64)
typedef __int64 la_ssize_t;
#  else
typedef long la_ssize_t;
#  endif
# else
# include <unistd.h>  /* ssize_t */
typedef ssize_t la_ssize_t;
# endif
#endif

/* Get a suitable definition for mode_t */
#if ARCHIVE_VERSION_NUMBER >= 3999000
/* Switch to plain 'int' for libarchive 4.0.  It's less broken than 'mode_t' */
# define	__LA_MODE_T	int
#elif defined(_WIN32) && !defined(__CYGWIN__) && !defined(__BORLANDC__) && !defined(__WATCOMC__)
# define	__LA_MODE_T	unsigned short
#else
# define	__LA_MODE_T	mode_t
#endif

#if ARCHIVE_VERSION_NUMBER < 4000000
/* Use the platform types for time_t */
#define __LA_TIME_T time_t
#else
/* Use 64-bits integer types for time_t */
#define __LA_TIME_T la_int64_t
#endif

#if ARCHIVE_VERSION_NUMBER < 4000000
/* Use the platform types for dev_t */
#define __LA_DEV_T dev_t
#else
/* Use 64-bits integer types for dev_t */
#define __LA_DEV_T la_int64_t
#endif

#if ARCHIVE_VERSION_NUMBER < 4000000
/* Libarchive 3.x used signed int64 for inode numbers */
#define __LA_INO_T la_int64_t
#else
/* Switch to unsigned for libarchive 4.0 */
#define __LA_INO_T la_uint64_t
#endif

/* Large file support for Android */
#if defined(__LIBARCHIVE_BUILD) && defined(__ANDROID__)

#endif

/*
 * On Windows, define LIBARCHIVE_STATIC if you're building or using a
 * .lib.  The default here assumes you're building a DLL.  Only
 * libarchive source should ever define __LIBARCHIVE_BUILD.
 */
#if ((defined __WIN32__) || (defined _WIN32) || defined(__CYGWIN__)) && (!defined LIBARCHIVE_STATIC)
# ifdef __LIBARCHIVE_BUILD
#  ifdef __GNUC__
#   define __LA_DECL	__attribute__((dllexport)) extern
#  else
#   define __LA_DECL	__declspec(dllexport)
#  endif
# else
#  ifdef __GNUC__
#   define __LA_DECL
#  else
#   define __LA_DECL	__declspec(dllimport)
#  endif
# endif
#elif defined __LIBARCHIVE_ENABLE_VISIBILITY
#  define __LA_DECL __attribute__((visibility("default")))
#else
/* Static libraries on all platforms and shared libraries on non-Windows. */
# define __LA_DECL
#endif

#if defined(__GNUC__) && (__GNUC__ > 3 || \
    (__GNUC__ == 3 && __GNUC_MINOR__ >= 1))
# define __LA_DEPRECATED __attribute__((deprecated))
#else
# define __LA_DEPRECATED
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Description of an archive entry.
 *
 * You can think of this as "struct stat" with some text fields added in.
 *
 * TODO: Add "comment", "charset", and possibly other entries that are
 * supported by "pax interchange" format.  However, GNU, ustar, cpio,
 * and other variants don't support these features, so they're not an
 * excruciatingly high priority right now.
 *
 * TODO: "pax interchange" format allows essentially arbitrary
 * key/value attributes to be attached to any entry.  Supporting
 * such extensions may make this library useful for special
 * applications (e.g., a package manager could attach special
 * package-management attributes to each entry).
 */
struct archive;
struct archive_entry;

/*
 * File-type constants.  These are returned from archive_entry_filetype()
 * and passed to archive_entry_set_filetype().
 *
 * These values match S_XXX defines on every platform I've checked,
 * including Windows, AIX, Linux, Solaris, and BSD.  They're
 * (re)defined here because platforms generally don't define the ones
 * they don't support.  For example, Windows doesn't define S_IFLNK or
 * S_IFBLK.  Instead of having a mass of conditional logic and system
 * checks to define any S_XXX values that aren't supported locally,
 * I've just defined a new set of such constants so that
 * libarchive-based applications can manipulate and identify archive
 * entries properly even if the hosting platform can't store them on
 * disk.
 *
 * These values are also used directly within some portable formats,
 * such as cpio.  If you find a platform that varies from these, the
 * correct solution is to leave these alone and translate from these
 * portable values to platform-native values when entries are read from
 * or written to disk.
 */
/*
 * In libarchive 4.0, we can drop the casts here.
 * They're needed to work around Borland C's broken mode_t.
 */
#define AE_IFMT		((__LA_MODE_T)0170000)
#define AE_IFREG	((__LA_MODE_T)0100000)
#define AE_IFLNK	((__LA_MODE_T)0120000)
#define AE_IFSOCK	((__LA_MODE_T)0140000)
#define AE_IFCHR	((__LA_MODE_T)0020000)
#define AE_IFBLK	((__LA_MODE_T)0060000)
#define AE_IFDIR	((__LA_MODE_T)0040000)
#define AE_IFIFO	((__LA_MODE_T)0010000)

/*
 * Symlink types
 */
#define AE_SYMLINK_TYPE_UNDEFINED	0
#define AE_SYMLINK_TYPE_FILE		1
#define AE_SYMLINK_TYPE_DIRECTORY	2

/*
 * Basic object manipulation
 */

__LA_DECL struct archive_entry	*archive_entry_clear(struct archive_entry *);
/* The 'clone' function does a deep copy; all of the strings are copied too. */
__LA_DECL struct archive_entry	*archive_entry_clone(struct archive_entry *);
__LA_DECL void			 archive_entry_free(struct archive_entry *);
__LA_DECL struct archive_entry	*archive_entry_new(void);

/*
 * This form of archive_entry_new2() will pull character-set
 * conversion information from the specified archive handle.  The
 * older archive_entry_new(void) form is equivalent to calling
 * archive_entry_new2(NULL) and will result in the use of an internal
 * default character-set conversion.
 */
__LA_DECL struct archive_entry	*archive_entry_new2(struct archive *);

/*
 * Retrieve fields from an archive_entry.
 *
 * There are a number of implicit conversions among these fields.  For
 * example, if a regular string field is set and you read the _w wide
 * character field, the entry will implicitly convert narrow-to-wide
 * using the current locale.  Similarly, dev values are automatically
 * updated when you write devmajor or devminor and vice versa.
 *
 * In addition, fields can be "set" or "unset."  Unset string fields
 * return NULL, non-string fields have _is_set() functions to test
 * whether they've been set.  You can "unset" a string field by
 * assigning NULL; non-string fields have _unset() functions to
 * unset them.
 *
 * Note: There is one ambiguity in the above; string fields will
 * also return NULL when implicit character set conversions fail.
 * This is usually what you want.
 */
__LA_DECL __LA_TIME_T	 archive_entry_atime(struct archive_entry *);
__LA_DECL long		 archive_entry_atime_nsec(struct archive_entry *);
__LA_DECL int		 archive_entry_atime_is_set(struct archive_entry *);
__LA_DECL __LA_TIME_T	 archive_entry_birthtime(struct archive_entry *);
__LA_DECL long		 archive_entry_birthtime_nsec(struct archive_entry *);
__LA_DECL int		 archive_entry_birthtime_is_set(struct archive_entry *);
__LA_DECL __LA_TIME_T	 archive_entry_ctime(struct archive_entry *);
__LA_DECL long		 archive_entry_ctime_nsec(struct archive_entry *);
__LA_DECL int		 archive_entry_ctime_is_set(struct archive_entry *);
__LA_DECL __LA_DEV_T		 archive_entry_dev(struct archive_entry *);
__LA_DECL int		 archive_entry_dev_is_set(struct archive_entry *);
__LA_DECL __LA_DEV_T		 archive_entry_devmajor(struct archive_entry *);
__LA_DECL __LA_DEV_T		 archive_entry_devminor(struct archive_entry *);
__LA_DECL __LA_MODE_T	 archive_entry_filetype(struct archive_entry *);
__LA_DECL int		 archive_entry_filetype_is_set(struct archive_entry *);
__LA_DECL void		 archive_entry_fflags(struct archive_entry *,
			    unsigned long * /* set */,
			    unsigned long * /* clear */);
__LA_DECL const char	*archive_entry_fflags_text(struct archive_entry *);
__LA_DECL la_int64_t	 archive_entry_gid(struct archive_entry *);
__LA_DECL int		 archive_entry_gid_is_set(struct archive_entry *);
__LA_DECL const char	*archive_entry_gname(struct archive_entry *);
__LA_DECL const char	*archive_entry_gname_utf8(struct archive_entry *);
__LA_DECL const wchar_t	*archive_entry_gname_w(struct archive_entry *);
__LA_DECL void		 archive_entry_set_link_to_hardlink(struct archive_entry *);
__LA_DECL const char	*archive_entry_hardlink(struct archive_entry *);
__LA_DECL const char	*archive_entry_hardlink_utf8(struct archive_entry *);
__LA_DECL const wchar_t	*archive_entry_hardlink_w(struct archive_entry *);
__LA_DECL int		 archive_entry_hardlink_is_set(struct archive_entry *);
__LA_DECL __LA_INO_T	 archive_entry_ino(struct archive_entry *);
__LA_DECL __LA_INO_T	 archive_entry_ino64(struct archive_entry *);
__LA_DECL int		 archive_entry_ino_is_set(struct archive_entry *);
__LA_DECL __LA_MODE_T	 archive_entry_mode(struct archive_entry *);
__LA_DECL time_t	 archive_entry_mtime(struct archive_entry *);
__LA_DECL long		 archive_entry_mtime_nsec(struct archive_entry *);
__LA_DECL int		 archive_entry_mtime_is_set(struct archive_entry *);
__LA_DECL unsigned int	 archive_entry_nlink(struct archive_entry *);
__LA_DECL const char	*archive_entry_pathname(struct archive_entry *);
__LA_DECL const char	*archive_entry_pathname_utf8(struct archive_entry *);
__LA_DECL const wchar_t	*archive_entry_pathname_w(struct archive_entry *);
__LA_DECL __LA_MODE_T	 archive_entry_perm(struct archive_entry *);
__LA_DECL int		 archive_entry_perm_is_set(struct archive_entry *);
__LA_DECL int		 archive_entry_rdev_is_set(struct archive_entry *);
__LA_DECL __LA_DEV_T		 archive_entry_rdev(struct archive_entry *);
__LA_DECL __LA_DEV_T		 archive_entry_rdevmajor(struct archive_entry *);
__LA_DECL __LA_DEV_T		 archive_entry_rdevminor(struct archive_entry *);
__LA_DECL const char	*archive_entry_sourcepath(struct archive_entry *);
__LA_DECL const wchar_t	*archive_entry_sourcepath_w(struct archive_entry *);
__LA_DECL la_int64_t	 archive_entry_size(struct archive_entry *);
__LA_DECL int		 archive_entry_size_is_set(struct archive_entry *);
__LA_DECL const char	*archive_entry_strmode(struct archive_entry *);
__LA_DECL void		 archive_entry_set_link_to_symlink(struct archive_entry *);
__LA_DECL const char	*archive_entry_symlink(struct archive_entry *);
__LA_DECL const char	*archive_entry_symlink_utf8(struct archive_entry *);
__LA_DECL int		 archive_entry_symlink_type(struct archive_entry *);
__LA_DECL const wchar_t	*archive_entry_symlink_w(struct archive_entry *);
__LA_DECL la_int64_t	 archive_entry_uid(struct archive_entry *);
__LA_DECL int		 archive_entry_uid_is_set(struct archive_entry *);
__LA_DECL const char	*archive_entry_uname(struct archive_entry *);
__LA_DECL const char	*archive_entry_uname_utf8(struct archive_entry *);
__LA_DECL const wchar_t	*archive_entry_uname_w(struct archive_entry *);
__LA_DECL int archive_entry_is_data_encrypted(struct archive_entry *);
__LA_DECL int archive_entry_is_metadata_encrypted(struct archive_entry *);
__LA_DECL int archive_entry_is_encrypted(struct archive_entry *);

/*
 * Set fields in an archive_entry.
 *
 * Note: Before libarchive 2.4, there were 'set' and 'copy' versions
 * of the string setters.  'copy' copied the actual string, 'set' just
 * stored the pointer.  In libarchive 2.4 and later, strings are
 * always copied.
 */

__LA_DECL void	archive_entry_set_atime(struct archive_entry *, __LA_TIME_T, long);
__LA_DECL void  archive_entry_unset_atime(struct archive_entry *);
#if defined(_WIN32) && !defined(__CYGWIN__)
__LA_DECL void archive_entry_copy_bhfi(struct archive_entry *, BY_HANDLE_FILE_INFORMATION *);
#endif
__LA_DECL void	archive_entry_set_birthtime(struct archive_entry *, __LA_TIME_T, long);
__LA_DECL void  archive_entry_unset_birthtime(struct archive_entry *);
__LA_DECL void	archive_entry_set_ctime(struct archive_entry *, __LA_TIME_T, long);
__LA_DECL void  archive_entry_unset_ctime(struct archive_entry *);
__LA_DECL void	archive_entry_set_dev(struct archive_entry *, __LA_DEV_T);
__LA_DECL void	archive_entry_set_devmajor(struct archive_entry *, __LA_DEV_T);
__LA_DECL void	archive_entry_set_devminor(struct archive_entry *, __LA_DEV_T);
__LA_DECL void	archive_entry_set_filetype(struct archive_entry *, unsigned int);
__LA_DECL void	archive_entry_set_fflags(struct archive_entry *,
	    unsigned long /* set */, unsigned long /* clear */);
/* Returns pointer to start of first invalid token, or NULL if none. */
/* Note that all recognized tokens are processed, regardless. */
__LA_DECL const char *archive_entry_copy_fflags_text(struct archive_entry *,
	    const char *);
__LA_DECL const char *archive_entry_copy_fflags_text_len(struct archive_entry *,
	    const char *, size_t);
__LA_DECL const wchar_t *archive_entry_copy_fflags_text_w(struct archive_entry *,
	    const wchar_t *);
__LA_DECL void	archive_entry_set_gid(struct archive_entry *, la_int64_t);
__LA_DECL void	archive_entry_set_gname(struct archive_entry *, const char *);
__LA_DECL void	archive_entry_set_gname_utf8(struct archive_entry *, const char *);
__LA_DECL void	archive_entry_copy_gname(struct archive_entry *, const char *);
__LA_DECL void	archive_entry_copy_gname_w(struct archive_entry *, const wchar_t *);
__LA_DECL int	archive_entry_update_gname_utf8(struct archive_entry *, const char *);
__LA_DECL void	archive_entry_set_hardlink(struct archive_entry *, const char *);
__LA_DECL void	archive_entry_set_hardlink_utf8(struct archive_entry *, const char *);
__LA_DECL void	archive_entry_copy_hardlink(struct archive_entry *, const char *);
__LA_DECL void	archive_entry_copy_hardlink_w(struct archive_entry *, const wchar_t *);
__LA_DECL int	archive_entry_update_hardlink_utf8(struct archive_entry *, const char *);
__LA_DECL void	archive_entry_set_ino(struct archive_entry *, __LA_INO_T);
__LA_DECL void	archive_entry_set_ino64(struct archive_entry *, __LA_INO_T);
__LA_DECL void	archive_entry_set_link(struct archive_entry *, const char *);
__LA_DECL void	archive_entry_set_link_utf8(struct archive_entry *, const char *);
__LA_DECL void	archive_entry_copy_link(struct archive_entry *, const char *);
__LA_DECL void	archive_entry_copy_link_w(struct archive_entry *, const wchar_t *);
__LA_DECL int	archive_entry_update_link_utf8(struct archive_entry *, const char *);
__LA_DECL void	archive_entry_set_mode(struct archive_entry *, __LA_MODE_T);
__LA_DECL void	archive_entry_set_mtime(struct archive_entry *, __LA_TIME_T, long);
__LA_DECL void  archive_entry_unset_mtime(struct archive_entry *);
__LA_DECL void	archive_entry_set_nlink(struct archive_entry *, unsigned int);
__LA_DECL void	archive_entry_set_pathname(struct archive_entry *, const char *);
__LA_DECL void	archive_entry_set_pathname_utf8(struct archive_entry *, const char *);
__LA_DECL void	archive_entry_copy_pathname(struct archive_entry *, const char *);
__LA_DECL void	archive_entry_copy_pathname_w(struct archive_entry *, const wchar_t *);
__LA_DECL int	archive_entry_update_pathname_utf8(struct archive_entry *, const char *);
__LA_DECL void	archive_entry_set_perm(struct archive_entry *, __LA_MODE_T);
__LA_DECL void	archive_entry_set_rdev(struct archive_entry *, __LA_DEV_T);
__LA_DECL void	archive_entry_set_rdevmajor(struct archive_entry *, __LA_DEV_T);
__LA_DECL void	archive_entry_set_rdevminor(struct archive_entry *, __LA_DEV_T);
__LA_DECL void	archive_entry_set_size(struct archive_entry *, la_int64_t);
__LA_DECL void	archive_entry_unset_size(struct archive_entry *);
__LA_DECL void	archive_entry_copy_sourcepath(struct archive_entry *, const char *);
__LA_DECL void	archive_entry_copy_sourcepath_w(struct archive_entry *, const wchar_t *);
__LA_DECL void	archive_entry_set_symlink(struct archive_entry *, const char *);
__LA_DECL void	archive_entry_set_symlink_type(struct archive_entry *, int);
__LA_DECL void	archive_entry_set_symlink_utf8(struct archive_entry *, const char *);
__LA_DECL void	archive_entry_copy_symlink(struct archive_entry *, const char *);
__LA_DECL void	archive_entry_copy_symlink_w(struct archive_entry *, const wchar_t *);
__LA_DECL int	archive_entry_update_symlink_utf8(struct archive_entry *, const char *);
__LA_DECL void	archive_entry_set_uid(struct archive_entry *, la_int64_t);
__LA_DECL void	archive_entry_set_uname(struct archive_entry *, const char *);
__LA_DECL void	archive_entry_set_uname_utf8(struct archive_entry *, const char *);
__LA_DECL void	archive_entry_copy_uname(struct archive_entry *, const char *);
__LA_DECL void	archive_entry_copy_uname_w(struct archive_entry *, const wchar_t *);
__LA_DECL int	archive_entry_update_uname_utf8(struct archive_entry *, const char *);
__LA_DECL void	archive_entry_set_is_data_encrypted(struct archive_entry *, char is_encrypted);
__LA_DECL void	archive_entry_set_is_metadata_encrypted(struct archive_entry *, char is_encrypted);
/*
 * Routines to bulk copy fields to/from a platform-native "struct
 * stat."  Libarchive used to just store a struct stat inside of each
 * archive_entry object, but this created issues when trying to
 * manipulate archives on systems different than the ones they were
 * created on.
 *
 * TODO: On Linux and other LFS systems, provide both stat32 and
 * stat64 versions of these functions and all of the macro glue so
 * that archive_entry_stat is magically defined to
 * archive_entry_stat32 or archive_entry_stat64 as appropriate.
 */
__LA_DECL const struct stat	*archive_entry_stat(struct archive_entry *);
__LA_DECL void	archive_entry_copy_stat(struct archive_entry *, const struct stat *);

/*
 * Storage for Mac OS-specific AppleDouble metadata information.
 * Apple-format tar files store a separate binary blob containing
 * encoded metadata with ACL, extended attributes, etc.
 * This provides a place to store that blob.
 */

__LA_DECL const void * archive_entry_mac_metadata(struct archive_entry *, size_t *);
__LA_DECL void archive_entry_copy_mac_metadata(struct archive_entry *, const void *, size_t);

/*
 * Digest routine. This is used to query the raw hex digest for the
 * given entry. The type of digest is provided as an argument.
 */
#define ARCHIVE_ENTRY_DIGEST_MD5              0x00000001
#define ARCHIVE_ENTRY_DIGEST_RMD160           0x00000002
#define ARCHIVE_ENTRY_DIGEST_SHA1             0x00000003
#define ARCHIVE_ENTRY_DIGEST_SHA256           0x00000004
#define ARCHIVE_ENTRY_DIGEST_SHA384           0x00000005
#define ARCHIVE_ENTRY_DIGEST_SHA512           0x00000006

__LA_DECL const unsigned char * archive_entry_digest(struct archive_entry *, int /* type */);
__LA_DECL int archive_entry_set_digest(struct archive_entry *, int, const unsigned char *);

/*
 * ACL routines.  This used to simply store and return text-format ACL
 * strings, but that proved insufficient for a number of reasons:
 *   = clients need control over uname/uid and gname/gid mappings
 *   = there are many different ACL text formats
 *   = would like to be able to read/convert archives containing ACLs
 *     on platforms that lack ACL libraries
 *
 *  This last point, in particular, forces me to implement a reasonably
 *  complete set of ACL support routines.
 */

/*
 * Permission bits.
 */
#define	ARCHIVE_ENTRY_ACL_EXECUTE             0x00000001
#define	ARCHIVE_ENTRY_ACL_WRITE               0x00000002
#define	ARCHIVE_ENTRY_ACL_READ                0x00000004
#define	ARCHIVE_ENTRY_ACL_READ_DATA           0x00000008
#define	ARCHIVE_ENTRY_ACL_LIST_DIRECTORY      0x00000008
#define	ARCHIVE_ENTRY_ACL_WRITE_DATA          0x00000010
#define	ARCHIVE_ENTRY_ACL_ADD_FILE            0x00000010
#define	ARCHIVE_ENTRY_ACL_APPEND_DATA         0x00000020
#define	ARCHIVE_ENTRY_ACL_ADD_SUBDIRECTORY    0x00000020
#define	ARCHIVE_ENTRY_ACL_READ_NAMED_ATTRS    0x00000040
#define	ARCHIVE_ENTRY_ACL_WRITE_NAMED_ATTRS   0x00000080
#define	ARCHIVE_ENTRY_ACL_DELETE_CHILD        0x00000100
#define	ARCHIVE_ENTRY_ACL_READ_ATTRIBUTES     0x00000200
#define	ARCHIVE_ENTRY_ACL_WRITE_ATTRIBUTES    0x00000400
#define	ARCHIVE_ENTRY_ACL_DELETE              0x00000800
#define	ARCHIVE_ENTRY_ACL_READ_ACL            0x00001000
#define	ARCHIVE_ENTRY_ACL_WRITE_ACL           0x00002000
#define	ARCHIVE_ENTRY_ACL_WRITE_OWNER         0x00004000
#define	ARCHIVE_ENTRY_ACL_SYNCHRONIZE         0x00008000

#define	ARCHIVE_ENTRY_ACL_PERMS_POSIX1E			\
	(ARCHIVE_ENTRY_ACL_EXECUTE			\
	    | ARCHIVE_ENTRY_ACL_WRITE			\
	    | ARCHIVE_ENTRY_ACL_READ)

#define ARCHIVE_ENTRY_ACL_PERMS_NFS4			\
	(ARCHIVE_ENTRY_ACL_EXECUTE			\
	    | ARCHIVE_ENTRY_ACL_READ_DATA		\
	    | ARCHIVE_ENTRY_ACL_LIST_DIRECTORY 		\
	    | ARCHIVE_ENTRY_ACL_WRITE_DATA		\
	    | ARCHIVE_ENTRY_ACL_ADD_FILE		\
	    | ARCHIVE_ENTRY_ACL_APPEND_DATA		\
	    | ARCHIVE_ENTRY_ACL_ADD_SUBDIRECTORY	\
	    | ARCHIVE_ENTRY_ACL_READ_NAMED_ATTRS	\
	    | ARCHIVE_ENTRY_ACL_WRITE_NAMED_ATTRS	\
	    | ARCHIVE_ENTRY_ACL_DELETE_CHILD		\
	    | ARCHIVE_ENTRY_ACL_READ_ATTRIBUTES		\
	    | ARCHIVE_ENTRY_ACL_WRITE_ATTRIBUTES	\
	    | ARCHIVE_ENTRY_ACL_DELETE			\
	    | ARCHIVE_ENTRY_ACL_READ_ACL		\
	    | ARCHIVE_ENTRY_ACL_WRITE_ACL		\
	    | ARCHIVE_ENTRY_ACL_WRITE_OWNER		\
	    | ARCHIVE_ENTRY_ACL_SYNCHRONIZE)

/*
 * Inheritance values (NFS4 ACLs only); included in permset.
 */
#define	ARCHIVE_ENTRY_ACL_ENTRY_INHERITED                   0x01000000
#define	ARCHIVE_ENTRY_ACL_ENTRY_FILE_INHERIT                0x02000000
#define	ARCHIVE_ENTRY_ACL_ENTRY_DIRECTORY_INHERIT           0x04000000
#define	ARCHIVE_ENTRY_ACL_ENTRY_NO_PROPAGATE_INHERIT        0x08000000
#define	ARCHIVE_ENTRY_ACL_ENTRY_INHERIT_ONLY                0x10000000
#define	ARCHIVE_ENTRY_ACL_ENTRY_SUCCESSFUL_ACCESS           0x20000000
#define	ARCHIVE_ENTRY_ACL_ENTRY_FAILED_ACCESS               0x40000000

#define	ARCHIVE_ENTRY_ACL_INHERITANCE_NFS4			\
	(ARCHIVE_ENTRY_ACL_ENTRY_FILE_INHERIT			\
	    | ARCHIVE_ENTRY_ACL_ENTRY_DIRECTORY_INHERIT		\
	    | ARCHIVE_ENTRY_ACL_ENTRY_NO_PROPAGATE_INHERIT	\
	    | ARCHIVE_ENTRY_ACL_ENTRY_INHERIT_ONLY		\
	    | ARCHIVE_ENTRY_ACL_ENTRY_SUCCESSFUL_ACCESS		\
	    | ARCHIVE_ENTRY_ACL_ENTRY_FAILED_ACCESS		\
	    | ARCHIVE_ENTRY_ACL_ENTRY_INHERITED)

/* We need to be able to specify combinations of these. */
#define	ARCHIVE_ENTRY_ACL_TYPE_ACCESS	0x00000100  /* POSIX.1e only */
#define	ARCHIVE_ENTRY_ACL_TYPE_DEFAULT	0x00000200  /* POSIX.1e only */
#define	ARCHIVE_ENTRY_ACL_TYPE_ALLOW	0x00000400 /* NFS4 only */
#define	ARCHIVE_ENTRY_ACL_TYPE_DENY	0x00000800 /* NFS4 only */
#define	ARCHIVE_ENTRY_ACL_TYPE_AUDIT	0x00001000 /* NFS4 only */
#define	ARCHIVE_ENTRY_ACL_TYPE_ALARM	0x00002000 /* NFS4 only */
#define	ARCHIVE_ENTRY_ACL_TYPE_POSIX1E	(ARCHIVE_ENTRY_ACL_TYPE_ACCESS \
	    | ARCHIVE_ENTRY_ACL_TYPE_DEFAULT)
#define	ARCHIVE_ENTRY_ACL_TYPE_NFS4	(ARCHIVE_ENTRY_ACL_TYPE_ALLOW \
	    | ARCHIVE_ENTRY_ACL_TYPE_DENY \
	    | ARCHIVE_ENTRY_ACL_TYPE_AUDIT \
	    | ARCHIVE_ENTRY_ACL_TYPE_ALARM)

/* Tag values mimic POSIX.1e */
#define	ARCHIVE_ENTRY_ACL_USER		10001	/* Specified user. */
#define	ARCHIVE_ENTRY_ACL_USER_OBJ 	10002	/* User who owns the file. */
#define	ARCHIVE_ENTRY_ACL_GROUP		10003	/* Specified group. */
#define	ARCHIVE_ENTRY_ACL_GROUP_OBJ	10004	/* Group who owns the file. */
#define	ARCHIVE_ENTRY_ACL_MASK		10005	/* Modify group access (POSIX.1e only) */
#define	ARCHIVE_ENTRY_ACL_OTHER		10006	/* Public (POSIX.1e only) */
#define	ARCHIVE_ENTRY_ACL_EVERYONE	10107   /* Everyone (NFS4 only) */

/*
 * Set the ACL by clearing it and adding entries one at a time.
 * Unlike the POSIX.1e ACL routines, you must specify the type
 * (access/default) for each entry.  Internally, the ACL data is just
 * a soup of entries.  API calls here allow you to retrieve just the
 * entries of interest.  This design (which goes against the spirit of
 * POSIX.1e) is useful for handling archive formats that combine
 * default and access information in a single ACL list.
 */
__LA_DECL void	 archive_entry_acl_clear(struct archive_entry *);
__LA_DECL int	 archive_entry_acl_add_entry(struct archive_entry *,
	    int /* type */, int /* permset */, int /* tag */,
	    int /* qual */, const char * /* name */);
__LA_DECL int	 archive_entry_acl_add_entry_w(struct archive_entry *,
	    int /* type */, int /* permset */, int /* tag */,
	    int /* qual */, const wchar_t * /* name */);

/*
 * To retrieve the ACL, first "reset", then repeatedly ask for the
 * "next" entry.  The want_type parameter allows you to request only
 * certain types of entries.
 */
__LA_DECL int	 archive_entry_acl_reset(struct archive_entry *, int /* want_type */);
__LA_DECL int	 archive_entry_acl_next(struct archive_entry *, int /* want_type */,
	    int * /* type */, int * /* permset */, int * /* tag */,
	    int * /* qual */, const char ** /* name */);

/*
 * Construct a text-format ACL.  The flags argument is a bitmask that
 * can include any of the following:
 *
 * Flags only for archive entries with POSIX.1e ACL:
 * ARCHIVE_ENTRY_ACL_TYPE_ACCESS - Include POSIX.1e "access" entries.
 * ARCHIVE_ENTRY_ACL_TYPE_DEFAULT - Include POSIX.1e "default" entries.
 * ARCHIVE_ENTRY_ACL_STYLE_MARK_DEFAULT - Include "default:" before each
 *    default ACL entry.
 * ARCHIVE_ENTRY_ACL_STYLE_SOLARIS - Output only one colon after "other" and
 *    "mask" entries.
 *
 * Flags only for archive entries with NFSv4 ACL:
 * ARCHIVE_ENTRY_ACL_STYLE_COMPACT - Do not output the minus character for
 *    unset permissions and flags in NFSv4 ACL permission and flag fields
 *
 * Flags for for archive entries with POSIX.1e ACL or NFSv4 ACL:
 * ARCHIVE_ENTRY_ACL_STYLE_EXTRA_ID - Include extra numeric ID field in
 *    each ACL entry.
 * ARCHIVE_ENTRY_ACL_STYLE_SEPARATOR_COMMA - Separate entries with comma
 *    instead of newline.
 */
#define	ARCHIVE_ENTRY_ACL_STYLE_EXTRA_ID	0x00000001
#define	ARCHIVE_ENTRY_ACL_STYLE_MARK_DEFAULT	0x00000002
#define	ARCHIVE_ENTRY_ACL_STYLE_SOLARIS		0x00000004
#define	ARCHIVE_ENTRY_ACL_STYLE_SEPARATOR_COMMA	0x00000008
#define	ARCHIVE_ENTRY_ACL_STYLE_COMPACT		0x00000010

__LA_DECL wchar_t *archive_entry_acl_to_text_w(struct archive_entry *,
	    la_ssize_t * /* len */, int /* flags */);
__LA_DECL char *archive_entry_acl_to_text(struct archive_entry *,
	    la_ssize_t * /* len */, int /* flags */);
__LA_DECL int archive_entry_acl_from_text_w(struct archive_entry *,
	    const wchar_t * /* wtext */, int /* type */);
__LA_DECL int archive_entry_acl_from_text(struct archive_entry *,
	    const char * /* text */, int /* type */);

/* Deprecated constants */
#define	OLD_ARCHIVE_ENTRY_ACL_STYLE_EXTRA_ID		1024
#define	OLD_ARCHIVE_ENTRY_ACL_STYLE_MARK_DEFAULT	2048

/* Deprecated functions */
__LA_DECL const wchar_t	*archive_entry_acl_text_w(struct archive_entry *,
		    int /* flags */) __LA_DEPRECATED;
__LA_DECL const char *archive_entry_acl_text(struct archive_entry *,
		    int /* flags */) __LA_DEPRECATED;

/* Return bitmask of ACL types in an archive entry */
__LA_DECL int	 archive_entry_acl_types(struct archive_entry *);

/* Return a count of entries matching 'want_type' */
__LA_DECL int	 archive_entry_acl_count(struct archive_entry *, int /* want_type */);

/* Return an opaque ACL object. */
/* There's not yet anything clients can actually do with this... */
struct archive_acl;
__LA_DECL struct archive_acl *archive_entry_acl(struct archive_entry *);

/*
 * extended attributes
 */

__LA_DECL void	 archive_entry_xattr_clear(struct archive_entry *);
__LA_DECL void	 archive_entry_xattr_add_entry(struct archive_entry *,
	    const char * /* name */, const void * /* value */,
	    size_t /* size */);

/*
 * To retrieve the xattr list, first "reset", then repeatedly ask for the
 * "next" entry.
 */

__LA_DECL int	archive_entry_xattr_count(struct archive_entry *);
__LA_DECL int	archive_entry_xattr_reset(struct archive_entry *);
__LA_DECL int	archive_entry_xattr_next(struct archive_entry *,
	    const char ** /* name */, const void ** /* value */, size_t *);

/*
 * sparse
 */

__LA_DECL void	 archive_entry_sparse_clear(struct archive_entry *);
__LA_DECL void	 archive_entry_sparse_add_entry(struct archive_entry *,
	    la_int64_t /* offset */, la_int64_t /* length */);

/*
 * To retrieve the xattr list, first "reset", then repeatedly ask for the
 * "next" entry.
 */

__LA_DECL int	archive_entry_sparse_count(struct archive_entry *);
__LA_DECL int	archive_entry_sparse_reset(struct archive_entry *);
__LA_DECL int	archive_entry_sparse_next(struct archive_entry *,
	    la_int64_t * /* offset */, la_int64_t * /* length */);

/*
 * Utility to match up hardlinks.
 *
 * The 'struct archive_entry_linkresolver' is a cache of archive entries
 * for files with multiple links.  Here's how to use it:
 *   1. Create a lookup object with archive_entry_linkresolver_new()
 *   2. Tell it the archive format you're using.
 *   3. Hand each archive_entry to archive_entry_linkify().
 *      That function will return 0, 1, or 2 entries that should
 *      be written.
 *   4. Call archive_entry_linkify(resolver, NULL) until
 *      no more entries are returned.
 *   5. Call archive_entry_linkresolver_free(resolver) to free resources.
 *
 * The entries returned have their hardlink and size fields updated
 * appropriately.  If an entry is passed in that does not refer to
 * a file with multiple links, it is returned unchanged.  The intention
 * is that you should be able to simply filter all entries through
 * this machine.
 *
 * To make things more efficient, be sure that each entry has a valid
 * nlinks value.  The hardlink cache uses this to track when all links
 * have been found.  If the nlinks value is zero, it will keep every
 * name in the cache indefinitely, which can use a lot of memory.
 *
 * Note that archive_entry_size() is reset to zero if the file
 * body should not be written to the archive.  Pay attention!
 */
struct archive_entry_linkresolver;

/*
 * There are three different strategies for marking hardlinks.
 * The descriptions below name them after the best-known
 * formats that rely on each strategy:
 *
 * "Old cpio" is the simplest, it always returns any entry unmodified.
 *    As far as I know, only cpio formats use this.  Old cpio archives
 *    store every link with the full body; the onus is on the dearchiver
 *    to detect and properly link the files as they are restored.
 * "tar" is also pretty simple; it caches a copy the first time it sees
 *    any link.  Subsequent appearances are modified to be hardlink
 *    references to the first one without any body.  Used by all tar
 *    formats, although the newest tar formats permit the "old cpio" strategy
 *    as well.  This strategy is very simple for the dearchiver,
 *    and reasonably straightforward for the archiver.
 * "new cpio" is trickier.  It stores the body only with the last
 *    occurrence.  The complication is that we might not
 *    see every link to a particular file in a single session, so
 *    there's no easy way to know when we've seen the last occurrence.
 *    The solution here is to queue one link until we see the next.
 *    At the end of the session, you can enumerate any remaining
 *    entries by calling archive_entry_linkify(NULL) and store those
 *    bodies.  If you have a file with three links l1, l2, and l3,
 *    you'll get the following behavior if you see all three links:
 *           linkify(l1) => NULL   (the resolver stores l1 internally)
 *           linkify(l2) => l1     (resolver stores l2, you write l1)
 *           linkify(l3) => l2, l3 (all links seen, you can write both).
 *    If you only see l1 and l2, you'll get this behavior:
 *           linkify(l1) => NULL
 *           linkify(l2) => l1
 *           linkify(NULL) => l2   (at end, you retrieve remaining links)
 *    As the name suggests, this strategy is used by newer cpio variants.
 *    It's noticeably more complex for the archiver, slightly more complex
 *    for the dearchiver than the tar strategy, but makes it straightforward
 *    to restore a file using any link by simply continuing to scan until
 *    you see a link that is stored with a body.  In contrast, the tar
 *    strategy requires you to rescan the archive from the beginning to
 *    correctly extract an arbitrary link.
 */

__LA_DECL struct archive_entry_linkresolver *archive_entry_linkresolver_new(void);
__LA_DECL void archive_entry_linkresolver_set_strategy(
	struct archive_entry_linkresolver *, int /* format_code */);
__LA_DECL void archive_entry_linkresolver_free(struct archive_entry_linkresolver *);
__LA_DECL void archive_entry_linkify(struct archive_entry_linkresolver *,
    struct archive_entry **, struct archive_entry **);
__LA_DECL struct archive_entry *archive_entry_partial_links(
    struct archive_entry_linkresolver *res, unsigned int *links);
#ifdef __cplusplus
}
#endif

/* This is meaningless outside of this header. */
#undef __LA_DECL

#endif /* !ARCHIVE_ENTRY_H_INCLUDED */

/* ==== archive_private.h ==== */
/*-
 * Copyright (c) 2003-2007 Tim Kientzle
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef ARCHIVE_PRIVATE_H_INCLUDED
#define ARCHIVE_PRIVATE_H_INCLUDED

#ifndef __LIBARCHIVE_BUILD
#ifndef __LIBARCHIVE_TEST
#error This header is only to be used internally to libarchive.
#endif
#endif

#if HAVE_ICONV_H
#include <iconv.h>
#endif


/* ==== archive_string.h ==== */
/*-
 * Copyright (c) 2003-2010 Tim Kientzle
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef ARCHIVE_STRING_H_INCLUDED
#define ARCHIVE_STRING_H_INCLUDED

#ifndef __LIBARCHIVE_BUILD
#ifndef __LIBARCHIVE_TEST
#error This header is only to be used internally to libarchive.
#endif
#endif

#include <stdarg.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>  /* required for wchar_t on some systems */
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_WCHAR_H
#include <wchar.h>
#endif



/*
 * Basic resizable/reusable string support similar to Java's "StringBuffer."
 *
 * Unlike sbuf(9), the buffers here are fully reusable and track the
 * length throughout.
 */

struct archive_string {
	char	*s;  /* Pointer to the storage */
	size_t	 length; /* Length of 's' in characters */
	size_t	 buffer_length; /* Length of malloc-ed storage in bytes. */
};

struct archive_wstring {
	wchar_t	*s;  /* Pointer to the storage */
	size_t	 length; /* Length of 's' in characters */
	size_t	 buffer_length; /* Length of malloc-ed storage in bytes. */
};

struct archive_string_conv;

/* Initialize an archive_string object on the stack or elsewhere. */
#define	archive_string_init(a)	\
	do { (a)->s = NULL; (a)->length = 0; (a)->buffer_length = 0; } while(0)

/* Append a C char to an archive_string, resizing as necessary. */
struct archive_string *
archive_strappend_char(struct archive_string *, char);

/* Ditto for a wchar_t and an archive_wstring. */
struct archive_wstring *
archive_wstrappend_wchar(struct archive_wstring *, wchar_t);

/* Append a raw array to an archive_string, resizing as necessary */
struct archive_string *
archive_array_append(struct archive_string *, const char *, size_t);

/* Convert a Unicode string to current locale and append the result. */
/* Returns -1 if conversion fails. */
int
archive_string_append_from_wcs(struct archive_string *, const wchar_t *, size_t);


/* Create a string conversion object.
 * Return NULL and set a error message if the conversion is not supported
 * on the platform. */
struct archive_string_conv *
archive_string_conversion_to_charset(struct archive *, const char *, int);
struct archive_string_conv *
archive_string_conversion_from_charset(struct archive *, const char *, int);
/* Create the default string conversion object for reading/writing an archive.
 * Return NULL if the conversion is unneeded.
 * Note: On non Windows platform this always returns NULL.
 */
struct archive_string_conv *
archive_string_default_conversion_for_read(struct archive *);
struct archive_string_conv *
archive_string_default_conversion_for_write(struct archive *);
/* Dispose of a string conversion object. */
void
archive_string_conversion_free(struct archive *);
const char *
archive_string_conversion_charset_name(struct archive_string_conv *);
void
archive_string_conversion_set_opt(struct archive_string_conv *, int);
#define SCONV_SET_OPT_UTF8_LIBARCHIVE2X	1
#define SCONV_SET_OPT_NORMALIZATION_C	2
#define SCONV_SET_OPT_NORMALIZATION_D	4


/* Copy one archive_string to another in locale conversion.
 * Return -1 if conversion fails. */
int
archive_strncpy_l(struct archive_string *, const void *, size_t,
    struct archive_string_conv *);

/* Copy one archive_string to another in locale conversion.
 * Return -1 if conversion fails. */
int
archive_strncat_l(struct archive_string *, const void *, size_t,
    struct archive_string_conv *);


/* Copy one archive_string to another */
#define	archive_string_copy(dest, src) \
	((dest)->length = 0, archive_string_concat((dest), (src)))
#define	archive_wstring_copy(dest, src) \
	((dest)->length = 0, archive_wstring_concat((dest), (src)))

/* Concatenate one archive_string to another */
void archive_string_concat(struct archive_string *dest, struct archive_string *src);
void archive_wstring_concat(struct archive_wstring *dest, struct archive_wstring *src);

/* Ensure that the underlying buffer is at least as large as the request. */
struct archive_string *
archive_string_ensure(struct archive_string *, size_t);
struct archive_wstring *
archive_wstring_ensure(struct archive_wstring *, size_t);

/* Append C string, which may lack trailing \0. */
/* The source is declared void * here because this gets used with
 * "signed char *", "unsigned char *" and "char *" arguments.
 * Declaring it "char *" as with some of the other functions just
 * leads to a lot of extra casts. */
struct archive_string *
archive_strncat(struct archive_string *, const void *, size_t);
struct archive_wstring *
archive_wstrncat(struct archive_wstring *, const wchar_t *, size_t);

/* Append a C string to an archive_string, resizing as necessary. */
struct archive_string *
archive_strcat(struct archive_string *, const void *);
struct archive_wstring *
archive_wstrcat(struct archive_wstring *, const wchar_t *);

/* Copy a C string to an archive_string, resizing as necessary. */
#define	archive_strcpy(as,p) \
	archive_strncpy((as), (p), ((p) == NULL ? 0 : strlen(p)))
#define	archive_wstrcpy(as,p) \
	archive_wstrncpy((as), (p), ((p) == NULL ? 0 : wcslen(p)))
#define	archive_strcpy_l(as,p,lo) \
	archive_strncpy_l((as), (p), ((p) == NULL ? 0 : strlen(p)), (lo))

/* Copy a C string to an archive_string with limit, resizing as necessary. */
#define	archive_strncpy(as,p,l) \
	((as)->length=0, archive_strncat((as), (p), (l)))
#define	archive_wstrncpy(as,p,l) \
	((as)->length = 0, archive_wstrncat((as), (p), (l)))

/* Return length of string. */
#define	archive_strlen(a) ((a)->length)

/* Set string length to zero. */
#define	archive_string_empty(a) ((a)->length = 0)
#define	archive_wstring_empty(a) ((a)->length = 0)

/* Release any allocated storage resources. */
void	archive_string_free(struct archive_string *);
void	archive_wstring_free(struct archive_wstring *);

/* Like 'vsprintf', but resizes the underlying string as necessary. */
/* Note: This only implements a small subset of standard printf functionality. */
void	archive_string_vsprintf(struct archive_string *, const char *,
	    va_list) __LA_PRINTF(2, 0);
void	archive_string_sprintf(struct archive_string *, const char *, ...)
	    __LA_PRINTF(2, 3);

/* Translates from MBS to Unicode. */
/* Returns non-zero if conversion failed in any way. */
int archive_wstring_append_from_mbs(struct archive_wstring *dest,
    const char *, size_t);


/* A "multistring" can hold Unicode, UTF8, or MBS versions of
 * the string.  If you set and read the same version, no translation
 * is done.  If you set and read different versions, the library
 * will attempt to transparently convert.
 */
struct archive_mstring {
	struct archive_string aes_mbs;
	struct archive_string aes_utf8;
	struct archive_wstring aes_wcs;
	struct archive_string aes_mbs_in_locale;
	/* Bitmap of which of the above are valid.  Because we're lazy
	 * about malloc-ing and reusing the underlying storage, we
	 * can't rely on NULL pointers to indicate whether a string
	 * has been set. */
	int aes_set;
#define	AES_SET_MBS 1
#define	AES_SET_UTF8 2
#define	AES_SET_WCS 4
};

void	archive_mstring_clean(struct archive_mstring *);
void	archive_mstring_copy(struct archive_mstring *dest, struct archive_mstring *src);
int archive_mstring_get_mbs(struct archive *, struct archive_mstring *, const char **);
int archive_mstring_get_utf8(struct archive *, struct archive_mstring *, const char **);
int archive_mstring_get_wcs(struct archive *, struct archive_mstring *, const wchar_t **);
int	archive_mstring_get_mbs_l(struct archive *, struct archive_mstring *, const char **,
	    size_t *, struct archive_string_conv *);
int	archive_mstring_copy_mbs(struct archive_mstring *, const char *mbs);
int	archive_mstring_copy_mbs_len(struct archive_mstring *, const char *mbs,
	    size_t);
int	archive_mstring_copy_utf8(struct archive_mstring *, const char *utf8);
int	archive_mstring_copy_wcs(struct archive_mstring *, const wchar_t *wcs);
int	archive_mstring_copy_wcs_len(struct archive_mstring *,
	    const wchar_t *wcs, size_t);
int	archive_mstring_copy_mbs_len_l(struct archive_mstring *,
	    const char *mbs, size_t, struct archive_string_conv *);
int     archive_mstring_update_utf8(struct archive *, struct archive_mstring *aes, const char *utf8);


#endif

#if defined(__GNUC__) && (__GNUC__ > 2 || \
						  (__GNUC__ == 2 && __GNUC_MINOR__ >= 5))
#define __LA_NORETURN __attribute__((__noreturn__))
#elif defined(_MSC_VER)
#define __LA_NORETURN __declspec(noreturn)
#else
#define __LA_NORETURN
#endif

#if defined(__GNUC__) && (__GNUC__ > 2 || \
			  (__GNUC__ == 2 && __GNUC_MINOR__ >= 7))
#define	__LA_UNUSED	__attribute__((__unused__))
#else
#define	__LA_UNUSED
#endif

#define	ARCHIVE_WRITE_MAGIC	(0xb0c5c0deU)
#define	ARCHIVE_READ_MAGIC	(0xdeb0c5U)
#define	ARCHIVE_WRITE_DISK_MAGIC (0xc001b0c5U)
#define	ARCHIVE_READ_DISK_MAGIC (0xbadb0c5U)
#define	ARCHIVE_MATCH_MAGIC	(0xcad11c9U)

#define	ARCHIVE_STATE_NEW	1U
#define	ARCHIVE_STATE_HEADER	2U
#define	ARCHIVE_STATE_DATA	4U
#define	ARCHIVE_STATE_EOF	0x10U
#define	ARCHIVE_STATE_CLOSED	0x20U
#define	ARCHIVE_STATE_FATAL	0x8000U
#define	ARCHIVE_STATE_ANY	(0xFFFFU & ~ARCHIVE_STATE_FATAL)

struct archive_vtable {
	int	(*archive_close)(struct archive *);
	int	(*archive_free)(struct archive *);
	int	(*archive_write_header)(struct archive *,
	    struct archive_entry *);
	int	(*archive_write_finish_entry)(struct archive *);
	ssize_t	(*archive_write_data)(struct archive *,
	    const void *, size_t);
	ssize_t	(*archive_write_data_block)(struct archive *,
	    const void *, size_t, int64_t);

	int	(*archive_read_next_header)(struct archive *,
	    struct archive_entry **);
	int	(*archive_read_next_header2)(struct archive *,
	    struct archive_entry *);
	int	(*archive_read_data_block)(struct archive *,
	    const void **, size_t *, int64_t *);

	int	(*archive_filter_count)(struct archive *);
	int64_t (*archive_filter_bytes)(struct archive *, int);
	int	(*archive_filter_code)(struct archive *, int);
	const char * (*archive_filter_name)(struct archive *, int);
};

struct archive_string_conv;

struct archive {
	/*
	 * The magic/state values are used to sanity-check the
	 * client's usage.  If an API function is called at a
	 * ridiculous time, or the client passes us an invalid
	 * pointer, these values allow me to catch that.
	 */
	unsigned int	magic;
	unsigned int	state;

	/*
	 * Some public API functions depend on the "real" type of the
	 * archive object.
	 */
	const struct archive_vtable *vtable;

	int		  archive_format;
	const char	 *archive_format_name;

	/* Number of file entries processed. */
	int		  file_count;

	int		  archive_error_number;
	const char	 *error;
	struct archive_string	error_string;

	char *current_code;
	unsigned current_codepage; /* Current ACP(ANSI CodePage). */
	unsigned current_oemcp; /* Current OEMCP(OEM CodePage). */
	struct archive_string_conv *sconv;

	/*
	 * Used by archive_read_data() to track blocks and copy
	 * data to client buffers, filling gaps with zero bytes.
	 */
	const char	 *read_data_block;
	int64_t		  read_data_offset;
	int64_t		  read_data_output_offset;
	size_t		  read_data_remaining;

	/*
	 * Used by formats/filters to determine the amount of data
	 * requested from a call to archive_read_data(). This is only
	 * useful when the format/filter has seek support.
	 */
	char		  read_data_is_posix_read;
	size_t		  read_data_requested;
};

/* Check magic value and state; return(ARCHIVE_FATAL) if it isn't valid. */
int	__archive_check_magic(struct archive *, unsigned int magic,
	    unsigned int state, const char *func);
#define	archive_check_magic(a, expected_magic, allowed_states, function_name) \
	do { \
		int magic_test = __archive_check_magic((a), (expected_magic), \
			(allowed_states), (function_name)); \
		if (magic_test == ARCHIVE_FATAL) \
			return ARCHIVE_FATAL; \
	} while (0)

__LA_NORETURN void	__archive_errx(int retvalue, const char *msg);

void	__archive_ensure_cloexec_flag(int fd);
int	__archive_mktemp(const char *tmpdir);
#if defined(_WIN32) && !defined(__CYGWIN__)
int	__archive_mkstemp(wchar_t *templates);
#else
int	__archive_mkstemp(char *templates);
#endif

int	__archive_clean(struct archive *);

void __archive_reset_read_data(struct archive *);

#define	err_combine(a,b)	((a) < (b) ? (a) : (b))

#if defined(__BORLANDC__) || (defined(_MSC_VER) &&  _MSC_VER <= 1300)
# define	ARCHIVE_LITERAL_LL(x)	x##i64
# define	ARCHIVE_LITERAL_ULL(x)	x##ui64
#else
# define	ARCHIVE_LITERAL_LL(x)	x##ll
# define	ARCHIVE_LITERAL_ULL(x)	x##ull
#endif

#endif

/* ==== archive_read_private.h ==== */
/*-
 * Copyright (c) 2003-2007 Tim Kientzle
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef ARCHIVE_READ_PRIVATE_H_INCLUDED
#define ARCHIVE_READ_PRIVATE_H_INCLUDED

#ifndef __LIBARCHIVE_BUILD
#ifndef __LIBARCHIVE_TEST
#error This header is only to be used internally to libarchive.
#endif
#endif





struct archive_read;
struct archive_read_filter_bidder;
struct archive_read_filter;

struct archive_read_filter_bidder_vtable {
	/* Taste the upstream filter to see if we handle this. */
	int (*bid)(struct archive_read_filter_bidder *,
	    struct archive_read_filter *);
	/* Initialize a newly-created filter. */
	int (*init)(struct archive_read_filter *);
	/* Release the bidder's configuration data. */
	void (*free)(struct archive_read_filter_bidder *);
};

/*
 * How bidding works for filters:
 *   * The bid manager initializes the client-provided reader as the
 *     first filter.
 *   * It invokes the bidder for each registered filter with the
 *     current head filter.
 *   * The bidders can use archive_read_filter_ahead() to peek ahead
 *     at the incoming data to compose their bids.
 *   * The bid manager creates a new filter structure for the winning
 *     bidder and gives the winning bidder a chance to initialize it.
 *   * The new filter becomes the new top filter and we repeat the
 *     process.
 * This ends only when no bidder provides a non-zero bid.  Then
 * we perform a similar dance with the registered format handlers.
 */
struct archive_read_filter_bidder {
	/* Configuration data for the bidder. */
	void *data;
	/* Name of the filter */
	const char *name;
	const struct archive_read_filter_bidder_vtable *vtable;
};

struct archive_read_filter_vtable {
	/* Return next block. */
	ssize_t (*read)(struct archive_read_filter *, const void **);
	/* Close (just this filter) and free(self). */
	int (*close)(struct archive_read_filter *self);
	/* Read any header metadata if available. */
	int (*read_header)(struct archive_read_filter *self, struct archive_entry *entry);
};

/*
 * This structure is allocated within the archive_read core
 * and initialized by archive_read and the init() method of the
 * corresponding bidder above.
 */
struct archive_read_filter {
	int64_t position;
	/* Essentially all filters will need these values, so
	 * just declare them here. */
	struct archive_read_filter_bidder *bidder; /* My bidder. */
	struct archive_read_filter *upstream; /* Who I read from. */
	struct archive_read *archive; /* Associated archive. */
	const struct archive_read_filter_vtable *vtable;
	/* My private data. */
	void *data;

	const char	*name;
	int		 code;
	int		 can_skip;
	int		 can_seek;

	/* Used by reblocking logic. */
	char		*buffer;
	size_t		 buffer_size;
	char		*next;		/* Current read location. */
	size_t		 avail;		/* Bytes in my buffer. */
	const void	*client_buff;	/* Client buffer information. */
	size_t		 client_total;
	const char	*client_next;
	size_t		 client_avail;
	char		 end_of_file;
	char		 closed;
	char		 fatal;
};

/*
 * The client looks a lot like a filter, so we just wrap it here.
 *
 * TODO: Make archive_read_filter and archive_read_client identical so
 * that users of the library can easily register their own
 * transformation filters.  This will probably break the API/ABI and
 * so should be deferred at least until libarchive 3.0.
 */
struct archive_read_data_node {
	int64_t begin_position;
	int64_t total_size;
	void *data;
};
struct archive_read_client {
	archive_open_callback	*opener;
	archive_read_callback	*reader;
	archive_skip_callback	*skipper;
	archive_seek_callback	*seeker;
	archive_close_callback	*closer;
	archive_switch_callback *switcher;
	unsigned int nodes;
	unsigned int cursor;
	int64_t position;
	struct archive_read_data_node *dataset;
};
struct archive_read_passphrase {
	char	*passphrase;
	struct archive_read_passphrase *next;
};

struct archive_read_extract {
	struct archive *ad; /* archive_write_disk object */

	/* Progress function invoked during extract. */
	void			(*extract_progress)(void *);
	void			 *extract_progress_user_data;
};

struct archive_read {
	struct archive	archive;

	struct archive_entry	*entry;

	/* Dev/ino of the archive being read/written. */
	int		  skip_file_set;
	int64_t		  skip_file_dev;
	int64_t		  skip_file_ino;

	/* Callbacks to open/read/write/close client archive streams. */
	struct archive_read_client client;

	/* Registered filter bidders. */
	struct archive_read_filter_bidder bidders[16];

	/* Last filter in chain */
	struct archive_read_filter *filter;

	/* Whether to bypass filter bidding process */
	int bypass_filter_bidding;

	/* File offset of beginning of most recently-read header. */
	int64_t		  header_position;

	/* Nodes and offsets of compressed data block */
	unsigned int data_start_node;
	unsigned int data_end_node;

	/*
	 * Format detection is mostly the same as compression
	 * detection, with one significant difference: The bidders
	 * use the read_ahead calls above to examine the stream rather
	 * than having the supervisor hand them a block of data to
	 * examine.
	 */

	struct archive_format_descriptor {
		void	 *data;
		const char *name;
		int	(*bid)(struct archive_read *, int best_bid);
		int	(*options)(struct archive_read *, const char *key,
		    const char *value);
		int	(*read_header)(struct archive_read *, struct archive_entry *);
		int	(*read_data)(struct archive_read *, const void **, size_t *, int64_t *);
		int	(*read_data_skip)(struct archive_read *);
		int64_t	(*seek_data)(struct archive_read *, int64_t, int);
		int	(*cleanup)(struct archive_read *);
		int	(*format_capabilties)(struct archive_read *);
		int	(*has_encrypted_entries)(struct archive_read *);
	}	formats[16];
	struct archive_format_descriptor	*format; /* Active format. */

	/*
	 * Various information needed by archive_extract.
	 */
	struct archive_read_extract		*extract;
	int			(*cleanup_archive_extract)(struct archive_read *);

	/*
	 * Decryption passphrase.
	 */
	struct {
		struct archive_read_passphrase *first;
		struct archive_read_passphrase **last;
		int candidate;
		archive_passphrase_callback *callback;
		void *client_data;
	}		passphrases;
};

int	__archive_read_register_format(struct archive_read *a,
		void *format_data,
		const char *name,
		int (*bid)(struct archive_read *, int),
		int (*options)(struct archive_read *, const char *, const char *),
		int (*read_header)(struct archive_read *, struct archive_entry *),
		int (*read_data)(struct archive_read *, const void **, size_t *, int64_t *),
		int (*read_data_skip)(struct archive_read *),
		int64_t (*seek_data)(struct archive_read *, int64_t, int),
		int (*cleanup)(struct archive_read *),
		int (*format_capabilities)(struct archive_read *),
		int (*has_encrypted_entries)(struct archive_read *));

int __archive_read_register_bidder(struct archive_read *a,
		void *bidder_data,
		const char *name,
		const struct archive_read_filter_bidder_vtable *vtable);

const void *__archive_read_ahead(struct archive_read *, size_t, ssize_t *);
const void *__archive_read_filter_ahead(struct archive_read_filter *,
    size_t, ssize_t *);
int64_t	__archive_read_seek(struct archive_read*, int64_t, int);
int64_t	__archive_read_filter_seek(struct archive_read_filter *, int64_t, int);
int64_t	__archive_read_consume(struct archive_read *, int64_t);
int64_t	__archive_read_filter_consume(struct archive_read_filter *, int64_t);
int __archive_read_header(struct archive_read *, struct archive_entry *);
int __archive_read_program(struct archive_read_filter *, const char *);
void __archive_read_free_filters(struct archive_read *);
struct archive_read_extract *__archive_read_get_extract(struct archive_read *);


/*
 * Get a decryption passphrase.
 */
void __archive_read_reset_passphrase(struct archive_read *a);
const char * __archive_read_next_passphrase(struct archive_read *a);
#endif

/* ==== archive_entry_locale.h ==== */
/*-
 * Copyright (c) 2011 Michihiro NAKAJIMA
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef ARCHIVE_ENTRY_LOCALE_H_INCLUDED
#define ARCHIVE_ENTRY_LOCALE_H_INCLUDED

#ifndef __LIBARCHIVE_BUILD
#error This header is only to be used internally to libarchive.
#endif

struct archive_entry;
struct archive_string_conv;

/*
 * Utility functions to set and get entry attributes by translating
 * character-set. These are designed for use in format readers and writers.
 *
 * The return code and interface of these are quite different from other
 * functions for archive_entry defined in archive_entry.h.
 * Common return code are:
 *   Return 0 if the string conversion succeeded.
 *   Return -1 if the string conversion failed.
 */

#define archive_entry_gname_l	_archive_entry_gname_l
int _archive_entry_gname_l(struct archive_entry *,
    const char **, size_t *, struct archive_string_conv *);
#define archive_entry_hardlink_l	_archive_entry_hardlink_l
int _archive_entry_hardlink_l(struct archive_entry *,
    const char **, size_t *, struct archive_string_conv *);
#define archive_entry_pathname_l	_archive_entry_pathname_l
int _archive_entry_pathname_l(struct archive_entry *,
    const char **, size_t *, struct archive_string_conv *);
#define archive_entry_symlink_l	_archive_entry_symlink_l
int _archive_entry_symlink_l(struct archive_entry *,
    const char **, size_t *, struct archive_string_conv *);
#define archive_entry_uname_l	_archive_entry_uname_l
int _archive_entry_uname_l(struct archive_entry *,
    const char **, size_t *, struct archive_string_conv *);
#define archive_entry_acl_text_l _archive_entry_acl_text_l
int _archive_entry_acl_text_l(struct archive_entry *, int,
const char **, size_t *, struct archive_string_conv *) __LA_DEPRECATED;
#define archive_entry_acl_to_text_l _archive_entry_acl_to_text_l
char *_archive_entry_acl_to_text_l(struct archive_entry *, ssize_t *, int,
    struct archive_string_conv *);
#define archive_entry_acl_from_text_l _archive_entry_acl_from_text_l
int _archive_entry_acl_from_text_l(struct archive_entry *, const char* text,
    int type, struct archive_string_conv *);
#define archive_entry_copy_gname_l	_archive_entry_copy_gname_l
int _archive_entry_copy_gname_l(struct archive_entry *,
    const char *, size_t, struct archive_string_conv *);
#define archive_entry_copy_hardlink_l	_archive_entry_copy_hardlink_l
int _archive_entry_copy_hardlink_l(struct archive_entry *,
    const char *, size_t, struct archive_string_conv *);
#define archive_entry_copy_link_l	_archive_entry_copy_link_l
int _archive_entry_copy_link_l(struct archive_entry *,
    const char *, size_t, struct archive_string_conv *);
#define archive_entry_copy_pathname_l	_archive_entry_copy_pathname_l
int _archive_entry_copy_pathname_l(struct archive_entry *,
    const char *, size_t, struct archive_string_conv *);
#define archive_entry_copy_symlink_l	_archive_entry_copy_symlink_l
int _archive_entry_copy_symlink_l(struct archive_entry *,
    const char *, size_t, struct archive_string_conv *);
#define archive_entry_copy_uname_l	_archive_entry_copy_uname_l
int _archive_entry_copy_uname_l(struct archive_entry *,
    const char *, size_t, struct archive_string_conv *);

#endif /* !ARCHIVE_ENTRY_LOCALE_H_INCLUDED */

#ifdef RAR_IMPLEMENTATION

/* ==== archive_read_support_format_rar.c ==== */
#define parse_filter rar_amalg_archive_read_support_format_rar_parse_filter
/*-
* Copyright (c) 2003-2007 Tim Kientzle
* Copyright (c) 2011 Andres Mejia
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions
* are met:
* 1. Redistributions of source code must retain the above copyright
*    notice, this list of conditions and the following disclaimer.
* 2. Redistributions in binary form must reproduce the above copyright
*    notice, this list of conditions and the following disclaimer in the
*    documentation and/or other materials provided with the distribution.
*
* THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
* IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
* OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
* IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
* INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
* NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
* DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
* THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
* THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/



#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#include <time.h>
#include <limits.h>
#ifdef HAVE_ZLIB_H
#include <zlib.h> /* crc32 */
#endif


#ifndef HAVE_ZLIB_H
/* ==== archive_crc32.h ==== */
/*-
 * Copyright (c) 2009 Joerg  Sonnenberger
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef ARCHIVE_CRC32_H
#define ARCHIVE_CRC32_H

#ifndef __LIBARCHIVE_BUILD
#error This header is only to be used internally to libarchive.
#endif

#include <stddef.h>

/*
 * When zlib is unavailable, we should still be able to validate
 * uncompressed zip archives.  That requires us to be able to compute
 * the CRC32 check value.  This is a drop-in compatible replacement
 * for crc32() from zlib.  It's slower than the zlib implementation,
 * but still pretty fast: This runs about 300MB/s on my 3GHz P4
 * compared to about 800MB/s for the zlib implementation.
 */
static unsigned long
crc32(unsigned long crc, const void *_p, size_t len)
{
	unsigned long crc2, b, i;
	const unsigned char *p = _p;
	static volatile int crc_tbl_inited = 0;
	static unsigned long crc_tbl[256];

	if (_p == NULL)
		return (0);

	if (!crc_tbl_inited) {
		for (b = 0; b < 256; ++b) {
			crc2 = b;
			for (i = 8; i > 0; --i) {
				if (crc2 & 1)
					crc2 = (crc2 >> 1) ^ 0xedb88320UL;
				else    
					crc2 = (crc2 >> 1);
			}
			crc_tbl[b] = crc2;
		}
		crc_tbl_inited = 1;
	}

	crc = crc ^ 0xffffffffUL;
	/* A use of this loop is about 20% - 30% faster than
	 * no use version in any optimization option of gcc.  */
	for (;len >= 8; len -= 8) {
		crc = crc_tbl[(crc ^ *p++) & 0xff] ^ (crc >> 8);
		crc = crc_tbl[(crc ^ *p++) & 0xff] ^ (crc >> 8);
		crc = crc_tbl[(crc ^ *p++) & 0xff] ^ (crc >> 8);
		crc = crc_tbl[(crc ^ *p++) & 0xff] ^ (crc >> 8);
		crc = crc_tbl[(crc ^ *p++) & 0xff] ^ (crc >> 8);
		crc = crc_tbl[(crc ^ *p++) & 0xff] ^ (crc >> 8);
		crc = crc_tbl[(crc ^ *p++) & 0xff] ^ (crc >> 8);
		crc = crc_tbl[(crc ^ *p++) & 0xff] ^ (crc >> 8);
	}
	while (len--)
		crc = crc_tbl[(crc ^ *p++) & 0xff] ^ (crc >> 8);
	return (crc ^ 0xffffffffUL);
}

#endif
#endif
/* ==== archive_endian.h ==== */
/*-
 * Copyright (c) 2002 Thomas Moestl <tmm@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Borrowed from FreeBSD's <sys/endian.h>
 */

#ifndef ARCHIVE_ENDIAN_H_INCLUDED
#define ARCHIVE_ENDIAN_H_INCLUDED

/* Note:  This is a purely internal header! */
/* Do not use this outside of libarchive internal code! */

#ifndef __LIBARCHIVE_BUILD
#error This header is only to be used internally to libarchive.
#endif

/*
 * Disabling inline keyword for compilers known to choke on it:
 * - Watcom C++ in C code.  (For any version?)
 * - SGI MIPSpro
 * - Microsoft Visual C++ 6.0 (supposedly newer versions too)
 * - IBM VisualAge 6 (XL v6)
 * - Sun WorkShop C (SunPro) before 5.9
 */
#if defined(__WATCOMC__) || defined(__sgi) || defined(__hpux) || defined(__BORLANDC__)
#define	inline
#elif defined(__IBMC__) && __IBMC__ < 700
#define	inline
#elif defined(__SUNPRO_C) && __SUNPRO_C < 0x590
#define inline
#elif defined(_MSC_VER) || defined(__osf__)
#define inline __inline
#endif

/* Alignment-agnostic encode/decode bytestream to/from little/big endian. */

static uint16_t
archive_be16dec(const void *pp)
{
	unsigned char const *p = (unsigned char const *)pp;

	/* Store into unsigned temporaries before left shifting, to avoid
	promotion to signed int and then left shifting into the sign bit,
	which is undefined behaviour. */
	unsigned int p1 = p[1];
	unsigned int p0 = p[0];

	return ((p0 << 8) | p1);
}

static uint32_t
archive_be32dec(const void *pp)
{
	unsigned char const *p = (unsigned char const *)pp;

	/* Store into unsigned temporaries before left shifting, to avoid
	promotion to signed int and then left shifting into the sign bit,
	which is undefined behaviour. */
	unsigned int p3 = p[3];
	unsigned int p2 = p[2];
	unsigned int p1 = p[1];
	unsigned int p0 = p[0];

	return ((p0 << 24) | (p1 << 16) | (p2 << 8) | p3);
}

static uint64_t
archive_be64dec(const void *pp)
{
	unsigned char const *p = (unsigned char const *)pp;

	return (((uint64_t)archive_be32dec(p) << 32) | archive_be32dec(p + 4));
}

static uint16_t
archive_le16dec(const void *pp)
{
	unsigned char const *p = (unsigned char const *)pp;

	/* Store into unsigned temporaries before left shifting, to avoid
	promotion to signed int and then left shifting into the sign bit,
	which is undefined behaviour. */
	unsigned int p1 = p[1];
	unsigned int p0 = p[0];

	return ((p1 << 8) | p0);
}

static uint32_t
archive_le32dec(const void *pp)
{
	unsigned char const *p = (unsigned char const *)pp;

	/* Store into unsigned temporaries before left shifting, to avoid
	promotion to signed int and then left shifting into the sign bit,
	which is undefined behaviour. */
	unsigned int p3 = p[3];
	unsigned int p2 = p[2];
	unsigned int p1 = p[1];
	unsigned int p0 = p[0];

	return ((p3 << 24) | (p2 << 16) | (p1 << 8) | p0);
}

static uint64_t
archive_le64dec(const void *pp)
{
	unsigned char const *p = (unsigned char const *)pp;

	return (((uint64_t)archive_le32dec(p + 4) << 32) | archive_le32dec(p));
}

static void
archive_be16enc(void *pp, uint16_t u)
{
	unsigned char *p = (unsigned char *)pp;

	p[0] = (u >> 8) & 0xff;
	p[1] = u & 0xff;
}

static void
archive_be32enc(void *pp, uint32_t u)
{
	unsigned char *p = (unsigned char *)pp;

	p[0] = (u >> 24) & 0xff;
	p[1] = (u >> 16) & 0xff;
	p[2] = (u >> 8) & 0xff;
	p[3] = u & 0xff;
}

static void
archive_be64enc(void *pp, uint64_t u)
{
	unsigned char *p = (unsigned char *)pp;

	archive_be32enc(p, (uint32_t)(u >> 32));
	archive_be32enc(p + 4, (uint32_t)(u & 0xffffffff));
}

static void
archive_le16enc(void *pp, uint16_t u)
{
	unsigned char *p = (unsigned char *)pp;

	p[0] = u & 0xff;
	p[1] = (u >> 8) & 0xff;
}

static void
archive_le32enc(void *pp, uint32_t u)
{
	unsigned char *p = (unsigned char *)pp;

	p[0] = u & 0xff;
	p[1] = (u >> 8) & 0xff;
	p[2] = (u >> 16) & 0xff;
	p[3] = (u >> 24) & 0xff;
}

static void
archive_le64enc(void *pp, uint64_t u)
{
	unsigned char *p = (unsigned char *)pp;

	archive_le32enc(p, (uint32_t)(u & 0xffffffff));
	archive_le32enc(p + 4, (uint32_t)(u >> 32));
}

#endif


/* ==== archive_ppmd7_private.h ==== */
/* Ppmd7.h -- PPMdH compression codec
2010-03-12 : Igor Pavlov : Public domain
This code is based on PPMd var.H (2001): Dmitry Shkarin : Public domain */

/* This code supports virtual RangeDecoder and includes the implementation
of RangeCoder from 7z, instead of RangeCoder from original PPMd var.H.
If you need the compatibility with original PPMd var.H, you can use external RangeDecoder */

#ifndef ARCHIVE_PPMD7_PRIVATE_H_INCLUDED
#define ARCHIVE_PPMD7_PRIVATE_H_INCLUDED

#ifndef __LIBARCHIVE_BUILD
#error This header is only to be used internally to libarchive.
#endif

/* ==== archive_ppmd_private.h ==== */
/* Ppmd.h -- PPMD codec common code
2010-03-12 : Igor Pavlov : Public domain
This code is based on PPMd var.H (2001): Dmitry Shkarin : Public domain */

#ifndef ARCHIVE_PPMD_PRIVATE_H_INCLUDED
#define ARCHIVE_PPMD_PRIVATE_H_INCLUDED

#ifndef __LIBARCHIVE_BUILD
#error This header is only to be used internally to libarchive.
#endif

#include <stddef.h>



/*** Begin defined in Types.h ***/

#if !defined(ZCONF_H)
typedef unsigned char Byte;
#endif
typedef short Int16;
typedef unsigned short UInt16;

#ifdef _LZMA_UINT32_IS_ULONG
typedef long Int32;
typedef unsigned long UInt32;
#else
typedef int Int32;
typedef unsigned int UInt32;
#endif

#ifdef _SZ_NO_INT_64

/* define _SZ_NO_INT_64, if your compiler doesn't support 64-bit integers.
   NOTES: Some code will work incorrectly in that case! */

typedef long Int64;
typedef unsigned long UInt64;

#else

#if defined(_MSC_VER) || defined(__BORLANDC__)
typedef __int64 Int64;
typedef unsigned __int64 UInt64;
#define UINT64_CONST(n) n
#else
typedef long long int Int64;
typedef unsigned long long int UInt64;
#define UINT64_CONST(n) n ## ULL
#endif

#endif

typedef int Bool;
#define True 1
#define False 0

/* The following interfaces use first parameter as pointer to structure */

typedef struct
{
  struct archive_read *a;
  Byte (*Read)(void *p); /* reads one byte, returns 0 in case of EOF or error */
} IByteIn;

typedef struct
{
  struct archive_write *a;
  void (*Write)(void *p, Byte b);
} IByteOut;

/*** End defined in Types.h ***/
/*** Begin defined in CpuArch.h ***/

#if defined(_M_IX86) || defined(__i386__)
#define MY_CPU_X86
#endif

#if defined(MY_CPU_X86) || defined(_M_ARM)
#define MY_CPU_32BIT
#endif

#ifdef MY_CPU_32BIT
#define PPMD_32BIT
#endif

/*** End defined in CpuArch.h ***/

#define PPMD_INT_BITS 7
#define PPMD_PERIOD_BITS 7
#define PPMD_BIN_SCALE (1 << (PPMD_INT_BITS + PPMD_PERIOD_BITS))

#define PPMD_GET_MEAN_SPEC(summ, shift, round) (((summ) + (1 << ((shift) - (round)))) >> (shift))
#define PPMD_GET_MEAN(summ) PPMD_GET_MEAN_SPEC((summ), PPMD_PERIOD_BITS, 2)
#define PPMD_UPDATE_PROB_0(prob) ((prob) + (1 << PPMD_INT_BITS) - PPMD_GET_MEAN(prob))
#define PPMD_UPDATE_PROB_1(prob) ((prob) - PPMD_GET_MEAN(prob))

#define PPMD_N1 4
#define PPMD_N2 4
#define PPMD_N3 4
#define PPMD_N4 ((128 + 3 - 1 * PPMD_N1 - 2 * PPMD_N2 - 3 * PPMD_N3) / 4)
#define PPMD_NUM_INDEXES (PPMD_N1 + PPMD_N2 + PPMD_N3 + PPMD_N4)

/* SEE-contexts for PPM-contexts with masked symbols */
typedef struct
{
  UInt16 Summ; /* Freq */
  Byte Shift;  /* Speed of Freq change; low Shift is for fast change */
  Byte Count;  /* Count to next change of Shift */
} CPpmd_See;

#define Ppmd_See_Update(p) do {						\
	if ((p)->Shift < PPMD_PERIOD_BITS && --(p)->Count == 0) {	\
		(p)->Summ <<= 1;					\
		(p)->Count = (Byte)(3 << (p)->Shift++);			\
    	}								\
} while (0)

typedef struct
{
  Byte Symbol;
  Byte Freq;
  UInt16 SuccessorLow;
  UInt16 SuccessorHigh;
} CPpmd_State;

typedef
  #ifdef PPMD_32BIT
    CPpmd_State *
  #else
    UInt32
  #endif
  CPpmd_State_Ref;

typedef
  #ifdef PPMD_32BIT
    void *
  #else
    UInt32
  #endif
  CPpmd_Void_Ref;

typedef
  #ifdef PPMD_32BIT
    Byte *
  #else
    UInt32
  #endif
  CPpmd_Byte_Ref;

#define PPMD_SetAllBitsIn256Bytes(p) do {				\
	unsigned j;							\
	for (j = 0; j < 256 / sizeof(p[0]); j += 8) {			\
		p[j+7] = p[j+6] = p[j+5] = p[j+4] =			\
		    p[j+3] = p[j+2] = p[j+1] = p[j+0] = ~(size_t)0;	\
	}								\
} while (0)

#endif

#define PPMD7_MIN_ORDER 2
#define PPMD7_MAX_ORDER 64

#define PPMD7_MIN_MEM_SIZE (1 << 11)
#define PPMD7_MAX_MEM_SIZE (0xFFFFFFFFu - 12 * 3)

struct CPpmd7_Context_;

typedef
  #ifdef PPMD_32BIT
    struct CPpmd7_Context_ *
  #else
    UInt32
  #endif
  CPpmd7_Context_Ref;

typedef struct CPpmd7_Context_
{
  UInt16 NumStats;
  UInt16 SummFreq;
  CPpmd_State_Ref Stats;
  CPpmd7_Context_Ref Suffix;
} CPpmd7_Context;

#define Ppmd7Context_OneState(p) ((CPpmd_State *)&(p)->SummFreq)

typedef struct
{
  CPpmd7_Context *MinContext, *MaxContext;
  CPpmd_State *FoundState;
  unsigned OrderFall, InitEsc, PrevSuccess, MaxOrder, HiBitsFlag;
  Int32 RunLength, InitRL; /* must be 32-bit at least */

  UInt32 Size;
  UInt32 GlueCount;
  Byte *Base, *LoUnit, *HiUnit, *Text, *UnitsStart;
  UInt32 AlignOffset;

  Byte Indx2Units[PPMD_NUM_INDEXES];
  Byte Units2Indx[128];
  CPpmd_Void_Ref FreeList[PPMD_NUM_INDEXES];
  Byte NS2Indx[256], NS2BSIndx[256], HB2Flag[256];
  CPpmd_See DummySee, See[25][16];
  UInt16 BinSumm[128][64];
} CPpmd7;

/* ---------- Decode ---------- */

typedef struct
{
  UInt32 (*GetThreshold)(void *p, UInt32 total);
  void (*Decode)(void *p, UInt32 start, UInt32 size);
  UInt32 (*DecodeBit)(void *p, UInt32 size0);
} IPpmd7_RangeDec;

typedef struct
{
  IPpmd7_RangeDec p;
  UInt32 Range;
  UInt32 Code;
  UInt32 Low;
  UInt32 Bottom;
  IByteIn *Stream;
} CPpmd7z_RangeDec;

/* ---------- Encode ---------- */

typedef struct
{
  UInt64 Low;
  UInt32 Range;
  Byte Cache;
  UInt64 CacheSize;
  IByteOut *Stream;
} CPpmd7z_RangeEnc;

typedef struct
{
  /* Base Functions */
  void (*Ppmd7_Construct)(CPpmd7 *p);
  Bool (*Ppmd7_Alloc)(CPpmd7 *p, UInt32 size);
  void (*Ppmd7_Free)(CPpmd7 *p);
  void (*Ppmd7_Init)(CPpmd7 *p, unsigned maxOrder);
  #define Ppmd7_WasAllocated(p) ((p)->Base != NULL)

  /* Decode Functions */
  void (*Ppmd7z_RangeDec_CreateVTable)(CPpmd7z_RangeDec *p);
  void (*PpmdRAR_RangeDec_CreateVTable)(CPpmd7z_RangeDec *p);
  Bool (*Ppmd7z_RangeDec_Init)(CPpmd7z_RangeDec *p);
  Bool (*PpmdRAR_RangeDec_Init)(CPpmd7z_RangeDec *p);
  #define Ppmd7z_RangeDec_IsFinishedOK(p) ((p)->Code == 0)
  int (*Ppmd7_DecodeSymbol)(CPpmd7 *p, IPpmd7_RangeDec *rc);

  /* Encode Functions */
  void (*Ppmd7z_RangeEnc_Init)(CPpmd7z_RangeEnc *p);
  void (*Ppmd7z_RangeEnc_FlushData)(CPpmd7z_RangeEnc *p);

  void (*Ppmd7_EncodeSymbol)(CPpmd7 *p, CPpmd7z_RangeEnc *rc, int symbol);
} IPpmd7;

extern const IPpmd7 __archive_ppmd7_functions;
#endif



/* RAR signature, also known as the mark header */
#define RAR_SIGNATURE "\x52\x61\x72\x21\x1A\x07\x00"

/* Header types */
#define MARK_HEAD    0x72
#define MAIN_HEAD    0x73
#define FILE_HEAD    0x74
#define COMM_HEAD    0x75
#define AV_HEAD      0x76
#define SUB_HEAD     0x77
#define PROTECT_HEAD 0x78
#define SIGN_HEAD    0x79
#define NEWSUB_HEAD  0x7a
#define ENDARC_HEAD  0x7b

/* Main Header Flags */
#define MHD_VOLUME       0x0001
#define MHD_COMMENT      0x0002
#define MHD_LOCK         0x0004
#define MHD_SOLID        0x0008
#define MHD_NEWNUMBERING 0x0010
#define MHD_AV           0x0020
#define MHD_PROTECT      0x0040
#define MHD_PASSWORD     0x0080
#define MHD_FIRSTVOLUME  0x0100
#define MHD_ENCRYPTVER   0x0200

/* Flags common to all headers */
#define HD_MARKDELETION     0x4000
#define HD_ADD_SIZE_PRESENT 0x8000

/* File Header Flags */
#define FHD_SPLIT_BEFORE 0x0001
#define FHD_SPLIT_AFTER  0x0002
#define FHD_PASSWORD     0x0004
#define FHD_COMMENT      0x0008
#define FHD_SOLID        0x0010
#define FHD_LARGE        0x0100
#define FHD_UNICODE      0x0200
#define FHD_SALT         0x0400
#define FHD_VERSION      0x0800
#define FHD_EXTTIME      0x1000
#define FHD_EXTFLAGS     0x2000

/* File dictionary sizes */
#define DICTIONARY_SIZE_64   0x00
#define DICTIONARY_SIZE_128  0x20
#define DICTIONARY_SIZE_256  0x40
#define DICTIONARY_SIZE_512  0x60
#define DICTIONARY_SIZE_1024 0x80
#define DICTIONARY_SIZE_2048 0xA0
#define DICTIONARY_SIZE_4096 0xC0
#define FILE_IS_DIRECTORY    0xE0
#define DICTIONARY_MASK      FILE_IS_DIRECTORY

/* OS Flags */
#define OS_MSDOS  0
#define OS_OS2    1
#define OS_WIN32  2
#define OS_UNIX   3
#define OS_MAC_OS 4
#define OS_BEOS   5

/* Compression Methods */
#define COMPRESS_METHOD_STORE   0x30
/* LZSS */
#define COMPRESS_METHOD_FASTEST 0x31
#define COMPRESS_METHOD_FAST    0x32
#define COMPRESS_METHOD_NORMAL  0x33
/* PPMd Variant H */
#define COMPRESS_METHOD_GOOD    0x34
#define COMPRESS_METHOD_BEST    0x35

#define CRC_POLYNOMIAL 0xEDB88320

#define NS_UNIT 10000000

#define DICTIONARY_MAX_SIZE 0x400000

#define MAINCODE_SIZE      299
#define OFFSETCODE_SIZE    60
#define LOWOFFSETCODE_SIZE 17
#define LENGTHCODE_SIZE    28
#define HUFFMAN_TABLE_SIZE \
  MAINCODE_SIZE + OFFSETCODE_SIZE + LOWOFFSETCODE_SIZE + LENGTHCODE_SIZE

#define MAX_SYMBOL_LENGTH 0xF
#define MAX_SYMBOLS       20

/* Virtual Machine Properties */
#define VM_MEMORY_SIZE 0x40000
#define VM_MEMORY_MASK (VM_MEMORY_SIZE - 1)
#define PROGRAM_WORK_SIZE 0x3C000
#define PROGRAM_GLOBAL_SIZE 0x2000
#define PROGRAM_SYSTEM_GLOBAL_ADDRESS PROGRAM_WORK_SIZE
#define PROGRAM_SYSTEM_GLOBAL_SIZE 0x40
#define PROGRAM_USER_GLOBAL_ADDRESS (PROGRAM_SYSTEM_GLOBAL_ADDRESS + PROGRAM_SYSTEM_GLOBAL_SIZE)
#define PROGRAM_USER_GLOBAL_SIZE (PROGRAM_GLOBAL_SIZE - PROGRAM_SYSTEM_GLOBAL_SIZE)

/*
 * Considering L1,L2 cache miss and a calling of write system-call,
 * the best size of the output buffer(uncompressed buffer) is 128K.
 * If the structure of extracting process is changed, this value
 * might be researched again.
 */
#define UNP_BUFFER_SIZE   (128 * 1024)

/* Define this here for non-Windows platforms */
#if !((defined(__WIN32__) || defined(_WIN32) || defined(__WIN32)) && !defined(__CYGWIN__))
#define FILE_ATTRIBUTE_DIRECTORY 0x10
#endif

#undef minimum
#define minimum(a, b)	((a)<(b)?(a):(b))

/* Stack overflow check */
#define MAX_COMPRESS_DEPTH 1024

/* Fields common to all headers */
struct rar_header
{
  char crc[2];
  char type;
  char flags[2];
  char size[2];
};

/* Fields common to all file headers */
struct rar_file_header
{
  char pack_size[4];
  char unp_size[4];
  char host_os;
  char file_crc[4];
  char file_time[4];
  char unp_ver;
  char method;
  char name_size[2];
  char file_attr[4];
};

struct huffman_tree_node
{
  int branches[2];
};

struct huffman_table_entry
{
  unsigned int length;
  int value;
};

struct huffman_code
{
  struct huffman_tree_node *tree;
  int numentries;
  int numallocatedentries;
  int minlength;
  int maxlength;
  int tablesize;
  struct huffman_table_entry *table;
};

struct lzss
{
  unsigned char *window;
  int mask;
  int64_t position;
};

struct data_block_offsets
{
  int64_t header_size;
  int64_t start_offset;
  int64_t end_offset;
};

struct rar_program_code
{
  uint8_t *staticdata;
  uint32_t staticdatalen;
  uint8_t *globalbackup;
  uint32_t globalbackuplen;
  uint64_t fingerprint;
  uint32_t usagecount;
  uint32_t oldfilterlength;
  struct rar_program_code *next;
};

struct rar_filter
{
  struct rar_program_code *prog;
  uint32_t initialregisters[8];
  uint8_t *globaldata;
  uint32_t globaldatalen;
  size_t blockstartpos;
  uint32_t blocklength;
  uint32_t filteredblockaddress;
  uint32_t filteredblocklength;
  struct rar_filter *next;
};

struct memory_bit_reader
{
  const uint8_t *bytes;
  size_t length;
  size_t offset;
  uint64_t bits;
  int available;
  int at_eof;
};

struct rar_virtual_machine
{
  uint32_t registers[8];
  uint8_t memory[VM_MEMORY_SIZE + sizeof(uint32_t)];
};

struct rar_filters
{
  struct rar_virtual_machine *vm;
  struct rar_program_code *progs;
  struct rar_filter *stack;
  int64_t filterstart;
  uint32_t lastfilternum;
  int64_t lastend;
  uint8_t *bytes;
  size_t bytes_ready;
};

struct audio_state
{
  int8_t weight[5];
  int16_t delta[4];
  int8_t lastdelta;
  int error[11];
  int count;
  uint8_t lastbyte;
};

struct rar
{
  /* Entries from main RAR header */
  unsigned main_flags;
  unsigned long file_crc;
  char reserved1[2];
  char reserved2[4];
  char encryptver;

  /* File header entries */
  char compression_method;
  unsigned file_flags;
  int64_t packed_size;
  int64_t unp_size;
  time_t mtime;
  long mnsec;
  mode_t mode;
  char *filename;
  char *filename_save;
  size_t filename_save_size;
  size_t filename_allocated;

  /* File header optional entries */
  char salt[8];
  time_t atime;
  long ansec;
  time_t ctime;
  long cnsec;
  time_t arctime;
  long arcnsec;

  /* Fields to help with tracking decompression of files. */
  int64_t bytes_unconsumed;
  int64_t bytes_remaining;
  int64_t bytes_uncopied;
  int64_t offset;
  int64_t offset_outgoing;
  int64_t offset_seek;
  char valid;
  unsigned int unp_offset;
  unsigned int unp_buffer_size;
  unsigned char *unp_buffer;
  unsigned int dictionary_size;
  char start_new_block;
  char entry_eof;
  unsigned long crc_calculated;
  int found_first_header;
  char has_endarc_header;
  struct data_block_offsets *dbo;
  size_t cursor;
  size_t nodes;
  char filename_must_match;

  /* LZSS members */
  struct huffman_code maincode;
  struct huffman_code offsetcode;
  struct huffman_code lowoffsetcode;
  struct huffman_code lengthcode;
  unsigned char lengthtable[HUFFMAN_TABLE_SIZE];
  struct lzss lzss;
  unsigned int lastlength;
  unsigned int lastoffset;
  unsigned int oldoffset[4];
  unsigned int lastlowoffset;
  unsigned int numlowoffsetrepeats;
  char start_new_table;

  /* Filters */
  struct rar_filters filters;

  /* PPMd Variant H members */
  char ppmd_valid;
  char ppmd_eod;
  char is_ppmd_block;
  int ppmd_escape;
  CPpmd7 ppmd7_context;
  CPpmd7z_RangeDec range_dec;
  IByteIn bytein;

  /*
   * String conversion object.
   */
  int init_default_conversion;
  struct archive_string_conv *sconv_default;
  struct archive_string_conv *opt_sconv;
  struct archive_string_conv *sconv_utf8;
  struct archive_string_conv *sconv_utf16be;

  /*
   * Bit stream reader.
   */
  struct rar_br {
#define CACHE_TYPE	uint64_t
#define CACHE_BITS	(8 * sizeof(CACHE_TYPE))
    /* Cache buffer. */
    CACHE_TYPE		 cache_buffer;
    /* Indicates how many bits avail in cache_buffer. */
    int			 cache_avail;
    ssize_t		 avail_in;
    const unsigned char *next_in;
  } br;

  /*
   * Custom field to denote that this archive contains encrypted entries
   */
  int has_encrypted_entries;
};

static int archive_read_support_format_rar_capabilities(struct archive_read *);
static int archive_read_format_rar_has_encrypted_entries(struct archive_read *);
static int archive_read_format_rar_bid(struct archive_read *, int);
static int archive_read_format_rar_options(struct archive_read *,
    const char *, const char *);
static int archive_read_format_rar_read_header(struct archive_read *,
    struct archive_entry *);
static int archive_read_format_rar_read_data(struct archive_read *,
    const void **, size_t *, int64_t *);
static int archive_read_format_rar_read_data_skip(struct archive_read *a);
static int64_t archive_read_format_rar_seek_data(struct archive_read *, int64_t,
    int);
static int archive_read_format_rar_cleanup(struct archive_read *);

/* Support functions */
static int read_header(struct archive_read *, struct archive_entry *, char);
static time_t get_time(int);
static int read_exttime(const char *, struct rar *, const char *);
static int read_symlink_stored(struct archive_read *, struct archive_entry *,
                               struct archive_string_conv *);
static int read_data_stored(struct archive_read *, const void **, size_t *,
                            int64_t *);
static int read_data_compressed(struct archive_read *, const void **, size_t *,
                                int64_t *, size_t);
static int rar_br_preparation(struct archive_read *, struct rar_br *);
static int parse_codes(struct archive_read *);
static void free_codes(struct archive_read *);
static int read_next_symbol(struct archive_read *, struct huffman_code *);
static int create_code(struct archive_read *, struct huffman_code *,
                       unsigned char *, int, char);
static int add_value(struct archive_read *, struct huffman_code *, int, int,
                     int);
static int new_node(struct huffman_code *);
static int make_table(struct archive_read *, struct huffman_code *);
static int make_table_recurse(struct archive_read *, struct huffman_code *, int,
                              struct huffman_table_entry *, int, int);
static int expand(struct archive_read *, int64_t *);
static int copy_from_lzss_window_to_unp(struct archive_read *, const void **,
                                        int64_t, size_t);
static const void *rar_read_ahead(struct archive_read *, size_t, ssize_t *);
static int parse_filter(struct archive_read *, const uint8_t *, uint16_t,
                        uint8_t);
static int run_filters(struct archive_read *);
static void clear_filters(struct rar_filters *);
static struct rar_filter *create_filter(struct rar_program_code *,
                                        const uint8_t *, uint32_t,
                                        uint32_t[8], size_t, uint32_t);
static void delete_filter(struct rar_filter *filter);
static struct rar_program_code *compile_program(const uint8_t *, size_t);
static void delete_program_code(struct rar_program_code *prog);
static uint32_t membr_next_rarvm_number(struct memory_bit_reader *br);
static uint32_t membr_bits(struct memory_bit_reader *br, int bits);
static int membr_fill(struct memory_bit_reader *br, int bits);
static int read_filter(struct archive_read *, int64_t *);
static int rar_decode_byte(struct archive_read*, uint8_t *);
static int execute_filter(struct archive_read*, struct rar_filter *,
                          struct rar_virtual_machine *, size_t);
static int copy_from_lzss_window(struct archive_read *, uint8_t *, int64_t, int);
static void vm_write_32(struct rar_virtual_machine*, size_t, uint32_t);
static uint32_t vm_read_32(struct rar_virtual_machine*, size_t);

/*
 * Bit stream reader.
 */
/* Check that the cache buffer has enough bits. */
#define rar_br_has(br, n) ((br)->cache_avail >= n)
/* Get compressed data by bit. */
#define rar_br_bits(br, n)        \
  (((uint32_t)((br)->cache_buffer >>    \
    ((br)->cache_avail - (n)))) & cache_masks[n])
#define rar_br_bits_forced(br, n)     \
  (((uint32_t)((br)->cache_buffer <<    \
    ((n) - (br)->cache_avail))) & cache_masks[n])
/* Read ahead to make sure the cache buffer has enough compressed data we
 * will use.
 *  True  : completed, there is enough data in the cache buffer.
 *  False : there is no data in the stream. */
#define rar_br_read_ahead(a, br, n) \
  ((rar_br_has(br, (n)) || rar_br_fillup(a, br)) || rar_br_has(br, (n)))
/* Notify how many bits we consumed. */
#define rar_br_consume(br, n) ((br)->cache_avail -= (n))
#define rar_br_consume_unaligned_bits(br) ((br)->cache_avail &= ~7)

static const uint32_t cache_masks[] = {
  0x00000000, 0x00000001, 0x00000003, 0x00000007,
  0x0000000F, 0x0000001F, 0x0000003F, 0x0000007F,
  0x000000FF, 0x000001FF, 0x000003FF, 0x000007FF,
  0x00000FFF, 0x00001FFF, 0x00003FFF, 0x00007FFF,
  0x0000FFFF, 0x0001FFFF, 0x0003FFFF, 0x0007FFFF,
  0x000FFFFF, 0x001FFFFF, 0x003FFFFF, 0x007FFFFF,
  0x00FFFFFF, 0x01FFFFFF, 0x03FFFFFF, 0x07FFFFFF,
  0x0FFFFFFF, 0x1FFFFFFF, 0x3FFFFFFF, 0x7FFFFFFF,
  0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF
};

/*
 * Shift away used bits in the cache data and fill it up with following bits.
 * Call this when cache buffer does not have enough bits you need.
 *
 * Returns 1 if the cache buffer is full.
 * Returns 0 if the cache buffer is not full; input buffer is empty.
 */
static int
rar_br_fillup(struct archive_read *a, struct rar_br *br)
{
  struct rar *rar = (struct rar *)(a->format->data);
  int n = CACHE_BITS - br->cache_avail;

  for (;;) {
    switch (n >> 3) {
    case 8:
      if (br->avail_in >= 8) {
        br->cache_buffer =
            ((uint64_t)br->next_in[0]) << 56 |
            ((uint64_t)br->next_in[1]) << 48 |
            ((uint64_t)br->next_in[2]) << 40 |
            ((uint64_t)br->next_in[3]) << 32 |
            ((uint32_t)br->next_in[4]) << 24 |
            ((uint32_t)br->next_in[5]) << 16 |
            ((uint32_t)br->next_in[6]) << 8 |
             (uint32_t)br->next_in[7];
        br->next_in += 8;
        br->avail_in -= 8;
        br->cache_avail += 8 * 8;
        rar->bytes_unconsumed += 8;
        rar->bytes_remaining -= 8;
        return (1);
      }
      break;
    case 7:
      if (br->avail_in >= 7) {
        br->cache_buffer =
           (br->cache_buffer << 56) |
            ((uint64_t)br->next_in[0]) << 48 |
            ((uint64_t)br->next_in[1]) << 40 |
            ((uint64_t)br->next_in[2]) << 32 |
            ((uint32_t)br->next_in[3]) << 24 |
            ((uint32_t)br->next_in[4]) << 16 |
            ((uint32_t)br->next_in[5]) << 8 |
             (uint32_t)br->next_in[6];
        br->next_in += 7;
        br->avail_in -= 7;
        br->cache_avail += 7 * 8;
        rar->bytes_unconsumed += 7;
        rar->bytes_remaining -= 7;
        return (1);
      }
      break;
    case 6:
      if (br->avail_in >= 6) {
        br->cache_buffer =
           (br->cache_buffer << 48) |
            ((uint64_t)br->next_in[0]) << 40 |
            ((uint64_t)br->next_in[1]) << 32 |
            ((uint32_t)br->next_in[2]) << 24 |
            ((uint32_t)br->next_in[3]) << 16 |
            ((uint32_t)br->next_in[4]) << 8 |
             (uint32_t)br->next_in[5];
        br->next_in += 6;
        br->avail_in -= 6;
        br->cache_avail += 6 * 8;
        rar->bytes_unconsumed += 6;
        rar->bytes_remaining -= 6;
        return (1);
      }
      break;
    case 0:
      /* We have enough compressed data in
       * the cache buffer.*/
      return (1);
    default:
      break;
    }
    if (br->avail_in <= 0) {

      if (rar->bytes_unconsumed > 0) {
        /* Consume as much as the decompressor
         * actually used. */
        __archive_read_consume(a, rar->bytes_unconsumed);
        rar->bytes_unconsumed = 0;
      }
      br->next_in = rar_read_ahead(a, 1, &(br->avail_in));
      if (br->next_in == NULL)
        return (0);
      if (br->avail_in == 0)
        return (0);
    }
    br->cache_buffer =
       (br->cache_buffer << 8) | *br->next_in++;
    br->avail_in--;
    br->cache_avail += 8;
    n -= 8;
    rar->bytes_unconsumed++;
    rar->bytes_remaining--;
  }
}

static int
rar_br_preparation(struct archive_read *a, struct rar_br *br)
{
  struct rar *rar = (struct rar *)(a->format->data);

  if (rar->bytes_remaining > 0) {
    br->next_in = rar_read_ahead(a, 1, &(br->avail_in));
    if (br->next_in == NULL) {
      archive_set_error(&a->archive,
          ARCHIVE_ERRNO_FILE_FORMAT,
          "Truncated RAR file data");
      return (ARCHIVE_FATAL);
    }
    if (br->cache_avail == 0)
      (void)rar_br_fillup(a, br);
  }
  return (ARCHIVE_OK);
}

/* Find last bit set */
static int
rar_fls(unsigned int word)
{
  word |= (word >>  1);
  word |= (word >>  2);
  word |= (word >>  4);
  word |= (word >>  8);
  word |= (word >> 16);
  return word - (word >> 1);
}

/* LZSS functions */
static int64_t
lzss_position(struct lzss *lzss)
{
  return lzss->position;
}

static int
lzss_mask(struct lzss *lzss)
{
  return lzss->mask;
}

static int
lzss_size(struct lzss *lzss)
{
  return lzss->mask + 1;
}

static int
lzss_offset_for_position(struct lzss *lzss, int64_t pos)
{
  return (int)(pos & lzss->mask);
}

static unsigned char *
lzss_pointer_for_position(struct lzss *lzss, int64_t pos)
{
  return &lzss->window[lzss_offset_for_position(lzss, pos)];
}

static int
lzss_current_offset(struct lzss *lzss)
{
  return lzss_offset_for_position(lzss, lzss->position);
}

static uint8_t *
lzss_current_pointer(struct lzss *lzss)
{
  return lzss_pointer_for_position(lzss, lzss->position);
}

static void
lzss_emit_literal(struct rar *rar, uint8_t literal)
{
  *lzss_current_pointer(&rar->lzss) = literal;
  rar->lzss.position++;
}

static void
lzss_emit_match(struct rar *rar, int offset, int length)
{
  int dstoffs = lzss_current_offset(&rar->lzss);
  int srcoffs = (dstoffs - offset) & lzss_mask(&rar->lzss);
  int l, li, remaining;
  unsigned char *d, *s;

  remaining = length;
  while (remaining > 0) {
    l = remaining;
    if (dstoffs > srcoffs) {
      if (l > lzss_size(&rar->lzss) - dstoffs)
        l = lzss_size(&rar->lzss) - dstoffs;
    } else {
      if (l > lzss_size(&rar->lzss) - srcoffs)
        l = lzss_size(&rar->lzss) - srcoffs;
    }
    d = &(rar->lzss.window[dstoffs]);
    s = &(rar->lzss.window[srcoffs]);
    if ((dstoffs + l < srcoffs) || (srcoffs + l < dstoffs))
      memcpy(d, s, l);
    else {
      for (li = 0; li < l; li++)
        d[li] = s[li];
    }
    remaining -= l;
    dstoffs = (dstoffs + l) & lzss_mask(&(rar->lzss));
    srcoffs = (srcoffs + l) & lzss_mask(&(rar->lzss));
  }
  rar->lzss.position += length;
}

static Byte
ppmd_read(void *p)
{
  struct archive_read *a = ((IByteIn*)p)->a;
  struct rar *rar = (struct rar *)(a->format->data);
  struct rar_br *br = &(rar->br);
  Byte b;
  if (!rar_br_read_ahead(a, br, 8))
  {
    archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                      "Truncated RAR file data");
    rar->valid = 0;
    return 0;
  }
  b = rar_br_bits(br, 8);
  rar_br_consume(br, 8);
  return b;
}

int
archive_read_support_format_rar(struct archive *_a)
{
  struct archive_read *a = (struct archive_read *)_a;
  struct rar *rar;
  int r;

  archive_check_magic(_a, ARCHIVE_READ_MAGIC, ARCHIVE_STATE_NEW,
                      "archive_read_support_format_rar");

  rar = calloc(1, sizeof(*rar));
  if (rar == NULL)
  {
    archive_set_error(&a->archive, ENOMEM, "Can't allocate rar data");
    return (ARCHIVE_FATAL);
  }

  /*
   * Until enough data has been read, we cannot tell about
   * any encrypted entries yet.
   */
  rar->has_encrypted_entries = ARCHIVE_READ_FORMAT_ENCRYPTION_DONT_KNOW;

  r = __archive_read_register_format(a,
                                     rar,
                                     "rar",
                                     archive_read_format_rar_bid,
                                     archive_read_format_rar_options,
                                     archive_read_format_rar_read_header,
                                     archive_read_format_rar_read_data,
                                     archive_read_format_rar_read_data_skip,
                                     archive_read_format_rar_seek_data,
                                     archive_read_format_rar_cleanup,
                                     archive_read_support_format_rar_capabilities,
                                     archive_read_format_rar_has_encrypted_entries);

  if (r != ARCHIVE_OK)
    free(rar);
  return (r);
}

static int
archive_read_support_format_rar_capabilities(struct archive_read * a)
{
  (void)a; /* UNUSED */
  return (ARCHIVE_READ_FORMAT_CAPS_ENCRYPT_DATA
    | ARCHIVE_READ_FORMAT_CAPS_ENCRYPT_METADATA);
}

static int
archive_read_format_rar_has_encrypted_entries(struct archive_read *_a)
{
  if (_a && _a->format) {
    struct rar * rar = (struct rar *)_a->format->data;
    if (rar) {
      return rar->has_encrypted_entries;
    }
  }
  return ARCHIVE_READ_FORMAT_ENCRYPTION_DONT_KNOW;
}


static int
archive_read_format_rar_bid(struct archive_read *a, int best_bid)
{
  const char *p;

  /* If there's already a bid > 30, we'll never win. */
  if (best_bid > 30)
    return (-1);

  if ((p = __archive_read_ahead(a, 7, NULL)) == NULL)
    return (-1);

  if (memcmp(p, RAR_SIGNATURE, 7) == 0)
    return (30);

  if ((p[0] == 'M' && p[1] == 'Z') || memcmp(p, "\x7F\x45LF", 4) == 0) {
    /* This is a PE file */
    ssize_t offset = 0x10000;
    ssize_t window = 4096;
    ssize_t bytes_avail;
    while (offset + window <= (1024 * 128)) {
      const char *buff = __archive_read_ahead(a, offset + window, &bytes_avail);
      if (buff == NULL) {
        /* Remaining bytes are less than window. */
        window >>= 1;
        if (window < 0x40)
          return (0);
        continue;
      }
      p = buff + offset;
      while (p + 7 < buff + bytes_avail) {
        if (memcmp(p, RAR_SIGNATURE, 7) == 0)
          return (30);
        p += 0x10;
      }
      offset = p - buff;
    }
  }
  return (0);
}

static int
skip_sfx(struct archive_read *a)
{
  const void *h;
  const char *p, *q;
  size_t skip, total;
  ssize_t bytes, window;

  total = 0;
  window = 4096;
  while (total + window <= (1024 * 128)) {
    h = __archive_read_ahead(a, window, &bytes);
    if (h == NULL) {
      /* Remaining bytes are less than window. */
      window >>= 1;
      if (window < 0x40)
      	goto fatal;
      continue;
    }
    if (bytes < 0x40)
      goto fatal;
    p = h;
    q = p + bytes;

    /*
     * Scan ahead until we find something that looks
     * like the RAR header.
     */
    while (p + 7 < q) {
      if (memcmp(p, RAR_SIGNATURE, 7) == 0) {
      	skip = p - (const char *)h;
      	__archive_read_consume(a, skip);
      	return (ARCHIVE_OK);
      }
      p += 0x10;
    }
    skip = p - (const char *)h;
    __archive_read_consume(a, skip);
    total += skip;
  }
fatal:
  archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
      "Couldn't find out RAR header");
  return (ARCHIVE_FATAL);
}

static int
archive_read_format_rar_options(struct archive_read *a,
    const char *key, const char *val)
{
  struct rar *rar;
  int ret = ARCHIVE_FAILED;

  rar = (struct rar *)(a->format->data);
  if (strcmp(key, "hdrcharset")  == 0) {
    if (val == NULL || val[0] == 0)
      archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
          "rar: hdrcharset option needs a character-set name");
    else {
      rar->opt_sconv =
          archive_string_conversion_from_charset(
              &a->archive, val, 0);
      if (rar->opt_sconv != NULL)
        ret = ARCHIVE_OK;
      else
        ret = ARCHIVE_FATAL;
    }
    return (ret);
  }

  /* Note: The "warn" return is just to inform the options
   * supervisor that we didn't handle it.  It will generate
   * a suitable error if no one used this option. */
  return (ARCHIVE_WARN);
}

static int
archive_read_format_rar_read_header(struct archive_read *a,
                                    struct archive_entry *entry)
{
  const void *h;
  const char *p;
  struct rar *rar;
  int64_t skip;
  char head_type;
  int ret;
  unsigned flags;
  unsigned long crc32_expected;

  a->archive.archive_format = ARCHIVE_FORMAT_RAR;
  if (a->archive.archive_format_name == NULL)
    a->archive.archive_format_name = "RAR";

  rar = (struct rar *)(a->format->data);

  /*
   * It should be sufficient to call archive_read_next_header() for
   * a reader to determine if an entry is encrypted or not. If the
   * encryption of an entry is only detectable when calling
   * archive_read_data(), so be it. We'll do the same check there
   * as well.
   */
  if (rar->has_encrypted_entries == ARCHIVE_READ_FORMAT_ENCRYPTION_DONT_KNOW) {
    rar->has_encrypted_entries = 0;
  }

  /* RAR files can be generated without EOF headers, so return ARCHIVE_EOF if
  * this fails.
  */
  if ((h = __archive_read_ahead(a, 7, NULL)) == NULL)
    return (ARCHIVE_EOF);

  p = h;
  if (rar->found_first_header == 0 &&
     ((p[0] == 'M' && p[1] == 'Z') || memcmp(p, "\x7F\x45LF", 4) == 0)) {
    /* This is an executable ? Must be self-extracting... */
    ret = skip_sfx(a);
    if (ret < ARCHIVE_WARN)
      return (ret);
  }
  rar->found_first_header = 1;

  while (1)
  {
    unsigned long crc32_val;

    if ((h = __archive_read_ahead(a, 7, NULL)) == NULL) {
      archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                        "Failed to read next header.");
      return (ARCHIVE_FATAL);
    }
    p = h;

    head_type = p[2];
    switch(head_type)
    {
    case MARK_HEAD:
      if (memcmp(p, RAR_SIGNATURE, 7) != 0) {
        archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
          "Invalid marker header");
        return (ARCHIVE_FATAL);
      }
      __archive_read_consume(a, 7);
      break;

    case MAIN_HEAD:
      rar->main_flags = archive_le16dec(p + 3);
      skip = archive_le16dec(p + 5);
      if ((size_t)skip < 7 + sizeof(rar->reserved1) + sizeof(rar->reserved2)) {
        archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
          "Invalid header size");
        return (ARCHIVE_FATAL);
      }
      if ((h = __archive_read_ahead(a, skip, NULL)) == NULL)
        return (ARCHIVE_FATAL);
      p = h;
      memcpy(rar->reserved1, p + 7, sizeof(rar->reserved1));
      memcpy(rar->reserved2, p + 7 + sizeof(rar->reserved1),
             sizeof(rar->reserved2));
      if (rar->main_flags & MHD_ENCRYPTVER) {
        if ((size_t)skip <
            7 + sizeof(rar->reserved1) + sizeof(rar->reserved2) + 1) {
          archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
            "Invalid header size");
          return (ARCHIVE_FATAL);
        }
        rar->encryptver = *(p + 7 + sizeof(rar->reserved1) +
                            sizeof(rar->reserved2));
      }

      /* Main header is password encrypted, so we cannot read any
         file names or any other info about files from the header. */
      if (rar->main_flags & MHD_PASSWORD)
      {
        archive_entry_set_is_metadata_encrypted(entry, 1);
        archive_entry_set_is_data_encrypted(entry, 1);
        rar->has_encrypted_entries = 1;
         archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                          "RAR encryption support unavailable.");
        return (ARCHIVE_FATAL);
      }

      crc32_val = crc32(0, (const unsigned char *)p + 2, (unsigned)skip - 2);
      if ((crc32_val & 0xffff) != archive_le16dec(p)) {
#ifndef DONT_FAIL_ON_CRC_ERROR
        archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
          "Header CRC error");
        return (ARCHIVE_FATAL);
#endif
      }
      __archive_read_consume(a, skip);
      break;

    case FILE_HEAD:
      return read_header(a, entry, head_type);

    case COMM_HEAD:
    case AV_HEAD:
    case SUB_HEAD:
    case PROTECT_HEAD:
    case SIGN_HEAD:
    case ENDARC_HEAD:
      flags = archive_le16dec(p + 3);
      skip = archive_le16dec(p + 5);
      if (skip < 7) {
        archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
          "Invalid header size too small");
        return (ARCHIVE_FATAL);
      }
      if (flags & HD_ADD_SIZE_PRESENT)
      {
        if (skip < 7 + 4) {
          archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
            "Invalid header size too small");
          return (ARCHIVE_FATAL);
        }
        if ((h = __archive_read_ahead(a, skip, NULL)) == NULL)
          return (ARCHIVE_FATAL);
        p = h;
        skip += archive_le32dec(p + 7);
      }

      /* Skip over the 2-byte CRC at the beginning of the header. */
      crc32_expected = archive_le16dec(p);
      __archive_read_consume(a, 2);
      skip -= 2;

      /* Skim the entire header and compute the CRC. */
      crc32_val = 0;
      while (skip > 0) {
        unsigned to_read;
        if (skip > 32 * 1024)
          to_read = 32 * 1024;
        else
          to_read = (unsigned)skip;
        if ((h = __archive_read_ahead(a, to_read, NULL)) == NULL) {
          archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
            "Bad RAR file");
          return (ARCHIVE_FATAL);
        }
        p = h;
        crc32_val = crc32(crc32_val, (const unsigned char *)p, to_read);
        __archive_read_consume(a, to_read);
        skip -= to_read;
      }
      if ((crc32_val & 0xffff) != crc32_expected) {
#ifndef DONT_FAIL_ON_CRC_ERROR
        archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
          "Header CRC error");
        return (ARCHIVE_FATAL);
#endif
      }
      if (head_type == ENDARC_HEAD)
        return (ARCHIVE_EOF);
      break;

    case NEWSUB_HEAD:
      if ((ret = read_header(a, entry, head_type)) < ARCHIVE_WARN)
        return ret;
      break;

    default:
      archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                        "Bad RAR file");
      return (ARCHIVE_FATAL);
    }
  }
}

static int
archive_read_format_rar_read_data(struct archive_read *a, const void **buff,
                                  size_t *size, int64_t *offset)
{
  struct rar *rar = (struct rar *)(a->format->data);
  int ret;

  if (rar->has_encrypted_entries == ARCHIVE_READ_FORMAT_ENCRYPTION_DONT_KNOW) {
    rar->has_encrypted_entries = 0;
  }

  if (rar->bytes_unconsumed > 0) {
      /* Consume as much as the decompressor actually used. */
      __archive_read_consume(a, rar->bytes_unconsumed);
      rar->bytes_unconsumed = 0;
  }

  *buff = NULL;
  if (rar->entry_eof || rar->offset_seek >= rar->unp_size) {
    *size = 0;
    *offset = rar->offset;
    if (*offset < rar->unp_size)
      *offset = rar->unp_size;
    return (ARCHIVE_EOF);
  }

  switch (rar->compression_method)
  {
  case COMPRESS_METHOD_STORE:
    ret = read_data_stored(a, buff, size, offset);
    break;

  case COMPRESS_METHOD_FASTEST:
  case COMPRESS_METHOD_FAST:
  case COMPRESS_METHOD_NORMAL:
  case COMPRESS_METHOD_GOOD:
  case COMPRESS_METHOD_BEST:
    ret = read_data_compressed(a, buff, size, offset, 0);
    if (ret != ARCHIVE_OK && ret != ARCHIVE_WARN) {
      __archive_ppmd7_functions.Ppmd7_Free(&rar->ppmd7_context);
      rar->start_new_table = 1;
      rar->ppmd_valid = 0;
    }
    break;

  default:
    archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                      "Unsupported compression method for RAR file.");
    ret = ARCHIVE_FATAL;
    break;
  }
  return (ret);
}

static int
archive_read_format_rar_read_data_skip(struct archive_read *a)
{
  struct rar *rar;
  int64_t bytes_skipped;
  int ret;

  rar = (struct rar *)(a->format->data);

  if (rar->bytes_unconsumed > 0) {
      /* Consume as much as the decompressor actually used. */
      __archive_read_consume(a, rar->bytes_unconsumed);
      rar->bytes_unconsumed = 0;
  }

  if (rar->bytes_remaining > 0) {
    bytes_skipped = __archive_read_consume(a, rar->bytes_remaining);
    if (bytes_skipped < 0)
      return (ARCHIVE_FATAL);
  }

  /* Compressed data to skip must be read from each header in a multivolume
   * archive.
   */
  if (rar->main_flags & MHD_VOLUME && rar->file_flags & FHD_SPLIT_AFTER)
  {
    ret = archive_read_format_rar_read_header(a, a->entry);
    if (ret == (ARCHIVE_EOF))
      ret = archive_read_format_rar_read_header(a, a->entry);
    if (ret != (ARCHIVE_OK))
      return ret;
    return archive_read_format_rar_read_data_skip(a);
  }

  return (ARCHIVE_OK);
}

static int64_t
archive_read_format_rar_seek_data(struct archive_read *a, int64_t offset,
    int whence)
{
  int64_t client_offset, ret;
  size_t i;
  struct rar *rar = (struct rar *)(a->format->data);

  if (rar->compression_method == COMPRESS_METHOD_STORE)
  {
    /* Modify the offset for use with SEEK_SET */
    switch (whence)
    {
      case SEEK_CUR:
        client_offset = rar->offset_seek;
        break;
      case SEEK_END:
        client_offset = rar->unp_size;
        break;
      case SEEK_SET:
      default:
        client_offset = 0;
    }
    client_offset += offset;
    if (client_offset < 0)
    {
      /* Can't seek past beginning of data block */
      return -1;
    }
    else if (client_offset > rar->unp_size)
    {
      /*
       * Set the returned offset but only seek to the end of
       * the data block.
       */
      rar->offset_seek = client_offset;
      client_offset = rar->unp_size;
    }

    client_offset += rar->dbo[0].start_offset;
    i = 0;
    while (i < rar->cursor)
    {
      i++;
      client_offset += rar->dbo[i].start_offset - rar->dbo[i-1].end_offset;
    }
    if (rar->main_flags & MHD_VOLUME)
    {
      /* Find the appropriate offset among the multivolume archive */
      while (1)
      {
        if (client_offset < rar->dbo[rar->cursor].start_offset &&
          rar->file_flags & FHD_SPLIT_BEFORE)
        {
          /* Search backwards for the correct data block */
          if (rar->cursor == 0)
          {
            archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
              "Attempt to seek past beginning of RAR data block");
            return (ARCHIVE_FAILED);
          }
          rar->cursor--;
          client_offset -= rar->dbo[rar->cursor+1].start_offset -
            rar->dbo[rar->cursor].end_offset;
          if (client_offset < rar->dbo[rar->cursor].start_offset)
            continue;
          ret = __archive_read_seek(a, rar->dbo[rar->cursor].start_offset -
            rar->dbo[rar->cursor].header_size, SEEK_SET);
          if (ret < (ARCHIVE_OK))
            return ret;
          ret = archive_read_format_rar_read_header(a, a->entry);
          if (ret != (ARCHIVE_OK))
          {
            archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
              "Error during seek of RAR file");
            return (ARCHIVE_FAILED);
          }
          rar->cursor--;
          break;
        }
        else if (client_offset > rar->dbo[rar->cursor].end_offset &&
          rar->file_flags & FHD_SPLIT_AFTER)
        {
          /* Search forward for the correct data block */
          rar->cursor++;
          if (rar->cursor < rar->nodes &&
            client_offset > rar->dbo[rar->cursor].end_offset)
          {
            client_offset += rar->dbo[rar->cursor].start_offset -
              rar->dbo[rar->cursor-1].end_offset;
            continue;
          }
          rar->cursor--;
          ret = __archive_read_seek(a, rar->dbo[rar->cursor].end_offset,
                                    SEEK_SET);
          if (ret < (ARCHIVE_OK))
            return ret;
          ret = archive_read_format_rar_read_header(a, a->entry);
          if (ret == (ARCHIVE_EOF))
          {
            rar->has_endarc_header = 1;
            ret = archive_read_format_rar_read_header(a, a->entry);
          }
          if (ret != (ARCHIVE_OK))
          {
            archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
              "Error during seek of RAR file");
            return (ARCHIVE_FAILED);
          }
          client_offset += rar->dbo[rar->cursor].start_offset -
            rar->dbo[rar->cursor-1].end_offset;
          continue;
        }
        break;
      }
    }

    ret = __archive_read_seek(a, client_offset, SEEK_SET);
    if (ret < (ARCHIVE_OK))
      return ret;
    rar->bytes_remaining = rar->dbo[rar->cursor].end_offset - ret;
    i = rar->cursor;
    while (i > 0)
    {
      i--;
      ret -= rar->dbo[i+1].start_offset - rar->dbo[i].end_offset;
    }
    ret -= rar->dbo[0].start_offset;

    /* Always restart reading the file after a seek */
    __archive_reset_read_data(&a->archive);

    rar->bytes_unconsumed = 0;
    rar->offset = 0;

    /*
     * If a seek past the end of file was requested, return the requested
     * offset.
     */
    if (ret == rar->unp_size && rar->offset_seek > rar->unp_size)
      return rar->offset_seek;

    /* Return the new offset */
    rar->offset_seek = ret;
    return rar->offset_seek;
  }
  else
  {
    archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
      "Seeking of compressed RAR files is unsupported");
  }
  return (ARCHIVE_FAILED);
}

static int
archive_read_format_rar_cleanup(struct archive_read *a)
{
  struct rar *rar;

  rar = (struct rar *)(a->format->data);
  free_codes(a);
  clear_filters(&rar->filters);
  free(rar->filename);
  free(rar->filename_save);
  free(rar->dbo);
  free(rar->unp_buffer);
  free(rar->lzss.window);
  __archive_ppmd7_functions.Ppmd7_Free(&rar->ppmd7_context);
  free(rar);
  (a->format->data) = NULL;
  return (ARCHIVE_OK);
}

static int
read_header(struct archive_read *a, struct archive_entry *entry,
            char head_type)
{
  const void *h;
  const char *p, *endp;
  struct rar *rar;
  struct rar_header rar_header;
  struct rar_file_header file_header;
  int64_t header_size;
  unsigned filename_size, end;
  char *filename;
  char *strp;
  char packed_size[8];
  char unp_size[8];
  int ttime;
  struct archive_string_conv *sconv, *fn_sconv;
  uint32_t crc32_computed, crc32_read;
  int ret = (ARCHIVE_OK), ret2;
  char *newptr;
  size_t newsize;

  rar = (struct rar *)(a->format->data);

  /* Setup a string conversion object for non-rar-unicode filenames. */
  sconv = rar->opt_sconv;
  if (sconv == NULL) {
    if (!rar->init_default_conversion) {
      rar->sconv_default =
          archive_string_default_conversion_for_read(
            &(a->archive));
      rar->init_default_conversion = 1;
    }
    sconv = rar->sconv_default;
  }


  if ((h = __archive_read_ahead(a, 7, NULL)) == NULL)
    return (ARCHIVE_FATAL);
  p = h;
  memcpy(&rar_header, p, sizeof(rar_header));
  rar->file_flags = archive_le16dec(rar_header.flags);
  header_size = archive_le16dec(rar_header.size);
  if (header_size < (int64_t)sizeof(file_header) + 7) {
    archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
      "Invalid header size");
    return (ARCHIVE_FATAL);
  }
  crc32_computed = crc32(0, (const unsigned char *)p + 2, 7 - 2);
  __archive_read_consume(a, 7);

  if (!(rar->file_flags & FHD_SOLID))
  {
    rar->compression_method = 0;
    rar->packed_size = 0;
    rar->unp_size = 0;
    rar->mtime = 0;
    rar->ctime = 0;
    rar->atime = 0;
    rar->arctime = 0;
    rar->mode = 0;
    memset(&rar->salt, 0, sizeof(rar->salt));
    rar->atime = 0;
    rar->ansec = 0;
    rar->ctime = 0;
    rar->cnsec = 0;
    rar->mtime = 0;
    rar->mnsec = 0;
    rar->arctime = 0;
    rar->arcnsec = 0;
  }
  else
  {
    archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                      "RAR solid archive support unavailable.");
    return (ARCHIVE_FATAL);
  }

  if ((h = __archive_read_ahead(a, (size_t)header_size - 7, NULL)) == NULL)
  {
    archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                      "Failed to read full header content.");
    return (ARCHIVE_FATAL);
  }

  /* File Header CRC check. */
  crc32_computed = crc32(crc32_computed, h, (unsigned)(header_size - 7));
  crc32_read = archive_le16dec(rar_header.crc);
  if ((crc32_computed & 0xffff) != crc32_read) {
#ifndef DONT_FAIL_ON_CRC_ERROR
    archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
      "Header CRC error");
    return (ARCHIVE_FATAL);
#endif
  }
  /* If no CRC error, Go on parsing File Header. */
  p = h;
  endp = p + header_size - 7;
  memcpy(&file_header, p, sizeof(file_header));
  p += sizeof(file_header);

  rar->compression_method = file_header.method;

  ttime = archive_le32dec(file_header.file_time);
  rar->mtime = get_time(ttime);

  rar->file_crc = archive_le32dec(file_header.file_crc);

  if (rar->file_flags & FHD_PASSWORD)
  {
    archive_entry_set_is_data_encrypted(entry, 1);
    rar->has_encrypted_entries = 1;
    archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                      "RAR encryption support unavailable.");
    /* Since it is only the data part itself that is encrypted we can at least
       extract information about the currently processed entry and don't need
       to return ARCHIVE_FATAL here. */
    /*return (ARCHIVE_FATAL);*/
  }

  if (rar->file_flags & FHD_LARGE)
  {
    if (p + 8 > endp) {
      archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                        "Invalid header size");
      return (ARCHIVE_FATAL);
    }
    memcpy(packed_size, file_header.pack_size, 4);
    memcpy(packed_size + 4, p, 4); /* High pack size */
    p += 4;
    memcpy(unp_size, file_header.unp_size, 4);
    memcpy(unp_size + 4, p, 4); /* High unpack size */
    p += 4;
    rar->packed_size = archive_le64dec(&packed_size);
    rar->unp_size = archive_le64dec(&unp_size);
  }
  else
  {
    rar->packed_size = archive_le32dec(file_header.pack_size);
    rar->unp_size = archive_le32dec(file_header.unp_size);
  }

  if (rar->packed_size < 0 || rar->unp_size < 0)
  {
    archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                      "Invalid sizes specified.");
    return (ARCHIVE_FATAL);
  }

  rar->bytes_remaining = rar->packed_size;

  /* TODO: RARv3 subblocks contain comments. For now the complete block is
   * consumed at the end.
   */
  if (head_type == NEWSUB_HEAD) {
    size_t distance = p - (const char *)h;
    if (rar->packed_size > INT64_MAX - header_size) {
      archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                        "Extended header size too large.");
      return (ARCHIVE_FATAL);
    }
    header_size += rar->packed_size;
    if ((uintmax_t)header_size > SIZE_MAX) {
      archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                        "Unable to read extended header data.");
      return (ARCHIVE_FATAL);
    }
    /* Make sure we have the extended data. */
    if ((h = __archive_read_ahead(a, (size_t)header_size - 7, NULL)) == NULL) {
      archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                        "Failed to read extended header data.");
      return (ARCHIVE_FATAL);
    }
    p = h;
    endp = p + header_size - 7;
    p += distance;
  }

  filename_size = archive_le16dec(file_header.name_size);
  if (p + filename_size > endp) {
    archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
      "Invalid filename size");
    return (ARCHIVE_FATAL);
  }
  if (rar->filename_allocated < filename_size * 2 + 2) {
    newsize = filename_size * 2 + 2;
    newptr = realloc(rar->filename, newsize);
    if (newptr == NULL) {
      archive_set_error(&a->archive, ENOMEM,
                        "Couldn't allocate memory.");
      return (ARCHIVE_FATAL);
    }
    rar->filename = newptr;
    rar->filename_allocated = newsize;
  }
  filename = rar->filename;
  memcpy(filename, p, filename_size);
  filename[filename_size] = '\0';
  if (rar->file_flags & FHD_UNICODE)
  {
    if (filename_size != strlen(filename))
    {
      unsigned char highbyte, flagbits, flagbyte;
      unsigned fn_end, offset;

      end = filename_size;
      fn_end = filename_size * 2;
      filename_size = 0;
      offset = (unsigned)strlen(filename) + 1;
      highbyte = offset >= end ? 0 : *(p + offset++);
      flagbits = 0;
      flagbyte = 0;
      while (offset < end && filename_size < fn_end)
      {
        if (!flagbits)
        {
          flagbyte = *(p + offset++);
          flagbits = 8;
        }

        flagbits -= 2;
        switch((flagbyte >> flagbits) & 3)
        {
          case 0:
            if (offset >= end)
              continue;
            filename[filename_size++] = '\0';
            filename[filename_size++] = *(p + offset++);
            break;
          case 1:
            if (offset >= end)
              continue;
            filename[filename_size++] = highbyte;
            filename[filename_size++] = *(p + offset++);
            break;
          case 2:
            if (offset >= end - 1) {
              offset = end;
              continue;
            }
            filename[filename_size++] = *(p + offset + 1);
            filename[filename_size++] = *(p + offset);
            offset += 2;
            break;
          case 3:
          {
            char extra, high;
            uint8_t length;

            if (offset >= end)
              continue;

            length = *(p + offset++);
            if (length & 0x80) {
              if (offset >= end)
                continue;
              extra = *(p + offset++);
              high = (char)highbyte;
            } else
              extra = high = 0;
            length = (length & 0x7f) + 2;
            while (length && filename_size < fn_end) {
              unsigned cp = filename_size >> 1;
              filename[filename_size++] = high;
              filename[filename_size++] = p[cp] + extra;
              length--;
            }
          }
          break;
        }
      }
      if (filename_size > fn_end) {
        archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
          "Invalid filename");
        return (ARCHIVE_FATAL);
      }
      filename[filename_size++] = '\0';
      /*
       * Do not increment filename_size here as the computations below
       * add the space for the terminating NUL explicitly.
       */
      filename[filename_size] = '\0';

      /* Decoded unicode form is UTF-16BE, so we have to update a string
       * conversion object for it. */
      if (rar->sconv_utf16be == NULL) {
        rar->sconv_utf16be = archive_string_conversion_from_charset(
           &a->archive, "UTF-16BE", 1);
        if (rar->sconv_utf16be == NULL)
          return (ARCHIVE_FATAL);
      }
      fn_sconv = rar->sconv_utf16be;

      strp = filename;
      while (memcmp(strp, "\x00\x00", 2))
      {
        if (!memcmp(strp, "\x00\\", 2))
          *(strp + 1) = '/';
        strp += 2;
      }
      p += offset;
    } else {
      /*
       * If FHD_UNICODE is set but no unicode data, this file name form
       * is UTF-8, so we have to update a string conversion object for
       * it accordingly.
       */
      if (rar->sconv_utf8 == NULL) {
        rar->sconv_utf8 = archive_string_conversion_from_charset(
           &a->archive, "UTF-8", 1);
        if (rar->sconv_utf8 == NULL)
          return (ARCHIVE_FATAL);
      }
      fn_sconv = rar->sconv_utf8;
      while ((strp = strchr(filename, '\\')) != NULL)
        *strp = '/';
      p += filename_size;
    }
  }
  else
  {
    fn_sconv = sconv;
    while ((strp = strchr(filename, '\\')) != NULL)
      *strp = '/';
    p += filename_size;
  }

  /* Split file in multivolume RAR. No more need to process header. */
  if (rar->filename_save &&
    filename_size == rar->filename_save_size &&
    !memcmp(rar->filename, rar->filename_save, filename_size + 1))
  {
    __archive_read_consume(a, header_size - 7);
    rar->br.avail_in = 0;
    rar->br.next_in = NULL;
    rar->cursor++;
    if (rar->cursor >= rar->nodes)
    {
      struct data_block_offsets *newdbo;

      newsize = sizeof(*rar->dbo) * (rar->nodes + 1);
      if ((newdbo = realloc(rar->dbo, newsize)) == NULL)
      {
        archive_set_error(&a->archive, ENOMEM, "Couldn't allocate memory.");
        return (ARCHIVE_FATAL);
      }
      rar->dbo = newdbo;
      rar->nodes++;
      rar->dbo[rar->cursor].header_size = header_size;
      rar->dbo[rar->cursor].start_offset = -1;
      rar->dbo[rar->cursor].end_offset = -1;
    }
    if (rar->dbo[rar->cursor].start_offset < 0)
    {
      if (rar->packed_size > INT64_MAX - a->filter->position)
      {
        archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                          "Unable to store offsets.");
        return (ARCHIVE_FATAL);
      }
      rar->dbo[rar->cursor].start_offset = a->filter->position;
      rar->dbo[rar->cursor].end_offset = rar->dbo[rar->cursor].start_offset +
        rar->packed_size;
    }
    return ret;
  }
  else if (rar->filename_must_match)
  {
    archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
      "Mismatch of file parts split across multi-volume archive");
    return (ARCHIVE_FATAL);
  }

  newsize = filename_size + 1;
  if ((newptr = realloc(rar->filename_save, newsize)) == NULL)
  {
    archive_set_error(&a->archive, ENOMEM, "Couldn't allocate memory.");
    return (ARCHIVE_FATAL);
  }
  rar->filename_save = newptr;
  memcpy(rar->filename_save, rar->filename, newsize);
  rar->filename_save_size = filename_size;

  /* Set info for seeking */
  free(rar->dbo);
  if ((rar->dbo = calloc(1, sizeof(*rar->dbo))) == NULL)
  {
    archive_set_error(&a->archive, ENOMEM, "Couldn't allocate memory.");
    return (ARCHIVE_FATAL);
  }
  rar->dbo[0].header_size = header_size;
  rar->dbo[0].start_offset = -1;
  rar->dbo[0].end_offset = -1;
  rar->cursor = 0;
  rar->nodes = 1;

  if (rar->file_flags & FHD_SALT)
  {
    if (p + 8 > endp) {
      archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
        "Invalid header size");
      return (ARCHIVE_FATAL);
    }
    memcpy(rar->salt, p, 8);
    p += 8;
  }

  if (rar->file_flags & FHD_EXTTIME) {
    if (read_exttime(p, rar, endp) < 0) {
      archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
        "Invalid header size");
      return (ARCHIVE_FATAL);
    }
  }

  __archive_read_consume(a, header_size - 7);
  if (rar->packed_size > INT64_MAX - a->filter->position) {
    archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                      "Unable to store offsets.");
    return (ARCHIVE_FATAL);
  }
  rar->dbo[0].start_offset = a->filter->position;
  rar->dbo[0].end_offset = rar->dbo[0].start_offset + rar->packed_size;

  switch(file_header.host_os)
  {
  case OS_MSDOS:
  case OS_OS2:
  case OS_WIN32:
    rar->mode = (__LA_MODE_T)archive_le32dec(file_header.file_attr);
    if (rar->mode & FILE_ATTRIBUTE_DIRECTORY)
      rar->mode = AE_IFDIR | S_IXUSR | S_IXGRP | S_IXOTH;
    else
      rar->mode = AE_IFREG;
    rar->mode |= S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    break;

  case OS_UNIX:
  case OS_MAC_OS:
  case OS_BEOS:
    rar->mode = (__LA_MODE_T)archive_le32dec(file_header.file_attr);
    break;

  default:
    archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                      "Unknown file attributes from RAR file's host OS");
    return (ARCHIVE_FATAL);
  }

  rar->bytes_uncopied = rar->bytes_unconsumed = 0;
  rar->lzss.position = rar->offset = 0;
  rar->offset_seek = 0;
  rar->dictionary_size = 0;
  rar->offset_outgoing = 0;
  rar->br.cache_avail = 0;
  rar->br.avail_in = 0;
  rar->br.next_in = NULL;
  rar->crc_calculated = 0;
  rar->entry_eof = 0;
  rar->valid = 1;
  rar->is_ppmd_block = 0;
  rar->start_new_table = 1;
  free(rar->unp_buffer);
  rar->unp_buffer = NULL;
  rar->unp_offset = 0;
  rar->unp_buffer_size = UNP_BUFFER_SIZE;
  memset(rar->lengthtable, 0, sizeof(rar->lengthtable));
  __archive_ppmd7_functions.Ppmd7_Free(&rar->ppmd7_context);
  rar->ppmd_valid = rar->ppmd_eod = 0;
  rar->filters.filterstart = INT64_MAX;

  /* Don't set any archive entries for non-file header types */
  if (head_type == NEWSUB_HEAD)
    return ret;

  archive_entry_set_mtime(entry, rar->mtime, rar->mnsec);
  archive_entry_set_ctime(entry, rar->ctime, rar->cnsec);
  archive_entry_set_atime(entry, rar->atime, rar->ansec);
  archive_entry_set_size(entry, rar->unp_size);
  archive_entry_set_mode(entry, rar->mode);

  if (archive_entry_copy_pathname_l(entry, filename, filename_size, fn_sconv))
  {
    if (errno == ENOMEM)
    {
      archive_set_error(&a->archive, ENOMEM,
                        "Can't allocate memory for Pathname");
      return (ARCHIVE_FATAL);
    }
    archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                      "Pathname cannot be converted from %s to current locale.",
                      archive_string_conversion_charset_name(fn_sconv));
    ret = (ARCHIVE_WARN);
  }

  if (((rar->mode) & AE_IFMT) == AE_IFLNK)
  {
    /* Make sure a symbolic-link file does not have its body. */
    rar->bytes_remaining = 0;
    archive_entry_set_size(entry, 0);

    /* Read a symbolic-link name. */
    if ((ret2 = read_symlink_stored(a, entry, sconv)) < (ARCHIVE_WARN))
      return ret2;
    if (ret > ret2)
      ret = ret2;
  }

  if (rar->bytes_remaining == 0)
    rar->entry_eof = 1;

  return ret;
}

static time_t
get_time(int ttime)
{
  struct tm tm;
  tm.tm_sec = 2 * (ttime & 0x1f);
  tm.tm_min = (ttime >> 5) & 0x3f;
  tm.tm_hour = (ttime >> 11) & 0x1f;
  tm.tm_mday = (ttime >> 16) & 0x1f;
  tm.tm_mon = ((ttime >> 21) & 0x0f) - 1;
  tm.tm_year = ((ttime >> 25) & 0x7f) + 80;
  tm.tm_isdst = -1;
  return mktime(&tm);
}

static int
read_exttime(const char *p, struct rar *rar, const char *endp)
{
  unsigned rmode, flags, rem, j, count;
  int ttime, i;
  struct tm *tm;
  time_t t;
  long nsec;
#if defined(HAVE_LOCALTIME_R) || defined(HAVE_LOCALTIME_S)
  struct tm tmbuf;
#endif

  if (p + 2 > endp)
    return (-1);
  flags = archive_le16dec(p);
  p += 2;

  for (i = 3; i >= 0; i--)
  {
    t = 0;
    if (i == 3)
      t = rar->mtime;
    rmode = flags >> i * 4;
    if (rmode & 8)
    {
      if (!t)
      {
        if (p + 4 > endp)
          return (-1);
        ttime = archive_le32dec(p);
        t = get_time(ttime);
        p += 4;
      }
      rem = 0;
      count = rmode & 3;
      if (p + count > endp)
        return (-1);
      for (j = 0; j < count; j++)
      {
        rem = (((unsigned)(unsigned char)*p) << 16) | (rem >> 8);
        p++;
      }
#if defined(HAVE_LOCALTIME_S)
      tm = localtime_s(&tmbuf, &t) ? NULL : &tmbuf;
#elif defined(HAVE_LOCALTIME_R)
      tm = localtime_r(&t, &tmbuf);
#else
      tm = localtime(&t);
#endif
      nsec = tm->tm_sec + rem / NS_UNIT;
      if (rmode & 4)
      {
        tm->tm_sec++;
        t = mktime(tm);
      }
      if (i == 3)
      {
        rar->mtime = t;
        rar->mnsec = nsec;
      }
      else if (i == 2)
      {
        rar->ctime = t;
        rar->cnsec = nsec;
      }
      else if (i == 1)
      {
        rar->atime = t;
        rar->ansec = nsec;
      }
      else
      {
        rar->arctime = t;
        rar->arcnsec = nsec;
      }
    }
  }
  return (0);
}

static int
read_symlink_stored(struct archive_read *a, struct archive_entry *entry,
                    struct archive_string_conv *sconv)
{
  const void *h;
  const char *p;
  struct rar *rar;
  int ret = (ARCHIVE_OK);

  rar = (struct rar *)(a->format->data);
  if ((uintmax_t)rar->packed_size > SIZE_MAX)
  {
    archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                      "Unable to read link.");
    return (ARCHIVE_FATAL);
  }
  if ((h = rar_read_ahead(a, (size_t)rar->packed_size, NULL)) == NULL)
  {
    archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                      "Failed to read link.");
    return (ARCHIVE_FATAL);
  }
  p = h;

  if (archive_entry_copy_symlink_l(entry,
      p, (size_t)rar->packed_size, sconv))
  {
    if (errno == ENOMEM)
    {
      archive_set_error(&a->archive, ENOMEM,
                        "Can't allocate memory for link");
      return (ARCHIVE_FATAL);
    }
    archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                      "link cannot be converted from %s to current locale.",
                      archive_string_conversion_charset_name(sconv));
    ret = (ARCHIVE_WARN);
  }
  __archive_read_consume(a, rar->packed_size);
  return ret;
}

static int
read_data_stored(struct archive_read *a, const void **buff, size_t *size,
                 int64_t *offset)
{
  struct rar *rar;
  ssize_t bytes_avail;

  rar = (struct rar *)(a->format->data);
  if (rar->bytes_remaining == 0 &&
    !(rar->main_flags & MHD_VOLUME && rar->file_flags & FHD_SPLIT_AFTER))
  {
    *buff = NULL;
    *size = 0;
    *offset = rar->offset;
    if (rar->file_crc != rar->crc_calculated) {
#ifndef DONT_FAIL_ON_CRC_ERROR
      archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                        "File CRC error");
      return (ARCHIVE_FATAL);
#endif
    }
    rar->entry_eof = 1;
    return (ARCHIVE_EOF);
  }

  *buff = rar_read_ahead(a, 1, &bytes_avail);
  if (bytes_avail <= 0)
  {
    archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                      "Truncated RAR file data");
    return (ARCHIVE_FATAL);
  }

  *size = bytes_avail;
  *offset = rar->offset;
  rar->offset += bytes_avail;
  rar->offset_seek += bytes_avail;
  rar->bytes_remaining -= bytes_avail;
  rar->bytes_unconsumed = bytes_avail;
  /* Calculate File CRC. */
  rar->crc_calculated = crc32(rar->crc_calculated, *buff,
    (unsigned)bytes_avail);
  return (ARCHIVE_OK);
}

static int
read_data_compressed(struct archive_read *a, const void **buff, size_t *size,
                     int64_t *offset, size_t looper)
{
  if (looper++ > MAX_COMPRESS_DEPTH)
    return (ARCHIVE_FATAL);

  struct rar *rar;
  int64_t start, end;
  size_t bs;
  int ret = (ARCHIVE_OK), sym, code, lzss_offset, length, i;

  rar = (struct rar *)(a->format->data);

  do {
    if (!rar->valid)
      return (ARCHIVE_FATAL);

    if (rar->filters.bytes_ready > 0)
    {
      /* Flush unp_buffer first */
      if (rar->unp_offset > 0)
      {
        *buff = rar->unp_buffer;
        *size = rar->unp_offset;
        rar->unp_offset = 0;
        *offset = rar->offset_outgoing;
        rar->offset_outgoing += *size;
      }
      else
      {
        *buff = rar->filters.bytes;
        *size = rar->filters.bytes_ready;

        rar->offset += *size;
        *offset = rar->offset_outgoing;
        rar->offset_outgoing += *size;

        rar->filters.bytes_ready -= *size;
        rar->filters.bytes += *size;
      }
      goto ending_block;
    }

    if (rar->ppmd_eod ||
       (rar->dictionary_size && rar->offset >= rar->unp_size))
    {
      if (rar->unp_offset > 0) {
        /*
         * We have unprocessed extracted data. write it out.
         */
        *buff = rar->unp_buffer;
        *size = rar->unp_offset;
        *offset = rar->offset_outgoing;
        rar->offset_outgoing += *size;
        /* Calculate File CRC. */
        rar->crc_calculated = crc32(rar->crc_calculated, *buff,
          (unsigned)*size);
        rar->unp_offset = 0;
        return (ARCHIVE_OK);
      }
      *buff = NULL;
      *size = 0;
      *offset = rar->offset;
      if (rar->file_crc != rar->crc_calculated) {
#ifndef DONT_FAIL_ON_CRC_ERROR
        archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                          "File CRC error");
        return (ARCHIVE_FATAL);
#endif
      }
      rar->entry_eof = 1;
      return (ARCHIVE_EOF);
    }

    if (!rar->is_ppmd_block && rar->dictionary_size && rar->bytes_uncopied > 0)
    {
      if (rar->bytes_uncopied > (rar->unp_buffer_size - rar->unp_offset))
        bs = rar->unp_buffer_size - rar->unp_offset;
      else
        bs = (size_t)rar->bytes_uncopied;
      ret = copy_from_lzss_window_to_unp(a, buff, rar->offset, bs);
      if (ret != ARCHIVE_OK)
        return (ret);
      rar->offset += bs;
      rar->bytes_uncopied -= bs;
      if (*buff != NULL) {
        rar->unp_offset = 0;
        *size = rar->unp_buffer_size;
        *offset = rar->offset_outgoing;
        rar->offset_outgoing += *size;
        /* Calculate File CRC. */
        rar->crc_calculated = crc32(rar->crc_calculated, *buff,
          (unsigned)*size);
        return (ret);
      }
      continue;
    }

    if (rar->filters.lastend == rar->filters.filterstart)
    {
      if (!run_filters(a))
        return (ARCHIVE_FATAL);
      continue;
    }

    if (!rar->br.next_in &&
      (ret = rar_br_preparation(a, &(rar->br))) < ARCHIVE_WARN)
      return (ret);
    if (rar->start_new_table && ((ret = parse_codes(a)) < (ARCHIVE_WARN)))
      return (ret);

    if (rar->is_ppmd_block)
    {
      if ((sym = __archive_ppmd7_functions.Ppmd7_DecodeSymbol(
        &rar->ppmd7_context, &rar->range_dec.p)) < 0)
      {
        archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                          "Invalid symbol");
        return (ARCHIVE_FATAL);
      }
      if(sym != rar->ppmd_escape)
      {
        lzss_emit_literal(rar, sym);
        rar->bytes_uncopied++;
      }
      else
      {
        if ((code = __archive_ppmd7_functions.Ppmd7_DecodeSymbol(
          &rar->ppmd7_context, &rar->range_dec.p)) < 0)
        {
          archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                            "Invalid symbol");
          return (ARCHIVE_FATAL);
        }

        switch(code)
        {
          case 0:
            rar->start_new_table = 1;
            return read_data_compressed(a, buff, size, offset, looper);

          case 2:
            rar->ppmd_eod = 1;/* End Of ppmd Data. */
            continue;

          case 3:
            archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
                              "Parsing filters is unsupported.");
            return (ARCHIVE_FAILED);

          case 4:
            lzss_offset = 0;
            for (i = 2; i >= 0; i--)
            {
              if ((code = __archive_ppmd7_functions.Ppmd7_DecodeSymbol(
                &rar->ppmd7_context, &rar->range_dec.p)) < 0)
              {
                archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                                  "Invalid symbol");
                return (ARCHIVE_FATAL);
              }
              lzss_offset |= code << (i * 8);
            }
            if ((length = __archive_ppmd7_functions.Ppmd7_DecodeSymbol(
              &rar->ppmd7_context, &rar->range_dec.p)) < 0)
            {
              archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                                "Invalid symbol");
              return (ARCHIVE_FATAL);
            }
            lzss_emit_match(rar, lzss_offset + 2, length + 32);
            rar->bytes_uncopied += length + 32;
            break;

          case 5:
            if ((length = __archive_ppmd7_functions.Ppmd7_DecodeSymbol(
              &rar->ppmd7_context, &rar->range_dec.p)) < 0)
            {
              archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                                "Invalid symbol");
              return (ARCHIVE_FATAL);
            }
            lzss_emit_match(rar, 1, length + 4);
            rar->bytes_uncopied += length + 4;
            break;

         default:
           lzss_emit_literal(rar, sym);
           rar->bytes_uncopied++;
        }
      }
    }
    else
    {
      start = rar->offset;
      end = start + rar->dictionary_size;

      /* We don't want to overflow the window and overwrite data that we write
       * at 'start'. Therefore, reduce the end length by the maximum match size,
       * which is 260 bytes. You can compute this maximum by looking at the
       * definition of 'expand', in particular when 'symbol >= 271'. */
      /* NOTE: It's possible for 'dictionary_size' to be less than this 260
       * value, however that will only be the case when 'unp_size' is small,
       * which should only happen when the entry size is small and there's no
       * risk of overflowing the buffer */
      if (rar->dictionary_size > 260) {
        end -= 260;
      }

      if (rar->filters.filterstart < end) {
        end = rar->filters.filterstart;
      }

      ret = expand(a, &end);
      if (ret != ARCHIVE_OK)
        return (ret);

      rar->bytes_uncopied = end - start;
      rar->filters.lastend = end;
      if (rar->filters.lastend != rar->filters.filterstart && rar->bytes_uncopied == 0) {
          /* Broken RAR files cause this case.
          * NOTE: If this case were possible on a normal RAR file
          * we would find out where it was actually bad and
          * what we would do to solve it. */
          archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                            "Internal error extracting RAR file");
          return (ARCHIVE_FATAL);
      }
    }
    if (rar->bytes_uncopied > (rar->unp_buffer_size - rar->unp_offset))
      bs = rar->unp_buffer_size - rar->unp_offset;
    else
      bs = (size_t)rar->bytes_uncopied;
    ret = copy_from_lzss_window_to_unp(a, buff, rar->offset, bs);
    if (ret != ARCHIVE_OK)
      return (ret);
    rar->offset += bs;
    rar->bytes_uncopied -= bs;
    /*
     * If *buff is NULL, it means unp_buffer is not full.
     * So we have to continue extracting a RAR file.
     */
  } while (*buff == NULL);

  rar->unp_offset = 0;
  *size = rar->unp_buffer_size;
  *offset = rar->offset_outgoing;
  rar->offset_outgoing += *size;
ending_block:
  /* Calculate File CRC. */
  rar->crc_calculated = crc32(rar->crc_calculated, *buff, (unsigned)*size);
  return ret;
}

static int
parse_codes(struct archive_read *a)
{
  int i, j, val, n, r;
  unsigned char bitlengths[MAX_SYMBOLS], zerocount, ppmd_flags;
  unsigned int maxorder;
  struct huffman_code precode;
  struct rar *rar = (struct rar *)(a->format->data);
  struct rar_br *br = &(rar->br);

  free_codes(a);

  /* Skip to the next byte */
  rar_br_consume_unaligned_bits(br);

  /* PPMd block flag */
  if (!rar_br_read_ahead(a, br, 1))
    goto truncated_data;
  if ((rar->is_ppmd_block = rar_br_bits(br, 1)) != 0)
  {
    rar_br_consume(br, 1);
    if (!rar_br_read_ahead(a, br, 7))
      goto truncated_data;
    ppmd_flags = rar_br_bits(br, 7);
    rar_br_consume(br, 7);

    /* Memory is allocated in MB */
    if (ppmd_flags & 0x20)
    {
      if (!rar_br_read_ahead(a, br, 8))
        goto truncated_data;
      rar->dictionary_size = (rar_br_bits(br, 8) + 1) << 20;
      rar_br_consume(br, 8);
    }

    if (ppmd_flags & 0x40)
    {
      if (!rar_br_read_ahead(a, br, 8))
        goto truncated_data;
      rar->ppmd_escape = rar->ppmd7_context.InitEsc = rar_br_bits(br, 8);
      rar_br_consume(br, 8);
    }
    else
      rar->ppmd_escape = 2;

    if (ppmd_flags & 0x20)
    {
      maxorder = (ppmd_flags & 0x1F) + 1;
      if(maxorder > 16)
        maxorder = 16 + (maxorder - 16) * 3;

      if (maxorder == 1)
      {
        archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                          "Truncated RAR file data");
        return (ARCHIVE_FATAL);
      }

      /* Make sure ppmd7_contest is freed before Ppmd7_Construct
       * because reading a broken file cause this abnormal sequence. */
      __archive_ppmd7_functions.Ppmd7_Free(&rar->ppmd7_context);

      rar->bytein.a = a;
      rar->bytein.Read = &ppmd_read;
      __archive_ppmd7_functions.PpmdRAR_RangeDec_CreateVTable(&rar->range_dec);
      rar->range_dec.Stream = &rar->bytein;
      __archive_ppmd7_functions.Ppmd7_Construct(&rar->ppmd7_context);

      if (rar->dictionary_size == 0) {
        archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                          "Invalid zero dictionary size");
        return (ARCHIVE_FATAL);
      }

      if (!__archive_ppmd7_functions.Ppmd7_Alloc(&rar->ppmd7_context,
        rar->dictionary_size))
      {
        archive_set_error(&a->archive, ENOMEM,
                          "Out of memory");
        return (ARCHIVE_FATAL);
      }
      if (!__archive_ppmd7_functions.PpmdRAR_RangeDec_Init(&rar->range_dec))
      {
        archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                          "Unable to initialize PPMd range decoder");
        return (ARCHIVE_FATAL);
      }
      __archive_ppmd7_functions.Ppmd7_Init(&rar->ppmd7_context, maxorder);
      rar->ppmd_valid = 1;
    }
    else
    {
      if (!rar->ppmd_valid) {
        archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                          "Invalid PPMd sequence");
        return (ARCHIVE_FATAL);
      }
      if (!__archive_ppmd7_functions.PpmdRAR_RangeDec_Init(&rar->range_dec))
      {
        archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                          "Unable to initialize PPMd range decoder");
        return (ARCHIVE_FATAL);
      }
    }
  }
  else
  {
    rar_br_consume(br, 1);

    /* Keep existing table flag */
    if (!rar_br_read_ahead(a, br, 1))
      goto truncated_data;
    if (!rar_br_bits(br, 1))
      memset(rar->lengthtable, 0, sizeof(rar->lengthtable));
    rar_br_consume(br, 1);

    memset(&bitlengths, 0, sizeof(bitlengths));
    for (i = 0; i < MAX_SYMBOLS;)
    {
      if (!rar_br_read_ahead(a, br, 4))
        goto truncated_data;
      bitlengths[i++] = rar_br_bits(br, 4);
      rar_br_consume(br, 4);
      if (bitlengths[i-1] == 0xF)
      {
        if (!rar_br_read_ahead(a, br, 4))
          goto truncated_data;
        zerocount = rar_br_bits(br, 4);
        rar_br_consume(br, 4);
        if (zerocount)
        {
          i--;
          for (j = 0; j < zerocount + 2 && i < MAX_SYMBOLS; j++)
            bitlengths[i++] = 0;
        }
      }
    }

    memset(&precode, 0, sizeof(precode));
    r = create_code(a, &precode, bitlengths, MAX_SYMBOLS, MAX_SYMBOL_LENGTH);
    if (r != ARCHIVE_OK) {
      free(precode.tree);
      free(precode.table);
      return (r);
    }

    for (i = 0; i < HUFFMAN_TABLE_SIZE;)
    {
      if ((val = read_next_symbol(a, &precode)) < 0) {
        free(precode.tree);
        free(precode.table);
        return (ARCHIVE_FATAL);
      }
      if (val < 16)
      {
        rar->lengthtable[i] = (rar->lengthtable[i] + val) & 0xF;
        i++;
      }
      else if (val < 18)
      {
        if (i == 0)
        {
          free(precode.tree);
          free(precode.table);
          archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                            "Internal error extracting RAR file.");
          return (ARCHIVE_FATAL);
        }

        if(val == 16) {
          if (!rar_br_read_ahead(a, br, 3)) {
            free(precode.tree);
            free(precode.table);
            goto truncated_data;
          }
          n = rar_br_bits(br, 3) + 3;
          rar_br_consume(br, 3);
        } else {
          if (!rar_br_read_ahead(a, br, 7)) {
            free(precode.tree);
            free(precode.table);
            goto truncated_data;
          }
          n = rar_br_bits(br, 7) + 11;
          rar_br_consume(br, 7);
        }

        for (j = 0; j < n && i < HUFFMAN_TABLE_SIZE; j++)
        {
          rar->lengthtable[i] = rar->lengthtable[i-1];
          i++;
        }
      }
      else
      {
        if(val == 18) {
          if (!rar_br_read_ahead(a, br, 3)) {
            free(precode.tree);
            free(precode.table);
            goto truncated_data;
          }
          n = rar_br_bits(br, 3) + 3;
          rar_br_consume(br, 3);
        } else {
          if (!rar_br_read_ahead(a, br, 7)) {
            free(precode.tree);
            free(precode.table);
            goto truncated_data;
          }
          n = rar_br_bits(br, 7) + 11;
          rar_br_consume(br, 7);
        }

        for(j = 0; j < n && i < HUFFMAN_TABLE_SIZE; j++)
          rar->lengthtable[i++] = 0;
      }
    }
    free(precode.tree);
    free(precode.table);

    r = create_code(a, &rar->maincode, &rar->lengthtable[0], MAINCODE_SIZE,
                MAX_SYMBOL_LENGTH);
    if (r != ARCHIVE_OK)
      return (r);
    r = create_code(a, &rar->offsetcode, &rar->lengthtable[MAINCODE_SIZE],
                OFFSETCODE_SIZE, MAX_SYMBOL_LENGTH);
    if (r != ARCHIVE_OK)
      return (r);
    r = create_code(a, &rar->lowoffsetcode,
                &rar->lengthtable[MAINCODE_SIZE + OFFSETCODE_SIZE],
                LOWOFFSETCODE_SIZE, MAX_SYMBOL_LENGTH);
    if (r != ARCHIVE_OK)
      return (r);
    r = create_code(a, &rar->lengthcode,
                &rar->lengthtable[MAINCODE_SIZE + OFFSETCODE_SIZE +
                LOWOFFSETCODE_SIZE], LENGTHCODE_SIZE, MAX_SYMBOL_LENGTH);
    if (r != ARCHIVE_OK)
      return (r);
  }

  if (!rar->dictionary_size || !rar->lzss.window)
  {
    /* Seems as though dictionary sizes are not used. Even so, minimize
     * memory usage as much as possible.
     */
    void *new_window;
    unsigned int new_size;

    if (rar->unp_size >= DICTIONARY_MAX_SIZE)
      new_size = DICTIONARY_MAX_SIZE;
    else
      new_size = rar_fls((unsigned int)rar->unp_size) << 1;
    if (new_size == 0) {
      archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                        "Zero window size is invalid.");
      return (ARCHIVE_FATAL);
    }
    new_window = realloc(rar->lzss.window, new_size);
    if (new_window == NULL) {
      archive_set_error(&a->archive, ENOMEM,
                        "Unable to allocate memory for uncompressed data.");
      return (ARCHIVE_FATAL);
    }
    rar->lzss.window = (unsigned char *)new_window;
    rar->dictionary_size = new_size;
    memset(rar->lzss.window, 0, rar->dictionary_size);
    rar->lzss.mask = rar->dictionary_size - 1;
  }

  rar->start_new_table = 0;
  return (ARCHIVE_OK);
truncated_data:
  archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                    "Truncated RAR file data");
  rar->valid = 0;
  return (ARCHIVE_FATAL);
}

static void
free_codes(struct archive_read *a)
{
  struct rar *rar = (struct rar *)(a->format->data);
  free(rar->maincode.tree);
  free(rar->offsetcode.tree);
  free(rar->lowoffsetcode.tree);
  free(rar->lengthcode.tree);
  free(rar->maincode.table);
  free(rar->offsetcode.table);
  free(rar->lowoffsetcode.table);
  free(rar->lengthcode.table);
  memset(&rar->maincode, 0, sizeof(rar->maincode));
  memset(&rar->offsetcode, 0, sizeof(rar->offsetcode));
  memset(&rar->lowoffsetcode, 0, sizeof(rar->lowoffsetcode));
  memset(&rar->lengthcode, 0, sizeof(rar->lengthcode));
}


static int
read_next_symbol(struct archive_read *a, struct huffman_code *code)
{
  unsigned char bit;
  unsigned int bits;
  int length, value, node;
  struct rar *rar;
  struct rar_br *br;

  if (!code->table)
  {
    if (make_table(a, code) != (ARCHIVE_OK))
      return -1;
  }

  rar = (struct rar *)(a->format->data);
  br = &(rar->br);

  /* Look ahead (peek) at bits */
  if (!rar_br_read_ahead(a, br, code->tablesize)) {
    archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                      "Truncated RAR file data");
    rar->valid = 0;
    return -1;
  }
  bits = rar_br_bits(br, code->tablesize);

  length = code->table[bits].length;
  value = code->table[bits].value;

  if (length < 0)
  {
    archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                      "Invalid prefix code in bitstream");
    return -1;
  }

  if (length <= code->tablesize)
  {
    /* Skip length bits */
    rar_br_consume(br, length);
    return value;
  }

  /* Skip tablesize bits */
  rar_br_consume(br, code->tablesize);

  node = value;
  while (code->tree[node].branches[0] != code->tree[node].branches[1])
  {
    if (!rar_br_read_ahead(a, br, 1)) {
      archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                        "Truncated RAR file data");
      rar->valid = 0;
      return -1;
    }
    bit = rar_br_bits(br, 1);
    rar_br_consume(br, 1);

    if (code->tree[node].branches[bit] < 0)
    {
      archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                        "Invalid prefix code in bitstream");
      return -1;
    }
    node = code->tree[node].branches[bit];
  }

  return code->tree[node].branches[0];
}

static int
create_code(struct archive_read *a, struct huffman_code *code,
            unsigned char *lengths, int numsymbols, char maxlength)
{
  int i, j, codebits = 0, symbolsleft = numsymbols;

  code->numentries = 0;
  code->numallocatedentries = 0;
  if (new_node(code) < 0) {
    archive_set_error(&a->archive, ENOMEM,
                      "Unable to allocate memory for node data.");
    return (ARCHIVE_FATAL);
  }
  code->numentries = 1;
  code->minlength = INT_MAX;
  code->maxlength = INT_MIN;
  codebits = 0;
  for(i = 1; i <= maxlength; i++)
  {
    for(j = 0; j < numsymbols; j++)
    {
      if (lengths[j] != i) continue;
      if (add_value(a, code, j, codebits, i) != ARCHIVE_OK)
        return (ARCHIVE_FATAL);
      codebits++;
      if (--symbolsleft <= 0)
        break;
    }
    if (symbolsleft <= 0)
      break;
    codebits <<= 1;
  }
  return (ARCHIVE_OK);
}

static int
add_value(struct archive_read *a, struct huffman_code *code, int value,
          int codebits, int length)
{
  int lastnode, bitpos, bit;
  /* int repeatpos, repeatnode, nextnode; */

  free(code->table);
  code->table = NULL;

  if(length > code->maxlength)
    code->maxlength = length;
  if(length < code->minlength)
    code->minlength = length;

  /*
   * Dead code, repeatpos was is -1
   *
  repeatpos = -1;
  if (repeatpos == 0 || (repeatpos >= 0
    && (((codebits >> (repeatpos - 1)) & 3) == 0
    || ((codebits >> (repeatpos - 1)) & 3) == 3)))
  {
    archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                      "Invalid repeat position");
    return (ARCHIVE_FATAL);
  }
  */

  lastnode = 0;
  for (bitpos = length - 1; bitpos >= 0; bitpos--)
  {
    bit = (codebits >> bitpos) & 1;

    /* Leaf node check */
    if (code->tree[lastnode].branches[0] ==
      code->tree[lastnode].branches[1])
    {
      archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                        "Prefix found");
      return (ARCHIVE_FATAL);
    }

    /*
     * Dead code, repeatpos was -1, bitpos >=0
     *
    if (bitpos == repeatpos)
    {
      * Open branch check *
      if (!(code->tree[lastnode].branches[bit] < 0))
      {
        archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                          "Invalid repeating code");
        return (ARCHIVE_FATAL);
      }

      if ((repeatnode = new_node(code)) < 0) {
        archive_set_error(&a->archive, ENOMEM,
                          "Unable to allocate memory for node data.");
        return (ARCHIVE_FATAL);
      }
      if ((nextnode = new_node(code)) < 0) {
        archive_set_error(&a->archive, ENOMEM,
                          "Unable to allocate memory for node data.");
        return (ARCHIVE_FATAL);
      }

      * Set branches *
      code->tree[lastnode].branches[bit] = repeatnode;
      code->tree[repeatnode].branches[bit] = repeatnode;
      code->tree[repeatnode].branches[bit^1] = nextnode;
      lastnode = nextnode;

      bitpos++; * terminating bit already handled, skip it *
    }
    else
    {
    */
      /* Open branch check */
      if (code->tree[lastnode].branches[bit] < 0)
      {
        if (new_node(code) < 0) {
          archive_set_error(&a->archive, ENOMEM,
                            "Unable to allocate memory for node data.");
          return (ARCHIVE_FATAL);
        }
        code->tree[lastnode].branches[bit] = code->numentries++;
      }

      /* set to branch */
      lastnode = code->tree[lastnode].branches[bit];
 /* } */
  }

  if (!(code->tree[lastnode].branches[0] == -1
    && code->tree[lastnode].branches[1] == -2))
  {
    archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                      "Prefix found");
    return (ARCHIVE_FATAL);
  }

  /* Set leaf value */
  code->tree[lastnode].branches[0] = value;
  code->tree[lastnode].branches[1] = value;

  return (ARCHIVE_OK);
}

static int
new_node(struct huffman_code *code)
{
  void *new_tree;
  if (code->numallocatedentries == code->numentries) {
    int new_num_entries = 256;
    if (code->numentries > 0) {
        new_num_entries = code->numentries * 2;
    }
    new_tree = realloc(code->tree, new_num_entries * sizeof(*code->tree));
    if (new_tree == NULL)
        return (-1);
    code->tree = (struct huffman_tree_node *)new_tree;
    code->numallocatedentries = new_num_entries;
  }
  code->tree[code->numentries].branches[0] = -1;
  code->tree[code->numentries].branches[1] = -2;
  return 1;
}

static int
make_table(struct archive_read *a, struct huffman_code *code)
{
  if (code->maxlength < code->minlength || code->maxlength > 10)
    code->tablesize = 10;
  else
    code->tablesize = code->maxlength;

  code->table = calloc(((size_t)1U) << code->tablesize, sizeof(*code->table));

  return make_table_recurse(a, code, 0, code->table, 0, code->tablesize);
}

static int
make_table_recurse(struct archive_read *a, struct huffman_code *code, int node,
                   struct huffman_table_entry *table, int depth,
                   int maxdepth)
{
  int currtablesize, i, ret = (ARCHIVE_OK);

  if (!code->tree)
  {
    archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                      "Huffman tree was not created.");
    return (ARCHIVE_FATAL);
  }
  if (node < 0 || node >= code->numentries)
  {
    archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                      "Invalid location to Huffman tree specified.");
    return (ARCHIVE_FATAL);
  }

  currtablesize = 1 << (maxdepth - depth);

  if (code->tree[node].branches[0] ==
    code->tree[node].branches[1])
  {
    for(i = 0; i < currtablesize; i++)
    {
      table[i].length = depth;
      table[i].value = code->tree[node].branches[0];
    }
  }
  /*
   * Dead code, node >= 0
   *
  else if (node < 0)
  {
    for(i = 0; i < currtablesize; i++)
      table[i].length = -1;
  }
   */
  else
  {
    if(depth == maxdepth)
    {
      table[0].length = maxdepth + 1;
      table[0].value = node;
    }
    else
    {
      ret |= make_table_recurse(a, code, code->tree[node].branches[0], table,
                                depth + 1, maxdepth);
      ret |= make_table_recurse(a, code, code->tree[node].branches[1],
                         table + currtablesize / 2, depth + 1, maxdepth);
    }
  }
  return ret;
}

static int
expand(struct archive_read *a, int64_t *end)
{
  static const unsigned char lengthbases[] =
    {   0,   1,   2,   3,   4,   5,   6,
        7,   8,  10,  12,  14,  16,  20,
       24,  28,  32,  40,  48,  56,  64,
       80,  96, 112, 128, 160, 192, 224 };
  static const unsigned char lengthbits[] =
    { 0, 0, 0, 0, 0, 0, 0,
      0, 1, 1, 1, 1, 2, 2,
      2, 2, 3, 3, 3, 3, 4,
      4, 4, 4, 5, 5, 5, 5 };
  static const int lengthb_min = minimum(
    (int)(sizeof(lengthbases)/sizeof(lengthbases[0])),
    (int)(sizeof(lengthbits)/sizeof(lengthbits[0]))
  );
  static const unsigned int offsetbases[] =
    {       0,       1,       2,       3,       4,       6,
            8,      12,      16,      24,      32,      48,
           64,      96,     128,     192,     256,     384,
          512,     768,    1024,    1536,    2048,    3072,
         4096,    6144,    8192,   12288,   16384,   24576,
        32768,   49152,   65536,   98304,  131072,  196608,
       262144,  327680,  393216,  458752,  524288,  589824,
       655360,  720896,  786432,  851968,  917504,  983040,
      1048576, 1310720, 1572864, 1835008, 2097152, 2359296,
      2621440, 2883584, 3145728, 3407872, 3670016, 3932160 };
  static const unsigned char offsetbits[] =
    {  0,  0,  0,  0,  1,  1,  2,  2,  3,  3,  4,  4,
       5,  5,  6,  6,  7,  7,  8,  8,  9,  9, 10, 10,
      11, 11, 12, 12, 13, 13, 14, 14, 15, 15, 16, 16,
      16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
      18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18 };
  static const int offsetb_min = minimum(
    (int)(sizeof(offsetbases)/sizeof(offsetbases[0])),
    (int)(sizeof(offsetbits)/sizeof(offsetbits[0]))
  );
  static const unsigned char shortbases[] =
    { 0, 4, 8, 16, 32, 64, 128, 192 };
  static const unsigned char shortbits[] =
    { 2, 2, 3, 4, 5, 6, 6, 6 };

  int symbol, offs, len, offsindex, lensymbol, i, offssymbol, lowoffsetsymbol;
  unsigned char newfile;
  struct rar *rar = (struct rar *)(a->format->data);
  struct rar_br *br = &(rar->br);

  if (rar->filters.filterstart < *end)
    *end = rar->filters.filterstart;

  while (1)
  {
    if(lzss_position(&rar->lzss) >= *end) {
      return (ARCHIVE_OK);
    }

    if(rar->is_ppmd_block) {
      *end = lzss_position(&rar->lzss);
      return (ARCHIVE_OK);
    }

    if ((symbol = read_next_symbol(a, &rar->maincode)) < 0)
      goto bad_data;

    if (symbol < 256)
    {
      lzss_emit_literal(rar, (uint8_t)symbol);
      continue;
    }
    else if (symbol == 256)
    {
      if (!rar_br_read_ahead(a, br, 1))
        goto truncated_data;
      newfile = !rar_br_bits(br, 1);
      rar_br_consume(br, 1);

      if(newfile)
      {
        rar->start_new_block = 1;
        if (!rar_br_read_ahead(a, br, 1))
          goto truncated_data;
        rar->start_new_table = rar_br_bits(br, 1);
        rar_br_consume(br, 1);
        *end = lzss_position(&rar->lzss);
        return (ARCHIVE_OK);
      }
      else
      {
        if (parse_codes(a) != ARCHIVE_OK)
          goto bad_data;
        continue;
      }
    }
    else if(symbol==257)
    {
      if (!read_filter(a, end))
          goto bad_data;
      continue;
    }
    else if(symbol==258)
    {
      if(rar->lastlength == 0)
        continue;

      offs = rar->lastoffset;
      len = rar->lastlength;
    }
    else if (symbol <= 262)
    {
      offsindex = symbol - 259;
      offs = rar->oldoffset[offsindex];

      if ((lensymbol = read_next_symbol(a, &rar->lengthcode)) < 0)
        goto bad_data;
      if (lensymbol >= lengthb_min)
        goto bad_data;
      len = lengthbases[lensymbol] + 2;
      if (lengthbits[lensymbol] > 0) {
        if (!rar_br_read_ahead(a, br, lengthbits[lensymbol]))
          goto truncated_data;
        len += rar_br_bits(br, lengthbits[lensymbol]);
        rar_br_consume(br, lengthbits[lensymbol]);
      }

      for (i = offsindex; i > 0; i--)
        rar->oldoffset[i] = rar->oldoffset[i-1];
      rar->oldoffset[0] = offs;
    }
    else if(symbol<=270)
    {
      offs = shortbases[symbol-263] + 1;
      if(shortbits[symbol-263] > 0) {
        if (!rar_br_read_ahead(a, br, shortbits[symbol-263]))
          goto truncated_data;
        offs += rar_br_bits(br, shortbits[symbol-263]);
        rar_br_consume(br, shortbits[symbol-263]);
      }

      len = 2;

      for(i = 3; i > 0; i--)
        rar->oldoffset[i] = rar->oldoffset[i-1];
      rar->oldoffset[0] = offs;
    }
    else
    {
      if (symbol-271 >= lengthb_min)
        goto bad_data;
      len = lengthbases[symbol-271]+3;
      if(lengthbits[symbol-271] > 0) {
        if (!rar_br_read_ahead(a, br, lengthbits[symbol-271]))
          goto truncated_data;
        len += rar_br_bits(br, lengthbits[symbol-271]);
        rar_br_consume(br, lengthbits[symbol-271]);
      }

      if ((offssymbol = read_next_symbol(a, &rar->offsetcode)) < 0)
        goto bad_data;
      if (offssymbol >= offsetb_min)
        goto bad_data;
      offs = offsetbases[offssymbol]+1;
      if(offsetbits[offssymbol] > 0)
      {
        if(offssymbol > 9)
        {
          if(offsetbits[offssymbol] > 4) {
            if (!rar_br_read_ahead(a, br, offsetbits[offssymbol] - 4))
              goto truncated_data;
            offs += rar_br_bits(br, offsetbits[offssymbol] - 4) << 4;
            rar_br_consume(br, offsetbits[offssymbol] - 4);
          }

          if(rar->numlowoffsetrepeats > 0)
          {
            rar->numlowoffsetrepeats--;
            offs += rar->lastlowoffset;
          }
          else
          {
            if ((lowoffsetsymbol =
              read_next_symbol(a, &rar->lowoffsetcode)) < 0)
              goto bad_data;
            if(lowoffsetsymbol == 16)
            {
              rar->numlowoffsetrepeats = 15;
              offs += rar->lastlowoffset;
            }
            else
            {
              offs += lowoffsetsymbol;
              rar->lastlowoffset = lowoffsetsymbol;
            }
          }
        }
        else {
          if (!rar_br_read_ahead(a, br, offsetbits[offssymbol]))
            goto truncated_data;
          offs += rar_br_bits(br, offsetbits[offssymbol]);
          rar_br_consume(br, offsetbits[offssymbol]);
        }
      }

      if (offs >= 0x40000)
        len++;
      if (offs >= 0x2000)
        len++;

      for(i = 3; i > 0; i--)
        rar->oldoffset[i] = rar->oldoffset[i-1];
      rar->oldoffset[0] = offs;
    }

    rar->lastoffset = offs;
    rar->lastlength = len;

    lzss_emit_match(rar, rar->lastoffset, rar->lastlength);
  }
truncated_data:
  archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                    "Truncated RAR file data");
  rar->valid = 0;
  return (ARCHIVE_FATAL);
bad_data:
  archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                    "Bad RAR file data");
  return (ARCHIVE_FATAL);
}

static int
copy_from_lzss_window(struct archive_read *a, uint8_t *buffer,
                      int64_t startpos, int length)
{
  int windowoffs, firstpart;
  struct rar *rar = (struct rar *)(a->format->data);

  windowoffs = lzss_offset_for_position(&rar->lzss, startpos);
  firstpart = lzss_size(&rar->lzss) - windowoffs;
  if (firstpart < 0) {
    archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                      "Bad RAR file data");
    return (ARCHIVE_FATAL);
  }
  if (firstpart < length) {
    memcpy(buffer, &rar->lzss.window[windowoffs], firstpart);
    memcpy(buffer + firstpart, &rar->lzss.window[0], length - firstpart);
  } else {
    memcpy(buffer, &rar->lzss.window[windowoffs], length);
  }
  return (ARCHIVE_OK);
}

static int
copy_from_lzss_window_to_unp(struct archive_read *a, const void **buffer,
                             int64_t startpos, size_t length)
{
  int windowoffs, firstpart;
  struct rar *rar = (struct rar *)(a->format->data);

  if (length > rar->unp_buffer_size)
  {
    goto fatal;
  }

  if (!rar->unp_buffer)
  {
    if ((rar->unp_buffer = malloc(rar->unp_buffer_size)) == NULL)
    {
      archive_set_error(&a->archive, ENOMEM,
                        "Unable to allocate memory for uncompressed data.");
      return (ARCHIVE_FATAL);
    }
  }

  windowoffs = lzss_offset_for_position(&rar->lzss, startpos);
  if(windowoffs + length <= (size_t)lzss_size(&rar->lzss)) {
    memcpy(&rar->unp_buffer[rar->unp_offset], &rar->lzss.window[windowoffs],
           length);
  } else if (length <= (size_t)lzss_size(&rar->lzss)) {
    firstpart = lzss_size(&rar->lzss) - windowoffs;
    if (firstpart < 0) {
      archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                        "Bad RAR file data");
      return (ARCHIVE_FATAL);
    }
    if ((size_t)firstpart < length) {
      memcpy(&rar->unp_buffer[rar->unp_offset],
             &rar->lzss.window[windowoffs], firstpart);
      memcpy(&rar->unp_buffer[rar->unp_offset + firstpart],
             &rar->lzss.window[0], length - firstpart);
    } else {
      memcpy(&rar->unp_buffer[rar->unp_offset],
             &rar->lzss.window[windowoffs], length);
    }
  } else {
      goto fatal;
  }
  rar->unp_offset += (unsigned int) length;
  if (rar->unp_offset >= rar->unp_buffer_size)
    *buffer = rar->unp_buffer;
  else
    *buffer = NULL;
  return (ARCHIVE_OK);

fatal:
  archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                    "Bad RAR file data");
  return (ARCHIVE_FATAL);
}

static const void *
rar_read_ahead(struct archive_read *a, size_t min, ssize_t *avail)
{
  struct rar *rar = (struct rar *)(a->format->data);
  const void *h;
  int ret;

again:
  h = __archive_read_ahead(a, min, avail);

  if (avail)
  {
    if (a->archive.read_data_is_posix_read && *avail > (ssize_t)a->archive.read_data_requested)
      *avail = a->archive.read_data_requested;
    if (*avail > rar->bytes_remaining)
      *avail = (ssize_t)rar->bytes_remaining;
    if (*avail < 0)
      return NULL;
    else if (*avail == 0 && rar->main_flags & MHD_VOLUME &&
      rar->file_flags & FHD_SPLIT_AFTER)
    {
      rar->filename_must_match = 1;
      ret = archive_read_format_rar_read_header(a, a->entry);
      if (ret == (ARCHIVE_EOF))
      {
        rar->has_endarc_header = 1;
        ret = archive_read_format_rar_read_header(a, a->entry);
      }
      rar->filename_must_match = 0;
      if (ret != (ARCHIVE_OK))
        return NULL;
      goto again;
    }
  }
  return h;
}

static int
parse_filter(struct archive_read *a, const uint8_t *bytes, uint16_t length, uint8_t flags)
{
  struct rar *rar = (struct rar *)(a->format->data);
  struct rar_filters *filters = &rar->filters;

  struct memory_bit_reader br = { 0 };
  struct rar_program_code *prog;
  struct rar_filter *filter, **nextfilter;

  uint32_t numprogs, num, blocklength, globaldatalen;
  uint8_t *globaldata;
  size_t blockstartpos;
  uint32_t registers[8] = { 0 };
  uint32_t i;

  br.bytes = bytes;
  br.length = length;

  numprogs = 0;
  for (prog = filters->progs; prog; prog = prog->next)
    numprogs++;

  if ((flags & 0x80))
  {
    num = membr_next_rarvm_number(&br);
    if (num == 0)
    {
      delete_filter(filters->stack);
      filters->stack = NULL;
      delete_program_code(filters->progs);
      filters->progs = NULL;
    }
    else
      num--;
    if (num > numprogs) {
      return 0;
    }
    filters->lastfilternum = num;
  }
  else
    num = filters->lastfilternum;

  prog = filters->progs;
  for (i = 0; i < num; i++)
    prog = prog->next;
  if (prog)
    prog->usagecount++;

  blockstartpos = membr_next_rarvm_number(&br) + (size_t)lzss_position(&rar->lzss);
  if ((flags & 0x40))
    blockstartpos += 258;
  if ((flags & 0x20))
    blocklength = membr_next_rarvm_number(&br);
  else
    blocklength = prog ? prog->oldfilterlength : 0;

  if (blocklength > rar->dictionary_size)
    return 0;

  registers[3] = PROGRAM_SYSTEM_GLOBAL_ADDRESS;
  registers[4] = blocklength;
  registers[5] = prog ? prog->usagecount : 0;
  registers[7] = VM_MEMORY_SIZE;

  if ((flags & 0x10))
  {
    uint8_t mask = (uint8_t)membr_bits(&br, 7);
    for (i = 0; i < 7; i++)
      if ((mask & (1 << i)))
        registers[i] = membr_next_rarvm_number(&br);
  }

  if (!prog)
  {
    uint32_t len = membr_next_rarvm_number(&br);
    uint8_t *bytecode;
    struct rar_program_code **next;

    if (len == 0 || len > 0x10000)
      return 0;
    bytecode = malloc(len);
    if (!bytecode)
      return 0;
    for (i = 0; i < len; i++)
      bytecode[i] = (uint8_t)membr_bits(&br, 8);
    prog = compile_program(bytecode, len);
    if (!prog) {
      free(bytecode);
      return 0;
    }
    free(bytecode);
    next = &filters->progs;
    while (*next)
      next = &(*next)->next;
    *next = prog;
  }
  prog->oldfilterlength = blocklength;

  globaldata = NULL;
  globaldatalen = 0;
  if ((flags & 0x08))
  {
    globaldatalen = membr_next_rarvm_number(&br);
    if (globaldatalen > PROGRAM_USER_GLOBAL_SIZE)
      return 0;
    globaldata = malloc(globaldatalen + PROGRAM_SYSTEM_GLOBAL_SIZE);
    if (!globaldata)
      return 0;
    for (i = 0; i < globaldatalen; i++)
      globaldata[i + PROGRAM_SYSTEM_GLOBAL_SIZE] = (uint8_t)membr_bits(&br, 8);
  }

  if (br.at_eof)
  {
      free(globaldata);
      return 0;
  }

  filter = create_filter(prog, globaldata, globaldatalen, registers, blockstartpos, blocklength);
  free(globaldata);
  if (!filter)
    return 0;

  for (i = 0; i < 7; i++)
    archive_le32enc(&filter->globaldata[i * 4], registers[i]);
  archive_le32enc(&filter->globaldata[0x1C], blocklength);
  archive_le32enc(&filter->globaldata[0x20], 0);
  archive_le32enc(&filter->globaldata[0x2C], prog->usagecount);

  nextfilter = &filters->stack;
  while (*nextfilter)
    nextfilter = &(*nextfilter)->next;
  *nextfilter = filter;

  if (!filters->stack->next)
    filters->filterstart = blockstartpos;

  return 1;
}

static struct rar_filter *
create_filter(struct rar_program_code *prog, const uint8_t *globaldata, uint32_t globaldatalen, uint32_t registers[8], size_t startpos, uint32_t length)
{
  struct rar_filter *filter;

  filter = calloc(1, sizeof(*filter));
  if (!filter)
    return NULL;
  filter->prog = prog;
  filter->globaldatalen = globaldatalen > PROGRAM_SYSTEM_GLOBAL_SIZE ? globaldatalen : PROGRAM_SYSTEM_GLOBAL_SIZE;
  filter->globaldata = calloc(1, filter->globaldatalen);
  if (!filter->globaldata)
  {
    free(filter);
    return NULL;
  }
  if (globaldata)
    memcpy(filter->globaldata, globaldata, globaldatalen);
  if (registers)
    memcpy(filter->initialregisters, registers, sizeof(filter->initialregisters));
  filter->blockstartpos = startpos;
  filter->blocklength = length;

  return filter;
}

static int
run_filters(struct archive_read *a)
{
  struct rar *rar = (struct rar *)(a->format->data);
  struct rar_filters *filters = &rar->filters;
  struct rar_filter *filter = filters->stack;
  struct rar_filter *f;
  size_t start, end;
  int64_t tend;
  uint32_t lastfilteraddress;
  uint32_t lastfilterlength;
  int ret;

  if (filters == NULL || filter == NULL)
    return (0);

  start = (size_t)filters->filterstart;
  end = start + filter->blocklength;

  filters->filterstart = INT64_MAX;
  tend = (int64_t)end;
  ret = expand(a, &tend);
  if (ret != ARCHIVE_OK)
    return 0;

  /* Check if filter stack was modified in expand() */
  ret = ARCHIVE_FATAL;
  f = filters->stack;
  while (f)
  {
    if (f == filter)
    {
      ret = ARCHIVE_OK;
      break;
    }
    f = f->next;
  }
  if (ret != ARCHIVE_OK)
    return 0;

  if (tend < 0)
    return 0;
  end = (size_t)tend;
  if (end != start + filter->blocklength)
    return 0;

  if (!filters->vm)
  {
    filters->vm = calloc(1, sizeof(*filters->vm));
    if (!filters->vm)
      return 0;
  }

  if (filter->blocklength > VM_MEMORY_SIZE)
  {
    archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT, "Bad RAR file data");
    return 0;
  }

  ret = copy_from_lzss_window(a, filters->vm->memory, start, filter->blocklength);
  if (ret != ARCHIVE_OK)
    return 0;
  if (!execute_filter(a, filter, filters->vm, (size_t)rar->offset))
    return 0;

  lastfilteraddress = filter->filteredblockaddress;
  lastfilterlength = filter->filteredblocklength;
  filters->stack = filter->next;
  filter->next = NULL;
  delete_filter(filter);

  while ((filter = filters->stack) != NULL && (int64_t)filter->blockstartpos == filters->filterstart && filter->blocklength == lastfilterlength)
  {
    memmove(&filters->vm->memory[0], &filters->vm->memory[lastfilteraddress], lastfilterlength);
    if (!execute_filter(a, filter, filters->vm, (size_t)rar->offset))
      return 0;

    lastfilteraddress = filter->filteredblockaddress;
    lastfilterlength = filter->filteredblocklength;
    filters->stack = filter->next;
    filter->next = NULL;
    delete_filter(filter);
  }

  if (filters->stack)
  {
    if (filters->stack->blockstartpos < end)
      return 0;
    filters->filterstart = filters->stack->blockstartpos;
  }

  filters->lastend = end;
  filters->bytes = &filters->vm->memory[lastfilteraddress];
  filters->bytes_ready = lastfilterlength;

  return 1;
}

static struct rar_program_code *
compile_program(const uint8_t *bytes, size_t length)
{
  struct memory_bit_reader br = { 0 };
  struct rar_program_code *prog;
  /* uint32_t instrcount = 0; */
  uint8_t xor;
  size_t i;

  xor = 0;
  for (i = 1; i < length; i++)
    xor ^= bytes[i];
  if (!length || xor != bytes[0])
    return NULL;

  br.bytes = bytes;
  br.length = length;
  br.offset = 1;

  prog = calloc(1, sizeof(*prog));
  if (!prog)
    return NULL;
  prog->fingerprint = crc32(0, bytes, (unsigned int)length) | ((uint64_t)length << 32);

  if (membr_bits(&br, 1))
  {
    prog->staticdatalen = membr_next_rarvm_number(&br) + 1;
    prog->staticdata = malloc(prog->staticdatalen);
    if (!prog->staticdata)
    {
      delete_program_code(prog);
      return NULL;
    }
    for (i = 0; i < prog->staticdatalen; i++)
      prog->staticdata[i] = (uint8_t)membr_bits(&br, 8);
  }

  return prog;
}

static void
delete_filter(struct rar_filter *filter)
{
  while (filter)
  {
    struct rar_filter *next = filter->next;
    free(filter->globaldata);
    free(filter);
    filter = next;
  }
}

static void
clear_filters(struct rar_filters *filters)
{
  delete_filter(filters->stack);
  delete_program_code(filters->progs);
  free(filters->vm);
}

static void
delete_program_code(struct rar_program_code *prog)
{
  while (prog)
  {
    struct rar_program_code *next = prog->next;
    free(prog->staticdata);
    free(prog->globalbackup);
    free(prog);
    prog = next;
  }
}

static uint32_t
membr_next_rarvm_number(struct memory_bit_reader *br)
{
  uint32_t val;
  switch (membr_bits(br, 2))
  {
    case 0:
      return membr_bits(br, 4);
    case 1:
      val = membr_bits(br, 8);
      if (val >= 16)
        return val;
      return 0xFFFFFF00 | (val << 4) | membr_bits(br, 4);
    case 2:
      return membr_bits(br, 16);
    default:
      return membr_bits(br, 32);
  }
}

static uint32_t
membr_bits(struct memory_bit_reader *br, int bits)
{
  if (bits > br->available && (br->at_eof || !membr_fill(br, bits)))
    return 0;
  return (uint32_t)((br->bits >> (br->available -= bits)) & (((uint64_t)1 << bits) - 1));
}

static int
membr_fill(struct memory_bit_reader *br, int bits)
{
  while (br->available < bits && br->offset < br->length)
  {
    br->bits = (br->bits << 8) | br->bytes[br->offset++];
    br->available += 8;
  }
  if (bits > br->available)
  {
    br->at_eof = 1;
    return 0;
  }
  return 1;
}

static int
read_filter(struct archive_read *a, int64_t *end)
{
  struct rar *rar = (struct rar *)(a->format->data);
  uint8_t flags, val, *code;
  uint16_t length, i;

  if (!rar_decode_byte(a, &flags))
    return 0;
  length = (flags & 0x07) + 1;
  if (length == 7)
  {
    if (!rar_decode_byte(a, &val))
      return 0;
    length = val + 7;
  }
  else if (length == 8)
  {
    if (!rar_decode_byte(a, &val))
      return 0;
    length = val << 8;
    if (!rar_decode_byte(a, &val))
      return 0;
    length |= val;
  }

  code = malloc(length);
  if (!code)
    return 0;
  for (i = 0; i < length; i++)
  {
    if (!rar_decode_byte(a, &code[i]))
    {
      free(code);
      return 0;
    }
  }
  if (!parse_filter(a, code, length, flags))
  {
    free(code);
    return 0;
  }
  free(code);

  if (rar->filters.filterstart < *end)
    *end = rar->filters.filterstart;

  return 1;
}

static int
execute_filter_delta(struct rar_filter *filter, struct rar_virtual_machine *vm)
{
  uint32_t length = filter->initialregisters[4];
  uint32_t numchannels = filter->initialregisters[0];
  uint8_t *src, *dst;
  uint32_t i, idx;

  if (length > PROGRAM_WORK_SIZE / 2)
    return 0;

  src = &vm->memory[0];
  dst = &vm->memory[length];
  for (i = 0; i < numchannels; i++)
  {
    uint8_t lastbyte = 0;
    for (idx = i; idx < length; idx += numchannels)
    {
      /*
       * The src block should not overlap with the dst block.
       * If so it would be better to consider this archive is broken.
       */
      if (src >= dst)
        return 0;
      lastbyte = dst[idx] = lastbyte - *src++;
    }
  }

  filter->filteredblockaddress = length;
  filter->filteredblocklength = length;

  return 1;
}

static int
execute_filter_e8(struct rar_filter *filter, struct rar_virtual_machine *vm, size_t pos, int e9also)
{
  uint32_t length = filter->initialregisters[4];
  uint32_t filesize = 0x1000000;
  uint32_t i;

  if (length > PROGRAM_WORK_SIZE || length <= 4)
    return 0;

  for (i = 0; i <= length - 5; i++)
  {
    if (vm->memory[i] == 0xE8 || (e9also && vm->memory[i] == 0xE9))
    {
      uint32_t currpos = (uint32_t)pos + i + 1;
      int32_t address = (int32_t)vm_read_32(vm, i + 1);
      if (address < 0 && currpos >= (~(uint32_t)address + 1))
        vm_write_32(vm, i + 1, address + filesize);
      else if (address >= 0 && (uint32_t)address < filesize)
        vm_write_32(vm, i + 1, address - currpos);
      i += 4;
    }
  }

  filter->filteredblockaddress = 0;
  filter->filteredblocklength = length;

  return 1;
}

static int
execute_filter_rgb(struct rar_filter *filter, struct rar_virtual_machine *vm)
{
  uint32_t stride = filter->initialregisters[0];
  uint32_t byteoffset = filter->initialregisters[1];
  uint32_t blocklength = filter->initialregisters[4];
  uint8_t *src, *dst;
  uint32_t i, j;

  if (blocklength > PROGRAM_WORK_SIZE / 2 || stride > blocklength || blocklength < 3 || byteoffset > 2)
    return 0;

  src = &vm->memory[0];
  dst = &vm->memory[blocklength];
  for (i = 0; i < 3; i++) {
    uint8_t byte = 0;
    uint8_t *prev = dst + i - stride;
    for (j = i; j < blocklength; j += 3)
    {
      /*
       * The src block should not overlap with the dst block.
       * If so it would be better to consider this archive is broken.
       */
      if (src >= dst)
        return 0;

      if (prev >= dst)
      {
        uint32_t delta1 = abs(prev[3] - prev[0]);
        uint32_t delta2 = abs(byte - prev[0]);
        uint32_t delta3 = abs(prev[3] - prev[0] + byte - prev[0]);
        if (delta1 > delta2 || delta1 > delta3)
          byte = delta2 <= delta3 ? prev[3] : prev[0];
      }
      byte -= *src++;
      dst[j] = byte;
      prev += 3;
    }
  }
  for (i = byteoffset; i < blocklength - 2; i += 3)
  {
    dst[i] += dst[i + 1];
    dst[i + 2] += dst[i + 1];
  }

  filter->filteredblockaddress = blocklength;
  filter->filteredblocklength = blocklength;

  return 1;
}

static int
execute_filter_audio(struct rar_filter *filter, struct rar_virtual_machine *vm)
{
  uint32_t length = filter->initialregisters[4];
  uint32_t numchannels = filter->initialregisters[0];
  uint8_t *src, *dst;
  uint32_t i, j;

  if (length > PROGRAM_WORK_SIZE / 2)
    return 0;

  src = &vm->memory[0];
  dst = &vm->memory[length];
  for (i = 0; i < numchannels; i++)
  {
    struct audio_state state;
    memset(&state, 0, sizeof(state));
    for (j = i; j < length; j += numchannels)
    {
      /*
       * The src block should not overlap with the dst block.
       * If so it would be better to consider this archive is broken.
       */
      if (src >= dst)
        return 0;

      int8_t delta = (int8_t)*src++;
      uint8_t predbyte, byte;
      int prederror;
      state.delta[2] = state.delta[1];
      state.delta[1] = state.lastdelta - state.delta[0];
      state.delta[0] = state.lastdelta;
      predbyte = ((8 * state.lastbyte + state.weight[0] * state.delta[0] + state.weight[1] * state.delta[1] + state.weight[2] * state.delta[2]) >> 3) & 0xFF;
      byte = (predbyte - delta) & 0xFF;
      prederror = delta << 3;
      state.error[0] += abs(prederror);
      state.error[1] += abs(prederror - state.delta[0]); state.error[2] += abs(prederror + state.delta[0]);
      state.error[3] += abs(prederror - state.delta[1]); state.error[4] += abs(prederror + state.delta[1]);
      state.error[5] += abs(prederror - state.delta[2]); state.error[6] += abs(prederror + state.delta[2]);
      state.lastdelta = (int8_t)(byte - state.lastbyte);
      dst[j] = state.lastbyte = byte;
      if (!(state.count++ & 0x1F))
      {
        uint8_t k, idx = 0;
        for (k = 1; k < 7; k++)
        {
          if (state.error[k] < state.error[idx])
            idx = k;
        }
        memset(state.error, 0, sizeof(state.error));
        switch (idx)
        {
          case 1: if (state.weight[0] >= -16) state.weight[0]--; break;
          case 2: if (state.weight[0] < 16) state.weight[0]++; break;
          case 3: if (state.weight[1] >= -16) state.weight[1]--; break;
          case 4: if (state.weight[1] < 16) state.weight[1]++; break;
          case 5: if (state.weight[2] >= -16) state.weight[2]--; break;
          case 6: if (state.weight[2] < 16) state.weight[2]++; break;
        }
      }
    }
  }

  filter->filteredblockaddress = length;
  filter->filteredblocklength = length;

  return 1;
}


static int
execute_filter(struct archive_read *a, struct rar_filter *filter, struct rar_virtual_machine *vm, size_t pos)
{
  if (filter->prog->fingerprint == 0x1D0E06077D)
    return execute_filter_delta(filter, vm);
  if (filter->prog->fingerprint == 0x35AD576887)
    return execute_filter_e8(filter, vm, pos, 0);
  if (filter->prog->fingerprint == 0x393CD7E57E)
    return execute_filter_e8(filter, vm, pos, 1);
  if (filter->prog->fingerprint == 0x951C2C5DC8)
    return execute_filter_rgb(filter, vm);
  if (filter->prog->fingerprint == 0xD8BC85E701)
    return execute_filter_audio(filter, vm);

  archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT, "No support for RAR VM program filter");
  return 0;
}

static int
rar_decode_byte(struct archive_read *a, uint8_t *byte)
{
  struct rar *rar = (struct rar *)(a->format->data);
  struct rar_br *br = &(rar->br);
  if (!rar_br_read_ahead(a, br, 8))
    return 0;
  *byte = (uint8_t)rar_br_bits(br, 8);
  rar_br_consume(br, 8);
  return 1;
}

static void
vm_write_32(struct rar_virtual_machine* vm, size_t offset, uint32_t u32)
{
  archive_le32enc(vm->memory + offset, u32);
}

static uint32_t
vm_read_32(struct rar_virtual_machine* vm, size_t offset)
{
  return archive_le32dec(vm->memory + offset);
}
#undef parse_filter
#undef AV_HEAD
#undef CACHE_BITS
#undef CACHE_TYPE
#undef COMM_HEAD
#undef COMPRESS_METHOD_BEST
#undef COMPRESS_METHOD_FAST
#undef COMPRESS_METHOD_FASTEST
#undef COMPRESS_METHOD_GOOD
#undef COMPRESS_METHOD_NORMAL
#undef COMPRESS_METHOD_STORE
#undef CRC_POLYNOMIAL
#undef DICTIONARY_MASK
#undef DICTIONARY_MAX_SIZE
#undef DICTIONARY_SIZE_1024
#undef DICTIONARY_SIZE_128
#undef DICTIONARY_SIZE_2048
#undef DICTIONARY_SIZE_256
#undef DICTIONARY_SIZE_4096
#undef DICTIONARY_SIZE_512
#undef DICTIONARY_SIZE_64
#undef ENDARC_HEAD
#undef FHD_COMMENT
#undef FHD_EXTFLAGS
#undef FHD_EXTTIME
#undef FHD_LARGE
#undef FHD_PASSWORD
#undef FHD_SALT
#undef FHD_SOLID
#undef FHD_SPLIT_AFTER
#undef FHD_SPLIT_BEFORE
#undef FHD_UNICODE
#undef FHD_VERSION
#undef FILE_ATTRIBUTE_DIRECTORY
#undef FILE_HEAD
#undef FILE_IS_DIRECTORY
#undef HD_ADD_SIZE_PRESENT
#undef HD_MARKDELETION
#undef HUFFMAN_TABLE_SIZE
#undef LENGTHCODE_SIZE
#undef LOWOFFSETCODE_SIZE
#undef MAINCODE_SIZE
#undef MAIN_HEAD
#undef MARK_HEAD
#undef MAX_COMPRESS_DEPTH
#undef MAX_SYMBOLS
#undef MAX_SYMBOL_LENGTH
#undef MHD_AV
#undef MHD_COMMENT
#undef MHD_ENCRYPTVER
#undef MHD_FIRSTVOLUME
#undef MHD_LOCK
#undef MHD_NEWNUMBERING
#undef MHD_PASSWORD
#undef MHD_PROTECT
#undef MHD_SOLID
#undef MHD_VOLUME
#undef NEWSUB_HEAD
#undef NS_UNIT
#undef OFFSETCODE_SIZE
#undef OS_BEOS
#undef OS_MAC_OS
#undef OS_MSDOS
#undef OS_OS2
#undef OS_UNIX
#undef OS_WIN32
#undef PROGRAM_GLOBAL_SIZE
#undef PROGRAM_SYSTEM_GLOBAL_ADDRESS
#undef PROGRAM_SYSTEM_GLOBAL_SIZE
#undef PROGRAM_USER_GLOBAL_ADDRESS
#undef PROGRAM_USER_GLOBAL_SIZE
#undef PROGRAM_WORK_SIZE
#undef PROTECT_HEAD
#undef RAR_SIGNATURE
#undef SIGN_HEAD
#undef SUB_HEAD
#undef UNP_BUFFER_SIZE
#undef VM_MEMORY_MASK
#undef VM_MEMORY_SIZE
#undef minimum
#undef rar_br_bits
#undef rar_br_bits_forced
#undef rar_br_consume
#undef rar_br_consume_unaligned_bits
#undef rar_br_has
#undef rar_br_read_ahead

/* ==== archive_read_support_format_rar5.c ==== */
#define parse_filter rar_amalg_archive_read_support_format_rar5_parse_filter
/*-
* Copyright (c) 2018 Grzegorz Antoniak (http://antoniak.org)
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions
* are met:
* 1. Redistributions of source code must retain the above copyright
*    notice, this list of conditions and the following disclaimer.
* 2. Redistributions in binary form must reproduce the above copyright
*    notice, this list of conditions and the following disclaimer in the
*    documentation and/or other materials provided with the distribution.
*
* THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
* IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
* OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
* IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
* INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
* NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
* DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
* THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
* THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/




#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#include <time.h>
#ifdef HAVE_ZLIB_H
#include <zlib.h> /* crc32 */
#endif
#ifdef HAVE_LIMITS_H
#include <limits.h>
#endif


#ifndef HAVE_ZLIB_H

#endif





/* ==== archive_time_private.h ==== */
/*-
 * Copyright © 2025 ARJANEN Loïc Jean David
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef ARCHIVE_TIME_PRIVATE_H_INCLUDED
#define ARCHIVE_TIME_PRIVATE_H_INCLUDED

#ifndef __LIBARCHIVE_BUILD
#error This header is only to be used internally to libarchive.
#endif
#include <stdint.h>

/* NTFS time to Unix sec/nsec. */
void ntfs_to_unix(uint64_t ntfs, int64_t* secs, uint32_t* nsecs);
/* DOS time to Unix sec. */
int64_t dos_to_unix(uint32_t dos);
/* Unix sec/nsec to NTFS time. */
uint64_t unix_to_ntfs(int64_t secs, uint32_t nsecs);
/* Unix sec to DOS time. */
uint32_t unix_to_dos(int64_t secs);
#if defined(_WIN32) && !defined(__CYGWIN__)
#include <windef.h>
#include <winbase.h>
/* Windows FILETIME to NTFS time. */
uint64_t FILETIME_to_ntfs(const FILETIME* filetime);
#endif
#endif /* ARCHIVE_TIME_PRIVATE_H_INCLUDED */

#ifdef HAVE_BLAKE2_H
#include <blake2.h>
#else
/* ==== archive_blake2.h ==== */
/*
   BLAKE2 reference source code package - reference C implementations

   Copyright 2012, Samuel Neves <sneves@dei.uc.pt>.  You may use this under the
   terms of the CC0, the OpenSSL Licence, or the Apache Public License 2.0, at
   your option.  The terms of these licenses can be found at:

   - CC0 1.0 Universal : http://creativecommons.org/publicdomain/zero/1.0
   - OpenSSL license   : https://www.openssl.org/source/license.html
   - Apache 2.0        : http://www.apache.org/licenses/LICENSE-2.0

   More information about the BLAKE2 hash function can be found at
   https://blake2.net.
*/

#ifndef ARCHIVE_BLAKE2_H
#define ARCHIVE_BLAKE2_H

#include <stddef.h>
#include <stdint.h>

#if defined(_MSC_VER)
#define BLAKE2_PACKED(x) __pragma(pack(push, 1)) x __pragma(pack(pop))
#elif defined(__GNUC__)
#define BLAKE2_PACKED(x) x __attribute__((packed))
#else
/* amalgamated: the non-MSVC, non-GNU fallback spelled itself with
 * _Pragma, which tcc 0.9.27 -- one of the compilers nob.c drives
 * -- does not parse at all. Dropping the packing costs nothing:
 * both blake2 parameter blocks are already naturally packed (every
 * field lands on its own alignment, 32 and 64 bytes exactly), which
 * is why the MSVC and GNU branches above agree with it. */
#define BLAKE2_PACKED(x) x
#endif

#if defined(__cplusplus)
extern "C" {
#endif

  enum blake2s_constant
  {
    BLAKE2S_BLOCKBYTES = 64,
    BLAKE2S_OUTBYTES   = 32,
    BLAKE2S_KEYBYTES   = 32,
    BLAKE2S_SALTBYTES  = 8,
    BLAKE2S_PERSONALBYTES = 8
  };

  enum blake2b_constant
  {
    BLAKE2B_BLOCKBYTES = 128,
    BLAKE2B_OUTBYTES   = 64,
    BLAKE2B_KEYBYTES   = 64,
    BLAKE2B_SALTBYTES  = 16,
    BLAKE2B_PERSONALBYTES = 16
  };

  typedef struct blake2s_state__
  {
    uint32_t h[8];
    uint32_t t[2];
    uint32_t f[2];
    uint8_t  buf[BLAKE2S_BLOCKBYTES];
    size_t   buflen;
    size_t   outlen;
    uint8_t  last_node;
  } blake2s_state;

  typedef struct blake2b_state__
  {
    uint64_t h[8];
    uint64_t t[2];
    uint64_t f[2];
    uint8_t  buf[BLAKE2B_BLOCKBYTES];
    size_t   buflen;
    size_t   outlen;
    uint8_t  last_node;
  } blake2b_state;

  typedef struct blake2sp_state__
  {
    blake2s_state S[8][1];
    blake2s_state R[1];
    uint8_t       buf[8 * BLAKE2S_BLOCKBYTES];
    size_t        buflen;
    size_t        outlen;
  } blake2sp_state;

  typedef struct blake2bp_state__
  {
    blake2b_state S[4][1];
    blake2b_state R[1];
    uint8_t       buf[4 * BLAKE2B_BLOCKBYTES];
    size_t        buflen;
    size_t        outlen;
  } blake2bp_state;

  BLAKE2_PACKED(struct blake2s_param__
  {
    uint8_t  digest_length; /* 1 */
    uint8_t  key_length;    /* 2 */
    uint8_t  fanout;        /* 3 */
    uint8_t  depth;         /* 4 */
    uint32_t leaf_length;   /* 8 */
    uint32_t node_offset;  /* 12 */
    uint16_t xof_length;    /* 14 */
    uint8_t  node_depth;    /* 15 */
    uint8_t  inner_length;  /* 16 */
    /* uint8_t  reserved[0]; */
    uint8_t  salt[BLAKE2S_SALTBYTES]; /* 24 */
    uint8_t  personal[BLAKE2S_PERSONALBYTES];  /* 32 */
  });

  typedef struct blake2s_param__ blake2s_param;

  BLAKE2_PACKED(struct blake2b_param__
  {
    uint8_t  digest_length; /* 1 */
    uint8_t  key_length;    /* 2 */
    uint8_t  fanout;        /* 3 */
    uint8_t  depth;         /* 4 */
    uint32_t leaf_length;   /* 8 */
    uint32_t node_offset;   /* 12 */
    uint32_t xof_length;    /* 16 */
    uint8_t  node_depth;    /* 17 */
    uint8_t  inner_length;  /* 18 */
    uint8_t  reserved[14];  /* 32 */
    uint8_t  salt[BLAKE2B_SALTBYTES]; /* 48 */
    uint8_t  personal[BLAKE2B_PERSONALBYTES];  /* 64 */
  });

  typedef struct blake2b_param__ blake2b_param;

  typedef struct blake2xs_state__
  {
    blake2s_state S[1];
    blake2s_param P[1];
  } blake2xs_state;

  typedef struct blake2xb_state__
  {
    blake2b_state S[1];
    blake2b_param P[1];
  } blake2xb_state;

  /* Padded structs result in a compile-time error */
  enum {
    BLAKE2_DUMMY_1 = 1/(sizeof(blake2s_param) == BLAKE2S_OUTBYTES),
    BLAKE2_DUMMY_2 = 1/(sizeof(blake2b_param) == BLAKE2B_OUTBYTES)
  };

  /* Streaming API */
  int blake2s_init( blake2s_state *S, size_t outlen );
  int blake2s_init_key( blake2s_state *S, size_t outlen, const void *key, size_t keylen );
  int blake2s_init_param( blake2s_state *S, const blake2s_param *P );
  int blake2s_update( blake2s_state *S, const void *in, size_t inlen );
  int blake2s_final( blake2s_state *S, void *out, size_t outlen );

  int blake2b_init( blake2b_state *S, size_t outlen );
  int blake2b_init_key( blake2b_state *S, size_t outlen, const void *key, size_t keylen );
  int blake2b_init_param( blake2b_state *S, const blake2b_param *P );
  int blake2b_update( blake2b_state *S, const void *in, size_t inlen );
  int blake2b_final( blake2b_state *S, void *out, size_t outlen );

  int blake2sp_init( blake2sp_state *S, size_t outlen );
  int blake2sp_init_key( blake2sp_state *S, size_t outlen, const void *key, size_t keylen );
  int blake2sp_update( blake2sp_state *S, const void *in, size_t inlen );
  int blake2sp_final( blake2sp_state *S, void *out, size_t outlen );

  int blake2bp_init( blake2bp_state *S, size_t outlen );
  int blake2bp_init_key( blake2bp_state *S, size_t outlen, const void *key, size_t keylen );
  int blake2bp_update( blake2bp_state *S, const void *in, size_t inlen );
  int blake2bp_final( blake2bp_state *S, void *out, size_t outlen );

  /* Variable output length API */
  int blake2xs_init( blake2xs_state *S, const size_t outlen );
  int blake2xs_init_key( blake2xs_state *S, const size_t outlen, const void *key, size_t keylen );
  int blake2xs_update( blake2xs_state *S, const void *in, size_t inlen );
  int blake2xs_final(blake2xs_state *S, void *out, size_t outlen);

  int blake2xb_init( blake2xb_state *S, const size_t outlen );
  int blake2xb_init_key( blake2xb_state *S, const size_t outlen, const void *key, size_t keylen );
  int blake2xb_update( blake2xb_state *S, const void *in, size_t inlen );
  int blake2xb_final(blake2xb_state *S, void *out, size_t outlen);

  /* Simple API */
  int blake2s( void *out, size_t outlen, const void *in, size_t inlen, const void *key, size_t keylen );
  int blake2b( void *out, size_t outlen, const void *in, size_t inlen, const void *key, size_t keylen );

  int blake2sp( void *out, size_t outlen, const void *in, size_t inlen, const void *key, size_t keylen );
  int blake2bp( void *out, size_t outlen, const void *in, size_t inlen, const void *key, size_t keylen );

  int blake2xs( void *out, size_t outlen, const void *in, size_t inlen, const void *key, size_t keylen );
  int blake2xb( void *out, size_t outlen, const void *in, size_t inlen, const void *key, size_t keylen );

  /* This is simply an alias for blake2b */
  int blake2( void *out, size_t outlen, const void *in, size_t inlen, const void *key, size_t keylen );

#if defined(__cplusplus)
}
#endif

#endif
#endif

/*#define CHECK_CRC_ON_SOLID_SKIP*/
/*#define DONT_FAIL_ON_CRC_ERROR*/
/*#define DEBUG*/

#define rar5_min(a, b) (((a) > (b)) ? (b) : (a))
#define rar5_max(a, b) (((a) > (b)) ? (a) : (b))
#define rar5_countof(X) ((const ssize_t) (sizeof(X) / sizeof(*X)))

#if defined DEBUG
#define DEBUG_CODE if(1)
#define LOG(...) do { printf("rar5: " __VA_ARGS__); puts(""); } while(0)
#else
#define DEBUG_CODE if(0)
#endif

/* Real RAR5 magic number is:
 *
 * 0x52, 0x61, 0x72, 0x21, 0x1a, 0x07, 0x01, 0x00
 * "Rar!→•☺·\x00"
 *
 * Retrieved with `rar5_signature()` by XOR'ing it with 0xA1, because I don't
 * want to put this magic sequence in each binary that uses libarchive, so
 * applications that scan through the file for this marker won't trigger on
 * this "false" one.
 *
 * The array itself is decrypted in `rar5_init` function. */

static unsigned char rar5_signature_xor[] = { 243, 192, 211, 128, 187, 166, 160, 161 };
static const size_t g_unpack_window_size = 0x20000;

/* These could have been static const's, but they aren't, because of
 * Visual Studio. */
#define MAX_NAME_IN_CHARS 2048
#define MAX_NAME_IN_BYTES (4 * MAX_NAME_IN_CHARS)

struct file_header {
	ssize_t bytes_remaining;
	ssize_t unpacked_size;
	int64_t last_offset;         /* Used in sanity checks. */
	int64_t last_size;           /* Used in sanity checks. */

	uint8_t solid : 1;           /* Is this a solid stream? */
	uint8_t service : 1;         /* Is this file a service data? */
	uint8_t eof : 1;             /* Did we finish unpacking the file? */
	uint8_t dir : 1;             /* Is this file entry a directory? */

	/* Optional time fields. */
	int64_t e_mtime;
	int64_t e_ctime;
	int64_t e_atime;
	uint32_t e_mtime_ns;
	uint32_t e_ctime_ns;
	uint32_t e_atime_ns;

	/* Optional hash fields. */
	uint32_t stored_crc32;
	uint32_t calculated_crc32;
	uint8_t blake2sp[32];
	blake2sp_state b2state;
	char has_blake2;

	/* Optional redir fields */
	uint64_t redir_type;
	uint64_t redir_flags;

	ssize_t solid_window_size; /* Used in file format check. */
};

enum EXTRA {
	EX_CRYPT = 0x01,
	EX_HASH = 0x02,
	EX_HTIME = 0x03,
	EX_VERSION = 0x04,
	EX_REDIR = 0x05,
	EX_UOWNER = 0x06,
	EX_SUBDATA = 0x07
};

#define REDIR_SYMLINK_IS_DIR	1

enum REDIR_TYPE {
	REDIR_TYPE_NONE = 0,
	REDIR_TYPE_UNIXSYMLINK = 1,
	REDIR_TYPE_WINSYMLINK = 2,
	REDIR_TYPE_JUNCTION = 3,
	REDIR_TYPE_HARDLINK = 4,
	REDIR_TYPE_FILECOPY = 5,
};

#define	OWNER_USER_NAME		0x01
#define	OWNER_GROUP_NAME	0x02
#define	OWNER_USER_UID		0x04
#define	OWNER_GROUP_GID		0x08
#define	OWNER_MAXNAMELEN	256

enum FILTER_TYPE {
	FILTER_DELTA = 0,   /* Generic pattern. */
	FILTER_E8    = 1,   /* Intel x86 code. */
	FILTER_E8E9  = 2,   /* Intel x86 code. */
	FILTER_ARM   = 3,   /* ARM code. */
	FILTER_AUDIO = 4,   /* Audio filter, not used in RARv5. */
	FILTER_RGB   = 5,   /* Color palette, not used in RARv5. */
	FILTER_ITANIUM = 6, /* Intel's Itanium, not used in RARv5. */
	FILTER_PPM   = 7,   /* Predictive pattern matching, not used in
			       RARv5. */
	FILTER_NONE  = 8,
};

struct filter_info {
	int type;
	int channels;
	int pos_r;

	int64_t block_start;
	ssize_t block_length;
	uint16_t width;
};

struct data_ready {
	char used;
	const uint8_t* buf;
	size_t size;
	int64_t offset;
};

struct cdeque {
	uint16_t beg_pos;
	uint16_t end_pos;
	uint16_t cap_mask;
	uint16_t size;
	size_t* arr;
};

struct decode_table {
	uint32_t size;
	int32_t decode_len[16];
	uint32_t decode_pos[16];
	uint32_t quick_bits;
	uint8_t quick_len[1 << 10];
	uint16_t quick_num[1 << 10];
	uint16_t decode_num[306];
};

struct comp_state {
	/* Flag used to specify if unpacker needs to reinitialize the
	   uncompression context. */
	uint8_t initialized : 1;

	/* Flag used when applying filters. */
	uint8_t all_filters_applied : 1;

	/* Flag used to skip file context reinitialization, used when unpacker
	   is skipping through different multivolume archives. */
	uint8_t switch_multivolume : 1;

	/* Flag used to specify if unpacker has processed the whole data block
	   or just a part of it. */
	uint8_t block_parsing_finished : 1;

	/* Flag used to indicate that a previous file using this buffer was
	   encrypted, meaning no data in the buffer can be trusted */
	uint8_t data_encrypted : 1;

	signed int notused : 3;

	int flags;                   /* Uncompression flags. */
	int method;                  /* Uncompression algorithm method. */
	int version;                 /* Uncompression algorithm version. */
	ssize_t window_size;         /* Size of window_buf. */
	uint8_t* window_buf;         /* Circular buffer used during
	                                decompression. */
	uint8_t* filtered_buf;       /* Buffer used when applying filters. */
	const uint8_t* block_buf;    /* Buffer used when merging blocks. */
	ssize_t window_mask;         /* Convenience field; window_size - 1. */
	int64_t write_ptr;           /* This amount of data has been unpacked
					in the window buffer. */
	int64_t last_write_ptr;      /* This amount of data has been stored in
	                                the output file. */
	int64_t last_unstore_ptr;    /* Counter of bytes extracted during
	                                unstoring. This is separate from
	                                last_write_ptr because of how SERVICE
	                                base blocks are handled during skipping
	                                in solid multiarchive archives. */
	int64_t solid_offset;        /* Additional offset inside the window
	                                buffer, used in unpacking solid
	                                archives. */
	ssize_t cur_block_size;      /* Size of current data block. */
	int last_len;                /* Flag used in lzss decompression. */

	/* Decode tables used during lzss uncompression. */

#define HUFF_BC 20
	struct decode_table bd;      /* huffman bit lengths */
#define HUFF_NC 306
	struct decode_table ld;      /* literals */
#define HUFF_DC 64
	struct decode_table dd;      /* distances */
#define HUFF_LDC 16
	struct decode_table ldd;     /* lower bits of distances */
#define HUFF_RC 44
	struct decode_table rd;      /* repeating distances */
#define HUFF_TABLE_SIZE (HUFF_NC + HUFF_DC + HUFF_RC + HUFF_LDC)

	/* Circular deque for storing filters. */
	struct cdeque filters;
	int64_t last_block_start;    /* Used for sanity checking. */
	ssize_t last_block_length;   /* Used for sanity checking. */

	/* Distance cache used during lzss uncompression. */
	int dist_cache[4];

	/* Data buffer stack. */
	struct data_ready dready[2];
};

/* Bit reader state. */
struct bit_reader {
	int8_t bit_addr;    /* Current bit pointer inside current byte. */
	int in_addr;        /* Current byte pointer. */
};

/* RARv5 block header structure. Use bf_* functions to get values from
 * block_flags_u8 field. I.e. bf_byte_count, etc. */
struct compressed_block_header {
	/* block_flags_u8 contain fields encoded in little-endian bitfield:
	 *
	 * - table present flag (shr 7, and 1),
	 * - last block flag    (shr 6, and 1),
	 * - byte_count         (shr 3, and 7),
	 * - bit_size           (shr 0, and 7).
	 */
	uint8_t block_flags_u8;
	uint8_t block_cksum;
};

/* RARv5 main header structure. */
struct main_header {
	/* Does the archive contain solid streams? */
	uint8_t solid : 1;

	/* If this a multi-file archive? */
	uint8_t volume : 1;
	uint8_t endarc : 1;
	uint8_t notused : 5;

	unsigned int vol_no;
};

struct generic_header {
	uint8_t split_after : 1;
	uint8_t split_before : 1;
	uint8_t padding : 6;
	int size;
	int last_header_id;
};

struct multivolume {
	unsigned int expected_vol_no;
	uint8_t* push_buf;
};

/* Main context structure. */
struct rar5 {
	int header_initialized;

	/* Set to 1 if current file is positioned AFTER the magic value
	 * of the archive file. This is used in header reading functions. */
	int skipped_magic;

	/* Set to not zero if we're in skip mode (either by calling
	 * rar5_data_skip function or when skipping over solid streams).
	 * Set to 0 when in * extraction mode. This is used during checksum
	 * calculation functions. */
	int skip_mode;

	/* Set to not zero if we're in block merging mode (i.e. when switching
	 * to another file in multivolume archive, last block from 1st archive
	 * needs to be merged with 1st block from 2nd archive). This flag
	 * guards against recursive use of the merging function, which doesn't
	 * support recursive calls. */
	int merge_mode;

	/* An offset to QuickOpen list. This is not supported by this unpacker,
	 * because we're focusing on streaming interface. QuickOpen is designed
	 * to make things quicker for non-stream interfaces, so it's not our
	 * use case. */
	uint64_t qlist_offset;

	/* An offset to additional Recovery data. This is not supported by this
	 * unpacker. Recovery data are additional Reed-Solomon codes that could
	 * be used to calculate bytes that are missing in archive or are
	 * corrupted. */
	uint64_t rr_offset;

	/* Various context variables grouped to different structures. */
	struct generic_header generic;
	struct main_header main;
	struct comp_state cstate;
	struct file_header file;
	struct bit_reader bits;
	struct multivolume vol;

	/* The header of currently processed RARv5 block. Used in main
	 * decompression logic loop. */
	struct compressed_block_header last_block_hdr;

	/*
	 * Custom field to denote that this archive contains encrypted entries
	 */
	int has_encrypted_entries;
	int headers_are_encrypted;
};

/* Forward function declarations. */

static void rar5_signature(char *buf);
static int verify_global_checksums(struct archive_read* a);
static int rar5_read_data_skip(struct archive_read *a);
static int push_data_ready(struct archive_read* a, struct rar5* rar,
	const uint8_t* buf, size_t size, int64_t offset);
static void clear_data_ready_stack(struct rar5* rar);

/* CDE_xxx = Circular Double Ended (Queue) return values. */
enum CDE_RETURN_VALUES {
	CDE_OK, CDE_ALLOC, CDE_PARAM, CDE_OUT_OF_BOUNDS,
};

/* Clears the contents of this circular deque. */
static void cdeque_clear(struct cdeque* d) {
	d->size = 0;
	d->beg_pos = 0;
	d->end_pos = 0;
}

/* Creates a new circular deque object. Capacity must be power of 2: 8, 16, 32,
 * 64, 256, etc. When the user will add another item above current capacity,
 * the circular deque will overwrite the oldest entry. */
static int cdeque_init(struct cdeque* d, int max_capacity_power_of_2) {
	if(d == NULL || max_capacity_power_of_2 == 0)
		return CDE_PARAM;

	d->cap_mask = max_capacity_power_of_2 - 1;
	d->arr = NULL;

	if((max_capacity_power_of_2 & d->cap_mask) != 0)
		return CDE_PARAM;

	cdeque_clear(d);
	d->arr = malloc(sizeof(void*) * max_capacity_power_of_2);

	return d->arr ? CDE_OK : CDE_ALLOC;
}

/* Return the current size (not capacity) of circular deque `d`. */
static size_t cdeque_size(struct cdeque* d) {
	return d->size;
}

/* Returns the first element of current circular deque. Note that this function
 * doesn't perform any bounds checking. If you need bounds checking, use
 * `cdeque_front()` function instead. */
static void cdeque_front_fast(struct cdeque* d, void** value) {
	*value = (void*) d->arr[d->beg_pos];
}

/* Returns the first element of current circular deque. This function
 * performs bounds checking. */
static int cdeque_front(struct cdeque* d, void** value) {
	if(d->size > 0) {
		cdeque_front_fast(d, value);
		return CDE_OK;
	} else
		return CDE_OUT_OF_BOUNDS;
}

/* Pushes a new element into the end of this circular deque object. If current
 * size will exceed capacity, the oldest element will be overwritten. */
static int cdeque_push_back(struct cdeque* d, void* item) {
	if(d == NULL)
		return CDE_PARAM;

	if(d->size == d->cap_mask + 1)
		return CDE_OUT_OF_BOUNDS;

	d->arr[d->end_pos] = (size_t) item;
	d->end_pos = (d->end_pos + 1) & d->cap_mask;
	d->size++;

	return CDE_OK;
}

/* Pops a front element of this circular deque object and returns its value.
 * This function doesn't perform any bounds checking. */
static void cdeque_pop_front_fast(struct cdeque* d, void** value) {
	*value = (void*) d->arr[d->beg_pos];
	d->beg_pos = (d->beg_pos + 1) & d->cap_mask;
	d->size--;
}

/* Pops a front element of this circular deque object and returns its value.
 * This function performs bounds checking. */
static int cdeque_pop_front(struct cdeque* d, void** value) {
	if(!d || !value)
		return CDE_PARAM;

	if(d->size == 0)
		return CDE_OUT_OF_BOUNDS;

	cdeque_pop_front_fast(d, value);
	return CDE_OK;
}

/* Convenience function to cast filter_info** to void **. */
static void** cdeque_filter_p(struct filter_info** f) {
	return (void**) (size_t) f;
}

/* Convenience function to cast filter_info* to void *. */
static void* cdeque_filter(struct filter_info* f) {
	return (void**) (size_t) f;
}

/* Destroys this circular deque object. Deallocates the memory of the
 * collection buffer, but doesn't deallocate the memory of any pointer passed
 * to this deque as a value. */
static void cdeque_free(struct cdeque* d) {
	if(!d)
		return;

	if(!d->arr)
		return;

	free(d->arr);

	d->arr = NULL;
	d->beg_pos = -1;
	d->end_pos = -1;
	d->cap_mask = 0;
}

static
uint8_t bf_bit_size(const struct compressed_block_header* hdr) {
	return hdr->block_flags_u8 & 7;
}

static
uint8_t bf_byte_count(const struct compressed_block_header* hdr) {
	return (hdr->block_flags_u8 >> 3) & 7;
}

static
uint8_t bf_is_table_present(const struct compressed_block_header* hdr) {
	return (hdr->block_flags_u8 >> 7) & 1;
}

static
uint8_t bf_is_last_block(const struct compressed_block_header* hdr) {
	return (hdr->block_flags_u8 >> 6) & 1;
}

static struct rar5* get_context(struct archive_read* a) {
	return (struct rar5*) a->format->data;
}

/* Convenience functions used by filter implementations. */
static void circular_memcpy(uint8_t* dst, uint8_t* window, const ssize_t mask,
    int64_t start, int64_t end)
{
	if((start & mask) > (end & mask)) {
		ssize_t len1 = mask + 1 - (start & mask);
		ssize_t len2 = end & mask;

		memcpy(dst, &window[start & mask], len1);
		memcpy(dst + len1, window, len2);
	} else {
		memcpy(dst, &window[start & mask], (size_t) (end - start));
	}
}

static uint32_t read_filter_data(struct rar5* rar, uint32_t offset) {
	uint8_t linear_buf[4];
	circular_memcpy(linear_buf, rar->cstate.window_buf,
	    rar->cstate.window_mask, offset, offset + 4);
	return archive_le32dec(linear_buf);
}

static void write_filter_data(struct rar5* rar, uint32_t offset,
    uint32_t value)
{
	archive_le32enc(&rar->cstate.filtered_buf[offset], value);
}

/* Allocates a new filter descriptor and adds it to the filter array. */
static struct filter_info* add_new_filter(struct rar5* rar) {
	struct filter_info* f = calloc(1, sizeof(*f));

	if(!f) {
		return NULL;
	}

	cdeque_push_back(&rar->cstate.filters, cdeque_filter(f));
	return f;
}

static int run_delta_filter(struct rar5* rar, struct filter_info* flt) {
	int i;
	ssize_t dest_pos, src_pos = 0;

	for(i = 0; i < flt->channels; i++) {
		uint8_t prev_byte = 0;
		for(dest_pos = i;
				dest_pos < flt->block_length;
				dest_pos += flt->channels)
		{
			uint8_t byte;

			byte = rar->cstate.window_buf[
			    (rar->cstate.solid_offset + flt->block_start +
			    src_pos) & rar->cstate.window_mask];

			prev_byte -= byte;
			rar->cstate.filtered_buf[dest_pos] = prev_byte;
			src_pos++;
		}
	}

	return ARCHIVE_OK;
}

static int run_e8e9_filter(struct rar5* rar, struct filter_info* flt,
		int extended)
{
	const uint32_t file_size = 0x1000000;
	ssize_t i;

	circular_memcpy(rar->cstate.filtered_buf,
	    rar->cstate.window_buf, rar->cstate.window_mask,
	    rar->cstate.solid_offset + flt->block_start,
	    rar->cstate.solid_offset + flt->block_start + flt->block_length);

	for(i = 0; i < flt->block_length - 4;) {
		uint8_t b = rar->cstate.window_buf[
		    (rar->cstate.solid_offset + flt->block_start +
		    i++) & rar->cstate.window_mask];

		/*
		 * 0xE8 = x86's call <relative_addr_uint32> (function call)
		 * 0xE9 = x86's jmp <relative_addr_uint32> (unconditional jump)
		 */
		if(b == 0xE8 || (extended && b == 0xE9)) {

			uint32_t addr;
			uint32_t offset = (i + flt->block_start) % file_size;

			addr = read_filter_data(rar,
			    (uint32_t)(rar->cstate.solid_offset +
			    flt->block_start + i) & rar->cstate.window_mask);

			if(addr & 0x80000000) {
				if(((addr + offset) & 0x80000000) == 0) {
					write_filter_data(rar, (uint32_t)i,
					    addr + file_size);
				}
			} else {
				if((addr - file_size) & 0x80000000) {
					uint32_t naddr = addr - offset;
					write_filter_data(rar, (uint32_t)i,
					    naddr);
				}
			}

			i += 4;
		}
	}

	return ARCHIVE_OK;
}

static int run_arm_filter(struct rar5* rar, struct filter_info* flt) {
	ssize_t i = 0;
	uint32_t offset;

	circular_memcpy(rar->cstate.filtered_buf,
	    rar->cstate.window_buf, rar->cstate.window_mask,
	    rar->cstate.solid_offset + flt->block_start,
	    rar->cstate.solid_offset + flt->block_start + flt->block_length);

	for(i = 0; i < flt->block_length - 3; i += 4) {
		uint8_t* b = &rar->cstate.window_buf[
		    (rar->cstate.solid_offset +
		    flt->block_start + i + 3) & rar->cstate.window_mask];

		if(*b == 0xEB) {
			/* 0xEB = ARM's BL (branch + link) instruction. */
			offset = read_filter_data(rar,
			    (rar->cstate.solid_offset + flt->block_start + i) &
			     (uint32_t)rar->cstate.window_mask) & 0x00ffffff;

			offset -= (uint32_t) ((i + flt->block_start) / 4);
			offset = (offset & 0x00ffffff) | 0xeb000000;
			write_filter_data(rar, (uint32_t)i, offset);
		}
	}

	return ARCHIVE_OK;
}

static int run_filter(struct archive_read* a, struct filter_info* flt) {
	int ret;
	struct rar5* rar = get_context(a);

	clear_data_ready_stack(rar);
	free(rar->cstate.filtered_buf);

	rar->cstate.filtered_buf = malloc(flt->block_length);
	if(!rar->cstate.filtered_buf) {
		archive_set_error(&a->archive, ENOMEM,
		    "Can't allocate memory for filter data.");
		return ARCHIVE_FATAL;
	}

	switch(flt->type) {
		case FILTER_DELTA:
			ret = run_delta_filter(rar, flt);
			break;

		case FILTER_E8:
			/* fallthrough */
		case FILTER_E8E9:
			ret = run_e8e9_filter(rar, flt,
			    flt->type == FILTER_E8E9);
			break;

		case FILTER_ARM:
			ret = run_arm_filter(rar, flt);
			break;

		default:
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_FILE_FORMAT,
			    "Unsupported filter type: 0x%x",
			    (unsigned int)flt->type);
			return ARCHIVE_FATAL;
	}

	if(ret != ARCHIVE_OK) {
		/* Filter has failed. */
		return ret;
	}

	if(ARCHIVE_OK != push_data_ready(a, rar, rar->cstate.filtered_buf,
	    flt->block_length, rar->cstate.last_write_ptr))
	{
		archive_set_error(&a->archive, ARCHIVE_ERRNO_PROGRAMMER,
		    "Stack overflow when submitting unpacked data");

		return ARCHIVE_FATAL;
	}

	rar->cstate.last_write_ptr += flt->block_length;
	return ARCHIVE_OK;
}

/* The `push_data` function submits the selected data range to the user.
 * Next call of `use_data` will use the pointer, size and offset arguments
 * that are specified here. These arguments are pushed to the FIFO stack here,
 * and popped from the stack by the `use_data` function. */
static void push_data(struct archive_read* a, struct rar5* rar,
    const uint8_t* buf, int64_t idx_begin, int64_t idx_end)
{
	const ssize_t wmask = rar->cstate.window_mask;
	const ssize_t solid_write_ptr = (rar->cstate.solid_offset +
	    rar->cstate.last_write_ptr) & wmask;

	idx_begin += rar->cstate.solid_offset;
	idx_end += rar->cstate.solid_offset;

	/* Check if our unpacked data is wrapped inside the window circular
	 * buffer.  If it's not wrapped, it can be copied out by using
	 * a single memcpy, but when it's wrapped, we need to copy the first
	 * part with one memcpy, and the second part with another memcpy. */

	if((idx_begin & wmask) > (idx_end & wmask)) {
		/* The data is wrapped (begin offset sis bigger than end
		 * offset). */
		const ssize_t frag1_size = rar->cstate.window_size -
		    (idx_begin & wmask);
		const ssize_t frag2_size = idx_end & wmask;

		/* Copy the first part of the buffer first. */
		push_data_ready(a, rar, buf + solid_write_ptr, frag1_size,
		    rar->cstate.last_write_ptr);

		/* Copy the second part of the buffer. */
		push_data_ready(a, rar, buf, frag2_size,
		    rar->cstate.last_write_ptr + frag1_size);

		rar->cstate.last_write_ptr += frag1_size + frag2_size;
	} else {
		/* Data is not wrapped, so we can just use one call to copy the
		 * data. */
		push_data_ready(a, rar,
		    buf + solid_write_ptr, (idx_end - idx_begin) & wmask,
		    rar->cstate.last_write_ptr);

		rar->cstate.last_write_ptr += idx_end - idx_begin;
	}
}

/* Convenience function that submits the data to the user. It uses the
 * unpack window buffer as a source location. */
static void push_window_data(struct archive_read* a, struct rar5* rar,
    int64_t idx_begin, int64_t idx_end)
{
	push_data(a, rar, rar->cstate.window_buf, idx_begin, idx_end);
}

static int apply_filters(struct archive_read* a) {
	struct filter_info* flt;
	struct rar5* rar = get_context(a);
	int ret;

	rar->cstate.all_filters_applied = 0;

	/* Get the first filter that can be applied to our data. The data
	 * needs to be fully unpacked before the filter can be run. */
	if(CDE_OK == cdeque_front(&rar->cstate.filters,
	    cdeque_filter_p(&flt))) {
		/* Check if our unpacked data fully covers this filter's
		 * range. */
		if(rar->cstate.write_ptr > flt->block_start &&
		    rar->cstate.write_ptr >= flt->block_start +
		    flt->block_length) {
			/* Check if we have some data pending to be written
			 * right before the filter's start offset. */
			if(rar->cstate.last_write_ptr == flt->block_start) {
				/* Run the filter specified by descriptor
				 * `flt`. */
				ret = run_filter(a, flt);
				if(ret != ARCHIVE_OK) {
					/* Filter failure, return error. */
					return ret;
				}

				/* Filter descriptor won't be needed anymore
				 * after it's used, * so remove it from the
				 * filter list and free its memory. */
				(void) cdeque_pop_front(&rar->cstate.filters,
				    cdeque_filter_p(&flt));

				free(flt);
			} else {
				/* We can't run filters yet, dump the memory
				 * right before the filter. */
				push_window_data(a, rar,
				    rar->cstate.last_write_ptr,
				    flt->block_start);
			}

			/* Return 'filter applied or not needed' state to the
			 * caller. */
			return ARCHIVE_RETRY;
		}
	}

	rar->cstate.all_filters_applied = 1;
	return ARCHIVE_OK;
}

static void dist_cache_push(struct rar5* rar, int value) {
	int* q = rar->cstate.dist_cache;

	q[3] = q[2];
	q[2] = q[1];
	q[1] = q[0];
	q[0] = value;
}

static int dist_cache_touch(struct rar5* rar, int idx) {
	int* q = rar->cstate.dist_cache;
	int i, dist = q[idx];

	for(i = idx; i > 0; i--)
		q[i] = q[i - 1];

	q[0] = dist;
	return dist;
}

static void free_filters(struct rar5* rar) {
	struct cdeque* d = &rar->cstate.filters;

	/* Free any remaining filters. All filters should be naturally
	 * consumed by the unpacking function, so remaining filters after
	 * unpacking normally mean that unpacking wasn't successful.
	 * But still of course we shouldn't leak memory in such case. */

	/* cdeque_size() is a fast operation, so we can use it as a loop
	 * expression. */
	while(cdeque_size(d) > 0) {
		struct filter_info* f = NULL;

		/* Pop_front will also decrease the collection's size. */
		if (CDE_OK == cdeque_pop_front(d, cdeque_filter_p(&f)))
			free(f);
	}

	cdeque_clear(d);

	/* Also clear out the variables needed for sanity checking. */
	rar->cstate.last_block_start = 0;
	rar->cstate.last_block_length = 0;
}

static void reset_file_context(struct rar5* rar) {
	memset(&rar->file, 0, sizeof(rar->file));
	blake2sp_init(&rar->file.b2state, 32);

	if(rar->main.solid) {
		rar->cstate.solid_offset += rar->cstate.write_ptr;
	} else {
		rar->cstate.solid_offset = 0;
	}

	rar->cstate.write_ptr = 0;
	rar->cstate.last_write_ptr = 0;
	rar->cstate.last_unstore_ptr = 0;

	rar->file.redir_type = REDIR_TYPE_NONE;
	rar->file.redir_flags = 0;

	free_filters(rar);
}

static int get_archive_read(struct archive* a,
    struct archive_read** ar)
{
	*ar = (struct archive_read*) a;
	archive_check_magic(a, ARCHIVE_READ_MAGIC, ARCHIVE_STATE_NEW,
	    "archive_read_support_format_rar5");

	return ARCHIVE_OK;
}

static int read_ahead(struct archive_read* a, size_t how_many,
    const uint8_t** ptr)
{
	ssize_t avail = -1;
	if(!ptr)
		return 0;

	*ptr = __archive_read_ahead(a, how_many, &avail);
	if(*ptr == NULL) {
		return 0;
	}

	return 1;
}

static int consume(struct archive_read* a, int64_t how_many) {
	int ret;

	ret = how_many == __archive_read_consume(a, how_many)
		? ARCHIVE_OK
		: ARCHIVE_FATAL;

	return ret;
}

/**
 * Read a RAR5 variable sized numeric value. This value will be stored in
 * `pvalue`. The `pvalue_len` argument points to a variable that will receive
 * the byte count that was consumed in order to decode the `pvalue` value, plus
 * one.
 *
 * pvalue_len is optional and can be NULL.
 *
 * NOTE: if `pvalue_len` is NOT NULL, the caller needs to manually consume
 * the number of bytes that `pvalue_len` value contains. If the `pvalue_len`
 * is NULL, this consuming operation is done automatically.
 *
 * Returns 1 if *pvalue was successfully read.
 * Returns 0 if there was an error. In this case, *pvalue contains an
 *           invalid value.
 */

static int read_var(struct archive_read* a, uint64_t* pvalue,
    uint64_t* pvalue_len)
{
	uint64_t result = 0;
	size_t shift, i;
	const uint8_t* p;
	uint8_t b;

	/* We will read maximum of 8 bytes. We don't have to handle the
	 * situation to read the RAR5 variable-sized value stored at the end of
	 * the file, because such situation will never happen. */
	if(!read_ahead(a, 8, &p))
		return 0;

	for(shift = 0, i = 0; i < 8; i++, shift += 7) {
		b = p[i];

		/* Strip the MSB from the input byte and add the resulting
		 * number to the `result`. */
		result += (b & (uint64_t)0x7F) << shift;

		/* MSB set to 1 means we need to continue decoding process.
		 * MSB set to 0 means we're done.
		 *
		 * This conditional checks for the second case. */
		if((b & 0x80) == 0) {
			if(pvalue) {
				*pvalue = result;
			}

			/* If the caller has passed the `pvalue_len` pointer,
			 * store the number of consumed bytes in it and do NOT
			 * consume those bytes, since the caller has all the
			 * information it needs to perform */
			if(pvalue_len) {
				*pvalue_len = 1 + i;
			} else {
				/* If the caller did not provide the
				 * `pvalue_len` pointer, it will not have the
				 * possibility to advance the file pointer,
				 * because it will not know how many bytes it
				 * needs to consume. This is why we handle
				 * such situation here automatically. */
				if(ARCHIVE_OK != consume(a, 1 + i)) {
					return 0;
				}
			}

			/* End of decoding process, return success. */
			return 1;
		}
	}

	/* The decoded value takes the maximum number of 8 bytes.
	 * It's a maximum number of bytes, so end decoding process here
	 * even if the first bit of last byte is 1. */
	if(pvalue) {
		*pvalue = result;
	}

	if(pvalue_len) {
		*pvalue_len = 9;
	} else {
		if(ARCHIVE_OK != consume(a, 9)) {
			return 0;
		}
	}

	return 1;
}

static int read_var_sized(struct archive_read* a, size_t* pvalue,
    size_t* pvalue_len)
{
	uint64_t v;
	uint64_t v_size = 0;

	const int ret = pvalue_len ? read_var(a, &v, &v_size)
				   : read_var(a, &v, NULL);

	if(ret == 1 && pvalue) {
		*pvalue = (size_t) v;
	}

	if(pvalue_len) {
		/* Possible data truncation should be safe. */
		*pvalue_len = (size_t) v_size;
	}

	return ret;
}

static int read_bits_32(struct archive_read* a, struct rar5* rar,
	const uint8_t* p, uint32_t* value)
{
	if(rar->bits.in_addr >= rar->cstate.cur_block_size) {
		archive_set_error(&a->archive,
			ARCHIVE_ERRNO_PROGRAMMER,
			"Premature end of stream during extraction of data (#1)");
		return ARCHIVE_FATAL;
	}

	uint32_t bits = ((uint32_t) p[rar->bits.in_addr]) << 24;
	bits |= p[rar->bits.in_addr + 1] << 16;
	bits |= p[rar->bits.in_addr + 2] << 8;
	bits |= p[rar->bits.in_addr + 3];
	bits <<= rar->bits.bit_addr;
	bits |= p[rar->bits.in_addr + 4] >> (8 - rar->bits.bit_addr);
	*value = bits;
	return ARCHIVE_OK;
}

static int read_bits_16(struct archive_read* a, struct rar5* rar,
	const uint8_t* p, uint16_t* value)
{
	if(rar->bits.in_addr >= rar->cstate.cur_block_size) {
		archive_set_error(&a->archive,
			ARCHIVE_ERRNO_PROGRAMMER,
			"Premature end of stream during extraction of data (#2)");
		return ARCHIVE_FATAL;
	}

	int bits = (int) ((uint32_t) p[rar->bits.in_addr]) << 16;
	bits |= (int) p[rar->bits.in_addr + 1] << 8;
	bits |= (int) p[rar->bits.in_addr + 2];
	bits >>= (8 - rar->bits.bit_addr);
	*value = bits & 0xffff;
	return ARCHIVE_OK;
}

static void skip_bits(struct rar5* rar, int bits) {
	const int new_bits = rar->bits.bit_addr + bits;
	rar->bits.in_addr += new_bits >> 3;
	rar->bits.bit_addr = new_bits & 7;
}

/* n = up to 16 */
static int read_consume_bits(struct archive_read* a, struct rar5* rar,
	const uint8_t* p, int n, int* value)
{
	uint16_t v;
	int ret, num;

	if(n == 0 || n > 16) {
		/* This is a programmer error and should never happen
		 * in runtime. */
		return ARCHIVE_FATAL;
	}

	ret = read_bits_16(a, rar, p, &v);
	if(ret != ARCHIVE_OK)
		return ret;

	num = (int) v;
	num >>= 16 - n;

	skip_bits(rar, n);

	if(value)
		*value = num;

	return ARCHIVE_OK;
}

static char read_u32(struct archive_read* a, uint32_t* pvalue) {
	const uint8_t* p;
	if(!read_ahead(a, 4, &p))
		return 0;

	*pvalue = archive_le32dec(p);
	return ARCHIVE_OK == consume(a, 4);
}

static char read_u64(struct archive_read* a, uint64_t* pvalue) {
	const uint8_t* p;
	if(!read_ahead(a, 8, &p))
		return 0;

	*pvalue = archive_le64dec(p);
	return ARCHIVE_OK == consume(a, 8);
}

static int bid_standard(struct archive_read* a) {
	const uint8_t* p;
	char signature[sizeof(rar5_signature_xor)];

	rar5_signature(signature);

	if(!read_ahead(a, sizeof(rar5_signature_xor), &p))
		return -1;

	if(!memcmp(signature, p, sizeof(rar5_signature_xor)))
		return 30;

	return -1;
}

static int bid_sfx(struct archive_read *a)
{
	const char *p;

	if ((p = __archive_read_ahead(a, 7, NULL)) == NULL)
		return -1;

	if ((p[0] == 'M' && p[1] == 'Z') || memcmp(p, "\x7F\x45LF", 4) == 0) {
		/* This is a PE file */
		char signature[sizeof(rar5_signature_xor)];
		ssize_t offset = 0x10000;
		ssize_t window = 4096;
		ssize_t bytes_avail;

		rar5_signature(signature);

		while (offset + window <= (1024 * 512)) {
			const char *buff = __archive_read_ahead(a, offset + window, &bytes_avail);
			if (buff == NULL) {
				/* Remaining bytes are less than window. */
				window >>= 1;
				if (window < 0x40)
					return 0;
				continue;
			}
			p = buff + offset;
			while (p + 8 < buff + bytes_avail) {
				if (memcmp(p, signature, sizeof(signature)) == 0)
					return 30;
				p += 0x10;
			}
			offset = p - buff;
		}
	}

	return 0;
}

static int rar5_bid(struct archive_read* a, int best_bid) {
	int my_bid;

	if(best_bid > 30)
		return -1;

	my_bid = bid_standard(a);
	if(my_bid > -1) {
		return my_bid;
	}
	my_bid = bid_sfx(a);
	if (my_bid > -1) {
		return my_bid;
	}

	return -1;
}

static int rar5_options(struct archive_read *a, const char *key,
    const char *val) {
	(void) a;
	(void) key;
	(void) val;

	/* No options supported in this version. Return the ARCHIVE_WARN code
	 * to signal the options supervisor that the unpacker didn't handle
	 * setting this option. */

	return ARCHIVE_WARN;
}

static void init_header(struct archive_read* a) {
	a->archive.archive_format = ARCHIVE_FORMAT_RAR_V5;
	a->archive.archive_format_name = "RAR5";
}

static void init_window_mask(struct rar5* rar) {
	if (rar->cstate.window_size)
		rar->cstate.window_mask = rar->cstate.window_size - 1;
	else
		rar->cstate.window_mask = 0;
}

enum HEADER_FLAGS {
	HFL_EXTRA_DATA = 0x0001,
	HFL_DATA = 0x0002,
	HFL_SKIP_IF_UNKNOWN = 0x0004,
	HFL_SPLIT_BEFORE = 0x0008,
	HFL_SPLIT_AFTER = 0x0010,
	HFL_CHILD = 0x0020,
	HFL_INHERITED = 0x0040
};

static int process_main_locator_extra_block(struct archive_read* a,
    struct rar5* rar)
{
	uint64_t locator_flags;

	enum LOCATOR_FLAGS {
		QLIST = 0x01, RECOVERY = 0x02,
	};

	if(!read_var(a, &locator_flags, NULL)) {
		return ARCHIVE_EOF;
	}

	if(locator_flags & QLIST) {
		if(!read_var(a, &rar->qlist_offset, NULL)) {
			return ARCHIVE_EOF;
		}

		/* qlist is not used */
	}

	if(locator_flags & RECOVERY) {
		if(!read_var(a, &rar->rr_offset, NULL)) {
			return ARCHIVE_EOF;
		}

		/* rr is not used */
	}

	return ARCHIVE_OK;
}

static int parse_file_extra_hash(struct archive_read* a, struct rar5* rar,
    int64_t* extra_data_size)
{
	size_t hash_type = 0;
	size_t value_len;

	enum HASH_TYPE {
		BLAKE2sp = 0x00
	};

	if(!read_var_sized(a, &hash_type, &value_len))
		return ARCHIVE_EOF;

	*extra_data_size -= value_len;
	if(ARCHIVE_OK != consume(a, value_len)) {
		return ARCHIVE_EOF;
	}

	/* The file uses BLAKE2sp checksum algorithm instead of plain old
	 * CRC32. */
	if(hash_type == BLAKE2sp) {
		const uint8_t* p;
		const int hash_size = sizeof(rar->file.blake2sp);

		if(!read_ahead(a, hash_size, &p))
			return ARCHIVE_EOF;

		rar->file.has_blake2 = 1;
		memcpy(&rar->file.blake2sp, p, hash_size);

		if(ARCHIVE_OK != consume(a, hash_size)) {
			return ARCHIVE_EOF;
		}

		*extra_data_size -= hash_size;
	} else {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
		    "Unsupported hash type (0x%jx)", (uintmax_t)hash_type);
		return ARCHIVE_FATAL;
	}

	return ARCHIVE_OK;
}

static int parse_htime_item(struct archive_read* a, char unix_time,
    int64_t* sec, uint32_t* nsec, int64_t* extra_data_size)
{
	if(unix_time) {
		uint32_t time_val;
		if(!read_u32(a, &time_val))
			return ARCHIVE_EOF;

		*extra_data_size -= 4;
		*sec = (int64_t) time_val;
	} else {
		uint64_t windows_time;
		if(!read_u64(a, &windows_time))
			return ARCHIVE_EOF;

		ntfs_to_unix(windows_time, sec, nsec);
		*extra_data_size -= 8;
	}

	return ARCHIVE_OK;
}

static int parse_file_extra_version(struct archive_read* a,
    struct archive_entry* e, int64_t* extra_data_size)
{
	size_t flags = 0;
	size_t version = 0;
	size_t value_len = 0;
	struct archive_string version_string;
	struct archive_string name_utf8_string;
	const char* cur_filename;

	/* Flags are ignored. */
	if(!read_var_sized(a, &flags, &value_len))
		return ARCHIVE_EOF;

	*extra_data_size -= value_len;
	if(ARCHIVE_OK != consume(a, value_len))
		return ARCHIVE_EOF;

	if(!read_var_sized(a, &version, &value_len))
		return ARCHIVE_EOF;

	*extra_data_size -= value_len;
	if(ARCHIVE_OK != consume(a, value_len))
		return ARCHIVE_EOF;

	/* extra_data_size should be zero here. */

	cur_filename = archive_entry_pathname_utf8(e);
	if(cur_filename == NULL) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_PROGRAMMER,
		    "Version entry without file name");
		return ARCHIVE_FATAL;
	}

	archive_string_init(&version_string);
	archive_string_init(&name_utf8_string);

	/* Prepare a ;123 suffix for the filename, where '123' is the version
	 * value of this file. */
	archive_string_sprintf(&version_string, ";%zu", version);

	/* Build the new filename. */
	archive_strcat(&name_utf8_string, cur_filename);
	archive_strcat(&name_utf8_string, version_string.s);

	/* Apply the new filename into this file's context. */
	archive_entry_update_pathname_utf8(e, name_utf8_string.s);

	/* Free buffers. */
	archive_string_free(&version_string);
	archive_string_free(&name_utf8_string);
	return ARCHIVE_OK;
}

static int parse_file_extra_htime(struct archive_read* a,
    struct archive_entry* e, struct rar5* rar, int64_t* extra_data_size)
{
	char unix_time, has_unix_ns, has_mtime, has_ctime, has_atime;
	size_t flags = 0;
	size_t value_len;

	enum HTIME_FLAGS {
		IS_UNIX       = 0x01,
		HAS_MTIME     = 0x02,
		HAS_CTIME     = 0x04,
		HAS_ATIME     = 0x08,
		HAS_UNIX_NS   = 0x10,
	};

	if(!read_var_sized(a, &flags, &value_len))
		return ARCHIVE_EOF;

	*extra_data_size -= value_len;
	if(ARCHIVE_OK != consume(a, value_len)) {
		return ARCHIVE_EOF;
	}

	unix_time = flags & IS_UNIX;
	has_unix_ns = unix_time && (flags & HAS_UNIX_NS);
	has_mtime = flags & HAS_MTIME;
	has_atime = flags & HAS_ATIME;
	has_ctime = flags & HAS_CTIME;
	rar->file.e_atime_ns = rar->file.e_ctime_ns = rar->file.e_mtime_ns = 0;

	if(has_mtime) {
		parse_htime_item(a, unix_time, &rar->file.e_mtime,
		    &rar->file.e_mtime_ns, extra_data_size);
	}

	if(has_ctime) {
		parse_htime_item(a, unix_time, &rar->file.e_ctime,
		    &rar->file.e_ctime_ns, extra_data_size);
	}

	if(has_atime) {
		parse_htime_item(a, unix_time, &rar->file.e_atime,
		    &rar->file.e_atime_ns, extra_data_size);
	}

	if(has_mtime && has_unix_ns) {
		if(!read_u32(a, &rar->file.e_mtime_ns))
			return ARCHIVE_EOF;

		*extra_data_size -= 4;
	}

	if(has_ctime && has_unix_ns) {
		if(!read_u32(a, &rar->file.e_ctime_ns))
			return ARCHIVE_EOF;

		*extra_data_size -= 4;
	}

	if(has_atime && has_unix_ns) {
		if(!read_u32(a, &rar->file.e_atime_ns))
			return ARCHIVE_EOF;

		*extra_data_size -= 4;
	}

	/* The seconds and nanoseconds are either together, or separated in two
	 * fields so we parse them, then set the archive_entry's times. */
	if(has_mtime) {
		archive_entry_set_mtime(e, rar->file.e_mtime, rar->file.e_mtime_ns);
	}

	if(has_ctime) {
		archive_entry_set_ctime(e, rar->file.e_ctime, rar->file.e_ctime_ns);
	}

	if(has_atime) {
		archive_entry_set_atime(e, rar->file.e_atime, rar->file.e_atime_ns);
	}

	return ARCHIVE_OK;
}

static int parse_file_extra_redir(struct archive_read* a,
    struct archive_entry* e, struct rar5* rar, int64_t* extra_data_size)
{
	uint64_t value_size = 0;
	size_t target_size = 0;
	char target_utf8_buf[MAX_NAME_IN_BYTES];
	const uint8_t* p;

	if(!read_var(a, &rar->file.redir_type, &value_size))
		return ARCHIVE_EOF;
	if(ARCHIVE_OK != consume(a, (int64_t)value_size))
		return ARCHIVE_EOF;
	*extra_data_size -= value_size;

	if(!read_var(a, &rar->file.redir_flags, &value_size))
		return ARCHIVE_EOF;
	if(ARCHIVE_OK != consume(a, (int64_t)value_size))
		return ARCHIVE_EOF;
	*extra_data_size -= value_size;

	if(!read_var_sized(a, &target_size, NULL))
		return ARCHIVE_EOF;
	*extra_data_size -= target_size + 1;

	if(target_size > (MAX_NAME_IN_CHARS - 1)) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
		    "Link target is too long");
		return ARCHIVE_FATAL;
	}

	if(target_size == 0) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
		    "No link target specified");
		return ARCHIVE_FATAL;
	}

	if(!read_ahead(a, target_size, &p))
		return ARCHIVE_EOF;

	memcpy(target_utf8_buf, p, target_size);
	target_utf8_buf[target_size] = 0;

	if(ARCHIVE_OK != consume(a, (int64_t)target_size))
		return ARCHIVE_EOF;

	switch(rar->file.redir_type) {
		case REDIR_TYPE_UNIXSYMLINK:
		case REDIR_TYPE_WINSYMLINK:
			archive_entry_set_filetype(e, AE_IFLNK);
			archive_entry_update_symlink_utf8(e, target_utf8_buf);
			if (rar->file.redir_flags & REDIR_SYMLINK_IS_DIR) {
				archive_entry_set_symlink_type(e,
					AE_SYMLINK_TYPE_DIRECTORY);
			} else {
				archive_entry_set_symlink_type(e,
				AE_SYMLINK_TYPE_FILE);
			}
			break;

		case REDIR_TYPE_HARDLINK:
			archive_entry_set_filetype(e, AE_IFREG);
			archive_entry_update_hardlink_utf8(e, target_utf8_buf);
			break;

		default:
			/* Unknown redir type, skip it. */
			break;
	}
	return ARCHIVE_OK;
}

static int parse_file_extra_owner(struct archive_read* a,
    struct archive_entry* e, int64_t* extra_data_size)
{
	uint64_t flags = 0;
	uint64_t value_size = 0;
	uint64_t id = 0;
	size_t name_len = 0;
	size_t name_size = 0;
	char namebuf[OWNER_MAXNAMELEN];
	const uint8_t* p;

	if(!read_var(a, &flags, &value_size))
		return ARCHIVE_EOF;
	if(ARCHIVE_OK != consume(a, (int64_t)value_size))
		return ARCHIVE_EOF;
	*extra_data_size -= value_size;

	if ((flags & OWNER_USER_NAME) != 0) {
		if(!read_var_sized(a, &name_size, NULL))
			return ARCHIVE_EOF;
		*extra_data_size -= name_size + 1;

		if(!read_ahead(a, name_size, &p))
			return ARCHIVE_EOF;

		if (name_size >= OWNER_MAXNAMELEN) {
			name_len = OWNER_MAXNAMELEN - 1;
		} else {
			name_len = name_size;
		}

		memcpy(namebuf, p, name_len);
		namebuf[name_len] = 0;
		if(ARCHIVE_OK != consume(a, (int64_t)name_size))
			return ARCHIVE_EOF;

		archive_entry_set_uname(e, namebuf);
	}
	if ((flags & OWNER_GROUP_NAME) != 0) {
		if(!read_var_sized(a, &name_size, NULL))
			return ARCHIVE_EOF;
		*extra_data_size -= name_size + 1;

		if(!read_ahead(a, name_size, &p))
			return ARCHIVE_EOF;

		if (name_size >= OWNER_MAXNAMELEN) {
			name_len = OWNER_MAXNAMELEN - 1;
		} else {
			name_len = name_size;
		}

		memcpy(namebuf, p, name_len);
		namebuf[name_len] = 0;
		if(ARCHIVE_OK != consume(a, (int64_t)name_size))
			return ARCHIVE_EOF;

		archive_entry_set_gname(e, namebuf);
	}
	if ((flags & OWNER_USER_UID) != 0) {
		if(!read_var(a, &id, &value_size))
			return ARCHIVE_EOF;
		if(ARCHIVE_OK != consume(a, (int64_t)value_size))
			return ARCHIVE_EOF;
		*extra_data_size -= value_size;

		archive_entry_set_uid(e, (la_int64_t)id);
	}
	if ((flags & OWNER_GROUP_GID) != 0) {
		if(!read_var(a, &id, &value_size))
			return ARCHIVE_EOF;
		if(ARCHIVE_OK != consume(a, (int64_t)value_size))
			return ARCHIVE_EOF;
		*extra_data_size -= value_size;

		archive_entry_set_gid(e, (la_int64_t)id);
	}
	return ARCHIVE_OK;
}

static int process_head_file_extra(struct archive_read* a,
    struct archive_entry* e, struct rar5* rar, int64_t extra_data_size)
{
	uint64_t extra_field_size;
	uint64_t extra_field_id = 0;
	int ret = ARCHIVE_FATAL;
	uint64_t var_size;

	while(extra_data_size > 0) {
		if(!read_var(a, &extra_field_size, &var_size))
			return ARCHIVE_EOF;

		extra_data_size -= var_size;
		if(ARCHIVE_OK != consume(a, var_size)) {
			return ARCHIVE_EOF;
		}

		if(!read_var(a, &extra_field_id, &var_size))
			return ARCHIVE_EOF;

		extra_field_size -= var_size;
		extra_data_size -= var_size;
		if(ARCHIVE_OK != consume(a, var_size)) {
			return ARCHIVE_EOF;
		}

		switch(extra_field_id) {
			case EX_HASH:
				ret = parse_file_extra_hash(a, rar,
				    &extra_data_size);
				break;
			case EX_HTIME:
				ret = parse_file_extra_htime(a, e, rar,
				    &extra_data_size);
				break;
			case EX_REDIR:
				ret = parse_file_extra_redir(a, e, rar,
				    &extra_data_size);
				break;
			case EX_UOWNER:
				ret = parse_file_extra_owner(a, e,
				    &extra_data_size);
				break;
			case EX_VERSION:
				ret = parse_file_extra_version(a, e,
				    &extra_data_size);
				break;
			case EX_CRYPT:
				/* Mark the entry as encrypted */
				archive_entry_set_is_data_encrypted(e, 1);
				rar->has_encrypted_entries = 1;
				rar->cstate.data_encrypted = 1;
				/* fallthrough */
			case EX_SUBDATA:
				/* fallthrough */
			default:
				/* Skip unsupported entry. */
				extra_data_size -= extra_field_size;
				if (ARCHIVE_OK != consume(a, extra_field_size)) {
					return ARCHIVE_EOF;
				}
		}
	}

	if(ret != ARCHIVE_OK) {
		/* Attribute not implemented. */
		return ret;
	}

	return ARCHIVE_OK;
}

static int process_head_file(struct archive_read* a, struct rar5* rar,
    struct archive_entry* entry, size_t block_flags)
{
	int64_t extra_data_size = 0;
	size_t data_size = 0;
	size_t file_flags = 0;
	size_t file_attr = 0;
	size_t compression_info = 0;
	size_t host_os = 0;
	size_t name_size = 0;
	uint64_t unpacked_size, window_size;
	uint32_t mtime = 0, crc = 0;
	int c_method = 0, c_version = 0;
	char name_utf8_buf[MAX_NAME_IN_BYTES];
	const uint8_t* p;

	enum FILE_FLAGS {
		DIRECTORY = 0x0001, UTIME = 0x0002, CRC32 = 0x0004,
		UNKNOWN_UNPACKED_SIZE = 0x0008,
	};

	enum FILE_ATTRS {
		ATTR_READONLY = 0x1, ATTR_HIDDEN = 0x2, ATTR_SYSTEM = 0x4,
		ATTR_DIRECTORY = 0x10,
	};

	enum COMP_INFO_FLAGS {
		SOLID = 0x0040,
	};

	enum HOST_OS {
		HOST_WINDOWS = 0,
		HOST_UNIX = 1,
	};

	archive_entry_clear(entry);

	/* Do not reset file context if we're switching archives. */
	if(!rar->cstate.switch_multivolume) {
		reset_file_context(rar);
	}

	if(block_flags & HFL_EXTRA_DATA) {
		uint64_t edata_size = 0;
		if(!read_var(a, &edata_size, NULL))
			return ARCHIVE_EOF;

		/* Intentional type cast from unsigned to signed. */
		extra_data_size = (int64_t) edata_size;
	}

	if(block_flags & HFL_DATA) {
		if(!read_var_sized(a, &data_size, NULL))
			return ARCHIVE_EOF;

		rar->file.bytes_remaining = data_size;
	} else {
		rar->file.bytes_remaining = 0;

		archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
				"no data found in file/service block");
		return ARCHIVE_FATAL;
	}

	if(!read_var_sized(a, &file_flags, NULL))
		return ARCHIVE_EOF;

	if(!read_var(a, &unpacked_size, NULL))
		return ARCHIVE_EOF;

	if(file_flags & UNKNOWN_UNPACKED_SIZE) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_PROGRAMMER,
		    "Files with unknown unpacked size are not supported");
		return ARCHIVE_FATAL;
	}

	rar->file.dir = (uint8_t) ((file_flags & DIRECTORY) > 0);

	if(!read_var_sized(a, &file_attr, NULL))
		return ARCHIVE_EOF;

	if(file_flags & UTIME) {
		if(!read_u32(a, &mtime))
			return ARCHIVE_EOF;
	}

	if(file_flags & CRC32) {
		if(!read_u32(a, &crc))
			return ARCHIVE_EOF;
	}

	if(!read_var_sized(a, &compression_info, NULL))
		return ARCHIVE_EOF;

	c_method = (int) (compression_info >> 7) & 0x7;
	c_version = (int) (compression_info & 0x3f);

	/* RAR5 seems to limit the dictionary size to 64MB. */
	window_size = (rar->file.dir > 0) ?
		0 :
		g_unpack_window_size << ((compression_info >> 10) & 15);
	rar->cstate.method = c_method;
	rar->cstate.version = c_version + 50;
	rar->file.solid = (compression_info & SOLID) > 0;

	/* Archives which declare solid files without initializing the window
	 * buffer first are invalid, unless previous data was encrypted, in
	 * which case we may never have had the chance */

	if(rar->file.solid > 0 && rar->cstate.data_encrypted == 0 &&
	    rar->cstate.window_buf == NULL) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
				  "Declared solid file, but no window buffer "
				  "initialized yet.");
		return ARCHIVE_FATAL;
	}

	/* Check if window_size is a sane value. Also, if the file is not
	 * declared as a directory, disallow window_size == 0. */
	if(window_size > (64 * 1024 * 1024) ||
	    (rar->file.dir == 0 && window_size == 0))
	{
		archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
		    "Declared dictionary size is not supported.");
		return ARCHIVE_FATAL;
	}

	if(rar->file.solid > 0) {
		/* Re-check if current window size is the same as previous
		 * window size (for solid files only). */
		if(rar->file.solid_window_size > 0 &&
		    rar->file.solid_window_size != (ssize_t) window_size)
		{
			archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
			    "Window size for this solid file doesn't match "
			    "the window size used in previous solid file. ");
			return ARCHIVE_FATAL;
		}
	}
	else
		rar->cstate.data_encrypted = 0; /* Reset for new buffer */

	if(rar->cstate.window_size < (ssize_t) window_size &&
	    rar->cstate.window_buf)
	{
		/* The `data_ready` stack contains pointers to the `window_buf` or
		 * `filtered_buf` buffers.  Since we're about to reallocate the first
		 * buffer, some of those pointers could become invalid. Therefore, we
		 * need to dispose of all entries from the stack before attempting the
		 * realloc. */
		clear_data_ready_stack(rar);

		/* If window_buf has been allocated before, reallocate it, so
		 * that its size will match new window_size. */

		uint8_t* new_window_buf =
			realloc(rar->cstate.window_buf, (size_t) window_size);

		if(!new_window_buf) {
			archive_set_error(&a->archive, ARCHIVE_ERRNO_PROGRAMMER,
				"Not enough memory when trying to realloc the window "
				"buffer.");
			return ARCHIVE_FATAL;
		}

		rar->cstate.window_buf = new_window_buf;
	}

	/* Values up to 64M should fit into ssize_t on every
	 * architecture. */
	rar->cstate.window_size = (ssize_t) window_size;

	if(rar->file.solid > 0 && rar->file.solid_window_size == 0) {
		/* Solid files have to have the same window_size across
		   whole archive. Remember the window_size parameter
		   for first solid file found. */
		rar->file.solid_window_size = rar->cstate.window_size;
	}

	init_window_mask(rar);

	rar->file.service = 0;

	if(!read_var_sized(a, &host_os, NULL))
		return ARCHIVE_EOF;

	if(host_os == HOST_WINDOWS) {
		/* Host OS is Windows */

		__LA_MODE_T mode;

		if(file_attr & ATTR_DIRECTORY) {
			if (file_attr & ATTR_READONLY) {
				mode = 0555 | AE_IFDIR;
			} else {
				mode = 0755 | AE_IFDIR;
			}
		} else {
			if (file_attr & ATTR_READONLY) {
				mode = 0444 | AE_IFREG;
			} else {
				mode = 0644 | AE_IFREG;
			}
		}

		archive_entry_set_mode(entry, mode);

		if (file_attr & (ATTR_READONLY | ATTR_HIDDEN | ATTR_SYSTEM)) {
			char *fflags_text, *ptr;
			/* allocate for ",rdonly,hidden,system" */
			fflags_text = malloc(22 * sizeof(*fflags_text));
			if (fflags_text != NULL) {
				ptr = fflags_text;
				if (file_attr & ATTR_READONLY) {
					strcpy(ptr, ",rdonly");
					ptr = ptr + 7;
				}
				if (file_attr & ATTR_HIDDEN) {
					strcpy(ptr, ",hidden");
					ptr = ptr + 7;
				}
				if (file_attr & ATTR_SYSTEM) {
					strcpy(ptr, ",system");
					ptr = ptr + 7;
				}
				if (ptr > fflags_text) {
					archive_entry_copy_fflags_text(entry,
					    fflags_text + 1);
				}
				free(fflags_text);
			}
		}
	} else if(host_os == HOST_UNIX) {
		/* Host OS is Unix */
		archive_entry_set_mode(entry, (__LA_MODE_T) file_attr);
	} else {
		/* Unknown host OS */
		archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
				"Unsupported Host OS: 0x%jx",
				(uintmax_t)host_os);

		return ARCHIVE_FATAL;
	}

	if(!read_var_sized(a, &name_size, NULL))
		return ARCHIVE_EOF;

	if(name_size > (MAX_NAME_IN_CHARS - 1)) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
				"Filename is too long");

		return ARCHIVE_FATAL;
	}

	if(name_size == 0) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
				"No filename specified");

		return ARCHIVE_FATAL;
	}

	if(!read_ahead(a, name_size, &p))
		return ARCHIVE_EOF;

	memcpy(name_utf8_buf, p, name_size);
	name_utf8_buf[name_size] = 0;
	if(ARCHIVE_OK != consume(a, name_size)) {
		return ARCHIVE_EOF;
	}

	archive_entry_update_pathname_utf8(entry, name_utf8_buf);

	if(extra_data_size > 0) {
		int ret = process_head_file_extra(a, entry, rar,
		    extra_data_size);

		/*
		 * TODO: rewrite or remove useless sanity check
		 *       as extra_data_size is not passed as a pointer
		 *
		if(extra_data_size < 0) {
			archive_set_error(&a->archive, ARCHIVE_ERRNO_PROGRAMMER,
			    "File extra data size is not zero");
			return ARCHIVE_FATAL;
		}
		 */

		if(ret != ARCHIVE_OK)
			return ret;
	}

	if((file_flags & UNKNOWN_UNPACKED_SIZE) == 0) {
		rar->file.unpacked_size = (ssize_t) unpacked_size;
		if(rar->file.redir_type == REDIR_TYPE_NONE)
			archive_entry_set_size(entry, unpacked_size);
	}

	if(file_flags & UTIME) {
		archive_entry_set_mtime(entry, (time_t) mtime, 0);
	}

	if(file_flags & CRC32) {
		rar->file.stored_crc32 = crc;
	}

	if(!rar->cstate.switch_multivolume) {
		/* Do not reinitialize unpacking state if we're switching
		 * archives. */
		rar->cstate.block_parsing_finished = 1;
		rar->cstate.all_filters_applied = 1;
		rar->cstate.initialized = 0;
	}

	if(rar->generic.split_before > 0) {
		/* If now we're standing on a header that has a 'split before'
		 * mark, it means we're standing on a 'continuation' file
		 * header. Signal the caller that if it wants to move to
		 * another file, it must call rar5_read_header() function
		 * again. */

		return ARCHIVE_RETRY;
	} else {
		return ARCHIVE_OK;
	}
}

static int process_head_service(struct archive_read* a, struct rar5* rar,
    struct archive_entry* entry, size_t block_flags)
{
	/* Process this SERVICE block the same way as FILE blocks. */
	int ret = process_head_file(a, rar, entry, block_flags);
	if(ret != ARCHIVE_OK)
		return ret;

	rar->file.service = 1;

	/* But skip the data part automatically. It's no use for the user
	 * anyway.  It contains only service data, not even needed to
	 * properly unpack the file. */
	ret = rar5_read_data_skip(a);
	if(ret != ARCHIVE_OK)
		return ret;

	/* After skipping, try parsing another block automatically. */
	return ARCHIVE_RETRY;
}

static int process_head_main(struct archive_read* a, struct rar5* rar,
    struct archive_entry* entry, size_t block_flags)
{
	int ret;
	uint64_t extra_data_size = 0;
	size_t extra_field_size = 0;
	size_t extra_field_id = 0;
	size_t archive_flags = 0;

	enum MAIN_FLAGS {
		VOLUME = 0x0001,         /* multi-volume archive */
		VOLUME_NUMBER = 0x0002,  /* volume number, first vol doesn't
					  * have it */
		SOLID = 0x0004,          /* solid archive */
		PROTECT = 0x0008,        /* contains Recovery info */
		LOCK = 0x0010,           /* readonly flag, not used */
	};

	enum MAIN_EXTRA {
		/* Just one attribute here. */
		LOCATOR = 0x01,
	};

	(void) entry;

	if(block_flags & HFL_EXTRA_DATA) {
		if(!read_var(a, &extra_data_size, NULL))
			return ARCHIVE_EOF;
	} else {
		extra_data_size = 0;
	}

	if(!read_var_sized(a, &archive_flags, NULL)) {
		return ARCHIVE_EOF;
	}

	rar->main.volume = (archive_flags & VOLUME) > 0;
	rar->main.solid = (archive_flags & SOLID) > 0;

	if(archive_flags & VOLUME_NUMBER) {
		size_t v = 0;
		if(!read_var_sized(a, &v, NULL)) {
			return ARCHIVE_EOF;
		}

		if (v > UINT_MAX) {
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_FILE_FORMAT,
			    "Invalid volume number");
			return ARCHIVE_FATAL;
		}

		rar->main.vol_no = (unsigned int) v;
	} else {
		rar->main.vol_no = 0;
	}

	if(rar->vol.expected_vol_no > 0 &&
		rar->main.vol_no != rar->vol.expected_vol_no)
	{
		/* Returning EOF instead of FATAL because of strange
		 * libarchive behavior. When opening multiple files via
		 * archive_read_open_filenames(), after reading up the whole
		 * last file, the __archive_read_ahead function wraps up to
		 * the first archive instead of returning EOF. */
		return ARCHIVE_EOF;
	}

	if(extra_data_size == 0) {
		/* Early return. */
		return ARCHIVE_OK;
	}

	if(!read_var_sized(a, &extra_field_size, NULL)) {
		return ARCHIVE_EOF;
	}

	if(!read_var_sized(a, &extra_field_id, NULL)) {
		return ARCHIVE_EOF;
	}

	if(extra_field_size == 0) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
		    "Invalid extra field size");
		return ARCHIVE_FATAL;
	}

	switch(extra_field_id) {
		case LOCATOR:
			ret = process_main_locator_extra_block(a, rar);
			if(ret != ARCHIVE_OK) {
				/* Error while parsing main locator extra
				 * block. */
				return ret;
			}

			break;
		default:
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_FILE_FORMAT,
			    "Unsupported extra type (0x%jx)",
			    (uintmax_t)extra_field_id);
			return ARCHIVE_FATAL;
	}

	return ARCHIVE_OK;
}

static int skip_unprocessed_bytes(struct archive_read* a) {
	struct rar5* rar = get_context(a);
	int ret;

	if(rar->file.bytes_remaining) {
		/* Use different skipping method in block merging mode than in
		 * normal mode. If merge mode is active, rar5_read_data_skip
		 * can't be used, because it could allow recursive use of
		 * merge_block() * function, and this function doesn't support
		 * recursive use. */
		if(rar->merge_mode) {
			/* Discard whole merged block. This is valid in solid
			 * mode as well, because the code will discard blocks
			 * only if those blocks are safe to discard (i.e.
			 * they're not FILE blocks).  */
			ret = consume(a, rar->file.bytes_remaining);
			if(ret != ARCHIVE_OK) {
				return ret;
			}
			rar->file.bytes_remaining = 0;
		} else {
			/* If we're not in merge mode, use safe skipping code.
			 * This will ensure we'll handle solid archives
			 * properly. */
			ret = rar5_read_data_skip(a);
			if(ret != ARCHIVE_OK) {
				return ret;
			}
		}
	}

	return ARCHIVE_OK;
}

static int scan_for_signature(struct archive_read* a);

/* Base block processing function. A 'base block' is a RARv5 header block
 * that tells the reader what kind of data is stored inside the block.
 *
 * From the birds-eye view a RAR file looks file this:
 *
 * <magic><base_block_1><base_block_2>...<base_block_n>
 *
 * There are a few types of base blocks. Those types are specified inside
 * the 'switch' statement in this function. For example purposes, I'll write
 * how a standard RARv5 file could look like here:
 *
 * <magic><MAIN><FILE><FILE><FILE><SERVICE><ENDARC>
 *
 * The structure above could describe an archive file with 3 files in it,
 * one service "QuickOpen" block (that is ignored by this parser), and an
 * end of file base block marker.
 *
 * If the file is stored in multiple archive files ("multiarchive"), it might
 * look like this:
 *
 * .part01.rar: <magic><MAIN><FILE><ENDARC>
 * .part02.rar: <magic><MAIN><FILE><ENDARC>
 * .part03.rar: <magic><MAIN><FILE><ENDARC>
 *
 * This example could describe 3 RAR files that contain ONE archived file.
 * Or it could describe 3 RAR files that contain 3 different files. Or 3
 * RAR files than contain 2 files. It all depends what metadata is stored in
 * the headers of <FILE> blocks.
 *
 * Each <FILE> block contains info about its size, the name of the file it's
 * storing inside, and whether this FILE block is a continuation block of
 * previous archive ('split before'), and is this FILE block should be
 * continued in another archive ('split after'). By parsing the 'split before'
 * and 'split after' flags, we're able to tell if multiple <FILE> base blocks
 * are describing one file, or multiple files (with the same filename, for
 * example).
 *
 * One thing to note is that if we're parsing the first <FILE> block, and
 * we see 'split after' flag, then we need to jump over to another <FILE>
 * block to be able to decompress rest of the data. To do this, we need
 * to skip the <ENDARC> block, then switch to another file, then skip the
 * <magic> block, <MAIN> block, and then we're standing on the proper
 * <FILE> block.
 */

static int process_base_block(struct archive_read* a,
    struct archive_entry* entry)
{
	const size_t SMALLEST_RAR5_BLOCK_SIZE = 3;

	struct rar5* rar = get_context(a);
	uint32_t hdr_crc, computed_crc;
	size_t raw_hdr_size = 0, hdr_size_len, hdr_size;
	size_t header_id = 0;
	size_t header_flags = 0;
	const uint8_t* p;
	int ret;

	enum HEADER_TYPE {
		HEAD_MARK    = 0x00, HEAD_MAIN  = 0x01, HEAD_FILE   = 0x02,
		HEAD_SERVICE = 0x03, HEAD_CRYPT = 0x04, HEAD_ENDARC = 0x05,
		HEAD_UNKNOWN = 0xff,
	};

	/* Skip any unprocessed data for this file. */
	ret = skip_unprocessed_bytes(a);
	if(ret != ARCHIVE_OK)
		return ret;

	/* Read the expected CRC32 checksum. */
	if(!read_u32(a, &hdr_crc)) {
		return ARCHIVE_EOF;
	}

	/* Read header size. */
	if(!read_var_sized(a, &raw_hdr_size, &hdr_size_len)) {
		return ARCHIVE_EOF;
	}

	hdr_size = raw_hdr_size + hdr_size_len;

	/* Sanity check, maximum header size for RAR5 is 2MB. */
	if(hdr_size > (2 * 1024 * 1024)) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
		    "Base block header is too large");

		return ARCHIVE_FATAL;
	}

	/* Additional sanity checks to weed out invalid files. */
	if(raw_hdr_size == 0 || hdr_size_len == 0 ||
		hdr_size < SMALLEST_RAR5_BLOCK_SIZE)
	{
		archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
		    "Too small block encountered (%zu bytes)",
		    raw_hdr_size);

		return ARCHIVE_FATAL;
	}

	/* Read the whole header data into memory, maximum memory use here is
	 * 2MB. */
	if(!read_ahead(a, hdr_size, &p)) {
		return ARCHIVE_EOF;
	}

	/* Verify the CRC32 of the header data. */
	computed_crc = (uint32_t) crc32(0, p, (int) hdr_size);
	if(computed_crc != hdr_crc) {
#ifndef DONT_FAIL_ON_CRC_ERROR
		archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
		    "Header CRC error");

		return ARCHIVE_FATAL;
#endif
	}

	/* If the checksum is OK, we proceed with parsing. */
	if(ARCHIVE_OK != consume(a, hdr_size_len)) {
		return ARCHIVE_EOF;
	}

	if(!read_var_sized(a, &header_id, NULL))
		return ARCHIVE_EOF;

	if(!read_var_sized(a, &header_flags, NULL))
		return ARCHIVE_EOF;

	rar->generic.split_after = (header_flags & HFL_SPLIT_AFTER) > 0;
	rar->generic.split_before = (header_flags & HFL_SPLIT_BEFORE) > 0;
	rar->generic.size = (int)hdr_size;
	rar->generic.last_header_id = (int)header_id;
	rar->main.endarc = 0;

	/* Those are possible header ids in RARv5. */
	switch(header_id) {
		case HEAD_MAIN:
			ret = process_head_main(a, rar, entry, header_flags);

			/* Main header doesn't have any files in it, so it's
			 * pointless to return to the caller. Retry to next
			 * header, which should be HEAD_FILE/HEAD_SERVICE. */
			if(ret == ARCHIVE_OK)
				return ARCHIVE_RETRY;

			return ret;
		case HEAD_SERVICE:
			ret = process_head_service(a, rar, entry, header_flags);
			return ret;
		case HEAD_FILE:
			ret = process_head_file(a, rar, entry, header_flags);
			return ret;
		case HEAD_CRYPT:
			archive_entry_set_is_metadata_encrypted(entry, 1);
			archive_entry_set_is_data_encrypted(entry, 1);
			rar->has_encrypted_entries = 1;
			rar->headers_are_encrypted = 1;
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_FILE_FORMAT,
			    "Encryption is not supported");
			return ARCHIVE_FATAL;
		case HEAD_ENDARC:
			rar->main.endarc = 1;

			/* After encountering an end of file marker, we need
			 * to take into consideration if this archive is
			 * continued in another file (i.e. is it part01.rar:
			 * is there a part02.rar?) */
			if(rar->main.volume) {
				/* In case there is part02.rar, position the
				 * read pointer in a proper place, so we can
				 * resume parsing. */
				ret = scan_for_signature(a);
				if(ret == ARCHIVE_FATAL) {
					return ARCHIVE_EOF;
				} else {
					if(rar->vol.expected_vol_no ==
					    UINT_MAX) {
						archive_set_error(&a->archive,
						    ARCHIVE_ERRNO_FILE_FORMAT,
						    "Header error");
							return ARCHIVE_FATAL;
					}

					rar->vol.expected_vol_no =
					    rar->main.vol_no + 1;
					return ARCHIVE_OK;
				}
			} else {
				return ARCHIVE_EOF;
			}
		case HEAD_MARK:
			return ARCHIVE_EOF;
		default:
			if((header_flags & HFL_SKIP_IF_UNKNOWN) == 0) {
				archive_set_error(&a->archive,
				    ARCHIVE_ERRNO_FILE_FORMAT,
				    "Header type error");
				return ARCHIVE_FATAL;
			} else {
				/* If the block is marked as 'skip if unknown',
				 * do as the flag says: skip the block
				 * instead on failing on it. */
				return ARCHIVE_RETRY;
			}
	}

#if !defined WIN32
	/* Not reached. */
	archive_set_error(&a->archive, ARCHIVE_ERRNO_PROGRAMMER,
	    "Internal unpacker error");
	return ARCHIVE_FATAL;
#endif
}

static int skip_base_block(struct archive_read* a) {
	int ret;
	struct rar5* rar = get_context(a);

	/* Create a new local archive_entry structure that will be operated on
	 * by header reader; operations on this archive_entry will be discarded.
	 */
	struct archive_entry* entry = archive_entry_new();
	ret = process_base_block(a, entry);

	/* Discard operations on this archive_entry structure. */
	archive_entry_free(entry);
	if(ret == ARCHIVE_FATAL)
		return ret;

	if(rar->generic.last_header_id == 2 && rar->generic.split_before > 0)
		return ARCHIVE_OK;

	if(ret == ARCHIVE_OK)
		return ARCHIVE_RETRY;
	else
		return ret;
}

static int try_skip_sfx(struct archive_read *a)
{
	const char *p;

	if ((p = __archive_read_ahead(a, 7, NULL)) == NULL)
		return ARCHIVE_EOF;

	if ((p[0] == 'M' && p[1] == 'Z') || memcmp(p, "\x7F\x45LF", 4) == 0)
	{
		char signature[sizeof(rar5_signature_xor)];
		const void *h;
		const char *q;
		size_t skip, total = 0;
		ssize_t bytes, window = 4096;

		rar5_signature(signature);

		while (total + window <= (1024 * 512)) {
			h = __archive_read_ahead(a, window, &bytes);
			if (h == NULL) {
				/* Remaining bytes are less than window. */
				window >>= 1;
				if (window < 0x40)
					goto fatal;
				continue;
			}
			if (bytes < 0x40)
				goto fatal;
			p = h;
			q = p + bytes;

			/*
			 * Scan ahead until we find something that looks
			 * like the RAR header.
			 */
			while (p + 8 < q) {
				if (memcmp(p, signature, sizeof(signature)) == 0) {
					skip = p - (const char *)h;
					__archive_read_consume(a, skip);
					return (ARCHIVE_OK);
				}
				p += 0x10;
			}
			skip = p - (const char *)h;
			__archive_read_consume(a, skip);
			total += skip;
		}
	}

	return ARCHIVE_OK;
fatal:
	archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
			"Couldn't find out RAR header");
	return (ARCHIVE_FATAL);
}

static int rar5_read_header(struct archive_read *a,
    struct archive_entry *entry)
{
	struct rar5* rar = get_context(a);
	int ret;

	/*
	 * It should be sufficient to call archive_read_next_header() for
	 * a reader to determine if an entry is encrypted or not.
	 */
	if (rar->has_encrypted_entries == ARCHIVE_READ_FORMAT_ENCRYPTION_DONT_KNOW) {
		rar->has_encrypted_entries = 0;
	}

	if(rar->header_initialized == 0) {
		init_header(a);
		if ((ret = try_skip_sfx(a)) < ARCHIVE_WARN)
			return ret;
		rar->header_initialized = 1;
	}

	if(rar->skipped_magic == 0) {
		if(ARCHIVE_OK != consume(a, sizeof(rar5_signature_xor))) {
			return ARCHIVE_EOF;
		}

		rar->skipped_magic = 1;
	}

	do {
		ret = process_base_block(a, entry);
	} while(ret == ARCHIVE_RETRY ||
			(rar->main.endarc > 0 && ret == ARCHIVE_OK));

	return ret;
}

static void init_unpack(struct rar5* rar) {
	rar->file.calculated_crc32 = 0;
	init_window_mask(rar);

	free(rar->cstate.window_buf);
	free(rar->cstate.filtered_buf);

	if(rar->cstate.window_size > 0) {
		rar->cstate.window_buf = calloc(1, rar->cstate.window_size);
		rar->cstate.filtered_buf = calloc(1, rar->cstate.window_size);
	} else {
		rar->cstate.window_buf = NULL;
		rar->cstate.filtered_buf = NULL;
	}

	clear_data_ready_stack(rar);

	rar->cstate.write_ptr = 0;
	rar->cstate.last_write_ptr = 0;

	memset(&rar->cstate.bd, 0, sizeof(rar->cstate.bd));
	memset(&rar->cstate.ld, 0, sizeof(rar->cstate.ld));
	memset(&rar->cstate.dd, 0, sizeof(rar->cstate.dd));
	memset(&rar->cstate.ldd, 0, sizeof(rar->cstate.ldd));
	memset(&rar->cstate.rd, 0, sizeof(rar->cstate.rd));
}

static void update_crc(struct rar5* rar, const uint8_t* p, size_t to_read) {
    int verify_crc;

	if(rar->skip_mode) {
#if defined CHECK_CRC_ON_SOLID_SKIP
		verify_crc = 1;
#else
		verify_crc = 0;
#endif
	} else
		verify_crc = 1;

	if(verify_crc) {
		/* Don't update CRC32 if the file doesn't have the
		 * `stored_crc32` info filled in. */
		if(rar->file.stored_crc32 > 0) {
			rar->file.calculated_crc32 =
				crc32(rar->file.calculated_crc32, p, (unsigned int)to_read);
		}

		/* Check if the file uses an optional BLAKE2sp checksum
		 * algorithm. */
		if(rar->file.has_blake2 > 0) {
			/* Return value of the `update` function is always 0,
			 * so we can explicitly ignore it here. */
			(void) blake2sp_update(&rar->file.b2state, p, to_read);
		}
	}
}

static int create_decode_tables(uint8_t* bit_length,
    struct decode_table* table, int size)
{
	int code, upper_limit = 0, i, lc[16];
	uint32_t decode_pos_clone[rar5_countof(table->decode_pos)];
	ssize_t cur_len, quick_data_size;

	memset(&lc, 0, sizeof(lc));
	memset(table->decode_num, 0, sizeof(table->decode_num));
	table->size = size;
	table->quick_bits = size == HUFF_NC ? 10 : 7;

	for(i = 0; i < size; i++) {
		lc[bit_length[i] & 15]++;
	}

	lc[0] = 0;
	table->decode_pos[0] = 0;
	table->decode_len[0] = 0;

	for(i = 1; i < 16; i++) {
		upper_limit += lc[i];

		table->decode_len[i] = upper_limit << (16 - i);
		table->decode_pos[i] = table->decode_pos[i - 1] + lc[i - 1];

		upper_limit <<= 1;
	}

	memcpy(decode_pos_clone, table->decode_pos, sizeof(decode_pos_clone));

	for(i = 0; i < size; i++) {
		uint8_t clen = bit_length[i] & 15;
		if(clen > 0) {
			int last_pos = decode_pos_clone[clen];
			table->decode_num[last_pos] = i;
			decode_pos_clone[clen]++;
		}
	}

	quick_data_size = (int64_t)1 << table->quick_bits;
	cur_len = 1;
	for(code = 0; code < quick_data_size; code++) {
		int bit_field = code << (16 - table->quick_bits);
		int dist, pos;

		while(cur_len < rar5_countof(table->decode_len) &&
				bit_field >= table->decode_len[cur_len]) {
			cur_len++;
		}

		table->quick_len[code] = (uint8_t) cur_len;

		dist = bit_field - table->decode_len[cur_len - 1];
		dist >>= (16 - cur_len);

		pos = table->decode_pos[cur_len & 15] + dist;
		if(cur_len < rar5_countof(table->decode_pos) && pos < size) {
			table->quick_num[code] = table->decode_num[pos];
		} else {
			table->quick_num[code] = 0;
		}
	}

	return ARCHIVE_OK;
}

static int decode_number(struct archive_read* a, struct decode_table* table,
    const uint8_t* p, uint16_t* num)
{
	int i, bits, dist, ret;
	uint16_t bitfield;
	uint32_t pos;
	struct rar5* rar = get_context(a);

	if(ARCHIVE_OK != (ret = read_bits_16(a, rar, p, &bitfield))) {
		return ret;
	}

	bitfield &= 0xfffe;

	if(bitfield < table->decode_len[table->quick_bits]) {
		int code = bitfield >> (16 - table->quick_bits);
		skip_bits(rar, table->quick_len[code]);
		*num = table->quick_num[code];
		return ARCHIVE_OK;
	}

	bits = 15;

	for(i = table->quick_bits + 1; i < 15; i++) {
		if(bitfield < table->decode_len[i]) {
			bits = i;
			break;
		}
	}

	skip_bits(rar, bits);

	dist = bitfield - table->decode_len[bits - 1];
	dist >>= (16 - bits);
	pos = table->decode_pos[bits] + dist;

	if(pos >= table->size)
		pos = 0;

	*num = table->decode_num[pos];
	return ARCHIVE_OK;
}

/* Reads and parses Huffman tables from the beginning of the block. */
static int parse_tables(struct archive_read* a, struct rar5* rar,
    const uint8_t* p)
{
	int ret, value, i, w, idx = 0;
	uint8_t bit_length[HUFF_BC],
		table[HUFF_TABLE_SIZE],
		nibble_mask = 0xF0,
		nibble_shift = 4;

	enum { ESCAPE = 15 };

	/* The data for table generation is compressed using a simple RLE-like
	 * algorithm when storing zeroes, so we need to unpack it first. */
	for(w = 0, i = 0; w < HUFF_BC;) {
		if(i >= rar->cstate.cur_block_size) {
			/* Truncated data, can't continue. */
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_FILE_FORMAT,
			    "Truncated data in huffman tables");
			return ARCHIVE_FATAL;
		}

		value = (p[i] & nibble_mask) >> nibble_shift;

		if(nibble_mask == 0x0F)
			++i;

		nibble_mask ^= 0xFF;
		nibble_shift ^= 4;

		/* Values smaller than 15 is data, so we write it directly.
		 * Value 15 is a flag telling us that we need to unpack more
		 * bytes. */
		if(value == ESCAPE) {
			value = (p[i] & nibble_mask) >> nibble_shift;
			if(nibble_mask == 0x0F)
				++i;
			nibble_mask ^= 0xFF;
			nibble_shift ^= 4;

			if(value == 0) {
				/* We sometimes need to write the actual value
				 * of 15, so this case handles that. */
				bit_length[w++] = ESCAPE;
			} else {
				int k;

				/* Fill zeroes. */
				for(k = 0; (k < value + 2) && (w < HUFF_BC);
				    k++) {
					bit_length[w++] = 0;
				}
			}
		} else {
			bit_length[w++] = value;
		}
	}

	rar->bits.in_addr = i;
	rar->bits.bit_addr = nibble_shift ^ 4;

	ret = create_decode_tables(bit_length, &rar->cstate.bd, HUFF_BC);
	if(ret != ARCHIVE_OK) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
		    "Decoding huffman tables failed");
		return ARCHIVE_FATAL;
	}

	for(i = 0; i < HUFF_TABLE_SIZE;) {
		uint16_t num;

		ret = decode_number(a, &rar->cstate.bd, p, &num);
		if(ret != ARCHIVE_OK) {
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_FILE_FORMAT,
			    "Decoding huffman tables failed");
			return ARCHIVE_FATAL;
		}

		if(num < 16) {
			/* 0..15: store directly */
			table[i] = (uint8_t) num;
			i++;
		} else if(num < 18) {
			/* 16..17: repeat previous code */
			uint16_t n;

			if(ARCHIVE_OK != (ret = read_bits_16(a, rar, p, &n)))
				return ret;

			if(num == 16) {
				n >>= 13;
				n += 3;
				skip_bits(rar, 3);
			} else {
				n >>= 9;
				n += 11;
				skip_bits(rar, 7);
			}

			if(i > 0) {
				while(n-- > 0 && i < HUFF_TABLE_SIZE) {
					table[i] = table[i - 1];
					i++;
				}
			} else {
				archive_set_error(&a->archive,
				    ARCHIVE_ERRNO_FILE_FORMAT,
				    "Unexpected error when decoding "
				    "huffman tables");
				return ARCHIVE_FATAL;
			}
		} else {
			/* other codes: fill with zeroes `n` times */
			uint16_t n;

			if(ARCHIVE_OK != (ret = read_bits_16(a, rar, p, &n)))
				return ret;

			if(num == 18) {
				n >>= 13;
				n += 3;
				skip_bits(rar, 3);
			} else {
				n >>= 9;
				n += 11;
				skip_bits(rar, 7);
			}

			while(n-- > 0 && i < HUFF_TABLE_SIZE)
				table[i++] = 0;
		}
	}

	ret = create_decode_tables(&table[idx], &rar->cstate.ld, HUFF_NC);
	if(ret != ARCHIVE_OK) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
		     "Failed to create literal table");
		return ARCHIVE_FATAL;
	}

	idx += HUFF_NC;

	ret = create_decode_tables(&table[idx], &rar->cstate.dd, HUFF_DC);
	if(ret != ARCHIVE_OK) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
		    "Failed to create distance table");
		return ARCHIVE_FATAL;
	}

	idx += HUFF_DC;

	ret = create_decode_tables(&table[idx], &rar->cstate.ldd, HUFF_LDC);
	if(ret != ARCHIVE_OK) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
		    "Failed to create lower bits of distances table");
		return ARCHIVE_FATAL;
	}

	idx += HUFF_LDC;

	ret = create_decode_tables(&table[idx], &rar->cstate.rd, HUFF_RC);
	if(ret != ARCHIVE_OK) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
		    "Failed to create repeating distances table");
		return ARCHIVE_FATAL;
	}

	return ARCHIVE_OK;
}

/* Parses the block header, verifies its CRC byte, and saves the header
 * fields inside the `hdr` pointer. */
static int parse_block_header(struct archive_read* a, const uint8_t* p,
    ssize_t* block_size, struct compressed_block_header* hdr)
{
	uint8_t calculated_cksum;
	memcpy(hdr, p, sizeof(struct compressed_block_header));

	if(bf_byte_count(hdr) > 2) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
		    "Unsupported block header size (was %d, max is 2)",
		    bf_byte_count(hdr));
		return ARCHIVE_FATAL;
	}

	/* This should probably use bit reader interface in order to be more
	 * future-proof. */
	*block_size = 0;
	switch(bf_byte_count(hdr)) {
		/* 1-byte block size */
		case 0:
			*block_size = *(const uint8_t*) &p[2];
			break;

		/* 2-byte block size */
		case 1:
			*block_size = archive_le16dec(&p[2]);
			break;

		/* 3-byte block size */
		case 2:
			*block_size = archive_le32dec(&p[2]);
			*block_size &= 0x00FFFFFF;
			break;

		/* Other block sizes are not supported. This case is not
		 * reached, because we have an 'if' guard before the switch
		 * that makes sure of it. */
		default:
			return ARCHIVE_FATAL;
	}

	/* Verify the block header checksum. 0x5A is a magic value and is
	 * always * constant. */
	calculated_cksum = 0x5A
	    ^ (uint8_t) hdr->block_flags_u8
	    ^ (uint8_t) *block_size
	    ^ (uint8_t) (*block_size >> 8)
	    ^ (uint8_t) (*block_size >> 16);

	if(calculated_cksum != hdr->block_cksum) {
#ifndef DONT_FAIL_ON_CRC_ERROR
		archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
		    "Block checksum error: got 0x%x, expected 0x%x",
		    hdr->block_cksum, calculated_cksum);

		return ARCHIVE_FATAL;
#endif
	}

	return ARCHIVE_OK;
}

/* Convenience function used during filter processing. */
static int parse_filter_data(struct archive_read* a, struct rar5* rar,
	const uint8_t* p, uint32_t* filter_data)
{
	int i, bytes, ret;
	uint32_t data = 0;

	if(ARCHIVE_OK != (ret = read_consume_bits(a, rar, p, 2, &bytes)))
		return ret;

	bytes++;

	for(i = 0; i < bytes; i++) {
		uint16_t byte;

		if(ARCHIVE_OK != (ret = read_bits_16(a, rar, p, &byte))) {
			return ret;
		}

		/* Cast to uint32_t will ensure the shift operation will not
		 * produce undefined result. */
		data += ((uint32_t) byte >> 8) << (i * 8);
		skip_bits(rar, 8);
	}

	*filter_data = data;
	return ARCHIVE_OK;
}

/* Function is used during sanity checking. */
static int is_valid_filter_block_start(struct rar5* rar,
    uint32_t start)
{
	const int64_t block_start = (ssize_t) start + rar->cstate.write_ptr;
	const int64_t last_bs = rar->cstate.last_block_start;
	const ssize_t last_bl = rar->cstate.last_block_length;

	if(last_bs == 0 || last_bl == 0) {
		/* We didn't have any filters yet, so accept this offset. */
		return 1;
	}

	if(block_start >= last_bs + last_bl) {
		/* Current offset is bigger than last block's end offset, so
		 * accept current offset. */
		return 1;
	}

	/* Any other case is not a normal situation and we should fail. */
	return 0;
}

/* The function will create a new filter, read its parameters from the input
 * stream and add it to the filter collection. */
static int parse_filter(struct archive_read* ar, const uint8_t* p) {
	uint32_t block_start, block_length;
	uint16_t filter_type;
	struct filter_info* filt = NULL;
	struct rar5* rar = get_context(ar);
	int ret;

	/* Read the parameters from the input stream. */
	if(ARCHIVE_OK != (ret = parse_filter_data(ar, rar, p, &block_start)))
		return ret;

	if(ARCHIVE_OK != (ret = parse_filter_data(ar, rar, p, &block_length)))
		return ret;

	if(ARCHIVE_OK != (ret = read_bits_16(ar, rar, p, &filter_type)))
		return ret;

	filter_type >>= 13;
	skip_bits(rar, 3);

	/* Perform some sanity checks on this filter parameters. Note that we
	 * allow only DELTA, E8/E9 and ARM filters here, because rest of
	 * filters are not used in RARv5. */

	if(block_length < 4 ||
	    block_length > 0x400000 ||
	    filter_type > FILTER_ARM ||
	    !is_valid_filter_block_start(rar, block_start))
	{
		archive_set_error(&ar->archive, ARCHIVE_ERRNO_FILE_FORMAT,
		    "Invalid filter encountered");
		return ARCHIVE_FATAL;
	}

	/* Allocate a new filter. */
	filt = add_new_filter(rar);
	if(filt == NULL) {
		archive_set_error(&ar->archive, ENOMEM,
		    "Can't allocate memory for a filter descriptor.");
		return ARCHIVE_FATAL;
	}

	filt->type = filter_type;
	filt->block_start = rar->cstate.write_ptr + block_start;
	filt->block_length = block_length;

	rar->cstate.last_block_start = filt->block_start;
	rar->cstate.last_block_length = filt->block_length;

	/* Read some more data in case this is a DELTA filter. Other filter
	 * types don't require any additional data over what was already
	 * read. */
	if(filter_type == FILTER_DELTA) {
		int channels;

		if(ARCHIVE_OK != (ret = read_consume_bits(ar, rar, p, 5, &channels)))
			return ret;

		filt->channels = channels + 1;
	}

	return ARCHIVE_OK;
}

static int decode_code_length(struct archive_read* a, struct rar5* rar,
	const uint8_t* p, uint16_t code)
{
	int lbits, length = 2;

	if(code < 8) {
		lbits = 0;
		length += code;
	} else {
		lbits = code / 4 - 1;
		length += (4 | (code & 3)) << lbits;
	}

	if(lbits > 0) {
		int add;

		if(ARCHIVE_OK != read_consume_bits(a, rar, p, lbits, &add))
			return -1;

		length += add;
	}

	return length;
}

static int copy_string(struct archive_read* a, int len, int dist) {
	struct rar5* rar = get_context(a);
	const ssize_t cmask = rar->cstate.window_mask;
	const uint64_t write_ptr = rar->cstate.write_ptr +
	    rar->cstate.solid_offset;
	int i;

	if (rar->cstate.window_buf == NULL)
		return ARCHIVE_FATAL;

	/* The unpacker spends most of the time in this function. It would be
	 * a good idea to introduce some optimizations here.
	 *
	 * Just remember that this loop treats buffers that overlap differently
	 * than buffers that do not overlap. This is why a simple memcpy(3)
	 * call will not be enough. */

	for(i = 0; i < len; i++) {
		const ssize_t write_idx = (write_ptr + i) & cmask;
		const ssize_t read_idx = (write_ptr + i - dist) & cmask;
		rar->cstate.window_buf[write_idx] =
		    rar->cstate.window_buf[read_idx];
	}

	rar->cstate.write_ptr += len;
	return ARCHIVE_OK;
}

static int do_uncompress_block(struct archive_read* a, const uint8_t* p) {
	struct rar5* rar = get_context(a);
	uint16_t num;
	int ret;

	const uint64_t cmask = rar->cstate.window_mask;
	const struct compressed_block_header* hdr = &rar->last_block_hdr;
	const uint8_t bit_size = 1 + bf_bit_size(hdr);

	while(1) {
		if(rar->cstate.write_ptr - rar->cstate.last_write_ptr >
		    (rar->cstate.window_size >> 1)) {
			/* Don't allow growing data by more than half of the
			 * window size at a time. In such case, break the loop;
			 *  next call to this function will continue processing
			 *  from this moment. */
			break;
		}

		if(rar->bits.in_addr > rar->cstate.cur_block_size - 1 ||
		    (rar->bits.in_addr == rar->cstate.cur_block_size - 1 &&
		    rar->bits.bit_addr >= bit_size))
		{
			/* If the program counter is here, it means the
			 * function has finished processing the block. */
			rar->cstate.block_parsing_finished = 1;
			break;
		}

		/* Decode the next literal. */
		if(ARCHIVE_OK != decode_number(a, &rar->cstate.ld, p, &num)) {
			return ARCHIVE_EOF;
		}

		/* Num holds a decompression literal, or 'command code'.
		 *
		 * - Values lower than 256 are just bytes. Those codes
		 *   can be stored in the output buffer directly.
		 *
		 * - Code 256 defines a new filter, which is later used to
		 *   transform the data block accordingly to the filter type.
		 *   The data block needs to be fully uncompressed first.
		 *
		 * - Code bigger than 257 and smaller than 262 define
		 *   a repetition pattern that should be copied from
		 *   an already uncompressed chunk of data.
		 */

		if(num < 256) {
			/* Directly store the byte. */
			int64_t write_idx = rar->cstate.solid_offset +
			    rar->cstate.write_ptr++;

			rar->cstate.window_buf[write_idx & cmask] =
			    (uint8_t) num;
			continue;
		} else if(num >= 262) {
			uint16_t dist_slot;
			int len = decode_code_length(a, rar, p, num - 262),
				dbits,
				dist = 1;

			if(len == -1) {
				archive_set_error(&a->archive,
				    ARCHIVE_ERRNO_PROGRAMMER,
				    "Failed to decode the code length");

				return ARCHIVE_FATAL;
			}

			if(ARCHIVE_OK != decode_number(a, &rar->cstate.dd, p,
			    &dist_slot))
			{
				archive_set_error(&a->archive,
				    ARCHIVE_ERRNO_PROGRAMMER,
				    "Failed to decode the distance slot");

				return ARCHIVE_FATAL;
			}

			if(dist_slot < 4) {
				dbits = 0;
				dist += dist_slot;
			} else {
				dbits = dist_slot / 2 - 1;

				/* Cast to uint32_t will make sure the shift
				 * left operation won't produce undefined
				 * result. Then, the uint32_t type will
				 * be implicitly casted to int. */
				dist += (uint32_t) (2 |
				    (dist_slot & 1)) << dbits;
			}

			if(dbits > 0) {
				if(dbits >= 4) {
					uint32_t add = 0;
					uint16_t low_dist;

					if(dbits > 4) {
						if(ARCHIVE_OK != (ret = read_bits_32(
						    a, rar, p, &add))) {
							/* Return EOF if we
							 * can't read more
							 * data. */
							return ret;
						}

						skip_bits(rar, dbits - 4);
						add = (add >> (
						    36 - dbits)) << 4;
						dist += add;
					}

					if(ARCHIVE_OK != decode_number(a,
					    &rar->cstate.ldd, p, &low_dist))
					{
						archive_set_error(&a->archive,
						    ARCHIVE_ERRNO_PROGRAMMER,
						    "Failed to decode the "
						    "distance slot");

						return ARCHIVE_FATAL;
					}

					if(dist >= INT_MAX - low_dist - 1) {
						/* This only happens in
						 * invalid archives. */
						archive_set_error(&a->archive,
						    ARCHIVE_ERRNO_FILE_FORMAT,
						    "Distance pointer "
						    "overflow");
						return ARCHIVE_FATAL;
					}

					dist += low_dist;
				} else {
					/* dbits is one of [0,1,2,3] */
					int add;

					if(ARCHIVE_OK != (ret = read_consume_bits(a, rar,
					     p, dbits, &add))) {
						/* Return EOF if we can't read
						 * more data. */
						return ret;
					}

					dist += add;
				}
			}

			if(dist > 0x100) {
				len++;

				if(dist > 0x2000) {
					len++;

					if(dist > 0x40000) {
						len++;
					}
				}
			}

			dist_cache_push(rar, dist);
			rar->cstate.last_len = len;

			if(ARCHIVE_OK != copy_string(a, len, dist))
				return ARCHIVE_FATAL;

			continue;
		} else if(num == 256) {
			/* Create a filter. */
			ret = parse_filter(a, p);
			if(ret != ARCHIVE_OK)
				return ret;

			continue;
		} else if(num == 257) {
			if(rar->cstate.last_len != 0) {
				if(ARCHIVE_OK != copy_string(a,
				    rar->cstate.last_len,
				    rar->cstate.dist_cache[0]))
				{
					return ARCHIVE_FATAL;
				}
			}

			continue;
		} else {
			/* num < 262 */
			const int idx = num - 258;
			const int dist = dist_cache_touch(rar, idx);

			uint16_t len_slot;
			int len;

			if(ARCHIVE_OK != decode_number(a, &rar->cstate.rd, p,
			    &len_slot)) {
				return ARCHIVE_FATAL;
			}

			len = decode_code_length(a, rar, p, len_slot);
			if (len == -1) {
				return ARCHIVE_FATAL;
			}

			rar->cstate.last_len = len;

			if(ARCHIVE_OK != copy_string(a, len, dist))
				return ARCHIVE_FATAL;

			continue;
		}
	}

	return ARCHIVE_OK;
}

/* Binary search for the RARv5 signature. */
static int scan_for_signature(struct archive_read* a) {
	const uint8_t* p;
	const int chunk_size = 512;
	ssize_t i;
	char signature[sizeof(rar5_signature_xor)];

	/* If we're here, it means we're on an 'unknown territory' data.
	 * There's no indication what kind of data we're reading here.
	 * It could be some text comment, any kind of binary data,
	 * digital sign, dragons, etc.
	 *
	 * We want to find a valid RARv5 magic header inside this unknown
	 * data. */

	/* Is it possible in libarchive to just skip everything until the
	 * end of the file? If so, it would be a better approach than the
	 * current implementation of this function. */

	rar5_signature(signature);

	while(1) {
		if(!read_ahead(a, chunk_size, &p))
			return ARCHIVE_EOF;

		for(i = 0; i < chunk_size - (int)sizeof(rar5_signature_xor);
		    i++) {
			if(memcmp(&p[i], signature,
			    sizeof(rar5_signature_xor)) == 0) {
				/* Consume the number of bytes we've used to
				 * search for the signature, as well as the
				 * number of bytes used by the signature
				 * itself. After this we should be standing
				 * on a valid base block header. */
				(void) consume(a,
				    i + sizeof(rar5_signature_xor));
				return ARCHIVE_OK;
			}
		}

		consume(a, chunk_size);
	}

	return ARCHIVE_FATAL;
}

/* This function will switch the multivolume archive file to another file,
 * i.e. from part03 to part 04. */
static int advance_multivolume(struct archive_read* a) {
	int lret;
	struct rar5* rar = get_context(a);

	/* A small state machine that will skip unnecessary data, needed to
	 * switch from one multivolume to another. Such skipping is needed if
	 * we want to be an stream-oriented (instead of file-oriented)
	 * unpacker.
	 *
	 * The state machine starts with `rar->main.endarc` == 0. It also
	 * assumes that current stream pointer points to some base block
	 * header.
	 *
	 * The `endarc` field is being set when the base block parsing
	 * function encounters the 'end of archive' marker.
	 */

	while(1) {
		if(rar->main.endarc == 1) {
			int looping = 1;

			rar->main.endarc = 0;

			while(looping) {
				lret = skip_base_block(a);
				switch(lret) {
					case ARCHIVE_RETRY:
						/* Continue looping. */
						break;
					case ARCHIVE_OK:
						/* Break loop. */
						looping = 0;
						break;
					default:
						/* Forward any errors to the
						 * caller. */
						return lret;
				}
			}

			break;
		} else {
			/* Skip current base block. In order to properly skip
			 * it, we really need to simply parse it and discard
			 * the results. */

			lret = skip_base_block(a);
			if(lret == ARCHIVE_FATAL || lret == ARCHIVE_FAILED)
				return lret;

			/* The `skip_base_block` function tells us if we
			 * should continue with skipping, or we should stop
			 * skipping. We're trying to skip everything up to
			 * a base FILE block. */

			if(lret != ARCHIVE_RETRY) {
				/* If there was an error during skipping, or we
				 * have just skipped a FILE base block... */

				if(rar->main.endarc == 0) {
					return lret;
				} else {
					continue;
				}
			}
		}
	}

	return ARCHIVE_OK;
}

/* Merges the partial block from the first multivolume archive file, and
 * partial block from the second multivolume archive file. The result is
 * a chunk of memory containing the whole block, and the stream pointer
 * is advanced to the next block in the second multivolume archive file. */
static int merge_block(struct archive_read* a, ssize_t block_size,
    const uint8_t** p)
{
	struct rar5* rar = get_context(a);
	ssize_t cur_block_size, partial_offset = 0;
	const uint8_t* lp;
	int ret;

	if(rar->merge_mode) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_PROGRAMMER,
		    "Recursive merge is not allowed");

		return ARCHIVE_FATAL;
	}

	/* Set a flag that we're in the switching mode. */
	rar->cstate.switch_multivolume = 1;

	/* Reallocate the memory which will hold the whole block. */
	if(rar->vol.push_buf)
		free((void*) rar->vol.push_buf);

	/* Increasing the allocation block by 8 is due to bit reading functions,
	 * which are using additional 2 or 4 bytes. Allocating the block size
	 * by exact value would make bit reader perform reads from invalid
	 * memory block when reading the last byte from the buffer. */
	rar->vol.push_buf = malloc(block_size + 8);
	if(!rar->vol.push_buf) {
		archive_set_error(&a->archive, ENOMEM,
		    "Can't allocate memory for a merge block buffer.");
		return ARCHIVE_FATAL;
	}

	/* Valgrind complains if the extension block for bit reader is not
	 * initialized, so initialize it. */
	memset(&rar->vol.push_buf[block_size], 0, 8);

	/* A single block can span across multiple multivolume archive files,
	 * so we use a loop here. This loop will consume enough multivolume
	 * archive files until the whole block is read. */

	while(1) {
		/* Get the size of current block chunk in this multivolume
		 * archive file and read it. */
		cur_block_size = rar5_min(rar->file.bytes_remaining,
		    block_size - partial_offset);

		if(cur_block_size == 0) {
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_FILE_FORMAT,
			    "Encountered block size == 0 during block merge");
			return ARCHIVE_FATAL;
		}

		if(!read_ahead(a, cur_block_size, &lp))
			return ARCHIVE_EOF;

		/* Sanity check; there should never be a situation where this
		 * function reads more data than the block's size. */
		if(partial_offset + cur_block_size > block_size) {
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_PROGRAMMER,
			    "Consumed too much data when merging blocks.");
			return ARCHIVE_FATAL;
		}

		/* Merge previous block chunk with current block chunk,
		 * or create first block chunk if this is our first
		 * iteration. */
		memcpy(&rar->vol.push_buf[partial_offset], lp, cur_block_size);

		/* Advance the stream read pointer by this block chunk size. */
		if(ARCHIVE_OK != consume(a, cur_block_size))
			return ARCHIVE_EOF;

		/* Update the pointers. `partial_offset` contains information
		 * about the sum of merged block chunks. */
		partial_offset += cur_block_size;
		rar->file.bytes_remaining -= cur_block_size;

		/* If `partial_offset` is the same as `block_size`, this means
		 * we've merged all block chunks and we have a valid full
		 * block. */
		if(partial_offset == block_size) {
			break;
		}

		/* If we don't have any bytes to read, this means we should
		 * switch to another multivolume archive file. */
		if(rar->file.bytes_remaining == 0) {
			rar->merge_mode++;
			ret = advance_multivolume(a);
			rar->merge_mode--;
			if(ret != ARCHIVE_OK) {
				return ret;
			}
		}
	}

	*p = rar->vol.push_buf;

	/* If we're here, we can resume unpacking by processing the block
	 * pointed to by the `*p` memory pointer. */

	return ARCHIVE_OK;
}

static int process_block(struct archive_read* a) {
	const uint8_t* p;
	struct rar5* rar = get_context(a);
	int ret;

	/* If we don't have any data to be processed, this most probably means
	 * we need to switch to the next volume. */
	if(rar->main.volume && rar->file.bytes_remaining == 0) {
		ret = advance_multivolume(a);
		if(ret != ARCHIVE_OK)
			return ret;
	}

	if(rar->cstate.block_parsing_finished) {
		ssize_t block_size;
		ssize_t to_skip;
		ssize_t cur_block_size;

		/* The header size won't be bigger than 6 bytes. */
		if(!read_ahead(a, 6, &p)) {
			/* Failed to prefetch data block header. */
			return ARCHIVE_EOF;
		}

		/*
		 * Read block_size by parsing block header. Validate the header
		 * by calculating CRC byte stored inside the header. Size of
		 * the header is not constant (block size can be stored either
		 * in 1 or 2 bytes), that's why block size is left out from the
		 * `compressed_block_header` structure and returned by
		 * `parse_block_header` as the second argument. */

		ret = parse_block_header(a, p, &block_size,
		    &rar->last_block_hdr);
		if(ret != ARCHIVE_OK) {
			return ret;
		}

		/* Skip block header. Next data is huffman tables,
		 * if present. */
		to_skip = sizeof(struct compressed_block_header) +
			bf_byte_count(&rar->last_block_hdr) + 1;

		if(ARCHIVE_OK != consume(a, to_skip))
			return ARCHIVE_EOF;

		rar->file.bytes_remaining -= to_skip;

		/* The block size gives information about the whole block size,
		 * but the block could be stored in split form when using
		 * multi-volume archives. In this case, the block size will be
		 * bigger than the actual data stored in this file. Remaining
		 * part of the data will be in another file. */

		cur_block_size =
			rar5_min(rar->file.bytes_remaining, block_size);

		if(block_size > rar->file.bytes_remaining) {
			/* If current blocks' size is bigger than our data
			 * size, this means we have a multivolume archive.
			 * In this case, skip all base headers until the end
			 * of the file, proceed to next "partXXX.rar" volume,
			 * find its signature, skip all headers up to the first
			 * FILE base header, and continue from there.
			 *
			 * Note that `merge_block` will update the `rar`
			 * context structure quite extensively. */

			ret = merge_block(a, block_size, &p);
			if(ret != ARCHIVE_OK) {
				return ret;
			}

			cur_block_size = block_size;

			/* Current stream pointer should be now directly
			 * *after* the block that spanned through multiple
			 * archive files. `p` pointer should have the data of
			 * the *whole* block (merged from partial blocks
			 * stored in multiple archives files). */
		} else {
			rar->cstate.switch_multivolume = 0;

			/* Read the whole block size into memory. This can take
			 * up to  8 megabytes of memory in theoretical cases.
			 * Might be worth to optimize this and use a standard
			 * chunk of 4kb's. */
			if(!read_ahead(a, 4 + cur_block_size, &p)) {
				/* Failed to prefetch block data. */
				return ARCHIVE_EOF;
			}
		}

		rar->cstate.block_buf = p;
		rar->cstate.cur_block_size = cur_block_size;
		rar->cstate.block_parsing_finished = 0;

		rar->bits.in_addr = 0;
		rar->bits.bit_addr = 0;

		if(bf_is_table_present(&rar->last_block_hdr)) {
			/* Load Huffman tables. */
			ret = parse_tables(a, rar, p);
			if(ret != ARCHIVE_OK) {
				/* Error during decompression of Huffman
				 * tables. */
				return ret;
			}
		}
	} else {
		/* Block parsing not finished, reuse previous memory buffer. */
		p = rar->cstate.block_buf;
	}

	/* Uncompress the block, or a part of it, depending on how many bytes
	 * will be generated by uncompressing the block.
	 *
	 * In case too many bytes will be generated, calling this function
	 * again will resume the uncompression operation. */
	ret = do_uncompress_block(a, p);
	if(ret != ARCHIVE_OK) {
		return ret;
	}

	if(rar->cstate.block_parsing_finished &&
	    rar->cstate.switch_multivolume == 0 &&
	    rar->cstate.cur_block_size > 0)
	{
		/* If we're processing a normal block, consume the whole
		 * block. We can do this because we've already read the whole
		 * block to memory. */
		if(ARCHIVE_OK != consume(a, rar->cstate.cur_block_size))
			return ARCHIVE_FATAL;

		rar->file.bytes_remaining -= rar->cstate.cur_block_size;
	} else if(rar->cstate.switch_multivolume) {
		/* Don't consume the block if we're doing multivolume
		 * processing. The volume switching function will consume
		 * the proper count of bytes instead. */
		rar->cstate.switch_multivolume = 0;
	}

	return ARCHIVE_OK;
}

/* Pops the `buf`, `size` and `offset` from the "data ready" stack.
 *
 * Returns ARCHIVE_OK when those arguments can be used, ARCHIVE_RETRY
 * when there is no data on the stack. */
static int use_data(struct rar5* rar, const void** buf, size_t* size,
    int64_t* offset)
{
	int i;

	for(i = 0; i < rar5_countof(rar->cstate.dready); i++) {
		struct data_ready *d = &rar->cstate.dready[i];

		if(d->used) {
			if(buf)    *buf = d->buf;
			if(size)   *size = d->size;
			if(offset) *offset = d->offset;

			d->used = 0;
			return ARCHIVE_OK;
		}
	}

	return ARCHIVE_RETRY;
}

static void clear_data_ready_stack(struct rar5* rar) {
	memset(&rar->cstate.dready, 0, sizeof(rar->cstate.dready));
}

/* Pushes the `buf`, `size` and `offset` arguments to the rar->cstate.dready
 * FIFO stack. Those values will be popped from this stack by the `use_data`
 * function. */
static int push_data_ready(struct archive_read* a, struct rar5* rar,
    const uint8_t* buf, size_t size, int64_t offset)
{
	int i;

	/* Don't push if we're in skip mode. This is needed because solid
	 * streams need full processing even if we're skipping data. After
	 * fully processing the stream, we need to discard the generated bytes,
	 * because we're interested only in the side effect: building up the
	 * internal window circular buffer. This window buffer will be used
	 * later during unpacking of requested data. */
	if(rar->skip_mode)
		return ARCHIVE_OK;

	/* Sanity check. */
	if(offset != rar->file.last_offset + rar->file.last_size) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_PROGRAMMER,
		    "Sanity check error: output stream is not continuous");
		return ARCHIVE_FATAL;
	}

	for(i = 0; i < rar5_countof(rar->cstate.dready); i++) {
		struct data_ready* d = &rar->cstate.dready[i];
		if(!d->used) {
			d->used = 1;
			d->buf = buf;
			d->size = size;
			d->offset = offset;

			/* These fields are used only in sanity checking. */
			rar->file.last_offset = offset;
			rar->file.last_size = size;

			/* Calculate the checksum of this new block before
			 * submitting data to libarchive's engine. */
			update_crc(rar, d->buf, d->size);

			return ARCHIVE_OK;
		}
	}

	/* Program counter will reach this code if the `rar->cstate.data_ready`
	 * stack will be filled up so that no new entries will be allowed. The
	 * code shouldn't allow such situation to occur. So we treat this case
	 * as an internal error. */

	archive_set_error(&a->archive, ARCHIVE_ERRNO_PROGRAMMER,
	    "Error: premature end of data_ready stack");
	return ARCHIVE_FATAL;
}

/* This function uncompresses the data that is stored in the <FILE> base
 * block.
 *
 * The FILE base block looks like this:
 *
 * <header><huffman tables><block_1><block_2>...<block_n>
 *
 * The <header> is a block header, that is parsed in parse_block_header().
 * It's a "compressed_block_header" structure, containing metadata needed
 * to know when we should stop looking for more <block_n> blocks.
 *
 * <huffman tables> contain data needed to set up the huffman tables, needed
 * for the actual decompression.
 *
 * Each <block_n> consists of series of literals:
 *
 * <literal><literal><literal>...<literal>
 *
 * Those literals generate the uncompression data. They operate on a circular
 * buffer, sometimes writing raw data into it, sometimes referencing
 * some previous data inside this buffer, and sometimes declaring a filter
 * that will need to be executed on the data stored in the circular buffer.
 * It all depends on the literal that is used.
 *
 * Sometimes blocks produce output data, sometimes they don't. For example, for
 * some huge files that use lots of filters, sometimes a block is filled with
 * only filter declaration literals. Such blocks won't produce any data in the
 * circular buffer.
 *
 * Sometimes blocks will produce 4 bytes of data, and sometimes 1 megabyte,
 * because a literal can reference previously decompressed data. For example,
 * there can be a literal that says: 'append a byte 0xFE here', and after
 * it another literal can say 'append 1 megabyte of data from circular buffer
 * offset 0x12345'. This is how RAR format handles compressing repeated
 * patterns.
 *
 * The RAR compressor creates those literals and the actual efficiency of
 * compression depends on what those literals are. The literals can also
 * be seen as a kind of a non-turing-complete virtual machine that simply
 * tells the decompressor what it should do.
 * */

static int do_uncompress_file(struct archive_read* a) {
	struct rar5* rar = get_context(a);
	int ret;
	int64_t max_end_pos;

	if(!rar->cstate.initialized) {
		/* Don't perform full context reinitialization if we're
		 * processing a solid archive. */
		if(!rar->main.solid || !rar->cstate.window_buf) {
			init_unpack(rar);
		}

		rar->cstate.initialized = 1;
	}

	/* Don't allow extraction if window_size is invalid. */
	if(rar->cstate.window_size == 0) {
		archive_set_error(&a->archive,
			ARCHIVE_ERRNO_FILE_FORMAT,
			"Invalid window size declaration in this file");

		/* This should never happen in valid files. */
		return ARCHIVE_FATAL;
	}

	if(rar->cstate.all_filters_applied == 1) {
		/* We use while(1) here, but standard case allows for just 1
		 * iteration. The loop will iterate if process_block() didn't
		 * generate any data at all. This can happen if the block
		 * contains only filter definitions (this is common in big
		 * files). */
		while(1) {
			ret = process_block(a);
			if(ret == ARCHIVE_EOF || ret == ARCHIVE_FATAL)
				return ret;

			if(rar->cstate.last_write_ptr ==
			    rar->cstate.write_ptr) {
				/* The block didn't generate any new data,
				 * so just process a new block if this one
				 * wasn't the last block in the file. */
				if (bf_is_last_block(&rar->last_block_hdr)) {
					return ARCHIVE_EOF;
				}

				continue;
			}

			/* The block has generated some new data, so break
			 * the loop. */
			break;
		}
	}

	/* Try to run filters. If filters won't be applied, it means that
	 * insufficient data was generated. */
	ret = apply_filters(a);
	if(ret == ARCHIVE_RETRY) {
		return ARCHIVE_OK;
	} else if(ret == ARCHIVE_FATAL) {
		return ARCHIVE_FATAL;
	}

	/* If apply_filters() will return ARCHIVE_OK, we can continue here. */

	if(cdeque_size(&rar->cstate.filters) > 0) {
		/* Check if we can write something before hitting first
		 * filter. */
		struct filter_info* flt;

		/* Get the block_start offset from the first filter. */
		if(CDE_OK != cdeque_front(&rar->cstate.filters,
		    cdeque_filter_p(&flt)))
		{
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_PROGRAMMER,
			    "Can't read first filter");
			return ARCHIVE_FATAL;
		}

		max_end_pos = rar5_min(flt->block_start,
		    rar->cstate.write_ptr);
	} else {
		/* There are no filters defined, or all filters were applied.
		 * This means we can just store the data without any
		 * postprocessing. */
		max_end_pos = rar->cstate.write_ptr;
	}

	if(max_end_pos == rar->cstate.last_write_ptr) {
		/* We can't write anything yet. The block uncompression
		 * function did not generate enough data, and no filter can be
		 * applied. At the same time we don't have any data that can be
		 *  stored without filter postprocessing. This means we need to
		 *  wait for more data to be generated, so we can apply the
		 * filters.
		 *
		 * Signal the caller that we need more data to be able to do
		 * anything.
		 */
		return ARCHIVE_RETRY;
	} else {
		/* We can write the data before hitting the first filter.
		 * So let's do it. The push_window_data() function will
		 * effectively return the selected data block to the user
		 * application. */
		push_window_data(a, rar, rar->cstate.last_write_ptr,
		    max_end_pos);
		rar->cstate.last_write_ptr = max_end_pos;
	}

	return ARCHIVE_OK;
}

static int uncompress_file(struct archive_read* a) {
	int ret;

	while(1) {
		/* Sometimes the uncompression function will return a
		 * 'retry' signal. If this will happen, we have to retry
		 * the function. */
		ret = do_uncompress_file(a);
		if(ret != ARCHIVE_RETRY)
			return ret;
	}
}


static int do_unstore_file(struct archive_read* a,
    struct rar5* rar, const void** buf, size_t* size, int64_t* offset)
{
	size_t to_read;
	const uint8_t* p;

	if(rar->file.bytes_remaining == 0 && rar->main.volume > 0 &&
	    rar->generic.split_after > 0)
	{
		int ret;

		rar->cstate.switch_multivolume = 1;
		ret = advance_multivolume(a);
		rar->cstate.switch_multivolume = 0;

		if(ret != ARCHIVE_OK) {
			/* Failed to advance to next multivolume archive
			 * file. */
			return ret;
		}
	}

	to_read = rar5_min(rar->file.bytes_remaining, 64 * 1024);
	if(to_read == 0) {
		return ARCHIVE_EOF;
	}

	if(!read_ahead(a, to_read, &p)) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
		    "I/O error when unstoring file");
		return ARCHIVE_FATAL;
	}

	if(ARCHIVE_OK != consume(a, to_read)) {
		return ARCHIVE_EOF;
	}

	if(buf)    *buf = p;
	if(size)   *size = to_read;
	if(offset) *offset = rar->cstate.last_unstore_ptr;

	rar->file.bytes_remaining -= to_read;
	rar->cstate.last_unstore_ptr += to_read;

	update_crc(rar, p, to_read);
	return ARCHIVE_OK;
}

static int do_unpack(struct archive_read* a, struct rar5* rar,
    const void** buf, size_t* size, int64_t* offset)
{
	enum COMPRESSION_METHOD {
		STORE = 0, FASTEST = 1, FAST = 2, NORMAL = 3, GOOD = 4,
		BEST = 5
	};

	if(rar->file.service > 0) {
		return do_unstore_file(a, rar, buf, size, offset);
	} else {
		switch(rar->cstate.method) {
			case STORE:
				return do_unstore_file(a, rar, buf, size,
				    offset);
			case FASTEST:
				/* fallthrough */
			case FAST:
				/* fallthrough */
			case NORMAL:
				/* fallthrough */
			case GOOD:
				/* fallthrough */
			case BEST:
				/* No data is returned here. But because a sparse-file aware
				 * caller (like archive_read_data_into_fd) may treat zero-size
				 * as a sparse file block, we need to update the offset
				 * accordingly. At this point the decoder doesn't have any
				 * pending uncompressed data blocks, so the current position in
				 * the output file should be last_write_ptr. */
				if (offset) *offset = rar->cstate.last_write_ptr;
				return uncompress_file(a);
			default:
				archive_set_error(&a->archive,
				    ARCHIVE_ERRNO_FILE_FORMAT,
				    "Compression method not supported: 0x%x",
				    (unsigned int)rar->cstate.method);

				return ARCHIVE_FATAL;
		}
	}

#if !defined WIN32
	/* Not reached. */
	return ARCHIVE_OK;
#endif
}

static int verify_checksums(struct archive_read* a) {
	int verify_crc;
	struct rar5* rar = get_context(a);

	/* Check checksums only when actually unpacking the data. There's no
	 * need to calculate checksum when we're skipping data in solid archives
	 * (skipping in solid archives is the same thing as unpacking compressed
	 * data and discarding the result). */

	if(!rar->skip_mode) {
		/* Always check checksums if we're not in skip mode */
		verify_crc = 1;
	} else {
		/* We can override the logic above with a compile-time option
		 * NO_CRC_ON_SOLID_SKIP. This option is used during debugging,
		 * and it will check checksums of unpacked data even when
		 * we're skipping it. */

#if defined CHECK_CRC_ON_SOLID_SKIP
		/* Debug case */
		verify_crc = 1;
#else
		/* Normal case */
		verify_crc = 0;
#endif
	}

	if(verify_crc) {
		/* During unpacking, on each unpacked block we're calling the
		 * update_crc() function. Since we are here, the unpacking
		 * process is already over and we can check if calculated
		 * checksum (CRC32 or BLAKE2sp) is the same as what is stored
		 * in the archive. */
		if(rar->file.stored_crc32 > 0) {
			/* Check CRC32 only when the file contains a CRC32
			 * value for this file. */

			if(rar->file.calculated_crc32 !=
			    rar->file.stored_crc32) {
				/* Checksums do not match; the unpacked file
				 * is corrupted. */

				DEBUG_CODE {
					printf("Checksum error: CRC32 "
					    "(was: %08" PRIx32 ", expected: %08" PRIx32 ")\n",
					    rar->file.calculated_crc32,
					    rar->file.stored_crc32);
				}

#ifndef DONT_FAIL_ON_CRC_ERROR
				archive_set_error(&a->archive,
				    ARCHIVE_ERRNO_FILE_FORMAT,
				    "Checksum error: CRC32");
				return ARCHIVE_FATAL;
#endif
			} else {
				DEBUG_CODE {
					printf("Checksum OK: CRC32 "
					    "(%08" PRIx32 "/%08" PRIx32 ")\n",
					    rar->file.stored_crc32,
					    rar->file.calculated_crc32);
				}
			}
		}

		if(rar->file.has_blake2 > 0) {
			/* BLAKE2sp is an optional checksum algorithm that is
			 * added to RARv5 archives when using the `-htb` switch
			 *  during creation of archive.
			 *
			 * We now finalize the hash calculation by calling the
			 * `final` function. This will generate the final hash
			 * value we can use to compare it with the BLAKE2sp
			 * checksum that is stored in the archive.
			 *
			 * The return value of this `final` function is not
			 * very helpful, as it guards only against improper use.
 			 * This is why we're explicitly ignoring it. */

			uint8_t b2_buf[32];
			(void) blake2sp_final(&rar->file.b2state, b2_buf, 32);

			if(memcmp(&rar->file.blake2sp, b2_buf, 32) != 0) {
#ifndef DONT_FAIL_ON_CRC_ERROR
				archive_set_error(&a->archive,
				    ARCHIVE_ERRNO_FILE_FORMAT,
				    "Checksum error: BLAKE2");

				return ARCHIVE_FATAL;
#endif
			}
		}
	}

	/* Finalization for this file has been successfully completed. */
	return ARCHIVE_OK;
}

static int verify_global_checksums(struct archive_read* a) {
	return verify_checksums(a);
}

/*
 * Decryption function for the magic signature pattern. Check the comment near
 * the `rar5_signature_xor` symbol to read the rationale behind this.
 */
static void rar5_signature(char *buf) {
		size_t i;

		for(i = 0; i < sizeof(rar5_signature_xor); i++) {
			buf[i] = rar5_signature_xor[i] ^ 0xA1;
		}
}

static int rar5_read_data(struct archive_read *a, const void **buff,
    size_t *size, int64_t *offset) {
	int ret;
	struct rar5* rar = get_context(a);

	if (size)
		*size = 0;

	if (rar->has_encrypted_entries == ARCHIVE_READ_FORMAT_ENCRYPTION_DONT_KNOW) {
		rar->has_encrypted_entries = 0;
	}

	if (rar->headers_are_encrypted || rar->cstate.data_encrypted) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
		    "Reading encrypted data is not currently supported");
		return ARCHIVE_FATAL;
	}

	if(rar->file.dir > 0) {
		/* Don't process any data if this file entry was declared
		 * as a directory. This is needed, because entries marked as
		 * directory doesn't have any dictionary buffer allocated, so
		 * it's impossible to perform any decompression. */
		archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
		    "Can't decompress an entry marked as a directory");
		return ARCHIVE_FAILED;
	}

	if(!rar->skip_mode && (rar->cstate.last_write_ptr > rar->file.unpacked_size)) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_PROGRAMMER,
		    "Unpacker has written too many bytes");
		return ARCHIVE_FATAL;
	}

	ret = use_data(rar, buff, size, offset);
	if(ret == ARCHIVE_OK) {
		return ret;
	}

	if(rar->file.eof == 1) {
		return ARCHIVE_EOF;
	}

	ret = do_unpack(a, rar, buff, size, offset);
	if(ret != ARCHIVE_OK) {
		return ret;
	}

	if(rar->file.bytes_remaining == 0 &&
			rar->cstate.last_write_ptr == rar->file.unpacked_size)
	{
		/* If all bytes of current file were processed, run
		 * finalization.
		 *
		 * Finalization will check checksum against proper values. If
		 * some of the checksums will not match, we'll return an error
		 * value in the last `archive_read_data` call to signal an error
		 * to the user. */

		rar->file.eof = 1;
		return verify_global_checksums(a);
	}

	return ARCHIVE_OK;
}

static int rar5_read_data_skip(struct archive_read *a) {
	struct rar5* rar = get_context(a);

	if(rar->main.solid && (rar->cstate.data_encrypted == 0)) {
		/* In solid archives, instead of skipping the data, we need to
		 * extract it, and dispose the result. The side effect of this
		 * operation will be setting up the initial window buffer state
		 * needed to be able to extract the selected file. Note that
		 * this is only possible when data withing this solid block is
		 * not encrypted, in which case we'll skip and fail if the user
		 * tries to read data. */

		int ret;

		/* Make sure to process all blocks in the compressed stream. */
		while(rar->file.bytes_remaining > 0) {
			/* Setting the "skip mode" will allow us to skip
			 * checksum checks during data skipping. Checking the
			 * checksum of skipped data isn't really necessary and
			 * it's only slowing things down.
			 *
			 * This is incremented instead of setting to 1 because
			 * this data skipping function can be called
			 * recursively. */
			rar->skip_mode++;

			/* We're disposing 1 block of data, so we use triple
			 * NULLs in arguments. */
			ret = rar5_read_data(a, NULL, NULL, NULL);

			/* Turn off "skip mode". */
			rar->skip_mode--;

			if(ret < 0 || ret == ARCHIVE_EOF) {
				/* Propagate any potential error conditions
				 * to the caller. */
				return ret;
			}
		}
	} else {
		/* In standard archives, we can just jump over the compressed
		 * stream. Each file in non-solid archives starts from an empty
		 * window buffer. */

		if(ARCHIVE_OK != consume(a, rar->file.bytes_remaining)) {
			return ARCHIVE_FATAL;
		}

		rar->file.bytes_remaining = 0;
	}

	return ARCHIVE_OK;
}

static int64_t rar5_seek_data(struct archive_read *a, int64_t offset,
    int whence)
{
	(void) a;
	(void) offset;
	(void) whence;

	/* We're a streaming unpacker, and we don't support seeking. */

	return ARCHIVE_FATAL;
}

static int rar5_cleanup(struct archive_read *a) {
	struct rar5* rar = get_context(a);

	free(rar->cstate.window_buf);
	free(rar->cstate.filtered_buf);
	clear_data_ready_stack(rar);

	free(rar->vol.push_buf);

	free_filters(rar);
	cdeque_free(&rar->cstate.filters);

	free(rar);
	a->format->data = NULL;

	return ARCHIVE_OK;
}

static int rar5_capabilities(struct archive_read * a) {
	(void) a;
	return (ARCHIVE_READ_FORMAT_CAPS_ENCRYPT_DATA
			| ARCHIVE_READ_FORMAT_CAPS_ENCRYPT_METADATA);
}

static int rar5_has_encrypted_entries(struct archive_read *_a) {
	if (_a && _a->format) {
		struct rar5 *rar = (struct rar5 *)_a->format->data;
		if (rar) {
			return rar->has_encrypted_entries;
		}
	}

	return ARCHIVE_READ_FORMAT_ENCRYPTION_DONT_KNOW;
}

static int rar5_init(struct rar5* rar) {
	memset(rar, 0, sizeof(struct rar5));

	if(CDE_OK != cdeque_init(&rar->cstate.filters, 8192))
		return ARCHIVE_FATAL;

	/*
	 * Until enough data has been read, we cannot tell about
	 * any encrypted entries yet.
	 */
	rar->has_encrypted_entries = ARCHIVE_READ_FORMAT_ENCRYPTION_DONT_KNOW;

	return ARCHIVE_OK;
}

int archive_read_support_format_rar5(struct archive *_a) {
	struct archive_read* ar;
	int ret;
	struct rar5* rar;

	if(ARCHIVE_OK != (ret = get_archive_read(_a, &ar)))
		return ret;

	rar = malloc(sizeof(*rar));
	if(rar == NULL) {
		archive_set_error(&ar->archive, ENOMEM,
		    "Can't allocate rar5 data");
		return ARCHIVE_FATAL;
	}

	if(ARCHIVE_OK != rar5_init(rar)) {
		archive_set_error(&ar->archive, ENOMEM,
		    "Can't allocate rar5 filter buffer");
		free(rar);
		return ARCHIVE_FATAL;
	}

	ret = __archive_read_register_format(ar,
	    rar,
	    "rar5",
	    rar5_bid,
	    rar5_options,
	    rar5_read_header,
	    rar5_read_data,
	    rar5_read_data_skip,
	    rar5_seek_data,
	    rar5_cleanup,
	    rar5_capabilities,
	    rar5_has_encrypted_entries);

	if(ret != ARCHIVE_OK) {
		(void) rar5_cleanup(ar);
	}

	return ret;
}
#undef parse_filter
#undef DEBUG_CODE
#undef HUFF_BC
#undef HUFF_DC
#undef HUFF_LDC
#undef HUFF_NC
#undef HUFF_RC
#undef HUFF_TABLE_SIZE
#undef LOG
#undef MAX_NAME_IN_BYTES
#undef MAX_NAME_IN_CHARS
#undef OWNER_GROUP_GID
#undef OWNER_GROUP_NAME
#undef OWNER_MAXNAMELEN
#undef OWNER_USER_NAME
#undef OWNER_USER_UID
#undef REDIR_SYMLINK_IS_DIR
#undef rar5_countof
#undef rar5_max
#undef rar5_min

/* ==== archive_ppmd7.c ==== */
/* Ppmd7.c -- PPMdH codec
2010-03-12 : Igor Pavlov : Public domain
This code is based on PPMd var.H (2001): Dmitry Shkarin : Public domain */



#include <stdlib.h>



#ifdef PPMD_32BIT
  #define Ppmd7_GetPtr(p, ptr) (ptr)
  #define Ppmd7_GetContext(p, ptr) (ptr)
  #define Ppmd7_GetStats(p, ctx) ((ctx)->Stats)
#else
  #define Ppmd7_GetPtr(p, offs) ((void *)((p)->Base + (offs)))
  #define Ppmd7_GetContext(p, offs) ((CPpmd7_Context *)Ppmd7_GetPtr((p), (offs)))
  #define Ppmd7_GetStats(p, ctx) ((CPpmd_State *)Ppmd7_GetPtr((p), ((ctx)->Stats)))
#endif

#define Ppmd7_GetBinSumm(p) \
    &p->BinSumm[Ppmd7Context_OneState(p->MinContext)->Freq - 1][p->PrevSuccess + \
    p->NS2BSIndx[Ppmd7_GetContext(p, p->MinContext->Suffix)->NumStats - 1] + \
    (p->HiBitsFlag = p->HB2Flag[p->FoundState->Symbol]) + \
    2 * p->HB2Flag[Ppmd7Context_OneState(p->MinContext)->Symbol] + \
    ((p->RunLength >> 26) & 0x20)]

#define kTopValue (1 << 24)
#define MAX_FREQ 124
#define UNIT_SIZE 12

#define U2B(nu) ((UInt32)(nu) * UNIT_SIZE)
#define U2I(nu) (p->Units2Indx[(nu) - 1])
#define I2U(indx) (p->Indx2Units[indx])

#ifdef PPMD_32BIT
  #define REF(ptr) (ptr)
#else
  #define REF(ptr) ((UInt32)((Byte *)(ptr) - (p)->Base))
#endif

#define STATS_REF(ptr) ((CPpmd_State_Ref)REF(ptr))

#define CTX(ref) ((CPpmd7_Context *)Ppmd7_GetContext(p, ref))
#define STATS(ctx) Ppmd7_GetStats(p, ctx)
#define ONE_STATE(ctx) Ppmd7Context_OneState(ctx)
#define SUFFIX(ctx) CTX((ctx)->Suffix)

static const UInt16 kInitBinEsc[] = { 0x3CDD, 0x1F3F, 0x59BF, 0x48F3, 0x64A1, 0x5ABC, 0x6632, 0x6051};
static const Byte PPMD7_kExpEscape[16] = { 25, 14, 9, 7, 5, 5, 4, 4, 4, 3, 3, 3, 2, 2, 2, 2 };

typedef CPpmd7_Context * CTX_PTR;

struct CPpmd7_Node_;

typedef
  #ifdef PPMD_32BIT
    struct CPpmd7_Node_ *
  #else
    UInt32
  #endif
  CPpmd7_Node_Ref;

typedef struct CPpmd7_Node_
{
  UInt16 Stamp; /* must be at offset 0 as CPpmd7_Context::NumStats. Stamp=0 means free */
  UInt16 NU;
  CPpmd7_Node_Ref Next; /* must be at offset >= 4 */
  CPpmd7_Node_Ref Prev;
} CPpmd7_Node;

#ifdef PPMD_32BIT
  #define NODE(ptr) (ptr)
#else
  #define NODE(offs) ((CPpmd7_Node *)(p->Base + (offs)))
#endif

static void Ppmd7_Update1(CPpmd7 *p);
static void Ppmd7_Update1_0(CPpmd7 *p);
static void Ppmd7_Update2(CPpmd7 *p);
static void Ppmd7_UpdateBin(CPpmd7 *p);
static CPpmd_See *Ppmd7_MakeEscFreq(CPpmd7 *p, unsigned numMasked,
                                    UInt32 *scale);

/* ----------- Base ----------- */

static void Ppmd7_Construct(CPpmd7 *p)
{
  unsigned i, k, m;

  p->Base = 0;

  for (i = 0, k = 0; i < PPMD_NUM_INDEXES; i++)
  {
    unsigned step = (i >= 12 ? 4 : (i >> 2) + 1);
    do { p->Units2Indx[k++] = (Byte)i; } while(--step);
    p->Indx2Units[i] = (Byte)k;
  }

  p->NS2BSIndx[0] = (0 << 1);
  p->NS2BSIndx[1] = (1 << 1);
  memset(p->NS2BSIndx + 2, (2 << 1), 9);
  memset(p->NS2BSIndx + 11, (3 << 1), 256 - 11);

  for (i = 0; i < 3; i++)
    p->NS2Indx[i] = (Byte)i;
  for (m = i, k = 1; i < 256; i++)
  {
    p->NS2Indx[i] = (Byte)m;
    if (--k == 0)
      k = (++m) - 2;
  }

  memset(p->HB2Flag, 0, 0x40);
  memset(p->HB2Flag + 0x40, 8, 0x100 - 0x40);
}

static void Ppmd7_Free(CPpmd7 *p)
{
  free(p->Base);
  p->Size = 0;
  p->Base = 0;
}

static Bool Ppmd7_Alloc(CPpmd7 *p, UInt32 size)
{
  if (p->Base == 0 || p->Size != size)
  {
    /* RestartModel() below assumes that p->Size >= UNIT_SIZE
       (see the calculation of m->MinContext). */
    if (size < UNIT_SIZE) {
      return False;
    }
    Ppmd7_Free(p);
    p->AlignOffset =
      #ifdef PPMD_32BIT
        (4 - size) & 3;
      #else
        4 - (size & 3);
      #endif
    if ((p->Base = malloc(p->AlignOffset + size
        #ifndef PPMD_32BIT
        + UNIT_SIZE
        #endif
        )) == 0)
      return False;
    p->Size = size;
  }
  return True;
}

static void InsertNode(CPpmd7 *p, void *node, unsigned indx)
{
  *((CPpmd_Void_Ref *)node) = p->FreeList[indx];
  p->FreeList[indx] = REF(node);
}

static void *RemoveNode(CPpmd7 *p, unsigned indx)
{
  CPpmd_Void_Ref *node = (CPpmd_Void_Ref *)Ppmd7_GetPtr(p, p->FreeList[indx]);
  p->FreeList[indx] = *node;
  return node;
}

static void SplitBlock(CPpmd7 *p, void *ptr, unsigned oldIndx, unsigned newIndx)
{
  unsigned i, nu = I2U(oldIndx) - I2U(newIndx);
  ptr = (Byte *)ptr + U2B(I2U(newIndx));
  if (I2U(i = U2I(nu)) != nu)
  {
    unsigned k = I2U(--i);
    InsertNode(p, ((Byte *)ptr) + U2B(k), nu - k - 1);
  }
  InsertNode(p, ptr, i);
}

static void GlueFreeBlocks(CPpmd7 *p)
{
  #ifdef PPMD_32BIT
  CPpmd7_Node headItem;
  CPpmd7_Node_Ref head = &headItem;
  #else
  CPpmd7_Node_Ref head = p->AlignOffset + p->Size;
  #endif
  
  CPpmd7_Node_Ref n = head;
  unsigned i;

  p->GlueCount = 255;

  /* create doubly-linked list of free blocks */
  for (i = 0; i < PPMD_NUM_INDEXES; i++)
  {
    UInt16 nu = I2U(i);
    CPpmd7_Node_Ref next = (CPpmd7_Node_Ref)p->FreeList[i];
    p->FreeList[i] = 0;
    while (next != 0)
    {
      CPpmd7_Node *node = NODE(next);
      node->Next = n;
      n = NODE(n)->Prev = next;
      next = *(const CPpmd7_Node_Ref *)node;
      node->Stamp = 0;
      node->NU = (UInt16)nu;
    }
  }
  NODE(head)->Stamp = 1;
  NODE(head)->Next = n;
  NODE(n)->Prev = head;
  if (p->LoUnit != p->HiUnit)
    ((CPpmd7_Node *)p->LoUnit)->Stamp = 1;
  
  /* Glue free blocks */
  while (n != head)
  {
    CPpmd7_Node *node = NODE(n);
    UInt32 nu = (UInt32)node->NU;
    for (;;)
    {
      CPpmd7_Node *node2 = NODE(n) + nu;
      nu += node2->NU;
      if (node2->Stamp != 0 || nu >= 0x10000)
        break;
      NODE(node2->Prev)->Next = node2->Next;
      NODE(node2->Next)->Prev = node2->Prev;
      node->NU = (UInt16)nu;
    }
    n = node->Next;
  }
  
  /* Fill lists of free blocks */
  for (n = NODE(head)->Next; n != head;)
  {
    CPpmd7_Node *node = NODE(n);
    unsigned nu;
    CPpmd7_Node_Ref next = node->Next;
    for (nu = node->NU; nu > 128; nu -= 128, node += 128)
      InsertNode(p, node, PPMD_NUM_INDEXES - 1);
    if (I2U(i = U2I(nu)) != nu)
    {
      unsigned k = I2U(--i);
      InsertNode(p, node + k, nu - k - 1);
    }
    InsertNode(p, node, i);
    n = next;
  }
}

static void *AllocUnitsRare(CPpmd7 *p, unsigned indx)
{
  unsigned i;
  void *retVal;
  if (p->GlueCount == 0)
  {
    GlueFreeBlocks(p);
    if (p->FreeList[indx] != 0)
      return RemoveNode(p, indx);
  }
  i = indx;
  do
  {
    if (++i == PPMD_NUM_INDEXES)
    {
      UInt32 numBytes = U2B(I2U(indx));
      p->GlueCount--;
      return ((UInt32)(p->UnitsStart - p->Text) > numBytes) ? (p->UnitsStart -= numBytes) : (NULL);
    }
  }
  while (p->FreeList[i] == 0);
  retVal = RemoveNode(p, i);
  SplitBlock(p, retVal, i, indx);
  return retVal;
}

static void *AllocUnits(CPpmd7 *p, unsigned indx)
{
  UInt32 numBytes;
  if (p->FreeList[indx] != 0)
    return RemoveNode(p, indx);
  numBytes = U2B(I2U(indx));
  if (numBytes <= (UInt32)(p->HiUnit - p->LoUnit))
  {
    void *retVal = p->LoUnit;
    p->LoUnit += numBytes;
    return retVal;
  }
  return AllocUnitsRare(p, indx);
}

#define MyMem12Cpy(dest, src, num) do {					\
	UInt32 *d = (UInt32 *)dest;					\
	const UInt32 *s = (const UInt32 *)src;				\
	UInt32 n = num;							\
	do {								\
		d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; s += 3; d += 3;	\
	} while(--n);							\
} while (0)

static void *ShrinkUnits(CPpmd7 *p, void *oldPtr, unsigned oldNU, unsigned newNU)
{
  unsigned i0 = U2I(oldNU);
  unsigned i1 = U2I(newNU);
  if (i0 == i1)
    return oldPtr;
  if (p->FreeList[i1] != 0)
  {
    void *ptr = RemoveNode(p, i1);
    MyMem12Cpy(ptr, oldPtr, newNU);
    InsertNode(p, oldPtr, i0);
    return ptr;
  }
  SplitBlock(p, oldPtr, i0, i1);
  return oldPtr;
}

#define SUCCESSOR(p) ((CPpmd_Void_Ref)((p)->SuccessorLow | ((UInt32)(p)->SuccessorHigh << 16)))

static void SetSuccessor(CPpmd_State *p, CPpmd_Void_Ref v)
{
  (p)->SuccessorLow = (UInt16)((UInt32)(v) & 0xFFFF);
  (p)->SuccessorHigh = (UInt16)(((UInt32)(v) >> 16) & 0xFFFF);
}

static void RestartModel(CPpmd7 *p)
{
  unsigned i, k, m;

  memset(p->FreeList, 0, sizeof(p->FreeList));
  p->Text = p->Base + p->AlignOffset;
  p->HiUnit = p->Text + p->Size;
  p->LoUnit = p->UnitsStart = p->HiUnit - p->Size / 8 / UNIT_SIZE * 7 * UNIT_SIZE;
  p->GlueCount = 0;

  p->OrderFall = p->MaxOrder;
  p->RunLength = p->InitRL = -(Int32)((p->MaxOrder < 12) ? p->MaxOrder : 12) - 1;
  p->PrevSuccess = 0;

  p->MinContext = p->MaxContext = (CTX_PTR)(p->HiUnit -= UNIT_SIZE); /* AllocContext(p); */
  p->MinContext->Suffix = 0;
  p->MinContext->NumStats = 256;
  p->MinContext->SummFreq = 256 + 1;
  p->FoundState = (CPpmd_State *)p->LoUnit; /* AllocUnits(p, PPMD_NUM_INDEXES - 1); */
  p->LoUnit += U2B(256 / 2);
  p->MinContext->Stats = REF(p->FoundState);
  for (i = 0; i < 256; i++)
  {
    CPpmd_State *s = &p->FoundState[i];
    s->Symbol = (Byte)i;
    s->Freq = 1;
    SetSuccessor(s, 0);
  }

  for (i = 0; i < 128; i++)
    for (k = 0; k < 8; k++)
    {
      UInt16 *dest = p->BinSumm[i] + k;
      UInt16 val = (UInt16)(PPMD_BIN_SCALE - kInitBinEsc[k] / (i + 2));
      for (m = 0; m < 64; m += 8)
        dest[m] = val;
    }
  
  for (i = 0; i < 25; i++)
    for (k = 0; k < 16; k++)
    {
      CPpmd_See *s = &p->See[i][k];
      s->Summ = (UInt16)((5 * i + 10) << (s->Shift = PPMD_PERIOD_BITS - 4));
      s->Count = 4;
    }
}

static void Ppmd7_Init(CPpmd7 *p, unsigned maxOrder)
{
  p->MaxOrder = maxOrder;
  RestartModel(p);
  p->DummySee.Shift = PPMD_PERIOD_BITS;
  p->DummySee.Summ = 0; /* unused */
  p->DummySee.Count = 64; /* unused */
}

static CTX_PTR CreateSuccessors(CPpmd7 *p, Bool skip)
{
  CPpmd_State upState;
  CTX_PTR c = p->MinContext;
  CPpmd_Byte_Ref upBranch = (CPpmd_Byte_Ref)SUCCESSOR(p->FoundState);
  CPpmd_State *ps[PPMD7_MAX_ORDER];
  unsigned numPs = 0;
  
  if (!skip)
    ps[numPs++] = p->FoundState;
  
  while (c->Suffix)
  {
    CPpmd_Void_Ref successor;
    CPpmd_State *s;
    c = SUFFIX(c);
    if (c->NumStats != 1)
    {
      for (s = STATS(c); s->Symbol != p->FoundState->Symbol; s++);
    }
    else
      s = ONE_STATE(c);
    successor = SUCCESSOR(s);
    if (successor != upBranch)
    {
      c = CTX(successor);
      if (numPs == 0)
        return c;
      break;
    }
    ps[numPs++] = s;
  }
  
  upState.Symbol = *(const Byte *)Ppmd7_GetPtr(p, upBranch);
  SetSuccessor(&upState, upBranch + 1);
  
  if (c->NumStats == 1)
    upState.Freq = ONE_STATE(c)->Freq;
  else
  {
    UInt32 cf, s0;
    CPpmd_State *s;
    for (s = STATS(c); s->Symbol != upState.Symbol; s++);
    cf = s->Freq - 1;
    s0 = c->SummFreq - c->NumStats - cf;
    upState.Freq = (Byte)(1 + ((2 * cf <= s0) ? (5 * cf > s0) : ((2 * cf + 3 * s0 - 1) / (2 * s0))));
  }

  while (numPs != 0)
  {
    /* Create Child */
    CTX_PTR c1; /* = AllocContext(p); */
    if (p->HiUnit != p->LoUnit)
      c1 = (CTX_PTR)(p->HiUnit -= UNIT_SIZE);
    else if (p->FreeList[0] != 0)
      c1 = (CTX_PTR)RemoveNode(p, 0);
    else
    {
      c1 = (CTX_PTR)AllocUnitsRare(p, 0);
      if (!c1)
        return NULL;
    }
    c1->NumStats = 1;
    *ONE_STATE(c1) = upState;
    c1->Suffix = REF(c);
    SetSuccessor(ps[--numPs], REF(c1));
    c = c1;
  }
  
  return c;
}

static void SwapStates(CPpmd_State *t1, CPpmd_State *t2)
{
  CPpmd_State tmp = *t1;
  *t1 = *t2;
  *t2 = tmp;
}

static void UpdateModel(CPpmd7 *p)
{
  CPpmd_Void_Ref successor, fSuccessor = SUCCESSOR(p->FoundState);
  CTX_PTR c;
  unsigned s0, ns;
  
  if (p->FoundState->Freq < MAX_FREQ / 4 && p->MinContext->Suffix != 0)
  {
    c = SUFFIX(p->MinContext);
    
    if (c->NumStats == 1)
    {
      CPpmd_State *s = ONE_STATE(c);
      if (s->Freq < 32)
        s->Freq++;
    }
    else
    {
      CPpmd_State *s = STATS(c);
      if (s->Symbol != p->FoundState->Symbol)
      {
        do { s++; } while (s->Symbol != p->FoundState->Symbol);
        if (s[0].Freq >= s[-1].Freq)
        {
          SwapStates(&s[0], &s[-1]);
          s--;
        }
      }
      if (s->Freq < MAX_FREQ - 9)
      {
        s->Freq += 2;
        c->SummFreq += 2;
      }
    }
  }

  if (p->OrderFall == 0)
  {
    p->MinContext = p->MaxContext = CreateSuccessors(p, True);
    if (p->MinContext == 0)
    {
      RestartModel(p);
      return;
    }
    SetSuccessor(p->FoundState, REF(p->MinContext));
    return;
  }
  
  *p->Text++ = p->FoundState->Symbol;
  successor = REF(p->Text);
  if (p->Text >= p->UnitsStart)
  {
    RestartModel(p);
    return;
  }
  
  if (fSuccessor)
  {
    if (fSuccessor <= successor)
    {
      CTX_PTR cs = CreateSuccessors(p, False);
      if (cs == NULL)
      {
        RestartModel(p);
        return;
      }
      fSuccessor = REF(cs);
    }
    if (--p->OrderFall == 0)
    {
      successor = fSuccessor;
      p->Text -= (p->MaxContext != p->MinContext);
    }
  }
  else
  {
    SetSuccessor(p->FoundState, successor);
    fSuccessor = REF(p->MinContext);
  }
  
  s0 = p->MinContext->SummFreq - (ns = p->MinContext->NumStats) - (p->FoundState->Freq - 1);
  
  for (c = p->MaxContext; c != p->MinContext; c = SUFFIX(c))
  {
    unsigned ns1;
    UInt32 cf, sf;
    if ((ns1 = c->NumStats) != 1)
    {
      if ((ns1 & 1) == 0)
      {
        /* Expand for one UNIT */
        unsigned oldNU = ns1 >> 1;
        unsigned i = U2I(oldNU);
        if (i != U2I(oldNU + 1))
        {
          void *ptr = AllocUnits(p, i + 1);
          void *oldPtr;
          if (!ptr)
          {
            RestartModel(p);
            return;
          }
          oldPtr = STATS(c);
          MyMem12Cpy(ptr, oldPtr, oldNU);
          InsertNode(p, oldPtr, i);
          c->Stats = STATS_REF(ptr);
        }
      }
      c->SummFreq = (UInt16)(c->SummFreq + (2 * ns1 < ns) + 2 * ((4 * ns1 <= ns) & (c->SummFreq <= 8 * ns1)));
    }
    else
    {
      CPpmd_State *s = (CPpmd_State*)AllocUnits(p, 0);
      if (!s)
      {
        RestartModel(p);
        return;
      }
      *s = *ONE_STATE(c);
      c->Stats = REF(s);
      if (s->Freq < MAX_FREQ / 4 - 1)
        s->Freq <<= 1;
      else
        s->Freq = MAX_FREQ - 4;
      c->SummFreq = (UInt16)(s->Freq + p->InitEsc + (ns > 3));
    }
    cf = 2 * (UInt32)p->FoundState->Freq * (c->SummFreq + 6);
    sf = (UInt32)s0 + c->SummFreq;
    if (cf < 6 * sf)
    {
      cf = 1 + (cf > sf) + (cf >= 4 * sf);
      c->SummFreq += 3;
    }
    else
    {
      cf = 4 + (cf >= 9 * sf) + (cf >= 12 * sf) + (cf >= 15 * sf);
      c->SummFreq = (UInt16)(c->SummFreq + cf);
    }
    {
      CPpmd_State *s = STATS(c) + ns1;
      SetSuccessor(s, successor);
      s->Symbol = p->FoundState->Symbol;
      s->Freq = (Byte)cf;
      c->NumStats = (UInt16)(ns1 + 1);
    }
  }
  p->MaxContext = p->MinContext = CTX(fSuccessor);
}
  
static void Rescale(CPpmd7 *p)
{
  unsigned i, adder, sumFreq, escFreq;
  CPpmd_State *stats = STATS(p->MinContext);
  CPpmd_State *s = p->FoundState;
  {
    CPpmd_State tmp = *s;
    for (; s != stats; s--)
      s[0] = s[-1];
    *s = tmp;
  }
  escFreq = p->MinContext->SummFreq - s->Freq;
  s->Freq += 4;
  adder = (p->OrderFall != 0);
  s->Freq = (Byte)((s->Freq + adder) >> 1);
  sumFreq = s->Freq;
  
  i = p->MinContext->NumStats - 1;
  do
  {
    escFreq -= (++s)->Freq;
    s->Freq = (Byte)((s->Freq + adder) >> 1);
    sumFreq += s->Freq;
    if (s[0].Freq > s[-1].Freq)
    {
      CPpmd_State *s1 = s;
      CPpmd_State tmp = *s1;
      do
        s1[0] = s1[-1];
      while (--s1 != stats && tmp.Freq > s1[-1].Freq);
      *s1 = tmp;
    }
  }
  while (--i);
  
  if (s->Freq == 0)
  {
    unsigned numStats = p->MinContext->NumStats;
    unsigned n0, n1;
    do { i++; } while ((--s)->Freq == 0);
    escFreq += i;
    p->MinContext->NumStats = (UInt16)(p->MinContext->NumStats - i);
    if (p->MinContext->NumStats == 1)
    {
      CPpmd_State tmp = *stats;
      do
      {
        tmp.Freq = (Byte)(tmp.Freq - (tmp.Freq >> 1));
        escFreq >>= 1;
      }
      while (escFreq > 1);
      InsertNode(p, stats, U2I(((numStats + 1) >> 1)));
      *(p->FoundState = ONE_STATE(p->MinContext)) = tmp;
      return;
    }
    n0 = (numStats + 1) >> 1;
    n1 = (p->MinContext->NumStats + 1) >> 1;
    if (n0 != n1)
      p->MinContext->Stats = STATS_REF(ShrinkUnits(p, stats, n0, n1));
  }
  p->MinContext->SummFreq = (UInt16)(sumFreq + escFreq - (escFreq >> 1));
  p->FoundState = STATS(p->MinContext);
}

static CPpmd_See *Ppmd7_MakeEscFreq(CPpmd7 *p, unsigned numMasked, UInt32 *escFreq)
{
  CPpmd_See *see;
  unsigned nonMasked = p->MinContext->NumStats - numMasked;
  if (p->MinContext->NumStats != 256)
  {
    see = p->See[p->NS2Indx[nonMasked - 1]] +
        (nonMasked < (unsigned)SUFFIX(p->MinContext)->NumStats - p->MinContext->NumStats) +
        2 * (p->MinContext->SummFreq < 11 * p->MinContext->NumStats) +
        4 * (numMasked > nonMasked) +
        p->HiBitsFlag;
    {
      unsigned r = (see->Summ >> see->Shift);
      see->Summ = (UInt16)(see->Summ - r);
      *escFreq = r + (r == 0);
    }
  }
  else
  {
    see = &p->DummySee;
    *escFreq = 1;
  }
  return see;
}

static void NextContext(CPpmd7 *p)
{
  CTX_PTR c = CTX(SUCCESSOR(p->FoundState));
  if (p->OrderFall == 0 && (Byte *)c > p->Text)
    p->MinContext = p->MaxContext = c;
  else
    UpdateModel(p);
}

static void Ppmd7_Update1(CPpmd7 *p)
{
  CPpmd_State *s = p->FoundState;
  s->Freq += 4;
  p->MinContext->SummFreq += 4;
  if (s[0].Freq > s[-1].Freq)
  {
    SwapStates(&s[0], &s[-1]);
    p->FoundState = --s;
    if (s->Freq > MAX_FREQ)
      Rescale(p);
  }
  NextContext(p);
}

static void Ppmd7_Update1_0(CPpmd7 *p)
{
  p->PrevSuccess = (2 * p->FoundState->Freq > p->MinContext->SummFreq);
  p->RunLength += p->PrevSuccess;
  p->MinContext->SummFreq += 4;
  if ((p->FoundState->Freq += 4) > MAX_FREQ)
    Rescale(p);
  NextContext(p);
}

static void Ppmd7_UpdateBin(CPpmd7 *p)
{
  p->FoundState->Freq = (Byte)(p->FoundState->Freq + (p->FoundState->Freq < 128 ? 1: 0));
  p->PrevSuccess = 1;
  p->RunLength++;
  NextContext(p);
}

static void Ppmd7_Update2(CPpmd7 *p)
{
  p->MinContext->SummFreq += 4;
  if ((p->FoundState->Freq += 4) > MAX_FREQ)
    Rescale(p);
  p->RunLength = p->InitRL;
  UpdateModel(p);
}

/* ---------- Decode ---------- */

static Bool Ppmd_RangeDec_Init(CPpmd7z_RangeDec *p)
{
  unsigned i;
  p->Low = p->Bottom = 0;
  p->Range = 0xFFFFFFFF;
  for (i = 0; i < 4; i++)
    p->Code = (p->Code << 8) | p->Stream->Read((void *)p->Stream);
  return (p->Code < 0xFFFFFFFF);
}

static Bool Ppmd7z_RangeDec_Init(CPpmd7z_RangeDec *p)
{
  if (p->Stream->Read((void *)p->Stream) != 0)
    return False;
  return Ppmd_RangeDec_Init(p);
}

static Bool PpmdRAR_RangeDec_Init(CPpmd7z_RangeDec *p)
{
  if (!Ppmd_RangeDec_Init(p))
    return False;
  p->Bottom = 0x8000;
  return True;
}

static UInt32 Range_GetThreshold(void *pp, UInt32 total)
{
  CPpmd7z_RangeDec *p = (CPpmd7z_RangeDec *)pp;
  return (p->Code - p->Low) / (p->Range /= total);
}

static void Range_Normalize(CPpmd7z_RangeDec *p)
{
  while (1)
  {
    if((p->Low ^ (p->Low + p->Range)) >= kTopValue)
    {
      if(p->Range >= p->Bottom)
        break;
      else
        p->Range = ((uint32_t)(-(int32_t)p->Low)) & (p->Bottom - 1);
    }
    p->Code = (p->Code << 8) | p->Stream->Read((void *)p->Stream);
    p->Range <<= 8;
    p->Low <<= 8;
  }
}

static void Range_Decode_7z(void *pp, UInt32 start, UInt32 size)
{
  CPpmd7z_RangeDec *p = (CPpmd7z_RangeDec *)pp;
  p->Code -= start * p->Range;
  p->Range *= size;
  Range_Normalize(p);
}

static void Range_Decode_RAR(void *pp, UInt32 start, UInt32 size)
{
  CPpmd7z_RangeDec *p = (CPpmd7z_RangeDec *)pp;
  p->Low += start * p->Range;
  p->Range *= size;
  Range_Normalize(p);
}

static UInt32 Range_DecodeBit_7z(void *pp, UInt32 size0)
{
  CPpmd7z_RangeDec *p = (CPpmd7z_RangeDec *)pp;
  UInt32 newBound = (p->Range >> 14) * size0;
  UInt32 symbol;
  if (p->Code < newBound)
  {
    symbol = 0;
    p->Range = newBound;
  }
  else
  {
    symbol = 1;
    p->Code -= newBound;
    p->Range -= newBound;
  }
  Range_Normalize(p);
  return symbol;
}

static UInt32 Range_DecodeBit_RAR(void *pp, UInt32 size0)
{
  CPpmd7z_RangeDec *p = (CPpmd7z_RangeDec *)pp;
  UInt32 bit, value = p->p.GetThreshold(p, PPMD_BIN_SCALE);
  if(value < size0)
  {
    bit = 0;
    p->p.Decode(p, 0, size0);
  }
  else
  {
    bit = 1;
    p->p.Decode(p, size0, PPMD_BIN_SCALE - size0);
  }
  return bit;
}

static void Ppmd7z_RangeDec_CreateVTable(CPpmd7z_RangeDec *p)
{
  p->p.GetThreshold = Range_GetThreshold;
  p->p.Decode = Range_Decode_7z;
  p->p.DecodeBit = Range_DecodeBit_7z;
}

static void PpmdRAR_RangeDec_CreateVTable(CPpmd7z_RangeDec *p)
{
  p->p.GetThreshold = Range_GetThreshold;
  p->p.Decode = Range_Decode_RAR;
  p->p.DecodeBit = Range_DecodeBit_RAR;
}

#define MASK(sym) ((signed char *)charMask)[sym]

static int Ppmd7_DecodeSymbol(CPpmd7 *p, IPpmd7_RangeDec *rc)
{
  size_t charMask[256 / sizeof(size_t)];
  if (p->MinContext->NumStats != 1)
  {
    CPpmd_State *s = Ppmd7_GetStats(p, p->MinContext);
    unsigned i;
    UInt32 count, hiCnt;
    if ((count = rc->GetThreshold(rc, p->MinContext->SummFreq)) < (hiCnt = s->Freq))
    {
      Byte symbol;
      rc->Decode(rc, 0, s->Freq);
      p->FoundState = s;
      symbol = s->Symbol;
      Ppmd7_Update1_0(p);
      return symbol;
    }
    p->PrevSuccess = 0;
    i = p->MinContext->NumStats - 1;
    do
    {
      if ((hiCnt += (++s)->Freq) > count)
      {
        Byte symbol;
        rc->Decode(rc, hiCnt - s->Freq, s->Freq);
        p->FoundState = s;
        symbol = s->Symbol;
        Ppmd7_Update1(p);
        return symbol;
      }
    }
    while (--i);
    if (count >= p->MinContext->SummFreq)
      return -2;
    p->HiBitsFlag = p->HB2Flag[p->FoundState->Symbol];
    rc->Decode(rc, hiCnt, p->MinContext->SummFreq - hiCnt);
    PPMD_SetAllBitsIn256Bytes(charMask);
    MASK(s->Symbol) = 0;
    i = p->MinContext->NumStats - 1;
    do { MASK((--s)->Symbol) = 0; } while (--i);
  }
  else
  {
    UInt16 *prob = Ppmd7_GetBinSumm(p);
    if (rc->DecodeBit(rc, *prob) == 0)
    {
      Byte symbol;
      *prob = (UInt16)PPMD_UPDATE_PROB_0(*prob);
      symbol = (p->FoundState = Ppmd7Context_OneState(p->MinContext))->Symbol;
      Ppmd7_UpdateBin(p);
      return symbol;
    }
    *prob = (UInt16)PPMD_UPDATE_PROB_1(*prob);
    p->InitEsc = PPMD7_kExpEscape[*prob >> 10];
    PPMD_SetAllBitsIn256Bytes(charMask);
    MASK(Ppmd7Context_OneState(p->MinContext)->Symbol) = 0;
    p->PrevSuccess = 0;
  }
  for (;;)
  {
    CPpmd_State *ps[256], *s;
    UInt32 freqSum, count, hiCnt;
    CPpmd_See *see;
    unsigned i, num, numMasked = p->MinContext->NumStats;
    do
    {
      p->OrderFall++;
      if (!p->MinContext->Suffix)
        return -1;
      p->MinContext = Ppmd7_GetContext(p, p->MinContext->Suffix);
    }
    while (p->MinContext->NumStats == numMasked);
    hiCnt = 0;
    s = Ppmd7_GetStats(p, p->MinContext);
    i = 0;
    num = p->MinContext->NumStats - numMasked;
    do
    {
      int k = (int)(MASK(s->Symbol));
      hiCnt += (s->Freq & k);
      ps[i] = s++;
      i -= k;
    }
    while (i != num);

    see = Ppmd7_MakeEscFreq(p, numMasked, &freqSum);
    freqSum += hiCnt;
    count = rc->GetThreshold(rc, freqSum);

    if (count < hiCnt)
    {
      Byte symbol;
      CPpmd_State **pps = ps;
      for (hiCnt = 0; (hiCnt += (*pps)->Freq) <= count; pps++);
      s = *pps;
      rc->Decode(rc, hiCnt - s->Freq, s->Freq);
      Ppmd_See_Update(see);
      p->FoundState = s;
      symbol = s->Symbol;
      Ppmd7_Update2(p);
      return symbol;
    }
    if (count >= freqSum)
      return -2;
    rc->Decode(rc, hiCnt, freqSum - hiCnt);
    see->Summ = (UInt16)(see->Summ + freqSum);
    do { MASK(ps[--i]->Symbol) = 0; } while (i != 0);
  }
}

/* ---------- Encode ---------- Ppmd7Enc.c */

#define kTopValue (1 << 24)

static void Ppmd7z_RangeEnc_Init(CPpmd7z_RangeEnc *p)
{
  p->Low = 0;
  p->Range = 0xFFFFFFFF;
  p->Cache = 0;
  p->CacheSize = 1;
}

static void RangeEnc_ShiftLow(CPpmd7z_RangeEnc *p)
{
  if ((UInt32)p->Low < (UInt32)0xFF000000 || (unsigned)(p->Low >> 32) != 0)
  {
    Byte temp = p->Cache;
    do
    {
      p->Stream->Write(p->Stream, (Byte)(temp + (Byte)(p->Low >> 32)));
      temp = 0xFF;
    }
    while(--p->CacheSize != 0);
    p->Cache = (Byte)((UInt32)p->Low >> 24);
  }
  p->CacheSize++;
  p->Low = ((UInt32)p->Low << 8) & 0xFFFFFFFF;
}

static void RangeEnc_Encode(CPpmd7z_RangeEnc *p, UInt32 start, UInt32 size, UInt32 total)
{
  p->Low += (UInt64)start * (UInt64)(p->Range /= total);
  p->Range *= size;
  while (p->Range < kTopValue)
  {
    p->Range <<= 8;
    RangeEnc_ShiftLow(p);
  }
}

static void RangeEnc_EncodeBit_0(CPpmd7z_RangeEnc *p, UInt32 size0)
{
  p->Range = (p->Range >> 14) * size0;
  while (p->Range < kTopValue)
  {
    p->Range <<= 8;
    RangeEnc_ShiftLow(p);
  }
}

static void RangeEnc_EncodeBit_1(CPpmd7z_RangeEnc *p, UInt32 size0)
{
  UInt32 newBound = (p->Range >> 14) * size0;
  p->Low += newBound;
  p->Range -= newBound;
  while (p->Range < kTopValue)
  {
    p->Range <<= 8;
    RangeEnc_ShiftLow(p);
  }
}

static void Ppmd7z_RangeEnc_FlushData(CPpmd7z_RangeEnc *p)
{
  unsigned i;
  for (i = 0; i < 5; i++)
    RangeEnc_ShiftLow(p);
}


#define MASK(sym) ((signed char *)charMask)[sym]

static void Ppmd7_EncodeSymbol(CPpmd7 *p, CPpmd7z_RangeEnc *rc, int symbol)
{
  size_t charMask[256 / sizeof(size_t)];
  if (p->MinContext->NumStats != 1)
  {
    CPpmd_State *s = Ppmd7_GetStats(p, p->MinContext);
    UInt32 sum;
    unsigned i;
    if (s->Symbol == symbol)
    {
      RangeEnc_Encode(rc, 0, s->Freq, p->MinContext->SummFreq);
      p->FoundState = s;
      Ppmd7_Update1_0(p);
      return;
    }
    p->PrevSuccess = 0;
    sum = s->Freq;
    i = p->MinContext->NumStats - 1;
    do
    {
      if ((++s)->Symbol == symbol)
      {
        RangeEnc_Encode(rc, sum, s->Freq, p->MinContext->SummFreq);
        p->FoundState = s;
        Ppmd7_Update1(p);
        return;
      }
      sum += s->Freq;
    }
    while (--i);
    
    p->HiBitsFlag = p->HB2Flag[p->FoundState->Symbol];
    PPMD_SetAllBitsIn256Bytes(charMask);
    MASK(s->Symbol) = 0;
    i = p->MinContext->NumStats - 1;
    do { MASK((--s)->Symbol) = 0; } while (--i);
    RangeEnc_Encode(rc, sum, p->MinContext->SummFreq - sum, p->MinContext->SummFreq);
  }
  else
  {
    UInt16 *prob = Ppmd7_GetBinSumm(p);
    CPpmd_State *s = Ppmd7Context_OneState(p->MinContext);
    if (s->Symbol == symbol)
    {
      RangeEnc_EncodeBit_0(rc, *prob);
      *prob = (UInt16)PPMD_UPDATE_PROB_0(*prob);
      p->FoundState = s;
      Ppmd7_UpdateBin(p);
      return;
    }
    else
    {
      RangeEnc_EncodeBit_1(rc, *prob);
      *prob = (UInt16)PPMD_UPDATE_PROB_1(*prob);
      p->InitEsc = PPMD7_kExpEscape[*prob >> 10];
      PPMD_SetAllBitsIn256Bytes(charMask);
      MASK(s->Symbol) = 0;
      p->PrevSuccess = 0;
    }
  }
  for (;;)
  {
    UInt32 escFreq;
    CPpmd_See *see;
    CPpmd_State *s;
    UInt32 sum;
    unsigned i, numMasked = p->MinContext->NumStats;
    do
    {
      p->OrderFall++;
      if (!p->MinContext->Suffix)
        return; /* EndMarker (symbol = -1) */
      p->MinContext = Ppmd7_GetContext(p, p->MinContext->Suffix);
    }
    while (p->MinContext->NumStats == numMasked);
    
    see = Ppmd7_MakeEscFreq(p, numMasked, &escFreq);
    s = Ppmd7_GetStats(p, p->MinContext);
    sum = 0;
    i = p->MinContext->NumStats;
    do
    {
      int cur = s->Symbol;
      if (cur == symbol)
      {
        UInt32 low = sum;
        CPpmd_State *s1 = s;
        do
        {
          sum += (s->Freq & (int)(MASK(s->Symbol)));
          s++;
        }
        while (--i);
        RangeEnc_Encode(rc, low, s1->Freq, sum + escFreq);
        Ppmd_See_Update(see);
        p->FoundState = s1;
        Ppmd7_Update2(p);
        return;
      }
      sum += (s->Freq & (int)(MASK(cur)));
      MASK(cur) = 0;
      s++;
    }
    while (--i);
    
    RangeEnc_Encode(rc, sum, escFreq, sum + escFreq);
    see->Summ = (UInt16)(see->Summ + sum + escFreq);
  }
}

const IPpmd7 __archive_ppmd7_functions =
{
  &Ppmd7_Construct,
  &Ppmd7_Alloc,
  &Ppmd7_Free,
  &Ppmd7_Init,
  &Ppmd7z_RangeDec_CreateVTable,
  &PpmdRAR_RangeDec_CreateVTable,
  &Ppmd7z_RangeDec_Init,
  &PpmdRAR_RangeDec_Init,
  &Ppmd7_DecodeSymbol,
  &Ppmd7z_RangeEnc_Init,
  &Ppmd7z_RangeEnc_FlushData,
  &Ppmd7_EncodeSymbol
};
#undef CTX
#undef I2U
#undef MASK
#undef MAX_FREQ
#undef MyMem12Cpy
#undef NODE
#undef ONE_STATE
#undef Ppmd7_GetBinSumm
#undef Ppmd7_GetContext
#undef Ppmd7_GetPtr
#undef Ppmd7_GetStats
#undef REF
#undef STATS
#undef STATS_REF
#undef SUCCESSOR
#undef SUFFIX
#undef U2B
#undef U2I
#undef UNIT_SIZE
#undef kTopValue

/* ==== archive_blake2s_ref.c ==== */
/*
   BLAKE2 reference source code package - reference C implementations

   Copyright 2012, Samuel Neves <sneves@dei.uc.pt>.  You may use this under the
   terms of the CC0, the OpenSSL Licence, or the Apache Public License 2.0, at
   your option.  The terms of these licenses can be found at:

   - CC0 1.0 Universal : http://creativecommons.org/publicdomain/zero/1.0
   - OpenSSL license   : https://www.openssl.org/source/license.html
   - Apache 2.0        : http://www.apache.org/licenses/LICENSE-2.0

   More information about the BLAKE2 hash function can be found at
   https://blake2.net.
*/



#include <stdint.h>
#include <string.h>
#include <stdio.h>


/* ==== archive_blake2_impl.h ==== */
/*
   BLAKE2 reference source code package - reference C implementations

   Copyright 2012, Samuel Neves <sneves@dei.uc.pt>.  You may use this under the
   terms of the CC0, the OpenSSL Licence, or the Apache Public License 2.0, at
   your option.  The terms of these licenses can be found at:

   - CC0 1.0 Universal : http://creativecommons.org/publicdomain/zero/1.0
   - OpenSSL license   : https://www.openssl.org/source/license.html
   - Apache 2.0        : http://www.apache.org/licenses/LICENSE-2.0

   More information about the BLAKE2 hash function can be found at
   https://blake2.net.
*/

#ifndef ARCHIVE_BLAKE2_IMPL_H
#define ARCHIVE_BLAKE2_IMPL_H

#include <stdint.h>
#include <string.h>

#if !defined(__cplusplus) && (!defined(__STDC_VERSION__) || __STDC_VERSION__ < 199901L)
  #if   defined(_MSC_VER)
    #define BLAKE2_INLINE __inline
  #elif defined(__GNUC__)
    #define BLAKE2_INLINE __inline__
  #else
    #define BLAKE2_INLINE
  #endif
#else
  #define BLAKE2_INLINE inline
#endif

static BLAKE2_INLINE uint32_t load32( const void *src )
{
#if defined(NATIVE_LITTLE_ENDIAN)
  uint32_t w;
  memcpy(&w, src, sizeof w);
  return w;
#else
  const uint8_t *p = ( const uint8_t * )src;
  return (( uint32_t )( p[0] ) <<  0) |
         (( uint32_t )( p[1] ) <<  8) |
         (( uint32_t )( p[2] ) << 16) |
         (( uint32_t )( p[3] ) << 24) ;
#endif
}

static BLAKE2_INLINE uint64_t load64( const void *src )
{
#if defined(NATIVE_LITTLE_ENDIAN)
  uint64_t w;
  memcpy(&w, src, sizeof w);
  return w;
#else
  const uint8_t *p = ( const uint8_t * )src;
  return (( uint64_t )( p[0] ) <<  0) |
         (( uint64_t )( p[1] ) <<  8) |
         (( uint64_t )( p[2] ) << 16) |
         (( uint64_t )( p[3] ) << 24) |
         (( uint64_t )( p[4] ) << 32) |
         (( uint64_t )( p[5] ) << 40) |
         (( uint64_t )( p[6] ) << 48) |
         (( uint64_t )( p[7] ) << 56) ;
#endif
}

static BLAKE2_INLINE uint16_t load16( const void *src )
{
#if defined(NATIVE_LITTLE_ENDIAN)
  uint16_t w;
  memcpy(&w, src, sizeof w);
  return w;
#else
  const uint8_t *p = ( const uint8_t * )src;
  return ( uint16_t )((( uint32_t )( p[0] ) <<  0) |
                      (( uint32_t )( p[1] ) <<  8));
#endif
}

static BLAKE2_INLINE void store16( void *dst, uint16_t w )
{
#if defined(NATIVE_LITTLE_ENDIAN)
  memcpy(dst, &w, sizeof w);
#else
  uint8_t *p = ( uint8_t * )dst;
  *p++ = ( uint8_t )w; w >>= 8;
  *p++ = ( uint8_t )w;
#endif
}

static BLAKE2_INLINE void store32( void *dst, uint32_t w )
{
#if defined(NATIVE_LITTLE_ENDIAN)
  memcpy(dst, &w, sizeof w);
#else
  uint8_t *p = ( uint8_t * )dst;
  p[0] = (uint8_t)(w >>  0);
  p[1] = (uint8_t)(w >>  8);
  p[2] = (uint8_t)(w >> 16);
  p[3] = (uint8_t)(w >> 24);
#endif
}

static BLAKE2_INLINE void store64( void *dst, uint64_t w )
{
#if defined(NATIVE_LITTLE_ENDIAN)
  memcpy(dst, &w, sizeof w);
#else
  uint8_t *p = ( uint8_t * )dst;
  p[0] = (uint8_t)(w >>  0);
  p[1] = (uint8_t)(w >>  8);
  p[2] = (uint8_t)(w >> 16);
  p[3] = (uint8_t)(w >> 24);
  p[4] = (uint8_t)(w >> 32);
  p[5] = (uint8_t)(w >> 40);
  p[6] = (uint8_t)(w >> 48);
  p[7] = (uint8_t)(w >> 56);
#endif
}

static BLAKE2_INLINE uint64_t load48( const void *src )
{
  const uint8_t *p = ( const uint8_t * )src;
  return (( uint64_t )( p[0] ) <<  0) |
         (( uint64_t )( p[1] ) <<  8) |
         (( uint64_t )( p[2] ) << 16) |
         (( uint64_t )( p[3] ) << 24) |
         (( uint64_t )( p[4] ) << 32) |
         (( uint64_t )( p[5] ) << 40) ;
}

static BLAKE2_INLINE void store48( void *dst, uint64_t w )
{
  uint8_t *p = ( uint8_t * )dst;
  p[0] = (uint8_t)(w >>  0);
  p[1] = (uint8_t)(w >>  8);
  p[2] = (uint8_t)(w >> 16);
  p[3] = (uint8_t)(w >> 24);
  p[4] = (uint8_t)(w >> 32);
  p[5] = (uint8_t)(w >> 40);
}

static BLAKE2_INLINE uint32_t rotr32( const uint32_t w, const unsigned c )
{
  return ( w >> c ) | ( w << ( 32 - c ) );
}

static BLAKE2_INLINE uint64_t rotr64( const uint64_t w, const unsigned c )
{
  return ( w >> c ) | ( w << ( 64 - c ) );
}

/* prevents compiler optimizing out memset() */
static BLAKE2_INLINE void secure_zero_memory(void *v, size_t n)
{
  static void *(__LA_LIBC_CC *const volatile memset_v)(void *, int, size_t) = &memset;
  memset_v(v, 0, n);
}

#endif

static const uint32_t blake2s_IV[8] =
{
  0x6A09E667UL, 0xBB67AE85UL, 0x3C6EF372UL, 0xA54FF53AUL,
  0x510E527FUL, 0x9B05688CUL, 0x1F83D9ABUL, 0x5BE0CD19UL
};

static const uint8_t blake2s_sigma[10][16] =
{
  {  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15 } ,
  { 14, 10,  4,  8,  9, 15, 13,  6,  1, 12,  0,  2, 11,  7,  5,  3 } ,
  { 11,  8, 12,  0,  5,  2, 15, 13, 10, 14,  3,  6,  7,  1,  9,  4 } ,
  {  7,  9,  3,  1, 13, 12, 11, 14,  2,  6,  5, 10,  4,  0, 15,  8 } ,
  {  9,  0,  5,  7,  2,  4, 10, 15, 14,  1, 11, 12,  6,  8,  3, 13 } ,
  {  2, 12,  6, 10,  0, 11,  8,  3,  4, 13,  7,  5, 15, 14,  1,  9 } ,
  { 12,  5,  1, 15, 14, 13,  4, 10,  0,  7,  6,  3,  9,  2,  8, 11 } ,
  { 13, 11,  7, 14, 12,  1,  3,  9,  5,  0, 15,  4,  8,  6,  2, 10 } ,
  {  6, 15, 14,  9, 11,  3,  0,  8, 12,  2, 13,  7,  1,  4, 10,  5 } ,
  { 10,  2,  8,  4,  7,  6,  1,  5, 15, 11,  9, 14,  3, 12, 13 , 0 } ,
};

static void blake2s_set_lastnode( blake2s_state *S )
{
  S->f[1] = (uint32_t)-1;
}

/* Some helper functions, not necessarily useful */
static int blake2s_is_lastblock( const blake2s_state *S )
{
  return S->f[0] != 0;
}

static void blake2s_set_lastblock( blake2s_state *S )
{
  if( S->last_node ) blake2s_set_lastnode( S );

  S->f[0] = (uint32_t)-1;
}

static void blake2s_increment_counter( blake2s_state *S, const uint32_t inc )
{
  S->t[0] += inc;
  S->t[1] += ( S->t[0] < inc );
}

static void blake2s_init0( blake2s_state *S )
{
  size_t i;
  memset( S, 0, sizeof( blake2s_state ) );

  for( i = 0; i < 8; ++i ) S->h[i] = blake2s_IV[i];
}

/* init2 xors IV with input parameter block */
int blake2s_init_param( blake2s_state *S, const blake2s_param *P )
{
  const unsigned char *p = ( const unsigned char * )( P );
  size_t i;

  blake2s_init0( S );

  /* IV XOR ParamBlock */
  for( i = 0; i < 8; ++i )
    S->h[i] ^= load32( &p[i * 4] );

  S->outlen = P->digest_length;
  return 0;
}


/* Sequential blake2s initialization */
int blake2s_init( blake2s_state *S, size_t outlen )
{
  blake2s_param P[1];

  /* Move interval verification here? */
  if ( ( !outlen ) || ( outlen > BLAKE2S_OUTBYTES ) ) return -1;

  P->digest_length = (uint8_t)outlen;
  P->key_length    = 0;
  P->fanout        = 1;
  P->depth         = 1;
  store32( &P->leaf_length, 0 );
  store32( &P->node_offset, 0 );
  store16( &P->xof_length, 0 );
  P->node_depth    = 0;
  P->inner_length  = 0;
  /* memset(P->reserved, 0, sizeof(P->reserved) ); */
  memset( P->salt,     0, sizeof( P->salt ) );
  memset( P->personal, 0, sizeof( P->personal ) );
  return blake2s_init_param( S, P );
}

int blake2s_init_key( blake2s_state *S, size_t outlen, const void *key, size_t keylen )
{
  blake2s_param P[1];

  if ( ( !outlen ) || ( outlen > BLAKE2S_OUTBYTES ) ) return -1;

  if ( !key || !keylen || keylen > BLAKE2S_KEYBYTES ) return -1;

  P->digest_length = (uint8_t)outlen;
  P->key_length    = (uint8_t)keylen;
  P->fanout        = 1;
  P->depth         = 1;
  store32( &P->leaf_length, 0 );
  store32( &P->node_offset, 0 );
  store16( &P->xof_length, 0 );
  P->node_depth    = 0;
  P->inner_length  = 0;
  /* memset(P->reserved, 0, sizeof(P->reserved) ); */
  memset( P->salt,     0, sizeof( P->salt ) );
  memset( P->personal, 0, sizeof( P->personal ) );

  if( blake2s_init_param( S, P ) < 0 ) return -1;

  {
    uint8_t block[BLAKE2S_BLOCKBYTES];
    memset( block, 0, BLAKE2S_BLOCKBYTES );
    memcpy( block, key, keylen );
    blake2s_update( S, block, BLAKE2S_BLOCKBYTES );
    secure_zero_memory( block, BLAKE2S_BLOCKBYTES ); /* Burn the key from stack */
  }
  return 0;
}

#define G(r,i,a,b,c,d)                      \
  do {                                      \
    a = a + b + m[blake2s_sigma[r][2*i+0]]; \
    d = rotr32(d ^ a, 16);                  \
    c = c + d;                              \
    b = rotr32(b ^ c, 12);                  \
    a = a + b + m[blake2s_sigma[r][2*i+1]]; \
    d = rotr32(d ^ a, 8);                   \
    c = c + d;                              \
    b = rotr32(b ^ c, 7);                   \
  } while(0)

#define ROUND(r)                    \
  do {                              \
    G(r,0,v[ 0],v[ 4],v[ 8],v[12]); \
    G(r,1,v[ 1],v[ 5],v[ 9],v[13]); \
    G(r,2,v[ 2],v[ 6],v[10],v[14]); \
    G(r,3,v[ 3],v[ 7],v[11],v[15]); \
    G(r,4,v[ 0],v[ 5],v[10],v[15]); \
    G(r,5,v[ 1],v[ 6],v[11],v[12]); \
    G(r,6,v[ 2],v[ 7],v[ 8],v[13]); \
    G(r,7,v[ 3],v[ 4],v[ 9],v[14]); \
  } while(0)

static void blake2s_compress( blake2s_state *S, const uint8_t in[BLAKE2S_BLOCKBYTES] )
{
  uint32_t m[16];
  uint32_t v[16];
  size_t i;

  for( i = 0; i < 16; ++i ) {
    m[i] = load32( in + i * sizeof( m[i] ) );
  }

  for( i = 0; i < 8; ++i ) {
    v[i] = S->h[i];
  }

  v[ 8] = blake2s_IV[0];
  v[ 9] = blake2s_IV[1];
  v[10] = blake2s_IV[2];
  v[11] = blake2s_IV[3];
  v[12] = S->t[0] ^ blake2s_IV[4];
  v[13] = S->t[1] ^ blake2s_IV[5];
  v[14] = S->f[0] ^ blake2s_IV[6];
  v[15] = S->f[1] ^ blake2s_IV[7];

  ROUND( 0 );
  ROUND( 1 );
  ROUND( 2 );
  ROUND( 3 );
  ROUND( 4 );
  ROUND( 5 );
  ROUND( 6 );
  ROUND( 7 );
  ROUND( 8 );
  ROUND( 9 );

  for( i = 0; i < 8; ++i ) {
    S->h[i] = S->h[i] ^ v[i] ^ v[i + 8];
  }
}

#undef G
#undef ROUND

int blake2s_update( blake2s_state *S, const void *pin, size_t inlen )
{
  const unsigned char * in = (const unsigned char *)pin;
  if( inlen > 0 )
  {
    size_t left = S->buflen;
    size_t fill = BLAKE2S_BLOCKBYTES - left;
    if( inlen > fill )
    {
      S->buflen = 0;
      memcpy( S->buf + left, in, fill ); /* Fill buffer */
      blake2s_increment_counter( S, BLAKE2S_BLOCKBYTES );
      blake2s_compress( S, S->buf ); /* Compress */
      in += fill; inlen -= fill;
      while(inlen > BLAKE2S_BLOCKBYTES) {
        blake2s_increment_counter(S, BLAKE2S_BLOCKBYTES);
        blake2s_compress( S, in );
        in += BLAKE2S_BLOCKBYTES;
        inlen -= BLAKE2S_BLOCKBYTES;
      }
    }
    memcpy( S->buf + S->buflen, in, inlen );
    S->buflen += inlen;
  }
  return 0;
}

int blake2s_final( blake2s_state *S, void *out, size_t outlen )
{
  uint8_t buffer[BLAKE2S_OUTBYTES] = {0};
  size_t i;

  if( out == NULL || outlen < S->outlen )
    return -1;

  if( blake2s_is_lastblock( S ) )
    return -1;

  blake2s_increment_counter( S, ( uint32_t )S->buflen );
  blake2s_set_lastblock( S );
  memset( S->buf + S->buflen, 0, BLAKE2S_BLOCKBYTES - S->buflen ); /* Padding */
  blake2s_compress( S, S->buf );

  for( i = 0; i < 8; ++i ) /* Output full hash to temp buffer */
    store32( buffer + sizeof( S->h[i] ) * i, S->h[i] );

  memcpy( out, buffer, outlen );
  secure_zero_memory(buffer, sizeof(buffer));
  return 0;
}

int blake2s( void *out, size_t outlen, const void *in, size_t inlen, const void *key, size_t keylen )
{
  blake2s_state S[1];

  /* Verify parameters */
  if ( NULL == in && inlen > 0 ) return -1;

  if ( NULL == out ) return -1;

  if ( NULL == key && keylen > 0) return -1;

  if( !outlen || outlen > BLAKE2S_OUTBYTES ) return -1;

  if( keylen > BLAKE2S_KEYBYTES ) return -1;

  if( keylen > 0 )
  {
    if( blake2s_init_key( S, outlen, key, keylen ) < 0 ) return -1;
  }
  else
  {
    if( blake2s_init( S, outlen ) < 0 ) return -1;
  }

  blake2s_update( S, ( const uint8_t * )in, inlen );
  blake2s_final( S, out, outlen );
  return 0;
}

#if defined(SUPERCOP)
int crypto_hash( unsigned char *out, unsigned char *in, unsigned long long inlen )
{
  return blake2s( out, BLAKE2S_OUTBYTES, in, inlen, NULL, 0 );
}
#endif

#if defined(BLAKE2S_SELFTEST)
#include <string.h>

int main( void )
{
  uint8_t key[BLAKE2S_KEYBYTES];
  uint8_t buf[BLAKE2_KAT_LENGTH];
  size_t i, step;

  for( i = 0; i < BLAKE2S_KEYBYTES; ++i )
    key[i] = ( uint8_t )i;

  for( i = 0; i < BLAKE2_KAT_LENGTH; ++i )
    buf[i] = ( uint8_t )i;

  /* Test simple API */
  for( i = 0; i < BLAKE2_KAT_LENGTH; ++i )
  {
    uint8_t hash[BLAKE2S_OUTBYTES];
    blake2s( hash, BLAKE2S_OUTBYTES, buf, i, key, BLAKE2S_KEYBYTES );

    if( 0 != memcmp( hash, blake2s_keyed_kat[i], BLAKE2S_OUTBYTES ) )
    {
      goto fail;
    }
  }

  /* Test streaming API */
  for(step = 1; step < BLAKE2S_BLOCKBYTES; ++step) {
    for (i = 0; i < BLAKE2_KAT_LENGTH; ++i) {
      uint8_t hash[BLAKE2S_OUTBYTES];
      blake2s_state S;
      uint8_t * p = buf;
      size_t mlen = i;
      int err = 0;

      if( (err = blake2s_init_key(&S, BLAKE2S_OUTBYTES, key, BLAKE2S_KEYBYTES)) < 0 ) {
        goto fail;
      }

      while (mlen >= step) {
        if ( (err = blake2s_update(&S, p, step)) < 0 ) {
          goto fail;
        }
        mlen -= step;
        p += step;
      }
      if ( (err = blake2s_update(&S, p, mlen)) < 0) {
        goto fail;
      }
      if ( (err = blake2s_final(&S, hash, BLAKE2S_OUTBYTES)) < 0) {
        goto fail;
      }

      if (0 != memcmp(hash, blake2s_keyed_kat[i], BLAKE2S_OUTBYTES)) {
        goto fail;
      }
    }
  }

  puts( "ok" );
  return 0;
fail:
  puts("error");
  return -1;
}
#endif
#undef G
#undef ROUND

/* ==== archive_blake2sp_ref.c ==== */
/*
   BLAKE2 reference source code package - reference C implementations

   Copyright 2012, Samuel Neves <sneves@dei.uc.pt>.  You may use this under the
   terms of the CC0, the OpenSSL Licence, or the Apache Public License 2.0, at
   your option.  The terms of these licenses can be found at:

   - CC0 1.0 Universal : http://creativecommons.org/publicdomain/zero/1.0
   - OpenSSL license   : https://www.openssl.org/source/license.html
   - Apache 2.0        : http://www.apache.org/licenses/LICENSE-2.0

   More information about the BLAKE2 hash function can be found at
   https://blake2.net.
*/



#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#if defined(_OPENMP)
#include <omp.h>
#endif




#define PARALLELISM_DEGREE 8

/*
  blake2sp_init_param defaults to setting the expecting output length
  from the digest_length parameter block field.

  In some cases, however, we do not want this, as the output length
  of these instances is given by inner_length instead.
*/
static int blake2sp_init_leaf_param( blake2s_state *S, const blake2s_param *P )
{
  int err = blake2s_init_param(S, P);
  S->outlen = P->inner_length;
  return err;
}

static int blake2sp_init_leaf( blake2s_state *S, size_t outlen, size_t keylen, uint32_t offset )
{
  blake2s_param P[1];
  P->digest_length = (uint8_t)outlen;
  P->key_length = (uint8_t)keylen;
  P->fanout = PARALLELISM_DEGREE;
  P->depth = 2;
  store32( &P->leaf_length, 0 );
  store32( &P->node_offset, offset );
  store16( &P->xof_length, 0 );
  P->node_depth = 0;
  P->inner_length = BLAKE2S_OUTBYTES;
  memset( P->salt, 0, sizeof( P->salt ) );
  memset( P->personal, 0, sizeof( P->personal ) );
  return blake2sp_init_leaf_param( S, P );
}

static int blake2sp_init_root( blake2s_state *S, size_t outlen, size_t keylen )
{
  blake2s_param P[1];
  P->digest_length = (uint8_t)outlen;
  P->key_length = (uint8_t)keylen;
  P->fanout = PARALLELISM_DEGREE;
  P->depth = 2;
  store32( &P->leaf_length, 0 );
  store32( &P->node_offset, 0 );
  store16( &P->xof_length, 0 );
  P->node_depth = 1;
  P->inner_length = BLAKE2S_OUTBYTES;
  memset( P->salt, 0, sizeof( P->salt ) );
  memset( P->personal, 0, sizeof( P->personal ) );
  return blake2s_init_param( S, P );
}


int blake2sp_init( blake2sp_state *S, size_t outlen )
{
  size_t i;

  if( !outlen || outlen > BLAKE2S_OUTBYTES ) return -1;

  memset( S->buf, 0, sizeof( S->buf ) );
  S->buflen = 0;
  S->outlen = outlen;

  if( blake2sp_init_root( S->R, outlen, 0 ) < 0 )
    return -1;

  for( i = 0; i < PARALLELISM_DEGREE; ++i )
    if( blake2sp_init_leaf( S->S[i], outlen, 0, (uint32_t)i ) < 0 ) return -1;

  S->R->last_node = 1;
  S->S[PARALLELISM_DEGREE - 1]->last_node = 1;
  return 0;
}

int blake2sp_init_key( blake2sp_state *S, size_t outlen, const void *key, size_t keylen )
{
  size_t i;

  if( !outlen || outlen > BLAKE2S_OUTBYTES ) return -1;

  if( !key || !keylen || keylen > BLAKE2S_KEYBYTES ) return -1;

  memset( S->buf, 0, sizeof( S->buf ) );
  S->buflen = 0;
  S->outlen = outlen;

  if( blake2sp_init_root( S->R, outlen, keylen ) < 0 )
    return -1;

  for( i = 0; i < PARALLELISM_DEGREE; ++i )
    if( blake2sp_init_leaf( S->S[i], outlen, keylen, (uint32_t)i ) < 0 ) return -1;

  S->R->last_node = 1;
  S->S[PARALLELISM_DEGREE - 1]->last_node = 1;
  {
    uint8_t block[BLAKE2S_BLOCKBYTES];
    memset( block, 0, BLAKE2S_BLOCKBYTES );
    memcpy( block, key, keylen );

    for( i = 0; i < PARALLELISM_DEGREE; ++i )
      blake2s_update( S->S[i], block, BLAKE2S_BLOCKBYTES );

    secure_zero_memory( block, BLAKE2S_BLOCKBYTES ); /* Burn the key from stack */
  }
  return 0;
}


int blake2sp_update( blake2sp_state *S, const void *pin, size_t inlen )
{
  const unsigned char * in = (const unsigned char *)pin;
  size_t left = S->buflen;
  size_t fill = sizeof( S->buf ) - left;
  size_t i;

  if( left && inlen >= fill )
  {
    memcpy( S->buf + left, in, fill );

    for( i = 0; i < PARALLELISM_DEGREE; ++i )
      blake2s_update( S->S[i], S->buf + i * BLAKE2S_BLOCKBYTES, BLAKE2S_BLOCKBYTES );

    in += fill;
    inlen -= fill;
    left = 0;
  }

#if defined(_OPENMP)
  #pragma omp parallel shared(S), num_threads(PARALLELISM_DEGREE)
#else
  for( i = 0; i < PARALLELISM_DEGREE; ++i )
#endif
  {
#if defined(_OPENMP)
    size_t      i = omp_get_thread_num();
#endif
    size_t inlen__ = inlen;
    const unsigned char *in__ = ( const unsigned char * )in;
    in__ += i * BLAKE2S_BLOCKBYTES;

    while( inlen__ >= PARALLELISM_DEGREE * BLAKE2S_BLOCKBYTES )
    {
      blake2s_update( S->S[i], in__, BLAKE2S_BLOCKBYTES );
      in__ += PARALLELISM_DEGREE * BLAKE2S_BLOCKBYTES;
      inlen__ -= PARALLELISM_DEGREE * BLAKE2S_BLOCKBYTES;
    }
  }

  in += inlen - inlen % ( PARALLELISM_DEGREE * BLAKE2S_BLOCKBYTES );
  inlen %= PARALLELISM_DEGREE * BLAKE2S_BLOCKBYTES;

  if( inlen > 0 )
    memcpy( S->buf + left, in, inlen );

  S->buflen = left + inlen;
  return 0;
}


int blake2sp_final( blake2sp_state *S, void *out, size_t outlen )
{
  uint8_t hash[PARALLELISM_DEGREE][BLAKE2S_OUTBYTES];
  size_t i;

  if(out == NULL || outlen < S->outlen) {
    return -1;
  }

  for( i = 0; i < PARALLELISM_DEGREE; ++i )
  {
    if( S->buflen > i * BLAKE2S_BLOCKBYTES )
    {
      size_t left = S->buflen - i * BLAKE2S_BLOCKBYTES;

      if( left > BLAKE2S_BLOCKBYTES ) left = BLAKE2S_BLOCKBYTES;

      blake2s_update( S->S[i], S->buf + i * BLAKE2S_BLOCKBYTES, left );
    }

    blake2s_final( S->S[i], hash[i], BLAKE2S_OUTBYTES );
  }

  for( i = 0; i < PARALLELISM_DEGREE; ++i )
    blake2s_update( S->R, hash[i], BLAKE2S_OUTBYTES );

  return blake2s_final( S->R, out, S->outlen );
}


int blake2sp( void *out, size_t outlen, const void *in, size_t inlen, const void *key, size_t keylen )
{
  uint8_t hash[PARALLELISM_DEGREE][BLAKE2S_OUTBYTES];
  blake2s_state S[PARALLELISM_DEGREE][1];
  blake2s_state FS[1];
  size_t i;

  /* Verify parameters */
  if ( NULL == in && inlen > 0 ) return -1;

  if ( NULL == out ) return -1;

  if ( NULL == key && keylen > 0) return -1;

  if( !outlen || outlen > BLAKE2S_OUTBYTES ) return -1;

  if( keylen > BLAKE2S_KEYBYTES ) return -1;

  for( i = 0; i < PARALLELISM_DEGREE; ++i )
    if( blake2sp_init_leaf( S[i], outlen, keylen, (uint32_t)i ) < 0 ) return -1;

  S[PARALLELISM_DEGREE - 1]->last_node = 1; /* mark last node */

  if( keylen > 0 )
  {
    uint8_t block[BLAKE2S_BLOCKBYTES];
    memset( block, 0, BLAKE2S_BLOCKBYTES );
    memcpy( block, key, keylen );

    for( i = 0; i < PARALLELISM_DEGREE; ++i )
      blake2s_update( S[i], block, BLAKE2S_BLOCKBYTES );

    secure_zero_memory( block, BLAKE2S_BLOCKBYTES ); /* Burn the key from stack */
  }

#if defined(_OPENMP)
  #pragma omp parallel shared(S,hash), num_threads(PARALLELISM_DEGREE)
#else

  for( i = 0; i < PARALLELISM_DEGREE; ++i )
#endif
  {
#if defined(_OPENMP)
    size_t      i = omp_get_thread_num();
#endif
    size_t inlen__ = inlen;
    const unsigned char *in__ = ( const unsigned char * )in;
    in__ += i * BLAKE2S_BLOCKBYTES;

    while( inlen__ >= PARALLELISM_DEGREE * BLAKE2S_BLOCKBYTES )
    {
      blake2s_update( S[i], in__, BLAKE2S_BLOCKBYTES );
      in__ += PARALLELISM_DEGREE * BLAKE2S_BLOCKBYTES;
      inlen__ -= PARALLELISM_DEGREE * BLAKE2S_BLOCKBYTES;
    }

    if( inlen__ > i * BLAKE2S_BLOCKBYTES )
    {
      const size_t left = inlen__ - i * BLAKE2S_BLOCKBYTES;
      const size_t len = left <= BLAKE2S_BLOCKBYTES ? left : BLAKE2S_BLOCKBYTES;
      blake2s_update( S[i], in__, len );
    }

    blake2s_final( S[i], hash[i], BLAKE2S_OUTBYTES );
  }

  if( blake2sp_init_root( FS, outlen, keylen ) < 0 )
    return -1;

  FS->last_node = 1;

  for( i = 0; i < PARALLELISM_DEGREE; ++i )
    blake2s_update( FS, hash[i], BLAKE2S_OUTBYTES );

  return blake2s_final( FS, out, outlen );
}



#if defined(BLAKE2SP_SELFTEST)
#include <string.h>

int main( void )
{
  uint8_t key[BLAKE2S_KEYBYTES];
  uint8_t buf[BLAKE2_KAT_LENGTH];
  size_t i, step;

  for( i = 0; i < BLAKE2S_KEYBYTES; ++i )
    key[i] = ( uint8_t )i;

  for( i = 0; i < BLAKE2_KAT_LENGTH; ++i )
    buf[i] = ( uint8_t )i;

  /* Test simple API */
  for( i = 0; i < BLAKE2_KAT_LENGTH; ++i )
  {
    uint8_t hash[BLAKE2S_OUTBYTES];
    blake2sp( hash, BLAKE2S_OUTBYTES, buf, i, key, BLAKE2S_KEYBYTES );

    if( 0 != memcmp( hash, blake2sp_keyed_kat[i], BLAKE2S_OUTBYTES ) )
    {
      goto fail;
    }
  }

  /* Test streaming API */
  for(step = 1; step < BLAKE2S_BLOCKBYTES; ++step) {
    for (i = 0; i < BLAKE2_KAT_LENGTH; ++i) {
      uint8_t hash[BLAKE2S_OUTBYTES];
      blake2sp_state S;
      uint8_t * p = buf;
      size_t mlen = i;
      int err = 0;

      if( (err = blake2sp_init_key(&S, BLAKE2S_OUTBYTES, key, BLAKE2S_KEYBYTES)) < 0 ) {
        goto fail;
      }

      while (mlen >= step) {
        if ( (err = blake2sp_update(&S, p, step)) < 0 ) {
          goto fail;
        }
        mlen -= step;
        p += step;
      }
      if ( (err = blake2sp_update(&S, p, mlen)) < 0) {
        goto fail;
      }
      if ( (err = blake2sp_final(&S, hash, BLAKE2S_OUTBYTES)) < 0) {
        goto fail;
      }

      if (0 != memcmp(hash, blake2sp_keyed_kat[i], BLAKE2S_OUTBYTES)) {
        goto fail;
      }
    }
  }

  puts( "ok" );
  return 0;
fail:
  puts("error");
  return -1;
}
#endif
#undef PARALLELISM_DEGREE

/* ==== archive_time.c ==== */
/*-
 * Copyright © 2025 ARJANEN Loïc Jean David
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */




#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define NTFS_EPOC_TIME ARCHIVE_LITERAL_ULL(11644473600)
#define NTFS_TICKS ARCHIVE_LITERAL_ULL(10000000)
#define NTFS_EPOC_TICKS (NTFS_EPOC_TIME * NTFS_TICKS)
#define DOS_MIN_TIME 0x00210000U
#define DOS_MAX_TIME 0xff9fbf7dU

#if defined(_WIN32) && !defined(__CYGWIN__)
#include <winnt.h>
/* Windows FILETIME to NTFS time. */
uint64_t
FILETIME_to_ntfs(const FILETIME* filetime)
{
	ULARGE_INTEGER utc;
	utc.HighPart = filetime->dwHighDateTime;
	utc.LowPart  = filetime->dwLowDateTime;
	return utc.QuadPart;
}
#endif

/* Convert an MSDOS-style date/time into Unix-style time. */
int64_t
dos_to_unix(uint32_t dos_time)
{
	uint16_t msTime, msDate;
	struct tm ts;
	time_t t;

	msTime = (0xFFFF & dos_time);
	msDate = (dos_time >> 16);

	memset(&ts, 0, sizeof(ts));
	ts.tm_year = ((msDate >> 9) & 0x7f) + 80; /* Years since 1900. */
	ts.tm_mon = ((msDate >> 5) & 0x0f) - 1; /* Month number. */
	ts.tm_mday = msDate & 0x1f; /* Day of month. */
	ts.tm_hour = (msTime >> 11) & 0x1f;
	ts.tm_min = (msTime >> 5) & 0x3f;
	ts.tm_sec = (msTime << 1) & 0x3e;
	ts.tm_isdst = -1;
	t = mktime(&ts);
	return (int64_t)(t == (time_t)-1 ? INT32_MAX : t);
}

/* Convert into MSDOS-style date/time. */
uint32_t
unix_to_dos(int64_t unix_time)
{
	struct tm *t;
	uint32_t dt;
	time_t ut = unix_time;
#if defined(HAVE_LOCALTIME_R) || defined(HAVE_LOCALTIME_S)
	struct tm tmbuf;
#endif

	if (sizeof(time_t) < sizeof(int64_t) && (int64_t)ut != unix_time) {
		ut = (time_t)(unix_time > 0 ? INT32_MAX : INT32_MIN);
	}

#if defined(HAVE_LOCALTIME_S)
	t = localtime_s(&tmbuf, &ut) ? NULL : &tmbuf;
#elif defined(HAVE_LOCALTIME_R)
	t = localtime_r(&ut, &tmbuf);
#else
	t = localtime(&ut);
#endif
	dt = 0;
	if (t != NULL && t->tm_year >= INT_MIN + 80) {
		const int year = t->tm_year - 80;

		if (year & ~0x7f) {
			dt = year > 0 ? DOS_MAX_TIME : DOS_MIN_TIME;
		}
		else {
			dt += (year & 0x7f) << 9;
			dt += ((t->tm_mon + 1) & 0x0f) << 5;
			dt += (t->tm_mday & 0x1f);
			dt <<= 16;
			dt += (t->tm_hour & 0x1f) << 11;
			dt += (t->tm_min & 0x3f) << 5;
			/* Only counting every 2 seconds. */
			dt += (t->tm_sec & 0x3e) >> 1;
		}
	}
	if (dt > DOS_MAX_TIME) {
		dt = DOS_MAX_TIME;
	}
	else if (dt < DOS_MIN_TIME) {
		dt = DOS_MIN_TIME;
	}
	return dt;
}

/* Convert NTFS time to Unix sec/nsec */
void
ntfs_to_unix(uint64_t ntfs, int64_t* secs, uint32_t* nsecs)
{
	if (ntfs > INT64_MAX) {
		ntfs -= NTFS_EPOC_TICKS;
		*secs = ntfs / NTFS_TICKS;
		*nsecs = 100 * (ntfs % NTFS_TICKS);
	}
	else {
		int64_t value = (int64_t)ntfs - (int64_t)NTFS_EPOC_TICKS;

		/* amalgamated: lldiv() is C99; plain division truncates the same */
		*secs = value / NTFS_TICKS;
		*nsecs = (uint32_t)((value % NTFS_TICKS) * 100);
	}
}

/* Convert Unix sec/nsec to NTFS time */
uint64_t
unix_to_ntfs(int64_t secs, uint32_t nsecs)
{
	uint64_t ntfs;

	if (secs < -(int64_t)NTFS_EPOC_TIME)
		return 0;

	ntfs = secs + NTFS_EPOC_TIME;

	if (ntfs > UINT64_MAX / NTFS_TICKS)
		return UINT64_MAX;

	ntfs *= NTFS_TICKS;

	if (ntfs > UINT64_MAX - nsecs/100)
		return UINT64_MAX;

	return ntfs + nsecs/100;
}
#undef DOS_MAX_TIME
#undef DOS_MIN_TIME
#undef NTFS_EPOC_TICKS
#undef NTFS_EPOC_TIME
#undef NTFS_TICKS

#endif /* RAR_IMPLEMENTATION */
