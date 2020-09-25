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

#include <stdio.h>
#include <string.h>

#include <gio/gio.h>

#define MELO_LOG_TAG "airplay_player"
#include <melo/melo_log.h>

#include "gstrtpraop.h"
#include "gstrtpraopdepay.h"
#include "gsttcpraop.h"

#include "melo_airplay_player.h"

struct _MeloAirplayPlayer {
  GObject parent_instance;

  /* Mutex */
  GMutex mutex;

  /* Gstreamer pipeline */
  GstElement *pipeline;
  GstElement *src;
  GstElement *raop_depay;
  guint bus_id;

  /* Server settings */
  MeloSettingsEntry *name;
  MeloSettingsEntry *password;
  MeloSettingsEntry *port;

  /* Player settings */
  MeloSettingsEntry *latency;
  MeloSettingsEntry *rtx_delay;
  MeloSettingsEntry *rtx_retry_period;
  MeloSettingsEntry *disable_sync;

  /* Format */
  unsigned int samplerate;
  unsigned int channel_count;

  /* Status */
  unsigned int start_rtptime;
  double volume;

  /* Settings callback */
  MeloAirplayPlayerSettingsCb settings_cb;
  void *settings_user_data;
};

MELO_DEFINE_PLAYER (MeloAirplayPlayer, melo_airplay_player)

static void melo_airplay_player_settings (
    MeloPlayer *player, MeloSettings *settings);
static bool melo_airplay_player_set_state (
    MeloPlayer *player, MeloPlayerState state);
static unsigned int melo_airplay_player_get_position (MeloPlayer *player);

static void
melo_airplay_player_finalize (GObject *object)
{
  MeloAirplayPlayer *player = MELO_AIRPLAY_PLAYER (object);

  /* Stop pipeline */
  melo_airplay_player_teardown (player);

  /* Clear mutex */
  g_mutex_clear (&player->mutex);

  /* Chain finalize */
  G_OBJECT_CLASS (melo_airplay_player_parent_class)->finalize (object);
}

static void
melo_airplay_player_class_init (MeloAirplayPlayerClass *klass)
{
  MeloPlayerClass *parent_class = MELO_PLAYER_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  /* Register TCP RAOP depayloader */
  gst_tcp_raop_plugin_init (NULL);

  /* Register RTP RAOP depayloader */
  gst_rtp_raop_plugin_init (NULL);
  gst_rtp_raop_depay_plugin_init (NULL);

  /* Setup callbacks */
  parent_class->settings = melo_airplay_player_settings;
  parent_class->set_state = melo_airplay_player_set_state;
  parent_class->get_position = melo_airplay_player_get_position;

  /* Set finalize */
  object_class->finalize = melo_airplay_player_finalize;
}

static void
melo_airplay_player_init (MeloAirplayPlayer *self)
{
  /* Init player mutex */
  g_mutex_init (&self->mutex);
}

MeloAirplayPlayer *
melo_airplay_player_new ()
{
  return g_object_new (MELO_TYPE_AIRPLAY_PLAYER, "id", MELO_AIRPLAY_PLAYER_ID,
      "name", "AirPlay", "description", "Play music with AirPlay", "icon",
      "fab:chromecast", NULL);
}

static gboolean
bus_cb (GstBus *bus, GstMessage *msg, gpointer user_data)
{
  MeloAirplayPlayer *aplayer = MELO_AIRPLAY_PLAYER (user_data);
  MeloPlayer *player = MELO_PLAYER (aplayer);

  /* Process bus message */
  switch (GST_MESSAGE_TYPE (msg)) {
  case GST_MESSAGE_EOS:
    /* Stop playing */
    gst_element_set_state (aplayer->pipeline, GST_STATE_NULL);
    melo_player_eos (player);
    break;
  case GST_MESSAGE_ERROR: {
    GError *error;

    /* Stop pipeline on error */
    gst_element_set_state (aplayer->pipeline, GST_STATE_NULL);
    melo_player_update_state (player, MELO_PLAYER_STATE_STOPPED);

    /* Set error message */
    gst_message_parse_error (msg, &error, NULL);
    melo_player_error (player, error->message);
    g_error_free (error);
    break;
  }
  default:;
  }

  return true;
}

