/*
 * melo_config_airplay.c: Airplay module configuration
 *
 * Copyright (C) 2016 Alexandre Dilly <dillya@sparod.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#include "melo_airplay.h"
#include "melo_config_airplay.h"

static MeloConfigItem melo_config_general[] = {
  {
    .id = "name",
    .name = "Device name",
    .type = MELO_CONFIG_TYPE_STRING,
    .element = MELO_CONFIG_ELEMENT_TEXT,
    .def._string = "Melo",
  },
  {
    .id = "port",
    .name = "RTSP port",
    .type = MELO_CONFIG_TYPE_INTEGER,
    .element = MELO_CONFIG_ELEMENT_NUMBER,
    .def._integer = 5000,
  },
  {
    .id = "password",
    .name = "Password",
    .type = MELO_CONFIG_TYPE_STRING,
    .element = MELO_CONFIG_ELEMENT_PASSWORD,
  },
};

static MeloConfigItem melo_config_advanced[] = {
  {
    .id = "latency",
    .name = "Latency of output (in ms)",
    .type = MELO_CONFIG_TYPE_INTEGER,
    .element = MELO_CONFIG_ELEMENT_NUMBER,
    .def._integer = 1000,
  },
  {
    .id = "rtx_delay",
    .name = "Delay before retransmit request (in ms)",
    .type = MELO_CONFIG_TYPE_INTEGER,
    .element = MELO_CONFIG_ELEMENT_NUMBER,
    .def._integer = 500,
  },
  {
    .id = "rtx_retry_period",
    .name = "Delay between two retransmit request (in ms)",
    .type = MELO_CONFIG_TYPE_INTEGER,
    .element = MELO_CONFIG_ELEMENT_NUMBER,
    .def._integer = 100,
  },
  {
    .id = "hack_sync",
    .name = "[HACK] Disable sync on audio output sink",
    .type = MELO_CONFIG_TYPE_BOOLEAN,
    .element = MELO_CONFIG_ELEMENT_CHECKBOX,
  },
};

static MeloConfigGroup melo_config_airplay[] = {
  {
    .id = "general",
    .name = "General",
    .items = melo_config_general,
    .items_count = G_N_ELEMENTS (melo_config_general),
  },
  {
    .id = "advanced",
    .name = "Advanced",
    .items = melo_config_advanced,
    .items_count = G_N_ELEMENTS (melo_config_advanced),
  },
};

MeloConfig *
melo_config_airplay_new (void)
{
  return melo_config_new ("airplay", melo_config_airplay,
                          G_N_ELEMENTS (melo_config_airplay));
}

void
melo_config_airplay_update (MeloConfigContext *context, gpointer user_data)
{
  MeloAirplay *air = MELO_AIRPLAY (user_data);
  const gchar *old, *new;
  gint64 port, old_port;

  /* Update name */
  if (melo_config_get_updated_string (context, "name", &new, &old) &&
      g_strcmp0 (new, old))
    melo_airplay_set_name (air, new);

  /* Update port */
  if (melo_config_get_updated_integer (context, "port", &port, &old_port) &&
      port != old_port)
    melo_airplay_set_port (air, port);

  /* Update password */
  if (melo_config_get_updated_string (context, "password", &new, &old) &&
      g_strcmp0 (new, old))
    melo_airplay_set_password (air, new);
}

void
melo_config_airplay_update_advanced (MeloConfigContext *context,
                                     gpointer user_data)
{
  MeloAirplay *air = MELO_AIRPLAY (user_data);
  gint64 delay, period;
  gint64 new, old;
  gboolean ret;

  /* Update latency */
  if (melo_config_get_updated_integer (context, "latency", &new, &old) &&
      new != old)
    melo_airplay_set_latency (air, new);

  /* Update retransmit delay */
  ret = melo_config_get_updated_integer (context, "rtx_delay", &delay, &old) &&
        delay != old;
  ret |= melo_config_get_updated_integer (context, "rtx_retry_period", &period,
                                         &old) && period != old;
  if (ret)
    melo_airplay_set_rtx (air, delay, period);
}
