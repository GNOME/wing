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

static void
accept_cancelled_cb (GObject      *source,
                     GAsyncResult *result,
                     gpointer      user_data)
{
  WingNamedPipeListener *listener = WING_NAMED_PIPE_LISTENER (source);
  WingNamedPipeConnection *conn;
  gboolean *cancelled = user_data;
  GError *error = NULL;

  conn = wing_named_pipe_listener_accept_finish (listener, result, NULL, &error);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);

  *cancelled = TRUE;
}

static gboolean
on_cancel_accept (gpointer user_data)
{
  GCancellable *cancellable = G_CANCELLABLE (user_data);

  g_cancellable_cancel (cancellable);

  return G_SOURCE_REMOVE;
}

static void
test_accept_cancel (void)
{
  WingNamedPipeListener *listener;
  GCancellable *cancellable;
  gboolean accept_cancelled = FALSE;
  GError *error = NULL;

  cancellable = g_cancellable_new ();
  listener = wing_named_pipe_listener_new ();

  wing_named_pipe_listener_add_named_pipe (listener,
                                           "\\\\.\\pipe\\gtest-named-pipe-name-cancel",
                                           NULL,
                                           &error);
  g_assert_no_error (error);

  wing_named_pipe_listener_accept_async (listener,
                                         cancellable,
                                         accept_cancelled_cb,
                                         &accept_cancelled);

  g_timeout_add_seconds (5, on_cancel_accept, cancellable);

  do
    g_main_context_iteration (NULL, TRUE);
  while (!accept_cancelled);

  g_object_unref (listener);
  g_object_unref (cancellable);
}

static void
test_connect_accept_cancel (void)
{
  WingNamedPipeListener *listener;
  WingNamedPipeClient *client;
  GCancellable *cancellable;
  gboolean success_accepted = FALSE;
  gboolean success_connected = FALSE;
  GError *error = NULL;

  cancellable = g_cancellable_new ();
  listener = wing_named_pipe_listener_new ();

  wing_named_pipe_listener_add_named_pipe (listener,
                                           "\\\\.\\pipe\\gtest-named-pipe-name-connect-then-cancel",
                                           NULL,
                                           &error);
  g_assert_no_error (error);

  wing_named_pipe_listener_accept_async (listener,
                                         cancellable,
                                         accepted_cb,
                                         &success_accepted);

  client = wing_named_pipe_client_new ();
  wing_named_pipe_client_connect_async (client,
                                        "\\\\.\\pipe\\gtest-named-pipe-name-connect-then-cancel",
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

  g_timeout_add_seconds (5, on_cancel_accept, cancellable);

  do
    g_main_context_iteration (NULL, TRUE);
  while (!success_accepted);

  g_object_unref (client);
  g_object_unref (listener);
  g_object_unref (cancellable);
}

static void
test_multi_client_basic (void)
{
  WingNamedPipeListener *listener;
  WingNamedPipeClient *client;
  GCancellable *cancellable;
  gboolean success_accepted = FALSE;
  gboolean success_connected = FALSE;
  GError *error = NULL;

  cancellable = g_cancellable_new ();
  listener = wing_named_pipe_listener_new ();

  wing_named_pipe_listener_add_named_pipe (listener,
                                           "\\\\.\\pipe\\gtest-named-pipe-name-connect-multi-client",
                                           NULL,
                                           &error);
  g_assert_no_error (error);

  wing_named_pipe_listener_accept_async (listener,
                                         cancellable,
                                         accepted_cb,
                                         &success_accepted);

  client = wing_named_pipe_client_new ();
  wing_named_pipe_client_connect_async (client,
                                        "\\\\.\\pipe\\gtest-named-pipe-name-connect-multi-client",
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
  wing_named_pipe_client_connect_async (client,
                                        "\\\\.\\pipe\\gtest-named-pipe-name-connect-multi-client",
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
  g_test_add_func ("/named-pipes/accept-cancel", test_accept_cancel);
  g_test_add_func ("/named-pipes/connect-accept-cancel", test_connect_accept_cancel);
  g_test_add_func ("/named-pipes/multi-client-basic", test_multi_client_basic);

  return g_test_run ();
}
