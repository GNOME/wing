/*
 * Copyright © 2016 NICE s.r.l.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 2 of the licence or (at
 * your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: Ignacio Casal Quinteiro <ignacio.casal@nice-software.com>
 */


#include "wingnamedpipeconnection.h"
#include "winginputstream.h"
#include "wingoutputstream.h"
#include "wingthreadpoolio.h"
#include "wingiocpinputstream.h"
#include "wingiocpoutputstream.h"
#include "wingutils.h"

#include <gio/gio.h>

#include <windows.h>
#include <sddl.h>

/**
 * SECTION:wingnamedpipeconnection
 * @short_description: A wrapper around a Windows pipe handle.
 * @include: gio/gio.h
 * @see_also: #GIOStream
 *
 * WingNamedPipeConnection creates a #GIOStream from an arbitrary handle.
 */

/**
 * WingNamedPipeConnection:
 *
 * A wrapper around a Windows pipe handle.
 */
struct _WingNamedPipeConnection
{
  GIOStream parent;

  gchar *pipe_name;
  void *handle;
  gboolean close_handle;

  GInputStream *input_stream;
  GOutputStream *output_stream;

  gboolean use_iocp;
  WingThreadPoolIo *thread_pool_io;
};

struct _WingNamedPipeConnectionClass
{
  GIOStreamClass parent;
};

typedef struct _WingNamedPipeConnectionClass WingNamedPipeConnectionClass;

enum
{
  PROP_0,
  PROP_PIPE_NAME,
  PROP_HANDLE,
  PROP_CLOSE_HANDLE,
  PROP_USE_IOCP,
  LAST_PROP
};

static GParamSpec *props[LAST_PROP];

G_DEFINE_TYPE (WingNamedPipeConnection, wing_named_pipe_connection, G_TYPE_IO_STREAM)

static void
wing_named_pipe_connection_finalize (GObject *object)
{
  WingNamedPipeConnection *connection = WING_NAMED_PIPE_CONNECTION (object);

  g_free (connection->pipe_name);

  if (connection->input_stream)
    g_object_unref (connection->input_stream);

  if (connection->output_stream)
    g_object_unref (connection->output_stream);

  if (connection->close_handle)
    {
      if (connection->thread_pool_io != NULL)
        {
          wing_thread_pool_io_close_handle(connection->thread_pool_io, NULL);
        }
      else if (connection->handle != INVALID_HANDLE_VALUE)
        {
          DisconnectNamedPipe (connection->handle);
          CloseHandle (connection->handle);
        }
    }

  g_clear_pointer (&connection->thread_pool_io, wing_thread_pool_io_unref);

  G_OBJECT_CLASS (wing_named_pipe_connection_parent_class)->finalize (object);
}

