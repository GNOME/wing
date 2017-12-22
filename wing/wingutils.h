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

#ifndef WING_UTILS_H
#define WING_UTILS_H

#include <glib.h>
#include <wing/wingversionmacros.h>

G_BEGIN_DECLS

WING_AVAILABLE_IN_ALL
gboolean     wing_is_wow_64            (void);

WING_AVAILABLE_IN_ALL
gboolean     wing_is_os_64bit          (void);

WING_AVAILABLE_IN_ALL
gboolean     wing_get_version_number   (gint *major,
                                        gint *minor);

WING_AVAILABLE_IN_ALL
gboolean     wing_get_process_memory   (gsize *total_virtual_memory,
                                        gsize *total_physical_memory);

WING_AVAILABLE_IN_ALL
gboolean     wing_get_process_times    (gint64 *current_user_time,
                                        gint64 *current_system_time);

WING_AVAILABLE_IN_ALL
guint        wing_get_n_processors     (void);

G_END_DECLS

#endif /* WING_UTILS_H */
