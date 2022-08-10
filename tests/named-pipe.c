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

#include <wing/wing.h>
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>

typedef struct
{
  gboolean use_iocp;
  void (*read_write_fn) (GIOStream *server_stream, GIOStream *client_stream);
} TestData;

static void
test_add_named_pipe (gconstpointer user_data)
{
  WingNamedPipeListener *listener;
  GError *error = NULL;
  TestData *test_data = (TestData *) user_data;

  listener = wing_named_pipe_listener_new ("\\\\.\\pipe\\gtest-good-named-pipe-name",
                                           NULL,
                                           FALSE,
                                           NULL,
                                           &error);
  g_assert (listener != NULL);
  g_assert_no_error (error);

  g_object_unref (listener);

  listener = wing_named_pipe_listener_new ("\\\\.\\pipe\\gtest-good-named-pipe-name",
                                           "D:(A;;GA;;;BA)(A;;GA;;;SY)(A;;GA;;;IU)",
                                           FALSE,
                                           NULL,
                                           &error);
  g_assert (listener != NULL);
  g_assert_no_error (error);

  g_object_unref (listener);

  listener = wing_named_pipe_listener_new ("\\\\.\\gtest-bad-named-pipe-name",
                                           NULL,
                                           FALSE,
                                           NULL,
                                           &error);
  g_assert (listener == NULL);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_FAILED);
}

static void
test_add_named_pipe_multiple_instances_no_protect (gconstpointer user_data)
{
  WingNamedPipeListener *listener1;
  WingNamedPipeListener *listener2;
  GError *error = NULL;
  TestData *test_data = (TestData *) user_data;

  listener1 = wing_named_pipe_listener_new ("\\\\.\\pipe\\unprotected-named-pipe",
                                           NULL,
                                           FALSE,
                                           NULL,
                                           &error);
  g_assert (listener1 != NULL);
  g_assert_no_error (error);

  listener2 = wing_named_pipe_listener_new ("\\\\.\\pipe\\unprotected-named-pipe",
                                           NULL,
                                           FALSE,
                                           NULL,
                                           &error);
  g_assert (listener2 != NULL);
  g_assert_no_error (error);

  g_object_unref (listener1);
  g_object_unref (listener2);
}

static void
test_add_named_pipe_multiple_instances_protected (gconstpointer user_data)
{
  WingNamedPipeListener *listener1;
  WingNamedPipeListener *listener2;
  GError *error = NULL;
  TestData *test_data = (TestData *) user_data;

  listener1 = wing_named_pipe_listener_new ("\\\\.\\pipe\\protected-named-pipe",
                                           NULL,
                                           TRUE,
                                           NULL,
                                           &error);
  g_assert (listener1 != NULL);
  g_assert_no_error (error);

  listener2 = wing_named_pipe_listener_new ("\\\\.\\pipe\\protected-named-pipe",
                                           NULL,
                                           TRUE,
                                           NULL,
                                           &error);
  g_assert (listener2 == NULL);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_FAILED);

  g_object_unref (listener1);
}

static void
accepted_cb (GObject      *source,
             GAsyncResult *result,
             gpointer      user_data)
{
  WingNamedPipeListener *listener = WING_NAMED_PIPE_LISTENER (source);
  WingNamedPipeConnection *conn;
  gboolean *success = user_data;
  GError *error = NULL;

  conn = wing_named_pipe_listener_accept_finish (listener, result, &error);
  g_assert_no_error (error);
  g_object_unref (conn);

  *success = TRUE;
}

static void
connected_cb (GObject      *source,
              GAsyncResult *result,
              gpointer      user_data)
{
  WingNamedPipeClient *client = WING_NAMED_PIPE_CLIENT (source);
  WingNamedPipeConnection *conn;
  gboolean *success = user_data;
  GError *error = NULL;

  conn = wing_named_pipe_client_connect_finish (client, result, &error);
  g_assert_no_error (error);
  g_object_unref (conn);

  *success = TRUE;
}

static void
test_connect_basic (gconstpointer user_data)
{
  WingNamedPipeListener *listener;
  WingNamedPipeClient *client;
  gboolean success_accepted = FALSE;
  gboolean success_connected = FALSE;
  GError *error = NULL;
  TestData *test_data = (TestData *) user_data;

  listener = wing_named_pipe_listener_new ("\\\\.\\pipe\\gtest-named-pipe-name",
                                           NULL,
                                           FALSE,
                                           NULL,
                                           &error);
  g_assert (listener != NULL);
  g_assert_no_error (error);

  wing_named_pipe_listener_set_use_iocp (listener, test_data->use_iocp);

  wing_named_pipe_listener_accept_async (listener,
                                         NULL,
                                         accepted_cb,
                                         &success_accepted);

  client = wing_named_pipe_client_new ();
  wing_named_pipe_client_set_use_iocp (client, test_data->use_iocp);

  wing_named_pipe_client_connect_async (client,
                                        "\\\\.\\pipe\\gtest-named-pipe-name",
                                        WING_NAMED_PIPE_CLIENT_GENERIC_READ | WING_NAMED_PIPE_CLIENT_GENERIC_WRITE,
                                        NULL,
                                        connected_cb,
                                        &success_connected);

  do
    g_main_context_iteration (NULL, TRUE);
  while (!success_accepted || !success_connected);

  g_object_unref (client);
  g_object_unref (listener);
}

