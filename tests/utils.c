/*
 * Copyright (C) 2017 NICE s.r.l.
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

#include <wing/wing.h>
#include <glib.h>

static void
test_monotonic_time (void)
{
  gint64 t1, t2;

  t1 = wing_get_monotonic_time ();
  t2 = wing_get_monotonic_time ();
  g_assert_cmpint (t2, >=, t1);

  g_usleep(10000);
  t2 = wing_get_monotonic_time ();
  g_assert_cmpint (t2, >, t1 + 10000);
}

int
main(int    argc,
     char **argv)
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/utils/monotonic-time", test_monotonic_time);

  return g_test_run ();
}
