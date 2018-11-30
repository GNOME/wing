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

#ifndef WING_NAMED_PIPE_CLIENT_H
#define WING_NAMED_PIPE_CLIENT_H

#include <gio/gio.h>
#include <wing/wingnamedpipeconnection.h>
#include <wing/wingversionmacros.h>

G_BEGIN_DECLS

#define WING_TYPE_NAMED_PIPE_CLIENT            (wing_named_pipe_client_get_type ())
#define WING_NAMED_PIPE_CLIENT(o)              (G_TYPE_CHECK_INSTANCE_CAST ((o), WING_TYPE_NAMED_PIPE_CLIENT, WingNamedPipeClient))
#define WING_NAMED_PIPE_CLIENT_CLASS(k)        (G_TYPE_CHECK_CLASS_CAST ((k), WING_TYPE_NAMED_PIPE_CLIENT, WingNamedPipeClientClass))
#define WING_IS_NAMED_PIPE_CLIENT(o)           (G_TYPE_CHECK_INSTANCE_TYPE ((o), WING_TYPE_NAMED_PIPE_CLIENT))
#define WING_IS_NAMED_PIPE_CLIENT_CLASS(k)     (G_TYPE_CHECK_CLASS_TYPE ((k),  WING_TYPE_NAMED_PIPE_CLIENT))
#define WING_NAMED_PIPE_CLIENT_GET_CLASS(o)    (G_TYPE_INSTANCE_GET_CLASS ((o), WING_TYPE_NAMED_PIPE_CLIENT, WingNamedPipeClientClass))

typedef struct _WingNamedPipeClient                       WingNamedPipeClient;
typedef struct _WingNamedPipeClientClass                  WingNamedPipeClientClass;

struct _WingNamedPipeClient
{
  /*< private >*/
  GObject parent_instance;
};

struct _WingNamedPipeClientClass
{
  GObjectClass parent_class;

  /*< private >*/
  gpointer padding[10];
};

typedef enum
{
  WING_NAMED_PIPE_CLIENT_GENERIC_READ  = (1 << 0),
  WING_NAMED_PIPE_CLIENT_GENERIC_WRITE = (1 << 1)
} WingNamedPipeClientFlags;

WING_AVAILABLE_IN_ALL
GType                     wing_named_pipe_client_get_type         (void) G_GNUC_CONST;

WING_AVAILABLE_IN_ALL
WingNamedPipeClient      *wing_named_pipe_client_new              (void);

WING_AVAILABLE_IN_ALL
WingNamedPipeConnection  *wing_named_pipe_client_connect          (WingNamedPipeClient      *client,
                                                                   const gchar              *pipe_name,
                                                                   WingNamedPipeClientFlags  flags,
                                                                   GCancellable             *cancellable,
                                                                   GError                   **error);

WING_AVAILABLE_IN_ALL
void                      wing_named_pipe_client_connect_async    (WingNamedPipeClient      *client,
                                                                   const gchar              *pipe_name,
                                                                   WingNamedPipeClientFlags  flags,
                                                                   GCancellable             *cancellable,
                                                                   GAsyncReadyCallback       callback,
                                                                   gpointer                  user_data);

WING_AVAILABLE_IN_ALL
WingNamedPipeConnection  *wing_named_pipe_client_connect_finish   (WingNamedPipeClient      *client,
                                                                   GAsyncResult             *result,
                                                                   GError                  **error);

WING_AVAILABLE_IN_ALL
void                      wing_named_pipe_client_set_use_iocp     (WingNamedPipeClient      *client,
                                                                   gboolean                  use_iocp);

G_END_DECLS

#endif /* WING_NAMED_PIPE_CLIENT_H */
