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
 * Author: Silvio Lazzeretti <silviola@amazon.com>
 */

#include "winginputstream.h"
#include "wingutils.h"
#include "wingsource.h"

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
  DWORD64 current_offset;

  OVERLAPPED overlap;
} WingInputStreamPrivate;

enum {
  PROP_0,
  PROP_HANDLE,
  PROP_CLOSE_HANDLE,
  LAST_PROP
};

static GParamSpec *props[LAST_PROP];

G_DEFINE_TYPE_WITH_PRIVATE (WingInputStream, wing_input_stream, G_TYPE_INPUT_STREAM)

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
wing_input_stream_read (GInputStream  *stream,
                        void          *buffer,
                        gsize          count,
                        GCancellable  *cancellable,
                        GError       **error)
{
  WingInputStream *wing_stream;
  WingInputStreamPrivate *priv;
  BOOL res;
  DWORD nbytes, nread;
  OVERLAPPED overlap = { 0, };
  gssize retval = -1;

  wing_stream = WING_INPUT_STREAM (stream);
  priv = wing_input_stream_get_instance_private (wing_stream);

  if (g_cancellable_set_error_if_cancelled (cancellable, error))
    return -1;

  if (count > G_MAXINT)
    nbytes = G_MAXINT;
  else
    nbytes = count;

  overlap.hEvent = CreateEvent (NULL, FALSE, FALSE, NULL);
  g_return_val_if_fail (overlap.hEvent != NULL, -1);

  overlap.OffsetHigh = (DWORD)(priv->current_offset >> 32);
  overlap.Offset = (DWORD)priv->current_offset;
  
  res = ReadFile (priv->handle, buffer, nbytes, &nread, &overlap);
  if (res)
    {
      retval = nread;

      if (GetFileType (priv->handle) == FILE_TYPE_DISK)
        {
          priv->current_offset += nread;
        }
    }
  else
    {
      int errsv = GetLastError ();

      if (errsv == ERROR_IO_PENDING &&
          wing_overlap_wait_result (priv->handle,
                                    &overlap, &nread, cancellable))
        {
          retval = nread;

          if (GetFileType (priv->handle) == FILE_TYPE_DISK)
            {
              priv->current_offset += nread;
            }
            
          goto end;
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

          emsg = g_win32_error_message (errsv);
          g_set_error (error, G_IO_ERROR,
                       g_io_error_from_win32_error (errsv),
                       "Error reading from handle: %s",
                       emsg);
          g_free (emsg);
        }
    }

end:
  CloseHandle (overlap.hEvent);
  return retval;
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

static gboolean
read_async_ready (HANDLE   handle,
                  gpointer user_data)
{
  WingInputStream *wing_stream;
  WingInputStreamPrivate *priv;
  GTask *task = user_data;
  GCancellable *cancellable;
  DWORD nread;
  gboolean result;

  wing_stream = g_task_get_source_object (task);
  priv = wing_input_stream_get_instance_private (wing_stream);

  cancellable = g_task_get_cancellable (task);
  if (cancellable != NULL && g_cancellable_is_cancelled (cancellable))
    {
      CancelIo (priv->handle);
      ResetEvent (priv->overlap.hEvent);

      g_task_return_new_error (task, G_IO_ERROR,
                               G_IO_ERROR_CANCELLED,
                               "Error reading from handle: the operation is cancelled");
      g_object_unref (task);

      return G_SOURCE_REMOVE;
    }

  result = GetOverlappedResult (priv->overlap.hEvent, &priv->overlap, &nread, FALSE);
  if (!result && GetLastError () == ERROR_IO_INCOMPLETE)
    {
      /* Try again to wait for the event to get ready */
      ResetEvent (priv->overlap.hEvent);
      return G_SOURCE_CONTINUE;
    }

  ResetEvent (priv->overlap.hEvent);
  
  if (GetFileType (priv->handle) == FILE_TYPE_DISK)
    {
      priv->current_offset += nread;
    }

  g_task_return_int (task, nread);
  g_object_unref (task);

  return G_SOURCE_REMOVE;
}

static void
wing_input_stream_read_async (GInputStream        *stream,
                              void                *buffer,
                              gsize                count,
                              int                  io_priority,
                              GCancellable        *cancellable,
                              GAsyncReadyCallback  callback,
                              gpointer             user_data)
{
  WingInputStream *wing_stream;
  WingInputStreamPrivate *priv;
  BOOL res;
  DWORD nbytes, nread;
  int errsv;
  GTask *task;
  gchar *emsg;

  wing_stream = WING_INPUT_STREAM (stream);
  priv = wing_input_stream_get_instance_private (wing_stream);

  task = g_task_new (stream, cancellable, callback, user_data);

  if (count > G_MAXINT)
    nbytes = G_MAXINT;
  else
    nbytes = count;

  ResetEvent (priv->overlap.hEvent);
  priv->overlap.OffsetHigh = (DWORD)(priv->current_offset >> 32);
  priv->overlap.Offset = (DWORD)priv->current_offset;

  res = ReadFile (priv->handle, buffer, nbytes, &nread, &priv->overlap);
  if (res)
    {
      ResetEvent (priv->overlap.hEvent);
      g_task_return_int (task, nread);
      g_object_unref (task);

      if (GetFileType (priv->handle) == FILE_TYPE_DISK)
        {
          priv->current_offset += nread;
        }
      
      return;
    }

  errsv = GetLastError ();

  if (errsv == ERROR_IO_PENDING)
    {
      GSource *handle_source;

      handle_source = wing_create_source (priv->overlap.hEvent, G_IO_IN, cancellable);
      g_task_attach_source (task, handle_source,
                            (GSourceFunc)read_async_ready);
      g_source_unref (handle_source);
      
      return;
    }

  if (errsv == ERROR_MORE_DATA)
    {
      /* If a named pipe is being read in message mode and the
       * next message is longer than the nNumberOfBytesToRead
       * parameter specifies, ReadFile returns FALSE and
       * GetLastError returns ERROR_MORE_DATA */
      ResetEvent (priv->overlap.hEvent);
      g_task_return_int (task, nread);
      return;
    }

  if (errsv == ERROR_HANDLE_EOF ||
      errsv == ERROR_BROKEN_PIPE)
    {
      /* TODO: the other end of a pipe may call the WriteFile
       * function with nNumberOfBytesToWrite set to zero. In this
       * case, it's not possible for the caller to know if it's
       * broken pipe or a read of 0. Perhaps we should add a
       * is_broken flag for this win32 case.. */
      g_task_return_int (task, 0);
      return;
    }

  emsg = g_win32_error_message (errsv);
  g_task_report_new_error (stream, callback, user_data,
                           wing_input_stream_read_async,
                           G_IO_ERROR, g_io_error_from_win32_error (errsv),
                           "Error reading from handle: %s",
                           emsg);
  g_free (emsg);
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
  stream_class->read_async = wing_input_stream_read_async;

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
  g_return_if_fail (priv->overlap.hEvent != INVALID_HANDLE_VALUE);
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

  return g_object_new (WING_TYPE_INPUT_STREAM,
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

  g_return_if_fail (WING_IS_INPUT_STREAM (stream));

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

  g_return_val_if_fail (WING_IS_INPUT_STREAM (stream), FALSE);

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

  g_return_val_if_fail (WING_IS_INPUT_STREAM (stream), NULL);

  priv = wing_input_stream_get_instance_private (stream);

  return priv->handle;
}
