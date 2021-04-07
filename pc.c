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
#include <sys/extattr.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "btree.h"
#include "digest.h"



typedef struct nodeinfo {
  char *p;
  struct stat s;
  char *l;
  struct {
    acl_t nfs; /* NFSv4 / ZFS */
    acl_t acc; /* POSIX */
    acl_t def; /* POSIX */
  } a;
  struct {
    BTREE *sys;
    BTREE *usr;
  } x;
  struct {
    unsigned char buf[DIGEST_BUFSIZE_MAX];
    size_t len;
  } d;
} NODEINFO;


char *argv0 = "pc";
char *version = "0.1";

int f_verbose = 0;
int f_debug   = 0;
int f_update  = 1;
int f_force   = 0;
int f_recurse = 0;
int f_remove  = 0;
int f_content = 1;
int f_zero    = 0;
int f_perms   = 0;
int f_times   = 0;
int f_acls    = 0;
int f_attrs   = 0;
int f_digest  = 0;


char *
strdupcat(const char *str,
	  ...) {
  va_list ap;
  char *retval, *res, *cp;
  size_t reslen = strlen(str)+1;
  
  va_start(ap, str);
  while ((cp = va_arg(ap, char *)) != NULL)
    reslen += strlen(cp);
  va_end(ap);
  
  retval = res = malloc(reslen);
  if (!res)
    return NULL;

  strcpy(res, str);
  while (*res)
    ++res;
  
  va_start(ap, str);
  while ((cp = va_arg(ap, char *)) != NULL) {
    strcpy(res, cp);
    while (*res)
      ++res;
  }
  va_end(ap);

  return retval;
}

typedef struct attrinfo {
  size_t len;
  unsigned char buf[];
} ATTRINFO;

typedef struct attrupdate {
  int fd;
  int ns;
  const char *pn;
} ATTRUPDATE;

int
attr_update(const char *key,
	    void *vp,
	    void *xp) {
  ATTRINFO   *aip = (ATTRINFO *) vp;
  ATTRUPDATE *aup = (ATTRUPDATE *) xp;

  if (aup->pn)
    return extattr_set_link(aup->pn, aup->ns, key, aip->buf, aip->len);

  return extattr_set_fd(aup->fd, aup->ns, key, aip->buf, aip->len);
}

int
file_digest(NODEINFO *nip) {
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


static inline int
buffer_zero_check(char *buf,
	   size_t len) {
  while (len > 0 && *buf++ == 0)
    --len;

  return (len == 0 ? 1 : 0);
}


int
file_copy(NODEINFO *src_nip,
	  const char *dstpath) {
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

  
  if (S_ISREG(src_nip->s.st_mode)) {
    src_fd = open(src_nip->p, O_RDONLY);
    if (src_fd < 0) {
      fprintf(stderr, "open(%s, O_RDONLY): %s\n", src_nip->p, strerror(errno));
      rc = -1;
      goto End;
    }
    
    dst_fd = open(dstpath, O_WRONLY|O_CREAT|O_TRUNC, src_nip->s.st_mode);
    if (dst_fd < 0) {
      fprintf(stderr, "open(%s, O_WRONLY|O_CREAT): %s\n", dstpath, strerror(errno));
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
      fprintf(stderr, "%s: Error: aio_read(): %s\n", argv0, strerror(errno));
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
	  fprintf(stderr, "%s: Error: lseek(%s, ..., %ld): %s\n", argv0, dstpath, sbytes, strerror(errno));
	  rc = -1;
	  goto End;
	}
      }
      else {
	rc = write(dst_fd, (const void *) cbp->aio_buf, sbytes);
	if (rc < 0) {
	  fprintf(stderr, "%s: Error: write(%s, ..., %ld): %s\n", argv0, dstpath, sbytes, strerror(errno));
	  rc = -1;
	  goto End;
	}
      }

      tbytes += sbytes;
      if (f_verbose > 1)
	printf("  %ld bytes copied\r", tbytes);
      
      cbp = NULL;
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
      fprintf(stderr, "sendfile(%s,%s): %s\n", src_nip->p, dstpath, strerror(errno));
      rc = -1;
      goto End;
    }
#else
    while ((sbytes = read(src_fd, buf, sizeof(buf))) > 0) {
      if (f_zero && buffer_zero_check(buf, sbytes)) {
        holed = 1;
	rc = lseek(dst_fd, sbytes, SEEK_CUR);
	if (rc < 0) {
	  fprintf(stderr, "%s: Error: lseek(%s, ..., %ld): %s\n", argv0, dstpath, sbytes, strerror(errno));
	  rc = -1;
	  goto End;
	}
      }
      else {
	rc = write(dst_fd, buf, sbytes);
	if (rc < 0) {
	  fprintf(stderr, "%s: Error: write(%s, ..., %ld): %s\n", argv0, dstpath, sbytes, strerror(errno));
	  rc = -1;
	  goto End;
	}
      }
      tbytes += sbytes;
      if (f_verbose > 1)
	printf("  %ld bytes copied\r", tbytes);
    }
    rc = sbytes;
#endif
    if (holed) {
      /* Must write atleast one byte at the end of the file */
      char z = 0;
      
      if (lseek(dst_fd, -1, SEEK_CUR) < 0) {
	fprintf(stderr, "lseek(%s, -1, SEEK_CUR): %s\n", dstpath, strerror(errno));
	rc = -1;
	goto End;
      }
      if (write(dst_fd, &z, 1) < 0) {
	fprintf(stderr, "write(%s, NUL, 1): %s\n", dstpath, strerror(errno));
	rc = -1;
	goto End;
      }
    }
    if (rc < 0) {
      fprintf(stderr, "write(%s): %s\n", dstpath, strerror(errno));
      rc = -1;
      goto End;
    }
    
    tbytes += sbytes;
    if (f_verbose > 1)
      printf("  %ld bytes copied\n", tbytes);
  }
  
 End:
  if (dst_fd >= 0)
    close(dst_fd);
  if (src_fd >= 0)
    close(src_fd);
  
  return rc;
}


