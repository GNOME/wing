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
  gchar *security_descriptor;
  gboolean protect_first_instance;

  gunichar2 *pipe_namew;
  HANDLE handle;
  OVERLAPPED overlapped;
  SECURITY_ATTRIBUTES *security_attributes;

  gboolean use_iocp;
} WingNamedPipeListenerPrivate;

enum
{
  PROP_0,
  PROP_PIPE_NAME,
  PROP_SECURITY_DESCRIPTOR,
  PROP_PROTECT_FIRST_INSTANCE,
  PROP_USE_IOCP,
  LAST_PROP
};

static GParamSpec *props[LAST_PROP];

static void wing_named_pipe_listener_initable_iface_init (GInitableIface *iface);

G_DEFINE_TYPE_EXTENDED (WingNamedPipeListener, wing_named_pipe_listener, G_TYPE_OBJECT, 0,
                        G_ADD_PRIVATE (WingNamedPipeListener)
                        G_IMPLEMENT_INTERFACE(G_TYPE_INITABLE,
                                              wing_named_pipe_listener_initable_iface_init))

static GQuark source_quark = 0;

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
    case PROP_PIPE_NAME:
      priv->pipe_name = g_value_dup_string (value);
      break;

    case PROP_SECURITY_DESCRIPTOR:
      priv->security_descriptor = g_value_dup_string (value);
      break;

    case PROP_PROTECT_FIRST_INSTANCE:
      priv->protect_first_instance = g_value_get_boolean (value);
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
    case PROP_PIPE_NAME:
      g_value_set_string (value, priv->pipe_name);
      break;

    case PROP_SECURITY_DESCRIPTOR:
      g_value_set_string (value, priv->security_descriptor);
      break;

    case PROP_PROTECT_FIRST_INSTANCE:
      g_value_set_boolean (value, priv->protect_first_instance);
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
wing_named_pipe_listener_finalize (GObject *object)
{
  WingNamedPipeListener *listener = WING_NAMED_PIPE_LISTENER (object);
  WingNamedPipeListenerPrivate *priv;

  priv = wing_named_pipe_listener_get_instance_private (listener);

  g_free (priv->pipe_name);
  g_free (priv->pipe_namew);
  g_free (priv->security_descriptor);
  CloseHandle (priv->handle);
  CloseHandle (priv->overlapped.hEvent);

  if (priv->security_attributes != NULL)
    {
      if (priv->security_attributes->lpSecurityDescriptor != NULL)
        LocalFree (priv->security_attributes->lpSecurityDescriptor);

      g_free (priv->security_attributes);
    }

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

  props[PROP_PIPE_NAME] =
      g_param_spec_string ("pipe-name",
                           "The name of the pipe",
                           "The name of the pipe",
                           NULL,
                           G_PARAM_READWRITE |
                           G_PARAM_CONSTRUCT_ONLY |
                           G_PARAM_STATIC_STRINGS);

  props[PROP_SECURITY_DESCRIPTOR] =
      g_param_spec_string ("security-descriptor",
                           "The security descriptor to apply to the pipe",
                           "The security descriptor to apply to the pipe",
                           NULL,
                           G_PARAM_READWRITE |
                           G_PARAM_CONSTRUCT_ONLY |
                           G_PARAM_STATIC_STRINGS);

  props[PROP_PROTECT_FIRST_INSTANCE] =
      g_param_spec_boolean ("protect-first-instance",
                           "Protect the first instance of the pipe",
                           "Whether the creation of the pipe should fail if an instance of the pipe already exists",
                           FALSE,
                           G_PARAM_READWRITE |
                           G_PARAM_CONSTRUCT_ONLY |
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

  g_object_class_install_properties (gobject_class, LAST_PROP, props);
}

static void
wing_named_pipe_listener_init (WingNamedPipeListener *listener)
{
}

/**
 * wing_named_pipe_listener_new:
 * @pipe_name: a name for the pipe.
 * @security_descriptor: (allow-none): a security descriptor or %NULL.
 * @protect_first_instance: if %TRUE, the pipe creation will fail if the pipe already exists
 * @cancellable: (allow-none): optional #GCancellable object, %NULL to ignore.
 * @error: #GError for error reporting, or %NULL to ignore.
 *
 * Creates a new #WingNamedPipeListener.
 *
 * @security_descriptor must be of the format specified by Microsoft:
 * https://msdn.microsoft.com/en-us/library/windows/desktop/aa379570(v=vs.85).aspx
 * or set to %NULL to not set any security descriptor to the pipe.
 *
 * @security_descriptor must be of the format specified by Microsoft:
 * https://msdn.microsoft.com/en-us/library/windows/desktop/aa379570(v=vs.85).aspx
 * or set to %NULL to not set any security descriptor to the pipe.
 * 
 * @protect_first_instance will cause the first instance of the named pipe to be
 * created with the FILE_FLAG_FIRST_PIPE_INSTANCE flag specified. This, in turn,
 * will cause the creation of the pipe to fail if an instance of the pipe already
 * exists. For more details see FILE_FLAG_FIRST_PIPE_INSTANCE details at:
 * https://docs.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-createnamedpipew
 *
 * Returns: (transfer full): a new #WingNamedPipeListener.
 */
WingNamedPipeListener *
wing_named_pipe_listener_new (const gchar            *pipe_name,
                              const gchar            *security_descriptor,
                              gboolean                protect_first_instance,
                              GCancellable           *cancellable,
                              GError                **error)
{
  return g_initable_new (WING_TYPE_NAMED_PIPE_LISTENER,
                         cancellable,
                         error,
                         "pipe-name", pipe_name,
                         "security-descriptor", security_descriptor,
                         "protect-first-instance", protect_first_instance,
                         NULL);
}

static gboolean
create_pipe (WingNamedPipeListenerPrivate  *priv,
             gboolean                       protect_first_instance,
             GError                       **error)
{
  g_assert (priv->handle == INVALID_HANDLE_VALUE);

  priv->handle = CreateNamedPipeW (priv->pipe_namew,
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
                                   priv->security_attributes);
  if (priv->handle == INVALID_HANDLE_VALUE)
    {
      int errsv = GetLastError ();
      gchar *emsg = g_win32_error_message (errsv);

      g_set_error (error,
                   G_IO_ERROR,
                   g_io_error_from_win32_error (errsv),
                   "Error creating named pipe '%s': %s",
                   priv->pipe_name, emsg);
      g_free (emsg);

      return FALSE;
    }

  return TRUE;
}

static gboolean
connect_ready (HANDLE   handle,
               gpointer user_data)
{
  GTask *task = user_data;
  WingNamedPipeListener *listener = g_task_get_source_object (task);
  WingNamedPipeListenerPrivate *priv;
  WingNamedPipeConnection *connection = NULL;
  gulong cbret;
  guint i;
  GError *error = NULL;
  GError *local_error = NULL;
  HANDLE old_handle = INVALID_HANDLE_VALUE;

  priv = wing_named_pipe_listener_get_instance_private (listener);

  if (g_task_return_error_if_cancelled (task))
    {
      g_object_unref (task);

      return G_SOURCE_REMOVE;
    }

  if (!GetOverlappedResult (priv->handle, &priv->overlapped, &cbret, FALSE))
    {
      int errsv = GetLastError ();
      gchar *emsg = g_win32_error_message (errsv);

      g_set_error (&error,
                   G_IO_ERROR,
                   G_IO_ERROR_INVALID_ARGUMENT,
                   "There was an error querying the named pipe: %s",
                   emsg);
      g_free (emsg);

      old_handle = priv->handle;
    }
  else
    {
      connection = g_object_new (WING_TYPE_NAMED_PIPE_CONNECTION,
                                 "pipe-name", priv->pipe_name,
                                 "handle", priv->handle,
                                 "close-handle", TRUE,
                                 "use-iocp", priv->use_iocp,
                                 NULL);
    }

  priv->handle = INVALID_HANDLE_VALUE;

  /* Create another pipe so more clients can already connect */
  if (!create_pipe (priv, FALSE, &local_error))
    {
      g_warning ("%s", local_error->message);
      g_error_free (local_error);
    }

  if (connection != NULL)
    {
      g_task_return_pointer (task, connection, g_object_unref);
    }
  else
    {
      g_task_return_error (task, error);

      /* We close the old pipe after having created the new one to avoid client connections
       * failures because the pipe does not exists. 
       */
      CloseHandle (old_handle);
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
  WingNamedPipeConnection *connection = NULL;
  GError *local_error = NULL;
  gboolean success = TRUE;
  HANDLE old_handle;

  g_return_val_if_fail (WING_IS_NAMED_PIPE_LISTENER (listener), NULL);

  priv = wing_named_pipe_listener_get_instance_private (listener);
  
  /* This should not happen. If it happens, it means the creation of the pipe failed in the previous accept */
  if  (priv->handle == INVALID_HANDLE_VALUE &&
       !create_pipe (priv, FALSE, error))
    {
      return NULL;
    }

  if (!ConnectNamedPipe (priv->handle, &priv->overlapped))
    {
      switch (GetLastError ())
        {
        case ERROR_PIPE_CONNECTED:
          break;
        case ERROR_IO_PENDING:
          {
            DWORD cbret;

            if (GetOverlappedResult (priv->handle, &priv->overlapped, &cbret, TRUE))
              break;

            /* FALLTHROUGH */
          }
        default:
          {
            int errsv = GetLastError ();
            gchar *emsg = g_win32_error_message (errsv);

            g_set_error (error,
                         G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                         "Failed to connect named pipe '%s': %s",
                         priv->pipe_name, emsg);
            g_free (emsg);

            old_handle = priv->handle;
            success = FALSE;
          }
        }
    }

  if (success)
    {
      connection = g_object_new (WING_TYPE_NAMED_PIPE_CONNECTION,
                                 "pipe-name", priv->pipe_name,
                                 "handle", priv->handle,
                                 "close-handle", TRUE,
                                 "use-iocp", priv->use_iocp,
                                 NULL);
    }

  priv->handle = INVALID_HANDLE_VALUE;

  /* Create another pipe so more clients can already connect */
  if (!create_pipe (priv, FALSE, &local_error))
    {
      g_warning ("%s", local_error->message);
      g_error_free (local_error);
    }

  if (!success)
    {
      /* We close the old pipe after having created the new one to avoid client connections
       * failures because the pipe does not exists. 
       */
      CloseHandle (old_handle);
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
  WingNamedPipeListenerPrivate *priv;
  WingNamedPipeConnection *connection;
  GError *local_error = NULL;
  gboolean success = TRUE;
  HANDLE old_handle;
  GTask *task;
  GError *error = NULL;

  g_return_if_fail (WING_IS_NAMED_PIPE_LISTENER (listener));

  task = g_task_new (listener, cancellable, callback, user_data);

  priv = wing_named_pipe_listener_get_instance_private (listener);
  
  /* This should not happen. If it happens, it means the creation of the pipe failed in the previous accept */
  if  (priv->handle == INVALID_HANDLE_VALUE &&
       !create_pipe (priv, FALSE, &error))
    {
      g_task_return_error (task, error);
      g_object_unref (task);
      return;
    }

  if (!ConnectNamedPipe (priv->handle, &priv->overlapped))
    {
      switch (GetLastError ())
        {
        case ERROR_PIPE_CONNECTED:
          break;
        case ERROR_IO_PENDING:
          {
            GSource *source;

            source = wing_create_source (priv->overlapped.hEvent,
                                        G_IO_IN,
                                        cancellable);
            g_source_set_callback (source,
                                  (GSourceFunc) connect_ready,
                                  task, NULL);
            g_source_attach (source, g_main_context_get_thread_default ());

            g_task_set_task_data (task, source, (GDestroyNotify) free_source);
            return;
          }
        default:
          {
            int errsv = GetLastError ();
            gchar *emsg = g_win32_error_message (errsv);

            g_set_error (&error,
                         G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                         "Failed to connect named pipe '%s': %s",
                         priv->pipe_name, emsg);
            g_free (emsg);
            
            old_handle = priv->handle;
            success = FALSE;
          }
        }
    }

  if (success)
    {
      connection = g_object_new (WING_TYPE_NAMED_PIPE_CONNECTION,
                                "pipe-name", priv->pipe_name,
                                "handle", priv->handle,
                                "close-handle", TRUE,
                                "use-iocp", priv->use_iocp,
                                NULL);
    }

  priv->handle = INVALID_HANDLE_VALUE;

  /* Create another pipe so more clients can already connect */
  if (!create_pipe (priv, FALSE, &local_error))
    {
      g_warning ("%s", local_error->message);
      g_error_free (local_error);
    }

  if (success)
    {
      g_task_return_pointer (task, connection, g_object_unref);
    }
  else
    {
      g_task_return_error (task, error);

      /* We close the old pipe after having created the new one to avoid client connections
       * failures because the pipe does not exists. 
       */
      CloseHandle (old_handle);
    }

  g_object_unref (task);
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

static gboolean
wing_named_pipe_listener_initable_init (GInitable     *initable,
                                        GCancellable  *cancellable,
                                        GError       **error)
{
  WingNamedPipeListener *listener;
  WingNamedPipeListenerPrivate *priv;
  gboolean success;

  g_return_val_if_fail (WING_IS_NAMED_PIPE_LISTENER (initable), FALSE);

  listener = WING_NAMED_PIPE_LISTENER (initable);
  priv = wing_named_pipe_listener_get_instance_private (listener);

  priv->pipe_namew = g_utf8_to_utf16 (priv->pipe_name, -1, NULL, NULL, NULL);
  priv->handle = INVALID_HANDLE_VALUE;
  priv->overlapped.hEvent = CreateEvent (NULL, /* default security attribute */
                                         TRUE, /* manual-reset event */
                                         FALSE, /* initial state = non-signaled */
                                         NULL); /* unnamed event object */

  if (priv->security_descriptor != NULL)
    {
      gunichar2 *security_descriptorw;
      gboolean success;
      SECURITY_ATTRIBUTES *sa;

      sa = g_malloc0 (sizeof (SECURITY_ATTRIBUTES));
      sa->nLength = sizeof (SECURITY_ATTRIBUTES);

      security_descriptorw = g_utf8_to_utf16 (priv->security_descriptor, -1, NULL, NULL, NULL);
      success = ConvertStringSecurityDescriptorToSecurityDescriptorW (security_descriptorw,
                                                                      SDDL_REVISION_1,
                                                                      &sa->lpSecurityDescriptor,
                                                                      NULL);
      g_free (security_descriptorw);

      if (!success)
        {
          int errsv = GetLastError ();
          gchar *emsg = g_win32_error_message (errsv);

          g_set_error (error,
                      G_IO_ERROR,
                      g_io_error_from_win32_error (errsv),
                      "Could not convert the security descriptor '%s': %s",
                      priv->security_descriptor, emsg);
          g_free (emsg);
          g_free (sa);

          return FALSE;
        }

      priv->security_attributes = sa;
    }

  return create_pipe (priv, priv->protect_first_instance, error);
}

static void
wing_named_pipe_listener_initable_iface_init (GInitableIface *iface)
{
  iface->init = wing_named_pipe_listener_initable_init;
}