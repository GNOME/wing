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


#include "wingsource.h"


typedef struct {
  GSource source;
  GPollFD pollfd;
} WingSource;

static gboolean
wing_source_prepare (GSource *source,
                     gint    *timeout)
{
  *timeout = -1;
  return FALSE;
}

static gboolean
wing_source_check (GSource *source)
{
  WingSource *hsource = (WingSource *)source;

  return hsource->pollfd.revents;
}

static gboolean
wing_source_dispatch (GSource     *source,
                      GSourceFunc  callback,
                      gpointer     user_data)
{
  WingSourceFunc func = (WingSourceFunc)callback;
  WingSource *hsource = (WingSource *)source;

  return func ((HANDLE)hsource->pollfd.fd, user_data);
}

static void
wing_source_finalize (GSource *source)
{
}

static gboolean
wing_source_closure_callback (HANDLE   handle,
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

GSourceFuncs wing_source_funcs = {
  wing_source_prepare,
  wing_source_check,
  wing_source_dispatch,
  wing_source_finalize,
  (GSourceFunc)wing_source_closure_callback,
};

/**
 * wing_create_source:
 * @handle: a windows HANDLE
 * @condition: a #GIOCondition mask to monitor
 * @cancellable: (allow-none): a %GCancellable or %NULL
 *
 * Creates a #GSource that can be attached to a %GMainContext to monitor
 * for the availability of the specified @condition on the handle.
 *
 * The callback on the source is of the #WingSourceFunc type.
 *
 * @cancellable if not %NULL can be used to cancel the source, which will
 * cause the source to trigger, reporting the current condition (which
 * is likely 0 unless cancellation happened at the same time as a
 * condition change). You can check for this in the callback using
 * g_cancellable_is_cancelled().
 *
 * Returns: (transfer full): a newly allocated %GSource, free with g_source_unref().
 */
GSource *
wing_create_source (HANDLE        handle,
                    GIOCondition  condition,
                    GCancellable *cancellable)
{
  WingSource *hsource;
  GSource *source;

  source = g_source_new (&wing_source_funcs, sizeof (WingSource));
  hsource = (WingSource *)source;
  g_source_set_name (source, "WingSource");

  if (cancellable)
    {
      GSource *cancellable_source;

      cancellable_source = g_cancellable_source_new (cancellable);
      g_source_add_child_source (source, cancellable_source);
      g_source_set_dummy_callback (cancellable_source);
      g_source_unref (cancellable_source);
    }

#if GLIB_SIZEOF_VOID_P == 8
  hsource->pollfd.fd = (gint64)handle;
#else
  hsource->pollfd.fd = (gint)handle;
#endif
  hsource->pollfd.events = condition;
  hsource->pollfd.revents = 0;
  g_source_add_poll (source, &hsource->pollfd);

  return source;
}
