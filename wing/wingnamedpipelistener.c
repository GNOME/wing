/*
 * Copyright (C) 2011 Red Hat, Inc.
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


#include "wingnamedpipelistener.h"
#include "wingnamedpipeconnection.h"
#include "wingsource.h"

#include <windows.h>
#include <sddl.h>

#define DEFAULT_PIPE_BUF_SIZE 8192

typedef struct
{
  gchar *pipe_name;
  gunichar2 *pipe_namew;
  gchar *security_descriptor;
  gunichar2 *security_descriptorw;
  HANDLE handle;
  OVERLAPPED overlapped;
  gboolean already_connected;
} PipeData;

typedef struct
{
  PipeData *named_pipe;
  gboolean use_iocp;
} WingNamedPipeListenerPrivate;

enum
{
  PROP_0,
  PROP_USE_IOCP,
  LAST_PROP
};

static GParamSpec *props[LAST_PROP];

G_DEFINE_TYPE_WITH_PRIVATE (WingNamedPipeListener, wing_named_pipe_listener, G_TYPE_OBJECT)

static GQuark source_quark = 0;

static PipeData *
pipe_data_new (const gchar *pipe_name,
               const gchar *security_descriptor)
{
  PipeData *data;

  data = g_slice_new0 (PipeData);
  data->pipe_name = g_strdup (pipe_name);
  data->pipe_namew = g_utf8_to_utf16 (pipe_name, -1, NULL, NULL, NULL);
  data->security_descriptor = g_strdup (security_descriptor);
  data->security_descriptorw = security_descriptor != NULL ? g_utf8_to_utf16 (security_descriptor, -1, NULL, NULL, NULL) : NULL;
  data->handle = INVALID_HANDLE_VALUE;
  data->overlapped.hEvent = CreateEvent (NULL, /* default security attribute */
                                         TRUE, /* manual-reset event */
                                         TRUE, /* initial state = signaled */
                                         NULL); /* unnamed event object */

  return data;
}

static void
pipe_data_free (PipeData *data)
{
  g_free (data->pipe_name);
  g_free (data->pipe_namew);
  g_free (data->security_descriptor);
  g_free (data->security_descriptorw);
  if (data->handle != INVALID_HANDLE_VALUE)
    CloseHandle (data->handle);
  CloseHandle (data->overlapped.hEvent);
  g_slice_free (PipeData, data);
}

static void
wing_named_pipe_listener_set_property (GObject      *object,
                                       guint         prop_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
  WingNamedPipeListener *listener = WING_NAMED_PIPE_LISTENER (object);
  WingNamedPipeListenerPrivate *priv;

  priv = wing_named_pipe_listener_get_instance_private (listener);

  switch (prop_id)
  {
  case PROP_USE_IOCP:
    priv->use_iocp = g_value_get_boolean (value);
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    break;
  }
}

