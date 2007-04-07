/*
    httperf -- a tool for measuring web server performance
    Copyright 2000-2007 Hewlett-Packard Company and Contributors listed in
    AUTHORS file. Originally contributed by David Mosberger-Tang

    This file is part of httperf, a web server performance measurment
    tool.

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 2 of the
    License, or (at your option) any later version.
    
    In addition, as a special exception, the copyright holders give
    permission to link the code of this work with the OpenSSL project's
    "OpenSSL" library (or with modified versions of it that use the same
    license as the "OpenSSL" library), and distribute linked combinations
    including the two.  You must obey the GNU General Public License in
    all respects for all of the code used other than "OpenSSL".  If you
    modify this file, you may extend this exception to your version of the
    file, but you are not obligated to do so.  If you do not wish to do
    so, delete this exception statement from your version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  
    02110-1301, USA
*/

/* This code is based on the generic writev() implementation of GNU
   libc.  It implements writev() simply in terms of write().  No
   atomicity guarantees and performance isn't great either, but its
   good enough for SSL purposes.  */

#include "config.h"

#ifdef HAVE_OPENSSL_SSL_H

/* AIX requires this to be the first thing in the file.  */
#ifndef __GNUC__
# if HAVE_ALLOCA_H
#  include <alloca.h>
# else
#  ifdef _AIX
#pragma alloca
#  else
#   ifndef alloca /* predefined by HP cc +Olibcalls */
char *alloca ();
#   endif
#  endif
# endif
#else
/* stdlib.h provides the alloca functionality. */
#include <stdlib.h>
#endif

#include <string.h>

#include <sys/types.h>
#include <sys/uio.h>

#include <openssl/rsa.h>
#include <openssl/crypto.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

ssize_t
SSL_writev (SSL *ssl, const struct iovec *vector, int count)
{
  char *buffer;
  register char *bp;
  size_t bytes, to_copy;
  int i;

  /* Find the total number of bytes to be written.  */
  bytes = 0;
  for (i = 0; i < count; ++i)
    bytes += vector[i].iov_len;

  /* Allocate a temporary buffer to hold the data.  */
  buffer = (char *) alloca (bytes);

  /* Copy the data into BUFFER.  */
  to_copy = bytes;
  bp = buffer;
  for (i = 0; i < count; ++i)
    {
#     define min(a, b)		((a) > (b) ? (b) : (a))
      size_t copy = min (vector[i].iov_len, to_copy);

      memcpy ((void *) bp, (void *) vector[i].iov_base, copy);
      bp += copy;

      to_copy -= copy;
      if (to_copy == 0)
        break;
    }

  return SSL_write (ssl, buffer, bytes);
}

#endif /* HAVE_OPENSSL_SSL_H */
