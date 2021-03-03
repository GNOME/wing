/*
 * Copyright © 2021 NICE s.r.l.
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
 * Authors: Silvio Lazzeretti <silviola@amazon.com>
 */


#include "wingthreadpoolio.h"
#include "wingutils.h"
#include <windows.h>

/**
 * SECTION:wingthreadpoolio
 * @short_description: A wrapper around a Windows thread pool IO.
 *
 * WingThreadPoolIo creates a ThreadpoolIO object attached to an handle.
 */

/**
 * WingThreadPoolIo:
 *
 * A wrapper around a Windows thread pool IO.
 */
struct _WingThreadPoolIo
{
  volatile gint ref_count;

  PTP_IO thread_pool_io;
  HANDLE handle;
};

G_DEFINE_BOXED_TYPE(WingThreadPoolIo, wing_thread_pool_io, wing_thread_pool_io_ref, wing_thread_pool_io_unref)

static void CALLBACK
threadpool_io_completion (PTP_CALLBACK_INSTANCE instance,
                          PVOID                 ctxt,
                          PVOID                 overlapped,
                          ULONG                 result,
                          ULONG_PTR             number_of_bytes_transferred,
                          PTP_IO                threadpool_io)
{
  WingOverlappedData *overlapped_data = (WingOverlappedData *) overlapped;

  overlapped_data->callback (instance,
                             ctxt,
                             overlapped,
                             result,
                             number_of_bytes_transferred,
                             threadpool_io,
                             overlapped_data->user_data);
}

WingThreadPoolIo *
wing_thread_pool_io_new (void *handle)
{
  WingThreadPoolIo *self;
  PTP_IO thread_pool_io;

  g_return_val_if_fail (handle != NULL && (HANDLE) handle != INVALID_HANDLE_VALUE, NULL);

  thread_pool_io = CreateThreadpoolIo ((HANDLE) handle, threadpool_io_completion, NULL, NULL);
  if (thread_pool_io == NULL)
    {
      gchar *emsg;

      emsg = g_win32_error_message (GetLastError ());
      g_warning ("Failed to create thread pool IO: %s", emsg);
      g_free (emsg);

      return NULL;
    }

  self = g_slice_new (WingThreadPoolIo);
  self->ref_count = 1;

  self->handle = (HANDLE) handle;
  self->thread_pool_io = thread_pool_io;

  return self;
}

WingThreadPoolIo *
wing_thread_pool_io_ref (WingThreadPoolIo *self)
{
  g_return_val_if_fail (self != NULL, NULL);

  g_atomic_int_inc (&self->ref_count);

  return self;
}

static gboolean
destroy_thread_pool_io_idle (gpointer user_data)
{
  WingThreadPoolIo *self = (WingThreadPoolIo *) user_data;

  WaitForThreadpoolIoCallbacks (self->thread_pool_io, FALSE);
  CloseThreadpoolIo (self->thread_pool_io);
  g_slice_free (WingThreadPoolIo, self);

  return G_SOURCE_REMOVE;
}

void
wing_thread_pool_io_unref (WingThreadPoolIo *self)
{
  g_return_if_fail (self != NULL);

  if (g_atomic_int_dec_and_test (&self->ref_count))
    {
      if (self->handle != INVALID_HANDLE_VALUE)
        CloseHandle (self->handle);

      g_idle_add (destroy_thread_pool_io_idle, (gpointer) self);
    }
}

void
wing_thread_pool_io_start (WingThreadPoolIo *self)
{
  g_return_if_fail (self != NULL);

  StartThreadpoolIo (self->thread_pool_io);
}

void
wing_thread_pool_io_cancel (WingThreadPoolIo *self)
{
  g_return_if_fail (self != NULL);

  CancelThreadpoolIo (self->thread_pool_io);
}

void *
wing_thread_pool_get_handle (WingThreadPoolIo *self)
{
  g_return_val_if_fail (self != NULL, (void *) INVALID_HANDLE_VALUE);

  return (void *) self->handle;
}

gboolean
wing_thread_pool_io_close_handle (WingThreadPoolIo  *self,
                                  GError           **error)
{
  gboolean res;

  g_return_val_if_fail (self != NULL, FALSE);

  res = CloseHandle (self->handle);
  if (!res && error != NULL)
    {
      int errsv = GetLastError ();
      gchar *emsg = g_win32_error_message (errsv);

      g_set_error (error, G_IO_ERROR,
                   g_io_error_from_win32_error (errsv),
                   "Error closing handle: %s",
                   emsg);
      g_free (emsg);
      return FALSE;
    }

  self->handle = INVALID_HANDLE_VALUE;

  return TRUE;
}