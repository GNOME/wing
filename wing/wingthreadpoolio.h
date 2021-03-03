/*
 * Copyright © 2021 NICE s.r.l.
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
 * Authors: Silvio Lazzeretti <silviola@amazon.com>
 */

#ifndef WING_THREAD_POOL_IO_H
#define WING_THREAD_POOL_IO_H

#include <gio/gio.h>
#include <wing/wingversionmacros.h>

G_BEGIN_DECLS

#define WING_TYPE_THREAD_POOL_IO                         (wing_thread_pool_io_get_type ())
#define WING_THREAD_POOL_IO(obj)                         ((WingThreadPoolIo *)obj)

typedef struct _WingThreadPoolIo WingThreadPoolIo;

WING_AVAILABLE_IN_ALL
GType                         wing_thread_pool_io_get_type                       (void) G_GNUC_CONST;

WING_AVAILABLE_IN_ALL
WingThreadPoolIo             *wing_thread_pool_io_new                            (void                     *handle);

WING_AVAILABLE_IN_ALL
WingThreadPoolIo             *wing_thread_pool_io_ref                            (WingThreadPoolIo         *self);

WING_AVAILABLE_IN_ALL
void                          wing_thread_pool_io_unref                          (WingThreadPoolIo         *self);

WING_AVAILABLE_IN_ALL
void                          wing_thread_pool_io_start                          (WingThreadPoolIo         *self);

WING_AVAILABLE_IN_ALL
void                          wing_thread_pool_io_cancel                         (WingThreadPoolIo         *self);

WING_AVAILABLE_IN_ALL
void                         *wing_thread_pool_get_handle                        (WingThreadPoolIo         *self);

WING_AVAILABLE_IN_ALL
gboolean                      wing_thread_pool_io_close_handle                   (WingThreadPoolIo         *self,
                                                                                  GError                  **error);

G_END_DECLS

#endif /* WING_THREAD_POOL_IO_H */
