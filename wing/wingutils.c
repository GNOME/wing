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
#include <Psapi.h>

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
                         gint *minor)
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

  return t1 / 10 - 11644473600000000; /* Jan 1, 1601 */
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