static bool
melo_airplay_player_settings_update_cb (MeloSettings *settings,
    MeloSettingsGroup *group, char **error, void *user_data)
{
  MeloAirplayPlayer *player = MELO_AIRPLAY_PLAYER (user_data);

  if (player->settings_cb)
    player->settings_cb (player, player->settings_user_data);

  return true;
}

static void
melo_airplay_player_settings (MeloPlayer *player, MeloSettings *settings)
{
  MeloAirplayPlayer *aplayer = MELO_AIRPLAY_PLAYER (player);
  MeloSettingsGroup *group;

  /* Create general group */
  group = melo_settings_add_group (settings, "general", "General", NULL,
      melo_airplay_player_settings_update_cb, aplayer);
  aplayer->name = melo_settings_group_add_string (group, "name", "Device name",
      "Discoverable name of the device", "Melo", NULL, MELO_SETTINGS_FLAG_NONE);
  aplayer->password = melo_settings_group_add_string (group, "password",
      "Password", "Password to restict device usage", NULL, NULL,
      MELO_SETTINGS_FLAG_PASSWORD);

  /* Create advanced group */
  group = melo_settings_add_group (settings, "advanced", "Advanced", NULL,
      melo_airplay_player_settings_update_cb, aplayer);
  aplayer->port = melo_settings_group_add_uint32 (group, "port", "RTSP port",
      "Port of the RTSP server", 5000, NULL, MELO_SETTINGS_FLAG_NONE);
  aplayer->latency =
      melo_settings_group_add_uint32 (group, "latency", "Output latency",
          "Latency of output (in ms)", 1000, NULL, MELO_SETTINGS_FLAG_NONE);
  aplayer->rtx_delay = melo_settings_group_add_uint32 (group, "rtx_delay",
      "RTX delay", "Delay before retransmit request (in ms)", 500, NULL,
      MELO_SETTINGS_FLAG_NONE);
  aplayer->rtx_retry_period =
      melo_settings_group_add_uint32 (group, "rtx_retry_period",
          "RTX retry delay", "Delay between two retransmit request (in ms)",
          100, NULL, MELO_SETTINGS_FLAG_NONE);
  aplayer->disable_sync = melo_settings_group_add_boolean (group, "hack_sync",
      "Disable sync", "[HACK] Disable sync on audio output sink", false, NULL,
      MELO_SETTINGS_FLAG_NONE);
}

static bool
melo_airplay_player_set_state (MeloPlayer *player, MeloPlayerState state)
{
  if (state != MELO_PLAYER_STATE_NONE)
    return false;

  return melo_airplay_player_teardown (MELO_AIRPLAY_PLAYER (player));
}

static unsigned int
melo_airplay_player_get_position (MeloPlayer *player)
{
  MeloAirplayPlayer *aplayer = MELO_AIRPLAY_PLAYER (player);
  guint32 pos = 0;

  if (!aplayer->pipeline)
    return pos;

  /* Lock player mutex */
  g_mutex_lock (&aplayer->mutex);

  /* Get RTP time */
  if (gst_rtp_raop_depay_query_rtptime (
          GST_RTP_RAOP_DEPAY (aplayer->raop_depay), &pos)) {
    if (pos > aplayer->start_rtptime)
      pos = ((pos - aplayer->start_rtptime) * G_GUINT64_CONSTANT (1000)) /
            aplayer->samplerate;
    else
      pos = 0;
  }

  /* Unlock player mutex */
  g_mutex_unlock (&aplayer->mutex);

  return pos;
}

void
melo_airplay_player_set_settings_cb (
    MeloAirplayPlayer *player, MeloAirplayPlayerSettingsCb cb, void *user_data)
{
  player->settings_cb = cb;
  player->settings_user_data = user_data;
}

