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
#include <string.h>
#include <errno.h>
#include <dirent.h>

#include <sys/types.h>
#include <sys/stat.h>

#include <ftw.h>

#include "btree.h"


BTREE *
dir_load(const char *path) {
  BTREE *bp = btree_create(NULL);
  DIR *dp;
  struct dirent *dep;
  

  dp = opendir(path);
  if (!dp)
    return NULL;

  while ((dep = readdir(dp)) != NULL) {
    if (strcmp(dep->d_name, ".") != 0 &&
	strcmp(dep->d_name, "..") != 0)
      btree_insert(bp, dep->d_name, dep->d_type == DT_DIR ? "d" : "");
  }

  closedir(dp);
  return bp;
}


int print(const char *key,
	  const char *val,
	  void *extra) {
  printf("%s (%s)\n", key, val);
  return 0;
}

int
dir_print(BTREE *bp) {
  return btree_foreach(bp, print, NULL);
}


int
check_new_or_updated(const char *key,
		     const char *val,
		     void *extra) {
  BTREE *dst = (BTREE *) extra;
  char *res;
  
  res = btree_search(dst, key);
  if (!res)
    printf("+ %s\n", key);
  else if (strcmp(val, res))
    printf("! %s\n", key);
  return 0;
}

int
check_removed(const char *key,
	      const char *val,
	      void *extra) {
  BTREE *src = (BTREE *) extra;
  char *res;
  
  res = btree_search(src, key);
  if (!res)
    printf("- %s\n", key);
  return 0;
}


int
main(int argc,
     char *argv[]) {
  BTREE *src;
  BTREE *dst;
  DIR *dp;
  struct dirent *dep;
  

  src = dir_load(argv[1]);
  dst = dir_load(argv[2]);

  btree_foreach(src, check_new_or_updated, dst);
  btree_foreach(dst, check_removed, src);
}


