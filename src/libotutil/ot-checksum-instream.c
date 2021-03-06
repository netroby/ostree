/* 
 * Copyright (C) 2017 Colin Walters <walters@verbum.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "config.h"

#include "ot-checksum-instream.h"
#include "ot-checksum-utils.h"

#if defined(HAVE_OPENSSL)
#include <openssl/evp.h>
#elif defined(HAVE_GNUTLS)
#include <gnutls/gnutls.h>
#include <gnutls/crypto.h>
#endif

G_DEFINE_TYPE (OtChecksumInstream, ot_checksum_instream, G_TYPE_FILTER_INPUT_STREAM)

struct _OtChecksumInstreamPrivate {
#if defined(HAVE_OPENSSL)
  EVP_MD_CTX *checksum;
#elif defined(HAVE_GNUTLS)
  gnutls_digest_algorithm_t checksum_type;
  gnutls_hash_hd_t checksum;
#else
  GChecksumType checksum_type;
  GChecksum *checksum;
#endif
};

static gssize   ot_checksum_instream_read         (GInputStream         *stream,
                                                           void                 *buffer,
                                                           gsize                 count,
                                                           GCancellable         *cancellable,
                                                           GError              **error);

static void
ot_checksum_instream_finalize (GObject *object)
{
  OtChecksumInstream *self = (OtChecksumInstream*)object;

#if defined(HAVE_OPENSSL)
  EVP_MD_CTX_destroy (self->priv->checksum);
#elif defined(HAVE_GNUTLS)
  gnutls_hash_deinit (self->priv->checksum, NULL);
#else
  g_checksum_free (self->priv->checksum);
#endif

  G_OBJECT_CLASS (ot_checksum_instream_parent_class)->finalize (object);
}

static void
ot_checksum_instream_class_init (OtChecksumInstreamClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GInputStreamClass *stream_class = G_INPUT_STREAM_CLASS (klass);

  g_type_class_add_private (klass, sizeof (OtChecksumInstreamPrivate));

  object_class->finalize = ot_checksum_instream_finalize;
  stream_class->read_fn = ot_checksum_instream_read;
}

static void
ot_checksum_instream_init (OtChecksumInstream *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, OT_TYPE_CHECKSUM_INSTREAM, OtChecksumInstreamPrivate);
}

#if defined(HAVE_OPENSSL)
static const EVP_MD *
gchecksum_type_to_openssl (GChecksumType checksum_type)
{
  switch (checksum_type)
    {
    case G_CHECKSUM_SHA256:
      return EVP_sha256 ();
    default:
      /* If there's something else, fill in here */
      g_assert_not_reached ();
    }
}
#elif defined(HAVE_GNUTLS)
static gnutls_digest_algorithm_t
gchecksum_type_to_gnutls (GChecksumType checksum_type)
{
  switch (checksum_type)
    {
    case G_CHECKSUM_SHA256:
      return GNUTLS_DIG_SHA256;
    default:
      g_assert_not_reached ();
    }
}
#endif

OtChecksumInstream *
ot_checksum_instream_new (GInputStream    *base,
                          GChecksumType    checksum_type)
{
  OtChecksumInstream *stream;

  g_return_val_if_fail (G_IS_INPUT_STREAM (base), NULL);

  stream = g_object_new (OT_TYPE_CHECKSUM_INSTREAM,
                         "base-stream", base,
                         NULL);

  /* For now */
  g_assert (checksum_type == G_CHECKSUM_SHA256);

#if defined(HAVE_OPENSSL)
  stream->priv->checksum = EVP_MD_CTX_create ();
  g_assert (stream->priv->checksum);
  g_assert (EVP_DigestInit_ex (stream->priv->checksum, gchecksum_type_to_openssl (checksum_type), NULL));
#elif defined(HAVE_GNUTLS)
  stream->priv->checksum_type = gchecksum_type_to_gnutls (checksum_type);
  g_assert (!gnutls_hash_init (&stream->priv->checksum, stream->priv->checksum_type));
#else
  stream->priv->checksum = g_checksum_new (checksum_type);
  stream->priv->checksum_type = checksum_type;
#endif

  return (OtChecksumInstream*) (stream);
}

static gssize
ot_checksum_instream_read (GInputStream  *stream,
                           void          *buffer,
                           gsize          count,
                           GCancellable  *cancellable,
                           GError       **error)
{
  OtChecksumInstream *self = (OtChecksumInstream*) stream;
  GFilterInputStream *fself = (GFilterInputStream*) self;
  gssize res = -1;

  res = g_input_stream_read (fself->base_stream,
                             buffer,
                             count,
                             cancellable,
                             error);
  if (res > 0)
    {
#if defined(HAVE_OPENSSL)
      g_assert (EVP_DigestUpdate (self->priv->checksum, buffer, res));
#elif defined(HAVE_GNUTLS)
      g_assert (!gnutls_hash (self->priv->checksum, buffer, res));
#else
      g_checksum_update (self->priv->checksum, buffer, res);
#endif
    }

  return res;
}

void
ot_checksum_instream_get_digest (OtChecksumInstream *stream,
                                 guint8          *buffer,
                                 gsize           *digest_len)
{
#if defined(HAVE_OPENSSL)
  unsigned len;
  EVP_DigestFinal_ex (stream->priv->checksum, buffer, &len);
  if (digest_len)
    *digest_len = len;
#elif defined(HAVE_GNUTLS)
  gnutls_hash_output (stream->priv->checksum, buffer);
  if (digest_len)
    *digest_len = gnutls_hash_get_len (stream->priv->checksum_type);
#else
  g_checksum_get_digest (stream->priv->checksum, buffer, digest_len);
#endif
}

guint8*
ot_checksum_instream_dup_digest (OtChecksumInstream *stream,
                                 gsize              *ret_len)
{
#if defined(HAVE_OPENSSL)
  guint len;
  guchar *ret = g_malloc0 (EVP_MAX_MD_SIZE);
  g_assert (EVP_DigestFinal_ex (stream->priv->checksum, ret, &len));
#elif defined(HAVE_GNUTLS)
  guint len = gnutls_hash_get_len (stream->priv->checksum_type);
  guchar *ret = g_malloc0 (len);
  gnutls_hash_output (stream->priv->checksum, ret);
#else
  gsize len = g_checksum_type_get_length (stream->priv->checksum_type);
  guchar *ret = g_malloc (len);
  g_checksum_get_digest (stream->priv->checksum, ret, &len);
#endif
  if (ret_len)
    *ret_len = len;
  return ret;
}

char *
ot_checksum_instream_get_string (OtChecksumInstream *stream)
{
#if defined(HAVE_OPENSSL)
  unsigned len;
  guint8 csum[EVP_MAX_MD_SIZE];
  g_assert (EVP_DigestFinal_ex (stream->priv->checksum, csum, &len));
  char *buf = g_malloc (len * 2 + 1);
  ot_bin2hex (buf, (guint8*)csum, len);
  return buf;
#elif defined(HAVE_GNUTLS)
  gsize len;
  guint8 *csum = ot_checksum_instream_dup_digest(stream, &len);
  char *buf = g_malloc0 (len * 2 + 1);
  ot_bin2hex (buf, csum, len);
  g_free (csum);
  return buf;
#else
  return g_strdup (g_checksum_get_string (stream->priv->checksum));
#endif
}