const char *
melo_airplay_player_get_name (MeloAirplayPlayer *player)
{
  const char *name;

  if (!melo_settings_entry_get_string (player->name, &name, NULL))
    name = "Melo";

  return name;
}

const char *
melo_airplay_player_get_password (MeloAirplayPlayer *player)
{
  const char *password;

  if (!melo_settings_entry_get_string (player->password, &password, NULL))
    password = NULL;

  return password;
}

unsigned int
melo_airplay_player_get_port (MeloAirplayPlayer *player)
{
  unsigned int port;

  if (!melo_settings_entry_get_uint32 (player->port, &port, NULL))
    port = 5000;

  return port;
}

static bool
melo_airplay_player_parse_format (MeloAirplayPlayer *player,
    MeloAirplayCodec codec, const char *format, const char **encoding)
{
  bool ret = true;

  switch (codec) {
  case MELO_AIRPLAY_CODEC_ALAC:
    /* Set encoding */
    *encoding = "ALAC";

    /* Get ALAC parameters:
     *  - Payload type
     *  - Max samples per frame (4 bytes)
     *  - Compatible version (1 byte)
     *  - Sample size (1 bytes)
     *  - History mult (1 byte)
     *  - Initial history (1 byte)
     *  - Rice param limit (1 byte)
     *  - Channel count (1 byte)
     *  - Max run (2 bytes)
     *  - Max coded frame size (4 bytes)
     *  - Average bitrate (4 bytes)
     *  - Sample rate (4 bytes)
     */
    if (sscanf (format, "%*d %*d %*d %*d %*d %*d %*d %d %*d %*d %*d %d",
            &player->channel_count, &player->samplerate) != 2)
      ret = false;
    break;
  case MELO_AIRPLAY_CODEC_PCM:
    /* Set encoding */
    *encoding = "L16";

    /* Get samplerate and channel count */
    if (sscanf (format, "%*d L%*d/%d/%d", &player->samplerate,
            &player->channel_count) != 2)
      ret = false;
    break;
  case MELO_AIRPLAY_CODEC_AAC:
    /* Set encoding */
    *encoding = "AAC";
  default:
    player->samplerate = 44100;
    player->channel_count = 2;
  }

  /* Set default values if not found */
  if (!player->samplerate)
    player->samplerate = 44100;
  if (!player->channel_count)
    player->channel_count = 2;

  return ret;
}

