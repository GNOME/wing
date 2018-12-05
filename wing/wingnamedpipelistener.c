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
  GObject *source_object;
  gboolean already_connected;
} PipeData;

typedef struct
{
  GPtrArray *named_pipes;
  GMainContext *main_context;
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
               const gchar *security_descriptor,
               GObject     *source_object)
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
  if (source_object)
    data->source_object = g_object_ref (source_object);

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
  g_clear_object (&data->source_object);
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

  if (priv->main_context)
    g_main_context_unref (priv->main_context);

  g_ptr_array_free (priv->named_pipes, TRUE);

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
  WingNamedPipeListenerPrivate *priv;

  priv = wing_named_pipe_listener_get_instance_private (listener);

  priv->named_pipes = g_ptr_array_new_with_free_func ((GDestroyNotify) pipe_data_free);
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
                                        FILE_FLAG_OVERLAPPED,
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
 * wing_named_pipe_listener_add_named_pipe:
 * @listener: a #WingNamedPipeListener.
 * @pipe_name: a name for the pipe.
 * @security_descriptor: (allow-none): a security descriptor or %NULL.
 * @source_object: (allow-none): Optional #GObject identifying this source
 * @error: #GError for error reporting, or %NULL to ignore.
 *
 * Adds @named_pipe to the set of named pipes that we try to accept clients
 * from.
 *
 * @security_descriptor must be of the format specified by Microsoft:
 * https://msdn.microsoft.com/en-us/library/windows/desktop/aa379570(v=vs.85).aspx
 * or set to %NULL to not set any security descriptor to the pipe.
 *
 * @source_object will be passed out in the various calls
 * to accept to identify this particular source, which is
 * useful if you're listening on multiple pipes and do
 * different things depending on what pipe is connected to.
 *
 * Returns: %TRUE on success, %FALSE on error.
 */
gboolean
wing_named_pipe_listener_add_named_pipe (WingNamedPipeListener  *listener,
                                         const gchar            *pipe_name,
                                         const gchar            *security_descriptor,
                                         GObject                *source_object,
                                         GError                **error)
{
  WingNamedPipeListenerPrivate *priv;
  PipeData *pipe_data;

  g_return_val_if_fail (WING_IS_NAMED_PIPE_LISTENER (listener), FALSE);
  g_return_val_if_fail (pipe_name != NULL, FALSE);

  priv = wing_named_pipe_listener_get_instance_private (listener);

  pipe_data = pipe_data_new (pipe_name, security_descriptor, source_object);
  if (!create_pipe_from_pipe_data (pipe_data, error))
    {
      pipe_data_free (pipe_data);
      return FALSE;
    }

  g_ptr_array_add (priv->named_pipes, pipe_data);

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

  for (i = 0; i < priv->named_pipes->len; i++)
    {
      PipeData *pdata;

      pdata = priv->named_pipes->pdata[i];
      if (pdata->overlapped.hEvent == handle)
        {
          pipe_data = pdata;
          break;
        }
    }

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

      if (pipe_data->source_object != NULL)
        g_object_set_qdata_full (G_OBJECT (task),
                                 source_quark,
                                 g_object_ref (pipe_data->source_object),
                                 g_object_unref);

      connection = g_object_new (WING_TYPE_NAMED_PIPE_CONNECTION,
                                 "pipe-name", pipe_data->pipe_name,
                                 "handle", pipe_data->handle,
                                 "close-handle", TRUE,
                                 "use-iocp", priv->use_iocp,
                                 NULL);

      /* Put another pipe to listen so more clients can already connect */
      if (!create_pipe_from_pipe_data (pipe_data, &error))
        {
          g_object_unref (connection);
          g_task_return_error (task, error);
        }
      else
        g_task_return_pointer (task, connection, g_object_unref);
    }

  g_object_unref (task);

  return FALSE;
}