int
node_update(NODEINFO *src_nip,
	    const char *dstpath) {
  int rc = 0;
  
  if (f_perms) {
    if (f_perms > 1) {
      if (src_nip->s.st_uid == getuid() || geteuid() == 0) {
	if (lchown(dstpath, src_nip->s.st_uid, src_nip->s.st_gid) < 0 && errno != EPERM) {
	  fprintf(stderr, "%s: Error: %s: lchown: %s\n",
		  argv0, dstpath, strerror(errno));
	  rc = -1;
	}
      }
    }
    
    /* Must be done after lchown() in case it clears setuid/setgid bits */
    if (lchmod(dstpath, src_nip->s.st_mode) < 0) {
      fprintf(stderr, "%s: Error: %s: lchmod: %s\n",
	      argv0, dstpath, strerror(errno));
      rc = -1;
    }
  }
  
  if (f_attrs) {
    ATTRUPDATE aub;
    
    aub.fd = -1;
    aub.pn = dstpath;
    
    if (src_nip->x.sys) {
      aub.ns = EXTATTR_NAMESPACE_SYSTEM;
      btree_foreach(src_nip->x.sys, attr_update, &aub);
    }
    
    if (src_nip->x.usr) {
      aub.ns = EXTATTR_NAMESPACE_USER;
      btree_foreach(src_nip->x.usr, attr_update, &aub);
    }
  }
  
  if (f_acls) {
    if (src_nip->a.nfs) {
      if (acl_set_link_np(dstpath, ACL_TYPE_NFS4, src_nip->a.nfs) < 0) {
	fprintf(stderr, "%s: Error: %s: acl_set_link_np(ACL_TYPE_NFS4): %s\n",
		argv0, dstpath, strerror(errno));
	rc = -1;
      }
    }
    if (src_nip->a.acc) {
      if (acl_set_link_np(dstpath, ACL_TYPE_ACCESS, src_nip->a.acc) < 0) {
	fprintf(stderr, "%s: Error: %s: acl_set_link_np(ACL_TYPE_ACCESS): %s\n",
		argv0, dstpath, strerror(errno));
	rc = -1;
      }
    }
    if (src_nip->a.def) {
      if (acl_set_link_np(dstpath, ACL_TYPE_DEFAULT, src_nip->a.def) < 0) {
	fprintf(stderr, "%s: Error: %s: acl_set_link_np(ACL_TYPE_DEFAULT): %s\n",
		argv0, dstpath, strerror(errno));
	rc = -1;
      }
    }
  }
  
  if (f_times > 1) {
    struct timespec times[2];
    
    times[0] = src_nip->s.st_atim;
    times[1] = src_nip->s.st_mtim;
    if (utimensat(AT_FDCWD, dstpath, times, AT_SYMLINK_NOFOLLOW) < 0) {
      fprintf(stderr, "%s: Error: utimensat(%s): %s\n",
	      argv0, dstpath, strerror(errno));
      rc = -1;
    }
  }
  
  return rc;
}



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

    
    len = *ap++;
    an = malloc(len+1);
    if (!an) {
      fprintf(stderr, "%s: Error: %s: malloc(%ld): %s\n",
	      argv0, objpath, len, strerror(errno));
      exit(1);
    }
    memcpy(an, ap, len);
    an[len] = '\0';
    ap += len;

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


