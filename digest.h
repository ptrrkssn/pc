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

#include "config.h"

#include <stdint.h>
#include <sys/types.h>

#if defined(HAVE_ZLIB_H)
#include <zlib.h>
#endif

#if defined(HAVE_NETTLE_MD5_H)
#include <nettle/md5.h>
#elif defined(HAVE_OPENSSL_MD5_H)
#include <openssl/md5.h>
#elif defined(HAVE_MD5_H)
#include <md5.h>
#endif

#if defined(HAVE_SKEIN_H)
#include <skein.h>
#endif

#if defined(HAVE_NETTLE_SHA2_H)
#include <nettle/sha2.h>
#else
#if defined(HAVE_SHA256_H)
#include <sha256.h>
#endif
#if defined(HAVE_SHA512_H)
#include <sha512.h>
#endif
#endif

#if defined(HAVE_NETTLE_SHA3_H)
#include <nettle/sha3.h>
#endif

#if defined(HAVE_OPENSSL_SHA_H)
#include <openssl/sha.h>
#endif


typedef enum {
	      DIGEST_TYPE_INVALID  = -1,
	      DIGEST_TYPE_NONE     = 0,
	      DIGEST_TYPE_ADLER32,
	      DIGEST_TYPE_CRC32,
	      DIGEST_TYPE_MD5,
	      DIGEST_TYPE_SKEIN256,
	      DIGEST_TYPE_SKEIN1024,
	      DIGEST_TYPE_SHA256,
	      DIGEST_TYPE_SHA512,
	      DIGEST_TYPE_SHA3_256,
	      DIGEST_TYPE_SHA3_512
} DIGEST_TYPE;
#define DIGEST_TYPE_LAST 11


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
#if defined(HAVE_CRC32_Z)
    uint32_t     crc32;
#endif
#if defined(HAVE_ADLER32_Z)
    uint32_t     adler32;
#endif

#if defined(HAVE_NETTLE_MD5_INIT)
    struct md5_ctx md5;
#elif defined(HAVE_MD5INIT) || defined(HAVE_MD5_INIT)
    MD5_CTX      md5;
#endif

#if defined(HAVE_SKEIN256_INIT)
    SKEIN256_CTX skein256;
#endif

#if defined(HAVE_SKEIN1024_INIT)
    SKEIN1024_CTX skein1024;
#endif

#if defined(HAVE_NETTLE_SHA256_INIT)
    struct sha256_ctx sha256;
#elif defined(HAVE_SHA256_INIT)
    SHA256_CTX   sha256;
#endif
    
#if defined(HAVE_NETTLE_SHA512_INIT)
    struct sha512_ctx sha512;
#elif defined(HAVE_SHA512_INIT)
    SHA512_CTX   sha512;
#endif

#if defined(HAVE_NETTLE_SHA3_256_INIT)
    struct sha3_256_ctx sha3_256;
#endif
#if defined(HAVE_NETTLE_SHA3_512_INIT)
    struct sha3_512_ctx sha3_512;
#endif
  } ctx;
} DIGEST;


/*
 * Result buffer sizes
 */
#define DIGEST_BUFSIZE_ADLER32  sizeof(uint32_t)
#define DIGEST_BUFSIZE_CRC32    sizeof(uint32_t)

#if defined(MD5_DIGEST_SIZE)
#define DIGEST_BUFSIZE_MD5      MD5_DIGEST_SIZE
#elif defined(MD5_DIGEST_LENGTH)
#define DIGEST_BUFSIZE_MD5      MD5_DIGEST_LENGTH
#else
#define DIGEST_BUFSIZE_MD5      16
#endif

#ifdef SKEIN256_DIGEST_LENGTH
#define DIGEST_BUFSIZE_SKEIN256 SKEIN256_DIGEST_LENGTH
#else
#define DIGEST_BUFSIZE_SKEIN256 32
#endif

#ifdef SKEIN1024_DIGEST_LENGTH
#define DIGEST_BUFSIZE_SKEIN1024 SKEIN1024_DIGEST_LENGTH
#else
#define DIGEST_BUFSIZE_SKEIN256 128
#endif

#if defined(SHA256_DIGEST_SIZE)
#define DIGEST_BUFSIZE_SHA256   SHA256_DIGEST_SIZE
#elif defined(SHA256_DIGEST_LENGTH)
#define DIGEST_BUFSIZE_SHA256   SHA256_DIGEST_LENGTH
#else
#define DIGEST_BUFSIZE_SHA256   32
#endif

#if defined(SHA256_DIGEST_SIZE)
#define DIGEST_BUFSIZE_SHA512   SHA512_DIGEST_SIZE
#elif defined(SHA512_DIGEST_LENGTH)
#define DIGEST_BUFSIZE_SHA512   SHA512_DIGEST_LENGTH
#else
#define DIGEST_BUFSIZE_SHA512   64
#endif


#if defined(SHA3_256_DIGEST_SIZE)
#define DIGEST_BUFSIZE_SHA3_256 SHA3_256_DIGEST_SIZE
#else
#define DIGEST_BUFSIZE_SHA3_256 32
#endif

#if defined(SHA3_512_DIGEST_SIZE)
#define DIGEST_BUFSIZE_SHA3_512 SHA3_512_DIGEST_SIZE
#else
#define DIGEST_BUFSIZE_SHA3_512 64
#endif

#define DIGEST_BUFSIZE_MAX DIGEST_BUFSIZE_SHA3_512


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
