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

#include "wingservice.h"
#include "wingservice-private.h"
#include "wingservicemanager.h"

#include <windows.h>
#include <winuser.h>
#include <dbt.h>

#define WING_SERVICE_STARTUP 256


typedef struct _WingServicePrivate
{
  gchar *name;
  wchar_t *namew;
  gchar *description;
  wchar_t *descriptionw;
  WingServiceFlags flags;
  GThread *thread;
  gboolean from_console;
  gint register_error;
  GMutex start_mutex;
  GCond start_cond;
  GMutex control_mutex;
  GCond control_cond;
  SERVICE_STATUS status;
  SERVICE_STATUS_HANDLE status_handle;
} WingServicePrivate;

typedef struct
{
  WingService *service;
  DWORD control;
  DWORD event_type;
  LPVOID event_data;
} IdleEventData;

enum
{
  PROP_0,
  PROP_NAME,
  PROP_DESCRIPTION,
  PROP_FLAGS,
  LAST_PROP
};

/* Signals */
enum
{
  START,
  STOP,
  PAUSE,
  RESUME,
  SESSION_CHANGE,
  DEVICE_CHANGE,
  LAST_SIGNAL
};

G_DEFINE_QUARK (wing-service-error-quark, wing_service_error)
G_DEFINE_TYPE_WITH_PRIVATE (WingService, wing_service, G_TYPE_OBJECT)

static GParamSpec *props[LAST_PROP];
static guint signals[LAST_SIGNAL] = { 0 };
static gboolean install_service;
static gboolean uninstall_service;
static gchar *service_start_type;
static gboolean start_service;
static gboolean stop_service;
static gint service_stop_timeout = 5;
static gboolean exec_service_as_application;
static const GOptionEntry entries[] =
{
  { "install", '\0', 0, G_OPTION_ARG_NONE, &install_service,
    "Installs the service in the Windows service manager" },
  { "uninstall", '\0', 0, G_OPTION_ARG_NONE, &uninstall_service,
    "Uninstalls the service from the Windows service manager" },
  { "start-type", '\0', 0, G_OPTION_ARG_STRING, &service_start_type,
    "Whether to start the service automatically or on demand",
    "auto|demand|disabled" },
  { "start", '\0', 0, G_OPTION_ARG_NONE, &start_service,
    "Starts the service using the Windows service manager" },
  { "stop-timeout", '\0', 0, G_OPTION_ARG_INT, &service_stop_timeout,
    "Time in seconds to wait for the service to be stopped" },
  { "stop", '\0', 0, G_OPTION_ARG_NONE, &stop_service,
    "Stops the service using the Windows service manager" },
  { "exec", '\0', 0, G_OPTION_ARG_NONE, &exec_service_as_application,
    "Launches the service as a normal application" },
  { NULL }
};

static void
free_idle_event_data (gpointer user_data)
{
  IdleEventData *data = (IdleEventData *)user_data;

  g_free (data->event_data);
  g_slice_free (IdleEventData, data);
}

static void
wing_service_finalize (GObject *object)
{
  WingService *service = WING_SERVICE (object);
  WingServicePrivate *priv;

  priv = wing_service_get_instance_private (service);

  g_free (priv->name);
  g_free (priv->namew);
  g_free (priv->description);
  g_free (priv->descriptionw);

  if (wing_service_get_default() == service)
    wing_service_set_default (NULL);

  g_mutex_clear (&priv->start_mutex);
  g_cond_clear (&priv->start_cond);
  g_mutex_clear (&priv->control_mutex);
  g_cond_clear (&priv->control_cond);

  G_OBJECT_CLASS (wing_service_parent_class)->finalize (object);
}

