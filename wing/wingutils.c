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

#include "wingutils.h"

#include <windows.h>
#include <psapi.h>

gboolean
wing_is_wow_64 (void)
{
  typedef BOOL (WINAPI *LPFN_ISWOW64PROCESS) (HANDLE, PBOOL);
  LPFN_ISWOW64PROCESS fnIsWow64Process;
  BOOL is_wow_64 = FALSE;

  fnIsWow64Process = (LPFN_ISWOW64PROCESS)GetProcAddress (GetModuleHandle (TEXT ("kernel32")), "IsWow64Process");
  if (fnIsWow64Process != NULL)
    {
      if (!fnIsWow64Process (GetCurrentProcess (), &is_wow_64))
        return FALSE;
    }

  return is_wow_64;
}

gboolean
wing_is_os_64bit (void)
{
#ifdef _WIN64
  return TRUE;
#else
  return wing_is_wow_64 ();
#endif
}

gboolean
wing_get_version_number (gint *major,
                         gint *minor,
                         gint *build,
                         gint *product_type)
{
  typedef NTSTATUS (WINAPI fRtlGetVersion) (PRTL_OSVERSIONINFOEXW);
  OSVERSIONINFOEXW osverinfo;
  fRtlGetVersion *RtlGetVersion;
  HMODULE hmodule;

  hmodule = LoadLibraryW (L"ntdll.dll");
  g_return_val_if_fail (hmodule != NULL, FALSE);

  RtlGetVersion = (fRtlGetVersion *)GetProcAddress (hmodule, "RtlGetVersion");
  g_return_val_if_fail (RtlGetVersion != NULL, FALSE);

  memset (&osverinfo, 0, sizeof (OSVERSIONINFOEXW));
  osverinfo.dwOSVersionInfoSize = sizeof (OSVERSIONINFOEXW);
  RtlGetVersion (&osverinfo);

  FreeLibrary (hmodule);

  *major = osverinfo.dwMajorVersion;
  *minor = osverinfo.dwMinorVersion;
  *build = osverinfo.dwBuildNumber;
  *product_type = osverinfo.wProductType;

  return TRUE;
}

gboolean
wing_get_process_memory (gsize *total_virtual_memory,
                         gsize *total_physical_memory)
{
  gboolean res;
  /* see: https://msdn.microsoft.com/en-us/library/windows/desktop/ms684879(v=vs.85).aspx */
  PROCESS_MEMORY_COUNTERS_EX pmc;

  res = GetProcessMemoryInfo (GetCurrentProcess (), (PPROCESS_MEMORY_COUNTERS) &pmc, sizeof (pmc));
  if (res)
    {
      /* note: the memory shown by the task manager is usually the private working set,
       * we are using the working set size instead */
      *total_virtual_memory = pmc.WorkingSetSize + pmc.PagefileUsage;
      *total_physical_memory = pmc.WorkingSetSize;
    }

  return res;
}

static gint64
get_time_from_filetime (const FILETIME *ft)
{
  gint64 t1 = (gint64)ft->dwHighDateTime << 32 | ft->dwLowDateTime;

  return t1 / 10;
}

gboolean
wing_get_process_times (gint64 *current_user_time,
                        gint64 *current_system_time)
{
  FILETIME creation_time, exit_time, kernel_time, user_time;

  GetProcessTimes (GetCurrentProcess (), &creation_time, &exit_time, &kernel_time, &user_time);

  *current_user_time = get_time_from_filetime (&user_time);
  *current_system_time = get_time_from_filetime (&kernel_time);

  return TRUE;
}

guint
wing_get_n_processors (void)
{
  int n;
  SYSTEM_INFO sysinfo;

  GetSystemInfo (&sysinfo);
  n = sysinfo.dwNumberOfProcessors;

  return n > 1 ? (guint)n : 1;
}

gboolean
wing_overlap_wait_result (HANDLE           hfile,
                          OVERLAPPED      *overlap,
                          DWORD           *transferred,
                          GCancellable    *cancellable)
{
  GPollFD pollfd[2];
  gboolean result = FALSE;
  gint num, npoll;

#if GLIB_SIZEOF_VOID_P == 8
  pollfd[0].fd = (gint64)overlap->hEvent;
#else
  pollfd[0].fd = (gint)overlap->hEvent;
#endif
  pollfd[0].events = G_IO_IN;
  num = 1;

  if (g_cancellable_make_pollfd (cancellable, &pollfd[1]))
    num++;

loop:
  npoll = g_poll (pollfd, num, -1);
  if (npoll <= 0)
    /* error out, should never happen */
    goto end;

  if (g_cancellable_is_cancelled (cancellable))
    {
      /* CancelIO only cancels pending operations issued by the
       * current thread and since we're doing only sync operations,
       * this is safe.... */
      /* CancelIoEx is only Vista+. Since we have only one overlap
       * operaton on this thread, we can just use: */
      result = CancelIo (hfile);
      g_warn_if_fail (result);
    }

  result = GetOverlappedResult (overlap->hEvent, overlap, transferred, FALSE);
  if (result == FALSE &&
      GetLastError () == ERROR_IO_INCOMPLETE &&
      !g_cancellable_is_cancelled (cancellable))
    goto loop;

end:
  if (num > 1)
    g_cancellable_release_fd (cancellable);

  return result;
}
