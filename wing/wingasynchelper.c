/*
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


#include "wingasynchelper.h"


typedef struct {
  GSource source;
  GPollFD pollfd;
} WingHandleSource;

static gboolean
wing_handle_source_prepare (GSource *source,
                            gint    *timeout)
{
  *timeout = -1;
  return FALSE;
}

static gboolean
wing_handle_source_check (GSource *source)
{
  WingHandleSource *hsource = (WingHandleSource *)source;

  return hsource->pollfd.revents;
}

static gboolean
wing_handle_source_dispatch (GSource     *source,
                             GSourceFunc  callback,
                             gpointer     user_data)
{
  WingHandleSourceFunc func = (WingHandleSourceFunc)callback;
  WingHandleSource *hsource = (WingHandleSource *)source;

  return func (hsource->pollfd.fd, user_data);
}

static void
wing_handle_source_finalize (GSource *source)
{
}

static gboolean
wing_handle_source_closure_callback (HANDLE   handle,
                                     gpointer data)
{
  GClosure *closure = data;

  GValue param = G_VALUE_INIT;
  GValue result_value = G_VALUE_INIT;
  gboolean result;

  g_value_init (&result_value, G_TYPE_BOOLEAN);

  g_value_init (&param, G_TYPE_POINTER);
  g_value_set_pointer (&param, handle);

  g_closure_invoke (closure, &result_value, 1, &param, NULL);

  result = g_value_get_boolean (&result_value);
  g_value_unset (&result_value);
  g_value_unset (&param);

  return result;
}

GSourceFuncs wing_handle_source_funcs = {
  wing_handle_source_prepare,
  wing_handle_source_check,
  wing_handle_source_dispatch,
  wing_handle_source_finalize,
  (GSourceFunc)wing_handle_source_closure_callback,
};

GSource *
_wing_handle_create_source (HANDLE        handle,
                            GCancellable *cancellable)
{
  WingHandleSource *hsource;
  GSource *source;

  source = g_source_new (&wing_handle_source_funcs, sizeof (WingHandleSource));
  hsource = (WingHandleSource *)source;
  g_source_set_name (source, "WingHandle");

  if (cancellable)
    {
      GSource *cancellable_source;

      cancellable_source = g_cancellable_source_new (cancellable);
      g_source_add_child_source (source, cancellable_source);
      g_source_set_dummy_callback (cancellable_source);
      g_source_unref (cancellable_source);
    }

  hsource->pollfd.fd = (gint)handle;
  hsource->pollfd.events = G_IO_IN;
  hsource->pollfd.revents = 0;
  g_source_add_poll (source, &hsource->pollfd);

  return source;
}
