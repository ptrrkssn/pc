/*
** digest.c - Digest/Checksum functions
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

#include <errno.h>
#include <string.h>
#include <arpa/inet.h>

#include "digest.h"


DIGEST_TYPE
digest_str2type(const char *s) {
  if (strcasecmp(s, "NONE") == 0)
    return DIGEST_TYPE_NONE;

#if defined(HAVE_ADLER32_Z)
  if (strcasecmp(s, "ADLER32") == 0 || strcasecmp(s, "ADLER-32") == 0)
    return DIGEST_TYPE_ADLER32;
#endif

#if defined(HAVE_CRC32_Z)
  if (strcasecmp(s, "CRC32") == 0 || strcasecmp(s, "CRC-32") == 0)
    return DIGEST_TYPE_CRC32;
#endif

#if defined(HAVE_NETTLE_MD5_INIT) || defined(HAVE_MD5_INIT) || defined(HAVE_MD5INIT)
  if (strcasecmp(s, "MD5") == 0 || strcasecmp(s, "MD-5") == 0)
    return DIGEST_TYPE_MD5;
#endif

#if defined(HAVE_SKEIN256_INIT)
  if (strcasecmp(s, "SKEIN256") == 0 || strcasecmp(s, "SKEIN-256") == 0)
    return DIGEST_TYPE_SKEIN256;
#endif

#if defined(HAVE_SKEIN1024_INIT)
  if (strcasecmp(s, "SKEIN1024") == 0 || strcasecmp(s, "SKEIN-1024") == 0)
    return DIGEST_TYPE_SKEIN1024;
#endif

#if defined(HAVE_SHA256_INIT) || defined(HAVE_NETTLE_SHA256_INIT)
  if (strcasecmp(s, "SHA256") == 0 || strcasecmp(s, "SHA-256") == 0 ||
      strcasecmp(s, "SHA2-256") == 0 || strcasecmp(s, "SHA2-256") == 0)
    return DIGEST_TYPE_SHA256;
#endif

#if defined(HAVE_SHA512_INIT) || defined(HAVE_NETTLE_SHA512_INIT)
  if (strcasecmp(s, "SHA512") == 0 || strcasecmp(s, "SHA-512") == 0 ||
      strcasecmp(s, "SHA2-512") == 0 || strcasecmp(s, "SHA2-512") == 0)
    return DIGEST_TYPE_SHA512;
#endif

#if defined(HAVE_NETTLE_SHA3_256_INIT)
  if (strcasecmp(s, "SHA3-256") == 0)
    return DIGEST_TYPE_SHA3_256;
#endif

#if defined(HAVE_NETTLE_SHA3_512_INIT)
  if (strcasecmp(s, "SHA3-512") == 0)
    return DIGEST_TYPE_SHA3_512;
#endif
  return -1;
}


const char *
digest_type2str(DIGEST_TYPE type) {
  switch (type) {
  case DIGEST_TYPE_NONE:
    return "NONE";

#if defined(HAVE_ADLER32_Z)
  case DIGEST_TYPE_ADLER32:
    return "ADLER32";
#endif

#if defined(HAVE_CRC32_Z)
  case DIGEST_TYPE_CRC32:
    return "CRC32";
#endif

#if defined(HAVE_NETTLE_MD5_INIT) || defined(HAVE_MD5_INIT) || defined(HAVE_MD5INIT)
  case DIGEST_TYPE_MD5:
    return "MD5";
#endif

#if defined(HAVE_SKEIN256_INIT)
  case DIGEST_TYPE_SKEIN256:
    return "SKEIN256";
#endif

#if defined(HAVE_SKEIN1024_INIT)
  case DIGEST_TYPE_SKEIN1024:
    return "SKEIN1024";
#endif

#if defined(HAVE_SHA256_INIT) || defined(HAVE_NETTLE_SHA256_INIT)
  case DIGEST_TYPE_SHA256:
    return "SHA256";
#endif

#if defined(HAVE_SHA512_INIT) || defined(HAVE_NETTLE_SHA512_INIT)
  case DIGEST_TYPE_SHA512:
    return "SHA512";
#endif

#if defined(HAVE_NETTLE_SHA3_256_INIT)
  case DIGEST_TYPE_SHA3_256:
    return "SHA3-256";
#endif

#if defined(HAVE_NETTLE_SHA3_512_INIT)
  case DIGEST_TYPE_SHA3_512:
    return "SHA3-512";
#endif

  default:
    return NULL;
  }
}


int
digest_init(DIGEST *dp,
	    DIGEST_TYPE type) {
  if (type == DIGEST_TYPE_INVALID)
    return -1;
  
  memset(dp, 0, sizeof(*dp));

  dp->state = DIGEST_STATE_NONE;
  
  switch (dp->type) {
  case DIGEST_TYPE_NONE:
    break;

  case DIGEST_TYPE_ADLER32:
#if defined(HAVE_ADLER32_Z)
    dp->ctx.adler32 = adler32_z(0L, NULL, 0);
    break;
#else
    errno = ENOSYS;
    return -1;
#endif

  case DIGEST_TYPE_CRC32:
#if defined(HAVE_CRC32_Z)
    dp->ctx.crc32 = crc32_z(0L, NULL, 0);
    break;
#else
    errno = ENOSYS;
    return -1;
#endif

  case DIGEST_TYPE_MD5:
#if defined(HAVE_NETTLE_MD5_INIT)
    md5_init(&dp->ctx.md5);
    break;
#elif defined(HAVE_MD5_INIT)
    MD5_Init(&dp->ctx.md5);
    break;
#elif defined(HAVE_MD5INIT)
    MD5Init(&dp->ctx.md5);
    break;
#else
    errno = ENOSYS;
    return -1;
#endif

  case DIGEST_TYPE_SKEIN256:
#if defined(HAVE_SKEIN256_INIT)
    SKEIN256_Init(&dp->ctx.skein256);
    break;
#else
    errno = ENOSYS;
    return -1;
#endif
    
  case DIGEST_TYPE_SKEIN1024:
#if defined(HAVE_SKEIN1024_INIT)
    SKEIN1024_Init(&dp->ctx.skein1024);
    break;
#else
    errno = ENOSYS;
    return -1;
#endif
    
  case DIGEST_TYPE_SHA256:
#if defined(HAVE_NETTLE_SHA256_INIT)
    sha256_init(&dp->ctx.sha256);
    break;
#elif defined(HAVE_SHA256_INIT)
    SHA256_Init(&dp->ctx.sha256);
    break;
#else
    errno = ENOSYS;
    return -1;
#endif
    
  case DIGEST_TYPE_SHA512:
#if defined(HAVE_NETTLE_SHA512_INIT)
    sha512_init(&dp->ctx.sha512);
    break;
#elif defined(HAVE_SHA512_INIT)
    SHA512_Init(&dp->ctx.sha512);
    break;
#else
    errno = ENOSYS;
    return -1;
#endif

  case DIGEST_TYPE_SHA3_256:
#if defined(HAVE_NETTLE_SHA3_256_INIT)
    sha3_256_init(&dp->ctx.sha3_256);
    break;
#else
    errno = ENOSYS;
    return -1;
#endif

  case DIGEST_TYPE_SHA3_512:
#if defined(HAVE_NETTLE_SHA3_512_INIT)
    sha3_512_init(&dp->ctx.sha3_512);
    break;
#else
    errno = ENOSYS;
    return -1;
#endif

#if 1
  case DIGEST_TYPE_INVALID:
    errno = EINVAL;
    return -1;
#else
  default:
    return -1;
#endif
  }

  dp->type = type;
  dp->state = DIGEST_STATE_INIT;
  return 0;
}



int
digest_update(DIGEST *dp,
	      unsigned char *buf,
	      size_t bufsize) {

  if (!dp)
    return -1;

  switch (dp->state) {
  case DIGEST_STATE_INIT:
  case DIGEST_STATE_UPDATE:
    switch (dp->type) {
    case DIGEST_TYPE_NONE:
      break;
      
    case DIGEST_TYPE_ADLER32:
#if defined(HAVE_ADLER32_Z)
      dp->ctx.adler32 = adler32_z(dp->ctx.adler32, buf, bufsize);
      break;
#else
      errno = ENOSYS;
      return -1;
#endif

    case DIGEST_TYPE_CRC32:
#if defined(HAVE_CRC32_Z)
      dp->ctx.crc32 = crc32_z(dp->ctx.crc32, buf, bufsize);
      break;
#else
      errno = ENOSYS;
      return -1;
#endif      

    case DIGEST_TYPE_MD5:
#if defined(HAVE_NETTLE_MD5_INIT)
      md5_update(&dp->ctx.md5, bufsize, buf);
      break;
#elif defined(HAVE_MD5_INIT)
      MD5_Update(&dp->ctx.md5, buf, bufsize);
      break;
#elif defined(HAVE_MD5INIT)
      MD5Update(&dp->ctx.md5, buf, bufsize);
      break;
#else
      errno = ENOSYS;
      return -1;
#endif      

    case DIGEST_TYPE_SKEIN256:
#if defined(HAVE_SKEIN256_INIT)
      SKEIN256_Update(&dp->ctx.skein256, buf, bufsize);
      break;
#else
      errno = ENOSYS;
      return -1;
#endif      

    case DIGEST_TYPE_SKEIN1024:
#if defined(HAVE_SKEIN1024_INIT)
      SKEIN1024_Update(&dp->ctx.skein1024, buf, bufsize);
      break;
#else
      errno = ENOSYS;
      return -1;
#endif      

    case DIGEST_TYPE_SHA256:
#if defined(HAVE_NETTLE_SHA256_INIT)
      sha256_update(&dp->ctx.sha256, bufsize, buf);
      break;
#elif defined(HAVE_SHA256_INIT)
      SHA256_Update(&dp->ctx.sha256, buf, bufsize);
      break;
#else
      errno = ENOSYS;
      return -1;
#endif

    case DIGEST_TYPE_SHA512:
#if defined(HAVE_NETTLE_SHA512_INIT)
      sha512_update(&dp->ctx.sha512, bufsize, buf);
      break;
#elif defined(HAVE_SHA512_INIT)
      SHA512_Update(&dp->ctx.sha512, buf, bufsize);
      break;
#else
      errno = ENOSYS;
      return -1;
#endif      

    case DIGEST_TYPE_SHA3_256:
#if defined(HAVE_NETTLE_SHA3_256_INIT)
      sha3_256_update(&dp->ctx.sha3_256, bufsize, buf);
      break;
#else
      errno = ENOSYS;
      return -1;
#endif
      
    case DIGEST_TYPE_SHA3_512:
#if defined(HAVE_NETTLE_SHA3_512_INIT)
      sha3_512_update(&dp->ctx.sha3_512, bufsize, buf);
      break;
#else
      errno = ENOSYS;
      return -1;
#endif

#if 1
  case DIGEST_TYPE_INVALID:
    errno = EINVAL;
    return -1;
#else
    default:
      return -1;
#endif
    }
    break;
    
  default:
    return -1;
  }
  
  dp->state = DIGEST_STATE_UPDATE;
  return 0;
}


ssize_t
digest_final(DIGEST *dp,
	     unsigned char *buf,
	     size_t bufsize) {
  ssize_t rlen = -1;

  
  switch (dp->state) {
  case DIGEST_STATE_NONE:
  case DIGEST_STATE_FINAL:
    errno = EINVAL;
    return -1;
    
  case DIGEST_STATE_INIT:
  case DIGEST_STATE_UPDATE:
    switch (dp->type) {
    case DIGEST_TYPE_INVALID:
      errno = EINVAL;
      return -1;
      
    case DIGEST_TYPE_NONE:
      rlen = 0;
      break;
      
    case DIGEST_TYPE_ADLER32:
      if (bufsize < DIGEST_BUFSIZE_ADLER32) {
	errno = EOVERFLOW;
	return -1;
      }
      rlen = DIGEST_BUFSIZE_ADLER32;
#if defined(HAVE_ADLER32_Z)
      * (uint32_t *) buf = htonl(dp->ctx.adler32);
      break;
#else
      errno = ENOSYS;
      return -1;
#endif
      
    case DIGEST_TYPE_CRC32:
      if (bufsize < DIGEST_BUFSIZE_CRC32) {
	errno = EOVERFLOW;
	return -1;
      }
      rlen = DIGEST_BUFSIZE_CRC32;
#if defined(HAVE_CRC32_Z)
      * (uint32_t *) buf = htonl(dp->ctx.crc32);
      break;
#else
      errno = ENOSYS;
      return -1;
#endif

    case DIGEST_TYPE_MD5:
      if (bufsize < DIGEST_BUFSIZE_MD5) {
	errno = EOVERFLOW;
	return -1;
      }
      rlen = DIGEST_BUFSIZE_MD5;
#if defined(HAVE_NETTLE_MD5_INIT)
      md5_digest(&dp->ctx.md5, DIGEST_BUFSIZE_MD5, buf);
      break;
#elif defined(HAVE_MD5_INIT)
      MD5_Final(buf, &dp->ctx.md5);
      break;
#elif defined(HAVE_MD5INIT)
      MD5Final(buf, &dp->ctx.md5);
      break;
#else
      errno = ENOSYS;
      return -1;
#endif
      
    case DIGEST_TYPE_SKEIN256:
      if (bufsize < DIGEST_BUFSIZE_SKEIN256) {
	errno = EOVERFLOW;
	return -1;
      }
      rlen = DIGEST_BUFSIZE_SKEIN256;
#if defined(HAVE_SKEIN256_INIT)
      SKEIN256_Final(buf, &dp->ctx.skein256);
      break;
#else
      errno = ENOSYS;
      return -1;
#endif
      
    case DIGEST_TYPE_SKEIN1024:
      if (bufsize < DIGEST_BUFSIZE_SKEIN1024) {
	errno = EOVERFLOW;
	return -1;
      }
      rlen = DIGEST_BUFSIZE_SKEIN1024;
#if defined(HAVE_SKEIN1024_INIT)
      SKEIN1024_Final(buf, &dp->ctx.skein1024);
      break;
#else
      errno = ENOSYS;
      return -1;
#endif
      
    case DIGEST_TYPE_SHA256:
      if (bufsize < DIGEST_BUFSIZE_SHA256) {
	errno = EOVERFLOW;
	return -1;
      }
      rlen = DIGEST_BUFSIZE_SHA256;
#if defined(HAVE_NETTLE_SHA256_INIT)
      sha256_digest(&dp->ctx.sha256, DIGEST_BUFSIZE_SHA256, buf);
      break;
#elif defined(HAVE_SHA256_INIT)
      SHA256_Final(buf, &dp->ctx.sha256);
      break;
#else
      errno = ENOSYS;
      return -1;
#endif      

    case DIGEST_TYPE_SHA512:
      if (bufsize < DIGEST_BUFSIZE_SHA512) {
	errno = EOVERFLOW;
	return -1;
      }
      rlen = DIGEST_BUFSIZE_SHA512;
#if defined(HAVE_NETTLE_SHA512_INIT)
      sha512_digest(&dp->ctx.sha512, DIGEST_BUFSIZE_SHA512, buf);
      break;
#elif defined(HAVE_SHA512_INIT)
      SHA512_Final(buf, &dp->ctx.sha512);
      break;
#else
      errno = ENOSYS;
      return -1;
#endif
      
    case DIGEST_TYPE_SHA3_256:
      if (bufsize < DIGEST_BUFSIZE_SHA3_256) {
	errno = EOVERFLOW;
	return -1;
      }
#if defined(HAVE_NETTLE_SHA3_256_INIT)
      sha3_256_digest(&dp->ctx.sha3_256, DIGEST_BUFSIZE_SHA3_256, buf);
      rlen = DIGEST_BUFSIZE_SHA3_256;
      break;
#else
      errno = ENOSYS;
      return -1;
#endif

    case DIGEST_TYPE_SHA3_512:
      if (bufsize < DIGEST_BUFSIZE_SHA3_512) {
	errno = EOVERFLOW;
	return -1;
      }
#if defined(HAVE_NETTLE_SHA3_512_INIT)
      sha3_512_digest(&dp->ctx.sha3_512, DIGEST_BUFSIZE_SHA3_512, buf);
      rlen = DIGEST_BUFSIZE_SHA3_512;
      break;
#else
      errno = ENOSYS;
      return -1;
#endif

#if 0
    default:
      errno = EINVAL;
      return -1;
#endif
    }
    break;
    
  default:
    errno = EINVAL;
    return -1;
  }
  
  dp->state = DIGEST_STATE_FINAL;
  return rlen;
}



void
digest_destroy(DIGEST *dp) {
  unsigned char tbuf[DIGEST_BUFSIZE_MAX];

  (void) digest_final(dp, tbuf, sizeof(tbuf));
  memset(dp, 0, sizeof(*dp));
  dp->state = DIGEST_STATE_NONE;
  dp->type  = DIGEST_TYPE_NONE;
}

DIGEST_TYPE
digest_typeof(DIGEST *dp) {
  if (!dp)
    return DIGEST_TYPE_NONE;
  
  return dp->type;
}

DIGEST_STATE
digest_stateof(DIGEST *dp) {
  if (!dp)
    return DIGEST_STATE_NONE;
  
  return dp->state;
}
