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
 * Authors: Silvio Lazzeretti <silviola@amazon.com>
 */

#include <stdlib.h>
#include <string.h>

#include <glib.h>

#include "wingcredentials.h"

struct _WingCredentials
{
  GObject parent_instance;

  gulong pid;
  gchar *sid;
};

enum
{
  PROP_0,
  PROP_PID,
  PROP_SID,
  LAST_PROP
};

static GParamSpec *props[LAST_PROP];

G_DEFINE_TYPE (WingCredentials, wing_credentials, G_TYPE_OBJECT)

static void
wing_credentials_finalize (GObject *object)
{
  WingCredentials *credentials = WING_CREDENTIALS (object);

  g_free (credentials->sid);

  G_OBJECT_CLASS(wing_credentials_parent_class)->finalize(object);
}

static void
wing_credentials_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  WingCredentials *credentials = WING_CREDENTIALS (object);

  switch (prop_id)
    {
    case PROP_PID:
      g_value_set_ulong (value, credentials->pid);
      break;
    case PROP_SID:
      g_value_set_string (value, credentials->sid);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
wing_credentials_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  WingCredentials *credentials = WING_CREDENTIALS (object);

  switch (prop_id)
    {
    case PROP_PID:
      credentials->pid = g_value_get_ulong (value);
      break;
    case PROP_SID:
      credentials->sid = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
wing_credentials_class_init (WingCredentialsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = wing_credentials_finalize;
  object_class->get_property = wing_credentials_get_property;
  object_class->set_property = wing_credentials_set_property;

  props[PROP_PID] =
    g_param_spec_ulong ("pid",
                        "pid",
                        "pid",
                        0,
                        G_MAXULONG,
                        0,
                        G_PARAM_READWRITE |
                        G_PARAM_CONSTRUCT_ONLY |
                        G_PARAM_STATIC_STRINGS);

  props[PROP_SID] =
    g_param_spec_string ("sid",
                         "sid",
                         "sid",
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, LAST_PROP, props);
}

static void
wing_credentials_init (WingCredentials *credentials)
{

}

/**
 * wing_credentials_new():
 *
 * Creates a new #WingCredentials object with the provided
 * process and user identifiers
 *
 * Returns: (transfer full): A #WingCredentials. Free with g_object_unref().
 */
WingCredentials *
wing_credentials_new (gulong       pid,
                      const gchar *sid)
{
  g_return_val_if_fail (pid != 0, NULL);
  g_return_val_if_fail (sid != NULL, NULL);

  return g_object_new (WING_TYPE_CREDENTIALS,
                       "pid", pid,
                       "sid", sid,
                       NULL);
}

/**
 * wing_credentials_to_string():
 * @credentials: A #WingCredentials object.
 *
 * Creates a human-readable textual representation of @credentials
 * that can be used in logging and debug messages. The format of the
 * returned string may change in future release.
 *
 * Returns: (transfer full): A string that should be freed with g_free().
 */
gchar *
wing_credentials_to_string (WingCredentials *credentials)
{
  GString *ret;

  g_return_val_if_fail (WING_IS_CREDENTIALS (credentials), NULL);

  ret = g_string_new ("WingCredentials:");
  g_string_append_printf (ret, "pid=%lu,", credentials->pid);
  g_string_append_printf (ret, "sid=%s", credentials->sid);

  return g_string_free (ret, FALSE);
}

/**
 * wing_credentials_is_same_user():
 * @credentials: A #WingCredentials.
 * @other_credentials: A #WingCredentials.
 *
 * Checks if @credentials and @other_credentials are the same user.
 *
 * Returns: %TRUE if @credentials and @other_credentials have the same
 * user, %FALSE otherwise.
 */
gboolean
wing_credentials_is_same_user (WingCredentials *credentials,
                               WingCredentials *other_credentials)
{
  g_return_val_if_fail (WING_IS_CREDENTIALS (credentials), FALSE);
  g_return_val_if_fail (WING_IS_CREDENTIALS (other_credentials), FALSE);

  return g_strcmp0 (credentials->sid, other_credentials->sid) == 0;
}

/**
 * wing_credentials_get_sid():
 * @credentials: A #WingCredentials
 *
 * Get the user identifier from @credentials.
 *
 * Returns: The user identifier.
 */
const gchar *
wing_credentials_get_sid (WingCredentials  *credentials)
{
  g_return_val_if_fail (WING_IS_CREDENTIALS (credentials), NULL);

  return credentials->sid;
}

/**
 * wing_credentials_get_pid():
 * @credentials: A #WingCredentials
 *
 * Get the process identifier from @credentials.
 *
 * Returns: The process ID.
 */
gulong
wing_credentials_get_pid (WingCredentials  *credentials)
{
  g_return_val_if_fail (WING_IS_CREDENTIALS (credentials), 0);

  return credentials->pid;
}
