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
