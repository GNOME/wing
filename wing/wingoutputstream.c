/*
 * Copyright (C) 2006-2010 Red Hat, Inc.
 * Copyright (C) 2018 NICE s.r.l.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Alexander Larsson <alexl@redhat.com>
 * Author: Tor Lillqvist <tml@iki.fi>
 * Author: Ignacio Casal Quinteiro <qignacio@amazon.com>
 */

#include "wingoutputstream.h"
#include "wingutils.h"
#include "wingsource.h"

#include <windows.h>

/**
 * SECTION:gwin32outputstream
 * @short_description: Streaming output operations for Windows file handles
 * @include: gio/gwin32outputstream.h
 * @see_also: #GOutputStream
 *
 * #WingOutputStream implements #GOutputStream for writing to a
 * Windows file handle.
 */

typedef struct {
  HANDLE handle;
  gboolean close_handle;

  OVERLAPPED overlap;
} WingOutputStreamPrivate;

enum {
  PROP_0,
  PROP_HANDLE,
  PROP_CLOSE_HANDLE,
  LAST_PROP
};

static GParamSpec *props[LAST_PROP];

static void wing_output_stream_pollable_iface_init (GPollableOutputStreamInterface *iface);

G_DEFINE_TYPE_WITH_CODE (WingOutputStream, wing_output_stream, G_TYPE_OUTPUT_STREAM,
                         G_ADD_PRIVATE (WingOutputStream)
                         G_IMPLEMENT_INTERFACE (G_TYPE_POLLABLE_OUTPUT_STREAM, wing_output_stream_pollable_iface_init)
                         )

static void
wing_output_stream_finalize (GObject *object)
{
  WingOutputStream *wing_stream;
  WingOutputStreamPrivate *priv;

  wing_stream = WING_OUTPUT_STREAM (object);
  priv = wing_output_stream_get_instance_private (wing_stream);

  if (priv->overlap.hEvent != INVALID_HANDLE_VALUE)
    CloseHandle (priv->overlap.hEvent);

  G_OBJECT_CLASS (wing_output_stream_parent_class)->finalize (object);
}