static void
test_connect_before_accept_sync (gconstpointer user_data)
{
  WingNamedPipeListener *listener;
  WingNamedPipeClient *client;
  gboolean success_accepted = FALSE;
  gboolean success_connected = FALSE;
  GError *client_error = NULL;
  GError *server_error = NULL;
  TestData *test_data = (TestData *) user_data;

  listener = wing_named_pipe_listener_new ("\\\\.\\pipe\\gtest-named-pipe-name",
                                           NULL,
                                           FALSE,
                                           NULL,
                                           &server_error);
  g_assert (listener != NULL);
  g_assert_no_error (server_error);

  wing_named_pipe_listener_set_use_iocp (listener, test_data->use_iocp);

  client = wing_named_pipe_client_new ();
  wing_named_pipe_client_set_use_iocp (client, test_data->use_iocp);

  wing_named_pipe_client_connect (client,
                                  "\\\\.\\pipe\\gtest-named-pipe-name",
                                   WING_NAMED_PIPE_CLIENT_GENERIC_READ | WING_NAMED_PIPE_CLIENT_GENERIC_WRITE,
                                   NULL,
                                   &client_error);

  g_assert_no_error (client_error);

  wing_named_pipe_listener_accept (listener,
                                   NULL,
                                   &server_error);

  g_assert_no_error (server_error);

  g_object_unref (client);
  g_object_unref (listener);
}

static void
test_connect_before_accept (gconstpointer user_data)
{
  WingNamedPipeListener *listener;
  WingNamedPipeClient *client;
  gboolean success_accepted = FALSE;
  gboolean success_connected = FALSE;
  GError *error = NULL;
  TestData *test_data = (TestData *) user_data;

  listener = wing_named_pipe_listener_new ("\\\\.\\pipe\\gtest-named-pipe-name",
                                           NULL,
                                           FALSE,
                                           NULL,
                                           &error);
  g_assert (listener != NULL);
  g_assert_no_error (error);

  wing_named_pipe_listener_set_use_iocp (listener, test_data->use_iocp);

  client = wing_named_pipe_client_new ();
  wing_named_pipe_client_set_use_iocp (client, test_data->use_iocp);

  wing_named_pipe_client_connect_async (client,
                                        "\\\\.\\pipe\\gtest-named-pipe-name",
                                        WING_NAMED_PIPE_CLIENT_GENERIC_READ | WING_NAMED_PIPE_CLIENT_GENERIC_WRITE,
                                        NULL,
                                        connected_cb,
                                        &success_connected);

  wing_named_pipe_listener_accept_async (listener,
                                         NULL,
                                         accepted_cb,
                                         &success_accepted);

  do
    g_main_context_iteration (NULL, TRUE);
  while (!success_accepted || !success_connected);

  g_object_unref (client);
  g_object_unref (listener);
}

static void
test_accept_fail_sync (gconstpointer user_data)
{
  WingNamedPipeListener *listener;
  WingNamedPipeClient *client;
  WingNamedPipeConnection *connection;
  GError *error = NULL;
  TestData *test_data = (TestData *) user_data;

  listener = wing_named_pipe_listener_new ("\\\\.\\pipe\\gtest-named-pipe-name",
                                           NULL,
                                           FALSE,
                                           NULL,
                                           &error);
  g_assert (listener != NULL);
  g_assert_no_error (error);

  wing_named_pipe_listener_set_use_iocp (listener, test_data->use_iocp);

  client = wing_named_pipe_client_new ();
  wing_named_pipe_client_set_use_iocp (client, test_data->use_iocp);

  connection = wing_named_pipe_client_connect (client,
                                               "\\\\.\\pipe\\gtest-named-pipe-name",
                                               WING_NAMED_PIPE_CLIENT_GENERIC_READ | WING_NAMED_PIPE_CLIENT_GENERIC_WRITE,
                                               NULL,
                                               &error);

  g_assert_no_error (error);

  /* This will cause the following accept to fail because the pipe was closed */
  g_object_unref (connection);
  wing_named_pipe_listener_accept (listener, NULL, &error);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);

  g_clear_error (&error);

  connection = wing_named_pipe_client_connect (client,
                                               "\\\\.\\pipe\\gtest-named-pipe-name",
                                               WING_NAMED_PIPE_CLIENT_GENERIC_READ | WING_NAMED_PIPE_CLIENT_GENERIC_WRITE,
                                               NULL,
                                               &error);
  g_assert_no_error (error);

  wing_named_pipe_listener_accept (listener, NULL, &error);
  g_assert_no_error (error);

  g_object_unref (connection);
  g_object_unref (client);
  g_object_unref (listener);
}

