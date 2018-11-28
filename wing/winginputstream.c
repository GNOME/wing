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

#include "winginputstream.h"

#include <windows.h>

/**
 * SECTION:winginputstream
 * @short_description: Streaming input operations for Windows file handles
 * @see_also: #GInputStream
 *
 * #WingInputStream implements #GInputStream for reading from a
 * Windows file handle.
 */

typedef struct {
  HANDLE handle;
  gboolean close_handle;

  OVERLAPPED overlap;
} WingInputStreamPrivate;

enum {
  PROP_0,
  PROP_HANDLE,
  PROP_CLOSE_HANDLE,
  LAST_PROP
};

static GParamSpec *props[LAST_PROP];

static void wing_input_stream_pollable_iface_init (GPollableInputStreamInterface *iface);

G_DEFINE_TYPE_WITH_CODE (WingInputStream, wing_input_stream, G_TYPE_INPUT_STREAM,
                         G_ADD_PRIVATE (WingInputStream)
                         G_IMPLEMENT_INTERFACE (G_TYPE_POLLABLE_INPUT_STREAM, wing_input_stream_pollable_iface_init)
                         )

static void
wing_input_stream_finalize (GObject *object)
{
  WingInputStream *wing_stream;
  WingInputStreamPrivate *priv;

  wing_stream = WING_INPUT_STREAM (object);
  priv = wing_input_stream_get_instance_private (wing_stream);

  if (priv->overlap.hEvent != INVALID_HANDLE_VALUE)
    CloseHandle (priv->overlap.hEvent);

  G_OBJECT_CLASS (wing_input_stream_parent_class)->finalize (object);
}

static void
wing_input_stream_set_property (GObject         *object,
                                guint            prop_id,
                                const GValue    *value,
                                GParamSpec      *pspec)
{
  WingInputStream *wing_stream;
  WingInputStreamPrivate *priv;

  wing_stream = WING_INPUT_STREAM (object);
  priv = wing_input_stream_get_instance_private (wing_stream);

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
wing_input_stream_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  WingInputStream *wing_stream;
  WingInputStreamPrivate *priv;

  wing_stream = WING_INPUT_STREAM (object);
  priv = wing_input_stream_get_instance_private (wing_stream);

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
read_internal (GInputStream  *stream,
               void          *buffer,
               gsize          count,
               gboolean       blocking,
               GCancellable  *cancellable,
               GError       **error)
{
  WingInputStream *wing_stream;
  WingInputStreamPrivate *priv;
  BOOL res;
  DWORD nbytes, nread;
  gssize retval = -1;

  wing_stream = WING_INPUT_STREAM (stream);
  priv = wing_input_stream_get_instance_private (wing_stream);

  if (g_cancellable_set_error_if_cancelled (cancellable, error))
    return -1;

  if (count > G_MAXINT)
    nbytes = G_MAXINT;
  else
    nbytes = count;

  ResetEvent (priv->overlap.hEvent);

  res = ReadFile (priv->handle, buffer, nbytes, &nread, &priv->overlap);
  if (res)
    retval = nread;
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
          else if (blocking && wing_overlap_wait_result (win32_stream->priv->handle,
                                                    &priv->overlap,
                                                    &nread, cancellable))
            {
              retval = nread;
              goto end;
            }
        }

      if (g_cancellable_set_error_if_cancelled (cancellable, error))
        goto end;

      errsv = GetLastError ();
      if (errsv == ERROR_MORE_DATA)
        {
          /* If a named pipe is being read in message mode and the
           * next message is longer than the nNumberOfBytesToRead
           * parameter specifies, ReadFile returns FALSE and
           * GetLastError returns ERROR_MORE_DATA */
          retval = nread;
          goto end;
        }
      else if (errsv == ERROR_HANDLE_EOF ||
               errsv == ERROR_BROKEN_PIPE)
        {
          /* TODO: the other end of a pipe may call the WriteFile
           * function with nNumberOfBytesToWrite set to zero. In this
           * case, it's not possible for the caller to know if it's
           * broken pipe or a read of 0. Perhaps we should add a
           * is_broken flag for this win32 case.. */
          retval = 0;
        }
      else
        {
          gchar *emsg;

          emsg = wing_error_message (errsv);
          g_set_error (error, G_IO_ERROR,
                       g_io_error_from_win32_error (errsv),
                       "Error reading from handle: %s",
                       emsg);
          g_free (emsg);
        }
    }

end:
  return retval;
}

static gssize
wing_input_stream_read (GInputStream  *stream,
                        void          *buffer,
                        gsize          count,
                        GCancellable  *cancellable,
                        GError       **error)
{
  return read_internal (stream, buffer, count, TRUE, cancellable, error);
}

static gboolean
wing_input_stream_close (GInputStream  *stream,
                           GCancellable  *cancellable,
                           GError       **error)
{
  WingInputStream *wing_stream;
  WingInputStreamPrivate *priv;
  BOOL res;

  wing_stream = WING_INPUT_STREAM (stream);
  priv = wing_input_stream_get_instance_private (wing_stream);

  if (priv->close_handle == INVALID_HANDLE_VALUE)
    return TRUE;

  res = CloseHandle (priv->handle);
  if (!res)
    {
      int errsv = GetLastError ();
      gchar *emsg = wing_error_message (errsv);

      g_set_error (error, G_IO_ERROR,
                   g_io_error_from_win32_error (errsv),
                   "Error closing handle: %s",
                   emsg);
      g_free (emsg);
      return FALSE;
    }

  priv->handle = INVALID_HANDLE_VALUE;

  return TRUE;
}

