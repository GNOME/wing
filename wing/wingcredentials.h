/*
 * Copyright © 2017 NICE s.r.l.
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
 * Authors: Silvio Lazzeretti <silviola@amazon.com>
 */

#ifndef WING_CREDENTIALS_H
#define WING_CREDENTIALS_H

#include <gio/gio.h>
#include <wing/wingversionmacros.h>

G_BEGIN_DECLS

#define WING_TYPE_CREDENTIALS         (wing_credentials_get_type ())

WING_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (WingCredentials, wing_credentials, WING, CREDENTIALS, GObject)

WING_AVAILABLE_IN_ALL
WingCredentials *wing_credentials_new                (gulong              pid,
                                                      const gchar        *sid);

WING_AVAILABLE_IN_ALL
gchar           *wing_credentials_to_string          (WingCredentials    *credentials);

WING_AVAILABLE_IN_ALL
gboolean         wing_credentials_is_same_user       (WingCredentials    *credentials,
                                                      WingCredentials    *other_credentials);

WING_AVAILABLE_IN_ALL
gulong           wing_credentials_get_pid            (WingCredentials    *credentials);

WING_AVAILABLE_IN_ALL
const gchar     *wing_credentials_get_sid            (WingCredentials    *credentials);

G_END_DECLS

#endif /* WING_CREDENTIALS_H */