static void
test_connect_sync (gconstpointer user_data)
{
  WingNamedPipeListener *listener;
  WingNamedPipeClient *client;
  WingNamedPipeConnection *connection;
  GError *error = NULL;
  TestData *test_data = (TestData *) user_data;

  listener = wing_named_pipe_listener_new ("\\\\.\\pipe\\gtest-connect-sync",
                                           NULL,
                                           FALSE,
                                           NULL,
                                           &error);
  g_assert (listener != NULL);
  g_assert_no_error (error);

  wing_named_pipe_listener_set_use_iocp (listener, test_data->use_iocp);

  client = wing_named_pipe_client_new ();
  wing_named_pipe_client_set_use_iocp (client, test_data->use_iocp);

  connection = wing_named_pipe_client_connect (client,
                                               "\\\\.\\pipe\\gtest-connect-sync",
                                               WING_NAMED_PIPE_CLIENT_GENERIC_READ | WING_NAMED_PIPE_CLIENT_GENERIC_WRITE,
                                               NULL,
                                               &error);

  g_assert_no_error (error);
  g_assert_cmpstr ("\\\\.\\pipe\\gtest-connect-sync", ==, wing_named_pipe_connection_get_pipe_name (connection));

  g_object_unref (connection);
  g_object_unref (client);
  g_object_unref (listener);
}

static void
test_connect_sync_fails (gconstpointer user_data)
{
  WingNamedPipeClient *client;
  WingNamedPipeConnection *connection;
  GError *error = NULL;
  TestData *test_data = (TestData *) user_data;

  client = wing_named_pipe_client_new ();
  wing_named_pipe_client_set_use_iocp (client, test_data->use_iocp);

  connection = wing_named_pipe_client_connect (client,
                                               "\\\\.\\pipe\\gtest-connect-sync-fails",
                                               WING_NAMED_PIPE_CLIENT_GENERIC_READ | WING_NAMED_PIPE_CLIENT_GENERIC_WRITE,
                                               NULL,
                                               &error);

  g_assert (connection == NULL);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_FAILED);

  g_object_unref (client);
}

static void
accept_cancelled_cb (GObject      *source,
                     GAsyncResult *result,
                     gpointer      user_data)
{
  WingNamedPipeListener *listener = WING_NAMED_PIPE_LISTENER (source);
  WingNamedPipeConnection *conn;
  gboolean *cancelled = user_data;
  GError *error = NULL;

  conn = wing_named_pipe_listener_accept_finish (listener, result, &error);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);

  *cancelled = TRUE;
}

static gboolean
on_cancel_task (gpointer user_data)
{
  GCancellable *cancellable = G_CANCELLABLE (user_data);

  g_cancellable_cancel (cancellable);

  return G_SOURCE_REMOVE;
}

static void
test_accept_cancel (gconstpointer user_data)
{
  WingNamedPipeListener *listener;
  GCancellable *cancellable;
  gboolean accept_cancelled = FALSE;
  GError *error = NULL;
  TestData *test_data = (TestData *) user_data;
  
  cancellable = g_cancellable_new ();
  listener = wing_named_pipe_listener_new ("\\\\.\\pipe\\gtest-named-pipe-name-cancel",
                                           NULL,
                                           FALSE,
                                           NULL,
                                           &error);
  g_assert (listener != NULL);
  g_assert_no_error (error);

  wing_named_pipe_listener_set_use_iocp (listener, test_data->use_iocp);

  wing_named_pipe_listener_accept_async (listener,
                                         cancellable,
                                         accept_cancelled_cb,
                                         &accept_cancelled);

  g_timeout_add_seconds (3, on_cancel_task, cancellable);

  do
    g_main_context_iteration (NULL, TRUE);
  while (!accept_cancelled);

  g_object_unref (listener);
  g_object_unref (cancellable);
}

