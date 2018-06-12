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

#ifndef WING_SERVICE_H
#define WING_SERVICE_H

#include <gio/gio.h>
#include <wing/wingversionmacros.h>

G_BEGIN_DECLS

#define WING_TYPE_SERVICE            (wing_service_get_type ())

WING_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (WingService, wing_service, WING, SERVICE, GObject)

struct _WingServiceClass
{
  GObjectClass parent_class;

  void (*start)           (WingService *service);
  void (*stop)            (WingService *service);
  void (*pause)           (WingService *service);
  void (*resume)          (WingService *service);
  void (*session_change)  (WingService *service);
  void (*device_change)   (WingService *service);

  gpointer reserved[10];
};

/**
 * WingServiceFlags:
 * @WING_SERVICE_NONE: no flags.
 * @WING_SERVICE_CAN_BE_SUSPENDED: whether the service can be suspended.
 * @WING_SERVICE_CAN_BE_STOPPED: whether the service can be stopped.
 * @WING_SERVICE_STOP_ON_SHUTDOWN: whether to stop the service on shutdown.
 * @WING_SERVICE_IS_INTERACTIVE: whether service can interact with the desktop.
 * @WING_SERVICE_SESSIONCHANGE: whether service can receive the SESSION_CHANGE events.
 *
 * The flags for the service.
 */
typedef enum
{
  WING_SERVICE_NONE                         = 0,
  WING_SERVICE_CAN_BE_SUSPENDED             = 1 << 0,
  WING_SERVICE_CAN_BE_STOPPED               = 1 << 1,
  WING_SERVICE_STOP_ON_SHUTDOWN             = 1 << 2,
  WING_SERVICE_IS_INTERACTIVE               = 1 << 3,
  WING_SERVICE_SESSION_CHANGE_NOTIFICATIONS = 1 << 4
} WingServiceFlags;

/**
 * WingServiceErrorEnum:
 * @WING_SERVICE_ERROR_GENERIC_ERROR: a generic error
 * @WING_SERVICE_ERROR_FROM_CONSOLE: emitted if trying to register the service from a console
 * @WING_SERVICE_ERROR_SERVICE_STOP_TIMEOUT: emitted if the stop of the service took more than 2 seconds
 *
 * The errors emitted by the service.
 */
typedef enum
{
  WING_SERVICE_ERROR_GENERIC,
  WING_SERVICE_ERROR_FROM_CONSOLE,
  WING_SERVICE_ERROR_SERVICE_STOP_TIMEOUT
} WingServiceErrorEnum;

#define WING_SERVICE_ERROR (wing_service_error_quark())
WING_AVAILABLE_IN_ALL
GQuark                 wing_service_error_quark                     (void);

WING_AVAILABLE_IN_ALL
WingService           *wing_service_new                             (const gchar      *name,
                                                                     const gchar      *description,
                                                                     WingServiceFlags  flags);

WING_AVAILABLE_IN_ALL
WingService           *wing_service_get_default                     (void);

WING_AVAILABLE_IN_ALL
void                   wing_service_set_default                     (WingService      *service);

WING_AVAILABLE_IN_ALL
const gchar           *wing_service_get_name                        (WingService      *service);

WING_AVAILABLE_IN_ALL
const gchar           *wing_service_get_description                 (WingService      *service);

WING_AVAILABLE_IN_ALL
WingServiceFlags       wing_service_get_flags                       (WingService      *service);

WING_AVAILABLE_IN_ALL
int                    wing_service_register                        (WingService      *service,
                                                                     GError          **error);

WING_AVAILABLE_IN_ALL
void                   wing_service_notify_stopped                  (WingService      *service);

WING_AVAILABLE_IN_ALL
int                    wing_service_run_application                 (WingService      *service,
                                                                     GApplication     *application,
                                                                     int               argc,
                                                                     char            **argv);

WING_AVAILABLE_IN_ALL
gpointer               wing_service_register_device_notification    (WingService      *service,
                                                                     gpointer          filter,
                                                                     gboolean          notify_all_interface_classes,
                                                                     GError          **error);

WING_AVAILABLE_IN_ALL
gboolean               wing_service_unregister_device_notification  (WingService      *service,
                                                                     gpointer          handle,
                                                                     GError          **error);

G_END_DECLS

#endif /* WING_SERVICE_H */
