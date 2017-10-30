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

#ifndef WING_SERVICE_MANAGER_H
#define WING_SERVICE_MANAGER_H

#include <glib-object.h>
#include <wing/wing.h>

G_BEGIN_DECLS

#define WING_TYPE_SERVICE_MANAGER            (wing_service_manager_get_type ())
#define WING_SERVICE_MANAGER(o)              (G_TYPE_CHECK_INSTANCE_CAST ((o), WING_TYPE_SERVICE_MANAGER, WingServiceManager))
#define WING_IS_SERVICE_MANAGER(o)           (G_TYPE_CHECK_INSTANCE_TYPE ((o), WING_TYPE_SERVICE_MANAGER))

typedef struct _WingServiceManager          WingServiceManager;

typedef enum
{
  WING_SERVICE_MANAGER_START_AUTO,
  WING_SERVICE_MANAGER_START_DEMAND,
  WING_SERVICE_MANAGER_START_DISABLED
} WingServiceManagerStartType;

WING_AVAILABLE_IN_ALL
GType                   wing_service_manager_get_type                 (void) G_GNUC_CONST;

WING_AVAILABLE_IN_ALL
WingServiceManager     *wing_service_manager_new                      (void);

WING_AVAILABLE_IN_ALL
gboolean                wing_service_manager_install_service          (WingServiceManager           *manager,
                                                                       WingService                  *service,
                                                                       WingServiceManagerStartType   start_type,
                                                                       GError                      **error);

WING_AVAILABLE_IN_ALL
gboolean                wing_service_manager_uninstall_service        (WingServiceManager           *manager,
                                                                       WingService                  *service,
                                                                       GError                      **error);

WING_AVAILABLE_IN_ALL
gboolean                wing_service_manager_get_service_installed    (WingServiceManager           *manager,
                                                                       WingService                  *service,
                                                                       GError                      **error);

WING_AVAILABLE_IN_ALL
gboolean                wing_service_manager_get_service_running      (WingServiceManager           *manager,
                                                                       WingService                  *service,
                                                                       GError                      **error);

WING_AVAILABLE_IN_ALL
gboolean                wing_service_manager_start_service            (WingServiceManager           *manager,
                                                                       WingService                  *service,
                                                                       int                           argc,
                                                                       char                        **argv,
                                                                       GError                      **error);

WING_AVAILABLE_IN_ALL
gboolean                wing_service_manager_stop_service             (WingServiceManager           *manager,
                                                                       WingService                  *service,
                                                                       guint                         timeout_in_sec,
                                                                       GError                      **error);

G_END_DECLS

#endif /* WING_SERVICE_MANAGER_H */
