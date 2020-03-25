/* GIO - GLib Input, Output and Streaming Library
 *
 * Copyright (C) 2016 NICE s.r.l.
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
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "wingnamedpipeclient.h"
#include "wingnamedpipeconnection.h"

#include <windows.h>

/**
 * SECTION:wingnamedpipeclient
 * @short_description: Helper for connecting to a named pipe
 * @include: gio/gio.h
 * @see_also: #WingNamedPipeListener
 *
 * #WingNamedPipeClient is a lightweight high-level utility class for
 * connecting to a named pipe.
 *
 * You create a #WingNamedPipeClient object, set any options you want, and then
 * call a sync or async connect operation, which returns a #WingNamedPipeConnection
 * on success.
 *
 * As #WingNamedPipeClient is a lightweight object, you don't need to
 * cache it. You can just create a new one any time you need one.
 */

typedef struct
{
  guint timeout;
  gboolean use_iocp;
} WingNamedPipeClientPrivate;

enum
{
  PROP_0,
  PROP_TIMEOUT,
  PROP_USE_IOCP,
  LAST_PROP
};

static GParamSpec *props[LAST_PROP];

G_DEFINE_TYPE_WITH_PRIVATE (WingNamedPipeClient, wing_named_pipe_client, G_TYPE_OBJECT)

