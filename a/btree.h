/*
 * btree.h - Binary Tree library
 *
 * Copyright (c) 2017 Peter Eriksson <pen@lysator.liu.se>
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

#ifndef BTREE_H
#define BTREE_H 1

typedef struct bnode {
  struct bnode *parent;
  struct bnode *left;
  struct bnode *right;
  char *key;
  char *val;
} BNODE;

typedef struct btree {
  BNODE *head;
  unsigned int entries;
  int (*cmp)(const char *ka, const char *kb);
} BTREE;


extern BTREE *
btree_create(int (*cmp)(const char *ka, const char *kb));

extern void
btree_destroy(BTREE *bt);

extern int
btree_insert(BTREE *bt,
	     const char *key,
	     const char *val);

extern int
btree_delete(BTREE *bt,
	     const char *key);

extern char *
btree_search(BTREE *bt,
	     const char *key);

extern int
btree_foreach(BTREE *bt,
	      int (*fun)(const char *key, const char *val, void *extra),
	      void *extra);


extern BNODE *
bnode_search(BTREE *bt,
	     const char *key);

extern BNODE *
bnode_min(BNODE *n);

extern BNODE *
bnode_max(BNODE *n);

extern BNODE *
bnode_prev(BNODE *n);

extern BNODE *
bnode_next(BNODE *n);

#endif
