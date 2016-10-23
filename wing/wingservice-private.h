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

#ifndef WING_SERVICE_PRIVATE_H
#define WING_SERVICE_PRIVATE_H

G_BEGIN_DECLS

const wchar_t  *_wing_service_get_namew         (WingService *service);

const wchar_t  *_wing_service_get_descriptionw  (WingService *service);

G_END_DECLS

#endif /* WING_SERVICE_PRIVATE_H */