static void
test_connect_accept_cancel (gconstpointer user_data)
{
  WingNamedPipeListener *listener;
  WingNamedPipeClient *client;
  GCancellable *cancellable;
  gboolean success_accepted = FALSE;
  gboolean success_connected = FALSE;
  GError *error = NULL;
  TestData *test_data = (TestData *) user_data;

  cancellable = g_cancellable_new ();
  listener = wing_named_pipe_listener_new ("\\\\.\\pipe\\gtest-named-pipe-name-connect-then-cancel",
                                           NULL,
                                           FALSE,
                                           NULL,
                                           &error);
  g_assert (listener != NULL);
  g_assert_no_error (error);

  wing_named_pipe_listener_set_use_iocp (listener, test_data->use_iocp);

  wing_named_pipe_listener_accept_async (listener,
                                         cancellable,
                                         accepted_cb,
                                         &success_accepted);

  client = wing_named_pipe_client_new ();
  wing_named_pipe_client_set_use_iocp (client, test_data->use_iocp);

  wing_named_pipe_client_connect_async (client,
                                        "\\\\.\\pipe\\gtest-named-pipe-name-connect-then-cancel",
                                        WING_NAMED_PIPE_CLIENT_GENERIC_READ | WING_NAMED_PIPE_CLIENT_GENERIC_WRITE,
                                        NULL,
                                        connected_cb,
                                        &success_connected);

  do
    g_main_context_iteration (NULL, TRUE);
  while (!success_accepted || !success_connected);

  success_accepted = FALSE;

  wing_named_pipe_listener_accept_async (listener,
                                         cancellable,
                                         accept_cancelled_cb,
                                         &success_accepted);

  g_timeout_add_seconds (3, on_cancel_task, cancellable);

  do
    g_main_context_iteration (NULL, TRUE);
  while (!success_accepted);

  g_object_unref (client);
  g_object_unref (listener);
  g_object_unref (cancellable);
}

static void
test_multi_client_basic (gconstpointer user_data)
{
  WingNamedPipeListener *listener;
  WingNamedPipeClient *client;
  GCancellable *cancellable;
  gboolean success_accepted = FALSE;
  gboolean success_connected = FALSE;
  GError *error = NULL;
  TestData *test_data = (TestData *) user_data;

  cancellable = g_cancellable_new ();
  listener = wing_named_pipe_listener_new ("\\\\.\\pipe\\gtest-named-pipe-name-connect-multi-client",
                                           NULL,
                                           FALSE,
                                           NULL,
                                           &error);
  g_assert (listener != NULL);
  g_assert_no_error (error);

  wing_named_pipe_listener_set_use_iocp (listener, test_data->use_iocp);

  wing_named_pipe_listener_accept_async (listener,
                                         cancellable,
                                         accepted_cb,
                                         &success_accepted);

  client = wing_named_pipe_client_new ();
  wing_named_pipe_client_set_use_iocp (client, test_data->use_iocp);

  wing_named_pipe_client_connect_async (client,
                                        "\\\\.\\pipe\\gtest-named-pipe-name-connect-multi-client",
                                        WING_NAMED_PIPE_CLIENT_GENERIC_READ | WING_NAMED_PIPE_CLIENT_GENERIC_WRITE,
                                        NULL,
                                        connected_cb,
                                        &success_connected);

  do
    g_main_context_iteration (NULL, TRUE);
  while (!success_accepted || !success_connected);

  g_object_unref (client);

  success_accepted = FALSE;
  success_connected = FALSE;

  wing_named_pipe_listener_accept_async (listener,
                                         cancellable,
                                         accepted_cb,
                                         &success_accepted);

  client = wing_named_pipe_client_new ();
  wing_named_pipe_client_set_use_iocp (client, test_data->use_iocp);

  wing_named_pipe_client_connect_async (client,
                                        "\\\\.\\pipe\\gtest-named-pipe-name-connect-multi-client",
                                        WING_NAMED_PIPE_CLIENT_GENERIC_READ | WING_NAMED_PIPE_CLIENT_GENERIC_WRITE,
                                        NULL,
                                        connected_cb,
                                        &success_connected);

  do
    g_main_context_iteration (NULL, TRUE);
  while (!success_accepted || !success_connected);

  g_object_unref (client);
  g_object_unref (listener);
  g_object_unref (cancellable);
}

static void
test_client_default_timeout (gconstpointer user_data)
{
  WingNamedPipeClient *client;
  guint timeout;
  TestData *test_data = (TestData *) user_data;

  client = wing_named_pipe_client_new ();
  wing_named_pipe_client_set_use_iocp (client, test_data->use_iocp);

  g_object_get (G_OBJECT (client), "timeout", &timeout, NULL);

  g_assert_cmpuint (timeout, ==, NMPWAIT_WAIT_FOREVER);
  g_object_unref (client);
}

static void
accepted_read_write_cb (GObject      *source,
                        GAsyncResult *result,
                        gpointer      user_data)
{
  WingNamedPipeListener *listener = WING_NAMED_PIPE_LISTENER (source);
  WingNamedPipeConnection **conn = user_data;
  GError *error = NULL;

  *conn = wing_named_pipe_listener_accept_finish (listener, result, &error);
  g_assert_no_error (error);
}