bool
melo_airplay_player_setup (MeloAirplayPlayer *player,
    MeloAirplayTransport transport, const char *ip, unsigned int *port,
    unsigned int *control_port, unsigned int *timing_port,
    MeloAirplayCodec codec, const char *format, const unsigned char *key,
    size_t key_len, const unsigned char *iv, size_t iv_len)
{
  unsigned int max_port = *port + 100;
  GstElement *src, *sink;
  GstState next_state = GST_STATE_READY;
  const char *encoding;
  GstBus *bus;

  /* Lock player mutex */
  g_mutex_lock (&player->mutex);

  if (player->pipeline)
    goto failed;

  /* Parse format */
  if (!melo_airplay_player_parse_format (player, codec, format, &encoding))
    goto failed;

  /* Create pipeline */
  player->pipeline = gst_pipeline_new (MELO_AIRPLAY_PLAYER_ID "_pipeline");

  /* Create melo audio sink */
  sink = melo_player_get_sink (
      MELO_PLAYER (player), MELO_AIRPLAY_PLAYER_ID "_sink");

  /* Create source */
  if (transport == MELO_AIRPLAY_TRANSPORT_UDP) {
    GstElement *src_caps, *raop, *rtp, *rtp_caps, *depay, *dec;
    uint32_t value_u32;
    int32_t value_i32;
    bool value_bool;
    GstCaps *caps;

    /* Add an UDP source and a RTP jitter buffer to pipeline */
    src = gst_element_factory_make ("udpsrc", NULL);
    src_caps = gst_element_factory_make ("capsfilter", NULL);
    raop = gst_element_factory_make ("rtpraop", NULL);
    rtp = gst_element_factory_make ("rtpjitterbuffer", NULL);
    rtp_caps = gst_element_factory_make ("capsfilter", NULL);
    depay = gst_element_factory_make ("rtpraopdepay", NULL);
    if (codec == MELO_AIRPLAY_CODEC_AAC)
      dec = gst_element_factory_make ("avdec_aac", NULL);
    else
      dec = gst_element_factory_make ("avdec_alac", NULL);
    gst_bin_add_many (GST_BIN (player->pipeline), src, src_caps, raop, rtp,
        rtp_caps, depay, dec, sink, NULL);

    /* Save RAOP depay element */
    player->raop_depay = depay;

    /* Set caps for UDP source -> RTP jitter buffer link */
    caps = gst_caps_new_simple ("application/x-rtp", "payload", G_TYPE_INT, 96,
        "clock-rate", G_TYPE_INT, player->samplerate, NULL);
    g_object_set (G_OBJECT (src_caps), "caps", caps, NULL);
    gst_caps_unref (caps);

    /* Set caps for RTP jitter -> RTP RAOP depayloader link */
    caps = gst_caps_new_simple ("application/x-rtp", "payload", G_TYPE_INT, 96,
        "clock-rate", G_TYPE_INT, player->samplerate, "encoding-name",
        G_TYPE_STRING, encoding, "config", G_TYPE_STRING, format, NULL);
    g_object_set (G_OBJECT (rtp_caps), "caps", caps, NULL);
    gst_caps_unref (caps);

    /* Set keys into RTP RAOP depayloader */
    if (key)
      gst_rtp_raop_depay_set_key (
          GST_RTP_RAOP_DEPAY (depay), key, key_len, iv, iv_len);

    /* Force UDP source to use a new port */
    g_object_set (src, "reuse", FALSE, NULL);

    /* Disable synchronization on sink */
    if (melo_settings_entry_get_boolean (
            player->disable_sync, &value_bool, NULL) &&
        value_bool)
      g_object_set (sink, "sync", FALSE, NULL);

    /* Set latency in jitter buffer */
    if (melo_settings_entry_get_uint32 (player->latency, &value_u32, NULL) &&
        value_u32)
      g_object_set (G_OBJECT (rtp), "latency", (guint) value_u32, NULL);

    /* Link all elements */
    gst_element_link_many (
        src, src_caps, raop, rtp, rtp_caps, depay, dec, sink, NULL);

    /* Add sync / retransmit support to pipeline */
    if (*control_port) {
      GstElement *ctrl_src, *ctrl_sink;
      GstPad *raop_pad, *udp_pad;
      guint ctrl_port = *control_port;
      guint max_control_port = *control_port + 100;
      GSocket *sock;

      /* Enable retransmit events */
      g_object_set (G_OBJECT (rtp), "do-retransmission", TRUE, NULL);

      /* Set RTX delay */
      if (melo_settings_entry_get_int32 (player->rtx_delay, &value_i32, NULL) &&
          value_i32 > 0)
        g_object_set (G_OBJECT (rtp), "rtx-delay", (gint) value_i32, NULL);

      /* Set RTX retry period */
      if (melo_settings_entry_get_int32 (
              player->rtx_retry_period, &value_i32, NULL) &&
          value_i32 > 0)
        g_object_set (
            G_OBJECT (rtp), "rtx-retry-period", (gint) value_i32, NULL);

      /* Send only one retransmit event */
      g_object_set (G_OBJECT (rtp), "rtx-max-retries", 0, NULL);

      /* Create and add control UDP source and sink */
      ctrl_src = gst_element_factory_make ("udpsrc", NULL);
      ctrl_sink = gst_element_factory_make ("udpsink", NULL);
      gst_bin_add_many (GST_BIN (player->pipeline), ctrl_src, ctrl_sink, NULL);

      /* Set control port */
      g_object_set (ctrl_src, "port", *control_port, "reuse", FALSE, NULL);
      while (gst_element_set_state (ctrl_src, GST_STATE_READY) ==
             GST_STATE_CHANGE_FAILURE) {
        /* Retry until a free port is available */
        *control_port += 2;
        if (*control_port > max_control_port)
          goto failed;

        /* Update UDP source port */
        g_object_set (ctrl_src, "port", *control_port, NULL);
      }

      /* Connect UDP source to ROAP control sink */
      udp_pad = gst_element_get_static_pad (ctrl_src, "src");
      raop_pad = gst_element_get_request_pad (raop, "sink_ctrl");
      gst_pad_link (udp_pad, raop_pad);
      gst_object_unref (raop_pad);
      gst_object_unref (udp_pad);

      /* Use socket from UDP source on UDP sink in order to get retransmit
       * replies on UDP source.
       */
      g_object_get (ctrl_src, "used-socket", &sock, NULL);
      g_object_set (ctrl_sink, "socket", sock, NULL);
      g_object_set (ctrl_sink, "port", ctrl_port, "host", ip, NULL);

      /* Disable async state and synchronization since we only send retransmit
       * requests on this UDP sink, so no need for synchronization..
       */
      g_object_set (ctrl_sink, "async", FALSE, "sync", FALSE, NULL);

      /* Connect RAOP control source to UDP sink */
      raop_pad = gst_element_get_request_pad (raop, "src_ctrl");
      udp_pad = gst_element_get_static_pad (ctrl_sink, "sink");
      gst_pad_link (raop_pad, udp_pad);
      gst_object_unref (raop_pad);
      gst_object_unref (udp_pad);
    }
  } else {
    GstElement *rtp_caps, *raop, *depay, *dec;
    GstCaps *caps;

    /* Create pipeline for TCP streaming */
    src = gst_element_factory_make ("tcpserversrc", NULL);
    rtp_caps = gst_element_factory_make ("capsfilter", NULL);
    raop = gst_element_factory_make ("tcpraop", NULL);
    depay = gst_element_factory_make ("rtpraopdepay", NULL);
    dec = gst_element_factory_make ("avdec_alac", NULL);
    gst_bin_add_many (GST_BIN (player->pipeline), src, rtp_caps, raop, depay,
        dec, sink, NULL);

    /* Save RAOP depay element */
    player->raop_depay = depay;

    /* Set caps for TCP source -> TCP RAOP depayloader link */
    caps = gst_caps_new_simple ("application/x-rtp-stream", "clock-rate",
        G_TYPE_INT, player->samplerate, "encoding-name", G_TYPE_STRING, "ALAC",
        "config", G_TYPE_STRING, format, NULL);
    g_object_set (G_OBJECT (rtp_caps), "caps", caps, NULL);
    gst_caps_unref (caps);

    /* Set keys into TCP RAOP decryptor */
    if (key)
      gst_rtp_raop_depay_set_key (
          GST_RTP_RAOP_DEPAY (depay), key, key_len, iv, iv_len);

    /* Listen on all interfaces */
    g_object_set (src, "host", "0.0.0.0", NULL);

    /* To start listening, state muste be set to playing */
    next_state = GST_STATE_PLAYING;

    /* Link all elements */
    gst_element_link_many (src, rtp_caps, raop, depay, dec, sink, NULL);
  }

  /* Set server port */
  g_object_set (src, "port", *port, NULL);

  /* Add a message handler */
  bus = gst_pipeline_get_bus (GST_PIPELINE (player->pipeline));
  player->bus_id = gst_bus_add_watch (bus, bus_cb, player);
  gst_object_unref (bus);

  /* Start the pipeline */
  while (gst_element_set_state (src, next_state) == GST_STATE_CHANGE_FAILURE) {
    /* Incremnent port until we found a free port */
    *port += 2;
    if (*port > max_port)
      goto failed;

    /* Update port */
    g_object_set (src, "port", *port, NULL);
  }

  /* Unlock player mutex */
  g_mutex_unlock (&player->mutex);

  return true;

failed:
  g_mutex_unlock (&player->mutex);
  return false;
}