static void
wing_named_pipe_listener_get_property (GObject    *object,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
  WingNamedPipeListener *listener = WING_NAMED_PIPE_LISTENER (object);
  WingNamedPipeListenerPrivate *priv;

  priv = wing_named_pipe_listener_get_instance_private (listener);

  switch (prop_id)
  {
  case PROP_USE_IOCP:
      g_value_set_boolean (value, priv->use_iocp);
      break;

  default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
wing_named_pipe_listener_finalize (GObject *object)
{
  WingNamedPipeListener *listener = WING_NAMED_PIPE_LISTENER (object);
  WingNamedPipeListenerPrivate *priv;

  priv = wing_named_pipe_listener_get_instance_private (listener);

  g_clear_pointer (&priv->named_pipe, pipe_data_free);

  G_OBJECT_CLASS (wing_named_pipe_listener_parent_class)->finalize (object);
}

static void
wing_named_pipe_listener_class_init (WingNamedPipeListenerClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = wing_named_pipe_listener_finalize;
  gobject_class->get_property = wing_named_pipe_listener_get_property;
  gobject_class->set_property = wing_named_pipe_listener_set_property;

  source_quark = g_quark_from_static_string ("g-win32-named-pipe-listener-source");

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

  g_object_class_install_properties (gobject_class, LAST_PROP, props);
}

static void
wing_named_pipe_listener_init (WingNamedPipeListener *listener)
{
}

/**
 * wing_named_pipe_listener_new:
 *
 * Creates a new #WingNamedPipeListener.
 *
 * Returns: (transfer full): a new #WingNamedPipeListener.
 */
WingNamedPipeListener *
wing_named_pipe_listener_new (void)
{
  return g_object_new (WING_TYPE_NAMED_PIPE_LISTENER, NULL);
}

static gboolean
create_pipe_from_pipe_data (PipeData  *pipe_data,
                            gboolean   protect_first_instance,
                            GError   **error)
{
  SECURITY_ATTRIBUTES sa = { 0, };

  if (pipe_data->security_descriptorw != NULL)
    {
      sa.nLength = sizeof (sa);

      if (!ConvertStringSecurityDescriptorToSecurityDescriptorW (pipe_data->security_descriptorw,
                                                                 SDDL_REVISION_1,
                                                                 &sa.lpSecurityDescriptor,
                                                                 NULL))
        {
          int errsv = GetLastError ();
          gchar *emsg = g_win32_error_message (errsv);

          g_set_error (error,
                       G_IO_ERROR,
                       g_io_error_from_win32_error (errsv),
                       "Could not convert the security descriptor '%s': %s",
                       pipe_data->security_descriptor, emsg);
          g_free (emsg);

          return FALSE;
        }
    }

  /* Set event as signaled */
  SetEvent(pipe_data->overlapped.hEvent);

  /* clear the flag if this was already connected */
  pipe_data->already_connected = FALSE;

  pipe_data->handle = CreateNamedPipeW (pipe_data->pipe_namew,
                                        PIPE_ACCESS_DUPLEX |
                                        FILE_FLAG_OVERLAPPED |
                                        (protect_first_instance ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0),
                                        PIPE_TYPE_BYTE |
                                        PIPE_READMODE_BYTE |
                                        PIPE_WAIT |
                                        PIPE_REJECT_REMOTE_CLIENTS,
                                        PIPE_UNLIMITED_INSTANCES,
                                        DEFAULT_PIPE_BUF_SIZE,
                                        DEFAULT_PIPE_BUF_SIZE,
                                        0,
                                        pipe_data->security_descriptorw != NULL ? &sa : NULL);

  if (sa.lpSecurityDescriptor != NULL)
    LocalFree (sa.lpSecurityDescriptor);

  if (pipe_data->handle == INVALID_HANDLE_VALUE)
    {
      int errsv = GetLastError ();
      gchar *emsg = g_win32_error_message (errsv);

      g_set_error (error,
                   G_IO_ERROR,
                   g_io_error_from_win32_error (errsv),
                   "Error creating named pipe '%s': %s",
                   pipe_data->pipe_name, emsg);
      g_free (emsg);

      return FALSE;
    }

  if (!ConnectNamedPipe (pipe_data->handle, &pipe_data->overlapped))
    {
      switch (GetLastError ())
      {
      case ERROR_IO_PENDING:
        break;
      case ERROR_PIPE_CONNECTED:
        pipe_data->already_connected = TRUE;
        break;
      default:
        {
          int errsv = GetLastError ();
          gchar *emsg = g_win32_error_message (errsv);

          g_set_error (error,
                       G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                       "Failed to connect named pipe '%s': %s",
                       pipe_data->pipe_name, emsg);
          g_free (emsg);

          return FALSE;
        }
      }
    }

  return TRUE;
}

/**
 * wing_named_pipe_listener_set_named_pipe:
 * @listener: a #WingNamedPipeListener.
 * @pipe_name: a name for the pipe.
 * @security_descriptor: (allow-none): a security descriptor or %NULL.
 * @protect_first_instance: if %TRUE, the pipe creation will fail if the pipe already exists
 * @error: #GError for error reporting, or %NULL to ignore.
 *
 * Adds @named_pipe to the set of named pipes that we try to accept clients
 * from.
 *
 * @security_descriptor must be of the format specified by Microsoft:
 * https://msdn.microsoft.com/en-us/library/windows/desktop/aa379570(v=vs.85).aspx
 * or set to %NULL to not set any security descriptor to the pipe.
 *
 * @protect_first_instance will cause the first instance of the named pipe to be
 * created with the FILE_FLAG_FIRST_PIPE_INSTANCE flag specified. This, in turn,
 * will cause the creation of the pipe to fail if an instance of the pipe already
 * exists.For more details see FILE_FLAG_FIRST_PIPE_INSTANCE details at:
 * https://docs.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-createnamedpipew
 *
 * @security_descriptor must be of the format specified by Microsoft:
 * https://msdn.microsoft.com/en-us/library/windows/desktop/aa379570(v=vs.85).aspx
 * or set to %NULL to not set any security descriptor to the pipe.
 *
 * Returns: %TRUE on success, %FALSE on error.
 */
gboolean
wing_named_pipe_listener_set_named_pipe (WingNamedPipeListener  *listener,
                                         const gchar            *pipe_name,
                                         const gchar            *security_descriptor,
                                         gboolean                protect_first_instance,
                                         GError                **error)
{
  WingNamedPipeListenerPrivate *priv;
  PipeData *pipe_data;

  g_return_val_if_fail (WING_IS_NAMED_PIPE_LISTENER (listener), FALSE);
  g_return_val_if_fail (pipe_name != NULL, FALSE);

  priv = wing_named_pipe_listener_get_instance_private (listener);

  pipe_data = pipe_data_new (pipe_name, security_descriptor);
  if (!create_pipe_from_pipe_data (pipe_data, protect_first_instance, error))
    {
      pipe_data_free (pipe_data);
      return FALSE;
    }

  g_clear_pointer (&priv->named_pipe, pipe_data_free);
  priv->named_pipe = pipe_data;

  return TRUE;
}

static gboolean
connect_ready (HANDLE   handle,
               gpointer user_data)
{
  GTask *task = user_data;
  WingNamedPipeListener *listener = g_task_get_source_object (task);
  WingNamedPipeListenerPrivate *priv;
  PipeData *pipe_data = NULL;
  gulong cbret;
  guint i;

  priv = wing_named_pipe_listener_get_instance_private (listener);

  pipe_data = priv->named_pipe;
  g_return_val_if_fail (pipe_data != NULL, FALSE);

  if (!GetOverlappedResult (pipe_data->handle, &pipe_data->overlapped, &cbret, FALSE))
    {
      int errsv = GetLastError ();
      gchar *emsg = g_win32_error_message (errsv);

      g_task_return_new_error (task,
                               G_IO_ERROR,
                               G_IO_ERROR_INVALID_ARGUMENT,
                               "There was an error querying the named pipe: %s",
                               emsg);
      g_free (emsg);
    }
  else
    {
      WingNamedPipeConnection *connection;
      GError *error = NULL;

      connection = g_object_new (WING_TYPE_NAMED_PIPE_CONNECTION,
                                 "pipe-name", pipe_data->pipe_name,
                                 "handle", pipe_data->handle,
                                 "close-handle", TRUE,
                                 "use-iocp", priv->use_iocp,
                                 NULL);

      /* Put another pipe to listen so more clients can already connect */
      if (!create_pipe_from_pipe_data (pipe_data, FALSE, &error))
        {
          g_object_unref (connection);
          g_task_return_error (task, error);
        }
      else
        g_task_return_pointer (task, connection, g_object_unref);
    }

  g_object_unref (task);

  return G_SOURCE_REMOVE;
}

static void
free_source (GSource *source)
{
  if (source != NULL)
    {
      g_source_destroy (source);
      g_source_unref (source);
    }
}

/**
 * wing_named_pipe_listener_accept:
 * @listener: a #WingNamedPipeListener
 * @cancellable: (allow-none): optional #GCancellable object, %NULL to ignore.
 * @error: #GError for error reporting, or %NULL to ignore.
 *
 * Blocks waiting for a client to connect to any of the named pipes added
 * to the listener. Returns the #WingNamedPipeConnection that was accepted.
 *
 * If @cancellable is not %NULL, then the operation can be cancelled by
 * triggering the cancellable object from another thread. If the operation
 * was cancelled, the error %G_IO_ERROR_CANCELLED will be returned.
 *
 * Returns: (transfer full): a #WingNamedPipeConnection on success, %NULL on error.
 */
WingNamedPipeConnection *
wing_named_pipe_listener_accept (WingNamedPipeListener  *listener,
                                 GCancellable           *cancellable,
                                 GError                **error)
{
  WingNamedPipeListenerPrivate *priv;
  PipeData *pipe_data = NULL;
  WingNamedPipeConnection *connection = NULL;
  gboolean success;

  g_return_val_if_fail (WING_IS_NAMED_PIPE_LISTENER (listener), NULL);

  priv = wing_named_pipe_listener_get_instance_private (listener);

  pipe_data = priv->named_pipe;
  g_return_val_if_fail(pipe_data != NULL, NULL);

  success = pipe_data->already_connected;

  if (!success)
    success = WaitForSingleObject (pipe_data->overlapped.hEvent, INFINITE) == WAIT_OBJECT_0;

  if (success)
    {
      connection = g_object_new (WING_TYPE_NAMED_PIPE_CONNECTION,
                                 "pipe-name", pipe_data->pipe_name,
                                 "handle", pipe_data->handle,
                                 "close-handle", TRUE,
                                 "use-iocp", priv->use_iocp,
                                 NULL);

      /* Put another pipe to listen so more clients can already connect */
      if (!create_pipe_from_pipe_data (pipe_data, FALSE, error))
        g_clear_object (&connection);
    }

  return connection;
}

/**
 * wing_named_pipe_listener_accept_async:
 * @listener: a #WingNamedPipeListener
 * @cancellable: (allow-none): a #GCancellable, or %NULL
 * @callback: (scope async): a #GAsyncReadyCallback
 * @user_data: (closure): user data for the callback
 *
 * This is the asynchronous version of wing_named_pipe_listener_accept().
 *
 * When the operation is finished @callback will be
 * called. You can then call wing_named_pipe_listener_accept_finish()
 * to get the result of the operation.
 */
void
wing_named_pipe_listener_accept_async (WingNamedPipeListener *listener,
                                       GCancellable          *cancellable,
                                       GAsyncReadyCallback    callback,
                                       gpointer               user_data)
{
  GTask *task;
  GSource *source;
  WingNamedPipeListenerPrivate *priv;

  task = g_task_new (listener, cancellable, callback, user_data);

  priv = wing_named_pipe_listener_get_instance_private (listener);
  g_return_if_fail (priv->named_pipe != NULL);

  if (priv->named_pipe->already_connected)
    {
      WingNamedPipeConnection *connection;
      GError *error = NULL;

      connection = g_object_new (WING_TYPE_NAMED_PIPE_CONNECTION,
                                 "pipe-name", priv->named_pipe->pipe_name,
                                 "handle", priv->named_pipe->handle,
                                 "close-handle", TRUE,
                                 "use-iocp", priv->use_iocp,
                                 NULL);

      if (!create_pipe_from_pipe_data (priv->named_pipe, FALSE, &error))
        {
          g_object_unref (connection);
          g_task_return_error (task, error);
        }
      else
        g_task_return_pointer (task, connection, g_object_unref);

      g_object_unref (task);

      return;
    }

  source = wing_create_source (priv->named_pipe->overlapped.hEvent,
                               G_IO_IN,
                               cancellable);
  g_source_set_callback (source,
                         (GSourceFunc) connect_ready,
                         task, NULL);
  g_source_attach (source, g_main_context_get_thread_default ());

  g_task_set_task_data (task, source, (GDestroyNotify) free_source);
}

/**
 * wing_named_pipe_listener_accept_finish:
 * @listener: a #WingNamedPipeListener.
 * @result: a #GAsyncResult.
 * @error: a #GError location to store the error occurring, or %NULL to ignore.
 *
 * Finishes an async accept operation. See wing_named_pipe_listener_accept_async()
 *
 * Returns: (transfer full): a #WingNamedPipeConnection on success, %NULL on error.
 */
WingNamedPipeConnection *
wing_named_pipe_listener_accept_finish (WingNamedPipeListener  *listener,
                                        GAsyncResult           *result,
                                        GError                **error)
{
  g_return_val_if_fail (WING_IS_NAMED_PIPE_LISTENER (listener), NULL);
  g_return_val_if_fail (g_task_is_valid (result, listener), NULL);

  return g_task_propagate_pointer (G_TASK (result), error);
}

void
wing_named_pipe_listener_set_use_iocp (WingNamedPipeListener *listener,
                                       gboolean               use_iocp)
{
  WingNamedPipeListenerPrivate *priv;

  g_return_if_fail (WING_IS_NAMED_PIPE_LISTENER (listener));

  priv = wing_named_pipe_listener_get_instance_private (listener);

  if (priv->use_iocp != use_iocp)
    {
      priv->use_iocp = use_iocp;
      g_object_notify_by_pspec (G_OBJECT (listener), props[PROP_USE_IOCP]);
    }
}