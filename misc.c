/*
 * misc.c
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
#include <stdarg.h>
#include <ctype.h>
#include <time.h>

#include "misc.h"


/* 
 * Concatenate the list of strings (and end at first NULL) 
 * into a dynamically allocated buffer
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
 * Parse a string into a size_t value
 */
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



/*
 * Convert a size_t value into a printable string
 */
char *
size2str(size_t b,
	 char *buf,
	 size_t bufsize,
	 int b2f) {
  int base = 1000;
  size_t t;
  
  
  if (b2f)
    base = 1024;

  if (b == 0) {
    strcpy(buf, "0");
    return buf;
  }
  
  t = b/base;
  if (b < 10000 && (t*base != b)) {
    snprintf(buf, bufsize, "%ld", b);
    return buf;
  }
  
  b /= base;
  t = b/base;
  if (b < 10000 && (t*base != b)) {
    snprintf(buf, bufsize, "%ld K%s", b, b2f == 1 ? "i" : "");
    return buf;
  }
  
  b /= base;
  t = b/base;
  if (b < 10000 && (t*base != b)) {
    snprintf(buf, bufsize, "%ld M%s", b, b2f == 1 ? "i" : "");
    return buf;
  }
  
  b /= base;
  t = b/base;
  if (b < 10000 && (t*base != b)) {
    snprintf(buf, bufsize, "%ld G%s", b, b2f == 1 ? "i" : "");
    return buf;
  }
  
  b /= base;
  t = b/base;
  if (b < 10000 && (t*base != b)) {
    snprintf(buf, bufsize, "%ld T%s", b, b2f == 1 ? "i" : "");
    return buf;
  }
  
  snprintf(buf, bufsize, "%ld P%s", b, b2f == 1 ? "i" : "");
  return buf;
}



/*
 * Convert a time_t value into a printable string
 */
char *
time2str(time_t t,
	 char *buf,
	 size_t bufsize,
	 int abs_f) {
  if (abs_f) {
    struct tm *tp = localtime(&t);
    
    strftime(buf, bufsize, "%Y-%m-%d %H:%M:%S", tp);
    return buf;
  }
  
    
  if (labs(t) < 120) {
    snprintf(buf, bufsize, "%lds", t);
    return buf;
  }

  t /= 60;
  if (labs(t) < 120) {
    snprintf(buf, bufsize, "%ldm", t);
    return buf;
  }

  t /= 60;
  if (labs(t) < 48) {
    snprintf(buf, bufsize, "%ldh", t);
    return buf;
  }
  
  t /= 24;
  if (labs(t) < 14) {
    snprintf(buf, bufsize, "%ldD", t);
    return buf;
  }
  
  if (labs(t) < 60) {
    t /= 7;
    snprintf(buf, bufsize, "%ldW", t);
    return buf;
  }

  if (labs(t) < 365*2) {
    t /= 30;
    snprintf(buf, bufsize, "%ldM", t);
    return buf;
  }

  t /= 365;
  snprintf(buf, bufsize, "%ldY", t);
  return buf;
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