NODEINFO *
nodeinfo_alloc(void) {
  NODEINFO *nip = malloc(sizeof(*nip));

  if (!nip)
    return NULL;

  memset(nip, 0, sizeof(*nip));
  return nip;
}


void
nodeinfo_free(void *vp) {
  NODEINFO *nip = (NODEINFO *) vp;

  if (!nip)
    return;

  if (nip->p)
    free(nip->p);
  
  if (nip->l)
    free(nip->l);

  if (nip->a.nfs)
    acl_free(nip->a.nfs);
  if (nip->a.acc)
    acl_free(nip->a.acc);
  if (nip->a.def)
    acl_free(nip->a.def);

  if (nip->x.sys)
    btree_destroy(nip->x.sys);
  if (nip->x.usr)
    btree_destroy(nip->x.usr);

  memset(nip, 0, sizeof(*nip));
  free(nip);
}

int
nodeinfo_get(NODEINFO *nip,
	     const char *path,
	     const char *file) {
  if (path) {
    if (file)
      nip->p = strdupcat(path, "/", file, NULL);
    else
      nip->p = strdup(path);
  }
  
  if (!nip->p) {
    errno = EINVAL;
    return -1;
  }

  if (nip->l)
    free(nip->l);
  nip->l = NULL;
  
  if (nip->a.nfs)
    acl_free(nip->a.nfs);
  nip->a.nfs = NULL;
  
  if (nip->a.acc)
    acl_free(nip->a.acc);
  nip->a.acc = NULL;
  
  if (nip->a.def)
    acl_free(nip->a.def); 
  nip->a.def = NULL;

  if (nip->x.sys)
    btree_destroy(nip->x.sys);
  nip->x.sys = NULL;

  if (nip->x.usr)
    btree_destroy(nip->x.usr);
  nip->x.usr = NULL;
  
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
      nip->a.nfs = acl_get_link_np(nip->p, ACL_TYPE_NFS4);
      nip->a.acc = acl_get_link_np(nip->p, ACL_TYPE_ACCESS);
      nip->a.def = acl_get_link_np(nip->p, ACL_TYPE_DEFAULT);
    } else {
      nip->a.nfs = acl_get_file(nip->p, ACL_TYPE_NFS4);
      nip->a.acc = acl_get_file(nip->p, ACL_TYPE_ACCESS);
      nip->a.def = acl_get_file(nip->p, ACL_TYPE_DEFAULT);
    }
  }
  
  if (f_attrs) {
    nip->x.sys = attrs_get(nip->p, S_ISLNK(nip->s.st_mode), EXTATTR_NAMESPACE_SYSTEM);
    nip->x.usr = attrs_get(nip->p, S_ISLNK(nip->s.st_mode), EXTATTR_NAMESPACE_USER);
  }

  if (f_digest && S_ISREG(nip->s.st_mode))
    file_digest(nip);
  
  return 0;
}

  
BTREE *
dir_load(const char *path) {
  BTREE *bp = btree_create(NULL, nodeinfo_free);
  DIR *dp;
  struct dirent *dep;
  

  if (!path)
    return bp;
  
  dp = opendir(path);
  if (!dp) {
    NODEINFO *nip = nodeinfo_alloc();

    if (nodeinfo_get(nip, path, NULL) < 0) {
      nodeinfo_free(nip);
      return NULL;
    }
    
    btree_insert(bp, strdup(path), nip);
    return bp;
  }

  while ((dep = readdir(dp)) != NULL) {
    if (strcmp(dep->d_name, ".") != 0 &&
	strcmp(dep->d_name, "..") != 0) {
      NODEINFO *nip = nodeinfo_alloc();

      if (nodeinfo_get(nip, path, dep->d_name) < 0) {
	fprintf(stderr, "%s: Error: %s/%s: %s\n", argv0, path, dep->d_name, strerror(errno));
	nodeinfo_free(nip);
	exit(1);
      }
      
      btree_insert(bp, strdup(dep->d_name), (void *) nip);
    }
  }

  closedir(dp);
  return bp;
}



