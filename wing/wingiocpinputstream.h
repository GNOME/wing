/*
 * Copyright (C) 2006-2010 Red Hat, Inc.
 * Copyright (C) 2018 NICE s.r.l.
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
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Alexander Larsson <alexl@redhat.com>
 * Author: Tor Lillqvist <tml@iki.fi>
 * Author: Ignacio Casal Quinteiro <qignacio@amazon.com>
 * Author: Silvio Lazzeretti <silviola@amazon.com>
 */

#ifndef WING_IOCP_INPUT_STREAM_H
#define WING_IOCP_INPUT_STREAM_H

#include <gio/gio.h>
#include <wing/wingversionmacros.h>
#include <wing/wingthreadpoolio.h>

G_BEGIN_DECLS

#define WING_TYPE_IOCP_INPUT_STREAM (wing_iocp_input_stream_get_type ())

/**
 * WingIocpInputStream:
 *
 * Implements #GInputStream for reading from selectable Windows file handles
 **/
WING_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (WingIocpInputStream, wing_iocp_input_stream, WING, IOCP_INPUT_STREAM, GInputStream)

struct _WingIocpInputStreamClass
{
  GInputStreamClass parent_class;

  /*< private >*/
  gpointer padding[10];
};

WING_AVAILABLE_IN_ALL
GType          wing_iocp_input_stream_get_type         (void) G_GNUC_CONST;

WING_AVAILABLE_IN_ALL
GInputStream * wing_iocp_input_stream_new              (gboolean             close_handle,
                                                        WingThreadPoolIo    *thread_pool_io);
WING_AVAILABLE_IN_ALL
void           wing_iocp_input_stream_set_close_handle (WingIocpInputStream *stream,
                                                        gboolean             close_handle);
WING_AVAILABLE_IN_ALL
gboolean       wing_iocp_input_stream_get_close_handle (WingIocpInputStream *stream);

WING_AVAILABLE_IN_ALL
void          *wing_iocp_input_stream_get_handle       (WingIocpInputStream *stream);

G_END_DECLS

#endif /* __WING_IOCP_INPUT_STREAM_H__ */
