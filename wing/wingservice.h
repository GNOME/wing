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
#define WING_SERVICE(o)              (G_TYPE_CHECK_INSTANCE_CAST ((o), WING_TYPE_SERVICE, WingService))
#define WING_SERVICE_CLASS(k)        (G_TYPE_CHECK_CLASS_CAST ((k), WING_TYPE_SERVICE, WingServiceClass))
#define WING_IS_SERVICE(o)           (G_TYPE_CHECK_INSTANCE_TYPE ((o), WING_TYPE_SERVICE))
#define WING_IS_SERVICE_CLASS(k)     (G_TYPE_CHECK_CLASS_TYPE ((k),  WING_TYPE_SERVICE))
#define WING_SERVICE_GET_CLASS(o)    (G_TYPE_INSTANCE_GET_CLASS ((o), WING_TYPE_SERVICE, WingServiceClass))

typedef struct _WingService                       WingService;
typedef struct _WingServiceClass                  WingServiceClass;

struct _WingService
{
  /*< private >*/
  GObject parent_instance;
};

struct _WingServiceClass
{
  GObjectClass parent_class;

  void (*start)   (WingService *service);
  void (*stop)    (WingService *service);
  void (*pause)   (WingService *service);
  void (*resume)  (WingService *service);

  gpointer reserved[12];
};

/**
 * WingServiceFlags:
 * @WING_SERVICE_NONE: no flags.
 * @WING_SERVICE_CAN_BE_SUSPENDED: whether the service can be suspended.
 * @WING_SERVICE_CAN_BE_STOPPED: whether the service can be stopped.
 * @WING_SERVICE_STOP_ON_SHUTDOWN: whether to stop the service on shutdown.
 * @WING_SERVICE_IS_INTERACTIVE: whether service can interact with the desktop.
 *
 * The flags for the service.
 */
typedef enum
{
  WING_SERVICE_NONE             = 0,
  WING_SERVICE_CAN_BE_SUSPENDED = 1 << 0,
  WING_SERVICE_CAN_BE_STOPPED   = 1 << 1,
  WING_SERVICE_STOP_ON_SHUTDOWN = 1 << 2,
  WING_SERVICE_IS_INTERACTIVE   = 1 << 3
} WingServiceFlags;

WING_AVAILABLE_IN_ALL
GType                  wing_service_get_type        (void) G_GNUC_CONST;

WING_AVAILABLE_IN_ALL
WingService           *wing_service_new             (const gchar      *name,
                                                     const gchar      *description,
                                                     WingServiceFlags  flags,
                                                     GApplication     *application);

WING_AVAILABLE_IN_ALL
WingService           *wing_service_get_default     (void);

WING_AVAILABLE_IN_ALL
void                   wing_service_set_default     (WingService *service);

WING_AVAILABLE_IN_ALL
const gchar           *wing_service_get_name        (WingService *service);

WING_AVAILABLE_IN_ALL
const gchar           *wing_service_get_description (WingService *service);

WING_AVAILABLE_IN_ALL
WingServiceFlags       wing_service_get_flags       (WingService *service);

WING_AVAILABLE_IN_ALL
int                    wing_service_run             (WingService  *service,
                                                     int           argc,
                                                     char        **argv);

G_END_DECLS

#endif /* WING_SERVICE_H */
