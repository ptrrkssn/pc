/*
 * attrs.h
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

#ifndef ATTRS_H
#define ATTRS_H 1

#if HAVE_SYS_EXTATTR_H
#include <sys/extattr.h>
#endif

#if HAVE_SYS_XATTR_H
#include <sys/xattr.h>
#endif


typedef struct attr {
  size_t len;
  unsigned char buf[];
} ATTR;


#if defined(EXTATTR_NAMESPACE_SYSTEM)
#define ATTR_NAMESPACE_SYSTEM    EXTATTR_NAMESPACE_SYSTEM
#else
#define ATTR_NAMESPACE_SYSTEM    0
#endif

#if defined(EXTATTR_NAMESPACE_USER)
#define ATTR_NAMESPACE_USER      EXTATTR_NAMESPACE_USER
#endif

#define ATTR_FLAG_NOFOLLOW       0x0100
#define ATTR_FLAG_GETDATA        0x0200


extern ssize_t
attr_get(const char *path,
	 int ns,
	 const char *name,
	 void *data,
	 size_t size,
	 int flags);

extern ssize_t
attr_set(const char *path,
	 int ns,
	 const char *name,
	 const void *data,
	 size_t size,
	 int flags);


extern ssize_t
attr_delete(const char *path,
	    int ns,
	    const char *name,
	    int flags);

extern int
attr_foreach(const char *path,
	     int ns,
	     int flags,
	     int (*handler)(const char *path,
			    int ns,
			    const char *name,
			    int flags,
			    void *xp),
	     void *xp);

extern BTREE *
attr_list(const char *path,
	  int ns,
	  int flags);

#endif