static void
wing_named_pipe_client_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
  WingNamedPipeClient *client = WING_NAMED_PIPE_CLIENT (object);
  WingNamedPipeClientPrivate *priv;

  priv = wing_named_pipe_client_get_instance_private (client);

  switch (prop_id)
    {
    case PROP_TIMEOUT:
      g_value_set_uint (value, priv->timeout);
      break;

    case PROP_USE_IOCP:
        g_value_set_boolean (value, priv->use_iocp);
        break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
wing_named_pipe_client_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
  WingNamedPipeClient *client = WING_NAMED_PIPE_CLIENT (object);
  WingNamedPipeClientPrivate *priv;

  priv = wing_named_pipe_client_get_instance_private (client);

  switch (prop_id)
    {
    case PROP_TIMEOUT:
      priv->timeout = g_value_get_uint (value);
      break;

    case PROP_USE_IOCP:
      priv->use_iocp = g_value_get_boolean (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
wing_named_pipe_client_class_init (WingNamedPipeClientClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = wing_named_pipe_client_get_property;
  object_class->set_property = wing_named_pipe_client_set_property;

  props[PROP_TIMEOUT] =
    g_param_spec_uint ("timeout",
                       "Timeout",
                       "The timeout in milliseconds to wait",
                       0,
                       NMPWAIT_WAIT_FOREVER,
                       NMPWAIT_WAIT_FOREVER,
                       G_PARAM_READWRITE |
                       G_PARAM_CONSTRUCT |
                       G_PARAM_STATIC_STRINGS);

  /**
   * WingNamedPipeConnection:use-iocp:
   *
   * Whether to use I/O completion port for async I/O.
   */
  props[PROP_USE_IOCP] =
    g_param_spec_boolean ("use-iocp",
                          "Use I/O completion port",
                          "Whether to use I/O completion port for async I/O",
                          FALSE,
                          G_PARAM_READABLE |
                          G_PARAM_WRITABLE |
                          G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, LAST_PROP, props);
}

static void
wing_named_pipe_client_init (WingNamedPipeClient *self)
{
}

/**
 * wing_named_pipe_client_new:
 *
 * Creates a new #WingNamedPipeClient.
 *
 * Returns: a #WingNamedPipeClient.
 *     Free the returned object with g_object_unref().
 */
WingNamedPipeClient *
wing_named_pipe_client_new (void)
{
  return g_object_new (WING_TYPE_NAMED_PIPE_CLIENT,
                       NULL);
}

/**
 * wing_named_pipe_client_connect:
 * @client: a #WingNamedPipeClient.
 * @pipe_name: a pipe name.
 * @flags: requested access to the pipe (read, write, both or none).
 * @cancellable: (allow-none): optional #GCancellable object, %NULL to ignore.
 * @error: #GError for error reporting, or %NULL to ignore.
 *
 * Waits until the pipe is available or the default timeout experies.
 *
 * When the pipe is available, a new #WingNamedPipeConnection is constructed
 * and returned.  The caller owns this new object and must drop their
 * reference to it when finished with it.
 *
 * Returns: (transfer full): a #WingNamedPipeConnection on success, %NULL on error.
 */
WingNamedPipeConnection *
wing_named_pipe_client_connect (WingNamedPipeClient     *client,
                                const gchar             *pipe_name,
                                WingNamedPipeClientFlags flags,
                                GCancellable            *cancellable,
                                GError                  **error)
{
  WingNamedPipeClientPrivate *priv;
  HANDLE handle = INVALID_HANDLE_VALUE;
  WingNamedPipeConnection *connection = NULL;
  gunichar2 *pipe_namew;
  DWORD pipe_flags = 0;

  g_return_val_if_fail (WING_IS_NAMED_PIPE_CLIENT (client), NULL);
  g_return_val_if_fail (pipe_name != NULL, NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  priv = wing_named_pipe_client_get_instance_private (client);

  if (g_cancellable_set_error_if_cancelled (cancellable, error))
    return NULL;

  pipe_namew = g_utf8_to_utf16 (pipe_name, -1, NULL, NULL, NULL);

  while (TRUE)
    {
      if (g_cancellable_set_error_if_cancelled (cancellable, error))
        break;

      if (flags & WING_NAMED_PIPE_CLIENT_GENERIC_READ)
        pipe_flags |= GENERIC_READ;

      if (flags & WING_NAMED_PIPE_CLIENT_GENERIC_WRITE)
        pipe_flags |= GENERIC_WRITE;

      handle = CreateFileW (pipe_namew,
                            pipe_flags,
                            FILE_SHARE_READ | FILE_SHARE_WRITE,
                            NULL,
                            OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
                            NULL);

      if (handle != INVALID_HANDLE_VALUE)
        break;

      if (GetLastError () != ERROR_PIPE_BUSY)
        {
          int errsv;
          gchar *err;

          errsv = GetLastError ();
          err = g_win32_error_message (errsv);
          g_set_error (error, G_IO_ERROR,
                       g_io_error_from_win32_error (errsv),
                       "Could not create file for named pipe '%s': %s",
                       pipe_name, err);
          g_free (err);
          break;
        }

      if (!WaitNamedPipeW (pipe_namew, priv->timeout))
        {
          int errsv;
          gchar *err;

          errsv = GetLastError ();
          err = g_win32_error_message (errsv);
          g_set_error (error, G_IO_ERROR,
                       g_io_error_from_win32_error (errsv),
                       "Failed to wait for named pipe '%s': %s",
                       pipe_name, err);
          g_free (err);
          break;
        }
    }

  g_free (pipe_namew);

  if (handle != INVALID_HANDLE_VALUE)
    connection = g_object_new (WING_TYPE_NAMED_PIPE_CONNECTION,
                               "pipe-name", pipe_name,
                               "handle", handle,
                               "close-handle", TRUE,
                               "use-iocp", priv->use_iocp,
                               NULL);

  return connection;
}

typedef struct
{
  gchar                      *pipe_name;
  WingNamedPipeClientFlags    flags;
} PipeAsyncTaskData;

static PipeAsyncTaskData*
create_pipe_async_task_data(const gchar                 *pipe_name,
                            WingNamedPipeClientFlags     flags)
{
  PipeAsyncTaskData *task_data = g_new0(PipeAsyncTaskData, 1);
  task_data->pipe_name = g_strdup(pipe_name);
  task_data->flags = flags;
  return task_data;
}

static void
free_pipe_async_task_data(PipeAsyncTaskData *task_data)
{
  g_free(task_data->pipe_name);
  g_free(task_data);
}

static void
client_connect_thread (GTask        *task,
                       gpointer      source_object,
                       gpointer      task_data,
                       GCancellable *cancellable)
{
  WingNamedPipeClient *client = WING_NAMED_PIPE_CLIENT (source_object);
  PipeAsyncTaskData *data = (PipeAsyncTaskData*)task_data;
  WingNamedPipeConnection *connection;
  GError *error = NULL;

  connection = wing_named_pipe_client_connect (client, (const gchar *)data->pipe_name,
                                               data->flags,
                                               cancellable, &error);

  if (connection == NULL)
    g_task_return_error (task, error);
  else
    g_task_return_pointer (task, connection, (GDestroyNotify)g_object_unref);

  g_object_unref(task);
}

/**
 * wing_named_pipe_client_connect_async:
 * @client: a #WingNamedPipeClient
 * @pipe_name: a pipe name.
 * @cancellable: (allow-none): a #GCancellable, or %NULL
 * @callback: (scope async): a #GAsyncReadyCallback
 * @user_data: (closure): user data for the callback
 *
 * This is the asynchronous version of wing_named_pipe_client_connect().
 *
 * When the operation is finished @callback will be
 * called. You can then call wing_named_pipe_client_connect_finish() to get
 * the result of the operation.
 */
void
wing_named_pipe_client_connect_async (WingNamedPipeClient       *client,
                                      const gchar               *pipe_name,
                                      WingNamedPipeClientFlags   flags,
                                      GCancellable              *cancellable,
                                      GAsyncReadyCallback        callback,
                                      gpointer                   user_data)
{
  GTask *task;

  g_return_if_fail (WING_IS_NAMED_PIPE_CLIENT (client));
  g_return_if_fail (pipe_name != NULL);

  task = g_task_new (client, cancellable, callback, user_data);
  g_task_set_task_data (task, create_pipe_async_task_data(pipe_name, flags),
                        (GDestroyNotify)free_pipe_async_task_data);

  g_task_run_in_thread (task, client_connect_thread);
}

/**
 * wing_named_pipe_client_connect_finish:
 * @client: a #WingNamedPipeClient.
 * @result: a #GAsyncResult.
 * @error: a #GError location to store the error occurring, or %NULL to
 * ignore.
 *
 * Finishes an async connect operation. See wing_named_pipe_client_connect_async()
 *
 * Returns: (transfer full): a #WingNamedPipeConnection on success, %NULL on error.
 */
WingNamedPipeConnection *
wing_named_pipe_client_connect_finish (WingNamedPipeClient  *client,
                                       GAsyncResult         *result,
                                       GError              **error)
{
  g_return_val_if_fail (WING_IS_NAMED_PIPE_CLIENT (client), NULL);
  g_return_val_if_fail (g_task_is_valid (result, client), NULL);

  return g_task_propagate_pointer (G_TASK (result), error);
}

void
wing_named_pipe_client_set_use_iocp (WingNamedPipeClient *client,
                                     gboolean             use_iocp)
{
  WingNamedPipeClientPrivate *priv;

  g_return_if_fail (WING_IS_NAMED_PIPE_CLIENT (client));

  priv = wing_named_pipe_client_get_instance_private (client);

  if (priv->use_iocp != use_iocp)
    {
      priv->use_iocp = use_iocp;
      g_object_notify_by_pspec (G_OBJECT (client), props[PROP_USE_IOCP]);
    }
}