bool
melo_airplay_player_record (MeloAirplayPlayer *player, unsigned int seq)
{
  if (!player || !player->pipeline)
    return false;

  /* Lock player mutex */
  g_mutex_lock (&player->mutex);

  /* Set playing */
  gst_element_set_state (player->pipeline, GST_STATE_PLAYING);
  melo_player_update_state (MELO_PLAYER (player), MELO_PLAYER_STATE_PLAYING);
  melo_player_update_stream_state (
      MELO_PLAYER (player), MELO_PLAYER_STREAM_STATE_NONE, 0);

  /* Unlock player mutex */
  g_mutex_unlock (&player->mutex);

  return true;
}

bool
melo_airplay_player_flush (MeloAirplayPlayer *player, unsigned int seq)
{
  if (!player)
    return false;

  /* Set paused */
  melo_player_update_state (MELO_PLAYER (player), MELO_PLAYER_STATE_PAUSED);

  return true;
}

bool
melo_airplay_player_teardown (MeloAirplayPlayer *player)
{
  if (!player)
    return false;

  /* Lock player mutex */
  g_mutex_lock (&player->mutex);

  /* Already stoped */
  if (!player->pipeline) {
    g_mutex_unlock (&player->mutex);
    return false;
  }

  /* Stop pipeline */
  gst_element_set_state (player->pipeline, GST_STATE_NULL);
  melo_player_update_state (MELO_PLAYER (player), MELO_PLAYER_STATE_NONE);

  /* Remove message handler */
  g_source_remove (player->bus_id);

  /* Free gstreamer pipeline */
  g_object_unref (player->pipeline);
  player->pipeline = NULL;

  /* Unlock player mutex */
  g_mutex_unlock (&player->mutex);

  return true;
}