static void
wing_named_pipe_connection_set_property (GObject      *object,
                                         guint         prop_id,
                                         const GValue *value,
                                         GParamSpec   *pspec)
{
  WingNamedPipeConnection *connection = WING_NAMED_PIPE_CONNECTION (object);

  switch (prop_id)
    {
    case PROP_PIPE_NAME:
      connection->pipe_name = g_value_dup_string (value);
      break;

    case PROP_HANDLE:
      connection->handle = g_value_get_pointer (value);
      break;

    case PROP_CLOSE_HANDLE:
      connection->close_handle = g_value_get_boolean (value);
      break;

    case PROP_USE_IOCP:
      connection->use_iocp = g_value_get_boolean (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
wing_named_pipe_connection_get_property (GObject    *object,
                                         guint       prop_id,
                                         GValue     *value,
                                         GParamSpec *pspec)
{
  WingNamedPipeConnection *connection = WING_NAMED_PIPE_CONNECTION (object);

  switch (prop_id)
    {
    case PROP_PIPE_NAME:
      g_value_set_string (value, connection->pipe_name);
      break;

    case PROP_HANDLE:
      g_value_set_pointer (value, connection->handle);
      break;

    case PROP_CLOSE_HANDLE:
      g_value_set_boolean (value, connection->close_handle);
      break;

    case PROP_USE_IOCP:
      g_value_set_boolean (value, connection->use_iocp);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
wing_named_pipe_connection_constructed (GObject *object)
{
  WingNamedPipeConnection *connection = WING_NAMED_PIPE_CONNECTION (object);

  if (connection->handle != NULL &&
      connection->handle != INVALID_HANDLE_VALUE)
    {
      if (connection->use_iocp)
        {
          connection->thread_pool_io = wing_thread_pool_io_new (connection->handle);
          if (connection->thread_pool_io != NULL)
            {
              connection->input_stream = wing_iocp_input_stream_new (FALSE, connection->thread_pool_io);
              connection->output_stream = wing_iocp_output_stream_new (FALSE, connection->thread_pool_io);
            }
          else
           {
             g_info ("Failed to create thread pool io, falling back to not iocp version");
           }
        }

      if (!connection->thread_pool_io)
        {
          connection->input_stream = wing_input_stream_new (connection->handle, FALSE);
          connection->output_stream = wing_output_stream_new (connection->handle, FALSE);
        }
    }

  G_OBJECT_CLASS (wing_named_pipe_connection_parent_class)->constructed (object);
}

static GInputStream *
wing_named_pipe_connection_get_input_stream (GIOStream *stream)
{
  WingNamedPipeConnection *connection = WING_NAMED_PIPE_CONNECTION (stream);

  return connection->input_stream;
}

static GOutputStream *
wing_named_pipe_connection_get_output_stream (GIOStream *stream)
{
  WingNamedPipeConnection *connection = WING_NAMED_PIPE_CONNECTION (stream);

  return connection->output_stream;
}

static gboolean
wing_named_pipe_connection_close (GIOStream     *stream,
                                  GCancellable  *cancellable,
                                  GError       **error)
{
  WingNamedPipeConnection *connection = WING_NAMED_PIPE_CONNECTION (stream);

  if (connection->output_stream)
    g_output_stream_close (connection->output_stream, cancellable, NULL);

  if (connection->input_stream)
    g_input_stream_close (connection->input_stream, cancellable, NULL);

  if (connection->close_handle)
    {
      if (connection->thread_pool_io != NULL)
        {
          wing_thread_pool_io_close_handle(connection->thread_pool_io, NULL);
        }
      else if (connection->handle != INVALID_HANDLE_VALUE)
        {
          DisconnectNamedPipe (connection->handle);
          CloseHandle (connection->handle);
        }

      connection->handle = INVALID_HANDLE_VALUE;
    }

  g_clear_pointer (&connection->thread_pool_io, wing_thread_pool_io_unref);

  return TRUE;
}

static void
wing_named_pipe_connection_class_init (WingNamedPipeConnectionClass *class)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (class);
  GIOStreamClass *io_class = G_IO_STREAM_CLASS (class);

  gobject_class->finalize = wing_named_pipe_connection_finalize;
  gobject_class->get_property = wing_named_pipe_connection_get_property;
  gobject_class->set_property = wing_named_pipe_connection_set_property;
  gobject_class->constructed = wing_named_pipe_connection_constructed;

  io_class->get_input_stream = wing_named_pipe_connection_get_input_stream;
  io_class->get_output_stream = wing_named_pipe_connection_get_output_stream;
  io_class->close_fn = wing_named_pipe_connection_close;

  /**
   * WingNamedPipeConnection:pipe-name:
   *
   * The name of the pipe.
   *
   */
  props[PROP_PIPE_NAME] =
    g_param_spec_string ("pipe-name",
                         "Pipe name",
                         "The pipe name",
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  /**
   * WingNamedPipeConnection:handle:
   *
   * The handle for the connection.
   */
  props[PROP_HANDLE] =
    g_param_spec_pointer ("handle",
                          "File handle",
                          "The file handle to read from",
                          G_PARAM_READABLE |
                          G_PARAM_WRITABLE |
                          G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS);

  /**
   * WingNamedPipeConnection:close-handle:
   *
   * Whether to close the file handle when the pipe connection is disposed.
   */
  props[PROP_CLOSE_HANDLE] =
    g_param_spec_boolean ("close-handle",
                          "Close file handle",
                          "Whether to close the file handle when the stream is closed",
                          TRUE,
                          G_PARAM_READABLE |
                          G_PARAM_WRITABLE |
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
                          G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS );

  g_object_class_install_properties (gobject_class, LAST_PROP, props);
}

static void
wing_named_pipe_connection_init (WingNamedPipeConnection *stream)
{
}

const gchar *
wing_named_pipe_connection_get_pipe_name (WingNamedPipeConnection *connection)
{
  g_return_val_if_fail (WING_IS_NAMED_PIPE_CONNECTION (connection), NULL);

  return connection->pipe_name;
}

static gulong
get_client_process_id (WingNamedPipeConnection  *connection,
                       GError                  **error)
{
  gulong process_id;

  g_return_val_if_fail (WING_IS_NAMED_PIPE_CONNECTION (connection), 0);

  if (!GetNamedPipeClientProcessId ((HANDLE)connection->handle, &process_id))
    {
      int errsv = GetLastError ();
      gchar *emsg = g_win32_error_message (errsv);

      g_set_error (error,
                   G_IO_ERROR,
                   g_io_error_from_win32_error (errsv),
                   "Could not get client process id: %s",
                   emsg);
      g_free (emsg);

      return 0;
    }

  return process_id;
}

static gchar *
get_sid_from_token (HANDLE   token,
                    GError **error)
{
  TOKEN_USER *user_info = NULL;
  DWORD user_info_length = 0;
  wchar_t *sid_string;
  gchar *sid_utf8;

  if (!GetTokenInformation (token,
                            TokenUser,
                            user_info,
                            user_info_length,
                            &user_info_length))
    {
      int errsv = GetLastError ();

      if (errsv != ERROR_INSUFFICIENT_BUFFER)
        {
          gchar *emsg = g_win32_error_message (errsv);

          g_set_error (error,
                       G_IO_ERROR,
                       g_io_error_from_win32_error (errsv),
                       "Could not get the token information: %s",
                       emsg);
          g_free (emsg);

          return NULL;
        }
    }
  else
    {
      g_assert_not_reached ();
    }

  user_info = (TOKEN_USER *)g_malloc (user_info_length);

  if (!GetTokenInformation (token,
                            TokenUser,
                            user_info,
                            user_info_length,
                            &user_info_length))
    {
      int errsv = GetLastError ();
      gchar *emsg = g_win32_error_message (errsv);

      g_set_error (error,
                   G_IO_ERROR,
                   g_io_error_from_win32_error (errsv),
                   "Could not get the token information: %s",
                   emsg);
      g_free (emsg);

      g_free (user_info);

      return NULL;
    }

  if (!ConvertSidToStringSidW (user_info->User.Sid, &sid_string))
    {
      int errsv = GetLastError ();
      gchar *emsg = g_win32_error_message (errsv);

      g_set_error (error,
                   G_IO_ERROR,
                   g_io_error_from_win32_error (errsv),
                   "Could not convert Sid to string: %s",
                   emsg);
      g_free (emsg);

      g_free (user_info);

      return NULL;
    }

  g_free (user_info);

  sid_utf8 = g_utf16_to_utf8 ((gunichar2 *)sid_string, -1, NULL, NULL, NULL);
  LocalFree (sid_string);

  return sid_utf8;
}

static gchar *
get_user_id (WingNamedPipeConnection   *connection,
             GError                   **error)
{
  HANDLE  token;
  gchar  *sid;

  if (!ImpersonateNamedPipeClient (connection->handle))
    {
      int errsv = GetLastError ();
      gchar *emsg = g_win32_error_message (errsv);

      g_set_error (error,
                   G_IO_ERROR,
                   g_io_error_from_win32_error (errsv),
                   "Could not impersonate the client: %s",
                   emsg);
      g_free (emsg);

      return NULL;
    }

  if (!OpenThreadToken (GetCurrentThread (), TOKEN_QUERY, FALSE, &token))
    {
      int errsv = GetLastError ();
      gchar *emsg = g_win32_error_message (errsv);

      g_set_error (error,
                   G_IO_ERROR,
                   g_io_error_from_win32_error (errsv),
                   "Could not get the thread's token: %s",
                   emsg);
      g_free (emsg);

      if (!RevertToSelf ())
        {
          errsv = GetLastError ();
          emsg = g_win32_error_message (errsv);

          /*
           * If we reach this point, it is better to abort the execution,
           * because otherwise it will continue in the context of the client,
           * which is not appropriate
           */
          g_error ("Failed to terminate the impersonation of the client: %s (%d)",
                   emsg,
                   errsv);
        }

      return NULL;
    }

  sid = get_sid_from_token (token, error);
  CloseHandle (token);

  if (!RevertToSelf ())
    {
      int errsv = GetLastError ();
      gchar *emsg = g_win32_error_message (errsv);

      /*
       * If we reach this point, it is better to abort the execution,
       * because otherwise it will continue in the context of the client,
       * which is not appropriate
       */
      g_error ("Failed to terminate the impersonation of the client: %s (%d)",
               emsg,
               errsv);
    }

  return sid;
}

/**
 * wing_named_pipe_connection_get_credentials():
 * @connection: a #WingNamedPipeConnection.
 * @error: a #GError location to store the error occurring, or %NULL to
 * ignore.
 *
 * Retrieves the WingCredentials of the pipe's client
 *
 * Returns: (transfer full): a #WingCredentials on success, %NULL on error.
 */
WingCredentials *
wing_named_pipe_connection_get_credentials (WingNamedPipeConnection  *connection,
                                            GError                  **error)
{
  WingCredentials *credentials;
  gulong pid;
  gchar *sid;

  g_return_val_if_fail (WING_IS_NAMED_PIPE_CONNECTION (connection), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  if (connection->handle == NULL || connection->handle == INVALID_HANDLE_VALUE)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_ARGUMENT,
                           "Pipe's handle is NULL or invalid");

      return FALSE;
    }

  pid = get_client_process_id (connection, error);
  if (pid == 0)
    return NULL;

  sid = get_user_id (connection, error);
  if (sid == NULL)
    return NULL;

  credentials = wing_credentials_new (pid, sid);

  g_free (sid);

  return credentials;
}