static GList *
add_sources (WingNamedPipeListener *listener,
             WingSourceFunc         callback,
             gpointer               callback_data,
             GCancellable          *cancellable,
             GMainContext          *context)
{
  WingNamedPipeListenerPrivate *priv;
  GList *sources;
  guint i;

  priv = wing_named_pipe_listener_get_instance_private (listener);

  sources = NULL;
  for (i = 0; i < priv->named_pipes->len; i++)
    {
      PipeData *data;
      GSource *source;

      data = priv->named_pipes->pdata[i];

      source = wing_create_source (data->overlapped.hEvent,
                                   G_IO_IN,
                                   cancellable);
      g_source_set_callback (source,
                             (GSourceFunc) callback,
                             callback_data, NULL);
      g_source_attach (source, context);

      sources = g_list_prepend (sources, source);
    }

  return sources;
}

static void
free_sources (GList *sources)
{
  while (sources != NULL)
    {
      GSource *source;

      source = sources->data;
      sources = g_list_delete_link (sources, sources);
      g_source_destroy (source);
      g_source_unref (source);
    }
}

struct AcceptData {
  WingNamedPipeListener *listener;
  GMainLoop *loop;
  PipeData *pipe_data;
};

static gboolean
accept_callback (HANDLE   handle,
                 gpointer user_data)
{
  struct AcceptData *data = user_data;
  WingNamedPipeListenerPrivate *priv;
  PipeData *pipe_data = NULL;
  guint i;

  priv = wing_named_pipe_listener_get_instance_private (data->listener);

  for (i = 0; i < priv->named_pipes->len; i++)
    {
      PipeData *pdata;

      pdata = priv->named_pipes->pdata[i];
      if (pdata->overlapped.hEvent == handle)
        {
          pipe_data = pdata;
          break;
        }
    }

  data->pipe_data = pipe_data;
  g_main_loop_quit (data->loop);

  return TRUE;
}

/* Check if any of the named pipes is already connected
 * and pick the the first one.
 */
static PipeData *
find_first_connected (WingNamedPipeListener *listener)
{
  WingNamedPipeListenerPrivate *priv;
  guint i;

  priv = wing_named_pipe_listener_get_instance_private (listener);

  for (i = 0; i < priv->named_pipes->len; i++)
    {
      PipeData *pdata = priv->named_pipes->pdata[i];

      if (pdata->already_connected)
        return pdata;
    }

  return NULL;
}

/**
 * wing_named_pipe_listener_accept:
 * @listener: a #WingNamedPipeListener
 * @source_object: (out) (transfer none) (allow-none): location where #GObject pointer will be stored, or %NULL.
 * @cancellable: (allow-none): optional #GCancellable object, %NULL to ignore.
 * @error: #GError for error reporting, or %NULL to ignore.
 *
 * Blocks waiting for a client to connect to any of the named pipes added
 * to the listener. Returns the #WingNamedPipeConnection that was accepted.
 *
 * If @source_object is not %NULL it will be filled out with the source
 * object specified when the corresponding named pipe was added
 * to the listener.
 *
 * If @cancellable is not %NULL, then the operation can be cancelled by
 * triggering the cancellable object from another thread. If the operation
 * was cancelled, the error %G_IO_ERROR_CANCELLED will be returned.
 *
 * Returns: (transfer full): a #WingNamedPipeConnection on success, %NULL on error.
 */
