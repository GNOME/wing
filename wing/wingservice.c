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

#include <Windows.h>

#define DCV_SERVICE_STARTUP 256


typedef struct _WingServicePrivate
{
  gchar *name;
  wchar_t *namew;
  WingServiceFlags flags;
  GApplication *application;
  GThread *thread;
  gboolean from_console;
  GMutex start_mutex;
  GCond start_cond;
  GMutex control_mutex;
  GCond control_cond;
  SERVICE_STATUS status;
  SERVICE_STATUS_HANDLE status_handle;
  gboolean notify_service_stop;
} WingServicePrivate;

typedef struct
{
  WingService *service;
  DWORD control;
} IdleEventData;

enum
{
  PROP_0,
  PROP_NAME,
  PROP_FLAGS,
  PROP_APPLICATION,
  LAST_PROP
};

/* Signals */
enum
{
  START,
  STOP,
  PAUSE,
  RESUME,
  LAST_SIGNAL
};

G_DEFINE_TYPE_WITH_PRIVATE (WingService, wing_service, G_TYPE_OBJECT)

static GParamSpec *props[LAST_PROP];
static guint signals[LAST_SIGNAL] = { 0 };

static gboolean install_service;
static gboolean uninstall_service;
static gboolean start_service;
static gboolean stop_service;
static gboolean exec_service_as_application;
static const GOptionEntry entries[] =
{
  { "install", 'i', 0, G_OPTION_ARG_NONE, &install_service,
    "Installs the service in the Windows service manager" },
  { "uninstall", 'u', 0, G_OPTION_ARG_NONE, &uninstall_service,
    "Uninstalls the service from the Windows service manager" },
  { "start", '\0', 0, G_OPTION_ARG_NONE, &start_service,
    "Starts the service using the Windows service manager" },
  { "stop", '\0', 0, G_OPTION_ARG_NONE, &stop_service,
    "Stops the service using the Windows service manager" },
  { "exec", 'e', 0, G_OPTION_ARG_NONE, &exec_service_as_application,
    "Launches the service as a normal application" },
  { NULL }
};

static void
free_idle_event_data (gpointer user_data)
{
  IdleEventData *data = (IdleEventData *)user_data;

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

  if (wing_service_get_default() == service)
    wing_service_set_default(NULL);

  g_mutex_clear (&priv->start_mutex);
  g_cond_clear (&priv->start_cond);
  g_mutex_clear (&priv->control_mutex);
  g_cond_clear (&priv->control_cond);

  G_OBJECT_CLASS (wing_service_parent_class)->finalize (object);
}

static void
wing_service_dispose (GObject *object)
{
  WingService *service = WING_SERVICE (object);
  WingServicePrivate *priv;

  priv = wing_service_get_instance_private (service);

  g_clear_object (&priv->application);

  G_OBJECT_CLASS (wing_service_parent_class)->dispose (object);
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
    case PROP_FLAGS:
      g_value_set_int (value, priv->flags);
      break;
    case PROP_APPLICATION:
      g_value_set_object (value, priv->application);
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
    case PROP_FLAGS:
      priv->flags = g_value_get_int (value);
      break;
    case PROP_APPLICATION:
      priv->application = g_value_dup_object (value);
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

  return control;
}

static gint
on_handle_local_options (GApplication *application,
                         GVariantDict *options,
                         WingService  *service)
{
  WingServicePrivate *priv;
  WingServiceManager *manager;
  gint ret = -1;

  manager = wing_service_manager_new ();

  priv = wing_service_get_instance_private (service);

  if (install_service)
    ret = wing_service_manager_install_service (manager, service, NULL) ? 0 : 1;
  else if (uninstall_service)
    ret = wing_service_manager_uninstall_service (manager, service, NULL) ? 0 : 1;
  else if (stop_service)
    ret =  wing_service_manager_stop_service (manager, service, NULL) ? 0 : 1;
  else if (exec_service_as_application)
    /* do nothing so the application continues to run */
    ret = -1;
  else if (start_service || priv->from_console)
    ret = wing_service_manager_start_service (manager, service, 0, NULL, NULL) ? 0 : 1;

  g_object_unref (manager);

  if (ret == -1)
    {
      priv->notify_service_stop = TRUE;
      g_application_hold (priv->application);
    }

  return ret;
}

