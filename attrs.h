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

#if defined(__FreeBSD__)
#include <sys/extattr.h>
#endif

#if defined(__APPLE__)
#include <sys/xattr.h>
#endif


#if defined(__APPLE__)
#define EXTATTR_NAMESPACE_SYSTEM 0
#define extattr_get_fd(fd, ns, name, data, size)     fgetxattr(fd, name, data, size, 0, 0)
#define extattr_get_file(path, ns, name, data, size) getxattr(path, name, data, size, 0, 0)
#define extattr_get_link(path, ns, name, data, size) getxattr(path, name, data, size, 0, XATTR_NOFOLLOW)

#define extattr_set_fd(fd, ns, name, data, size)     fsetxattr(fd, name, data, size, 0, 0)
#define extattr_set_file(path, ns, name, data, size) setxattr(path, name, data, size, 0, 0)
#define extattr_set_link(path, ns, name, data, size) setxattr(path, name, data, size, 0, XATTR_NOFOLLOW)

#define extattr_delete_fd(fd, ns, name)              fremovexattr(fd, name, 0)
#define extattr_delete_file(path, ns, name)          removexattr(path, name, 0)
#define extattr_delete_link(path, ns, name)          removexattr(path, name, XATTR_NOFOLLOW)

/* Beware that buf is not formatted in the same way on FreeBSD vs the rest */
#define extattr_list_fd(fd, ns, buf, size)	     flistxattr(fd, (char *) buf, size, 0)
#define extattr_list_file(fd, ns, buf, size) 	     listxattr(fd, (char *) buf, size, 0)
#define extattr_list_link(fd, ns, buf, size)	     listxattr(fd, (char *) buf, size, XATTR_NOFOLLOW)

#endif
#endif
