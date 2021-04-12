/*
** digest.h - Digest/Checksum functions
**
** Copyright (c) 2020-2021, Peter Eriksson <pen@lysator.liu.se>
** All rights reserved.
** 
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are met:
** 
** 1. Redistributions of source code must retain the above copyright notice, this
**    list of conditions and the following disclaimer.
** 
** 2. Redistributions in binary form must reproduce the above copyright notice,
**    this list of conditions and the following disclaimer in the documentation
**    and/or other materials provided with the distribution.
** 
** 3. Neither the name of the copyright holder nor the names of its
**    contributors may be used to endorse or promote products derived from
**    this software without specific prior written permission.
** 
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
** AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
** DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
** FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
** DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
** SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
** CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
** OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
** OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef DIGEST_H
#define DIGEST_H 1

#include <sys/types.h>

#include <zlib.h>
#define HAVE_ADLER32 1
#define HAVE_CRC32   1

#ifdef __FreeBSD__
#include <md5.h>
#define HAVE_MD5 1

#include <skein.h>
#define HAVE_SKEIN256 1

#include <sha256.h>
#define HAVE_SHA256 1

#include <sha384.h>
#define HAVE_SHA384 1

#include <sha512.h>
#define HAVE_SHA512 1
#endif


typedef enum {
	      DIGEST_TYPE_INVALID  = -1,
	      DIGEST_TYPE_NONE     = 0,
#if HAVE_ADLER32
	      DIGEST_TYPE_ADLER32  = 1,
#endif
#if HAVE_CRC32
	      DIGEST_TYPE_CRC32    = 2,
#endif
#if HAVE_MD5
	      DIGEST_TYPE_MD5      = 3,
#endif
#if HAVE_SKEIN256
	      DIGEST_TYPE_SKEIN256 = 4,
#endif
#if HAVE_SHA256
	      DIGEST_TYPE_SHA256   = 5,
#endif
#if HAVE_SHA384
	      DIGEST_TYPE_SHA384   = 6,
#endif
#if HAVE_SHA512
	      DIGEST_TYPE_SHA512   = 7,
#endif
} DIGEST_TYPE;

#if defined(DIGEST_TYPE_SHA512)
#define DIGEST_TYPE_BEST DIGEST_TYPE_SHA512
#elif defined(DIGEST_TYPE_CRC32)
#define DIGEST_TYPE_BEST DIGEST_TYPE_CRC32
#else
#define DIGEST_TYPE_BEST DIGEST_TYPE_NONE
#endif

typedef enum {
	      DIGEST_STATE_NONE    = 0,
	      DIGEST_STATE_INIT    = 1,
	      DIGEST_STATE_UPDATE  = 2,
	      DIGEST_STATE_FINAL   = 3,
} DIGEST_STATE;


typedef struct digest {
  DIGEST_TYPE  type;
  DIGEST_STATE state;
  union {
#if HAVE_CRC32
    uint32_t     crc32;
#endif
#if HAVE_ADLER32 
    uint32_t     adler32;
#endif
#if HAVE_MD5
    MD5_CTX      md5;
#endif
#if HAVE_SKEIN256
    SKEIN256_CTX skein256;
#endif
#if HAVE_SHA256
    SHA256_CTX   sha256;
#endif
#if HAVE_SHA384
    SHA384_CTX   sha384;
#endif
#if HAVE_SHA512
    SHA512_CTX   sha512;
#endif
  } ctx;
} DIGEST;


/*
 * Result buffer sizes
 */
#if HAVE_ADLER32
#define DIGEST_BUFSIZE_ADLER32  sizeof(uint32_t)
#endif
#if HAVE_CRC32
#define DIGEST_BUFSIZE_CRC32    sizeof(uint32_t)
#endif
#if HAVE_MD5
#define DIGEST_BUFSIZE_MD5      16
#endif
#if HAVE_SKEIN256
#define DIGEST_BUFSIZE_SKEIN256 32
#endif
#if HAVE_SHA256
#define DIGEST_BUFSIZE_SHA256   32
#endif
#if HAVE_SHA384
#define DIGEST_BUFSIZE_SHA384   48  
#endif
#if HAVE_SHA512
#define DIGEST_BUFSIZE_SHA512   64
#endif
#define DIGEST_BUFSIZE_MAX      64


extern int
digest_init(DIGEST *dp,
	    DIGEST_TYPE type);

extern void
digest_destroy(DIGEST *dp);

extern int
digest_update(DIGEST *dp,
	      unsigned char *buf,
	      size_t bufsize);

extern ssize_t
digest_final(DIGEST *dp,
	     unsigned char *buf,
	     size_t bufsize);


extern DIGEST_TYPE
digest_typeof(DIGEST *dp);

extern DIGEST_STATE
digest_stateof(DIGEST *dp);


extern DIGEST_TYPE
digest_str2type(const char *str);

extern const char *
digest_type2str(DIGEST_TYPE type);

#endif
