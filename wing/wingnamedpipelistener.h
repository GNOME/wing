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

#ifndef WING_NAMED_PIPE_LISTENER_H
#define WING_NAMED_PIPE_LISTENER_H

#include <gio/gio.h>
#include <wing/wing.h>

G_BEGIN_DECLS

#define WING_TYPE_NAMED_PIPE_LISTENER            (wing_named_pipe_listener_get_type ())
#define WING_NAMED_PIPE_LISTENER(o)              (G_TYPE_CHECK_INSTANCE_CAST ((o), WING_TYPE_NAMED_PIPE_LISTENER, WingNamedPipeListener))
#define WING_NAMED_PIPE_LISTENER_CLASS(k)        (G_TYPE_CHECK_CLASS_CAST ((k), WING_TYPE_NAMED_PIPE_LISTENER, WingNamedPipeListenerClass))
#define WING_IS_NAMED_PIPE_LISTENER(o)           (G_TYPE_CHECK_INSTANCE_TYPE ((o), WING_TYPE_NAMED_PIPE_LISTENER))
#define WING_IS_NAMED_PIPE_LISTENER_CLASS(k)     (G_TYPE_CHECK_CLASS_TYPE ((k),  WING_TYPE_NAMED_PIPE_LISTENER))
#define WING_NAMED_PIPE_LISTENER_GET_CLASS(o)    (G_TYPE_INSTANCE_GET_CLASS ((o), WING_TYPE_NAMED_PIPE_LISTENER, WingNamedPipeListenerClass))

typedef struct _WingNamedPipeListener                       WingNamedPipeListener;
typedef struct _WingNamedPipeListenerClass                  WingNamedPipeListenerClass;

struct _WingNamedPipeListener
{
  /*< private >*/
  GObject parent_instance;
};

struct _WingNamedPipeListenerClass
{
  GObjectClass parent_class;

  /*< private >*/
  gpointer padding[10];
};

WING_AVAILABLE_IN_ALL
GType                     wing_named_pipe_listener_get_type       (void) G_GNUC_CONST;

WING_AVAILABLE_IN_ALL
WingNamedPipeListener    *wing_named_pipe_listener_new            (const gchar            *pipe_name,
                                                                   const gchar            *security_descriptor,
                                                                   gboolean                protect_first_instance,
                                                                   GCancellable           *cancellable,
                                                                   GError                **error);

WING_AVAILABLE_IN_ALL
WingNamedPipeConnection  *wing_named_pipe_listener_accept         (WingNamedPipeListener  *listener,
                                                                   GCancellable           *cancellable,
                                                                   GError                **error);

WING_AVAILABLE_IN_ALL
void                      wing_named_pipe_listener_accept_async   (WingNamedPipeListener  *listener,
                                                                   GCancellable           *cancellable,
                                                                   GAsyncReadyCallback     callback,
                                                                   gpointer                user_data);

WING_AVAILABLE_IN_ALL
WingNamedPipeConnection  *wing_named_pipe_listener_accept_finish  (WingNamedPipeListener  *listener,
                                                                   GAsyncResult           *result,
                                                                   GError                **error);

WING_AVAILABLE_IN_ALL
void                      wing_named_pipe_listener_set_use_iocp   (WingNamedPipeListener  *listener,
                                                                   gboolean                use_iocp);

G_END_DECLS

#endif /* WING_NAMED_PIPE_LISTENER_H */
