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

#ifndef _MELO_AIRPLAY_PLAYER_H_
#define _MELO_AIRPLAY_PLAYER_H_

#include <melo/melo_player.h>

G_BEGIN_DECLS

#define MELO_AIRPLAY_PLAYER_ID "com.sparod.airplay.player"

#define MELO_AIRPLAY_PLAYER_ICON \
  "svg:<svg viewBox=\"0 0 46 42\"><g><path " \
  "d=\"M22.2,24.2L8.5,39.9c-0.5,0.6-0.1,1.5,0.7,1.5h27.5c0.8,0,1.2-0.9,0.7-1." \
  "5L23.8,24.2c-0.2-0.2-0.5-0.4-0.8-0.4C22.7,23.8,22.4,23.9,22.2,24.2 " \
  "M6.5,0.6c-2.3,0-3.1,0.2-3.9,0.7C1.8,1.7,1.1,2.4,0.7,3.2C0.2,4,0,4.9,0,7." \
  "1v17.5c0,2.3,0.2,3.1,0.7,3.9c0.4,0.8,1.1,1.5,1.9,1.9c0.8,0.4,1.7,0.7,3.9," \
  "0.7h5.2l2.2-2.6H5.8c-1.1,0-1.6-0.1-2-0.3c-0.4-0.2-0.7-0.5-1-1c-0.2-0.4-0." \
  "3-0.8-0.3-2v-19c0-1.1,0.1-1.6,0.3-2c0.2-0.4,0.5-0.7,1-1c0.4-0.2,0.8-0.3,2-" \
  "0.3h34.3c1.1,0,1.6,0.1,2,0.3c0.4,0.2,0.7,0.5,1,1c0.2,0.4,0.3,0.8,0.3," \
  "2v19c0,1.1-0.1,1.6-0.3,2c-0.2,0.4-0.5,0.7-1,1c-0.4,0.2-0.8,0.3-2,0.3H32l2." \
  "2,2.6h5.2c2.3,0,3.1-0.2,3.9-0.7c0.8-0.4,1.5-1.1,1.9-1.9c0.4-0.8,0.7-1.7,0." \
  "7-3.9V7.1c0-2.3-0.2-3.1-0.7-3.9c-0.4-0.8-1.1-1.5-1.9-1.9c-0.8-0.4-1.7-0.7-" \
  "3.9-0.7H6.5z\"/></g></svg>"

#define MELO_TYPE_AIRPLAY_PLAYER melo_airplay_player_get_type ()
MELO_DECLARE_PLAYER (MeloAirplayPlayer, melo_airplay_player, AIRPLAY_PLAYER)

typedef enum {
  MELO_AIRPLAY_CODEC_ALAC = 0,
  MELO_AIRPLAY_CODEC_PCM,
  MELO_AIRPLAY_CODEC_AAC,
} MeloAirplayCodec;

typedef enum {
  MELO_AIRPLAY_TRANSPORT_TCP = 0,
  MELO_AIRPLAY_TRANSPORT_UDP,
} MeloAirplayTransport;

typedef void (*MeloAirplayPlayerSettingsCb) (
    MeloAirplayPlayer *player, void *user_data);

MeloAirplayPlayer *melo_airplay_player_new (void);

void melo_airplay_player_set_settings_cb (
    MeloAirplayPlayer *player, MeloAirplayPlayerSettingsCb cb, void *user_data);
const char *melo_airplay_player_get_name (MeloAirplayPlayer *player);
const char *melo_airplay_player_get_password (MeloAirplayPlayer *player);
unsigned int melo_airplay_player_get_port (MeloAirplayPlayer *player);

bool melo_airplay_player_setup (MeloAirplayPlayer *player,
    MeloAirplayTransport transport, const char *ip, unsigned int *port,
    unsigned int *control_port, unsigned int *timing_port,
    MeloAirplayCodec codec, const char *format, const unsigned char *key,
    size_t key_len, const unsigned char *iv, size_t iv_len);
bool melo_airplay_player_record (MeloAirplayPlayer *player, unsigned int seq);
bool melo_airplay_player_flush (MeloAirplayPlayer *player, unsigned int seq);
bool melo_airplay_player_teardown (MeloAirplayPlayer *player);

bool melo_airplay_player_set_volume (MeloAirplayPlayer *player, double volume);
bool melo_airplay_player_set_progress (MeloAirplayPlayer *player,
    unsigned int start, unsigned int cur, unsigned int end);
void melo_airplay_player_take_tags (
    MeloAirplayPlayer *player, MeloTags *tags, bool reset);
void melo_airplay_player_reset_cover (MeloAirplayPlayer *player);

double melo_airplay_player_get_volume (MeloAirplayPlayer *player);

G_END_DECLS

#endif /* !_MELO_AIRPLAY_PLAYER_H_ */
