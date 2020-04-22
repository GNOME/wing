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
 * Authors: Ignacio Casal Quinteiro <ignacio.casal@nice-software.com>
 */

#ifndef WING_EVENT_WINDOW_H
#define WING_EVENT_WINDOW_H

#include <glib-object.h>
#include <wing/wingversionmacros.h>

#include <windows.h>

G_BEGIN_DECLS

#define WING_TYPE_EVENT_WINDOW (wing_event_window_get_type ())

WING_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (WingEventWindow, wing_event_window, WING, EVENT_WINDOW, GObject)

typedef gboolean (* WingEventCallback)    (WingEventWindow *window,
                                           WPARAM           wparam,
                                           LPARAM           lparam,
                                           gpointer         user_data);

WING_AVAILABLE_IN_ALL
WingEventWindow         *wing_event_window_new             (const gchar       *name,
                                                            gboolean           track_clipboard,
                                                            GError           **error);

WING_AVAILABLE_IN_ALL
HWND                     wing_event_window_get_hwnd        (WingEventWindow   *window);

WING_AVAILABLE_IN_ALL
void                     wing_event_window_connect         (WingEventWindow   *window,
                                                            guint              message,
                                                            WingEventCallback  callback,
                                                            gpointer           user_data);

G_END_DECLS

#endif /* WING_EVENT_WINDOW_H */

/* ex:set ts=4 et: */
