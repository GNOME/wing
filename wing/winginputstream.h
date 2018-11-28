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

#ifndef WING_INPUT_STREAM_H
#define WING_INPUT_STREAM_H

#include <gio/gio.h>
#include <wing/wingversionmacros.h>

G_BEGIN_DECLS

#define WING_TYPE_INPUT_STREAM (wing_input_stream_get_type ())

/**
 * WingInputStream:
 *
 * Implements #GInputStream for reading from selectable Windows file handles
 **/
WING_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (WingInputStream, wing_input_stream, WING, INPUT_STREAM, GInputStream)

struct _WingInputStreamClass
{
  GInputStreamClass parent_class;

  /*< private >*/
  gpointer padding[10];
};

WING_AVAILABLE_IN_ALL
GInputStream * wing_input_stream_new              (void              *handle,
                                                   gboolean           close_handle);
WING_AVAILABLE_IN_ALL
void           wing_input_stream_set_close_handle (WingInputStream *stream,
                                                   gboolean           close_handle);
WING_AVAILABLE_IN_ALL
gboolean       wing_input_stream_get_close_handle (WingInputStream *stream);

WING_AVAILABLE_IN_ALL
void          *wing_input_stream_get_handle       (WingInputStream *stream);

G_END_DECLS

#endif /* __WING_INPUT_STREAM_H__ */