static void
connected_read_write_cb (GObject      *source,
                         GAsyncResult *result,
                         gpointer      user_data)
{
  WingNamedPipeClient *client = WING_NAMED_PIPE_CLIENT (source);
  WingNamedPipeConnection **conn = user_data;
  GError *error = NULL;

  *conn = wing_named_pipe_client_connect_finish (client, result, &error);
  g_assert_no_error (error);
}

typedef struct
{
  gchar data[256];
  gboolean *read;
} ReadData;

#define MAX_ITERATIONS 100
static const gchar *some_text = "This is some data to read and to write";

static void
on_some_text_read (GObject      *source,
                   GAsyncResult *result,
                   gpointer      user_data)
{
  ReadData *data = user_data;
  gssize read;
  GError *error = NULL;

  read = g_input_stream_read_finish (G_INPUT_STREAM (source), result, &error);
  g_assert_no_error (error);
  g_assert_cmpint (read, ==, strlen (some_text) + 1);

  *data->read = TRUE;
  g_free (data);
}

static void
on_some_text_written (GObject      *source,
                      GAsyncResult *result,
                      gpointer      user_data)
{
  gboolean *wrote = user_data;
  gssize read;
  GError *error = NULL;

  read = g_output_stream_write_finish (G_OUTPUT_STREAM (source), result, &error);
  g_assert_no_error (error);
  g_assert_cmpint (read, ==, strlen (some_text) + 1);

  *wrote = TRUE;
}

static void
write_and_read (GIOStream *server_stream,
                GIOStream *client_stream)
{
  gint i;

  for (i = 0; i < MAX_ITERATIONS; i++)
    {
      ReadData *data;
      GInputStream *in;
      GOutputStream *out;
      gboolean server_wrote = FALSE;
      gboolean server_read = FALSE;
      gboolean client_wrote = FALSE;
      gboolean client_read = FALSE;

      /* Server */
      out = g_io_stream_get_output_stream (server_stream);
      in = g_io_stream_get_input_stream (server_stream);

      g_output_stream_write_async (out,
                                   some_text,
                                   strlen (some_text) + 1,
                                   G_PRIORITY_DEFAULT,
                                   NULL,
                                   on_some_text_written,
                                   &server_wrote);

      data = g_new0 (ReadData, 1);
      data->read = &server_read;
      g_input_stream_read_async (in,
                                 data->data,
                                 sizeof (data->data),
                                 G_PRIORITY_DEFAULT,
                                 NULL,
                                 on_some_text_read,
                                 data);

      /* Client */
      out = g_io_stream_get_output_stream (client_stream);
      in = g_io_stream_get_input_stream (client_stream);

      g_output_stream_write_async (out,
                                   some_text,
                                   strlen (some_text) + 1,
                                   G_PRIORITY_DEFAULT,
                                   NULL,
                                   on_some_text_written,
                                   &client_wrote);

      data = g_new0 (ReadData, 1);
      data->read = &client_read;
      g_input_stream_read_async (in,
                                 data->data,
                                 sizeof (data->data),
                                 G_PRIORITY_DEFAULT,
                                 NULL,
                                 on_some_text_read,
                                 data);

      do
        g_main_context_iteration (NULL, TRUE);
      while (!server_wrote || !client_wrote ||
             !server_read || !client_read);
    }
}

static void
write_and_read_mix_sync_async (GIOStream *server_stream,
                               GIOStream *client_stream)
{
  gint i;

  for (i = 0; i < MAX_ITERATIONS; i++)
    {
      ReadData *data;
      GInputStream *in;
      GOutputStream *out;
      gboolean server_read = FALSE;
      gboolean client_wrote = FALSE;
      GError *error = NULL;
      gssize readed_bytes;
      gssize written_bytes;

      /* Server */
      out = g_io_stream_get_output_stream (server_stream);
      in = g_io_stream_get_input_stream (server_stream);

      written_bytes = g_output_stream_write (out,
                                             some_text,
                                             strlen (some_text) + 1,
                                             NULL,
                                             &error);
      g_assert_no_error (error);
      g_assert_cmpint (written_bytes, == , strlen (some_text) + 1);

      data = g_new0 (ReadData, 1);
      data->read = &server_read;
      g_input_stream_read_async (in,
                                 data->data,
                                 sizeof (data->data),
                                 G_PRIORITY_DEFAULT,
                                 NULL,
                                 on_some_text_read,
                                 data);

      /* Client */
      out = g_io_stream_get_output_stream (client_stream);
      in = g_io_stream_get_input_stream (client_stream);

      g_output_stream_write_async (out,
                                   some_text,
                                   strlen (some_text) + 1,
                                   G_PRIORITY_DEFAULT,
                                   NULL,
                                   on_some_text_written,
                                   &client_wrote);

      data = g_new0 (ReadData, 1);
      readed_bytes = g_input_stream_read (in,
                                          data->data,
                                          sizeof (data->data),
                                          NULL,
                                          &error);
      g_assert_no_error (error);
      g_assert_cmpint (readed_bytes, == , strlen (some_text) + 1);

      do
        g_main_context_iteration (NULL, TRUE);
      while (!client_wrote || !server_read);
    }
}

