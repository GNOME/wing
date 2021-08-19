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

#include "wingiocpoutputstream.h"
#include "wingutils.h"

#include <windows.h>

 /**
  * SECTION:wingiocpoutputstream
  * @short_description: Streaming output operations for Windows file handles using I/O completion ports
  * @see_also: #GOutputStream
  *
  * #WingIocpOutputStream implements #GOutputStream for writing to a
  * Windows file handle.
  */

typedef struct {
  gboolean close_handle;
  WingThreadPoolIo *thread_pool_io;
} WingIocpOutputStreamPrivate;

enum {
  PROP_0,
  PROP_CLOSE_HANDLE,
  PROP_THREADPOOL_IO,
  LAST_PROP
};

static GParamSpec *props[LAST_PROP];

G_DEFINE_TYPE_WITH_PRIVATE (WingIocpOutputStream, wing_iocp_output_stream, G_TYPE_OUTPUT_STREAM)

static void
wing_iocp_output_stream_finalize (GObject *object)
{
  WingIocpOutputStream *wing_stream;
  WingIocpOutputStreamPrivate *priv;

  wing_stream = WING_IOCP_OUTPUT_STREAM (object);
  priv = wing_iocp_output_stream_get_instance_private (wing_stream);

  if (priv->thread_pool_io)
    {
      if (priv->close_handle)
        wing_thread_pool_io_close_handle (priv->thread_pool_io, NULL);

      wing_thread_pool_io_unref (priv->thread_pool_io);
    }

  G_OBJECT_CLASS (wing_iocp_output_stream_parent_class)->finalize (object);
}

