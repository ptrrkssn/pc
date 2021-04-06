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

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/acl.h>
#include <sys/param.h>
#include <sys/vnode.h>
#include <sys/extattr.h>

#include "btree.h"
#include "digest.h"


typedef struct nodeinfo {
  struct stat s;
  char *d;
  struct {
    acl_t nfs; /* NFSv4 / ZFS */
    acl_t acc; /* POSIX */
    acl_t def; /* POSIX */
  } a;
  struct {
    BTREE *sys;
    BTREE *usr;
  } x;
} NODEINFO;


char *argv0 = "pc";

int f_verbose = 0;
int f_debug   = 0;
int f_update  = 1;
int f_recurse = 0;
int f_mirror  = 0;
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
  
  if (nip->d)
    free(nip->d);

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

  if (file) {
    path = strdupcat(path, "/", file, NULL);
    if (!path)
      abort();
  }
  
  nip = malloc(sizeof(*nip));
  if (!nip)
    abort();
  
  memset(nip, 0, sizeof(*nip));
  
  if (lstat(path, &nip->s) < 0) {
    free(nip);
    nip = NULL;
  } else {
    if (S_ISLNK(nip->s.st_mode)) {
      char buf[1024];
      
      if (readlink(path, buf, sizeof(buf)) < 0) {
	fprintf(stderr, "%s: Error: %s: readlink: %s\n",
		argv0, path, strerror(errno));
	exit(1); /* XXX: Better handling needed */
      } else {
	nip->d = strdup(buf);
	if (!nip->d)
	  abort(); /* XXX: Better error handling */
      }
    }
    
    if (f_acls) {
      if (S_ISLNK(nip->s.st_mode)) {
	nip->a.nfs = acl_get_link_np(path, ACL_TYPE_NFS4);
	nip->a.acc = acl_get_link_np(path, ACL_TYPE_ACCESS);
	nip->a.def = acl_get_link_np(path, ACL_TYPE_DEFAULT);
      } else {
	nip->a.nfs = acl_get_file(path, ACL_TYPE_NFS4);
	nip->a.acc = acl_get_file(path, ACL_TYPE_ACCESS);
	nip->a.def = acl_get_file(path, ACL_TYPE_DEFAULT);
      }
    }
    
    if (f_attrs) {
      nip->x.sys = attrs_get(path, S_ISLNK(nip->s.st_mode), EXTATTR_NAMESPACE_SYSTEM);
      nip->x.usr = attrs_get(path, S_ISLNK(nip->s.st_mode), EXTATTR_NAMESPACE_USER);
    }
  }
  
  if (file)
    free((void *) path);
  
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
    printf(" -> %s", nip->d);
    
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
  if (f_verbose) {
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


unsigned int
nodeinfo_compare(NODEINFO *a,
		 NODEINFO *b) {
  unsigned int d = 0;

  if ((a->s.st_mode & S_IFMT) != (b->s.st_mode & S_IFMT))
    d |= 0x0001;
  
  if (a->s.st_size != b->s.st_size)
    d |= 0x0002;

  if (a->s.st_uid != b->s.st_uid)
    d |= 0x0004;

  if (a->s.st_gid != b->s.st_gid)
    d |= 0x0008;

#if 0
  if (a->s.st_mtime != b->s.st_mtime)
    d |= 0x0010;
#endif

  return d;
}


int
check_new_or_updated(const char *key,
		     void *val,
		     void *extra) {
  NODEINFO *src_nip = (NODEINFO *) val;
  NODEINFO *dst_nip = NULL;
  XDATA *xd = (XDATA *) extra;
  int rc, d;

  
  btree_search(xd->dst, key, (void **) &dst_nip);
  if (!dst_nip) {
    /* New file or dir */

    if (f_verbose) {
      printf("+ %s/", xd->dstpath);
      node_print(key, src_nip, NULL);
    }
    
    if (strcmp(mode2str(src_nip), "d") == 0) {
      rc = dir_recurse(xd->srcpath, xd->dstpath, key);
    }
  } else if ((d = nodeinfo_compare(src_nip, dst_nip)) != 0) {
    if (S_ISDIR(dst_nip->s.st_mode) && !S_ISDIR(src_nip->s.st_mode)) {
      /* Changed from file -> dir */

      if (f_verbose) {
	printf("- %s/", xd->dstpath);
	node_print(key, dst_nip, NULL);
	printf("+ %s/", xd->dstpath);
	node_print(key, src_nip, NULL);
      }
      
      rc = dir_recurse(xd->srcpath, xd->dstpath, key);
    } else if (S_ISDIR(src_nip->s.st_mode) && !S_ISDIR(dst_nip->s.st_mode)) {
      /* Changed from dir -> file */ 
      rc = dir_recurse(xd->srcpath, xd->dstpath, key);

      if (f_verbose) {
	printf("- %s/", xd->dstpath);
	node_print(key, dst_nip, NULL);
	printf("+ %s/", xd->dstpath);
	node_print(key, src_nip, NULL);
      }
    } else {
      if (f_verbose) {
	printf("%d %s/", d, xd->dstpath);
	node_print(key, src_nip, NULL);
      }
    }
  } else {
    if (strcmp(mode2str(dst_nip), "d") == 0) {
      rc = dir_recurse(xd->srcpath, xd->dstpath, key);
    } else {
      /* Check if file has changed timestamps or content */
    }
  }
  
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
    
    if (strcmp(mode2str(dst_nip), "d") == 0)
      dir_recurse(xd->srcpath, xd->dstpath, key);

    if (f_verbose) {
      printf("- %s/", xd->dstpath);
      node_print(key, dst_nip, NULL);
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

      case 'm':
	++f_mirror;
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
	break;

      case 'D':
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


