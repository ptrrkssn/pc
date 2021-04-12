/*
 * pc.c
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

#define USE_AIO      0
#define USE_SENDFILE 0

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#if USE_AIO
#include <aio.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/acl.h>
#include <sys/param.h>
#include <sys/vnode.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "btree.h"
#include "digest.h"
#include "attrs.h"



/* 
 * Node information
 */
typedef struct node {
  char *p;		/* Path to node */
  struct stat s;	/* Stat info */
  char *l;		/* Symbolic link content */
  struct {
#ifdef ACL_TYPE_NFS4
    acl_t nfs;		/* NFSv4 / ZFS */
#endif
#ifdef ACL_TYPE_ACCESS
    acl_t acc;		/* POSIX */
#endif
#ifdef ACL_TYPE_DEFAULT
    acl_t def;		/* POSIX */
#endif
  } a;
  struct {
#ifdef EXTATTR_NAMESPACE_SYSTEM
    BTREE *sys;		/* System Extended Attributes */
#endif
#ifdef EXTATTR_NAMESPACE_USER
    BTREE *usr;		/* User Extended Attributes */
#endif
  } x;
  struct {		/* Content Digest */
    unsigned char buf[DIGEST_BUFSIZE_MAX];
    size_t len;
  } d;
} NODE;


/*
 * Directory contents
 */
typedef struct dirnode {
  char *path;		/* Path to directory */
  BTREE *nodes;		/* Nodes in directory */
} DIRNODE;


/*
 * Directory pairs
 */
typedef struct dirpair {
  DIRNODE *src;
  DIRNODE *dst;
} DIRPAIR;


/* 
 * Extended Attributes 
 */
typedef struct attrinfo {
  size_t len;
  unsigned char buf[];
} ATTRINFO;

typedef struct attrupdate {
  int fd;
  int ns;
  const char *pn;
  BTREE *attrs;
} ATTRUPDATE;




char *argv0 = "pc";
char *version = "0.3";

int f_verbose = 0;
int f_debug   = 0;
int f_update  = 1;
int f_force   = 0;
int f_ignore  = 0;
int f_recurse = 0;
int f_remove  = 0;
int f_content = 1;
int f_zero    = 0;
int f_perms   = 0;
int f_owner   = 0;
int f_times   = 0;
int f_acls    = 0;
int f_attrs   = 0;
int f_flags   = 0; /* Check file flags, special UF_ARCHIVE handling if > 1 */
int f_digest  = 0; /* Generate and check a content digest for files */
size_t f_bufsize = 128*1024;

gid_t gidsetv[NGROUPS_MAX+1];
int gidsetlen = 0;


static inline int
in_gidset(gid_t g) {
  int i;

  for (i = 0; i < gidsetlen; i++)
    if (gidsetv[i] == g)
      return 1;

  return 0;
}

/* 
 * Concatenate the list of strings (and at first NULL) into a dynamically allocated buffer
 */
char *
strdupcat(const char *str,
	  ...) {
  va_list ap;
  char *retval, *res;
  const char *cp;
  size_t reslen;

  /* Get the length of the first string, plus 1 for ending NUL */
  reslen = strlen(str)+1;

  /* Add length of the other strings */
  va_start(ap, str);
  while ((cp = va_arg(ap, char *)) != NULL)
    reslen += strlen(cp);
  va_end(ap);

  /* Allocate storage */
  retval = res = malloc(reslen);
  if (!retval)
    return NULL;

  /* Get the first string */
  cp = str;
  while (*cp)
    *res++ = *cp++;
  
  /* And then append the rest */
  va_start(ap, str);
  while ((cp = va_arg(ap, char *)) != NULL) {
    while (*cp)
      *res++ = *cp++;
  }
  va_end(ap);

  /* NUL-terminate the string */
  *res = '\0';
  return retval;
}



/*
 * Compare two timespec structures
 * Returns: -1 if a < b, 1 if a > b, and 0 if equal
 */
int
timespec_compare(struct timespec *a,
		 struct timespec *b) {
  if (a->tv_sec == b->tv_sec) {
    if (a->tv_nsec < b->tv_nsec)
      return -1;
    if (a->tv_nsec > b->tv_nsec)
      return 1;
    return 0;
  }

  if (a->tv_sec < b->tv_sec)
    return -1;
  if (a->tv_sec > b->tv_sec)
    return 1;

  return 0;
}





int
acl_compare(acl_t a,
	    acl_t b) {
  char *as, *bs;
  int rc = 0;

  
  if (!a && b)
    return -1;

  if (a && !b)
    return -1;
  
  /* Hack, do a better/faster comparision */
  as = acl_to_text(a, NULL);
  bs = acl_to_text(b, NULL);
  
  if (strcmp(as, bs) != 0)
    rc = 1;
  
  acl_free(as);
  acl_free(bs);

  return rc;
  
}



/*
 * Calculate a digest checksum for a file
 */
int
file_digest(NODE *nip) {
  unsigned char buf[128*1024];
  ssize_t len;
  int fd;
  DIGEST d;
  

  if (!nip)
    return 0;
  
  fd = open(nip->p, O_RDONLY);
  if (fd < 0)
    return -1;
  
  digest_init(&d, f_digest);
  while ((len = read(fd, buf, sizeof(buf))) > 0) {
    digest_update(&d, buf, len);
  }
  close(fd);
  nip->d.len = digest_final(&d, nip->d.buf, sizeof(nip->d.buf));
  return 0;
}


/*
 * Check if a buffer contains just NUL (0x00) bytes
 */
static inline int
buffer_zero_check(char *buf,
	   size_t len) {
  while (len > 0 && *buf++ == 0)
    --len;

  return (len == 0 ? 1 : 0);
}


/*
 * Copy file contents
 */
int
file_copy(const char *srcpath,
	  const char *dstpath,
	  mode_t mode) {
  off_t sbytes, tbytes;
  int src_fd = -1, dst_fd = -1, rc = -1;
  int holed = 0;
#if USE_AIO
  char bufv[2][128*1024];
  struct aiocb cb[2], *cbp;
  int ap;
#else
  char buf[128*1024];
#endif

  
  src_fd = open(srcpath, O_RDONLY);
  if (src_fd < 0) {
    fprintf(stderr, "%s: Error: %s: open(O_RDONLY): %s\n",
	    argv0, srcpath, strerror(errno));
    rc = -1;
    goto End;
  }
  
  dst_fd = open(dstpath, O_WRONLY|O_CREAT|O_TRUNC, mode);
  if (dst_fd < 0) {
    fprintf(stderr, "%s: Error: %s: open(O_WRONLY|O_CREAT, 0x%x): %s\n",
	    argv0, dstpath, mode, strerror(errno));
    rc = -1;
    goto End;
  }
  
  sbytes = 0;
  tbytes = 0;
#if USE_AIO
  /* Start first read */
  memset(&cb, 0, sizeof(cb));
  cb[0].aio_fildes = src_fd;
  cb[1].aio_fildes = src_fd;
  cb[0].aio_buf = bufv[0];
  cb[1].aio_buf = bufv[1];
  cb[0].aio_nbytes = sizeof(bufv[ap]);
  cb[1].aio_nbytes = sizeof(bufv[ap]);
  
  ap = 0;
  cb[ap].aio_offset = 0;
  if (aio_read(&cb[ap]) < 0) {
    fprintf(stderr, "%s: Error: %s: aio_read(): %s\n", argv0, srcpath, strerror(errno));
    exit(1);
  }
  
  cbp = NULL;
  while ((rc = aio_waitcomplete(&cbp, NULL)) > 0) {
    sbytes = rc;
    
    /* Start next read immediately in order to keep interleave reading & writing */
    ap = !ap;
    cb[ap].aio_offset = tbytes+sbytes;
    if (aio_read(&cb[ap]) < 0) {
      fprintf(stderr, "%s: Error: aio_read(): %s\n", argv0, strerror(errno));
      exit(1);
    }
    
    if (f_zero && sbytes && buffer_zero_check((char *) cbp->aio_buf, sbytes)) {
      holed = 1;
      rc = lseek(dst_fd, sbytes, SEEK_CUR);
      if (rc < 0) {
	fprintf(stderr, "%s: Error: %s: lseek(%ld): %s\n", argv0, dstpath, sbytes, strerror(errno));
	rc = -1;
	goto End;
      }
    }
    else {
      rc = write(dst_fd, (const void *) cbp->aio_buf, sbytes);
      if (rc < 0) {
	fprintf(stderr, "%s: Error: %s: write(%ld): %s\n", argv0, dstpath, sbytes, strerror(errno));
	rc = -1;
	goto End;
      }
    }
    
    tbytes += sbytes;
    if (f_verbose > 1)
      printf("  %ld bytes copied\r", tbytes);
    
    cbp = NULL;
  }
  if (rc < 0) {
    fprintf(stderr, "%s: Error: %s: aio_waitcomplete: %s\n", srcpath, strerror(errno));
    rc = -1;
    goto End;
  }
#elif USE_SENDFILE
  while ((rc = sendfile(src_fd, dst_fd, tbytes, SENDFILE_BUFSIZE, NULL, &sbytes, SF_NOCACHE)) > 0 ||
	 errno == EAGAIN) {
    tbytes += sbytes;
    if (f_verbose > 1)
      printf("  %ld bytes copied\r", tbytes);
    sbytes = 0;
  }
  if (rc < 0) {
    fprintf(stderr, "%s: Error: %s -> %s: sendfile: %s\n", srcpath, dstpath, strerror(errno));
    rc = -1;
    goto End;
  }
#else
  while ((rc = read(src_fd, buf, sizeof(buf))) > 0) {
    sbytes = rc;
    if (f_zero && buffer_zero_check(buf, sbytes)) {
      holed = 1;
      rc = lseek(dst_fd, sbytes, SEEK_CUR);
      if (rc < 0) {
	fprintf(stderr, "%s: Error: %s: lseek(%lld): %s\n",
		argv0, dstpath, (long long) sbytes, strerror(errno));
	rc = -1;
	goto End;
      }
    }
    else {
      rc = write(dst_fd, buf, sbytes);
      if (rc < 0) {
	fprintf(stderr, "%s: Error: %s: write(%lld): %s\n",
		argv0, dstpath, (long long) sbytes, strerror(errno));
	rc = -1;
	goto End;
      }
    }
    tbytes += sbytes;
    if (f_verbose > 1)
      printf("  %lld bytes copied\r", (long long) tbytes);
  }
  if (rc < 0) {
    fprintf(stderr, "%s: Error: %s: read(): %s\n",
	    argv0, srcpath, strerror(errno));
    rc = -1;
    goto End;
  }

  rc = sbytes;
#endif
  
  if (holed) {
    /* Must write atleast one byte at the end of the file */
    char z = 0;
    
    if (lseek(dst_fd, -1, SEEK_CUR) < 0) {
      fprintf(stderr, "%s: Error: %s: lseek(-1, SEEK_CUR): %s\n",
	      argv0, dstpath, strerror(errno));
      rc = -1;
      goto End;
    }
    if (write(dst_fd, &z, 1) < 0) {
      fprintf(stderr, "%s: Error: %s: write(NUL, 1): %s\n",
	      argv0, dstpath, strerror(errno));
      rc = -1;
      goto End;
    }
  }
  
  tbytes += sbytes;
  if (f_verbose > 1)
    printf("  %lld bytes copied\n", (long long) tbytes);
  
 End:
  if (dst_fd >= 0)
    close(dst_fd);
  if (src_fd >= 0)
    close(src_fd);
  
  return rc;
}



