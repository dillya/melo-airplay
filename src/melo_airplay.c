/*
 * Copyright (C) 2020 Alexandre Dilly <dillya@sparod.com>
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 2.1 of the License, or any later version.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.
 * See the GNU Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 */

#include <stddef.h>

#include <melo/melo_module.h>

#define MELO_LOG_TAG "melo_airplay"
#include <melo/melo_log.h>

#include "melo_airplay_player.h"
#include "melo_airplay_rtsp.h"

#define MELO_AIRPLAY_ID "com.sparod.airplay"

static MeloAirplayPlayer *player;
static MeloAirplayRtsp *rtsp;

static void
melo_airplay_enable (void)
{
  /* Create airplay player */
  player = melo_airplay_player_new ();

  /* Create RTSP server */
  rtsp = melo_airplay_rtsp_new ();
  if (rtsp) {
    /* Attach player to RTSP server */
    melo_airplay_rtsp_set_player (rtsp, player);

    /* Start RTSP server */
    melo_airplay_rtsp_start (rtsp);
  }
}

static void
melo_airplay_disable (void)
{
  /* Stop RTSP server */
  melo_airplay_rtsp_stop (rtsp);

  /* Release RTSP server */
  g_object_unref (rtsp);

  /* Release airplay player */
  g_object_unref (player);
}

static const char *melo_airplay_player_list[] = {MELO_AIRPLAY_PLAYER_ID, NULL};

const MeloModule MELO_MODULE_SYM = {
    .id = MELO_AIRPLAY_ID,
    .version = MELO_VERSION (1, 0, 0),
    .api_version = MELO_API_VERSION,

    .name = "AirPlay",
    .description = "AirPlay support for Melo.",

    .browser_list = NULL,
    .player_list = melo_airplay_player_list,

    .enable_cb = melo_airplay_enable,
    .disable_cb = melo_airplay_disable,
};
