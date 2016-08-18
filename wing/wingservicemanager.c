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

#include "wingservicemanager.h"
#include "wingservice-private.h"

#include <gio/gio.h>
#include <Windows.h>

struct _WingServiceManager
{
  GObject parent_instance;
};

struct _WingServiceManagerClass
{
  GObjectClass parent;
};

typedef struct _WingServiceManagerClass     WingServiceManagerClass;

G_DEFINE_TYPE (WingServiceManager, wing_service_manager, G_TYPE_OBJECT)

static void
wing_service_manager_class_init (WingServiceManagerClass *klass)
{
}

static void
wing_service_manager_init (WingServiceManager *self)
{
}

static SC_HANDLE
open_sc_manager (DWORD    desired_access,
                 GError **error)
{
  SC_HANDLE handle;

  handle = OpenSCManager (NULL, NULL, desired_access);
  if (handle == NULL)
    {
      int errsv = GetLastError ();
      gchar *emsg = g_win32_error_message (errsv);

      g_set_error (error, G_IO_ERROR,
                   g_io_error_from_win32_error (errsv),
                   emsg);
      g_free (emsg);
    }

  return handle;
}

static SC_HANDLE
open_service (SC_HANDLE     sc,
              WingService  *service,
              DWORD         desired_access,
              GError      **error)
{
  SC_HANDLE handle;

  handle = OpenServiceW (sc, _wing_service_get_namew (service),
                         desired_access);
  if (handle == NULL)
    {
      int errsv = GetLastError ();
      gchar *emsg = g_win32_error_message (errsv);

      g_set_error (error, G_IO_ERROR,
                   g_io_error_from_win32_error (errsv),
                   emsg);
      g_free (emsg);
    }

  return handle;
}

WingServiceManager *
wing_service_manager_new (void)
{
    return g_object_new (WING_TYPE_SERVICE_MANAGER, NULL);
}

static wchar_t *
get_file_path (void)
{
  wchar_t *path;

  path = g_malloc (MAX_PATH);

  GetModuleFileNameW (NULL, path, MAX_PATH);
  return path;
}