static void
wing_service_constructed (GObject *object)
{
  WingService *service = WING_SERVICE (object);
  WingServicePrivate *priv;
  GOptionGroup *option_group;

  priv = wing_service_get_instance_private (service);

  if (wing_service_get_default() == NULL)
    wing_service_set_default (service);

  priv->status.dwCurrentState = SERVICE_STOPPED;
  priv->status.dwServiceType = SERVICE_WIN32_OWN_PROCESS | SERVICE_INTERACTIVE_PROCESS;
  priv->status.dwControlsAccepted = service_flags (service);

  /* service options */
  option_group = g_option_group_new ("dcvservice",
                                     "Windows Service Options",
                                     "Show the Windows Service Options",
                                     NULL, NULL);
  g_option_group_add_entries (option_group, entries);
  g_application_add_option_group (priv->application, option_group);

  /* HACK to avoid reaching the default handler. It seems that the default
   * handler return value will always gain over our one on the signal connection.
   * Since we want to give priority to our one we set the default one to NULL.
   * This is though a bug in GApplication and we should remove this once it is fixed.
   * See https://bugzilla.gnome.org/show_bug.cgi?id=750796
   */
  G_APPLICATION_GET_CLASS (priv->application)->handle_local_options = NULL;
  g_signal_connect (priv->application,
                    "handle-local-options",
                    G_CALLBACK (on_handle_local_options),
                    service);

  G_OBJECT_CLASS (wing_service_parent_class)->constructed (object);
}

static void
wing_service_class_init (WingServiceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = wing_service_finalize;
  object_class->dispose = wing_service_dispose;
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

  props[PROP_FLAGS] =
    g_param_spec_int ("flags",
                      "Flags",
                      "Flags",
                      0,
                      G_MAXINT,
                      0,
                      G_PARAM_READWRITE |
                      G_PARAM_CONSTRUCT_ONLY);

  props[PROP_APPLICATION] =
    g_param_spec_object ("application",
                         "Application",
                         "Application",
                         G_TYPE_APPLICATION,
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

WingService *
wing_service_new (const gchar      *name,
                  WingServiceFlags  flags,
                  GApplication     *application)
{
  return g_object_new (WING_TYPE_SERVICE,
                       "name", name,
                       "flags", flags,
                       "application", application,
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
    case DCV_SERVICE_STARTUP:
      g_signal_emit (G_OBJECT (service), signals[START], 0);
      break;
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
      g_signal_emit (G_OBJECT (service), signals[STOP], 0);
      g_application_release (priv->application);
      g_application_quit (priv->application);
      break;
    case SERVICE_CONTROL_PAUSE:
      g_signal_emit (G_OBJECT (service), signals[PAUSE], 0);
      break;
    case SERVICE_CONTROL_CONTINUE:
      g_signal_emit (G_OBJECT (service), signals[RESUME], 0);
      break;
    }

  g_cond_signal (&priv->control_cond);

  return G_SOURCE_REMOVE;
}

static void WINAPI
control_handler (DWORD control)
{
  WingService *service;
  WingServicePrivate *priv;
  IdleEventData *data;

  service = wing_service_get_default ();
  if (service == NULL)
    return;

  priv = wing_service_get_instance_private (service);

  data = g_slice_new (IdleEventData);
  data->service = service;
  data->control = control;

  g_mutex_lock (&priv->control_mutex);

  switch (control)
  {
    case DCV_SERVICE_STARTUP:
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
      break;
    case SERVICE_CONTROL_SHUTDOWN:
      /* do not waste time informing the SCM about the state just do it */
      g_idle_add_full (G_PRIORITY_HIGH,
                       on_control_handler_idle,
                       data, free_idle_event_data);
      g_cond_wait (&priv->control_cond, &priv->control_mutex);
      break;
    default:
      /* XXX: do something else here? */
      g_slice_free (IdleEventData, data);
  }

  g_mutex_unlock (&priv->control_mutex);

  if (priv->status.dwCurrentState != SERVICE_STOPPED)
    SetServiceStatus (priv->status_handle, &priv->status);
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

  priv->status_handle = RegisterServiceCtrlHandlerW (priv->namew, control_handler);

  control_handler (DCV_SERVICE_STARTUP);
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
      if (GetLastError () == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT)
        {
          /* Means we're started from the console, not from the service manager */
          priv->from_console = TRUE;
        }

      g_cond_signal (&priv->start_cond);
    }

  return NULL;
}

int
wing_service_run (WingService  *service,
                  int           argc,
                  char        **argv)
{
  WingServicePrivate *priv;
  int status = 0;
  gchar *thread_name;
  gint64 end_time;

  g_return_val_if_fail (WING_IS_SERVICE (service), 1);

  priv = wing_service_get_instance_private (service);

  g_mutex_lock (&priv->start_mutex);

  /* StartServiceCtrlDispatcher is a blocking method, run
   * it from a different thread to not block the whole application */
  thread_name = g_strdup_printf ("service %s", priv->name);
  priv->thread = g_thread_new (thread_name, service_dispatcher_run, service);
  g_free (thread_name);

  /* if the service did not start in 20 seconds is because something
   * bad happened, abort the execution in that case */
  end_time = g_get_monotonic_time () + 20 * G_TIME_SPAN_SECOND;
  if (!g_cond_wait_until (&priv->start_cond, &priv->start_mutex, end_time))
    {
      g_mutex_unlock (&priv->start_mutex);
      return 1;
    }

  g_mutex_unlock (&priv->start_mutex);

  status = g_application_run (priv->application, argc, argv);

  if (priv->notify_service_stop)
    set_service_status (service, SERVICE_STOPPED);

  return status;
}
