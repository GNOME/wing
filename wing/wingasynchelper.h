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

#ifndef __WING_ASYNC_HELPER_H__
#define __WING_ASYNC_HELPER_H__

#include <gio/gio.h>

#include <windows.h>

G_BEGIN_DECLS

typedef gboolean (* WingHandleSourceFunc) (HANDLE   handle,
                                           gpointer user_data);

GSource *_wing_handle_create_source (HANDLE        handle,
                                     GCancellable *cancellable);

G_END_DECLS

#endif /* __WING_ASYNC_HELPER_H__ */
