/*
 * attrs.c
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

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "btree.h"
#include "attrs.h"



static ATTR *
attr_alloc(size_t size) {
  ATTR *ap;

  
  ap = malloc(sizeof(*ap)+size);
  if (!ap)
    return NULL;

  ap->len = size;
  return ap;
}


#if defined(HAVE_GETXATTR)
/* Linux or MacOS */

#if defined(HAVE_LGETXATTR)
/* Linux */

ssize_t
attr_get(const char *path,
	 int ns,
	 const char *name,
	 void *data,
	 size_t size,
	 int flags) {
  if (flags & ATTR_FLAG_NOFOLLOW)
    return lgetxattr(path, name, data, size);
  else
    return getxattr(path, name, data, size);
}

ssize_t
attr_set(const char *path,
	 int ns,
	 const char *name,
	 const void *data,
	 size_t size,
	 int flags) {
  if (flags & ATTR_FLAG_NOFOLLOW)
    return lsetxattr(path, name, data, size, 0);
  else
    return setxattr(path, name, data, size, 0);
}


ssize_t
attr_delete(const char *path,
	    int ns,
	    const char *name,
	    int flags) {
  if (flags & ATTR_FLAG_NOFOLLOW)
    return lremovexattr(path, name);
  else
    return removexattr(path, name);
}



int
attr_foreach(const char *path,
	     int ns,
	     int flags,
	     int (*handler)(const char *path,
			    int ns,
			    const char *name,
			    int flags,
			    void *xp),
	     void *xp) {
  ssize_t bufsize;
  char *buf, *end, *name;
  int rc = 0;
  
  
  if (flags & ATTR_FLAG_NOFOLLOW)
    bufsize = llistxattr(path, NULL, 0);
  else
    bufsize = listxattr(path, NULL, 0);
  if (bufsize < 0)
    return -1;
    
  buf = (char *) malloc(bufsize);
  if (!buf)
    return -1;
  
  if (flags & ATTR_FLAG_NOFOLLOW)
    bufsize = llistxattr(path, buf, bufsize);
  else
    bufsize = listxattr(path, buf, bufsize);
  if (bufsize < 0) {
    free(buf);
    return -1;
  }
  
  end = buf+bufsize;
  name = buf;
  while (name < end) {
    rc = handler(path, ns, name, flags, xp);
    if (rc != 0)
      break;

    /* Locate terminating NUL */
    while (name < end && *name != '\0')
      ++name;
    ++name; /* Skip NUL */
  }

  free(buf);
  return rc;
}


#else
/* MacOS */

ssize_t
attr_get(const char *path,
	 int ns,
	 const char *name,
	 void *data,
	 size_t size,
	 int flags) {
  return getxattr(path, name, data, size, 0,
		  (flags & ATTR_FLAG_NOFOLLOW) ? XATTR_NOFOLLOW : 0);
}

ssize_t
attr_set(const char *path,
	 int ns,
	 const char *name,
	 const void *data,
	 size_t size,
	 int flags) {
  return setxattr(path, name, (void *) data, size, 0,
		  (flags & ATTR_FLAG_NOFOLLOW) ? XATTR_NOFOLLOW : 0);
}


ssize_t
attr_delete(const char *path,
	    int ns,
	    const char *name,
	    int flags) {
  return removexattr(path, name,
		     (flags & ATTR_FLAG_NOFOLLOW) ? XATTR_NOFOLLOW : 0);
}