static void
wing_output_stream_set_property (GObject         *object,
                                 guint            prop_id,
                                 const GValue    *value,
                                 GParamSpec      *pspec)
{
  WingOutputStream *wing_stream;
  WingOutputStreamPrivate *priv;

  wing_stream = WING_OUTPUT_STREAM (object);
  priv = wing_output_stream_get_instance_private (wing_stream);

  switch (prop_id)
    {
    case PROP_HANDLE:
      priv->handle = g_value_get_pointer (value);
      break;
    case PROP_CLOSE_HANDLE:
      priv->close_handle = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
wing_output_stream_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  WingOutputStream *wing_stream;
  WingOutputStreamPrivate *priv;

  wing_stream = WING_OUTPUT_STREAM (object);
  priv = wing_output_stream_get_instance_private (wing_stream);

  switch (prop_id)
    {
    case PROP_HANDLE:
      g_value_set_pointer (value, priv->handle);
      break;
    case PROP_CLOSE_HANDLE:
      g_value_set_boolean (value, priv->close_handle);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static gssize
write_internal (GOutputStream  *stream,
                const void     *buffer,
                gsize           count,
                gboolean        blocking,
                GCancellable   *cancellable,
                GError        **error)
{
  WingOutputStream *wing_stream;
  WingOutputStreamPrivate *priv;
  BOOL res;
  DWORD nbytes, nwritten;
  gssize retval = -1;

  wing_stream = WING_OUTPUT_STREAM (stream);
  priv = wing_output_stream_get_instance_private (wing_stream);

  if (g_cancellable_set_error_if_cancelled (cancellable, error))
    return -1;

  if (!blocking && g_pollable_output_stream_is_writable (G_POLLABLE_OUTPUT_STREAM (stream)))
    {
      gboolean result;

      result = GetOverlappedResult (priv->overlap.hEvent, &priv->overlap, &nwritten, FALSE);
      if (!result && GetLastError () == ERROR_IO_INCOMPLETE)
        {
          g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK,
                               g_strerror (EAGAIN));
          return -1;
        }

      ResetEvent (priv->overlap.hEvent);

      retval = nwritten;
      goto end;
    }

  if (count > G_MAXINT)
    nbytes = G_MAXINT;
  else
    nbytes = count;

  ResetEvent (priv->overlap.hEvent);

  res = WriteFile (priv->handle, buffer, nbytes, &nwritten, &priv->overlap);
  if (res)
    {
      retval = nwritten;
      ResetEvent (priv->overlap.hEvent);
    }
  else
    {
      int errsv = GetLastError ();

      if (errsv == ERROR_IO_PENDING)
        {
          if (!blocking)
            {
              g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK,
                                   g_strerror (EAGAIN));
              goto end;
            }
          else if (blocking && wing_overlap_wait_result (priv->handle,
                                                         &priv->overlap,
                                                         &nwritten, cancellable))
            {
              retval = nwritten;
              goto end;
            }
        }

      if (g_cancellable_set_error_if_cancelled (cancellable, error))
        goto end;

      errsv = GetLastError ();
      if (errsv == ERROR_HANDLE_EOF ||
          errsv == ERROR_BROKEN_PIPE)
        {
          retval = 0;
        }
      else
        {
          gchar *emsg;

          emsg = g_win32_error_message (errsv);
          g_set_error (error, G_IO_ERROR,
                       g_io_error_from_win32_error (errsv),
                       "Error writing to handle: %s",
                       emsg);
          g_free (emsg);
        }
    }

end:
  return retval;
}

static gssize
wing_output_stream_write (GOutputStream  *stream,
                          const void     *buffer,
                          gsize           count,
                          GCancellable   *cancellable,
                          GError        **error)
{
  return write_internal (stream, buffer, count, TRUE, cancellable, error);
}

static gboolean
wing_output_stream_close (GOutputStream  *stream,
                          GCancellable   *cancellable,
                          GError        **error)
{
  WingOutputStream *wing_stream;
  WingOutputStreamPrivate *priv;
  BOOL res;

  wing_stream = WING_OUTPUT_STREAM (stream);
  priv = wing_output_stream_get_instance_private (wing_stream);

  if (!priv->close_handle)
    return TRUE;

  res = CloseHandle (priv->handle);
  if (!res)
    {
      int errsv = GetLastError ();
      gchar *emsg = g_win32_error_message (errsv);

      g_set_error (error, G_IO_ERROR,
                   g_io_error_from_win32_error (errsv),
                   "Error closing handle: %s",
                   emsg);
      g_free (emsg);
      return FALSE;
    }

  return TRUE;
}

static void
wing_output_stream_class_init (WingOutputStreamClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GOutputStreamClass *stream_class = G_OUTPUT_STREAM_CLASS (klass);

  gobject_class->finalize = wing_output_stream_finalize;
  gobject_class->get_property = wing_output_stream_get_property;
  gobject_class->set_property = wing_output_stream_set_property;

  stream_class->write_fn = wing_output_stream_write;
  stream_class->close_fn = wing_output_stream_close;

   /**
   * WingOutputStream:handle:
   *
   * The file handle that the stream writes to.
   */
  props[PROP_HANDLE] =
    g_param_spec_pointer ("handle",
                          "File handle",
                          "The file handle to write to",
                          G_PARAM_READWRITE |
                          G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS);

  /**
   * WingOutputStream:close-handle:
   *
   * Whether to close the file handle when the stream is closed.
   */
  props[PROP_CLOSE_HANDLE] =
    g_param_spec_boolean ("close-handle",
                          "Close file handle",
                          "Whether to close the file handle when the stream is closed",
                          TRUE,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class, LAST_PROP, props);
}

static void
wing_output_stream_init (WingOutputStream *wing_stream)
{
  WingOutputStreamPrivate *priv;

  priv = wing_output_stream_get_instance_private (wing_stream);
  priv->handle = NULL;
  priv->close_handle = TRUE;
  priv->overlap.hEvent = CreateEvent (NULL, TRUE, FALSE, NULL);
  g_return_if_fail (priv->overlap.hEvent != INVALID_HANDLE_VALUE);
}

static gboolean
wing_output_stream_pollable_is_writable (GPollableOutputStream *pollable)
{
  WingOutputStream *wing_stream = WING_OUTPUT_STREAM (pollable);
  WingOutputStreamPrivate *priv;

  priv = wing_output_stream_get_instance_private (wing_stream);

  return WaitForSingleObject (priv->overlap.hEvent, 0) == WAIT_OBJECT_0;
}

static GSource *
wing_output_stream_pollable_create_source (GPollableOutputStream *pollable,
                                           GCancellable          *cancellable)
{
  WingOutputStream *wing_stream = WING_OUTPUT_STREAM (pollable);
  WingOutputStreamPrivate *priv;
  GSource *handle_source, *pollable_source;

  priv = wing_output_stream_get_instance_private (wing_stream);

  handle_source = wing_create_source (priv->overlap.hEvent,
                                      G_IO_IN, cancellable);
  pollable_source = g_pollable_source_new_full (pollable, handle_source, cancellable);
  g_source_unref (handle_source);

  return pollable_source;
}

static gssize
wing_output_stream_pollable_write_nonblocking (GPollableOutputStream  *pollable,
                                               const void             *buffer,
                                               gsize                   size,
                                               GError                **error)
{
  return write_internal (G_OUTPUT_STREAM (pollable), buffer, size, FALSE, NULL, error);
}

static void
wing_output_stream_pollable_iface_init (GPollableOutputStreamInterface *iface)
{
  iface->is_writable = wing_output_stream_pollable_is_writable;
  iface->create_source = wing_output_stream_pollable_create_source;
  iface->write_nonblocking = wing_output_stream_pollable_write_nonblocking;
}

/**
 * wing_output_stream_new:
 * @handle: a Win32 file handle
 * @close_handle: %TRUE to close the handle when done
 *
 * Creates a new #WingOutputStream for the given @handle.
 *
 * If @close_handle, is %TRUE, the handle will be closed when the
 * output stream is destroyed.
 *
 * Returns: a new #GOutputStream
**/
GOutputStream *
wing_output_stream_new (void    *handle,
                        gboolean close_handle)
{
  g_return_val_if_fail (handle != NULL, NULL);

  return g_object_new (WING_TYPE_OUTPUT_STREAM,
                       "handle", handle,
                       "close-handle", close_handle,
                       NULL);
}

/**
 * wing_output_stream_set_close_handle:
 * @stream: a #WingOutputStream
 * @close_handle: %TRUE to close the handle when done
 *
 * Sets whether the handle of @stream shall be closed when the stream
 * is closed.
 */
void
wing_output_stream_set_close_handle (WingOutputStream *stream,
                                     gboolean          close_handle)
{
  WingOutputStreamPrivate *priv;

  g_return_if_fail (WING_IS_OUTPUT_STREAM (stream));

  priv = wing_output_stream_get_instance_private (stream);

  close_handle = close_handle != FALSE;
  if (priv->close_handle != close_handle)
    {
      priv->close_handle = close_handle;
      g_object_notify (G_OBJECT (stream), "close-handle");
    }
}

/**
 * wing_output_stream_get_close_handle:
 * @stream: a #WingOutputStream
 *
 * Returns whether the handle of @stream will be closed when the
 * stream is closed.
 *
 * Returns: %TRUE if the handle is closed when done
 */
gboolean
wing_output_stream_get_close_handle (WingOutputStream *stream)
{
  WingOutputStreamPrivate *priv;

  g_return_val_if_fail (WING_IS_OUTPUT_STREAM (stream), FALSE);

  priv = wing_output_stream_get_instance_private (stream);

  return priv->close_handle;
}

/**
 * wing_output_stream_get_handle:
 * @stream: a #WingOutputStream
 *
 * Return the Windows handle that the stream writes to.
 *
 * Returns: The handle descriptor of @stream
 */
void *
wing_output_stream_get_handle (WingOutputStream *stream)
{
  WingOutputStreamPrivate *priv;

  g_return_val_if_fail (WING_IS_OUTPUT_STREAM (stream), NULL);

  priv = wing_output_stream_get_instance_private (stream);

  return priv->handle;
}