bool
melo_airplay_player_set_volume (MeloAirplayPlayer *player, double volume)
{
  if (!player)
    return false;

  /* Set volume */
  if (volume > -144.0)
    player->volume = (volume + 30.0) / 30.0;
  else
    player->volume = 0.0;

  /* Update status volume */
  melo_player_update_volume (MELO_PLAYER (player), player->volume, false);

  return true;
}

bool
melo_airplay_player_set_progress (MeloAirplayPlayer *player, unsigned int start,
    unsigned int cur, unsigned int end)
{
  unsigned int pos, dur;

  if (!player)
    return false;

  /* Calculate position and duration */
  if (cur > start)
    pos = (cur - start) * G_GUINT64_CONSTANT (1000) / player->samplerate;
  else
    pos = 0;
  dur = (end - start) * G_GUINT64_CONSTANT (1000) / player->samplerate;

  /* Set progression */
  player->start_rtptime = start;
  melo_player_update_state (MELO_PLAYER (player), MELO_PLAYER_STATE_PLAYING);
  melo_player_update_stream_state (
      MELO_PLAYER (player), MELO_PLAYER_STREAM_STATE_NONE, 0);
  melo_player_update_duration (MELO_PLAYER (player), pos, dur);

  return true;
}

void
melo_airplay_player_take_tags (
    MeloAirplayPlayer *player, MeloTags *tags, bool reset)
{
  if (!player) {
    melo_tags_unref (tags);
    return;
  }

  if (reset)
    melo_player_update_media (
        MELO_PLAYER (player), NULL, tags, MELO_TAGS_MERGE_FLAG_NONE);
  else
    melo_player_update_tags (
        MELO_PLAYER (player), tags, MELO_TAGS_MERGE_FLAG_NONE);
}

void
melo_airplay_player_reset_cover (MeloAirplayPlayer *player)
{
  melo_player_update_tags (
      MELO_PLAYER (player), melo_tags_new (), MELO_TAGS_MERGE_FLAG_SKIP_COVER);
}

double
melo_airplay_player_get_volume (MeloAirplayPlayer *player)
{
  if (!player || player->volume == 0.0)
    return -144.0;
  return (player->volume - 1.0) * 30.0;
}
