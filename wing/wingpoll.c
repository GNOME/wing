#include "wingpoll.h"

#ifdef G_OS_WIN32
#define STRICT
#include <windows.h>
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
	  gchar *emsg = g_win32_error_message (GetLastError ());
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
	  gchar *emsg = g_win32_error_message (GetLastError ());
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
      recursed_result = poll_rest (NULL, handles, handle_to_fd, nhandles, 0);
      return (recursed_result == -1) ? -1 : 1 + recursed_result;
    }
  else if (ready >= WAIT_OBJECT_0 && ready < WAIT_OBJECT_0 + nhandles)
    {
      f = handle_to_fd[ready - WAIT_OBJECT_0];
      f->revents = f->events;
      if (_g_main_poll_debug)
        g_print ("  got event %p\n", (HANDLE) f->fd);

      /* If no timeout and polling several handles, recurse to poll
       * the rest of them.
       */
      if (timeout == 0 && nhandles > 1)
	{
	  /* Poll the handles with index > ready */
          HANDLE  *shorter_handles;
          GPollFD **shorter_handle_to_fd;
          gint     shorter_nhandles;

          shorter_handles = &handles[ready - WAIT_OBJECT_0 + 1];
          shorter_handle_to_fd = &handle_to_fd[ready - WAIT_OBJECT_0 + 1];
          shorter_nhandles = nhandles - (ready - WAIT_OBJECT_0 + 1);

	  recursed_result = poll_rest (NULL, shorter_handles, shorter_handle_to_fd, shorter_nhandles, 0);
	  return (recursed_result == -1) ? -1 : 1 + recursed_result;
	}
      return 1;
    }

  return 0;
}

gint
wing_poll (GPollFD *fds,
           guint    nfds,
           gint     timeout)
{
  HANDLE handles[MAXIMUM_WAIT_OBJECTS];
  GPollFD *handle_to_fd[MAXIMUM_WAIT_OBJECTS];
  GPollFD *msg_fd = NULL;
  GPollFD *f;
  gint nhandles = 0;
  int retval;

  if (_g_main_poll_debug)
    g_print ("g_poll: waiting for");

  for (f = fds; f < &fds[nfds]; ++f)
    {
      if (f->fd == G_WIN32_MSG_HANDLE && (f->events & G_IO_IN))
        {
          if (_g_main_poll_debug && msg_fd == NULL)
            g_print (" MSG");
          msg_fd = f;
        }
      else if (f->fd > 0)
        {
          if (nhandles == MAXIMUM_WAIT_OBJECTS)
            {
              g_warning ("Too many handles to wait for!\n");
              break;
            }
          else
            {
              if (_g_main_poll_debug)
                g_print (" %p", (HANDLE) f->fd);
              handle_to_fd[nhandles] = f;
              handles[nhandles++] = (HANDLE) f->fd;
            }
        }
      f->revents = 0;
    }

  if (_g_main_poll_debug)
    g_print ("\n");

  if (timeout == -1)
    timeout = INFINITE;

  /* Polling for several things? */
  if (nhandles > 1 || (nhandles > 0 && msg_fd != NULL))
    {
      /* First check if one or several of them are immediately
       * available
       */
      retval = poll_rest (msg_fd, handles, handle_to_fd, nhandles, 0);

      /* If not, and we have a significant timeout, poll again with
       * timeout then. Note that this will return indication for only
       * one event, or only for messages.
       */
      if (retval == 0 && (timeout == INFINITE || timeout > 0))
	retval = poll_rest (msg_fd, handles, handle_to_fd, nhandles, timeout);
    }
  else
    {
      /* Just polling for one thing, so no need to check first if
       * available immediately
       */
      retval = poll_rest (msg_fd, handles, handle_to_fd, nhandles, timeout);
    }

  if (retval == -1)
    for (f = fds; f < &fds[nfds]; ++f)
      f->revents = 0;

  return retval;
}