static void
wing_input_stream_class_init (WingInputStreamClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GInputStreamClass *stream_class = G_INPUT_STREAM_CLASS (klass);

  gobject_class->finalize = wing_input_stream_finalize;
  gobject_class->get_property = wing_input_stream_get_property;
  gobject_class->set_property = wing_input_stream_set_property;

  stream_class->read_fn = wing_input_stream_read;
  stream_class->close_fn = wing_input_stream_close;

  /**
   * WingInputStream:handle:
   *
   * The handle that the stream reads from.
   */
  props[PROP_HANDLE] =
    g_param_spec_pointer ("handle",
                          "File handle",
                          "The file handle to read from",
                          G_PARAM_READWRITE |
                          G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS);

  /**
   * WingInputStream:close-handle:
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
wing_input_stream_init (WingInputStream *wing_stream)
{
  WingInputStreamPrivate *priv;

  priv = wing_input_stream_get_instance_private (wing_stream);
  priv->handle = NULL;
  priv->close_handle = TRUE;
  priv->overlap.hEvent = CreateEvent (NULL, TRUE, FALSE, NULL);
  g_return_val_if_fail (priv->overlap.hEvent != INVALID_HANDLE_VALUE, -1);
}


static gboolean
wing_input_stream_pollable_is_readable (GPollableInputStream *pollable)
{
  WingInputStream *wing_stream = WING_INPUT_STREAM (pollable);
  WingInputStreamPrivate *priv;

  priv = wing_input_stream_get_instance_private (wing_stream);

  return WaitForSingleObject (priv->overlapped.hEvent, 0) == WAIT_OBJECT_0;
}

static GSource *
wing_input_stream_pollable_create_source (GPollableInputStream *pollable,
                                          GCancellable         *cancellable)
{
  WingInputStream *wing_stream = WING_INPUT_STREAM (pollable);
  WingInputStreamPrivate *priv;
  GSource *handle_source, *pollable_source;

  priv = wing_input_stream_get_instance_private (wing_stream);

  pollable_source = g_pollable_source_new (G_OBJECT (input_stream));
  handle_source = wing_create_source (priv->overlapped.hEvent,
                                      G_IO_IN, cancellable);
  g_source_set_dummy_callback (handle_source);
  g_source_add_child_source (pollable_source, handle_source);
  g_source_unref (handle_source);

  return pollable_source;
}

static gssize
g_socket_input_stream_pollable_read_nonblocking (GPollableInputStream  *pollable,
                                                 void                  *buffer,
                                                 gsize                  count,
                                                 GError               **error)
{
  return read_internal (stream, buffer, count, FALSE, NULL, error);
}

static void
wing_input_stream_pollable_iface_init (GPollableInputStreamInterface *iface)
{
  iface->is_readable = wing_input_stream_pollable_is_readable;
  iface->create_source = wing_input_stream_pollable_create_source;
  iface->read_nonblocking = wing_input_stream_pollable_read_nonblocking;
}

/**
 * wing_input_stream_new:
 * @handle: a Win32 file handle
 * @close_handle: %TRUE to close the handle when done
 *
 * Creates a new #WingInputStream for the given @handle.
 *
 * If @close_handle is %TRUE, the handle will be closed
 * when the stream is closed.
 *
 * Note that "handle" here means a Win32 HANDLE, not a "file descriptor"
 * as used in the Windows C libraries.
 *
 * Returns: a new #WingInputStream
 **/
GInputStream *
wing_input_stream_new (void     *handle,
                          gboolean close_handle)
{
  g_return_val_if_fail (handle != NULL, NULL);

  return g_object_new (G_TYPE_WIN32_INPUT_STREAM,
                       "handle", handle,
                       "close-handle", close_handle,
                       NULL);
}

/**
 * wing_input_stream_set_close_handle:
 * @stream: a #WingInputStream
 * @close_handle: %TRUE to close the handle when done
 *
 * Sets whether the handle of @stream shall be closed
 * when the stream is closed.
 */
void
wing_input_stream_set_close_handle (WingInputStream *stream,
                                    gboolean         close_handle)
{
  WingInputStreamPrivate *priv;

  g_return_if_fail (G_IS_WIN32_INPUT_STREAM (stream));

  priv = wing_input_stream_get_instance_private (stream);

  close_handle = close_handle != FALSE;
  if (priv->close_handle != close_handle)
    {
      priv->close_handle = close_handle;
      g_object_notify (G_OBJECT (stream), "close-handle");
    }
}

/**
 * wing_input_stream_get_close_handle:
 * @stream: a #WingInputStream
 *
 * Returns whether the handle of @stream will be
 * closed when the stream is closed.
 *
 * Returns: %TRUE if the handle is closed when done
 */
gboolean
wing_input_stream_get_close_handle (WingInputStream *stream)
{
  WingInputStreamPrivate *priv;

  g_return_val_if_fail (G_IS_WIN32_INPUT_STREAM (stream), FALSE);

  priv = wing_input_stream_get_instance_private (stream);

  return priv->close_handle;
}

/**
 * wing_input_stream_get_handle:
 * @stream: a #WingInputStream
 *
 * Return the Windows file handle that the stream reads from.
 *
 * Returns: The file handle of @stream
 */
void *
wing_input_stream_get_handle (WingInputStream *stream)
{
  WingInputStreamPrivate *priv;

  g_return_val_if_fail (G_IS_WIN32_INPUT_STREAM (stream), NULL);

  priv = wing_input_stream_get_instance_private (stream);

  return priv->handle;
}