WingNamedPipeConnection *
wing_named_pipe_listener_accept (WingNamedPipeListener  *listener,
                                 GObject               **source_object,
                                 GCancellable           *cancellable,
                                 GError                **error)
{
  WingNamedPipeListenerPrivate *priv;
  PipeData *pipe_data = NULL;
  WingNamedPipeConnection *connection = NULL;

  g_return_val_if_fail (WING_IS_NAMED_PIPE_LISTENER (listener), NULL);

  priv = wing_named_pipe_listener_get_instance_private (listener);

  if (priv->named_pipes->len == 1)
    {
      gboolean success;

      pipe_data = priv->named_pipes->pdata[0];
      success = pipe_data->already_connected;

      if (!success)
        success = WaitForSingleObject (pipe_data->overlapped.hEvent, INFINITE) == WAIT_OBJECT_0;

      if (!success)
        pipe_data = NULL;
    }
  else
    {
      pipe_data = find_first_connected (listener);

      if (pipe_data == NULL)
        {
          GList *sources;
          struct AcceptData data;
          GMainLoop *loop;

          if (priv->main_context == NULL)
            priv->main_context = g_main_context_new ();

          loop = g_main_loop_new (priv->main_context, FALSE);
          data.loop = loop;
          data.listener = listener;

          sources = add_sources (listener,
                                 accept_callback,
                                 &data,
                                 cancellable,
                                 priv->main_context);
          g_main_loop_run (loop);
          pipe_data = data.pipe_data;
          free_sources (sources);
          g_main_loop_unref (loop);
        }
    }

  if (pipe_data != NULL)
    {
      connection = g_object_new (WING_TYPE_NAMED_PIPE_CONNECTION,
                                 "pipe-name", pipe_data->pipe_name,
                                 "handle", pipe_data->handle,
                                 "close-handle", TRUE,
                                 "use-iocp", priv->use_iocp,
                                 NULL);

      if (source_object)
        *source_object = pipe_data->source_object;

      /* Put another pipe to listen so more clients can already connect */
      if (!create_pipe_from_pipe_data (pipe_data, error))
        {
          g_object_unref (connection);
          connection = NULL;
        }
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
  PipeData *pipe_data;
  GTask *task;
  GList *sources;

  task = g_task_new (listener, cancellable, callback, user_data);

  pipe_data = find_first_connected (listener);

  if (pipe_data != NULL)
    {
      WingNamedPipeConnection *connection;
      WingNamedPipeListenerPrivate *priv;
      GError *error = NULL;

      priv = wing_named_pipe_listener_get_instance_private (listener);

      if (pipe_data->source_object)
        g_object_set_qdata_full (G_OBJECT (task),
                                 source_quark,
                                 g_object_ref (pipe_data->source_object),
                                 g_object_unref);

      connection = g_object_new (WING_TYPE_NAMED_PIPE_CONNECTION,
                                 "pipe-name", pipe_data->pipe_name,
                                 "handle", pipe_data->handle,
                                 "close-handle", TRUE,
                                 "use-iocp", priv->use_iocp,
                                 NULL);

      if (!create_pipe_from_pipe_data (pipe_data, &error))
        {
          g_object_unref (connection);
          g_task_return_error (task, error);
        }
      else
        g_task_return_pointer (task, connection, g_object_unref);

      g_object_unref (task);

      return;
    }

  sources = add_sources (listener,
                         connect_ready,
                         task,
                         cancellable,
                         g_main_context_get_thread_default ());
  g_task_set_task_data (task, sources, (GDestroyNotify) free_sources);
}

/**
 * wing_named_pipe_listener_accept_finish:
 * @listener: a #WingNamedPipeListener.
 * @result: a #GAsyncResult.
 * @source_object: (out) (transfer none) (allow-none): Optional #GObject identifying this source
 * @error: a #GError location to store the error occurring, or %NULL to ignore.
 *
 * Finishes an async accept operation. See wing_named_pipe_listener_accept_async()
 *
 * Returns: (transfer full): a #WingNamedPipeConnection on success, %NULL on error.
 */
WingNamedPipeConnection *
wing_named_pipe_listener_accept_finish (WingNamedPipeListener  *listener,
                                        GAsyncResult           *result,
                                        GObject               **source_object,
                                        GError                **error)
{
  g_return_val_if_fail (WING_IS_NAMED_PIPE_LISTENER (listener), NULL);
  g_return_val_if_fail (g_task_is_valid (result, listener), NULL);

  if (source_object)
    *source_object = g_object_get_qdata (G_OBJECT (result), source_quark);

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