static void
test_read_write_basic (gconstpointer user_data)
{
  WingNamedPipeListener *listener;
  WingNamedPipeClient *client;
  WingNamedPipeConnection *conn_server = NULL;
  WingNamedPipeConnection *conn_client = NULL;
  GError *error = NULL;
  TestData *test_data = (TestData *) user_data;

  listener = wing_named_pipe_listener_new ("\\\\.\\pipe\\gtest-named-pipe-name",
                                           NULL,
                                           FALSE,
                                           NULL,
                                           &error);
  g_assert (listener != NULL);
  g_assert_no_error (error);

  wing_named_pipe_listener_set_use_iocp (listener, test_data->use_iocp);

  wing_named_pipe_listener_accept_async (listener,
                                         NULL,
                                         accepted_read_write_cb,
                                         &conn_server);

  client = wing_named_pipe_client_new ();
  wing_named_pipe_client_set_use_iocp (client, test_data->use_iocp);

  wing_named_pipe_client_connect_async (client,
                                        "\\\\.\\pipe\\gtest-named-pipe-name",
                                        WING_NAMED_PIPE_CLIENT_GENERIC_READ | WING_NAMED_PIPE_CLIENT_GENERIC_WRITE,
                                        NULL,
                                        connected_read_write_cb,
                                        &conn_client);

  do
    g_main_context_iteration (NULL, TRUE);
  while (conn_server == NULL || conn_client == NULL);

  test_data->read_write_fn (G_IO_STREAM (conn_server),
                            G_IO_STREAM (conn_client));

  g_object_unref (conn_client);
  g_object_unref (conn_server);
  g_object_unref (client);
  g_object_unref (listener);
}

static void
test_read_write_several_connections (gconstpointer user_data)
{
  WingNamedPipeListener *listener;
  gint i;
  GError *error = NULL;
  TestData *test_data = (TestData *) user_data;

  listener = wing_named_pipe_listener_new ("\\\\.\\pipe\\gtest-named-pipe-name-read-write-several",
                                           NULL,
                                           FALSE,
                                           NULL,
                                           &error);
  g_assert (listener != NULL);
  g_assert_no_error (error);
  
  wing_named_pipe_listener_set_use_iocp (listener, test_data->use_iocp);

  for (i = 0; i < MAX_ITERATIONS; i++)
    {
      WingNamedPipeClient *client;
      WingNamedPipeConnection *conn_server = NULL;
      WingNamedPipeConnection *conn_client = NULL;

      wing_named_pipe_listener_accept_async (listener,
                                             NULL,
                                             accepted_read_write_cb,
                                             &conn_server);

      client = wing_named_pipe_client_new ();
      wing_named_pipe_client_set_use_iocp (client, test_data->use_iocp);

      wing_named_pipe_client_connect_async (client,
                                            "\\\\.\\pipe\\gtest-named-pipe-name-read-write-several",
                                            WING_NAMED_PIPE_CLIENT_GENERIC_READ | WING_NAMED_PIPE_CLIENT_GENERIC_WRITE,
                                            NULL,
                                            connected_read_write_cb,
                                            &conn_client);

      do
        g_main_context_iteration (NULL, TRUE);
      while (conn_server == NULL || conn_client == NULL);

      test_data->read_write_fn (G_IO_STREAM(conn_server),
                                G_IO_STREAM(conn_client));

      g_object_unref (conn_client);
      g_object_unref (conn_server);
      g_object_unref (client);
    }

  g_object_unref (listener);
}

