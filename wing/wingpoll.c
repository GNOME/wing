/*
 * Copyright (C) 1995-1997  Peter Mattis, Spencer Kimball and Josh MacDonald
 *
 * gpoll.c: poll(2) abstraction
 * Copyright 1998 Owen Taylor
 * Copyright 2008 Red Hat, Inc.
 * Copyright (C) 2018 NICE s.r.l.
 * 
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Modified by the GLib Team and others 1997-2000.  See the AUTHORS
 * file for a list of people on the GLib Team.  See the ChangeLog
 * files for a list of changes.  These files are distributed with
 * GLib at ftp://ftp.gtk.org/pub/gtk/.
 */

#include "wingpoll.h"

#ifdef G_OS_WIN32
#define STRICT
#include <windows.h>
#include <process.h>
#endif /* G_OS_WIN32 */

#ifdef _WIN32
/* Always enable debugging printout on Windows, as it is more often
 * needed there...
 */
#define G_MAIN_POLL_DEBUG
#endif

#ifdef G_MAIN_POLL_DEBUG
extern gboolean _g_main_poll_debug;
#endif

static int
poll_rest (GPollFD *msg_fd,
           GPollFD *stop_fd,
           HANDLE  *handles,
           GPollFD *handle_to_fd[],
           gint     nhandles,
           gint     timeout)
{
  DWORD ready;
  GPollFD *f;
  int recursed_result;

  if (msg_fd != NULL)
    {
      /* Wait for either messages or handles
       * -> Use MsgWaitForMultipleObjectsEx
       */
      if (_g_main_poll_debug)
        g_print ("  MsgWaitForMultipleObjectsEx(%d, %d)\n", nhandles, timeout);

      ready = MsgWaitForMultipleObjectsEx (nhandles, handles, timeout,
                                           QS_ALLINPUT, MWMO_ALERTABLE);

      if (ready == WAIT_FAILED)
        {
          gchar *emsg;

          emsg = g_win32_error_message (GetLastError ());
          g_warning ("MsgWaitForMultipleObjectsEx failed: %s", emsg);
          g_free (emsg);
        }
    }
  else if (nhandles == 0)
    {
      /* No handles to wait for, just the timeout */
      if (timeout == INFINITE)
        ready = WAIT_FAILED;
      else
        {
          /* Wait for the current process to die, more efficient than SleepEx(). */
          WaitForSingleObjectEx (GetCurrentProcess (), timeout, TRUE);
          ready = WAIT_TIMEOUT;
        }
    }
  else
    {
      /* Wait for just handles
       * -> Use WaitForMultipleObjectsEx
       */
      if (_g_main_poll_debug)
        g_print ("  WaitForMultipleObjectsEx(%d, %d)\n", nhandles, timeout);

      ready = WaitForMultipleObjectsEx (nhandles, handles, FALSE, timeout, TRUE);
      if (ready == WAIT_FAILED)
        {
          gchar *emsg;

          emsg = g_win32_error_message (GetLastError ());
          g_warning ("WaitForMultipleObjectsEx failed: %s", emsg);
          g_free (emsg);
        }
    }

  if (_g_main_poll_debug)
    g_print ("  wait returns %ld%s\n",
             ready,
             (ready == WAIT_FAILED ? " (WAIT_FAILED)" :
              (ready == WAIT_TIMEOUT ? " (WAIT_TIMEOUT)" :
               (msg_fd != NULL && ready == WAIT_OBJECT_0 + nhandles ? " (msg)" : ""))));

  if (ready == WAIT_FAILED)
    return -1;
  else if (ready == WAIT_TIMEOUT ||
           ready == WAIT_IO_COMPLETION)
    return 0;
  else if (msg_fd != NULL && ready == WAIT_OBJECT_0 + nhandles)
    {
      msg_fd->revents |= G_IO_IN;

      /* If we have a timeout, or no handles to poll, be satisfied
       * with just noticing we have messages waiting.
       */
      if (timeout != 0 || nhandles == 0)
        return 1;

      /* If no timeout and handles to poll, recurse to poll them,
       * too.
       */
      recursed_result = poll_rest (NULL, stop_fd, handles, handle_to_fd, nhandles, 0);
      return (recursed_result == -1) ? -1 : 1 + recursed_result;
    }
  else if (ready >= WAIT_OBJECT_0 && ready < WAIT_OBJECT_0 + nhandles)
    {
      int retval;

      f = handle_to_fd[ready - WAIT_OBJECT_0];
      f->revents = f->events;
      if (_g_main_poll_debug)
        g_print ("  got event %p\n", (HANDLE) f->fd);

      /* Do not count the stop_fd */
      retval = (f != stop_fd) ? 1 : 0;

      /* If no timeout and polling several handles, recurse to poll
       * the rest of them.
       */
      if (timeout == 0 && nhandles > 1)
        {
          /* Poll the handles with index > ready */
          HANDLE *shorter_handles;
          GPollFD **shorter_handle_to_fd;
          gint shorter_nhandles;

          shorter_handles = &handles[ready - WAIT_OBJECT_0 + 1];
          shorter_handle_to_fd = &handle_to_fd[ready - WAIT_OBJECT_0 + 1];
          shorter_nhandles = nhandles - (ready - WAIT_OBJECT_0 + 1);

          recursed_result = poll_rest (NULL, stop_fd, shorter_handles, shorter_handle_to_fd, shorter_nhandles, 0);
          return (recursed_result == -1) ? -1 : retval + recursed_result;
        }
      return retval;
    }

  return 0;
}

