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

#include "wingeventwindow.h"
#include <gio/gio.h>
#include <glib.h>

#define WINDOW_USER_DATA_KEY L"WING_EVENT_WINDOW"

typedef LRESULT (CALLBACK *WinProcHook) (HWND   hWnd,
                                         UINT   message,
                                         WPARAM wParam,
                                         LPARAM lParam);

typedef struct
{
  WingEventCallback callback;
  gpointer user_data;
} Message;

struct _WingEventWindow
{
  GObject parent_instance;

  gchar *name;
  wchar_t *namew;
  gboolean track_clipboard;
  HWND hwnd;
  guint watch_id;
  GIOChannel *channel;
  GHashTable *messages;
};

enum
{
    PROP_0,
    PROP_NAME,
    PROP_TRACK_CLIPBOARD,
    LAST_PROP
};

static GParamSpec *props[LAST_PROP];

static void wing_event_window_initable_iface_init (GInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE (WingEventWindow,
                         wing_event_window,
                         G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                wing_event_window_initable_iface_init))

static Message *
create_message (WingEventCallback callback,
                gpointer          user_data)
{
  Message *message;

  message = g_slice_new (Message);
  message->callback = callback;
  message->user_data = user_data;

  return message;
}

static void
free_message (Message *message)
{
    g_slice_free (Message, message);
}

static void
wing_event_window_finalize (GObject *object)
{
  WingEventWindow *window = WING_EVENT_WINDOW (object);

  g_free (window->name);
  g_free (window->namew);
  DestroyWindow (window->hwnd);

  G_OBJECT_CLASS (wing_event_window_parent_class)->finalize (object);
}

static void
wing_event_window_dispose (GObject *object)
{
  WingEventWindow *window = WING_EVENT_WINDOW (object);

  if (window->watch_id != 0)
    {
      g_source_remove (window->watch_id);
      window->watch_id = 0;
    }

  g_clear_pointer (&window->messages, g_hash_table_unref);

  if (window->channel != NULL)
    {
      g_io_channel_shutdown (window->channel, FALSE, NULL);
      g_io_channel_unref (window->channel);
      window->channel = NULL;
    }

  G_OBJECT_CLASS (wing_event_window_parent_class)->dispose (object);
}