static void
test_read_write_same_time_several_connections (gconstpointer user_data)
{
  WingNamedPipeListener *listener;
  GPtrArray *client_conns;
  GPtrArray *server_conns;
  gint i;
  GError *error = NULL;
  TestData *test_data = (TestData *) user_data;

  listener = wing_named_pipe_listener_new ("\\\\.\\pipe\\gtest-named-pipe-name-read-write-several",
                                           NULL,
                                           FALSE,
                                           NULL,
                                           &error);
  g_assert (listener != NULL);
  g_assert_no_error (error);

  wing_named_pipe_listener_set_use_iocp (listener, test_data->use_iocp);

  client_conns = g_ptr_array_new_with_free_func (g_object_unref);
  server_conns = g_ptr_array_new_with_free_func (g_object_unref);

  for (i = 0; i < MAX_ITERATIONS; i++)
    {
      WingNamedPipeClient *client;
      WingNamedPipeConnection *conn_server = NULL;
      WingNamedPipeConnection *conn_client = NULL;

      wing_named_pipe_listener_accept_async (listener,
                                             NULL,
                                             accepted_read_write_cb,
                                             &conn_server);

      client = wing_named_pipe_client_new ();
      wing_named_pipe_client_set_use_iocp (client, test_data->use_iocp);

      wing_named_pipe_client_connect_async (client,
                                            "\\\\.\\pipe\\gtest-named-pipe-name-read-write-several",
                                            WING_NAMED_PIPE_CLIENT_GENERIC_READ | WING_NAMED_PIPE_CLIENT_GENERIC_WRITE,
                                            NULL,
                                            connected_read_write_cb,
                                            &conn_client);

      do
        g_main_context_iteration (NULL, TRUE);
      while (conn_server == NULL || conn_client == NULL);

      g_ptr_array_add (client_conns, conn_client);
      g_ptr_array_add (server_conns, conn_server);

      g_object_unref (client);
    }

  for (i = 0; i < MAX_ITERATIONS; i++)
    {
      WingNamedPipeConnection *conn_server = client_conns->pdata[i];
      WingNamedPipeConnection *conn_client = server_conns->pdata[i];

      test_data->read_write_fn (G_IO_STREAM(conn_server),
                                G_IO_STREAM(conn_client));
    }

  g_ptr_array_unref (client_conns);
  g_ptr_array_unref (server_conns);

  g_object_unref (listener);
}

static void
read_cancelled_cb (GObject      *source,
                   GAsyncResult *result,
                   gpointer      user_data)
{
  gboolean *cancelled = user_data;
  gboolean res;
  GError *error = NULL;

  res = g_input_stream_read_all_finish (G_INPUT_STREAM(source), result, NULL, &error);
  g_assert(!res);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);

  *cancelled = TRUE;
}

static void
test_cancel_read (gconstpointer user_data)
{
  WingNamedPipeListener *listener;
  WingNamedPipeClient *client;
  WingNamedPipeConnection *conn_server = NULL;
  WingNamedPipeConnection *conn_client = NULL;
  GInputStream *in;
  gboolean read_cancelled = FALSE;
  GCancellable  *cancellable;
  GError *error = NULL;
  gchar data[256];
  TestData *test_data = (TestData *) user_data;

  listener = wing_named_pipe_listener_new ("\\\\.\\pipe\\gtest-named-pipe-name",
                                           NULL,
                                           FALSE,
                                           NULL,
                                           &error);
  g_assert (listener != NULL);
  g_assert_no_error (error);

  wing_named_pipe_listener_set_use_iocp (listener, test_data->use_iocp);

  wing_named_pipe_listener_accept_async (listener,
                                         NULL,
                                         accepted_read_write_cb,
                                         &conn_server);

  client = wing_named_pipe_client_new ();
  wing_named_pipe_client_set_use_iocp (client, test_data->use_iocp);

  wing_named_pipe_client_connect_async (client,
                                        "\\\\.\\pipe\\gtest-named-pipe-name",
                                        WING_NAMED_PIPE_CLIENT_GENERIC_READ | WING_NAMED_PIPE_CLIENT_GENERIC_WRITE,
                                        NULL,
                                        connected_read_write_cb,
                                        &conn_client);

  do
    g_main_context_iteration (NULL, TRUE);
  while (conn_server == NULL || conn_client == NULL);

  in = g_io_stream_get_input_stream (G_IO_STREAM(conn_server));
  cancellable = g_cancellable_new();
  g_input_stream_read_all_async(in,
                                data,
                                sizeof (data),
                                G_PRIORITY_DEFAULT,
                                cancellable,
                                read_cancelled_cb,
                                &read_cancelled);

  g_timeout_add_seconds (1, on_cancel_task, cancellable);

  do
    g_main_context_iteration (NULL, TRUE);
  while (!read_cancelled);

  g_object_unref (conn_client);
  g_object_unref (conn_server);
  g_object_unref (client);
  g_object_unref (listener);
  g_object_unref (cancellable);
}