/*
 * Update Extended Attributes
 */
int
attr_update(const char *key,
	    void *vp,
	    void *xp) {
  ATTRINFO   *aip = (ATTRINFO *) vp;
  ATTRINFO   *bip = NULL;
  ATTRUPDATE *aup = (ATTRUPDATE *) xp;

  
  /* Does the attribute exist in the destination node? */
  if (aup->attrs && !f_force) {
    btree_search(aup->attrs, key, (void **) &bip);
    if (bip && bip->len == aip->len && memcmp(bip->buf, aip->buf, bip->len) == 0)
      return 0; /* Already exists, and is identical */
  }
  
  if (aup->pn)
    return extattr_set_link(aup->pn, aup->ns, key, aip->buf, aip->len);

  return extattr_set_fd(aup->fd, aup->ns, key, aip->buf, aip->len);
}

int
attr_remove(const char *key,
	    void *vp,
	    void *xp) {
  ATTRINFO   *aip = NULL;
  ATTRUPDATE *aup = (ATTRUPDATE *) xp;

  if (f_debug)
    fprintf(stderr, "attr_remove(%s)\n", key);
	    
  /* Does the attribute exist in the source node? */
  if (aup->attrs) {
    btree_search(aup->attrs, key, (void **) &aip);
    if (aip) {
      if (f_debug)
	fprintf(stderr, "attr_remove(%s): found in source, not deleting\n", key);
      return 0;
    }
  }
  
  /* Nope - so delete it */
  if (aup->pn)
    return extattr_delete_link(aup->pn, aup->ns, key);
  
  return extattr_delete_fd(aup->fd, aup->ns, key);
}



/*
 * Update node metadata 
 */
