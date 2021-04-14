/*
 * acls.h
 * 
 * Copyright (c) 2021, Peter Eriksson <pen@lysator.liu.se>
 *
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef ACLS_H
#define ACLS_H 1

#if defined(HAVE_SYS_ACL_H)

#if defined(HAVE_ACL)
# define acl_t sunacl_t
# define acl_type_t sunacl_type_t
# define acl_free sunacl_free
#endif

#include <sys/types.h>
#include <sys/acl.h>

#if defined(HAVE_ACL)
# undef acl_t
# undef acl_type_t
# undef acl_free
#endif

#if defined(__APPLE__)
/* MacOS Extended is very similar to ZFS/NFS4 */
#define ACL_TYPE_NFS4 ACL_TYPE_EXTENDED
#endif

#if defined(HAVE_ACL)
/* Solaris/OmniOS */

typedef int acl_type_t;

#define ACL_TYPE_NONE    0
#define ACL_TYPE_POSIX   1
#define ACL_TYPE_NFS4    4

#define ACL_TYPE_ACCESS  ACL_TYPE_POSIX


typedef union gace {
  aclent_t posix;
  ace_t    nfs;
} GACE;

typedef struct gacl_info {
  acl_type_t type;
  int nev;
  int sev;
  GACE ev[];
} *acl_t;

extern acl_t
acl_init(int count);

extern void
acl_free(void *vp);

extern int
acl_set_file(const char *path,
	     acl_type_t type,
	     acl_t ap);

extern acl_t
acl_get_file(const char *path,
	     acl_type_t type);

extern char *
acl_to_text(acl_t ap,
	    ssize_t *len);
	    
#endif
#endif

extern int
acl_compare(acl_t a,
	    acl_t b);

#endif
