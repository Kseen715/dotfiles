/*
 * lzmasdk.h -- the LZMA SDK 25.01 decoders, amalgamated into one
 * single-header library for os-rice.
 *
 * Upstream (https://www.7-zip.org/sdk.html, Igor Pavlov) is in the public
 * domain. This carries the decode side only: the 7z container reader, and
 * the LZMA, LZMA2, PPMd7, BCJ/BCJ2, delta and xz decoders under it. It is
 * two of the formats lib/archive.c has to open without help from the system
 * -- 7z and xz -- and, through xz, the compression layer under most of the
 * .tar.xz downloads lib/build.c fetches.
 *
 * Vendored the way thirdparty/yaml.h and thirdparty/bearssl.h are: one file
 * in the source tree, no submodule, no package to install, and the same
 * stb-style split -- including it plainly gets the declarations, and exactly
 * one translation unit defines the code:
 *
 *   #define LZMASDK_IMPLEMENTATION
 *   #include "../thirdparty/lzmasdk.h"
 *
 * That unit is lib/lzmasdk.c. Everywhere else just includes it.
 *
 * All C89, like the rest of this tree. Upstream's only non-C90 spelling is
 * the `//` comment, and the script that generated this file rewrote every
 * one of them to a block comment, so nob.c builds lib/lzmasdk.c at -std=c89
 * alongside everything else (at -w -- upstream's warnings are upstream's).
 *
 * Reading an archive, in short (see lib/archive.c for the whole thing):
 *
 *   CSzArEx db;    CFileInStream fs;   CLookToRead2 look;
 *   InFile_Open(&fs.file, path);
 *   FileInStream_CreateVTable(&fs);
 *   LookToRead2_CreateVTable(&look, False);
 *   SzArEx_Init(&db);
 *   SzArEx_Open(&db, &look.vt, &g_Alloc, &g_Alloc);
 *   SzArEx_Extract(&db, &look.vt, i, &blockIndex, &buf, &bufSize, ...);
 *
 * Regenerating:
 *
 *   curl -L -o lzma2501.7z https://www.7-zip.org/a/lzma2501.7z
 *   7z x -olzma2501 lzma2501.7z
 *   python3 amalgamate_lzmasdk.py lzma2501 lzmasdk.h
 *
 * What the script does to upstream, beyond concatenating: expands each local
 * #include once (so the file order below is upstream's own, taken from its
 * include lines rather than hardcoded here), rewrites `//` comments, drops
 * the `inline` keyword from Z7_FORCE_INLINE (C90 has no such word; the
 * always_inline attribute beside it stays), renames the statics two files
 * both claim, gives each source's file-scope macros an #undef after its body
 * so they do not leak into the next file, and turns off the one line in
 * Sha256.c that enables the SHA-NI code path, because the hand-written
 * assembly implementing it (Sha256Opt.c) is not carried -- see the module
 * docstring for why. System #includes are left where upstream put them:
 * hoisting <windows.h> out of its #ifdef _WIN32 is how that goes wrong.
 *
 * ---- upstream licence ----
 *
 * LZMA SDK is written and placed in the public domain by Igor Pavlov.
 *
 * Some code in LZMA SDK is based on public domain code from another
 * developers:
 *   1) PPMd var.H (2001): Dmitry Shkarin
 *   2) SHA-256: Wei Dai (Crypto++)
 *
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * the original LZMA SDK code, either in source code form or as a compiled
 * binary, for any purpose, commercial or non-commercial, and by any means.
 */

/* Single-threaded, and the plain byte-at-a-time CRC tables. The wide-table
 * CRC paths live in 7zCrcOpt.c / XzCrc64Opt.c, which this amalgamation does
 * not carry; a checksum is never what makes an install slow. */
#define Z7_ST
#define Z7_CRC_NUM_TABLES 1
#define Z7_CRC64_NUM_TABLES 1

/* 7zDec.c ships the PPMd branch of the 7z decoder switched off, and this
 * turns it on. Not optional here: Ppmd7.c and Ppmd7Dec.c are carried, and
 * the only #include of Ppmd7.h in the whole set is the one inside that
 * branch -- left off, the declarations the two sources need are inside a
 * dead #ifdef and the amalgamation does not compile. 7-Zip writes PPMd
 * blocks for text often enough that a decoder refusing them is a bug. */
#define Z7_PPMD_SUPPORT


/* ==== 7zTypes.h ==== */
/* 7zTypes.h -- Basic types
2024-01-24 : Igor Pavlov : Public domain */

#ifndef ZIP7_7Z_TYPES_H
#define ZIP7_7Z_TYPES_H

#ifdef _WIN32
/* #include <windows.h> */
#else
#include <errno.h>
#endif

#include <stddef.h>

#ifndef EXTERN_C_BEGIN
#ifdef __cplusplus
#define EXTERN_C_BEGIN extern "C" {
#define EXTERN_C_END }
#else
#define EXTERN_C_BEGIN
#define EXTERN_C_END
#endif
#endif

EXTERN_C_BEGIN

#define SZ_OK 0

#define SZ_ERROR_DATA 1
#define SZ_ERROR_MEM 2
#define SZ_ERROR_CRC 3
#define SZ_ERROR_UNSUPPORTED 4
#define SZ_ERROR_PARAM 5
#define SZ_ERROR_INPUT_EOF 6
#define SZ_ERROR_OUTPUT_EOF 7
#define SZ_ERROR_READ 8
#define SZ_ERROR_WRITE 9
#define SZ_ERROR_PROGRESS 10
#define SZ_ERROR_FAIL 11
#define SZ_ERROR_THREAD 12

#define SZ_ERROR_ARCHIVE 16
#define SZ_ERROR_NO_ARCHIVE 17

typedef int SRes;


#ifdef _MSC_VER
  #if _MSC_VER > 1200
    #define MY_ALIGN(n) __declspec(align(n))
  #else
    #define MY_ALIGN(n)
  #endif
#else
  /*
  // C11/C++11:
  #include <stdalign.h>
  #define MY_ALIGN(n) alignas(n)
  */
  #define MY_ALIGN(n) __attribute__ ((aligned(n)))
#endif


#ifdef _WIN32

/* typedef DWORD WRes; */
typedef unsigned WRes;
#define MY_SRes_HRESULT_FROM_WRes(x) HRESULT_FROM_WIN32(x)

/* #define MY_HRES_ERROR_INTERNAL_ERROR  MY_SRes_HRESULT_FROM_WRes(ERROR_INTERNAL_ERROR) */

#else /* _WIN32 */

/* #define ENV_HAVE_LSTAT */
typedef int WRes;

/* (FACILITY_ERRNO = 0x800) is 7zip's FACILITY constant to represent (errno) errors in HRESULT */
#define MY_FACILITY_ERRNO  0x800
#define MY_FACILITY_WIN32  7
#define MY_FACILITY_WRes  MY_FACILITY_ERRNO

#define MY_HRESULT_FROM_errno_CONST_ERROR(x) ((HRESULT)( \
          ( (HRESULT)(x) & 0x0000FFFF) \
          | (MY_FACILITY_WRes << 16)  \
          | (HRESULT)0x80000000 ))

#define MY_SRes_HRESULT_FROM_WRes(x) \
  ((HRESULT)(x) <= 0 ? ((HRESULT)(x)) : MY_HRESULT_FROM_errno_CONST_ERROR(x))

/* we call macro HRESULT_FROM_WIN32 for system errors (WRes) that are (errno) */
#define HRESULT_FROM_WIN32(x) MY_SRes_HRESULT_FROM_WRes(x)

/*
#define ERROR_FILE_NOT_FOUND             2L
#define ERROR_ACCESS_DENIED              5L
#define ERROR_NO_MORE_FILES              18L
#define ERROR_LOCK_VIOLATION             33L
#define ERROR_FILE_EXISTS                80L
#define ERROR_DISK_FULL                  112L
#define ERROR_NEGATIVE_SEEK              131L
#define ERROR_ALREADY_EXISTS             183L
#define ERROR_DIRECTORY                  267L
#define ERROR_TOO_MANY_POSTS             298L

#define ERROR_INTERNAL_ERROR             1359L
#define ERROR_INVALID_REPARSE_DATA       4392L
#define ERROR_REPARSE_TAG_INVALID        4393L
#define ERROR_REPARSE_TAG_MISMATCH       4394L
*/

/* we use errno equivalents for some WIN32 errors: */

#define ERROR_INVALID_PARAMETER     EINVAL
#define ERROR_INVALID_FUNCTION      EINVAL
#define ERROR_ALREADY_EXISTS        EEXIST
#define ERROR_FILE_EXISTS           EEXIST
#define ERROR_PATH_NOT_FOUND        ENOENT
#define ERROR_FILE_NOT_FOUND        ENOENT
#define ERROR_DISK_FULL             ENOSPC
/* #define ERROR_INVALID_HANDLE        EBADF */

/* we use FACILITY_WIN32 for errors that has no errno equivalent */
/* Too many posts were made to a semaphore. */
#define ERROR_TOO_MANY_POSTS        ((HRESULT)0x8007012AL)
#define ERROR_INVALID_REPARSE_DATA  ((HRESULT)0x80071128L)
#define ERROR_REPARSE_TAG_INVALID   ((HRESULT)0x80071129L)

/* if (MY_FACILITY_WRes != FACILITY_WIN32), */
/* we use FACILITY_WIN32 for COM errors: */
#define E_OUTOFMEMORY               ((HRESULT)0x8007000EL)
#define E_INVALIDARG                ((HRESULT)0x80070057L)
#define MY_E_ERROR_NEGATIVE_SEEK    ((HRESULT)0x80070083L)

/*
// we can use FACILITY_ERRNO for some COM errors, that have errno equivalents:
#define E_OUTOFMEMORY             MY_HRESULT_FROM_errno_CONST_ERROR(ENOMEM)
#define E_INVALIDARG              MY_HRESULT_FROM_errno_CONST_ERROR(EINVAL)
#define MY_E_ERROR_NEGATIVE_SEEK  MY_HRESULT_FROM_errno_CONST_ERROR(EINVAL)
*/

#define TEXT(quote) quote

#define FILE_ATTRIBUTE_READONLY       0x0001
#define FILE_ATTRIBUTE_HIDDEN         0x0002
#define FILE_ATTRIBUTE_SYSTEM         0x0004
#define FILE_ATTRIBUTE_DIRECTORY      0x0010
#define FILE_ATTRIBUTE_ARCHIVE        0x0020
#define FILE_ATTRIBUTE_DEVICE         0x0040
#define FILE_ATTRIBUTE_NORMAL         0x0080
#define FILE_ATTRIBUTE_TEMPORARY      0x0100
#define FILE_ATTRIBUTE_SPARSE_FILE    0x0200
#define FILE_ATTRIBUTE_REPARSE_POINT  0x0400
#define FILE_ATTRIBUTE_COMPRESSED     0x0800
#define FILE_ATTRIBUTE_OFFLINE        0x1000
#define FILE_ATTRIBUTE_NOT_CONTENT_INDEXED 0x2000
#define FILE_ATTRIBUTE_ENCRYPTED      0x4000

#define FILE_ATTRIBUTE_UNIX_EXTENSION 0x8000   /* trick for Unix */

#endif


#ifndef RINOK
#define RINOK(x) { const int _result_ = (x); if (_result_ != 0) return _result_; }
#endif

#ifndef RINOK_WRes
#define RINOK_WRes(x) { const WRes _result_ = (x); if (_result_ != 0) return _result_; }
#endif

typedef unsigned char Byte;
typedef short Int16;
typedef unsigned short UInt16;

#ifdef Z7_DECL_Int32_AS_long
typedef long Int32;
typedef unsigned long UInt32;
#else
typedef int Int32;
typedef unsigned int UInt32;
#endif


#ifndef _WIN32

typedef int INT;
typedef Int32 INT32;
typedef unsigned int UINT;
typedef UInt32 UINT32;
typedef INT32 LONG;   /* LONG, ULONG and DWORD must be 32-bit for _WIN32 compatibility */
typedef UINT32 ULONG;

#undef DWORD
typedef UINT32 DWORD;

#define VOID void

#define HRESULT LONG

typedef void *LPVOID;
/* typedef void VOID; */
/* typedef ULONG_PTR DWORD_PTR, *PDWORD_PTR; */
/* gcc / clang on Unix  : sizeof(long==sizeof(void*) in 32 or 64 bits) */
typedef          long  INT_PTR;
typedef unsigned long  UINT_PTR;
typedef          long  LONG_PTR;
typedef unsigned long  DWORD_PTR;

typedef size_t SIZE_T;

#endif /*  _WIN32 */


#define MY_HRES_ERROR_INTERNAL_ERROR  ((HRESULT)0x8007054FL)


#ifdef Z7_DECL_Int64_AS_long

typedef long Int64;
typedef unsigned long UInt64;

#else

#if (defined(_MSC_VER) || defined(__BORLANDC__)) && !defined(__clang__)
typedef __int64 Int64;
typedef unsigned __int64 UInt64;
#else
#if defined(__clang__) || defined(__GNUC__)
#include <stdint.h>
typedef int64_t Int64;
typedef uint64_t UInt64;
#else
typedef long long int Int64;
typedef unsigned long long int UInt64;
/* #define UINT64_CONST(n) n ## ULL */
#endif
#endif

#endif

#define UINT64_CONST(n) n


#ifdef Z7_DECL_SizeT_AS_unsigned_int
typedef unsigned int SizeT;
#else
typedef size_t SizeT;
#endif

/*
#if (defined(_MSC_VER) && _MSC_VER <= 1200)
typedef size_t MY_uintptr_t;
#else
#include <stdint.h>
typedef uintptr_t MY_uintptr_t;
#endif
*/

typedef int BoolInt;
/* typedef BoolInt Bool; */
#define True 1
#define False 0


#ifdef _WIN32
#define Z7_STDCALL __stdcall
#else
#define Z7_STDCALL
#endif

#ifdef _MSC_VER

#if _MSC_VER >= 1300
#define Z7_NO_INLINE __declspec(noinline)
#else
#define Z7_NO_INLINE
#endif

#define Z7_FORCE_INLINE __forceinline

#define Z7_CDECL      __cdecl
#define Z7_FASTCALL  __fastcall

#else /*  _MSC_VER */

#if (defined(__GNUC__) && (__GNUC__ >= 4)) \
    || (defined(__clang__) && (__clang_major__ >= 4)) \
    || defined(__INTEL_COMPILER) \
    || defined(__xlC__)
#define Z7_NO_INLINE      __attribute__((noinline))
#define Z7_FORCE_INLINE   __attribute__((always_inline))
#else
#define Z7_NO_INLINE
#define Z7_FORCE_INLINE
#endif

#define Z7_CDECL

#if  defined(_M_IX86) \
  || defined(__i386__)
/* #define Z7_FASTCALL __attribute__((fastcall)) */
/* #define Z7_FASTCALL __attribute__((cdecl)) */
#define Z7_FASTCALL
#elif defined(MY_CPU_AMD64)
/* #define Z7_FASTCALL __attribute__((ms_abi)) */
#define Z7_FASTCALL
#else
#define Z7_FASTCALL
#endif

#endif /*  _MSC_VER */


/* The following interfaces use first parameter as pointer to structure */

/* #define Z7_C_IFACE_CONST_QUAL */
#define Z7_C_IFACE_CONST_QUAL const

#define Z7_C_IFACE_DECL(a) \
  struct a ## _; \
  typedef Z7_C_IFACE_CONST_QUAL struct a ## _ * a ## Ptr; \
  typedef struct a ## _ a; \
  struct a ## _


Z7_C_IFACE_DECL (IByteIn)
{
  Byte (*Read)(IByteInPtr p); /* reads one byte, returns 0 in case of EOF or error */
};
#define IByteIn_Read(p) (p)->Read(p)


Z7_C_IFACE_DECL (IByteOut)
{
  void (*Write)(IByteOutPtr p, Byte b);
};
#define IByteOut_Write(p, b) (p)->Write(p, b)


Z7_C_IFACE_DECL (ISeqInStream)
{
  SRes (*Read)(ISeqInStreamPtr p, void *buf, size_t *size);
    /* if (input(*size) != 0 && output(*size) == 0) means end_of_stream.
       (output(*size) < input(*size)) is allowed */
};
#define ISeqInStream_Read(p, buf, size) (p)->Read(p, buf, size)

/* try to read as much as avail in stream and limited by (*processedSize) */
SRes SeqInStream_ReadMax(ISeqInStreamPtr stream, void *buf, size_t *processedSize);
/* it can return SZ_ERROR_INPUT_EOF */
/* SRes SeqInStream_Read(ISeqInStreamPtr stream, void *buf, size_t size); */
/* SRes SeqInStream_Read2(ISeqInStreamPtr stream, void *buf, size_t size, SRes errorType); */
SRes SeqInStream_ReadByte(ISeqInStreamPtr stream, Byte *buf);


Z7_C_IFACE_DECL (ISeqOutStream)
{
  size_t (*Write)(ISeqOutStreamPtr p, const void *buf, size_t size);
    /* Returns: result - the number of actually written bytes.
       (result < size) means error */
};
#define ISeqOutStream_Write(p, buf, size) (p)->Write(p, buf, size)

typedef enum
{
  SZ_SEEK_SET = 0,
  SZ_SEEK_CUR = 1,
  SZ_SEEK_END = 2
} ESzSeek;


Z7_C_IFACE_DECL (ISeekInStream)
{
  SRes (*Read)(ISeekInStreamPtr p, void *buf, size_t *size);  /* same as ISeqInStream::Read */
  SRes (*Seek)(ISeekInStreamPtr p, Int64 *pos, ESzSeek origin);
};
#define ISeekInStream_Read(p, buf, size)   (p)->Read(p, buf, size)
#define ISeekInStream_Seek(p, pos, origin) (p)->Seek(p, pos, origin)


Z7_C_IFACE_DECL (ILookInStream)
{
  SRes (*Look)(ILookInStreamPtr p, const void **buf, size_t *size);
    /* if (input(*size) != 0 && output(*size) == 0) means end_of_stream.
       (output(*size) > input(*size)) is not allowed
       (output(*size) < input(*size)) is allowed */
  SRes (*Skip)(ILookInStreamPtr p, size_t offset);
    /* offset must be <= output(*size) of Look */
  SRes (*Read)(ILookInStreamPtr p, void *buf, size_t *size);
    /* reads directly (without buffer). It's same as ISeqInStream::Read */
  SRes (*Seek)(ILookInStreamPtr p, Int64 *pos, ESzSeek origin);
};

#define ILookInStream_Look(p, buf, size)   (p)->Look(p, buf, size)
#define ILookInStream_Skip(p, offset)      (p)->Skip(p, offset)
#define ILookInStream_Read(p, buf, size)   (p)->Read(p, buf, size)
#define ILookInStream_Seek(p, pos, origin) (p)->Seek(p, pos, origin)


SRes LookInStream_LookRead(ILookInStreamPtr stream, void *buf, size_t *size);
SRes LookInStream_SeekTo(ILookInStreamPtr stream, UInt64 offset);

/* reads via ILookInStream::Read */
SRes LookInStream_Read2(ILookInStreamPtr stream, void *buf, size_t size, SRes errorType);
SRes LookInStream_Read(ILookInStreamPtr stream, void *buf, size_t size);


typedef struct
{
  ILookInStream vt;
  ISeekInStreamPtr realStream;
 
  size_t pos;
  size_t size; /* it's data size */
  
  /* the following variables must be set outside */
  Byte *buf;
  size_t bufSize;
} CLookToRead2;

void LookToRead2_CreateVTable(CLookToRead2 *p, int lookahead);

#define LookToRead2_INIT(p) { (p)->pos = (p)->size = 0; }


typedef struct
{
  ISeqInStream vt;
  ILookInStreamPtr realStream;
} CSecToLook;

void SecToLook_CreateVTable(CSecToLook *p);



typedef struct
{
  ISeqInStream vt;
  ILookInStreamPtr realStream;
} CSecToRead;

void SecToRead_CreateVTable(CSecToRead *p);


Z7_C_IFACE_DECL (ICompressProgress)
{
  SRes (*Progress)(ICompressProgressPtr p, UInt64 inSize, UInt64 outSize);
    /* Returns: result. (result != SZ_OK) means break.
       Value (UInt64)(Int64)-1 for size means unknown value. */
};

#define ICompressProgress_Progress(p, inSize, outSize) (p)->Progress(p, inSize, outSize)



typedef struct ISzAlloc ISzAlloc;
typedef const ISzAlloc * ISzAllocPtr;

struct ISzAlloc
{
  void *(*Alloc)(ISzAllocPtr p, size_t size);
  void (*Free)(ISzAllocPtr p, void *address); /* address can be 0 */
};

#define ISzAlloc_Alloc(p, size) (p)->Alloc(p, size)
#define ISzAlloc_Free(p, a) (p)->Free(p, a)

/* deprecated */
#define IAlloc_Alloc(p, size) ISzAlloc_Alloc(p, size)
#define IAlloc_Free(p, a) ISzAlloc_Free(p, a)





#ifndef MY_offsetof
  #ifdef offsetof
    #define MY_offsetof(type, m) offsetof(type, m)
    /*
    #define MY_offsetof(type, m) FIELD_OFFSET(type, m)
    */
  #else
    #define MY_offsetof(type, m) ((size_t)&(((type *)0)->m))
  #endif
#endif



#ifndef Z7_container_of

/*
#define Z7_container_of(ptr, type, m) container_of(ptr, type, m)
#define Z7_container_of(ptr, type, m) CONTAINING_RECORD(ptr, type, m)
#define Z7_container_of(ptr, type, m) ((type *)((char *)(ptr) - offsetof(type, m)))
#define Z7_container_of(ptr, type, m) (&((type *)0)->m == (ptr), ((type *)(((char *)(ptr)) - MY_offsetof(type, m))))
*/

/*
  GCC shows warning: "perhaps the 'offsetof' macro was used incorrectly"
    GCC 3.4.4 : classes with constructor
    GCC 4.8.1 : classes with non-public variable members"
*/

#define Z7_container_of(ptr, type, m) \
  ((type *)(void *)((char *)(void *) \
  (1 ? (ptr) : &((type *)NULL)->m) - MY_offsetof(type, m)))

#define Z7_container_of_CONST(ptr, type, m) \
  ((const type *)(const void *)((const char *)(const void *) \
  (1 ? (ptr) : &((type *)NULL)->m) - MY_offsetof(type, m)))

/*
#define Z7_container_of_NON_CONST_FROM_CONST(ptr, type, m) \
  ((type *)(void *)(const void *)((const char *)(const void *) \
  (1 ? (ptr) : &((type *)NULL)->m) - MY_offsetof(type, m)))
*/

#endif

#define Z7_CONTAINER_FROM_VTBL_SIMPLE(ptr, type, m) ((type *)(void *)(ptr))

/* #define Z7_CONTAINER_FROM_VTBL(ptr, type, m) Z7_CONTAINER_FROM_VTBL_SIMPLE(ptr, type, m) */
#define Z7_CONTAINER_FROM_VTBL(ptr, type, m) Z7_container_of(ptr, type, m)
/* #define Z7_CONTAINER_FROM_VTBL(ptr, type, m) Z7_container_of_NON_CONST_FROM_CONST(ptr, type, m) */

#define Z7_CONTAINER_FROM_VTBL_CONST(ptr, type, m) Z7_container_of_CONST(ptr, type, m)

#define Z7_CONTAINER_FROM_VTBL_CLS(ptr, type, m) Z7_CONTAINER_FROM_VTBL_SIMPLE(ptr, type, m)
/*
#define Z7_CONTAINER_FROM_VTBL_CLS(ptr, type, m) Z7_CONTAINER_FROM_VTBL(ptr, type, m)
*/
/* amalgamated: !__PCC__ added to upstream's guard. pcc defines __GNUC__ (4,
 * for glibc's headers) but its ccom rejects a _Pragma("GCC diagnostic ...")
 * outright -- "bad argument to #pragma", from every function that uses
 * Z7_CONTAINER_FROM_VTBL. The #else below defines both macros empty, which
 * costs a warning suppression and nothing else; nob.c builds this file with
 * -w anyway. */
#if (defined (__clang__) || defined(__GNUC__)) && !defined(__PCC__)
#define Z7_DIAGNOSTIC_IGNORE_BEGIN_CAST_QUAL \
  _Pragma("GCC diagnostic push") \
  _Pragma("GCC diagnostic ignored \"-Wcast-qual\"")
#define Z7_DIAGNOSTIC_IGNORE_END_CAST_QUAL \
  _Pragma("GCC diagnostic pop")
#else
#define Z7_DIAGNOSTIC_IGNORE_BEGIN_CAST_QUAL
#define Z7_DIAGNOSTIC_IGNORE_END_CAST_QUAL
#endif

#define Z7_CONTAINER_FROM_VTBL_TO_DECL_VAR(ptr, type, m, p) \
  Z7_DIAGNOSTIC_IGNORE_BEGIN_CAST_QUAL \
  type *p = Z7_CONTAINER_FROM_VTBL(ptr, type, m); \
  Z7_DIAGNOSTIC_IGNORE_END_CAST_QUAL

#define Z7_CONTAINER_FROM_VTBL_TO_DECL_VAR_pp_vt_p(type) \
  Z7_CONTAINER_FROM_VTBL_TO_DECL_VAR(pp, type, vt, p)


/* #define ZIP7_DECLARE_HANDLE(name)  typedef void *name; */
#define Z7_DECLARE_HANDLE(name)  struct name##_dummy{int unused;}; typedef struct name##_dummy *name;


#define Z7_memset_0_ARRAY(a)  memset((a), 0, sizeof(a))

#ifndef Z7_ARRAY_SIZE
#define Z7_ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif


#ifdef _WIN32

#define CHAR_PATH_SEPARATOR '\\'
#define WCHAR_PATH_SEPARATOR L'\\'
#define STRING_PATH_SEPARATOR "\\"
#define WSTRING_PATH_SEPARATOR L"\\"

#else

#define CHAR_PATH_SEPARATOR '/'
#define WCHAR_PATH_SEPARATOR L'/'
#define STRING_PATH_SEPARATOR "/"
#define WSTRING_PATH_SEPARATOR L"/"

#endif

#define k_PropVar_TimePrec_0        0
#define k_PropVar_TimePrec_Unix     1
#define k_PropVar_TimePrec_DOS      2
#define k_PropVar_TimePrec_HighPrec 3
#define k_PropVar_TimePrec_Base     16
#define k_PropVar_TimePrec_100ns (k_PropVar_TimePrec_Base + 7)
#define k_PropVar_TimePrec_1ns   (k_PropVar_TimePrec_Base + 9)

EXTERN_C_END

#endif

/*
#ifndef Z7_ST
#ifdef _7ZIP_ST
#define Z7_ST
#endif
#endif
*/

/* ==== CpuArch.h ==== */
/* CpuArch.h -- CPU specific code
Igor Pavlov : Public domain */

#ifndef ZIP7_INC_CPU_ARCH_H
#define ZIP7_INC_CPU_ARCH_H



EXTERN_C_BEGIN

/*
MY_CPU_LE means that CPU is LITTLE ENDIAN.
MY_CPU_BE means that CPU is BIG ENDIAN.
If MY_CPU_LE and MY_CPU_BE are not defined, we don't know about ENDIANNESS of platform.

MY_CPU_LE_UNALIGN means that CPU is LITTLE ENDIAN and CPU supports unaligned memory accesses.

MY_CPU_64BIT means that processor can work with 64-bit registers.
  MY_CPU_64BIT can be used to select fast code branch
  MY_CPU_64BIT doesn't mean that (sizeof(void *) == 8)
*/

#if !defined(_M_ARM64EC)
#if  defined(_M_X64) \
  || defined(_M_AMD64) \
  || defined(__x86_64__) \
  || defined(__AMD64__) \
  || defined(__amd64__)
  #define MY_CPU_AMD64
  #ifdef __ILP32__
    #define MY_CPU_NAME "x32"
    #define MY_CPU_SIZEOF_POINTER 4
  #else
    #define MY_CPU_NAME "x64"
    #define MY_CPU_SIZEOF_POINTER 8
  #endif
  #define MY_CPU_64BIT
#endif
#endif


#if  defined(_M_IX86) \
  || defined(__i386__)
  #define MY_CPU_X86
  #define MY_CPU_NAME "x86"
  /* #define MY_CPU_32BIT */
  #define MY_CPU_SIZEOF_POINTER 4
#endif

#if defined(__SSE2__) \
    || defined(MY_CPU_AMD64) \
    || defined(_M_IX86_FP) && (_M_IX86_FP >= 2)
#define MY_CPU_SSE2
#endif


#if  defined(_M_ARM64) \
  || defined(_M_ARM64EC) \
  || defined(__AARCH64EL__) \
  || defined(__AARCH64EB__) \
  || defined(__aarch64__)
  #define MY_CPU_ARM64
#if   defined(__ILP32__) \
   || defined(__SIZEOF_POINTER__) && (__SIZEOF_POINTER__ == 4)
    #define MY_CPU_NAME "arm64-32"
    #define MY_CPU_SIZEOF_POINTER 4
#elif defined(__SIZEOF_POINTER__) && (__SIZEOF_POINTER__ == 16)
    #define MY_CPU_NAME "arm64-128"
    #define MY_CPU_SIZEOF_POINTER 16
#else
#if defined(_M_ARM64EC)
    #define MY_CPU_NAME "arm64ec"
#else
    #define MY_CPU_NAME "arm64"
#endif
    #define MY_CPU_SIZEOF_POINTER 8
#endif
  #define MY_CPU_64BIT
#endif


#if  defined(_M_ARM) \
  || defined(_M_ARM_NT) \
  || defined(_M_ARMT) \
  || defined(__arm__) \
  || defined(__thumb__) \
  || defined(__ARMEL__) \
  || defined(__ARMEB__) \
  || defined(__THUMBEL__) \
  || defined(__THUMBEB__)
  #define MY_CPU_ARM

  #if defined(__thumb__) || defined(__THUMBEL__) || defined(_M_ARMT)
    #define MY_CPU_ARMT
    #define MY_CPU_NAME "armt"
  #else
    #define MY_CPU_ARM32
    #define MY_CPU_NAME "arm"
  #endif
  /* #define MY_CPU_32BIT */
  #define MY_CPU_SIZEOF_POINTER 4
#endif


#if  defined(_M_IA64) \
  || defined(__ia64__)
  #define MY_CPU_IA64
  #define MY_CPU_NAME "ia64"
  #define MY_CPU_64BIT
#endif


#if  defined(__mips64) \
  || defined(__mips64__) \
  || (defined(__mips) && (__mips == 64 || __mips == 4 || __mips == 3))
  #define MY_CPU_NAME "mips64"
  #define MY_CPU_64BIT
#elif defined(__mips__)
  #define MY_CPU_NAME "mips"
  /* #define MY_CPU_32BIT */
#endif


#if  defined(__ppc64__) \
  || defined(__powerpc64__) \
  || defined(__ppc__) \
  || defined(__powerpc__) \
  || defined(__PPC__) \
  || defined(_POWER)

#define MY_CPU_PPC_OR_PPC64

#if  defined(__ppc64__) \
  || defined(__powerpc64__) \
  || defined(_LP64) \
  || defined(__64BIT__)
  #ifdef __ILP32__
    #define MY_CPU_NAME "ppc64-32"
    #define MY_CPU_SIZEOF_POINTER 4
  #else
    #define MY_CPU_NAME "ppc64"
    #define MY_CPU_SIZEOF_POINTER 8
  #endif
  #define MY_CPU_64BIT
#else
  #define MY_CPU_NAME "ppc"
  #define MY_CPU_SIZEOF_POINTER 4
  /* #define MY_CPU_32BIT */
#endif
#endif


#if   defined(__sparc__) \
   || defined(__sparc)
  #define MY_CPU_SPARC
  #if  defined(__LP64__) \
    || defined(_LP64) \
    || defined(__SIZEOF_POINTER__) && (__SIZEOF_POINTER__ == 8)
    #define MY_CPU_NAME "sparcv9"
    #define MY_CPU_SIZEOF_POINTER 8
    #define MY_CPU_64BIT
  #elif defined(__sparc_v9__) \
     || defined(__sparcv9)
    #define MY_CPU_64BIT
    #if defined(__SIZEOF_POINTER__) && (__SIZEOF_POINTER__ == 4)
      #define MY_CPU_NAME "sparcv9-32"
    #else
      #define MY_CPU_NAME "sparcv9m"
    #endif
  #elif defined(__sparc_v8__) \
     || defined(__sparcv8)
    #define MY_CPU_NAME "sparcv8"
    #define MY_CPU_SIZEOF_POINTER 4
  #else
    #define MY_CPU_NAME "sparc"
  #endif
#endif


#if  defined(__riscv) \
  || defined(__riscv__)
    #define MY_CPU_RISCV
  #if __riscv_xlen == 32
    #define MY_CPU_NAME "riscv32"
  #elif __riscv_xlen == 64
    #define MY_CPU_NAME "riscv64"
  #else
    #define MY_CPU_NAME "riscv"
  #endif
#endif


#if defined(__loongarch__)
  #define MY_CPU_LOONGARCH
  #if defined(__loongarch64) || defined(__loongarch_grlen) && (__loongarch_grlen == 64)
  #define MY_CPU_64BIT
  #endif
  #if defined(__loongarch64)
  #define MY_CPU_NAME "loongarch64"
  #define MY_CPU_LOONGARCH64
  #else
  #define MY_CPU_NAME "loongarch"
  #endif
#endif


/* #undef MY_CPU_NAME */
/* #undef MY_CPU_SIZEOF_POINTER */
/* #define __e2k__ */
/* #define __SIZEOF_POINTER__ 4 */
#if  defined(__e2k__)
  #define MY_CPU_E2K
  #if defined(__ILP32__) || defined(__SIZEOF_POINTER__) && (__SIZEOF_POINTER__ == 4)
    #define MY_CPU_NAME "e2k-32"
    #define MY_CPU_SIZEOF_POINTER 4
  #else
    #define MY_CPU_NAME "e2k"
    #if defined(__LP64__) || defined(__SIZEOF_POINTER__) && (__SIZEOF_POINTER__ == 8)
      #define MY_CPU_SIZEOF_POINTER 8
    #endif
  #endif
  #define MY_CPU_64BIT
#endif


#if defined(MY_CPU_X86) || defined(MY_CPU_AMD64)
#define MY_CPU_X86_OR_AMD64
#endif

#if defined(MY_CPU_ARM) || defined(MY_CPU_ARM64)
#define MY_CPU_ARM_OR_ARM64
#endif


#ifdef _WIN32

  #ifdef MY_CPU_ARM
  #define MY_CPU_ARM_LE
  #endif

  #ifdef MY_CPU_ARM64
  #define MY_CPU_ARM64_LE
  #endif

  #ifdef _M_IA64
  #define MY_CPU_IA64_LE
  #endif

#endif


#if defined(MY_CPU_X86_OR_AMD64) \
    || defined(MY_CPU_ARM_LE) \
    || defined(MY_CPU_ARM64_LE) \
    || defined(MY_CPU_IA64_LE) \
    || defined(_LITTLE_ENDIAN) \
    || defined(__LITTLE_ENDIAN__) \
    || defined(__ARMEL__) \
    || defined(__THUMBEL__) \
    || defined(__AARCH64EL__) \
    || defined(__MIPSEL__) \
    || defined(__MIPSEL) \
    || defined(_MIPSEL) \
    || defined(__BFIN__) \
    || (defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__))
  #define MY_CPU_LE
#endif

#if defined(__BIG_ENDIAN__) \
    || defined(__ARMEB__) \
    || defined(__THUMBEB__) \
    || defined(__AARCH64EB__) \
    || defined(__MIPSEB__) \
    || defined(__MIPSEB) \
    || defined(_MIPSEB) \
    || defined(__m68k__) \
    || defined(__s390__) \
    || defined(__s390x__) \
    || defined(__zarch__) \
    || (defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__))
  #define MY_CPU_BE
#endif


#if defined(MY_CPU_LE) && defined(MY_CPU_BE)
  #error Stop_Compiling_Bad_Endian
#endif

#if !defined(MY_CPU_LE) && !defined(MY_CPU_BE)
  #error Stop_Compiling_CPU_ENDIAN_must_be_detected_at_compile_time
#endif

#if defined(MY_CPU_32BIT) && defined(MY_CPU_64BIT)
  #error Stop_Compiling_Bad_32_64_BIT
#endif

#ifdef __SIZEOF_POINTER__
  #ifdef MY_CPU_SIZEOF_POINTER
    #if MY_CPU_SIZEOF_POINTER != __SIZEOF_POINTER__
      #error Stop_Compiling_Bad_MY_CPU_PTR_SIZE
    #endif
  #else
    #define MY_CPU_SIZEOF_POINTER  __SIZEOF_POINTER__
  #endif
#endif

#if defined(MY_CPU_SIZEOF_POINTER) && (MY_CPU_SIZEOF_POINTER == 4)
#if defined (_LP64)
      #error Stop_Compiling_Bad_MY_CPU_PTR_SIZE
#endif
#endif

#ifdef _MSC_VER
  #if _MSC_VER >= 1300
    #define MY_CPU_pragma_pack_push_1   __pragma(pack(push, 1))
    #define MY_CPU_pragma_pop           __pragma(pack(pop))
  #else
    #define MY_CPU_pragma_pack_push_1
    #define MY_CPU_pragma_pop
  #endif
#else
  #ifdef __xlC__
    #define MY_CPU_pragma_pack_push_1   _Pragma("pack(1)")
    #define MY_CPU_pragma_pop           _Pragma("pack()")
  #else
    #define MY_CPU_pragma_pack_push_1   _Pragma("pack(push, 1)")
    #define MY_CPU_pragma_pop           _Pragma("pack(pop)")
  #endif
#endif


#ifndef MY_CPU_NAME
  /* #define MY_CPU_IS_UNKNOWN */
  #ifdef MY_CPU_LE
    #define MY_CPU_NAME "LE"
  #elif defined(MY_CPU_BE)
    #define MY_CPU_NAME "BE"
  #else
    /*
    #define MY_CPU_NAME ""
    */
  #endif
#endif





#ifdef __has_builtin
  #define Z7_has_builtin(x)  __has_builtin(x)
#else
  #define Z7_has_builtin(x)  0
#endif


#define Z7_BSWAP32_CONST(v) \
       ( (((UInt32)(v) << 24)                   ) \
       | (((UInt32)(v) <<  8) & (UInt32)0xff0000) \
       | (((UInt32)(v) >>  8) & (UInt32)0xff00  ) \
       | (((UInt32)(v) >> 24)                   ))


#if defined(_MSC_VER) && (_MSC_VER >= 1300)

#include <stdlib.h>

/* Note: these macros will use bswap instruction (486), that is unsupported in 386 cpu */

#pragma intrinsic(_byteswap_ushort)
#pragma intrinsic(_byteswap_ulong)
#pragma intrinsic(_byteswap_uint64)

#define Z7_BSWAP16(v)  _byteswap_ushort(v)
#define Z7_BSWAP32(v)  _byteswap_ulong (v)
#define Z7_BSWAP64(v)  _byteswap_uint64(v)
#define Z7_CPU_FAST_BSWAP_SUPPORTED

/* GCC can generate slow code that calls function for __builtin_bswap32() for:
     - GCC for RISCV, if Zbb/XTHeadBb extension is not used.
     - GCC for SPARC.
   The code from CLANG for SPARC also is not fastest.
   So we don't define Z7_CPU_FAST_BSWAP_SUPPORTED in some cases.
*/
#elif (!defined(MY_CPU_RISCV) || defined (__riscv_zbb) || defined(__riscv_xtheadbb)) \
    && !defined(MY_CPU_SPARC) \
    && ( \
       (defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 3))) \
    || (defined(__clang__) && Z7_has_builtin(__builtin_bswap16)) \
    )

#define Z7_BSWAP16(v)  __builtin_bswap16(v)
#define Z7_BSWAP32(v)  __builtin_bswap32(v)
#define Z7_BSWAP64(v)  __builtin_bswap64(v)
#define Z7_CPU_FAST_BSWAP_SUPPORTED

#else

#define Z7_BSWAP16(v) ((UInt16) \
       ( ((UInt32)(v) << 8) \
       | ((UInt32)(v) >> 8) \
       ))

#define Z7_BSWAP32(v) Z7_BSWAP32_CONST(v)

#define Z7_BSWAP64(v) \
       ( ( ( (UInt64)(v)                           ) << 8 * 7 ) \
       | ( ( (UInt64)(v) & ((UInt32)0xff << 8 * 1) ) << 8 * 5 ) \
       | ( ( (UInt64)(v) & ((UInt32)0xff << 8 * 2) ) << 8 * 3 ) \
       | ( ( (UInt64)(v) & ((UInt32)0xff << 8 * 3) ) << 8 * 1 ) \
       | ( ( (UInt64)(v) >> 8 * 1 ) & ((UInt32)0xff << 8 * 3) ) \
       | ( ( (UInt64)(v) >> 8 * 3 ) & ((UInt32)0xff << 8 * 2) ) \
       | ( ( (UInt64)(v) >> 8 * 5 ) & ((UInt32)0xff << 8 * 1) ) \
       | ( ( (UInt64)(v) >> 8 * 7 )                           ) \
       )

#endif



#ifdef MY_CPU_LE
  #if defined(MY_CPU_X86_OR_AMD64) \
      || defined(MY_CPU_ARM64) \
      || defined(MY_CPU_RISCV) && defined(__riscv_misaligned_fast) \
      || defined(MY_CPU_E2K) && defined(__iset__) && (__iset__ >= 6)
    #define MY_CPU_LE_UNALIGN
    #define MY_CPU_LE_UNALIGN_64
  #elif defined(__ARM_FEATURE_UNALIGNED)
/* === ALIGNMENT on 32-bit arm and LDRD/STRD/LDM/STM instructions.
  Description of problems:
problem-1 : 32-bit ARM architecture:
  multi-access (pair of 32-bit accesses) instructions (LDRD/STRD/LDM/STM)
  require 32-bit (WORD) alignment (by 32-bit ARM architecture).
  So there is "Alignment fault exception", if data is not aligned for 32-bit.

problem-2 : 32-bit kernels and arm64 kernels:
  32-bit linux kernels provide fixup for these "paired" instruction "Alignment fault exception".
  So unaligned paired-access instructions work via exception handler in kernel in 32-bit linux.
 
  But some arm64 kernels do not handle these faults in 32-bit programs.
  So we have unhandled exception for such instructions.
  Probably some new arm64 kernels have fixed it, and unaligned
  paired-access instructions work in new kernels?

problem-3 : compiler for 32-bit arm:
  Compilers use LDRD/STRD/LDM/STM for UInt64 accesses
  and for another cases where two 32-bit accesses are fused
  to one multi-access instruction.
  So UInt64 variables must be aligned for 32-bit, and each
  32-bit access must be aligned for 32-bit, if we want to
  avoid "Alignment fault" exception (handled or unhandled).

problem-4 : performace:
  Even if unaligned access is handled by kernel, it will be slow.
  So if we allow unaligned access, we can get fast unaligned
  single-access, and slow unaligned paired-access.

  We don't allow unaligned access on 32-bit arm, because compiler
  genarates paired-access instructions that require 32-bit alignment,
  and some arm64 kernels have no handler for these instructions.
  Also unaligned paired-access instructions will be slow, if kernel handles them.
*/
    /* it must be disabled: */
    /* #define MY_CPU_LE_UNALIGN */
  #endif
#endif


#ifdef MY_CPU_LE_UNALIGN

#define GetUi16(p) (*(const UInt16 *)(const void *)(p))
#define GetUi32(p) (*(const UInt32 *)(const void *)(p))
#ifdef MY_CPU_LE_UNALIGN_64
#define GetUi64(p) (*(const UInt64 *)(const void *)(p))
#define SetUi64(p, v) { *(UInt64 *)(void *)(p) = (v); }
#endif

#define SetUi16(p, v) { *(UInt16 *)(void *)(p) = (v); }
#define SetUi32(p, v) { *(UInt32 *)(void *)(p) = (v); }

#else

#define GetUi16(p) ( (UInt16) ( \
             ((const Byte *)(p))[0] | \
    ((UInt16)((const Byte *)(p))[1] << 8) ))

#define GetUi32(p) ( \
             ((const Byte *)(p))[0]        | \
    ((UInt32)((const Byte *)(p))[1] <<  8) | \
    ((UInt32)((const Byte *)(p))[2] << 16) | \
    ((UInt32)((const Byte *)(p))[3] << 24))

#define SetUi16(p, v) { Byte *_ppp_ = (Byte *)(p); UInt32 _vvv_ = (v); \
    _ppp_[0] = (Byte)_vvv_; \
    _ppp_[1] = (Byte)(_vvv_ >> 8); }

#define SetUi32(p, v) { Byte *_ppp_ = (Byte *)(p); UInt32 _vvv_ = (v); \
    _ppp_[0] = (Byte)_vvv_; \
    _ppp_[1] = (Byte)(_vvv_ >> 8); \
    _ppp_[2] = (Byte)(_vvv_ >> 16); \
    _ppp_[3] = (Byte)(_vvv_ >> 24); }

#endif


#ifndef GetUi64
#define GetUi64(p) (GetUi32(p) | ((UInt64)GetUi32(((const Byte *)(p)) + 4) << 32))
#endif

#ifndef SetUi64
#define SetUi64(p, v) { Byte *_ppp2_ = (Byte *)(p); UInt64 _vvv2_ = (v); \
    SetUi32(_ppp2_    , (UInt32)_vvv2_) \
    SetUi32(_ppp2_ + 4, (UInt32)(_vvv2_ >> 32)) }
#endif


#if defined(MY_CPU_LE_UNALIGN) && defined(Z7_CPU_FAST_BSWAP_SUPPORTED)

#if 0
/* Z7_BSWAP16 can be slow for x86-msvc */
#define GetBe16_to32(p)  (Z7_BSWAP16 (*(const UInt16 *)(const void *)(p)))
#else
#define GetBe16_to32(p)  (Z7_BSWAP32 (*(const UInt16 *)(const void *)(p)) >> 16)
#endif

#define GetBe32(p)  Z7_BSWAP32 (*(const UInt32 *)(const void *)(p))
#define SetBe32(p, v) { (*(UInt32 *)(void *)(p)) = Z7_BSWAP32(v); }

#if defined(MY_CPU_LE_UNALIGN_64)
#define GetBe64(p)  Z7_BSWAP64 (*(const UInt64 *)(const void *)(p))
#define SetBe64(p, v) { (*(UInt64 *)(void *)(p)) = Z7_BSWAP64(v); }
#endif

#else

#define GetBe32(p) ( \
    ((UInt32)((const Byte *)(p))[0] << 24) | \
    ((UInt32)((const Byte *)(p))[1] << 16) | \
    ((UInt32)((const Byte *)(p))[2] <<  8) | \
             ((const Byte *)(p))[3] )

#define SetBe32(p, v) { Byte *_ppp_ = (Byte *)(p); UInt32 _vvv_ = (v); \
    _ppp_[0] = (Byte)(_vvv_ >> 24); \
    _ppp_[1] = (Byte)(_vvv_ >> 16); \
    _ppp_[2] = (Byte)(_vvv_ >> 8); \
    _ppp_[3] = (Byte)_vvv_; }

#endif

#ifndef GetBe64
#define GetBe64(p) (((UInt64)GetBe32(p) << 32) | GetBe32(((const Byte *)(p)) + 4))
#endif

#ifndef SetBe64
#define SetBe64(p, v) { Byte *_ppp_ = (Byte *)(p); UInt64 _vvv_ = (v); \
    _ppp_[0] = (Byte)(_vvv_ >> 56); \
    _ppp_[1] = (Byte)(_vvv_ >> 48); \
    _ppp_[2] = (Byte)(_vvv_ >> 40); \
    _ppp_[3] = (Byte)(_vvv_ >> 32); \
    _ppp_[4] = (Byte)(_vvv_ >> 24); \
    _ppp_[5] = (Byte)(_vvv_ >> 16); \
    _ppp_[6] = (Byte)(_vvv_ >> 8); \
    _ppp_[7] = (Byte)_vvv_; }
#endif

#ifndef GetBe16
#ifdef GetBe16_to32
#define GetBe16(p) ( (UInt16) GetBe16_to32(p))
#else
#define GetBe16(p) ( (UInt16) ( \
    ((UInt16)((const Byte *)(p))[0] << 8) | \
             ((const Byte *)(p))[1] ))
#endif
#endif


#if defined(MY_CPU_BE)
#define Z7_CONV_BE_TO_NATIVE_CONST32(v)  (v)
#define Z7_CONV_LE_TO_NATIVE_CONST32(v)  Z7_BSWAP32_CONST(v)
#define Z7_CONV_NATIVE_TO_BE_32(v)       (v)
/* #define Z7_GET_NATIVE16_FROM_2_BYTES(b0, b1)  ((b1) | ((b0) << 8)) */
#elif defined(MY_CPU_LE)
#define Z7_CONV_BE_TO_NATIVE_CONST32(v)  Z7_BSWAP32_CONST(v)
#define Z7_CONV_LE_TO_NATIVE_CONST32(v)  (v)
#define Z7_CONV_NATIVE_TO_BE_32(v)       Z7_BSWAP32(v)
/* #define Z7_GET_NATIVE16_FROM_2_BYTES(b0, b1)  ((b0) | ((b1) << 8)) */
#else
#error Stop_Compiling_Unknown_Endian_CONV
#endif


#if defined(MY_CPU_BE)

#define GetBe64a(p)      (*(const UInt64 *)(const void *)(p))
#define GetBe32a(p)      (*(const UInt32 *)(const void *)(p))
#define GetBe16a(p)      (*(const UInt16 *)(const void *)(p))
#define SetBe32a(p, v)   { *(UInt32 *)(void *)(p) = (v); }
#define SetBe16a(p, v)   { *(UInt16 *)(void *)(p) = (v); }

#define GetUi64a(p)      GetUi64(p)
#define GetUi32a(p)      GetUi32(p)
#define GetUi16a(p)      GetUi16(p)
#define SetUi32a(p, v)   SetUi32(p, v)
#define SetUi16a(p, v)   SetUi16(p, v)

#elif defined(MY_CPU_LE)

#define GetUi64a(p)      (*(const UInt64 *)(const void *)(p))
#define GetUi32a(p)      (*(const UInt32 *)(const void *)(p))
#define GetUi16a(p)      (*(const UInt16 *)(const void *)(p))
#define SetUi32a(p, v)   { *(UInt32 *)(void *)(p) = (v); }
#define SetUi16a(p, v)   { *(UInt16 *)(void *)(p) = (v); }

#define GetBe64a(p)      GetBe64(p)
#define GetBe32a(p)      GetBe32(p)
#define GetBe16a(p)      GetBe16(p)
#define SetBe32a(p, v)   SetBe32(p, v)
#define SetBe16a(p, v)   SetBe16(p, v)

#else
#error Stop_Compiling_Unknown_Endian_CPU_a
#endif


#ifndef GetBe16_to32
#define GetBe16_to32(p) GetBe16(p)
#endif


#if defined(MY_CPU_X86_OR_AMD64) \
  || defined(MY_CPU_ARM_OR_ARM64) \
  || defined(MY_CPU_PPC_OR_PPC64)
  #define Z7_CPU_FAST_ROTATE_SUPPORTED
#endif


#ifdef MY_CPU_X86_OR_AMD64

void Z7_FASTCALL z7_x86_cpuid(UInt32 a[4], UInt32 function);
UInt32 Z7_FASTCALL z7_x86_cpuid_GetMaxFunc(void);
#if defined(MY_CPU_AMD64)
#define Z7_IF_X86_CPUID_SUPPORTED
#else
#define Z7_IF_X86_CPUID_SUPPORTED if (z7_x86_cpuid_GetMaxFunc())
#endif

BoolInt CPU_IsSupported_AES(void);
BoolInt CPU_IsSupported_AVX(void);
BoolInt CPU_IsSupported_AVX2(void);
BoolInt CPU_IsSupported_AVX512F_AVX512VL(void);
BoolInt CPU_IsSupported_VAES_AVX2(void);
BoolInt CPU_IsSupported_CMOV(void);
BoolInt CPU_IsSupported_SSE(void);
BoolInt CPU_IsSupported_SSE2(void);
BoolInt CPU_IsSupported_SSSE3(void);
BoolInt CPU_IsSupported_SSE41(void);
BoolInt CPU_IsSupported_SHA(void);
BoolInt CPU_IsSupported_SHA512(void);
BoolInt CPU_IsSupported_PageGB(void);

#elif defined(MY_CPU_ARM_OR_ARM64)

BoolInt CPU_IsSupported_CRC32(void);
BoolInt CPU_IsSupported_NEON(void);

#if defined(_WIN32)
BoolInt CPU_IsSupported_CRYPTO(void);
#define CPU_IsSupported_SHA1  CPU_IsSupported_CRYPTO
#define CPU_IsSupported_SHA2  CPU_IsSupported_CRYPTO
#define CPU_IsSupported_AES   CPU_IsSupported_CRYPTO
#else
BoolInt CPU_IsSupported_SHA1(void);
BoolInt CPU_IsSupported_SHA2(void);
BoolInt CPU_IsSupported_AES(void);
#endif
BoolInt CPU_IsSupported_SHA512(void);

#endif

#if defined(__APPLE__)
int z7_sysctlbyname_Get(const char *name, void *buf, size_t *bufSize);
int z7_sysctlbyname_Get_UInt32(const char *name, UInt32 *val);
#endif

EXTERN_C_END

#endif

/* ==== Alloc.h ==== */
/* Alloc.h -- Memory allocation functions
2024-01-22 : Igor Pavlov : Public domain */

#ifndef ZIP7_INC_ALLOC_H
#define ZIP7_INC_ALLOC_H



EXTERN_C_BEGIN

/*
  MyFree(NULL)        : is allowed, as free(NULL)
  MyAlloc(0)          : returns NULL : but malloc(0)        is allowed to return NULL or non_NULL
  MyRealloc(NULL, 0)  : returns NULL : but realloc(NULL, 0) is allowed to return NULL or non_NULL
MyRealloc() is similar to realloc() for the following cases:
  MyRealloc(non_NULL, 0)         : returns NULL and always calls MyFree(ptr)
  MyRealloc(NULL, non_ZERO)      : returns NULL, if allocation failed
  MyRealloc(non_NULL, non_ZERO)  : returns NULL, if reallocation failed
*/

void *MyAlloc(size_t size);
void MyFree(void *address);
void *MyRealloc(void *address, size_t size);

void *z7_AlignedAlloc(size_t size);
void  z7_AlignedFree(void *p);

#ifdef _WIN32

#ifdef Z7_LARGE_PAGES
void SetLargePageSize(void);
#endif

void *MidAlloc(size_t size);
void MidFree(void *address);
void *BigAlloc(size_t size);
void BigFree(void *address);

/* #define Z7_BIG_ALLOC_IS_ZERO_FILLED */

#else

#define MidAlloc(size)    z7_AlignedAlloc(size)
#define MidFree(address)  z7_AlignedFree(address)
#define BigAlloc(size)    z7_AlignedAlloc(size)
#define BigFree(address)  z7_AlignedFree(address)

#endif

extern const ISzAlloc g_Alloc;

#ifdef _WIN32
extern const ISzAlloc g_BigAlloc;
extern const ISzAlloc g_MidAlloc;
#else
#define g_BigAlloc g_AlignedAlloc
#define g_MidAlloc g_AlignedAlloc
#endif

extern const ISzAlloc g_AlignedAlloc;


typedef struct
{
  ISzAlloc vt;
  ISzAllocPtr baseAlloc;
  unsigned numAlignBits; /* ((1 << numAlignBits) >= sizeof(void *)) */
  size_t offset;         /* (offset == (k * sizeof(void *)) && offset < (1 << numAlignBits) */
} CAlignOffsetAlloc;

void AlignOffsetAlloc_CreateVTable(CAlignOffsetAlloc *p);


EXTERN_C_END

#endif

/* ==== 7zCrc.h ==== */
/* 7zCrc.h -- CRC32 calculation
2024-01-22 : Igor Pavlov : Public domain */

#ifndef ZIP7_INC_7Z_CRC_H
#define ZIP7_INC_7Z_CRC_H



EXTERN_C_BEGIN

extern UInt32 g_CrcTable[];

/* Call CrcGenerateTable one time before other CRC functions */
void Z7_FASTCALL CrcGenerateTable(void);

#define CRC_INIT_VAL 0xFFFFFFFF
#define CRC_GET_DIGEST(crc) ((crc) ^ CRC_INIT_VAL)
#define CRC_UPDATE_BYTE(crc, b) (g_CrcTable[((crc) ^ (b)) & 0xFF] ^ ((crc) >> 8))

UInt32 Z7_FASTCALL CrcUpdate(UInt32 crc, const void *data, size_t size);
UInt32 Z7_FASTCALL CrcCalc(const void *data, size_t size);

typedef UInt32 (Z7_FASTCALL *Z7_CRC_UPDATE_FUNC)(UInt32 v, const void *data, size_t size);
Z7_CRC_UPDATE_FUNC z7_GetFunc_CrcUpdate(unsigned algo);

EXTERN_C_END

#endif

/* ==== 7zBuf.h ==== */
/* 7zBuf.h -- Byte Buffer
2023-03-04 : Igor Pavlov : Public domain */

#ifndef ZIP7_INC_7Z_BUF_H
#define ZIP7_INC_7Z_BUF_H



EXTERN_C_BEGIN

typedef struct
{
  Byte *data;
  size_t size;
} CBuf;

void Buf_Init(CBuf *p);
int Buf_Create(CBuf *p, size_t size, ISzAllocPtr alloc);
void Buf_Free(CBuf *p, ISzAllocPtr alloc);

typedef struct
{
  Byte *data;
  size_t size;
  size_t pos;
} CDynBuf;

void DynBuf_Construct(CDynBuf *p);
void DynBuf_SeekToBeg(CDynBuf *p);
int DynBuf_Write(CDynBuf *p, const Byte *buf, size_t size, ISzAllocPtr alloc);
void DynBuf_Free(CDynBuf *p, ISzAllocPtr alloc);

EXTERN_C_END

#endif

/* ==== 7zFile.h ==== */
/* 7zFile.h -- File IO
2023-03-05 : Igor Pavlov : Public domain */

#ifndef ZIP7_INC_FILE_H
#define ZIP7_INC_FILE_H

#ifdef _WIN32
#define USE_WINDOWS_FILE
/* #include <windows.h> */
#endif

#ifdef USE_WINDOWS_FILE
/* ==== 7zWindows.h ==== */
/* 7zWindows.h -- StdAfx
2023-04-02 : Igor Pavlov : Public domain */

#ifndef ZIP7_INC_7Z_WINDOWS_H
#define ZIP7_INC_7Z_WINDOWS_H

#ifdef _WIN32

#if defined(__clang__)
# pragma clang diagnostic push
#endif

#if defined(_MSC_VER)

#pragma warning(push)
#pragma warning(disable : 4668) /* '_WIN32_WINNT' is not defined as a preprocessor macro, replacing with '0' for '#if/#elif' */

#if _MSC_VER == 1900
/* for old kit10 versions */
/* #pragma warning(disable : 4255) // winuser.h(13979): warning C4255: 'GetThreadDpiAwarenessContext': */
#endif
/* win10 Windows Kit: */
#endif /* _MSC_VER */

#if defined(_MSC_VER) && _MSC_VER <= 1200 && !defined(_WIN64)
/* for msvc6 without sdk2003 */
#define RPC_NO_WINDOWS_H
#endif

#if defined(__MINGW32__) || defined(__MINGW64__)
/* #if defined(__GNUC__) && !defined(__clang__) */
#include <windows.h>
#else
#include <Windows.h>
#endif
/* #include <basetsd.h> */
/* #include <wtypes.h> */

/* but if precompiled with clang-cl then we need */
/* #include <windows.h> */
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#if defined(__clang__)
# pragma clang diagnostic pop
#endif

#if defined(_MSC_VER) && _MSC_VER <= 1200 && !defined(_WIN64)
#ifndef _W64

typedef long LONG_PTR, *PLONG_PTR;
typedef unsigned long ULONG_PTR, *PULONG_PTR;
typedef ULONG_PTR DWORD_PTR, *PDWORD_PTR;

#define Z7_OLD_WIN_SDK
#endif /* _W64 */
#endif /* _MSC_VER == 1200 */

#ifdef Z7_OLD_WIN_SDK

#ifndef INVALID_FILE_ATTRIBUTES
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#endif
#ifndef INVALID_SET_FILE_POINTER
#define INVALID_SET_FILE_POINTER ((DWORD)-1)
#endif
#ifndef FILE_SPECIAL_ACCESS
#define FILE_SPECIAL_ACCESS    (FILE_ANY_ACCESS)
#endif

/* ShlObj.h: */
/* #define BIF_NEWDIALOGSTYLE     0x0040 */

#pragma warning(disable : 4201)
/* #pragma warning(disable : 4115) */

#undef  VARIANT_TRUE
#define VARIANT_TRUE ((VARIANT_BOOL)-1)
#endif

#endif /* Z7_OLD_WIN_SDK */

#ifdef UNDER_CE
#undef  VARIANT_TRUE
#define VARIANT_TRUE ((VARIANT_BOOL)-1)
#endif


#if defined(_MSC_VER)
#if _MSC_VER >= 1400 && _MSC_VER <= 1600
  /* BaseTsd.h(148) : 'HandleToULong' : unreferenced inline function has been removed */
  /* string.h */
  /* #pragma warning(disable : 4514) */
#endif
#endif


/* #include "7zTypes.h" */

#endif

#else
/* note: USE_FOPEN mode is limited to 32-bit file size */
/* #define USE_FOPEN */
/* #include <stdio.h> */
#endif



EXTERN_C_BEGIN

/* ---------- File ---------- */

typedef struct
{
  #ifdef USE_WINDOWS_FILE
  HANDLE handle;
  #elif defined(USE_FOPEN)
  FILE *file;
  #else
  int fd;
  #endif
} CSzFile;

void File_Construct(CSzFile *p);
#if !defined(UNDER_CE) || !defined(USE_WINDOWS_FILE)
WRes InFile_Open(CSzFile *p, const char *name);
WRes OutFile_Open(CSzFile *p, const char *name);
#endif
#ifdef USE_WINDOWS_FILE
WRes InFile_OpenW(CSzFile *p, const WCHAR *name);
WRes OutFile_OpenW(CSzFile *p, const WCHAR *name);
#endif
WRes File_Close(CSzFile *p);

/* reads max(*size, remain file's size) bytes */
WRes File_Read(CSzFile *p, void *data, size_t *size);

/* writes *size bytes */
WRes File_Write(CSzFile *p, const void *data, size_t *size);

WRes File_Seek(CSzFile *p, Int64 *pos, ESzSeek origin);
WRes File_GetLength(CSzFile *p, UInt64 *length);


/* ---------- FileInStream ---------- */

typedef struct
{
  ISeqInStream vt;
  CSzFile file;
  WRes wres;
} CFileSeqInStream;

void FileSeqInStream_CreateVTable(CFileSeqInStream *p);


typedef struct
{
  ISeekInStream vt;
  CSzFile file;
  WRes wres;
} CFileInStream;

void FileInStream_CreateVTable(CFileInStream *p);


typedef struct
{
  ISeqOutStream vt;
  CSzFile file;
  WRes wres;
} CFileOutStream;

void FileOutStream_CreateVTable(CFileOutStream *p);

EXTERN_C_END

#endif

/* ==== LzmaDec.h ==== */
/* LzmaDec.h -- LZMA Decoder
2023-04-02 : Igor Pavlov : Public domain */

#ifndef ZIP7_INC_LZMA_DEC_H
#define ZIP7_INC_LZMA_DEC_H



EXTERN_C_BEGIN

/* #define Z7_LZMA_PROB32 */
/* Z7_LZMA_PROB32 can increase the speed on some CPUs,
   but memory usage for CLzmaDec::probs will be doubled in that case */

typedef
#ifdef Z7_LZMA_PROB32
  UInt32
#else
  UInt16
#endif
  CLzmaProb;


/* ---------- LZMA Properties ---------- */

#define LZMA_PROPS_SIZE 5

typedef struct
{
  Byte lc;
  Byte lp;
  Byte pb;
  Byte _pad_;
  UInt32 dicSize;
} CLzmaProps;

/* LzmaProps_Decode - decodes properties
Returns:
  SZ_OK
  SZ_ERROR_UNSUPPORTED - Unsupported properties
*/

SRes LzmaProps_Decode(CLzmaProps *p, const Byte *data, unsigned size);


/* ---------- LZMA Decoder state ---------- */

/* LZMA_REQUIRED_INPUT_MAX = number of required input bytes for worst case.
   Num bits = log2((2^11 / 31) ^ 22) + 26 < 134 + 26 = 160; */

#define LZMA_REQUIRED_INPUT_MAX 20

typedef struct
{
  /* Don't change this structure. ASM code can use it. */
  CLzmaProps prop;
  CLzmaProb *probs;
  CLzmaProb *probs_1664;
  Byte *dic;
  SizeT dicBufSize;
  SizeT dicPos;
  const Byte *buf;
  UInt32 range;
  UInt32 code;
  UInt32 processedPos;
  UInt32 checkDicSize;
  UInt32 reps[4];
  UInt32 state;
  UInt32 remainLen;

  UInt32 numProbs;
  unsigned tempBufSize;
  Byte tempBuf[LZMA_REQUIRED_INPUT_MAX];
} CLzmaDec;

#define LzmaDec_CONSTRUCT(p) { (p)->dic = NULL; (p)->probs = NULL; }
#define LzmaDec_Construct(p) LzmaDec_CONSTRUCT(p)

void LzmaDec_Init(CLzmaDec *p);

/* There are two types of LZMA streams:
     - Stream with end mark. That end mark adds about 6 bytes to compressed size.
     - Stream without end mark. You must know exact uncompressed size to decompress such stream. */

typedef enum
{
  LZMA_FINISH_ANY,   /* finish at any point */
  LZMA_FINISH_END    /* block must be finished at the end */
} ELzmaFinishMode;

/* ELzmaFinishMode has meaning only if the decoding reaches output limit !!!

   You must use LZMA_FINISH_END, when you know that current output buffer
   covers last bytes of block. In other cases you must use LZMA_FINISH_ANY.

   If LZMA decoder sees end marker before reaching output limit, it returns SZ_OK,
   and output value of destLen will be less than output buffer size limit.
   You can check status result also.

   You can use multiple checks to test data integrity after full decompression:
     1) Check Result and "status" variable.
     2) Check that output(destLen) = uncompressedSize, if you know real uncompressedSize.
     3) Check that output(srcLen) = compressedSize, if you know real compressedSize.
        You must use correct finish mode in that case. */

typedef enum
{
  LZMA_STATUS_NOT_SPECIFIED,               /* use main error code instead */
  LZMA_STATUS_FINISHED_WITH_MARK,          /* stream was finished with end mark. */
  LZMA_STATUS_NOT_FINISHED,                /* stream was not finished */
  LZMA_STATUS_NEEDS_MORE_INPUT,            /* you must provide more input bytes */
  LZMA_STATUS_MAYBE_FINISHED_WITHOUT_MARK  /* there is probability that stream was finished without end mark */
} ELzmaStatus;

/* ELzmaStatus is used only as output value for function call */


/* ---------- Interfaces ---------- */

/* There are 3 levels of interfaces:
     1) Dictionary Interface
     2) Buffer Interface
     3) One Call Interface
   You can select any of these interfaces, but don't mix functions from different
   groups for same object. */


/* There are two variants to allocate state for Dictionary Interface:
     1) LzmaDec_Allocate / LzmaDec_Free
     2) LzmaDec_AllocateProbs / LzmaDec_FreeProbs
   You can use variant 2, if you set dictionary buffer manually.
   For Buffer Interface you must always use variant 1.

LzmaDec_Allocate* can return:
  SZ_OK
  SZ_ERROR_MEM         - Memory allocation error
  SZ_ERROR_UNSUPPORTED - Unsupported properties
*/
   
SRes LzmaDec_AllocateProbs(CLzmaDec *p, const Byte *props, unsigned propsSize, ISzAllocPtr alloc);
void LzmaDec_FreeProbs(CLzmaDec *p, ISzAllocPtr alloc);

SRes LzmaDec_Allocate(CLzmaDec *p, const Byte *props, unsigned propsSize, ISzAllocPtr alloc);
void LzmaDec_Free(CLzmaDec *p, ISzAllocPtr alloc);

/* ---------- Dictionary Interface ---------- */

/* You can use it, if you want to eliminate the overhead for data copying from
   dictionary to some other external buffer.
   You must work with CLzmaDec variables directly in this interface.

   STEPS:
     LzmaDec_Construct()
     LzmaDec_Allocate()
     for (each new stream)
     {
       LzmaDec_Init()
       while (it needs more decompression)
       {
         LzmaDec_DecodeToDic()
         use data from CLzmaDec::dic and update CLzmaDec::dicPos
       }
     }
     LzmaDec_Free()
*/

/* LzmaDec_DecodeToDic
   
   The decoding to internal dictionary buffer (CLzmaDec::dic).
   You must manually update CLzmaDec::dicPos, if it reaches CLzmaDec::dicBufSize !!!

finishMode:
  It has meaning only if the decoding reaches output limit (dicLimit).
  LZMA_FINISH_ANY - Decode just dicLimit bytes.
  LZMA_FINISH_END - Stream must be finished after dicLimit.

Returns:
  SZ_OK
    status:
      LZMA_STATUS_FINISHED_WITH_MARK
      LZMA_STATUS_NOT_FINISHED
      LZMA_STATUS_NEEDS_MORE_INPUT
      LZMA_STATUS_MAYBE_FINISHED_WITHOUT_MARK
  SZ_ERROR_DATA - Data error
  SZ_ERROR_FAIL - Some unexpected error: internal error of code, memory corruption or hardware failure
*/

SRes LzmaDec_DecodeToDic(CLzmaDec *p, SizeT dicLimit,
    const Byte *src, SizeT *srcLen, ELzmaFinishMode finishMode, ELzmaStatus *status);


/* ---------- Buffer Interface ---------- */

/* It's zlib-like interface.
   See LzmaDec_DecodeToDic description for information about STEPS and return results,
   but you must use LzmaDec_DecodeToBuf instead of LzmaDec_DecodeToDic and you don't need
   to work with CLzmaDec variables manually.

finishMode:
  It has meaning only if the decoding reaches output limit (*destLen).
  LZMA_FINISH_ANY - Decode just destLen bytes.
  LZMA_FINISH_END - Stream must be finished after (*destLen).
*/

SRes LzmaDec_DecodeToBuf(CLzmaDec *p, Byte *dest, SizeT *destLen,
    const Byte *src, SizeT *srcLen, ELzmaFinishMode finishMode, ELzmaStatus *status);


/* ---------- One Call Interface ---------- */

/* LzmaDecode

finishMode:
  It has meaning only if the decoding reaches output limit (*destLen).
  LZMA_FINISH_ANY - Decode just destLen bytes.
  LZMA_FINISH_END - Stream must be finished after (*destLen).

Returns:
  SZ_OK
    status:
      LZMA_STATUS_FINISHED_WITH_MARK
      LZMA_STATUS_NOT_FINISHED
      LZMA_STATUS_MAYBE_FINISHED_WITHOUT_MARK
  SZ_ERROR_DATA - Data error
  SZ_ERROR_MEM  - Memory allocation error
  SZ_ERROR_UNSUPPORTED - Unsupported properties
  SZ_ERROR_INPUT_EOF - It needs more bytes in input buffer (src).
  SZ_ERROR_FAIL - Some unexpected error: internal error of code, memory corruption or hardware failure
*/

SRes LzmaDecode(Byte *dest, SizeT *destLen, const Byte *src, SizeT *srcLen,
    const Byte *propData, unsigned propSize, ELzmaFinishMode finishMode,
    ELzmaStatus *status, ISzAllocPtr alloc);

EXTERN_C_END

#endif

/* ==== 7z.h ==== */
/* 7z.h -- 7z interface
2023-04-02 : Igor Pavlov : Public domain */

#ifndef ZIP7_INC_7Z_H
#define ZIP7_INC_7Z_H



EXTERN_C_BEGIN

#define k7zStartHeaderSize 0x20
#define k7zSignatureSize 6

extern const Byte k7zSignature[k7zSignatureSize];

typedef struct
{
  const Byte *Data;
  size_t Size;
} CSzData;

/* CSzCoderInfo & CSzFolder support only default methods */

typedef struct
{
  size_t PropsOffset;
  UInt32 MethodID;
  Byte NumStreams;
  Byte PropsSize;
} CSzCoderInfo;

typedef struct
{
  UInt32 InIndex;
  UInt32 OutIndex;
} CSzBond;

#define SZ_NUM_CODERS_IN_FOLDER_MAX 4
#define SZ_NUM_BONDS_IN_FOLDER_MAX 3
#define SZ_NUM_PACK_STREAMS_IN_FOLDER_MAX 4

typedef struct
{
  UInt32 NumCoders;
  UInt32 NumBonds;
  UInt32 NumPackStreams;
  UInt32 UnpackStream;
  UInt32 PackStreams[SZ_NUM_PACK_STREAMS_IN_FOLDER_MAX];
  CSzBond Bonds[SZ_NUM_BONDS_IN_FOLDER_MAX];
  CSzCoderInfo Coders[SZ_NUM_CODERS_IN_FOLDER_MAX];
} CSzFolder;


SRes SzGetNextFolderItem(CSzFolder *f, CSzData *sd);

typedef struct
{
  UInt32 Low;
  UInt32 High;
} CNtfsFileTime;

typedef struct
{
  Byte *Defs; /* MSB 0 bit numbering */
  UInt32 *Vals;
} CSzBitUi32s;

typedef struct
{
  Byte *Defs; /* MSB 0 bit numbering */
  /* UInt64 *Vals; */
  CNtfsFileTime *Vals;
} CSzBitUi64s;

#define SzBitArray_Check(p, i) (((p)[(i) >> 3] & (0x80 >> ((i) & 7))) != 0)

#define SzBitWithVals_Check(p, i) ((p)->Defs && ((p)->Defs[(i) >> 3] & (0x80 >> ((i) & 7))) != 0)

typedef struct
{
  UInt32 NumPackStreams;
  UInt32 NumFolders;

  UInt64 *PackPositions;          /* NumPackStreams + 1 */
  CSzBitUi32s FolderCRCs;         /* NumFolders */

  size_t *FoCodersOffsets;        /* NumFolders + 1 */
  UInt32 *FoStartPackStreamIndex; /* NumFolders + 1 */
  UInt32 *FoToCoderUnpackSizes;   /* NumFolders + 1 */
  Byte *FoToMainUnpackSizeIndex;  /* NumFolders */
  UInt64 *CoderUnpackSizes;       /* for all coders in all folders */

  Byte *CodersData;

  UInt64 RangeLimit;
} CSzAr;

UInt64 SzAr_GetFolderUnpackSize(const CSzAr *p, UInt32 folderIndex);

SRes SzAr_DecodeFolder(const CSzAr *p, UInt32 folderIndex,
    ILookInStreamPtr stream, UInt64 startPos,
    Byte *outBuffer, size_t outSize,
    ISzAllocPtr allocMain);

typedef struct
{
  CSzAr db;

  UInt64 startPosAfterHeader;
  UInt64 dataPos;
  
  UInt32 NumFiles;

  UInt64 *UnpackPositions;  /* NumFiles + 1 */
  /* Byte *IsEmptyFiles; */
  Byte *IsDirs;
  CSzBitUi32s CRCs;

  CSzBitUi32s Attribs;
  /* CSzBitUi32s Parents; */
  CSzBitUi64s MTime;
  CSzBitUi64s CTime;

  UInt32 *FolderToFile;   /* NumFolders + 1 */
  UInt32 *FileToFolder;   /* NumFiles */

  size_t *FileNameOffsets; /* in 2-byte steps */
  Byte *FileNames;  /* UTF-16-LE */
} CSzArEx;

#define SzArEx_IsDir(p, i) (SzBitArray_Check((p)->IsDirs, i))

#define SzArEx_GetFileSize(p, i) ((p)->UnpackPositions[(i) + 1] - (p)->UnpackPositions[i])

void SzArEx_Init(CSzArEx *p);
void SzArEx_Free(CSzArEx *p, ISzAllocPtr alloc);
UInt64 SzArEx_GetFolderStreamPos(const CSzArEx *p, UInt32 folderIndex, UInt32 indexInFolder);
int SzArEx_GetFolderFullPackSize(const CSzArEx *p, UInt32 folderIndex, UInt64 *resSize);

/*
if dest == NULL, the return value specifies the required size of the buffer,
  in 16-bit characters, including the null-terminating character.
if dest != NULL, the return value specifies the number of 16-bit characters that
  are written to the dest, including the null-terminating character. */

size_t SzArEx_GetFileNameUtf16(const CSzArEx *p, size_t fileIndex, UInt16 *dest);

/*
size_t SzArEx_GetFullNameLen(const CSzArEx *p, size_t fileIndex);
UInt16 *SzArEx_GetFullNameUtf16_Back(const CSzArEx *p, size_t fileIndex, UInt16 *dest);
*/



/*
  SzArEx_Extract extracts file from archive

  *outBuffer must be 0 before first call for each new archive.

  Extracting cache:
    If you need to decompress more than one file, you can send
    these values from previous call:
      *blockIndex,
      *outBuffer,
      *outBufferSize
    You can consider "*outBuffer" as cache of solid block. If your archive is solid,
    it will increase decompression speed.
  
    If you use external function, you can declare these 3 cache variables
    (blockIndex, outBuffer, outBufferSize) as static in that external function.
    
    Free *outBuffer and set *outBuffer to 0, if you want to flush cache.
*/

SRes SzArEx_Extract(
    const CSzArEx *db,
    ILookInStreamPtr inStream,
    UInt32 fileIndex,         /* index of file */
    UInt32 *blockIndex,       /* index of solid block */
    Byte **outBuffer,         /* pointer to pointer to output buffer (allocated with allocMain) */
    size_t *outBufferSize,    /* buffer size for output buffer */
    size_t *offset,           /* offset of stream for required file in *outBuffer */
    size_t *outSizeProcessed, /* size of file in *outBuffer */
    ISzAllocPtr allocMain,
    ISzAllocPtr allocTemp);


/*
SzArEx_Open Errors:
SZ_ERROR_NO_ARCHIVE
SZ_ERROR_ARCHIVE
SZ_ERROR_UNSUPPORTED
SZ_ERROR_MEM
SZ_ERROR_CRC
SZ_ERROR_INPUT_EOF
SZ_ERROR_FAIL
*/

SRes SzArEx_Open(CSzArEx *p, ILookInStreamPtr inStream,
    ISzAllocPtr allocMain, ISzAllocPtr allocTemp);

EXTERN_C_END

#endif

/* ==== Xz.h ==== */
/* Xz.h - Xz interface
Igor Pavlov : Public domain */

#ifndef ZIP7_INC_XZ_H
#define ZIP7_INC_XZ_H

/* ==== Sha256.h ==== */
/* Sha256.h -- SHA-256 Hash
: Igor Pavlov : Public domain */

#ifndef ZIP7_INC_SHA256_H
#define ZIP7_INC_SHA256_H



EXTERN_C_BEGIN

#define SHA256_NUM_BLOCK_WORDS  16
#define SHA256_NUM_DIGEST_WORDS  8

#define SHA256_BLOCK_SIZE   (SHA256_NUM_BLOCK_WORDS * 4)
#define SHA256_DIGEST_SIZE  (SHA256_NUM_DIGEST_WORDS * 4)




typedef void (Z7_FASTCALL *SHA256_FUNC_UPDATE_BLOCKS)(UInt32 state[8], const Byte *data, size_t numBlocks);

/*
  if (the system supports different SHA256 code implementations)
  {
    (CSha256::func_UpdateBlocks) will be used
    (CSha256::func_UpdateBlocks) can be set by
       Sha256_Init()        - to default (fastest)
       Sha256_SetFunction() - to any algo
  }
  else
  {
    (CSha256::func_UpdateBlocks) is ignored.
  }
*/

typedef struct
{
  union
  {
    struct
    {
      SHA256_FUNC_UPDATE_BLOCKS func_UpdateBlocks;
      UInt64 count;
    } vars;
    UInt64 _pad_64bit[4];
    void *_pad_align_ptr[2];
  } v;
  UInt32 state[SHA256_NUM_DIGEST_WORDS];

  Byte buffer[SHA256_BLOCK_SIZE];
} CSha256;


#define SHA256_ALGO_DEFAULT 0
#define SHA256_ALGO_SW      1
#define SHA256_ALGO_HW      2

/*
Sha256_SetFunction()
return:
  0 - (algo) value is not supported, and func_UpdateBlocks was not changed
  1 - func_UpdateBlocks was set according (algo) value.
*/

BoolInt Sha256_SetFunction(CSha256 *p, unsigned algo);

void Sha256_InitState(CSha256 *p);
void Sha256_Init(CSha256 *p);
void Sha256_Update(CSha256 *p, const Byte *data, size_t size);
void Sha256_Final(CSha256 *p, Byte *digest);




/* void Z7_FASTCALL Sha256_UpdateBlocks(UInt32 state[8], const Byte *data, size_t numBlocks); */

/*
call Sha256Prepare() once at program start.
It prepares all supported implementations, and detects the fastest implementation.
*/

void Sha256Prepare(void);

EXTERN_C_END

#endif
/* ==== Delta.h ==== */
/* Delta.h -- Delta converter
2023-03-03 : Igor Pavlov : Public domain */

#ifndef ZIP7_INC_DELTA_H
#define ZIP7_INC_DELTA_H



EXTERN_C_BEGIN

#define DELTA_STATE_SIZE 256

void Delta_Init(Byte *state);
void Delta_Encode(Byte *state, unsigned delta, Byte *data, SizeT size);
void Delta_Decode(Byte *state, unsigned delta, Byte *data, SizeT size);

EXTERN_C_END

#endif

EXTERN_C_BEGIN

#define XZ_ID_Subblock 1
#define XZ_ID_Delta 3
#define XZ_ID_X86   4
#define XZ_ID_PPC   5
#define XZ_ID_IA64  6
#define XZ_ID_ARM   7
#define XZ_ID_ARMT  8
#define XZ_ID_SPARC 9
#define XZ_ID_ARM64 0xa
#define XZ_ID_RISCV 0xb
#define XZ_ID_LZMA2 0x21

unsigned Xz_ReadVarInt(const Byte *p, size_t maxSize, UInt64 *value);
unsigned Xz_WriteVarInt(Byte *buf, UInt64 v);

/* ---------- xz block ---------- */

#define XZ_BLOCK_HEADER_SIZE_MAX 1024

#define XZ_NUM_FILTERS_MAX 4
#define XZ_BF_NUM_FILTERS_MASK 3
#define XZ_BF_PACK_SIZE (1 << 6)
#define XZ_BF_UNPACK_SIZE (1 << 7)

#define XZ_FILTER_PROPS_SIZE_MAX 20

typedef struct
{
  UInt64 id;
  UInt32 propsSize;
  Byte props[XZ_FILTER_PROPS_SIZE_MAX];
} CXzFilter;

typedef struct
{
  UInt64 packSize;
  UInt64 unpackSize;
  Byte flags;
  CXzFilter filters[XZ_NUM_FILTERS_MAX];
} CXzBlock;

#define XzBlock_GetNumFilters(p) (((unsigned)(p)->flags & XZ_BF_NUM_FILTERS_MASK) + 1)
#define XzBlock_HasPackSize(p)   (((p)->flags & XZ_BF_PACK_SIZE) != 0)
#define XzBlock_HasUnpackSize(p) (((p)->flags & XZ_BF_UNPACK_SIZE) != 0)
#define XzBlock_HasUnsupportedFlags(p) (((p)->flags & ~(XZ_BF_NUM_FILTERS_MASK | XZ_BF_PACK_SIZE | XZ_BF_UNPACK_SIZE)) != 0)

SRes XzBlock_Parse(CXzBlock *p, const Byte *header);
SRes XzBlock_ReadHeader(CXzBlock *p, ISeqInStreamPtr inStream, BoolInt *isIndex, UInt32 *headerSizeRes);

/* ---------- xz stream ---------- */

#define XZ_SIG_SIZE 6
#define XZ_FOOTER_SIG_SIZE 2

extern const Byte XZ_SIG[XZ_SIG_SIZE];

/*
extern const Byte XZ_FOOTER_SIG[XZ_FOOTER_SIG_SIZE];
*/

#define XZ_FOOTER_SIG_0 'Y'
#define XZ_FOOTER_SIG_1 'Z'

#define XZ_STREAM_FLAGS_SIZE 2
#define XZ_STREAM_CRC_SIZE 4

#define XZ_STREAM_HEADER_SIZE (XZ_SIG_SIZE + XZ_STREAM_FLAGS_SIZE + XZ_STREAM_CRC_SIZE)
#define XZ_STREAM_FOOTER_SIZE (XZ_FOOTER_SIG_SIZE + XZ_STREAM_FLAGS_SIZE + XZ_STREAM_CRC_SIZE + 4)

#define XZ_CHECK_MASK 0xF
#define XZ_CHECK_NO 0
#define XZ_CHECK_CRC32 1
#define XZ_CHECK_CRC64 4
#define XZ_CHECK_SHA256 10

typedef struct
{
  unsigned mode;
  UInt32 crc;
  UInt64 crc64;
  CSha256 sha;
} CXzCheck;

void XzCheck_Init(CXzCheck *p, unsigned mode);
void XzCheck_Update(CXzCheck *p, const void *data, size_t size);
int XzCheck_Final(CXzCheck *p, Byte *digest);

typedef UInt16 CXzStreamFlags;

#define XzFlags_IsSupported(f) ((f) <= XZ_CHECK_MASK)
#define XzFlags_GetCheckType(f) ((f) & XZ_CHECK_MASK)
#define XzFlags_HasDataCrc32(f) (Xz_GetCheckType(f) == XZ_CHECK_CRC32)
unsigned XzFlags_GetCheckSize(CXzStreamFlags f);

SRes Xz_ParseHeader(CXzStreamFlags *p, const Byte *buf);
SRes Xz_ReadHeader(CXzStreamFlags *p, ISeqInStreamPtr inStream);

typedef struct
{
  UInt64 unpackSize;
  UInt64 totalSize;
} CXzBlockSizes;

typedef struct
{
  CXzStreamFlags flags;
  /* Byte _pad[6]; */
  size_t numBlocks;
  CXzBlockSizes *blocks;
  UInt64 startOffset;
} CXzStream;

#define Xz_CONSTRUCT(p) { (p)->numBlocks = 0;  (p)->blocks = NULL;  (p)->flags = 0; }
void Xz_Construct(CXzStream *p);
void Xz_Free(CXzStream *p, ISzAllocPtr alloc);

#define XZ_SIZE_OVERFLOW ((UInt64)(Int64)-1)

UInt64 Xz_GetUnpackSize(const CXzStream *p);
UInt64 Xz_GetPackSize(const CXzStream *p);

typedef struct
{
  size_t num;
  size_t numAllocated;
  CXzStream *streams;
} CXzs;

#define Xzs_CONSTRUCT(p) { (p)->num = 0;  (p)->numAllocated = 0;  (p)->streams = NULL; }
void Xzs_Construct(CXzs *p);
void Xzs_Free(CXzs *p, ISzAllocPtr alloc);
/*
Xzs_ReadBackward() must be called for empty CXzs object.
Xzs_ReadBackward() can return non empty object with (p->num != 0) even in case of error.
*/
SRes Xzs_ReadBackward(CXzs *p, ILookInStreamPtr inStream, Int64 *startOffset, ICompressProgressPtr progress, ISzAllocPtr alloc);

UInt64 Xzs_GetNumBlocks(const CXzs *p);
UInt64 Xzs_GetUnpackSize(const CXzs *p);


/* ECoderStatus values are identical to ELzmaStatus values of LZMA2 decoder */

typedef enum
{
  CODER_STATUS_NOT_SPECIFIED,               /* use main error code instead */
  CODER_STATUS_FINISHED_WITH_MARK,          /* stream was finished with end mark. */
  CODER_STATUS_NOT_FINISHED,                /* stream was not finished */
  CODER_STATUS_NEEDS_MORE_INPUT             /* you must provide more input bytes */
} ECoderStatus;


/* ECoderFinishMode values are identical to ELzmaFinishMode */

typedef enum
{
  CODER_FINISH_ANY,   /* finish at any point */
  CODER_FINISH_END    /* block must be finished at the end */
} ECoderFinishMode;


typedef struct
{
  void *p; /* state object; */
  void (*Free)(void *p, ISzAllocPtr alloc);
  SRes (*SetProps)(void *p, const Byte *props, size_t propSize, ISzAllocPtr alloc);
  void (*Init)(void *p);
  SRes (*Code2)(void *p, Byte *dest, SizeT *destLen, const Byte *src, SizeT *srcLen,
      int srcWasFinished, ECoderFinishMode finishMode,
      /* int *wasFinished, */
      ECoderStatus *status);
  SizeT (*Filter)(void *p, Byte *data, SizeT size);
} IStateCoder;


typedef struct
{
  UInt32 methodId;
  UInt32 delta;
  UInt32 ip;
  UInt32 X86_State;
  Byte delta_State[DELTA_STATE_SIZE];
} CXzBcFilterStateBase;

typedef SizeT (*Xz_Func_BcFilterStateBase_Filter)(CXzBcFilterStateBase *p, Byte *data, SizeT size);

SRes Xz_StateCoder_Bc_SetFromMethod_Func(IStateCoder *p, UInt64 id,
    Xz_Func_BcFilterStateBase_Filter func, ISzAllocPtr alloc);


#define MIXCODER_NUM_FILTERS_MAX 4

typedef struct
{
  ISzAllocPtr alloc;
  Byte *buf;
  unsigned numCoders;

  Byte *outBuf;
  size_t outBufSize;
  size_t outWritten; /* is equal to lzmaDecoder.dicPos (in outBuf mode) */
  BoolInt wasFinished;
  SRes res;
  ECoderStatus status;
  /* BoolInt SingleBufMode; */
  
  int finished[MIXCODER_NUM_FILTERS_MAX - 1];
  size_t pos[MIXCODER_NUM_FILTERS_MAX - 1];
  size_t size[MIXCODER_NUM_FILTERS_MAX - 1];
  UInt64 ids[MIXCODER_NUM_FILTERS_MAX];
  SRes results[MIXCODER_NUM_FILTERS_MAX];
  IStateCoder coders[MIXCODER_NUM_FILTERS_MAX];
} CMixCoder;


typedef enum
{
  XZ_STATE_STREAM_HEADER,
  XZ_STATE_STREAM_INDEX,
  XZ_STATE_STREAM_INDEX_CRC,
  XZ_STATE_STREAM_FOOTER,
  XZ_STATE_STREAM_PADDING,
  XZ_STATE_BLOCK_HEADER,
  XZ_STATE_BLOCK,
  XZ_STATE_BLOCK_FOOTER
} EXzState;


typedef struct
{
  EXzState state;
  unsigned pos;
  unsigned alignPos;
  unsigned indexPreSize;

  CXzStreamFlags streamFlags;
  
  unsigned blockHeaderSize;
  UInt64 packSize;
  UInt64 unpackSize;

  UInt64 numBlocks; /* number of finished blocks in current stream */
  UInt64 indexSize;
  UInt64 indexPos;
  UInt64 padSize;

  UInt64 numStartedStreams;
  UInt64 numFinishedStreams;
  UInt64 numTotalBlocks;

  UInt32 crc;
  CMixCoder decoder;
  CXzBlock block;
  CXzCheck check;
  CSha256 sha;

  BoolInt parseMode;
  BoolInt headerParsedOk;
  BoolInt decodeToStreamSignature;
  unsigned decodeOnlyOneBlock;

  Byte *outBuf;
  size_t outBufSize;
  size_t outDataWritten; /* the size of data in (outBuf) that were fully unpacked */

  UInt32 shaDigest32[SHA256_DIGEST_SIZE / 4];
  Byte buf[XZ_BLOCK_HEADER_SIZE_MAX]; /* it must be aligned for 4-bytes */
} CXzUnpacker;

/* alloc : aligned for cache line allocation is better */
void XzUnpacker_Construct(CXzUnpacker *p, ISzAllocPtr alloc);
void XzUnpacker_Init(CXzUnpacker *p);
void XzUnpacker_SetOutBuf(CXzUnpacker *p, Byte *outBuf, size_t outBufSize);
void XzUnpacker_Free(CXzUnpacker *p);

/*
  XzUnpacker
  The sequence for decoding functions:
  {
    XzUnpacker_Construct()
    [Decoding_Calls]
    XzUnpacker_Free()
  }

  [Decoding_Calls]

  There are 3 types of interfaces for [Decoding_Calls] calls:

  Interface-1 : Partial output buffers:
    {
      XzUnpacker_Init()
      for()
      {
        XzUnpacker_Code();
      }
      XzUnpacker_IsStreamWasFinished()
    }
    
  Interface-2 : Direct output buffer:
    Use it, if you know exact size of decoded data, and you need
    whole xz unpacked data in one output buffer.
    xz unpacker doesn't allocate additional buffer for lzma2 dictionary in that mode.
    {
      XzUnpacker_Init()
      XzUnpacker_SetOutBufMode(); // to set output buffer and size
      for()
      {
        XzUnpacker_Code(); // (dest = NULL) in XzUnpacker_Code()
      }
      XzUnpacker_IsStreamWasFinished()
    }

  Interface-3 : Direct output buffer : One call full decoding
    It unpacks whole input buffer to output buffer in one call.
    It uses Interface-2 internally.
    {
      XzUnpacker_CodeFull()
      XzUnpacker_IsStreamWasFinished()
    }
*/

/*
finishMode:
  It has meaning only if the decoding reaches output limit (*destLen).
  CODER_FINISH_ANY - use smallest number of input bytes
  CODER_FINISH_END - read EndOfStream marker after decoding

Returns:
  SZ_OK
    status:
      CODER_STATUS_NOT_FINISHED,
      CODER_STATUS_NEEDS_MORE_INPUT - the decoder can return it in two cases:
         1) it needs more input data to finish current xz stream
         2) xz stream was finished successfully. But the decoder supports multiple
            concatented xz streams. So it expects more input data for new xz streams.
         Call XzUnpacker_IsStreamWasFinished() to check that latest xz stream was finished successfully.

  SZ_ERROR_MEM  - Memory allocation error
  SZ_ERROR_DATA - Data error
  SZ_ERROR_UNSUPPORTED - Unsupported method or method properties
  SZ_ERROR_CRC  - CRC error
  // SZ_ERROR_INPUT_EOF - It needs more bytes in input buffer (src).

  SZ_ERROR_NO_ARCHIVE - the error with xz Stream Header with one of the following reasons:
     - xz Stream Signature failure
     - CRC32 of xz Stream Header is failed
     - The size of Stream padding is not multiple of four bytes.
    It's possible to get that error, if xz stream was finished and the stream
    contains some another data. In that case you can call XzUnpacker_GetExtraSize()
    function to get real size of xz stream.
*/


SRes XzUnpacker_Code(CXzUnpacker *p, Byte *dest, SizeT *destLen,
    const Byte *src, SizeT *srcLen, int srcFinished,
    ECoderFinishMode finishMode, ECoderStatus *status);

SRes XzUnpacker_CodeFull(CXzUnpacker *p, Byte *dest, SizeT *destLen,
    const Byte *src, SizeT *srcLen,
    ECoderFinishMode finishMode, ECoderStatus *status);

/*
If you decode full xz stream(s), then you can call XzUnpacker_IsStreamWasFinished()
after successful XzUnpacker_CodeFull() or after last call of XzUnpacker_Code().
*/

BoolInt XzUnpacker_IsStreamWasFinished(const CXzUnpacker *p);

/*
XzUnpacker_GetExtraSize() returns then number of unconfirmed bytes,
 if it's in (XZ_STATE_STREAM_HEADER) state or in (XZ_STATE_STREAM_PADDING) state.
These bytes can be some data after xz archive, or
it can be start of new xz stream.
 
Call XzUnpacker_GetExtraSize() after XzUnpacker_Code() function to detect real size of
xz stream in two cases, if XzUnpacker_Code() returns:
  res == SZ_OK && status == CODER_STATUS_NEEDS_MORE_INPUT
  res == SZ_ERROR_NO_ARCHIVE
*/

UInt64 XzUnpacker_GetExtraSize(const CXzUnpacker *p);


/*
  for random block decoding:
    XzUnpacker_Init();
    set CXzUnpacker::streamFlags
    XzUnpacker_PrepareToRandomBlockDecoding()
    loop
    {
      XzUnpacker_Code()
      XzUnpacker_IsBlockFinished()
    }
*/

void XzUnpacker_PrepareToRandomBlockDecoding(CXzUnpacker *p);
BoolInt XzUnpacker_IsBlockFinished(const CXzUnpacker *p);

#define XzUnpacker_GetPackSizeForIndex(p) ((p)->packSize + (p)->blockHeaderSize + XzFlags_GetCheckSize((p)->streamFlags))






/* ---- Single-Thread and Multi-Thread xz Decoding with Input/Output Streams ---- */

/*
  if (CXzDecMtProps::numThreads > 1), the decoder can try to use
  Multi-Threading. The decoder analyses xz block header, and if
  there are pack size and unpack size values stored in xz block header,
  the decoder reads compressed data of block to internal buffers,
  and then it can start parallel decoding, if there are another blocks.
  The decoder can switch back to Single-Thread decoding after some conditions.

  The sequence of calls for xz decoding with in/out Streams:
  {
    XzDecMt_Create()
    XzDecMtProps_Init(XzDecMtProps) to set default values of properties
    // then you can change some XzDecMtProps parameters with required values
    // here you can set the number of threads and (memUseMax) - the maximum
    Memory usage for multithreading decoding.
    for()
    {
      XzDecMt_Decode() // one call per one file
    }
    XzDecMt_Destroy()
  }
*/


typedef struct
{
  size_t inBufSize_ST;    /* size of input buffer for Single-Thread decoding */
  size_t outStep_ST;      /* size of output buffer for Single-Thread decoding */
  BoolInt ignoreErrors;   /* if set to 1, the decoder can ignore some errors and it skips broken parts of data. */
  
  #ifndef Z7_ST
  unsigned numThreads;    /* the number of threads for Multi-Thread decoding. if (umThreads == 1) it will use Single-thread decoding */
  size_t inBufSize_MT;    /* size of small input data buffers for Multi-Thread decoding. Big number of such small buffers can be created */
  size_t memUseMax;       /* the limit of total memory usage for Multi-Thread decoding. */
                          /* it's recommended to set (memUseMax) manually to value that is smaller of total size of RAM in computer. */
  #endif
} CXzDecMtProps;

void XzDecMtProps_Init(CXzDecMtProps *p);

typedef struct CXzDecMt CXzDecMt;
typedef CXzDecMt * CXzDecMtHandle;
/* Z7_DECLARE_HANDLE(CXzDecMtHandle) */

/*
  alloc    : XzDecMt uses CAlignOffsetAlloc internally for addresses allocated by (alloc).
  allocMid : for big allocations, aligned allocation is better
*/

CXzDecMtHandle XzDecMt_Create(ISzAllocPtr alloc, ISzAllocPtr allocMid);
void XzDecMt_Destroy(CXzDecMtHandle p);


typedef struct
{
  Byte UnpackSize_Defined;
  Byte NumStreams_Defined;
  Byte NumBlocks_Defined;

  Byte DataAfterEnd;      /* there are some additional data after good xz streams, and that data is not new xz stream. */
  Byte DecodingTruncated; /* Decoding was Truncated, we need only partial output data */

  UInt64 InSize;          /* pack size processed. That value doesn't include the data after */
                          /* end of xz stream, if that data was not correct */
  UInt64 OutSize;

  UInt64 NumStreams;
  UInt64 NumBlocks;

  SRes DecodeRes;         /* the error code of xz streams data decoding */
  SRes ReadRes;           /* error code from ISeqInStream:Read() */
  SRes ProgressRes;       /* error code from ICompressProgress:Progress() */

  SRes CombinedRes;       /* Combined result error code that shows main rusult */
                          /* = S_OK, if there is no error. */
                          /* but check also (DataAfterEnd) that can show additional minor errors. */
 
  SRes CombinedRes_Type;  /* = SZ_ERROR_READ,     if error from ISeqInStream */
                          /* = SZ_ERROR_PROGRESS, if error from ICompressProgress */
                          /* = SZ_ERROR_WRITE,    if error from ISeqOutStream */
                          /* = SZ_ERROR_* codes for decoding */
} CXzStatInfo;

void XzStatInfo_Clear(CXzStatInfo *p);

/*

XzDecMt_Decode()
SRes: it's combined decoding result. It also is equal to stat->CombinedRes.

  SZ_OK               - no error
                        check also output value in (stat->DataAfterEnd)
                        that can show additional possible error

  SZ_ERROR_MEM        - Memory allocation error
  SZ_ERROR_NO_ARCHIVE - is not xz archive
  SZ_ERROR_ARCHIVE    - Headers error
  SZ_ERROR_DATA       - Data Error
  SZ_ERROR_UNSUPPORTED - Unsupported method or method properties
  SZ_ERROR_CRC        - CRC Error
  SZ_ERROR_INPUT_EOF  - it needs more input data
  SZ_ERROR_WRITE      - ISeqOutStream error
  (SZ_ERROR_READ)     - ISeqInStream errors
  (SZ_ERROR_PROGRESS) - ICompressProgress errors
  // SZ_ERROR_THREAD     - error in multi-threading functions
  MY_SRes_HRESULT_FROM_WRes(WRes_error) - error in multi-threading function
*/

SRes XzDecMt_Decode(CXzDecMtHandle p,
    const CXzDecMtProps *props,
    const UInt64 *outDataSize, /* NULL means undefined */
    int finishMode,            /* 0 - partial unpacking is allowed, 1 - xz stream(s) must be finished */
    ISeqOutStreamPtr outStream,
    /* Byte *outBuf, size_t *outBufSize, */
    ISeqInStreamPtr inStream,
    /* const Byte *inData, size_t inDataSize, */
    CXzStatInfo *stat,         /* out: decoding results and statistics */
    int *isMT,                 /* out: 0 means that ST (Single-Thread) version was used */
                               /*      1 means that MT (Multi-Thread) version was used */
    ICompressProgressPtr progress);

EXTERN_C_END

#endif

/* ==== XzCrc64.h ==== */
/* XzCrc64.h -- CRC64 calculation
2023-12-08 : Igor Pavlov : Public domain */

#ifndef ZIP7_INC_XZ_CRC64_H
#define ZIP7_INC_XZ_CRC64_H

#include <stddef.h>



EXTERN_C_BEGIN

/* extern UInt64 g_Crc64Table[]; */

void Z7_FASTCALL Crc64GenerateTable(void);

#define CRC64_INIT_VAL UINT64_CONST(0xFFFFFFFFFFFFFFFF)
#define CRC64_GET_DIGEST(crc) ((crc) ^ CRC64_INIT_VAL)
/* #define CRC64_UPDATE_BYTE(crc, b) (g_Crc64Table[((crc) ^ (b)) & 0xFF] ^ ((crc) >> 8)) */

UInt64 Z7_FASTCALL Crc64Update(UInt64 crc, const void *data, size_t size);
/* UInt64 Z7_FASTCALL Crc64Calc(const void *data, size_t size); */

EXTERN_C_END

#endif

#ifdef LZMASDK_IMPLEMENTATION

/* ==== 7zAlloc.c ==== */
#define g_allocCount z7_amalg_7zAlloc_g_allocCount
/* 7zAlloc.c -- Allocation functions for 7z processing
2023-03-04 : Igor Pavlov : Public domain */

/* ==== Precomp.h ==== */
/* Precomp.h -- precompilation file
2024-01-25 : Igor Pavlov : Public domain */

#ifndef ZIP7_INC_PRECOMP_H
#define ZIP7_INC_PRECOMP_H

/*
  this file must be included before another *.h files and before <windows.h>.
  this file is included from the following files:
    C\*.c
    C\Util\*\Precomp.h   <-  C\Util\*\*.c
    CPP\Common\Common.h  <-  *\StdAfx.h    <-  *\*.cpp

  this file can set the following macros:
    Z7_LARGE_PAGES 1
    Z7_LONG_PATH 1
    Z7_WIN32_WINNT_MIN  0x0500 (or higher) : we require at least win2000+ for 7-Zip
    _WIN32_WINNT        0x0500 (or higher)
    WINVER  _WIN32_WINNT
    UNICODE 1
    _UNICODE 1
*/

/* ==== Compiler.h ==== */
/* Compiler.h : Compiler specific defines and pragmas
: Igor Pavlov : Public domain */

#ifndef ZIP7_INC_COMPILER_H
#define ZIP7_INC_COMPILER_H

#if defined(__clang__)
# define Z7_CLANG_VERSION  (__clang_major__ * 10000 + __clang_minor__ * 100 + __clang_patchlevel__)
#endif
#if defined(__clang__) && defined(__apple_build_version__)
# define Z7_APPLE_CLANG_VERSION   Z7_CLANG_VERSION
#elif defined(__clang__)
# define Z7_LLVM_CLANG_VERSION    Z7_CLANG_VERSION
#elif defined(__GNUC__)
# define Z7_GCC_VERSION (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)
#endif

#ifdef _MSC_VER
#if !defined(__clang__) && !defined(__GNUC__)
#define Z7_MSC_VER_ORIGINAL _MSC_VER
#endif
#endif

#if defined(__MINGW32__) || defined(__MINGW64__)
#define Z7_MINGW
#endif

#if defined(__LCC__) && (defined(__MCST__) || defined(__e2k__))
#define Z7_MCST_LCC
#define Z7_MCST_LCC_VERSION (__LCC__ * 100 + __LCC_MINOR__)
#endif

/*
#if defined(__AVX2__) \
    || defined(Z7_GCC_VERSION) && (Z7_GCC_VERSION >= 40900) \
    || defined(Z7_APPLE_CLANG_VERSION) && (Z7_APPLE_CLANG_VERSION >= 40600) \
    || defined(Z7_LLVM_CLANG_VERSION) && (Z7_LLVM_CLANG_VERSION >= 30100) \
    || defined(Z7_MSC_VER_ORIGINAL) && (Z7_MSC_VER_ORIGINAL >= 1800) \
    || defined(__INTEL_COMPILER) && (__INTEL_COMPILER >= 1400)
    #define Z7_COMPILER_AVX2_SUPPORTED
  #endif
#endif
*/

/* #pragma GCC diagnostic ignored "-Wunknown-pragmas" */

#ifdef __clang__
/* padding size of '' with 4 bytes to alignment boundary */
#pragma GCC diagnostic ignored "-Wpadded"

#if defined(Z7_LLVM_CLANG_VERSION) && (__clang_major__ == 13) \
  && defined(__FreeBSD__)
/* freebsd: */
#pragma GCC diagnostic ignored "-Wexcess-padding"
#endif

#if __clang_major__ >= 16
#pragma GCC diagnostic ignored "-Wunsafe-buffer-usage"
#endif

#if __clang_major__ == 13
#if defined(__SIZEOF_POINTER__) && (__SIZEOF_POINTER__ == 16)
/* cheri */
#pragma GCC diagnostic ignored "-Wcapability-to-integer-cast"
#endif
#endif

#if __clang_major__ == 13
  /* for <arm_neon.h> */
  #pragma GCC diagnostic ignored "-Wreserved-identifier"
#endif

#endif /* __clang__ */

#if defined(_WIN32) && defined(__clang__) && __clang_major__ >= 16
/* #pragma GCC diagnostic ignored "-Wcast-function-type-strict" */
#define Z7_DIAGNOSTIC_IGNORE_CAST_FUNCTION \
  _Pragma("GCC diagnostic ignored \"-Wcast-function-type-strict\"")
#else
#define Z7_DIAGNOSTIC_IGNORE_CAST_FUNCTION
#endif

typedef void (*Z7_void_Function)(void);
#if defined(__clang__) || defined(__GNUC__)
#define Z7_CAST_FUNC_C  (Z7_void_Function)
#elif defined(_MSC_VER) && _MSC_VER > 1920
#define Z7_CAST_FUNC_C  (void *)
/* #pragma warning(disable : 4191) // 'type cast': unsafe conversion from 'FARPROC' to 'void (__cdecl *)()' */
#else
#define Z7_CAST_FUNC_C
#endif
/*
#if (defined(__GNUC__) && (__GNUC__ >= 8)) || defined(__clang__)
  // #pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
*/
#ifdef __GNUC__
#if defined(Z7_GCC_VERSION) && (Z7_GCC_VERSION >= 40000) && (Z7_GCC_VERSION < 70000)
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
#endif
#endif


#ifdef _MSC_VER

  #ifdef UNDER_CE
    #define RPC_NO_WINDOWS_H
    /* #pragma warning(disable : 4115) // '_RPC_ASYNC_STATE' : named type definition in parentheses */
    #pragma warning(disable : 4201) /* nonstandard extension used : nameless struct/union */
    #pragma warning(disable : 4214) /* nonstandard extension used : bit field types other than int */
  #endif

#if defined(_MSC_VER) && _MSC_VER >= 1800
#pragma warning(disable : 4464) /* relative include path contains '..' */
#endif

/* == 1200 : -O1 : for __forceinline */
/* >= 1900 : -O1 : for printf */
#pragma warning(disable : 4710) /* function not inlined */

#if _MSC_VER < 1900
/* winnt.h: 'Int64ShllMod32' */
#pragma warning(disable : 4514) /* unreferenced inline function has been removed */
#endif
    
#if _MSC_VER < 1300
/* #pragma warning(disable : 4702) // unreachable code */
/* Bra.c : -O1: */
#pragma warning(disable : 4714) /* function marked as __forceinline not inlined */
#endif

/*
#if _MSC_VER > 1400 && _MSC_VER <= 1900
// strcat: This function or variable may be unsafe
// sysinfoapi.h: kit10: GetVersion was declared deprecated
#pragma warning(disable : 4996)
#endif
*/

#if _MSC_VER > 1200
/* -Wall warnings */

#pragma warning(disable : 4711) /* function selected for automatic inline expansion */
#pragma warning(disable : 4820) /* '2' bytes padding added after data member */

#if _MSC_VER >= 1400 && _MSC_VER < 1920
/* 1400: string.h: _DBG_MEMCPY_INLINE_ */
/* 1600 - 191x : smmintrin.h __cplusplus' */
/* is not defined as a preprocessor macro, replacing with '0' for '#if/#elif' */
#pragma warning(disable : 4668)

/* 1400 - 1600 : WinDef.h : 'FARPROC' : */
/* 1900 - 191x : immintrin.h: _readfsbase_u32 */
/* no function prototype given : converting '()' to '(void)' */
#pragma warning(disable : 4255)
#endif

#if _MSC_VER >= 1914
/* Compiler will insert Spectre mitigation for memory load if /Qspectre switch specified */
#pragma warning(disable : 5045)
#endif

#endif /* _MSC_VER > 1200 */
#endif /* _MSC_VER */


#if defined(__clang__) && (__clang_major__ >= 4)
  #define Z7_PRAGMA_OPT_DISABLE_LOOP_UNROLL_VECTORIZE \
    _Pragma("clang loop unroll(disable)") \
    _Pragma("clang loop vectorize(disable)")
  #define Z7_ATTRIB_NO_VECTORIZE
#elif defined(__GNUC__) && (__GNUC__ >= 5) \
    && (!defined(Z7_MCST_LCC_VERSION) || (Z7_MCST_LCC_VERSION >= 12610))
  #define Z7_ATTRIB_NO_VECTORIZE __attribute__((optimize("no-tree-vectorize")))
  /* __attribute__((optimize("no-unroll-loops"))); */
  #define Z7_PRAGMA_OPT_DISABLE_LOOP_UNROLL_VECTORIZE
#elif defined(_MSC_VER) && (_MSC_VER >= 1920)
  #define Z7_PRAGMA_OPT_DISABLE_LOOP_UNROLL_VECTORIZE \
    _Pragma("loop( no_vector )")
  #define Z7_ATTRIB_NO_VECTORIZE
#else
  #define Z7_PRAGMA_OPT_DISABLE_LOOP_UNROLL_VECTORIZE
  #define Z7_ATTRIB_NO_VECTORIZE
#endif

#if defined(Z7_MSC_VER_ORIGINAL) && (Z7_MSC_VER_ORIGINAL >= 1920)
  #define Z7_PRAGMA_OPTIMIZE_FOR_CODE_SIZE _Pragma("optimize ( \"s\", on )")
  #define Z7_PRAGMA_OPTIMIZE_DEFAULT       _Pragma("optimize ( \"\", on )")
#else
  #define Z7_PRAGMA_OPTIMIZE_FOR_CODE_SIZE
  #define Z7_PRAGMA_OPTIMIZE_DEFAULT
#endif



#if defined(MY_CPU_X86_OR_AMD64) && ( \
       defined(__clang__) && (__clang_major__ >= 4) \
    || defined(__GNUC__) && (__GNUC__ >= 5))
  #define Z7_ATTRIB_NO_SSE  __attribute__((__target__("no-sse")))
#else
  #define Z7_ATTRIB_NO_SSE
#endif

#define Z7_ATTRIB_NO_VECTOR \
  Z7_ATTRIB_NO_VECTORIZE \
  Z7_ATTRIB_NO_SSE


#if defined(__clang__) && (__clang_major__ >= 8) \
  || defined(__GNUC__) && (__GNUC__ >= 1000) \
  /* || defined(_MSC_VER) && (_MSC_VER >= 1920) */
  /* GCC is not good for __builtin_expect() */
  #define Z7_LIKELY(x)   (__builtin_expect((x), 1))
  #define Z7_UNLIKELY(x) (__builtin_expect((x), 0))
  /* #define Z7_unlikely [[unlikely]] */
  /* #define Z7_likely [[likely]] */
#else
  #define Z7_LIKELY(x)   (x)
  #define Z7_UNLIKELY(x) (x)
  /* #define Z7_likely */
#endif


#if (defined(Z7_CLANG_VERSION) && (Z7_CLANG_VERSION >= 30600))

#if (Z7_CLANG_VERSION < 130000)
#define Z7_DIAGNOSTIC_IGNORE_BEGIN_RESERVED_MACRO_IDENTIFIER \
  _Pragma("GCC diagnostic push") \
  _Pragma("GCC diagnostic ignored \"-Wreserved-id-macro\"")
#else
#define Z7_DIAGNOSTIC_IGNORE_BEGIN_RESERVED_MACRO_IDENTIFIER \
  _Pragma("GCC diagnostic push") \
  _Pragma("GCC diagnostic ignored \"-Wreserved-macro-identifier\"")
#endif

#define Z7_DIAGNOSTIC_IGNORE_END_RESERVED_MACRO_IDENTIFIER \
  _Pragma("GCC diagnostic pop")
#else
#define Z7_DIAGNOSTIC_IGNORE_BEGIN_RESERVED_MACRO_IDENTIFIER
#define Z7_DIAGNOSTIC_IGNORE_END_RESERVED_MACRO_IDENTIFIER
#endif

#define UNUSED_VAR(x) (void)x;
/* #define UNUSED_VAR(x) x=x; */

#endif

#ifdef _MSC_VER
/* #pragma warning(disable : 4206) // nonstandard extension used : translation unit is empty */
#if _MSC_VER >= 1912
/* #pragma warning(disable : 5039) // pointer or reference to potentially throwing function passed to 'extern "C"' function under - EHc.Undefined behavior may occur if this function throws an exception. */
#endif
#endif

/*
// for debug:
#define UNICODE 1
#define _UNICODE 1
#define  _WIN32_WINNT  0x0500  // win2000
#ifndef WINVER
  #define WINVER  _WIN32_WINNT
#endif
*/

#ifdef _WIN32
/*
  this "Precomp.h" file must be included before <windows.h>,
  if we want to define _WIN32_WINNT before <windows.h>.
*/

#ifndef Z7_LARGE_PAGES
#ifndef Z7_NO_LARGE_PAGES
#define Z7_LARGE_PAGES 1
#endif
#endif

#ifndef Z7_LONG_PATH
#ifndef Z7_NO_LONG_PATH
#define Z7_LONG_PATH 1
#endif
#endif

#ifndef Z7_DEVICE_FILE
#ifndef Z7_NO_DEVICE_FILE
/* #define Z7_DEVICE_FILE 1 */
#endif
#endif

/* we don't change macros if included after <windows.h> */
#ifndef _WINDOWS_

#ifndef Z7_WIN32_WINNT_MIN
  #if defined(_M_ARM64) || defined(__aarch64__)
    /* #define Z7_WIN32_WINNT_MIN  0x0a00  // win10 */
    #define Z7_WIN32_WINNT_MIN  0x0600  /* vista */
  #elif defined(_M_ARM) && defined(_M_ARMT) && defined(_M_ARM_NT)
    /* #define Z7_WIN32_WINNT_MIN  0x0602  // win8 */
    #define Z7_WIN32_WINNT_MIN  0x0600  /* vista */
  #elif defined(_M_X64) || defined(_M_AMD64) || defined(__x86_64__) || defined(_M_IA64)
    #define Z7_WIN32_WINNT_MIN  0x0503  /* win2003 */
  /* #elif defined(_M_IX86) || defined(__i386__) */
  /*   #define Z7_WIN32_WINNT_MIN  0x0500  // win2000 */
  #else /* x86 and another(old) systems */
    #define Z7_WIN32_WINNT_MIN  0x0500  /* win2000 */
    /* #define Z7_WIN32_WINNT_MIN  0x0502  // win2003 // for debug */
  #endif
#endif /* Z7_WIN32_WINNT_MIN */


#ifndef Z7_DO_NOT_DEFINE_WIN32_WINNT
#ifdef _WIN32_WINNT
  /* #error Stop_Compiling_Bad_WIN32_WINNT */
#else
  #ifndef Z7_NO_DEFINE_WIN32_WINNT
Z7_DIAGNOSTIC_IGNORE_BEGIN_RESERVED_MACRO_IDENTIFIER
    #define _WIN32_WINNT  Z7_WIN32_WINNT_MIN
Z7_DIAGNOSTIC_IGNORE_END_RESERVED_MACRO_IDENTIFIER
  #endif
#endif /* _WIN32_WINNT */

#ifndef WINVER
  #define WINVER  _WIN32_WINNT
#endif
#endif /* Z7_DO_NOT_DEFINE_WIN32_WINNT */


#ifndef _MBCS
#ifndef Z7_NO_UNICODE
/* UNICODE and _UNICODE are used by <windows.h> and by 7-zip code. */

#ifndef UNICODE
#define UNICODE 1
#endif

#ifndef _UNICODE
Z7_DIAGNOSTIC_IGNORE_BEGIN_RESERVED_MACRO_IDENTIFIER
#define _UNICODE 1
Z7_DIAGNOSTIC_IGNORE_END_RESERVED_MACRO_IDENTIFIER
#endif

#endif /* Z7_NO_UNICODE */
#endif /* _MBCS */
#endif /* _WINDOWS_ */

/* #include "7zWindows.h" */

#endif /* _WIN32 */

#endif

#include <stdlib.h>

/* ==== 7zAlloc.h ==== */
/* 7zAlloc.h -- Allocation functions
2023-03-04 : Igor Pavlov : Public domain */

#ifndef ZIP7_INC_7Z_ALLOC_H
#define ZIP7_INC_7Z_ALLOC_H



EXTERN_C_BEGIN

void *SzAlloc(ISzAllocPtr p, size_t size);
void SzFree(ISzAllocPtr p, void *address);

void *SzAllocTemp(ISzAllocPtr p, size_t size);
void SzFreeTemp(ISzAllocPtr p, void *address);

EXTERN_C_END

#endif

/* #define SZ_ALLOC_DEBUG */
/* use SZ_ALLOC_DEBUG to debug alloc/free operations */

#ifdef SZ_ALLOC_DEBUG

/*
#ifdef _WIN32

#endif
*/

#include <stdio.h>
static int g_allocCount = 0;
static int g_allocCountTemp = 0;

static void Print_Alloc(const char *s, size_t size, int *counter)
{
  const unsigned size2 = (unsigned)size;
  fprintf(stderr, "\n%s count = %10d : %10u bytes; ", s, *counter, size2);
  (*counter)++;
}
static void Print_Free(const char *s, int *counter)
{
  (*counter)--;
  fprintf(stderr, "\n%s count = %10d", s, *counter);
}
#endif

void *SzAlloc(ISzAllocPtr p, size_t size)
{
  UNUSED_VAR(p)
  if (size == 0)
    return 0;
  #ifdef SZ_ALLOC_DEBUG
  Print_Alloc("Alloc", size, &g_allocCount);
  #endif
  return malloc(size);
}

void SzFree(ISzAllocPtr p, void *address)
{
  UNUSED_VAR(p)
  #ifdef SZ_ALLOC_DEBUG
  if (address)
    Print_Free("Free ", &g_allocCount);
  #endif
  free(address);
}

void *SzAllocTemp(ISzAllocPtr p, size_t size)
{
  UNUSED_VAR(p)
  if (size == 0)
    return 0;
  #ifdef SZ_ALLOC_DEBUG
  Print_Alloc("Alloc_temp", size, &g_allocCountTemp);
  /*
  #ifdef _WIN32
  return HeapAlloc(GetProcessHeap(), 0, size);
  #endif
  */
  #endif
  return malloc(size);
}

void SzFreeTemp(ISzAllocPtr p, void *address)
{
  UNUSED_VAR(p)
  #ifdef SZ_ALLOC_DEBUG
  if (address)
    Print_Free("Free_temp ", &g_allocCountTemp);
  /*
  #ifdef _WIN32
  HeapFree(GetProcessHeap(), 0, address);
  return;
  #endif
  */
  #endif
  free(address);
}
#undef g_allocCount

/* ==== 7zArcIn.c ==== */
/* 7zArcIn.c -- 7z Input functions
2023-09-07 : Igor Pavlov : Public domain */



#include <string.h>






#define MY_ALLOC(T, p, size, alloc) \
  { if ((p = (T *)ISzAlloc_Alloc(alloc, (size) * sizeof(T))) == NULL) return SZ_ERROR_MEM; }

#define MY_ALLOC_ZE(T, p, size, alloc) \
  { if ((size) == 0) p = NULL; else MY_ALLOC(T, p, size, alloc) }

#define MY_ALLOC_AND_CPY(to, size, from, alloc) \
  { MY_ALLOC(Byte, to, size, alloc); memcpy(to, from, size); }

#define MY_ALLOC_ZE_AND_CPY(to, size, from, alloc) \
  { if ((size) == 0) to = NULL; else { MY_ALLOC_AND_CPY(to, size, from, alloc) } }

#define k7zMajorVersion 0

enum EIdEnum
{
  k7zIdEnd,
  k7zIdHeader,
  k7zIdArchiveProperties,
  k7zIdAdditionalStreamsInfo,
  k7zIdMainStreamsInfo,
  k7zIdFilesInfo,
  k7zIdPackInfo,
  k7zIdUnpackInfo,
  k7zIdSubStreamsInfo,
  k7zIdSize,
  k7zIdCRC,
  k7zIdFolder,
  k7zIdCodersUnpackSize,
  k7zIdNumUnpackStream,
  k7zIdEmptyStream,
  k7zIdEmptyFile,
  k7zIdAnti,
  k7zIdName,
  k7zIdCTime,
  k7zIdATime,
  k7zIdMTime,
  k7zIdWinAttrib,
  k7zIdComment,
  k7zIdEncodedHeader,
  k7zIdStartPos,
  k7zIdDummy
  /* k7zNtSecure, */
  /* k7zParent, */
  /* k7zIsReal */
};

const Byte k7zSignature[k7zSignatureSize] = {'7', 'z', 0xBC, 0xAF, 0x27, 0x1C};

#define SzBitUi32s_INIT(p) { (p)->Defs = NULL; (p)->Vals = NULL; }

static SRes SzBitUi32s_Alloc(CSzBitUi32s *p, size_t num, ISzAllocPtr alloc)
{
  if (num == 0)
  {
    p->Defs = NULL;
    p->Vals = NULL;
  }
  else
  {
    MY_ALLOC(Byte, p->Defs, (num + 7) >> 3, alloc)
    MY_ALLOC(UInt32, p->Vals, num, alloc)
  }
  return SZ_OK;
}

static void SzBitUi32s_Free(CSzBitUi32s *p, ISzAllocPtr alloc)
{
  ISzAlloc_Free(alloc, p->Defs); p->Defs = NULL;
  ISzAlloc_Free(alloc, p->Vals); p->Vals = NULL;
}

#define SzBitUi64s_INIT(p) { (p)->Defs = NULL; (p)->Vals = NULL; }

static void SzBitUi64s_Free(CSzBitUi64s *p, ISzAllocPtr alloc)
{
  ISzAlloc_Free(alloc, p->Defs); p->Defs = NULL;
  ISzAlloc_Free(alloc, p->Vals); p->Vals = NULL;
}


static void SzAr_Init(CSzAr *p)
{
  p->NumPackStreams = 0;
  p->NumFolders = 0;
  
  p->PackPositions = NULL;
  SzBitUi32s_INIT(&p->FolderCRCs)

  p->FoCodersOffsets = NULL;
  p->FoStartPackStreamIndex = NULL;
  p->FoToCoderUnpackSizes = NULL;
  p->FoToMainUnpackSizeIndex = NULL;
  p->CoderUnpackSizes = NULL;

  p->CodersData = NULL;

  p->RangeLimit = 0;
}

static void SzAr_Free(CSzAr *p, ISzAllocPtr alloc)
{
  ISzAlloc_Free(alloc, p->PackPositions);
  SzBitUi32s_Free(&p->FolderCRCs, alloc);
 
  ISzAlloc_Free(alloc, p->FoCodersOffsets);
  ISzAlloc_Free(alloc, p->FoStartPackStreamIndex);
  ISzAlloc_Free(alloc, p->FoToCoderUnpackSizes);
  ISzAlloc_Free(alloc, p->FoToMainUnpackSizeIndex);
  ISzAlloc_Free(alloc, p->CoderUnpackSizes);
  
  ISzAlloc_Free(alloc, p->CodersData);

  SzAr_Init(p);
}


void SzArEx_Init(CSzArEx *p)
{
  SzAr_Init(&p->db);
  
  p->NumFiles = 0;
  p->dataPos = 0;
  
  p->UnpackPositions = NULL;
  p->IsDirs = NULL;
  
  p->FolderToFile = NULL;
  p->FileToFolder = NULL;
  
  p->FileNameOffsets = NULL;
  p->FileNames = NULL;
  
  SzBitUi32s_INIT(&p->CRCs)
  SzBitUi32s_INIT(&p->Attribs)
  /* SzBitUi32s_INIT(&p->Parents) */
  SzBitUi64s_INIT(&p->MTime)
  SzBitUi64s_INIT(&p->CTime)
}

void SzArEx_Free(CSzArEx *p, ISzAllocPtr alloc)
{
  ISzAlloc_Free(alloc, p->UnpackPositions);
  ISzAlloc_Free(alloc, p->IsDirs);

  ISzAlloc_Free(alloc, p->FolderToFile);
  ISzAlloc_Free(alloc, p->FileToFolder);

  ISzAlloc_Free(alloc, p->FileNameOffsets);
  ISzAlloc_Free(alloc, p->FileNames);

  SzBitUi32s_Free(&p->CRCs, alloc);
  SzBitUi32s_Free(&p->Attribs, alloc);
  /* SzBitUi32s_Free(&p->Parents, alloc); */
  SzBitUi64s_Free(&p->MTime, alloc);
  SzBitUi64s_Free(&p->CTime, alloc);
  
  SzAr_Free(&p->db, alloc);
  SzArEx_Init(p);
}


static int TestSignatureCandidate(const Byte *testBytes)
{
  unsigned i;
  for (i = 0; i < k7zSignatureSize; i++)
    if (testBytes[i] != k7zSignature[i])
      return 0;
  return 1;
}

#define SzData_CLEAR(p) { (p)->Data = NULL; (p)->Size = 0; }

#define SZ_READ_BYTE_SD_NOCHECK(_sd_, dest) \
    (_sd_)->Size--; dest = *(_sd_)->Data++;

#define SZ_READ_BYTE_SD(_sd_, dest) \
    if ((_sd_)->Size == 0) return SZ_ERROR_ARCHIVE; \
    SZ_READ_BYTE_SD_NOCHECK(_sd_, dest)

#define SZ_READ_BYTE(dest) SZ_READ_BYTE_SD(sd, dest)

#define SZ_READ_BYTE_2(dest) \
    if (sd.Size == 0) return SZ_ERROR_ARCHIVE; \
    sd.Size--; dest = *sd.Data++;

#define SKIP_DATA(sd, size) { sd->Size -= (size_t)(size); sd->Data += (size_t)(size); }
#define SKIP_DATA2(sd, size) { sd.Size -= (size_t)(size); sd.Data += (size_t)(size); }

#define SZ_READ_32(dest) if (sd.Size < 4) return SZ_ERROR_ARCHIVE; \
   dest = GetUi32(sd.Data); SKIP_DATA2(sd, 4);

static Z7_NO_INLINE SRes ReadNumber(CSzData *sd, UInt64 *value)
{
  Byte firstByte, mask;
  unsigned i;
  UInt32 v;

  SZ_READ_BYTE(firstByte)
  if ((firstByte & 0x80) == 0)
  {
    *value = firstByte;
    return SZ_OK;
  }
  SZ_READ_BYTE(v)
  if ((firstByte & 0x40) == 0)
  {
    *value = (((UInt32)firstByte & 0x3F) << 8) | v;
    return SZ_OK;
  }
  SZ_READ_BYTE(mask)
  *value = v | ((UInt32)mask << 8);
  mask = 0x20;
  for (i = 2; i < 8; i++)
  {
    Byte b;
    if ((firstByte & mask) == 0)
    {
      const UInt64 highPart = (unsigned)firstByte & (unsigned)(mask - 1);
      *value |= (highPart << (8 * i));
      return SZ_OK;
    }
    SZ_READ_BYTE(b)
    *value |= ((UInt64)b << (8 * i));
    mask >>= 1;
  }
  return SZ_OK;
}


static Z7_NO_INLINE SRes SzReadNumber32(CSzData *sd, UInt32 *value)
{
  Byte firstByte;
  UInt64 value64;
  if (sd->Size == 0)
    return SZ_ERROR_ARCHIVE;
  firstByte = *sd->Data;
  if ((firstByte & 0x80) == 0)
  {
    *value = firstByte;
    sd->Data++;
    sd->Size--;
    return SZ_OK;
  }
  RINOK(ReadNumber(sd, &value64))
  if (value64 >= (UInt32)0x80000000 - 1)
    return SZ_ERROR_UNSUPPORTED;
  if (value64 >= ((UInt64)(1) << ((sizeof(size_t) - 1) * 8 + 4)))
    return SZ_ERROR_UNSUPPORTED;
  *value = (UInt32)value64;
  return SZ_OK;
}

#define ReadID(sd, value) ReadNumber(sd, value)

static SRes SkipData(CSzData *sd)
{
  UInt64 size;
  RINOK(ReadNumber(sd, &size))
  if (size > sd->Size)
    return SZ_ERROR_ARCHIVE;
  SKIP_DATA(sd, size)
  return SZ_OK;
}

static SRes WaitId(CSzData *sd, UInt32 id)
{
  for (;;)
  {
    UInt64 type;
    RINOK(ReadID(sd, &type))
    if (type == id)
      return SZ_OK;
    if (type == k7zIdEnd)
      return SZ_ERROR_ARCHIVE;
    RINOK(SkipData(sd))
  }
}

static SRes RememberBitVector(CSzData *sd, UInt32 numItems, const Byte **v)
{
  const UInt32 numBytes = (numItems + 7) >> 3;
  if (numBytes > sd->Size)
    return SZ_ERROR_ARCHIVE;
  *v = sd->Data;
  SKIP_DATA(sd, numBytes)
  return SZ_OK;
}

static UInt32 CountDefinedBits(const Byte *bits, UInt32 numItems)
{
  unsigned b = 0;
  unsigned m = 0;
  UInt32 sum = 0;
  for (; numItems != 0; numItems--)
  {
    if (m == 0)
    {
      b = *bits++;
      m = 8;
    }
    m--;
    sum += (UInt32)((b >> m) & 1);
  }
  return sum;
}

static Z7_NO_INLINE SRes ReadBitVector(CSzData *sd, UInt32 numItems, Byte **v, ISzAllocPtr alloc)
{
  Byte allAreDefined;
  Byte *v2;
  const UInt32 numBytes = (numItems + 7) >> 3;
  *v = NULL;
  SZ_READ_BYTE(allAreDefined)
  if (numBytes == 0)
    return SZ_OK;
  if (allAreDefined == 0)
  {
    if (numBytes > sd->Size)
      return SZ_ERROR_ARCHIVE;
    MY_ALLOC_AND_CPY(*v, numBytes, sd->Data, alloc)
    SKIP_DATA(sd, numBytes)
    return SZ_OK;
  }
  MY_ALLOC(Byte, *v, numBytes, alloc)
  v2 = *v;
  memset(v2, 0xFF, (size_t)numBytes);
  {
    const unsigned numBits = (unsigned)numItems & 7;
    if (numBits != 0)
      v2[(size_t)numBytes - 1] = (Byte)((((UInt32)1 << numBits) - 1) << (8 - numBits));
  }
  return SZ_OK;
}

static Z7_NO_INLINE SRes ReadUi32s(CSzData *sd2, UInt32 numItems, CSzBitUi32s *crcs, ISzAllocPtr alloc)
{
  UInt32 i;
  CSzData sd;
  UInt32 *vals;
  const Byte *defs;
  MY_ALLOC_ZE(UInt32, crcs->Vals, numItems, alloc)
  sd = *sd2;
  defs = crcs->Defs;
  vals = crcs->Vals;
  for (i = 0; i < numItems; i++)
    if (SzBitArray_Check(defs, i))
    {
      SZ_READ_32(vals[i])
    }
    else
      vals[i] = 0;
  *sd2 = sd;
  return SZ_OK;
}

static SRes ReadBitUi32s(CSzData *sd, UInt32 numItems, CSzBitUi32s *crcs, ISzAllocPtr alloc)
{
  SzBitUi32s_Free(crcs, alloc);
  RINOK(ReadBitVector(sd, numItems, &crcs->Defs, alloc))
  return ReadUi32s(sd, numItems, crcs, alloc);
}

static SRes SkipBitUi32s(CSzData *sd, UInt32 numItems)
{
  Byte allAreDefined;
  UInt32 numDefined = numItems;
  SZ_READ_BYTE(allAreDefined)
  if (!allAreDefined)
  {
    const size_t numBytes = (numItems + 7) >> 3;
    if (numBytes > sd->Size)
      return SZ_ERROR_ARCHIVE;
    numDefined = CountDefinedBits(sd->Data, numItems);
    SKIP_DATA(sd, numBytes)
  }
  if (numDefined > (sd->Size >> 2))
    return SZ_ERROR_ARCHIVE;
  SKIP_DATA(sd, (size_t)numDefined * 4)
  return SZ_OK;
}

static SRes ReadPackInfo(CSzAr *p, CSzData *sd, ISzAllocPtr alloc)
{
  RINOK(SzReadNumber32(sd, &p->NumPackStreams))

  RINOK(WaitId(sd, k7zIdSize))
  MY_ALLOC(UInt64, p->PackPositions, (size_t)p->NumPackStreams + 1, alloc)
  {
    UInt64 sum = 0;
    UInt32 i;
    const UInt32 numPackStreams = p->NumPackStreams;
    for (i = 0; i < numPackStreams; i++)
    {
      UInt64 packSize;
      p->PackPositions[i] = sum;
      RINOK(ReadNumber(sd, &packSize))
      sum += packSize;
      if (sum < packSize)
        return SZ_ERROR_ARCHIVE;
    }
    p->PackPositions[i] = sum;
  }

  for (;;)
  {
    UInt64 type;
    RINOK(ReadID(sd, &type))
    if (type == k7zIdEnd)
      return SZ_OK;
    if (type == k7zIdCRC)
    {
      /* CRC of packed streams is unused now */
      RINOK(SkipBitUi32s(sd, p->NumPackStreams))
      continue;
    }
    RINOK(SkipData(sd))
  }
}

/*
static SRes SzReadSwitch(CSzData *sd)
{
  Byte external;
  RINOK(SzReadByte(sd, &external));
  return (external == 0) ? SZ_OK: SZ_ERROR_UNSUPPORTED;
}
*/

#define k_NumCodersStreams_in_Folder_MAX (SZ_NUM_BONDS_IN_FOLDER_MAX + SZ_NUM_PACK_STREAMS_IN_FOLDER_MAX)

SRes SzGetNextFolderItem(CSzFolder *f, CSzData *sd)
{
  UInt32 numCoders, i;
  UInt32 numInStreams = 0;
  const Byte *dataStart = sd->Data;

  f->NumCoders = 0;
  f->NumBonds = 0;
  f->NumPackStreams = 0;
  f->UnpackStream = 0;
  
  RINOK(SzReadNumber32(sd, &numCoders))
  if (numCoders == 0 || numCoders > SZ_NUM_CODERS_IN_FOLDER_MAX)
    return SZ_ERROR_UNSUPPORTED;
  
  for (i = 0; i < numCoders; i++)
  {
    Byte mainByte;
    CSzCoderInfo *coder = f->Coders + i;
    unsigned idSize, j;
    UInt64 id;
    
    SZ_READ_BYTE(mainByte)
    if ((mainByte & 0xC0) != 0)
      return SZ_ERROR_UNSUPPORTED;
    
    idSize = (unsigned)(mainByte & 0xF);
    if (idSize > sizeof(id))
      return SZ_ERROR_UNSUPPORTED;
    if (idSize > sd->Size)
      return SZ_ERROR_ARCHIVE;
    id = 0;
    for (j = 0; j < idSize; j++)
    {
      id = ((id << 8) | *sd->Data);
      sd->Data++;
      sd->Size--;
    }
    if (id > (UInt32)0xFFFFFFFF)
      return SZ_ERROR_UNSUPPORTED;
    coder->MethodID = (UInt32)id;
    
    coder->NumStreams = 1;
    coder->PropsOffset = 0;
    coder->PropsSize = 0;
    
    if ((mainByte & 0x10) != 0)
    {
      UInt32 numStreams;
      
      RINOK(SzReadNumber32(sd, &numStreams))
      if (numStreams > k_NumCodersStreams_in_Folder_MAX)
        return SZ_ERROR_UNSUPPORTED;
      coder->NumStreams = (Byte)numStreams;

      RINOK(SzReadNumber32(sd, &numStreams))
      if (numStreams != 1)
        return SZ_ERROR_UNSUPPORTED;
    }

    numInStreams += coder->NumStreams;

    if (numInStreams > k_NumCodersStreams_in_Folder_MAX)
      return SZ_ERROR_UNSUPPORTED;

    if ((mainByte & 0x20) != 0)
    {
      UInt32 propsSize = 0;
      RINOK(SzReadNumber32(sd, &propsSize))
      if (propsSize > sd->Size)
        return SZ_ERROR_ARCHIVE;
      if (propsSize >= 0x80)
        return SZ_ERROR_UNSUPPORTED;
      coder->PropsOffset = (size_t)(sd->Data - dataStart);
      coder->PropsSize = (Byte)propsSize;
      sd->Data += (size_t)propsSize;
      sd->Size -= (size_t)propsSize;
    }
  }

  /*
  if (numInStreams == 1 && numCoders == 1)
  {
    f->NumPackStreams = 1;
    f->PackStreams[0] = 0;
  }
  else
  */
  {
    Byte streamUsed[k_NumCodersStreams_in_Folder_MAX];
    UInt32 numBonds, numPackStreams;
    
    numBonds = numCoders - 1;
    if (numInStreams < numBonds)
      return SZ_ERROR_ARCHIVE;
    if (numBonds > SZ_NUM_BONDS_IN_FOLDER_MAX)
      return SZ_ERROR_UNSUPPORTED;
    f->NumBonds = numBonds;
    
    numPackStreams = numInStreams - numBonds;
    if (numPackStreams > SZ_NUM_PACK_STREAMS_IN_FOLDER_MAX)
      return SZ_ERROR_UNSUPPORTED;
    f->NumPackStreams = numPackStreams;
  
    for (i = 0; i < numInStreams; i++)
      streamUsed[i] = False;
    
    if (numBonds != 0)
    {
      Byte coderUsed[SZ_NUM_CODERS_IN_FOLDER_MAX];

      for (i = 0; i < numCoders; i++)
        coderUsed[i] = False;
      
      for (i = 0; i < numBonds; i++)
      {
        CSzBond *bp = f->Bonds + i;
        
        RINOK(SzReadNumber32(sd, &bp->InIndex))
        if (bp->InIndex >= numInStreams || streamUsed[bp->InIndex])
          return SZ_ERROR_ARCHIVE;
        streamUsed[bp->InIndex] = True;
        
        RINOK(SzReadNumber32(sd, &bp->OutIndex))
        if (bp->OutIndex >= numCoders || coderUsed[bp->OutIndex])
          return SZ_ERROR_ARCHIVE;
        coderUsed[bp->OutIndex] = True;
      }
      
      for (i = 0; i < numCoders; i++)
        if (!coderUsed[i])
        {
          f->UnpackStream = i;
          break;
        }
      
      if (i == numCoders)
        return SZ_ERROR_ARCHIVE;
    }
    
    if (numPackStreams == 1)
    {
      for (i = 0; i < numInStreams; i++)
        if (!streamUsed[i])
          break;
      if (i == numInStreams)
        return SZ_ERROR_ARCHIVE;
      f->PackStreams[0] = i;
    }
    else
      for (i = 0; i < numPackStreams; i++)
      {
        UInt32 index;
        RINOK(SzReadNumber32(sd, &index))
        if (index >= numInStreams || streamUsed[index])
          return SZ_ERROR_ARCHIVE;
        streamUsed[index] = True;
        f->PackStreams[i] = index;
      }
  }

  f->NumCoders = numCoders;

  return SZ_OK;
}


static Z7_NO_INLINE SRes SkipNumbers(CSzData *sd2, UInt32 num)
{
  CSzData sd;
  sd = *sd2;
  for (; num != 0; num--)
  {
    Byte firstByte, mask;
    unsigned i;
    SZ_READ_BYTE_2(firstByte)
    if ((firstByte & 0x80) == 0)
      continue;
    if ((firstByte & 0x40) == 0)
    {
      if (sd.Size == 0)
        return SZ_ERROR_ARCHIVE;
      sd.Size--;
      sd.Data++;
      continue;
    }
    mask = 0x20;
    for (i = 2; i < 8 && (firstByte & mask) != 0; i++)
      mask >>= 1;
    if (i > sd.Size)
      return SZ_ERROR_ARCHIVE;
    SKIP_DATA2(sd, i)
  }
  *sd2 = sd;
  return SZ_OK;
}


#define k_Scan_NumCoders_MAX 64
#define k_Scan_NumCodersStreams_in_Folder_MAX 64


static SRes ReadUnpackInfo(CSzAr *p,
    CSzData *sd2,
    UInt32 numFoldersMax,
    const CBuf *tempBufs, UInt32 numTempBufs,
    ISzAllocPtr alloc)
{
  CSzData sd;
  
  UInt32 fo, numFolders, numCodersOutStreams, packStreamIndex;
  const Byte *startBufPtr;
  Byte external;
  
  RINOK(WaitId(sd2, k7zIdFolder))
  
  RINOK(SzReadNumber32(sd2, &numFolders))
  if (numFolders > numFoldersMax)
    return SZ_ERROR_UNSUPPORTED;
  p->NumFolders = numFolders;

  SZ_READ_BYTE_SD(sd2, external)
  if (external == 0)
    sd = *sd2;
  else
  {
    UInt32 index;
    RINOK(SzReadNumber32(sd2, &index))
    if (index >= numTempBufs)
      return SZ_ERROR_ARCHIVE;
    sd.Data = tempBufs[index].data;
    sd.Size = tempBufs[index].size;
  }
  
  MY_ALLOC(size_t, p->FoCodersOffsets, (size_t)numFolders + 1, alloc)
  MY_ALLOC(UInt32, p->FoStartPackStreamIndex, (size_t)numFolders + 1, alloc)
  MY_ALLOC(UInt32, p->FoToCoderUnpackSizes, (size_t)numFolders + 1, alloc)
  MY_ALLOC_ZE(Byte, p->FoToMainUnpackSizeIndex, (size_t)numFolders, alloc)
  
  startBufPtr = sd.Data;
  
  packStreamIndex = 0;
  numCodersOutStreams = 0;

  for (fo = 0; fo < numFolders; fo++)
  {
    UInt32 numCoders, ci, numInStreams = 0;
    
    p->FoCodersOffsets[fo] = (size_t)(sd.Data - startBufPtr);
    
    RINOK(SzReadNumber32(&sd, &numCoders))
    if (numCoders == 0 || numCoders > k_Scan_NumCoders_MAX)
      return SZ_ERROR_UNSUPPORTED;
    
    for (ci = 0; ci < numCoders; ci++)
    {
      Byte mainByte;
      unsigned idSize;
      UInt32 coderInStreams;
      
      SZ_READ_BYTE_2(mainByte)
      if ((mainByte & 0xC0) != 0)
        return SZ_ERROR_UNSUPPORTED;
      idSize = (mainByte & 0xF);
      if (idSize > 8)
        return SZ_ERROR_UNSUPPORTED;
      if (idSize > sd.Size)
        return SZ_ERROR_ARCHIVE;
      SKIP_DATA2(sd, idSize)
      
      coderInStreams = 1;
      
      if ((mainByte & 0x10) != 0)
      {
        UInt32 coderOutStreams;
        RINOK(SzReadNumber32(&sd, &coderInStreams))
        RINOK(SzReadNumber32(&sd, &coderOutStreams))
        if (coderInStreams > k_Scan_NumCodersStreams_in_Folder_MAX || coderOutStreams != 1)
          return SZ_ERROR_UNSUPPORTED;
      }
      
      numInStreams += coderInStreams;

      if ((mainByte & 0x20) != 0)
      {
        UInt32 propsSize;
        RINOK(SzReadNumber32(&sd, &propsSize))
        if (propsSize > sd.Size)
          return SZ_ERROR_ARCHIVE;
        SKIP_DATA2(sd, propsSize)
      }
    }
    
    {
      UInt32 indexOfMainStream = 0;
      UInt32 numPackStreams = 1;
      
      if (numCoders != 1 || numInStreams != 1)
      {
        Byte streamUsed[k_Scan_NumCodersStreams_in_Folder_MAX];
        Byte coderUsed[k_Scan_NumCoders_MAX];
    
        UInt32 i;
        const UInt32 numBonds = numCoders - 1;
        if (numInStreams < numBonds)
          return SZ_ERROR_ARCHIVE;
        
        if (numInStreams > k_Scan_NumCodersStreams_in_Folder_MAX)
          return SZ_ERROR_UNSUPPORTED;
        
        for (i = 0; i < numInStreams; i++)
          streamUsed[i] = False;
        for (i = 0; i < numCoders; i++)
          coderUsed[i] = False;
        
        for (i = 0; i < numBonds; i++)
        {
          UInt32 index;
          
          RINOK(SzReadNumber32(&sd, &index))
          if (index >= numInStreams || streamUsed[index])
            return SZ_ERROR_ARCHIVE;
          streamUsed[index] = True;
          
          RINOK(SzReadNumber32(&sd, &index))
          if (index >= numCoders || coderUsed[index])
            return SZ_ERROR_ARCHIVE;
          coderUsed[index] = True;
        }
        
        numPackStreams = numInStreams - numBonds;
        
        if (numPackStreams != 1)
          for (i = 0; i < numPackStreams; i++)
          {
            UInt32 index;
            RINOK(SzReadNumber32(&sd, &index))
            if (index >= numInStreams || streamUsed[index])
              return SZ_ERROR_ARCHIVE;
            streamUsed[index] = True;
          }
          
        for (i = 0; i < numCoders; i++)
          if (!coderUsed[i])
          {
            indexOfMainStream = i;
            break;
          }
 
        if (i == numCoders)
          return SZ_ERROR_ARCHIVE;
      }
      
      p->FoStartPackStreamIndex[fo] = packStreamIndex;
      p->FoToCoderUnpackSizes[fo] = numCodersOutStreams;
      p->FoToMainUnpackSizeIndex[fo] = (Byte)indexOfMainStream;
      numCodersOutStreams += numCoders;
      if (numCodersOutStreams < numCoders)
        return SZ_ERROR_UNSUPPORTED;
      if (numPackStreams > p->NumPackStreams - packStreamIndex)
        return SZ_ERROR_ARCHIVE;
      packStreamIndex += numPackStreams;
    }
  }

  p->FoToCoderUnpackSizes[fo] = numCodersOutStreams;
  
  {
    const size_t dataSize = (size_t)(sd.Data - startBufPtr);
    p->FoStartPackStreamIndex[fo] = packStreamIndex;
    p->FoCodersOffsets[fo] = dataSize;
    MY_ALLOC_ZE_AND_CPY(p->CodersData, dataSize, startBufPtr, alloc)
  }
  
  if (external != 0)
  {
    if (sd.Size != 0)
      return SZ_ERROR_ARCHIVE;
    sd = *sd2;
  }
  
  RINOK(WaitId(&sd, k7zIdCodersUnpackSize))
  
  MY_ALLOC_ZE(UInt64, p->CoderUnpackSizes, (size_t)numCodersOutStreams, alloc)
  {
    UInt32 i;
    for (i = 0; i < numCodersOutStreams; i++)
    {
      RINOK(ReadNumber(&sd, p->CoderUnpackSizes + i))
    }
  }

  for (;;)
  {
    UInt64 type;
    RINOK(ReadID(&sd, &type))
    if (type == k7zIdEnd)
    {
      *sd2 = sd;
      return SZ_OK;
    }
    if (type == k7zIdCRC)
    {
      RINOK(ReadBitUi32s(&sd, numFolders, &p->FolderCRCs, alloc))
      continue;
    }
    RINOK(SkipData(&sd))
  }
}


UInt64 SzAr_GetFolderUnpackSize(const CSzAr *p, UInt32 folderIndex)
{
  return p->CoderUnpackSizes[p->FoToCoderUnpackSizes[folderIndex] + p->FoToMainUnpackSizeIndex[folderIndex]];
}


typedef struct
{
  UInt32 NumTotalSubStreams;
  UInt32 NumSubDigests;
  CSzData sdNumSubStreams;
  CSzData sdSizes;
  CSzData sdCRCs;
} CSubStreamInfo;


static SRes ReadSubStreamsInfo(CSzAr *p, CSzData *sd, CSubStreamInfo *ssi)
{
  UInt64 type = 0;
  UInt32 numSubDigests = 0;
  const UInt32 numFolders = p->NumFolders;
  UInt32 numUnpackStreams = numFolders;
  UInt32 numUnpackSizesInData = 0;

  for (;;)
  {
    RINOK(ReadID(sd, &type))
    if (type == k7zIdNumUnpackStream)
    {
      UInt32 i;
      ssi->sdNumSubStreams.Data = sd->Data;
      numUnpackStreams = 0;
      numSubDigests = 0;
      for (i = 0; i < numFolders; i++)
      {
        UInt32 numStreams;
        RINOK(SzReadNumber32(sd, &numStreams))
        if (numUnpackStreams > numUnpackStreams + numStreams)
          return SZ_ERROR_UNSUPPORTED;
        numUnpackStreams += numStreams;
        if (numStreams != 0)
          numUnpackSizesInData += (numStreams - 1);
        if (numStreams != 1 || !SzBitWithVals_Check(&p->FolderCRCs, i))
          numSubDigests += numStreams;
      }
      ssi->sdNumSubStreams.Size = (size_t)(sd->Data - ssi->sdNumSubStreams.Data);
      continue;
    }
    if (type == k7zIdCRC || type == k7zIdSize || type == k7zIdEnd)
      break;
    RINOK(SkipData(sd))
  }

  if (!ssi->sdNumSubStreams.Data)
  {
    numSubDigests = numFolders;
    if (p->FolderCRCs.Defs)
      numSubDigests = numFolders - CountDefinedBits(p->FolderCRCs.Defs, numFolders);
  }
  
  ssi->NumTotalSubStreams = numUnpackStreams;
  ssi->NumSubDigests = numSubDigests;

  if (type == k7zIdSize)
  {
    ssi->sdSizes.Data = sd->Data;
    RINOK(SkipNumbers(sd, numUnpackSizesInData))
    ssi->sdSizes.Size = (size_t)(sd->Data - ssi->sdSizes.Data);
    RINOK(ReadID(sd, &type))
  }

  for (;;)
  {
    if (type == k7zIdEnd)
      return SZ_OK;
    if (type == k7zIdCRC)
    {
      ssi->sdCRCs.Data = sd->Data;
      RINOK(SkipBitUi32s(sd, numSubDigests))
      ssi->sdCRCs.Size = (size_t)(sd->Data - ssi->sdCRCs.Data);
    }
    else
    {
      RINOK(SkipData(sd))
    }
    RINOK(ReadID(sd, &type))
  }
}

static SRes SzReadStreamsInfo(CSzAr *p,
    CSzData *sd,
    UInt32 numFoldersMax, const CBuf *tempBufs, UInt32 numTempBufs,
    UInt64 *dataOffset,
    CSubStreamInfo *ssi,
    ISzAllocPtr alloc)
{
  UInt64 type;

  SzData_CLEAR(&ssi->sdSizes)
  SzData_CLEAR(&ssi->sdCRCs)
  SzData_CLEAR(&ssi->sdNumSubStreams)

  *dataOffset = 0;
  RINOK(ReadID(sd, &type))
  if (type == k7zIdPackInfo)
  {
    RINOK(ReadNumber(sd, dataOffset))
    if (*dataOffset > p->RangeLimit)
      return SZ_ERROR_ARCHIVE;
    RINOK(ReadPackInfo(p, sd, alloc))
    if (p->PackPositions[p->NumPackStreams] > p->RangeLimit - *dataOffset)
      return SZ_ERROR_ARCHIVE;
    RINOK(ReadID(sd, &type))
  }
  if (type == k7zIdUnpackInfo)
  {
    RINOK(ReadUnpackInfo(p, sd, numFoldersMax, tempBufs, numTempBufs, alloc))
    RINOK(ReadID(sd, &type))
  }
  if (type == k7zIdSubStreamsInfo)
  {
    RINOK(ReadSubStreamsInfo(p, sd, ssi))
    RINOK(ReadID(sd, &type))
  }
  else
  {
    ssi->NumTotalSubStreams = p->NumFolders;
    /* ssi->NumSubDigests = 0; */
  }

  return (type == k7zIdEnd ? SZ_OK : SZ_ERROR_UNSUPPORTED);
}

static SRes SzReadAndDecodePackedStreams(
    ILookInStreamPtr inStream,
    CSzData *sd,
    CBuf *tempBufs,
    UInt32 numFoldersMax,
    UInt64 baseOffset,
    CSzAr *p,
    ISzAllocPtr allocTemp)
{
  UInt64 dataStartPos;
  UInt32 fo;
  CSubStreamInfo ssi;

  RINOK(SzReadStreamsInfo(p, sd, numFoldersMax, NULL, 0, &dataStartPos, &ssi, allocTemp))
  
  dataStartPos += baseOffset;
  if (p->NumFolders == 0)
    return SZ_ERROR_ARCHIVE;
 
  for (fo = 0; fo < p->NumFolders; fo++)
    Buf_Init(tempBufs + fo);
  
  for (fo = 0; fo < p->NumFolders; fo++)
  {
    CBuf *tempBuf = tempBufs + fo;
    const UInt64 unpackSize = SzAr_GetFolderUnpackSize(p, fo);
    if ((size_t)unpackSize != unpackSize)
      return SZ_ERROR_MEM;
    if (!Buf_Create(tempBuf, (size_t)unpackSize, allocTemp))
      return SZ_ERROR_MEM;
  }
  
  for (fo = 0; fo < p->NumFolders; fo++)
  {
    const CBuf *tempBuf = tempBufs + fo;
    RINOK(LookInStream_SeekTo(inStream, dataStartPos))
    RINOK(SzAr_DecodeFolder(p, fo, inStream, dataStartPos, tempBuf->data, tempBuf->size, allocTemp))
  }
  
  return SZ_OK;
}

static SRes SzReadFileNames(const Byte *data, size_t size, UInt32 numFiles, size_t *offsets)
{
  size_t pos = 0;
  *offsets++ = 0;
  if (numFiles == 0)
    return (size == 0) ? SZ_OK : SZ_ERROR_ARCHIVE;
  if (size < 2)
    return SZ_ERROR_ARCHIVE;
  if (data[size - 2] != 0 || data[size - 1] != 0)
    return SZ_ERROR_ARCHIVE;
  do
  {
    const Byte *p;
    if (pos == size)
      return SZ_ERROR_ARCHIVE;
    for (p = data + pos;
      #ifdef _WIN32
      *(const UInt16 *)(const void *)p != 0
      #else
      p[0] != 0 || p[1] != 0
      #endif
      ; p += 2);
    pos = (size_t)(p - data) + 2;
    *offsets++ = (pos >> 1);
  }
  while (--numFiles);
  return (pos == size) ? SZ_OK : SZ_ERROR_ARCHIVE;
}

static Z7_NO_INLINE SRes ReadTime(CSzBitUi64s *p, UInt32 num,
    CSzData *sd2,
    const CBuf *tempBufs, UInt32 numTempBufs,
    ISzAllocPtr alloc)
{
  CSzData sd;
  UInt32 i;
  CNtfsFileTime *vals;
  Byte *defs;
  Byte external;
  
  RINOK(ReadBitVector(sd2, num, &p->Defs, alloc))
  
  SZ_READ_BYTE_SD(sd2, external)
  if (external == 0)
    sd = *sd2;
  else
  {
    UInt32 index;
    RINOK(SzReadNumber32(sd2, &index))
    if (index >= numTempBufs)
      return SZ_ERROR_ARCHIVE;
    sd.Data = tempBufs[index].data;
    sd.Size = tempBufs[index].size;
  }
  
  MY_ALLOC_ZE(CNtfsFileTime, p->Vals, num, alloc)
  vals = p->Vals;
  defs = p->Defs;
  for (i = 0; i < num; i++)
    if (SzBitArray_Check(defs, i))
    {
      if (sd.Size < 8)
        return SZ_ERROR_ARCHIVE;
      vals[i].Low = GetUi32(sd.Data);
      vals[i].High = GetUi32(sd.Data + 4);
      SKIP_DATA2(sd, 8)
    }
    else
      vals[i].High = vals[i].Low = 0;
  
  if (external == 0)
    *sd2 = sd;
  
  return SZ_OK;
}


#define NUM_ADDITIONAL_STREAMS_MAX 8


static SRes SzReadHeader2(
    CSzArEx *p,   /* allocMain */
    CSzData *sd,
    ILookInStreamPtr inStream,
    CBuf *tempBufs, UInt32 *numTempBufs,
    ISzAllocPtr allocMain,
    ISzAllocPtr allocTemp
    )
{
  CSubStreamInfo ssi;

{
  UInt64 type;
  
  SzData_CLEAR(&ssi.sdSizes)
  SzData_CLEAR(&ssi.sdCRCs)
  SzData_CLEAR(&ssi.sdNumSubStreams)

  ssi.NumSubDigests = 0;
  ssi.NumTotalSubStreams = 0;

  RINOK(ReadID(sd, &type))

  if (type == k7zIdArchiveProperties)
  {
    for (;;)
    {
      UInt64 type2;
      RINOK(ReadID(sd, &type2))
      if (type2 == k7zIdEnd)
        break;
      RINOK(SkipData(sd))
    }
    RINOK(ReadID(sd, &type))
  }

  if (type == k7zIdAdditionalStreamsInfo)
  {
    CSzAr tempAr;
    SRes res;
    
    SzAr_Init(&tempAr);
    tempAr.RangeLimit = p->db.RangeLimit;

    res = SzReadAndDecodePackedStreams(inStream, sd, tempBufs, NUM_ADDITIONAL_STREAMS_MAX,
        p->startPosAfterHeader, &tempAr, allocTemp);
    *numTempBufs = tempAr.NumFolders;
    SzAr_Free(&tempAr, allocTemp);
    
    if (res != SZ_OK)
      return res;
    RINOK(ReadID(sd, &type))
  }

  if (type == k7zIdMainStreamsInfo)
  {
    RINOK(SzReadStreamsInfo(&p->db, sd, (UInt32)1 << 30, tempBufs, *numTempBufs,
        &p->dataPos, &ssi, allocMain))
    p->dataPos += p->startPosAfterHeader;
    RINOK(ReadID(sd, &type))
  }

  if (type == k7zIdEnd)
  {
    return SZ_OK;
  }

  if (type != k7zIdFilesInfo)
    return SZ_ERROR_ARCHIVE;
}

{
  UInt32 numFiles = 0;
  UInt32 numEmptyStreams = 0;
  const Byte *emptyStreams = NULL;
  const Byte *emptyFiles = NULL;
  
  RINOK(SzReadNumber32(sd, &numFiles))
  p->NumFiles = numFiles;

  for (;;)
  {
    UInt64 type;
    UInt64 size;
    RINOK(ReadID(sd, &type))
    if (type == k7zIdEnd)
      break;
    RINOK(ReadNumber(sd, &size))
    if (size > sd->Size)
      return SZ_ERROR_ARCHIVE;
    
    if (type >= ((UInt32)1 << 8))
    {
      SKIP_DATA(sd, size)
    }
    else switch ((unsigned)type)
    {
      case k7zIdName:
      {
        size_t namesSize;
        const Byte *namesData;
        Byte external;

        SZ_READ_BYTE(external)
        if (external == 0)
        {
          namesSize = (size_t)size - 1;
          namesData = sd->Data;
        }
        else
        {
          UInt32 index;
          RINOK(SzReadNumber32(sd, &index))
          if (index >= *numTempBufs)
            return SZ_ERROR_ARCHIVE;
          namesData = (tempBufs)[index].data;
          namesSize = (tempBufs)[index].size;
        }

        if ((namesSize & 1) != 0)
          return SZ_ERROR_ARCHIVE;
        MY_ALLOC(size_t, p->FileNameOffsets, numFiles + 1, allocMain)
        MY_ALLOC_ZE_AND_CPY(p->FileNames, namesSize, namesData, allocMain)
        RINOK(SzReadFileNames(p->FileNames, namesSize, numFiles, p->FileNameOffsets))
        if (external == 0)
        {
          SKIP_DATA(sd, namesSize)
        }
        break;
      }
      case k7zIdEmptyStream:
      {
        RINOK(RememberBitVector(sd, numFiles, &emptyStreams))
        numEmptyStreams = CountDefinedBits(emptyStreams, numFiles);
        emptyFiles = NULL;
        break;
      }
      case k7zIdEmptyFile:
      {
        RINOK(RememberBitVector(sd, numEmptyStreams, &emptyFiles))
        break;
      }
      case k7zIdWinAttrib:
      {
        Byte external;
        CSzData sdSwitch;
        CSzData *sdPtr;
        SzBitUi32s_Free(&p->Attribs, allocMain);
        RINOK(ReadBitVector(sd, numFiles, &p->Attribs.Defs, allocMain))

        SZ_READ_BYTE(external)
        if (external == 0)
          sdPtr = sd;
        else
        {
          UInt32 index;
          RINOK(SzReadNumber32(sd, &index))
          if (index >= *numTempBufs)
            return SZ_ERROR_ARCHIVE;
          sdSwitch.Data = (tempBufs)[index].data;
          sdSwitch.Size = (tempBufs)[index].size;
          sdPtr = &sdSwitch;
        }
        RINOK(ReadUi32s(sdPtr, numFiles, &p->Attribs, allocMain))
        break;
      }
      /*
      case k7zParent:
      {
        SzBitUi32s_Free(&p->Parents, allocMain);
        RINOK(ReadBitVector(sd, numFiles, &p->Parents.Defs, allocMain));
        RINOK(SzReadSwitch(sd));
        RINOK(ReadUi32s(sd, numFiles, &p->Parents, allocMain));
        break;
      }
      */
      case k7zIdMTime: RINOK(ReadTime(&p->MTime, numFiles, sd, tempBufs, *numTempBufs, allocMain)) break;
      case k7zIdCTime: RINOK(ReadTime(&p->CTime, numFiles, sd, tempBufs, *numTempBufs, allocMain)) break;
      default:
      {
        SKIP_DATA(sd, size)
      }
    }
  }

  if (numFiles - numEmptyStreams != ssi.NumTotalSubStreams)
    return SZ_ERROR_ARCHIVE;

  for (;;)
  {
    UInt64 type;
    RINOK(ReadID(sd, &type))
    if (type == k7zIdEnd)
      break;
    RINOK(SkipData(sd))
  }

  {
    UInt32 i;
    UInt32 emptyFileIndex = 0;
    UInt32 folderIndex = 0;
    UInt32 remSubStreams = 0;
    UInt32 numSubStreams = 0;
    UInt64 unpackPos = 0;
    const Byte *digestsDefs = NULL;
    const Byte *digestsVals = NULL;
    UInt32 digestIndex = 0;
    Byte isDirMask = 0;
    Byte crcMask = 0;
    Byte mask = 0x80;
    
    MY_ALLOC(UInt32, p->FolderToFile, p->db.NumFolders + 1, allocMain)
    MY_ALLOC_ZE(UInt32, p->FileToFolder, p->NumFiles, allocMain)
    MY_ALLOC(UInt64, p->UnpackPositions, p->NumFiles + 1, allocMain)
    MY_ALLOC_ZE(Byte, p->IsDirs, (p->NumFiles + 7) >> 3, allocMain)

    RINOK(SzBitUi32s_Alloc(&p->CRCs, p->NumFiles, allocMain))

    if (ssi.sdCRCs.Size != 0)
    {
      Byte allDigestsDefined = 0;
      SZ_READ_BYTE_SD_NOCHECK(&ssi.sdCRCs, allDigestsDefined)
      if (allDigestsDefined)
        digestsVals = ssi.sdCRCs.Data;
      else
      {
        const size_t numBytes = (ssi.NumSubDigests + 7) >> 3;
        digestsDefs = ssi.sdCRCs.Data;
        digestsVals = digestsDefs + numBytes;
      }
    }

    for (i = 0; i < numFiles; i++, mask >>= 1)
    {
      if (mask == 0)
      {
        const UInt32 byteIndex = (i - 1) >> 3;
        p->IsDirs[byteIndex] = isDirMask;
        p->CRCs.Defs[byteIndex] = crcMask;
        isDirMask = 0;
        crcMask = 0;
        mask = 0x80;
      }

      p->UnpackPositions[i] = unpackPos;
      p->CRCs.Vals[i] = 0;
      
      if (emptyStreams && SzBitArray_Check(emptyStreams, i))
      {
        if (emptyFiles)
        {
          if (!SzBitArray_Check(emptyFiles, emptyFileIndex))
            isDirMask |= mask;
          emptyFileIndex++;
        }
        else
          isDirMask |= mask;
        if (remSubStreams == 0)
        {
          p->FileToFolder[i] = (UInt32)-1;
          continue;
        }
      }
      
      if (remSubStreams == 0)
      {
        for (;;)
        {
          if (folderIndex >= p->db.NumFolders)
            return SZ_ERROR_ARCHIVE;
          p->FolderToFile[folderIndex] = i;
          numSubStreams = 1;
          if (ssi.sdNumSubStreams.Data)
          {
            RINOK(SzReadNumber32(&ssi.sdNumSubStreams, &numSubStreams))
          }
          remSubStreams = numSubStreams;
          if (numSubStreams != 0)
            break;
          {
            const UInt64 folderUnpackSize = SzAr_GetFolderUnpackSize(&p->db, folderIndex);
            unpackPos += folderUnpackSize;
            if (unpackPos < folderUnpackSize)
              return SZ_ERROR_ARCHIVE;
          }
          folderIndex++;
        }
      }
      
      p->FileToFolder[i] = folderIndex;
      
      if (emptyStreams && SzBitArray_Check(emptyStreams, i))
        continue;
      
      if (--remSubStreams == 0)
      {
        const UInt64 folderUnpackSize = SzAr_GetFolderUnpackSize(&p->db, folderIndex);
        const UInt64 startFolderUnpackPos = p->UnpackPositions[p->FolderToFile[folderIndex]];
        if (folderUnpackSize < unpackPos - startFolderUnpackPos)
          return SZ_ERROR_ARCHIVE;
        unpackPos = startFolderUnpackPos + folderUnpackSize;
        if (unpackPos < folderUnpackSize)
          return SZ_ERROR_ARCHIVE;

        if (numSubStreams == 1 && SzBitWithVals_Check(&p->db.FolderCRCs, folderIndex))
        {
          p->CRCs.Vals[i] = p->db.FolderCRCs.Vals[folderIndex];
          crcMask |= mask;
        }
        folderIndex++;
      }
      else
      {
        UInt64 v;
        RINOK(ReadNumber(&ssi.sdSizes, &v))
        unpackPos += v;
        if (unpackPos < v)
          return SZ_ERROR_ARCHIVE;
      }
      if ((crcMask & mask) == 0 && digestsVals)
      {
        if (!digestsDefs || SzBitArray_Check(digestsDefs, digestIndex))
        {
          p->CRCs.Vals[i] = GetUi32(digestsVals);
          digestsVals += 4;
          crcMask |= mask;
        }
        digestIndex++;
      }
    }

    if (mask != 0x80)
    {
      const UInt32 byteIndex = (i - 1) >> 3;
      p->IsDirs[byteIndex] = isDirMask;
      p->CRCs.Defs[byteIndex] = crcMask;
    }
    
    p->UnpackPositions[i] = unpackPos;

    if (remSubStreams != 0)
      return SZ_ERROR_ARCHIVE;

    for (;;)
    {
      p->FolderToFile[folderIndex] = i;
      if (folderIndex >= p->db.NumFolders)
        break;
      if (!ssi.sdNumSubStreams.Data)
        return SZ_ERROR_ARCHIVE;
      RINOK(SzReadNumber32(&ssi.sdNumSubStreams, &numSubStreams))
      if (numSubStreams != 0)
        return SZ_ERROR_ARCHIVE;
      /*
      {
        UInt64 folderUnpackSize = SzAr_GetFolderUnpackSize(&p->db, folderIndex);
        unpackPos += folderUnpackSize;
        if (unpackPos < folderUnpackSize)
          return SZ_ERROR_ARCHIVE;
      }
      */
      folderIndex++;
    }

    if (ssi.sdNumSubStreams.Data && ssi.sdNumSubStreams.Size != 0)
      return SZ_ERROR_ARCHIVE;
  }
}
  return SZ_OK;
}


static SRes SzReadHeader(
    CSzArEx *p,
    CSzData *sd,
    ILookInStreamPtr inStream,
    ISzAllocPtr allocMain,
    ISzAllocPtr allocTemp)
{
  UInt32 i;
  UInt32 numTempBufs = 0;
  SRes res;
  CBuf tempBufs[NUM_ADDITIONAL_STREAMS_MAX];

  for (i = 0; i < NUM_ADDITIONAL_STREAMS_MAX; i++)
    Buf_Init(tempBufs + i);
  
  res = SzReadHeader2(p, sd, inStream,
      tempBufs, &numTempBufs,
      allocMain, allocTemp);
  
  for (i = 0; i < NUM_ADDITIONAL_STREAMS_MAX; i++)
    Buf_Free(tempBufs + i, allocTemp);

  RINOK(res)

  if (sd->Size != 0)
    return SZ_ERROR_FAIL;

  return res;
}

static SRes SzArEx_Open2(
    CSzArEx *p,
    ILookInStreamPtr inStream,
    ISzAllocPtr allocMain,
    ISzAllocPtr allocTemp)
{
  Byte header[k7zStartHeaderSize];
  Int64 startArcPos;
  UInt64 nextHeaderOffset, nextHeaderSize;
  size_t nextHeaderSizeT;
  UInt32 nextHeaderCRC;
  CBuf buf;
  SRes res;

  startArcPos = 0;
  RINOK(ILookInStream_Seek(inStream, &startArcPos, SZ_SEEK_CUR))

  RINOK(LookInStream_Read2(inStream, header, k7zStartHeaderSize, SZ_ERROR_NO_ARCHIVE))

  if (!TestSignatureCandidate(header))
    return SZ_ERROR_NO_ARCHIVE;
  if (header[6] != k7zMajorVersion)
    return SZ_ERROR_UNSUPPORTED;

  nextHeaderOffset = GetUi64(header + 12);
  nextHeaderSize = GetUi64(header + 20);
  nextHeaderCRC = GetUi32(header + 28);

  p->startPosAfterHeader = (UInt64)startArcPos + k7zStartHeaderSize;
  
  if (CrcCalc(header + 12, 20) != GetUi32(header + 8))
    return SZ_ERROR_CRC;

  p->db.RangeLimit = nextHeaderOffset;

  nextHeaderSizeT = (size_t)nextHeaderSize;
  if (nextHeaderSizeT != nextHeaderSize)
    return SZ_ERROR_MEM;
  if (nextHeaderSizeT == 0)
    return SZ_OK;
  if (nextHeaderOffset > nextHeaderOffset + nextHeaderSize ||
      nextHeaderOffset > nextHeaderOffset + nextHeaderSize + k7zStartHeaderSize)
    return SZ_ERROR_NO_ARCHIVE;

  {
    Int64 pos = 0;
    RINOK(ILookInStream_Seek(inStream, &pos, SZ_SEEK_END))
    if ((UInt64)pos < (UInt64)startArcPos + nextHeaderOffset ||
        (UInt64)pos < (UInt64)startArcPos + k7zStartHeaderSize + nextHeaderOffset ||
        (UInt64)pos < (UInt64)startArcPos + k7zStartHeaderSize + nextHeaderOffset + nextHeaderSize)
      return SZ_ERROR_INPUT_EOF;
  }

  RINOK(LookInStream_SeekTo(inStream, (UInt64)startArcPos + k7zStartHeaderSize + nextHeaderOffset))

  if (!Buf_Create(&buf, nextHeaderSizeT, allocTemp))
    return SZ_ERROR_MEM;

  res = LookInStream_Read(inStream, buf.data, nextHeaderSizeT);
  
  if (res == SZ_OK)
  {
    res = SZ_ERROR_ARCHIVE;
    if (CrcCalc(buf.data, nextHeaderSizeT) == nextHeaderCRC)
    {
      CSzData sd;
      UInt64 type;
      sd.Data = buf.data;
      sd.Size = buf.size;
      
      res = ReadID(&sd, &type);
      
      if (res == SZ_OK && type == k7zIdEncodedHeader)
      {
        CSzAr tempAr;
        CBuf tempBuf;
        Buf_Init(&tempBuf);
        
        SzAr_Init(&tempAr);
        tempAr.RangeLimit = p->db.RangeLimit;

        res = SzReadAndDecodePackedStreams(inStream, &sd, &tempBuf, 1, p->startPosAfterHeader, &tempAr, allocTemp);
        SzAr_Free(&tempAr, allocTemp);
       
        if (res != SZ_OK)
        {
          Buf_Free(&tempBuf, allocTemp);
        }
        else
        {
          Buf_Free(&buf, allocTemp);
          buf.data = tempBuf.data;
          buf.size = tempBuf.size;
          sd.Data = buf.data;
          sd.Size = buf.size;
          res = ReadID(&sd, &type);
        }
      }
  
      if (res == SZ_OK)
      {
        if (type == k7zIdHeader)
        {
          /*
          CSzData sd2;
          unsigned ttt;
          for (ttt = 0; ttt < 40000; ttt++)
          {
            SzArEx_Free(p, allocMain);
            sd2 = sd;
            res = SzReadHeader(p, &sd2, inStream, allocMain, allocTemp);
            if (res != SZ_OK)
              break;
          }
          */
          res = SzReadHeader(p, &sd, inStream, allocMain, allocTemp);
        }
        else
          res = SZ_ERROR_UNSUPPORTED;
      }
    }
  }
 
  Buf_Free(&buf, allocTemp);
  return res;
}


SRes SzArEx_Open(CSzArEx *p, ILookInStreamPtr inStream,
    ISzAllocPtr allocMain, ISzAllocPtr allocTemp)
{
  const SRes res = SzArEx_Open2(p, inStream, allocMain, allocTemp);
  if (res != SZ_OK)
    SzArEx_Free(p, allocMain);
  return res;
}


SRes SzArEx_Extract(
    const CSzArEx *p,
    ILookInStreamPtr inStream,
    UInt32 fileIndex,
    UInt32 *blockIndex,
    Byte **tempBuf,
    size_t *outBufferSize,
    size_t *offset,
    size_t *outSizeProcessed,
    ISzAllocPtr allocMain,
    ISzAllocPtr allocTemp)
{
  const UInt32 folderIndex = p->FileToFolder[fileIndex];
  SRes res = SZ_OK;
  
  *offset = 0;
  *outSizeProcessed = 0;
  
  if (folderIndex == (UInt32)-1)
  {
    ISzAlloc_Free(allocMain, *tempBuf);
    *blockIndex = folderIndex;
    *tempBuf = NULL;
    *outBufferSize = 0;
    return SZ_OK;
  }

  if (*tempBuf == NULL || *blockIndex != folderIndex)
  {
    const UInt64 unpackSizeSpec = SzAr_GetFolderUnpackSize(&p->db, folderIndex);
    /*
    UInt64 unpackSizeSpec =
        p->UnpackPositions[p->FolderToFile[(size_t)folderIndex + 1]] -
        p->UnpackPositions[p->FolderToFile[folderIndex]];
    */
    const size_t unpackSize = (size_t)unpackSizeSpec;

    if (unpackSize != unpackSizeSpec)
      return SZ_ERROR_MEM;
    *blockIndex = folderIndex;
    ISzAlloc_Free(allocMain, *tempBuf);
    *tempBuf = NULL;
    
    if (res == SZ_OK)
    {
      *outBufferSize = unpackSize;
      if (unpackSize != 0)
      {
        *tempBuf = (Byte *)ISzAlloc_Alloc(allocMain, unpackSize);
        if (*tempBuf == NULL)
          res = SZ_ERROR_MEM;
      }
  
      if (res == SZ_OK)
      {
        res = SzAr_DecodeFolder(&p->db, folderIndex,
            inStream, p->dataPos, *tempBuf, unpackSize, allocTemp);
      }
    }
  }

  if (res == SZ_OK)
  {
    const UInt64 unpackPos = p->UnpackPositions[fileIndex];
    *offset = (size_t)(unpackPos - p->UnpackPositions[p->FolderToFile[folderIndex]]);
    *outSizeProcessed = (size_t)(p->UnpackPositions[(size_t)fileIndex + 1] - unpackPos);
    if (*offset + *outSizeProcessed > *outBufferSize)
      return SZ_ERROR_FAIL;
    if (SzBitWithVals_Check(&p->CRCs, fileIndex))
      if (CrcCalc(*tempBuf + *offset, *outSizeProcessed) != p->CRCs.Vals[fileIndex])
        res = SZ_ERROR_CRC;
  }

  return res;
}


size_t SzArEx_GetFileNameUtf16(const CSzArEx *p, size_t fileIndex, UInt16 *dest)
{
  const size_t offs = p->FileNameOffsets[fileIndex];
  const size_t len = p->FileNameOffsets[fileIndex + 1] - offs;
  if (dest != 0)
  {
    size_t i;
    const Byte *src = p->FileNames + offs * 2;
    for (i = 0; i < len; i++)
      dest[i] = GetUi16(src + i * 2);
  }
  return len;
}

/*
size_t SzArEx_GetFullNameLen(const CSzArEx *p, size_t fileIndex)
{
  size_t len;
  if (!p->FileNameOffsets)
    return 1;
  len = 0;
  for (;;)
  {
    UInt32 parent = (UInt32)(Int32)-1;
    len += p->FileNameOffsets[fileIndex + 1] - p->FileNameOffsets[fileIndex];
    if SzBitWithVals_Check(&p->Parents, fileIndex)
      parent = p->Parents.Vals[fileIndex];
    if (parent == (UInt32)(Int32)-1)
      return len;
    fileIndex = parent;
  }
}

UInt16 *SzArEx_GetFullNameUtf16_Back(const CSzArEx *p, size_t fileIndex, UInt16 *dest)
{
  BoolInt needSlash;
  if (!p->FileNameOffsets)
  {
    *(--dest) = 0;
    return dest;
  }
  needSlash = False;
  for (;;)
  {
    UInt32 parent = (UInt32)(Int32)-1;
    size_t curLen = p->FileNameOffsets[fileIndex + 1] - p->FileNameOffsets[fileIndex];
    SzArEx_GetFileNameUtf16(p, fileIndex, dest - curLen);
    if (needSlash)
      *(dest - 1) = '/';
    needSlash = True;
    dest -= curLen;

    if SzBitWithVals_Check(&p->Parents, fileIndex)
      parent = p->Parents.Vals[fileIndex];
    if (parent == (UInt32)(Int32)-1)
      return dest;
    fileIndex = parent;
  }
}
*/
#undef MY_ALLOC
#undef MY_ALLOC_AND_CPY
#undef MY_ALLOC_ZE
#undef MY_ALLOC_ZE_AND_CPY
#undef NUM_ADDITIONAL_STREAMS_MAX
#undef ReadID
#undef SKIP_DATA
#undef SKIP_DATA2
#undef SZ_READ_32
#undef SZ_READ_BYTE
#undef SZ_READ_BYTE_2
#undef SZ_READ_BYTE_SD
#undef SZ_READ_BYTE_SD_NOCHECK
#undef SzBitUi32s_INIT
#undef SzBitUi64s_INIT
#undef SzData_CLEAR
#undef k7zMajorVersion
#undef k_NumCodersStreams_in_Folder_MAX
#undef k_Scan_NumCodersStreams_in_Folder_MAX
#undef k_Scan_NumCoders_MAX

/* ==== 7zBuf.c ==== */
/* 7zBuf.c -- Byte Buffer
2017-04-03 : Igor Pavlov : Public domain */





void Buf_Init(CBuf *p)
{
  p->data = 0;
  p->size = 0;
}

int Buf_Create(CBuf *p, size_t size, ISzAllocPtr alloc)
{
  p->size = 0;
  if (size == 0)
  {
    p->data = 0;
    return 1;
  }
  p->data = (Byte *)ISzAlloc_Alloc(alloc, size);
  if (p->data)
  {
    p->size = size;
    return 1;
  }
  return 0;
}

void Buf_Free(CBuf *p, ISzAllocPtr alloc)
{
  ISzAlloc_Free(alloc, p->data);
  p->data = 0;
  p->size = 0;
}

/* ==== 7zBuf2.c ==== */
/* 7zBuf2.c -- Byte Buffer
2017-04-03 : Igor Pavlov : Public domain */



#include <string.h>



void DynBuf_Construct(CDynBuf *p)
{
  p->data = 0;
  p->size = 0;
  p->pos = 0;
}

void DynBuf_SeekToBeg(CDynBuf *p)
{
  p->pos = 0;
}

int DynBuf_Write(CDynBuf *p, const Byte *buf, size_t size, ISzAllocPtr alloc)
{
  if (size > p->size - p->pos)
  {
    size_t newSize = p->pos + size;
    Byte *data;
    newSize += newSize / 4;
    data = (Byte *)ISzAlloc_Alloc(alloc, newSize);
    if (!data)
      return 0;
    p->size = newSize;
    if (p->pos != 0)
      memcpy(data, p->data, p->pos);
    ISzAlloc_Free(alloc, p->data);
    p->data = data;
  }
  if (size != 0)
  {
    memcpy(p->data + p->pos, buf, size);
    p->pos += size;
  }
  return 1;
}

void DynBuf_Free(CDynBuf *p, ISzAllocPtr alloc)
{
  ISzAlloc_Free(alloc, p->data);
  p->data = 0;
  p->size = 0;
  p->pos = 0;
}

/* ==== 7zCrc.c ==== */
/* 7zCrc.c -- CRC32 calculation and init
2024-03-01 : Igor Pavlov : Public domain */






/* for debug: */
/* #define __ARM_FEATURE_CRC32 1 */

#ifdef __ARM_FEATURE_CRC32
/* #pragma message("__ARM_FEATURE_CRC32") */
#define Z7_CRC_HW_FORCE
#endif

/* #define Z7_CRC_DEBUG_BE */
#ifdef Z7_CRC_DEBUG_BE
#undef MY_CPU_LE
#define MY_CPU_BE
#endif

#ifdef Z7_CRC_HW_FORCE
  #define Z7_CRC_NUM_TABLES_USE  1
#else
#ifdef Z7_CRC_NUM_TABLES
  #define Z7_CRC_NUM_TABLES_USE  Z7_CRC_NUM_TABLES
#else
  #define Z7_CRC_NUM_TABLES_USE  12
#endif
#endif

#if Z7_CRC_NUM_TABLES_USE < 1
  #error Stop_Compiling_Bad_Z7_CRC_NUM_TABLES
#endif

#if defined(MY_CPU_LE) || (Z7_CRC_NUM_TABLES_USE == 1)
  #define Z7_CRC_NUM_TABLES_TOTAL  Z7_CRC_NUM_TABLES_USE
#else
  #define Z7_CRC_NUM_TABLES_TOTAL  (Z7_CRC_NUM_TABLES_USE + 1)
#endif

#ifndef Z7_CRC_HW_FORCE

#if Z7_CRC_NUM_TABLES_USE == 1 \
   || (!defined(MY_CPU_LE) && !defined(MY_CPU_BE))
#define CRC_UPDATE_BYTE_2(crc, b)   (table[((crc) ^ (b)) & 0xFF] ^ ((crc) >> 8))
#define Z7_CRC_UPDATE_T1_FUNC_NAME  CrcUpdateGT1
static UInt32 Z7_FASTCALL Z7_CRC_UPDATE_T1_FUNC_NAME(UInt32 v, const void *data, size_t size)
{
  const UInt32 *table = g_CrcTable;
  const Byte *p = (const Byte *)data;
  const Byte *lim = p + size;
  for (; p != lim; p++)
    v = CRC_UPDATE_BYTE_2(v, *p);
  return v;
}
#endif


#if Z7_CRC_NUM_TABLES_USE != 1
#ifndef MY_CPU_BE
  #define FUNC_NAME_LE_2(s)   CrcUpdateT ## s
  #define FUNC_NAME_LE_1(s)   FUNC_NAME_LE_2(s)
  #define FUNC_NAME_LE        FUNC_NAME_LE_1(Z7_CRC_NUM_TABLES_USE)
  UInt32 Z7_FASTCALL FUNC_NAME_LE (UInt32 v, const void *data, size_t size, const UInt32 *table);
#endif
#ifndef MY_CPU_LE
  #define FUNC_NAME_BE_2(s)   CrcUpdateT1_BeT ## s
  #define FUNC_NAME_BE_1(s)   FUNC_NAME_BE_2(s)
  #define FUNC_NAME_BE        FUNC_NAME_BE_1(Z7_CRC_NUM_TABLES_USE)
  UInt32 Z7_FASTCALL FUNC_NAME_BE (UInt32 v, const void *data, size_t size, const UInt32 *table);
#endif
#endif

#endif /* Z7_CRC_HW_FORCE */

/* ---------- hardware CRC ---------- */

#ifdef MY_CPU_LE

#if defined(MY_CPU_ARM_OR_ARM64)
/* #pragma message("ARM*") */

  #if (defined(__clang__) && (__clang_major__ >= 3)) \
     || defined(__GNUC__) && (__GNUC__ >= 6) && defined(MY_CPU_ARM64) \
     || defined(__GNUC__) && (__GNUC__ >= 8)
      #if !defined(__ARM_FEATURE_CRC32)
/*        #pragma message("!defined(__ARM_FEATURE_CRC32)") */
Z7_DIAGNOSTIC_IGNORE_BEGIN_RESERVED_MACRO_IDENTIFIER
        #define __ARM_FEATURE_CRC32 1
Z7_DIAGNOSTIC_IGNORE_END_RESERVED_MACRO_IDENTIFIER
        #define Z7_ARM_FEATURE_CRC32_WAS_SET
        #if defined(__clang__)
          #if defined(MY_CPU_ARM64)
            #define ATTRIB_CRC __attribute__((__target__("crc")))
          #else
            #define ATTRIB_CRC __attribute__((__target__("armv8-a,crc")))
          #endif
        #else
          #if defined(MY_CPU_ARM64)
#if !defined(Z7_GCC_VERSION) || (Z7_GCC_VERSION >= 60000)
            #define ATTRIB_CRC __attribute__((__target__("+crc")))
#endif
          #else
#if !defined(Z7_GCC_VERSION) || (__GNUC__  >= 8)
#if defined(__ARM_FP) && __GNUC__ >= 8
/* for -mfloat-abi=hard: similar to <arm_acle.h> */
            #define ATTRIB_CRC __attribute__((__target__("arch=armv8-a+crc+simd")))
#else
            #define ATTRIB_CRC __attribute__((__target__("arch=armv8-a+crc")))
#endif
#endif
          #endif
        #endif
      #endif
      #if defined(__ARM_FEATURE_CRC32)
      /* #pragma message("<arm_acle.h>") */
/*
arm_acle.h (GGC):
    before Nov 17, 2017:
#ifdef __ARM_FEATURE_CRC32

    Nov 17, 2017: gcc10.0  (gcc 9.2.0) checked"
#if __ARM_ARCH >= 8
#pragma GCC target ("arch=armv8-a+crc")

    Aug 22, 2019: GCC 8.4?, 9.2.1, 10.1:
#ifdef __ARM_FEATURE_CRC32
#ifdef __ARM_FP
#pragma GCC target ("arch=armv8-a+crc+simd")
#else
#pragma GCC target ("arch=armv8-a+crc")
#endif
*/
#if defined(__ARM_ARCH) && __ARM_ARCH < 8
#if defined(Z7_GCC_VERSION) && (__GNUC__ ==   8) && (Z7_GCC_VERSION <  80400) \
 || defined(Z7_GCC_VERSION) && (__GNUC__ ==   9) && (Z7_GCC_VERSION <  90201) \
 || defined(Z7_GCC_VERSION) && (__GNUC__ ==  10) && (Z7_GCC_VERSION < 100100)
Z7_DIAGNOSTIC_IGNORE_BEGIN_RESERVED_MACRO_IDENTIFIER
/* #pragma message("#define __ARM_ARCH 8") */
#undef  __ARM_ARCH
#define __ARM_ARCH 8
Z7_DIAGNOSTIC_IGNORE_END_RESERVED_MACRO_IDENTIFIER
#endif
#endif
        #define Z7_CRC_HW_USE
        #include <arm_acle.h>
      #endif
  #elif defined(_MSC_VER)
    #if defined(MY_CPU_ARM64)
    #if (_MSC_VER >= 1910)
    #ifdef __clang__
       /* #define Z7_CRC_HW_USE */
       /* #include <arm_acle.h> */
    #else
       #define Z7_CRC_HW_USE
       #include <intrin.h>
    #endif
    #endif
    #endif
  #endif

#else /* non-ARM* */

/* #define Z7_CRC_HW_USE // for debug : we can test HW-branch of code */
#ifdef Z7_CRC_HW_USE

#endif

#endif /* non-ARM* */



#if defined(Z7_CRC_HW_USE)

/* #pragma message("USE ARM HW CRC") */

#ifdef MY_CPU_64BIT
  #define CRC_HW_WORD_TYPE  UInt64
  #define CRC_HW_WORD_FUNC  __crc32d
#else
  #define CRC_HW_WORD_TYPE  UInt32
  #define CRC_HW_WORD_FUNC  __crc32w
#endif

#define CRC_HW_UNROLL_BYTES (sizeof(CRC_HW_WORD_TYPE) * 4)

#ifdef ATTRIB_CRC
  ATTRIB_CRC
#endif
Z7_NO_INLINE
#ifdef Z7_CRC_HW_FORCE
         UInt32 Z7_FASTCALL CrcUpdate
#else
  static UInt32 Z7_FASTCALL CrcUpdate_HW
#endif
    (UInt32 v, const void *data, size_t size)
{
  const Byte *p = (const Byte *)data;
  for (; size != 0 && ((unsigned)(ptrdiff_t)p & (CRC_HW_UNROLL_BYTES - 1)) != 0; size--)
    v = __crc32b(v, *p++);
  if (size >= CRC_HW_UNROLL_BYTES)
  {
    const Byte *lim = p + size;
    size &= CRC_HW_UNROLL_BYTES - 1;
    lim -= size;
    do
    {
      v = CRC_HW_WORD_FUNC(v, *(const CRC_HW_WORD_TYPE *)(const void *)(p));
      v = CRC_HW_WORD_FUNC(v, *(const CRC_HW_WORD_TYPE *)(const void *)(p + sizeof(CRC_HW_WORD_TYPE)));
      p += 2 * sizeof(CRC_HW_WORD_TYPE);
      v = CRC_HW_WORD_FUNC(v, *(const CRC_HW_WORD_TYPE *)(const void *)(p));
      v = CRC_HW_WORD_FUNC(v, *(const CRC_HW_WORD_TYPE *)(const void *)(p + sizeof(CRC_HW_WORD_TYPE)));
      p += 2 * sizeof(CRC_HW_WORD_TYPE);
    }
    while (p != lim);
  }
  
  for (; size != 0; size--)
    v = __crc32b(v, *p++);

  return v;
}

#ifdef Z7_ARM_FEATURE_CRC32_WAS_SET
Z7_DIAGNOSTIC_IGNORE_BEGIN_RESERVED_MACRO_IDENTIFIER
#undef __ARM_FEATURE_CRC32
Z7_DIAGNOSTIC_IGNORE_END_RESERVED_MACRO_IDENTIFIER
#undef Z7_ARM_FEATURE_CRC32_WAS_SET
#endif

#endif /* defined(Z7_CRC_HW_USE) */
#endif /* MY_CPU_LE */



#ifndef Z7_CRC_HW_FORCE

#if defined(Z7_CRC_HW_USE) || defined(Z7_CRC_UPDATE_T1_FUNC_NAME)
/*
typedef UInt32 (Z7_FASTCALL *Z7_CRC_UPDATE_WITH_TABLE_FUNC)
    (UInt32 v, const void *data, size_t size, const UInt32 *table);
Z7_CRC_UPDATE_WITH_TABLE_FUNC g_CrcUpdate;
*/
static unsigned g_Crc_Algo;
#if (!defined(MY_CPU_LE) && !defined(MY_CPU_BE))
static unsigned g_Crc_Be;
#endif
#endif /* defined(Z7_CRC_HW_USE) || defined(Z7_CRC_UPDATE_T1_FUNC_NAME) */



Z7_NO_INLINE
#ifdef Z7_CRC_HW_USE
  static UInt32 Z7_FASTCALL CrcUpdate_Base
#else
         UInt32 Z7_FASTCALL CrcUpdate
#endif
    (UInt32 crc, const void *data, size_t size)
{
#if Z7_CRC_NUM_TABLES_USE == 1
    return Z7_CRC_UPDATE_T1_FUNC_NAME(crc, data, size);
#else /* Z7_CRC_NUM_TABLES_USE != 1 */
#ifdef Z7_CRC_UPDATE_T1_FUNC_NAME
  if (g_Crc_Algo == 1)
    return Z7_CRC_UPDATE_T1_FUNC_NAME(crc, data, size);
#endif

#ifdef MY_CPU_LE
    return FUNC_NAME_LE(crc, data, size, g_CrcTable);
#elif defined(MY_CPU_BE)
    return FUNC_NAME_BE(crc, data, size, g_CrcTable);
#else
  if (g_Crc_Be)
    return FUNC_NAME_BE(crc, data, size, g_CrcTable);
  else
    return FUNC_NAME_LE(crc, data, size, g_CrcTable);
#endif
#endif /* Z7_CRC_NUM_TABLES_USE != 1 */
}


#ifdef Z7_CRC_HW_USE
Z7_NO_INLINE
UInt32 Z7_FASTCALL CrcUpdate(UInt32 crc, const void *data, size_t size)
{
  if (g_Crc_Algo == 0)
    return CrcUpdate_HW(crc, data, size);
  return CrcUpdate_Base(crc, data, size);
}
#endif

#endif /* !defined(Z7_CRC_HW_FORCE) */



UInt32 Z7_FASTCALL CrcCalc(const void *data, size_t size)
{
  return CrcUpdate(CRC_INIT_VAL, data, size) ^ CRC_INIT_VAL;
}


MY_ALIGN(64)
UInt32 g_CrcTable[256 * Z7_CRC_NUM_TABLES_TOTAL];


void Z7_FASTCALL CrcGenerateTable(void)
{
  UInt32 i;
  for (i = 0; i < 256; i++)
  {
#if defined(Z7_CRC_HW_FORCE)
    g_CrcTable[i] = __crc32b(i, 0);
#else
    #define kCrcPoly 0xEDB88320
    UInt32 r = i;
    unsigned j;
    for (j = 0; j < 8; j++)
      r = (r >> 1) ^ (kCrcPoly & ((UInt32)0 - (r & 1)));
    g_CrcTable[i] = r;
#endif
  }
  for (i = 256; i < 256 * Z7_CRC_NUM_TABLES_USE; i++)
  {
    const UInt32 r = g_CrcTable[(size_t)i - 256];
    g_CrcTable[i] = g_CrcTable[r & 0xFF] ^ (r >> 8);
  }

#if !defined(Z7_CRC_HW_FORCE) && \
    (defined(Z7_CRC_HW_USE) || defined(Z7_CRC_UPDATE_T1_FUNC_NAME) || defined(MY_CPU_BE))

#if Z7_CRC_NUM_TABLES_USE <= 1
    g_Crc_Algo = 1;
#else /* Z7_CRC_NUM_TABLES_USE <= 1 */

#if defined(MY_CPU_LE)
    g_Crc_Algo = Z7_CRC_NUM_TABLES_USE;
#else /* !defined(MY_CPU_LE) */
  {
#ifndef MY_CPU_BE
    UInt32 k = 0x01020304;
    const Byte *p = (const Byte *)&k;
    if (p[0] == 4 && p[1] == 3)
      g_Crc_Algo = Z7_CRC_NUM_TABLES_USE;
    else if (p[0] != 1 || p[1] != 2)
      g_Crc_Algo = 1;
    else
#endif /* MY_CPU_BE */
    {
      for (i = 256 * Z7_CRC_NUM_TABLES_TOTAL - 1; i >= 256; i--)
      {
        const UInt32 x = g_CrcTable[(size_t)i - 256];
        g_CrcTable[i] = Z7_BSWAP32(x);
      }
#if defined(Z7_CRC_UPDATE_T1_FUNC_NAME)
      g_Crc_Algo = Z7_CRC_NUM_TABLES_USE;
#endif
#if (!defined(MY_CPU_LE) && !defined(MY_CPU_BE))
      g_Crc_Be = 1;
#endif
    }
  }
#endif  /* !defined(MY_CPU_LE) */

#ifdef MY_CPU_LE
#ifdef Z7_CRC_HW_USE
  if (CPU_IsSupported_CRC32())
    g_Crc_Algo = 0;
#endif /* Z7_CRC_HW_USE */
#endif /* MY_CPU_LE */

#endif /* Z7_CRC_NUM_TABLES_USE <= 1 */
#endif /* g_Crc_Algo was declared */
}

Z7_CRC_UPDATE_FUNC z7_GetFunc_CrcUpdate(unsigned algo)
{
  if (algo == 0)
    return &CrcUpdate;

#if defined(Z7_CRC_HW_USE)
  if (algo == sizeof(CRC_HW_WORD_TYPE) * 8)
  {
#ifdef Z7_CRC_HW_FORCE
    return &CrcUpdate;
#else
    if (g_Crc_Algo == 0)
      return &CrcUpdate_HW;
#endif
  }
#endif

#ifndef Z7_CRC_HW_FORCE
  if (algo == Z7_CRC_NUM_TABLES_USE)
    return
  #ifdef Z7_CRC_HW_USE
      &CrcUpdate_Base;
  #else
      &CrcUpdate;
  #endif
#endif

  return NULL;
}

#undef kCrcPoly
#undef Z7_CRC_NUM_TABLES_USE
#undef Z7_CRC_NUM_TABLES_TOTAL
#undef CRC_UPDATE_BYTE_2
#undef FUNC_NAME_LE_2
#undef FUNC_NAME_LE_1
#undef FUNC_NAME_LE
#undef FUNC_NAME_BE_2
#undef FUNC_NAME_BE_1
#undef FUNC_NAME_BE

#undef CRC_HW_UNROLL_BYTES
#undef CRC_HW_WORD_FUNC
#undef CRC_HW_WORD_TYPE
#undef ATTRIB_CRC
#undef CRC_HW_UNROLL_BYTES
#undef CRC_HW_WORD_FUNC
#undef CRC_HW_WORD_TYPE
#undef CRC_UPDATE_BYTE_2
#undef FUNC_NAME_BE
#undef FUNC_NAME_BE_1
#undef FUNC_NAME_BE_2
#undef FUNC_NAME_LE
#undef FUNC_NAME_LE_1
#undef FUNC_NAME_LE_2
#undef MY_CPU_BE
#undef Z7_ARM_FEATURE_CRC32_WAS_SET
#undef Z7_CRC_HW_FORCE
#undef Z7_CRC_HW_USE
#undef Z7_CRC_NUM_TABLES_TOTAL
#undef Z7_CRC_NUM_TABLES_USE
#undef Z7_CRC_UPDATE_T1_FUNC_NAME
#undef __ARM_ARCH
#undef __ARM_FEATURE_CRC32
#undef kCrcPoly

/* ==== 7zDec.c ==== */
/* 7zDec.c -- Decoding from 7z folder
: Igor Pavlov : Public domain */



#include <string.h>

/* #define Z7_PPMD_SUPPORT */




/* ==== Bcj2.h ==== */
/* Bcj2.h -- BCJ2 converter for x86 code (Branch CALL/JUMP variant2)
2023-03-02 : Igor Pavlov : Public domain */

#ifndef ZIP7_INC_BCJ2_H
#define ZIP7_INC_BCJ2_H



EXTERN_C_BEGIN

#define BCJ2_NUM_STREAMS 4

enum
{
  BCJ2_STREAM_MAIN,
  BCJ2_STREAM_CALL,
  BCJ2_STREAM_JUMP,
  BCJ2_STREAM_RC
};

enum
{
  BCJ2_DEC_STATE_ORIG_0 = BCJ2_NUM_STREAMS,
  BCJ2_DEC_STATE_ORIG_1,
  BCJ2_DEC_STATE_ORIG_2,
  BCJ2_DEC_STATE_ORIG_3,
  
  BCJ2_DEC_STATE_ORIG,
  BCJ2_DEC_STATE_ERROR     /* after detected data error */
};

enum
{
  BCJ2_ENC_STATE_ORIG = BCJ2_NUM_STREAMS,
  BCJ2_ENC_STATE_FINISHED  /* it's state after fully encoded stream */
};


/* #define BCJ2_IS_32BIT_STREAM(s) ((s) == BCJ2_STREAM_CALL || (s) == BCJ2_STREAM_JUMP) */
#define BCJ2_IS_32BIT_STREAM(s) ((unsigned)((unsigned)(s) - (unsigned)BCJ2_STREAM_CALL) < 2)

/*
CBcj2Dec / CBcj2Enc
bufs sizes:
  BUF_SIZE(n) = lims[n] - bufs[n]
bufs sizes for BCJ2_STREAM_CALL and BCJ2_STREAM_JUMP must be multiply of 4:
    (BUF_SIZE(BCJ2_STREAM_CALL) & 3) == 0
    (BUF_SIZE(BCJ2_STREAM_JUMP) & 3) == 0
*/

/* typedef UInt32 CBcj2Prob; */
typedef UInt16 CBcj2Prob;

/*
BCJ2 encoder / decoder internal requirements:
  - If last bytes of stream contain marker (e8/e8/0f8x), then
    there is also encoded symbol (0 : no conversion) in RC stream.
  - One case of overlapped instructions is supported,
    if last byte of converted instruction is (0f) and next byte is (8x):
      marker [xx xx xx 0f] 8x
    then the pair (0f 8x) is treated as marker.
*/

/* ---------- BCJ2 Decoder ---------- */

/*
CBcj2Dec:
(dest) is allowed to overlap with bufs[BCJ2_STREAM_MAIN], with the following conditions:
  bufs[BCJ2_STREAM_MAIN] >= dest &&
  bufs[BCJ2_STREAM_MAIN] - dest >=
        BUF_SIZE(BCJ2_STREAM_CALL) +
        BUF_SIZE(BCJ2_STREAM_JUMP)
  reserve = bufs[BCJ2_STREAM_MAIN] - dest -
      ( BUF_SIZE(BCJ2_STREAM_CALL) +
        BUF_SIZE(BCJ2_STREAM_JUMP) )
  and additional conditions:
  if (it's first call of Bcj2Dec_Decode() after Bcj2Dec_Init())
  {
    (reserve != 1) : if (ver <  v23.00)
  }
  else // if there are more than one calls of Bcj2Dec_Decode() after Bcj2Dec_Init())
  {
    (reserve >= 6) : if (ver <  v23.00)
    (reserve >= 4) : if (ver >= v23.00)
    We need that (reserve) because after first call of Bcj2Dec_Decode(),
    CBcj2Dec::temp can contain up to 4 bytes for writing to (dest).
  }
  (reserve == 0) is allowed, if we decode full stream via single call of Bcj2Dec_Decode().
  (reserve == 0) also is allowed in case of multi-call, if we use fixed buffers,
     and (reserve) is calculated from full (final) sizes of all streams before first call.
*/

typedef struct
{
  const Byte *bufs[BCJ2_NUM_STREAMS];
  const Byte *lims[BCJ2_NUM_STREAMS];
  Byte *dest;
  const Byte *destLim;

  unsigned state; /* BCJ2_STREAM_MAIN has more priority than BCJ2_STATE_ORIG */

  UInt32 ip;      /* property of starting base for decoding */
  UInt32 temp;    /* Byte temp[4]; */
  UInt32 range;
  UInt32 code;
  CBcj2Prob probs[2 + 256];
} CBcj2Dec;


/* Note:
   Bcj2Dec_Init() sets (CBcj2Dec::ip = 0)
   if (ip != 0) property is required, the caller must set CBcj2Dec::ip after Bcj2Dec_Init()
*/
void Bcj2Dec_Init(CBcj2Dec *p);


/* Bcj2Dec_Decode():
   returns:
     SZ_OK
     SZ_ERROR_DATA : if data in 5 starting bytes of BCJ2_STREAM_RC stream are not correct
*/
SRes Bcj2Dec_Decode(CBcj2Dec *p);

/* To check that decoding was finished you can compare
   sizes of processed streams with sizes known from another sources.
   You must do at least one mandatory check from the two following options:
      - the check for size of processed output (ORIG) stream.
      - the check for size of processed input  (MAIN) stream.
   additional optional checks:
      - the checks for processed sizes of all input streams (MAIN, CALL, JUMP, RC)
      - the checks Bcj2Dec_IsMaybeFinished*()
   also before actual decoding you can check that the
   following condition is met for stream sizes:
     ( size(ORIG) == size(MAIN) + size(CALL) + size(JUMP) )
*/

/* (state == BCJ2_STREAM_MAIN) means that decoder is ready for
      additional input data in BCJ2_STREAM_MAIN stream.
   Note that (state == BCJ2_STREAM_MAIN) is allowed for non-finished decoding.
*/
#define Bcj2Dec_IsMaybeFinished_state_MAIN(_p_) ((_p_)->state == BCJ2_STREAM_MAIN)

/* if the stream decoding was finished correctly, then range decoder
   part of CBcj2Dec also was finished, and then (CBcj2Dec::code == 0).
   Note that (CBcj2Dec::code == 0) is allowed for non-finished decoding.
*/
#define Bcj2Dec_IsMaybeFinished_code(_p_) ((_p_)->code == 0)

/* use Bcj2Dec_IsMaybeFinished() only as additional check
    after at least one mandatory check from the two following options:
      - the check for size of processed output (ORIG) stream.
      - the check for size of processed input  (MAIN) stream.
*/
#define Bcj2Dec_IsMaybeFinished(_p_) ( \
        Bcj2Dec_IsMaybeFinished_state_MAIN(_p_) && \
        Bcj2Dec_IsMaybeFinished_code(_p_))



/* ---------- BCJ2 Encoder ---------- */

typedef enum
{
  BCJ2_ENC_FINISH_MODE_CONTINUE,
  BCJ2_ENC_FINISH_MODE_END_BLOCK,
  BCJ2_ENC_FINISH_MODE_END_STREAM
} EBcj2Enc_FinishMode;

/*
  BCJ2_ENC_FINISH_MODE_CONTINUE:
     process non finished encoding.
     It notifies the encoder that additional further calls
     can provide more input data (src) than provided by current call.
     In  that case the CBcj2Enc encoder still can move (src) pointer
     up to (srcLim), but CBcj2Enc encoder can store some of the last
     processed bytes (up to 4 bytes) from src to internal CBcj2Enc::temp[] buffer.
   at return:
       (CBcj2Enc::src will point to position that includes
       processed data and data copied to (temp[]) buffer)
       That data from (temp[]) buffer will be used in further calls.
  
  BCJ2_ENC_FINISH_MODE_END_BLOCK:
     finish encoding of current block (ended at srcLim) without RC flushing.
   at return: if (CBcj2Enc::state == BCJ2_ENC_STATE_ORIG) &&
                  CBcj2Enc::src == CBcj2Enc::srcLim)
        :  it shows that block encoding was finished. And the encoder is
           ready for new (src) data or for stream finish operation.
     finished block means
     {
       CBcj2Enc has completed block encoding up to (srcLim).
       (1 + 4 bytes) or (2 + 4 bytes) CALL/JUMP cortages will
       not cross block boundary at (srcLim).
       temporary CBcj2Enc buffer for (ORIG) src data is empty.
       3 output uncompressed streams (MAIN, CALL, JUMP) were flushed.
       RC stream was not flushed. And RC stream will cross block boundary.
     }
     Note: some possible implementation of BCJ2 encoder could
     write branch marker (e8/e8/0f8x) in one call of Bcj2Enc_Encode(),
     and it could calculate symbol for RC in another call of Bcj2Enc_Encode().
     BCJ2 encoder uses ip/fileIp/fileSize/relatLimit values to calculate RC symbol.
     And these CBcj2Enc variables can have different values in different Bcj2Enc_Encode() calls.
     So caller must finish each block with BCJ2_ENC_FINISH_MODE_END_BLOCK
     to ensure that RC symbol is calculated and written in proper block.
    
  BCJ2_ENC_FINISH_MODE_END_STREAM
     finish encoding of stream (ended at srcLim) fully including RC flushing.
   at return: if (CBcj2Enc::state == BCJ2_ENC_STATE_FINISHED)
        : it shows that stream encoding was finished fully,
          and all output streams were flushed fully.
     also Bcj2Enc_IsFinished() can be called.
*/


/*
  32-bit relative offset in JUMP/CALL commands is
    - (mod 4 GiB)  for 32-bit x86 code
    - signed Int32 for 64-bit x86-64 code
  BCJ2 encoder also does internal relative to absolute address conversions.
  And there are 2 possible ways to do it:
    before v23: we used 32-bit variables and (mod 4 GiB) conversion
    since  v23: we use  64-bit variables and (signed Int32 offset) conversion.
  The absolute address condition for conversion in v23:
    ((UInt64)((Int64)ip64 - (Int64)fileIp64 + 5 + (Int32)offset) < (UInt64)fileSize64)
  note that if (fileSize64 > 2 GiB). there is difference between
  old (mod 4 GiB) way (v22) and new (signed Int32 offset) way (v23).
  And new (v23) way is more suitable to encode 64-bit x86-64 code for (fileSize64 > 2 GiB) cases.
*/

/*
// for old (v22) way for conversion:
typedef UInt32 CBcj2Enc_ip_unsigned;
typedef  Int32 CBcj2Enc_ip_signed;
#define BCJ2_ENC_FileSize_MAX ((UInt32)1 << 31)
*/
typedef UInt64 CBcj2Enc_ip_unsigned;
typedef  Int64 CBcj2Enc_ip_signed;

/* maximum size of file that can be used for conversion condition */
#define BCJ2_ENC_FileSize_MAX             ((CBcj2Enc_ip_unsigned)0 - 2)

/* default value of fileSize64_minus1 variable that means
   that absolute address limitation will not be used */
#define BCJ2_ENC_FileSizeField_UNLIMITED  ((CBcj2Enc_ip_unsigned)0 - 1)

/* calculate value that later can be set to CBcj2Enc::fileSize64_minus1 */
#define BCJ2_ENC_GET_FileSizeField_VAL_FROM_FileSize(fileSize) \
    ((CBcj2Enc_ip_unsigned)(fileSize) - 1)

/* set CBcj2Enc::fileSize64_minus1 variable from size of file */
#define Bcj2Enc_SET_FileSize(p, fileSize) \
    (p)->fileSize64_minus1 = BCJ2_ENC_GET_FileSizeField_VAL_FROM_FileSize(fileSize);


typedef struct
{
  Byte *bufs[BCJ2_NUM_STREAMS];
  const Byte *lims[BCJ2_NUM_STREAMS];
  const Byte *src;
  const Byte *srcLim;

  unsigned state;
  EBcj2Enc_FinishMode finishMode;

  Byte context;
  Byte flushRem;
  Byte isFlushState;

  Byte cache;
  UInt32 range;
  UInt64 low;
  UInt64 cacheSize;
  
  /* UInt32 context;  // for marker version, it can include marker flag. */

  /* (ip64) and (fileIp64) correspond to virtual source stream position
     that doesn't include data in temp[] */
  CBcj2Enc_ip_unsigned ip64;         /* current (ip) position */
  CBcj2Enc_ip_unsigned fileIp64;     /* start (ip) position of current file */
  CBcj2Enc_ip_unsigned fileSize64_minus1;   /* size of current file (for conversion limitation) */
  UInt32 relatLimit;  /* (relatLimit <= ((UInt32)1 << 31)) : 0 means disable_conversion */
  /* UInt32 relatExcludeBits; */

  UInt32 tempTarget;
  unsigned tempPos; /* the number of bytes that were copied to temp[] buffer
                       (tempPos <= 4) outside of Bcj2Enc_Encode() */
  /* Byte temp[4]; // for marker version */
  Byte temp[8];
  CBcj2Prob probs[2 + 256];
} CBcj2Enc;

void Bcj2Enc_Init(CBcj2Enc *p);


/*
Bcj2Enc_Encode(): at exit:
  p->State <  BCJ2_NUM_STREAMS    : we need more buffer space for output stream
                                    (bufs[p->State] == lims[p->State])
  p->State == BCJ2_ENC_STATE_ORIG : we need more data in input src stream
                                    (src == srcLim)
  p->State == BCJ2_ENC_STATE_FINISHED : after fully encoded stream
*/
void Bcj2Enc_Encode(CBcj2Enc *p);

/* Bcj2Enc encoder can look ahead for up 4 bytes of source stream.
   CBcj2Enc::tempPos : is the number of bytes that were copied from input stream to temp[] buffer.
   (CBcj2Enc::src) after Bcj2Enc_Encode() is starting position after
   fully processed data and after data copied to temp buffer.
   So if the caller needs to get real number of fully processed input
   bytes (without look ahead data in temp buffer),
   the caller must subtruct (CBcj2Enc::tempPos) value from processed size
   value that is calculated based on current (CBcj2Enc::src):
     cur_processed_pos = Calc_Big_Processed_Pos(enc.src)) -
        Bcj2Enc_Get_AvailInputSize_in_Temp(&enc);
*/
/* get the size of input data that was stored in temp[] buffer: */
#define Bcj2Enc_Get_AvailInputSize_in_Temp(p) ((p)->tempPos)

#define Bcj2Enc_IsFinished(p) ((p)->flushRem == 0)

/* Note : the decoder supports overlapping of marker (0f 80).
   But we can eliminate such overlapping cases by setting
   the limit for relative offset conversion as
     CBcj2Enc::relatLimit <= (0x0f << 24) == (240 MiB)
*/
/* default value for CBcj2Enc::relatLimit */
#define BCJ2_ENC_RELAT_LIMIT_DEFAULT  ((UInt32)0x0f << 24)
#define BCJ2_ENC_RELAT_LIMIT_MAX      ((UInt32)1 << 31)
/* #define BCJ2_RELAT_EXCLUDE_NUM_BITS 5 */

EXTERN_C_END

#endif
/* ==== Bra.h ==== */
/* Bra.h -- Branch converters for executables
2024-01-20 : Igor Pavlov : Public domain */

#ifndef ZIP7_INC_BRA_H
#define ZIP7_INC_BRA_H



EXTERN_C_BEGIN

/* #define PPC BAD_PPC_11 // for debug */

#define Z7_BRANCH_CONV_DEC_2(name)  z7_ ## name ## _Dec
#define Z7_BRANCH_CONV_ENC_2(name)  z7_ ## name ## _Enc
#define Z7_BRANCH_CONV_DEC(name)    Z7_BRANCH_CONV_DEC_2(BranchConv_ ## name)
#define Z7_BRANCH_CONV_ENC(name)    Z7_BRANCH_CONV_ENC_2(BranchConv_ ## name)
#define Z7_BRANCH_CONV_ST_DEC(name) z7_BranchConvSt_ ## name ## _Dec
#define Z7_BRANCH_CONV_ST_ENC(name) z7_BranchConvSt_ ## name ## _Enc

#define Z7_BRANCH_CONV_DECL(name)    Byte * name(Byte *data, SizeT size, UInt32 pc)
#define Z7_BRANCH_CONV_ST_DECL(name) Byte * name(Byte *data, SizeT size, UInt32 pc, UInt32 *state)

typedef Z7_BRANCH_CONV_DECL(   (*z7_Func_BranchConv));
typedef Z7_BRANCH_CONV_ST_DECL((*z7_Func_BranchConvSt));

#define Z7_BRANCH_CONV_ST_X86_STATE_INIT_VAL 0
Z7_BRANCH_CONV_ST_DECL (Z7_BRANCH_CONV_ST_DEC(X86));
Z7_BRANCH_CONV_ST_DECL (Z7_BRANCH_CONV_ST_ENC(X86));

#define Z7_BRANCH_FUNCS_DECL(name) \
Z7_BRANCH_CONV_DECL (Z7_BRANCH_CONV_DEC_2(name)); \
Z7_BRANCH_CONV_DECL (Z7_BRANCH_CONV_ENC_2(name));

Z7_BRANCH_FUNCS_DECL (BranchConv_ARM64)
Z7_BRANCH_FUNCS_DECL (BranchConv_ARM)
Z7_BRANCH_FUNCS_DECL (BranchConv_ARMT)
Z7_BRANCH_FUNCS_DECL (BranchConv_PPC)
Z7_BRANCH_FUNCS_DECL (BranchConv_SPARC)
Z7_BRANCH_FUNCS_DECL (BranchConv_IA64)
Z7_BRANCH_FUNCS_DECL (BranchConv_RISCV)

/*
These functions convert data that contain CPU instructions.
Each such function converts relative addresses to absolute addresses in some
branch instructions: CALL (in all converters) and JUMP (X86 converter only).
Such conversion allows to increase compression ratio, if we compress that data.

There are 2 types of converters:
  Byte * Conv_RISC (Byte *data, SizeT size, UInt32 pc);
  Byte * ConvSt_X86(Byte *data, SizeT size, UInt32 pc, UInt32 *state);
Each Converter supports 2 versions: one for encoding
and one for decoding (_Enc/_Dec postfixes in function name).

In params:
  data  : data buffer
  size  : size of data
  pc    : current virtual Program Counter (Instruction Pointer) value
In/Out param:
  state : pointer to state variable (for X86 converter only)

Return:
  The pointer to position in (data) buffer after last byte that was processed.
  If the caller calls converter again, it must call it starting with that position.
  But the caller is allowed to move data in buffer. So pointer to
  current processed position also will be changed for next call.
  Also the caller must increase internal (pc) value for next call.
  
Each converter has some characteristics: Endian, Alignment, LookAhead.
  Type   Endian  Alignment  LookAhead
  
  X86    little      1          4
  ARMT   little      2          2
  RISCV  little      2          6
  ARM    little      4          0
  ARM64  little      4          0
  PPC     big        4          0
  SPARC   big        4          0
  IA64   little     16          0

  (data) must be aligned for (Alignment).
  processed size can be calculated as:
    SizeT processed = Conv(data, size, pc) - data;
  if (processed == 0)
    it means that converter needs more data for processing.
  If (size < Alignment + LookAhead)
    then (processed == 0) is allowed.

Example code for conversion in loop:
  UInt32 pc = 0;
  size = 0;
  for (;;)
  {
    size += Load_more_input_data(data + size);
    SizeT processed = Conv(data, size, pc) - data;
    if (processed == 0 && no_more_input_data_after_size)
      break; // we stop convert loop
    data += processed;
    size -= processed;
    pc += processed;
  }
*/

EXTERN_C_END

#endif



/* ==== Lzma2Dec.h ==== */
/* Lzma2Dec.h -- LZMA2 Decoder
2023-03-03 : Igor Pavlov : Public domain */

#ifndef ZIP7_INC_LZMA2_DEC_H
#define ZIP7_INC_LZMA2_DEC_H



EXTERN_C_BEGIN

/* ---------- State Interface ---------- */

typedef struct
{
  unsigned state;
  Byte control;
  Byte needInitLevel;
  Byte isExtraMode;
  Byte _pad_;
  UInt32 packSize;
  UInt32 unpackSize;
  CLzmaDec decoder;
} CLzma2Dec;

#define Lzma2Dec_CONSTRUCT(p)  LzmaDec_CONSTRUCT(&(p)->decoder)
#define Lzma2Dec_Construct(p)  Lzma2Dec_CONSTRUCT(p)
#define Lzma2Dec_FreeProbs(p, alloc)  LzmaDec_FreeProbs(&(p)->decoder, alloc)
#define Lzma2Dec_Free(p, alloc)  LzmaDec_Free(&(p)->decoder, alloc)

SRes Lzma2Dec_AllocateProbs(CLzma2Dec *p, Byte prop, ISzAllocPtr alloc);
SRes Lzma2Dec_Allocate(CLzma2Dec *p, Byte prop, ISzAllocPtr alloc);
void Lzma2Dec_Init(CLzma2Dec *p);

/*
finishMode:
  It has meaning only if the decoding reaches output limit (*destLen or dicLimit).
  LZMA_FINISH_ANY - use smallest number of input bytes
  LZMA_FINISH_END - read EndOfStream marker after decoding

Returns:
  SZ_OK
    status:
      LZMA_STATUS_FINISHED_WITH_MARK
      LZMA_STATUS_NOT_FINISHED
      LZMA_STATUS_NEEDS_MORE_INPUT
  SZ_ERROR_DATA - Data error
*/

SRes Lzma2Dec_DecodeToDic(CLzma2Dec *p, SizeT dicLimit,
    const Byte *src, SizeT *srcLen, ELzmaFinishMode finishMode, ELzmaStatus *status);

SRes Lzma2Dec_DecodeToBuf(CLzma2Dec *p, Byte *dest, SizeT *destLen,
    const Byte *src, SizeT *srcLen, ELzmaFinishMode finishMode, ELzmaStatus *status);


/* ---------- LZMA2 block and chunk parsing ---------- */

/*
Lzma2Dec_Parse() parses compressed data stream up to next independent block or next chunk data.
It can return LZMA_STATUS_* code or LZMA2_PARSE_STATUS_* code:
  - LZMA2_PARSE_STATUS_NEW_BLOCK - there is new block, and 1 additional byte (control byte of next block header) was read from input.
  - LZMA2_PARSE_STATUS_NEW_CHUNK - there is new chunk, and only lzma2 header of new chunk was read.
                                   CLzma2Dec::unpackSize contains unpack size of that chunk
*/

typedef enum
{
/*
  LZMA_STATUS_NOT_SPECIFIED                 // data error
  LZMA_STATUS_FINISHED_WITH_MARK
  LZMA_STATUS_NOT_FINISHED                  //
  LZMA_STATUS_NEEDS_MORE_INPUT
  LZMA_STATUS_MAYBE_FINISHED_WITHOUT_MARK   // unused
*/
  LZMA2_PARSE_STATUS_NEW_BLOCK = LZMA_STATUS_MAYBE_FINISHED_WITHOUT_MARK + 1,
  LZMA2_PARSE_STATUS_NEW_CHUNK
} ELzma2ParseStatus;

ELzma2ParseStatus Lzma2Dec_Parse(CLzma2Dec *p,
    SizeT outSize,   /* output size */
    const Byte *src, SizeT *srcLen,
    int checkFinishBlock   /* set (checkFinishBlock = 1), if it must read full input data, if decoder.dicPos reaches blockMax position. */
    );

/*
LZMA2 parser doesn't decode LZMA chunks, so we must read
  full input LZMA chunk to decode some part of LZMA chunk.

Lzma2Dec_GetUnpackExtra() returns the value that shows
    max possible number of output bytes that can be output by decoder
    at current input positon.
*/

#define Lzma2Dec_GetUnpackExtra(p)  ((p)->isExtraMode ? (p)->unpackSize : 0)


/* ---------- One Call Interface ---------- */

/*
finishMode:
  It has meaning only if the decoding reaches output limit (*destLen).
  LZMA_FINISH_ANY - use smallest number of input bytes
  LZMA_FINISH_END - read EndOfStream marker after decoding

Returns:
  SZ_OK
    status:
      LZMA_STATUS_FINISHED_WITH_MARK
      LZMA_STATUS_NOT_FINISHED
  SZ_ERROR_DATA - Data error
  SZ_ERROR_MEM  - Memory allocation error
  SZ_ERROR_UNSUPPORTED - Unsupported properties
  SZ_ERROR_INPUT_EOF - It needs more bytes in input buffer (src).
*/

SRes Lzma2Decode(Byte *dest, SizeT *destLen, const Byte *src, SizeT *srcLen,
    Byte prop, ELzmaFinishMode finishMode, ELzmaStatus *status, ISzAllocPtr alloc);

EXTERN_C_END

#endif
#ifdef Z7_PPMD_SUPPORT
/* ==== Ppmd7.h ==== */
/* Ppmd7.h -- Ppmd7 (PPMdH) compression codec
2023-04-02 : Igor Pavlov : Public domain
This code is based on:
  PPMd var.H (2001): Dmitry Shkarin : Public domain */
 

#ifndef ZIP7_INC_PPMD7_H
#define ZIP7_INC_PPMD7_H

/* ==== Ppmd.h ==== */
/* Ppmd.h -- PPMD codec common code
2023-03-05 : Igor Pavlov : Public domain
This code is based on PPMd var.H (2001): Dmitry Shkarin : Public domain */

#ifndef ZIP7_INC_PPMD_H
#define ZIP7_INC_PPMD_H



EXTERN_C_BEGIN

#if defined(MY_CPU_SIZEOF_POINTER) && (MY_CPU_SIZEOF_POINTER == 4)
/*
   PPMD code always uses 32-bit internal fields in PPMD structures to store internal references in main block.
   if (PPMD_32BIT is     defined), the PPMD code stores internal pointers to 32-bit reference fields.
   if (PPMD_32BIT is NOT defined), the PPMD code stores internal UInt32 offsets to reference fields.
   if (pointer size is 64-bit), then (PPMD_32BIT) mode is not allowed,
   if (pointer size is 32-bit), then (PPMD_32BIT) mode is optional,
     and it's allowed to disable PPMD_32BIT mode even if pointer is 32-bit.
   PPMD code works slightly faster in (PPMD_32BIT) mode.
*/
  #define PPMD_32BIT
#endif

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

/* amalgamated: MY_CPU_pragma_pack_push_1 spelled out. It expands
 * to _Pragma("pack(push, 1)"), which tcc 0.9.27 -- one of the
 * compilers nob.c drives -- does not parse at all, while the
 * #pragma spelling below it accepts. Nothing is lost either way:
 * every struct between here and the pop is already naturally
 * packed on the ABIs this tree builds for, which is what
 * upstream's own comment below says. */
#pragma pack(push, 1)
/* Most compilers works OK here even without #pragma pack(push, 1), but some GCC compilers need it. */

/* SEE-contexts for PPM-contexts with masked symbols */
typedef struct
{
  UInt16 Summ; /* Freq */
  Byte Shift;  /* Speed of Freq change; low Shift is for fast change */
  Byte Count;  /* Count to next change of Shift */
} CPpmd_See;

#define Ppmd_See_UPDATE(p) \
  { if ((p)->Shift < PPMD_PERIOD_BITS && --(p)->Count == 0) \
    { (p)->Summ = (UInt16)((p)->Summ << 1); \
      (p)->Count = (Byte)(3 << (p)->Shift++); }}


typedef struct
{
  Byte Symbol;
  Byte Freq;
  UInt16 Successor_0;
  UInt16 Successor_1;
} CPpmd_State;

typedef struct CPpmd_State2_
{
  Byte Symbol;
  Byte Freq;
} CPpmd_State2;

typedef struct CPpmd_State4_
{
  UInt16 Successor_0;
  UInt16 Successor_1;
} CPpmd_State4;

#pragma pack(pop)

/*
   PPMD code can write full CPpmd_State structure data to CPpmd*_Context
      at (byte offset = 2) instead of some fields of original CPpmd*_Context structure.
   
   If we use pointers to different types, but that point to shared
   memory space, we can have aliasing problem (strict aliasing).
   
   XLC compiler in -O2 mode can change the order of memory write instructions
   in relation to read instructions, if we have use pointers to different types.
   
   To solve that aliasing problem we use combined CPpmd*_Context structure
   with unions that contain the fields from both structures:
   the original CPpmd*_Context and CPpmd_State.
   So we can access the fields from both structures via one pointer,
   and the compiler doesn't change the order of write instructions
   in relation to read instructions.

   If we don't use memory write instructions to shared memory in
   some local code, and we use only reading instructions (read only),
   then probably it's safe to use pointers to different types for reading.
*/
  


#ifdef PPMD_32BIT

  #define Ppmd_Ref_Type(type)   type *
  #define Ppmd_GetRef(p, ptr)   (ptr)
  #define Ppmd_GetPtr(p, ptr)   (ptr)
  #define Ppmd_GetPtr_Type(p, ptr, note_type) (ptr)

#else

  #define Ppmd_Ref_Type(type)   UInt32
  #define Ppmd_GetRef(p, ptr)   ((UInt32)((Byte *)(ptr) - (p)->Base))
  #define Ppmd_GetPtr(p, offs)  ((void *)((p)->Base + (offs)))
  #define Ppmd_GetPtr_Type(p, offs, type) ((type *)Ppmd_GetPtr(p, offs))

#endif /* PPMD_32BIT */


typedef Ppmd_Ref_Type(CPpmd_State) CPpmd_State_Ref;
typedef Ppmd_Ref_Type(void)        CPpmd_Void_Ref;
typedef Ppmd_Ref_Type(Byte)        CPpmd_Byte_Ref;


/*
#ifdef MY_CPU_LE_UNALIGN
// the unaligned 32-bit access latency can be too large, if the data is not in L1 cache.
#define Ppmd_GET_SUCCESSOR(p) ((CPpmd_Void_Ref)*(const UInt32 *)(const void *)&(p)->Successor_0)
#define Ppmd_SET_SUCCESSOR(p, v) *(UInt32 *)(void *)(void *)&(p)->Successor_0 = (UInt32)(v)

#else
*/

/*
   We can write 16-bit halves to 32-bit (Successor) field in any selected order.
   But the native order is more consistent way.
   So we use the native order, if LE/BE order can be detected here at compile time.
*/

#ifdef MY_CPU_BE

  #define Ppmd_GET_SUCCESSOR(p) \
    ( (CPpmd_Void_Ref) (((UInt32)(p)->Successor_0 << 16) | (p)->Successor_1) )

  #define Ppmd_SET_SUCCESSOR(p, v) { \
    (p)->Successor_0 = (UInt16)(((UInt32)(v) >> 16) /* & 0xFFFF */); \
    (p)->Successor_1 = (UInt16)((UInt32)(v) /* & 0xFFFF */); }

#else

  #define Ppmd_GET_SUCCESSOR(p) \
    ( (CPpmd_Void_Ref) ((p)->Successor_0 | ((UInt32)(p)->Successor_1 << 16)) )

  #define Ppmd_SET_SUCCESSOR(p, v) { \
    (p)->Successor_0 = (UInt16)((UInt32)(v) /* & 0xFFFF */); \
    (p)->Successor_1 = (UInt16)(((UInt32)(v) >> 16) /* & 0xFFFF */); }

#endif

/* #endif */


#define PPMD_SetAllBitsIn256Bytes(p) \
  { size_t z; for (z = 0; z < 256 / sizeof(p[0]); z += 8) { \
  p[z+7] = p[z+6] = p[z+5] = p[z+4] = p[z+3] = p[z+2] = p[z+1] = p[z+0] = ~(size_t)0; }}

EXTERN_C_END
 
#endif

EXTERN_C_BEGIN

#define PPMD7_MIN_ORDER 2
#define PPMD7_MAX_ORDER 64

#define PPMD7_MIN_MEM_SIZE (1 << 11)
#define PPMD7_MAX_MEM_SIZE (0xFFFFFFFF - 12 * 3)

struct CPpmd7_Context_;

typedef Ppmd_Ref_Type(struct CPpmd7_Context_) CPpmd7_Context_Ref;

/* MY_CPU_pragma_pack_push_1 */

typedef struct CPpmd7_Context_
{
  UInt16 NumStats;


  union
  {
    UInt16 SummFreq;
    CPpmd_State2 State2;
  } Union2;

  union
  {
    CPpmd_State_Ref Stats;
    CPpmd_State4 State4;
  } Union4;

  CPpmd7_Context_Ref Suffix;
} CPpmd7_Context;

/* MY_CPU_pragma_pop */

#define Ppmd7Context_OneState(p) ((CPpmd_State *)&(p)->Union2)




typedef struct
{
  UInt32 Range;
  UInt32 Code;
  UInt32 Low;
  IByteInPtr Stream;
} CPpmd7_RangeDec;


typedef struct
{
  UInt32 Range;
  Byte Cache;
  /* Byte _dummy_[3]; */
  UInt64 Low;
  UInt64 CacheSize;
  IByteOutPtr Stream;
} CPpmd7z_RangeEnc;


typedef struct
{
  CPpmd7_Context *MinContext, *MaxContext;
  CPpmd_State *FoundState;
  unsigned OrderFall, InitEsc, PrevSuccess, MaxOrder, HiBitsFlag;
  Int32 RunLength, InitRL; /* must be 32-bit at least */

  UInt32 Size;
  UInt32 GlueCount;
  UInt32 AlignOffset;
  Byte *Base, *LoUnit, *HiUnit, *Text, *UnitsStart;


  
  
  union
  {
    CPpmd7_RangeDec dec;
    CPpmd7z_RangeEnc enc;
  } rc;
  
  Byte Indx2Units[PPMD_NUM_INDEXES + 2]; /* +2 for alignment */
  Byte Units2Indx[128];
  CPpmd_Void_Ref FreeList[PPMD_NUM_INDEXES];

  Byte NS2BSIndx[256], NS2Indx[256];
  Byte ExpEscape[16];
  CPpmd_See DummySee, See[25][16];
  UInt16 BinSumm[128][64];
  /* int LastSymbol; */
} CPpmd7;


void Ppmd7_Construct(CPpmd7 *p);
BoolInt Ppmd7_Alloc(CPpmd7 *p, UInt32 size, ISzAllocPtr alloc);
void Ppmd7_Free(CPpmd7 *p, ISzAllocPtr alloc);
void Ppmd7_Init(CPpmd7 *p, unsigned maxOrder);
#define Ppmd7_WasAllocated(p) ((p)->Base != NULL)


/* ---------- Internal Functions ---------- */

#define Ppmd7_GetPtr(p, ptr)     Ppmd_GetPtr(p, ptr)
#define Ppmd7_GetContext(p, ptr) Ppmd_GetPtr_Type(p, ptr, CPpmd7_Context)
#define Ppmd7_GetStats(p, ctx)   Ppmd_GetPtr_Type(p, (ctx)->Union4.Stats, CPpmd_State)

void Ppmd7_Update1(CPpmd7 *p);
void Ppmd7_Update1_0(CPpmd7 *p);
void Ppmd7_Update2(CPpmd7 *p);

#define PPMD7_HiBitsFlag_3(sym) ((((unsigned)sym + 0xC0) >> (8 - 3)) & (1 << 3))
#define PPMD7_HiBitsFlag_4(sym) ((((unsigned)sym + 0xC0) >> (8 - 4)) & (1 << 4))
/* #define PPMD7_HiBitsFlag_3(sym) ((sym) < 0x40 ? 0 : (1 << 3)) */
/* #define PPMD7_HiBitsFlag_4(sym) ((sym) < 0x40 ? 0 : (1 << 4)) */

#define Ppmd7_GetBinSumm(p) \
    &p->BinSumm[(size_t)(unsigned)Ppmd7Context_OneState(p->MinContext)->Freq - 1] \
    [ p->PrevSuccess + ((p->RunLength >> 26) & 0x20) \
    + p->NS2BSIndx[(size_t)Ppmd7_GetContext(p, p->MinContext->Suffix)->NumStats - 1] \
    + PPMD7_HiBitsFlag_4(Ppmd7Context_OneState(p->MinContext)->Symbol) \
    + (p->HiBitsFlag = PPMD7_HiBitsFlag_3(p->FoundState->Symbol)) ]

CPpmd_See *Ppmd7_MakeEscFreq(CPpmd7 *p, unsigned numMasked, UInt32 *scale);


/*
We support two versions of Ppmd7 (PPMdH) methods that use same CPpmd7 structure:
  1) Ppmd7a_*: original PPMdH
  2) Ppmd7z_*: modified PPMdH with 7z Range Coder
Ppmd7_*: the structures and functions that are common for both versions of PPMd7 (PPMdH)
*/

/* ---------- Decode ---------- */

#define PPMD7_SYM_END    (-1)
#define PPMD7_SYM_ERROR  (-2)

/*
You must set (CPpmd7::rc.dec.Stream) before Ppmd7*_RangeDec_Init()

Ppmd7*_DecodeSymbol()
out:
  >= 0 : decoded byte
    -1 : PPMD7_SYM_END   : End of payload marker
    -2 : PPMD7_SYM_ERROR : Data error
*/

/* Ppmd7a_* : original PPMdH */
BoolInt Ppmd7a_RangeDec_Init(CPpmd7_RangeDec *p);
#define Ppmd7a_RangeDec_IsFinishedOK(p) ((p)->Code == 0)
int Ppmd7a_DecodeSymbol(CPpmd7 *p);

/* Ppmd7z_* : modified PPMdH with 7z Range Coder */
BoolInt Ppmd7z_RangeDec_Init(CPpmd7_RangeDec *p);
#define Ppmd7z_RangeDec_IsFinishedOK(p) ((p)->Code == 0)
int Ppmd7z_DecodeSymbol(CPpmd7 *p);
/* Byte *Ppmd7z_DecodeSymbols(CPpmd7 *p, Byte *buf, const Byte *lim); */


/* ---------- Encode ---------- */

void Ppmd7z_Init_RangeEnc(CPpmd7 *p);
void Ppmd7z_Flush_RangeEnc(CPpmd7 *p);
/* void Ppmd7z_EncodeSymbol(CPpmd7 *p, int symbol); */
void Ppmd7z_EncodeSymbols(CPpmd7 *p, const Byte *buf, const Byte *lim);

EXTERN_C_END
 
#endif
#endif

#define k_Copy 0
#ifndef Z7_NO_METHOD_LZMA2
#define k_LZMA2 0x21
#endif
#define k_LZMA  0x30101
#define k_BCJ2  0x303011B

#if !defined(Z7_NO_METHODS_FILTERS)
#define Z7_USE_BRANCH_FILTER
#endif

#if !defined(Z7_NO_METHODS_FILTERS) || \
     defined(Z7_USE_NATIVE_BRANCH_FILTER) && defined(MY_CPU_ARM64)
#define Z7_USE_FILTER_ARM64
#ifndef Z7_USE_BRANCH_FILTER
#define Z7_USE_BRANCH_FILTER
#endif
#define k_ARM64 0xa
#endif

#if !defined(Z7_NO_METHODS_FILTERS) || \
     defined(Z7_USE_NATIVE_BRANCH_FILTER) && defined(MY_CPU_ARMT)
#define Z7_USE_FILTER_ARMT
#ifndef Z7_USE_BRANCH_FILTER
#define Z7_USE_BRANCH_FILTER
#endif
#define k_ARMT  0x3030701
#endif

#ifndef Z7_NO_METHODS_FILTERS
#define k_Delta 3
#define k_RISCV 0xb
#define k_BCJ   0x3030103
#define k_PPC   0x3030205
#define k_IA64  0x3030401
#define k_ARM   0x3030501
#define k_SPARC 0x3030805
#endif

#ifdef Z7_PPMD_SUPPORT

#define k_PPMD 0x30401

typedef struct
{
  IByteIn vt;
  const Byte *cur;
  const Byte *end;
  const Byte *begin;
  UInt64 processed;
  BoolInt extra;
  SRes res;
  ILookInStreamPtr inStream;
} CByteInToLook;

static Byte ReadByte(IByteInPtr pp)
{
  Z7_CONTAINER_FROM_VTBL_TO_DECL_VAR_pp_vt_p(CByteInToLook)
  if (p->cur != p->end)
    return *p->cur++;
  if (p->res == SZ_OK)
  {
    size_t size = (size_t)(p->cur - p->begin);
    p->processed += size;
    p->res = ILookInStream_Skip(p->inStream, size);
    size = (1 << 25);
    p->res = ILookInStream_Look(p->inStream, (const void **)&p->begin, &size);
    p->cur = p->begin;
    p->end = p->begin + size;
    if (size != 0)
      return *p->cur++;
  }
  p->extra = True;
  return 0;
}

static SRes SzDecodePpmd(const Byte *props, unsigned propsSize, UInt64 inSize, ILookInStreamPtr inStream,
    Byte *outBuffer, SizeT outSize, ISzAllocPtr allocMain)
{
  CPpmd7 ppmd;
  CByteInToLook s;
  SRes res = SZ_OK;

  s.vt.Read = ReadByte;
  s.inStream = inStream;
  s.begin = s.end = s.cur = NULL;
  s.extra = False;
  s.res = SZ_OK;
  s.processed = 0;

  if (propsSize != 5)
    return SZ_ERROR_UNSUPPORTED;

  {
    unsigned order = props[0];
    UInt32 memSize = GetUi32(props + 1);
    if (order < PPMD7_MIN_ORDER ||
        order > PPMD7_MAX_ORDER ||
        memSize < PPMD7_MIN_MEM_SIZE ||
        memSize > PPMD7_MAX_MEM_SIZE)
      return SZ_ERROR_UNSUPPORTED;
    Ppmd7_Construct(&ppmd);
    if (!Ppmd7_Alloc(&ppmd, memSize, allocMain))
      return SZ_ERROR_MEM;
    Ppmd7_Init(&ppmd, order);
  }
  {
    ppmd.rc.dec.Stream = &s.vt;
    if (!Ppmd7z_RangeDec_Init(&ppmd.rc.dec))
      res = SZ_ERROR_DATA;
    else if (!s.extra)
    {
      Byte *buf = outBuffer;
      const Byte *lim = buf + outSize;
      for (; buf != lim; buf++)
      {
        int sym = Ppmd7z_DecodeSymbol(&ppmd);
        if (s.extra || sym < 0)
          break;
        *buf = (Byte)sym;
      }
      if (buf != lim)
        res = SZ_ERROR_DATA;
      else if (!Ppmd7z_RangeDec_IsFinishedOK(&ppmd.rc.dec))
      {
        /* if (Ppmd7z_DecodeSymbol(&ppmd) != PPMD7_SYM_END || !Ppmd7z_RangeDec_IsFinishedOK(&ppmd.rc.dec)) */
        res = SZ_ERROR_DATA;
      }
    }
    if (s.extra)
      res = (s.res != SZ_OK ? s.res : SZ_ERROR_DATA);
    else if (s.processed + (size_t)(s.cur - s.begin) != inSize)
      res = SZ_ERROR_DATA;
  }
  Ppmd7_Free(&ppmd, allocMain);
  return res;
}

#endif


static SRes SzDecodeLzma(const Byte *props, unsigned propsSize, UInt64 inSize, ILookInStreamPtr inStream,
    Byte *outBuffer, SizeT outSize, ISzAllocPtr allocMain)
{
  CLzmaDec state;
  SRes res = SZ_OK;

  LzmaDec_CONSTRUCT(&state)
  RINOK(LzmaDec_AllocateProbs(&state, props, propsSize, allocMain))
  state.dic = outBuffer;
  state.dicBufSize = outSize;
  LzmaDec_Init(&state);

  for (;;)
  {
    const void *inBuf = NULL;
    size_t lookahead = (1 << 18);
    if (lookahead > inSize)
      lookahead = (size_t)inSize;
    res = ILookInStream_Look(inStream, &inBuf, &lookahead);
    if (res != SZ_OK)
      break;

    {
      SizeT inProcessed = (SizeT)lookahead, dicPos = state.dicPos;
      ELzmaStatus status;
      res = LzmaDec_DecodeToDic(&state, outSize, (const Byte *)inBuf, &inProcessed, LZMA_FINISH_END, &status);
      lookahead -= inProcessed;
      inSize -= inProcessed;
      if (res != SZ_OK)
        break;

      if (status == LZMA_STATUS_FINISHED_WITH_MARK)
      {
        if (outSize != state.dicPos || inSize != 0)
          res = SZ_ERROR_DATA;
        break;
      }

      if (outSize == state.dicPos && inSize == 0 && status == LZMA_STATUS_MAYBE_FINISHED_WITHOUT_MARK)
        break;

      if (inProcessed == 0 && dicPos == state.dicPos)
      {
        res = SZ_ERROR_DATA;
        break;
      }

      res = ILookInStream_Skip(inStream, inProcessed);
      if (res != SZ_OK)
        break;
    }
  }

  LzmaDec_FreeProbs(&state, allocMain);
  return res;
}


#ifndef Z7_NO_METHOD_LZMA2

static SRes SzDecodeLzma2(const Byte *props, unsigned propsSize, UInt64 inSize, ILookInStreamPtr inStream,
    Byte *outBuffer, SizeT outSize, ISzAllocPtr allocMain)
{
  CLzma2Dec state;
  SRes res = SZ_OK;

  Lzma2Dec_CONSTRUCT(&state)
  if (propsSize != 1)
    return SZ_ERROR_DATA;
  RINOK(Lzma2Dec_AllocateProbs(&state, props[0], allocMain))
  state.decoder.dic = outBuffer;
  state.decoder.dicBufSize = outSize;
  Lzma2Dec_Init(&state);

  for (;;)
  {
    const void *inBuf = NULL;
    size_t lookahead = (1 << 18);
    if (lookahead > inSize)
      lookahead = (size_t)inSize;
    res = ILookInStream_Look(inStream, &inBuf, &lookahead);
    if (res != SZ_OK)
      break;

    {
      SizeT inProcessed = (SizeT)lookahead, dicPos = state.decoder.dicPos;
      ELzmaStatus status;
      res = Lzma2Dec_DecodeToDic(&state, outSize, (const Byte *)inBuf, &inProcessed, LZMA_FINISH_END, &status);
      lookahead -= inProcessed;
      inSize -= inProcessed;
      if (res != SZ_OK)
        break;

      if (status == LZMA_STATUS_FINISHED_WITH_MARK)
      {
        if (outSize != state.decoder.dicPos || inSize != 0)
          res = SZ_ERROR_DATA;
        break;
      }

      if (inProcessed == 0 && dicPos == state.decoder.dicPos)
      {
        res = SZ_ERROR_DATA;
        break;
      }

      res = ILookInStream_Skip(inStream, inProcessed);
      if (res != SZ_OK)
        break;
    }
  }

  Lzma2Dec_FreeProbs(&state, allocMain);
  return res;
}

#endif


static SRes SzDecodeCopy(UInt64 inSize, ILookInStreamPtr inStream, Byte *outBuffer)
{
  while (inSize > 0)
  {
    const void *inBuf;
    size_t curSize = (1 << 18);
    if (curSize > inSize)
      curSize = (size_t)inSize;
    RINOK(ILookInStream_Look(inStream, &inBuf, &curSize))
    if (curSize == 0)
      return SZ_ERROR_INPUT_EOF;
    memcpy(outBuffer, inBuf, curSize);
    outBuffer += curSize;
    inSize -= curSize;
    RINOK(ILookInStream_Skip(inStream, curSize))
  }
  return SZ_OK;
}

static BoolInt IS_MAIN_METHOD(UInt32 m)
{
  switch (m)
  {
    case k_Copy:
    case k_LZMA:
  #ifndef Z7_NO_METHOD_LZMA2
    case k_LZMA2:
  #endif
  #ifdef Z7_PPMD_SUPPORT
    case k_PPMD:
  #endif
      return True;
    default:
      return False;
  }
}

static BoolInt IS_SUPPORTED_CODER(const CSzCoderInfo *c)
{
  return
      c->NumStreams == 1
      /* && c->MethodID <= (UInt32)0xFFFFFFFF */
      && IS_MAIN_METHOD((UInt32)c->MethodID);
}

#define IS_BCJ2(c) ((c)->MethodID == k_BCJ2 && (c)->NumStreams == 4)

static SRes CheckSupportedFolder(const CSzFolder *f)
{
  if (f->NumCoders < 1 || f->NumCoders > 4)
    return SZ_ERROR_UNSUPPORTED;
  if (!IS_SUPPORTED_CODER(&f->Coders[0]))
    return SZ_ERROR_UNSUPPORTED;
  if (f->NumCoders == 1)
  {
    if (f->NumPackStreams != 1 || f->PackStreams[0] != 0 || f->NumBonds != 0)
      return SZ_ERROR_UNSUPPORTED;
    return SZ_OK;
  }
  
  
  #if defined(Z7_USE_BRANCH_FILTER)

  if (f->NumCoders == 2)
  {
    const CSzCoderInfo *c = &f->Coders[1];
    if (
        /* c->MethodID > (UInt32)0xFFFFFFFF || */
        c->NumStreams != 1
        || f->NumPackStreams != 1
        || f->PackStreams[0] != 0
        || f->NumBonds != 1
        || f->Bonds[0].InIndex != 1
        || f->Bonds[0].OutIndex != 0)
      return SZ_ERROR_UNSUPPORTED;
    switch ((UInt32)c->MethodID)
    {
    #if !defined(Z7_NO_METHODS_FILTERS)
      case k_Delta:
      case k_BCJ:
      case k_PPC:
      case k_IA64:
      case k_SPARC:
      case k_ARM:
      case k_RISCV:
    #endif
    #ifdef Z7_USE_FILTER_ARM64
      case k_ARM64:
    #endif
    #ifdef Z7_USE_FILTER_ARMT
      case k_ARMT:
    #endif
        break;
      default:
        return SZ_ERROR_UNSUPPORTED;
    }
    return SZ_OK;
  }

  #endif

  
  if (f->NumCoders == 4)
  {
    if (!IS_SUPPORTED_CODER(&f->Coders[1])
        || !IS_SUPPORTED_CODER(&f->Coders[2])
        || !IS_BCJ2(&f->Coders[3]))
      return SZ_ERROR_UNSUPPORTED;
    if (f->NumPackStreams != 4
        || f->PackStreams[0] != 2
        || f->PackStreams[1] != 6
        || f->PackStreams[2] != 1
        || f->PackStreams[3] != 0
        || f->NumBonds != 3
        || f->Bonds[0].InIndex != 5 || f->Bonds[0].OutIndex != 0
        || f->Bonds[1].InIndex != 4 || f->Bonds[1].OutIndex != 1
        || f->Bonds[2].InIndex != 3 || f->Bonds[2].OutIndex != 2)
      return SZ_ERROR_UNSUPPORTED;
    return SZ_OK;
  }
  
  return SZ_ERROR_UNSUPPORTED;
}






static SRes SzFolder_Decode2(const CSzFolder *folder,
    const Byte *propsData,
    const UInt64 *unpackSizes,
    const UInt64 *packPositions,
    ILookInStreamPtr inStream, UInt64 startPos,
    Byte *outBuffer, SizeT outSize, ISzAllocPtr allocMain,
    Byte *tempBuf[])
{
  UInt32 ci;
  SizeT tempSizes[3] = { 0, 0, 0};
  SizeT tempSize3 = 0;
  Byte *tempBuf3 = 0;

  RINOK(CheckSupportedFolder(folder))

  for (ci = 0; ci < folder->NumCoders; ci++)
  {
    const CSzCoderInfo *coder = &folder->Coders[ci];

    if (IS_MAIN_METHOD((UInt32)coder->MethodID))
    {
      UInt32 si = 0;
      UInt64 offset;
      UInt64 inSize;
      Byte *outBufCur = outBuffer;
      SizeT outSizeCur = outSize;
      if (folder->NumCoders == 4)
      {
        const UInt32 indices[] = { 3, 2, 0 };
        const UInt64 unpackSize = unpackSizes[ci];
        si = indices[ci];
        if (ci < 2)
        {
          Byte *temp;
          outSizeCur = (SizeT)unpackSize;
          if (outSizeCur != unpackSize)
            return SZ_ERROR_MEM;
          temp = (Byte *)ISzAlloc_Alloc(allocMain, outSizeCur);
          if (!temp && outSizeCur != 0)
            return SZ_ERROR_MEM;
          outBufCur = tempBuf[1 - ci] = temp;
          tempSizes[1 - ci] = outSizeCur;
        }
        else if (ci == 2)
        {
          if (unpackSize > outSize) /* check it */
            return SZ_ERROR_PARAM;
          tempBuf3 = outBufCur = outBuffer + (outSize - (size_t)unpackSize);
          tempSize3 = outSizeCur = (SizeT)unpackSize;
        }
        else
          return SZ_ERROR_UNSUPPORTED;
      }
      offset = packPositions[si];
      inSize = packPositions[(size_t)si + 1] - offset;
      RINOK(LookInStream_SeekTo(inStream, startPos + offset))

      if (coder->MethodID == k_Copy)
      {
        if (inSize != outSizeCur) /* check it */
          return SZ_ERROR_DATA;
        RINOK(SzDecodeCopy(inSize, inStream, outBufCur))
      }
      else if (coder->MethodID == k_LZMA)
      {
        RINOK(SzDecodeLzma(propsData + coder->PropsOffset, coder->PropsSize, inSize, inStream, outBufCur, outSizeCur, allocMain))
      }
    #ifndef Z7_NO_METHOD_LZMA2
      else if (coder->MethodID == k_LZMA2)
      {
        RINOK(SzDecodeLzma2(propsData + coder->PropsOffset, coder->PropsSize, inSize, inStream, outBufCur, outSizeCur, allocMain))
      }
    #endif
    #ifdef Z7_PPMD_SUPPORT
      else if (coder->MethodID == k_PPMD)
      {
        RINOK(SzDecodePpmd(propsData + coder->PropsOffset, coder->PropsSize, inSize, inStream, outBufCur, outSizeCur, allocMain))
      }
    #endif
      else
        return SZ_ERROR_UNSUPPORTED;
    }
    else if (coder->MethodID == k_BCJ2)
    {
      const UInt64 offset = packPositions[1];
      const UInt64 s3Size = packPositions[2] - offset;
      
      if (ci != 3)
        return SZ_ERROR_UNSUPPORTED;
      
      tempSizes[2] = (SizeT)s3Size;
      if (tempSizes[2] != s3Size)
        return SZ_ERROR_MEM;
      tempBuf[2] = (Byte *)ISzAlloc_Alloc(allocMain, tempSizes[2]);
      if (!tempBuf[2] && tempSizes[2] != 0)
        return SZ_ERROR_MEM;
      
      RINOK(LookInStream_SeekTo(inStream, startPos + offset))
      RINOK(SzDecodeCopy(s3Size, inStream, tempBuf[2]))

      if ((tempSizes[0] & 3) != 0 ||
          (tempSizes[1] & 3) != 0 ||
          tempSize3 + tempSizes[0] + tempSizes[1] != outSize)
        return SZ_ERROR_DATA;

      {
        CBcj2Dec p;
        
        p.bufs[0] = tempBuf3;   p.lims[0] = tempBuf3 + tempSize3;
        p.bufs[1] = tempBuf[0]; p.lims[1] = tempBuf[0] + tempSizes[0];
        p.bufs[2] = tempBuf[1]; p.lims[2] = tempBuf[1] + tempSizes[1];
        p.bufs[3] = tempBuf[2]; p.lims[3] = tempBuf[2] + tempSizes[2];
        
        p.dest = outBuffer;
        p.destLim = outBuffer + outSize;
        
        Bcj2Dec_Init(&p);
        RINOK(Bcj2Dec_Decode(&p))

        {
          unsigned i;
          for (i = 0; i < 4; i++)
            if (p.bufs[i] != p.lims[i])
              return SZ_ERROR_DATA;
          if (p.dest != p.destLim || !Bcj2Dec_IsMaybeFinished(&p))
            return SZ_ERROR_DATA;
        }
      }
    }
#if defined(Z7_USE_BRANCH_FILTER)
    else if (ci == 1)
    {
#if !defined(Z7_NO_METHODS_FILTERS)
      if (coder->MethodID == k_Delta)
      {
        if (coder->PropsSize != 1)
          return SZ_ERROR_UNSUPPORTED;
        {
          Byte state[DELTA_STATE_SIZE];
          Delta_Init(state);
          Delta_Decode(state, (unsigned)(propsData[coder->PropsOffset]) + 1, outBuffer, outSize);
        }
        continue;
      }
#endif
     
#ifdef Z7_USE_FILTER_ARM64
      if (coder->MethodID == k_ARM64)
      {
        UInt32 pc = 0;
        if (coder->PropsSize == 4)
        {
          pc = GetUi32(propsData + coder->PropsOffset);
          if (pc & 3)
            return SZ_ERROR_UNSUPPORTED;
        }
        else if (coder->PropsSize != 0)
          return SZ_ERROR_UNSUPPORTED;
        z7_BranchConv_ARM64_Dec(outBuffer, outSize, pc);
        continue;
      }
#endif

#if !defined(Z7_NO_METHODS_FILTERS)
      if (coder->MethodID == k_RISCV)
      {
        UInt32 pc = 0;
        if (coder->PropsSize == 4)
        {
          pc = GetUi32(propsData + coder->PropsOffset);
          if (pc & 1)
            return SZ_ERROR_UNSUPPORTED;
        }
        else if (coder->PropsSize != 0)
          return SZ_ERROR_UNSUPPORTED;
        z7_BranchConv_RISCV_Dec(outBuffer, outSize, pc);
        continue;
      }
#endif
      
#if !defined(Z7_NO_METHODS_FILTERS) || defined(Z7_USE_FILTER_ARMT)
      {
        if (coder->PropsSize != 0)
          return SZ_ERROR_UNSUPPORTED;
       #define CASE_BRA_CONV(isa) case k_ ## isa: Z7_BRANCH_CONV_DEC(isa)(outBuffer, outSize, 0); break; /* pc = 0; */
        switch (coder->MethodID)
        {
         #if !defined(Z7_NO_METHODS_FILTERS)
          case k_BCJ:
          {
            UInt32 state = Z7_BRANCH_CONV_ST_X86_STATE_INIT_VAL;
            z7_BranchConvSt_X86_Dec(outBuffer, outSize, 0, &state); /* pc = 0 */
            break;
          }
          case k_PPC: Z7_BRANCH_CONV_DEC_2(BranchConv_PPC)(outBuffer, outSize, 0); break; /* pc = 0; */
          /* CASE_BRA_CONV(PPC) */
          CASE_BRA_CONV(IA64)
          CASE_BRA_CONV(SPARC)
          CASE_BRA_CONV(ARM)
         #endif
         #if !defined(Z7_NO_METHODS_FILTERS) || defined(Z7_USE_FILTER_ARMT)
          CASE_BRA_CONV(ARMT)
         #endif
          default:
            return SZ_ERROR_UNSUPPORTED;
        }
        continue;
      }
#endif
    } /* (c == 1) */
#endif /* Z7_USE_BRANCH_FILTER */
    else
      return SZ_ERROR_UNSUPPORTED;
  }

  return SZ_OK;
}


SRes SzAr_DecodeFolder(const CSzAr *p, UInt32 folderIndex,
    ILookInStreamPtr inStream, UInt64 startPos,
    Byte *outBuffer, size_t outSize,
    ISzAllocPtr allocMain)
{
  SRes res;
  CSzFolder folder;
  CSzData sd;
  
  const Byte *data = p->CodersData + p->FoCodersOffsets[folderIndex];
  sd.Data = data;
  sd.Size = p->FoCodersOffsets[(size_t)folderIndex + 1] - p->FoCodersOffsets[folderIndex];
  
  res = SzGetNextFolderItem(&folder, &sd);
  
  if (res != SZ_OK)
    return res;

  if (sd.Size != 0
      || folder.UnpackStream != p->FoToMainUnpackSizeIndex[folderIndex]
      || outSize != SzAr_GetFolderUnpackSize(p, folderIndex))
    return SZ_ERROR_FAIL;
  {
    unsigned i;
    Byte *tempBuf[3] = { 0, 0, 0};

    res = SzFolder_Decode2(&folder, data,
        &p->CoderUnpackSizes[p->FoToCoderUnpackSizes[folderIndex]],
        p->PackPositions + p->FoStartPackStreamIndex[folderIndex],
        inStream, startPos,
        outBuffer, (SizeT)outSize, allocMain, tempBuf);
    
    for (i = 0; i < 3; i++)
      ISzAlloc_Free(allocMain, tempBuf[i]);

    if (res == SZ_OK)
      if (SzBitWithVals_Check(&p->FolderCRCs, folderIndex))
        if (CrcCalc(outBuffer, outSize) != p->FolderCRCs.Vals[folderIndex])
          res = SZ_ERROR_CRC;

    return res;
  }
}
#undef CASE_BRA_CONV
#undef IS_BCJ2
#undef Z7_USE_BRANCH_FILTER
#undef Z7_USE_FILTER_ARM64
#undef Z7_USE_FILTER_ARMT
#undef k_ARM
#undef k_ARM64
#undef k_ARMT
#undef k_BCJ
#undef k_BCJ2
#undef k_Copy
#undef k_Delta
#undef k_IA64
#undef k_LZMA
#undef k_LZMA2
#undef k_PPC
#undef k_PPMD
#undef k_RISCV
#undef k_SPARC

/* ==== 7zFile.c ==== */
/* 7zFile.c -- File IO
2023-04-02 : Igor Pavlov : Public domain */





#ifndef USE_WINDOWS_FILE

  #include <errno.h>

  #ifndef USE_FOPEN
    #include <stdio.h>
    #include <fcntl.h>
    #ifdef _WIN32
      #include <io.h>
      typedef int ssize_t;
      typedef int off_t;
    #else
      #include <unistd.h>
    #endif
  #endif

#else

/*
   ReadFile and WriteFile functions in Windows have BUG:
   If you Read or Write 64MB or more (probably min_failure_size = 64MB - 32KB + 1)
   from/to Network file, it returns ERROR_NO_SYSTEM_RESOURCES
   (Insufficient system resources exist to complete the requested service).
   Probably in some version of Windows there are problems with other sizes:
   for 32 MB (maybe also for 16 MB).
   And message can be "Network connection was lost"
*/

#endif

#define kChunkSizeMax (1 << 22)

void File_Construct(CSzFile *p)
{
  #ifdef USE_WINDOWS_FILE
  p->handle = INVALID_HANDLE_VALUE;
  #elif defined(USE_FOPEN)
  p->file = NULL;
  #else
  p->fd = -1;
  #endif
}

#if !defined(UNDER_CE) || !defined(USE_WINDOWS_FILE)

static WRes File_Open(CSzFile *p, const char *name, int writeMode)
{
  #ifdef USE_WINDOWS_FILE
  
  p->handle = CreateFileA(name,
      writeMode ? GENERIC_WRITE : GENERIC_READ,
      FILE_SHARE_READ, NULL,
      writeMode ? CREATE_ALWAYS : OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL, NULL);
  return (p->handle != INVALID_HANDLE_VALUE) ? 0 : GetLastError();
  
  #elif defined(USE_FOPEN)
  
  p->file = fopen(name, writeMode ? "wb+" : "rb");
  return (p->file != 0) ? 0 :
    #ifdef UNDER_CE
    2; /* ENOENT */
    #else
    errno;
    #endif
  
  #else

  int flags = (writeMode ? (O_CREAT | O_EXCL | O_WRONLY) : O_RDONLY);
  #ifdef O_BINARY
  flags |= O_BINARY;
  #endif
  p->fd = open(name, flags, 0666);
  return (p->fd != -1) ? 0 : errno;

  #endif
}

WRes InFile_Open(CSzFile *p, const char *name) { return File_Open(p, name, 0); }

WRes OutFile_Open(CSzFile *p, const char *name)
{
  #if defined(USE_WINDOWS_FILE) || defined(USE_FOPEN)
  return File_Open(p, name, 1);
  #else
  p->fd = creat(name, 0666);
  return (p->fd != -1) ? 0 : errno;
  #endif
}

#endif


#ifdef USE_WINDOWS_FILE
static WRes File_OpenW(CSzFile *p, const WCHAR *name, int writeMode)
{
  p->handle = CreateFileW(name,
      writeMode ? GENERIC_WRITE : GENERIC_READ,
      FILE_SHARE_READ, NULL,
      writeMode ? CREATE_ALWAYS : OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL, NULL);
  return (p->handle != INVALID_HANDLE_VALUE) ? 0 : GetLastError();
}
WRes InFile_OpenW(CSzFile *p, const WCHAR *name) { return File_OpenW(p, name, 0); }
WRes OutFile_OpenW(CSzFile *p, const WCHAR *name) { return File_OpenW(p, name, 1); }
#endif

WRes File_Close(CSzFile *p)
{
  #ifdef USE_WINDOWS_FILE
  
  if (p->handle != INVALID_HANDLE_VALUE)
  {
    if (!CloseHandle(p->handle))
      return GetLastError();
    p->handle = INVALID_HANDLE_VALUE;
  }
  
  #elif defined(USE_FOPEN)

  if (p->file != NULL)
  {
    int res = fclose(p->file);
    if (res != 0)
    {
      if (res == EOF)
        return errno;
      return res;
    }
    p->file = NULL;
  }

  #else

  if (p->fd != -1)
  {
    if (close(p->fd) != 0)
      return errno;
    p->fd = -1;
  }

  #endif

  return 0;
}


WRes File_Read(CSzFile *p, void *data, size_t *size)
{
  size_t originalSize = *size;
  *size = 0;
  if (originalSize == 0)
    return 0;

  #ifdef USE_WINDOWS_FILE

  do
  {
    const DWORD curSize = (originalSize > kChunkSizeMax) ? kChunkSizeMax : (DWORD)originalSize;
    DWORD processed = 0;
    const BOOL res = ReadFile(p->handle, data, curSize, &processed, NULL);
    data = (void *)((Byte *)data + processed);
    originalSize -= processed;
    *size += processed;
    if (!res)
      return GetLastError();
    /* debug : we can break here for partial reading mode */
    if (processed == 0)
      break;
  }
  while (originalSize > 0);

  #elif defined(USE_FOPEN)

  do
  {
    const size_t curSize = (originalSize > kChunkSizeMax) ? kChunkSizeMax : originalSize;
    const size_t processed = fread(data, 1, curSize, p->file);
    data = (void *)((Byte *)data + (size_t)processed);
    originalSize -= processed;
    *size += processed;
    if (processed != curSize)
      return ferror(p->file);
    /* debug : we can break here for partial reading mode */
    if (processed == 0)
      break;
  }
  while (originalSize > 0);

  #else

  do
  {
    const size_t curSize = (originalSize > kChunkSizeMax) ? kChunkSizeMax : originalSize;
    const ssize_t processed = read(p->fd, data, curSize);
    if (processed == -1)
      return errno;
    if (processed == 0)
      break;
    data = (void *)((Byte *)data + (size_t)processed);
    originalSize -= (size_t)processed;
    *size += (size_t)processed;
    /* debug : we can break here for partial reading mode */
    /* break; */
  }
  while (originalSize > 0);

  #endif

  return 0;
}


WRes File_Write(CSzFile *p, const void *data, size_t *size)
{
  size_t originalSize = *size;
  *size = 0;
  if (originalSize == 0)
    return 0;
  
  #ifdef USE_WINDOWS_FILE

  do
  {
    const DWORD curSize = (originalSize > kChunkSizeMax) ? kChunkSizeMax : (DWORD)originalSize;
    DWORD processed = 0;
    const BOOL res = WriteFile(p->handle, data, curSize, &processed, NULL);
    data = (const void *)((const Byte *)data + processed);
    originalSize -= processed;
    *size += processed;
    if (!res)
      return GetLastError();
    if (processed == 0)
      break;
  }
  while (originalSize > 0);

  #elif defined(USE_FOPEN)

  do
  {
    const size_t curSize = (originalSize > kChunkSizeMax) ? kChunkSizeMax : originalSize;
    const size_t processed = fwrite(data, 1, curSize, p->file);
    data = (void *)((Byte *)data + (size_t)processed);
    originalSize -= processed;
    *size += processed;
    if (processed != curSize)
      return ferror(p->file);
    if (processed == 0)
      break;
  }
  while (originalSize > 0);

  #else

  do
  {
    const size_t curSize = (originalSize > kChunkSizeMax) ? kChunkSizeMax : originalSize;
    const ssize_t processed = write(p->fd, data, curSize);
    if (processed == -1)
      return errno;
    if (processed == 0)
      break;
    data = (const void *)((const Byte *)data + (size_t)processed);
    originalSize -= (size_t)processed;
    *size += (size_t)processed;
  }
  while (originalSize > 0);

  #endif

  return 0;
}


WRes File_Seek(CSzFile *p, Int64 *pos, ESzSeek origin)
{
  #ifdef USE_WINDOWS_FILE

  DWORD moveMethod;
  UInt32 low = (UInt32)*pos;
  LONG high = (LONG)((UInt64)*pos >> 16 >> 16); /* for case when UInt64 is 32-bit only */
  /* (int) to eliminate clang warning */
  switch ((int)origin)
  {
    case SZ_SEEK_SET: moveMethod = FILE_BEGIN; break;
    case SZ_SEEK_CUR: moveMethod = FILE_CURRENT; break;
    case SZ_SEEK_END: moveMethod = FILE_END; break;
    default: return ERROR_INVALID_PARAMETER;
  }
  low = SetFilePointer(p->handle, (LONG)low, &high, moveMethod);
  if (low == (UInt32)0xFFFFFFFF)
  {
    WRes res = GetLastError();
    if (res != NO_ERROR)
      return res;
  }
  *pos = ((Int64)high << 32) | low;
  return 0;

  #else
  
  int moveMethod; /* = origin; */

  switch ((int)origin)
  {
    case SZ_SEEK_SET: moveMethod = SEEK_SET; break;
    case SZ_SEEK_CUR: moveMethod = SEEK_CUR; break;
    case SZ_SEEK_END: moveMethod = SEEK_END; break;
    default: return EINVAL;
  }
  
  #if defined(USE_FOPEN)
  {
    int res = fseek(p->file, (long)*pos, moveMethod);
    if (res == -1)
      return errno;
    *pos = ftell(p->file);
    if (*pos == -1)
      return errno;
    return 0;
  }
  #else
  {
    off_t res = lseek(p->fd, (off_t)*pos, moveMethod);
    if (res == -1)
      return errno;
    *pos = res;
    return 0;
  }
  
  #endif /* USE_FOPEN */
  #endif /* USE_WINDOWS_FILE */
}


WRes File_GetLength(CSzFile *p, UInt64 *length)
{
  #ifdef USE_WINDOWS_FILE
  
  DWORD sizeHigh;
  DWORD sizeLow = GetFileSize(p->handle, &sizeHigh);
  if (sizeLow == 0xFFFFFFFF)
  {
    DWORD res = GetLastError();
    if (res != NO_ERROR)
      return res;
  }
  *length = (((UInt64)sizeHigh) << 32) + sizeLow;
  return 0;
  
  #elif defined(USE_FOPEN)
  
  long pos = ftell(p->file);
  int res = fseek(p->file, 0, SEEK_END);
  *length = ftell(p->file);
  fseek(p->file, pos, SEEK_SET);
  return res;

  #else

  off_t pos;
  *length = 0;
  pos = lseek(p->fd, 0, SEEK_CUR);
  if (pos != -1)
  {
    const off_t len2 = lseek(p->fd, 0, SEEK_END);
    const off_t res2 = lseek(p->fd, pos, SEEK_SET);
    if (len2 != -1)
    {
      *length = (UInt64)len2;
      if (res2 != -1)
        return 0;
    }
  }
  return errno;
  
  #endif
}


/* ---------- FileSeqInStream ---------- */

static SRes FileSeqInStream_Read(ISeqInStreamPtr pp, void *buf, size_t *size)
{
  Z7_CONTAINER_FROM_VTBL_TO_DECL_VAR_pp_vt_p(CFileSeqInStream)
  const WRes wres = File_Read(&p->file, buf, size);
  p->wres = wres;
  return (wres == 0) ? SZ_OK : SZ_ERROR_READ;
}

void FileSeqInStream_CreateVTable(CFileSeqInStream *p)
{
  p->vt.Read = FileSeqInStream_Read;
}


/* ---------- FileInStream ---------- */

static SRes FileInStream_Read(ISeekInStreamPtr pp, void *buf, size_t *size)
{
  Z7_CONTAINER_FROM_VTBL_TO_DECL_VAR_pp_vt_p(CFileInStream)
  const WRes wres = File_Read(&p->file, buf, size);
  p->wres = wres;
  return (wres == 0) ? SZ_OK : SZ_ERROR_READ;
}

static SRes FileInStream_Seek(ISeekInStreamPtr pp, Int64 *pos, ESzSeek origin)
{
  Z7_CONTAINER_FROM_VTBL_TO_DECL_VAR_pp_vt_p(CFileInStream)
  const WRes wres = File_Seek(&p->file, pos, origin);
  p->wres = wres;
  return (wres == 0) ? SZ_OK : SZ_ERROR_READ;
}

void FileInStream_CreateVTable(CFileInStream *p)
{
  p->vt.Read = FileInStream_Read;
  p->vt.Seek = FileInStream_Seek;
}


/* ---------- FileOutStream ---------- */

static size_t FileOutStream_Write(ISeqOutStreamPtr pp, const void *data, size_t size)
{
  Z7_CONTAINER_FROM_VTBL_TO_DECL_VAR_pp_vt_p(CFileOutStream)
  const WRes wres = File_Write(&p->file, data, &size);
  p->wres = wres;
  return size;
}

void FileOutStream_CreateVTable(CFileOutStream *p)
{
  p->vt.Write = FileOutStream_Write;
}
#undef kChunkSizeMax

/* ==== 7zStream.c ==== */
/* 7zStream.c -- 7z Stream functions
2023-04-02 : Igor Pavlov : Public domain */



#include <string.h>




SRes SeqInStream_ReadMax(ISeqInStreamPtr stream, void *buf, size_t *processedSize)
{
  size_t size = *processedSize;
  *processedSize = 0;
  while (size != 0)
  {
    size_t cur = size;
    const SRes res = ISeqInStream_Read(stream, buf, &cur);
    *processedSize += cur;
    buf = (void *)((Byte *)buf + cur);
    size -= cur;
    if (res != SZ_OK)
      return res;
    if (cur == 0)
      return SZ_OK;
  }
  return SZ_OK;
}

/*
SRes SeqInStream_Read2(ISeqInStreamPtr stream, void *buf, size_t size, SRes errorType)
{
  while (size != 0)
  {
    size_t processed = size;
    RINOK(ISeqInStream_Read(stream, buf, &processed))
    if (processed == 0)
      return errorType;
    buf = (void *)((Byte *)buf + processed);
    size -= processed;
  }
  return SZ_OK;
}

SRes SeqInStream_Read(ISeqInStreamPtr stream, void *buf, size_t size)
{
  return SeqInStream_Read2(stream, buf, size, SZ_ERROR_INPUT_EOF);
}
*/


SRes SeqInStream_ReadByte(ISeqInStreamPtr stream, Byte *buf)
{
  size_t processed = 1;
  RINOK(ISeqInStream_Read(stream, buf, &processed))
  return (processed == 1) ? SZ_OK : SZ_ERROR_INPUT_EOF;
}



SRes LookInStream_SeekTo(ILookInStreamPtr stream, UInt64 offset)
{
  Int64 t = (Int64)offset;
  return ILookInStream_Seek(stream, &t, SZ_SEEK_SET);
}

SRes LookInStream_LookRead(ILookInStreamPtr stream, void *buf, size_t *size)
{
  const void *lookBuf;
  if (*size == 0)
    return SZ_OK;
  RINOK(ILookInStream_Look(stream, &lookBuf, size))
  memcpy(buf, lookBuf, *size);
  return ILookInStream_Skip(stream, *size);
}

SRes LookInStream_Read2(ILookInStreamPtr stream, void *buf, size_t size, SRes errorType)
{
  while (size != 0)
  {
    size_t processed = size;
    RINOK(ILookInStream_Read(stream, buf, &processed))
    if (processed == 0)
      return errorType;
    buf = (void *)((Byte *)buf + processed);
    size -= processed;
  }
  return SZ_OK;
}

SRes LookInStream_Read(ILookInStreamPtr stream, void *buf, size_t size)
{
  return LookInStream_Read2(stream, buf, size, SZ_ERROR_INPUT_EOF);
}



#define GET_LookToRead2  Z7_CONTAINER_FROM_VTBL_TO_DECL_VAR_pp_vt_p(CLookToRead2)

static SRes LookToRead2_Look_Lookahead(ILookInStreamPtr pp, const void **buf, size_t *size)
{
  SRes res = SZ_OK;
  GET_LookToRead2
  size_t size2 = p->size - p->pos;
  if (size2 == 0 && *size != 0)
  {
    p->pos = 0;
    p->size = 0;
    size2 = p->bufSize;
    res = ISeekInStream_Read(p->realStream, p->buf, &size2);
    p->size = size2;
  }
  if (*size > size2)
    *size = size2;
  *buf = p->buf + p->pos;
  return res;
}

static SRes LookToRead2_Look_Exact(ILookInStreamPtr pp, const void **buf, size_t *size)
{
  SRes res = SZ_OK;
  GET_LookToRead2
  size_t size2 = p->size - p->pos;
  if (size2 == 0 && *size != 0)
  {
    p->pos = 0;
    p->size = 0;
    if (*size > p->bufSize)
      *size = p->bufSize;
    res = ISeekInStream_Read(p->realStream, p->buf, size);
    size2 = p->size = *size;
  }
  if (*size > size2)
    *size = size2;
  *buf = p->buf + p->pos;
  return res;
}

static SRes LookToRead2_Skip(ILookInStreamPtr pp, size_t offset)
{
  GET_LookToRead2
  p->pos += offset;
  return SZ_OK;
}

static SRes LookToRead2_Read(ILookInStreamPtr pp, void *buf, size_t *size)
{
  GET_LookToRead2
  size_t rem = p->size - p->pos;
  if (rem == 0)
    return ISeekInStream_Read(p->realStream, buf, size);
  if (rem > *size)
    rem = *size;
  memcpy(buf, p->buf + p->pos, rem);
  p->pos += rem;
  *size = rem;
  return SZ_OK;
}

static SRes LookToRead2_Seek(ILookInStreamPtr pp, Int64 *pos, ESzSeek origin)
{
  GET_LookToRead2
  p->pos = p->size = 0;
  return ISeekInStream_Seek(p->realStream, pos, origin);
}

void LookToRead2_CreateVTable(CLookToRead2 *p, int lookahead)
{
  p->vt.Look = lookahead ?
      LookToRead2_Look_Lookahead :
      LookToRead2_Look_Exact;
  p->vt.Skip = LookToRead2_Skip;
  p->vt.Read = LookToRead2_Read;
  p->vt.Seek = LookToRead2_Seek;
}



static SRes SecToLook_Read(ISeqInStreamPtr pp, void *buf, size_t *size)
{
  Z7_CONTAINER_FROM_VTBL_TO_DECL_VAR_pp_vt_p(CSecToLook)
  return LookInStream_LookRead(p->realStream, buf, size);
}

void SecToLook_CreateVTable(CSecToLook *p)
{
  p->vt.Read = SecToLook_Read;
}

static SRes SecToRead_Read(ISeqInStreamPtr pp, void *buf, size_t *size)
{
  Z7_CONTAINER_FROM_VTBL_TO_DECL_VAR_pp_vt_p(CSecToRead)
  return ILookInStream_Read(p->realStream, buf, size);
}

void SecToRead_CreateVTable(CSecToRead *p)
{
  p->vt.Read = SecToRead_Read;
}
#undef GET_LookToRead2

/* ==== Bcj2.c ==== */
/* Bcj2.c -- BCJ2 Decoder (Converter for x86 code)
2023-03-01 : Igor Pavlov : Public domain */






#define kTopValue ((UInt32)1 << 24)
#define kNumBitModelTotalBits 11
#define kBitModelTotal (1 << kNumBitModelTotalBits)
#define kNumMoveBits 5

/* UInt32 bcj2_stats[256 + 2][2]; */

void Bcj2Dec_Init(CBcj2Dec *p)
{
  unsigned i;
  p->state = BCJ2_STREAM_RC; /* BCJ2_DEC_STATE_OK; */
  p->ip = 0;
  p->temp = 0;
  p->range = 0;
  p->code = 0;
  for (i = 0; i < sizeof(p->probs) / sizeof(p->probs[0]); i++)
    p->probs[i] = kBitModelTotal >> 1;
}

SRes Bcj2Dec_Decode(CBcj2Dec *p)
{
  UInt32 v = p->temp;
  /* const Byte *src; */
  if (p->range <= 5)
  {
    UInt32 code = p->code;
    p->state = BCJ2_DEC_STATE_ERROR; /* for case if we return SZ_ERROR_DATA; */
    for (; p->range != 5; p->range++)
    {
      if (p->range == 1 && code != 0)
        return SZ_ERROR_DATA;
      if (p->bufs[BCJ2_STREAM_RC] == p->lims[BCJ2_STREAM_RC])
      {
        p->state = BCJ2_STREAM_RC;
        return SZ_OK;
      }
      code = (code << 8) | *(p->bufs[BCJ2_STREAM_RC])++;
      p->code = code;
    }
    if (code == 0xffffffff)
      return SZ_ERROR_DATA;
    p->range = 0xffffffff;
  }
  /* else */
  {
    unsigned state = p->state;
    /* we check BCJ2_IS_32BIT_STREAM() here instead of check in the main loop */
    if (BCJ2_IS_32BIT_STREAM(state))
    {
      const Byte *cur = p->bufs[state];
      if (cur == p->lims[state])
        return SZ_OK;
      p->bufs[state] = cur + 4;
      {
        const UInt32 ip = p->ip + 4;
        v = GetBe32a(cur) - ip;
        p->ip = ip;
      }
      state = BCJ2_DEC_STATE_ORIG_0;
    }
    if ((unsigned)(state - BCJ2_DEC_STATE_ORIG_0) < 4)
    {
      Byte *dest = p->dest;
      for (;;)
      {
        if (dest == p->destLim)
        {
          p->state = state;
          p->temp = v;
          return SZ_OK;
        }
        *dest++ = (Byte)v;
        p->dest = dest;
        if (++state == BCJ2_DEC_STATE_ORIG_3 + 1)
          break;
        v >>= 8;
      }
    }
  }

  /* src = p->bufs[BCJ2_STREAM_MAIN]; */
  for (;;)
  {
    /*
    if (BCJ2_IS_32BIT_STREAM(p->state))
      p->state = BCJ2_DEC_STATE_OK;
    else
    */
    {
      if (p->range < kTopValue)
      {
        if (p->bufs[BCJ2_STREAM_RC] == p->lims[BCJ2_STREAM_RC])
        {
          p->state = BCJ2_STREAM_RC;
          p->temp = v;
          return SZ_OK;
        }
        p->range <<= 8;
        p->code = (p->code << 8) | *(p->bufs[BCJ2_STREAM_RC])++;
      }
      {
        const Byte *src = p->bufs[BCJ2_STREAM_MAIN];
        const Byte *srcLim;
        Byte *dest = p->dest;
        {
          const SizeT rem = (SizeT)(p->lims[BCJ2_STREAM_MAIN] - src);
          SizeT num = (SizeT)(p->destLim - dest);
          if (num >= rem)
            num = rem;
        #define NUM_ITERS 4
        #if (NUM_ITERS & (NUM_ITERS - 1)) == 0
          num &= ~((SizeT)NUM_ITERS - 1);   /* if (NUM_ITERS == (1 << x)) */
        #else
          num -= num % NUM_ITERS; /* if (NUM_ITERS != (1 << x)) */
        #endif
          srcLim = src + num;
        }

        #define NUM_SHIFT_BITS  24
        #define ONE_ITER(indx) { \
          const unsigned b = src[indx]; \
          *dest++ = (Byte)b; \
          v = (v << NUM_SHIFT_BITS) | b; \
          if (((b + (0x100 - 0xe8)) & 0xfe) == 0) break; \
          if (((v - (((UInt32)0x0f << (NUM_SHIFT_BITS)) + 0x80)) & \
              ((((UInt32)1 << (4 + NUM_SHIFT_BITS)) - 0x1) << 4)) == 0) break; \
            /* ++dest */; /* v = b; */ }
          
        if (src != srcLim)
        for (;;)
        {
            /* The dependency chain of 2-cycle for (v) calculation is not big problem here.
               But we can remove dependency chain with v = b in the end of loop. */
          ONE_ITER(0)
          #if (NUM_ITERS > 1)
            ONE_ITER(1)
          #if (NUM_ITERS > 2)
            ONE_ITER(2)
          #if (NUM_ITERS > 3)
            ONE_ITER(3)
          #if (NUM_ITERS > 4)
            ONE_ITER(4)
          #if (NUM_ITERS > 5)
            ONE_ITER(5)
          #if (NUM_ITERS > 6)
            ONE_ITER(6)
          #if (NUM_ITERS > 7)
            ONE_ITER(7)
          #endif
          #endif
          #endif
          #endif
          #endif
          #endif
          #endif
          
          src += NUM_ITERS;
          if (src == srcLim)
            break;
        }

        if (src == srcLim)
      #if (NUM_ITERS > 1)
        for (;;)
      #endif
        {
        #if (NUM_ITERS > 1)
          if (src == p->lims[BCJ2_STREAM_MAIN] || dest == p->destLim)
        #endif
          {
            const SizeT num = (SizeT)(src - p->bufs[BCJ2_STREAM_MAIN]);
            p->bufs[BCJ2_STREAM_MAIN] = src;
            p->dest = dest;
            p->ip += (UInt32)num;
            /* state BCJ2_STREAM_MAIN has more priority than BCJ2_STATE_ORIG */
            p->state =
              src == p->lims[BCJ2_STREAM_MAIN] ?
                (unsigned)BCJ2_STREAM_MAIN :
                (unsigned)BCJ2_DEC_STATE_ORIG;
            p->temp = v;
            return SZ_OK;
          }
        #if (NUM_ITERS > 1)
          ONE_ITER(0)
          src++;
        #endif
        }

        {
          const SizeT num = (SizeT)(dest - p->dest);
          p->dest = dest; /* p->dest += num; */
          p->bufs[BCJ2_STREAM_MAIN] += num; /* = src; */
          p->ip += (UInt32)num;
        }
        {
          UInt32 bound, ttt;
          CBcj2Prob *prob; /* unsigned index; */
          /*
          prob = p->probs + (unsigned)((Byte)v == 0xe8 ?
              2 + (Byte)(v >> 8) :
              ((v >> 5) & 1));  // ((Byte)v < 0xe8 ? 0 : 1));
          */
          {
            const unsigned c = ((v + 0x17) >> 6) & 1;
            prob = p->probs + (unsigned)
                (((0 - c) & (Byte)(v >> NUM_SHIFT_BITS)) + c + ((v >> 5) & 1));
                /* (Byte) */
                /* 8x->0     : e9->1     : xxe8->xx+2 */
                /* 8x->0x100 : e9->0x101 : xxe8->xx */
                /* (((0x100 - (e & ~v)) & (0x100 | (v >> 8))) + (e & v)); */
                /* (((0x101 + (~e | v)) & (0x100 | (v >> 8))) + (e & v)); */
          }
          ttt = *prob;
          bound = (p->range >> kNumBitModelTotalBits) * ttt;
          if (p->code < bound)
          {
            /* bcj2_stats[prob - p->probs][0]++; */
            p->range = bound;
            *prob = (CBcj2Prob)(ttt + ((kBitModelTotal - ttt) >> kNumMoveBits));
            continue;
          }
          {
            /* bcj2_stats[prob - p->probs][1]++; */
            p->range -= bound;
            p->code -= bound;
            *prob = (CBcj2Prob)(ttt - (ttt >> kNumMoveBits));
          }
        }
      }
    }
    {
      /* (v == 0xe8 ? 0 : 1) uses setcc instruction with additional zero register usage in x64 MSVC. */
      /* const unsigned cj = ((Byte)v == 0xe8) ? BCJ2_STREAM_CALL : BCJ2_STREAM_JUMP; */
      const unsigned cj = (((v + 0x57) >> 6) & 1) + BCJ2_STREAM_CALL;
      const Byte *cur = p->bufs[cj];
      Byte *dest;
      SizeT rem;
      if (cur == p->lims[cj])
      {
        p->state = cj;
        break;
      }
      v = GetBe32a(cur);
      p->bufs[cj] = cur + 4;
      {
        const UInt32 ip = p->ip + 4;
        v -= ip;
        p->ip = ip;
      }
      dest = p->dest;
      rem = (SizeT)(p->destLim - dest);
      if (rem < 4)
      {
        if ((unsigned)rem > 0) { dest[0] = (Byte)v;  v >>= 8;
        if ((unsigned)rem > 1) { dest[1] = (Byte)v;  v >>= 8;
        if ((unsigned)rem > 2) { dest[2] = (Byte)v;  v >>= 8; }}}
        p->temp = v;
        p->dest = dest + rem;
        p->state = BCJ2_DEC_STATE_ORIG_0 + (unsigned)rem;
        break;
      }
      SetUi32(dest, v)
      v >>= 24;
      p->dest = dest + 4;
    }
  }

  if (p->range < kTopValue && p->bufs[BCJ2_STREAM_RC] != p->lims[BCJ2_STREAM_RC])
  {
    p->range <<= 8;
    p->code = (p->code << 8) | *(p->bufs[BCJ2_STREAM_RC])++;
  }
  return SZ_OK;
}

#undef NUM_ITERS
#undef ONE_ITER
#undef NUM_SHIFT_BITS
#undef kTopValue
#undef kNumBitModelTotalBits
#undef kBitModelTotal
#undef kNumMoveBits
#undef NUM_ITERS
#undef NUM_SHIFT_BITS
#undef ONE_ITER
#undef kBitModelTotal
#undef kNumBitModelTotalBits
#undef kNumMoveBits
#undef kTopValue

/* ==== Bra.c ==== */
/* Bra.c -- Branch converters for RISC code
2024-01-20 : Igor Pavlov : Public domain */




/* ==== RotateDefs.h ==== */
/* RotateDefs.h -- Rotate functions
2023-06-18 : Igor Pavlov : Public domain */

#ifndef ZIP7_INC_ROTATE_DEFS_H
#define ZIP7_INC_ROTATE_DEFS_H

#ifdef _MSC_VER

#include <stdlib.h>

/* don't use _rotl with old MINGW. It can insert slow call to function. */
 
/* #if (_MSC_VER >= 1200) */
#pragma intrinsic(_rotl)
#pragma intrinsic(_rotr)
/* #endif */

#define rotlFixed(x, n) _rotl((x), (n))
#define rotrFixed(x, n) _rotr((x), (n))

#if (_MSC_VER >= 1300)
#define Z7_ROTL64(x, n) _rotl64((x), (n))
#define Z7_ROTR64(x, n) _rotr64((x), (n))
#else
#define Z7_ROTL64(x, n) (((x) << (n)) | ((x) >> (64 - (n))))
#define Z7_ROTR64(x, n) (((x) >> (n)) | ((x) << (64 - (n))))
#endif

#else

/* new compilers can translate these macros to fast commands. */

#if defined(__clang__) && (__clang_major__ >= 4) \
  || defined(__GNUC__) && (__GNUC__ >= 5)
/* GCC 4.9.0 and clang 3.5 can recognize more correct version: */
#define rotlFixed(x, n) (((x) << (n)) | ((x) >> (-(n) & 31)))
#define rotrFixed(x, n) (((x) >> (n)) | ((x) << (-(n) & 31)))
#define Z7_ROTL64(x, n) (((x) << (n)) | ((x) >> (-(n) & 63)))
#define Z7_ROTR64(x, n) (((x) >> (n)) | ((x) << (-(n) & 63)))
#else
/* for old GCC / clang: */
#define rotlFixed(x, n) (((x) << (n)) | ((x) >> (32 - (n))))
#define rotrFixed(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define Z7_ROTL64(x, n) (((x) << (n)) | ((x) >> (64 - (n))))
#define Z7_ROTR64(x, n) (((x) >> (n)) | ((x) << (64 - (n))))
#endif

#endif

#endif


#if defined(MY_CPU_SIZEOF_POINTER) \
    && ( MY_CPU_SIZEOF_POINTER == 4 \
      || MY_CPU_SIZEOF_POINTER == 8)
  #define BR_CONV_USE_OPT_PC_PTR
#endif

#ifdef BR_CONV_USE_OPT_PC_PTR
#define BR_PC_INIT  pc -= (UInt32)(SizeT)p;
#define BR_PC_GET   (pc + (UInt32)(SizeT)p)
#else
#define BR_PC_INIT  pc += (UInt32)size;
#define BR_PC_GET   (pc - (UInt32)(SizeT)(lim - p))
/* #define BR_PC_INIT */
/* #define BR_PC_GET   (pc + (UInt32)(SizeT)(p - data)) */
#endif

#define BR_CONVERT_VAL(v, c) if (encoding) v += c; else v -= c;
/* #define BR_CONVERT_VAL(v, c) if (!encoding) c = (UInt32)0 - c; v += c; */

#define Z7_BRANCH_CONV(name) z7_ ## name

#define Z7_BRANCH_FUNC_MAIN(name) \
static \
Z7_FORCE_INLINE \
Z7_ATTRIB_NO_VECTOR \
Byte *Z7_BRANCH_CONV(name)(Byte *p, SizeT size, UInt32 pc, int encoding)

#define Z7_BRANCH_FUNC_IMP(name, m, encoding) \
Z7_NO_INLINE \
Z7_ATTRIB_NO_VECTOR \
Byte *m(name)(Byte *data, SizeT size, UInt32 pc) \
  { return Z7_BRANCH_CONV(name)(data, size, pc, encoding); } \

#ifdef Z7_EXTRACT_ONLY
#define Z7_BRANCH_FUNCS_IMP(name) \
  Z7_BRANCH_FUNC_IMP(name, Z7_BRANCH_CONV_DEC_2, 0)
#else
#define Z7_BRANCH_FUNCS_IMP(name) \
  Z7_BRANCH_FUNC_IMP(name, Z7_BRANCH_CONV_DEC_2, 0) \
  Z7_BRANCH_FUNC_IMP(name, Z7_BRANCH_CONV_ENC_2, 1)
#endif

#if defined(__clang__)
#define BR_EXTERNAL_FOR
#define BR_NEXT_ITERATION  continue;
#else
#define BR_EXTERNAL_FOR    for (;;)
#define BR_NEXT_ITERATION  break;
#endif

#if defined(__clang__) && (__clang_major__ >= 8) \
  || defined(__GNUC__) && (__GNUC__ >= 1000) \
  /* GCC is not good for __builtin_expect() here */
  /* || defined(_MSC_VER) && (_MSC_VER >= 1920) */
  /* #define Z7_unlikely [[unlikely]] */
  /* #define Z7_LIKELY(x)   (__builtin_expect((x), 1)) */
  #define Z7_UNLIKELY(x) (__builtin_expect((x), 0))
  /* #define Z7_likely [[likely]] */
#else
  /* #define Z7_LIKELY(x)   (x) */
  #define Z7_UNLIKELY(x) (x)
  /* #define Z7_likely */
#endif


Z7_BRANCH_FUNC_MAIN(BranchConv_ARM64)
{
  /* Byte *p = data; */
  const Byte *lim;
  const UInt32 flag = (UInt32)1 << (24 - 4);
  const UInt32 mask = ((UInt32)1 << 24) - (flag << 1);
  size &= ~(SizeT)3;
  /* if (size == 0) return p; */
  lim = p + size;
  BR_PC_INIT
  pc -= 4;  /* because (p) will point to next instruction */
  
  BR_EXTERNAL_FOR
  {
    /* Z7_PRAGMA_OPT_DISABLE_LOOP_UNROLL_VECTORIZE */
    for (;;)
    {
      UInt32 v;
      if Z7_UNLIKELY(p == lim)
        return p;
      v = GetUi32a(p);
      p += 4;
      if Z7_UNLIKELY(((v - 0x94000000) & 0xfc000000) == 0)
      {
        UInt32 c = BR_PC_GET >> 2;
        BR_CONVERT_VAL(v, c)
        v &= 0x03ffffff;
        v |= 0x94000000;
        SetUi32a(p - 4, v)
        BR_NEXT_ITERATION
      }
      /* v = rotlFixed(v, 8);  v += (flag << 8) - 0x90;  if Z7_UNLIKELY((v & ((mask << 8) + 0x9f)) == 0) */
      v -= 0x90000000;  if Z7_UNLIKELY((v & 0x9f000000) == 0)
      {
        UInt32 z, c;
        /* v = rotrFixed(v, 8); */
        v += flag; if Z7_UNLIKELY(v & mask) continue;
        z = (v & 0xffffffe0) | (v >> 26);
        c = (BR_PC_GET >> (12 - 3)) & ~(UInt32)7;
        BR_CONVERT_VAL(z, c)
        v &= 0x1f;
        v |= 0x90000000;
        v |= z << 26;
        v |= 0x00ffffe0 & ((z & (((flag << 1) - 1))) - flag);
        SetUi32a(p - 4, v)
      }
    }
  }
}
Z7_BRANCH_FUNCS_IMP(BranchConv_ARM64)


Z7_BRANCH_FUNC_MAIN(BranchConv_ARM)
{
  /* Byte *p = data; */
  const Byte *lim;
  size &= ~(SizeT)3;
  lim = p + size;
  BR_PC_INIT
  /* in ARM: branch offset is relative to the +2 instructions from current instruction.
     (p) will point to next instruction */
  pc += 8 - 4;
  
  for (;;)
  {
    for (;;)
    {
      if Z7_UNLIKELY(p >= lim) { return p; }  p += 4;  if Z7_UNLIKELY(p[-1] == 0xeb) break;
      if Z7_UNLIKELY(p >= lim) { return p; }  p += 4;  if Z7_UNLIKELY(p[-1] == 0xeb) break;
    }
    {
      UInt32 v = GetUi32a(p - 4);
      UInt32 c = BR_PC_GET >> 2;
      BR_CONVERT_VAL(v, c)
      v &= 0x00ffffff;
      v |= 0xeb000000;
      SetUi32a(p - 4, v)
    }
  }
}
Z7_BRANCH_FUNCS_IMP(BranchConv_ARM)


Z7_BRANCH_FUNC_MAIN(BranchConv_PPC)
{
  /* Byte *p = data; */
  const Byte *lim;
  size &= ~(SizeT)3;
  lim = p + size;
  BR_PC_INIT
  pc -= 4;  /* because (p) will point to next instruction */
  
  for (;;)
  {
    UInt32 v;
    for (;;)
    {
      if Z7_UNLIKELY(p == lim)
        return p;
      /* v = GetBe32a(p); */
      v = *(UInt32 *)(void *)p;
      p += 4;
      /* if ((v & 0xfc000003) == 0x48000001) break; */
      /* if ((p[-4] & 0xFC) == 0x48 && (p[-1] & 3) == 1) break; */
      if Z7_UNLIKELY(
          ((v - Z7_CONV_BE_TO_NATIVE_CONST32(0x48000001))
              & Z7_CONV_BE_TO_NATIVE_CONST32(0xfc000003)) == 0) break;
    }
    {
      v = Z7_CONV_NATIVE_TO_BE_32(v);
      {
        UInt32 c = BR_PC_GET;
        BR_CONVERT_VAL(v, c)
      }
      v &= 0x03ffffff;
      v |= 0x48000000;
      SetBe32a(p - 4, v)
    }
  }
}
Z7_BRANCH_FUNCS_IMP(BranchConv_PPC)


#ifdef Z7_CPU_FAST_ROTATE_SUPPORTED
#define BR_SPARC_USE_ROTATE
#endif

Z7_BRANCH_FUNC_MAIN(BranchConv_SPARC)
{
  /* Byte *p = data; */
  const Byte *lim;
  const UInt32 flag = (UInt32)1 << 22;
  size &= ~(SizeT)3;
  lim = p + size;
  BR_PC_INIT
  pc -= 4;  /* because (p) will point to next instruction */
  for (;;)
  {
    UInt32 v;
    for (;;)
    {
      if Z7_UNLIKELY(p == lim)
        return p;
      /* // the code without GetBe32a():
      { const UInt32 v = GetUi16a(p) & 0xc0ff; p += 4; if (v == 0x40 || v == 0xc07f) break; }
      */
      v = GetBe32a(p);
      p += 4;
    #ifdef BR_SPARC_USE_ROTATE
      v = rotlFixed(v, 2);
      v += (flag << 2) - 1;
      if Z7_UNLIKELY((v & (3 - (flag << 3))) == 0)
    #else
      v += (UInt32)5 << 29;
      v ^= (UInt32)7 << 29;
      v += flag;
      if Z7_UNLIKELY((v & (0 - (flag << 1))) == 0)
    #endif
        break;
    }
    {
      /* UInt32 v = GetBe32a(p - 4); */
    #ifndef BR_SPARC_USE_ROTATE
      v <<= 2;
    #endif
      {
        UInt32 c = BR_PC_GET;
        BR_CONVERT_VAL(v, c)
      }
      v &= (flag << 3) - 1;
    #ifdef BR_SPARC_USE_ROTATE
      v -= (flag << 2) - 1;
      v = rotrFixed(v, 2);
    #else
      v -= (flag << 2);
      v >>= 2;
      v |= (UInt32)1 << 30;
    #endif
      SetBe32a(p - 4, v)
    }
  }
}
Z7_BRANCH_FUNCS_IMP(BranchConv_SPARC)


Z7_BRANCH_FUNC_MAIN(BranchConv_ARMT)
{
  /* Byte *p = data; */
  Byte *lim;
  size &= ~(SizeT)1;
  /* if (size == 0) return p; */
  if (size <= 2) return p;
  size -= 2;
  lim = p + size;
  BR_PC_INIT
  /* in ARM: branch offset is relative to the +2 instructions from current instruction.
     (p) will point to the +2 instructions from current instruction */
  /* pc += 4 - 4; */
  /* if (encoding) pc -= 0xf800 << 1; else pc += 0xf800 << 1; */
  /* #define ARMT_TAIL_PROC { goto armt_tail; } */
  #define ARMT_TAIL_PROC { return p; }
  
  do
  {
    /* in MSVC 32-bit x86 compilers:
       UInt32 version : it loads value from memory with movzx
       Byte   version : it loads value to 8-bit register (AL/CL)
       movzx version is slightly faster in some cpus
    */
    unsigned b1;
    /* Byte / unsigned */
    b1 = p[1];
    /* optimized version to reduce one (p >= lim) check: */
    /* unsigned a1 = p[1];  b1 = p[3];  p += 2;  if Z7_LIKELY((b1 & (a1 ^ 8)) < 0xf8) */
    for (;;)
    {
      unsigned b3; /* Byte / UInt32 */
      /* (Byte)(b3) normalization can use low byte computations in MSVC.
         It gives smaller code, and no loss of speed in some compilers/cpus.
         But new MSVC 32-bit x86 compilers use more slow load
         from memory to low byte register in that case.
         So we try to use full 32-bit computations for faster code.
      */
      /* if (p >= lim) { ARMT_TAIL_PROC }  b3 = b1 + 8;  b1 = p[3];  p += 2;  if ((b3 & b1) >= 0xf8) break; */
      if Z7_UNLIKELY(p >= lim) { ARMT_TAIL_PROC }  b3 = p[3];  p += 2;  if Z7_UNLIKELY((b3 & (b1 ^ 8)) >= 0xf8) break;
      if Z7_UNLIKELY(p >= lim) { ARMT_TAIL_PROC }  b1 = p[3];  p += 2;  if Z7_UNLIKELY((b1 & (b3 ^ 8)) >= 0xf8) break;
    }
    {
      /* we can adjust pc for (0xf800) to rid of (& 0x7FF) operation.
         But gcc/clang for arm64 can use bfi instruction for full code here */
      UInt32 v =
          ((UInt32)GetUi16a(p - 2) << 11) |
          ((UInt32)GetUi16a(p) & 0x7FF);
      /*
      UInt32 v =
            ((UInt32)p[1 - 2] << 19)
          + (((UInt32)p[1] & 0x7) << 8)
          + (((UInt32)p[-2] << 11))
          + (p[0]);
      */
      p += 2;
      {
        UInt32 c = BR_PC_GET >> 1;
        BR_CONVERT_VAL(v, c)
      }
      SetUi16a(p - 4, (UInt16)(((v >> 11) & 0x7ff) | 0xf000))
      SetUi16a(p - 2, (UInt16)(v | 0xf800))
      /*
      p[-4] = (Byte)(v >> 11);
      p[-3] = (Byte)(0xf0 | ((v >> 19) & 0x7));
      p[-2] = (Byte)v;
      p[-1] = (Byte)(0xf8 | (v >> 8));
      */
    }
  }
  while (p < lim);
  return p;
  /* armt_tail: */
  /* if ((Byte)((lim[1] & 0xf8)) != 0xf0) { lim += 2; }  return lim; */
  /* return (Byte *)(lim + ((Byte)((lim[1] ^ 0xf0) & 0xf8) == 0 ? 0 : 2)); */
  /* return (Byte *)(lim + (((lim[1] ^ ~0xfu) & ~7u) == 0 ? 0 : 2)); */
  /* return (Byte *)(lim + 2 - (((((unsigned)lim[1] ^ 8) + 8) >> 7) & 2)); */
}
Z7_BRANCH_FUNCS_IMP(BranchConv_ARMT)


/* #define BR_IA64_NO_INLINE */

Z7_BRANCH_FUNC_MAIN(BranchConv_IA64)
{
  /* Byte *p = data; */
  const Byte *lim;
  size &= ~(SizeT)15;
  lim = p + size;
  pc -= 1 << 4;
  pc >>= 4 - 1;
  /* pc -= 1 << 1; */
  
  for (;;)
  {
    unsigned m;
    for (;;)
    {
      if Z7_UNLIKELY(p == lim)
        return p;
      m = (unsigned)((UInt32)0x334b0000 >> (*p & 0x1e));
      p += 16;
      pc += 1 << 1;
      if (m &= 3)
        break;
    }
    {
      p += (ptrdiff_t)m * 5 - 20; /* negative value is expected here. */
      do
      {
        const UInt32 t =
          #if defined(MY_CPU_X86_OR_AMD64)
            /* we use 32-bit load here to reduce code size on x86: */
            GetUi32(p);
          #else
            GetUi16(p);
          #endif
        UInt32 z = GetUi32(p + 1) >> m;
        p += 5;
        if (((t >> m) & (0x70 << 1)) == 0
            && ((z - (0x5000000 << 1)) & (0xf000000 << 1)) == 0)
        {
          UInt32 v = (UInt32)((0x8fffff << 1) | 1) & z;
          z ^= v;
        #ifdef BR_IA64_NO_INLINE
          v |= (v & ((UInt32)1 << (23 + 1))) >> 3;
          {
            UInt32 c = pc;
            BR_CONVERT_VAL(v, c)
          }
          v &= (0x1fffff << 1) | 1;
        #else
          {
            if (encoding)
            {
              /* pc &= ~(0xc00000 << 1); // we just need to clear at least 2 bits */
              pc &= (0x1fffff << 1) | 1;
              v += pc;
            }
            else
            {
              /* pc |= 0xc00000 << 1; // we need to set at least 2 bits */
              pc |= ~(UInt32)((0x1fffff << 1) | 1);
              v -= pc;
            }
          }
          v &= ~(UInt32)(0x600000 << 1);
        #endif
          v += (0x700000 << 1);
          v &= (0x8fffff << 1) | 1;
          z |= v;
          z <<= m;
          SetUi32(p + 1 - 5, z)
        }
        m++;
      }
      while (m &= 3); /* while (m < 4); */
    }
  }
}
Z7_BRANCH_FUNCS_IMP(BranchConv_IA64)


#define BR_CONVERT_VAL_ENC(v)  v += BR_PC_GET;
#define BR_CONVERT_VAL_DEC(v)  v -= BR_PC_GET;

#if 1 && defined(MY_CPU_LE_UNALIGN)
  #define RISCV_USE_UNALIGNED_LOAD
#endif

#ifdef RISCV_USE_UNALIGNED_LOAD
  #define RISCV_GET_UI32(p)      GetUi32(p)
  #define RISCV_SET_UI32(p, v)   { SetUi32(p, v) }
#else
  #define RISCV_GET_UI32(p) \
    ((UInt32)GetUi16a(p) + \
    ((UInt32)GetUi16a((p) + 2) << 16))
  #define RISCV_SET_UI32(p, v) { \
    SetUi16a(p, (UInt16)(v)) \
    SetUi16a((p) + 2, (UInt16)(v >> 16)) }
#endif

#if 1 && defined(MY_CPU_LE)
  #define RISCV_USE_16BIT_LOAD
#endif

#ifdef RISCV_USE_16BIT_LOAD
  #define RISCV_LOAD_VAL(p)  GetUi16a(p)
#else
  #define RISCV_LOAD_VAL(p)  (*(p))
#endif

#define RISCV_INSTR_SIZE  2
#define RISCV_STEP_1      (4 + RISCV_INSTR_SIZE)
#define RISCV_STEP_2      4
#define RISCV_REG_VAL     (2 << 7)
#define RISCV_CMD_VAL     3
#if 1
  /* for code size optimization: */
  #define RISCV_DELTA_7F  0x7f
#else
  #define RISCV_DELTA_7F  0
#endif

#define RISCV_CHECK_1(v, b) \
    (((((b) - RISCV_CMD_VAL) ^ ((v) << 8)) & (0xf8000 + RISCV_CMD_VAL)) == 0)

#if 1
  #define RISCV_CHECK_2(v, r) \
    ((((v) - ((RISCV_CMD_VAL << 12) | RISCV_REG_VAL | 8)) \
           << 18) \
     < ((r) & 0x1d))
#else
  /* this branch gives larger code, because */
  /* compilers generate larger code for big constants. */
  #define RISCV_CHECK_2(v, r) \
    ((((v) - ((RISCV_CMD_VAL << 12) | RISCV_REG_VAL)) \
           & ((RISCV_CMD_VAL << 12) | RISCV_REG_VAL)) \
     < ((r) & 0x1d))
#endif


#define RISCV_SCAN_LOOP \
  Byte *lim; \
  size &= ~(SizeT)(RISCV_INSTR_SIZE - 1); \
  if (size <= 6) return p; \
  size -= 6; \
  lim = p + size; \
  BR_PC_INIT \
  for (;;) \
  { \
    UInt32 a, v; \
    /* Z7_PRAGMA_OPT_DISABLE_LOOP_UNROLL_VECTORIZE */ \
    for (;;) \
    { \
      if Z7_UNLIKELY(p >= lim) { return p; } \
      a = (RISCV_LOAD_VAL(p) ^ 0x10u) + 1; \
      if ((a & 0x77) == 0) break; \
      a = (RISCV_LOAD_VAL(p + RISCV_INSTR_SIZE) ^ 0x10u) + 1; \
      p += RISCV_INSTR_SIZE * 2; \
      if ((a & 0x77) == 0) \
      { \
        p -= RISCV_INSTR_SIZE; \
        if Z7_UNLIKELY(p >= lim) { return p; } \
        break; \
      } \
    }
/* (xx6f ^ 10) + 1 = xx7f + 1 = xx80       : JAL */
/* (xxef ^ 10) + 1 = xxff + 1 = xx00 + 100 : JAL */
/* (xx17 ^ 10) + 1 = xx07 + 1 = xx08       : AUIPC */
/* (xx97 ^ 10) + 1 = xx87 + 1 = xx88       : AUIPC */

Byte * Z7_BRANCH_CONV_ENC(RISCV)(Byte *p, SizeT size, UInt32 pc)
{
  RISCV_SCAN_LOOP
    v = a;
    a = RISCV_GET_UI32(p);
#ifndef RISCV_USE_16BIT_LOAD
    v += (UInt32)p[1] << 8;
#endif

    if ((v & 8) == 0) /* JAL */
    {
      if ((v - (0x100 /* - RISCV_DELTA_7F */)) & 0xd80)
      {
        p += RISCV_INSTR_SIZE;
        continue;
      }
      {
        v = ((a &    1u << 31) >> 11)
          | ((a & 0x3ff << 21) >> 20)
          | ((a &     1 << 20) >> 9)
          |  (a &  0xff << 12);
        BR_CONVERT_VAL_ENC(v)
        /* ((v & 1) == 0) */
        /* v: bits [1 : 20] contain offset bits */
#if 0 && defined(RISCV_USE_UNALIGNED_LOAD)
        a &= 0xfff;
        a |= ((UInt32)(v << 23))
          |  ((UInt32)(v <<  7) & ((UInt32)0xff << 16))
          |  ((UInt32)(v >>  5) & ((UInt32)0xf0 << 8));
        RISCV_SET_UI32(p, a)
#else /* aligned */
#if 0
        SetUi16a(p, (UInt16)(((v >> 5) & 0xf000) | (a & 0xfff)))
#else
        p[1] = (Byte)(((v >> 13) & 0xf0) | ((a >> 8) & 0xf));
#endif

#if 1 && defined(Z7_CPU_FAST_BSWAP_SUPPORTED) && defined(MY_CPU_LE)
        v <<= 15;
        v = Z7_BSWAP32(v);
        SetUi16a(p + 2, (UInt16)v)
#else
        p[2] = (Byte)(v >> 9);
        p[3] = (Byte)(v >> 1);
#endif
#endif /* aligned */
      }
      p += 4;
      continue;
    } /* JAL */

    {
      /* AUIPC */
      if (v & 0xe80)  /* (not x0) and (not x2) */
      {
        const UInt32 b = RISCV_GET_UI32(p + 4);
        if (RISCV_CHECK_1(v, b))
        {
          {
            const UInt32 temp = (b << 12) | (0x17 + RISCV_REG_VAL);
            RISCV_SET_UI32(p, temp)
          }
          a &= 0xfffff000;
          {
#if 1
          const int t = -1 >> 1;
          if (t != -1)
            a += (b >> 20) - ((b >> 19) & 0x1000); /* arithmetic right shift emulation */
          else
#endif
            a += (UInt32)((Int32)b >> 20); /* arithmetic right shift (sign-extension). */
          }
          BR_CONVERT_VAL_ENC(a)
#if 1 && defined(Z7_CPU_FAST_BSWAP_SUPPORTED) && defined(MY_CPU_LE)
          a = Z7_BSWAP32(a);
          RISCV_SET_UI32(p + 4, a)
#else
          SetBe32(p + 4, a)
#endif
          p += 8;
        }
        else
          p += RISCV_STEP_1;
      }
      else
      {
        UInt32 r = a >> 27;
        if (RISCV_CHECK_2(v, r))
        {
          v = RISCV_GET_UI32(p + 4);
          r = (r << 7) + 0x17 + (v & 0xfffff000);
          a = (a >> 12) | (v << 20);
          RISCV_SET_UI32(p, r)
          RISCV_SET_UI32(p + 4, a)
          p += 8;
        }
        else
          p += RISCV_STEP_2;
      }
    }
  } /* for */
}


Byte * Z7_BRANCH_CONV_DEC(RISCV)(Byte *p, SizeT size, UInt32 pc)
{
  RISCV_SCAN_LOOP
#ifdef RISCV_USE_16BIT_LOAD
    if ((a & 8) == 0)
    {
#else
    v = a;
    a += (UInt32)p[1] << 8;
    if ((v & 8) == 0)
    {
#endif
      /* JAL */
      a -= 0x100 - RISCV_DELTA_7F;
      if (a & 0xd80)
      {
        p += RISCV_INSTR_SIZE;
        continue;
      }
      {
        const UInt32 a_old = (a + (0xef - RISCV_DELTA_7F)) & 0xfff;
#if 0 /* unaligned */
        a = GetUi32(p);
        v = (UInt32)(a >> 23) & ((UInt32)0xff << 1)
          | (UInt32)(a >>  7) & ((UInt32)0xff << 9)
#elif 1 && defined(Z7_CPU_FAST_BSWAP_SUPPORTED) && defined(MY_CPU_LE)
        v = GetUi16a(p + 2);
        v = Z7_BSWAP32(v) >> 15
#else
        v = (UInt32)p[3] << 1
          | (UInt32)p[2] << 9
#endif
          | (UInt32)((a & 0xf000) << 5);
        BR_CONVERT_VAL_DEC(v)
        a = a_old
          | (v << 11 &    1u << 31)
          | (v << 20 & 0x3ff << 21)
          | (v <<  9 &     1 << 20)
          | (v       &  0xff << 12);
        RISCV_SET_UI32(p, a)
      }
      p += 4;
      continue;
    } /* JAL */

    {
      /* AUIPC */
      v = a;
#if 1 && defined(RISCV_USE_UNALIGNED_LOAD)
      a = GetUi32(p);
#else
      a |= (UInt32)GetUi16a(p + 2) << 16;
#endif
      if ((v & 0xe80) == 0)  /* x0/x2 */
      {
        const UInt32 r = a >> 27;
        if (RISCV_CHECK_2(v, r))
        {
          UInt32 b;
#if 1 && defined(Z7_CPU_FAST_BSWAP_SUPPORTED) && defined(MY_CPU_LE)
          b = RISCV_GET_UI32(p + 4);
          b = Z7_BSWAP32(b);
#else
          b = GetBe32(p + 4);
#endif
          v = a >> 12;
          BR_CONVERT_VAL_DEC(b)
          a = (r << 7) + 0x17;
          a += (b + 0x800) & 0xfffff000;
          v |= b << 20;
          RISCV_SET_UI32(p, a)
          RISCV_SET_UI32(p + 4, v)
          p += 8;
        }
        else
          p += RISCV_STEP_2;
      }
      else
      {
        const UInt32 b = RISCV_GET_UI32(p + 4);
        if (!RISCV_CHECK_1(v, b))
          p += RISCV_STEP_1;
        else
        {
          v = (a & 0xfffff000) | (b >> 20);
          a = (b << 12) | (0x17 + RISCV_REG_VAL);
          RISCV_SET_UI32(p, a)
          RISCV_SET_UI32(p + 4, v)
          p += 8;
        }
      }
    }
  } /* for */
}
#undef ARMT_TAIL_PROC
#undef BR_CONVERT_VAL
#undef BR_CONVERT_VAL_DEC
#undef BR_CONVERT_VAL_ENC
#undef BR_CONV_USE_OPT_PC_PTR
#undef BR_EXTERNAL_FOR
#undef BR_NEXT_ITERATION
#undef BR_PC_GET
#undef BR_PC_INIT
#undef BR_SPARC_USE_ROTATE
#undef RISCV_CHECK_1
#undef RISCV_CHECK_2
#undef RISCV_CMD_VAL
#undef RISCV_DELTA_7F
#undef RISCV_GET_UI32
#undef RISCV_INSTR_SIZE
#undef RISCV_LOAD_VAL
#undef RISCV_REG_VAL
#undef RISCV_SCAN_LOOP
#undef RISCV_SET_UI32
#undef RISCV_STEP_1
#undef RISCV_STEP_2
#undef RISCV_USE_16BIT_LOAD
#undef RISCV_USE_UNALIGNED_LOAD
#undef Z7_BRANCH_CONV
#undef Z7_BRANCH_FUNCS_IMP
#undef Z7_BRANCH_FUNC_IMP
#undef Z7_BRANCH_FUNC_MAIN
#undef Z7_UNLIKELY

/* ==== Bra86.c ==== */
/* Bra86.c -- Branch converter for X86 code (BCJ)
2023-04-02 : Igor Pavlov : Public domain */







#if defined(MY_CPU_SIZEOF_POINTER) \
    && ( MY_CPU_SIZEOF_POINTER == 4 \
      || MY_CPU_SIZEOF_POINTER == 8)
  #define BR_CONV_USE_OPT_PC_PTR
#endif

#ifdef BR_CONV_USE_OPT_PC_PTR
#define BR_PC_INIT  pc -= (UInt32)(SizeT)p; /* (MY_uintptr_t) */
#define BR_PC_GET   (pc + (UInt32)(SizeT)p)
#else
#define BR_PC_INIT  pc += (UInt32)size;
#define BR_PC_GET   (pc - (UInt32)(SizeT)(lim - p))
/* #define BR_PC_INIT */
/* #define BR_PC_GET   (pc + (UInt32)(SizeT)(p - data)) */
#endif

#define BR_CONVERT_VAL(v, c) if (encoding) v += c; else v -= c;
/* #define BR_CONVERT_VAL(v, c) if (!encoding) c = (UInt32)0 - c; v += c; */

#define Z7_BRANCH_CONV_ST(name) z7_BranchConvSt_ ## name

#define BR86_NEED_CONV_FOR_MS_BYTE(b) ((((b) + 1) & 0xfe) == 0)

#ifdef MY_CPU_LE_UNALIGN
  #define BR86_PREPARE_BCJ_SCAN  const UInt32 v = GetUi32(p) ^ 0xe8e8e8e8;
  #define BR86_IS_BCJ_BYTE(n)    ((v & ((UInt32)0xfe << (n) * 8)) == 0)
#else
  #define BR86_PREPARE_BCJ_SCAN
  /* bad for MSVC X86 (partial write to byte reg): */
  #define BR86_IS_BCJ_BYTE(n)    ((p[n - 4] & 0xfe) == 0xe8)
  /* bad for old MSVC (partial write to byte reg): */
  /* #define BR86_IS_BCJ_BYTE(n)    (((*p ^ 0xe8) & 0xfe) == 0) */
#endif
 
static
Z7_FORCE_INLINE
Z7_ATTRIB_NO_VECTOR
Byte *Z7_BRANCH_CONV_ST(X86)(Byte *p, SizeT size, UInt32 pc, UInt32 *state, int encoding)
{
  if (size < 5)
    return p;
 {
  /* Byte *p = data; */
  const Byte *lim = p + size - 4;
  unsigned mask = (unsigned)*state;  /* & 7; */
#ifdef BR_CONV_USE_OPT_PC_PTR
  /* if BR_CONV_USE_OPT_PC_PTR is defined: we need to adjust (pc) for (+4),
        because call/jump offset is relative to the next instruction.
     if BR_CONV_USE_OPT_PC_PTR is not defined : we don't need to adjust (pc) for (+4),
         because  BR_PC_GET uses (pc - (lim - p)), and lim was adjusted for (-4) before.
  */
  pc += 4;
#endif
  BR_PC_INIT
  goto start;

  for (;; mask |= 4)
  {
    /* cont: mask |= 4; */
  start:
    if (p >= lim)
      goto fin;
    {
      BR86_PREPARE_BCJ_SCAN
      p += 4;
      if (BR86_IS_BCJ_BYTE(0))  { goto m0; }  mask >>= 1;
      if (BR86_IS_BCJ_BYTE(1))  { goto m1; }  mask >>= 1;
      if (BR86_IS_BCJ_BYTE(2))  { goto m2; }  mask = 0;
      if (BR86_IS_BCJ_BYTE(3))  { goto a3; }
    }
    goto main_loop;

  m0: p--;
  m1: p--;
  m2: p--;
    if (mask == 0)
      goto a3;
    if (p > lim)
      goto fin_p;
   
    /* if (((0x17u >> mask) & 1) == 0) */
    if (mask > 4 || mask == 3)
    {
      mask >>= 1;
      continue; /* goto cont; */
    }
    mask >>= 1;
    if (BR86_NEED_CONV_FOR_MS_BYTE(p[mask]))
      continue; /* goto cont; */
    /* if (!BR86_NEED_CONV_FOR_MS_BYTE(p[3])) continue; // goto cont; */
    {
      UInt32 v = GetUi32(p);
      UInt32 c;
      v += (1 << 24);  if (v & 0xfe000000) continue; /* goto cont; */
      c = BR_PC_GET;
      BR_CONVERT_VAL(v, c)
      {
        mask <<= 3;
        if (BR86_NEED_CONV_FOR_MS_BYTE(v >> mask))
        {
          v ^= (((UInt32)0x100 << mask) - 1);
          #ifdef MY_CPU_X86
          /* for X86 : we can recalculate (c) to reduce register pressure */
            c = BR_PC_GET;
          #endif
          BR_CONVERT_VAL(v, c)
        }
        mask = 0;
      }
      /* v = (v & ((1 << 24) - 1)) - (v & (1 << 24)); */
      v &= (1 << 25) - 1;  v -= (1 << 24);
      SetUi32(p, v)
      p += 4;
      goto main_loop;
    }

  main_loop:
    if (p >= lim)
      goto fin;
    for (;;)
    {
      BR86_PREPARE_BCJ_SCAN
      p += 4;
      if (BR86_IS_BCJ_BYTE(0))  { goto a0; }
      if (BR86_IS_BCJ_BYTE(1))  { goto a1; }
      if (BR86_IS_BCJ_BYTE(2))  { goto a2; }
      if (BR86_IS_BCJ_BYTE(3))  { goto a3; }
      if (p >= lim)
        goto fin;
    }
  
  a0: p--;
  a1: p--;
  a2: p--;
  a3:
    if (p > lim)
      goto fin_p;
    /* if (!BR86_NEED_CONV_FOR_MS_BYTE(p[3])) continue; // goto cont; */
    {
      UInt32 v = GetUi32(p);
      UInt32 c;
      v += (1 << 24);  if (v & 0xfe000000) continue; /* goto cont; */
      c = BR_PC_GET;
      BR_CONVERT_VAL(v, c)
      /* v = (v & ((1 << 24) - 1)) - (v & (1 << 24)); */
      v &= (1 << 25) - 1;  v -= (1 << 24);
      SetUi32(p, v)
      p += 4;
      goto main_loop;
    }
  }

fin_p:
  p--;
fin:
  /* the following processing for tail is optional and can be commented */
  /*
  lim += 4;
  for (; p < lim; p++, mask >>= 1)
    if ((*p & 0xfe) == 0xe8)
      break;
  */
  *state = (UInt32)mask;
  return p;
 }
}


#define Z7_BRANCH_CONV_ST_FUNC_IMP(name, m, encoding) \
Z7_NO_INLINE \
Z7_ATTRIB_NO_VECTOR \
Byte *m(name)(Byte *data, SizeT size, UInt32 pc, UInt32 *state) \
  { return Z7_BRANCH_CONV_ST(name)(data, size, pc, state, encoding); }

Z7_BRANCH_CONV_ST_FUNC_IMP(X86, Z7_BRANCH_CONV_ST_DEC, 0)
#ifndef Z7_EXTRACT_ONLY
Z7_BRANCH_CONV_ST_FUNC_IMP(X86, Z7_BRANCH_CONV_ST_ENC, 1)
#endif
#undef BR86_IS_BCJ_BYTE
#undef BR86_NEED_CONV_FOR_MS_BYTE
#undef BR86_PREPARE_BCJ_SCAN
#undef BR_CONVERT_VAL
#undef BR_CONV_USE_OPT_PC_PTR
#undef BR_PC_GET
#undef BR_PC_INIT
#undef Z7_BRANCH_CONV_ST
#undef Z7_BRANCH_CONV_ST_FUNC_IMP

/* ==== BraIA64.c ==== */
/* BraIA64.c -- Converter for IA-64 code
2023-02-20 : Igor Pavlov : Public domain */



/* the code was moved to Bra.c */

#ifdef _MSC_VER
#pragma warning(disable : 4206) /* nonstandard extension used : translation unit is empty */
#endif

#if defined(__clang__)
#pragma GCC diagnostic ignored "-Wempty-translation-unit"
#endif

/* ==== CpuArch.c ==== */
/* CpuArch.c -- CPU specific code
Igor Pavlov : Public domain */



/* #include <stdio.h> */



#ifdef MY_CPU_X86_OR_AMD64

#undef NEED_CHECK_FOR_CPUID
#if !defined(MY_CPU_AMD64)
#define NEED_CHECK_FOR_CPUID
#endif

/*
  cpuid instruction supports (subFunction) parameter in ECX,
  that is used only with some specific (function) parameter values.
  most functions use only (subFunction==0).
*/
/*
  __cpuid(): MSVC and GCC/CLANG use same function/macro name
             but parameters are different.
   We use MSVC __cpuid() parameters style for our z7_x86_cpuid() function.
*/

#if defined(__GNUC__) /* && (__GNUC__ >= 10) */ \
    || defined(__clang__) /* && (__clang_major__ >= 10) */

/* there was some CLANG/GCC compilers that have issues with
   rbx(ebx) handling in asm blocks in -fPIC mode (__PIC__ is defined).
   compiler's <cpuid.h> contains the macro __cpuid() that is similar to our code.
   The history of __cpuid() changes in CLANG/GCC:
   GCC:
     2007: it preserved ebx for (__PIC__ && __i386__)
     2013: it preserved rbx and ebx for __PIC__
     2014: it doesn't preserves rbx and ebx anymore
     we suppose that (__GNUC__ >= 5) fixed that __PIC__ ebx/rbx problem.
   CLANG:
     2014+: it preserves rbx, but only for 64-bit code. No __PIC__ check.
   Why CLANG cares about 64-bit mode only, and doesn't care about ebx (in 32-bit)?
   Do we need __PIC__ test for CLANG or we must care about rbx even if
   __PIC__ is not defined?
*/

#define ASM_LN "\n"
   
#if defined(MY_CPU_AMD64) && defined(__PIC__) \
    && ((defined (__GNUC__) && (__GNUC__ < 5)) || defined(__clang__))

  /* "=&r" selects free register. It can select even rbx, if that register is free.
     "=&D" for (RDI) also works, but the code can be larger with "=&D"
     "2"(subFun) : 2 is (zero-based) index in the output constraint list "=c" (ECX). */

#define x86_cpuid_MACRO_2(p, func, subFunc) { \
  __asm__ __volatile__ ( \
    ASM_LN   "mov     %%rbx, %q1"  \
    ASM_LN   "cpuid"               \
    ASM_LN   "xchg    %%rbx, %q1"  \
    : "=a" ((p)[0]), "=&r" ((p)[1]), "=c" ((p)[2]), "=d" ((p)[3]) : "0" (func), "2"(subFunc)); }

#elif defined(MY_CPU_X86) && defined(__PIC__) \
    && ((defined (__GNUC__) && (__GNUC__ < 5)) || defined(__clang__))

#define x86_cpuid_MACRO_2(p, func, subFunc) { \
  __asm__ __volatile__ ( \
    ASM_LN   "mov     %%ebx, %k1"  \
    ASM_LN   "cpuid"               \
    ASM_LN   "xchg    %%ebx, %k1"  \
    : "=a" ((p)[0]), "=&r" ((p)[1]), "=c" ((p)[2]), "=d" ((p)[3]) : "0" (func), "2"(subFunc)); }

#else

#define x86_cpuid_MACRO_2(p, func, subFunc) { \
  __asm__ __volatile__ ( \
    ASM_LN   "cpuid"               \
    : "=a" ((p)[0]), "=b" ((p)[1]), "=c" ((p)[2]), "=d" ((p)[3]) : "0" (func), "2"(subFunc)); }

#endif

#define x86_cpuid_MACRO(p, func)  x86_cpuid_MACRO_2(p, func, 0)

void Z7_FASTCALL z7_x86_cpuid(UInt32 p[4], UInt32 func)
{
  x86_cpuid_MACRO(p, func)
}

static
void Z7_FASTCALL z7_x86_cpuid_subFunc(UInt32 p[4], UInt32 func, UInt32 subFunc)
{
  x86_cpuid_MACRO_2(p, func, subFunc)
}


Z7_NO_INLINE
UInt32 Z7_FASTCALL z7_x86_cpuid_GetMaxFunc(void)
{
 #if defined(NEED_CHECK_FOR_CPUID)
  #define EFALGS_CPUID_BIT 21
  UInt32 a;
  __asm__ __volatile__ (
    ASM_LN   "pushf"
    ASM_LN   "pushf"
    ASM_LN   "pop     %0"
    /* ASM_LN   "movl    %0, %1" */
    /* ASM_LN   "xorl    $0x200000, %0" */
    ASM_LN   "btc     %1, %0"
    ASM_LN   "push    %0"
    ASM_LN   "popf"
    ASM_LN   "pushf"
    ASM_LN   "pop     %0"
    ASM_LN   "xorl    (%%esp), %0"

    ASM_LN   "popf"
    ASM_LN
    : "=&r" (a) /* "=a" */
    : "i" (EFALGS_CPUID_BIT)
    );
  if ((a & (1 << EFALGS_CPUID_BIT)) == 0)
    return 0;
 #endif
  {
    UInt32 p[4];
    x86_cpuid_MACRO(p, 0)
    return p[0];
  }
}

#undef ASM_LN

#elif !defined(_MSC_VER)

/*
// for gcc/clang and other: we can try to use __cpuid macro:
#include <cpuid.h>
void Z7_FASTCALL z7_x86_cpuid(UInt32 p[4], UInt32 func)
{
  __cpuid(func, p[0], p[1], p[2], p[3]);
}
UInt32 Z7_FASTCALL z7_x86_cpuid_GetMaxFunc(void)
{
  return (UInt32)__get_cpuid_max(0, NULL);
}
*/
/* for unsupported cpuid: */
void Z7_FASTCALL z7_x86_cpuid(UInt32 p[4], UInt32 func)
{
  UNUSED_VAR(func)
  p[0] = p[1] = p[2] = p[3] = 0;
}
UInt32 Z7_FASTCALL z7_x86_cpuid_GetMaxFunc(void)
{
  return 0;
}
/* amalgamated: upstream's no-cpuid branch forgets
 * z7_x86_cpuid_subFunc, which CPU_IsSupported_SHA512 below calls
 * unconditionally -- so on a compiler that is neither GNU nor
 * MSVC (tcc 0.9.27, which nob.c drives) every other unit links
 * and this one symbol does not. Zeros match the branch above it,
 * and GetMaxFunc returning 0 means SHA512 answers False before
 * this is ever reached; it exists to be defined, not called. */
static
void Z7_FASTCALL z7_x86_cpuid_subFunc(UInt32 p[4], UInt32 func, UInt32 subFunc)
{
  UNUSED_VAR(func)
  UNUSED_VAR(subFunc)
  p[0] = p[1] = p[2] = p[3] = 0;
}

#else /* _MSC_VER */

#if !defined(MY_CPU_AMD64)

UInt32 __declspec(naked) Z7_FASTCALL z7_x86_cpuid_GetMaxFunc(void)
{
  #if defined(NEED_CHECK_FOR_CPUID)
  #define EFALGS_CPUID_BIT 21
  __asm   pushfd
  __asm   pushfd
  /*
  __asm   pop     eax
  // __asm   mov     edx, eax
  __asm   btc     eax, EFALGS_CPUID_BIT
  __asm   push    eax
  */
  __asm   btc     dword ptr [esp], EFALGS_CPUID_BIT
  __asm   popfd
  __asm   pushfd
  __asm   pop     eax
  /* __asm   xor     eax, edx */
  __asm   xor     eax, [esp]
  /* __asm   push    edx */
  __asm   popfd
  __asm   and     eax, (1 shl EFALGS_CPUID_BIT)
  __asm   jz end_func
  #endif
  __asm   push    ebx
  __asm   xor     eax, eax    /* func */
  __asm   xor     ecx, ecx    /* subFunction (optional) for (func == 0) */
  __asm   cpuid
  __asm   pop     ebx
  #if defined(NEED_CHECK_FOR_CPUID)
  end_func:
  #endif
  __asm   ret 0
}

void __declspec(naked) Z7_FASTCALL z7_x86_cpuid(UInt32 p[4], UInt32 func)
{
  UNUSED_VAR(p)
  UNUSED_VAR(func)
  __asm   push    ebx
  __asm   push    edi
  __asm   mov     edi, ecx    /* p */
  __asm   mov     eax, edx    /* func */
  __asm   xor     ecx, ecx    /* subfunction (optional) for (func == 0) */
  __asm   cpuid
  __asm   mov     [edi     ], eax
  __asm   mov     [edi +  4], ebx
  __asm   mov     [edi +  8], ecx
  __asm   mov     [edi + 12], edx
  __asm   pop     edi
  __asm   pop     ebx
  __asm   ret     0
}

static
void __declspec(naked) Z7_FASTCALL z7_x86_cpuid_subFunc(UInt32 p[4], UInt32 func, UInt32 subFunc)
{
  UNUSED_VAR(p)
  UNUSED_VAR(func)
  UNUSED_VAR(subFunc)
  __asm   push    ebx
  __asm   push    edi
  __asm   mov     edi, ecx    /* p */
  __asm   mov     eax, edx    /* func */
  __asm   mov     ecx, [esp + 12]  /* subFunc */
  __asm   cpuid
  __asm   mov     [edi     ], eax
  __asm   mov     [edi +  4], ebx
  __asm   mov     [edi +  8], ecx
  __asm   mov     [edi + 12], edx
  __asm   pop     edi
  __asm   pop     ebx
  __asm   ret     4
}

#else /* MY_CPU_AMD64 */

    #if _MSC_VER >= 1600
      #include <intrin.h>
      #define MY_cpuidex  __cpuidex

static
void Z7_FASTCALL z7_x86_cpuid_subFunc(UInt32 p[4], UInt32 func, UInt32 subFunc)
{
  __cpuidex((int *)p, func, subFunc);
}

    #else
/*
 __cpuid (func == (0 or 7)) requires subfunction number in ECX.
  MSDN: The __cpuid intrinsic clears the ECX register before calling the cpuid instruction.
   __cpuid() in new MSVC clears ECX.
   __cpuid() in old MSVC (14.00) x64 doesn't clear ECX
 We still can use __cpuid for low (func) values that don't require ECX,
 but __cpuid() in old MSVC will be incorrect for some func values: (func == 7).
 So here we use the hack for old MSVC to send (subFunction) in ECX register to cpuid instruction,
 where ECX value is first parameter for FASTCALL / NO_INLINE func.
 So the caller of MY_cpuidex_HACK() sets ECX as subFunction, and
 old MSVC for __cpuid() doesn't change ECX and cpuid instruction gets (subFunction) value.
 
DON'T remove Z7_NO_INLINE and Z7_FASTCALL for MY_cpuidex_HACK(): !!!
*/
static
Z7_NO_INLINE void Z7_FASTCALL MY_cpuidex_HACK(Int32 subFunction, Int32 func, Int32 *CPUInfo)
{
  UNUSED_VAR(subFunction)
  __cpuid(CPUInfo, func);
}
      #define MY_cpuidex(info, func, func2)  MY_cpuidex_HACK(func2, func, info)
      #pragma message("======== MY_cpuidex_HACK WAS USED ========")
static
void Z7_FASTCALL z7_x86_cpuid_subFunc(UInt32 p[4], UInt32 func, UInt32 subFunc)
{
  MY_cpuidex_HACK(subFunc, func, (Int32 *)p);
}
    #endif /* _MSC_VER >= 1600 */

#if !defined(MY_CPU_AMD64)
/* inlining for __cpuid() in MSVC x86 (32-bit) produces big ineffective code,
   so we disable inlining here */
Z7_NO_INLINE
#endif
void Z7_FASTCALL z7_x86_cpuid(UInt32 p[4], UInt32 func)
{
  MY_cpuidex((Int32 *)p, (Int32)func, 0);
}

Z7_NO_INLINE
UInt32 Z7_FASTCALL z7_x86_cpuid_GetMaxFunc(void)
{
  Int32 a[4];
  MY_cpuidex(a, 0, 0);
  return a[0];
}

#endif /* MY_CPU_AMD64 */
#endif /* _MSC_VER */

#if defined(NEED_CHECK_FOR_CPUID)
#define CHECK_CPUID_IS_SUPPORTED { if (z7_x86_cpuid_GetMaxFunc() == 0) return 0; }
#else
#define CHECK_CPUID_IS_SUPPORTED
#endif
#undef NEED_CHECK_FOR_CPUID


static
BoolInt x86cpuid_Func_1(UInt32 *p)
{
  CHECK_CPUID_IS_SUPPORTED
  z7_x86_cpuid(p, 1);
  return True;
}

/*
static const UInt32 kVendors[][1] =
{
  { 0x756E6547 }, // , 0x49656E69, 0x6C65746E },
  { 0x68747541 }, // , 0x69746E65, 0x444D4163 },
  { 0x746E6543 }  // , 0x48727561, 0x736C7561 }
};
*/

/*
typedef struct
{
  UInt32 maxFunc;
  UInt32 vendor[3];
  UInt32 ver;
  UInt32 b;
  UInt32 c;
  UInt32 d;
} Cx86cpuid;

enum
{
  CPU_FIRM_INTEL,
  CPU_FIRM_AMD,
  CPU_FIRM_VIA
};
int x86cpuid_GetFirm(const Cx86cpuid *p);
#define x86cpuid_ver_GetFamily(ver) (((ver >> 16) & 0xff0) | ((ver >> 8) & 0xf))
#define x86cpuid_ver_GetModel(ver)  (((ver >> 12) &  0xf0) | ((ver >> 4) & 0xf))
#define x86cpuid_ver_GetStepping(ver) (ver & 0xf)

int x86cpuid_GetFirm(const Cx86cpuid *p)
{
  unsigned i;
  for (i = 0; i < sizeof(kVendors) / sizeof(kVendors[0]); i++)
  {
    const UInt32 *v = kVendors[i];
    if (v[0] == p->vendor[0]
        // && v[1] == p->vendor[1]
        // && v[2] == p->vendor[2]
        )
      return (int)i;
  }
  return -1;
}

BoolInt CPU_Is_InOrder()
{
  Cx86cpuid p;
  UInt32 family, model;
  if (!x86cpuid_CheckAndRead(&p))
    return True;

  family = x86cpuid_ver_GetFamily(p.ver);
  model = x86cpuid_ver_GetModel(p.ver);

  switch (x86cpuid_GetFirm(&p))
  {
    case CPU_FIRM_INTEL: return (family < 6 || (family == 6 && (
        // In-Order Atom CPU
           model == 0x1C  // 45 nm, N4xx, D4xx, N5xx, D5xx, 230, 330
        || model == 0x26  // 45 nm, Z6xx
        || model == 0x27  // 32 nm, Z2460
        || model == 0x35  // 32 nm, Z2760
        || model == 0x36  // 32 nm, N2xxx, D2xxx
        )));
    case CPU_FIRM_AMD: return (family < 5 || (family == 5 && (model < 6 || model == 0xA)));
    case CPU_FIRM_VIA: return (family < 6 || (family == 6 && model < 0xF));
  }
  return False; // v23 : unknown processors are not In-Order
}
*/

#ifdef _WIN32

#endif

#if !defined(MY_CPU_AMD64) && defined(_WIN32)

/* for legacy SSE ia32: there is no user-space cpu instruction to check
   that OS supports SSE register storing/restoring on context switches.
   So we need some OS-specific function to check that it's safe to use SSE registers.
*/

Z7_FORCE_INLINE
static BoolInt CPU_Sys_Is_SSE_Supported(void)
{
#ifdef _MSC_VER
  #pragma warning(push)
  #pragma warning(disable : 4996) /* `GetVersion': was declared deprecated */
#endif
  /* low byte is major version of Windows
     We suppose that any Windows version since
     Windows2000 (major == 5) supports SSE registers */
  return (Byte)GetVersion() >= 5;
#if defined(_MSC_VER)
  #pragma warning(pop)
#endif
}
#define CHECK_SYS_SSE_SUPPORT if (!CPU_Sys_Is_SSE_Supported()) return False;
#else
#define CHECK_SYS_SSE_SUPPORT
#endif


#if !defined(MY_CPU_AMD64)

BoolInt CPU_IsSupported_CMOV(void)
{
  UInt32 a[4];
  if (!x86cpuid_Func_1(&a[0]))
    return 0;
  return (BoolInt)(a[3] >> 15) & 1;
}

BoolInt CPU_IsSupported_SSE(void)
{
  UInt32 a[4];
  CHECK_SYS_SSE_SUPPORT
  if (!x86cpuid_Func_1(&a[0]))
    return 0;
  return (BoolInt)(a[3] >> 25) & 1;
}

BoolInt CPU_IsSupported_SSE2(void)
{
  UInt32 a[4];
  CHECK_SYS_SSE_SUPPORT
  if (!x86cpuid_Func_1(&a[0]))
    return 0;
  return (BoolInt)(a[3] >> 26) & 1;
}

#endif


static UInt32 x86cpuid_Func_1_ECX(void)
{
  UInt32 a[4];
  CHECK_SYS_SSE_SUPPORT
  if (!x86cpuid_Func_1(&a[0]))
    return 0;
  return a[2];
}

BoolInt CPU_IsSupported_AES(void)
{
  return (BoolInt)(x86cpuid_Func_1_ECX() >> 25) & 1;
}

BoolInt CPU_IsSupported_SSSE3(void)
{
  return (BoolInt)(x86cpuid_Func_1_ECX() >> 9) & 1;
}

BoolInt CPU_IsSupported_SSE41(void)
{
  return (BoolInt)(x86cpuid_Func_1_ECX() >> 19) & 1;
}

BoolInt CPU_IsSupported_SHA(void)
{
  CHECK_SYS_SSE_SUPPORT

  if (z7_x86_cpuid_GetMaxFunc() < 7)
    return False;
  {
    UInt32 d[4];
    z7_x86_cpuid(d, 7);
    return (BoolInt)(d[1] >> 29) & 1;
  }
}


BoolInt CPU_IsSupported_SHA512(void)
{
  if (!CPU_IsSupported_AVX2()) return False; /* maybe CPU_IsSupported_AVX() is enough here */

  if (z7_x86_cpuid_GetMaxFunc() < 7)
    return False;
  {
    UInt32 d[4];
    z7_x86_cpuid_subFunc(d, 7, 0);
    if (d[0] < 1) /* d[0] - is max supported subleaf value */
      return False;
    z7_x86_cpuid_subFunc(d, 7, 1);
    return (BoolInt)(d[0]) & 1;
  }
}

/*
MSVC: _xgetbv() intrinsic is available since VS2010SP1.
   MSVC also defines (_XCR_XFEATURE_ENABLED_MASK) macro in
   <immintrin.h> that we can use or check.
   For any 32-bit x86 we can use asm code in MSVC,
   but MSVC asm code is huge after compilation.
   So _xgetbv() is better

ICC: _xgetbv() intrinsic is available (in what version of ICC?)
   ICC defines (__GNUC___) and it supports gnu assembler
   also ICC supports MASM style code with -use-msasm switch.
   but ICC doesn't support __attribute__((__target__))

GCC/CLANG 9:
  _xgetbv() is macro that works via __builtin_ia32_xgetbv()
  and we need __attribute__((__target__("xsave")).
  But with __target__("xsave") the function will be not
  inlined to function that has no __target__("xsave") attribute.
  If we want _xgetbv() call inlining, then we should use asm version
  instead of calling _xgetbv().
  Note:intrinsic is broke before GCC 8.2:
    https://gcc.gnu.org/bugzilla/show_bug.cgi?id=85684
*/

#if    defined(__INTEL_COMPILER) && (__INTEL_COMPILER >= 1100) \
    || defined(_MSC_VER) && (_MSC_VER >= 1600) && (_MSC_FULL_VER >= 160040219)  \
    || defined(__GNUC__) && (__GNUC__ >= 9) \
    || defined(__clang__) && (__clang_major__ >= 9)
/* we define ATTRIB_XGETBV, if we want to use predefined _xgetbv() from compiler */
#if defined(__INTEL_COMPILER)
#define ATTRIB_XGETBV
#elif defined(__GNUC__) || defined(__clang__)
/* we don't define ATTRIB_XGETBV here, because asm version is better for inlining. */
/* #define ATTRIB_XGETBV __attribute__((__target__("xsave"))) */
#else
#define ATTRIB_XGETBV
#endif
#endif

#if defined(ATTRIB_XGETBV)
#include <immintrin.h>
#endif


/* XFEATURE_ENABLED_MASK/XCR0 */
#define MY_XCR_XFEATURE_ENABLED_MASK 0

#if defined(ATTRIB_XGETBV)
ATTRIB_XGETBV
#endif
static UInt64 x86_xgetbv_0(UInt32 num)
{
#if defined(ATTRIB_XGETBV)
  {
    return
      #if (defined(_MSC_VER))
        _xgetbv(num);
      #else
        __builtin_ia32_xgetbv(
          #if !defined(__clang__)
            (int)
          #endif
            num);
      #endif
  }

#elif defined(__GNUC__) || defined(__clang__) || defined(__SUNPRO_CC)

  UInt32 a, d;
 #if defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 4))
  __asm__
  (
    "xgetbv"
    : "=a"(a), "=d"(d) : "c"(num) : "cc"
  );
 #else /* is old gcc */
  __asm__
  (
    ".byte 0x0f, 0x01, 0xd0" "\n\t"
    : "=a"(a), "=d"(d) : "c"(num) : "cc"
  );
 #endif
  return ((UInt64)d << 32) | a;
  /* return a; */

#elif defined(_MSC_VER) && !defined(MY_CPU_AMD64)
  
  UInt32 a, d;
  __asm {
    push eax
    push edx
    push ecx
    mov ecx, num;
    /* xor ecx, ecx // = MY_XCR_XFEATURE_ENABLED_MASK */
    _emit 0x0f
    _emit 0x01
    _emit 0xd0
    mov a, eax
    mov d, edx
    pop ecx
    pop edx
    pop eax
  }
  return ((UInt64)d << 32) | a;
  /* return a; */

#else /* it's unknown compiler */
  /* #error "Need xgetbv function" */
  UNUSED_VAR(num)
  /* for MSVC-X64 we could call external function from external file. */
  /* Actually we had checked OSXSAVE/AVX in cpuid before.
     So it's expected that OS supports at least AVX and below. */
  /* if (num != MY_XCR_XFEATURE_ENABLED_MASK) return 0; // if not XCR0 */
  return
      /* (1 << 0) |  // x87 */
        (1 << 1)   /* SSE */
      | (1 << 2);  /* AVX */
  
#endif
}

#ifdef _WIN32
/*
  Windows versions do not know about new ISA extensions that
  can be introduced. But we still can use new extensions,
  even if Windows doesn't report about supporting them,
  But we can use new extensions, only if Windows knows about new ISA extension
  that changes the number or size of registers: SSE, AVX/XSAVE, AVX512
  So it's enough to check
    MY_PF_AVX_INSTRUCTIONS_AVAILABLE
      instead of
    MY_PF_AVX2_INSTRUCTIONS_AVAILABLE
*/
#define MY_PF_XSAVE_ENABLED                            17
/* #define MY_PF_SSSE3_INSTRUCTIONS_AVAILABLE             36 */
/* #define MY_PF_SSE4_1_INSTRUCTIONS_AVAILABLE            37 */
/* #define MY_PF_SSE4_2_INSTRUCTIONS_AVAILABLE            38 */
/* #define MY_PF_AVX_INSTRUCTIONS_AVAILABLE               39 */
/* #define MY_PF_AVX2_INSTRUCTIONS_AVAILABLE              40 */
/* #define MY_PF_AVX512F_INSTRUCTIONS_AVAILABLE           41 */
#endif

BoolInt CPU_IsSupported_AVX(void)
{
  #ifdef _WIN32
  if (!IsProcessorFeaturePresent(MY_PF_XSAVE_ENABLED))
    return False;
  /* PF_AVX_INSTRUCTIONS_AVAILABLE probably is supported starting from
     some latest Win10 revisions. But we need AVX in older Windows also.
     So we don't use the following check: */
  /*
  if (!IsProcessorFeaturePresent(MY_PF_AVX_INSTRUCTIONS_AVAILABLE))
    return False;
  */
  #endif

  /*
    OS must use new special XSAVE/XRSTOR instructions to save
    AVX registers when it required for context switching.
    At OS statring:
      OS sets CR4.OSXSAVE flag to signal the processor that OS supports the XSAVE extensions.
      Also OS sets bitmask in XCR0 register that defines what
      registers will be processed by XSAVE instruction:
        XCR0.SSE[bit 0] - x87 registers and state
        XCR0.SSE[bit 1] - SSE registers and state
        XCR0.AVX[bit 2] - AVX registers and state
    CR4.OSXSAVE is reflected to CPUID.1:ECX.OSXSAVE[bit 27].
       So we can read that bit in user-space.
    XCR0 is available for reading in user-space by new XGETBV instruction.
  */
  {
    const UInt32 c = x86cpuid_Func_1_ECX();
    if (0 == (1
        & (c >> 28)   /* AVX instructions are supported by hardware */
        & (c >> 27))) /* OSXSAVE bit: XSAVE and related instructions are enabled by OS. */
      return False;
  }

  /* also we can check
     CPUID.1:ECX.XSAVE [bit 26] : that shows that
        XSAVE, XRESTOR, XSETBV, XGETBV instructions are supported by hardware.
     But that check is redundant, because if OSXSAVE bit is set, then XSAVE is also set */

  /* If OS have enabled XSAVE extension instructions (OSXSAVE == 1),
     in most cases we expect that OS also will support storing/restoring
     for AVX and SSE states at least.
     But to be ensure for that we call user-space instruction
     XGETBV(0) to get XCR0 value that contains bitmask that defines
     what exact states(registers) OS have enabled for storing/restoring.
  */

  {
    const UInt32 bm = (UInt32)x86_xgetbv_0(MY_XCR_XFEATURE_ENABLED_MASK);
    /* printf("\n=== XGetBV=0x%x\n", bm); */
    return 1
        & (BoolInt)(bm >> 1)  /* SSE state is supported (set by OS) for storing/restoring */
        & (BoolInt)(bm >> 2); /* AVX state is supported (set by OS) for storing/restoring */
  }
  /* since Win7SP1: we can use GetEnabledXStateFeatures(); */
}


BoolInt CPU_IsSupported_AVX2(void)
{
  if (!CPU_IsSupported_AVX())
    return False;
  if (z7_x86_cpuid_GetMaxFunc() < 7)
    return False;
  {
    UInt32 d[4];
    z7_x86_cpuid(d, 7);
    /* printf("\ncpuid(7): ebx=%8x ecx=%8x\n", d[1], d[2]); */
    return 1
      & (BoolInt)(d[1] >> 5); /* avx2 */
  }
}

#if 0
BoolInt CPU_IsSupported_AVX512F_AVX512VL(void)
{
  if (!CPU_IsSupported_AVX())
    return False;
  if (z7_x86_cpuid_GetMaxFunc() < 7)
    return False;
  {
    UInt32 d[4];
    BoolInt v;
    z7_x86_cpuid(d, 7);
    /* printf("\ncpuid(7): ebx=%8x ecx=%8x\n", d[1], d[2]); */
    v = 1
      & (BoolInt)(d[1] >> 16)  /* avx512f */
      & (BoolInt)(d[1] >> 31); /* avx512vl */
    if (!v)
      return False;
  }
  {
    const UInt32 bm = (UInt32)x86_xgetbv_0(MY_XCR_XFEATURE_ENABLED_MASK);
    /* printf("\n=== XGetBV=0x%x\n", bm); */
    return 1
        & (BoolInt)(bm >> 5)  /* OPMASK */
        & (BoolInt)(bm >> 6)  /* ZMM upper 256-bit */
        & (BoolInt)(bm >> 7); /* ZMM16 ... ZMM31 */
  }
}
#endif

BoolInt CPU_IsSupported_VAES_AVX2(void)
{
  if (!CPU_IsSupported_AVX())
    return False;
  if (z7_x86_cpuid_GetMaxFunc() < 7)
    return False;
  {
    UInt32 d[4];
    z7_x86_cpuid(d, 7);
    /* printf("\ncpuid(7): ebx=%8x ecx=%8x\n", d[1], d[2]); */
    return 1
      & (BoolInt)(d[1] >> 5) /* avx2 */
      /* & (d[1] >> 31) // avx512vl */
      & (BoolInt)(d[2] >> 9); /* vaes // VEX-256/EVEX */
  }
}

BoolInt CPU_IsSupported_PageGB(void)
{
  CHECK_CPUID_IS_SUPPORTED
  {
    UInt32 d[4];
    z7_x86_cpuid(d, 0x80000000);
    if (d[0] < 0x80000001)
      return False;
    z7_x86_cpuid(d, 0x80000001);
    return (BoolInt)(d[3] >> 26) & 1;
  }
}


#elif defined(MY_CPU_ARM_OR_ARM64)

#ifdef _WIN32



BoolInt CPU_IsSupported_CRC32(void)  { return IsProcessorFeaturePresent(PF_ARM_V8_CRC32_INSTRUCTIONS_AVAILABLE) ? 1 : 0; }
BoolInt CPU_IsSupported_CRYPTO(void) { return IsProcessorFeaturePresent(PF_ARM_V8_CRYPTO_INSTRUCTIONS_AVAILABLE) ? 1 : 0; }
BoolInt CPU_IsSupported_NEON(void)   { return IsProcessorFeaturePresent(PF_ARM_NEON_INSTRUCTIONS_AVAILABLE) ? 1 : 0; }

#else

#if defined(__APPLE__)

/*
#include <stdio.h>
#include <string.h>
static void Print_sysctlbyname(const char *name)
{
  size_t bufSize = 256;
  char buf[256];
  int res = sysctlbyname(name, &buf, &bufSize, NULL, 0);
  {
    int i;
    printf("\nres = %d : %s : '%s' : bufSize = %d, numeric", res, name, buf, (unsigned)bufSize);
    for (i = 0; i < 20; i++)
      printf(" %2x", (unsigned)(Byte)buf[i]);

  }
}
*/
/*
  Print_sysctlbyname("hw.pagesize");
  Print_sysctlbyname("machdep.cpu.brand_string");
*/

static BoolInt z7_sysctlbyname_Get_BoolInt(const char *name)
{
  UInt32 val = 0;
  if (z7_sysctlbyname_Get_UInt32(name, &val) == 0 && val == 1)
    return 1;
  return 0;
}

BoolInt CPU_IsSupported_CRC32(void)
{
  return z7_sysctlbyname_Get_BoolInt("hw.optional.armv8_crc32");
}

BoolInt CPU_IsSupported_NEON(void)
{
  return z7_sysctlbyname_Get_BoolInt("hw.optional.neon");
}

BoolInt CPU_IsSupported_SHA512(void)
{
  return z7_sysctlbyname_Get_BoolInt("hw.optional.armv8_2_sha512");
}

/*
BoolInt CPU_IsSupported_SHA3(void)
{
  return z7_sysctlbyname_Get_BoolInt("hw.optional.armv8_2_sha3");
}
*/

#ifdef MY_CPU_ARM64
#define APPLE_CRYPTO_SUPPORT_VAL 1
#else
#define APPLE_CRYPTO_SUPPORT_VAL 0
#endif

BoolInt CPU_IsSupported_SHA1(void) { return APPLE_CRYPTO_SUPPORT_VAL; }
BoolInt CPU_IsSupported_SHA2(void) { return APPLE_CRYPTO_SUPPORT_VAL; }
BoolInt CPU_IsSupported_AES (void) { return APPLE_CRYPTO_SUPPORT_VAL; }


#else /* __APPLE__ */

#if defined(__GLIBC__) && (__GLIBC__ * 100 + __GLIBC_MINOR__ >= 216)
  #define Z7_GETAUXV_AVAILABLE
#else
/* #pragma message("=== is not NEW GLIBC === ") */
  #if defined __has_include
  #if __has_include (<sys/auxv.h>)
/* #pragma message("=== sys/auxv.h is avail=== ") */
    #define Z7_GETAUXV_AVAILABLE
  #endif
  #endif
#endif

#ifdef Z7_GETAUXV_AVAILABLE
/* #pragma message("=== Z7_GETAUXV_AVAILABLE === ") */
#include <sys/auxv.h>
#define USE_HWCAP
#endif

#ifdef USE_HWCAP

#if defined(__FreeBSD__)
static unsigned long MY_getauxval(int aux)
{
  unsigned long val;
  if (elf_aux_info(aux, &val, sizeof(val)))
    return 0;
  return val;
}
#else
#define MY_getauxval  getauxval
  #if defined __has_include
  #if __has_include (<asm/hwcap.h>)
#include <asm/hwcap.h>
  #endif
  #endif
#endif

  #define MY_HWCAP_CHECK_FUNC_2(name1, name2) \
  BoolInt CPU_IsSupported_ ## name1(void) { return (MY_getauxval(AT_HWCAP)  & (HWCAP_  ## name2)); }

#ifdef MY_CPU_ARM64
  #define MY_HWCAP_CHECK_FUNC(name) \
  MY_HWCAP_CHECK_FUNC_2(name, name)
#if 1 || defined(__ARM_NEON)
  BoolInt CPU_IsSupported_NEON(void) { return True; }
#else
  MY_HWCAP_CHECK_FUNC_2(NEON, ASIMD)
#endif
/* MY_HWCAP_CHECK_FUNC (ASIMD) */
#elif defined(MY_CPU_ARM)
  #define MY_HWCAP_CHECK_FUNC(name) \
  BoolInt CPU_IsSupported_ ## name(void) { return (MY_getauxval(AT_HWCAP2) & (HWCAP2_ ## name)); }
  MY_HWCAP_CHECK_FUNC_2(NEON, NEON)
#endif

#else /* USE_HWCAP */

  #define MY_HWCAP_CHECK_FUNC(name) \
  BoolInt CPU_IsSupported_ ## name(void) { return 0; }
#if defined(__ARM_NEON)
  BoolInt CPU_IsSupported_NEON(void) { return True; }
#else
  MY_HWCAP_CHECK_FUNC(NEON)
#endif

#endif /* USE_HWCAP */

MY_HWCAP_CHECK_FUNC (CRC32)
MY_HWCAP_CHECK_FUNC (SHA1)
MY_HWCAP_CHECK_FUNC (SHA2)
MY_HWCAP_CHECK_FUNC (AES)
#ifdef MY_CPU_ARM64
/* <hwcap.h> supports HWCAP_SHA512 and HWCAP_SHA3 since 2017. */
/* we define them here, if they are not defined */
#ifndef HWCAP_SHA3
/* #define HWCAP_SHA3    (1 << 17) */
#endif
#ifndef HWCAP_SHA512
/* #pragma message("=== HWCAP_SHA512 define === ") */
#define HWCAP_SHA512  (1 << 21)
#endif
MY_HWCAP_CHECK_FUNC (SHA512)
/* MY_HWCAP_CHECK_FUNC (SHA3) */
#endif

#endif /* __APPLE__ */
#endif /* _WIN32 */

#endif /* MY_CPU_ARM_OR_ARM64 */



#ifdef __APPLE__

#include <sys/sysctl.h>

int z7_sysctlbyname_Get(const char *name, void *buf, size_t *bufSize)
{
  return sysctlbyname(name, buf, bufSize, NULL, 0);
}

int z7_sysctlbyname_Get_UInt32(const char *name, UInt32 *val)
{
  size_t bufSize = sizeof(*val);
  const int res = z7_sysctlbyname_Get(name, val, &bufSize);
  if (res == 0 && bufSize != sizeof(*val))
    return EFAULT;
  return res;
}

#endif
#undef APPLE_CRYPTO_SUPPORT_VAL
#undef ASM_LN
#undef ATTRIB_XGETBV
#undef CHECK_CPUID_IS_SUPPORTED
#undef CHECK_SYS_SSE_SUPPORT
#undef EFALGS_CPUID_BIT
#undef HWCAP_SHA512
#undef MY_HWCAP_CHECK_FUNC
#undef MY_HWCAP_CHECK_FUNC_2
#undef MY_PF_XSAVE_ENABLED
#undef MY_XCR_XFEATURE_ENABLED_MASK
#undef MY_cpuidex
#undef MY_getauxval
#undef NEED_CHECK_FOR_CPUID
#undef USE_HWCAP
#undef Z7_GETAUXV_AVAILABLE
#undef x86_cpuid_MACRO
#undef x86_cpuid_MACRO_2
#undef x86cpuid_ver_GetFamily
#undef x86cpuid_ver_GetModel
#undef x86cpuid_ver_GetStepping

/* ==== Delta.c ==== */
/* Delta.c -- Delta converter
2021-02-09 : Igor Pavlov : Public domain */





void Delta_Init(Byte *state)
{
  unsigned i;
  for (i = 0; i < DELTA_STATE_SIZE; i++)
    state[i] = 0;
}


void Delta_Encode(Byte *state, unsigned delta, Byte *data, SizeT size)
{
  Byte temp[DELTA_STATE_SIZE];

  if (size == 0)
    return;

  {
    unsigned i = 0;
    do
      temp[i] = state[i];
    while (++i != delta);
  }

  if (size <= delta)
  {
    unsigned i = 0, k;
    do
    {
      Byte b = *data;
      *data++ = (Byte)(b - temp[i]);
      temp[i] = b;
    }
    while (++i != size);
    
    k = 0;
    
    do
    {
      if (i == delta)
        i = 0;
      state[k] = temp[i++];
    }
    while (++k != delta);
    
    return;
  }
    
  {
    Byte *p = data + size - delta;
    {
      unsigned i = 0;
      do
        state[i] = *p++;
      while (++i != delta);
    }
    {
      const Byte *lim = data + delta;
      ptrdiff_t dif = -(ptrdiff_t)delta;
      
      if (((ptrdiff_t)size + dif) & 1)
      {
        --p;  *p = (Byte)(*p - p[dif]);
      }

      while (p != lim)
      {
        --p;  *p = (Byte)(*p - p[dif]);
        --p;  *p = (Byte)(*p - p[dif]);
      }
      
      dif = -dif;
      
      do
      {
        --p;  *p = (Byte)(*p - temp[--dif]);
      }
      while (dif != 0);
    }
  }
}


void Delta_Decode(Byte *state, unsigned delta, Byte *data, SizeT size)
{
  unsigned i;
  const Byte *lim;

  if (size == 0)
    return;
  
  i = 0;
  lim = data + size;
  
  if (size <= delta)
  {
    do
      *data = (Byte)(*data + state[i++]);
    while (++data != lim);

    for (; delta != i; state++, delta--)
      *state = state[i];
    data -= i;
  }
  else
  {
    /*
    #define B(n) b ## n
    #define I(n) Byte B(n) = state[n];
    #define U(n) { B(n) = (Byte)((B(n)) + *data++); data[-1] = (B(n)); }
    #define F(n) if (data != lim) { U(n) }

    if (delta == 1)
    {
      I(0)
      if ((lim - data) & 1) { U(0) }
      while (data != lim) { U(0) U(0) }
      data -= 1;
    }
    else if (delta == 2)
    {
      I(0) I(1)
      lim -= 1; while (data < lim) { U(0) U(1) }
      lim += 1; F(0)
      data -= 2;
    }
    else if (delta == 3)
    {
      I(0) I(1) I(2)
      lim -= 2; while (data < lim) { U(0) U(1) U(2) }
      lim += 2; F(0) F(1)
      data -= 3;
    }
    else if (delta == 4)
    {
      I(0) I(1) I(2) I(3)
      lim -= 3; while (data < lim) { U(0) U(1) U(2) U(3) }
      lim += 3; F(0) F(1) F(2)
      data -= 4;
    }
    else
    */
    {
      do
      {
        *data = (Byte)(*data + state[i++]);
        data++;
      }
      while (i != delta);
  
      {
        ptrdiff_t dif = -(ptrdiff_t)delta;
        do
          *data = (Byte)(*data + data[dif]);
        while (++data != lim);
        data += dif;
      }
    }
  }

  do
    *state++ = *data;
  while (++data != lim);
}
#undef B
#undef F
#undef I
#undef U

/* ==== Lzma2Dec.c ==== */
/* Lzma2Dec.c -- LZMA2 Decoder
2024-03-01 : Igor Pavlov : Public domain */

/* #define SHOW_DEBUG_INFO */



#ifdef SHOW_DEBUG_INFO
#include <stdio.h>
#endif

#include <string.h>



/*
00000000  -  End of data
00000001 U U  -  Uncompressed, reset dic, need reset state and set new prop
00000010 U U  -  Uncompressed, no reset
100uuuuu U U P P  -  LZMA, no reset
101uuuuu U U P P  -  LZMA, reset state
110uuuuu U U P P S  -  LZMA, reset state + set new prop
111uuuuu U U P P S  -  LZMA, reset state + set new prop, reset dic

  u, U - Unpack Size
  P - Pack Size
  S - Props
*/

#define LZMA2_CONTROL_COPY_RESET_DIC 1

#define LZMA2_IS_UNCOMPRESSED_STATE(p) (((p)->control & (1 << 7)) == 0)

#define LZMA2_LCLP_MAX 4
#define LZMA2_DIC_SIZE_FROM_PROP(p) (((UInt32)2 | ((p) & 1)) << ((p) / 2 + 11))

#ifdef SHOW_DEBUG_INFO
#define PRF(x) x
#else
#define PRF(x)
#endif

typedef enum
{
  LZMA2_STATE_CONTROL,
  LZMA2_STATE_UNPACK0,
  LZMA2_STATE_UNPACK1,
  LZMA2_STATE_PACK0,
  LZMA2_STATE_PACK1,
  LZMA2_STATE_PROP,
  LZMA2_STATE_DATA,
  LZMA2_STATE_DATA_CONT,
  LZMA2_STATE_FINISHED,
  LZMA2_STATE_ERROR
} ELzma2State;

static SRes Lzma2Dec_GetOldProps(Byte prop, Byte *props)
{
  UInt32 dicSize;
  if (prop > 40)
    return SZ_ERROR_UNSUPPORTED;
  dicSize = (prop == 40) ? 0xFFFFFFFF : LZMA2_DIC_SIZE_FROM_PROP(prop);
  props[0] = (Byte)LZMA2_LCLP_MAX;
  props[1] = (Byte)(dicSize);
  props[2] = (Byte)(dicSize >> 8);
  props[3] = (Byte)(dicSize >> 16);
  props[4] = (Byte)(dicSize >> 24);
  return SZ_OK;
}

SRes Lzma2Dec_AllocateProbs(CLzma2Dec *p, Byte prop, ISzAllocPtr alloc)
{
  Byte props[LZMA_PROPS_SIZE];
  RINOK(Lzma2Dec_GetOldProps(prop, props))
  return LzmaDec_AllocateProbs(&p->decoder, props, LZMA_PROPS_SIZE, alloc);
}

SRes Lzma2Dec_Allocate(CLzma2Dec *p, Byte prop, ISzAllocPtr alloc)
{
  Byte props[LZMA_PROPS_SIZE];
  RINOK(Lzma2Dec_GetOldProps(prop, props))
  return LzmaDec_Allocate(&p->decoder, props, LZMA_PROPS_SIZE, alloc);
}

void Lzma2Dec_Init(CLzma2Dec *p)
{
  p->state = LZMA2_STATE_CONTROL;
  p->needInitLevel = 0xE0;
  p->isExtraMode = False;
  p->unpackSize = 0;
  
  /* p->decoder.dicPos = 0; // we can use it instead of full init */
  LzmaDec_Init(&p->decoder);
}

/* ELzma2State */
static unsigned Lzma2Dec_UpdateState(CLzma2Dec *p, Byte b)
{
  switch (p->state)
  {
    case LZMA2_STATE_CONTROL:
      p->isExtraMode = False;
      p->control = b;
      PRF(printf("\n %8X", (unsigned)p->decoder.dicPos));
      PRF(printf(" %02X", (unsigned)b));
      if (b == 0)
        return LZMA2_STATE_FINISHED;
      if (LZMA2_IS_UNCOMPRESSED_STATE(p))
      {
        if (b == LZMA2_CONTROL_COPY_RESET_DIC)
          p->needInitLevel = 0xC0;
        else if (b > 2 || p->needInitLevel == 0xE0)
          return LZMA2_STATE_ERROR;
      }
      else
      {
        if (b < p->needInitLevel)
          return LZMA2_STATE_ERROR;
        p->needInitLevel = 0;
        p->unpackSize = (UInt32)(b & 0x1F) << 16;
      }
      return LZMA2_STATE_UNPACK0;
    
    case LZMA2_STATE_UNPACK0:
      p->unpackSize |= (UInt32)b << 8;
      return LZMA2_STATE_UNPACK1;
    
    case LZMA2_STATE_UNPACK1:
      p->unpackSize |= (UInt32)b;
      p->unpackSize++;
      PRF(printf(" %7u", (unsigned)p->unpackSize));
      return LZMA2_IS_UNCOMPRESSED_STATE(p) ? LZMA2_STATE_DATA : LZMA2_STATE_PACK0;
    
    case LZMA2_STATE_PACK0:
      p->packSize = (UInt32)b << 8;
      return LZMA2_STATE_PACK1;

    case LZMA2_STATE_PACK1:
      p->packSize |= (UInt32)b;
      p->packSize++;
      /* if (p->packSize < 5) return LZMA2_STATE_ERROR; */
      PRF(printf(" %5u", (unsigned)p->packSize));
      return (p->control & 0x40) ? LZMA2_STATE_PROP : LZMA2_STATE_DATA;

    case LZMA2_STATE_PROP:
    {
      unsigned lc, lp;
      if (b >= (9 * 5 * 5))
        return LZMA2_STATE_ERROR;
      lc = b % 9;
      b /= 9;
      p->decoder.prop.pb = (Byte)(b / 5);
      lp = b % 5;
      if (lc + lp > LZMA2_LCLP_MAX)
        return LZMA2_STATE_ERROR;
      p->decoder.prop.lc = (Byte)lc;
      p->decoder.prop.lp = (Byte)lp;
      return LZMA2_STATE_DATA;
    }
    
    default:
      return LZMA2_STATE_ERROR;
  }
}

static void LzmaDec_UpdateWithUncompressed(CLzmaDec *p, const Byte *src, SizeT size)
{
  memcpy(p->dic + p->dicPos, src, size);
  p->dicPos += size;
  if (p->checkDicSize == 0 && p->prop.dicSize - p->processedPos <= size)
    p->checkDicSize = p->prop.dicSize;
  p->processedPos += (UInt32)size;
}

void LzmaDec_InitDicAndState(CLzmaDec *p, BoolInt initDic, BoolInt initState);


SRes Lzma2Dec_DecodeToDic(CLzma2Dec *p, SizeT dicLimit,
    const Byte *src, SizeT *srcLen, ELzmaFinishMode finishMode, ELzmaStatus *status)
{
  SizeT inSize = *srcLen;
  *srcLen = 0;
  *status = LZMA_STATUS_NOT_SPECIFIED;

  while (p->state != LZMA2_STATE_ERROR)
  {
    SizeT dicPos;

    if (p->state == LZMA2_STATE_FINISHED)
    {
      *status = LZMA_STATUS_FINISHED_WITH_MARK;
      return SZ_OK;
    }
    
    dicPos = p->decoder.dicPos;
    
    if (dicPos == dicLimit && finishMode == LZMA_FINISH_ANY)
    {
      *status = LZMA_STATUS_NOT_FINISHED;
      return SZ_OK;
    }

    if (p->state != LZMA2_STATE_DATA && p->state != LZMA2_STATE_DATA_CONT)
    {
      if (*srcLen == inSize)
      {
        *status = LZMA_STATUS_NEEDS_MORE_INPUT;
        return SZ_OK;
      }
      (*srcLen)++;
      p->state = Lzma2Dec_UpdateState(p, *src++);
      if (dicPos == dicLimit && p->state != LZMA2_STATE_FINISHED)
        break;
      continue;
    }
    
    {
      SizeT inCur = inSize - *srcLen;
      SizeT outCur = dicLimit - dicPos;
      ELzmaFinishMode curFinishMode = LZMA_FINISH_ANY;
      
      if (outCur >= p->unpackSize)
      {
        outCur = (SizeT)p->unpackSize;
        curFinishMode = LZMA_FINISH_END;
      }

      if (LZMA2_IS_UNCOMPRESSED_STATE(p))
      {
        if (inCur == 0)
        {
          *status = LZMA_STATUS_NEEDS_MORE_INPUT;
          return SZ_OK;
        }

        if (p->state == LZMA2_STATE_DATA)
        {
          BoolInt initDic = (p->control == LZMA2_CONTROL_COPY_RESET_DIC);
          LzmaDec_InitDicAndState(&p->decoder, initDic, False);
        }

        if (inCur > outCur)
          inCur = outCur;
        if (inCur == 0)
          break;

        LzmaDec_UpdateWithUncompressed(&p->decoder, src, inCur);

        src += inCur;
        *srcLen += inCur;
        p->unpackSize -= (UInt32)inCur;
        p->state = (p->unpackSize == 0) ? LZMA2_STATE_CONTROL : LZMA2_STATE_DATA_CONT;
      }
      else
      {
        SRes res;

        if (p->state == LZMA2_STATE_DATA)
        {
          BoolInt initDic = (p->control >= 0xE0);
          BoolInt initState = (p->control >= 0xA0);
          LzmaDec_InitDicAndState(&p->decoder, initDic, initState);
          p->state = LZMA2_STATE_DATA_CONT;
        }
  
        if (inCur > p->packSize)
          inCur = (SizeT)p->packSize;
        
        res = LzmaDec_DecodeToDic(&p->decoder, dicPos + outCur, src, &inCur, curFinishMode, status);

        src += inCur;
        *srcLen += inCur;
        p->packSize -= (UInt32)inCur;
        outCur = p->decoder.dicPos - dicPos;
        p->unpackSize -= (UInt32)outCur;

        if (res != 0)
          break;
        
        if (*status == LZMA_STATUS_NEEDS_MORE_INPUT)
        {
          if (p->packSize == 0)
            break;
          return SZ_OK;
        }

        if (inCur == 0 && outCur == 0)
        {
          if (*status != LZMA_STATUS_MAYBE_FINISHED_WITHOUT_MARK
              || p->unpackSize != 0
              || p->packSize != 0)
            break;
          p->state = LZMA2_STATE_CONTROL;
        }
        
        *status = LZMA_STATUS_NOT_SPECIFIED;
      }
    }
  }
  
  *status = LZMA_STATUS_NOT_SPECIFIED;
  p->state = LZMA2_STATE_ERROR;
  return SZ_ERROR_DATA;
}




ELzma2ParseStatus Lzma2Dec_Parse(CLzma2Dec *p,
    SizeT outSize,
    const Byte *src, SizeT *srcLen,
    int checkFinishBlock)
{
  SizeT inSize = *srcLen;
  *srcLen = 0;

  while (p->state != LZMA2_STATE_ERROR)
  {
    if (p->state == LZMA2_STATE_FINISHED)
      return (ELzma2ParseStatus)LZMA_STATUS_FINISHED_WITH_MARK;

    if (outSize == 0 && !checkFinishBlock)
      return (ELzma2ParseStatus)LZMA_STATUS_NOT_FINISHED;
    
    if (p->state != LZMA2_STATE_DATA && p->state != LZMA2_STATE_DATA_CONT)
    {
      if (*srcLen == inSize)
        return (ELzma2ParseStatus)LZMA_STATUS_NEEDS_MORE_INPUT;
      (*srcLen)++;

      p->state = Lzma2Dec_UpdateState(p, *src++);

      if (p->state == LZMA2_STATE_UNPACK0)
      {
        /* if (p->decoder.dicPos != 0) */
        if (p->control == LZMA2_CONTROL_COPY_RESET_DIC || p->control >= 0xE0)
          return LZMA2_PARSE_STATUS_NEW_BLOCK;
        /* if (outSize == 0) return LZMA_STATUS_NOT_FINISHED; */
      }

      /* The following code can be commented. */
      /* It's not big problem, if we read additional input bytes. */
      /* It will be stopped later in LZMA2_STATE_DATA / LZMA2_STATE_DATA_CONT state. */

      if (outSize == 0 && p->state != LZMA2_STATE_FINISHED)
      {
        /* checkFinishBlock is true. So we expect that block must be finished, */
        /* We can return LZMA_STATUS_NOT_SPECIFIED or LZMA_STATUS_NOT_FINISHED here */
        /* break; */
        return (ELzma2ParseStatus)LZMA_STATUS_NOT_FINISHED;
      }

      if (p->state == LZMA2_STATE_DATA)
        return LZMA2_PARSE_STATUS_NEW_CHUNK;

      continue;
    }

    if (outSize == 0)
      return (ELzma2ParseStatus)LZMA_STATUS_NOT_FINISHED;

    {
      SizeT inCur = inSize - *srcLen;

      if (LZMA2_IS_UNCOMPRESSED_STATE(p))
      {
        if (inCur == 0)
          return (ELzma2ParseStatus)LZMA_STATUS_NEEDS_MORE_INPUT;
        if (inCur > p->unpackSize)
          inCur = p->unpackSize;
        if (inCur > outSize)
          inCur = outSize;
        p->decoder.dicPos += inCur;
        src += inCur;
        *srcLen += inCur;
        outSize -= inCur;
        p->unpackSize -= (UInt32)inCur;
        p->state = (p->unpackSize == 0) ? LZMA2_STATE_CONTROL : LZMA2_STATE_DATA_CONT;
      }
      else
      {
        p->isExtraMode = True;

        if (inCur == 0)
        {
          if (p->packSize != 0)
            return (ELzma2ParseStatus)LZMA_STATUS_NEEDS_MORE_INPUT;
        }
        else if (p->state == LZMA2_STATE_DATA)
        {
          p->state = LZMA2_STATE_DATA_CONT;
          if (*src != 0)
          {
            /* first byte of lzma chunk must be Zero */
            *srcLen += 1;
            p->packSize--;
            break;
          }
        }
  
        if (inCur > p->packSize)
          inCur = (SizeT)p->packSize;

        src += inCur;
        *srcLen += inCur;
        p->packSize -= (UInt32)inCur;

        if (p->packSize == 0)
        {
          SizeT rem = outSize;
          if (rem > p->unpackSize)
            rem = p->unpackSize;
          p->decoder.dicPos += rem;
          p->unpackSize -= (UInt32)rem;
          outSize -= rem;
          if (p->unpackSize == 0)
            p->state = LZMA2_STATE_CONTROL;
        }
      }
    }
  }
  
  p->state = LZMA2_STATE_ERROR;
  return (ELzma2ParseStatus)LZMA_STATUS_NOT_SPECIFIED;
}




SRes Lzma2Dec_DecodeToBuf(CLzma2Dec *p, Byte *dest, SizeT *destLen, const Byte *src, SizeT *srcLen, ELzmaFinishMode finishMode, ELzmaStatus *status)
{
  SizeT outSize = *destLen, inSize = *srcLen;
  *srcLen = *destLen = 0;
  
  for (;;)
  {
    SizeT inCur = inSize, outCur, dicPos;
    ELzmaFinishMode curFinishMode;
    SRes res;
    
    if (p->decoder.dicPos == p->decoder.dicBufSize)
      p->decoder.dicPos = 0;
    dicPos = p->decoder.dicPos;
    curFinishMode = LZMA_FINISH_ANY;
    outCur = p->decoder.dicBufSize - dicPos;
    
    if (outCur >= outSize)
    {
      outCur = outSize;
      curFinishMode = finishMode;
    }

    res = Lzma2Dec_DecodeToDic(p, dicPos + outCur, src, &inCur, curFinishMode, status);
    
    src += inCur;
    inSize -= inCur;
    *srcLen += inCur;
    outCur = p->decoder.dicPos - dicPos;
    memcpy(dest, p->decoder.dic + dicPos, outCur);
    dest += outCur;
    outSize -= outCur;
    *destLen += outCur;
    if (res != 0)
      return res;
    if (outCur == 0 || outSize == 0)
      return SZ_OK;
  }
}


SRes Lzma2Decode(Byte *dest, SizeT *destLen, const Byte *src, SizeT *srcLen,
    Byte prop, ELzmaFinishMode finishMode, ELzmaStatus *status, ISzAllocPtr alloc)
{
  CLzma2Dec p;
  SRes res;
  SizeT outSize = *destLen, inSize = *srcLen;
  *destLen = *srcLen = 0;
  *status = LZMA_STATUS_NOT_SPECIFIED;
  Lzma2Dec_CONSTRUCT(&p)
  RINOK(Lzma2Dec_AllocateProbs(&p, prop, alloc))
  p.decoder.dic = dest;
  p.decoder.dicBufSize = outSize;
  Lzma2Dec_Init(&p);
  *srcLen = inSize;
  res = Lzma2Dec_DecodeToDic(&p, outSize, src, srcLen, finishMode, status);
  *destLen = p.decoder.dicPos;
  if (res == SZ_OK && *status == LZMA_STATUS_NEEDS_MORE_INPUT)
    res = SZ_ERROR_INPUT_EOF;
  Lzma2Dec_FreeProbs(&p, alloc);
  return res;
}

#undef PRF
#undef LZMA2_CONTROL_COPY_RESET_DIC
#undef LZMA2_DIC_SIZE_FROM_PROP
#undef LZMA2_IS_UNCOMPRESSED_STATE
#undef LZMA2_LCLP_MAX
#undef PRF

/* ==== LzmaDec.c ==== */
/* LzmaDec.c -- LZMA Decoder
2023-04-07 : Igor Pavlov : Public domain */



#include <string.h>

/* #include "CpuArch.h" */


/* #define kNumTopBits 24 */
#define kTopValue ((UInt32)1 << 24)

#define kNumBitModelTotalBits 11
#define kBitModelTotal (1 << kNumBitModelTotalBits)

#define RC_INIT_SIZE 5

#ifndef Z7_LZMA_DEC_OPT

#define kNumMoveBits 5
#define NORMALIZE if (range < kTopValue) { range <<= 8; code = (code << 8) | (*buf++); }

#define IF_BIT_0(p) ttt = *(p); NORMALIZE; bound = (range >> kNumBitModelTotalBits) * (UInt32)ttt; if (code < bound)
#define UPDATE_0(p) range = bound; *(p) = (CLzmaProb)(ttt + ((kBitModelTotal - ttt) >> kNumMoveBits));
#define UPDATE_1(p) range -= bound; code -= bound; *(p) = (CLzmaProb)(ttt - (ttt >> kNumMoveBits));
#define GET_BIT2(p, i, A0, A1) IF_BIT_0(p) \
  { UPDATE_0(p)  i = (i + i); A0; } else \
  { UPDATE_1(p)  i = (i + i) + 1; A1; }

#define TREE_GET_BIT(probs, i) { GET_BIT2(probs + i, i, ;, ;); }

#define REV_BIT(p, i, A0, A1) IF_BIT_0(p + i) \
  { UPDATE_0(p + i)  A0; } else \
  { UPDATE_1(p + i)  A1; }
#define REV_BIT_VAR(  p, i, m) REV_BIT(p, i, i += m; m += m, m += m; i += m; )
#define REV_BIT_CONST(p, i, m) REV_BIT(p, i, i += m;       , i += m * 2; )
#define REV_BIT_LAST( p, i, m) REV_BIT(p, i, i -= m        , ; )

#define TREE_DECODE(probs, limit, i) \
  { i = 1; do { TREE_GET_BIT(probs, i); } while (i < limit); i -= limit; }

/* #define Z7_LZMA_SIZE_OPT */

#ifdef Z7_LZMA_SIZE_OPT
#define TREE_6_DECODE(probs, i) TREE_DECODE(probs, (1 << 6), i)
#else
#define TREE_6_DECODE(probs, i) \
  { i = 1; \
  TREE_GET_BIT(probs, i) \
  TREE_GET_BIT(probs, i) \
  TREE_GET_BIT(probs, i) \
  TREE_GET_BIT(probs, i) \
  TREE_GET_BIT(probs, i) \
  TREE_GET_BIT(probs, i) \
  i -= 0x40; }
#endif

#define NORMAL_LITER_DEC TREE_GET_BIT(prob, symbol)
#define MATCHED_LITER_DEC \
  matchByte += matchByte; \
  bit = offs; \
  offs &= matchByte; \
  probLit = prob + (offs + bit + symbol); \
  GET_BIT2(probLit, symbol, offs ^= bit; , ;)

#endif /* Z7_LZMA_DEC_OPT */


#define NORMALIZE_CHECK if (range < kTopValue) { if (buf >= bufLimit) return DUMMY_INPUT_EOF; range <<= 8; code = (code << 8) | (*buf++); }

#define IF_BIT_0_CHECK(p) ttt = *(p); NORMALIZE_CHECK bound = (range >> kNumBitModelTotalBits) * (UInt32)ttt; if (code < bound)
#define UPDATE_0_CHECK range = bound;
#define UPDATE_1_CHECK range -= bound; code -= bound;
#define GET_BIT2_CHECK(p, i, A0, A1) IF_BIT_0_CHECK(p) \
  { UPDATE_0_CHECK  i = (i + i); A0; } else \
  { UPDATE_1_CHECK  i = (i + i) + 1; A1; }
#define GET_BIT_CHECK(p, i) GET_BIT2_CHECK(p, i, ; , ;)
#define TREE_DECODE_CHECK(probs, limit, i) \
  { i = 1; do { GET_BIT_CHECK(probs + i, i) } while (i < limit); i -= limit; }


#define REV_BIT_CHECK(p, i, m) IF_BIT_0_CHECK(p + i) \
  { UPDATE_0_CHECK  i += m; m += m; } else \
  { UPDATE_1_CHECK  m += m; i += m; }


#define kNumPosBitsMax 4
#define kNumPosStatesMax (1 << kNumPosBitsMax)

#define kLenNumLowBits 3
#define kLenNumLowSymbols (1 << kLenNumLowBits)
#define kLenNumHighBits 8
#define kLenNumHighSymbols (1 << kLenNumHighBits)

#define LenLow 0
#define LenHigh (LenLow + 2 * (kNumPosStatesMax << kLenNumLowBits))
#define kNumLenProbs (LenHigh + kLenNumHighSymbols)

#define LenChoice LenLow
#define LenChoice2 (LenLow + (1 << kLenNumLowBits))

#define kNumStates 12
#define kNumStates2 16
#define kNumLitStates 7

#define kStartPosModelIndex 4
#define kEndPosModelIndex 14
#define kNumFullDistances (1 << (kEndPosModelIndex >> 1))

#define kNumPosSlotBits 6
#define kNumLenToPosStates 4

#define kNumAlignBits 4
#define kAlignTableSize (1 << kNumAlignBits)

#define kMatchMinLen 2
#define kMatchSpecLenStart (kMatchMinLen + kLenNumLowSymbols * 2 + kLenNumHighSymbols)

#define kMatchSpecLen_Error_Data (1 << 9)
#define kMatchSpecLen_Error_Fail (kMatchSpecLen_Error_Data - 1)

/* External ASM code needs same CLzmaProb array layout. So don't change it. */

/* (probs_1664) is faster and better for code size at some platforms */
/*
#ifdef MY_CPU_X86_OR_AMD64
*/
#define kStartOffset 1664
#define GET_PROBS p->probs_1664
/*
#define GET_PROBS p->probs + kStartOffset
#else
#define kStartOffset 0
#define GET_PROBS p->probs
#endif
*/

#define SpecPos (-kStartOffset)
#define IsRep0Long (SpecPos + kNumFullDistances)
#define RepLenCoder (IsRep0Long + (kNumStates2 << kNumPosBitsMax))
#define LenCoder (RepLenCoder + kNumLenProbs)
#define IsMatch (LenCoder + kNumLenProbs)
#define Align (IsMatch + (kNumStates2 << kNumPosBitsMax))
#define IsRep (Align + kAlignTableSize)
#define IsRepG0 (IsRep + kNumStates)
#define IsRepG1 (IsRepG0 + kNumStates)
#define IsRepG2 (IsRepG1 + kNumStates)
#define PosSlot (IsRepG2 + kNumStates)
#define Literal (PosSlot + (kNumLenToPosStates << kNumPosSlotBits))
#define NUM_BASE_PROBS (Literal + kStartOffset)

#if Align != 0 && kStartOffset != 0
  #error Stop_Compiling_Bad_LZMA_kAlign
#endif

#if NUM_BASE_PROBS != 1984
  #error Stop_Compiling_Bad_LZMA_PROBS
#endif


#define LZMA_LIT_SIZE 0x300

#define LzmaProps_GetNumProbs(p) (NUM_BASE_PROBS + ((UInt32)LZMA_LIT_SIZE << ((p)->lc + (p)->lp)))


#define CALC_POS_STATE(processedPos, pbMask) (((processedPos) & (pbMask)) << 4)
#define COMBINED_PS_STATE (posState + state)
#define GET_LEN_STATE (posState)

#define LZMA_DIC_MIN (1 << 12)

/*
p->remainLen : shows status of LZMA decoder:
    < kMatchSpecLenStart  : the number of bytes to be copied with (p->rep0) offset
    = kMatchSpecLenStart  : the LZMA stream was finished with end mark
    = kMatchSpecLenStart + 1  : need init range coder
    = kMatchSpecLenStart + 2  : need init range coder and state
    = kMatchSpecLen_Error_Fail                : Internal Code Failure
    = kMatchSpecLen_Error_Data + [0 ... 273]  : LZMA Data Error
*/

/* ---------- LZMA_DECODE_REAL ---------- */
/*
LzmaDec_DecodeReal_3() can be implemented in external ASM file.
3 - is the code compatibility version of that function for check at link time.
*/

#define LZMA_DECODE_REAL LzmaDec_DecodeReal_3

/*
LZMA_DECODE_REAL()
In:
  RangeCoder is normalized
  if (p->dicPos == limit)
  {
    LzmaDec_TryDummy() was called before to exclude LITERAL and MATCH-REP cases.
    So first symbol can be only MATCH-NON-REP. And if that MATCH-NON-REP symbol
    is not END_OF_PAYALOAD_MARKER, then the function doesn't write any byte to dictionary,
    the function returns SZ_OK, and the caller can use (p->remainLen) and (p->reps[0]) later.
  }

Processing:
  The first LZMA symbol will be decoded in any case.
  All main checks for limits are at the end of main loop,
  It decodes additional LZMA-symbols while (p->buf < bufLimit && dicPos < limit),
  RangeCoder is still without last normalization when (p->buf < bufLimit) is being checked.
  But if (p->buf < bufLimit), the caller provided at least (LZMA_REQUIRED_INPUT_MAX + 1) bytes for
  next iteration  before limit (bufLimit + LZMA_REQUIRED_INPUT_MAX),
  that is enough for worst case LZMA symbol with one additional RangeCoder normalization for one bit.
  So that function never reads bufLimit [LZMA_REQUIRED_INPUT_MAX] byte.

Out:
  RangeCoder is normalized
  Result:
    SZ_OK - OK
      p->remainLen:
        < kMatchSpecLenStart : the number of bytes to be copied with (p->reps[0]) offset
        = kMatchSpecLenStart : the LZMA stream was finished with end mark

    SZ_ERROR_DATA - error, when the MATCH-Symbol refers out of dictionary
      p->remainLen : undefined
      p->reps[*]    : undefined
*/


#ifdef Z7_LZMA_DEC_OPT

int Z7_FASTCALL LZMA_DECODE_REAL(CLzmaDec *p, SizeT limit, const Byte *bufLimit);

#else

static
int Z7_FASTCALL LZMA_DECODE_REAL(CLzmaDec *p, SizeT limit, const Byte *bufLimit)
{
  CLzmaProb *probs = GET_PROBS;
  unsigned state = (unsigned)p->state;
  UInt32 rep0 = p->reps[0], rep1 = p->reps[1], rep2 = p->reps[2], rep3 = p->reps[3];
  unsigned pbMask = ((unsigned)1 << (p->prop.pb)) - 1;
  unsigned lc = p->prop.lc;
  unsigned lpMask = ((unsigned)0x100 << p->prop.lp) - ((unsigned)0x100 >> lc);

  Byte *dic = p->dic;
  SizeT dicBufSize = p->dicBufSize;
  SizeT dicPos = p->dicPos;
  
  UInt32 processedPos = p->processedPos;
  UInt32 checkDicSize = p->checkDicSize;
  unsigned len = 0;

  const Byte *buf = p->buf;
  UInt32 range = p->range;
  UInt32 code = p->code;

  do
  {
    CLzmaProb *prob;
    UInt32 bound;
    unsigned ttt;
    unsigned posState = CALC_POS_STATE(processedPos, pbMask);

    prob = probs + IsMatch + COMBINED_PS_STATE;
    IF_BIT_0(prob)
    {
      unsigned symbol;
      UPDATE_0(prob)
      prob = probs + Literal;
      if (processedPos != 0 || checkDicSize != 0)
        prob += (UInt32)3 * ((((processedPos << 8) + dic[(dicPos == 0 ? dicBufSize : dicPos) - 1]) & lpMask) << lc);
      processedPos++;

      if (state < kNumLitStates)
      {
        state -= (state < 4) ? state : 3;
        symbol = 1;
        #ifdef Z7_LZMA_SIZE_OPT
        do { NORMAL_LITER_DEC } while (symbol < 0x100);
        #else
        NORMAL_LITER_DEC
        NORMAL_LITER_DEC
        NORMAL_LITER_DEC
        NORMAL_LITER_DEC
        NORMAL_LITER_DEC
        NORMAL_LITER_DEC
        NORMAL_LITER_DEC
        NORMAL_LITER_DEC
        #endif
      }
      else
      {
        unsigned matchByte = dic[dicPos - rep0 + (dicPos < rep0 ? dicBufSize : 0)];
        unsigned offs = 0x100;
        state -= (state < 10) ? 3 : 6;
        symbol = 1;
        #ifdef Z7_LZMA_SIZE_OPT
        do
        {
          unsigned bit;
          CLzmaProb *probLit;
          MATCHED_LITER_DEC
        }
        while (symbol < 0x100);
        #else
        {
          unsigned bit;
          CLzmaProb *probLit;
          MATCHED_LITER_DEC
          MATCHED_LITER_DEC
          MATCHED_LITER_DEC
          MATCHED_LITER_DEC
          MATCHED_LITER_DEC
          MATCHED_LITER_DEC
          MATCHED_LITER_DEC
          MATCHED_LITER_DEC
        }
        #endif
      }

      dic[dicPos++] = (Byte)symbol;
      continue;
    }
    
    {
      UPDATE_1(prob)
      prob = probs + IsRep + state;
      IF_BIT_0(prob)
      {
        UPDATE_0(prob)
        state += kNumStates;
        prob = probs + LenCoder;
      }
      else
      {
        UPDATE_1(prob)
        prob = probs + IsRepG0 + state;
        IF_BIT_0(prob)
        {
          UPDATE_0(prob)
          prob = probs + IsRep0Long + COMBINED_PS_STATE;
          IF_BIT_0(prob)
          {
            UPDATE_0(prob)
  
            /* that case was checked before with kBadRepCode */
            /* if (checkDicSize == 0 && processedPos == 0) { len = kMatchSpecLen_Error_Data + 1; break; } */
            /* The caller doesn't allow (dicPos == limit) case here */
            /* so we don't need the following check: */
            /* if (dicPos == limit) { state = state < kNumLitStates ? 9 : 11; len = 1; break; } */
            
            dic[dicPos] = dic[dicPos - rep0 + (dicPos < rep0 ? dicBufSize : 0)];
            dicPos++;
            processedPos++;
            state = state < kNumLitStates ? 9 : 11;
            continue;
          }
          UPDATE_1(prob)
        }
        else
        {
          UInt32 distance;
          UPDATE_1(prob)
          prob = probs + IsRepG1 + state;
          IF_BIT_0(prob)
          {
            UPDATE_0(prob)
            distance = rep1;
          }
          else
          {
            UPDATE_1(prob)
            prob = probs + IsRepG2 + state;
            IF_BIT_0(prob)
            {
              UPDATE_0(prob)
              distance = rep2;
            }
            else
            {
              UPDATE_1(prob)
              distance = rep3;
              rep3 = rep2;
            }
            rep2 = rep1;
          }
          rep1 = rep0;
          rep0 = distance;
        }
        state = state < kNumLitStates ? 8 : 11;
        prob = probs + RepLenCoder;
      }
      
      #ifdef Z7_LZMA_SIZE_OPT
      {
        unsigned lim, offset;
        CLzmaProb *probLen = prob + LenChoice;
        IF_BIT_0(probLen)
        {
          UPDATE_0(probLen)
          probLen = prob + LenLow + GET_LEN_STATE;
          offset = 0;
          lim = (1 << kLenNumLowBits);
        }
        else
        {
          UPDATE_1(probLen)
          probLen = prob + LenChoice2;
          IF_BIT_0(probLen)
          {
            UPDATE_0(probLen)
            probLen = prob + LenLow + GET_LEN_STATE + (1 << kLenNumLowBits);
            offset = kLenNumLowSymbols;
            lim = (1 << kLenNumLowBits);
          }
          else
          {
            UPDATE_1(probLen)
            probLen = prob + LenHigh;
            offset = kLenNumLowSymbols * 2;
            lim = (1 << kLenNumHighBits);
          }
        }
        TREE_DECODE(probLen, lim, len)
        len += offset;
      }
      #else
      {
        CLzmaProb *probLen = prob + LenChoice;
        IF_BIT_0(probLen)
        {
          UPDATE_0(probLen)
          probLen = prob + LenLow + GET_LEN_STATE;
          len = 1;
          TREE_GET_BIT(probLen, len)
          TREE_GET_BIT(probLen, len)
          TREE_GET_BIT(probLen, len)
          len -= 8;
        }
        else
        {
          UPDATE_1(probLen)
          probLen = prob + LenChoice2;
          IF_BIT_0(probLen)
          {
            UPDATE_0(probLen)
            probLen = prob + LenLow + GET_LEN_STATE + (1 << kLenNumLowBits);
            len = 1;
            TREE_GET_BIT(probLen, len)
            TREE_GET_BIT(probLen, len)
            TREE_GET_BIT(probLen, len)
          }
          else
          {
            UPDATE_1(probLen)
            probLen = prob + LenHigh;
            TREE_DECODE(probLen, (1 << kLenNumHighBits), len)
            len += kLenNumLowSymbols * 2;
          }
        }
      }
      #endif

      if (state >= kNumStates)
      {
        UInt32 distance;
        prob = probs + PosSlot +
            ((len < kNumLenToPosStates ? len : kNumLenToPosStates - 1) << kNumPosSlotBits);
        TREE_6_DECODE(prob, distance)
        if (distance >= kStartPosModelIndex)
        {
          unsigned posSlot = (unsigned)distance;
          unsigned numDirectBits = (unsigned)(((distance >> 1) - 1));
          distance = (2 | (distance & 1));
          if (posSlot < kEndPosModelIndex)
          {
            distance <<= numDirectBits;
            prob = probs + SpecPos;
            {
              UInt32 m = 1;
              distance++;
              do
              {
                REV_BIT_VAR(prob, distance, m)
              }
              while (--numDirectBits);
              distance -= m;
            }
          }
          else
          {
            numDirectBits -= kNumAlignBits;
            do
            {
              NORMALIZE
              range >>= 1;
              
              {
                UInt32 t;
                code -= range;
                t = (0 - ((UInt32)code >> 31)); /* (UInt32)((Int32)code >> 31) */
                distance = (distance << 1) + (t + 1);
                code += range & t;
              }
              /*
              distance <<= 1;
              if (code >= range)
              {
                code -= range;
                distance |= 1;
              }
              */
            }
            while (--numDirectBits);
            prob = probs + Align;
            distance <<= kNumAlignBits;
            {
              unsigned i = 1;
              REV_BIT_CONST(prob, i, 1)
              REV_BIT_CONST(prob, i, 2)
              REV_BIT_CONST(prob, i, 4)
              REV_BIT_LAST (prob, i, 8)
              distance |= i;
            }
            if (distance == (UInt32)0xFFFFFFFF)
            {
              len = kMatchSpecLenStart;
              state -= kNumStates;
              break;
            }
          }
        }
        
        rep3 = rep2;
        rep2 = rep1;
        rep1 = rep0;
        rep0 = distance + 1;
        state = (state < kNumStates + kNumLitStates) ? kNumLitStates : kNumLitStates + 3;
        if (distance >= (checkDicSize == 0 ? processedPos: checkDicSize))
        {
          len += kMatchSpecLen_Error_Data + kMatchMinLen;
          /* len = kMatchSpecLen_Error_Data; */
          /* len += kMatchMinLen; */
          break;
        }
      }

      len += kMatchMinLen;

      {
        SizeT rem;
        unsigned curLen;
        SizeT pos;
        
        if ((rem = limit - dicPos) == 0)
        {
          /*
          We stop decoding and return SZ_OK, and we can resume decoding later.
          Any error conditions can be tested later in caller code.
          For more strict mode we can stop decoding with error
          // len += kMatchSpecLen_Error_Data;
          */
          break;
        }
        
        curLen = ((rem < len) ? (unsigned)rem : len);
        pos = dicPos - rep0 + (dicPos < rep0 ? dicBufSize : 0);

        processedPos += (UInt32)curLen;

        len -= curLen;
        if (curLen <= dicBufSize - pos)
        {
          Byte *dest = dic + dicPos;
          ptrdiff_t src = (ptrdiff_t)pos - (ptrdiff_t)dicPos;
          const Byte *lim = dest + curLen;
          dicPos += (SizeT)curLen;
          do
            *(dest) = (Byte)*(dest + src);
          while (++dest != lim);
        }
        else
        {
          do
          {
            dic[dicPos++] = dic[pos];
            if (++pos == dicBufSize)
              pos = 0;
          }
          while (--curLen != 0);
        }
      }
    }
  }
  while (dicPos < limit && buf < bufLimit);

  NORMALIZE
  
  p->buf = buf;
  p->range = range;
  p->code = code;
  p->remainLen = (UInt32)len; /* & (kMatchSpecLen_Error_Data - 1); // we can write real length for error matches too. */
  p->dicPos = dicPos;
  p->processedPos = processedPos;
  p->reps[0] = rep0;
  p->reps[1] = rep1;
  p->reps[2] = rep2;
  p->reps[3] = rep3;
  p->state = (UInt32)state;
  if (len >= kMatchSpecLen_Error_Data)
    return SZ_ERROR_DATA;
  return SZ_OK;
}
#endif



static void Z7_FASTCALL LzmaDec_WriteRem(CLzmaDec *p, SizeT limit)
{
  unsigned len = (unsigned)p->remainLen;
  if (len == 0 /* || len >= kMatchSpecLenStart */)
    return;
  {
    SizeT dicPos = p->dicPos;
    Byte *dic;
    SizeT dicBufSize;
    SizeT rep0;   /* we use SizeT to avoid the BUG of VC14 for AMD64 */
    {
      SizeT rem = limit - dicPos;
      if (rem < len)
      {
        len = (unsigned)(rem);
        if (len == 0)
          return;
      }
    }

    if (p->checkDicSize == 0 && p->prop.dicSize - p->processedPos <= len)
      p->checkDicSize = p->prop.dicSize;

    p->processedPos += (UInt32)len;
    p->remainLen -= (UInt32)len;
    dic = p->dic;
    rep0 = p->reps[0];
    dicBufSize = p->dicBufSize;
    do
    {
      dic[dicPos] = dic[dicPos - rep0 + (dicPos < rep0 ? dicBufSize : 0)];
      dicPos++;
    }
    while (--len);
    p->dicPos = dicPos;
  }
}


/*
At staring of new stream we have one of the following symbols:
  - Literal        - is allowed
  - Non-Rep-Match  - is allowed only if it's end marker symbol
  - Rep-Match      - is not allowed
We use early check of (RangeCoder:Code) over kBadRepCode to simplify main decoding code
*/

#define kRange0 0xFFFFFFFF
#define kBound0 ((kRange0 >> kNumBitModelTotalBits) << (kNumBitModelTotalBits - 1))
#define kBadRepCode (kBound0 + (((kRange0 - kBound0) >> kNumBitModelTotalBits) << (kNumBitModelTotalBits - 1)))
#if kBadRepCode != (0xC0000000 - 0x400)
  #error Stop_Compiling_Bad_LZMA_Check
#endif


/*
LzmaDec_DecodeReal2():
  It calls LZMA_DECODE_REAL() and it adjusts limit according (p->checkDicSize).

We correct (p->checkDicSize) after LZMA_DECODE_REAL() and in LzmaDec_WriteRem(),
and we support the following state of (p->checkDicSize):
  if (total_processed < p->prop.dicSize) then
  {
    (total_processed == p->processedPos)
    (p->checkDicSize == 0)
  }
  else
    (p->checkDicSize == p->prop.dicSize)
*/

static int Z7_FASTCALL LzmaDec_DecodeReal2(CLzmaDec *p, SizeT limit, const Byte *bufLimit)
{
  if (p->checkDicSize == 0)
  {
    UInt32 rem = p->prop.dicSize - p->processedPos;
    if (limit - p->dicPos > rem)
      limit = p->dicPos + rem;
  }
  {
    int res = LZMA_DECODE_REAL(p, limit, bufLimit);
    if (p->checkDicSize == 0 && p->processedPos >= p->prop.dicSize)
      p->checkDicSize = p->prop.dicSize;
    return res;
  }
}



typedef enum
{
  DUMMY_INPUT_EOF, /* need more input data */
  DUMMY_LIT,
  DUMMY_MATCH,
  DUMMY_REP
} ELzmaDummy;


#define IS_DUMMY_END_MARKER_POSSIBLE(dummyRes) ((dummyRes) == DUMMY_MATCH)

static ELzmaDummy LzmaDec_TryDummy(const CLzmaDec *p, const Byte *buf, const Byte **bufOut)
{
  UInt32 range = p->range;
  UInt32 code = p->code;
  const Byte *bufLimit = *bufOut;
  const CLzmaProb *probs = GET_PROBS;
  unsigned state = (unsigned)p->state;
  ELzmaDummy res;

  for (;;)
  {
    const CLzmaProb *prob;
    UInt32 bound;
    unsigned ttt;
    unsigned posState = CALC_POS_STATE(p->processedPos, ((unsigned)1 << p->prop.pb) - 1);

    prob = probs + IsMatch + COMBINED_PS_STATE;
    IF_BIT_0_CHECK(prob)
    {
      UPDATE_0_CHECK

      prob = probs + Literal;
      if (p->checkDicSize != 0 || p->processedPos != 0)
        prob += ((UInt32)LZMA_LIT_SIZE *
            ((((p->processedPos) & (((unsigned)1 << (p->prop.lp)) - 1)) << p->prop.lc) +
            ((unsigned)p->dic[(p->dicPos == 0 ? p->dicBufSize : p->dicPos) - 1] >> (8 - p->prop.lc))));

      if (state < kNumLitStates)
      {
        unsigned symbol = 1;
        do { GET_BIT_CHECK(prob + symbol, symbol) } while (symbol < 0x100);
      }
      else
      {
        unsigned matchByte = p->dic[p->dicPos - p->reps[0] +
            (p->dicPos < p->reps[0] ? p->dicBufSize : 0)];
        unsigned offs = 0x100;
        unsigned symbol = 1;
        do
        {
          unsigned bit;
          const CLzmaProb *probLit;
          matchByte += matchByte;
          bit = offs;
          offs &= matchByte;
          probLit = prob + (offs + bit + symbol);
          GET_BIT2_CHECK(probLit, symbol, offs ^= bit; , ; )
        }
        while (symbol < 0x100);
      }
      res = DUMMY_LIT;
    }
    else
    {
      unsigned len;
      UPDATE_1_CHECK

      prob = probs + IsRep + state;
      IF_BIT_0_CHECK(prob)
      {
        UPDATE_0_CHECK
        state = 0;
        prob = probs + LenCoder;
        res = DUMMY_MATCH;
      }
      else
      {
        UPDATE_1_CHECK
        res = DUMMY_REP;
        prob = probs + IsRepG0 + state;
        IF_BIT_0_CHECK(prob)
        {
          UPDATE_0_CHECK
          prob = probs + IsRep0Long + COMBINED_PS_STATE;
          IF_BIT_0_CHECK(prob)
          {
            UPDATE_0_CHECK
            break;
          }
          else
          {
            UPDATE_1_CHECK
          }
        }
        else
        {
          UPDATE_1_CHECK
          prob = probs + IsRepG1 + state;
          IF_BIT_0_CHECK(prob)
          {
            UPDATE_0_CHECK
          }
          else
          {
            UPDATE_1_CHECK
            prob = probs + IsRepG2 + state;
            IF_BIT_0_CHECK(prob)
            {
              UPDATE_0_CHECK
            }
            else
            {
              UPDATE_1_CHECK
            }
          }
        }
        state = kNumStates;
        prob = probs + RepLenCoder;
      }
      {
        unsigned limit, offset;
        const CLzmaProb *probLen = prob + LenChoice;
        IF_BIT_0_CHECK(probLen)
        {
          UPDATE_0_CHECK
          probLen = prob + LenLow + GET_LEN_STATE;
          offset = 0;
          limit = 1 << kLenNumLowBits;
        }
        else
        {
          UPDATE_1_CHECK
          probLen = prob + LenChoice2;
          IF_BIT_0_CHECK(probLen)
          {
            UPDATE_0_CHECK
            probLen = prob + LenLow + GET_LEN_STATE + (1 << kLenNumLowBits);
            offset = kLenNumLowSymbols;
            limit = 1 << kLenNumLowBits;
          }
          else
          {
            UPDATE_1_CHECK
            probLen = prob + LenHigh;
            offset = kLenNumLowSymbols * 2;
            limit = 1 << kLenNumHighBits;
          }
        }
        TREE_DECODE_CHECK(probLen, limit, len)
        len += offset;
      }

      if (state < 4)
      {
        unsigned posSlot;
        prob = probs + PosSlot +
            ((len < kNumLenToPosStates - 1 ? len : kNumLenToPosStates - 1) <<
            kNumPosSlotBits);
        TREE_DECODE_CHECK(prob, 1 << kNumPosSlotBits, posSlot)
        if (posSlot >= kStartPosModelIndex)
        {
          unsigned numDirectBits = ((posSlot >> 1) - 1);

          if (posSlot < kEndPosModelIndex)
          {
            prob = probs + SpecPos + ((2 | (posSlot & 1)) << numDirectBits);
          }
          else
          {
            numDirectBits -= kNumAlignBits;
            do
            {
              NORMALIZE_CHECK
              range >>= 1;
              code -= range & (((code - range) >> 31) - 1);
              /* if (code >= range) code -= range; */
            }
            while (--numDirectBits);
            prob = probs + Align;
            numDirectBits = kNumAlignBits;
          }
          {
            unsigned i = 1;
            unsigned m = 1;
            do
            {
              REV_BIT_CHECK(prob, i, m)
            }
            while (--numDirectBits);
          }
        }
      }
    }
    break;
  }
  NORMALIZE_CHECK

  *bufOut = buf;
  return res;
}

void LzmaDec_InitDicAndState(CLzmaDec *p, BoolInt initDic, BoolInt initState);
void LzmaDec_InitDicAndState(CLzmaDec *p, BoolInt initDic, BoolInt initState)
{
  p->remainLen = kMatchSpecLenStart + 1;
  p->tempBufSize = 0;

  if (initDic)
  {
    p->processedPos = 0;
    p->checkDicSize = 0;
    p->remainLen = kMatchSpecLenStart + 2;
  }
  if (initState)
    p->remainLen = kMatchSpecLenStart + 2;
}

void LzmaDec_Init(CLzmaDec *p)
{
  p->dicPos = 0;
  LzmaDec_InitDicAndState(p, True, True);
}


/*
LZMA supports optional end_marker.
So the decoder can lookahead for one additional LZMA-Symbol to check end_marker.
That additional LZMA-Symbol can require up to LZMA_REQUIRED_INPUT_MAX bytes in input stream.
When the decoder reaches dicLimit, it looks (finishMode) parameter:
  if (finishMode == LZMA_FINISH_ANY), the decoder doesn't lookahead
  if (finishMode != LZMA_FINISH_ANY), the decoder lookahead, if end_marker is possible for current position

When the decoder lookahead, and the lookahead symbol is not end_marker, we have two ways:
  1) Strict mode (default) : the decoder returns SZ_ERROR_DATA.
  2) The relaxed mode (alternative mode) : we could return SZ_OK, and the caller
     must check (status) value. The caller can show the error,
     if the end of stream is expected, and the (status) is noit
     LZMA_STATUS_FINISHED_WITH_MARK or LZMA_STATUS_MAYBE_FINISHED_WITHOUT_MARK.
*/


#define RETURN_NOT_FINISHED_FOR_FINISH \
  *status = LZMA_STATUS_NOT_FINISHED; \
  return SZ_ERROR_DATA; /* for strict mode */
  /* return SZ_OK; // for relaxed mode */


SRes LzmaDec_DecodeToDic(CLzmaDec *p, SizeT dicLimit, const Byte *src, SizeT *srcLen,
    ELzmaFinishMode finishMode, ELzmaStatus *status)
{
  SizeT inSize = *srcLen;
  (*srcLen) = 0;
  *status = LZMA_STATUS_NOT_SPECIFIED;

  if (p->remainLen > kMatchSpecLenStart)
  {
    if (p->remainLen > kMatchSpecLenStart + 2)
      return p->remainLen == kMatchSpecLen_Error_Fail ? SZ_ERROR_FAIL : SZ_ERROR_DATA;

    for (; inSize > 0 && p->tempBufSize < RC_INIT_SIZE; (*srcLen)++, inSize--)
      p->tempBuf[p->tempBufSize++] = *src++;
    if (p->tempBufSize != 0 && p->tempBuf[0] != 0)
      return SZ_ERROR_DATA;
    if (p->tempBufSize < RC_INIT_SIZE)
    {
      *status = LZMA_STATUS_NEEDS_MORE_INPUT;
      return SZ_OK;
    }
    p->code =
        ((UInt32)p->tempBuf[1] << 24)
      | ((UInt32)p->tempBuf[2] << 16)
      | ((UInt32)p->tempBuf[3] << 8)
      | ((UInt32)p->tempBuf[4]);

    if (p->checkDicSize == 0
        && p->processedPos == 0
        && p->code >= kBadRepCode)
      return SZ_ERROR_DATA;

    p->range = 0xFFFFFFFF;
    p->tempBufSize = 0;

    if (p->remainLen > kMatchSpecLenStart + 1)
    {
      SizeT numProbs = LzmaProps_GetNumProbs(&p->prop);
      SizeT i;
      CLzmaProb *probs = p->probs;
      for (i = 0; i < numProbs; i++)
        probs[i] = kBitModelTotal >> 1;
      p->reps[0] = p->reps[1] = p->reps[2] = p->reps[3] = 1;
      p->state = 0;
    }

    p->remainLen = 0;
  }

  for (;;)
  {
    if (p->remainLen == kMatchSpecLenStart)
    {
      if (p->code != 0)
        return SZ_ERROR_DATA;
      *status = LZMA_STATUS_FINISHED_WITH_MARK;
      return SZ_OK;
    }

    LzmaDec_WriteRem(p, dicLimit);

    {
      /* (p->remainLen == 0 || p->dicPos == dicLimit) */

      int checkEndMarkNow = 0;

      if (p->dicPos >= dicLimit)
      {
        if (p->remainLen == 0 && p->code == 0)
        {
          *status = LZMA_STATUS_MAYBE_FINISHED_WITHOUT_MARK;
          return SZ_OK;
        }
        if (finishMode == LZMA_FINISH_ANY)
        {
          *status = LZMA_STATUS_NOT_FINISHED;
          return SZ_OK;
        }
        if (p->remainLen != 0)
        {
          RETURN_NOT_FINISHED_FOR_FINISH
        }
        checkEndMarkNow = 1;
      }

      /* (p->remainLen == 0) */

      if (p->tempBufSize == 0)
      {
        const Byte *bufLimit;
        int dummyProcessed = -1;
        
        if (inSize < LZMA_REQUIRED_INPUT_MAX || checkEndMarkNow)
        {
          const Byte *bufOut = src + inSize;
          
          ELzmaDummy dummyRes = LzmaDec_TryDummy(p, src, &bufOut);
          
          if (dummyRes == DUMMY_INPUT_EOF)
          {
            size_t i;
            if (inSize >= LZMA_REQUIRED_INPUT_MAX)
              break;
            (*srcLen) += inSize;
            p->tempBufSize = (unsigned)inSize;
            for (i = 0; i < inSize; i++)
              p->tempBuf[i] = src[i];
            *status = LZMA_STATUS_NEEDS_MORE_INPUT;
            return SZ_OK;
          }
 
          dummyProcessed = (int)(bufOut - src);
          if ((unsigned)dummyProcessed > LZMA_REQUIRED_INPUT_MAX)
            break;
          
          if (checkEndMarkNow && !IS_DUMMY_END_MARKER_POSSIBLE(dummyRes))
          {
            unsigned i;
            (*srcLen) += (unsigned)dummyProcessed;
            p->tempBufSize = (unsigned)dummyProcessed;
            for (i = 0; i < (unsigned)dummyProcessed; i++)
              p->tempBuf[i] = src[i];
            /* p->remainLen = kMatchSpecLen_Error_Data; */
            RETURN_NOT_FINISHED_FOR_FINISH
          }
          
          bufLimit = src;
          /* we will decode only one iteration */
        }
        else
          bufLimit = src + inSize - LZMA_REQUIRED_INPUT_MAX;

        p->buf = src;
        
        {
          int res = LzmaDec_DecodeReal2(p, dicLimit, bufLimit);
          
          SizeT processed = (SizeT)(p->buf - src);

          if (dummyProcessed < 0)
          {
            if (processed > inSize)
              break;
          }
          else if ((unsigned)dummyProcessed != processed)
            break;

          src += processed;
          inSize -= processed;
          (*srcLen) += processed;

          if (res != SZ_OK)
          {
            p->remainLen = kMatchSpecLen_Error_Data;
            return SZ_ERROR_DATA;
          }
        }
        continue;
      }

      {
        /* we have some data in (p->tempBuf) */
        /* in strict mode: tempBufSize is not enough for one Symbol decoding. */
        /* in relaxed mode: tempBufSize not larger than required for one Symbol decoding. */

        unsigned rem = p->tempBufSize;
        unsigned ahead = 0;
        int dummyProcessed = -1;
        
        while (rem < LZMA_REQUIRED_INPUT_MAX && ahead < inSize)
          p->tempBuf[rem++] = src[ahead++];
        
        /* ahead - the size of new data copied from (src) to (p->tempBuf) */
        /* rem   - the size of temp buffer including new data from (src) */
        
        if (rem < LZMA_REQUIRED_INPUT_MAX || checkEndMarkNow)
        {
          const Byte *bufOut = p->tempBuf + rem;
        
          ELzmaDummy dummyRes = LzmaDec_TryDummy(p, p->tempBuf, &bufOut);
          
          if (dummyRes == DUMMY_INPUT_EOF)
          {
            if (rem >= LZMA_REQUIRED_INPUT_MAX)
              break;
            p->tempBufSize = rem;
            (*srcLen) += (SizeT)ahead;
            *status = LZMA_STATUS_NEEDS_MORE_INPUT;
            return SZ_OK;
          }
          
          dummyProcessed = (int)(bufOut - p->tempBuf);

          if ((unsigned)dummyProcessed < p->tempBufSize)
            break;

          if (checkEndMarkNow && !IS_DUMMY_END_MARKER_POSSIBLE(dummyRes))
          {
            (*srcLen) += (unsigned)dummyProcessed - p->tempBufSize;
            p->tempBufSize = (unsigned)dummyProcessed;
            /* p->remainLen = kMatchSpecLen_Error_Data; */
            RETURN_NOT_FINISHED_FOR_FINISH
          }
        }

        p->buf = p->tempBuf;
        
        {
          /* we decode one symbol from (p->tempBuf) here, so the (bufLimit) is equal to (p->buf) */
          int res = LzmaDec_DecodeReal2(p, dicLimit, p->buf);

          SizeT processed = (SizeT)(p->buf - p->tempBuf);
          rem = p->tempBufSize;
          
          if (dummyProcessed < 0)
          {
            if (processed > LZMA_REQUIRED_INPUT_MAX)
              break;
            if (processed < rem)
              break;
          }
          else if ((unsigned)dummyProcessed != processed)
            break;
          
          processed -= rem;

          src += processed;
          inSize -= processed;
          (*srcLen) += processed;
          p->tempBufSize = 0;
          
          if (res != SZ_OK)
          {
            p->remainLen = kMatchSpecLen_Error_Data;
            return SZ_ERROR_DATA;
          }
        }
      }
    }
  }

  /*  Some unexpected error: internal error of code, memory corruption or hardware failure */
  p->remainLen = kMatchSpecLen_Error_Fail;
  return SZ_ERROR_FAIL;
}



SRes LzmaDec_DecodeToBuf(CLzmaDec *p, Byte *dest, SizeT *destLen, const Byte *src, SizeT *srcLen, ELzmaFinishMode finishMode, ELzmaStatus *status)
{
  SizeT outSize = *destLen;
  SizeT inSize = *srcLen;
  *srcLen = *destLen = 0;
  for (;;)
  {
    SizeT inSizeCur = inSize, outSizeCur, dicPos;
    ELzmaFinishMode curFinishMode;
    SRes res;
    if (p->dicPos == p->dicBufSize)
      p->dicPos = 0;
    dicPos = p->dicPos;
    if (outSize > p->dicBufSize - dicPos)
    {
      outSizeCur = p->dicBufSize;
      curFinishMode = LZMA_FINISH_ANY;
    }
    else
    {
      outSizeCur = dicPos + outSize;
      curFinishMode = finishMode;
    }

    res = LzmaDec_DecodeToDic(p, outSizeCur, src, &inSizeCur, curFinishMode, status);
    src += inSizeCur;
    inSize -= inSizeCur;
    *srcLen += inSizeCur;
    outSizeCur = p->dicPos - dicPos;
    memcpy(dest, p->dic + dicPos, outSizeCur);
    dest += outSizeCur;
    outSize -= outSizeCur;
    *destLen += outSizeCur;
    if (res != 0)
      return res;
    if (outSizeCur == 0 || outSize == 0)
      return SZ_OK;
  }
}

void LzmaDec_FreeProbs(CLzmaDec *p, ISzAllocPtr alloc)
{
  ISzAlloc_Free(alloc, p->probs);
  p->probs = NULL;
}

static void LzmaDec_FreeDict(CLzmaDec *p, ISzAllocPtr alloc)
{
  ISzAlloc_Free(alloc, p->dic);
  p->dic = NULL;
}

void LzmaDec_Free(CLzmaDec *p, ISzAllocPtr alloc)
{
  LzmaDec_FreeProbs(p, alloc);
  LzmaDec_FreeDict(p, alloc);
}

SRes LzmaProps_Decode(CLzmaProps *p, const Byte *data, unsigned size)
{
  UInt32 dicSize;
  Byte d;
  
  if (size < LZMA_PROPS_SIZE)
    return SZ_ERROR_UNSUPPORTED;
  else
    dicSize = data[1] | ((UInt32)data[2] << 8) | ((UInt32)data[3] << 16) | ((UInt32)data[4] << 24);
 
  if (dicSize < LZMA_DIC_MIN)
    dicSize = LZMA_DIC_MIN;
  p->dicSize = dicSize;

  d = data[0];
  if (d >= (9 * 5 * 5))
    return SZ_ERROR_UNSUPPORTED;

  p->lc = (Byte)(d % 9);
  d /= 9;
  p->pb = (Byte)(d / 5);
  p->lp = (Byte)(d % 5);

  return SZ_OK;
}

static SRes LzmaDec_AllocateProbs2(CLzmaDec *p, const CLzmaProps *propNew, ISzAllocPtr alloc)
{
  UInt32 numProbs = LzmaProps_GetNumProbs(propNew);
  if (!p->probs || numProbs != p->numProbs)
  {
    LzmaDec_FreeProbs(p, alloc);
    p->probs = (CLzmaProb *)ISzAlloc_Alloc(alloc, numProbs * sizeof(CLzmaProb));
    if (!p->probs)
      return SZ_ERROR_MEM;
    p->probs_1664 = p->probs + 1664;
    p->numProbs = numProbs;
  }
  return SZ_OK;
}

SRes LzmaDec_AllocateProbs(CLzmaDec *p, const Byte *props, unsigned propsSize, ISzAllocPtr alloc)
{
  CLzmaProps propNew;
  RINOK(LzmaProps_Decode(&propNew, props, propsSize))
  RINOK(LzmaDec_AllocateProbs2(p, &propNew, alloc))
  p->prop = propNew;
  return SZ_OK;
}

SRes LzmaDec_Allocate(CLzmaDec *p, const Byte *props, unsigned propsSize, ISzAllocPtr alloc)
{
  CLzmaProps propNew;
  SizeT dicBufSize;
  RINOK(LzmaProps_Decode(&propNew, props, propsSize))
  RINOK(LzmaDec_AllocateProbs2(p, &propNew, alloc))

  {
    UInt32 dictSize = propNew.dicSize;
    SizeT mask = ((UInt32)1 << 12) - 1;
         if (dictSize >= ((UInt32)1 << 30)) mask = ((UInt32)1 << 22) - 1;
    else if (dictSize >= ((UInt32)1 << 22)) mask = ((UInt32)1 << 20) - 1;
    dicBufSize = ((SizeT)dictSize + mask) & ~mask;
    if (dicBufSize < dictSize)
      dicBufSize = dictSize;
  }

  if (!p->dic || dicBufSize != p->dicBufSize)
  {
    LzmaDec_FreeDict(p, alloc);
    p->dic = (Byte *)ISzAlloc_Alloc(alloc, dicBufSize);
    if (!p->dic)
    {
      LzmaDec_FreeProbs(p, alloc);
      return SZ_ERROR_MEM;
    }
  }
  p->dicBufSize = dicBufSize;
  p->prop = propNew;
  return SZ_OK;
}

SRes LzmaDecode(Byte *dest, SizeT *destLen, const Byte *src, SizeT *srcLen,
    const Byte *propData, unsigned propSize, ELzmaFinishMode finishMode,
    ELzmaStatus *status, ISzAllocPtr alloc)
{
  CLzmaDec p;
  SRes res;
  SizeT outSize = *destLen, inSize = *srcLen;
  *destLen = *srcLen = 0;
  *status = LZMA_STATUS_NOT_SPECIFIED;
  if (inSize < RC_INIT_SIZE)
    return SZ_ERROR_INPUT_EOF;
  LzmaDec_CONSTRUCT(&p)
  RINOK(LzmaDec_AllocateProbs(&p, propData, propSize, alloc))
  p.dic = dest;
  p.dicBufSize = outSize;
  LzmaDec_Init(&p);
  *srcLen = inSize;
  res = LzmaDec_DecodeToDic(&p, outSize, src, srcLen, finishMode, status);
  *destLen = p.dicPos;
  if (res == SZ_OK && *status == LZMA_STATUS_NEEDS_MORE_INPUT)
    res = SZ_ERROR_INPUT_EOF;
  LzmaDec_FreeProbs(&p, alloc);
  return res;
}
#undef Align
#undef CALC_POS_STATE
#undef COMBINED_PS_STATE
#undef GET_BIT2
#undef GET_BIT2_CHECK
#undef GET_BIT_CHECK
#undef GET_LEN_STATE
#undef GET_PROBS
#undef IF_BIT_0
#undef IF_BIT_0_CHECK
#undef IS_DUMMY_END_MARKER_POSSIBLE
#undef IsMatch
#undef IsRep
#undef IsRep0Long
#undef IsRepG0
#undef IsRepG1
#undef IsRepG2
#undef LZMA_DECODE_REAL
#undef LZMA_DIC_MIN
#undef LZMA_LIT_SIZE
#undef LenChoice
#undef LenChoice2
#undef LenCoder
#undef LenHigh
#undef LenLow
#undef Literal
#undef LzmaProps_GetNumProbs
#undef MATCHED_LITER_DEC
#undef NORMALIZE
#undef NORMALIZE_CHECK
#undef NORMAL_LITER_DEC
#undef NUM_BASE_PROBS
#undef PosSlot
#undef RC_INIT_SIZE
#undef RETURN_NOT_FINISHED_FOR_FINISH
#undef REV_BIT
#undef REV_BIT_CHECK
#undef REV_BIT_CONST
#undef REV_BIT_LAST
#undef REV_BIT_VAR
#undef RepLenCoder
#undef SpecPos
#undef TREE_6_DECODE
#undef TREE_DECODE
#undef TREE_DECODE_CHECK
#undef TREE_GET_BIT
#undef UPDATE_0
#undef UPDATE_0_CHECK
#undef UPDATE_1
#undef UPDATE_1_CHECK
#undef kAlignTableSize
#undef kBadRepCode
#undef kBitModelTotal
#undef kBound0
#undef kEndPosModelIndex
#undef kLenNumHighBits
#undef kLenNumHighSymbols
#undef kLenNumLowBits
#undef kLenNumLowSymbols
#undef kMatchMinLen
#undef kMatchSpecLenStart
#undef kMatchSpecLen_Error_Data
#undef kMatchSpecLen_Error_Fail
#undef kNumAlignBits
#undef kNumBitModelTotalBits
#undef kNumFullDistances
#undef kNumLenProbs
#undef kNumLenToPosStates
#undef kNumLitStates
#undef kNumMoveBits
#undef kNumPosBitsMax
#undef kNumPosSlotBits
#undef kNumPosStatesMax
#undef kNumStates
#undef kNumStates2
#undef kRange0
#undef kStartOffset
#undef kStartPosModelIndex
#undef kTopValue

/* ==== Ppmd7.c ==== */
/* Ppmd7.c -- PPMdH codec
2023-09-07 : Igor Pavlov : Public domain
This code is based on PPMd var.H (2001): Dmitry Shkarin : Public domain */



#include <string.h>



/* define PPMD7_ORDER_0_SUPPPORT to suport order-0 mode, unsupported by orignal PPMd var.H. code */
/* #define PPMD7_ORDER_0_SUPPPORT */
 
MY_ALIGN(16)
static const Byte PPMD7_kExpEscape[16] = { 25, 14, 9, 7, 5, 5, 4, 4, 4, 3, 3, 3, 2, 2, 2, 2 };
MY_ALIGN(16)
static const UInt16 PPMD7_kInitBinEsc[] = { 0x3CDD, 0x1F3F, 0x59BF, 0x48F3, 0x64A1, 0x5ABC, 0x6632, 0x6051};

#define MAX_FREQ 124
#define UNIT_SIZE 12

#define U2B(nu) ((UInt32)(nu) * UNIT_SIZE)
#define U2I(nu) (p->Units2Indx[(size_t)(nu) - 1])
#define I2U(indx) ((unsigned)p->Indx2Units[indx])
#define I2U_UInt16(indx) ((UInt16)p->Indx2Units[indx])

#define REF(ptr) Ppmd_GetRef(p, ptr)

#define STATS_REF(ptr) ((CPpmd_State_Ref)REF(ptr))

#define CTX(ref) ((CPpmd7_Context *)Ppmd7_GetContext(p, ref))
#define STATS(ctx) Ppmd7_GetStats(p, ctx)
#define ONE_STATE(ctx) Ppmd7Context_OneState(ctx)
#define SUFFIX(ctx) CTX((ctx)->Suffix)

typedef CPpmd7_Context * PPMD7_CTX_PTR;

struct CPpmd7_Node_;

typedef Ppmd_Ref_Type(struct CPpmd7_Node_) CPpmd7_Node_Ref;

typedef struct CPpmd7_Node_
{
  UInt16 Stamp; /* must be at offset 0 as CPpmd7_Context::NumStats. Stamp=0 means free */
  UInt16 NU;
  CPpmd7_Node_Ref Next; /* must be at offset >= 4 */
  CPpmd7_Node_Ref Prev;
} CPpmd7_Node;

#define NODE(r)  Ppmd_GetPtr_Type(p, r, CPpmd7_Node)

void Ppmd7_Construct(CPpmd7 *p)
{
  unsigned i, k, m;

  p->Base = NULL;

  for (i = 0, k = 0; i < PPMD_NUM_INDEXES; i++)
  {
    unsigned step = (i >= 12 ? 4 : (i >> 2) + 1);
    do { p->Units2Indx[k++] = (Byte)i; } while (--step);
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

  memcpy(p->ExpEscape, PPMD7_kExpEscape, 16);
}


void Ppmd7_Free(CPpmd7 *p, ISzAllocPtr alloc)
{
  ISzAlloc_Free(alloc, p->Base);
  p->Size = 0;
  p->Base = NULL;
}


BoolInt Ppmd7_Alloc(CPpmd7 *p, UInt32 size, ISzAllocPtr alloc)
{
  if (!p->Base || p->Size != size)
  {
    Ppmd7_Free(p, alloc);
    p->AlignOffset = (4 - size) & 3;
    if ((p->Base = (Byte *)ISzAlloc_Alloc(alloc, p->AlignOffset + size)) == NULL)
      return False;
    p->Size = size;
  }
  return True;
}



/* ---------- Internal Memory Allocator ---------- */

/* We can use CPpmd7_Node in list of free units (as in Ppmd8)
   But we still need one additional list walk pass in Ppmd7_GlueFreeBlocks().
   So we use simple CPpmd_Void_Ref instead of CPpmd7_Node in Ppmd7_InsertNode() / Ppmd7_RemoveNode()
*/

#define EMPTY_NODE 0


static void Ppmd7_InsertNode(CPpmd7 *p, void *node, unsigned indx)
{
  *((CPpmd_Void_Ref *)node) = p->FreeList[indx];
  /* ((CPpmd7_Node *)node)->Next = (CPpmd7_Node_Ref)p->FreeList[indx]; */

  p->FreeList[indx] = REF(node);

}


static void *Ppmd7_RemoveNode(CPpmd7 *p, unsigned indx)
{
  CPpmd_Void_Ref *node = (CPpmd_Void_Ref *)Ppmd7_GetPtr(p, p->FreeList[indx]);
  p->FreeList[indx] = *node;
  /* CPpmd7_Node *node = NODE((CPpmd7_Node_Ref)p->FreeList[indx]); */
  /* p->FreeList[indx] = node->Next; */
  return node;
}


static void Ppmd7_SplitBlock(CPpmd7 *p, void *ptr, unsigned oldIndx, unsigned newIndx)
{
  unsigned i, nu = I2U(oldIndx) - I2U(newIndx);
  ptr = (Byte *)ptr + U2B(I2U(newIndx));
  if (I2U(i = U2I(nu)) != nu)
  {
    unsigned k = I2U(--i);
    Ppmd7_InsertNode(p, ((Byte *)ptr) + U2B(k), nu - k - 1);
  }
  Ppmd7_InsertNode(p, ptr, i);
}


/* we use CPpmd7_Node_Union union to solve XLC -O2 strict pointer aliasing problem */

typedef union
{
  CPpmd7_Node     Node;
  CPpmd7_Node_Ref NextRef;
} CPpmd7_Node_Union;

/* Original PPmdH (Ppmd7) code uses doubly linked list in Ppmd7_GlueFreeBlocks()
   we use single linked list similar to Ppmd8 code */


static void Ppmd7_GlueFreeBlocks(CPpmd7 *p)
{
  /*
  we use first UInt16 field of 12-bytes UNITs as record type stamp
    CPpmd_State    { Byte Symbol; Byte Freq; : Freq != 0
    CPpmd7_Context { UInt16 NumStats;        : NumStats != 0
    CPpmd7_Node    { UInt16 Stamp            : Stamp == 0 for free record
                                             : Stamp == 1 for head record and guard
    Last 12-bytes UNIT in array is always contains 12-bytes order-0 CPpmd7_Context record.
  */
  CPpmd7_Node_Ref head, n = 0;
 
  p->GlueCount = 255;

  
  /* we set guard NODE at LoUnit */
  if (p->LoUnit != p->HiUnit)
    ((CPpmd7_Node *)(void *)p->LoUnit)->Stamp = 1;

  {
    /* Create list of free blocks.
       We still need one additional list walk pass before Glue. */
    unsigned i;
    for (i = 0; i < PPMD_NUM_INDEXES; i++)
    {
      const UInt16 nu = I2U_UInt16(i);
      CPpmd7_Node_Ref next = (CPpmd7_Node_Ref)p->FreeList[i];
      p->FreeList[i] = 0;
      while (next != 0)
      {
        /* Don't change the order of the following commands: */
        CPpmd7_Node_Union *un = (CPpmd7_Node_Union *)NODE(next);
        const CPpmd7_Node_Ref tmp = next;
        next = un->NextRef;
        un->Node.Stamp = EMPTY_NODE;
        un->Node.NU = nu;
        un->Node.Next = n;
        n = tmp;
      }
    }
  }

  head = n;
  /* Glue and Fill must walk the list in same direction */
  {
    /* Glue free blocks */
    CPpmd7_Node_Ref *prev = &head;
    while (n)
    {
      CPpmd7_Node *node = NODE(n);
      UInt32 nu = node->NU;
      n = node->Next;
      if (nu == 0)
      {
        *prev = n;
        continue;
      }
      prev = &node->Next;
      for (;;)
      {
        CPpmd7_Node *node2 = node + nu;
        nu += node2->NU;
        if (node2->Stamp != EMPTY_NODE || nu >= 0x10000)
          break;
        node->NU = (UInt16)nu;
        node2->NU = 0;
      }
    }
  }

  /* Fill lists of free blocks */
  for (n = head; n != 0;)
  {
    CPpmd7_Node *node = NODE(n);
    UInt32 nu = node->NU;
    unsigned i;
    n = node->Next;
    if (nu == 0)
      continue;
    for (; nu > 128; nu -= 128, node += 128)
      Ppmd7_InsertNode(p, node, PPMD_NUM_INDEXES - 1);
    if (I2U(i = U2I(nu)) != nu)
    {
      unsigned k = I2U(--i);
      Ppmd7_InsertNode(p, node + k, (unsigned)nu - k - 1);
    }
    Ppmd7_InsertNode(p, node, i);
  }
}


Z7_NO_INLINE
static void *Ppmd7_AllocUnitsRare(CPpmd7 *p, unsigned indx)
{
  unsigned i;
  
  if (p->GlueCount == 0)
  {
    Ppmd7_GlueFreeBlocks(p);
    if (p->FreeList[indx] != 0)
      return Ppmd7_RemoveNode(p, indx);
  }
  
  i = indx;
  
  do
  {
    if (++i == PPMD_NUM_INDEXES)
    {
      UInt32 numBytes = U2B(I2U(indx));
      Byte *us = p->UnitsStart;
      p->GlueCount--;
      return ((UInt32)(us - p->Text) > numBytes) ? (p->UnitsStart = us - numBytes) : NULL;
    }
  }
  while (p->FreeList[i] == 0);

  {
    void *block = Ppmd7_RemoveNode(p, i);
    Ppmd7_SplitBlock(p, block, i, indx);
    return block;
  }
}


static void *Ppmd7_AllocUnits(CPpmd7 *p, unsigned indx)
{
  if (p->FreeList[indx] != 0)
    return Ppmd7_RemoveNode(p, indx);
  {
    UInt32 numBytes = U2B(I2U(indx));
    Byte *lo = p->LoUnit;
    if ((UInt32)(p->HiUnit - lo) >= numBytes)
    {
      p->LoUnit = lo + numBytes;
      return lo;
    }
  }
  return Ppmd7_AllocUnitsRare(p, indx);
}


#define MEM_12_CPY(dest, src, num) \
  { UInt32 *d = (UInt32 *)(dest); \
    const UInt32 *z = (const UInt32 *)(src); \
    unsigned n = (num); \
    do { \
      d[0] = z[0]; \
      d[1] = z[1]; \
      d[2] = z[2]; \
      z += 3; \
      d += 3; \
    } while (--n); \
  }


/*
static void *ShrinkUnits(CPpmd7 *p, void *oldPtr, unsigned oldNU, unsigned newNU)
{
  unsigned i0 = U2I(oldNU);
  unsigned i1 = U2I(newNU);
  if (i0 == i1)
    return oldPtr;
  if (p->FreeList[i1] != 0)
  {
    void *ptr = Ppmd7_RemoveNode(p, i1);
    MEM_12_CPY(ptr, oldPtr, newNU)
    Ppmd7_InsertNode(p, oldPtr, i0);
    return ptr;
  }
  Ppmd7_SplitBlock(p, oldPtr, i0, i1);
  return oldPtr;
}
*/


#define SUCCESSOR(p) Ppmd_GET_SUCCESSOR(p)
static void SetSuccessor(CPpmd_State *p, CPpmd_Void_Ref v)
{
  Ppmd_SET_SUCCESSOR(p, v)
}



Z7_NO_INLINE
static
void Ppmd7_RestartModel(CPpmd7 *p)
{
  unsigned i, k;

  memset(p->FreeList, 0, sizeof(p->FreeList));
  
  p->Text = p->Base + p->AlignOffset;
  p->HiUnit = p->Text + p->Size;
  p->LoUnit = p->UnitsStart = p->HiUnit - p->Size / 8 / UNIT_SIZE * 7 * UNIT_SIZE;
  p->GlueCount = 0;

  p->OrderFall = p->MaxOrder;
  p->RunLength = p->InitRL = -(Int32)((p->MaxOrder < 12) ? p->MaxOrder : 12) - 1;
  p->PrevSuccess = 0;

  {
    CPpmd7_Context *mc = (PPMD7_CTX_PTR)(void *)(p->HiUnit -= UNIT_SIZE); /* AllocContext(p); */
    CPpmd_State *s = (CPpmd_State *)p->LoUnit; /* Ppmd7_AllocUnits(p, PPMD_NUM_INDEXES - 1); */
    
    p->LoUnit += U2B(256 / 2);
    p->MaxContext = p->MinContext = mc;
    p->FoundState = s;

    mc->NumStats = 256;
    mc->Union2.SummFreq = 256 + 1;
    mc->Union4.Stats = REF(s);
    mc->Suffix = 0;

    for (i = 0; i < 256; i++, s++)
    {
      s->Symbol = (Byte)i;
      s->Freq = 1;
      SetSuccessor(s, 0);
    }

    #ifdef PPMD7_ORDER_0_SUPPPORT
    if (p->MaxOrder == 0)
    {
      CPpmd_Void_Ref r = REF(mc);
      s = p->FoundState;
      for (i = 0; i < 256; i++, s++)
        SetSuccessor(s, r);
      return;
    }
    #endif
  }

  for (i = 0; i < 128; i++)
    
    
    
    for (k = 0; k < 8; k++)
    {
      unsigned m;
      UInt16 *dest = p->BinSumm[i] + k;
      const UInt16 val = (UInt16)(PPMD_BIN_SCALE - PPMD7_kInitBinEsc[k] / (i + 2));
      for (m = 0; m < 64; m += 8)
        dest[m] = val;
    }

    
  for (i = 0; i < 25; i++)
  {

    CPpmd_See *s = p->See[i];
    
    
    
    unsigned summ = ((5 * i + 10) << (PPMD_PERIOD_BITS - 4));
    for (k = 0; k < 16; k++, s++)
    {
      s->Summ = (UInt16)summ;
      s->Shift = (PPMD_PERIOD_BITS - 4);
      s->Count = 4;
    }
  }
  
  p->DummySee.Summ = 0; /* unused */
  p->DummySee.Shift = PPMD_PERIOD_BITS;
  p->DummySee.Count = 64; /* unused */
}


void Ppmd7_Init(CPpmd7 *p, unsigned maxOrder)
{
  p->MaxOrder = maxOrder;

  Ppmd7_RestartModel(p);
}



/*
  Ppmd7_CreateSuccessors()
  It's called when (FoundState->Successor) is RAW-Successor,
  that is the link to position in Raw text.
  So we create Context records and write the links to
  FoundState->Successor and to identical RAW-Successors in suffix
  contexts of MinContex.
  
  The function returns:
  if (OrderFall == 0) then MinContext is already at MAX order,
    { return pointer to new or existing context of same MAX order }
  else
    { return pointer to new real context that will be (Order+1) in comparison with MinContext

  also it can return pointer to real context of same order,
*/

Z7_NO_INLINE
static PPMD7_CTX_PTR Ppmd7_CreateSuccessors(CPpmd7 *p)
{
  PPMD7_CTX_PTR c = p->MinContext;
  CPpmd_Byte_Ref upBranch = (CPpmd_Byte_Ref)SUCCESSOR(p->FoundState);
  Byte newSym, newFreq;
  unsigned numPs = 0;
  CPpmd_State *ps[PPMD7_MAX_ORDER];

  if (p->OrderFall != 0)
    ps[numPs++] = p->FoundState;
  
  while (c->Suffix)
  {
    CPpmd_Void_Ref successor;
    CPpmd_State *s;
    c = SUFFIX(c);
    

    if (c->NumStats != 1)
    {
      Byte sym = p->FoundState->Symbol;
      for (s = STATS(c); s->Symbol != sym; s++);

    }
    else
    {
      s = ONE_STATE(c);

    }
    successor = SUCCESSOR(s);
    if (successor != upBranch)
    {
      /* (c) is real record Context here, */
      c = CTX(successor);
      if (numPs == 0)
      {
        /* (c) is real record MAX Order Context here, */
        /* So we don't need to create any new contexts. */
        return c;
      }
      break;
    }
    ps[numPs++] = s;
  }
  
  /* All created contexts will have single-symbol with new RAW-Successor */
  /* All new RAW-Successors will point to next position in RAW text */
  /* after FoundState->Successor */

  newSym = *(const Byte *)Ppmd7_GetPtr(p, upBranch);
  upBranch++;
  
  
  if (c->NumStats == 1)
    newFreq = ONE_STATE(c)->Freq;
  else
  {
    UInt32 cf, s0;
    CPpmd_State *s;
    for (s = STATS(c); s->Symbol != newSym; s++);
    cf = (UInt32)s->Freq - 1;
    s0 = (UInt32)c->Union2.SummFreq - c->NumStats - cf;
    /*
      cf - is frequency of symbol that will be Successor in new context records.
      s0 - is commulative frequency sum of another symbols from parent context.
      max(newFreq)= (s->Freq + 1), when (s0 == 1)
      we have requirement (Ppmd7Context_OneState()->Freq <= 128) in BinSumm[]
      so (s->Freq < 128) - is requirement for multi-symbol contexts
    */
    newFreq = (Byte)(1 + ((2 * cf <= s0) ? (5 * cf > s0) : (2 * cf + s0 - 1) / (2 * s0) + 1));
  }

  /* Create new single-symbol contexts from low order to high order in loop */

  do
  {
    PPMD7_CTX_PTR c1;
    /* = AllocContext(p); */
    if (p->HiUnit != p->LoUnit)
      c1 = (PPMD7_CTX_PTR)(void *)(p->HiUnit -= UNIT_SIZE);
    else if (p->FreeList[0] != 0)
      c1 = (PPMD7_CTX_PTR)Ppmd7_RemoveNode(p, 0);
    else
    {
      c1 = (PPMD7_CTX_PTR)Ppmd7_AllocUnitsRare(p, 0);
      if (!c1)
        return NULL;
    }
    
    c1->NumStats = 1;
    ONE_STATE(c1)->Symbol = newSym;
    ONE_STATE(c1)->Freq = newFreq;
    SetSuccessor(ONE_STATE(c1), upBranch);
    c1->Suffix = REF(c);
    SetSuccessor(ps[--numPs], REF(c1));
    c = c1;
  }
  while (numPs != 0);
  
  return c;
}



#define SWAP_STATES(s) \
  { CPpmd_State tmp = s[0]; s[0] = s[-1]; s[-1] = tmp; }


void Ppmd7_UpdateModel(CPpmd7 *p);
Z7_NO_INLINE
void Ppmd7_UpdateModel(CPpmd7 *p)
{
  CPpmd_Void_Ref maxSuccessor, minSuccessor;
  PPMD7_CTX_PTR c, mc;
  unsigned s0, ns;



  if (p->FoundState->Freq < MAX_FREQ / 4 && p->MinContext->Suffix != 0)
  {
    /* Update Freqs in Suffix Context */

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
      Byte sym = p->FoundState->Symbol;
      
      if (s->Symbol != sym)
      {
        do
        {
          /* s++; if (s->Symbol == sym) break; */
          s++;
        }
        while (s->Symbol != sym);
        
        if (s[0].Freq >= s[-1].Freq)
        {
          SWAP_STATES(s)
          s--;
        }
      }

      if (s->Freq < MAX_FREQ - 9)
      {
        s->Freq = (Byte)(s->Freq + 2);
        c->Union2.SummFreq = (UInt16)(c->Union2.SummFreq + 2);
      }
    }
  }

  
  if (p->OrderFall == 0)
  {
    /* MAX ORDER context */
    /* (FoundState->Successor) is RAW-Successor. */
    p->MaxContext = p->MinContext = Ppmd7_CreateSuccessors(p);
    if (!p->MinContext)
    {
      Ppmd7_RestartModel(p);
      return;
    }
    SetSuccessor(p->FoundState, REF(p->MinContext));
    return;
  }

  
  /* NON-MAX ORDER context */
  
  {
    Byte *text = p->Text;
    *text++ = p->FoundState->Symbol;
    p->Text = text;
    if (text >= p->UnitsStart)
    {
      Ppmd7_RestartModel(p);
      return;
    }
    maxSuccessor = REF(text);
  }
  
  minSuccessor = SUCCESSOR(p->FoundState);

  if (minSuccessor)
  {
    /* there is Successor for FoundState in MinContext. */
    /* So the next context will be one order higher than MinContext. */
    
    if (minSuccessor <= maxSuccessor)
    {
      /* minSuccessor is RAW-Successor. So we will create real contexts records: */
      PPMD7_CTX_PTR cs = Ppmd7_CreateSuccessors(p);
      if (!cs)
      {
        Ppmd7_RestartModel(p);
        return;
      }
      minSuccessor = REF(cs);
    }

    /* minSuccessor now is real Context pointer that points to existing (Order+1) context */
    
    if (--p->OrderFall == 0)
    {
      /*
      if we move to MaxOrder context, then minSuccessor will be common Succesor for both:
        MinContext that is (MaxOrder - 1)
        MaxContext that is (MaxOrder)
      so we don't need new RAW-Successor, and we can use real minSuccessor
      as succssors for both MinContext and MaxContext.
      */
      maxSuccessor = minSuccessor;
      
      /*
      if (MaxContext != MinContext)
      {
        there was order fall from MaxOrder and we don't need current symbol
        to transfer some RAW-Succesors to real contexts.
        So we roll back pointer in raw data for one position.
      }
      */
      p->Text -= (p->MaxContext != p->MinContext);
    }
  }
  else
  {
    /*
    FoundState has NULL-Successor here.
    And only root 0-order context can contain NULL-Successors.
    We change Successor in FoundState to RAW-Successor,
    And next context will be same 0-order root Context.
    */
    SetSuccessor(p->FoundState, maxSuccessor);
    minSuccessor = REF(p->MinContext);
  }

  mc = p->MinContext;
  c = p->MaxContext;

  p->MaxContext = p->MinContext = CTX(minSuccessor);

  if (c == mc)
    return;

  /* s0 : is pure Escape Freq */
  s0 = mc->Union2.SummFreq - (ns = mc->NumStats) - ((unsigned)p->FoundState->Freq - 1);

  do
  {
    unsigned ns1;
    UInt32 sum;
    
    if ((ns1 = c->NumStats) != 1)
    {
      if ((ns1 & 1) == 0)
      {
        /* Expand for one UNIT */
        const unsigned oldNU = ns1 >> 1;
        const unsigned i = U2I(oldNU);
        if (i != U2I((size_t)oldNU + 1))
        {
          void *ptr = Ppmd7_AllocUnits(p, i + 1);
          void *oldPtr;
          if (!ptr)
          {
            Ppmd7_RestartModel(p);
            return;
          }
          oldPtr = STATS(c);
          MEM_12_CPY(ptr, oldPtr, oldNU)
          Ppmd7_InsertNode(p, oldPtr, i);
          c->Union4.Stats = STATS_REF(ptr);
        }
      }
      sum = c->Union2.SummFreq;
      /* max increase of Escape_Freq is 3 here.
         total increase of Union2.SummFreq for all symbols is less than 256 here */
      sum += (UInt32)(unsigned)((2 * ns1 < ns) + 2 * ((unsigned)(4 * ns1 <= ns) & (sum <= 8 * ns1)));
      /* original PPMdH uses 16-bit variable for (sum) here.
         But (sum < 0x9000). So we don't truncate (sum) to 16-bit */
      /* sum = (UInt16)sum; */
    }
    else
    {
      /* instead of One-symbol context we create 2-symbol context */
      CPpmd_State *s = (CPpmd_State*)Ppmd7_AllocUnits(p, 0);
      if (!s)
      {
        Ppmd7_RestartModel(p);
        return;
      }
      {
        unsigned freq = c->Union2.State2.Freq;
        /* s = *ONE_STATE(c); */
        s->Symbol = c->Union2.State2.Symbol;
        s->Successor_0 = c->Union4.State4.Successor_0;
        s->Successor_1 = c->Union4.State4.Successor_1;
        /* SetSuccessor(s, c->Union4.Stats);  // call it only for debug purposes to check the order of */
                                              /* (Successor_0 and Successor_1) in LE/BE. */
        c->Union4.Stats = REF(s);
        if (freq < MAX_FREQ / 4 - 1)
          freq <<= 1;
        else
          freq = MAX_FREQ - 4;
        /* (max(s->freq) == 120), when we convert from 1-symbol into 2-symbol context */
        s->Freq = (Byte)freq;
        /* max(InitEsc = PPMD7_kExpEscape[*]) is 25. So the max(escapeFreq) is 26 here */
        sum = (UInt32)(freq + p->InitEsc + (ns > 3));
      }
    }
    
    {
      CPpmd_State *s = STATS(c) + ns1;
      UInt32 cf = 2 * (sum + 6) * (UInt32)p->FoundState->Freq;
      UInt32 sf = (UInt32)s0 + sum;
      s->Symbol = p->FoundState->Symbol;
      c->NumStats = (UInt16)(ns1 + 1);
      SetSuccessor(s, maxSuccessor);
      
      if (cf < 6 * sf)
      {
        cf = (UInt32)1 + (cf > sf) + (cf >= 4 * sf);
        sum += 3;
        /* It can add (0, 1, 2) to Escape_Freq */
      }
      else
      {
        cf = (UInt32)4 + (cf >= 9 * sf) + (cf >= 12 * sf) + (cf >= 15 * sf);
        sum += cf;
      }
     
      c->Union2.SummFreq = (UInt16)sum;
      s->Freq = (Byte)cf;
    }
    c = SUFFIX(c);
  }
  while (c != mc);
}
  


Z7_NO_INLINE
static void Ppmd7_Rescale(CPpmd7 *p)
{
  unsigned i, adder, sumFreq, escFreq;
  CPpmd_State *stats = STATS(p->MinContext);
  CPpmd_State *s = p->FoundState;

  /* Sort the list by Freq */
  if (s != stats)
  {
    CPpmd_State tmp = *s;
    do
      s[0] = s[-1];
    while (--s != stats);
    *s = tmp;
  }

  sumFreq = s->Freq;
  escFreq = p->MinContext->Union2.SummFreq - sumFreq;
  
  /*
  if (p->OrderFall == 0), adder = 0 : it's     allowed to remove symbol from     MAX Order context
  if (p->OrderFall != 0), adder = 1 : it's NOT allowed to remove symbol from NON-MAX Order context
  */

  adder = (p->OrderFall != 0);

  #ifdef PPMD7_ORDER_0_SUPPPORT
  adder |= (p->MaxOrder == 0); /* we don't remove symbols from order-0 context */
  #endif

  sumFreq = (sumFreq + 4 + adder) >> 1;
  i = (unsigned)p->MinContext->NumStats - 1;
  s->Freq = (Byte)sumFreq;
  
  do
  {
    unsigned freq = (++s)->Freq;
    escFreq -= freq;
    freq = (freq + adder) >> 1;
    sumFreq += freq;
    s->Freq = (Byte)freq;
    if (freq > s[-1].Freq)
    {
      CPpmd_State tmp = *s;
      CPpmd_State *s1 = s;
      do
      {
        s1[0] = s1[-1];
      }
      while (--s1 != stats && freq > s1[-1].Freq);
      *s1 = tmp;
    }
  }
  while (--i);
  
  if (s->Freq == 0)
  {
    /* Remove all items with Freq == 0 */
    CPpmd7_Context *mc;
    unsigned numStats, numStatsNew, n0, n1;
    
    i = 0; do { i++; } while ((--s)->Freq == 0);
    
    /* We increase (escFreq) for the number of removed symbols.
       So we will have (0.5) increase for Escape_Freq in avarage per
       removed symbol after Escape_Freq halving */
    escFreq += i;
    mc = p->MinContext;
    numStats = mc->NumStats;
    numStatsNew = numStats - i;
    mc->NumStats = (UInt16)(numStatsNew);
    n0 = (numStats + 1) >> 1;
    
    if (numStatsNew == 1)
    {
      /* Create Single-Symbol context */
      unsigned freq = stats->Freq;
      
      do
      {
        escFreq >>= 1;
        freq = (freq + 1) >> 1;
      }
      while (escFreq > 1);

      s = ONE_STATE(mc);
      *s = *stats;
      s->Freq = (Byte)freq; /* (freq <= 260 / 4) */
      p->FoundState = s;
      Ppmd7_InsertNode(p, stats, U2I(n0));
      return;
    }
    
    n1 = (numStatsNew + 1) >> 1;
    if (n0 != n1)
    {
      /* p->MinContext->Union4.Stats = STATS_REF(ShrinkUnits(p, stats, n0, n1)); */
      unsigned i0 = U2I(n0);
      unsigned i1 = U2I(n1);
      if (i0 != i1)
      {
        if (p->FreeList[i1] != 0)
        {
          void *ptr = Ppmd7_RemoveNode(p, i1);
          p->MinContext->Union4.Stats = STATS_REF(ptr);
          MEM_12_CPY(ptr, (const void *)stats, n1)
          Ppmd7_InsertNode(p, stats, i0);
        }
        else
          Ppmd7_SplitBlock(p, stats, i0, i1);
      }
    }
  }
  {
    CPpmd7_Context *mc = p->MinContext;
    mc->Union2.SummFreq = (UInt16)(sumFreq + escFreq - (escFreq >> 1));
    /* Escape_Freq halving here */
    p->FoundState = STATS(mc);
  }
}


CPpmd_See *Ppmd7_MakeEscFreq(CPpmd7 *p, unsigned numMasked, UInt32 *escFreq)
{
  CPpmd_See *see;
  const CPpmd7_Context *mc = p->MinContext;
  unsigned numStats = mc->NumStats;
  if (numStats != 256)
  {
    unsigned nonMasked = numStats - numMasked;
    see = p->See[(unsigned)p->NS2Indx[(size_t)nonMasked - 1]]
        + (nonMasked < (unsigned)SUFFIX(mc)->NumStats - numStats)
        + 2 * (unsigned)(mc->Union2.SummFreq < 11 * numStats)
        + 4 * (unsigned)(numMasked > nonMasked) +
        p->HiBitsFlag;
    {
      /* if (see->Summ) field is larger than 16-bit, we need only low 16 bits of Summ */
      const unsigned summ = (UInt16)see->Summ; /* & 0xFFFF */
      const unsigned r = (summ >> see->Shift);
      see->Summ = (UInt16)(summ - r);
      *escFreq = (UInt32)(r + (r == 0));
    }
  }
  else
  {
    see = &p->DummySee;
    *escFreq = 1;
  }
  return see;
}


static void Ppmd7_NextContext(CPpmd7 *p)
{
  PPMD7_CTX_PTR c = CTX(SUCCESSOR(p->FoundState));
  if (p->OrderFall == 0 && (const Byte *)c > p->Text)
    p->MaxContext = p->MinContext = c;
  else
    Ppmd7_UpdateModel(p);
}


void Ppmd7_Update1(CPpmd7 *p)
{
  CPpmd_State *s = p->FoundState;
  unsigned freq = s->Freq;
  freq += 4;
  p->MinContext->Union2.SummFreq = (UInt16)(p->MinContext->Union2.SummFreq + 4);
  s->Freq = (Byte)freq;
  if (freq > s[-1].Freq)
  {
    SWAP_STATES(s)
    p->FoundState = --s;
    if (freq > MAX_FREQ)
      Ppmd7_Rescale(p);
  }
  Ppmd7_NextContext(p);
}


void Ppmd7_Update1_0(CPpmd7 *p)
{
  CPpmd_State *s = p->FoundState;
  CPpmd7_Context *mc = p->MinContext;
  unsigned freq = s->Freq;
  const unsigned summFreq = mc->Union2.SummFreq;
  p->PrevSuccess = (2 * freq > summFreq);
  p->RunLength += (Int32)p->PrevSuccess;
  mc->Union2.SummFreq = (UInt16)(summFreq + 4);
  freq += 4;
  s->Freq = (Byte)freq;
  if (freq > MAX_FREQ)
    Ppmd7_Rescale(p);
  Ppmd7_NextContext(p);
}


/*
void Ppmd7_UpdateBin(CPpmd7 *p)
{
  unsigned freq = p->FoundState->Freq;
  p->FoundState->Freq = (Byte)(freq + (freq < 128));
  p->PrevSuccess = 1;
  p->RunLength++;
  Ppmd7_NextContext(p);
}
*/

void Ppmd7_Update2(CPpmd7 *p)
{
  CPpmd_State *s = p->FoundState;
  unsigned freq = s->Freq;
  freq += 4;
  p->RunLength = p->InitRL;
  p->MinContext->Union2.SummFreq = (UInt16)(p->MinContext->Union2.SummFreq + 4);
  s->Freq = (Byte)freq;
  if (freq > MAX_FREQ)
    Ppmd7_Rescale(p);
  Ppmd7_UpdateModel(p);
}



/*
PPMd Memory Map:
{
  [ 0 ]           contains subset of original raw text, that is required to create context
                  records, Some symbols are not written, when max order context was reached
  [ Text ]        free area
  [ UnitsStart ]  CPpmd_State vectors and CPpmd7_Context records
  [ LoUnit ]      free  area for CPpmd_State and CPpmd7_Context items
[ HiUnit ]      CPpmd7_Context records
  [ Size ]        end of array
}

These addresses don't cross at any time.
And the following condtions is true for addresses:
  (0  <= Text < UnitsStart <= LoUnit <= HiUnit <= Size)

Raw text is BYTE--aligned.
the data in block [ UnitsStart ... Size ] contains 12-bytes aligned UNITs.

Last UNIT of array at offset (Size - 12) is root order-0 CPpmd7_Context record.
The code can free UNITs memory blocks that were allocated to store CPpmd_State vectors.
The code doesn't free UNITs allocated for CPpmd7_Context records.

The code calls Ppmd7_RestartModel(), when there is no free memory for allocation.
And Ppmd7_RestartModel() changes the state to orignal start state, with full free block.


The code allocates UNITs with the following order:

Allocation of 1 UNIT for Context record
  - from free space (HiUnit) down to (LoUnit)
  - from FreeList[0]
  - Ppmd7_AllocUnitsRare()

Ppmd7_AllocUnits() for CPpmd_State vectors:
  - from FreeList[i]
  - from free space (LoUnit) up to (HiUnit)
  - Ppmd7_AllocUnitsRare()

Ppmd7_AllocUnitsRare()
  - if (GlueCount == 0)
       {  Glue lists, GlueCount = 255, allocate from FreeList[i]] }
  - loop for all higher sized FreeList[...] lists
  - from (UnitsStart - Text), GlueCount--
  - ERROR


Each Record with Context contains the CPpmd_State vector, where each
CPpmd_State contains the link to Successor.
There are 3 types of Successor:
  1) NULL-Successor   - NULL pointer. NULL-Successor links can be stored
                        only in 0-order Root Context Record.
                        We use 0 value as NULL-Successor
  2) RAW-Successor    - the link to position in raw text,
                        that "RAW-Successor" is being created after first
                        occurrence of new symbol for some existing context record.
                        (RAW-Successor > 0).
  3) RECORD-Successor - the link to CPpmd7_Context record of (Order+1),
                        that record is being created when we go via RAW-Successor again.

For any successors at any time: the following condtions are true for Successor links:
(NULL-Successor < RAW-Successor < UnitsStart <= RECORD-Successor)


---------- Symbol Frequency, SummFreq and Range in Range_Coder ----------

CPpmd7_Context::SummFreq = Sum(Stats[].Freq) + Escape_Freq

The PPMd code tries to fulfill the condition:
  (SummFreq <= (256 * 128 = RC::kBot))

We have (Sum(Stats[].Freq) <= 256 * 124), because of (MAX_FREQ = 124)
So (4 = 128 - 124) is average reserve for Escape_Freq for each symbol.
If (CPpmd_State::Freq) is not aligned for 4, the reserve can be 5, 6 or 7.
SummFreq and Escape_Freq can be changed in Ppmd7_Rescale() and *Update*() functions.
Ppmd7_Rescale() can remove symbols only from max-order contexts. So Escape_Freq can increase after multiple calls of Ppmd7_Rescale() for
max-order context.

When the PPMd code still break (Total <= RC::Range) condition in range coder,
we have two ways to resolve that problem:
  1) we can report error, if we want to keep compatibility with original PPMd code that has no fix for such cases.
  2) we can reduce (Total) value to (RC::Range) by reducing (Escape_Freq) part of (Total) value.
*/

#undef MAX_FREQ
#undef UNIT_SIZE
#undef U2B
#undef U2I
#undef I2U
#undef I2U_UInt16
#undef REF
#undef STATS_REF
#undef CTX
#undef STATS
#undef ONE_STATE
#undef SUFFIX
#undef NODE
#undef EMPTY_NODE
#undef MEM_12_CPY
#undef SUCCESSOR
#undef SWAP_STATES
#undef CTX
#undef EMPTY_NODE
#undef I2U
#undef I2U_UInt16
#undef MAX_FREQ
#undef MEM_12_CPY
#undef NODE
#undef ONE_STATE
#undef REF
#undef STATS
#undef STATS_REF
#undef SUCCESSOR
#undef SUFFIX
#undef SWAP_STATES
#undef U2B
#undef U2I
#undef UNIT_SIZE

/* ==== Ppmd7Dec.c ==== */
/* Ppmd7Dec.c -- Ppmd7z (PPMdH with 7z Range Coder) Decoder
2023-09-07 : Igor Pavlov : Public domain
This code is based on:
  PPMd var.H (2001): Dmitry Shkarin : Public domain */






#define kTopValue ((UInt32)1 << 24)


#define READ_BYTE(p) IByteIn_Read((p)->Stream)

BoolInt Ppmd7z_RangeDec_Init(CPpmd7_RangeDec *p)
{
  unsigned i;
  p->Code = 0;
  p->Range = 0xFFFFFFFF;
  if (READ_BYTE(p) != 0)
    return False;
  for (i = 0; i < 4; i++)
    p->Code = (p->Code << 8) | READ_BYTE(p);
  return (p->Code < 0xFFFFFFFF);
}

#define RC_NORM_BASE(p) if ((p)->Range < kTopValue) \
  { (p)->Code = ((p)->Code << 8) | READ_BYTE(p); (p)->Range <<= 8;

#define RC_NORM_1(p)  RC_NORM_BASE(p) }
#define RC_NORM(p)    RC_NORM_BASE(p) RC_NORM_BASE(p) }}

/* we must use only one type of Normalization from two: LOCAL or REMOTE */
#define RC_NORM_LOCAL(p)    /* RC_NORM(p) */
#define RC_NORM_REMOTE(p)   RC_NORM(p)

#define R (&p->rc.dec)

Z7_FORCE_INLINE
/* Z7_NO_INLINE */
static void Ppmd7z_RD_Decode(CPpmd7 *p, UInt32 start, UInt32 size)
{

  
  R->Code -= start * R->Range;
  R->Range *= size;
  RC_NORM_LOCAL(R)
}

#define RC_Decode(start, size)  Ppmd7z_RD_Decode(p, start, size);
#define RC_DecodeFinal(start, size)  RC_Decode(start, size)  RC_NORM_REMOTE(R)
#define RC_GetThreshold(total)  (R->Code / (R->Range /= (total)))


#define CTX(ref) ((CPpmd7_Context *)Ppmd7_GetContext(p, ref))
/* typedef CPpmd7_Context * CTX_PTR; */
#define SUCCESSOR(p) Ppmd_GET_SUCCESSOR(p)
void Ppmd7_UpdateModel(CPpmd7 *p);

#define MASK(sym)  ((Byte *)charMask)[sym]
/* Z7_FORCE_INLINE */
/* static */
int Ppmd7z_DecodeSymbol(CPpmd7 *p)
{
  size_t charMask[256 / sizeof(size_t)];

  if (p->MinContext->NumStats != 1)
  {
    CPpmd_State *s = Ppmd7_GetStats(p, p->MinContext);
    unsigned i;
    UInt32 count, hiCnt;
    const UInt32 summFreq = p->MinContext->Union2.SummFreq;

    
    
    
    count = RC_GetThreshold(summFreq);
    hiCnt = count;
    
    if ((Int32)(count -= s->Freq) < 0)
    {
      Byte sym;
      RC_DecodeFinal(0, s->Freq)
      p->FoundState = s;
      sym = s->Symbol;
      Ppmd7_Update1_0(p);
      return sym;
    }
  
    p->PrevSuccess = 0;
    i = (unsigned)p->MinContext->NumStats - 1;
    
    do
    {
      if ((Int32)(count -= (++s)->Freq) < 0)
      {
        Byte sym;
        RC_DecodeFinal((hiCnt - count) - s->Freq, s->Freq)
        p->FoundState = s;
        sym = s->Symbol;
        Ppmd7_Update1(p);
        return sym;
      }
    }
    while (--i);
    
    if (hiCnt >= summFreq)
      return PPMD7_SYM_ERROR;
    
    hiCnt -= count;
    RC_Decode(hiCnt, summFreq - hiCnt)

    p->HiBitsFlag = PPMD7_HiBitsFlag_3(p->FoundState->Symbol);
    PPMD_SetAllBitsIn256Bytes(charMask)
    /* i = p->MinContext->NumStats - 1; */
    /* do { MASK((--s)->Symbol) = 0; } while (--i); */
    {
      CPpmd_State *s2 = Ppmd7_GetStats(p, p->MinContext);
      MASK(s->Symbol) = 0;
      do
      {
        const unsigned sym0 = s2[0].Symbol;
        const unsigned sym1 = s2[1].Symbol;
        s2 += 2;
        MASK(sym0) = 0;
        MASK(sym1) = 0;
      }
      while (s2 < s);
    }
  }
  else
  {
    CPpmd_State *s = Ppmd7Context_OneState(p->MinContext);
    UInt16 *prob = Ppmd7_GetBinSumm(p);
    UInt32 pr = *prob;
    UInt32 size0 = (R->Range >> 14) * pr;
    pr = PPMD_UPDATE_PROB_1(pr);

    if (R->Code < size0)
    {
      Byte sym;
      *prob = (UInt16)(pr + (1 << PPMD_INT_BITS));
      
      /* RangeDec_DecodeBit0(size0); */
      R->Range = size0;
      RC_NORM_1(R)
      /* we can use single byte normalization here because of
         (min(BinSumm[][]) = 95) > (1 << (14 - 8)) */

      /* sym = (p->FoundState = Ppmd7Context_OneState(p->MinContext))->Symbol; */
      /* Ppmd7_UpdateBin(p); */
      {
        unsigned freq = s->Freq;
        CPpmd7_Context *c = CTX(SUCCESSOR(s));
        sym = s->Symbol;
        p->FoundState = s;
        p->PrevSuccess = 1;
        p->RunLength++;
        s->Freq = (Byte)(freq + (freq < 128));
        /* NextContext(p); */
        if (p->OrderFall == 0 && (const Byte *)c > p->Text)
          p->MaxContext = p->MinContext = c;
        else
          Ppmd7_UpdateModel(p);
      }
      return sym;
    }

    *prob = (UInt16)pr;
    p->InitEsc = p->ExpEscape[pr >> 10];

    /* RangeDec_DecodeBit1(size0); */
    
    R->Code -= size0;
    R->Range -= size0;
    RC_NORM_LOCAL(R)
    
    PPMD_SetAllBitsIn256Bytes(charMask)
    MASK(Ppmd7Context_OneState(p->MinContext)->Symbol) = 0;
    p->PrevSuccess = 0;
  }

  for (;;)
  {
    CPpmd_State *s, *s2;
    UInt32 freqSum, count, hiCnt;

    CPpmd_See *see;
    CPpmd7_Context *mc;
    unsigned numMasked;
    RC_NORM_REMOTE(R)
    mc = p->MinContext;
    numMasked = mc->NumStats;

    do
    {
      p->OrderFall++;
      if (!mc->Suffix)
        return PPMD7_SYM_END;
      mc = Ppmd7_GetContext(p, mc->Suffix);
    }
    while (mc->NumStats == numMasked);
    
    s = Ppmd7_GetStats(p, mc);

    {
      unsigned num = mc->NumStats;
      unsigned num2 = num / 2;
      
      num &= 1;
      hiCnt = (s->Freq & (UInt32)(MASK(s->Symbol))) & (0 - (UInt32)num);
      s += num;
      p->MinContext = mc;

      do
      {
        const unsigned sym0 = s[0].Symbol;
        const unsigned sym1 = s[1].Symbol;
        s += 2;
        hiCnt += (s[-2].Freq & (UInt32)(MASK(sym0)));
        hiCnt += (s[-1].Freq & (UInt32)(MASK(sym1)));
      }
      while (--num2);
    }

    see = Ppmd7_MakeEscFreq(p, numMasked, &freqSum);
    freqSum += hiCnt;




    count = RC_GetThreshold(freqSum);
    
    if (count < hiCnt)
    {
      Byte sym;

      s = Ppmd7_GetStats(p, p->MinContext);
      hiCnt = count;
      /* count -= s->Freq & (UInt32)(MASK(s->Symbol)); */
      /* if ((Int32)count >= 0) */
      {
        for (;;)
        {
          count -= s->Freq & (UInt32)(MASK((s)->Symbol)); s++; if ((Int32)count < 0) break;
          /* count -= s->Freq & (UInt32)(MASK((s)->Symbol)); s++; if ((Int32)count < 0) break; */
        }
      }
      s--;
      RC_DecodeFinal((hiCnt - count) - s->Freq, s->Freq)

      /* new (see->Summ) value can overflow over 16-bits in some rare cases */
      Ppmd_See_UPDATE(see)
      p->FoundState = s;
      sym = s->Symbol;
      Ppmd7_Update2(p);
      return sym;
    }

    if (count >= freqSum)
      return PPMD7_SYM_ERROR;
    
    RC_Decode(hiCnt, freqSum - hiCnt)

    /* We increase (see->Summ) for sum of Freqs of all non_Masked symbols. */
    /* new (see->Summ) value can overflow over 16-bits in some rare cases */
    see->Summ = (UInt16)(see->Summ + freqSum);

    s = Ppmd7_GetStats(p, p->MinContext);
    s2 = s + p->MinContext->NumStats;
    do
    {
      MASK(s->Symbol) = 0;
      s++;
    }
    while (s != s2);
  }
}

/*
Byte *Ppmd7z_DecodeSymbols(CPpmd7 *p, Byte *buf, const Byte *lim)
{
  int sym = 0;
  if (buf != lim)
  do
  {
    sym = Ppmd7z_DecodeSymbol(p);
    if (sym < 0)
      break;
    *buf = (Byte)sym;
  }
  while (++buf < lim);
  p->LastSymbol = sym;
  return buf;
}
*/

#undef kTopValue
#undef READ_BYTE
#undef RC_NORM_BASE
#undef RC_NORM_1
#undef RC_NORM
#undef RC_NORM_LOCAL
#undef RC_NORM_REMOTE
#undef R
#undef RC_Decode
#undef RC_DecodeFinal
#undef RC_GetThreshold
#undef CTX
#undef SUCCESSOR
#undef MASK
#undef CTX
#undef MASK
#undef R
#undef RC_Decode
#undef RC_DecodeFinal
#undef RC_GetThreshold
#undef RC_NORM
#undef RC_NORM_1
#undef RC_NORM_BASE
#undef RC_NORM_LOCAL
#undef RC_NORM_REMOTE
#undef READ_BYTE
#undef SUCCESSOR
#undef kTopValue

/* ==== Alloc.c ==== */
#define SzAlloc z7_amalg_Alloc_SzAlloc
#define SzFree z7_amalg_Alloc_SzFree
#define g_allocCount z7_amalg_Alloc_g_allocCount
/* Alloc.c -- Memory allocation functions
2024-02-18 : Igor Pavlov : Public domain */



#ifdef _WIN32

#endif
#include <stdlib.h>



#if defined(Z7_LARGE_PAGES) && defined(_WIN32) && \
    (!defined(Z7_WIN32_WINNT_MIN) || Z7_WIN32_WINNT_MIN < 0x0502)  /* < Win2003 (xp-64) */
  #define Z7_USE_DYN_GetLargePageMinimum
#endif

/* for debug: */
#if 0
#if defined(__CHERI__) && defined(__SIZEOF_POINTER__) && (__SIZEOF_POINTER__ == 16)
/* #pragma message("=== Z7_ALLOC_NO_OFFSET_ALLOCATOR === ") */
#define Z7_ALLOC_NO_OFFSET_ALLOCATOR
#endif
#endif

/* #define SZ_ALLOC_DEBUG */
/* #define SZ_ALLOC_DEBUG */

/* use SZ_ALLOC_DEBUG to debug alloc/free operations */
#ifdef SZ_ALLOC_DEBUG

#include <string.h>
#include <stdio.h>
static int g_allocCount = 0;
#ifdef _WIN32
static int g_allocCountMid = 0;
static int g_allocCountBig = 0;
#endif


#define CONVERT_INT_TO_STR(charType, tempSize) \
  char temp[tempSize]; unsigned i = 0; \
  while (val >= 10) { temp[i++] = (char)('0' + (unsigned)(val % 10)); val /= 10; } \
  *s++ = (charType)('0' + (unsigned)val); \
  while (i != 0) { i--; *s++ = temp[i]; } \
  *s = 0;

static void ConvertUInt64ToString(UInt64 val, char *s)
{
  CONVERT_INT_TO_STR(char, 24)
}

#define GET_HEX_CHAR(t) ((char)(((t < 10) ? ('0' + t) : ('A' + (t - 10)))))

static void ConvertUInt64ToHex(UInt64 val, char *s)
{
  UInt64 v = val;
  unsigned i;
  for (i = 1;; i++)
  {
    v >>= 4;
    if (v == 0)
      break;
  }
  s[i] = 0;
  do
  {
    unsigned t = (unsigned)(val & 0xF);
    val >>= 4;
    s[--i] = GET_HEX_CHAR(t);
  }
  while (i);
}

#define DEBUG_OUT_STREAM stderr

static void Print(const char *s)
{
  fputs(s, DEBUG_OUT_STREAM);
}

static void PrintAligned(const char *s, size_t align)
{
  size_t len = strlen(s);
  for(;;)
  {
    fputc(' ', DEBUG_OUT_STREAM);
    if (len >= align)
      break;
    ++len;
  }
  Print(s);
}

static void PrintLn(void)
{
  Print("\n");
}

static void PrintHex(UInt64 v, size_t align)
{
  char s[32];
  ConvertUInt64ToHex(v, s);
  PrintAligned(s, align);
}

static void PrintDec(int v, size_t align)
{
  char s[32];
  ConvertUInt64ToString((unsigned)v, s);
  PrintAligned(s, align);
}

static void PrintAddr(void *p)
{
  PrintHex((UInt64)(size_t)(ptrdiff_t)p, 12);
}


#define PRINT_REALLOC(name, cnt, size, ptr) { \
    Print(name " "); \
    if (!ptr) PrintDec(cnt++, 10); \
    PrintHex(size, 10); \
    PrintAddr(ptr); \
    PrintLn(); }

#define PRINT_ALLOC(name, cnt, size, ptr) { \
    Print(name " "); \
    PrintDec(cnt++, 10); \
    PrintHex(size, 10); \
    PrintAddr(ptr); \
    PrintLn(); }
 
#define PRINT_FREE(name, cnt, ptr) if (ptr) { \
    Print(name " "); \
    PrintDec(--cnt, 10); \
    PrintAddr(ptr); \
    PrintLn(); }
 
#else

#ifdef _WIN32
#define PRINT_ALLOC(name, cnt, size, ptr)
#endif
#define PRINT_FREE(name, cnt, ptr)
#define Print(s)
#define PrintLn()
#ifndef Z7_ALLOC_NO_OFFSET_ALLOCATOR
#define PrintHex(v, align)
#endif
#define PrintAddr(p)

#endif


/*
by specification:
  malloc(non_NULL, 0)   : returns NULL or a unique pointer value that can later be successfully passed to free()
  realloc(NULL, size)   : the call is equivalent to malloc(size)
  realloc(non_NULL, 0)  : the call is equivalent to free(ptr)

in main compilers:
  malloc(0)             : returns non_NULL
  realloc(NULL,     0)  : returns non_NULL
  realloc(non_NULL, 0)  : returns NULL
*/


void *MyAlloc(size_t size)
{
  if (size == 0)
    return NULL;
  /* PRINT_ALLOC("Alloc    ", g_allocCount, size, NULL) */
  #ifdef SZ_ALLOC_DEBUG
  {
    void *p = malloc(size);
    if (p)
    {
      PRINT_ALLOC("Alloc    ", g_allocCount, size, p)
    }
    return p;
  }
  #else
  return malloc(size);
  #endif
}

void MyFree(void *address)
{
  PRINT_FREE("Free    ", g_allocCount, address)
  
  free(address);
}

void *MyRealloc(void *address, size_t size)
{
  if (size == 0)
  {
    MyFree(address);
    return NULL;
  }
  /* PRINT_REALLOC("Realloc  ", g_allocCount, size, address) */
  #ifdef SZ_ALLOC_DEBUG
  {
    void *p = realloc(address, size);
    if (p)
    {
      PRINT_REALLOC("Realloc    ", g_allocCount, size, address)
    }
    return p;
  }
  #else
  return realloc(address, size);
  #endif
}


#ifdef _WIN32

void *MidAlloc(size_t size)
{
  if (size == 0)
    return NULL;
  #ifdef SZ_ALLOC_DEBUG
  {
    void *p = VirtualAlloc(NULL, size, MEM_COMMIT, PAGE_READWRITE);
    if (p)
    {
      PRINT_ALLOC("Alloc-Mid", g_allocCountMid, size, p)
    }
    return p;
  }
  #else
  return VirtualAlloc(NULL, size, MEM_COMMIT, PAGE_READWRITE);
  #endif
}

void MidFree(void *address)
{
  PRINT_FREE("Free-Mid", g_allocCountMid, address)

  if (!address)
    return;
  VirtualFree(address, 0, MEM_RELEASE);
}

#ifdef Z7_LARGE_PAGES

#ifdef MEM_LARGE_PAGES
  #define MY_MEM_LARGE_PAGES  MEM_LARGE_PAGES
#else
  #define MY_MEM_LARGE_PAGES  0x20000000
#endif

extern
SIZE_T g_LargePageSize;
SIZE_T g_LargePageSize = 0;
typedef SIZE_T (WINAPI *Func_GetLargePageMinimum)(VOID);

void SetLargePageSize(void)
{
  SIZE_T size;
#ifdef Z7_USE_DYN_GetLargePageMinimum
Z7_DIAGNOSTIC_IGNORE_CAST_FUNCTION

  const
   Func_GetLargePageMinimum fn =
  (Func_GetLargePageMinimum) Z7_CAST_FUNC_C GetProcAddress(GetModuleHandle(TEXT("kernel32.dll")),
       "GetLargePageMinimum");
  if (!fn)
    return;
  size = fn();
#else
  size = GetLargePageMinimum();
#endif
  if (size == 0 || (size & (size - 1)) != 0)
    return;
  g_LargePageSize = size;
}

#endif /* Z7_LARGE_PAGES */

void *BigAlloc(size_t size)
{
  if (size == 0)
    return NULL;

  PRINT_ALLOC("Alloc-Big", g_allocCountBig, size, NULL)

  #ifdef Z7_LARGE_PAGES
  {
    SIZE_T ps = g_LargePageSize;
    if (ps != 0 && ps <= (1 << 30) && size > (ps / 2))
    {
      size_t size2;
      ps--;
      size2 = (size + ps) & ~ps;
      if (size2 >= size)
      {
        void *p = VirtualAlloc(NULL, size2, MEM_COMMIT | MY_MEM_LARGE_PAGES, PAGE_READWRITE);
        if (p)
        {
          PRINT_ALLOC("Alloc-BM ", g_allocCountMid, size2, p)
          return p;
        }
      }
    }
  }
  #endif

  return MidAlloc(size);
}

void BigFree(void *address)
{
  PRINT_FREE("Free-Big", g_allocCountBig, address)
  MidFree(address);
}

#endif /* _WIN32 */


static void *SzAlloc(ISzAllocPtr p, size_t size) { UNUSED_VAR(p)  return MyAlloc(size); }
static void SzFree(ISzAllocPtr p, void *address) { UNUSED_VAR(p)  MyFree(address); }
const ISzAlloc g_Alloc = { SzAlloc, SzFree };

#ifdef _WIN32
static void *SzMidAlloc(ISzAllocPtr p, size_t size) { UNUSED_VAR(p)  return MidAlloc(size); }
static void SzMidFree(ISzAllocPtr p, void *address) { UNUSED_VAR(p)  MidFree(address); }
static void *SzBigAlloc(ISzAllocPtr p, size_t size) { UNUSED_VAR(p)  return BigAlloc(size); }
static void SzBigFree(ISzAllocPtr p, void *address) { UNUSED_VAR(p)  BigFree(address); }
const ISzAlloc g_MidAlloc = { SzMidAlloc, SzMidFree };
const ISzAlloc g_BigAlloc = { SzBigAlloc, SzBigFree };
#endif

#ifndef Z7_ALLOC_NO_OFFSET_ALLOCATOR

#define ADJUST_ALLOC_SIZE 0
/*
#define ADJUST_ALLOC_SIZE (sizeof(void *) - 1)
*/
/*
  Use (ADJUST_ALLOC_SIZE = (sizeof(void *) - 1)), if
     MyAlloc() can return address that is NOT multiple of sizeof(void *).
*/

/*
  uintptr_t : <stdint.h> C99 (optional)
            : unsupported in VS6
*/
typedef
  #ifdef _WIN32
    UINT_PTR
  /* amalgamated: uintptr_t is C99 and optional; the ptrdiff_t
   * branch below is upstream's own C89 fallback. */
  #elif 0
    uintptr_t
  #else
    ptrdiff_t
  #endif
    MY_uintptr_t;

#if 0 \
    || (defined(__CHERI__) \
    || defined(__SIZEOF_POINTER__) && (__SIZEOF_POINTER__ > 8))
/* for 128-bit pointers (cheri): */
#define MY_ALIGN_PTR_DOWN(p, align)  \
    ((void *)((char *)(p) - ((size_t)(MY_uintptr_t)(p) & ((align) - 1))))
#else
#define MY_ALIGN_PTR_DOWN(p, align) \
    ((void *)((((MY_uintptr_t)(p)) & ~((MY_uintptr_t)(align) - 1))))
#endif

#endif

#if !defined(_WIN32) \
    && (defined(Z7_ALLOC_NO_OFFSET_ALLOCATOR) \
        || defined(_POSIX_C_SOURCE) && (_POSIX_C_SOURCE >= 200112L))
  #define USE_posix_memalign
#endif

#ifndef USE_posix_memalign
#define MY_ALIGN_PTR_UP_PLUS(p, align) MY_ALIGN_PTR_DOWN(((char *)(p) + (align) + ADJUST_ALLOC_SIZE), align)
#endif

/*
  This posix_memalign() is for test purposes only.
  We also need special Free() function instead of free(),
  if this posix_memalign() is used.
*/

/*
static int posix_memalign(void **ptr, size_t align, size_t size)
{
  size_t newSize = size + align;
  void *p;
  void *pAligned;
  *ptr = NULL;
  if (newSize < size)
    return 12; // ENOMEM
  p = MyAlloc(newSize);
  if (!p)
    return 12; // ENOMEM
  pAligned = MY_ALIGN_PTR_UP_PLUS(p, align);
  ((void **)pAligned)[-1] = p;
  *ptr = pAligned;
  return 0;
}
*/

/*
  ALLOC_ALIGN_SIZE >= sizeof(void *)
  ALLOC_ALIGN_SIZE >= cache_line_size
*/

#define ALLOC_ALIGN_SIZE ((size_t)1 << 7)

void *z7_AlignedAlloc(size_t size)
{
#ifndef USE_posix_memalign
  
  void *p;
  void *pAligned;
  size_t newSize;

  /* also we can allocate additional dummy ALLOC_ALIGN_SIZE bytes after aligned
     block to prevent cache line sharing with another allocated blocks */

  newSize = size + ALLOC_ALIGN_SIZE * 1 + ADJUST_ALLOC_SIZE;
  if (newSize < size)
    return NULL;

  p = MyAlloc(newSize);
  
  if (!p)
    return NULL;
  pAligned = MY_ALIGN_PTR_UP_PLUS(p, ALLOC_ALIGN_SIZE);

  Print(" size="); PrintHex(size, 8);
  Print(" a_size="); PrintHex(newSize, 8);
  Print(" ptr="); PrintAddr(p);
  Print(" a_ptr="); PrintAddr(pAligned);
  PrintLn();

  ((void **)pAligned)[-1] = p;

  return pAligned;

#else

  void *p;
  if (posix_memalign(&p, ALLOC_ALIGN_SIZE, size))
    return NULL;

  Print(" posix_memalign="); PrintAddr(p);
  PrintLn();

  return p;

#endif
}


void z7_AlignedFree(void *address)
{
#ifndef USE_posix_memalign
  if (address)
    MyFree(((void **)address)[-1]);
#else
  free(address);
#endif
}


static void *SzAlignedAlloc(ISzAllocPtr pp, size_t size)
{
  UNUSED_VAR(pp)
  return z7_AlignedAlloc(size);
}


static void SzAlignedFree(ISzAllocPtr pp, void *address)
{
  UNUSED_VAR(pp)
#ifndef USE_posix_memalign
  if (address)
    MyFree(((void **)address)[-1]);
#else
  free(address);
#endif
}


const ISzAlloc g_AlignedAlloc = { SzAlignedAlloc, SzAlignedFree };



/* we align ptr to support cases where CAlignOffsetAlloc::offset is not multiply of sizeof(void *) */
#ifndef Z7_ALLOC_NO_OFFSET_ALLOCATOR
#if 1
  #define MY_ALIGN_PTR_DOWN_1(p)  MY_ALIGN_PTR_DOWN(p, sizeof(void *))
  #define REAL_BLOCK_PTR_VAR(p)  ((void **)MY_ALIGN_PTR_DOWN_1(p))[-1]
#else
  /* we can use this simplified code, */
  /* if (CAlignOffsetAlloc::offset == (k * sizeof(void *)) */
  #define REAL_BLOCK_PTR_VAR(p)  (((void **)(p))[-1])
#endif
#endif


#if 0
#ifndef Z7_ALLOC_NO_OFFSET_ALLOCATOR
#include <stdio.h>
static void PrintPtr(const char *s, const void *p)
{
  const Byte *p2 = (const Byte *)&p;
  unsigned i;
  printf("%s %p ", s, p);
  for (i = sizeof(p); i != 0;)
  {
    i--;
    printf("%02x", p2[i]);
  }
  printf("\n");
}
#endif
#endif


static void *AlignOffsetAlloc_Alloc(ISzAllocPtr pp, size_t size)
{
#if defined(Z7_ALLOC_NO_OFFSET_ALLOCATOR)
  UNUSED_VAR(pp)
  return z7_AlignedAlloc(size);
#else
  const CAlignOffsetAlloc *p = Z7_CONTAINER_FROM_VTBL_CONST(pp, CAlignOffsetAlloc, vt);
  void *adr;
  void *pAligned;
  size_t newSize;
  size_t extra;
  size_t alignSize = (size_t)1 << p->numAlignBits;

  if (alignSize < sizeof(void *))
    alignSize = sizeof(void *);
  
  if (p->offset >= alignSize)
    return NULL;

  /* also we can allocate additional dummy ALLOC_ALIGN_SIZE bytes after aligned
     block to prevent cache line sharing with another allocated blocks */
  extra = p->offset & (sizeof(void *) - 1);
  newSize = size + alignSize + extra + ADJUST_ALLOC_SIZE;
  if (newSize < size)
    return NULL;

  adr = ISzAlloc_Alloc(p->baseAlloc, newSize);
  
  if (!adr)
    return NULL;

  pAligned = (char *)MY_ALIGN_PTR_DOWN((char *)adr +
      alignSize - p->offset + extra + ADJUST_ALLOC_SIZE, alignSize) + p->offset;

#if 0
  printf("\nalignSize = %6x, offset=%6x, size=%8x \n", (unsigned)alignSize, (unsigned)p->offset, (unsigned)size);
  PrintPtr("base", adr);
  PrintPtr("alig", pAligned);
#endif

  PrintLn();
  Print("- Aligned: ");
  Print(" size="); PrintHex(size, 8);
  Print(" a_size="); PrintHex(newSize, 8);
  Print(" ptr="); PrintAddr(adr);
  Print(" a_ptr="); PrintAddr(pAligned);
  PrintLn();

  REAL_BLOCK_PTR_VAR(pAligned) = adr;

  return pAligned;
#endif
}


static void AlignOffsetAlloc_Free(ISzAllocPtr pp, void *address)
{
#if defined(Z7_ALLOC_NO_OFFSET_ALLOCATOR)
  UNUSED_VAR(pp)
  z7_AlignedFree(address);
#else
  if (address)
  {
    const CAlignOffsetAlloc *p = Z7_CONTAINER_FROM_VTBL_CONST(pp, CAlignOffsetAlloc, vt);
    PrintLn();
    Print("- Aligned Free: ");
    PrintLn();
    ISzAlloc_Free(p->baseAlloc, REAL_BLOCK_PTR_VAR(address));
  }
#endif
}


void AlignOffsetAlloc_CreateVTable(CAlignOffsetAlloc *p)
{
  p->vt.Alloc = AlignOffsetAlloc_Alloc;
  p->vt.Free = AlignOffsetAlloc_Free;
}
#undef SzAlloc
#undef SzFree
#undef g_allocCount
#undef ADJUST_ALLOC_SIZE
#undef ALLOC_ALIGN_SIZE
#undef CONVERT_INT_TO_STR
#undef DEBUG_OUT_STREAM
#undef GET_HEX_CHAR
#undef MY_ALIGN_PTR_DOWN
#undef MY_ALIGN_PTR_DOWN_1
#undef MY_ALIGN_PTR_UP_PLUS
#undef MY_MEM_LARGE_PAGES
#undef PRINT_ALLOC
#undef PRINT_FREE
#undef PRINT_REALLOC
#undef Print
#undef PrintAddr
#undef PrintHex
#undef PrintLn
#undef REAL_BLOCK_PTR_VAR
#undef USE_posix_memalign
#undef Z7_ALLOC_NO_OFFSET_ALLOCATOR
#undef Z7_USE_DYN_GetLargePageMinimum

/* ==== Sort.c ==== */
/* Sort.c -- Sort functions
: Igor Pavlov : Public domain */



/* ==== Sort.h ==== */
/* Sort.h -- Sort functions
: Igor Pavlov : Public domain */

#ifndef ZIP7_INC_SORT_H
#define ZIP7_INC_SORT_H



EXTERN_C_BEGIN

void Z7_FASTCALL HeapSort(UInt32 *p, size_t size);

EXTERN_C_END

#endif


#if (  (defined(__GNUC__) && (__GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 1))) \
    || (defined(__clang__) && Z7_has_builtin(__builtin_prefetch)) \
    )
/* the code with prefetch is slow for small arrays on x86. */
/* So we disable prefetch for x86. */
#ifndef MY_CPU_X86
  /* #pragma message("Z7_PREFETCH : __builtin_prefetch") */
  #define Z7_PREFETCH(a)  __builtin_prefetch((a))
#endif

#elif defined(_WIN32) /* || defined(_MSC_VER) && (_MSC_VER >= 1200) */



/* NOTE: CLANG/GCC/MSVC can define different values for _MM_HINT_T0 / PF_TEMPORAL_LEVEL_1. */
/* For example, clang-cl can generate "prefetcht2" instruction for */
/* PreFetchCacheLine(PF_TEMPORAL_LEVEL_1) call. */
/* But we want to generate "prefetcht0" instruction. */
/* So for CLANG/GCC we must use __builtin_prefetch() in code branch above */
/* instead of PreFetchCacheLine() / _mm_prefetch(). */

/* New msvc-x86 compiler generates "prefetcht0" instruction for PreFetchCacheLine() call. */
/* But old x86 cpus don't support "prefetcht0". */
/* So we will use PreFetchCacheLine(), only if we are sure that */
/* generated instruction is supported by all cpus of that isa. */
#if defined(MY_CPU_AMD64) \
    || defined(MY_CPU_ARM64) \
    || defined(MY_CPU_IA64)
/* we need to use additional braces for (a) in PreFetchCacheLine call, because */
/* PreFetchCacheLine macro doesn't use braces: */
/*   #define PreFetchCacheLine(l, a)  _mm_prefetch((CHAR CONST *) a, l) */
  /* #pragma message("Z7_PREFETCH : PreFetchCacheLine") */
  #define Z7_PREFETCH(a)  PreFetchCacheLine(PF_TEMPORAL_LEVEL_1, (a))
#endif

#endif /* _WIN32 */


#define PREFETCH_NO(p,k,s,size)

#ifndef Z7_PREFETCH
  #define SORT_PREFETCH(p,k,s,size)
#else

/* #define PREFETCH_LEVEL 2  // use it if cache line is 32-bytes */
#define PREFETCH_LEVEL 3  /* it is fast for most cases (64-bytes cache line prefetch) */
/* #define PREFETCH_LEVEL 4  // it can be faster for big array (128-bytes prefetch) */

#if PREFETCH_LEVEL == 0

  #define SORT_PREFETCH(p,k,s,size)

#else /* PREFETCH_LEVEL != 0 */

/*
if  defined(USE_PREFETCH_FOR_ALIGNED_ARRAY)
    we prefetch one value per cache line.
    Use it if array is aligned for cache line size (64 bytes)
    or if array is small (less than L1 cache size).

if !defined(USE_PREFETCH_FOR_ALIGNED_ARRAY)
    we perfetch all cache lines that can be required.
    it can be faster for big unaligned arrays.
*/
  #define USE_PREFETCH_FOR_ALIGNED_ARRAY

/* s == k * 2 */
#if 0 && PREFETCH_LEVEL <= 3 && defined(MY_CPU_X86_OR_AMD64)
  /* x86 supports (lea r1*8+offset) */
  #define PREFETCH_OFFSET(k,s)  ((s) << PREFETCH_LEVEL)
#else
  #define PREFETCH_OFFSET(k,s)  ((k) << (PREFETCH_LEVEL + 1))
#endif

#if 1 && PREFETCH_LEVEL <= 3 && defined(USE_PREFETCH_FOR_ALIGNED_ARRAY)
  #define PREFETCH_ADD_OFFSET   0
#else
  /* last offset that can be reqiured in PREFETCH_LEVEL step: */
  #define PREFETCH_RANGE        ((2 << PREFETCH_LEVEL) - 1)
  #define PREFETCH_ADD_OFFSET   PREFETCH_RANGE / 2
#endif

#if PREFETCH_LEVEL <= 3

#ifdef USE_PREFETCH_FOR_ALIGNED_ARRAY
  #define SORT_PREFETCH(p,k,s,size) \
  { const size_t s2 = PREFETCH_OFFSET(k,s) + PREFETCH_ADD_OFFSET; \
    if (s2 <= size) { \
      Z7_PREFETCH((p + s2)); \
  }}
#else /* for unaligned array */
  #define SORT_PREFETCH(p,k,s,size) \
  { const size_t s2 = PREFETCH_OFFSET(k,s) + PREFETCH_RANGE; \
    if (s2 <= size) { \
      Z7_PREFETCH((p + s2 - PREFETCH_RANGE)); \
      Z7_PREFETCH((p + s2)); \
  }}
#endif

#else /* PREFETCH_LEVEL > 3 */

#ifdef USE_PREFETCH_FOR_ALIGNED_ARRAY
  #define SORT_PREFETCH(p,k,s,size) \
  { const size_t s2 = PREFETCH_OFFSET(k,s) + PREFETCH_RANGE - 16 / 2; \
    if (s2 <= size) { \
      Z7_PREFETCH((p + s2 - 16)); \
      Z7_PREFETCH((p + s2)); \
  }}
#else /* for unaligned array */
  #define SORT_PREFETCH(p,k,s,size) \
  { const size_t s2 = PREFETCH_OFFSET(k,s) + PREFETCH_RANGE; \
    if (s2 <= size) { \
      Z7_PREFETCH((p + s2 - PREFETCH_RANGE)); \
      Z7_PREFETCH((p + s2 - PREFETCH_RANGE / 2)); \
      Z7_PREFETCH((p + s2)); \
  }}
#endif

#endif /* PREFETCH_LEVEL > 3 */
#endif /* PREFETCH_LEVEL != 0 */
#endif /* Z7_PREFETCH */


#if defined(MY_CPU_ARM64) \
    /* || defined(MY_CPU_AMD64) */ \
    /* || defined(MY_CPU_ARM) && !defined(_MSC_VER) */
  /* we want to use cmov, if cmov is very fast: */
  /* - this cmov version is slower for clang-x64. */
  /* - this cmov version is faster for gcc-arm64 for some fast arm64 cpus. */
  #define Z7_FAST_CMOV_SUPPORTED
#endif
 
#ifdef Z7_FAST_CMOV_SUPPORTED
  /* we want to use cmov here, if cmov is fast: new arm64 cpus. */
  /* we want the compiler to use conditional move for this branch */
  #define GET_MAX_VAL(n0, n1, max_val_slow)  if (n0 < n1) n0 = n1;
#else
  /* use this branch, if cpu doesn't support fast conditional move. */
  /* it uses slow array access reading: */
  #define GET_MAX_VAL(n0, n1, max_val_slow)  n0 = max_val_slow;
#endif

#define HeapSortDown(p, k, size, temp, macro_prefetch) \
{ \
  for (;;) { \
    UInt32 n0, n1; \
    size_t s = k * 2; \
    if (s >= size) { \
      if (s == size) { \
        n0 = p[s]; \
        p[k] = n0; \
        if (temp < n0) k = s; \
      } \
      break; \
    } \
    n0 = p[k * 2]; \
    n1 = p[k * 2 + 1]; \
    s += n0 < n1; \
    GET_MAX_VAL(n0, n1, p[s]) \
    if (temp >= n0) break; \
    macro_prefetch(p, k, s, size) \
    p[k] = n0; \
    k = s; \
  } \
  p[k] = temp; \
}


/*
stage-1 : O(n) :
  we generate intermediate partially sorted binary tree:
  p[0]  : it's additional item for better alignment of tree structure in memory.
  p[1]
  p[2]       p[3]
  p[4] p[5]  p[6] p[7]
  ...
  p[x] >= p[x * 2]
  p[x] >= p[x * 2 + 1]
  
stage-2 : O(n)*log2(N):
  we move largest item p[0] from head of tree to the end of array
  and insert last item to sorted binary tree.
*/

/* (p) must be aligned for cache line size (64-bytes) for best performance */

void Z7_FASTCALL HeapSort(UInt32 *p, size_t size)
{
  if (size < 2)
    return;
  if (size == 2)
  {
    const UInt32 a0 = p[0];
    const UInt32 a1 = p[1];
    const unsigned k = a1 < a0;
    p[k] = a0;
    p[k ^ 1] = a1;
    return;
  }
  {
    /* stage-1 : O(n) */
    /* we transform array to partially sorted binary tree. */
    size_t i = --size / 2;
    /* (size) now is the index of the last item in tree, */
    /* if (i) */
    {
      do
      {
        const UInt32 temp = p[i];
        size_t k = i;
        HeapSortDown(p, k, size, temp, PREFETCH_NO)
      }
      while (--i);
    }
    {
      const UInt32 temp = p[0];
      const UInt32 a1 = p[1];
      if (temp < a1)
      {
        size_t k = 1;
        p[0] = a1;
        HeapSortDown(p, k, size, temp, PREFETCH_NO)
      }
    }
  }

  if (size < 3)
  {
    /* size == 2 */
    const UInt32 a0 = p[0];
    p[0] = p[2];
    p[2] = a0;
    return;
  }
  if (size != 3)
  {
    /* stage-2 : O(size) * log2(size): */
    /* we move largest item p[0] from head to the end of array, */
    /* and insert last item to sorted binary tree. */
    do
    {
      const UInt32 temp = p[size];
      size_t k = p[2] < p[3] ? 3 : 2;
      p[size--] = p[0];
      p[0] = p[1];
      p[1] = p[k];
      HeapSortDown(p, k, size, temp, SORT_PREFETCH) /* PREFETCH_NO */
    }
    while (size != 3);
  }
  {
    const UInt32 a2 = p[2];
    const UInt32 a3 = p[3];
    const size_t k = a2 < a3;
    p[2] = p[1];
    p[3] = p[0];
    p[k] = a3;
    p[k ^ 1] = a2;
  }
}
#undef GET_MAX_VAL
#undef HeapSortDown
#undef PREFETCH_ADD_OFFSET
#undef PREFETCH_LEVEL
#undef PREFETCH_NO
#undef PREFETCH_OFFSET
#undef PREFETCH_RANGE
#undef SORT_PREFETCH
#undef USE_PREFETCH_FOR_ALIGNED_ARRAY
#undef Z7_FAST_CMOV_SUPPORTED
#undef Z7_PREFETCH

/* ==== Sha256.c ==== */
/* Sha256.c -- SHA-256 Hash
: Igor Pavlov : Public domain
This code is based on public domain code from Wei Dai's Crypto++ library. */



#include <string.h>





#ifdef MY_CPU_X86_OR_AMD64
  #if   defined(Z7_LLVM_CLANG_VERSION)  && (Z7_LLVM_CLANG_VERSION  >= 30800) \
     || defined(Z7_APPLE_CLANG_VERSION) && (Z7_APPLE_CLANG_VERSION >= 50100) \
     || defined(Z7_GCC_VERSION)         && (Z7_GCC_VERSION         >= 40900) \
     || defined(__INTEL_COMPILER) && (__INTEL_COMPILER >= 1600) \
     || defined(_MSC_VER) && (_MSC_VER >= 1200)
      /* amalgamated: SHA-NI path dropped with Sha256Opt.c */
      #define Z7_COMPILER_SHA256_SUPPORTED_UNUSED
  #endif
#elif defined(MY_CPU_ARM_OR_ARM64) && defined(MY_CPU_LE)

  #if   defined(__ARM_FEATURE_SHA2) \
     || defined(__ARM_FEATURE_CRYPTO)
    /* amalgamated: SHA-NI path dropped with Sha256Opt.c */
    #define Z7_COMPILER_SHA256_SUPPORTED_UNUSED
  #else
    #if  defined(MY_CPU_ARM64) \
      || defined(__ARM_ARCH) && (__ARM_ARCH >= 4) \
      || defined(Z7_MSC_VER_ORIGINAL)
    #if  defined(__ARM_FP) && \
          (   defined(Z7_CLANG_VERSION) && (Z7_CLANG_VERSION >= 30800) \
           || defined(__GNUC__) && (__GNUC__ >= 6) \
          ) \
      || defined(Z7_MSC_VER_ORIGINAL) && (_MSC_VER >= 1910)
    #if  defined(MY_CPU_ARM64) \
      || !defined(Z7_CLANG_VERSION) \
      || defined(__ARM_NEON) && \
          (Z7_CLANG_VERSION < 170000 || \
           Z7_CLANG_VERSION > 170001)
      /* amalgamated: SHA-NI path dropped with Sha256Opt.c */
      #define Z7_COMPILER_SHA256_SUPPORTED_UNUSED
    #endif
    #endif
    #endif
  #endif
#endif

void Z7_FASTCALL Sha256_UpdateBlocks(UInt32 state[8], const Byte *data, size_t numBlocks);

#ifdef Z7_COMPILER_SHA256_SUPPORTED
  void Z7_FASTCALL Sha256_UpdateBlocks_HW(UInt32 state[8], const Byte *data, size_t numBlocks);

  static SHA256_FUNC_UPDATE_BLOCKS g_SHA256_FUNC_UPDATE_BLOCKS = Sha256_UpdateBlocks;
  static SHA256_FUNC_UPDATE_BLOCKS g_SHA256_FUNC_UPDATE_BLOCKS_HW;

  #define SHA256_UPDATE_BLOCKS(p) p->v.vars.func_UpdateBlocks
#else
  #define SHA256_UPDATE_BLOCKS(p) Sha256_UpdateBlocks
#endif


BoolInt Sha256_SetFunction(CSha256 *p, unsigned algo)
{
  SHA256_FUNC_UPDATE_BLOCKS func = Sha256_UpdateBlocks;
  
  #ifdef Z7_COMPILER_SHA256_SUPPORTED
    if (algo != SHA256_ALGO_SW)
    {
      if (algo == SHA256_ALGO_DEFAULT)
        func = g_SHA256_FUNC_UPDATE_BLOCKS;
      else
      {
        if (algo != SHA256_ALGO_HW)
          return False;
        func = g_SHA256_FUNC_UPDATE_BLOCKS_HW;
        if (!func)
          return False;
      }
    }
  #else
    if (algo > 1)
      return False;
  #endif

  p->v.vars.func_UpdateBlocks = func;
  return True;
}


/* define it for speed optimization */

#ifdef Z7_SFX
  #define STEP_PRE 1
  #define STEP_MAIN 1
#else
  #define STEP_PRE 2
  #define STEP_MAIN 4
  /* #define Z7_SHA256_UNROLL */
#endif

#undef Z7_SHA256_BIG_W
#if STEP_MAIN != 16
  #define Z7_SHA256_BIG_W
#endif




void Sha256_InitState(CSha256 *p)
{
  p->v.vars.count = 0;
  p->state[0] = 0x6a09e667;
  p->state[1] = 0xbb67ae85;
  p->state[2] = 0x3c6ef372;
  p->state[3] = 0xa54ff53a;
  p->state[4] = 0x510e527f;
  p->state[5] = 0x9b05688c;
  p->state[6] = 0x1f83d9ab;
  p->state[7] = 0x5be0cd19;
}








void Sha256_Init(CSha256 *p)
{
  p->v.vars.func_UpdateBlocks =
  #ifdef Z7_COMPILER_SHA256_SUPPORTED
      g_SHA256_FUNC_UPDATE_BLOCKS;
  #else
      NULL;
  #endif
  Sha256_InitState(p);
}

#define S0(x) (rotrFixed(x, 2) ^ rotrFixed(x,13) ^ rotrFixed(x,22))
#define S1(x) (rotrFixed(x, 6) ^ rotrFixed(x,11) ^ rotrFixed(x,25))
#define s0(x) (rotrFixed(x, 7) ^ rotrFixed(x,18) ^ (x >> 3))
#define s1(x) (rotrFixed(x,17) ^ rotrFixed(x,19) ^ (x >>10))

#define Ch(x,y,z) (z^(x&(y^z)))
#define Maj(x,y,z) ((x&y)|(z&(x|y)))


#define W_PRE(i) (W[(i) + (size_t)(j)] = GetBe32(data + ((size_t)(j) + i) * 4))

#define blk2_main(j, i)  s1(w(j, (i)-2)) + w(j, (i)-7) + s0(w(j, (i)-15))

#ifdef Z7_SHA256_BIG_W
    /* we use +i instead of +(i) to change the order to solve CLANG compiler warning for signed/unsigned. */
    #define w(j, i)     W[(size_t)(j) + i]
    #define blk2(j, i)  (w(j, i) = w(j, (i)-16) + blk2_main(j, i))
#else
    #if STEP_MAIN == 16
        #define w(j, i)  W[(i) & 15]
    #else
        #define w(j, i)  W[((size_t)(j) + (i)) & 15]
    #endif
    #define blk2(j, i)  (w(j, i) += blk2_main(j, i))
#endif

#define W_MAIN(i)  blk2(j, i)


#define T1(wx, i) \
    tmp = h + S1(e) + Ch(e,f,g) + K[(i)+(size_t)(j)] + wx(i); \
    h = g; \
    g = f; \
    f = e; \
    e = d + tmp; \
    tmp += S0(a) + Maj(a, b, c); \
    d = c; \
    c = b; \
    b = a; \
    a = tmp; \

#define R1_PRE(i)  T1( W_PRE, i)
#define R1_MAIN(i) T1( W_MAIN, i)

#if (!defined(Z7_SHA256_UNROLL) || STEP_MAIN < 8) && (STEP_MAIN >= 4)
#define R2_MAIN(i) \
    R1_MAIN(i) \
    R1_MAIN(i + 1) \

#endif



#if defined(Z7_SHA256_UNROLL) && STEP_MAIN >= 8

#define T4( a,b,c,d,e,f,g,h, wx, i) \
    h += S1(e) + Ch(e,f,g) + K[(i)+(size_t)(j)] + wx(i); \
    tmp = h; \
    h += d; \
    d = tmp + S0(a) + Maj(a, b, c); \

#define R4( wx, i) \
    T4 ( a,b,c,d,e,f,g,h, wx, (i  )); \
    T4 ( d,a,b,c,h,e,f,g, wx, (i+1)); \
    T4 ( c,d,a,b,g,h,e,f, wx, (i+2)); \
    T4 ( b,c,d,a,f,g,h,e, wx, (i+3)); \

#define R4_PRE(i)  R4( W_PRE, i)
#define R4_MAIN(i) R4( W_MAIN, i)


#define T8( a,b,c,d,e,f,g,h, wx, i) \
    h += S1(e) + Ch(e,f,g) + K[(i)+(size_t)(j)] + wx(i); \
    d += h; \
    h += S0(a) + Maj(a, b, c); \

#define R8( wx, i) \
    T8 ( a,b,c,d,e,f,g,h, wx, i  ); \
    T8 ( h,a,b,c,d,e,f,g, wx, i+1); \
    T8 ( g,h,a,b,c,d,e,f, wx, i+2); \
    T8 ( f,g,h,a,b,c,d,e, wx, i+3); \
    T8 ( e,f,g,h,a,b,c,d, wx, i+4); \
    T8 ( d,e,f,g,h,a,b,c, wx, i+5); \
    T8 ( c,d,e,f,g,h,a,b, wx, i+6); \
    T8 ( b,c,d,e,f,g,h,a, wx, i+7); \

#define R8_PRE(i)  R8( W_PRE, i)
#define R8_MAIN(i) R8( W_MAIN, i)

#endif


extern
MY_ALIGN(64) const UInt32 SHA256_K_ARRAY[64];
MY_ALIGN(64) const UInt32 SHA256_K_ARRAY[64] = {
  0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
  0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
  0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
  0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
  0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
  0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
  0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
  0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
  0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
  0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
  0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
  0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
  0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
  0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
  0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
  0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};





#define K SHA256_K_ARRAY

Z7_NO_INLINE
void Z7_FASTCALL Sha256_UpdateBlocks(UInt32 state[8], const Byte *data, size_t numBlocks)
{
  UInt32 W
#ifdef Z7_SHA256_BIG_W
      [64];
#else
      [16];
#endif
  unsigned j;
  UInt32 a,b,c,d,e,f,g,h;
#if !defined(Z7_SHA256_UNROLL) || (STEP_MAIN <= 4) || (STEP_PRE <= 4)
  UInt32 tmp;
#endif
  
  if (numBlocks == 0) return;

  a = state[0];
  b = state[1];
  c = state[2];
  d = state[3];
  e = state[4];
  f = state[5];
  g = state[6];
  h = state[7];

  do
  {

  for (j = 0; j < 16; j += STEP_PRE)
  {
    #if STEP_PRE > 4

      #if STEP_PRE < 8
      R4_PRE(0);
      #else
      R8_PRE(0);
      #if STEP_PRE == 16
      R8_PRE(8);
      #endif
      #endif

    #else

      R1_PRE(0)
      #if STEP_PRE >= 2
      R1_PRE(1)
      #if STEP_PRE >= 4
      R1_PRE(2)
      R1_PRE(3)
      #endif
      #endif
    
    #endif
  }

  for (j = 16; j < 64; j += STEP_MAIN)
  {
    #if defined(Z7_SHA256_UNROLL) && STEP_MAIN >= 8

      #if STEP_MAIN < 8
      R4_MAIN(0)
      #else
      R8_MAIN(0)
      #if STEP_MAIN == 16
      R8_MAIN(8)
      #endif
      #endif

    #else
      
      R1_MAIN(0)
      #if STEP_MAIN >= 2
      R1_MAIN(1)
      #if STEP_MAIN >= 4
      R2_MAIN(2)
      #if STEP_MAIN >= 8
      R2_MAIN(4)
      R2_MAIN(6)
      #if STEP_MAIN >= 16
      R2_MAIN(8)
      R2_MAIN(10)
      R2_MAIN(12)
      R2_MAIN(14)
      #endif
      #endif
      #endif
      #endif
    #endif
  }

  a += state[0]; state[0] = a;
  b += state[1]; state[1] = b;
  c += state[2]; state[2] = c;
  d += state[3]; state[3] = d;
  e += state[4]; state[4] = e;
  f += state[5]; state[5] = f;
  g += state[6]; state[6] = g;
  h += state[7]; state[7] = h;

  data += SHA256_BLOCK_SIZE;
  }
  while (--numBlocks);
}


#define Sha256_UpdateBlock(p) SHA256_UPDATE_BLOCKS(p)(p->state, p->buffer, 1)

void Sha256_Update(CSha256 *p, const Byte *data, size_t size)
{
  if (size == 0)
    return;
  {
    const unsigned pos = (unsigned)p->v.vars.count & (SHA256_BLOCK_SIZE - 1);
    const unsigned num = SHA256_BLOCK_SIZE - pos;
    p->v.vars.count += size;
    if (num > size)
    {
      memcpy(p->buffer + pos, data, size);
      return;
    }
    if (pos != 0)
    {
      size -= num;
      memcpy(p->buffer + pos, data, num);
      data += num;
      Sha256_UpdateBlock(p);
    }
  }
  {
    const size_t numBlocks = size >> 6;
    /* if (numBlocks) */
    SHA256_UPDATE_BLOCKS(p)(p->state, data, numBlocks);
    size &= SHA256_BLOCK_SIZE - 1;
    if (size == 0)
      return;
    data += (numBlocks << 6);
    memcpy(p->buffer, data, size);
  }
}


void Sha256_Final(CSha256 *p, Byte *digest)
{
  unsigned pos = (unsigned)p->v.vars.count & (SHA256_BLOCK_SIZE - 1);
  p->buffer[pos++] = 0x80;
  if (pos > (SHA256_BLOCK_SIZE - 4 * 2))
  {
    while (pos != SHA256_BLOCK_SIZE) { p->buffer[pos++] = 0; }
    /* memset(&p->buf.buffer[pos], 0, SHA256_BLOCK_SIZE - pos); */
    Sha256_UpdateBlock(p);
    pos = 0;
  }
  memset(&p->buffer[pos], 0, (SHA256_BLOCK_SIZE - 4 * 2) - pos);
  {
    const UInt64 numBits = p->v.vars.count << 3;
    SetBe32(p->buffer + SHA256_BLOCK_SIZE - 4 * 2, (UInt32)(numBits >> 32))
    SetBe32(p->buffer + SHA256_BLOCK_SIZE - 4 * 1, (UInt32)(numBits))
  }
  Sha256_UpdateBlock(p);
#if 1 && defined(MY_CPU_BE)
  memcpy(digest, p->state, SHA256_DIGEST_SIZE);
#else
  {
    unsigned i;
    for (i = 0; i < 8; i += 2)
    {
      const UInt32 v0 = p->state[i];
      const UInt32 v1 = p->state[(size_t)i + 1];
      SetBe32(digest    , v0)
      SetBe32(digest + 4, v1)
      digest += 4 * 2;
    }
  }




#endif
  Sha256_InitState(p);
}


void Sha256Prepare(void)
{
#ifdef Z7_COMPILER_SHA256_SUPPORTED
  SHA256_FUNC_UPDATE_BLOCKS f, f_hw;
  f = Sha256_UpdateBlocks;
  f_hw = NULL;
#ifdef MY_CPU_X86_OR_AMD64
  if (CPU_IsSupported_SHA()
      && CPU_IsSupported_SSSE3()
      )
#else
  if (CPU_IsSupported_SHA2())
#endif
  {
    /* printf("\n========== HW SHA256 ======== \n"); */
    f = f_hw = Sha256_UpdateBlocks_HW;
  }
  g_SHA256_FUNC_UPDATE_BLOCKS    = f;
  g_SHA256_FUNC_UPDATE_BLOCKS_HW = f_hw;
#endif
}

#undef U64C
#undef K
#undef S0
#undef S1
#undef s0
#undef s1
#undef Ch
#undef Maj
#undef W_MAIN
#undef W_PRE
#undef w
#undef blk2_main
#undef blk2
#undef T1
#undef T4
#undef T8
#undef R1_PRE
#undef R1_MAIN
#undef R2_MAIN
#undef R4
#undef R4_PRE
#undef R4_MAIN
#undef R8
#undef R8_PRE
#undef R8_MAIN
#undef STEP_PRE
#undef STEP_MAIN
#undef Z7_SHA256_BIG_W
#undef Z7_SHA256_UNROLL
#undef Z7_COMPILER_SHA256_SUPPORTED
#undef Ch
#undef K
#undef Maj
#undef R1_MAIN
#undef R1_PRE
#undef R2_MAIN
#undef R4
#undef R4_MAIN
#undef R4_PRE
#undef R8
#undef R8_MAIN
#undef R8_PRE
#undef S0
#undef S1
#undef SHA256_UPDATE_BLOCKS
#undef STEP_MAIN
#undef STEP_PRE
#undef Sha256_UpdateBlock
#undef T1
#undef T4
#undef T8
#undef W_MAIN
#undef W_PRE
#undef Z7_COMPILER_SHA256_SUPPORTED_UNUSED
#undef Z7_SHA256_BIG_W
#undef blk2
#undef blk2_main
#undef s0
#undef s1
#undef w

/* ==== Xz.c ==== */
/* Xz.c - Xz
2024-03-01 : Igor Pavlov : Public domain */








const Byte XZ_SIG[XZ_SIG_SIZE] = { 0xFD, '7', 'z', 'X', 'Z', 0 };
/* const Byte XZ_FOOTER_SIG[XZ_FOOTER_SIG_SIZE] = { 'Y', 'Z' }; */

unsigned Xz_WriteVarInt(Byte *buf, UInt64 v)
{
  unsigned i = 0;
  do
  {
    buf[i++] = (Byte)((v & 0x7F) | 0x80);
    v >>= 7;
  }
  while (v != 0);
  buf[(size_t)i - 1] &= 0x7F;
  return i;
}

void Xz_Construct(CXzStream *p)
{
  p->numBlocks = 0;
  p->blocks = NULL;
  p->flags = 0;
}

void Xz_Free(CXzStream *p, ISzAllocPtr alloc)
{
  ISzAlloc_Free(alloc, p->blocks);
  p->numBlocks = 0;
  p->blocks = NULL;
}

unsigned XzFlags_GetCheckSize(CXzStreamFlags f)
{
  unsigned t = XzFlags_GetCheckType(f);
  return (t == 0) ? 0 : ((unsigned)4 << ((t - 1) / 3));
}

void XzCheck_Init(CXzCheck *p, unsigned mode)
{
  p->mode = mode;
  switch (mode)
  {
    case XZ_CHECK_CRC32: p->crc = CRC_INIT_VAL; break;
    case XZ_CHECK_CRC64: p->crc64 = CRC64_INIT_VAL; break;
    case XZ_CHECK_SHA256: Sha256_Init(&p->sha); break;
    default: break;
  }
}

void XzCheck_Update(CXzCheck *p, const void *data, size_t size)
{
  switch (p->mode)
  {
    case XZ_CHECK_CRC32: p->crc = CrcUpdate(p->crc, data, size); break;
    case XZ_CHECK_CRC64: p->crc64 = Crc64Update(p->crc64, data, size); break;
    case XZ_CHECK_SHA256: Sha256_Update(&p->sha, (const Byte *)data, size); break;
    default: break;
  }
}

int XzCheck_Final(CXzCheck *p, Byte *digest)
{
  switch (p->mode)
  {
    case XZ_CHECK_CRC32:
      SetUi32(digest, CRC_GET_DIGEST(p->crc))
      break;
    case XZ_CHECK_CRC64:
    {
      int i;
      UInt64 v = CRC64_GET_DIGEST(p->crc64);
      for (i = 0; i < 8; i++, v >>= 8)
        digest[i] = (Byte)(v & 0xFF);
      break;
    }
    case XZ_CHECK_SHA256:
      Sha256_Final(&p->sha, digest);
      break;
    default:
      return 0;
  }
  return 1;
}

/* ==== XzDec.c ==== */
/* XzDec.c -- Xz Decode
: Igor Pavlov : Public domain */



/* #include <stdio.h> */

/* #define XZ_DUMP */

/* #define XZ_DUMP */

#ifdef XZ_DUMP
#include <stdio.h>
#endif

/* #define SHOW_DEBUG_INFO */

#ifdef SHOW_DEBUG_INFO
#include <stdio.h>
#endif

#ifdef SHOW_DEBUG_INFO
#define PRF(x) x
#else
#define PRF(x)
#endif

#define PRF_STR(s) PRF(printf("\n" s "\n"))
#define PRF_STR_INT(s, d) PRF(printf("\n" s " %d\n", (unsigned)d))

#include <stdlib.h>
#include <string.h>








/* #define USE_SUBBLOCK */

#ifdef USE_SUBBLOCK


#endif



#define XZ_CHECK_SIZE_MAX 64

#define CODER_BUF_SIZE ((size_t)1 << 17)

unsigned Xz_ReadVarInt(const Byte *p, size_t maxSize, UInt64 *value)
{
  unsigned i, limit;
  *value = 0;
  limit = (maxSize > 9) ? 9 : (unsigned)maxSize;

  for (i = 0; i < limit;)
  {
    const unsigned b = p[i];
    *value |= (UInt64)(b & 0x7F) << (7 * i++);
    if ((b & 0x80) == 0)
      return (b == 0 && i != 1) ? 0 : i;
  }
  return 0;
}


/* ---------- XzBcFilterState ---------- */

#define BRA_BUF_SIZE (1 << 14)

typedef struct
{
  size_t bufPos;
  size_t bufConv;
  size_t bufTotal;
  Byte *buf;  /* must be aligned for 4 bytes */
  Xz_Func_BcFilterStateBase_Filter filter_func;
  /* int encodeMode; */
  CXzBcFilterStateBase base;
  /* Byte buf[BRA_BUF_SIZE]; */
} CXzBcFilterState;


static void XzBcFilterState_Free(void *pp, ISzAllocPtr alloc)
{
  if (pp)
  {
    CXzBcFilterState *p = ((CXzBcFilterState *)pp);
    ISzAlloc_Free(alloc, p->buf);
    ISzAlloc_Free(alloc, pp);
  }
}


static SRes XzBcFilterState_SetProps(void *pp, const Byte *props, size_t propSize, ISzAllocPtr alloc)
{
  CXzBcFilterStateBase *p = &((CXzBcFilterState *)pp)->base;
  UNUSED_VAR(alloc)
  p->ip = 0;
  if (p->methodId == XZ_ID_Delta)
  {
    if (propSize != 1)
      return SZ_ERROR_UNSUPPORTED;
    p->delta = (UInt32)props[0] + 1;
  }
  else
  {
    if (propSize == 4)
    {
      const UInt32 v = GetUi32(props);
      switch (p->methodId)
      {
        case XZ_ID_PPC:
        case XZ_ID_ARM:
        case XZ_ID_SPARC:
        case XZ_ID_ARM64:
          if (v & 3)
            return SZ_ERROR_UNSUPPORTED;
          break;
        case XZ_ID_ARMT:
        case XZ_ID_RISCV:
          if (v & 1)
            return SZ_ERROR_UNSUPPORTED;
          break;
        case XZ_ID_IA64:
          if (v & 0xf)
            return SZ_ERROR_UNSUPPORTED;
          break;
        default: break;
      }
      p->ip = v;
    }
    else if (propSize != 0)
      return SZ_ERROR_UNSUPPORTED;
  }
  return SZ_OK;
}


static void XzBcFilterState_Init(void *pp)
{
  CXzBcFilterState *p = ((CXzBcFilterState *)pp);
  p->bufPos = p->bufConv = p->bufTotal = 0;
  p->base.X86_State = Z7_BRANCH_CONV_ST_X86_STATE_INIT_VAL;
  if (p->base.methodId == XZ_ID_Delta)
    Delta_Init(p->base.delta_State);
}


static const z7_Func_BranchConv g_Funcs_BranchConv_RISC_Dec[] =
{
  Z7_BRANCH_CONV_DEC_2 (BranchConv_PPC),
  Z7_BRANCH_CONV_DEC_2 (BranchConv_IA64),
  Z7_BRANCH_CONV_DEC_2 (BranchConv_ARM),
  Z7_BRANCH_CONV_DEC_2 (BranchConv_ARMT),
  Z7_BRANCH_CONV_DEC_2 (BranchConv_SPARC),
  Z7_BRANCH_CONV_DEC_2 (BranchConv_ARM64),
  Z7_BRANCH_CONV_DEC_2 (BranchConv_RISCV)
};

static SizeT XzBcFilterStateBase_Filter_Dec(CXzBcFilterStateBase *p, Byte *data, SizeT size)
{
  switch (p->methodId)
  {
    case XZ_ID_Delta:
      Delta_Decode(p->delta_State, p->delta, data, size);
      break;
    case XZ_ID_X86:
      size = (SizeT)(z7_BranchConvSt_X86_Dec(data, size, p->ip, &p->X86_State) - data);
      break;
    default:
      if (p->methodId >= XZ_ID_PPC)
      {
        const UInt32 i = p->methodId - XZ_ID_PPC;
        if (i < Z7_ARRAY_SIZE(g_Funcs_BranchConv_RISC_Dec))
          size = (SizeT)(g_Funcs_BranchConv_RISC_Dec[i](data, size, p->ip) - data);
      }
      break;
  }
  p->ip += (UInt32)size;
  return size;
}


static SizeT XzBcFilterState_Filter(void *pp, Byte *data, SizeT size)
{
  CXzBcFilterState *p = ((CXzBcFilterState *)pp);
  return p->filter_func(&p->base, data, size);
}


static SRes XzBcFilterState_Code2(void *pp,
    Byte *dest, SizeT *destLen,
    const Byte *src, SizeT *srcLen, int srcWasFinished,
    ECoderFinishMode finishMode,
    /* int *wasFinished */
    ECoderStatus *status)
{
  CXzBcFilterState *p = ((CXzBcFilterState *)pp);
  SizeT destRem = *destLen;
  SizeT srcRem = *srcLen;
  UNUSED_VAR(finishMode)

  *destLen = 0;
  *srcLen = 0;
  /* *wasFinished = False; */
  *status = CODER_STATUS_NOT_FINISHED;
  
  while (destRem != 0)
  {
    {
      size_t size = p->bufConv - p->bufPos;
      if (size)
      {
        if (size > destRem)
          size = destRem;
        memcpy(dest, p->buf + p->bufPos, size);
        p->bufPos += size;
        *destLen += size;
        dest += size;
        destRem -= size;
        continue;
      }
    }
    
    p->bufTotal -= p->bufPos;
    memmove(p->buf, p->buf + p->bufPos, p->bufTotal);
    p->bufPos = 0;
    p->bufConv = 0;
    {
      size_t size = BRA_BUF_SIZE - p->bufTotal;
      if (size > srcRem)
        size = srcRem;
      memcpy(p->buf + p->bufTotal, src, size);
      *srcLen += size;
      src += size;
      srcRem -= size;
      p->bufTotal += size;
    }
    if (p->bufTotal == 0)
      break;
    
    p->bufConv = p->filter_func(&p->base, p->buf, p->bufTotal);

    if (p->bufConv == 0)
    {
      if (!srcWasFinished)
        break;
      p->bufConv = p->bufTotal;
    }
  }

  if (p->bufTotal == p->bufPos && srcRem == 0 && srcWasFinished)
  {
    *status = CODER_STATUS_FINISHED_WITH_MARK;
    /* *wasFinished = 1; */
  }

  return SZ_OK;
}


#define XZ_IS_SUPPORTED_FILTER_ID(id) \
    ((id) >= XZ_ID_Delta && (id) <= XZ_ID_RISCV)
     
SRes Xz_StateCoder_Bc_SetFromMethod_Func(IStateCoder *p, UInt64 id,
    Xz_Func_BcFilterStateBase_Filter func, ISzAllocPtr alloc)
{
  CXzBcFilterState *decoder;
  if (!XZ_IS_SUPPORTED_FILTER_ID(id))
    return SZ_ERROR_UNSUPPORTED;
  decoder = (CXzBcFilterState *)p->p;
  if (!decoder)
  {
    decoder = (CXzBcFilterState *)ISzAlloc_Alloc(alloc, sizeof(CXzBcFilterState));
    if (!decoder)
      return SZ_ERROR_MEM;
    decoder->buf = ISzAlloc_Alloc(alloc, BRA_BUF_SIZE);
    if (!decoder->buf)
    {
      ISzAlloc_Free(alloc, decoder);
      return SZ_ERROR_MEM;
    }
    p->p = decoder;
    p->Free     = XzBcFilterState_Free;
    p->SetProps = XzBcFilterState_SetProps;
    p->Init     = XzBcFilterState_Init;
    p->Code2    = XzBcFilterState_Code2;
    p->Filter   = XzBcFilterState_Filter;
    decoder->filter_func = func;
  }
  decoder->base.methodId = (UInt32)id;
  /* decoder->encodeMode = encodeMode; */
  return SZ_OK;
}



/* ---------- SbState ---------- */

#ifdef USE_SUBBLOCK

static void SbState_Free(void *pp, ISzAllocPtr alloc)
{
  CSbDec *p = (CSbDec *)pp;
  SbDec_Free(p);
  ISzAlloc_Free(alloc, pp);
}

static SRes SbState_SetProps(void *pp, const Byte *props, size_t propSize, ISzAllocPtr alloc)
{
  UNUSED_VAR(pp)
  UNUSED_VAR(props)
  UNUSED_VAR(alloc)
  return (propSize == 0) ? SZ_OK : SZ_ERROR_UNSUPPORTED;
}

static void SbState_Init(void *pp)
{
  SbDec_Init((CSbDec *)pp);
}

static SRes SbState_Code2(void *pp, Byte *dest, SizeT *destLen, const Byte *src, SizeT *srcLen,
    int srcWasFinished, ECoderFinishMode finishMode,
    /* int *wasFinished */
    ECoderStatus *status)
{
  CSbDec *p = (CSbDec *)pp;
  SRes res;
  UNUSED_VAR(srcWasFinished)
  p->dest = dest;
  p->destLen = *destLen;
  p->src = src;
  p->srcLen = *srcLen;
  p->finish = finishMode; /* change it */
  res = SbDec_Decode((CSbDec *)pp);
  *destLen -= p->destLen;
  *srcLen -= p->srcLen;
  /* *wasFinished = (*destLen == 0 && *srcLen == 0); /* change it *\/ */
  *status = (*destLen == 0 && *srcLen == 0) ?
      CODER_STATUS_FINISHED_WITH_MARK :
      CODER_STATUS_NOT_FINISHED;
  return res;
}

static SRes SbState_SetFromMethod(IStateCoder *p, ISzAllocPtr alloc)
{
  CSbDec *decoder = (CSbDec *)p->p;
  if (!decoder)
  {
    decoder = (CSbDec *)ISzAlloc_Alloc(alloc, sizeof(CSbDec));
    if (!decoder)
      return SZ_ERROR_MEM;
    p->p = decoder;
    p->Free = SbState_Free;
    p->SetProps = SbState_SetProps;
    p->Init = SbState_Init;
    p->Code2 = SbState_Code2;
    p->Filter = NULL;
  }
  SbDec_Construct(decoder);
  SbDec_SetAlloc(decoder, alloc);
  return SZ_OK;
}

#endif



/* ---------- Lzma2 ---------- */

typedef struct
{
  CLzma2Dec decoder;
  BoolInt outBufMode;
} CLzma2Dec_Spec;


static void Lzma2State_Free(void *pp, ISzAllocPtr alloc)
{
  CLzma2Dec_Spec *p = (CLzma2Dec_Spec *)pp;
  if (p->outBufMode)
    Lzma2Dec_FreeProbs(&p->decoder, alloc);
  else
    Lzma2Dec_Free(&p->decoder, alloc);
  ISzAlloc_Free(alloc, pp);
}

static SRes Lzma2State_SetProps(void *pp, const Byte *props, size_t propSize, ISzAllocPtr alloc)
{
  if (propSize != 1)
    return SZ_ERROR_UNSUPPORTED;
  {
    CLzma2Dec_Spec *p = (CLzma2Dec_Spec *)pp;
    if (p->outBufMode)
      return Lzma2Dec_AllocateProbs(&p->decoder, props[0], alloc);
    else
      return Lzma2Dec_Allocate(&p->decoder, props[0], alloc);
  }
}

static void Lzma2State_Init(void *pp)
{
  Lzma2Dec_Init(&((CLzma2Dec_Spec *)pp)->decoder);
}


/*
  if (outBufMode), then (dest) is not used. Use NULL.
         Data is unpacked to (spec->decoder.decoder.dic) output buffer.
*/

static SRes Lzma2State_Code2(void *pp, Byte *dest, SizeT *destLen, const Byte *src, SizeT *srcLen,
    int srcWasFinished, ECoderFinishMode finishMode,
    /* int *wasFinished, */
    ECoderStatus *status)
{
  CLzma2Dec_Spec *spec = (CLzma2Dec_Spec *)pp;
  ELzmaStatus status2;
  /* ELzmaFinishMode fm = (finishMode == LZMA_FINISH_ANY) ? LZMA_FINISH_ANY : LZMA_FINISH_END; */
  SRes res;
  UNUSED_VAR(srcWasFinished)
  if (spec->outBufMode)
  {
    SizeT dicPos = spec->decoder.decoder.dicPos;
    SizeT dicLimit = dicPos + *destLen;
    res = Lzma2Dec_DecodeToDic(&spec->decoder, dicLimit, src, srcLen, (ELzmaFinishMode)finishMode, &status2);
    *destLen = spec->decoder.decoder.dicPos - dicPos;
  }
  else
    res = Lzma2Dec_DecodeToBuf(&spec->decoder, dest, destLen, src, srcLen, (ELzmaFinishMode)finishMode, &status2);
  /* *wasFinished = (status2 == LZMA_STATUS_FINISHED_WITH_MARK); */
  /* ECoderStatus values are identical to ELzmaStatus values of LZMA2 decoder */
  *status = (ECoderStatus)status2;
  return res;
}


static SRes Lzma2State_SetFromMethod(IStateCoder *p, Byte *outBuf, size_t outBufSize, ISzAllocPtr alloc)
{
  CLzma2Dec_Spec *spec = (CLzma2Dec_Spec *)p->p;
  if (!spec)
  {
    spec = (CLzma2Dec_Spec *)ISzAlloc_Alloc(alloc, sizeof(CLzma2Dec_Spec));
    if (!spec)
      return SZ_ERROR_MEM;
    p->p = spec;
    p->Free = Lzma2State_Free;
    p->SetProps = Lzma2State_SetProps;
    p->Init = Lzma2State_Init;
    p->Code2 = Lzma2State_Code2;
    p->Filter = NULL;
    Lzma2Dec_CONSTRUCT(&spec->decoder)
  }
  spec->outBufMode = False;
  if (outBuf)
  {
    spec->outBufMode = True;
    spec->decoder.decoder.dic = outBuf;
    spec->decoder.decoder.dicBufSize = outBufSize;
  }
  return SZ_OK;
}


static SRes Lzma2State_ResetOutBuf(IStateCoder *p, Byte *outBuf, size_t outBufSize)
{
  CLzma2Dec_Spec *spec = (CLzma2Dec_Spec *)p->p;
  if ((spec->outBufMode && !outBuf) || (!spec->outBufMode && outBuf))
    return SZ_ERROR_FAIL;
  if (outBuf)
  {
    spec->decoder.decoder.dic = outBuf;
    spec->decoder.decoder.dicBufSize = outBufSize;
  }
  return SZ_OK;
}



static void MixCoder_Construct(CMixCoder *p, ISzAllocPtr alloc)
{
  unsigned i;
  p->alloc = alloc;
  p->buf = NULL;
  p->numCoders = 0;
  
  p->outBufSize = 0;
  p->outBuf = NULL;
  /* p->SingleBufMode = False; */

  for (i = 0; i < MIXCODER_NUM_FILTERS_MAX; i++)
    p->coders[i].p = NULL;
}


static void MixCoder_Free(CMixCoder *p)
{
  unsigned i;
  p->numCoders = 0;
  for (i = 0; i < MIXCODER_NUM_FILTERS_MAX; i++)
  {
    IStateCoder *sc = &p->coders[i];
    if (sc->p)
    {
      sc->Free(sc->p, p->alloc);
      sc->p = NULL;
    }
  }
  if (p->buf)
  {
    ISzAlloc_Free(p->alloc, p->buf);
    p->buf = NULL; /* 9.31: the BUG was fixed */
  }
}

static void MixCoder_Init(CMixCoder *p)
{
  unsigned i;
  for (i = 0; i < MIXCODER_NUM_FILTERS_MAX - 1; i++)
  {
    p->size[i] = 0;
    p->pos[i] = 0;
    p->finished[i] = 0;
  }
  for (i = 0; i < p->numCoders; i++)
  {
    IStateCoder *coder = &p->coders[i];
    coder->Init(coder->p);
    p->results[i] = SZ_OK;
  }
  p->outWritten = 0;
  p->wasFinished = False;
  p->res = SZ_OK;
  p->status = CODER_STATUS_NOT_SPECIFIED;
}


static SRes MixCoder_SetFromMethod(CMixCoder *p, unsigned coderIndex, UInt64 methodId, Byte *outBuf, size_t outBufSize)
{
  IStateCoder *sc = &p->coders[coderIndex];
  p->ids[coderIndex] = methodId;
  if (methodId == XZ_ID_LZMA2)
    return Lzma2State_SetFromMethod(sc, outBuf, outBufSize, p->alloc);
#ifdef USE_SUBBLOCK
  if (methodId == XZ_ID_Subblock)
    return SbState_SetFromMethod(sc, p->alloc);
#endif
  if (coderIndex == 0)
    return SZ_ERROR_UNSUPPORTED;
  return Xz_StateCoder_Bc_SetFromMethod_Func(sc, methodId,
      XzBcFilterStateBase_Filter_Dec, p->alloc);
}


static SRes MixCoder_ResetFromMethod(CMixCoder *p, unsigned coderIndex, UInt64 methodId, Byte *outBuf, size_t outBufSize)
{
  IStateCoder *sc = &p->coders[coderIndex];
  if (methodId == XZ_ID_LZMA2)
    return Lzma2State_ResetOutBuf(sc, outBuf, outBufSize);
  return SZ_ERROR_UNSUPPORTED;
}



/*
 if (destFinish) - then unpack data block is finished at (*destLen) position,
                   and we can return data that were not processed by filter

output (status) can be :
  CODER_STATUS_NOT_FINISHED
  CODER_STATUS_FINISHED_WITH_MARK
  CODER_STATUS_NEEDS_MORE_INPUT - not implemented still
*/

static SRes MixCoder_Code(CMixCoder *p,
    Byte *dest, SizeT *destLen, int destFinish,
    const Byte *src, SizeT *srcLen, int srcWasFinished,
    ECoderFinishMode finishMode)
{
  SizeT destLenOrig = *destLen;
  SizeT srcLenOrig = *srcLen;

  *destLen = 0;
  *srcLen = 0;

  if (p->wasFinished)
    return p->res;
  
  p->status = CODER_STATUS_NOT_FINISHED;

  /* if (p->SingleBufMode) */
  if (p->outBuf)
  {
    SRes res;
    SizeT destLen2, srcLen2;
    int wasFinished;
    
    PRF_STR("------- MixCoder Single ----------")
      
    srcLen2 = srcLenOrig;
    destLen2 = destLenOrig;
    
    {
      IStateCoder *coder = &p->coders[0];
      res = coder->Code2(coder->p, NULL, &destLen2, src, &srcLen2, srcWasFinished, finishMode,
          /* &wasFinished, */
          &p->status);
      wasFinished = (p->status == CODER_STATUS_FINISHED_WITH_MARK);
    }
    
    p->res = res;
    
    /*
    if (wasFinished)
      p->status = CODER_STATUS_FINISHED_WITH_MARK;
    else
    {
      if (res == SZ_OK)
        if (destLen2 != destLenOrig)
          p->status = CODER_STATUS_NEEDS_MORE_INPUT;
    }
    */

    
    *srcLen = srcLen2;
    src += srcLen2;
    p->outWritten += destLen2;
    
    if (res != SZ_OK || srcWasFinished || wasFinished)
      p->wasFinished = True;
    
    if (p->numCoders == 1)
      *destLen = destLen2;
    else if (p->wasFinished)
    {
      unsigned i;
      size_t processed = p->outWritten;
      
      for (i = 1; i < p->numCoders; i++)
      {
        IStateCoder *coder = &p->coders[i];
        processed = coder->Filter(coder->p, p->outBuf, processed);
        if (wasFinished || (destFinish && p->outWritten == destLenOrig))
          processed = p->outWritten;
        PRF_STR_INT("filter", i)
      }
      *destLen = processed;
    }
    return res;
  }

  PRF_STR("standard mix")

  if (p->numCoders != 1)
  {
    if (!p->buf)
    {
      p->buf = (Byte *)ISzAlloc_Alloc(p->alloc, CODER_BUF_SIZE * (MIXCODER_NUM_FILTERS_MAX - 1));
      if (!p->buf)
        return SZ_ERROR_MEM;
    }
    
    finishMode = CODER_FINISH_ANY;
  }

  for (;;)
  {
    BoolInt processed = False;
    BoolInt allFinished = True;
    SRes resMain = SZ_OK;
    unsigned i;

    p->status = CODER_STATUS_NOT_FINISHED;
    /*
    if (p->numCoders == 1 && *destLen == destLenOrig && finishMode == LZMA_FINISH_ANY)
      break;
    */

    for (i = 0; i < p->numCoders; i++)
    {
      SRes res;
      IStateCoder *coder = &p->coders[i];
      Byte *dest2;
      SizeT destLen2, srcLen2; /* destLen2_Orig; */
      const Byte *src2;
      int srcFinished2;
      int encodingWasFinished;
      ECoderStatus status2;
      
      if (i == 0)
      {
        src2 = src;
        srcLen2 = srcLenOrig - *srcLen;
        srcFinished2 = srcWasFinished;
      }
      else
      {
        size_t k = i - 1;
        src2 = p->buf + (CODER_BUF_SIZE * k) + p->pos[k];
        srcLen2 = p->size[k] - p->pos[k];
        srcFinished2 = p->finished[k];
      }
      
      if (i == p->numCoders - 1)
      {
        dest2 = dest;
        destLen2 = destLenOrig - *destLen;
      }
      else
      {
        if (p->pos[i] != p->size[i])
          continue;
        dest2 = p->buf + (CODER_BUF_SIZE * i);
        destLen2 = CODER_BUF_SIZE;
      }
      
      /* destLen2_Orig = destLen2; */
      
      if (p->results[i] != SZ_OK)
      {
        if (resMain == SZ_OK)
          resMain = p->results[i];
        continue;
      }

      res = coder->Code2(coder->p,
          dest2, &destLen2,
          src2, &srcLen2, srcFinished2,
          finishMode,
          /* &encodingWasFinished, */
          &status2);

      if (res != SZ_OK)
      {
        p->results[i] = res;
        if (resMain == SZ_OK)
          resMain = res;
      }

      encodingWasFinished = (status2 == CODER_STATUS_FINISHED_WITH_MARK);
      
      if (!encodingWasFinished)
      {
        allFinished = False;
        if (p->numCoders == 1 && res == SZ_OK)
          p->status = status2;
      }

      if (i == 0)
      {
        *srcLen += srcLen2;
        src += srcLen2;
      }
      else
        p->pos[(size_t)i - 1] += srcLen2;

      if (i == p->numCoders - 1)
      {
        *destLen += destLen2;
        dest += destLen2;
      }
      else
      {
        p->size[i] = destLen2;
        p->pos[i] = 0;
        p->finished[i] = encodingWasFinished;
      }
      
      if (destLen2 != 0 || srcLen2 != 0)
        processed = True;
    }
    
    if (!processed)
    {
      if (allFinished)
        p->status = CODER_STATUS_FINISHED_WITH_MARK;
      return resMain;
    }
  }
}


SRes Xz_ParseHeader(CXzStreamFlags *p, const Byte *buf)
{
  *p = (CXzStreamFlags)GetBe16(buf + XZ_SIG_SIZE);
  if (CrcCalc(buf + XZ_SIG_SIZE, XZ_STREAM_FLAGS_SIZE) !=
      GetUi32(buf + XZ_SIG_SIZE + XZ_STREAM_FLAGS_SIZE))
    return SZ_ERROR_NO_ARCHIVE;
  return XzFlags_IsSupported(*p) ? SZ_OK : SZ_ERROR_UNSUPPORTED;
}

static BoolInt Xz_CheckFooter(CXzStreamFlags flags, UInt64 indexSize, const Byte *buf)
{
  return indexSize == (((UInt64)GetUi32a(buf + 4) + 1) << 2)
      && GetUi32a(buf) == CrcCalc(buf + 4, 6)
      && flags == GetBe16a(buf + 8)
      && GetUi16a(buf + 10) == (XZ_FOOTER_SIG_0 | (XZ_FOOTER_SIG_1 << 8));
}

#define READ_VARINT_AND_CHECK(buf, pos, size, res) \
  { const unsigned s = Xz_ReadVarInt(buf + pos, size - pos, res); \
  if (s == 0) return SZ_ERROR_ARCHIVE; \
  pos += s; }


static BoolInt XzBlock_AreSupportedFilters(const CXzBlock *p)
{
  const unsigned numFilters = XzBlock_GetNumFilters(p) - 1;
  unsigned i;
  {
    const CXzFilter *f = &p->filters[numFilters];
    if (f->id != XZ_ID_LZMA2 || f->propsSize != 1 || f->props[0] > 40)
      return False;
  }

  for (i = 0; i < numFilters; i++)
  {
    const CXzFilter *f = &p->filters[i];
    if (f->id == XZ_ID_Delta)
    {
      if (f->propsSize != 1)
        return False;
    }
    else if (!XZ_IS_SUPPORTED_FILTER_ID(f->id)
        || (f->propsSize != 0 && f->propsSize != 4))
      return False;
  }
  return True;
}


SRes XzBlock_Parse(CXzBlock *p, const Byte *header)
{
  unsigned pos;
  unsigned numFilters, i;
  unsigned headerSize = (unsigned)header[0] << 2;

  /* (headerSize != 0) : another code checks */

  if (CrcCalc(header, headerSize) != GetUi32(header + headerSize))
    return SZ_ERROR_ARCHIVE;

  pos = 1;
  p->flags = header[pos++];

  p->packSize = (UInt64)(Int64)-1;
  if (XzBlock_HasPackSize(p))
  {
    READ_VARINT_AND_CHECK(header, pos, headerSize, &p->packSize)
    if (p->packSize == 0 || p->packSize + headerSize >= (UInt64)1 << 63)
      return SZ_ERROR_ARCHIVE;
  }

  p->unpackSize = (UInt64)(Int64)-1;
  if (XzBlock_HasUnpackSize(p))
  {
    READ_VARINT_AND_CHECK(header, pos, headerSize, &p->unpackSize)
  }

  numFilters = XzBlock_GetNumFilters(p);
  for (i = 0; i < numFilters; i++)
  {
    CXzFilter *filter = p->filters + i;
    UInt64 size;
    READ_VARINT_AND_CHECK(header, pos, headerSize, &filter->id)
    READ_VARINT_AND_CHECK(header, pos, headerSize, &size)
    if (size > headerSize - pos || size > XZ_FILTER_PROPS_SIZE_MAX)
      return SZ_ERROR_ARCHIVE;
    filter->propsSize = (UInt32)size;
    memcpy(filter->props, header + pos, (size_t)size);
    pos += (unsigned)size;

    #ifdef XZ_DUMP
    printf("\nf[%u] = %2X: ", i, (unsigned)filter->id);
    {
      unsigned i;
      for (i = 0; i < size; i++)
        printf(" %2X", filter->props[i]);
    }
    #endif
  }

  if (XzBlock_HasUnsupportedFlags(p))
    return SZ_ERROR_UNSUPPORTED;

  while (pos < headerSize)
    if (header[pos++] != 0)
      return SZ_ERROR_ARCHIVE;
  return SZ_OK;
}




static SRes XzDecMix_Init(CMixCoder *p, const CXzBlock *block, Byte *outBuf, size_t outBufSize)
{
  unsigned i;
  BoolInt needReInit = True;
  unsigned numFilters = XzBlock_GetNumFilters(block);

  if (numFilters == p->numCoders && ((p->outBuf && outBuf) || (!p->outBuf && !outBuf)))
  {
    needReInit = False;
    for (i = 0; i < numFilters; i++)
      if (p->ids[i] != block->filters[numFilters - 1 - i].id)
      {
        needReInit = True;
        break;
      }
  }

  /* p->SingleBufMode = (outBuf != NULL); */
  p->outBuf = outBuf;
  p->outBufSize = outBufSize;

  /* p->SingleBufMode = False; */
  /* outBuf = NULL; */
  
  if (needReInit)
  {
    MixCoder_Free(p);
    for (i = 0; i < numFilters; i++)
    {
      RINOK(MixCoder_SetFromMethod(p, i, block->filters[numFilters - 1 - i].id, outBuf, outBufSize))
    }
    p->numCoders = numFilters;
  }
  else
  {
    RINOK(MixCoder_ResetFromMethod(p, 0, block->filters[numFilters - 1].id, outBuf, outBufSize))
  }

  for (i = 0; i < numFilters; i++)
  {
    const CXzFilter *f = &block->filters[numFilters - 1 - i];
    IStateCoder *sc = &p->coders[i];
    RINOK(sc->SetProps(sc->p, f->props, f->propsSize, p->alloc))
  }
  
  MixCoder_Init(p);
  return SZ_OK;
}



void XzUnpacker_Init(CXzUnpacker *p)
{
  p->state = XZ_STATE_STREAM_HEADER;
  p->pos = 0;
  p->numStartedStreams = 0;
  p->numFinishedStreams = 0;
  p->numTotalBlocks = 0;
  p->padSize = 0;
  p->decodeOnlyOneBlock = 0;

  p->parseMode = False;
  p->decodeToStreamSignature = False;

  /* p->outBuf = NULL; */
  /* p->outBufSize = 0; */
  p->outDataWritten = 0;
}


void XzUnpacker_SetOutBuf(CXzUnpacker *p, Byte *outBuf, size_t outBufSize)
{
  p->outBuf = outBuf;
  p->outBufSize = outBufSize;
}


void XzUnpacker_Construct(CXzUnpacker *p, ISzAllocPtr alloc)
{
  MixCoder_Construct(&p->decoder, alloc);
  p->outBuf = NULL;
  p->outBufSize = 0;
  XzUnpacker_Init(p);
}


void XzUnpacker_Free(CXzUnpacker *p)
{
  MixCoder_Free(&p->decoder);
}


void XzUnpacker_PrepareToRandomBlockDecoding(CXzUnpacker *p)
{
  p->indexSize = 0;
  p->numBlocks = 0;
  Sha256_Init(&p->sha);
  p->state = XZ_STATE_BLOCK_HEADER;
  p->pos = 0;
  p->decodeOnlyOneBlock = 1;
}


static void XzUnpacker_UpdateIndex(CXzUnpacker *p, UInt64 packSize, UInt64 unpackSize)
{
  Byte temp[32];
  unsigned num = Xz_WriteVarInt(temp, packSize);
  num += Xz_WriteVarInt(temp + num, unpackSize);
  Sha256_Update(&p->sha, temp, num);
  p->indexSize += num;
  p->numBlocks++;
}



SRes XzUnpacker_Code(CXzUnpacker *p, Byte *dest, SizeT *destLen,
    const Byte *src, SizeT *srcLen, int srcFinished,
    ECoderFinishMode finishMode, ECoderStatus *status)
{
  SizeT destLenOrig = *destLen;
  SizeT srcLenOrig = *srcLen;
  *destLen = 0;
  *srcLen = 0;
  *status = CODER_STATUS_NOT_SPECIFIED;

  for (;;)
  {
    SizeT srcRem;

    if (p->state == XZ_STATE_BLOCK)
    {
      SizeT destLen2 = destLenOrig - *destLen;
      SizeT srcLen2 = srcLenOrig - *srcLen;
      SRes res;

      ECoderFinishMode finishMode2 = finishMode;
      BoolInt srcFinished2 = (BoolInt)srcFinished;
      BoolInt destFinish = False;

      if (p->block.packSize != (UInt64)(Int64)-1)
      {
        UInt64 rem = p->block.packSize - p->packSize;
        if (srcLen2 >= rem)
        {
          srcFinished2 = True;
          srcLen2 = (SizeT)rem;
        }
        if (rem == 0 && p->block.unpackSize == p->unpackSize)
          return SZ_ERROR_DATA;
      }

      if (p->block.unpackSize != (UInt64)(Int64)-1)
      {
        UInt64 rem = p->block.unpackSize - p->unpackSize;
        if (destLen2 >= rem)
        {
          destFinish = True;
          finishMode2 = CODER_FINISH_END;
          destLen2 = (SizeT)rem;
        }
      }

      /*
      if (srcLen2 == 0 && destLen2 == 0)
      {
        *status = CODER_STATUS_NOT_FINISHED;
        return SZ_OK;
      }
      */
      
      {
        res = MixCoder_Code(&p->decoder,
            (p->outBuf ? NULL : dest), &destLen2, destFinish,
            src, &srcLen2, srcFinished2,
            finishMode2);

        *status = p->decoder.status;
        XzCheck_Update(&p->check, (p->outBuf ? p->outBuf + p->outDataWritten : dest), destLen2);
        if (!p->outBuf)
          dest += destLen2;
        p->outDataWritten += destLen2;
      }
      
      (*srcLen) += srcLen2;
      src += srcLen2;
      p->packSize += srcLen2;
      (*destLen) += destLen2;
      p->unpackSize += destLen2;

      RINOK(res)

      if (*status != CODER_STATUS_FINISHED_WITH_MARK)
      {
        if (p->block.packSize == p->packSize
            && *status == CODER_STATUS_NEEDS_MORE_INPUT)
        {
          PRF_STR("CODER_STATUS_NEEDS_MORE_INPUT")
          *status = CODER_STATUS_NOT_SPECIFIED;
          return SZ_ERROR_DATA;
        }
        
        return SZ_OK;
      }
      {
        XzUnpacker_UpdateIndex(p, XzUnpacker_GetPackSizeForIndex(p), p->unpackSize);
        p->state = XZ_STATE_BLOCK_FOOTER;
        p->pos = 0;
        p->alignPos = 0;
        *status = CODER_STATUS_NOT_SPECIFIED;

        if ((p->block.packSize != (UInt64)(Int64)-1 && p->block.packSize != p->packSize)
           || (p->block.unpackSize != (UInt64)(Int64)-1 && p->block.unpackSize != p->unpackSize))
        {
          PRF_STR("ERROR: block.size mismatch")
          return SZ_ERROR_DATA;
        }
      }
      /* continue; */
    }

    srcRem = srcLenOrig - *srcLen;

    /* XZ_STATE_BLOCK_FOOTER can transit to XZ_STATE_BLOCK_HEADER without input bytes */
    if (srcRem == 0 && p->state != XZ_STATE_BLOCK_FOOTER)
    {
      *status = CODER_STATUS_NEEDS_MORE_INPUT;
      return SZ_OK;
    }

    switch ((int)p->state)
    {
      case XZ_STATE_STREAM_HEADER:
      {
        if (p->pos < XZ_STREAM_HEADER_SIZE)
        {
          if (p->pos < XZ_SIG_SIZE && *src != XZ_SIG[p->pos])
            return SZ_ERROR_NO_ARCHIVE;
          if (p->decodeToStreamSignature)
            return SZ_OK;
          p->buf[p->pos++] = *src++;
          (*srcLen)++;
        }
        else
        {
          RINOK(Xz_ParseHeader(&p->streamFlags, p->buf))
          p->numStartedStreams++;
          p->indexSize = 0;
          p->numBlocks = 0;
          Sha256_Init(&p->sha);
          p->state = XZ_STATE_BLOCK_HEADER;
          p->pos = 0;
        }
        break;
      }

      case XZ_STATE_BLOCK_HEADER:
      {
        if (p->pos == 0)
        {
          p->buf[p->pos++] = *src++;
          (*srcLen)++;
          if (p->buf[0] == 0)
          {
            if (p->decodeOnlyOneBlock)
              return SZ_ERROR_DATA;
            p->indexPreSize = 1 + Xz_WriteVarInt(p->buf + 1, p->numBlocks);
            p->indexPos = p->indexPreSize;
            p->indexSize += p->indexPreSize;
            Sha256_Final(&p->sha, (Byte *)(void *)p->shaDigest32);
            Sha256_Init(&p->sha);
            p->crc = CrcUpdate(CRC_INIT_VAL, p->buf, p->indexPreSize);
            p->state = XZ_STATE_STREAM_INDEX;
            break;
          }
          p->blockHeaderSize = ((unsigned)p->buf[0] << 2) + 4;
          break;
        }
        
        if (p->pos != p->blockHeaderSize)
        {
          unsigned cur = p->blockHeaderSize - p->pos;
          if (cur > srcRem)
            cur = (unsigned)srcRem;
          memcpy(p->buf + p->pos, src, cur);
          p->pos += cur;
          (*srcLen) += cur;
          src += cur;
        }
        else
        {
          RINOK(XzBlock_Parse(&p->block, p->buf))
          if (!XzBlock_AreSupportedFilters(&p->block))
            return SZ_ERROR_UNSUPPORTED;
          p->numTotalBlocks++;
          p->state = XZ_STATE_BLOCK;
          p->packSize = 0;
          p->unpackSize = 0;
          XzCheck_Init(&p->check, XzFlags_GetCheckType(p->streamFlags));
          if (p->parseMode)
          {
            p->headerParsedOk = True;
            return SZ_OK;
          }
          RINOK(XzDecMix_Init(&p->decoder, &p->block, p->outBuf, p->outBufSize))
        }
        break;
      }

      case XZ_STATE_BLOCK_FOOTER:
      {
        if ((((unsigned)p->packSize + p->alignPos) & 3) != 0)
        {
          if (srcRem == 0)
          {
            *status = CODER_STATUS_NEEDS_MORE_INPUT;
            return SZ_OK;
          }
          (*srcLen)++;
          p->alignPos++;
          if (*src++ != 0)
            return SZ_ERROR_CRC;
        }
        else
        {
          const unsigned checkSize = XzFlags_GetCheckSize(p->streamFlags);
          unsigned cur = checkSize - p->pos;
          if (cur != 0)
          {
            if (srcRem == 0)
            {
              *status = CODER_STATUS_NEEDS_MORE_INPUT;
              return SZ_OK;
            }
            if (cur > srcRem)
              cur = (unsigned)srcRem;
            memcpy(p->buf + p->pos, src, cur);
            p->pos += cur;
            (*srcLen) += cur;
            src += cur;
            if (checkSize != p->pos)
              break;
          }
          {
            UInt32 digest32[XZ_CHECK_SIZE_MAX / 4];
            p->state = XZ_STATE_BLOCK_HEADER;
            p->pos = 0;
            if (XzCheck_Final(&p->check, (void *)digest32) && memcmp(digest32, p->buf, checkSize) != 0)
              return SZ_ERROR_CRC;
            if (p->decodeOnlyOneBlock)
            {
              *status = CODER_STATUS_FINISHED_WITH_MARK;
              return SZ_OK;
            }
          }
        }
        break;
      }

      case XZ_STATE_STREAM_INDEX:
      {
        if (p->pos < p->indexPreSize)
        {
          (*srcLen)++;
          if (*src++ != p->buf[p->pos++])
            return SZ_ERROR_CRC;
        }
        else
        {
          if (p->indexPos < p->indexSize)
          {
            UInt64 cur = p->indexSize - p->indexPos;
            if (srcRem > cur)
              srcRem = (SizeT)cur;
            p->crc = CrcUpdate(p->crc, src, srcRem);
            Sha256_Update(&p->sha, src, srcRem);
            (*srcLen) += srcRem;
            src += srcRem;
            p->indexPos += srcRem;
          }
          else if ((p->indexPos & 3) != 0)
          {
            Byte b = *src++;
            p->crc = CRC_UPDATE_BYTE(p->crc, b);
            (*srcLen)++;
            p->indexPos++;
            p->indexSize++;
            if (b != 0)
              return SZ_ERROR_CRC;
          }
          else
          {
            UInt32 digest32[SHA256_DIGEST_SIZE / 4];
            p->state = XZ_STATE_STREAM_INDEX_CRC;
            p->indexSize += 4;
            p->pos = 0;
            Sha256_Final(&p->sha, (void *)digest32);
            if (memcmp(digest32, p->shaDigest32, SHA256_DIGEST_SIZE) != 0)
              return SZ_ERROR_CRC;
          }
        }
        break;
      }

      case XZ_STATE_STREAM_INDEX_CRC:
      {
        if (p->pos < 4)
        {
          (*srcLen)++;
          p->buf[p->pos++] = *src++;
        }
        else
        {
          const Byte *ptr = p->buf;
          p->state = XZ_STATE_STREAM_FOOTER;
          p->pos = 0;
          if (CRC_GET_DIGEST(p->crc) != GetUi32a(ptr))
            return SZ_ERROR_CRC;
        }
        break;
      }

      case XZ_STATE_STREAM_FOOTER:
      {
        unsigned cur = XZ_STREAM_FOOTER_SIZE - p->pos;
        if (cur > srcRem)
          cur = (unsigned)srcRem;
        memcpy(p->buf + p->pos, src, cur);
        p->pos += cur;
        (*srcLen) += cur;
        src += cur;
        if (p->pos == XZ_STREAM_FOOTER_SIZE)
        {
          p->state = XZ_STATE_STREAM_PADDING;
          p->numFinishedStreams++;
          p->padSize = 0;
          if (!Xz_CheckFooter(p->streamFlags, p->indexSize, p->buf))
            return SZ_ERROR_CRC;
        }
        break;
      }

      case XZ_STATE_STREAM_PADDING:
      {
        if (*src != 0)
        {
          if ((unsigned)p->padSize & 3)
            return SZ_ERROR_NO_ARCHIVE;
          p->pos = 0;
          p->state = XZ_STATE_STREAM_HEADER;
        }
        else
        {
          (*srcLen)++;
          src++;
          p->padSize++;
        }
        break;
      }
      
      case XZ_STATE_BLOCK: break; /* to disable GCC warning */
      
      default: return SZ_ERROR_FAIL;
    }
  }
  /*
  if (p->state == XZ_STATE_FINISHED)
    *status = CODER_STATUS_FINISHED_WITH_MARK;
  return SZ_OK;
  */
}


SRes XzUnpacker_CodeFull(CXzUnpacker *p, Byte *dest, SizeT *destLen,
    const Byte *src, SizeT *srcLen,
    ECoderFinishMode finishMode, ECoderStatus *status)
{
  XzUnpacker_Init(p);
  XzUnpacker_SetOutBuf(p, dest, *destLen);

  return XzUnpacker_Code(p,
      NULL, destLen,
      src, srcLen, True,
      finishMode, status);
}


BoolInt XzUnpacker_IsBlockFinished(const CXzUnpacker *p)
{
  return (p->state == XZ_STATE_BLOCK_HEADER) && (p->pos == 0);
}

BoolInt XzUnpacker_IsStreamWasFinished(const CXzUnpacker *p)
{
  return (p->state == XZ_STATE_STREAM_PADDING) && (((UInt32)p->padSize & 3) == 0);
}

UInt64 XzUnpacker_GetExtraSize(const CXzUnpacker *p)
{
  UInt64 num = 0;
  if (p->state == XZ_STATE_STREAM_PADDING)
    num = p->padSize;
  else if (p->state == XZ_STATE_STREAM_HEADER)
    num = p->padSize + p->pos;
  return num;
}





















#ifndef Z7_ST
/* ==== MtDec.h ==== */
/* MtDec.h -- Multi-thread Decoder
2023-04-02 : Igor Pavlov : Public domain */

#ifndef ZIP7_INC_MT_DEC_H
#define ZIP7_INC_MT_DEC_H



#ifndef Z7_ST
/* ==== Threads.h ==== */
/* Threads.h -- multithreading library
: Igor Pavlov : Public domain */

#ifndef ZIP7_INC_THREADS_H
#define ZIP7_INC_THREADS_H

#ifdef _WIN32


#else



/* #define Z7_AFFINITY_DISABLE */
#if defined(__linux__)
#if !defined(__APPLE__) && !defined(_AIX) && !defined(__ANDROID__)
#ifndef Z7_AFFINITY_DISABLE
#define Z7_AFFINITY_SUPPORTED
/* #pragma message(" ==== Z7_AFFINITY_SUPPORTED") */
#if !defined(_GNU_SOURCE)
/* #pragma message(" ==== _GNU_SOURCE set") */
/* we need _GNU_SOURCE for cpu_set_t, if we compile for MUSL */
Z7_DIAGNOSTIC_IGNORE_BEGIN_RESERVED_MACRO_IDENTIFIER
#define _GNU_SOURCE
Z7_DIAGNOSTIC_IGNORE_END_RESERVED_MACRO_IDENTIFIER
#endif
#endif
#endif
#endif

#include <pthread.h>

#endif



EXTERN_C_BEGIN

#ifdef _WIN32

WRes HandlePtr_Close(HANDLE *h);
WRes Handle_WaitObject(HANDLE h);

typedef HANDLE CThread;

#define Thread_CONSTRUCT(p) { *(p) = NULL; }
#define Thread_WasCreated(p) (*(p) != NULL)
#define Thread_Close(p) HandlePtr_Close(p)
/* #define Thread_Wait(p) Handle_WaitObject(*(p)) */

#ifdef UNDER_CE
  /* if (USE_THREADS_CreateThread is      defined), we use _beginthreadex() */
  /* if (USE_THREADS_CreateThread is not definned), we use CreateThread() */
  #define USE_THREADS_CreateThread
#endif

typedef
    #ifdef USE_THREADS_CreateThread
      DWORD
    #else
      unsigned
    #endif
    THREAD_FUNC_RET_TYPE;

#define THREAD_FUNC_RET_ZERO  0

typedef DWORD_PTR CAffinityMask;
typedef DWORD_PTR CCpuSet;

#define CpuSet_Zero(p)        *(p) = (0)
#define CpuSet_Set(p, cpu)    *(p) |= ((DWORD_PTR)1 << (cpu))

#else /*  _WIN32 */

typedef struct
{
  pthread_t _tid;
  int _created;
} CThread;

#define Thread_CONSTRUCT(p)   { (p)->_tid = 0;  (p)->_created = 0; }
#define Thread_WasCreated(p)  ((p)->_created != 0)
WRes Thread_Close(CThread *p);
/* #define Thread_Wait Thread_Wait_Close */

typedef void * THREAD_FUNC_RET_TYPE;
#define THREAD_FUNC_RET_ZERO  NULL


typedef UInt64 CAffinityMask;

#ifdef Z7_AFFINITY_SUPPORTED

typedef cpu_set_t CCpuSet;
#define CpuSet_Zero(p)        CPU_ZERO(p)
#define CpuSet_Set(p, cpu)    CPU_SET(cpu, p)
#define CpuSet_IsSet(p, cpu)  CPU_ISSET(cpu, p)

#else

typedef UInt64 CCpuSet;
#define CpuSet_Zero(p)        *(p) = (0)
#define CpuSet_Set(p, cpu)    *(p) |= ((UInt64)1 << (cpu))
#define CpuSet_IsSet(p, cpu)  ((*(p) & ((UInt64)1 << (cpu))) != 0)

#endif


#endif /*  _WIN32 */


#define THREAD_FUNC_CALL_TYPE Z7_STDCALL

#if defined(_WIN32) && defined(__GNUC__)
/* GCC compiler for x86 32-bit uses the rule:
   the stack is 16-byte aligned before CALL instruction for function calling.
   But only root function main() contains instructions that
   set 16-byte alignment for stack pointer. And another functions
   just keep alignment, if it was set in some parent function.
   
   The problem:
    if we create new thread in MinGW (GCC) 32-bit x86 via _beginthreadex() or CreateThread(),
       the root function of thread doesn't set 16-byte alignment.
       And stack frames in all child functions also will be unaligned in that case.
   
   Here we set (force_align_arg_pointer) attribute for root function of new thread.
   Do we need (force_align_arg_pointer) also for another systems?  */
  
  #define THREAD_FUNC_ATTRIB_ALIGN_ARG __attribute__((force_align_arg_pointer))
  /* #define THREAD_FUNC_ATTRIB_ALIGN_ARG // for debug : bad alignment in SSE functions */
#else
  #define THREAD_FUNC_ATTRIB_ALIGN_ARG
#endif

#define THREAD_FUNC_DECL  THREAD_FUNC_ATTRIB_ALIGN_ARG THREAD_FUNC_RET_TYPE THREAD_FUNC_CALL_TYPE

typedef THREAD_FUNC_RET_TYPE (THREAD_FUNC_CALL_TYPE * THREAD_FUNC_TYPE)(void *);
WRes Thread_Create(CThread *p, THREAD_FUNC_TYPE func, LPVOID param);
WRes Thread_Create_With_Affinity(CThread *p, THREAD_FUNC_TYPE func, LPVOID param, CAffinityMask affinity);
WRes Thread_Wait_Close(CThread *p);

#ifdef _WIN32
WRes Thread_Create_With_Group(CThread *p, THREAD_FUNC_TYPE func, LPVOID param, unsigned group, CAffinityMask affinityMask);
#define Thread_Create_With_CpuSet(p, func, param, cs) \
  Thread_Create_With_Affinity(p, func, param, *cs)
#else
WRes Thread_Create_With_CpuSet(CThread *p, THREAD_FUNC_TYPE func, LPVOID param, const CCpuSet *cpuSet);
#endif

typedef struct
{
  unsigned NumGroups;
  unsigned NextGroup;
} CThreadNextGroup;

void ThreadNextGroup_Init(CThreadNextGroup *p, unsigned numGroups, unsigned startGroup);
unsigned ThreadNextGroup_GetNext(CThreadNextGroup *p);


#ifdef _WIN32

typedef HANDLE CEvent;
typedef CEvent CAutoResetEvent;
typedef CEvent CManualResetEvent;
#define Event_Construct(p) *(p) = NULL
#define Event_IsCreated(p) (*(p) != NULL)
#define Event_Close(p) HandlePtr_Close(p)
#define Event_Wait(p) Handle_WaitObject(*(p))
WRes Event_Set(CEvent *p);
WRes Event_Reset(CEvent *p);
WRes ManualResetEvent_Create(CManualResetEvent *p, int signaled);
WRes ManualResetEvent_CreateNotSignaled(CManualResetEvent *p);
WRes AutoResetEvent_Create(CAutoResetEvent *p, int signaled);
WRes AutoResetEvent_CreateNotSignaled(CAutoResetEvent *p);

typedef HANDLE CSemaphore;
#define Semaphore_Construct(p) *(p) = NULL
#define Semaphore_IsCreated(p) (*(p) != NULL)
#define Semaphore_Close(p) HandlePtr_Close(p)
#define Semaphore_Wait(p) Handle_WaitObject(*(p))
WRes Semaphore_Create(CSemaphore *p, UInt32 initCount, UInt32 maxCount);
WRes Semaphore_OptCreateInit(CSemaphore *p, UInt32 initCount, UInt32 maxCount);
WRes Semaphore_ReleaseN(CSemaphore *p, UInt32 num);
WRes Semaphore_Release1(CSemaphore *p);

typedef CRITICAL_SECTION CCriticalSection;
WRes CriticalSection_Init(CCriticalSection *p);
#define CriticalSection_Delete(p) DeleteCriticalSection(p)
#define CriticalSection_Enter(p) EnterCriticalSection(p)
#define CriticalSection_Leave(p) LeaveCriticalSection(p)


#else /* _WIN32 */

typedef struct
{
  int _created;
  int _manual_reset;
  int _state;
  pthread_mutex_t _mutex;
  pthread_cond_t _cond;
} CEvent;

typedef CEvent CAutoResetEvent;
typedef CEvent CManualResetEvent;

#define Event_Construct(p) (p)->_created = 0
#define Event_IsCreated(p) ((p)->_created)

WRes ManualResetEvent_Create(CManualResetEvent *p, int signaled);
WRes ManualResetEvent_CreateNotSignaled(CManualResetEvent *p);
WRes AutoResetEvent_Create(CAutoResetEvent *p, int signaled);
WRes AutoResetEvent_CreateNotSignaled(CAutoResetEvent *p);

WRes Event_Set(CEvent *p);
WRes Event_Reset(CEvent *p);
WRes Event_Wait(CEvent *p);
WRes Event_Close(CEvent *p);


typedef struct
{
  int _created;
  UInt32 _count;
  UInt32 _maxCount;
  pthread_mutex_t _mutex;
  pthread_cond_t _cond;
} CSemaphore;

#define Semaphore_Construct(p) (p)->_created = 0
#define Semaphore_IsCreated(p) ((p)->_created)

WRes Semaphore_Create(CSemaphore *p, UInt32 initCount, UInt32 maxCount);
WRes Semaphore_OptCreateInit(CSemaphore *p, UInt32 initCount, UInt32 maxCount);
WRes Semaphore_ReleaseN(CSemaphore *p, UInt32 num);
#define Semaphore_Release1(p) Semaphore_ReleaseN(p, 1)
WRes Semaphore_Wait(CSemaphore *p);
WRes Semaphore_Close(CSemaphore *p);


typedef struct
{
  pthread_mutex_t _mutex;
} CCriticalSection;

WRes CriticalSection_Init(CCriticalSection *p);
void CriticalSection_Delete(CCriticalSection *cs);
void CriticalSection_Enter(CCriticalSection *cs);
void CriticalSection_Leave(CCriticalSection *cs);

LONG InterlockedIncrement(LONG volatile *addend);
LONG InterlockedDecrement(LONG volatile *addend);

#endif  /* _WIN32 */

WRes AutoResetEvent_OptCreate_And_Reset(CAutoResetEvent *p);

EXTERN_C_END

#endif
#endif

EXTERN_C_BEGIN

#ifndef Z7_ST

#ifndef Z7_ST
  #define MTDEC_THREADS_MAX 32
#else
  #define MTDEC_THREADS_MAX 1
#endif


typedef struct
{
  ICompressProgressPtr progress;
  SRes res;
  UInt64 totalInSize;
  UInt64 totalOutSize;
  CCriticalSection cs;
} CMtProgress;

void MtProgress_Init(CMtProgress *p, ICompressProgressPtr progress);
SRes MtProgress_Progress_ST(CMtProgress *p);
SRes MtProgress_ProgressAdd(CMtProgress *p, UInt64 inSize, UInt64 outSize);
SRes MtProgress_GetError(CMtProgress *p);
void MtProgress_SetError(CMtProgress *p, SRes res);

struct CMtDec;

typedef struct
{
  struct CMtDec_ *mtDec;
  unsigned index;
  void *inBuf;

  size_t inDataSize_Start; /* size of input data in start block */
  UInt64 inDataSize;       /* total size of input data in all blocks */

  CThread thread;
  CAutoResetEvent canRead;
  CAutoResetEvent canWrite;
  void  *allocaPtr;
} CMtDecThread;

void MtDecThread_FreeInBufs(CMtDecThread *t);


typedef enum
{
  MTDEC_PARSE_CONTINUE, /* continue this block with more input data */
  MTDEC_PARSE_OVERFLOW, /* MT buffers overflow, need switch to single-thread */
  MTDEC_PARSE_NEW,      /* new block */
  MTDEC_PARSE_END       /* end of block threading. But we still can return to threading after Write(&needContinue) */
} EMtDecParseState;

typedef struct
{
  /* in */
  int startCall;
  const Byte *src;
  size_t srcSize;
      /* in  : (srcSize == 0) is allowed */
      /* out : it's allowed to return less that actually was used ? */
  int srcFinished;

  /* out */
  EMtDecParseState state;
  BoolInt canCreateNewThread;
  UInt64 outPos; /* check it (size_t) */
} CMtDecCallbackInfo;


typedef struct
{
  void (*Parse)(void *p, unsigned coderIndex, CMtDecCallbackInfo *ci);
  
  /* PreCode() and Code(): */
  /* (SRes_return_result != SZ_OK) means stop decoding, no need another blocks */
  SRes (*PreCode)(void *p, unsigned coderIndex);
  SRes (*Code)(void *p, unsigned coderIndex,
      const Byte *src, size_t srcSize, int srcFinished,
      UInt64 *inCodePos, UInt64 *outCodePos, int *stop);
  /* stop - means stop another Code calls */


  /* Write() must be called, if Parse() was called
      set (needWrite) if
      {
         && (was not interrupted by progress)
         && (was not interrupted in previous block)
      }

    out:
      if (*needContinue), decoder still need to continue decoding with new iteration,
         even after MTDEC_PARSE_END
      if (*canRecode), we didn't flush current block data, so we still can decode current block later.
  */
  SRes (*Write)(void *p, unsigned coderIndex,
      BoolInt needWriteToStream,
      const Byte *src, size_t srcSize, BoolInt isCross,
      /* int srcFinished, */
      BoolInt *needContinue,
      BoolInt *canRecode);

} IMtDecCallback2;



typedef struct CMtDec_
{
  /* input variables */
  
  size_t inBufSize;        /* size of input block */
  unsigned numThreadsMax;
  /* size_t inBlockMax; */
  unsigned numThreadsMax_2;

  ISeqInStreamPtr inStream;
  /* const Byte *inData; */
  /* size_t inDataSize; */

  ICompressProgressPtr progress;
  ISzAllocPtr alloc;

  IMtDecCallback2 *mtCallback;
  void *mtCallbackObject;

  
  /* internal variables */
  
  size_t allocatedBufsSize;

  BoolInt exitThread;
  WRes exitThreadWRes;

  UInt64 blockIndex;
  BoolInt isAllocError;
  BoolInt overflow;
  SRes threadingErrorSRes;

  BoolInt needContinue;

  /* CAutoResetEvent finishedEvent; */

  SRes readRes;
  SRes codeRes;

  BoolInt wasInterrupted;

  unsigned numStartedThreads_Limit;
  unsigned numStartedThreads;

  Byte *crossBlock;
  size_t crossStart;
  size_t crossEnd;
  UInt64 readProcessed;
  BoolInt readWasFinished;
  UInt64 inProcessed;

  unsigned filledThreadStart;
  unsigned numFilledThreads;

  #ifndef Z7_ST
  BoolInt needInterrupt;
  UInt64 interruptIndex;
  CMtProgress mtProgress;
  CMtDecThread threads[MTDEC_THREADS_MAX];
  #endif
} CMtDec;


void MtDec_Construct(CMtDec *p);
void MtDec_Destruct(CMtDec *p);

/*
MtDec_Code() returns:
  SZ_OK - in most cases
  MY_SRes_HRESULT_FROM_WRes(WRes_error) - in case of unexpected error in threading function
*/
  
SRes MtDec_Code(CMtDec *p);
Byte *MtDec_GetCrossBuff(CMtDec *p);

int MtDec_PrepareRead(CMtDec *p);
const Byte *MtDec_Read(CMtDec *p, size_t *inLim);

#endif

EXTERN_C_END

#endif
#endif


void XzDecMtProps_Init(CXzDecMtProps *p)
{
  p->inBufSize_ST = 1 << 18;
  p->outStep_ST = 1 << 20;
  p->ignoreErrors = False;

  #ifndef Z7_ST
  p->numThreads = 1;
  p->inBufSize_MT = 1 << 18;
  p->memUseMax = sizeof(size_t) << 28;
  #endif
}



#ifndef Z7_ST

/* ---------- CXzDecMtThread ---------- */

typedef struct
{
  Byte *outBuf;
  size_t outBufSize;
  size_t outPreSize;
  size_t inPreSize;
  size_t inPreHeaderSize;
  size_t blockPackSize_for_Index;  /* including block header and checksum. */
  size_t blockPackTotal;  /* including stream header, block header and checksum. */
  size_t inCodeSize;
  size_t outCodeSize;
  ECoderStatus status;
  SRes codeRes;
  BoolInt skipMode;
  /* BoolInt finishedWithMark; */
  EMtDecParseState parseState;
  BoolInt parsing_Truncated;
  BoolInt atBlockHeader;
  CXzStreamFlags streamFlags;
  /* UInt64 numFinishedStreams */
  UInt64 numStreams;
  UInt64 numTotalBlocks;
  UInt64 numBlocks;

  BoolInt dec_created;
  CXzUnpacker dec;

  Byte mtPad[1 << 7];
} CXzDecMtThread;

#endif


/* ---------- CXzDecMt ---------- */

struct CXzDecMt
{
  CAlignOffsetAlloc alignOffsetAlloc;
  ISzAllocPtr allocMid;

  CXzDecMtProps props;
  size_t unpackBlockMaxSize;
  
  ISeqInStreamPtr inStream;
  ISeqOutStreamPtr outStream;
  ICompressProgressPtr progress;

  BoolInt finishMode;
  BoolInt outSize_Defined;
  UInt64 outSize;

  UInt64 outProcessed;
  UInt64 inProcessed;
  UInt64 readProcessed;
  BoolInt readWasFinished;
  SRes readRes;
  SRes writeRes;

  Byte *outBuf;
  size_t outBufSize;
  Byte *inBuf;
  size_t inBufSize;

  CXzUnpacker dec;

  ECoderStatus status;
  SRes codeRes;

  #ifndef Z7_ST
  BoolInt mainDecoderWasCalled;
  /* int statErrorDefined; */
  int finishedDecoderIndex;

  /* global values that are used in Parse stage */
  CXzStreamFlags streamFlags;
  /* UInt64 numFinishedStreams */
  UInt64 numStreams;
  UInt64 numTotalBlocks;
  UInt64 numBlocks;

  /* UInt64 numBadBlocks; */
  SRes mainErrorCode;  /* it's set to error code, if the size Code() output doesn't patch the size from Parsing stage */
                       /* it can be = SZ_ERROR_INPUT_EOF */
                       /* it can be = SZ_ERROR_DATA, in some another cases */
  BoolInt isBlockHeaderState_Parse;
  BoolInt isBlockHeaderState_Write;
  UInt64 outProcessed_Parse;
  BoolInt parsing_Truncated;

  BoolInt mtc_WasConstructed;
  CMtDec mtc;
  CXzDecMtThread coders[MTDEC_THREADS_MAX];
  #endif
};



CXzDecMtHandle XzDecMt_Create(ISzAllocPtr alloc, ISzAllocPtr allocMid)
{
  CXzDecMt *p = (CXzDecMt *)ISzAlloc_Alloc(alloc, sizeof(CXzDecMt));
  if (!p)
    return NULL;
  
  AlignOffsetAlloc_CreateVTable(&p->alignOffsetAlloc);
  p->alignOffsetAlloc.baseAlloc = alloc;
  p->alignOffsetAlloc.numAlignBits = 7;
  p->alignOffsetAlloc.offset = 0;

  p->allocMid = allocMid;

  p->outBuf = NULL;
  p->outBufSize = 0;
  p->inBuf = NULL;
  p->inBufSize = 0;

  XzUnpacker_Construct(&p->dec, &p->alignOffsetAlloc.vt);

  p->unpackBlockMaxSize = 0;

  XzDecMtProps_Init(&p->props);

  #ifndef Z7_ST
  p->mtc_WasConstructed = False;
  {
    unsigned i;
    for (i = 0; i < MTDEC_THREADS_MAX; i++)
    {
      CXzDecMtThread *coder = &p->coders[i];
      coder->dec_created = False;
      coder->outBuf = NULL;
      coder->outBufSize = 0;
    }
  }
  #endif

  return (CXzDecMtHandle)p;
}


#ifndef Z7_ST

static void XzDecMt_FreeOutBufs(CXzDecMt *p)
{
  unsigned i;
  for (i = 0; i < MTDEC_THREADS_MAX; i++)
  {
    CXzDecMtThread *coder = &p->coders[i];
    if (coder->outBuf)
    {
      ISzAlloc_Free(p->allocMid, coder->outBuf);
      coder->outBuf = NULL;
      coder->outBufSize = 0;
    }
  }
  p->unpackBlockMaxSize = 0;
}

#endif



static void XzDecMt_FreeSt(CXzDecMt *p)
{
  XzUnpacker_Free(&p->dec);
  
  if (p->outBuf)
  {
    ISzAlloc_Free(p->allocMid, p->outBuf);
    p->outBuf = NULL;
  }
  p->outBufSize = 0;
  
  if (p->inBuf)
  {
    ISzAlloc_Free(p->allocMid, p->inBuf);
    p->inBuf = NULL;
  }
  p->inBufSize = 0;
}


/* #define GET_CXzDecMt_p  CXzDecMt *p = pp; */

void XzDecMt_Destroy(CXzDecMtHandle p)
{
  /* GET_CXzDecMt_p */

  XzDecMt_FreeSt(p);

  #ifndef Z7_ST

  if (p->mtc_WasConstructed)
  {
    MtDec_Destruct(&p->mtc);
    p->mtc_WasConstructed = False;
  }
  {
    unsigned i;
    for (i = 0; i < MTDEC_THREADS_MAX; i++)
    {
      CXzDecMtThread *t = &p->coders[i];
      if (t->dec_created)
      {
        /* we don't need to free dict here */
        XzUnpacker_Free(&t->dec);
        t->dec_created = False;
      }
    }
  }
  XzDecMt_FreeOutBufs(p);

  #endif

  ISzAlloc_Free(p->alignOffsetAlloc.baseAlloc, p);
}



#ifndef Z7_ST

static void XzDecMt_Callback_Parse(void *obj, unsigned coderIndex, CMtDecCallbackInfo *cc)
{
  CXzDecMt *me = (CXzDecMt *)obj;
  CXzDecMtThread *coder = &me->coders[coderIndex];
  size_t srcSize = cc->srcSize;

  cc->srcSize = 0;
  cc->outPos = 0;
  cc->state = MTDEC_PARSE_CONTINUE;

  cc->canCreateNewThread = True;

  if (cc->startCall)
  {
    coder->outPreSize = 0;
    coder->inPreSize = 0;
    coder->inPreHeaderSize = 0;
    coder->parseState = MTDEC_PARSE_CONTINUE;
    coder->parsing_Truncated = False;
    coder->skipMode = False;
    coder->codeRes = SZ_OK;
    coder->status = CODER_STATUS_NOT_SPECIFIED;
    coder->inCodeSize = 0;
    coder->outCodeSize = 0;

    coder->numStreams = me->numStreams;
    coder->numTotalBlocks = me->numTotalBlocks;
    coder->numBlocks = me->numBlocks;

    if (!coder->dec_created)
    {
      XzUnpacker_Construct(&coder->dec, &me->alignOffsetAlloc.vt);
      coder->dec_created = True;
    }
    
    XzUnpacker_Init(&coder->dec);

    if (me->isBlockHeaderState_Parse)
    {
      coder->dec.streamFlags = me->streamFlags;
      coder->atBlockHeader = True;
      XzUnpacker_PrepareToRandomBlockDecoding(&coder->dec);
    }
    else
    {
      coder->atBlockHeader = False;
      me->isBlockHeaderState_Parse = True;
    }

    coder->dec.numStartedStreams = me->numStreams;
    coder->dec.numTotalBlocks = me->numTotalBlocks;
    coder->dec.numBlocks = me->numBlocks;
  }

  while (!coder->skipMode)
  {
    ECoderStatus status;
    SRes res;
    size_t srcSize2 = srcSize;
    size_t destSize = (size_t)0 - 1;

    coder->dec.parseMode = True;
    coder->dec.headerParsedOk = False;
    
    PRF_STR_INT("Parse", srcSize2)
    
    res = XzUnpacker_Code(&coder->dec,
        NULL, &destSize,
        cc->src, &srcSize2, cc->srcFinished,
        CODER_FINISH_END, &status);
    
    /* PRF(printf(" res = %d, srcSize2 = %d", res, (unsigned)srcSize2)); */
    
    coder->codeRes = res;
    coder->status = status;
    cc->srcSize += srcSize2;
    srcSize -= srcSize2;
    coder->inPreHeaderSize += srcSize2;
    coder->inPreSize = coder->inPreHeaderSize;
    
    if (res != SZ_OK)
    {
      cc->state =
      coder->parseState = MTDEC_PARSE_END;
      /*
      if (res == SZ_ERROR_MEM)
        return res;
      return SZ_OK;
      */
      return; /* res; */
    }
    
    if (coder->dec.headerParsedOk)
    {
      const CXzBlock *block = &coder->dec.block;
      if (XzBlock_HasUnpackSize(block)
          /* && block->unpackSize <= me->props.outBlockMax */
          && XzBlock_HasPackSize(block))
      {
        {
          if (block->unpackSize * 2 * me->mtc.numStartedThreads > me->props.memUseMax)
          {
            cc->state = MTDEC_PARSE_OVERFLOW;
            return; /* SZ_OK; */
          }
        }
        {
        const UInt64 packSize = block->packSize;
        const UInt64 packSizeAligned = packSize + ((0 - (unsigned)packSize) & 3);
        const unsigned checkSize = XzFlags_GetCheckSize(coder->dec.streamFlags);
        const UInt64 blockPackSum = coder->inPreSize + packSizeAligned + checkSize;
        /* if (blockPackSum <= me->props.inBlockMax) */
        /* unpackBlockMaxSize */
        {
          coder->blockPackSize_for_Index = (size_t)(coder->dec.blockHeaderSize + packSize + checkSize);
          coder->blockPackTotal = (size_t)blockPackSum;
          coder->outPreSize = (size_t)block->unpackSize;
          coder->streamFlags = coder->dec.streamFlags;
          me->streamFlags = coder->dec.streamFlags;
          coder->skipMode = True;
          break;
        }
        }
      }
    }
    else
    /* if (coder->inPreSize <= me->props.inBlockMax) */
    {
      if (!cc->srcFinished)
        return; /* SZ_OK; */
      cc->state =
      coder->parseState = MTDEC_PARSE_END;
      return; /* SZ_OK; */
    }
    cc->state = MTDEC_PARSE_OVERFLOW;
    return; /* SZ_OK; */
  }

  /* ---------- skipMode ---------- */
  {
    UInt64 rem = coder->blockPackTotal - coder->inPreSize;
    size_t cur = srcSize;
    if (cur > rem)
      cur = (size_t)rem;
    cc->srcSize += cur;
    coder->inPreSize += cur;
    srcSize -= cur;

    if (coder->inPreSize == coder->blockPackTotal)
    {
      if (srcSize == 0)
      {
        if (!cc->srcFinished)
          return; /* SZ_OK; */
        cc->state = MTDEC_PARSE_END;
      }
      else if ((cc->src)[cc->srcSize] == 0) /* we check control byte of next block */
        cc->state = MTDEC_PARSE_END;
      else
      {
        cc->state = MTDEC_PARSE_NEW;

        {
          size_t blockMax = me->unpackBlockMaxSize;
          if (blockMax < coder->outPreSize)
            blockMax = coder->outPreSize;
          {
            UInt64 required = (UInt64)blockMax * (me->mtc.numStartedThreads + 1) * 2;
            if (me->props.memUseMax < required)
              cc->canCreateNewThread = False;
          }
        }

        if (me->outSize_Defined)
        {
          /* next block can be zero size */
          const UInt64 rem2 = me->outSize - me->outProcessed_Parse;
          if (rem2 < coder->outPreSize)
          {
            coder->parsing_Truncated = True;
            cc->state = MTDEC_PARSE_END;
          }
          me->outProcessed_Parse += coder->outPreSize;
        }
      }
    }
    else if (cc->srcFinished)
      cc->state = MTDEC_PARSE_END;
    else
      return; /* SZ_OK; */

    coder->parseState = cc->state;
    cc->outPos = coder->outPreSize;
    
    me->numStreams = coder->dec.numStartedStreams;
    me->numTotalBlocks = coder->dec.numTotalBlocks;
    me->numBlocks = coder->dec.numBlocks + 1;
    return; /* SZ_OK; */
  }
}


static SRes XzDecMt_Callback_PreCode(void *pp, unsigned coderIndex)
{
  CXzDecMt *me = (CXzDecMt *)pp;
  CXzDecMtThread *coder = &me->coders[coderIndex];
  Byte *dest;

  if (!coder->dec.headerParsedOk)
    return SZ_OK;

  dest = coder->outBuf;

  if (!dest || coder->outBufSize < coder->outPreSize)
  {
    if (dest)
    {
      ISzAlloc_Free(me->allocMid, dest);
      coder->outBuf = NULL;
      coder->outBufSize = 0;
    }
    {
      size_t outPreSize = coder->outPreSize;
      if (outPreSize == 0)
        outPreSize = 1;
      dest = (Byte *)ISzAlloc_Alloc(me->allocMid, outPreSize);
    }
    if (!dest)
      return SZ_ERROR_MEM;
    coder->outBuf = dest;
    coder->outBufSize = coder->outPreSize;

    if (coder->outBufSize > me->unpackBlockMaxSize)
      me->unpackBlockMaxSize = coder->outBufSize;
  }

  /* return SZ_ERROR_MEM; */

  XzUnpacker_SetOutBuf(&coder->dec, coder->outBuf, coder->outBufSize);

  {
    SRes res = XzDecMix_Init(&coder->dec.decoder, &coder->dec.block, coder->outBuf, coder->outBufSize);
    /* res = SZ_ERROR_UNSUPPORTED; // to test */
    coder->codeRes = res;
    if (res != SZ_OK)
    {
      /* if (res == SZ_ERROR_MEM) return res; */
      if (me->props.ignoreErrors && res != SZ_ERROR_MEM)
        return SZ_OK;
      return res;
    }
  }

  return SZ_OK;
}


static SRes XzDecMt_Callback_Code(void *pp, unsigned coderIndex,
    const Byte *src, size_t srcSize, int srcFinished,
    /* int finished, int blockFinished, */
    UInt64 *inCodePos, UInt64 *outCodePos, int *stop)
{
  CXzDecMt *me = (CXzDecMt *)pp;
  CXzDecMtThread *coder = &me->coders[coderIndex];

  *inCodePos = coder->inCodeSize;
  *outCodePos = coder->outCodeSize;
  *stop = True;

  if (srcSize > coder->inPreSize - coder->inCodeSize)
    return SZ_ERROR_FAIL;
  
  if (coder->inCodeSize < coder->inPreHeaderSize)
  {
    size_t step = coder->inPreHeaderSize - coder->inCodeSize;
    if (step > srcSize)
      step = srcSize;
    src += step;
    srcSize -= step;
    coder->inCodeSize += step;
    *inCodePos = coder->inCodeSize;
    if (coder->inCodeSize < coder->inPreHeaderSize)
    {
      *stop = False;
      return SZ_OK;
    }
  }

  if (!coder->dec.headerParsedOk)
    return SZ_OK;
  if (!coder->outBuf)
    return SZ_OK;

  if (coder->codeRes == SZ_OK)
  {
    ECoderStatus status;
    SRes res;
    size_t srcProcessed = srcSize;
    size_t outSizeCur = coder->outPreSize - coder->dec.outDataWritten;

    /* PRF(printf("\nCallback_Code: Code %d %d\n", (unsigned)srcSize, (unsigned)outSizeCur)); */

    res = XzUnpacker_Code(&coder->dec,
        NULL, &outSizeCur,
        src, &srcProcessed, srcFinished,
        /* coder->finishedWithMark ? CODER_FINISH_END : CODER_FINISH_ANY, */
        CODER_FINISH_END,
        &status);

    /* PRF(printf(" res = %d, srcSize2 = %d, outSizeCur = %d", res, (unsigned)srcProcessed, (unsigned)outSizeCur)); */

    coder->codeRes = res;
    coder->status = status;
    coder->inCodeSize += srcProcessed;
    coder->outCodeSize = coder->dec.outDataWritten;
    *inCodePos = coder->inCodeSize;
    *outCodePos = coder->outCodeSize;

    if (res == SZ_OK)
    {
      if (srcProcessed == srcSize)
        *stop = False;
      return SZ_OK;
    }
  }

  if (me->props.ignoreErrors && coder->codeRes != SZ_ERROR_MEM)
  {
    *inCodePos = coder->inPreSize;
    *outCodePos = coder->outPreSize;
    return SZ_OK;
  }
  return coder->codeRes;
}


#define XZDECMT_STREAM_WRITE_STEP (1 << 24)

static SRes XzDecMt_Callback_Write(void *pp, unsigned coderIndex,
    BoolInt needWriteToStream,
    const Byte *src, size_t srcSize, BoolInt isCross,
    /* int srcFinished, */
    BoolInt *needContinue,
    BoolInt *canRecode)
{
  CXzDecMt *me = (CXzDecMt *)pp;
  const CXzDecMtThread *coder = &me->coders[coderIndex];

  /* PRF(printf("\nWrite processed = %d srcSize = %d\n", (unsigned)me->mtc.inProcessed, (unsigned)srcSize)); */
  
  *needContinue = False;
  *canRecode = True;
  
  if (!needWriteToStream)
    return SZ_OK;

  if (!coder->dec.headerParsedOk || !coder->outBuf)
  {
    if (me->finishedDecoderIndex < 0)
      me->finishedDecoderIndex = (int)coderIndex;
    return SZ_OK;
  }

  if (me->finishedDecoderIndex >= 0)
    return SZ_OK;

  me->mtc.inProcessed += coder->inCodeSize;

  *canRecode = False;

  {
    SRes res;
    size_t size = coder->outCodeSize;
    Byte *data = coder->outBuf;
    
    /* we use in me->dec: sha, numBlocks, indexSize */

    if (!me->isBlockHeaderState_Write)
    {
      XzUnpacker_PrepareToRandomBlockDecoding(&me->dec);
      me->dec.decodeOnlyOneBlock = False;
      me->dec.numStartedStreams = coder->dec.numStartedStreams;
      me->dec.streamFlags = coder->streamFlags;

      me->isBlockHeaderState_Write = True;
    }
    
    me->dec.numTotalBlocks = coder->dec.numTotalBlocks;
    XzUnpacker_UpdateIndex(&me->dec, coder->blockPackSize_for_Index, coder->outPreSize);
    
    if (coder->outPreSize != size)
    {
      if (me->props.ignoreErrors)
      {
        memset(data + size, 0, coder->outPreSize - size);
        size = coder->outPreSize;
      }
      /* me->numBadBlocks++; */
      if (me->mainErrorCode == SZ_OK)
      {
        if ((int)coder->status == LZMA_STATUS_NEEDS_MORE_INPUT)
          me->mainErrorCode = SZ_ERROR_INPUT_EOF;
        else
          me->mainErrorCode = SZ_ERROR_DATA;
      }
    }
    
    if (me->writeRes != SZ_OK)
      return me->writeRes;

    res = SZ_OK;
    {
      if (me->outSize_Defined)
      {
        const UInt64 rem = me->outSize - me->outProcessed;
        if (size > rem)
          size = (SizeT)rem;
      }

      for (;;)
      {
        size_t cur = size;
        size_t written;
        if (cur > XZDECMT_STREAM_WRITE_STEP)
          cur = XZDECMT_STREAM_WRITE_STEP;

        written = ISeqOutStream_Write(me->outStream, data, cur);

        /* PRF(printf("\nWritten ask = %d written = %d\n", (unsigned)cur, (unsigned)written)); */
        
        me->outProcessed += written;
        if (written != cur)
        {
          me->writeRes = SZ_ERROR_WRITE;
          res = me->writeRes;
          break;
        }
        data += cur;
        size -= cur;
        /* PRF_STR_INT("Written size =", size) */
        if (size == 0)
          break;
        res = MtProgress_ProgressAdd(&me->mtc.mtProgress, 0, 0);
        if (res != SZ_OK)
          break;
      }
    }

    if (coder->codeRes != SZ_OK)
      if (!me->props.ignoreErrors)
      {
        me->finishedDecoderIndex = (int)coderIndex;
        return res;
      }

    RINOK(res)

    if (coder->inPreSize != coder->inCodeSize
        || coder->blockPackTotal != coder->inCodeSize)
    {
      me->finishedDecoderIndex = (int)coderIndex;
      return SZ_OK;
    }

    if (coder->parseState != MTDEC_PARSE_END)
    {
      *needContinue = True;
      return SZ_OK;
    }
  }

  /* (coder->state == MTDEC_PARSE_END) means that there are no other working threads */
  /* so we can use mtc variables without lock */

  PRF_STR_INT("Write MTDEC_PARSE_END", me->mtc.inProcessed)

  me->mtc.mtProgress.totalInSize = me->mtc.inProcessed;
  {
    CXzUnpacker *dec = &me->dec;
    
    PRF_STR_INT("PostSingle", srcSize)
    
    {
      size_t srcProcessed = srcSize;
      ECoderStatus status;
      size_t outSizeCur = 0;
      SRes res;
      
      /* dec->decodeOnlyOneBlock = False; */
      dec->decodeToStreamSignature = True;

      me->mainDecoderWasCalled = True;

      if (coder->parsing_Truncated)
      {
        me->parsing_Truncated = True;
        return SZ_OK;
      }
      
      /*
      We have processed all xz-blocks of stream,
      And xz unpacker is at XZ_STATE_BLOCK_HEADER state, where
      (src) is a pointer to xz-Index structure.
      We finish reading of current xz-Stream, including Zero padding after xz-Stream.
      We exit, if we reach extra byte (first byte of new-Stream or another data).
      But we don't update input stream pointer for that new extra byte.
      If extra byte is not correct first byte of xz-signature,
      we have SZ_ERROR_NO_ARCHIVE error here.
      */

      res = XzUnpacker_Code(dec,
          NULL, &outSizeCur,
          src, &srcProcessed,
          me->mtc.readWasFinished, /* srcFinished */
          CODER_FINISH_END, /* CODER_FINISH_ANY, */
          &status);

      /* res = SZ_ERROR_ARCHIVE; // for failure test */
      
      me->status = status;
      me->codeRes = res;

      if (isCross)
        me->mtc.crossStart += srcProcessed;

      me->mtc.inProcessed += srcProcessed;
      me->mtc.mtProgress.totalInSize = me->mtc.inProcessed;

      srcSize -= srcProcessed;
      src += srcProcessed;

      if (res != SZ_OK)
      {
        return SZ_OK;
        /* return res; */
      }
      
      if (dec->state == XZ_STATE_STREAM_HEADER)
      {
        *needContinue = True;
        me->isBlockHeaderState_Parse = False;
        me->isBlockHeaderState_Write = False;

        if (!isCross)
        {
          Byte *crossBuf = MtDec_GetCrossBuff(&me->mtc);
          if (!crossBuf)
            return SZ_ERROR_MEM;
          if (srcSize != 0)
            memcpy(crossBuf, src, srcSize);
          me->mtc.crossStart = 0;
          me->mtc.crossEnd = srcSize;
        }

        PRF_STR_INT("XZ_STATE_STREAM_HEADER crossEnd = ", (unsigned)me->mtc.crossEnd)

        return SZ_OK;
      }
      
      if (status != CODER_STATUS_NEEDS_MORE_INPUT || srcSize != 0)
      {
        return SZ_ERROR_FAIL;
      }
      
      if (me->mtc.readWasFinished)
      {
        return SZ_OK;
      }
    }
    
    {
      size_t inPos;
      size_t inLim;
      /* const Byte *inData; */
      UInt64 inProgressPrev = me->mtc.inProcessed;
      
      /* XzDecMt_Prepare_InBuf_ST(p); */
      Byte *crossBuf = MtDec_GetCrossBuff(&me->mtc);
      if (!crossBuf)
        return SZ_ERROR_MEM;
      
      inPos = 0;
      inLim = 0;
      
      /* inData = crossBuf; */
      
      for (;;)
      {
        SizeT inProcessed;
        SizeT outProcessed;
        ECoderStatus status;
        SRes res;
        
        if (inPos == inLim)
        {
          if (!me->mtc.readWasFinished)
          {
            inPos = 0;
            inLim = me->mtc.inBufSize;
            me->mtc.readRes = ISeqInStream_Read(me->inStream, (void *)crossBuf, &inLim);
            me->mtc.readProcessed += inLim;
            if (inLim == 0 || me->mtc.readRes != SZ_OK)
              me->mtc.readWasFinished = True;
          }
        }
        
        inProcessed = inLim - inPos;
        outProcessed = 0;

        res = XzUnpacker_Code(dec,
            NULL, &outProcessed,
            crossBuf + inPos, &inProcessed,
            (inProcessed == 0), /* srcFinished */
            CODER_FINISH_END, &status);
        
        me->codeRes = res;
        me->status = status;
        inPos += inProcessed;
        me->mtc.inProcessed += inProcessed;
        me->mtc.mtProgress.totalInSize = me->mtc.inProcessed;

        if (res != SZ_OK)
        {
          return SZ_OK;
          /* return res; */
        }

        if (dec->state == XZ_STATE_STREAM_HEADER)
        {
          *needContinue = True;
          me->mtc.crossStart = inPos;
          me->mtc.crossEnd = inLim;
          me->isBlockHeaderState_Parse = False;
          me->isBlockHeaderState_Write = False;
          return SZ_OK;
        }
        
        if (status != CODER_STATUS_NEEDS_MORE_INPUT)
          return SZ_ERROR_FAIL;
        
        if (me->mtc.progress)
        {
          UInt64 inDelta = me->mtc.inProcessed - inProgressPrev;
          if (inDelta >= (1 << 22))
          {
            RINOK(MtProgress_Progress_ST(&me->mtc.mtProgress))
            inProgressPrev = me->mtc.inProcessed;
          }
        }
        if (me->mtc.readWasFinished)
          return SZ_OK;
      }
    }
  }
}


#endif



void XzStatInfo_Clear(CXzStatInfo *p)
{
  p->InSize = 0;
  p->OutSize = 0;
  
  p->NumStreams = 0;
  p->NumBlocks = 0;
  
  p->UnpackSize_Defined = False;
  
  p->NumStreams_Defined = False;
  p->NumBlocks_Defined = False;
  
  p->DataAfterEnd = False;
  p->DecodingTruncated = False;
  
  p->DecodeRes = SZ_OK;
  p->ReadRes = SZ_OK;
  p->ProgressRes = SZ_OK;

  p->CombinedRes = SZ_OK;
  p->CombinedRes_Type = SZ_OK;
}



/*
  XzDecMt_Decode_ST() can return SZ_OK or the following errors
     - SZ_ERROR_MEM for memory allocation error
     - error from XzUnpacker_Code() function
     - SZ_ERROR_WRITE for ISeqOutStream::Write(). stat->CombinedRes_Type = SZ_ERROR_WRITE in that case
     - ICompressProgress::Progress() error,  stat->CombinedRes_Type = SZ_ERROR_PROGRESS.
  But XzDecMt_Decode_ST() doesn't return ISeqInStream::Read() errors.
  ISeqInStream::Read() result is set to p->readRes.
  also it can set stat->CombinedRes_Type to SZ_ERROR_WRITE or SZ_ERROR_PROGRESS.
*/

static SRes XzDecMt_Decode_ST(CXzDecMt *p
    #ifndef Z7_ST
    , BoolInt tMode
    #endif
    , CXzStatInfo *stat)
{
  size_t outPos;
  size_t inPos, inLim;
  const Byte *inData;
  UInt64 inPrev, outPrev;

  CXzUnpacker *dec;

  #ifndef Z7_ST
  if (tMode)
  {
    XzDecMt_FreeOutBufs(p);
    tMode = (BoolInt)MtDec_PrepareRead(&p->mtc);
  }
  #endif

  if (!p->outBuf || p->outBufSize != p->props.outStep_ST)
  {
    ISzAlloc_Free(p->allocMid, p->outBuf);
    p->outBufSize = 0;
    p->outBuf = (Byte *)ISzAlloc_Alloc(p->allocMid, p->props.outStep_ST);
    if (!p->outBuf)
      return SZ_ERROR_MEM;
    p->outBufSize = p->props.outStep_ST;
  }

  if (!p->inBuf || p->inBufSize != p->props.inBufSize_ST)
  {
    ISzAlloc_Free(p->allocMid, p->inBuf);
    p->inBufSize = 0;
    p->inBuf = (Byte *)ISzAlloc_Alloc(p->allocMid, p->props.inBufSize_ST);
    if (!p->inBuf)
      return SZ_ERROR_MEM;
    p->inBufSize = p->props.inBufSize_ST;
  }

  dec = &p->dec;
  dec->decodeToStreamSignature = False;
  /* dec->decodeOnlyOneBlock = False; */

  XzUnpacker_SetOutBuf(dec, NULL, 0);

  inPrev = p->inProcessed;
  outPrev = p->outProcessed;

  inPos = 0;
  inLim = 0;
  inData = NULL;
  outPos = 0;

  for (;;)
  {
    SizeT outSize;
    BoolInt finished;
    ECoderFinishMode finishMode;
    SizeT inProcessed;
    ECoderStatus status;
    SRes res;

    SizeT outProcessed;



    if (inPos == inLim)
    {
      #ifndef Z7_ST
      if (tMode)
      {
        inData = MtDec_Read(&p->mtc, &inLim);
        inPos = 0;
        if (inData)
          continue;
        tMode = False;
        inLim = 0;
      }
      #endif
      
      if (!p->readWasFinished)
      {
        inPos = 0;
        inLim = p->inBufSize;
        inData = p->inBuf;
        p->readRes = ISeqInStream_Read(p->inStream, (void *)p->inBuf, &inLim);
        p->readProcessed += inLim;
        if (inLim == 0 || p->readRes != SZ_OK)
          p->readWasFinished = True;
      }
    }

    outSize = p->props.outStep_ST - outPos;

    finishMode = CODER_FINISH_ANY;
    if (p->outSize_Defined)
    {
      const UInt64 rem = p->outSize - p->outProcessed;
      if (outSize >= rem)
      {
        outSize = (SizeT)rem;
        if (p->finishMode)
          finishMode = CODER_FINISH_END;
      }
    }

    inProcessed = inLim - inPos;
    outProcessed = outSize;

    res = XzUnpacker_Code(dec, p->outBuf + outPos, &outProcessed,
        inData + inPos, &inProcessed,
        (inPos == inLim), /* srcFinished */
        finishMode, &status);

    p->codeRes = res;
    p->status = status;

    inPos += inProcessed;
    outPos += outProcessed;
    p->inProcessed += inProcessed;
    p->outProcessed += outProcessed;

    finished = ((inProcessed == 0 && outProcessed == 0) || res != SZ_OK);

    if (finished || outProcessed >= outSize)
      if (outPos != 0)
      {
        const size_t written = ISeqOutStream_Write(p->outStream, p->outBuf, outPos);
        /* p->outProcessed += written; // 21.01: BUG fixed */
        if (written != outPos)
        {
          stat->CombinedRes_Type = SZ_ERROR_WRITE;
          return SZ_ERROR_WRITE;
        }
        outPos = 0;
      }

    if (p->progress && res == SZ_OK)
    {
      if (p->inProcessed - inPrev >= (1 << 22) ||
          p->outProcessed - outPrev >= (1 << 22))
      {
        res = ICompressProgress_Progress(p->progress, p->inProcessed, p->outProcessed);
        if (res != SZ_OK)
        {
          stat->CombinedRes_Type = SZ_ERROR_PROGRESS;
          stat->ProgressRes = res;
          return res;
        }
        inPrev = p->inProcessed;
        outPrev = p->outProcessed;
      }
    }

    if (finished)
    {
      /* p->codeRes is preliminary error from XzUnpacker_Code. */
      /* and it can be corrected later as final result */
      /* so we return SZ_OK here instead of (res); */
      return SZ_OK;
      /* return res; */
    }
  }
}



/*
XzStatInfo_SetStat() transforms
    CXzUnpacker return code and status to combined CXzStatInfo results.
    it can convert SZ_OK to SZ_ERROR_INPUT_EOF
    it can convert SZ_ERROR_NO_ARCHIVE to SZ_OK and (DataAfterEnd = 1)
*/

static void XzStatInfo_SetStat(const CXzUnpacker *dec,
    int finishMode,
    /* UInt64 readProcessed, */
    UInt64 inProcessed,
    SRes res,                     /* it's result from CXzUnpacker unpacker */
    ECoderStatus status,
    BoolInt decodingTruncated,
    CXzStatInfo *stat)
{
  UInt64 extraSize;
  
  stat->DecodingTruncated = (Byte)(decodingTruncated ? 1 : 0);
  stat->InSize = inProcessed;
  stat->NumStreams = dec->numStartedStreams;
  stat->NumBlocks = dec->numTotalBlocks;
  
  stat->UnpackSize_Defined = True;
  stat->NumStreams_Defined = True;
  stat->NumBlocks_Defined = True;
  
  extraSize = XzUnpacker_GetExtraSize(dec);
  
  if (res == SZ_OK)
  {
    if (status == CODER_STATUS_NEEDS_MORE_INPUT)
    {
      /* CODER_STATUS_NEEDS_MORE_INPUT is expected status for correct xz streams */
      /* any extra data is part of correct data */
      extraSize = 0;
      /* if xz stream was not finished, then we need more data */
      if (!XzUnpacker_IsStreamWasFinished(dec))
        res = SZ_ERROR_INPUT_EOF;
    }
    else
    {
      /* CODER_STATUS_FINISHED_WITH_MARK is not possible for multi stream xz decoding */
      /* so he we have (status == CODER_STATUS_NOT_FINISHED) */
      /* if (status != CODER_STATUS_FINISHED_WITH_MARK) */
      if (!decodingTruncated || finishMode)
        res = SZ_ERROR_DATA;
    }
  }
  else if (res == SZ_ERROR_NO_ARCHIVE)
  {
    /*
    SZ_ERROR_NO_ARCHIVE is possible for 2 states:
      XZ_STATE_STREAM_HEADER  - if bad signature or bad CRC
      XZ_STATE_STREAM_PADDING - if non-zero padding data
    extraSize and inProcessed don't include "bad" byte
    */
    /* if (inProcessed == extraSize), there was no any good xz stream header, and we keep error */
    if (inProcessed != extraSize) /* if there were good xz streams before error */
    {
      /* if (extraSize != 0 || readProcessed != inProcessed) */
      {
        /* he we suppose that all xz streams were finsihed OK, and we have */
        /* some extra data after all streams */
        stat->DataAfterEnd = True;
        res = SZ_OK;
      }
    }
  }
  
  if (stat->DecodeRes == SZ_OK)
    stat->DecodeRes = res;

  stat->InSize -= extraSize;
}



SRes XzDecMt_Decode(CXzDecMtHandle p,
    const CXzDecMtProps *props,
    const UInt64 *outDataSize, int finishMode,
    ISeqOutStreamPtr outStream,
    /* Byte *outBuf, size_t *outBufSize, */
    ISeqInStreamPtr inStream,
    /* const Byte *inData, size_t inDataSize, */
    CXzStatInfo *stat,
    int *isMT,
    ICompressProgressPtr progress)
{
  /* GET_CXzDecMt_p */
  #ifndef Z7_ST
  BoolInt tMode;
  #endif

  XzStatInfo_Clear(stat);

  p->props = *props;

  p->inStream = inStream;
  p->outStream = outStream;
  p->progress = progress;
  /* p->stat = stat; */

  p->outSize = 0;
  p->outSize_Defined = False;
  if (outDataSize)
  {
    p->outSize_Defined = True;
    p->outSize = *outDataSize;
  }

  p->finishMode = (BoolInt)finishMode;

  /* p->outSize = 457; p->outSize_Defined = True; p->finishMode = False; // for test */

  p->writeRes = SZ_OK;
  p->outProcessed = 0;
  p->inProcessed = 0;
  p->readProcessed = 0;
  p->readWasFinished = False;
  p->readRes = SZ_OK;

  p->codeRes = SZ_OK;
  p->status = CODER_STATUS_NOT_SPECIFIED;

  XzUnpacker_Init(&p->dec);

  *isMT = False;

    /*
    p->outBuf = NULL;
    p->outBufSize = 0;
    if (!outStream)
    {
      p->outBuf = outBuf;
      p->outBufSize = *outBufSize;
      *outBufSize = 0;
    }
    */

  
  #ifndef Z7_ST

  p->isBlockHeaderState_Parse = False;
  p->isBlockHeaderState_Write = False;
  /* p->numBadBlocks = 0; */
  p->mainErrorCode = SZ_OK;
  p->mainDecoderWasCalled = False;

  tMode = False;

  if (p->props.numThreads > 1)
  {
    IMtDecCallback2 vt;
    BoolInt needContinue;
    SRes res;
    /* we just free ST buffers here */
    /* but we still keep state variables, that was set in XzUnpacker_Init() */
    XzDecMt_FreeSt(p);

    p->outProcessed_Parse = 0;
    p->parsing_Truncated = False;

    p->numStreams = 0;
    p->numTotalBlocks = 0;
    p->numBlocks = 0;
    p->finishedDecoderIndex = -1;

    if (!p->mtc_WasConstructed)
    {
      p->mtc_WasConstructed = True;
      MtDec_Construct(&p->mtc);
    }
    
    p->mtc.mtCallback = &vt;
    p->mtc.mtCallbackObject = p;

    p->mtc.progress = progress;
    p->mtc.inStream = inStream;
    p->mtc.alloc = &p->alignOffsetAlloc.vt;
    /* p->mtc.inData = inData; */
    /* p->mtc.inDataSize = inDataSize; */
    p->mtc.inBufSize = p->props.inBufSize_MT;
    /* p->mtc.inBlockMax = p->props.inBlockMax; */
    p->mtc.numThreadsMax = p->props.numThreads;

    *isMT = True;

    vt.Parse = XzDecMt_Callback_Parse;
    vt.PreCode = XzDecMt_Callback_PreCode;
    vt.Code = XzDecMt_Callback_Code;
    vt.Write = XzDecMt_Callback_Write;


    res = MtDec_Code(&p->mtc);


    stat->InSize = p->mtc.inProcessed;
    
    p->inProcessed = p->mtc.inProcessed;
    p->readRes = p->mtc.readRes;
    p->readWasFinished = p->mtc.readWasFinished;
    p->readProcessed = p->mtc.readProcessed;
    
    tMode = True;
    needContinue = False;
    
    if (res == SZ_OK)
    {
      if (p->mtc.mtProgress.res != SZ_OK)
      {
        res = p->mtc.mtProgress.res;
        stat->ProgressRes = res;
        stat->CombinedRes_Type = SZ_ERROR_PROGRESS;
      }
      else
        needContinue = p->mtc.needContinue;
    }
    
    if (!needContinue)
    {
      {
        SRes codeRes;
        BoolInt truncated = False;
        ECoderStatus status;
        const CXzUnpacker *dec;

        stat->OutSize = p->outProcessed;
       
        if (p->finishedDecoderIndex >= 0)
        {
          const CXzDecMtThread *coder = &p->coders[(unsigned)p->finishedDecoderIndex];
          codeRes = coder->codeRes;
          dec = &coder->dec;
          status = coder->status;
        }
        else if (p->mainDecoderWasCalled)
        {
          codeRes = p->codeRes;
          dec = &p->dec;
          status = p->status;
          truncated = p->parsing_Truncated;
        }
        else
          return SZ_ERROR_FAIL;

        if (p->mainErrorCode != SZ_OK)
          stat->DecodeRes = p->mainErrorCode;

        XzStatInfo_SetStat(dec, p->finishMode,
            /* p->mtc.readProcessed, */
            p->mtc.inProcessed,
            codeRes, status,
            truncated,
            stat);
      }

      if (res == SZ_OK)
      {
        stat->ReadRes = p->mtc.readRes;

        if (p->writeRes != SZ_OK)
        {
          res = p->writeRes;
          stat->CombinedRes_Type = SZ_ERROR_WRITE;
        }
        else if (p->mtc.readRes != SZ_OK
            /* && p->mtc.inProcessed == p->mtc.readProcessed */
            && stat->DecodeRes == SZ_ERROR_INPUT_EOF)
        {
          res = p->mtc.readRes;
          stat->CombinedRes_Type = SZ_ERROR_READ;
        }
        else if (stat->DecodeRes != SZ_OK)
          res = stat->DecodeRes;
      }
      
      stat->CombinedRes = res;
      if (stat->CombinedRes_Type == SZ_OK)
        stat->CombinedRes_Type = res;
      return res;
    }

    PRF_STR("----- decoding ST -----")
  }

  #endif


  *isMT = False;

  {
    SRes res = XzDecMt_Decode_ST(p
        #ifndef Z7_ST
        , tMode
        #endif
        , stat
        );

    #ifndef Z7_ST
    /* we must set error code from MT decoding at first */
    if (p->mainErrorCode != SZ_OK)
      stat->DecodeRes = p->mainErrorCode;
    #endif

    XzStatInfo_SetStat(&p->dec,
        p->finishMode,
        /* p->readProcessed, */
        p->inProcessed,
        p->codeRes, p->status,
        False, /* truncated */
        stat);

    stat->ReadRes = p->readRes;

    if (res == SZ_OK)
    {
      if (p->readRes != SZ_OK
          /* && p->inProcessed == p->readProcessed */
          && stat->DecodeRes == SZ_ERROR_INPUT_EOF)
      {
        /* we set read error as combined error, only if that error was the reason */
        /* of decoding problem */
        res = p->readRes;
        stat->CombinedRes_Type = SZ_ERROR_READ;
      }
      else if (stat->DecodeRes != SZ_OK)
        res = stat->DecodeRes;
    }

    stat->CombinedRes = res;
    if (stat->CombinedRes_Type == SZ_OK)
      stat->CombinedRes_Type = res;
    return res;
  }
}

#undef PRF
#undef PRF_STR
#undef PRF_STR_INT_2
#undef BRA_BUF_SIZE
#undef CODER_BUF_SIZE
#undef PRF
#undef PRF_STR
#undef PRF_STR_INT
#undef READ_VARINT_AND_CHECK
#undef XZDECMT_STREAM_WRITE_STEP
#undef XZ_CHECK_SIZE_MAX
#undef XZ_IS_SUPPORTED_FILTER_ID

/* ==== XzIn.c ==== */
/* XzIn.c - Xz input
: Igor Pavlov : Public domain */



#include <string.h>





#define XZ_FOOTER_12B_ALIGNED16_SIG_CHECK(p) \
    (GetUi16a((const Byte *)(const void *)(p) + 10) == \
      (XZ_FOOTER_SIG_0 | (XZ_FOOTER_SIG_1 << 8)))

SRes Xz_ReadHeader(CXzStreamFlags *p, ISeqInStreamPtr inStream)
{
  UInt32 data32[XZ_STREAM_HEADER_SIZE / 4];
  size_t processedSize = XZ_STREAM_HEADER_SIZE;
  RINOK(SeqInStream_ReadMax(inStream, data32, &processedSize))
  if (processedSize != XZ_STREAM_HEADER_SIZE
      || memcmp(data32, XZ_SIG, XZ_SIG_SIZE) != 0)
    return SZ_ERROR_NO_ARCHIVE;
  return Xz_ParseHeader(p, (const Byte *)(const void *)data32);
}

#define READ_VARINT_AND_CHECK(buf, size, res) \
{ const unsigned s = Xz_ReadVarInt(buf, size, res); \
  if (s == 0) return SZ_ERROR_ARCHIVE; \
  size -= s; \
  buf += s; \
}

SRes XzBlock_ReadHeader(CXzBlock *p, ISeqInStreamPtr inStream, BoolInt *isIndex, UInt32 *headerSizeRes)
{
  MY_ALIGN(4)
  Byte header[XZ_BLOCK_HEADER_SIZE_MAX];
  unsigned headerSize;
  *headerSizeRes = 0;
  RINOK(SeqInStream_ReadByte(inStream, &header[0]))
  headerSize = header[0];
  if (headerSize == 0)
  {
    *headerSizeRes = 1;
    *isIndex = True;
    return SZ_OK;
  }

  *isIndex = False;
  headerSize = (headerSize << 2) + 4;
  *headerSizeRes = (UInt32)headerSize;
  {
    size_t processedSize = headerSize - 1;
    RINOK(SeqInStream_ReadMax(inStream, header + 1, &processedSize))
    if (processedSize != headerSize - 1)
      return SZ_ERROR_INPUT_EOF;
  }
  return XzBlock_Parse(p, header);
}


#define ADD_SIZE_CHECK(size, val) \
{ const UInt64 newSize = size + (val); \
  if (newSize < size) return XZ_SIZE_OVERFLOW; \
  size = newSize; \
}

UInt64 Xz_GetUnpackSize(const CXzStream *p)
{
  UInt64 size = 0;
  size_t i;
  for (i = 0; i < p->numBlocks; i++)
  {
    ADD_SIZE_CHECK(size, p->blocks[i].unpackSize)
  }
  return size;
}

UInt64 Xz_GetPackSize(const CXzStream *p)
{
  UInt64 size = 0;
  size_t i;
  for (i = 0; i < p->numBlocks; i++)
  {
    ADD_SIZE_CHECK(size, (p->blocks[i].totalSize + 3) & ~(UInt64)3)
  }
  return size;
}


/* input; */
/*   CXzStream (p) is empty object. */
/*   size != 0 */
/*   (size & 3) == 0 */
/*   (buf) is aligned for at least 4 bytes. */
/* output: */
/*   p->numBlocks is number of allocated items in p->blocks */
/*   p->blocks[*] values must be ignored, if function returns error. */
static SRes Xz_ParseIndex(CXzStream *p, const Byte *buf, size_t size, ISzAllocPtr alloc)
{
  size_t numBlocks;
  if (size < 5 || buf[0] != 0)
    return SZ_ERROR_ARCHIVE;
  size -= 4;
  {
    const UInt32 crc = CrcCalc(buf, size);
    if (crc != GetUi32a(buf + size))
      return SZ_ERROR_ARCHIVE;
  }
  buf++;
  size--;
  {
    UInt64 numBlocks64;
    READ_VARINT_AND_CHECK(buf, size, &numBlocks64)
    /* (numBlocks64) is 63-bit value, so we can calculate (numBlocks64 * 2): */
    if (numBlocks64 * 2 > size)
      return SZ_ERROR_ARCHIVE;
    if (numBlocks64 >= ((size_t)1 << (sizeof(size_t) * 8 - 1)) / sizeof(CXzBlockSizes))
      return SZ_ERROR_MEM; /* SZ_ERROR_ARCHIVE */
    numBlocks = (size_t)numBlocks64;
  }
  /* Xz_Free(p, alloc); // it's optional, because (p) is empty already */
  if (numBlocks)
  {
    CXzBlockSizes *blocks = (CXzBlockSizes *)ISzAlloc_Alloc(alloc, sizeof(CXzBlockSizes) * numBlocks);
    if (!blocks)
      return SZ_ERROR_MEM;
    p->blocks = blocks;
    p->numBlocks = numBlocks;
    /* the caller will call Xz_Free() in case of error */
    do
    {
      READ_VARINT_AND_CHECK(buf, size, &blocks->totalSize)
      READ_VARINT_AND_CHECK(buf, size, &blocks->unpackSize)
      if (blocks->totalSize == 0)
        return SZ_ERROR_ARCHIVE;
      blocks++;
    }
    while (--numBlocks);
  }
  if (size >= 4)
    return SZ_ERROR_ARCHIVE;
  while (size)
    if (buf[--size])
      return SZ_ERROR_ARCHIVE;
  return SZ_OK;
}


/*
static SRes Xz_ReadIndex(CXzStream *p, ILookInStreamPtr stream, UInt64 indexSize, ISzAllocPtr alloc)
{
  SRes res;
  size_t size;
  Byte *buf;
  if (indexSize >= ((size_t)1 << (sizeof(size_t) * 8 - 1)))
    return SZ_ERROR_MEM; // SZ_ERROR_ARCHIVE
  size = (size_t)indexSize;
  buf = (Byte *)ISzAlloc_Alloc(alloc, size);
  if (!buf)
    return SZ_ERROR_MEM;
  res = LookInStream_Read2(stream, buf, size, SZ_ERROR_UNSUPPORTED);
  if (res == SZ_OK)
    res = Xz_ParseIndex(p, buf, size, alloc);
  ISzAlloc_Free(alloc, buf);
  return res;
}
*/

static SRes LookInStream_SeekRead_ForArc(ILookInStreamPtr stream, UInt64 offset, void *buf, size_t size)
{
  RINOK(LookInStream_SeekTo(stream, offset))
  return LookInStream_Read(stream, buf, size);
  /* return LookInStream_Read2(stream, buf, size, SZ_ERROR_NO_ARCHIVE); */
}


/*
in:
  (*startOffset) is position in (stream) where xz_stream must be finished.
out:
  if returns SZ_OK, then (*startOffset) is position in stream that shows start of xz_stream.
*/
static SRes Xz_ReadBackward(CXzStream *p, ILookInStreamPtr stream, Int64 *startOffset, ISzAllocPtr alloc)
{
  #define TEMP_BUF_SIZE  (1 << 10)
  UInt32 buf32[TEMP_BUF_SIZE / 4];
  UInt64 pos = (UInt64)*startOffset;

  if ((pos & 3) || pos < XZ_STREAM_FOOTER_SIZE)
    return SZ_ERROR_NO_ARCHIVE;
  pos -= XZ_STREAM_FOOTER_SIZE;
  RINOK(LookInStream_SeekRead_ForArc(stream, pos, buf32, XZ_STREAM_FOOTER_SIZE))
  
  if (!XZ_FOOTER_12B_ALIGNED16_SIG_CHECK(buf32))
  {
    pos += XZ_STREAM_FOOTER_SIZE;
    for (;;)
    {
      /* pos != 0 */
      /* (pos & 3) == 0 */
      size_t i = pos >= TEMP_BUF_SIZE ? TEMP_BUF_SIZE : (size_t)pos;
      pos -= i;
      RINOK(LookInStream_SeekRead_ForArc(stream, pos, buf32, i))
      i /= 4;
      do
        if (buf32[i - 1] != 0)
          break;
      while (--i);

      pos += i * 4;
      #define XZ_STREAM_BACKWARD_READING_PAD_MAX (1 << 16)
      /* here we don't support rare case with big padding for xz stream. */
      /* so we have padding limit for backward reading. */
      if ((UInt64)*startOffset - pos > XZ_STREAM_BACKWARD_READING_PAD_MAX)
        return SZ_ERROR_NO_ARCHIVE;
      if (i)
        break;
    }
    /* we try to open xz stream after skipping zero padding. */
    /* ((UInt64)*startOffset == pos) is possible here! */
    if (pos < XZ_STREAM_FOOTER_SIZE)
      return SZ_ERROR_NO_ARCHIVE;
    pos -= XZ_STREAM_FOOTER_SIZE;
    RINOK(LookInStream_SeekRead_ForArc(stream, pos, buf32, XZ_STREAM_FOOTER_SIZE))
    if (!XZ_FOOTER_12B_ALIGNED16_SIG_CHECK(buf32))
      return SZ_ERROR_NO_ARCHIVE;
  }
  
  p->flags = (CXzStreamFlags)GetBe16a(buf32 + 2);
  if (!XzFlags_IsSupported(p->flags))
    return SZ_ERROR_UNSUPPORTED;
  {
    /* to eliminate GCC 6.3 warning:
       dereferencing type-punned pointer will break strict-aliasing rules */
    const UInt32 *buf_ptr = buf32;
    if (GetUi32a(buf_ptr) != CrcCalc(buf32 + 1, 6))
      return SZ_ERROR_ARCHIVE;
  }
  {
    const UInt64 indexSize = ((UInt64)GetUi32a(buf32 + 1) + 1) << 2;
    if (pos < indexSize)
      return SZ_ERROR_ARCHIVE;
    pos -= indexSize;
    /* v25.00: relaxed indexSize check. We allow big index table. */
    /* if (indexSize > ((UInt32)1 << 31)) */
    if (indexSize >= ((size_t)1 << (sizeof(size_t) * 8 - 1)))
      return SZ_ERROR_MEM; /* SZ_ERROR_ARCHIVE */
    RINOK(LookInStream_SeekTo(stream, pos))
    /* RINOK(Xz_ReadIndex(p, stream, indexSize, alloc)) */
    {
      SRes res;
      const size_t size = (size_t)indexSize;
      /* if (size != indexSize) return SZ_ERROR_UNSUPPORTED; */
      Byte *buf = (Byte *)ISzAlloc_Alloc(alloc, size);
      if (!buf)
        return SZ_ERROR_MEM;
      res = LookInStream_Read2(stream, buf, size, SZ_ERROR_UNSUPPORTED);
      if (res == SZ_OK)
        res = Xz_ParseIndex(p, buf, size, alloc);
      ISzAlloc_Free(alloc, buf);
      RINOK(res)
    }
  }
  {
    UInt64 total = Xz_GetPackSize(p);
    if (total == XZ_SIZE_OVERFLOW || total >= ((UInt64)1 << 63))
      return SZ_ERROR_ARCHIVE;
    total += XZ_STREAM_HEADER_SIZE;
    if (pos < total)
      return SZ_ERROR_ARCHIVE;
    pos -= total;
    RINOK(LookInStream_SeekTo(stream, pos))
    *startOffset = (Int64)pos;
  }
  {
    CXzStreamFlags headerFlags;
    CSecToRead secToRead;
    SecToRead_CreateVTable(&secToRead);
    secToRead.realStream = stream;
    RINOK(Xz_ReadHeader(&headerFlags, &secToRead.vt))
    return (p->flags == headerFlags) ? SZ_OK : SZ_ERROR_ARCHIVE;
  }
}


/* ---------- Xz Streams ---------- */

void Xzs_Construct(CXzs *p)
{
  Xzs_CONSTRUCT(p)
}

void Xzs_Free(CXzs *p, ISzAllocPtr alloc)
{
  size_t i;
  for (i = 0; i < p->num; i++)
    Xz_Free(&p->streams[i], alloc);
  ISzAlloc_Free(alloc, p->streams);
  p->num = p->numAllocated = 0;
  p->streams = NULL;
}

UInt64 Xzs_GetNumBlocks(const CXzs *p)
{
  UInt64 num = 0;
  size_t i;
  for (i = 0; i < p->num; i++)
    num += p->streams[i].numBlocks;
  return num;
}

UInt64 Xzs_GetUnpackSize(const CXzs *p)
{
  UInt64 size = 0;
  size_t i;
  for (i = 0; i < p->num; i++)
  {
    ADD_SIZE_CHECK(size, Xz_GetUnpackSize(&p->streams[i]))
  }
  return size;
}

/*
UInt64 Xzs_GetPackSize(const CXzs *p)
{
  UInt64 size = 0;
  size_t i;
  for (i = 0; i < p->num; i++)
  {
    ADD_SIZE_CHECK(size, Xz_GetTotalSize(&p->streams[i]))
  }
  return size;
}
*/

SRes Xzs_ReadBackward(CXzs *p, ILookInStreamPtr stream, Int64 *startOffset, ICompressProgressPtr progress, ISzAllocPtr alloc)
{
  Int64 endOffset = 0;
  /* it's supposed that CXzs object is empty here. */
  /* if CXzs object is not empty, it will add new streams to that non-empty object. */
  /* Xzs_Free(p, alloc); // it's optional call to empty CXzs object. */
  RINOK(ILookInStream_Seek(stream, &endOffset, SZ_SEEK_END))
  *startOffset = endOffset;
  for (;;)
  {
    CXzStream st;
    SRes res;
    Xz_CONSTRUCT(&st)
    res = Xz_ReadBackward(&st, stream, startOffset, alloc);
    /* if (res == SZ_OK), then (*startOffset) is start offset of new stream if */
    /* if (res != SZ_OK), then (*startOffset) is unchend or it's expected start offset of stream with error */
    st.startOffset = (UInt64)*startOffset;
    /* we must store (st) object to array, or we must free (st) local object. */
    if (res != SZ_OK)
    {
      Xz_Free(&st, alloc);
      return res;
    }
    if (p->num == p->numAllocated)
    {
      const size_t newNum = p->num + p->num / 4 + 1;
      void *data = ISzAlloc_Alloc(alloc, newNum * sizeof(CXzStream));
      if (!data)
      {
        Xz_Free(&st, alloc);
        return SZ_ERROR_MEM;
      }
      p->numAllocated = newNum;
      if (p->num != 0)
        memcpy(data, p->streams, p->num * sizeof(CXzStream));
      ISzAlloc_Free(alloc, p->streams);
      p->streams = (CXzStream *)data;
    }
    /* we use direct copying of raw data from local variable (st) to object in array. */
    /* so we don't need to call Xz_Free(&st, alloc) after copying and after p->num++ */
    p->streams[p->num++] = st;
    if (*startOffset == 0)
      return SZ_OK;
    /* seek operation is optional: */
    /* RINOK(LookInStream_SeekTo(stream, (UInt64)*startOffset)) */
    if (progress && ICompressProgress_Progress(progress, (UInt64)(endOffset - *startOffset), (UInt64)(Int64)-1) != SZ_OK)
      return SZ_ERROR_PROGRESS;
  }
}
#undef ADD_SIZE_CHECK
#undef READ_VARINT_AND_CHECK
#undef TEMP_BUF_SIZE
#undef XZ_FOOTER_12B_ALIGNED16_SIG_CHECK
#undef XZ_STREAM_BACKWARD_READING_PAD_MAX

/* ==== XzCrc64.c ==== */
/* XzCrc64.c -- CRC64 calculation
2023-12-08 : Igor Pavlov : Public domain */






#define kCrc64Poly UINT64_CONST(0xC96C5795D7870F42)

/* for debug only : define Z7_CRC64_DEBUG_BE to test big-endian code in little-endian cpu */
/* #define Z7_CRC64_DEBUG_BE */
#ifdef Z7_CRC64_DEBUG_BE
#undef MY_CPU_LE
#define MY_CPU_BE
#endif

#ifdef Z7_CRC64_NUM_TABLES
  #define Z7_CRC64_NUM_TABLES_USE  Z7_CRC64_NUM_TABLES
#else
  #define Z7_CRC64_NUM_TABLES_USE  12
#endif

#if Z7_CRC64_NUM_TABLES_USE < 1
  #error Stop_Compiling_Bad_Z7_CRC_NUM_TABLES
#endif


#if Z7_CRC64_NUM_TABLES_USE != 1

#ifndef MY_CPU_BE
  #define FUNC_NAME_LE_2(s)   XzCrc64UpdateT ## s
  #define FUNC_NAME_LE_1(s)   FUNC_NAME_LE_2(s)
  #define FUNC_NAME_LE        FUNC_NAME_LE_1(Z7_CRC64_NUM_TABLES_USE)
  UInt64 Z7_FASTCALL FUNC_NAME_LE (UInt64 v, const void *data, size_t size, const UInt64 *table);
#endif
#ifndef MY_CPU_LE
  #define FUNC_NAME_BE_2(s)   XzCrc64UpdateBeT ## s
  #define FUNC_NAME_BE_1(s)   FUNC_NAME_BE_2(s)
  #define FUNC_NAME_BE        FUNC_NAME_BE_1(Z7_CRC64_NUM_TABLES_USE)
  UInt64 Z7_FASTCALL FUNC_NAME_BE (UInt64 v, const void *data, size_t size, const UInt64 *table);
#endif

#if defined(MY_CPU_LE)
  #define FUNC_REF  FUNC_NAME_LE
#elif defined(MY_CPU_BE)
  #define FUNC_REF  FUNC_NAME_BE
#else
  #define FUNC_REF  g_Crc64Update
  static UInt64 (Z7_FASTCALL *FUNC_REF)(UInt64 v, const void *data, size_t size, const UInt64 *table);
#endif

#endif


MY_ALIGN(64)
static UInt64 g_Crc64Table[256 * Z7_CRC64_NUM_TABLES_USE];


UInt64 Z7_FASTCALL Crc64Update(UInt64 v, const void *data, size_t size)
{
#if Z7_CRC64_NUM_TABLES_USE == 1
  #define CRC64_UPDATE_BYTE_2(crc, b)  (table[((crc) ^ (b)) & 0xFF] ^ ((crc) >> 8))
  const UInt64 *table = g_Crc64Table;
  const Byte *p = (const Byte *)data;
  const Byte *lim = p + size;
  for (; p != lim; p++)
    v = CRC64_UPDATE_BYTE_2(v, *p);
  return v;
  #undef CRC64_UPDATE_BYTE_2
#else
  return FUNC_REF (v, data, size, g_Crc64Table);
#endif
}


Z7_NO_INLINE
void Z7_FASTCALL Crc64GenerateTable(void)
{
  unsigned i;
  for (i = 0; i < 256; i++)
  {
    UInt64 r = i;
    unsigned j;
    for (j = 0; j < 8; j++)
      r = (r >> 1) ^ (kCrc64Poly & ((UInt64)0 - (r & 1)));
    g_Crc64Table[i] = r;
  }

#if Z7_CRC64_NUM_TABLES_USE != 1
#if 1 || 1 && defined(MY_CPU_X86) /* low register count */
  for (i = 0; i < 256 * (Z7_CRC64_NUM_TABLES_USE - 1); i++)
  {
    const UInt64 r0 = g_Crc64Table[(size_t)i];
    g_Crc64Table[(size_t)i + 256] = g_Crc64Table[(Byte)r0] ^ (r0 >> 8);
  }
#else
  for (i = 0; i < 256 * (Z7_CRC64_NUM_TABLES_USE - 1); i += 2)
  {
    UInt64 r0 = g_Crc64Table[(size_t)(i)    ];
    UInt64 r1 = g_Crc64Table[(size_t)(i) + 1];
    r0 = g_Crc64Table[(Byte)r0] ^ (r0 >> 8);
    r1 = g_Crc64Table[(Byte)r1] ^ (r1 >> 8);
    g_Crc64Table[(size_t)i + 256    ] = r0;
    g_Crc64Table[(size_t)i + 256 + 1] = r1;
  }
#endif

#ifndef MY_CPU_LE
  {
#ifndef MY_CPU_BE
    UInt32 k = 1;
    if (*(const Byte *)&k == 1)
      FUNC_REF = FUNC_NAME_LE;
    else
#endif
    {
#ifndef MY_CPU_BE
      FUNC_REF = FUNC_NAME_BE;
#endif
      for (i = 0; i < 256 * Z7_CRC64_NUM_TABLES_USE; i++)
      {
        const UInt64 x = g_Crc64Table[i];
        g_Crc64Table[i] = Z7_BSWAP64(x);
      }
    }
  }
#endif /* ndef MY_CPU_LE */
#endif /* Z7_CRC64_NUM_TABLES_USE != 1 */
}

#undef kCrc64Poly
#undef Z7_CRC64_NUM_TABLES_USE
#undef FUNC_REF
#undef FUNC_NAME_LE_2
#undef FUNC_NAME_LE_1
#undef FUNC_NAME_LE
#undef FUNC_NAME_BE_2
#undef FUNC_NAME_BE_1
#undef FUNC_NAME_BE
#undef CRC64_UPDATE_BYTE_2
#undef FUNC_NAME_BE
#undef FUNC_NAME_BE_1
#undef FUNC_NAME_BE_2
#undef FUNC_NAME_LE
#undef FUNC_NAME_LE_1
#undef FUNC_NAME_LE_2
#undef FUNC_REF
#undef MY_CPU_BE
#undef Z7_CRC64_NUM_TABLES_USE
#undef kCrc64Poly

#endif /* LZMASDK_IMPLEMENTATION */