gboolean
wing_service_manager_install_service (WingServiceManager  *manager,
                                      WingService         *service,
                                      GError             **error)
{
  SC_HANDLE sc;
  SC_HANDLE service_handle;
  wchar_t *path;
  gboolean result = FALSE;
  const gchar *service_name;

  g_return_val_if_fail (WING_IS_SERVICE_MANAGER (manager), FALSE);
  g_return_val_if_fail (WING_IS_SERVICE (service), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  sc = open_sc_manager (SC_MANAGER_ALL_ACCESS, error);
  if (sc == NULL)
    return FALSE;

  path = get_file_path ();
  service_name = wing_service_get_name (service);
  service_handle = CreateServiceW (sc,
                                   _wing_service_get_namew (service),
                                   _wing_service_get_namew (service),
                                   SERVICE_ALL_ACCESS,
                                   SERVICE_WIN32_OWN_PROCESS | SERVICE_INTERACTIVE_PROCESS,
                                   SERVICE_AUTO_START,
                                   SERVICE_ERROR_NORMAL,
                                   path,
                                   NULL, 0, NULL,
                                   NULL, NULL);

  if (service_handle != NULL)
    {
      result = TRUE;
      CloseServiceHandle(service_handle);
    }
  else
    {
      int errsv = GetLastError ();
      gchar *emsg = g_win32_error_message (errsv);

      g_set_error (error, G_IO_ERROR,
                   g_io_error_from_win32_error (errsv),
                   emsg);
      g_free (emsg);
    }

  CloseServiceHandle (sc);

  return result;
}

gboolean
wing_service_manager_uninstall_service (WingServiceManager  *manager,
                                        WingService         *service,
                                        GError             **error)
{
  SC_HANDLE sc;
  SC_HANDLE service_handle;
  gboolean result = FALSE;

  g_return_val_if_fail (WING_IS_SERVICE_MANAGER (manager), FALSE);
  g_return_val_if_fail (WING_IS_SERVICE (service), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  sc = open_sc_manager (SC_MANAGER_ALL_ACCESS, error);
  if (sc == NULL)
    return FALSE;

  service_handle = open_service (sc, service, DELETE, error);
  if (service_handle != NULL)
    {
      if (DeleteService (service_handle))
        result = TRUE;
      else
        {
          int errsv = GetLastError ();
          gchar *emsg = g_win32_error_message (errsv);

          g_set_error (error, G_IO_ERROR,
                       g_io_error_from_win32_error (errsv),
                       emsg);
          g_free (emsg);
        }

      CloseServiceHandle (service_handle);
    }

  CloseServiceHandle (sc);

  return result;
}

gboolean
wing_service_manager_get_service_installed (WingServiceManager  *manager,
                                            WingService         *service,
                                            GError             **error)
{
  SC_HANDLE sc;
  SC_HANDLE service_handle;
  gboolean result = FALSE;

  g_return_val_if_fail (WING_IS_SERVICE_MANAGER (manager), FALSE);
  g_return_val_if_fail (WING_IS_SERVICE (service), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  sc = open_sc_manager (0, error);
  if (sc == NULL)
    return FALSE;

  service_handle = open_service (sc, service, SERVICE_QUERY_STATUS, error);
  if (service_handle != NULL)
    {
      result = TRUE;
      CloseServiceHandle (service_handle);
    }

  CloseServiceHandle (sc);

  return result;
}

gboolean
wing_service_manager_get_service_running (WingServiceManager  *manager,
                                          WingService         *service,
                                          GError             **error)
{
  SC_HANDLE sc;
  SC_HANDLE service_handle;
  SERVICE_STATUS info;
  gboolean result = FALSE;

  g_return_val_if_fail (WING_IS_SERVICE_MANAGER (manager), FALSE);
  g_return_val_if_fail (WING_IS_SERVICE (service), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  sc = open_sc_manager (0, error);
  if (sc == NULL)
    return FALSE;

  service_handle = open_service (sc, service, SERVICE_QUERY_STATUS, error);
  if (service_handle != NULL)
    {
      if (QueryServiceStatus (service_handle, &info))
        result = info.dwCurrentState != SERVICE_STOPPED;

      CloseServiceHandle (service_handle);
    }

  CloseServiceHandle (sc);

  return result;
}

gboolean
wing_service_manager_start_service (WingServiceManager  *manager,
                                    WingService         *service,
                                    int                  argc,
                                    char               **argv,
                                    GError             **error)
{
  SC_HANDLE sc;
  SC_HANDLE service_handle;
  gboolean result = FALSE;

  g_return_val_if_fail (WING_IS_SERVICE_MANAGER (manager), FALSE);
  g_return_val_if_fail (WING_IS_SERVICE (service), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  sc = open_sc_manager (SC_MANAGER_CONNECT, error);
  if (sc == NULL)
    return FALSE;

  service_handle = open_service (sc, service, SERVICE_START, error);
  if (service_handle != NULL)
    {
      gint i;
      wchar_t **argvw;

      argvw = g_new (wchar_t *, argc);
      for (i = 0; i < argc; i++)
        argvw[i] = g_utf8_to_utf16 (argv[i], -1, NULL, NULL, NULL);

      if (StartServiceW (service_handle, argc, argvw))
        result = TRUE;
      else
        {
          int errsv = GetLastError ();
          gchar *emsg = g_win32_error_message (errsv);

          g_set_error (error, G_IO_ERROR,
                       g_io_error_from_win32_error (errsv),
                       emsg);
          g_free (emsg);
        }

      g_strfreev ((gchar **)argvw);

      CloseServiceHandle (service_handle);
  }

  CloseServiceHandle (sc);

  return result;
}

gboolean
wing_service_manager_stop_service (WingServiceManager  *manager,
                                   WingService         *service,
                                   GError             **error)
{
  SC_HANDLE sc;
  SC_HANDLE service_handle;
  gboolean result = FALSE;

  g_return_val_if_fail (WING_IS_SERVICE_MANAGER (manager), FALSE);
  g_return_val_if_fail (WING_IS_SERVICE (service), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  sc = open_sc_manager (SC_MANAGER_CONNECT, error);
  if (sc == NULL)
    return FALSE;

  service_handle = open_service (sc, service,
                                 SERVICE_STOP | SERVICE_QUERY_STATUS,
                                 error);
  if (service_handle != NULL)
    {
      SERVICE_STATUS status;

      if (ControlService (service_handle, SERVICE_CONTROL_STOP, &status))
        {
          gboolean stopped = status.dwCurrentState == SERVICE_STOPPED;
          gint i = 0;

          /* It may take some time to get a response that the service was stopped */
          while (!stopped && i < 10)
            {
              g_usleep (200);

              if (!QueryServiceStatus (service_handle, &status))
                break;

              stopped = status.dwCurrentState == SERVICE_STOPPED;
              i++;
            }

          result = stopped;
        }
      else
        {
          int errsv = GetLastError ();
          gchar *emsg = g_win32_error_message (errsv);

          g_set_error (error, G_IO_ERROR,
                       g_io_error_from_win32_error (errsv),
                       emsg);
          g_free (emsg);
        }

      CloseServiceHandle (service_handle);
    }

  CloseServiceHandle (sc);

  return result;
}