typedef struct
{
  HANDLE handles[MAXIMUM_WAIT_OBJECTS];
  GPollFD *handle_to_fd[MAXIMUM_WAIT_OBJECTS];
  GPollFD *msg_fd;
  GPollFD *stop_fd;
  gint nhandles;
  gint timeout;
} GWin32PollThreadData;

static gint
poll_single_thread (GWin32PollThreadData *data)
{
  int retval;

  /* Polling for several things? */
  if (data->nhandles > 1 || (data->nhandles > 0 && data->msg_fd != NULL))
    {
      /* First check if one or several of them are immediately
       * available
       */
      retval = poll_rest (data->msg_fd, data->stop_fd, data->handles, data->handle_to_fd, data->nhandles, 0);

      /* If not, and we have a significant timeout, poll again with
       * timeout then. Note that this will return indication for only
       * one event, or only for messages.
       */
      if (retval == 0 && (data->timeout == INFINITE || data->timeout > 0))
        retval = poll_rest (data->msg_fd, data->stop_fd, data->handles, data->handle_to_fd, data->nhandles, data->timeout);
    }
  else
    {
      /* Just polling for one thing, so no need to check first if
       * available immediately
       */
      retval = poll_rest (data->msg_fd, data->stop_fd, data->handles, data->handle_to_fd, data->nhandles, data->timeout);
    }

  return retval;
}

static void
fill_poll_thread_data (GPollFD              *fds,
                       guint                 nfds,
                       gint                  timeout,
                       GPollFD              *stop_fd,
                       GWin32PollThreadData *data)
{
  GPollFD *f;

  data->timeout = timeout;

  if (stop_fd != NULL)
    {
      if (_g_main_poll_debug)
        g_print (" Stop FD: %p", (HANDLE) stop_fd->fd);

      data->stop_fd = stop_fd;
      data->handle_to_fd[data->nhandles] = stop_fd;
      data->handles[data->nhandles++] = (HANDLE) stop_fd->fd;
    }

  for (f = fds; f < &fds[nfds]; ++f)
    {
      if ((data->nhandles == MAXIMUM_WAIT_OBJECTS) ||
          (data->msg_fd != NULL && (data->nhandles == MAXIMUM_WAIT_OBJECTS - 1)))
        {
          g_warning ("Too many handles to wait for!");
          break;
        }

      if (f->fd == G_WIN32_MSG_HANDLE && (f->events & G_IO_IN))
        {
          if (_g_main_poll_debug && data->msg_fd == NULL)
            g_print (" MSG");
          data->msg_fd = f;
        }
      else if (f->fd > 0)
        {
          if (_g_main_poll_debug)
            g_print (" %p", (HANDLE) f->fd);
          data->handle_to_fd[data->nhandles] = f;
          data->handles[data->nhandles++] = (HANDLE) f->fd;
        }

      f->revents = 0;
    }
}