int
attr_foreach(const char *path,
	     int ns,
	     int flags,
	     int (*handler)(const char *path,
			    int ns,
			    const char *name,
			    int flags,
			    void *xp),
	     void *xp) {
  ssize_t bufsize;
  char *buf, *end, *name;
  int rc = 0;
  

  bufsize = listxattr(path, NULL, 0,
		      (flags & ATTR_FLAG_NOFOLLOW) ? XATTR_NOFOLLOW : 0);
  if (bufsize < 0)
    return -1;
    
  buf = (char *) malloc(bufsize);
  if (!buf)
    return -1;
  
  bufsize = listxattr(path, buf, bufsize,
		      (flags & ATTR_FLAG_NOFOLLOW) ? XATTR_NOFOLLOW : 0);
  if (bufsize < 0) {
    free(buf);
    return -1;
  }
  
  end = buf+bufsize;
  name = buf;
  while (name < end) {
    rc = handler(path, ns, name, flags, xp);
    if (rc != 0)
      break;

    /* Locate terminating NUL */
    while (name < end && *name != '\0')
      ++name;
    ++name; /* Skip NUL */
  }

  free(buf);
  return rc;
}

#endif
#elif defined(HAVE_EXTATTR_GET_LINK)
/* FreeBSD */

ssize_t
attr_get(const char *path,
	  int ns,
	  const char *name,
	  void *data,
	  size_t size,
	  int flags) {
  if (flags & ATTR_FLAG_NOFOLLOW)  
    return extattr_get_link(path, ns, name, data, size);
  else
    return extattr_get_file(path, ns, name, data, size);
}


ssize_t
attr_set(const char *path,
	 int ns,
	 const char *name,
	 const void *data,
	 size_t size,
	 int flags) {
  int rc;
  
  if (flags & ATTR_FLAG_NOFOLLOW)
    rc = extattr_set_link(path, ns, name, data, size);
  else
    rc = extattr_set_file(path, ns, name, data, size);

  return rc;
}


ssize_t
attr_delete(const char *path,
	    int ns,
	    const char *name,
	    int flags) {
  if (flags & ATTR_FLAG_NOFOLLOW)
    return extattr_delete_link(path, ns, name);
  else
    return extattr_delete_file(path, ns, name);
}


int
attr_foreach(const char *path,
	     int ns,
	     int flags,
	     int (*handler)(const char *path,
			    int ns,
			    const char *name,
			    int flags,
			    void *xp),
	     void *xp) {
  ssize_t bufsize;
  char *buf, *end, *anp;
  
  
  if (flags & ATTR_FLAG_NOFOLLOW)
    bufsize = extattr_list_link(path, ns, NULL, 0);
  else
    bufsize = extattr_list_file(path, ns, NULL, 0);
  if (bufsize < 0)
    return -1;
  
  buf = (char *) malloc(bufsize);
  if (!buf)
    return -1;
  
  if (flags & ATTR_FLAG_NOFOLLOW)
    bufsize = extattr_list_link(path, ns, buf, bufsize);
  else
    bufsize = extattr_list_file(path, ns, buf, bufsize);
  if (bufsize < 0) {
    free(buf);
    return -1;
  }

  end = buf+bufsize;
  anp = buf;
  while (anp < end) {
    int namelen;
    char *name;
    int rc;
    
    namelen = * (unsigned char *) anp++;
    if (namelen == 0) {
      free(buf);
      errno = EINVAL;
      return -1;
    }

    name = strndup(anp, namelen);
    if (!name) {
      free(buf);
      return -1;
    }

    rc = handler(path, ns, name, flags, xp);
    free(name);
    if (rc != 0) {
      free(buf);
      return rc;
    }
    
    anp += namelen;
  }

  free(buf);
  return 0;
}


#elif defined(HAVE_ATTROPEN)
/* Solaris/Illumos/OmniOS */

#include <dirent.h>

ssize_t
attr_get(const char *path,
	 int ns,
	 const char *name,
	 void *data,
	 size_t size,
	 int flags) {
  int fd;
  struct stat sb;
  ssize_t len;
  

  fd = attropen(path, name, O_RDONLY|((flags & ATTR_FLAG_NOFOLLOW) ? O_NOFOLLOW : 0));
  if (fd < 0)
    return -1;
  
  if (fstat(fd, &sb) < 0) {
    close(fd);
    return -1;
  }

  if (!data) {
    close(fd);
    return sb.st_size;
  }

  len = read(fd, data, size);
  
  close(fd);
  return len;
}