static void
wing_event_window_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  WingEventWindow *window = WING_EVENT_WINDOW (object);

  switch (prop_id)
    {
    case PROP_NAME:
      g_value_set_string (value, window->name);
      break;
    case PROP_TRACK_CLIPBOARD:
      g_value_set_boolean (value, window->track_clipboard);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
wing_event_window_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  WingEventWindow *window = WING_EVENT_WINDOW (object);

  switch (prop_id)
    {
    case PROP_NAME:
      window->name = g_value_dup_string (value);
      if (window->name != NULL)
        window->namew = g_utf8_to_utf16 (window->name, -1, NULL, NULL, NULL);
      break;
    case PROP_TRACK_CLIPBOARD:
      window->track_clipboard = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static LRESULT CALLBACK
wnd_proc (HWND   hwnd,
          UINT   message,
          WPARAM wparam,
          LPARAM lparam)
{
  if (message == WM_NCCREATE)
    {
      CREATESTRUCT *cs = (CREATESTRUCT *)lparam;
      SetPropW (hwnd, WINDOW_USER_DATA_KEY, (HANDLE)cs->lpCreateParams);
      return 1;
    }
  else if (message == WM_DESTROY)
    {
      PostQuitMessage (0);
      return 0;
    }
  else
    {
      WingEventWindow *window = WING_EVENT_WINDOW (GetPropW (hwnd, WINDOW_USER_DATA_KEY));
      Message *m;

      m = (Message *)g_hash_table_lookup (window->messages, GUINT_TO_POINTER(message));
      if (m != NULL)
        {
          if (m->callback (window, wparam, lparam, m->user_data))
            return 0;
        }

      return DefWindowProc (hwnd, message, wparam, lparam);
    }
}

static ATOM
register_window_class (WingEventWindow *window)
{
  WNDCLASSEXW wc;

  wc.cbSize = sizeof (WNDCLASSEX);

  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = wnd_proc;
  wc.cbClsExtra = 0;
  wc.cbWndExtra = 0;
  wc.hInstance = GetModuleHandle (NULL);
  wc.hIcon = NULL;
  wc.hCursor = NULL;
  wc.hbrBackground = NULL;
  wc.lpszMenuName = NULL;
  wc.lpszClassName = window->namew;
  wc.hIconSm = NULL;

  return RegisterClassExW (&wc);
}

static gboolean
recv_windows_message (GIOChannel  *channel,
                      GIOCondition cond,
                      gpointer     data)
{
  GIOStatus status;
  MSG msg;

  while (TRUE)
    {
      gsize bytes_read;

      status = g_io_channel_read_chars (channel, (gchar *)&msg, sizeof (MSG),
                                        &bytes_read, NULL);

      if (status == G_IO_STATUS_AGAIN)
        continue;

      break;
    }

  if (status == G_IO_STATUS_NORMAL)
    DispatchMessage (&msg);

  return TRUE;
}

static void
wing_event_window_class_init (WingEventWindowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = wing_event_window_finalize;
  object_class->dispose = wing_event_window_dispose;
  object_class->get_property = wing_event_window_get_property;
  object_class->set_property = wing_event_window_set_property;

  props[PROP_NAME] =
    g_param_spec_string ("name",
                         "Name",
                         "Name",
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY);

  props[PROP_TRACK_CLIPBOARD] =
    g_param_spec_boolean ("track-clipboard",
                          "Track clipboard",
                          "Track clipboard",
                          FALSE,
                          G_PARAM_READWRITE |
                          G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_properties (object_class, LAST_PROP, props);
}

static void
wing_event_window_init (WingEventWindow *window)
{
  window->messages = g_hash_table_new_full (g_direct_hash,
                                            g_direct_equal,
                                            NULL,
                                            (GDestroyNotify)free_message);
}

static gboolean
wing_event_window_initable_init (GInitable     *initable,
                                 GCancellable  *cancellable,
                                 GError       **error)
{
  WingEventWindow *window = WING_EVENT_WINDOW (initable);
  long style;

  if (g_cancellable_set_error_if_cancelled (cancellable, error))
    return FALSE;


  if (!register_window_class (window))
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                           "Window Registration Failed");
      return FALSE;
    }

  window->hwnd = CreateWindowW (window->namew, window->namew, WS_POPUPWINDOW,
                                0, 100000, 1, 1, NULL, NULL,
                                GetModuleHandle (NULL), window);
  if (!window->hwnd)
    {
      gchar *err_msg;

      err_msg = g_win32_error_message (GetLastError ());

      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Could not create window '%s': %s",
                   window->name, err_msg);
      g_free (err_msg);
      return FALSE;
    }

  if (window->track_clipboard &&
      !AddClipboardFormatListener (window->hwnd))
    {
      gchar *err_msg;

      err_msg = g_win32_error_message (GetLastError ());

      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Could not add clipboard format listener: %s",
                   err_msg);
      g_free (err_msg);
      return FALSE;
    }

  /* Special style to remove the application from the Windows taskbar */
  ShowWindow (window->hwnd, SW_HIDE);
  style = GetWindowLong (window->hwnd, GWL_EXSTYLE);
  style |= WS_EX_TOOLWINDOW; /* flags don't work - windows remains in taskbar */
  style &= ~(WS_EX_APPWINDOW);
  SetWindowLong (window->hwnd, GWL_EXSTYLE, style);
  ShowWindow (window->hwnd, SW_SHOW);

  window->channel = g_io_channel_win32_new_messages ((gsize)window->hwnd);
  g_io_channel_set_encoding (window->channel, NULL, NULL);
  window->watch_id = g_io_add_watch (window->channel, G_IO_IN, recv_windows_message, window);

  return TRUE;
}

static void
wing_event_window_initable_iface_init (GInitableIface *iface)
{
  iface->init = wing_event_window_initable_init;
}

WingEventWindow *
wing_event_window_new (const gchar  *name,
                       gboolean      track_clipboard,
                       GError      **error)
{
  return g_initable_new (WING_TYPE_EVENT_WINDOW,
                         NULL, error,
                         "name", name,
                         "track-clipboard", track_clipboard,
                         NULL);
}

HWND
wing_event_window_get_hwnd (WingEventWindow *window)
{
  g_return_val_if_fail (WING_IS_EVENT_WINDOW (window), NULL);

  return window->hwnd;
}

void
wing_event_window_connect (WingEventWindow   *window,
                           guint              message,
                           WingEventCallback  callback,
                           gpointer           user_data)
{
  Message *m;

  g_return_if_fail (WING_IS_EVENT_WINDOW (window));
  g_return_if_fail (callback != NULL);

  m = create_message (callback, user_data);
  g_hash_table_insert (window->messages, GUINT_TO_POINTER (message), m);
}

/* ex:set ts=2 et: */
