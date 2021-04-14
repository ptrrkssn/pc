/*
 * acls.c
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

#include <stdlib.h>

#include "acls.h"


#if defined(HAVE_ACL_GET)

acl_t
acl_init(int count) {
  acl_t ap;


  ap = malloc(sizeof(acl_t)+count*sizeof(GACE));
  if (!ap)
    return NULL;

  ap->type = ACL_TYPE_NONE;
  ap->sev = count;
  ap->nev = 0;
}


void
acl_free(void *vp) {
  free(vp);
}


acl_t
acl_get_file(const char *path,
	     acl_type_t type) {
  acl_t ap;
  int ne;

  switch (type) {
  case ACL_TYPE_POSIX:
    ne = acl(path, GETACLCNT, 0, NULL);
    break;
  case ACL_TYPE_NFS4:
    ne = acl(path, ACE_GETACLCNT, 0, NULL);
    break;
  default:
    ne = acl(path, ACE_GETACLCNT, 0, NULL);
    if (ne < 0) {
      ne = acl(path, GETACLCNT, 0, NULL);
      type = ACL_TYPE_POSIX;
    } else
      type = ACL_TYPE_NFS4;
  }
  if (ne < 0)
    return NULL;
  
  ap = acl_init(ne);
  if (!ap)
    return NULL;
  
  switch (type) {
  case ACL_TYPE_POSIX:
    ne = acl(path, GETACL, ap->sev, ap->ev);
    break;
  case ACL_TYPE_NFS4:
    ne = acl(path, ACE_GETACL, ap->sev, ap->ev);
    break;
  }
  if (ne < 0) {
    acl_free(ap);
    return NULL;
  }

  ap->nev = ne;
  return ap;
}

#endif