ssize_t
attr_set(const char *path,
	 int ns,
	 const char *name,
	 const void *data,
	 size_t size,
	 int flags) {
  int fd;
  struct stat sb;
  ssize_t len;
  

  fd = attropen(path,
		name,
		O_WRONLY|O_CREAT|O_TRUNC|((flags & ATTR_FLAG_NOFOLLOW) ? O_NOFOLLOW : 0),
		0777);
  if (fd < 0)
    return -1;
  
  len = write(fd, data, size);
  close(fd);
  return len;
}


ssize_t
attr_delete(const char *path,
	    int ns,
	    const char *name,
	    int flags) {
  int fd, rc;

  fd = attropen(path, ".", O_RDWR|((flags & ATTR_FLAG_NOFOLLOW) ? O_NOFOLLOW : 0), 0);
  if (fd < 0)
    return -1;

  /* XXX: Handle attribute directories */
  rc = unlinkat(fd, name, 0);
  close(fd);

  return rc;
}


int
attr_foreach(const char *path,
	     int ns,
	     int flags,
	     int (*handler)(const char *path,
			    int ns,
			    const char *name,
			    int flags,
			    void *xp),
	     void *xp) {
  int dfd = -1;
  DIR *dp = NULL;
  struct dirent *dep;
  int rc = 0;

  
  dfd = attropen(path, ".", O_RDONLY|((flags & ATTR_FLAG_NOFOLLOW) ? O_NOFOLLOW : 0));
  if (dfd < 0)
    return NULL;

  dp = fdopendir(dfd);
  if (!dp) {
    close(dfd);
    return -1;
  }

  while ((dep = readdir(dp)) != NULL) {
    /* XXX: Handle attribute directories */
    rc = (*handler)(path, ns, dep->d_name, flags, xp);
    if (rc)
      break;
  }

  closedir(dp);
  return rc;
}

#else
/* No attributes support */

ssize_t
attr_get(const char *path,
	 int ns,
	 const char *name,
	 void *data,
	 size_t size,
	 int flags) {
  errno = ENOSYS;
  return -1;
}

ssize_t
attr_set(const char *path,
	 int ns,
	 const char *name,
	 const void *data,
	 size_t size,
	 int flags) {
  errno = ENOSYS;
  return -1;
}


ssize_t
attr_delete(const char *path,
	    int ns,
	    const char *name,
	    int flags) {
  errno = ENOSYS;
  return -1;
}


int
attr_foreach(const char *path,
	     int ns,
	     int flags,
	     int (*handler)(const char *path,
			    int ns,
			    const char *name,
			    int flags,
			    void *xp),
	     void *xp) {
  errno = ENOSYS;
  return NULL;
}

#endif



static int
attr_list_handler(const char *path,
		  int ns,
		  const char *name,
		  int flags,
		  void *xp) {
  BTREE *bp = (BTREE *) xp;
  ATTR *ap = NULL;


  if (flags & ATTR_FLAG_GETDATA) {
    size_t asize;

    asize = attr_get(path, ns, name, NULL, 0, flags);
    if (asize < 0)
      return asize;

    ap = attr_alloc(asize);
    if (!ap)
      return -1;
    
    asize = attr_get(path, ns, name, ap->buf, asize, flags);
    if (asize < 0) {
      free(ap);
      return -1;
    }
    
    ap->len = asize;
  }

  name = strdup(name);
  if (!name)
    return -1;

  return btree_insert(bp, name, (void *) ap);
}


BTREE *
attr_list(const char *path,
	  int ns,
	  int flags) {
  BTREE *bp;
  int rc;

  
  bp = btree_create(NULL, NULL);
  if (!bp)
    return NULL;

  rc = attr_foreach(path, ns, flags, attr_list_handler, (void *) bp);
  if (rc) {
    btree_destroy(bp);
    return NULL;
  }
    
  return bp;
}