int
node_update(NODE *src_nip,
	    NODE *dst_nip,
	    const char *dstpath) {
  int rc = 0;

  
  if (f_owner) {
    if (!dst_nip ||
	src_nip->s.st_uid != dst_nip->s.st_uid ||
	src_nip->s.st_gid != dst_nip->s.st_gid) {
      if (src_nip->s.st_uid == getuid() || geteuid() == 0) {
	if (lchown(dstpath, src_nip->s.st_uid, src_nip->s.st_gid) < 0 && errno != EPERM) {
	  fprintf(stderr, "%s: Error: %s: lchown: %s\n",
		  argv0, dstpath, strerror(errno));
	  rc = -1;
	  if (!f_ignore)
	    return rc;
	}
      }
    }
  }

  if (f_perms) {
    /* Must be done after lchown() in case it clears setuid/setgid bits */
    if (!dst_nip || src_nip->s.st_mode != dst_nip->s.st_mode) {
      if (lchmod(dstpath, src_nip->s.st_mode) < 0) {
	fprintf(stderr, "%s: Error: %s: lchmod: %s\n",
		argv0, dstpath, strerror(errno));
	rc = -1;
	if (!f_ignore)
	  return rc;
      }
    }
  }

  if (f_attrs) {
    ATTRUPDATE aub;

    aub.fd = -1;
    aub.pn = dstpath;

#ifdef EXTATTR_NAMESPACE_SYSTEM
    if (src_nip->x.sys) {
      aub.ns = EXTATTR_NAMESPACE_SYSTEM;
      aub.attrs = dst_nip->x.sys;
      btree_foreach(src_nip->x.sys, attr_update, &aub);
      if (f_remove && src_nip->x.sys) {
	aub.attrs = src_nip->x.sys;
	btree_foreach(dst_nip->x.sys, attr_remove, &aub);
      }
    }
#endif
#ifdef EXTATTR_NAMESPACE_USER
    if (src_nip->x.usr) {
      aub.ns = EXTATTR_NAMESPACE_USER;
      aub.attrs = dst_nip->x.usr;
      btree_foreach(src_nip->x.usr, attr_update, &aub);
      if (f_remove && dst_nip->x.usr) {
	aub.attrs = src_nip->x.usr;
	btree_foreach(dst_nip->x.usr, attr_remove, &aub);
      }
    }
#endif
  }
  
  if (f_acls) {
#ifdef ACL_TYPE_NFS4
    if (src_nip->a.nfs) {
      if (!dst_nip->a.nfs || acl_compare(src_nip->a.nfs, dst_nip->a.nfs) != 0) {
	if (acl_set_link_np(dstpath, ACL_TYPE_NFS4, src_nip->a.nfs) < 0) {
	  fprintf(stderr, "%s: Error: %s: acl_set_link_np(ACL_TYPE_NFS4): %s\n",
		  argv0, dstpath, strerror(errno));
	  rc = -1;
	  if (!f_ignore)
	    return rc;
	}
      }
    }
#endif
#ifdef ACL_TYPE_ACCESS
    if (src_nip->a.acc) {
      if (!dst_nip->a.acc || acl_compare(src_nip->a.acc, dst_nip->a.acc) != 0) {
	if (acl_set_link_np(dstpath, ACL_TYPE_ACCESS, src_nip->a.acc) < 0) {
	  fprintf(stderr, "%s: Error: %s: acl_set_link_np(ACL_TYPE_ACCESS): %s\n",
		  argv0, dstpath, strerror(errno));
	  rc = -1;
	  if (!f_ignore)
	    return rc;
	}
      }
    }
#endif
#ifdef ACL_TYPE_DEFAULT
    if (src_nip->a.def) {
      if (!dst_nip->a.def || acl_compare(src_nip->a.def, dst_nip->a.def) != 0) {
	if (acl_set_link_np(dstpath, ACL_TYPE_DEFAULT, src_nip->a.def) < 0) {
	  fprintf(stderr, "%s: Error: %s: acl_set_link_np(ACL_TYPE_DEFAULT): %s\n",
		  argv0, dstpath, strerror(errno));
	  rc = -1;
	  if (!f_ignore)
	    return rc;
	}
      }
    }
#endif
  }
  
  if (f_times > 1) {
    if (!dst_nip
#ifdef __FreeBSD__
	|| timespec_compare(&src_nip->s.st_mtim, &dst_nip->s.st_mtim) 
	|| timespec_compare(&src_nip->s.st_atim, &dst_nip->s.st_atim)) {
      struct timespec times[2];
      
      times[0] = src_nip->s.st_atim;
      times[1] = src_nip->s.st_mtim;
      rc = utimensat(AT_FDCWD, dstpath, times, AT_SYMLINK_NOFOLLOW);
      if (rc < 0) {
	fprintf(stderr, "%s: Error: utimensat(%s): %s\n",
		argv0, dstpath, strerror(errno));
#else
#ifdef __APPLE__
	|| timespec_compare(&src_nip->s.st_mtimespec, &dst_nip->s.st_mtimespec) 
        || timespec_compare(&src_nip->s.st_atimespec, &dst_nip->s.st_atimespec)) {
#else
	|| difftime(src_nip->s.st_mtime, dst_nip->s.st_mtime)
        || difftime(src_nip->s.st_mtime, dst_nip->s.st_mtime)) {
#endif
      struct timeval times[2];
      
      times[0].tv_sec = src_nip->s.st_atime;
      times[1].tv_sec = src_nip->s.st_mtime;
#ifdef __APPLE__
      times[0].tv_usec = src_nip->s.st_atimespec.st_nsec/1000;
      times[1].tv_usec = src_nip->s.st_mtimespec.st_nsec/1000;
#else
      times[0].tv_usec = 0;
      times[1].tv_usec = 0;
#endif
      if (S_ISLNK(dst_nip->s.st_flags))
	rc = lutimes(dstpath, times);
      else
	rc = utimes(dstpath, times);
      if (rc < 0) {
	fprintf(stderr, "%s: Error: utimens(%s): %s\n",
		argv0, dstpath, strerror(errno));
#endif
	return f_ignore ? 0 : rc;
      }
    }
  }
  
#ifdef UF_ARCHIVE
  if (f_flags) {
    if (!dst_nip || (src_nip->s.st_flags & ~UF_ARCHIVE) != (dst_nip->s.st_flags & ~UF_ARCHIVE)) {
      rc = lchflags(dstpath, src_nip->s.st_flags);
      if (rc < 0) {
	fprintf(stderr, "%s: Error: %s: lchflags: %s\n",
		argv0, dstpath, strerror(errno));
	return f_ignore ? 0 : rc;
      }
    }
    
    if (f_flags > 1 && (src_nip->s.st_flags & UF_ARCHIVE)) {
      rc = lchflags(src_nip->p, src_nip->s.st_flags & ~UF_ARCHIVE);
      if (rc < 0) {
	fprintf(stderr, "%s: Error: %s: lchflags: %s\n",
		argv0, src_nip->p, strerror(errno));
	return f_ignore ? 0 : rc;
      }
    }
  }
#endif

  return rc;
}



/*
 * Get all Extended Attributes from a node
 */
BTREE *
attrs_get(const char *objpath,
	  int is_link,
	  int attrnamespace) {
  ssize_t avsize;
  unsigned char *alist, *end, *ap;
  size_t len;
  BTREE *bp;

  if (is_link)
    avsize = extattr_list_link(objpath, attrnamespace, NULL, 0);
  else
    avsize = extattr_list_file(objpath, attrnamespace, NULL, 0);
  if (avsize < 0) {
    return NULL;
  }
  
  alist = malloc(avsize);
  if (!alist) {
    fprintf(stderr, "%s: Error: %s: malloc(%ld): %s\n",
	    argv0, objpath, avsize, strerror(errno));
    exit(1);
  }

  if (is_link)
    avsize = extattr_list_link(objpath, attrnamespace, alist, avsize);
  else
    avsize = extattr_list_file(objpath, attrnamespace, alist, avsize);
  
  if (avsize < 0) {
    fprintf(stderr, "%s: Error: %s: extattr_list_file: %s\n",
	    argv0, objpath, strerror(errno));
    exit(1);
  }

  bp = btree_create(NULL, NULL);
  end = alist+avsize;
  ap = alist;
  
  while (ap < end) {
    char *an;
    ATTRINFO *ad;
    ssize_t adsize;

#ifdef __FreeBSD__
    len = *ap++; /* 1 byte = length */
#else
    len = strlen((char *) ap); /* NUL-terminated strings */
#endif
    an = malloc(len+1);
    if (!an) {
      fprintf(stderr, "%s: Error: %s: malloc(%ld): %s\n",
	      argv0, objpath, len, strerror(errno));
      exit(1);
    }
    memcpy(an, ap, len);
    an[len] = '\0';
    ap += len;
#ifndef __FreeBSD__
    ++ap;
#endif

    if (is_link)
      adsize = extattr_get_link(objpath, attrnamespace, an, NULL, 0);
    else
      adsize = extattr_get_file(objpath, attrnamespace, an, NULL, 0);
    
    if (adsize < 0) {
      fprintf(stderr, "%s: Error: %s: extattr_get_file(%s): %s\n",
	      argv0, objpath, an, strerror(errno));
      exit(1);
    }
    
    ad = malloc(sizeof(ATTRINFO) + adsize);
    if (!ad) {
      fprintf(stderr, "%s: Error: %s: malloc(%ld): %s\n",
	      argv0, objpath, sizeof(ATTRINFO)+adsize, strerror(errno));
      exit(1);
    }

    if (is_link)
      adsize = extattr_get_link(objpath, attrnamespace, an, ad->buf, adsize);
    else
      adsize = extattr_get_file(objpath, attrnamespace, an, ad->buf, adsize);
    
    if (adsize < 0) {
      fprintf(stderr, "%s: Error: %s: extattr_get_file(%s): %s\n",
	      argv0, objpath, an, strerror(errno));
      exit(1);
    }

    ad->len = adsize;
    btree_insert(bp, an, (void *) ad);
  }

  return bp;
}


/*
 * Allocate an empty node
 */
NODE *
node_alloc(void) {
  NODE *nip = malloc(sizeof(*nip));

  if (!nip)
    return NULL;

  memset(nip, 0, sizeof(*nip));
  return nip;
}


/*
 * Free node data
 */
void
node_free(void *vp) {
  NODE *nip = (NODE *) vp;

  if (!nip)
    return;

  if (nip->p)
    free(nip->p);
  
  if (nip->l)
    free(nip->l);

#ifdef ACL_TYPE_NFS4
  if (nip->a.nfs)
    acl_free(nip->a.nfs);
#endif
#ifdef ACL_TYPE_ACCESS
  if (nip->a.acc)
    acl_free(nip->a.acc);
#endif
#ifdef ACL_TYPE_DEFAULT
  if (nip->a.def)
    acl_free(nip->a.def);
#endif

#ifdef EXTATTR_NAMESPACE_SYSTEM
  if (nip->x.sys)
    btree_destroy(nip->x.sys);
#endif
#ifdef EXTATTR_NAMESPACE_USER
  if (nip->x.usr)
    btree_destroy(nip->x.usr);
#endif

  memset(nip, 0, sizeof(*nip));
  free(nip);
}


/*
 * Get node metadata
 */
int
node_get(NODE *nip,
	 const char *path) {
  if (f_debug)
    fprintf(stderr, "*** node_get(%s, %s)\n",
	    nip->p ? nip->p : "<null>",
	    path ? path : "<null>");
  
  if (path) {
    if (nip->p)
      free(nip->p);
    nip->p = strdup(path);
  }

  if (!nip->p) {
    errno = EINVAL;
    return -1;
  }

  if (nip->l)
    free(nip->l);
  nip->l = NULL;

#ifdef ACL_TYPE_NFS4
  if (nip->a.nfs)
    acl_free(nip->a.nfs);
  nip->a.nfs = NULL;
#endif
#ifdef ACL_TYPE_ACCESS
  if (nip->a.acc)
    acl_free(nip->a.acc);
  nip->a.acc = NULL;
#endif
#ifdef ACL_TYPE_DEFAULT
  if (nip->a.def)
    acl_free(nip->a.def); 
  nip->a.def = NULL;
#endif

#ifdef EXTATTR_NAMESPACE_SYSTEM
  if (nip->x.sys)
    btree_destroy(nip->x.sys);
  nip->x.sys = NULL;
#endif
  
#ifdef EXTATTR_NAMESPACE_USER
  if (nip->x.usr)
    btree_destroy(nip->x.usr);
  nip->x.usr = NULL;
#endif
  
  nip->d.len = 0;
  

  if (lstat(nip->p, &nip->s) < 0)
    return -1;
  
  if (S_ISLNK(nip->s.st_mode)) {
    char buf[1024];
    ssize_t len;
    
    len = readlink(nip->p, buf, sizeof(buf)-1);

    if (len < 0) {
      if (f_verbose)
	fprintf(stderr, "%s: Error: %s: readlink: %s\n",
		argv0, nip->p, strerror(errno));
      return -1;
    } else {
      nip->l = strndup(buf, len);
      if (!nip->l)
	abort(); /* XXX: Better error handling */
    }
  }
  
  if (f_acls) {
    if (S_ISLNK(nip->s.st_mode)) {
#ifdef ACL_TYPE_NFS4
      nip->a.nfs = acl_get_link_np(nip->p, ACL_TYPE_NFS4);
#endif
#ifdef ACL_TYPE_ACCESS
      nip->a.acc = acl_get_link_np(nip->p, ACL_TYPE_ACCESS);
#endif
#ifdef ACL_TYPE_ACCESS
      nip->a.def = acl_get_link_np(nip->p, ACL_TYPE_DEFAULT);
#endif
    } else {
#ifdef ACL_TYPE_NFS4
      nip->a.nfs = acl_get_file(nip->p, ACL_TYPE_NFS4);
#endif
#ifdef ACL_TYPE_ACCESS
      nip->a.acc = acl_get_file(nip->p, ACL_TYPE_ACCESS);
#endif
#ifdef ACL_TYPE_DEFAULT
      nip->a.def = acl_get_file(nip->p, ACL_TYPE_DEFAULT);
#endif
    }
  }
  
  if (f_attrs) {
#ifdef EXTATTR_NAMESPACE_SYSTEM
    nip->x.sys = attrs_get(nip->p, S_ISLNK(nip->s.st_mode), EXTATTR_NAMESPACE_SYSTEM);
#endif
#ifdef EXTATTR_NAMESPACE_USER
    nip->x.usr = attrs_get(nip->p, S_ISLNK(nip->s.st_mode), EXTATTR_NAMESPACE_USER);
#endif
  }

  if (f_digest && S_ISREG(nip->s.st_mode))
    file_digest(nip);
  
  return 0;
}


/*
 * Allocate empty directory node
 */
DIRNODE *
dirnode_alloc(const char *path) {
  DIRNODE *dnp;
  

  dnp = malloc(sizeof(*dnp));
  if (!dnp)
    abort();

  memset(dnp, 0, sizeof(*dnp));

  if (path) {
    dnp->path = strdup(path);
  }
  
  dnp->nodes = btree_create(NULL, node_free);

  return dnp;
}


/*
 * Free directory node data
 */
void
dirnode_free(DIRNODE *dnp) {
  if (dnp->nodes) {
    btree_destroy(dnp->nodes);
    dnp->nodes = NULL;
  }
  if (dnp->path) {
    free((void *) dnp->path);
    dnp->path = NULL;
  }
  free(dnp);
}


/*
 * Add node or directory contents to a directory ndoe
 *
 * 1. path/dir/ 
 *    add contents of 'dir' to DIRNODE
 * 2. path/node
 *    add 'node' to DIRNODE
 */
int
dirnode_add(DIRNODE *dnp,
	    const char *path,
	    int dir_contents_f) {
  DIR *dp;
  struct dirent *dep;
  int len, rc;
  int n_trail;
  char *pbuf, *dirname, *nodename;
  

  pbuf = strdup(path);
  len = strlen(pbuf);

  /* Remove and count trailing '/' */
  n_trail = 0;
  while (len > 0 && pbuf[len-1] == '/') {
    ++n_trail;
    --len;
  }
  pbuf[len--] = '\0';

  while (len >= 0 && pbuf[len] != '/')
    --len;
  if (len < 0) {
    dirname = strdup(".");
    nodename = pbuf;
  } else {
    if (len == 0)
      len = 1;
    dirname = strndup(pbuf, len);
    nodename = pbuf+len+1;
  }
  
  if (dir_contents_f || n_trail > 0) {
    dp = opendir(pbuf);
    if (!dp)
      return -1;
    
    if (dp) {
      while ((dep = readdir(dp)) != NULL) {
	if (strcmp(dep->d_name, ".") != 0 &&
	    strcmp(dep->d_name, "..") != 0) {
	  char *tmppath = strdupcat(pbuf, "/", dep->d_name, NULL);
	  NODE *nip = node_alloc();

	  if (node_get(nip, tmppath) < 0) {
	    if (f_verbose)
	      fprintf(stderr, "%s: Error: %s: node_get: %s\n", argv0, tmppath, strerror(errno));
	    free(tmppath);
	    node_free(nip);
	    return -1;
	  }
	  
	  if (btree_insert(dnp->nodes, strdup(dep->d_name), (void *) nip) < 0) {
	    if (f_ignore && errno == EEXIST) {
	      if (f_verbose)
		fprintf(stderr, "%s: Ignoring duplicate node name\n", tmppath);
	    } else {
	      fprintf(stderr, "%s: Error: %s: btree_insert: %s\n",
		      argv0, tmppath, strerror(errno));
	      exit(1);
	    }
	  }

	  free(tmppath);
	}
      }
    }
    
    closedir(dp);
  } else {
    NODE *nip = node_alloc();

    rc = node_get(nip, path);
    if (rc < 0) {
      if (f_debug)
	fprintf(stderr, "%s: Error: %s: node_get: %s\n", argv0, path, strerror(errno));
      node_free(nip);
      return -1;
    }
    
    rc = btree_insert(dnp->nodes, strdup(nodename), (void *) nip);
    if (rc < 0) {
      if (f_ignore && errno == EEXIST) {
	if (f_verbose)
	  fprintf(stderr, "%s: Ignoring duplicate node name\n", path);
      } else {
	fprintf(stderr, "%s: Error: %s: btree_insert: %s\n",
		argv0, path, strerror(errno));
	return rc;
      }
    }
  }

  free(dirname);
  free(pbuf);
  return 0;
}


/*
 * Return a string representing the node type
 */
char *
mode2str(NODE *nip) {
  struct stat *sp;

  if (!nip)
    return "-";
  
  sp = &nip->s;
  
  if (S_ISDIR(sp->st_mode))
    return "d";

  if (S_ISREG(sp->st_mode))
    return "f";
  
  if (S_ISBLK(sp->st_mode))
    return "b";
  
  if (S_ISCHR(sp->st_mode))
    return "c";
  
  if (S_ISLNK(sp->st_mode))
    return "l";
  
  if (S_ISFIFO(sp->st_mode))
    return "p";
  
  if (S_ISSOCK(sp->st_mode))
    return "s";

#ifdef S_ISWHT
  if (S_ISWHT(sp->st_mode))
    return "w";
#endif

  return "?";
}


int
is_printable(const unsigned char *buf,
	     size_t len) {
  int i;

  for (i = 0; i < len && isprint(buf[i]); i++)
    ;
  
  return i < len ? 0 : 1;
}


int
print_hex(const unsigned char *buf,
	  size_t len) {
  int i;

  for (i = 0; i < len; i++)
    printf("%s%02x", i ? " " : "", buf[i]);

  return 0;
}


/*
 * Print Extended Attribute
 */
int attr_print(const char *key,
	       void *val,
	       void *extra) {
  ATTRINFO *aip = (ATTRINFO *) val;

  printf("      %s = ", key);
  if (is_printable(aip->buf, aip->len))
    printf("\"%s\"\n", aip->buf);
  else {
    print_hex(aip->buf, aip->len);
    puts(" [hex]");
  }
  return 0;
}


int node_print(const char *key,
	       void *val,
	       void *extra) {
  NODE *nip = (NODE *) val;
  int verbose = 0;

  if (extra)
    verbose = * (int *) extra;
  
  printf("%s%s",
	 key ? nip->p : "",
	 (nip && S_ISDIR(nip->s.st_mode)) ? "/" : "");
  
  if (S_ISLNK(nip->s.st_mode))
    printf(" -> %s", nip->l);
    
  printf(" [%s", mode2str(nip));

#ifdef ACL_TYPE_NFS4
  if (nip->a.nfs)
    putchar('N');
#endif
#ifdef ACL_TYPE_ACCESS
  if (nip->a.acc)
    putchar('A');
#endif
#ifdef ACL_TYPE_DEFAULT
  if (nip->a.def)
    putchar('D');
#endif
#ifdef EXTATTR_NAMESPACE_SYSTEM
  if (nip->x.sys && btree_entries(nip->x.sys) > 0)
    putchar('S');
#endif
#ifdef EXTATTR_NAMESPACE_USER
  if (nip->x.usr && btree_entries(nip->x.usr) > 0)
    putchar('U');
#endif

  putchar(']');

#ifdef UF_ARCHIVE
  if (nip->s.st_flags)
    printf(" {%s}", fflagstostr(nip->s.st_flags));
#endif
  putchar('\n');
  
  if (verbose > 1) {
    if (verbose > 2) {
      puts("    General:");
      printf("      Size  = %ld\n", nip->s.st_size);
      printf("      Uid   = %d\n", nip->s.st_uid);
      printf("      Gid   = %d\n", nip->s.st_gid);
      printf("      Atime = %ld\n", nip->s.st_atime);
      printf("      Ctime = %ld\n", nip->s.st_ctime);
      printf("      Mtime = %ld\n", nip->s.st_mtime);
    }
    
#ifdef ACL_TYPE_NFS4
    if (nip->a.nfs) {
      puts("    NFSv4/ZFS ACL:");
      char *t = acl_to_text(nip->a.nfs, NULL);
      if (t)
	fputs(t, stdout);
      acl_free(t);
    }
#endif
#ifdef ACL_TYPE_ACESS
    if (nip->a.acc) {
      puts("    POSIX Access ACL:");
      char *t = acl_to_text(nip->a.acc, NULL);
      if (t)
	fputs(t, stdout);
      acl_free(t);
    }
#endif
#ifdef ACL_TYPE_DEFAULT
    if (nip->a.def) {
      puts("    POSIX Default ACL:");
      char *t = acl_to_text(nip->a.def, NULL);
      if (t)
	fputs(t, stdout);
      acl_free(t);
    }
#endif
#ifdef EXTATTR_NAMESPACE_SYSTEM
    if (nip->x.sys && btree_entries(nip->x.sys) > 0) {
      puts("    System Attributes:");
      btree_foreach(nip->x.sys, attr_print, NULL);
    }
#endif
#ifdef EXTATTR_NAMESPACE_USER
    if (nip->x.usr && btree_entries(nip->x.usr) > 0) {
      puts("    User Attributes:");
      btree_foreach(nip->x.usr, attr_print, NULL);
    }
#endif
    if (nip->d.len) {
      int i;
      printf("    %s Digest:", digest_type2str(f_digest));
      for (i = 0; i < nip->d.len; i++) {
	printf("%s%02x", ((i & 15) == 0 ? "\n      " : " "), nip->d.buf[i]);
      }
      putchar('\n');
    }
  }
  
  return 0;
}



int
dirnode_print(DIRNODE *dnp) {
  printf("Directory %s:\n", dnp->path ? dnp->path : "<null>");
  return btree_foreach(dnp->nodes, node_print, &f_verbose);
}


int
dir_compare(const char *srcpath,
	    const char *dstpath);


int
dirpair_recurse(DIRPAIR *dp,
		const char *key) {
  const char *nsrc, *ndst;
  int rc;


  if (dp->src->path)
    nsrc = strdupcat(dp->src->path, "/", key, NULL);
  else
    nsrc = key;

  if (dp->dst->path)
    ndst = strdupcat(dp->dst->path, "/", key, NULL);
  else
    ndst = key;
  
  rc = dir_compare(nsrc, ndst);

  if (dp->dst->path)
    free((void *) ndst);
  if (dp->src->path)
    free((void *) nsrc);
  
  return rc;
}



int
attr_compare_handler(const char *key,
		     void *vp,
		     void *xp) {
  ATTRINFO *aip = (ATTRINFO *) vp;
  ATTRINFO *bip = NULL;
  BTREE *btp = (BTREE *) xp;

  
  btree_search(btp, key, (void **) &bip);
  if (!bip)
    return -1;
  
  if (aip->len != bip->len)
    return 1;
  
  if (memcmp(aip->buf, bip->buf, aip->len) != 0)
    return 2;

  return 0;
}


int
attr_compare(BTREE *a,
	     BTREE *b) {
  int rc;


  if (f_debug)
    fprintf(stderr, "*** attr_compare a=%p, b=%p\n", a, b);
  
  rc = btree_foreach(a, attr_compare_handler, (void *) b);
  if (rc) {
    if (f_debug)
      fprintf(stderr, "ATTRS a->b differs: %d\n", rc);
    return 1;
  }

  if (f_remove && b) {
    rc = btree_foreach(b, attr_compare_handler, (void *) a);
    if (rc) {
      if (f_debug)
	fprintf(stderr, "ATTRS b->a differs: %d\n", rc);
      return -1;
    }
  }
  
  return 0;
}


int
node_compare(NODE *a,
		 NODE *b) {
  int d = 0;


  if (!a && !b)
    return 0;
  
  if ((a && !b) || (!a && b))
    return -1;


  /* Check file type mode bits */
  if ((a->s.st_mode & S_IFMT) != (b->s.st_mode & S_IFMT))
    d |= 0x00000001;


  if (f_owner) {
    /* Check file ownership */
    if (a->s.st_uid != b->s.st_uid) {
      if (a->s.st_uid == getuid() || geteuid() == 0)
	d |= 0x00000002;
    }

    if (a->s.st_gid != b->s.st_gid) {
      if (geteuid() == 0 || in_gidset(a->s.st_gid)) /* Allow if root or one of our groups */
	d |= 0x00000004;
    }
  }


  /* Check symbolic link content */
  if (S_ISLNK(a->s.st_mode)) {
    if ((a->l && !b->l) || (!a->l && b->l) || (a->l && b->l && strcmp(a->l, b->l) != 0))
      d |= 0x00000010;
  }

  /* Check block & character devices */
  if (S_ISBLK(a->s.st_mode) || S_ISCHR(a->s.st_mode)) {
    if (a->s.st_dev != b->s.st_dev)
      d |= 0x00000020;
  }

  if (f_times) {
    if (f_times < 2) {
      if (a->s.st_mtime > b->s.st_mtime)
	/* Check file modification times - newer */
	d |= 0x00000100;
    }
    else {
      if (a->s.st_mtime != b->s.st_mtime)
	/* Check file modification times - exact */      
	d |= 0x00000100;
    }
  }
  
  if (f_content && S_ISREG(a->s.st_mode)) {
    /* Check filesize */
    if (a->s.st_size != b->s.st_size)
      d |= 0x00001000;

    /* Check digest */
    if (f_digest) {
      if (a->d.len) {
	if (a->d.len != b->d.len)
	  d |= 0x00010000;
	else if (memcmp(a->d.buf, b->d.buf, a->d.len))
	  d |= 0x00020000;
      }
    }
  }

  /* Check ACLs */
  if (f_acls) {
#ifdef ACL_TYPE_NFS4
    if (a->a.nfs && acl_compare(a->a.nfs, b->a.nfs))
      d |= 0x00100000;
#endif
#ifdef ACL_TYPE_ACCESS
    if (a->a.acc && acl_compare(a->a.acc, b->a.acc))
      d |= 0x00200000;
#endif
#ifdef ACL_TYPE_DEFAULT
    if (a->a.def && acl_compare(a->a.def, b->a.def))
      d |= 0x00400000;
#endif
  }

  if (f_attrs) {
#ifdef EXTATTR_NAMESPACE_SYSTEM
    /* Check Extended Attributes */
    if (a->x.sys && attr_compare(a->x.sys, b->x.sys))
      d |= 0x01000000;
#endif
#ifdef EXTATTR_NAMESPACE_USER
    if (a->x.usr && attr_compare(a->x.usr, b->x.usr))
      d |= 0x02000000;
#endif
  }

#ifdef UF_ARCHIVE
  if (f_flags) {
    if ((a->s.st_flags & ~UF_ARCHIVE) != (b->s.st_flags & ~UF_ARCHIVE))
      d |= 0x10000000;
    if (f_flags > 1 && a->s.st_flags & UF_ARCHIVE)
      d |= 0x20000000;
  }
#endif
  
  return d;
}


int
check_new_or_updated(const char *key,
		     void *val,
		     void *extra) {
  DIRPAIR *xd = (DIRPAIR *) extra;
  NODE *src_nip = (NODE *) val;
  NODE *dst_nip;
  int rc;
  const char *srcpath;
  char *dstpath;

  
  if (xd->dst && xd->dst->path)
    dstpath = strdupcat(xd->dst->path, "/", key, NULL);
  else
    dstpath = strdup(key);

  srcpath = src_nip->p;
  
  if (f_debug)
    fprintf(stderr, "*** check_new_or_updated: src=%s, dst=%s\n",
	    srcpath,
	    dstpath);

  dst_nip = NULL;
  if (xd->dst->nodes)
    btree_search(xd->dst->nodes, key, (void **) &dst_nip);

  if (!dst_nip) {
    /* New file or dir */

    if (f_verbose) {
      printf("+ %s", dstpath);
      node_print(NULL, src_nip, &f_verbose);
    }

    if (f_update) {
      if (S_ISREG(src_nip->s.st_mode)) {
	/* Regular file */
	if (f_content) {
	  rc = file_copy(srcpath, dstpath, src_nip->s.st_mode);
	  if (rc < 0) {
	    if (f_debug)
	      fprintf(stderr, "check_new_or_updated: file_copy(%s, %s, 0x%x) -> %d\n", srcpath, dstpath, src_nip->s.st_mode, rc);
	    return f_ignore ? 0 : rc;
	  }
	}
	  
      } else if (S_ISDIR(src_nip->s.st_mode)) {
	/* Directory */
	rc = mkdir(dstpath, src_nip->s.st_mode);
	if (rc < 0) {
	  fprintf(stderr, "%s: Error: %s: mkdir: %s\n",
		  argv0, dstpath, strerror(errno));
	  return f_ignore ? 0 : rc;
	}
      } else if (S_ISLNK(src_nip->s.st_mode)) {
	rc = symlink(src_nip->l, dstpath);
	if (rc < 0) {
	  fprintf(stderr, "%s: Error: %s: symlink: %s\n",
		  argv0, dstpath, strerror(errno));
	  return f_ignore ? 0 : -1;
	}
      } else if (S_ISBLK(src_nip->s.st_mode) || S_ISCHR(src_nip->s.st_mode)) {
	rc = mknod(dstpath, src_nip->s.st_mode, src_nip->s.st_dev);
	if (rc < 0) {
	  fprintf(stderr, "%s: Error: %s: mknod: %s\n",
		  argv0, dstpath, strerror(errno));
	  return f_ignore ? 0 : rc;
	}
      } else if (S_ISFIFO(src_nip->s.st_mode)) {
	rc = mkfifo(dstpath, src_nip->s.st_mode);
	if (rc < 0) {
	  fprintf(stderr, "%s: Error: %s: mkfifo: %s\n",
		  argv0, dstpath, strerror(errno));
	  return f_ignore ? 0 : rc;
	}
      } else if (S_ISSOCK(src_nip->s.st_mode)) {
	struct sockaddr_un su;
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	
	if (fd < 0) {
	  fprintf(stderr, "%s: Error: socket(AF_UNIX): %s\n",
		  argv0, strerror(errno));
	  return f_ignore ? 0 : -1;
	}

	memset(&su, 0, sizeof(su));
	su.sun_family = AF_UNIX;
	strncpy(su.sun_path, dstpath, sizeof(su.sun_path)-1);
	if (bind(fd, (struct sockaddr *) &su, sizeof(su)) < 0) {
	  fprintf(stderr, "%s: Error: %s: bind(AF_UNIX): %s\n",
		  argv0, dstpath, strerror(errno));
	  close(fd);
	  return f_ignore ? 0 : -1;
	}
	close(fd);
      }
    }
    
    if (f_recurse && S_ISDIR(src_nip->s.st_mode)) {
      rc = dirpair_recurse(xd, key);
      if (rc < 0) {
	if (f_debug)
	  fprintf(stderr, "check_new_or_updated: dirpair_recurse(%s, %s, %s): rc=%d\n",
		  xd->src->path,
		  xd->dst->path,
		  key,
		  rc);
	return f_ignore ? 0 : rc;
      }
    }

    /* Update after subdirectory has been traversed to preserve timestamps */
    if (f_update) {
      /* Refresh dst node */
      rc = node_update(src_nip, NULL, dstpath);
      if (rc < 0) {
	if (f_debug)
	  fprintf(stderr, "check_new_or_updated: node_update(%s, NULL, %s) [refresh]: rc=%d\n",
		  src_nip->p,
		  dstpath,
		  rc);
	return f_ignore ? 0 : rc;
      }
    }
  
  } else if ((src_nip->s.st_mode & S_IFMT) != (dst_nip->s.st_mode & S_IFMT)) {
    /* Node changed type */
    
    if (S_ISDIR(src_nip->s.st_mode) && !S_ISDIR(dst_nip->s.st_mode)) {
      /* Changed from non-dir -> dir */

      if (f_verbose) {
	printf("- %s", dstpath);
	node_print(NULL, dst_nip, NULL);
	
	printf("+ %s", dstpath);
	node_print(NULL, src_nip, &f_verbose);
      }

      if (f_update) {
	rc = unlink(dstpath);
	if (rc < 0) {
	  fprintf(stderr, "%s: Error: %s: unlink: %s\n",
		  argv0, dstpath, strerror(errno));
	  return f_ignore ? 0 : rc;
	}
	
	rc = mkdir(dstpath, src_nip->s.st_mode);
	if (rc < 0) {
	  fprintf(stderr, "%s: Error: %s: mkdir: %s\n",
		  argv0, dstpath, strerror(errno));
	  return f_ignore ? 0 : rc;
	}

	/* Refresh dst node */
	rc = node_get(dst_nip, NULL);
	if (rc < 0) {
	  if (f_debug)
	    fprintf(stderr, "check_new_or_updated: node_get(%s) [refresh]: rc=%d\n", dst_nip->p, rc);
	  return f_ignore ? 0 : rc;
	}
      }

      if (f_recurse) {
	rc = dirpair_recurse(xd, key);
	if (rc < 0) {
	  if (f_debug)
	    fprintf(stderr, "check_new_or_updated: dirpair_recurse(%s,%s,%s): rc=%d\n",
		    xd->src->path ? xd->src->path : "<null>",
		    xd->dst->path ? xd->dst->path : "<null",
		    key, rc);
	  return f_ignore ? 0 : rc;
	}
      }
      
      /* Update after subdirectory has been traversed to preserve timestamps */
      if (f_update) {
	rc = node_update(src_nip, dst_nip, dstpath);
	if (rc < 0) {
	  if (f_debug)
	    fprintf(stderr, "check_new_or_updated: node_update(%s, %s, %s): rc=%d\n",
		    src_nip->p,
		    dst_nip->p,
		    dstpath, rc);
	  return f_ignore ? 0 : rc;
	}
      }
      
    } else if (!S_ISDIR(src_nip->s.st_mode) && S_ISDIR(dst_nip->s.st_mode)) {
      /* Changed from dir -> non-dir */
      if (f_recurse) {
	rc = dirpair_recurse(xd, key);
	if (rc < 0) {
	  if (f_debug)
	    fprintf(stderr, "check_new_or_updated: dirpair_recurse(%s,%s,%s): rc=%d\n",
		    xd->src->path ? xd->src->path : "<null>",
		    xd->dst->path ? xd->dst->path : "<null",
		    key, rc);
	  return f_ignore ? 0 : rc;
	}
      }

      if (f_verbose) {
	printf("- %s", dstpath);
	node_print(NULL, dst_nip, NULL);
	printf("+ %s", dstpath);
	node_print(NULL, src_nip, &f_verbose);
      }

      if (f_update) {
	rc = rmdir(dstpath);
	if (rc < 0) {
	  fprintf(stderr, "%s: Error: %s: rmdir: %s\n",
		  argv0, dstpath, strerror(errno));
	  return f_ignore ? 0 : rc;
	}
	
	if (S_ISREG(src_nip->s.st_mode)) {
	  if (f_content) {
	    rc = file_copy(srcpath, dstpath, src_nip->s.st_mode);
	    if (rc < 0) {
	      if (f_debug)
		fprintf(stderr, "check_new_or_updated: file_copy(%s, %s, 0x%x) -> %d\n",
			srcpath, dstpath, src_nip->s.st_mode, rc);
	      return f_ignore ? 0 : rc;
	    }
	  }
	} else if (S_ISLNK(src_nip->s.st_mode)) {
	  rc = symlink(src_nip->l, dstpath);
	  if (rc < 0) {
	    fprintf(stderr, "%s: Error: %s: symlink: %s\n",
		    argv0, dstpath, strerror(errno));
	    return f_ignore ? 0 : rc;
	  }
	} else if (S_ISBLK(src_nip->s.st_mode) || S_ISCHR(src_nip->s.st_mode)) {
	  rc = mknod(dstpath, src_nip->s.st_mode, src_nip->s.st_dev);
	  if (rc < 0) {
	    fprintf(stderr, "%s: Error: %s: mknod: %s\n",
		    argv0, dstpath, strerror(errno));
	    return f_ignore ? 0 : rc;
	  }
	} else if (S_ISFIFO(src_nip->s.st_mode)) {
	  rc = mkfifo(dstpath, src_nip->s.st_mode);
	  if (rc < 0) {
	    fprintf(stderr, "%s: Error: %s: mkfifo: %s\n",
		    argv0, dstpath, strerror(errno));
	    return f_ignore ? 0 : rc;
	  }
	} else if (S_ISSOCK(src_nip->s.st_mode)) {
	  struct sockaddr_un su;
	  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	  
	  if (fd < 0) {
	    fprintf(stderr, "%s: Error: socket(AF_UNIX): %s\n",
		    argv0, strerror(errno));
	    return f_ignore ? 0 : -1;
	  }
	  
	  memset(&su, 0, sizeof(su));
	  su.sun_family = AF_UNIX;
	  strncpy(su.sun_path, dstpath, sizeof(su.sun_path)-1);
	  if (bind(fd, (struct sockaddr *) &su, sizeof(su)) < 0) {
	    fprintf(stderr, "%s: Error: %s: bind(AF_UNIX): %s\n",
		    argv0, dstpath, strerror(errno));
	    close(fd);
	    return f_ignore ? 0 : -1;
	  }
	  close(fd);
	}
	
	rc = node_update(src_nip, dst_nip, dstpath);
	if (rc < 0) {
	  if (f_debug)
	    fprintf(stderr, "check_new_or_updated: node_update(%s, %s, %s): rc=%d\n",
		    src_nip->p,
		    dst_nip->p,
		    dstpath, rc);
	  return f_ignore ? 0 : rc;
	}

	/* Refresh dst node */
	rc = node_get(dst_nip, NULL);
	if (rc < 0) {
	  if (f_debug)
	    fprintf(stderr, "check_new_or_updated: node_get(%s) [refresh]: rc=%d\n",
		    dst_nip->p, rc);
	  return f_ignore ? 0 : rc;
	}
      }
      
    } else {
      /* Changed from non-dir to non-dir */
      
      if (f_verbose) {
	printf("* %s", dstpath);
	node_print(NULL, src_nip, &f_verbose);
      }
      
      if (f_update) {
	rc = unlink(dstpath);
	if (rc < 0) {
	  fprintf(stderr, "%s: Error: %s: unlink: %s\n",
		  argv0, dstpath, strerror(errno));
	  return f_ignore ? 0 : rc;
	}
	
	if (S_ISREG(src_nip->s.st_mode)) {
	  if (f_content) {
	    rc = file_copy(srcpath, dstpath, src_nip->s.st_mode);
	    if (rc < 0) {
	      if (f_debug)
		fprintf(stderr, "check_new_or_updated: file_copy(%s, %s, 0x%x) -> %d\n",
			srcpath, dstpath, src_nip->s.st_mode, rc);
	      return f_ignore ? 0 : rc;
	    }
	  }
	  
	} else if (S_ISLNK(src_nip->s.st_mode)) {
	  rc = symlink(src_nip->l, dstpath);
	  if (rc < 0) {
	    fprintf(stderr, "%s: Error: %s: symlink: %s\n",
		    argv0, dstpath, strerror(errno));
	    return f_ignore ? 0 : rc;
	  }
	} else if (S_ISBLK(src_nip->s.st_mode) || S_ISCHR(src_nip->s.st_mode)) {
	  rc = mknod(dstpath, src_nip->s.st_mode, src_nip->s.st_dev);
	  if (rc < 0) {
	    fprintf(stderr, "%s: Error: %s: symlink: %s\n",
		    argv0, dstpath, strerror(errno));
	    return f_ignore ? 0 : rc;
	  }
	} else if (S_ISFIFO(src_nip->s.st_mode)) {
	  rc = mkfifo(dstpath, src_nip->s.st_mode);
	  if (rc < 0) {
	    fprintf(stderr, "%s: Error: %s: mkfifo: %s\n",
		    argv0, dstpath, strerror(errno));
	    return f_ignore ? 0 : rc;
	  }
	} else if (S_ISSOCK(src_nip->s.st_mode)) {
	  struct sockaddr_un su;
	  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	  
	  if (fd < 0) {
	    fprintf(stderr, "%s: Error: socket(AF_UNIX): %s\n",
		    argv0, strerror(errno));
	    return f_ignore ? 0 : -1;
	  }
	  
	  memset(&su, 0, sizeof(su));
	  su.sun_family = AF_UNIX;
	  strncpy(su.sun_path, dstpath, sizeof(su.sun_path)-1);
	  if (bind(fd, (struct sockaddr *) &su, sizeof(su)) < 0) {
	    fprintf(stderr, "%s: Error: %s: bind(AF_UNIX): %s\n",
		    argv0, dstpath, strerror(errno));
	    close(fd);
	    return f_ignore ? 0 : -1;
	  }
	  close(fd);
	}
	
	rc = node_update(src_nip, dst_nip, dstpath);
	if (rc < 0) {
	  if (f_debug)
	    fprintf(stderr, "check_new_or_updated: node_update(%s, %s, %s): rc=%d\n",
		    src_nip->p,
		    dst_nip->p,
		    dstpath, rc);
	  return f_ignore ? 0 : rc;
	}
	
	/* Refresh dst node */
	rc = node_get(dst_nip, NULL);
	if (rc < 0) {
	  if (f_debug)
	    fprintf(stderr, "check_new_or_updated: node_get(%s) [refresh] rc=%d\n",
		    dst_nip->p, rc);
	  return f_ignore ? 0 : rc;
	}
      }
    }
  } else {
    /* Object is same type */
    int d;
    
    if (f_recurse && S_ISDIR(dst_nip->s.st_mode)) {
      rc = dirpair_recurse(xd, key);
      if (rc < 0) {
	if (f_debug)
	  fprintf(stderr, "check_new_or_updated: dirpair_recurse(%s, %s, %s): rc=%d\n",
		  xd->src->path,
		  xd->dst->path,
		  key, rc);
	return f_ignore ? 0 : rc;
      }
    }
    
    d = 0;
    if (f_force || (d = node_compare(src_nip, dst_nip)) != 0) {
      /* Something is different */
      
      if (f_verbose) {
	printf("! %s", dstpath);
	node_print(NULL, src_nip, &f_verbose);
      }
      
      if (f_update) {
	if (S_ISREG(src_nip->s.st_mode) &&
	    f_content &&
	    (f_force || d < 0 || (d & 0x200fff00))) { /* Force, Not Found,  Archive, Digest, Mtime, Size */
	  /* Regular file */
	  int rc;

	  rc = file_copy(srcpath, dstpath, src_nip->s.st_mode);
	  if (rc < 0) {
	    if (f_debug)
	      fprintf(stderr, "file_copy(%s, %s, 0x%x) -> %d\n", srcpath, dstpath, src_nip->s.st_mode, rc);
	    return f_ignore ? 0 : rc;
	  }
	} else if (S_ISLNK(src_nip->s.st_mode) &&
		   (f_force || d < 0 || (d & 0x000000f0))) { /* Force, Not Found, Content */
	  rc = unlink(dstpath);
	  if (rc < 0) {
	    fprintf(stderr, "%s: Error: %s: unlink: %s\n",
		    argv0, dstpath, strerror(errno));
	    return f_ignore ? 0 : rc;
	  }
	  rc = symlink(src_nip->l, dstpath);
	  if (rc < 0) {
	    fprintf(stderr, "%s: Error: %s: symlink: %s\n",
		    argv0, dstpath, strerror(errno));
	    return f_ignore ? 0 : rc;
	  }
	} else if (S_ISBLK(src_nip->s.st_mode) || S_ISCHR(src_nip->s.st_mode)) {
	  rc = unlink(dstpath);
	  if (rc < 0) {
	    fprintf(stderr, "%s: Error: %s: unlink: %s\n",
		    argv0, dstpath, strerror(errno));
	    return f_ignore ? 0 : rc;
	  }
	  rc = mknod(dstpath, src_nip->s.st_mode, src_nip->s.st_dev);
	  if (rc < 0) {
	    fprintf(stderr, "%s: Error: %s: symlink: %s\n",
		    argv0, dstpath, strerror(errno));
	    return f_ignore ? 0 : rc;
	  }
	} /* else do nothing special for fifos or sockets */
	
	rc = node_update(src_nip, dst_nip, dstpath);
	if (rc < 0) {
	  if (f_debug)
	    fprintf(stderr, "check_new_or_updated: node_update(%s, %s, %s): rc=%d\n",
		    src_nip->p,
		    dst_nip->p,
		    dstpath, rc);
	  return f_ignore ? 0 : rc;
	}
	
	/* Refresh dst node */
	rc = node_get(dst_nip, NULL);
	if (rc < 0) {
	  if (f_debug)
	    fprintf(stderr, "check_new_or_updated: node_get(%s) [refresh] rc=%d\n",
		    dst_nip->p, rc);
	  return f_ignore ? 0 : rc;
	}
      }
    }
  }
  
  free(dstpath);
  return 0;
}


int
check_removed(const char *key,
	      void *val,
	      void *extra) {
  NODE *dst_nip = (NODE *) val;
  DIRPAIR *xd = (DIRPAIR *) extra;
  char *dstpath = dst_nip->p;
  NODE *src_nip;
  int rc = -1;
  

  src_nip = NULL;
  btree_search(xd->src->nodes, key, (void **) &src_nip);
  if (src_nip)
    return 0;
  
  /* Object not found in source */

  /* First we recurse down */
  if (f_recurse && S_ISDIR(dst_nip->s.st_mode)) {
    rc = dirpair_recurse(xd, key);
    if (rc < 0) {
      if (f_debug)
	fprintf(stderr, "check_removed: dirpair_recurse(%s, %s, %s): rc=%d\n",
		xd->src->path,
		xd->dst->path,
		key, rc);
      return f_ignore ? 0 : rc;
    }
  }
  
  if (f_verbose) {
    printf("- %s", dstpath);
    node_print(NULL, dst_nip, NULL);
  }
  
  if (f_update) {
    if (S_ISDIR(dst_nip->s.st_mode)) {
      rc = rmdir(dstpath);
      if (rc < 0) {
	fprintf(stderr, "%s: Error: %s: rmdir: %s\n",
		argv0, dstpath, strerror(errno));
	return f_ignore ? 0 : rc;
      }
    }
    else {
      rc = unlink(dstpath);
      if (rc < 0) {
	fprintf(stderr, "%s: Error: %s: unlink: %s\n",
		argv0, dstpath, strerror(errno));
	return f_ignore ? 0 : rc;
      }
    }
  }
  
  return 0;
}


int
dirnode_compare(DIRNODE *src,
		DIRNODE *dst) {
  DIRPAIR dat;
  int rc;
  

  dat.src = src;
  dat.dst = dst;

  if (f_debug)
    fprintf(stderr, "*** dirnode_compare: src=%s vs dst=%s\n",
	    src->path ? src->path : "<null>",
	    dst->path ? dst->path : "<null>");
  
  rc = btree_foreach(dat.src->nodes, check_new_or_updated, &dat);
  if (f_remove)
    btree_foreach(dat.dst->nodes, check_removed, &dat);

  return rc;
}


int
dir_compare(const char *srcpath,
	    const char *dstpath) {
  DIRNODE *src;
  DIRNODE *dst;
  int rc;
  

  src = dirnode_alloc(srcpath);
  dirnode_add(src, srcpath, 1);

  dst = dirnode_alloc(dstpath);
  dirnode_add(dst, dstpath, 1);

  rc = dirnode_compare(src, dst);
  
  dirnode_free(dst);
  dirnode_free(src);
  
  return rc;
}


int
str2size(const char **str,
	 size_t *vp) {
  size_t base = 1000, vbm, sbm, v, sv;
  int rc = 0, rv = 0, sign = 1;
  char c, i;

  
  *vp = 0;

  if (**str == '-' && isdigit((*str)[1])) {
    ++*str;
    sign = -1;
  }
  
  while (**str && isdigit(**str)) {
    c = i = 0;

    if ((*str)[0] == '0' && (*str)[1] == 'x') {
      ++*str;
      ++*str;
      
      rc = sscanf(*str, "%lx%c%c", &v, &c, &i);
      if (rc < 1)
	return -1;
      
      while (**str && isxdigit(**str))
	++*str;
      
      rv = 1;
    } else {
      rc = sscanf(*str, "%ld%c%c", &v, &c, &i);
      if (rc < 1)
	return -1;
      
      while (**str && isdigit(**str))
	++*str;
      
      rv = 1;
    }

    if (c) {
      if (i == 'i') {
	++*str;
	base = 1024;
      }
    }

    switch (toupper(c)) {
    case 'K':
      vbm = base;
      sbm = 1;
      break;
      
    case 'M':
      vbm = base*base;
      sbm = base;
      break;
      
    case 'G':
      vbm = base*base*base;
      sbm = base*base;
      break;
      
    case 'T':
      vbm = base*base*base*base;
      sbm = base*base*base;
      break;
      
    case 'P':
      vbm = base*base*base*base;
      sbm = base*base*base;
      break;
      
    default:
      c = 0;
      vbm = 1;
      sbm = 1;
      break;
    }

    v *= vbm;
    
    if (c) {
      int nd = 0;
      ++*str;
      sv = 0;
      while (isdigit(**str)) {
	sv *= 10;
	sv += **str - '0';
	++nd;
	++*str;
      }
      switch (nd) {
      case 1:
	sv *= 100;
	break;
      case 2:
	sv *= 10;
	break;
      }
		     
      v += sv*sbm;
    }
    
    *vp += v;
    
    if (**str == '+')
      ++*str;
    else
      break;
  }

  *vp *= sign;
  return rv;
}

#define OPT_NONE 0
#define OPT_INT  1
#define OPT_STR  2
#define OPT_SIZE 3

typedef struct option {
  char c;
  char *s;
  char *a;
  char *h;
  int f;
  void *v;
} OPTION;

OPTION opts[] = {
  { 'h', "help",        NULL,        "Display this information", 0, NULL },
  { 'v', "verbose",     NULL,        "Increase verbosity", 0, NULL },
  { 'd', "debug",       NULL,        "Increase debug level", 0, NULL },
  { 'n', "dry-run",     NULL,        "Do a dry-run (No updates)", 0, NULL },
  { 'f', "force",       NULL,        "Force updates", 0, NULL },
  { 'i', "ignore",      NULL,        "Ignore errors and continue", 0, NULL },
  { 'r', "recurse",     NULL,        "Recurse into subdirectories", 0, NULL },
  { 'p', "preserve",    NULL,        "Check and preserve mode bits", 0, NULL },
  { 'o', "owner",       NULL,        "Check and preserve owner & group", 0, NULL },
  { 't', "times",       NULL,        "Check mtime (and preserve mtime and atime if '-tt')", 0, NULL },
  { 'x', "expunge",     NULL,        "Remove/replace deleted/changed objects", 0, NULL },
  { 'u', "no-copy",     NULL,        "Do not copy file contents", 0, NULL },
  { 'z', "zero-fill",   NULL,        "Try to generate zero-holed files", 0, NULL },
  { 'A', "acls",        NULL,        "Copy ACLs", 0, NULL },
  { 'X', "attributes",  NULL,        "Copy extended attributes", 0, NULL },
  { 'F', "file-flags",  NULL,        "Copy file flags (and check/update 'uarch' if '-FF')", 0, NULL },
  { 'a', "archive",     NULL,        "Archive mode (equal to '-rpottAXFF')", 0, NULL },
  { 'M', "mirror",      NULL,        "Mirror mode (equal to '-ax')", 0, NULL },
  { 'B', "buffer-size", "<size>",    "Set copy buffer size", OPT_SIZE, &f_bufsize },
  { 'D', "digest",      "<digest>",  "Set file content digest algorithm", 0, NULL },
  { 0, NULL, NULL },
};

  
int
get_option(const char *s,
	   int *jp) {
  int i, len;
  char *d;

  
  if (s[0] != '-' || s[1] != '-')
    return 0;

  len = strlen(s);
  d = strchr(s, '=');

  if (d) {
    *d = '\0';
    *jp = d-s;
  } else
    *jp = len-1;

  s += 2;
  for (i = 0; opts[i].s; i++) {
    if (strcmp(opts[i].s, s) == 0)
      return opts[i].c;
  }

  return 0;
}


int
main(int argc,
     char *argv[]) {
  int i, j, k, rc;
  char *ds;
  const char *bs;
  DIRNODE *src;
  DIRNODE *dst;

  if (geteuid() != 0) {
    int rc;
    
    rc = getgroups(NGROUPS_MAX+1, &gidsetv[0]);
    if (rc < 0) {
      fprintf(stderr, "%s: Error: getgroups: %s\n", argv0, strerror(errno));
      exit(1);
    }
    
    gidsetlen = rc;
  }
  
  for (i = 1; i < argc && argv[i][0] == '-'; i++) {
    int c;
    
    if (!argv[i][1]) {
      ++i;
      break;
    }

    if (argv[i][1] == '-' && (c = get_option(argv[i], &j)) != 0)
      goto GotArg;
    
    for (j = 1; argv[i][j]; j++) {
      c = argv[i][j];

    GotArg:
      switch (c) {
      case 'v':
	if (sscanf(argv[i]+j+1, "%d", &f_verbose) == 1)
	  goto NextArg;
	else
	  ++f_verbose;
	break;

      case 'd':
	if (sscanf(argv[i]+j+1, "%d", &f_debug) == 1)
	  goto NextArg;
	else
	  ++f_debug;
	break;

      case 'n':
	f_update = 0;
	break;

      case 'i':
	++f_ignore;
	break;

      case 'f':
	++f_force;
	break;

      case 'r':
	++f_recurse;
	break;

      case 'u':
	f_content = 0;
	break;

      case 'z':
	++f_zero;
	break;

      case 'p':
	++f_perms;
	break;

      case 'o':
	++f_owner;
	break;

      case 't':
	++f_times;
	break;

      case 'x':
	++f_remove;
	break;

      case 'A':
	++f_acls;
	break;

      case 'X':
	++f_attrs;
	break;

      case 'F':
	++f_flags;
	break;

      case 'M':
	f_remove = 1;
	/* no break */
      case 'a':
	f_recurse = 1;
	f_acls    = 1;
	f_attrs   = 1;
	f_perms   = 1; /* Copy permission bits */
	f_owner   = 1; /* Copy owner/group */
	f_times   = 2; /* Exact mtime comparision */
	f_flags   = 2; /* Handle UF_ARCHIVE magic */
	break;

      case 'D':
	ds = NULL;
	if (argv[i][j+1])
	  ds = argv[i]+j+1;
	else if (argv[i+1])
	  ds = argv[++i];
	f_digest = digest_str2type(ds);
	if (f_digest < 0) {
	  fprintf(stderr, "%s: Error: %s: Invalid digest algorithm\n",
		  argv0, ds);
	  exit(1);
	}
	goto NextArg;
	
      case 'B':
	bs = NULL;
	if (argv[i][j+1])
	  bs = argv[i]+j+1;
	else if (argv[i+1])
	  bs = argv[++i];
	if (str2size(&bs, &f_bufsize) < 0) {
	  fprintf(stderr, "%s: Error: %s: Invalid buffer size\n",
		  argv0, bs);
	  exit(1);
	}
	goto NextArg;
	
      case '-':
	++i;
	goto EndArg;

      case 'h':
	printf("Usage:\n");
	printf("  %s [<options>] <src> <dst>\n", argv0);
	printf("\nOptions:\n");
	for (k = 0; opts[k].s; k++) {
	  printf("  -%c | --%s%*s%-15s%s",
		 opts[k].c,
		 opts[k].s,
		 (int) (15-strlen(opts[k].s)), "",
		 opts[k].a ? opts[k].a : "",
		 opts[k].h);
	  if (opts[k].v) {
	    putchar(' ');
	    switch (opts[k].f) {
	    case OPT_INT:
	      printf("[%d]", * (int *) opts[k].v);
	      break;
	    case OPT_SIZE:
	      printf("[%ld]", * (size_t *) opts[k].v);
	      break;
	    case OPT_STR:
	      printf("[%s]", * (char **) opts[k].v);
	      break;
	    }
	  }
	  putchar('\n');
	}
	printf("\nDigests:\n  ");
	for (k = DIGEST_TYPE_NONE; k <= DIGEST_TYPE_BEST; k++)
	  printf("%s%s", (k != DIGEST_TYPE_NONE ? ", " : ""), digest_type2str(k));
	putchar('\n');
	puts("\nUsage:");
	puts("  Options may be specified multiple times (-vv), or values may be specified");
	puts("  (-v2 or --verbose=2). A single '-' ends option parsing. If no Digest is ");
	puts("  selected then only mtime & file size will be used to detect file");
	puts("  content changes.");
	printf("\nVersion:\n  %s\n", version);
	printf("\nAuthor:\n");
	puts("  Peter Eriksson <pen@lysator.liu.se>");
	exit(0);
	
      default:
	fprintf(stderr, "%s: Error: -%c: Invalid switch\n",
		argv0, argv[i][j]);
	exit(1);
      }
    }
  NextArg:;
  }
  
 EndArg:
  argv0 = argv[0];

  if (i+2 > argc) {
    fprintf(stderr, "%s: Error: Missing required arguments: <src-1> [.. <src-N>] <dst>\n",
	    argv0);
    exit(1);
  }

  src = dirnode_alloc(NULL);
  for (j = i; j < argc-1; j++) {
    rc = dirnode_add(src, argv[j], 0);
    if (rc < 0)
      exit(1);
  }
  
  dst = dirnode_alloc(argv[argc-1]);
  
  /* XXX - Handle dst being non-dir */
  rc = dirnode_add(dst, argv[argc-1], 1);
  if (rc < 0)
    exit(1);
  
  return dirnode_compare(src, dst);
}


