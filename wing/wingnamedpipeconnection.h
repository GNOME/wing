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

#ifndef WING_NAMED_PIPE_CONNECTION_H
#define WING_NAMED_PIPE_CONNECTION_H

#include <gio/gio.h>
#include <wing/wingversionmacros.h>
#include <wing/wingcredentials.h>

G_BEGIN_DECLS

#define WING_TYPE_NAMED_PIPE_CONNECTION                  (wing_named_pipe_connection_get_type ())
#define WING_NAMED_PIPE_CONNECTION(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), WING_TYPE_NAMED_PIPE_CONNECTION, WingNamedPipeConnection))
#define WING_IS_NAMED_PIPE_CONNECTION(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), WING_TYPE_NAMED_PIPE_CONNECTION))

typedef struct _WingNamedPipeConnection WingNamedPipeConnection;

WING_AVAILABLE_IN_ALL
GType                         wing_named_pipe_connection_get_type                (void) G_GNUC_CONST;

WING_AVAILABLE_IN_ALL
const gchar                  *wing_named_pipe_connection_get_pipe_name           (WingNamedPipeConnection  *connection);

WING_AVAILABLE_IN_ALL
WingCredentials              *wing_named_pipe_connection_get_credentials         (WingNamedPipeConnection  *connection,
                                                                                  GError                  **error);
G_END_DECLS

#endif /* WING_NAMED_PIPE_CONNECTION_H */