int
main (int   argc,
      char *argv[])
{
  TestData test_data;
  TestData test_data_sync_async;
  TestData test_data_iocp;
  TestData test_data_iocp_sync_async;

  g_test_init (&argc, &argv, NULL);

  g_test_bug_base ("http://bugzilla.gnome.org/");

  test_data.use_iocp = FALSE;
  test_data.read_write_fn = write_and_read;

  test_data_sync_async.use_iocp = FALSE;
  test_data_sync_async.read_write_fn = write_and_read_mix_sync_async;

  test_data_iocp.use_iocp = TRUE;
  test_data_iocp.read_write_fn = write_and_read;

  test_data_iocp_sync_async.use_iocp = TRUE;
  test_data_iocp_sync_async.read_write_fn = write_and_read_mix_sync_async;

  g_test_add_data_func ("/named-pipes/add-named-pipe", &test_data, test_add_named_pipe);
  g_test_add_data_func ("/named-pipes/add-named-pipe-multiple-instances-no-protect", &test_data, test_add_named_pipe_multiple_instances_no_protect);
  g_test_add_data_func ("/named-pipes/add-named-pipe-multiple-instances-protected", &test_data, test_add_named_pipe_multiple_instances_protected);
  g_test_add_data_func ("/named-pipes/connect-basic", &test_data, test_connect_basic);
  g_test_add_data_func ("/named-pipes/connect-before-accept", &test_data, test_connect_before_accept);
  g_test_add_data_func ("/named-pipes/connect-before-accept-sync", &test_data, test_connect_before_accept_sync);
  g_test_add_data_func ("/named-pipes/connect-sync", &test_data, test_connect_sync);
  g_test_add_data_func ("/named-pipes/connect-sync-fails", &test_data, test_connect_sync_fails);
  g_test_add_data_func ("/named-pipes/accept-cancel", &test_data, test_accept_cancel);
  g_test_add_data_func ("/named-pipes/accept-fail-sync", &test_data, test_accept_fail_sync);
  g_test_add_data_func ("/named-pipes/connect-accept-cancel", &test_data, test_connect_accept_cancel);
  g_test_add_data_func ("/named-pipes/multi-client-basic", &test_data, test_multi_client_basic);
  g_test_add_data_func ("/named-pipes/client-default-timeout", &test_data, test_client_default_timeout);
  g_test_add_data_func ("/named-pipes/read-write-basic", &test_data, test_read_write_basic);
  g_test_add_data_func ("/named-pipes/read-write-several-connections", &test_data, test_read_write_several_connections);
  g_test_add_data_func ("/named-pipes/read-write-same-time-several-connections", &test_data, test_read_write_same_time_several_connections);
  g_test_add_data_func ("/named-pipes/read-write-mix-sync-async-basic", &test_data_sync_async, test_read_write_basic);
  g_test_add_data_func ("/named-pipes/read-write-mix-sync-async-several-connections", &test_data_sync_async, test_read_write_several_connections);
  g_test_add_data_func ("/named-pipes/read-write-mix-sync-async-same-time-several-connections", &test_data_sync_async, test_read_write_same_time_several_connections);
  g_test_add_data_func ("/named-pipes/test_cancel_read", &test_data_sync_async, test_cancel_read);

  /* I/O completion port tests */
  g_test_add_data_func ("/named-pipes-iocp/add-named-pipe", &test_data_iocp, test_add_named_pipe);
  g_test_add_data_func ("/named-pipes-iocp/connect-basic", &test_data_iocp, test_connect_basic);
  g_test_add_data_func ("/named-pipes-iocp/connect-before-accept", &test_data_iocp, test_connect_before_accept);
  g_test_add_data_func ("/named-pipes-iocp/connect-before-accept-sync", &test_data_iocp, test_connect_before_accept_sync);
  g_test_add_data_func ("/named-pipes-iocp/connect-sync", &test_data_iocp, test_connect_sync);
  g_test_add_data_func ("/named-pipes-iocp/connect-sync-fails", &test_data_iocp, test_connect_sync_fails);
  g_test_add_data_func ("/named-pipes-iocp/accept-cancel", &test_data_iocp, test_accept_cancel);
  g_test_add_data_func ("/named-pipes-iocp/accept-fail-sync", &test_data_iocp, test_accept_fail_sync);
  g_test_add_data_func ("/named-pipes-iocp/connect-accept-cancel", &test_data_iocp, test_connect_accept_cancel);
  g_test_add_data_func ("/named-pipes-iocp/multi-client-basic", &test_data_iocp, test_multi_client_basic);
  g_test_add_data_func ("/named-pipes-iocp/client-default-timeout", &test_data_iocp, test_client_default_timeout);
  g_test_add_data_func ("/named-pipes-iocp/read-write-basic", &test_data_iocp, test_read_write_basic);
  g_test_add_data_func ("/named-pipes-iocp/read-write-several-connections", &test_data_iocp, test_read_write_several_connections);
  g_test_add_data_func ("/named-pipes-iocp/read-write-same-time-several-connections", &test_data_iocp, test_read_write_same_time_several_connections);
  g_test_add_data_func ("/named-pipes-iocp/read-write-mix-sync-async-basic", &test_data_iocp_sync_async, test_read_write_basic);
  g_test_add_data_func ("/named-pipes-iocp/read-write-mix-sync-async-several-connections", &test_data_iocp_sync_async, test_read_write_several_connections);
  g_test_add_data_func ("/named-pipes-iocp/read-write-mix-sync-async-same-time-several-connections", &test_data_iocp_sync_async, test_read_write_same_time_several_connections);
  g_test_add_data_func ("/named-pipes-iocp/test_cancel_read", &test_data_iocp_sync_async, test_cancel_read);

  return g_test_run ();
}
