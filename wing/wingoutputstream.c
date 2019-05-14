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

G_DEFINE_TYPE_WITH_PRIVATE (WingOutputStream, wing_output_stream, G_TYPE_OUTPUT_STREAM)

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
wing_output_stream_write (GOutputStream  *stream,
                          const void     *buffer,
                          gsize           count,
                          GCancellable   *cancellable,
                          GError        **error)
{
  WingOutputStream *wing_stream;
  WingOutputStreamPrivate *priv;
  BOOL res;
  DWORD nbytes, nwritten;
  OVERLAPPED overlap = { 0, };
  gssize retval = -1;

  wing_stream = WING_OUTPUT_STREAM (stream);
  priv = wing_output_stream_get_instance_private (wing_stream);

  if (g_cancellable_set_error_if_cancelled (cancellable, error))
    return -1;

  if (count > G_MAXINT)
    nbytes = G_MAXINT;
  else
    nbytes = count;

  overlap.hEvent = CreateEvent (NULL, FALSE, FALSE, NULL);
  g_return_val_if_fail (overlap.hEvent != NULL, -1);

  res = WriteFile (priv->handle, buffer, nbytes, &nwritten, &overlap);
  if (res)
    retval = nwritten;
  else
    {
      int errsv = GetLastError ();

      if (errsv == ERROR_IO_PENDING &&
          wing_overlap_wait_result (priv->handle,
                                    &overlap, &nwritten, cancellable))
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

static gboolean
write_async_ready (HANDLE   handle,
                   gpointer user_data)
{
  WingOutputStream *wing_stream;
  WingOutputStreamPrivate *priv;
  GTask *task = user_data;
  GCancellable *cancellable;
  DWORD nwritten;
  gboolean result;

  wing_stream = g_task_get_source_object (task);
  priv = wing_output_stream_get_instance_private (wing_stream);

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

  result = GetOverlappedResult (priv->overlap.hEvent, &priv->overlap, &nwritten, FALSE);
  if (!result && GetLastError () == ERROR_IO_INCOMPLETE)
    {
      /* Try again to wait for the event to get ready */
      ResetEvent (priv->overlap.hEvent);
      return G_SOURCE_CONTINUE;
    }

  ResetEvent (priv->overlap.hEvent);

  g_task_return_int (task, nwritten);
  g_object_unref (task);

  return G_SOURCE_REMOVE;
}

static void
wing_output_stream_write_async (GOutputStream            *stream,
                                const void               *buffer,
                                gsize                     count,
                                int                       io_priority,
                                GCancellable             *cancellable,
                                GAsyncReadyCallback       callback,
                                gpointer                  user_data)
{
  WingOutputStream *wing_stream;
  WingOutputStreamPrivate *priv;
  BOOL res;
  DWORD nbytes, nwritten;
  int errsv;
  GTask *task;
  gchar *emsg;

  wing_stream = WING_OUTPUT_STREAM (stream);
  priv = wing_output_stream_get_instance_private (wing_stream);

  task = g_task_new (stream, cancellable, callback, user_data);

  if (count > G_MAXINT)
    nbytes = G_MAXINT;
  else
    nbytes = count;

  ResetEvent (priv->overlap.hEvent);

  res = WriteFile (priv->handle, buffer, nbytes, &nwritten, &priv->overlap);
  if (res)
    {
      ResetEvent (priv->overlap.hEvent);
      g_task_return_int (task, nwritten);
      g_object_unref (task);
      return;
    }

  errsv = GetLastError ();

  if (errsv == ERROR_IO_PENDING)
    {
      GSource *handle_source;

      handle_source = wing_create_source (priv->overlap.hEvent, G_IO_IN, cancellable);
      g_task_attach_source (task, handle_source,
                            (GSourceFunc)write_async_ready);
      g_source_unref (handle_source);
      return;
    }

  if (errsv == ERROR_HANDLE_EOF ||
      errsv == ERROR_BROKEN_PIPE)
    {
      g_task_return_int (task, 0);
      return;
    }

  emsg = g_win32_error_message (errsv);
  g_task_report_new_error (stream, callback, user_data,
                           wing_output_stream_write_async,
                           G_IO_ERROR, g_io_error_from_win32_error (errsv),
                           "Error writing to handle: %s",
                           emsg);
  g_free (emsg);
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
  stream_class->write_async = wing_output_stream_write_async;

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
