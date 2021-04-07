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


#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/acl.h>
#include <sys/param.h>
#include <sys/vnode.h>
#include <sys/extattr.h>
#include <sys/socket.h>


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

int f_verbose = 0;
int f_debug   = 0;
int f_update  = 1;
int f_recurse = 0;
int f_remove  = 0;
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


int
file_copy(NODEINFO *src_nip,
	  const char *dstpath) {
  off_t sbytes, tbytes;
  int src_fd = -1, dst_fd = -1, rc = -1;
  char buf[128*1024];
  

  if (S_ISREG(src_nip->s.st_mode)) {
    src_fd = open(src_nip->p, O_RDONLY);
    if (src_fd < 0) {
      fprintf(stderr, "open(%s, O_RDONLY): %s\n", src_nip->p, strerror(errno));
      rc = -1;
      goto End;
    }
    
    dst_fd = open(dstpath, O_WRONLY|O_CREAT, src_nip->s.st_mode);
    if (dst_fd < 0) {
      fprintf(stderr, "open(%s, O_WRONLY|O_CREAT): %s\n", dstpath, strerror(errno));
      rc = -1;
      goto End;
    }
    
    sbytes = 0;
    tbytes = 0;
#if 0
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
      rc = write(dst_fd, buf, sbytes);
      if (rc < 0) {
	fprintf(stderr, "%s: Error: write(%s, ..., %ld): %s\n", argv0, dstpath, sbytes, strerror(errno));
	rc = -1;
	goto End;
      }
      tbytes += sbytes;
      if (f_verbose > 1)
	printf("  %ld bytes copied\r", tbytes);
    }
    rc = sbytes;
#endif
    if (rc < 0) {
      fprintf(stderr, "write(%s): %s\n", dstpath, strerror(errno));
      rc = -1;
      goto End;
    }
    
    tbytes += sbytes;
    if (f_verbose > 1)
      printf("  %ld bytes copied\n", tbytes);
  }

  if (0) {
  if (f_perms) {
    if (fchown(dst_fd, src_nip->s.st_uid, src_nip->s.st_gid) < 0) {
      fprintf(stderr, "%s: Error: futimens(%s): %s\n",
	      argv0, dstpath, strerror(errno));
      rc = -1;
      goto End;
    }
  }
  
  if (f_attrs) {
    ATTRUPDATE aub;
    
    aub.fd = dst_fd;
    aub.pn = NULL;
    
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
      if (acl_set_fd_np(dst_fd, src_nip->a.nfs, ACL_TYPE_NFS4) < 0) {
	fprintf(stderr, "%s: Error: %s: acl_set_fd_np(ACL_TYPE_NFS4): %s\n",
		argv0, dstpath, strerror(errno));
	rc = -1;
	goto End;
      }
    }
    if (src_nip->a.acc) {
      if (acl_set_fd_np(dst_fd, src_nip->a.acc, ACL_TYPE_ACCESS) < 0) {
	fprintf(stderr, "%s: Error: %s: acl_set_fd_np(ACL_TYPE_ACCESS): %s\n",
		argv0, dstpath, strerror(errno));
	rc = -1;
	goto End;
      }
    }
    if (src_nip->a.def) {
      if (acl_set_fd_np(dst_fd, src_nip->a.def, ACL_TYPE_DEFAULT) < 0) {
	fprintf(stderr, "%s: Error: %s: acl_set_fd_np(ACL_TYPE_DEFAULT): %s\n",
		argv0, dstpath, strerror(errno));
	rc = -1;
	goto End;
      }
    }
  }
  
  if (f_times) {
    struct timespec times[2];

    times[0] = src_nip->s.st_atim;
    times[1] = src_nip->s.st_mtim;
    if (futimens(dst_fd, times) < 0) {
      fprintf(stderr, "%s: Error: futimens(%s): %s\n",
	      argv0, dstpath, strerror(errno));
      rc = -1;
      goto End;
    }
  }
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
    if (lchmod(dstpath, src_nip->s.st_mode) < 0) {
      fprintf(stderr, "%s: Error: %s: lchmod: %s\n",
	      argv0, dstpath, strerror(errno));
      rc = -1;
    }
    
    if (lchown(dstpath, src_nip->s.st_uid, src_nip->s.st_gid) < 0) {
      fprintf(stderr, "%s: Error: %s: lchown: %s\n",
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
  
  if (f_times) {
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

NODEINFO *
nodeinfo_get(const char *path,
	     const char *file) {
  NODEINFO *nip = NULL;

  
  nip = malloc(sizeof(*nip));
  if (!nip)
    abort();

  if (file)
    nip->p = strdupcat(path, "/", file, NULL);
  else
    nip->p = strdup(path);
  if (!nip->p)
    abort();
  
  nip->l = NULL;
  nip->a.nfs = NULL;
  nip->a.acc = NULL;
  nip->a.def = NULL;
  nip->x.sys = NULL;
  nip->x.usr = NULL;
  nip->d.len = 0;
  
  if (lstat(nip->p, &nip->s) < 0) {
    free(nip->p);
    free(nip);
    nip = NULL;
  } else {
    if (S_ISLNK(nip->s.st_mode)) {
      char buf[1024];
      
      if (readlink(nip->p, buf, sizeof(buf)) < 0) {
	fprintf(stderr, "%s: Error: %s: readlink: %s\n",
		argv0, nip->p, strerror(errno));
	exit(1); /* XXX: Better handling needed */
      } else {
	nip->l = strdup(buf);
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
  }

  if (f_digest) {
    file_digest(nip);
  }
  return nip;
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
    NODEINFO *nip = nodeinfo_get(path, NULL);
    
    if (nip)
      btree_insert(bp, strdup(path), nip);
    else if (errno != ENOENT) {
      fprintf(stderr, "%s: Error: %s: %s\n", argv0, path, strerror(errno));
      exit(1);
    }
    return bp;
  }

  while ((dep = readdir(dp)) != NULL) {
    if (strcmp(dep->d_name, ".") != 0 &&
	strcmp(dep->d_name, "..") != 0) {
      NODEINFO *nip = nodeinfo_get(path, dep->d_name);
      
      if (!nip) {
	fprintf(stderr, "%s: Error: %s/%s: %s\n", argv0, path, dep->d_name, strerror(errno));
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
  
  if (f_verbose > 1) {
    if (f_verbose > 2) {
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
  return btree_foreach(bp, node_print, NULL);
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
nodeinfo_compare(NODEINFO *a,
		 NODEINFO *b) {
  int d = 0;


  if (!a && !b)
    return 0;
  
  if ((a && !b) || (!a && b))
    return -1;
  
  if ((a->s.st_mode & S_IFMT) != (b->s.st_mode & S_IFMT))
    d |= 0x0001;
  
  if (a->s.st_uid != b->s.st_uid)
    d |= 0x0002;

  if (a->s.st_gid != b->s.st_gid)
    d |= 0x0004;

  if (S_ISLNK(a->s.st_mode) && S_ISLNK(b->s.st_mode)) {
    if ((a->l && !b->l) || (!a->l && b->l) || (a->l && b->l && strcmp(a->l, b->l) != 0))
      d |= 0x0010;
  }

  if (S_ISREG(a->s.st_mode) && S_ISREG(b->s.st_mode)) {
    if (a->s.st_size != b->s.st_size)
      d |= 0x0100;
  }

  if (a->s.st_mtime != b->s.st_mtime)
    d |= 0x1000;

  if (a->s.st_atime != b->s.st_atime)
    d |= 0x2000;

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
  
  btree_search(xd->dst, key, (void **) &dst_nip);

  
  if (!dst_nip) {
    /* New file or dir */

    if (f_verbose) {
      printf("+ %s/", xd->dstpath);
      node_print(key, src_nip, NULL);

      if (f_update) {
	if (S_ISREG(src_nip->s.st_mode)) {
	  /* Regular file */
	  file_copy(src_nip, dstpath);
	} else if (S_ISDIR(src_nip->s.st_mode)) {
	  mkdir(dstpath, src_nip->s.st_mode);
	} else if (S_ISLNK(src_nip->s.st_mode)) {
	  symlink(src_nip->l, dstpath);
	} else if (S_ISBLK(src_nip->s.st_mode) || S_ISCHR(src_nip->s.st_mode)) {
	  mknod(dstpath, src_nip->s.st_mode, src_nip->s.st_dev);
	}
      }
    }
    
    if (strcmp(mode2str(src_nip), "d") == 0) {
      dir_recurse(xd->srcpath, xd->dstpath, key);
    }

    /* Update after subdirectory has been traversed to preserve timestamps */
    if (f_update)
      node_update(src_nip, dstpath);
  
  } else if ((src_nip->s.st_mode & S_IFMT) != (dst_nip->s.st_mode & S_IFMT)) {
    /* Node changed type */
    
    if (S_ISDIR(dst_nip->s.st_mode) && !S_ISDIR(src_nip->s.st_mode)) {
      /* Changed from file -> dir */

      if (f_verbose) {
	printf("- %s/", xd->dstpath);
	node_print(key, dst_nip, NULL);
	
	printf("+ %s/", xd->dstpath);
	node_print(key, src_nip, NULL);
      }

      if (f_update) {
	unlink(dstpath);
	mkdir(dstpath, src_nip->s.st_mode);
      }
      
      dir_recurse(xd->srcpath, xd->dstpath, key);
      
      /* Update after subdirectory has been traversed to preserve timestamps */
      if (f_update)
	node_update(src_nip, dstpath);
      
    } else if (S_ISDIR(src_nip->s.st_mode) && !S_ISDIR(dst_nip->s.st_mode)) {
      /* Changed from dir -> file */ 
      dir_recurse(xd->srcpath, xd->dstpath, key);

      if (f_verbose) {
	printf("- %s/", xd->dstpath);
	node_print(key, dst_nip, NULL);
	printf("+ %s/", xd->dstpath);
	node_print(key, src_nip, NULL);
      }

      if (f_update) {
	rmdir(dstpath);
	
	if (S_ISREG(src_nip->s.st_mode)) {
	  file_copy(src_nip, dstpath);
	} else if (S_ISLNK(src_nip->s.st_mode)) {
	  symlink(src_nip->l, dstpath);
	} else if (S_ISBLK(src_nip->s.st_mode) || S_ISCHR(src_nip->s.st_mode)) {
	  mknod(dstpath, src_nip->s.st_mode, src_nip->s.st_dev);
	}

	node_update(src_nip, dstpath);
      }
      
    } else {
      /* Changed from non-dir to non-dir */
      
      if (f_verbose) {
	printf("* %s/", xd->dstpath);
	node_print(key, src_nip, NULL);
      }
      
      if (f_update) {
	unlink(dstpath);
	
	if (S_ISREG(src_nip->s.st_mode)) {
	  file_copy(src_nip, dstpath);
	} else if (S_ISLNK(src_nip->s.st_mode)) {
	  symlink(src_nip->l, dstpath);
	} else if (S_ISBLK(src_nip->s.st_mode) || S_ISCHR(src_nip->s.st_mode)) {
	  mknod(dstpath, src_nip->s.st_mode, src_nip->s.st_dev);
	}
	
	node_update(src_nip, dstpath);
      }
    }
  } else {
    /* Object is same type */
    int d;
    
    if (strcmp(mode2str(dst_nip), "d") == 0) {
      dir_recurse(xd->srcpath, xd->dstpath, key);

    }

    d = nodeinfo_compare(src_nip, dst_nip);
    if (d) {
      if (f_verbose) {
	printf("! %s/", xd->dstpath);
	node_print(key, src_nip, NULL);
      }
      
      /* Something is different */
      if (f_update)
	node_update(src_nip, dstpath);
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
    
    if (S_ISDIR(dst_nip->s.st_mode))
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

      case 'r':
	++f_recurse;
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

      case 'a':
	++f_recurse;
	++f_acls;
	++f_attrs;
	++f_perms;
	++f_times;
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
	printf("  -v             Be more verbose\n");
	printf("  -d             Enable debugging info\n");
	printf("  -n             No-update mode\n");
	printf("  -r             Recurse\n");
	printf("  -A             Enable ACLs\n");
	printf("  -X             Enable Extended Attributes\n");
	printf("  -m             Mirror mode\n");
	printf("  -a             Archive mode (equal to -rAX)\n");
	printf("  -D <digest>    Select file digest algorithm\n");
	printf("\nDigests:\n  ");
	for (k = DIGEST_TYPE_NONE; k <= DIGEST_TYPE_SHA512; k++)
	  printf("%s%s", (k != DIGEST_TYPE_NONE ? ", " : ""), digest_type2str(k));
	putchar('\n');
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


