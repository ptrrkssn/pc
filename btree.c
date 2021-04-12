/*
 * btree.c - Binary Tree library
 *
 * Copyright (c) 2017-2021 Peter Eriksson <pen@lysator.liu.se>
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 * 
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
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
#include <string.h>
#include <errno.h>
#include <time.h>

#include "btree.h"



/* Allocate and initialize a btree node */
static BNODE *
bnode_create(const char *key,
	     void *val) {
  BNODE *n;

  
  n = malloc(sizeof(*n));
  if (!n)
    return NULL;

  memset(n, 0, sizeof(*n));
  
  n->key = key;
  n->val = val;

  n->parent = NULL;
  n->left   = NULL;
  n->right  = NULL;

  return n;
}


/* Destroy the btree starting at node 'n' */
static void
bnode_destroy(BTREE *bp,
	      BNODE *n) {
  if (!n)
    return;

  if (n->left) {
    bnode_destroy(bp, n->left);
    n->left = NULL;
  }

  if (n->right) {
    bnode_destroy(bp, n->right);
    n->right = NULL;
  }

  if (n->parent) {
    BNODE *pn = n->parent;
    
    if (pn->left == n)
      pn->left = NULL;
    if (pn->right == n)
      pn->right = NULL;

    n->parent = NULL;
  }

  if (bp->vfree)
    bp->vfree(n->val);
  n->val = NULL;
  
  free((void *) n->key);
  n->key = NULL;
  
  free(n);
}


/* Search a btree for a specific node */
static BNODE *
bnode_search(BTREE *bt,
	     BNODE *head,
	     const char *key) {
  BNODE *cn;

  
  cn = head;
  while (cn) {
    int rc = bt->kcmp(key, cn->key);

    if (rc < 0)
      cn = cn->left;  /* not found, key < cn->key -> go left */
    else if (rc > 0)
      cn = cn->right; /* not found, key > cn->key -> go right */
    else
      return cn;      /* found, return the matching key */
  }

  return NULL;
}



/* Locate the left-most (first) node of a tree */
static inline BNODE *
bnode_min(BNODE *n) {
  if (n) {
    while (n->left)
      n = n->left;
  }

  return n;
}

#if 0 /* Not used currently */

/* Locate the right-most (last) node of a tree */
static inline BNODE *
bnode_max(BNODE *n) {
  if (n) {
    while (n->right)
      n = n->right;
  }

  return n;
}

/* Locate the previous node of a tree */
static inline BNODE *
bnode_prev(BNODE *n) {
  BNODE *pn;

  if (n->left)
    n = bnode_max(n->left);
  else {
    pn = n->parent;
    while (pn && n == pn->left) {
      n = pn;
      pn = n->parent;
    }
    n = pn;
  }
  
  return n;
}


/* Locate the next node of a tree */
static inline BNODE *
bnode_next(BNODE *n) {
  BNODE *pn;

  if (n->right)
    n = bnode_min(n->right);
  else {
    pn = n->parent;
    while (pn && n == pn->right) {
      n = pn;
      pn = n->parent;
    }
    n = pn;
  }
  
  return n;
}
#endif

static inline BNODE *
bnode_delete(BTREE *bt,
	     const char *key,
	     BNODE *n,
	     BNODE *pn) {
  int rc;

  
  rc = bt->kcmp(key, n->key);
  if (rc < 0) {
    /* Not this node, check left subtree */
    if (n->left)
      return bnode_delete(bt, key, n->left, n);
    else
      return NULL;
  } else if (rc > 0) {
    /* Not this node, check right subtree */
    if (n->right)
      return bnode_delete(bt, key, n->right, n);
    else
      return NULL;
  } else {
    /* Delete this node */
    
    if (n->left && n->right) {
      /* We have both left and right trees below us */
      BNODE *min = bnode_min(n->right);
      /* Replace the key & val with the data from the left-most node to the right */
      n->key = min->key;
      n->val = min->val;

      /* min->key & min->val will be free'd by our caller */
      return bnode_delete(bt, key, n->right, n);
    } else if (pn->left == n) {
      /* We are parent's left node */
      pn->left = n->left ? n->left : n->right;
      return n;
    } else if (pn->right == n) {
      /* We are parent's right node */
      pn->right = n->left ? n->left : n->right;
      return n;
    } else
      return NULL;
  }
}


static inline int
bnode_walker(BNODE *n,
	     int (*fun)(const char *key, void *val, void *extra),
	     void *extra) {
  int rc;


  if (n->left) {
    rc = bnode_walker(n->left, fun, extra);
    if (rc)
      return rc;
  }

  rc = fun(n->key, n->val, extra);
  if (rc)
    return rc;

  if (n->right) {
    rc = bnode_walker(n->right, fun, extra);
    return rc;
  }

  return 0;
}



/* --- Public functions -------------------------------------------------- */

BTREE *
btree_create(int (*kcmp)(const char *ka, const char *kb),
	     void (*vfree)(void *val)) {
  BTREE *bt;

  bt = malloc(sizeof(*bt));
  if (!bt)
    return NULL;

  bt->head = NULL;
  bt->kcmp = kcmp ? kcmp : strcmp;
  bt->vfree = vfree;
  bt->entries = 0;
  
  return bt;
}