char *
mode2str(NODEINFO *nip) {
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

int attr_print(const char *key,
	       void *val,
	       void *extra) {
  ATTRINFO *aip = (ATTRINFO *) val;

  printf("      %s = %s\n", key, aip->buf);
  return 0;
}


int node_print(const char *key,
	       void *val,
	       void *extra) {
  NODEINFO *nip = (NODEINFO *) val;
  int verbose = 0;

  if (extra)
    verbose = * (int *) extra;
  
  printf("%s%s", key, (nip && S_ISDIR(nip->s.st_mode)) ? "/" : "");
  
  if (S_ISLNK(nip->s.st_mode))
    printf(" -> %s", nip->l);
    
  printf(" [%s", mode2str(nip));

  if (nip->a.nfs || nip->a.acc || nip->a.def) {
    if (nip->a.nfs)
      putchar('N');
    if (nip->a.acc)
      putchar('A');
    if (nip->a.def)
      putchar('D');
    if (nip->x.sys && btree_entries(nip->x.sys) > 0)
      putchar('S');
    if (nip->x.usr && btree_entries(nip->x.usr) > 0)
      putchar('U');
  }
  putchar(']');
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
    
    if (nip->a.nfs) {
      puts("    NFSv4/ZFS ACL:");
      char *t = acl_to_text(nip->a.nfs, NULL);
      if (t)
	fputs(t, stdout);
      acl_free(t);
    }
    
    if (nip->a.acc) {
      puts("    POSIX Access ACL:");
      char *t = acl_to_text(nip->a.acc, NULL);
      if (t)
	fputs(t, stdout);
      acl_free(t);
    }
    if (nip->a.def) {
      puts("    POSIX Default ACL:");
      char *t = acl_to_text(nip->a.def, NULL);
      if (t)
	fputs(t, stdout);
      acl_free(t);
    }
    if (nip->x.sys && btree_entries(nip->x.sys) > 0) {
      puts("    System Attributes:");
      btree_foreach(nip->x.sys, attr_print, NULL);
    }
    if (nip->x.usr && btree_entries(nip->x.usr) > 0) {
      puts("    User Attributes:");
      btree_foreach(nip->x.usr, attr_print, NULL);
    }

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
dir_print(BTREE *bp) {
  return btree_foreach(bp, node_print, &f_verbose);
}



typedef struct xdata {
  const char *srcpath;
  const char *dstpath;
  BTREE *src;
  BTREE *dst;
} XDATA;


int
dir_compare(const char *srcpath,
	    const char *dstpath);


int
dir_recurse(const char *srcpath,
	    const char *dstpath,
	    const char *key) {
  char *nsrc, *ndst;
  int rc;
  
  nsrc = strdupcat(srcpath, "/", key, NULL);
  ndst = strdupcat(dstpath, "/", key, NULL);
  rc = dir_compare(nsrc, ndst);
  free(ndst);
  free(nsrc);
  return rc;
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
  
  /* Hack, do a better comparision */
  
  as = acl_to_text(a, NULL);
  bs = acl_to_text(b, NULL);
  
  if (strcmp(as, bs) != 0)
    rc = 1;
  
  acl_free(as);
  acl_free(bs);

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
  int rc = btree_foreach(a, attr_compare_handler, (void *) b);

  if (rc)
    printf("ATTRS differs: %d\n", rc);
  
  return rc;
}


/* XXX Fixme: Add support for fifos & sockets */
int
nodeinfo_compare(NODEINFO *a,
		 NODEINFO *b) {
  int d = 0;


  if (!a && !b)
    return 0;
  
  if ((a && !b) || (!a && b))
    return -1;


  /* Check file type mode bits */
  if ((a->s.st_mode & S_IFMT) != (b->s.st_mode & S_IFMT))
    d |= 0x00000001;


  if (f_perms) {
    /* Check file ownership */
    if (a->s.st_uid != b->s.st_uid) {
      if (a->s.st_uid == getuid() || geteuid() == 0)
	d |= 0x00000002;
    }
    
    if (a->s.st_gid != b->s.st_gid) {
      if (geteuid() == 0) /* XXX: TODO: Also allow if a->s.st_gid is in users own groups */
	d |= 0x00000004;
    }
  }


  /* Check symbolic link content */
  if (S_ISLNK(a->s.st_mode) && S_ISLNK(b->s.st_mode)) {
    if ((a->l && !b->l) || (!a->l && b->l) || (a->l && b->l && strcmp(a->l, b->l) != 0))
      d |= 0x00000010;
  }

  if (S_ISBLK(a->s.st_mode) || S_ISCHR(a->s.st_mode)) {
    if (a->s.st_dev != b->s.st_dev)
      d |= 0x00000020;
  }

  if (f_times) {
    if (f_times <= 2 && a->s.st_mtime > b->s.st_mtime)
      /* Check file modification times - newer */
      d |= 0x00001000;
    else if (f_times > 2 && a->s.st_mtime != b->s.st_mtime)
      /* Check file modification times - exact */      
      d |= 0x00001000;
  }
  
  /* Check file size (if regular file) */
  if (f_content) {
    if (S_ISREG(a->s.st_mode) && S_ISREG(b->s.st_mode)) {
      if (a->s.st_size != b->s.st_size)
	d |= 0x00000100;
    }
    
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
    if (a->a.nfs && acl_compare(a->a.nfs, b->a.nfs))
      d |= 0x00100000;
    if (a->a.acc && acl_compare(a->a.acc, b->a.acc))
      d |= 0x00200000;
    if (a->a.def && acl_compare(a->a.def, b->a.def))
      d |= 0x00400000;
  }

  if (f_attrs) {
    /* Check Extended Attributes */
    if (a->x.sys && attr_compare(a->x.sys, b->x.sys))
      d |= 0x01000000;
    if (a->x.usr && attr_compare(a->x.usr, b->x.usr))
      d |= 0x02000000;
  }

  return d;
}


int
check_new_or_updated(const char *key,
		     void *val,
		     void *extra) {
  NODEINFO *src_nip = (NODEINFO *) val;
  NODEINFO *dst_nip = NULL;
  XDATA *xd = (XDATA *) extra;
  char *dstpath = strdupcat(xd->dstpath, "/", key, NULL);

  if (xd->dst)
    btree_search(xd->dst, key, (void **) &dst_nip);

  
  if (!dst_nip) {
    /* New file or dir */

    if (f_verbose) {
      printf("+ %s/", xd->dstpath);
      node_print(key, src_nip, &f_verbose);
    }

    if (f_update) {
      if (S_ISREG(src_nip->s.st_mode)) {
	/* Regular file */
	if (f_content)
	  file_copy(src_nip, dstpath);
	
      } else if (S_ISDIR(src_nip->s.st_mode)) {
	/* Directory */
	if (mkdir(dstpath, src_nip->s.st_mode) < 0) {
	  fprintf(stderr, "%s: Error: %s: mkdir: %s\n",
		  argv0, dstpath, strerror(errno));
	  exit(1);
	}
      } else if (S_ISLNK(src_nip->s.st_mode)) {
	if (symlink(src_nip->l, dstpath) < 0) {
	  fprintf(stderr, "%s: Error: %s: symlink: %s\n",
		  argv0, dstpath, strerror(errno));
	  exit(1);
	}
      } else if (S_ISBLK(src_nip->s.st_mode) || S_ISCHR(src_nip->s.st_mode)) {
	if (mknod(dstpath, src_nip->s.st_mode, src_nip->s.st_dev) < 0) {
	  fprintf(stderr, "%s: Error: %s: mknod: %s\n",
		  argv0, dstpath, strerror(errno));
	  exit(1);
	}
      } else if (S_ISFIFO(src_nip->s.st_mode)) {
	if (mkfifo(dstpath, src_nip->s.st_mode) < 0) {
	  fprintf(stderr, "%s: Error: %s: mkfifo: %s\n",
		  argv0, dstpath, strerror(errno));
	  exit(1);
	}
      } else if (S_ISSOCK(src_nip->s.st_mode)) {
	struct sockaddr_un su;
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	
	if (fd < 0) {
	  fprintf(stderr, "%s: Error: socket(AF_UNIX): %s\n",
		  argv0, strerror(errno));
	  exit(1);
	}

	memset(&su, 0, sizeof(su));
	su.sun_family = AF_UNIX;
	strncpy(su.sun_path, dstpath, sizeof(su.sun_path)-1);
	if (bind(fd, (struct sockaddr *) &su, sizeof(su)) < 0) {
	  fprintf(stderr, "%s: Error: %s: bind(AF_UNIX): %s\n",
		  argv0, dstpath, strerror(errno));
	  exit(1);
	}
	close(fd);
      }
      
    }
    
    if (f_recurse && S_ISDIR(src_nip->s.st_mode)) {
      dir_recurse(xd->srcpath, xd->dstpath, key);
    }

    /* Update after subdirectory has been traversed to preserve timestamps */
    if (f_update)
      node_update(src_nip, dstpath);
  
  } else if ((src_nip->s.st_mode & S_IFMT) != (dst_nip->s.st_mode & S_IFMT)) {
    /* Node changed type */
    
    if (S_ISDIR(src_nip->s.st_mode) && !S_ISDIR(dst_nip->s.st_mode)) {
      /* Changed from non-dir -> dir */

      if (f_verbose) {
	printf("- %s/", xd->dstpath);
	node_print(key, dst_nip, NULL);
	
	printf("+ %s/", xd->dstpath);
	node_print(key, src_nip, &f_verbose);
      }

      if (f_update) {
	if (unlink(dstpath) < 0) {
	  fprintf(stderr, "%s: Error: %s: unlink: %s\n",
		  argv0, dstpath, strerror(errno));
	  exit(1);
	}
	
	if (mkdir(dstpath, src_nip->s.st_mode) < 0) {
	  fprintf(stderr, "%s: Error: %s: mkdir: %s\n",
		  argv0, dstpath, strerror(errno));
	  exit(1);
	}

	/* Refresh dst nodeinfo */
	nodeinfo_get(dst_nip, NULL, NULL);
      }

      if (f_recurse)
	dir_recurse(xd->srcpath, xd->dstpath, key);
      
      /* Update after subdirectory has been traversed to preserve timestamps */
      if (f_update)
	node_update(src_nip, dstpath);
      
    } else if (!S_ISDIR(src_nip->s.st_mode) && S_ISDIR(dst_nip->s.st_mode)) {
      /* Changed from dir -> non-dir */
      if (f_recurse)
	dir_recurse(xd->srcpath, xd->dstpath, key);

      if (f_verbose) {
	printf("- %s/", xd->dstpath);
	node_print(key, dst_nip, NULL);
	printf("+ %s/", xd->dstpath);
	node_print(key, src_nip, &f_verbose);
      }

      if (f_update) {
	if (rmdir(dstpath) < 0) {
	  fprintf(stderr, "%s: Error: %s: rmdir: %s\n",
		  argv0, dstpath, strerror(errno));
	  exit(1);
	}
	
	if (S_ISREG(src_nip->s.st_mode)) {
	  if (f_content)
	    file_copy(src_nip, dstpath);
	} else if (S_ISLNK(src_nip->s.st_mode)) {
	  if (symlink(src_nip->l, dstpath) < 0) {
	    fprintf(stderr, "%s: Error: %s: symlink: %s\n",
		    argv0, dstpath, strerror(errno));
	    exit(1);
	  }
	} else if (S_ISBLK(src_nip->s.st_mode) || S_ISCHR(src_nip->s.st_mode)) {
	  if (mknod(dstpath, src_nip->s.st_mode, src_nip->s.st_dev) < 0) {
	    fprintf(stderr, "%s: Error: %s: mknod: %s\n",
		    argv0, dstpath, strerror(errno));
	    exit(1);
	  }
	} else if (S_ISFIFO(src_nip->s.st_mode)) {
	  if (mkfifo(dstpath, src_nip->s.st_mode) < 0) {
	    fprintf(stderr, "%s: Error: %s: mkfifo: %s\n",
		    argv0, dstpath, strerror(errno));
	    exit(1);
	  }
	} else if (S_ISSOCK(src_nip->s.st_mode)) {
	  struct sockaddr_un su;
	  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	  
	  if (fd < 0) {
	    fprintf(stderr, "%s: Error: socket(AF_UNIX): %s\n",
		    argv0, strerror(errno));
	    exit(1);
	  }
	  
	  memset(&su, 0, sizeof(su));
	  su.sun_family = AF_UNIX;
	  strncpy(su.sun_path, dstpath, sizeof(su.sun_path)-1);
	  if (bind(fd, (struct sockaddr *) &su, sizeof(su)) < 0) {
	    fprintf(stderr, "%s: Error: %s: bind(AF_UNIX): %s\n",
		    argv0, dstpath, strerror(errno));
	    exit(1);
	  }
	  close(fd);
	}
	
	node_update(src_nip, dstpath);

	/* Refresh dst nodeinfo */
	nodeinfo_get(dst_nip, NULL, NULL);
      }
      
    } else {
      /* Changed from non-dir to non-dir */
      
      if (f_verbose) {
	printf("* %s/", xd->dstpath);
	node_print(key, src_nip, &f_verbose);
      }
      
      if (f_update) {
	if (unlink(dstpath) < 0) {
	  fprintf(stderr, "%s: Error: %s: unlink: %s\n",
		  argv0, dstpath, strerror(errno));
	  exit(1);
	}
	
	if (S_ISREG(src_nip->s.st_mode)) {
	  if (f_content)
	    file_copy(src_nip, dstpath);
	} else if (S_ISLNK(src_nip->s.st_mode)) {
	  if (symlink(src_nip->l, dstpath) < 0) {
	    fprintf(stderr, "%s: Error: %s: symlink: %s\n",
		    argv0, dstpath, strerror(errno));
	    exit(1);
	  }
	} else if (S_ISBLK(src_nip->s.st_mode) || S_ISCHR(src_nip->s.st_mode)) {
	  if (mknod(dstpath, src_nip->s.st_mode, src_nip->s.st_dev) < 0) {
	    fprintf(stderr, "%s: Error: %s: symlink: %s\n",
		    argv0, dstpath, strerror(errno));
	    exit(1);
	  }
	} else if (S_ISFIFO(src_nip->s.st_mode)) {
	  if (mkfifo(dstpath, src_nip->s.st_mode) < 0) {
	    fprintf(stderr, "%s: Error: %s: mkfifo: %s\n",
		    argv0, dstpath, strerror(errno));
	    exit(1);
	  }
	} else if (S_ISSOCK(src_nip->s.st_mode)) {
	  struct sockaddr_un su;
	  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	  
	  if (fd < 0) {
	    fprintf(stderr, "%s: Error: socket(AF_UNIX): %s\n",
		    argv0, strerror(errno));
	    exit(1);
	  }
	  
	  memset(&su, 0, sizeof(su));
	  su.sun_family = AF_UNIX;
	  strncpy(su.sun_path, dstpath, sizeof(su.sun_path)-1);
	  if (bind(fd, (struct sockaddr *) &su, sizeof(su)) < 0) {
	    fprintf(stderr, "%s: Error: %s: bind(AF_UNIX): %s\n",
		    argv0, dstpath, strerror(errno));
	    exit(1);
	  }
	  close(fd);
	}
	
	node_update(src_nip, dstpath);
	
	/* Refresh dst nodeinfo */
	nodeinfo_get(dst_nip, NULL, NULL);
      }
    }
  } else {
    /* Object is same type */
    int d;
    
    if (f_recurse && S_ISDIR(dst_nip->s.st_mode))
      dir_recurse(xd->srcpath, xd->dstpath, key);

    d = 0;
    if (f_force || (d = nodeinfo_compare(src_nip, dst_nip)) != 0) {
      if (f_verbose) {
	printf("! %s/", xd->dstpath);
	node_print(key, src_nip, &f_verbose);

	if (f_debug)
	  fprintf(stderr, "nodeinfo_compare(%s, %s) -> %08x\n", src_nip->p, dst_nip ? dst_nip->p : "<null>", d);
      }
      
      /* Something is different */
      if (f_update) {
	
	if (S_ISREG(src_nip->s.st_mode) &&
	    f_content &&
	    (f_force || d < 0 || (d & 0x000fff00))) { /* Force, Not Found, Digest, Mtime, Size */
	  /* Regular file */
	  int rc;

	  rc = file_copy(src_nip, dstpath);
	  if (f_debug)
	    fprintf(stderr, "file_copy(%s, %s) -> %d\n", src_nip->p, dstpath, rc);
	  
	} else if (S_ISLNK(src_nip->s.st_mode) &&
		   (f_force || d < 0 || (d & 0x000000f0))) { /* Force, Not Found, Content */
	  if (unlink(dstpath) < 0) {
	    fprintf(stderr, "%s: Error: %s: unlink: %s\n",
		    argv0, dstpath, strerror(errno));
	    exit(1);
	  }
	  if (symlink(src_nip->l, dstpath) < 0) {
	    fprintf(stderr, "%s: Error: %s: symlink: %s\n",
		    argv0, dstpath, strerror(errno));
	    exit(1);
	  }
	} else if (S_ISBLK(src_nip->s.st_mode) || S_ISCHR(src_nip->s.st_mode)) {
	  if (unlink(dstpath) < 0) {
	    fprintf(stderr, "%s: Error: %s: unlink: %s\n",
		    argv0, dstpath, strerror(errno));
	    exit(1);
	  }
	  if (mknod(dstpath, src_nip->s.st_mode, src_nip->s.st_dev) < 0) {
	    fprintf(stderr, "%s: Error: %s: symlink: %s\n",
		    argv0, dstpath, strerror(errno));
	    exit(1);
	  }
	} /* else do nothing special for fifos or sockets */
	
	node_update(src_nip, dstpath);
	
	/* Refresh dst nodeinfo */
	nodeinfo_get(dst_nip, NULL, NULL);
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
  NODEINFO *dst_nip = (NODEINFO *) val;
  NODEINFO *src_nip = NULL;
  XDATA *xd = (XDATA *) extra;
  
  btree_search(xd->src, key, (void **) &src_nip);
  
  if (!src_nip) {
    /* Object not found in source */
    
    if (f_recurse && S_ISDIR(dst_nip->s.st_mode))
      dir_recurse(xd->srcpath, xd->dstpath, key);

    if (f_verbose) {
      printf("- %s/", xd->dstpath);
      node_print(key, dst_nip, NULL);
    }

    if (f_update) {
      char *dstpath = strdupcat(xd->dstpath, "/", key, NULL);

      if (S_ISDIR(dst_nip->s.st_mode))
	rmdir(dstpath);
      else
	unlink(dstpath);
      
      free(dstpath);
    }
  }
  return 0;
}

int
dir_compare(const char *srcpath,
	    const char *dstpath) {
  XDATA dat;
  

  dat.srcpath = srcpath;
  dat.dstpath = dstpath;
  
  dat.src = dir_load(srcpath);
  dat.dst = dir_load(dstpath);

  btree_foreach(dat.src, check_new_or_updated, &dat);

  if (f_remove)
    btree_foreach(dat.dst, check_removed, &dat);

  btree_destroy(dat.src);
  btree_destroy(dat.dst);
  return 0;
}
 
int
main(int argc,
     char *argv[]) {
  int i, j, k;
  char *ds;
  

  for (i = 1; i < argc && argv[i][0] == '-'; i++) {
    if (!argv[i][1]) {
      ++i;
      break;
    }
    
    for (j = 1; argv[i][j]; j++)
      switch (argv[i][j]) {
      case 'v':
	++f_verbose;
	break;

      case 'd':
	++f_debug;
	break;

      case 'n':
	f_update = 0;
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

      case 'm':
	f_remove = 1;
      case 'a':
	f_recurse = 1;
	f_acls    = 1;
	f_attrs   = 1;
	f_perms   = 2;
	f_times   = 3; /* Exact mtime comparision */
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
	
      case '-':
	++i;
	goto EndArg;

      case 'h':
	printf("Usage:\n");
	printf("  %s [<options>] <src> <dst>\n", argv0);
	printf("\nOptions:\n");
	printf("  -h             Display this information\n");
	printf("  -v             Increase verbosity\n");
	printf("  -d             Increase debugging level\n");
	printf("  -n             No-update mode\n");
	printf("  -f             Force-copy mode\n");
	printf("  -r             Recurse into subdirectories\n");
	printf("  -p             Check and preserve mode bits\n");
	printf("  -pp            Check and preserve mode bits and owner/group\n");
	printf("  -t             Check if source mtime is newer\n");
	printf("  -tt            Check if source mtime is newer and reset on target\n");
	printf("  -ttt           Check if source mtime differs and reset on target\n");
	printf("  -x             Remove/replace deleted/changed objects\n");
	printf("  -u             Do not update file contents\n");
	printf("  -z             Generate zero-holed files\n");
	printf("  -A             Enable ACLs\n");
	printf("  -X             Enable Extended Attributes\n");
	printf("  -a             Archive mode (equal to -rpptttAX)\n");
	printf("  -m             Mirror mode (equal to -ax)\n");
	printf("  -D <digest>    Select file digest algorithm\n");
	printf("\nDigests:\n  ");
	for (k = DIGEST_TYPE_NONE; k <= DIGEST_TYPE_SHA512; k++)
	  printf("%s%s", (k != DIGEST_TYPE_NONE ? ", " : ""), digest_type2str(k));
	putchar('\n');
	puts("\nUsage:");
	puts("  Options may be specified multiple times (-vv). A single '-' ends option");
	puts("  parsing. If no Digest is selected then only ctime, mtime & file size will");
	puts("  be used to detect file content changes.");
	printf("\nVersion:\n  %s\n", version);
	printf("\nAuthor:\n");
	puts("  Peter Eriksson <pen@lysator.liu.se>");
	exit(0);
	
      default:
	fprintf(stderr, "%s: Error: -%c: Invalid switch\n",
		argv0, argv[i][j]);
	exit(1);
      }
  NextArg:;
  }
  
 EndArg:
  argv0 = argv[0];

  if (i+2 != argc) {
    fprintf(stderr, "%s: Error: Missing required arguments: <src> <dst>\n",
	    argv0);
    exit(1);
  }
  
  return dir_compare(argv[i], argv[i+1]);
}


