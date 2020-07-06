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

#ifndef _MELO_AIRPLAY_RTSP_H_
#define _MELO_AIRPLAY_RTSP_H_

G_BEGIN_DECLS

#define MELO_TYPE_AIRPLAY_RTSP (melo_airplay_rtsp_get_type ())
G_DECLARE_FINAL_TYPE (
    MeloAirplayRtsp, melo_airplay_rtsp, MELO, AIRPLAY_RTSP, GObject)

MeloAirplayRtsp *melo_airplay_rtsp_new (void);

void melo_airplay_rtsp_set_player (
    MeloAirplayRtsp *rtsp, MeloAirplayPlayer *player);

bool melo_airplay_rtsp_start (MeloAirplayRtsp *rtsp);
bool melo_airplay_rtsp_stop (MeloAirplayRtsp *rtsp);

G_END_DECLS

#endif /* !_MELO_AIRPLAY_RTSP_H_ */