void
btree_destroy(BTREE *bt) {
  bnode_destroy(bt, bt->head);
  bt->head = NULL;
  bt->kcmp = NULL;
  bt->vfree = NULL;
  free(bt);
}


int
btree_insert(BTREE *bt,
	     const char *key,
	     void *val) {
  BNODE *pn, *cn, *nn;


  nn = bnode_create(key, val);
  if (!nn)
    return -1;

  pn = NULL;
  cn = bt->head;

  while (cn) {
    int rc = bt->kcmp(key, cn->key);
    
    pn = cn;
    if (rc < 0)
      cn = cn->left;
    else if (rc > 0) 
      cn = cn->right;
    else {
      bnode_destroy(bt, nn);
      errno = EEXIST;
      return -1;
    }
  }
  
  nn->parent = pn;
  
  if (!pn)
    bt->head = nn;
  else {
    int rc = bt->kcmp(key, pn->key);
    if (rc < 0)
      pn->left = nn;
    else
      pn->right = nn;
  }

  bt->entries++;
  return 0;
}


int
btree_search(BTREE *bt,
	     const char *key,
	     void **val) {
  BNODE *n;
  
  n = bnode_search(bt, bt->head, key);
  if (n) {
    if (val)
      *val = n->val;
    return 0;
  }

  errno = ENOENT;
  return -1;
}


int
btree_delete(BTREE *bt,
	     const char *key) {
  BNODE *n, *rn;


  n = bnode_search(bt, bt->head, key);
  if (!n)
    return 1;

  if (bt->head == n) {
    BNODE *althead, *rn;

    althead = malloc(sizeof(*althead));
    if (!althead)
      return -1;
    
    althead->parent = NULL;
    althead->left = bt->head;
    althead->right = NULL;

    rn = bnode_delete(bt, key, n, althead);
    bt->head = althead->left;
    if (rn) {
      free(rn);
      return 0;
    } else
      return 1;
  } else {
    rn = bnode_delete(bt, key, bt->head, NULL);
    if (rn) {
      free(rn);
      return 0;
    } else
      return 1;
  }
}

int
btree_foreach(BTREE *bt,
	      int (*fun)(const char *key, void *val, void *extra),
	      void *extra) {
  if (!bt) {
    errno = EINVAL;
    return -1;
  }
  
  if (bt->head)
    return bnode_walker(bt->head, fun, extra);
  
  return 0;
}



#ifdef BTREE_MAIN
int
print_node(const char *key,
	   void *val,
	   void *extra) {
  printf("%-30s  %s\n", key, val);
  return 0;
}

int
nothing_node(const char *key,
	     void *val,
	     void *extra) {
  return 0;
}


int
main(int argc,
     char *argv[]) {
  FILE *fp;
  char buf[2048];
  char *ptr, *key, *val;
  BTREE *bt;
  time_t t1, t2;
  BNODE *n;
  

  bt = btree_create(NULL);
  if (!bt) {
    fprintf(stderr, "%s: Error: Btree Create failed: %s\n", argv[0], strerror(errno));
    exit(1);
  }
  
  fp = fopen(argv[1], "r");
  if (!fp) {
    fprintf(stderr, "%s: Error: %s: Open failed: %s\n", argv[0], argv[1], strerror(errno));
    exit(1);
  }

  time(&t1);
  while (fgets(buf, sizeof(buf), fp)) {
    if (*buf != '/')
      continue;

    key = strtok_r(buf, " \t\n\r", &ptr);
    val = strtok_r(NULL, "\n\r", &ptr);

    if (btree_insert(bt, key, (void *) val) < 0) {
      fprintf(stderr, "%s: Error: %s (%s): Btree Insert failed: %s\n",
	      argv[0], key, val, strerror(errno));
      exit(1);
    }
  }
  time(&t2);
  fclose(fp);

  
  fprintf(stderr, "%u entries loaded in %f seconds\n", bt->entries, difftime(t1, t2));
  
  if (argv[2]) {
    int rc;
    
    rc = btree_delete(bt, argv[2]);
    printf("Btree Delete (%s) -> %d\n", argv[2], rc);
  
    if (argv[3]) {
      val = btree_search(bt, argv[3]);
      printf("Btree Search (%s) -> %s\n", argv[2], val);
    }
  }
  
  puts("\nBtree Foreach:");
  /*  btree_foreach(bt, print_node, NULL); */
  btree_foreach(bt, print_node, NULL);

#if 0
  puts("\nWalking Forward:");
  n = bnode_min(bt->head);
  while (n) {
    printf("%-30s  %s\n", n->key, n->val);
    n = bnode_next(n);
  }

  puts("\nWalking Backwards:");
  n = bnode_max(bt->head);
  while (n) {
    printf("%-30s  %s\n", n->key, n->val);
    n = bnode_prev(n);
  }
#endif
  
  return 0;
}
#endif