static void
wing_service_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
  WingService *service = WING_SERVICE (object);
  WingServicePrivate *priv;

  priv = wing_service_get_instance_private (service);

  switch (prop_id)
    {
    case PROP_NAME:
      g_value_set_string (value, priv->name);
      break;
    case PROP_DESCRIPTION:
      g_value_set_string (value, priv->description);
      break;
    case PROP_FLAGS:
      g_value_set_int (value, priv->flags);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
wing_service_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  WingService *service = WING_SERVICE (object);
  WingServicePrivate *priv;

  priv = wing_service_get_instance_private (service);

  switch (prop_id)
    {
    case PROP_NAME:
      priv->name = g_value_dup_string (value);
      priv->namew = g_utf8_to_utf16 (priv->name, -1, NULL, NULL, NULL);
      break;
    case PROP_DESCRIPTION:
      priv->description = g_value_dup_string (value);
      priv->descriptionw = g_utf8_to_utf16 (priv->description, -1, NULL, NULL, NULL);
      break;
    case PROP_FLAGS:
      priv->flags = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static DWORD
service_flags (WingService *service)
{
  WingServicePrivate *priv;
  DWORD control = 0;

  priv = wing_service_get_instance_private (service);

  if (priv->flags & WING_SERVICE_CAN_BE_SUSPENDED)
    control |= SERVICE_ACCEPT_PAUSE_CONTINUE;

  if (priv->flags & WING_SERVICE_CAN_BE_STOPPED)
    control |= SERVICE_ACCEPT_STOP;

  if (priv->flags & WING_SERVICE_STOP_ON_SHUTDOWN)
    control |= SERVICE_ACCEPT_SHUTDOWN;

  if (priv->flags & WING_SERVICE_SESSION_CHANGE_NOTIFICATIONS)
    control |= SERVICE_ACCEPT_SESSIONCHANGE;

  return control;
}

static void
wing_service_constructed (GObject *object)
{
  WingService *service = WING_SERVICE (object);
  WingServicePrivate *priv;

  priv = wing_service_get_instance_private (service);

  if (wing_service_get_default() == NULL)
    wing_service_set_default (service);

  priv->status.dwCurrentState = SERVICE_STOPPED;

  priv->status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
  if (priv->flags & WING_SERVICE_IS_INTERACTIVE)
    priv->status.dwServiceType |= SERVICE_INTERACTIVE_PROCESS;

  priv->status.dwControlsAccepted = service_flags (service);

  G_OBJECT_CLASS (wing_service_parent_class)->constructed (object);
}

static void
wing_service_class_init (WingServiceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = wing_service_finalize;
  object_class->get_property = wing_service_get_property;
  object_class->set_property = wing_service_set_property;
  object_class->constructed = wing_service_constructed;

  props[PROP_NAME] =
    g_param_spec_string ("name",
                         "Name",
                         "Name",
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY);

  props[PROP_DESCRIPTION] =
    g_param_spec_string ("description",
                         "Description",
                         "Description",
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY);

  props[PROP_FLAGS] =
    g_param_spec_int ("flags",
                      "Flags",
                      "Flags",
                      0,
                      G_MAXINT,
                      0,
                      G_PARAM_READWRITE |
                      G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  signals[START] =
      g_signal_new ("start",
                    G_OBJECT_CLASS_TYPE (object_class),
                    G_SIGNAL_RUN_LAST,
                    G_STRUCT_OFFSET (WingServiceClass, start),
                    NULL, NULL,
                    g_cclosure_marshal_VOID__VOID,
                    G_TYPE_NONE,
                    0);

  signals[STOP] =
      g_signal_new ("stop",
                    G_OBJECT_CLASS_TYPE (object_class),
                    G_SIGNAL_RUN_LAST,
                    G_STRUCT_OFFSET (WingServiceClass, stop),
                    NULL, NULL,
                    g_cclosure_marshal_VOID__VOID,
                    G_TYPE_NONE,
                    0);

  signals[PAUSE] =
      g_signal_new ("pause",
                    G_OBJECT_CLASS_TYPE (object_class),
                    G_SIGNAL_RUN_LAST,
                    G_STRUCT_OFFSET (WingServiceClass, pause),
                    NULL, NULL,
                    g_cclosure_marshal_VOID__VOID,
                    G_TYPE_NONE,
                    0);

  signals[RESUME] =
      g_signal_new ("resume",
                    G_OBJECT_CLASS_TYPE (object_class),
                    G_SIGNAL_RUN_LAST,
                    G_STRUCT_OFFSET (WingServiceClass, resume),
                    NULL, NULL,
                    g_cclosure_marshal_VOID__VOID,
                    G_TYPE_NONE,
                    0);

  signals[SESSION_CHANGE] =
      g_signal_new ("session-change",
                    G_OBJECT_CLASS_TYPE (object_class),
                    G_SIGNAL_RUN_LAST,
                    G_STRUCT_OFFSET (WingServiceClass, session_change),
                    NULL, NULL,
                    g_cclosure_marshal_VOID__UINT_POINTER,
                    G_TYPE_NONE,
                    2,
                    G_TYPE_UINT,
                    G_TYPE_POINTER);

  signals[DEVICE_CHANGE] =
      g_signal_new ("device-change",
                    G_OBJECT_CLASS_TYPE (object_class),
                    G_SIGNAL_RUN_LAST,
                    G_STRUCT_OFFSET (WingServiceClass, device_change),
                    NULL, NULL,
                    g_cclosure_marshal_VOID__UINT_POINTER,
                    G_TYPE_NONE,
                    2,
                    G_TYPE_UINT,
                    G_TYPE_POINTER);
}

static void
wing_service_init (WingService *service)
{
  WingServicePrivate *priv;

  priv = wing_service_get_instance_private (service);

  g_mutex_init (&priv->start_mutex);
  g_cond_init (&priv->start_cond);
  g_mutex_init (&priv->control_mutex);
  g_cond_init (&priv->control_cond);
}

/**
 * wing_service_new:
 * @name: the name of the service
 * @description: the description of the service
 * @flags: the #WingServiceFlags for the service
 *
 * Creates a new #WingService.
 *
 * Returns: a new #WingService.
 */
WingService *
wing_service_new (const gchar      *name,
                  const gchar      *description,
                  WingServiceFlags  flags)
{
  return g_object_new (WING_TYPE_SERVICE,
                       "name", name,
                       "description", description,
                       "flags", flags,
                       NULL);
}

static WingService *default_service;

WingService *
wing_service_get_default (void)
{
  return default_service;
}

void
wing_service_set_default (WingService *service)
{
  default_service = service;
}

/**
 * wing_service_get_name:
 * @service: a #WingService
 *
 * Gets the name of the service.
 *
 * Returns: the name of the service.
 */
const gchar *
wing_service_get_name (WingService *service)
{
  WingServicePrivate *priv;

  g_return_val_if_fail (WING_IS_SERVICE (service), NULL);

  priv = wing_service_get_instance_private (service);

  return priv->name;
}

const wchar_t *
_wing_service_get_namew (WingService *service)
{
  WingServicePrivate *priv;

  g_return_val_if_fail(WING_IS_SERVICE (service), NULL);

  priv = wing_service_get_instance_private (service);

  return priv->namew;
}

/**
 * wing_service_get_description:
 * @service: a #WingService
 *
 * Gets the description of the service.
 *
 * Returns: the description of the service.
 */
const gchar *
wing_service_get_description (WingService *service)
{
  WingServicePrivate *priv;

  g_return_val_if_fail (WING_IS_SERVICE (service), NULL);

  priv = wing_service_get_instance_private (service);

  return priv->description;
}

const wchar_t *
_wing_service_get_descriptionw (WingService *service)
{
  WingServicePrivate *priv;

  g_return_val_if_fail(WING_IS_SERVICE (service), NULL);

  priv = wing_service_get_instance_private (service);

  return priv->descriptionw;
}

/**
 * wing_service_get_flags:
 * @service: a #WingService
 *
 * Gets the flags of the service.
 *
 * Returns: the flags of the service.
 */
WingServiceFlags
wing_service_get_flags (WingService *service)
{
  WingServicePrivate *priv;

  g_return_val_if_fail (WING_IS_SERVICE (service), WING_SERVICE_NONE);

  priv = wing_service_get_instance_private (service);

  return priv->flags;
}

static void
set_service_status (WingService *service,
                    DWORD        state)
{
  WingServicePrivate *priv;

  priv = wing_service_get_instance_private (service);

  priv->status.dwCurrentState = state;
  SetServiceStatus (priv->status_handle, &priv->status);
}

static gboolean
on_control_handler_idle (gpointer user_data)
{
  IdleEventData *data = (IdleEventData *)user_data;
  WingService *service = data->service;
  WingServicePrivate *priv;

  priv = wing_service_get_instance_private (service);

  switch (data->control)
    {
    case WING_SERVICE_STARTUP:
      g_signal_emit (G_OBJECT (service), signals[START], 0);
      g_mutex_lock (&priv->control_mutex);
      g_cond_signal (&priv->control_cond);
      g_mutex_unlock (&priv->control_mutex);
      break;
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
      g_signal_emit (G_OBJECT (service), signals[STOP], 0);
      g_mutex_lock (&priv->control_mutex);
      g_cond_signal (&priv->control_cond);
      g_mutex_unlock (&priv->control_mutex);
      break;
    case SERVICE_CONTROL_PAUSE:
      g_signal_emit (G_OBJECT (service), signals[PAUSE], 0);
      g_mutex_lock (&priv->control_mutex);
      g_cond_signal (&priv->control_cond);
      g_mutex_unlock (&priv->control_mutex);
      break;
    case SERVICE_CONTROL_CONTINUE:
      g_signal_emit (G_OBJECT (service), signals[RESUME], 0);
      g_mutex_lock (&priv->control_mutex);
      g_cond_signal (&priv->control_cond);
      g_mutex_unlock (&priv->control_mutex);
      break;
    case SERVICE_CONTROL_SESSIONCHANGE:
      g_signal_emit (G_OBJECT (service), signals[SESSION_CHANGE], 0, data->event_type, data->event_data);
      break;
    case SERVICE_CONTROL_DEVICEEVENT:
      g_signal_emit (G_OBJECT (service), signals[DEVICE_CHANGE], 0, data->event_type, data->event_data);
      break;
    }

  return G_SOURCE_REMOVE;
}

static DWORD WINAPI
control_handler (DWORD  control,
                 DWORD  event_type,
                 LPVOID event_data,
                 LPVOID context)
{
  WingService *service;
  WingServicePrivate *priv;
  IdleEventData *data;
  DWORD res = NO_ERROR;

  service = wing_service_get_default ();
  if (service == NULL)
    return res;

  priv = wing_service_get_instance_private (service);

  data = g_slice_new (IdleEventData);
  data->service = service;
  data->control = control;
  data->event_type = event_type;
  data->event_data = NULL;

  g_mutex_lock (&priv->control_mutex);

  switch (control)
    {
    case WING_SERVICE_STARTUP:
      set_service_status (service, SERVICE_START_PENDING);
      g_idle_add_full (G_PRIORITY_DEFAULT,
                       on_control_handler_idle,
                       data, free_idle_event_data);
      g_cond_wait (&priv->control_cond, &priv->control_mutex);
      set_service_status (service, SERVICE_RUNNING);
      break;
    case SERVICE_CONTROL_STOP:
      set_service_status (service, SERVICE_STOP_PENDING);
      g_idle_add_full (G_PRIORITY_DEFAULT,
                       on_control_handler_idle,
                       data, free_idle_event_data);
      g_cond_wait (&priv->control_cond, &priv->control_mutex);
      /* No need to set the status after that since the application will
       * be stopped and the SCM will already realize about it */
      break;
    case SERVICE_CONTROL_PAUSE:
      set_service_status (service, SERVICE_PAUSE_PENDING);
      g_idle_add_full (G_PRIORITY_DEFAULT,
                       on_control_handler_idle,
                       data, free_idle_event_data);
      g_cond_wait (&priv->control_cond, &priv->control_mutex);
      set_service_status (service, SERVICE_PAUSED);
      break;
    case SERVICE_CONTROL_CONTINUE:
      set_service_status (service, SERVICE_CONTINUE_PENDING);
      g_idle_add_full (G_PRIORITY_DEFAULT,
                       on_control_handler_idle,
                       data, free_idle_event_data);
      g_cond_wait (&priv->control_cond, &priv->control_mutex);
      set_service_status (service, SERVICE_RUNNING);
      break;
    case SERVICE_CONTROL_INTERROGATE:
      /* We do nothing with the data so just free it */
      g_slice_free (IdleEventData, data);
      break;
    case SERVICE_CONTROL_SHUTDOWN:
      /* do not waste time informing the SCM about the state just do it */
      g_idle_add_full (G_PRIORITY_HIGH,
                       on_control_handler_idle,
                       data, free_idle_event_data);
      g_cond_wait (&priv->control_cond, &priv->control_mutex);
      break;
    case SERVICE_CONTROL_SESSIONCHANGE:
      data->event_data = g_new (WTSSESSION_NOTIFICATION, 1);
      memcpy (data->event_data, event_data, sizeof (WTSSESSION_NOTIFICATION));
      g_idle_add_full (G_PRIORITY_DEFAULT,
                       on_control_handler_idle,
                       data, free_idle_event_data);
      break;
    case SERVICE_CONTROL_DEVICEEVENT:
      switch(event_type)
        {
        case DBT_CUSTOMEVENT:
        case DBT_DEVICEARRIVAL:
        case DBT_DEVICEQUERYREMOVE:
        case DBT_DEVICEQUERYREMOVEFAILED:
        case DBT_DEVICEREMOVECOMPLETE:
        case DBT_DEVICEREMOVEPENDING:
        case DBT_DEVICETYPESPECIFIC:
        case DBT_USERDEFINED:
          data->event_data = g_malloc (((DEV_BROADCAST_HDR *)event_data)->dbch_size);
          memcpy (data->event_data, event_data, ((DEV_BROADCAST_HDR *)event_data)->dbch_size);
          break;
        }

      g_idle_add_full (G_PRIORITY_DEFAULT,
                       on_control_handler_idle,
                       data, free_idle_event_data);
      break;
    default:
      /* XXX: do something else here? */
      g_slice_free (IdleEventData, data);
      res = ERROR_CALL_NOT_IMPLEMENTED;
    }

  g_mutex_unlock (&priv->control_mutex);

  if (priv->status.dwCurrentState != SERVICE_STOPPED)
    SetServiceStatus (priv->status_handle, &priv->status);

  return res;
}

static void WINAPI
service_main (DWORD     argc,
              wchar_t **argv)
{
  WingService *service;
  WingServicePrivate *priv;

  service = wing_service_get_default ();
  if (service == NULL)
    return;

  priv = wing_service_get_instance_private (service);

  g_cond_signal (&priv->start_cond);

  priv->status_handle = RegisterServiceCtrlHandlerExW (priv->namew, control_handler, NULL);

  control_handler (WING_SERVICE_STARTUP, 0, NULL, NULL);
}

static gpointer
service_dispatcher_run (gpointer user_data)
{
  WingService *service = WING_SERVICE (user_data);
  WingServicePrivate *priv;
  SERVICE_TABLE_ENTRYW st[2];

  priv = wing_service_get_instance_private (service);

  st[0].lpServiceName = priv->namew;
  st[0].lpServiceProc = (LPSERVICE_MAIN_FUNCTIONW)service_main;
  st[1].lpServiceName = NULL;
  st[1].lpServiceProc = NULL;

  if (!StartServiceCtrlDispatcherW (st))
    {
      priv->register_error = GetLastError ();
      g_cond_signal (&priv->start_cond);
    }

  return NULL;
}

/**
 * wing_service_register:
 * @service: a #WingService
 * @error: a #GError or %NULL
 *
 * Registers the service to be dispatched by the service manager.
 *
 * Returns: %TRUE if it was properly registered.
 */
gboolean
wing_service_register (WingService  *service,
                       GError      **error)
{
  WingServicePrivate *priv = wing_service_get_instance_private (service);
  gchar *thread_name;
  gint64 end_time;

  g_return_val_if_fail (WING_IS_SERVICE (service), FALSE);

  g_mutex_lock (&priv->start_mutex);

  /* StartServiceCtrlDispatcher is a blocking method, run
   * it from a different thread to not block the whole application */
  thread_name = g_strdup_printf ("Wing Service %s", priv->name);
  priv->thread = g_thread_new (thread_name, service_dispatcher_run, service);
  g_free (thread_name);

  /* if the service did not start in 20 seconds is because something
   * bad happened, abort the execution in that case */
  end_time = g_get_monotonic_time () + 20 * G_TIME_SPAN_SECOND;
  if (!g_cond_wait_until (&priv->start_cond, &priv->start_mutex, end_time))
    {
      g_mutex_unlock (&priv->start_mutex);
      g_set_error_literal (error,
                           WING_SERVICE_ERROR,
                           WING_SERVICE_ERROR_GENERIC,
                           "Time out registering the service");
      return FALSE;
    }

  g_mutex_unlock (&priv->start_mutex);

  switch (priv->register_error)
    {
    case 0:
      /* No error, just return */
      return TRUE;
    case ERROR_FAILED_SERVICE_CONTROLLER_CONNECT:
      g_set_error (error,
                   WING_SERVICE_ERROR,
                   WING_SERVICE_ERROR_FROM_CONSOLE,
                   "Cannot register the service when launching from a console");
      break;
    default:
      {
        gchar *err_msg;

        err_msg = g_win32_error_message (priv->register_error);
        g_set_error (error,
                     WING_SERVICE_ERROR,
                     WING_SERVICE_ERROR_GENERIC,
                     "%s", err_msg);
        g_free (err_msg);
      }
    }

  return FALSE;
}

/**
 * wing_service_notify_stopped:
 * @service: a #WingService
 *
 * Called when the service is exiting. This is required
 * to let the service manager know that the service is stopped.
 */
void
wing_service_notify_stopped (WingService *service)
{
  WingServicePrivate *priv;

  g_return_if_fail (WING_IS_SERVICE (service));

  set_service_status (service, SERVICE_STOPPED);

  priv = wing_service_get_instance_private (service);

  if (priv->thread != NULL)
    {
      g_thread_join (priv->thread);
      priv->thread = NULL;
    }
}

static gint
on_handle_local_options (GApplication *application,
                         GVariantDict *options,
                         WingService  *service)
{
  WingServicePrivate *priv;
  WingServiceManager *manager;
  WingServiceManagerStartType start_type = WING_SERVICE_MANAGER_START_AUTO;
  guint stop_timeout = 5;
  gint ret = -1;

  manager = wing_service_manager_new ();

  priv = wing_service_get_instance_private (service);

  if (g_strcmp0 (service_start_type, "demand") == 0)
    start_type = WING_SERVICE_MANAGER_START_DEMAND;
  else if (g_strcmp0 (service_start_type, "disabled") == 0)
    start_type = WING_SERVICE_MANAGER_START_DISABLED;

  if (service_stop_timeout >= 0)
    stop_timeout = service_stop_timeout;

  if (install_service)
    ret = wing_service_manager_install_service (manager, service, start_type, NULL) ? 0 : 1;
  else if (uninstall_service)
    ret = wing_service_manager_uninstall_service (manager, service, NULL) ? 0 : 1;
  else if (stop_service)
    ret =  wing_service_manager_stop_service (manager, service, stop_timeout, NULL) ? 0 : 1;
  else if (exec_service_as_application)
    /* do nothing so the application continues to run */
    ret = -1;
  else if (start_service || priv->from_console)
    ret = wing_service_manager_start_service (manager, service, 0, NULL, NULL) ? 0 : 1;

  g_clear_pointer (&service_start_type, g_free);
  g_object_unref (manager);

  if (ret == -1)
    g_application_hold (application);

  return ret;
}

static void
on_service_stopped (WingService  *service,
                    GApplication *application)
{
  g_application_release (application);
  g_application_quit (application);
}

int
wing_service_run_application (WingService   *service,
                              GApplication  *application,
                              int            argc,
                              char         **argv)
{
  WingServicePrivate *priv;
  GOptionGroup *option_group;
  int status;
  GError *error = NULL;

  g_return_val_if_fail (WING_IS_SERVICE (service), 1);
  g_return_val_if_fail (G_IS_APPLICATION (application), 1);

  priv = wing_service_get_instance_private (service);

  /* service options */
  option_group = g_option_group_new ("wing",
                                     "Windows Service Options",
                                     "Show the Windows Service Options",
                                     NULL, NULL);
  g_option_group_add_entries (option_group, entries);
  g_application_add_option_group (application, option_group);

  g_signal_connect (application,
                    "handle-local-options",
                    G_CALLBACK (on_handle_local_options),
                    service);

  if (!wing_service_register (service, &error))
    {
      if (g_error_matches (error, WING_SERVICE_ERROR, WING_SERVICE_ERROR_FROM_CONSOLE))
        priv->from_console = TRUE;
      else
        {
          g_warning ("Could not register the service: %s",
                     error->message);
          g_error_free (error);
          return 1;
        }
    }

  g_signal_connect (service,
                    "stop",
                    G_CALLBACK (on_service_stopped),
                    application);

  status = g_application_run (application, argc, argv);
  wing_service_notify_stopped (service);

  return status;
}

gpointer
wing_service_register_device_notification (WingService  *service,
                                           gpointer      filter,
                                           gboolean      notify_all_interface_classes,
                                           GError      **error)
{
  WingServicePrivate *priv;
  HDEVNOTIFY handle;

  g_return_val_if_fail (WING_IS_SERVICE (service), NULL);

  priv = wing_service_get_instance_private (service);

  handle = RegisterDeviceNotification (priv->status_handle,
                                       filter,
                                       DEVICE_NOTIFY_SERVICE_HANDLE | (notify_all_interface_classes ? DEVICE_NOTIFY_ALL_INTERFACE_CLASSES : 0));
  if (handle == NULL)
    {
      gchar *err_msg;

      err_msg = g_win32_error_message (GetLastError());
      g_set_error (error,
                   WING_SERVICE_ERROR,
                   WING_SERVICE_ERROR_GENERIC,
                   "%s", err_msg);
      g_free (err_msg);
    }

  return handle;
}

gboolean
wing_service_unregister_device_notification (WingService  *service,
                                             gpointer      handle,
                                             GError      **error)
{
  gboolean result;

  g_return_val_if_fail (WING_IS_SERVICE (service), FALSE);

  result = UnregisterDeviceNotification ((HDEVNOTIFY)handle);
  if (!result)
    {
      gchar *err_msg;

      err_msg = g_win32_error_message (GetLastError());
      g_set_error (error,
                   WING_SERVICE_ERROR,
                   WING_SERVICE_ERROR_GENERIC,
                   "%s", err_msg);
      g_free (err_msg);
    }

  return result;
}