static void
wing_iocp_output_stream_set_property (GObject         *object,
                                      guint            prop_id,
                                      const GValue    *value,
                                      GParamSpec      *pspec)
{
  WingIocpOutputStream *wing_stream;
  WingIocpOutputStreamPrivate *priv;

  wing_stream = WING_IOCP_OUTPUT_STREAM (object);
  priv = wing_iocp_output_stream_get_instance_private (wing_stream);

  switch (prop_id)
    {
    case PROP_CLOSE_HANDLE:
      priv->close_handle = g_value_get_boolean (value);
      break;

    case PROP_THREADPOOL_IO:
      priv->thread_pool_io = g_value_dup_boxed (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
wing_iocp_output_stream_get_property (GObject    *object,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
  WingIocpOutputStream *wing_stream;
  WingIocpOutputStreamPrivate *priv;

  wing_stream = WING_IOCP_OUTPUT_STREAM (object);
  priv = wing_iocp_output_stream_get_instance_private (wing_stream);

  switch (prop_id)
    {
    case PROP_CLOSE_HANDLE:
      g_value_set_boolean (value, priv->close_handle);
      break;
    case PROP_THREADPOOL_IO:
      g_value_set_boxed (value, priv->thread_pool_io);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static gboolean
wing_iocp_output_stream_close (GOutputStream  *stream,
                               GCancellable   *cancellable,
                               GError        **error)
{
  WingIocpOutputStream *wing_stream;
  WingIocpOutputStreamPrivate *priv;
  BOOL res;

  wing_stream = WING_IOCP_OUTPUT_STREAM (stream);
  priv = wing_iocp_output_stream_get_instance_private (wing_stream);

  if (!priv->close_handle)
    return TRUE;

  res = wing_thread_pool_io_close_handle (priv->thread_pool_io, error);

  if (res)
    g_clear_pointer (&priv->thread_pool_io, wing_thread_pool_io_unref);

  return res;
}

static void
threadpool_io_completion (PTP_CALLBACK_INSTANCE instance,
                          PVOID                 ctxt,
                          PVOID                 overlapped,
                          ULONG                 result,
                          ULONG_PTR             number_of_bytes_transferred,
                          PTP_IO                threadpool_io,
                          gpointer              user_data)
{
  WingOverlappedData *overlapped_data = overlapped;
  GTask *task;

  task = G_TASK (user_data);
  if (result == NO_ERROR)
    {
      g_task_return_int (task, number_of_bytes_transferred);
    }
  else
    {
      gchar *emsg = g_win32_error_message (result);

      g_task_return_new_error (task, G_IO_ERROR,
                               g_io_error_from_win32_error (result),
                               "Error writing to handle: %s",
                               emsg);
      g_free (emsg);
    }

  if (g_task_get_cancellable (task) != NULL)
    g_cancellable_disconnect (g_task_get_cancellable (task),
                              overlapped_data->cancellable_id);
  g_object_unref (task);
  g_slice_free (WingOverlappedData, overlapped);
}

static void
on_cancellable_cancelled (GCancellable *cancellable,
                          gpointer      user_data)
{
  WingIocpOutputStream *wing_stream;
  WingIocpOutputStreamPrivate *priv;

  wing_stream = WING_IOCP_OUTPUT_STREAM (user_data);
  priv = wing_iocp_output_stream_get_instance_private (wing_stream);

  CancelIo (wing_thread_pool_get_handle (priv->thread_pool_io));
}

static void
wing_iocp_output_stream_write_async (GOutputStream       *stream,
                                     const void          *buffer,
                                     gsize                count,
                                     int                  io_priority,
                                     GCancellable        *cancellable,
                                     GAsyncReadyCallback  callback,
                                     gpointer             user_data)
{
  GTask *task;
  WingOverlappedData *overlapped;
  WingIocpOutputStream *wing_stream;
  WingIocpOutputStreamPrivate *priv;
  HANDLE handle;

  wing_stream = WING_IOCP_OUTPUT_STREAM (stream);
  priv = wing_iocp_output_stream_get_instance_private (wing_stream);

  task = g_task_new (stream, cancellable, callback, user_data);
  g_task_set_priority (task, io_priority);

  if (g_task_return_error_if_cancelled (task))
    {
      g_object_unref (task);
      return;
    }

  handle = wing_thread_pool_get_handle (priv->thread_pool_io);
  if (handle == INVALID_HANDLE_VALUE)
    {
      g_task_return_new_error (task, G_IO_ERROR,
                               G_IO_ERROR_CLOSED,
                               "Error writing to handle: the handle is closed");
      g_object_unref (task);

      return;
    }

  overlapped = g_slice_new0 (WingOverlappedData);
  overlapped->user_data = task;
  overlapped->callback = threadpool_io_completion;

  if (g_task_get_cancellable (task) != NULL)
    overlapped->cancellable_id = g_cancellable_connect (g_task_get_cancellable (task),
                                                        G_CALLBACK (on_cancellable_cancelled),
                                                        wing_stream, NULL);

  wing_thread_pool_io_start (priv->thread_pool_io);

  if (!WriteFile (handle, buffer, (DWORD)count, NULL, (OVERLAPPED *)overlapped))
    {
      int errsv;

      errsv = GetLastError ();
      if (errsv != NO_ERROR && errsv != ERROR_IO_PENDING)
        {
          gchar *emsg = g_win32_error_message (errsv);

          g_task_return_new_error (task, G_IO_ERROR,
                                   g_io_error_from_win32_error (errsv),
                                   "Error writing to handle: %s",
                                   emsg);
          g_free (emsg);

          wing_thread_pool_io_cancel (priv->thread_pool_io);

          if (g_task_get_cancellable (task) != NULL)
            g_cancellable_disconnect (g_task_get_cancellable (task),
                                      overlapped->cancellable_id);
          g_object_unref (task);
          g_slice_free (WingOverlappedData, overlapped);
        }
    }
}

static gssize
wing_iocp_output_stream_write (GOutputStream  *stream,
                               const void     *buffer,
                               gsize           count,
                               GCancellable   *cancellable,
                               GError        **error)
{
  WingIocpOutputStream *wing_stream;
  WingIocpOutputStreamPrivate *priv;
  BOOL res;
  DWORD nbytes, nwritten;
  OVERLAPPED overlap = { 0, };
  gssize retval = -1;
  HANDLE handle;

  wing_stream = WING_IOCP_OUTPUT_STREAM (stream);
  priv = wing_iocp_output_stream_get_instance_private (wing_stream);

  if (g_cancellable_set_error_if_cancelled (cancellable, error))
    return -1;

  if (count > G_MAXINT)
    nbytes = G_MAXINT;
  else
    nbytes = (DWORD) count;

  overlap.hEvent = CreateEvent (NULL, FALSE, FALSE, NULL);
  g_return_val_if_fail (overlap.hEvent != NULL, -1);

  handle = wing_thread_pool_get_handle (priv->thread_pool_io);

  /* This prevents the I/O completion port to be notified.
   * It is described in the documentation of the GetQueuedCompletionStatus
   * function, in the section related to the lpOverlapped parameter
   */
#if GLIB_SIZEOF_VOID_P == 8
   overlap.hEvent = (HANDLE) ((gint64) overlap.hEvent | 0x1);
#else
  overlap.hEvent = (HANDLE) ((gint) overlap.hEvent | 0x1);
#endif

  res = WriteFile (handle, buffer, nbytes, &nwritten, &overlap);
  if (res)
    retval = nwritten;
  else
    {
      int errsv = GetLastError ();

      if (errsv == ERROR_IO_PENDING &&
          wing_overlap_wait_result (handle,
                                    &overlap, &nwritten,
                                    cancellable))
        {
          retval = nwritten;
          goto end;
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
  CloseHandle (overlap.hEvent);

  return retval;
}

static void
wing_iocp_output_stream_class_init (WingIocpOutputStreamClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GOutputStreamClass *stream_class = G_OUTPUT_STREAM_CLASS (klass);

  gobject_class->finalize = wing_iocp_output_stream_finalize;
  gobject_class->get_property = wing_iocp_output_stream_get_property;
  gobject_class->set_property = wing_iocp_output_stream_set_property;

  stream_class->close_fn = wing_iocp_output_stream_close;
  stream_class->write_async = wing_iocp_output_stream_write_async;
  stream_class->write_fn = wing_iocp_output_stream_write;

  /**
   * WingIocpOutputStream:close-handle:
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

  /**
   * WingNamedPipeConnection:threadpool-io:
   *
   * The threadpool I/O object, returned by CreateThreadpoolIo, used to
   * perform async I/O with completion ports.
   */
  props[PROP_THREADPOOL_IO] =
    g_param_spec_boxed ("threadpool-io",
                        "Threadpool I/O object",
                        "The threadpool I/O object used to perform async I / O with completion ports",
                        WING_TYPE_THREAD_POOL_IO,
                        G_PARAM_READWRITE |
                        G_PARAM_CONSTRUCT_ONLY |
                        G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class, LAST_PROP, props);
}

static void
wing_iocp_output_stream_init (WingIocpOutputStream *wing_stream)
{
  WingIocpOutputStreamPrivate *priv;

  priv = wing_iocp_output_stream_get_instance_private (wing_stream);
  
  priv->close_handle = TRUE;
}

/**
 * wing_iocp_output_stream_new:
 * @handle: a Win32 file handle
 * @close_handle: %TRUE to close the handle when done
 * @threadpool_io: a pointer to the TP_IO structure returned by CreateThreadpoolIo
 *
 * Creates a new #WingIocpOutputStream for the given @handle.
 *
 * If @close_handle, is %TRUE, the handle and the threadpool-io will be closed
 * when the output stream is destroyed.
 *
 * Note that "handle" here means a Win32 HANDLE, not a "file descriptor"
 * as used in the Windows C libraries.
 *
 * Returns: a new #GOutputStream
**/
GOutputStream *
wing_iocp_output_stream_new (gboolean          close_handle,
                             WingThreadPoolIo *threadpool_io)
{
    g_return_val_if_fail (threadpool_io != NULL, NULL);

  return g_object_new (WING_TYPE_IOCP_OUTPUT_STREAM,
                       "close-handle", close_handle,
                       "threadpool-io", threadpool_io,
                       NULL);
}

/**
 * wing_iocp_output_stream_set_close_handle:
 * @stream: a #WingIocpOutputStream
 * @close_handle: %TRUE to close the handle when done
 *
 * Sets whether the handle of @stream shall be closed when the stream
 * is closed.
 */
void
wing_iocp_output_stream_set_close_handle (WingIocpOutputStream *stream,
                                          gboolean              close_handle)
{
  WingIocpOutputStreamPrivate *priv;

  g_return_if_fail (WING_IS_IOCP_OUTPUT_STREAM (stream));

  priv = wing_iocp_output_stream_get_instance_private (stream);

  close_handle = close_handle != FALSE;
  if (priv->close_handle != close_handle)
    {
      priv->close_handle = close_handle;
      g_object_notify (G_OBJECT (stream), "close-handle");
    }
}

/**
 * wing_iocp_output_stream_get_close_handle:
 * @stream: a #WingIocpOutputStream
 *
 * Returns whether the handle of @stream will be closed when the
 * stream is closed.
 *
 * Returns: %TRUE if the handle is closed when done
 */
gboolean
wing_iocp_output_stream_get_close_handle (WingIocpOutputStream *stream)
{
  WingIocpOutputStreamPrivate *priv;

  g_return_val_if_fail (WING_IS_IOCP_OUTPUT_STREAM (stream), FALSE);

  priv = wing_iocp_output_stream_get_instance_private (stream);

  return priv->close_handle;
}

/**
 * wing_iocp_output_stream_get_handle:
 * @stream: a #WingIocpOutputStream
 *
 * Return the Windows handle that the stream writes to.
 *
 * Returns: The handle descriptor of @stream
 */
void *
wing_iocp_output_stream_get_handle (WingIocpOutputStream *stream)
{
  WingIocpOutputStreamPrivate *priv;

  g_return_val_if_fail (WING_IS_IOCP_OUTPUT_STREAM (stream), NULL);

  priv = wing_iocp_output_stream_get_instance_private (stream);

  return wing_thread_pool_get_handle (priv->thread_pool_io);
}