static guint __stdcall
poll_thread_run (gpointer user_data)
{
  GWin32PollThreadData *data = data;

  /* Docs say that it is safer to call _endthreadex by our own */
  _endthreadex (poll_single_thread (data));

  g_assert_not_reached ();

  return 0;
}

/* One slot for a possible msg object and another for the stop event */
#define MAXIMUM_WAIT_OBJECTS_PER_THREAD (MAXIMUM_WAIT_OBJECTS - 2)

gint
wing_poll (GPollFD *fds,
           guint    nfds,
           gint     timeout)
{
  guint nthreads, threads_remain;
  HANDLE thread_handles[MAXIMUM_WAIT_OBJECTS];
  GWin32PollThreadData *threads_data;
  GPollFD stop_event = { 0, };
  GPollFD *f;
  guint i, fds_idx = 0;
  DWORD ready;
  DWORD thread_retval;
  int retval;

  if (timeout == -1)
    timeout = INFINITE;

  /* Simple case without extra threads, note that the MSG fd could take a slot
   * so we check for less than MAXIMUM_WAIT_OBJECTS
   */
  if (nfds < MAXIMUM_WAIT_OBJECTS)
    {
      GWin32PollThreadData data = { 0, };

      if (_g_main_poll_debug)
        g_print ("wing_poll: waiting for");

      fill_poll_thread_data (fds, nfds, timeout, NULL, &data);

      if (_g_main_poll_debug)
        g_print ("\n");

      retval = poll_single_thread (&data);
      if (retval == -1)
        for (f = fds; f < &fds[nfds]; ++f)
          f->revents = 0;

      return retval;
    }

  if (_g_main_poll_debug)
    g_print ("wing_poll: polling with threads\n");

  nthreads = nfds / MAXIMUM_WAIT_OBJECTS_PER_THREAD;
  threads_remain = nfds % MAXIMUM_WAIT_OBJECTS_PER_THREAD;
  if (threads_remain > 0)
    nthreads++;

  if (nthreads > MAXIMUM_WAIT_OBJECTS)
    {
      g_warning ("Too many handles to wait for in threads!");
      nthreads = MAXIMUM_WAIT_OBJECTS;
    }

#if GLIB_SIZEOF_VOID_P == 8
  stop_event.fd = (gint64)CreateEventW (NULL, TRUE, FALSE, NULL);
#else
  stop_event.fd = (gint)CreateEventW (NULL, TRUE, FALSE, NULL);
#endif
  stop_event.events = G_IO_IN;

  threads_data = g_new0 (GWin32PollThreadData, nthreads);
  for (i = 0; i < nthreads; i++)
    {
      guint thread_fds;
      guint ignore;

      if (i == (nthreads - 1) && threads_remain > 0)
        thread_fds = threads_remain;
      else
        thread_fds = MAXIMUM_WAIT_OBJECTS_PER_THREAD;

      fill_poll_thread_data (fds + fds_idx, thread_fds, timeout, &stop_event, &threads_data[i]);
      fds_idx += thread_fds;

      thread_handles[i] = (HANDLE) _beginthreadex (NULL, 0, poll_thread_run, &threads_data[i], 0, &ignore);
    }

  /* Wait for at least one thread to return */
  WaitForMultipleObjects (nthreads, thread_handles, FALSE, timeout);

  /* Signal the stop in case any of the threads did not stop yet */
  SetEvent ((HANDLE)stop_event.fd);

  /* Wait for the rest of the threads to finish */
  WaitForMultipleObjects (nthreads, thread_handles, TRUE, INFINITE);

  /* The return value of all the threads give us all the fds that changed state */
  retval = 0;
  for (i = 0; i < nthreads; i++)
    {
      if (GetExitCodeThread (thread_handles[i], &thread_retval))
        retval = retval == -1 ? -1 : thread_retval == -1 ? -1 : retval + thread_retval;

      CloseHandle (thread_handles[i]);
    }

  if (retval == -1)
    for (f = fds; f < &fds[nfds]; ++f)
      f->revents = 0;

  g_free (threads_data);
  CloseHandle ((HANDLE)stop_event.fd);

  return retval;
}
