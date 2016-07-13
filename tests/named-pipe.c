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

static void
test_add_named_pipe (void)
{
  WingNamedPipeListener *listener;
  GError *error = NULL;

  listener = wing_named_pipe_listener_new ();

  wing_named_pipe_listener_add_named_pipe (listener,
                                           "\\\\.\\pipe\\gtest-good-named-pipe-name",
                                           NULL,
                                           &error);
  g_assert_no_error (error);

  wing_named_pipe_listener_add_named_pipe (listener,
                                           "\\\\.\\gtest-bad-named-pipe-name",
                                           NULL,
                                           &error);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_FAILED);

  g_object_unref (listener);
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

  conn = wing_named_pipe_listener_accept_finish (listener, result, NULL, &error);
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
test_connect_basic (void)
{
  WingNamedPipeListener *listener;
  WingNamedPipeClient *client;
  gboolean success_accepted = FALSE;
  gboolean success_connected = FALSE;
  GError *error = NULL;

  listener = wing_named_pipe_listener_new ();

  wing_named_pipe_listener_add_named_pipe (listener,
                                           "\\\\.\\pipe\\gtest-named-pipe-name",
                                           NULL,
                                           &error);
  g_assert_no_error (error);

  wing_named_pipe_listener_accept_async (listener,
                                         NULL,
                                         accepted_cb,
                                         &success_accepted);

  client = wing_named_pipe_client_new ();
  wing_named_pipe_client_connect_async (client,
                                        "\\\\.\\pipe\\gtest-named-pipe-name",
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
test_connect_before_accept (void)
{
  WingNamedPipeListener *listener;
  WingNamedPipeClient *client;
  gboolean success_accepted = FALSE;
  gboolean success_connected = FALSE;
  GError *error = NULL;

  listener = wing_named_pipe_listener_new ();

  wing_named_pipe_listener_add_named_pipe (listener,
                                           "\\\\.\\pipe\\gtest-named-pipe-name",
                                           NULL,
                                           &error);
  g_assert_no_error (error);

  client = wing_named_pipe_client_new ();
  wing_named_pipe_client_connect_async (client,
                                        "\\\\.\\pipe\\gtest-named-pipe-name",
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
test_connect_sync (void)
{
  WingNamedPipeListener *listener;
  WingNamedPipeClient *client;
  WingNamedPipeConnection *connection;
  GError *error = NULL;

  listener = wing_named_pipe_listener_new ();

  wing_named_pipe_listener_add_named_pipe (listener,
                                           "\\\\.\\pipe\\gtest-connect-sync",
                                           NULL,
                                           &error);
  g_assert_no_error (error);

  client = wing_named_pipe_client_new ();
  connection = wing_named_pipe_client_connect (client,
                                               "\\\\.\\pipe\\gtest-connect-sync",
                                               NULL,
                                               &error);

  g_assert_no_error (error);

  g_object_unref (client);
  g_object_unref (listener);
}

static void
test_connect_sync_fails (void)
{
  WingNamedPipeClient *client;
  WingNamedPipeConnection *connection;
  GError *error = NULL;

  client = wing_named_pipe_client_new ();
  connection = wing_named_pipe_client_connect (client,
                                               "\\\\.\\pipe\\gtest-connect-sync-fails",
                                               NULL,
                                               &error);

  g_assert (connection == NULL);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_FAILED);

  g_object_unref (client);
}

int
main (int   argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);

  g_test_bug_base ("http://bugzilla.gnome.org/");

  g_test_add_func ("/named-pipes/add-named-pipe", test_add_named_pipe);
  g_test_add_func ("/named-pipes/connect-basic", test_connect_basic);
  g_test_add_func ("/named-pipes/connect-before-accept", test_connect_before_accept);
  g_test_add_func ("/named-pipes/connect-sync", test_connect_sync);
  g_test_add_func ("/named-pipes/connect-sync-fails", test_connect_sync_fails);

  return g_test_run ();
}
