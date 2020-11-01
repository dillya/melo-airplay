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

#include <ifaddrs.h>
#include <net/if.h>
#include <netpacket/packet.h>
#include <sys/types.h>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

#include <gst/sdp/sdp.h>

#include <melo/melo_cover.h>
#include <melo/melo_mdns.h>
#include <melo/melo_playlist.h>
#include <melo/melo_rtsp_server.h>

#define MELO_LOG_TAG "airplay_rtsp"
#include <melo/melo_log.h>

#include "melo_airplay_pkey.h"
#include "melo_airplay_player.h"
#include "melo_airplay_rtsp.h"

typedef struct {
  /* Connection */
  MeloRtspServerConnection *conn;

  /* Authentication */
  bool is_auth;

  /* Content type */
  char *type;

  /* Item status */
  uint64_t mper;
  char *cover;

  /* Cover art */
  unsigned char *img;
  size_t img_size;
  size_t img_len;

  /* Format */
  MeloAirplayCodec codec;
  char *format;

  /* AES key and IV */
  unsigned char *key;
  size_t key_len;
  unsigned char *iv;
  size_t iv_len;

  /* RAOP configuration */
  MeloAirplayTransport transport;
  unsigned int port;
  unsigned int control_port;
  unsigned int timing_port;
  char *client_ip;
  unsigned int client_control_port;
  unsigned int client_timing_port;

  /* Airplay player */
  MeloAirplayPlayer *player;
} MeloAirplayClient;

struct _MeloAirplayRtsp {
  /* Parent instance */
  GObject parent_instance;

  /* RTSP server */
  GMutex mutex;
  MeloRtspServer *server;
  unsigned int port;
  bool is_started;

  /* Authentication */
  RSA *pkey;
  char *password;

  /* Service */
  char *name;
  unsigned char hw_addr[6];
  MeloMdns *mdns;
  const MeloMdnsService *service;

  /* Player */
  MeloAirplayPlayer *player;
  MeloAirplayClient *current_client;
};

G_DEFINE_TYPE (MeloAirplayRtsp, melo_airplay_rtsp, G_TYPE_OBJECT)

static void melo_airplay_rtsp_request_cb (MeloRtspServerConnection *connection,
    MeloRtspMethod method, const char *url, void *user_data, void **conn_data);
static void melo_airplay_rtsp_read_cb (MeloRtspServerConnection *connection,
    unsigned char *buffer, size_t size, bool last, void *user_data,
    void **conn_data);
static void melo_airplay_rtsp_close_cb (
    MeloRtspServerConnection *connection, void *user_data, void **conn_data);

static unsigned char *melo_airplay_rtsp_base64_decode (
    const char *text, size_t *out_len);

static void
melo_airplay_rtsp_finalize (GObject *gobject)
{
  MeloAirplayRtsp *rtsp = MELO_AIRPLAY_RTSP (gobject);

  /* Stop server */
  melo_airplay_rtsp_stop (rtsp);

  /* Free mDNS client */
  if (rtsp->mdns)
    g_object_unref (rtsp->mdns);

  /* Free RTSP server */
  melo_rtsp_server_stop (rtsp->server);

  g_object_unref (rtsp->server);

  /* Free private key */
  if (rtsp->pkey)
    RSA_free (rtsp->pkey);

  /* Free password */
  g_free (rtsp->password);

  /* Free name */
  g_free (rtsp->name);

  /* Clear mutex */
  g_mutex_clear (&rtsp->mutex);

  /* Chain up to the parent class */
  G_OBJECT_CLASS (melo_airplay_rtsp_parent_class)->finalize (gobject);
}

static void
melo_airplay_rtsp_class_init (MeloAirplayRtspClass *klass)
{
  GObjectClass *oclass = G_OBJECT_CLASS (klass);

  /* Add custom finalize() function */
  oclass->finalize = melo_airplay_rtsp_finalize;
}

static void
melo_airplay_rtsp_set_hardware_address (MeloAirplayRtsp *rtsp)
{
  static const unsigned char default_hw_addr[6] = {
      0x00, 0x51, 0x52, 0x53, 0x54, 0x55};

  struct ifaddrs *ifap, *i;

  /* Get network interfaces */
  if (getifaddrs (&ifap))
    goto default_hw;

  /* Find first MAC */
  for (i = ifap; i != NULL; i = i->ifa_next) {
    if (i && i->ifa_addr->sa_family == AF_PACKET &&
        !(i->ifa_flags & IFF_LOOPBACK)) {
      struct sockaddr_ll *s = (struct sockaddr_ll *) i->ifa_addr;
      memcpy (rtsp->hw_addr, s->sll_addr, 6);
      break;
    }
  }

  /* Free interfaces list */
  freeifaddrs (ifap);

  /* Hardware address found */
  if (i)
    return;

default_hw:
  /* Set default hardware address */
  memcpy (rtsp->hw_addr, default_hw_addr, 6);
}

static void
melo_airplay_rtsp_init (MeloAirplayRtsp *self)
{
  BIO *temp_bio;

  /* Init mutex */
  g_mutex_init (&self->mutex);

  /* Load RSA private key */
  temp_bio = BIO_new_mem_buf (AIRPORT_PRIVATE_KEY, -1);
  self->pkey = PEM_read_bio_RSAPrivateKey (temp_bio, NULL, NULL, NULL);
  BIO_free (temp_bio);

  /* Set hardware address */
  melo_airplay_rtsp_set_hardware_address (self);

  /* Create RTSP server */
  self->server = melo_rtsp_server_new ();
  melo_rtsp_server_set_request_callback (
      self->server, melo_airplay_rtsp_request_cb, self);
  melo_rtsp_server_set_read_callback (
      self->server, melo_airplay_rtsp_read_cb, self);
  melo_rtsp_server_set_close_callback (
      self->server, melo_airplay_rtsp_close_cb, self);

  /* Create mDNS client */
  self->mdns = melo_mdns_new ();
}

/**
 * melo_airplay_rtsp_new:
 *
 * Instantiate a new #MeloAirplayRtsp object.
 *
 * Returns: (transfer full): the new #MeloAirplayRtsp instance or %NULL if
 * failed.
 */
MeloAirplayRtsp *
melo_airplay_rtsp_new (void)
{
  return g_object_new (MELO_TYPE_AIRPLAY_RTSP, NULL);
}

static void
melo_airplay_rtsp_update_service (MeloAirplayRtsp *rtsp)
{
#define RAOP_SERVICE_TXT \
  "tp=TCP,UDP", "sm=false", "sv=false", "ek=1", "et=0,1", "cn=0,1", "ch=2", \
      "ss=16", "sr=44100", password, "vn=3", "md=0,1,2", "txtvers=1", NULL
  gchar *password, *sname;

  if (!rtsp->mdns)
    return;

  /* Generate service name */
  sname = g_strdup_printf ("%02x%02x%02x%02x%02x%02x@%s", rtsp->hw_addr[0],
      rtsp->hw_addr[1], rtsp->hw_addr[2], rtsp->hw_addr[3], rtsp->hw_addr[4],
      rtsp->hw_addr[5], rtsp->name);

  /* Set password */
  password = rtsp->password && *rtsp->password != '\0' ? "pw=true" : "pw=false";

  /* Add service */
  if (!rtsp->service)
    rtsp->service = melo_mdns_add_service (
        rtsp->mdns, sname, "_raop._tcp", rtsp->port, RAOP_SERVICE_TXT);
  else
    melo_mdns_update_service (rtsp->mdns, rtsp->service, sname, NULL,
        rtsp->port, true, RAOP_SERVICE_TXT);

  /* Free service name */
  g_free (sname);
#undef RAOP_SERVICE_TXT
}

static void
melo_airplay_rtsp_settings_cb (MeloAirplayPlayer *player, void *user_data)
{
  MeloAirplayRtsp *rtsp = MELO_AIRPLAY_RTSP (user_data);

  /* Update name */
  g_free (rtsp->name);
  rtsp->name = g_strdup (melo_airplay_player_get_name (rtsp->player));

  /* Update password */
  g_free (rtsp->password);
  rtsp->password = g_strdup (melo_airplay_player_get_password (rtsp->player));

  /* Update port */
  rtsp->port = melo_airplay_player_get_port (rtsp->player);

  /* Update service */
  if (rtsp->is_started)
    melo_airplay_rtsp_update_service (rtsp);
}

void
melo_airplay_rtsp_set_player (MeloAirplayRtsp *rtsp, MeloAirplayPlayer *player)
{
  if (!rtsp)
    return;

  rtsp->player = player;
  if (player)
    melo_airplay_player_set_settings_cb (
        player, melo_airplay_rtsp_settings_cb, rtsp);
}

bool
melo_airplay_rtsp_start (MeloAirplayRtsp *rtsp)
{
  if (!rtsp || rtsp->is_started)
    return false;

  /* Set default name and port */
  g_free (rtsp->name);
  g_free (rtsp->password);
  if (rtsp->player) {
    rtsp->name = g_strdup (melo_airplay_player_get_name (rtsp->player));
    rtsp->password = g_strdup (melo_airplay_player_get_password (rtsp->player));
    rtsp->port = melo_airplay_player_get_port (rtsp->player);
  } else {
    rtsp->name = g_strdup ("Melo");
    rtsp->password = NULL;
    rtsp->port = 5000;
  }

  /* Start RTSP server */
  melo_rtsp_server_start (rtsp->server, rtsp->port);
  melo_rtsp_server_attach (rtsp->server, g_main_context_default ());

  /* Update mDNS service */
  melo_airplay_rtsp_update_service (rtsp);
  rtsp->is_started = true;

  return true;
}

bool
melo_airplay_rtsp_stop (MeloAirplayRtsp *rtsp)
{
  if (!rtsp || !rtsp->is_started)
    return false;

  /* Free mDNS client */
  if (rtsp->service)
    melo_mdns_remove_service (rtsp->mdns, rtsp->service);
  rtsp->service = NULL;

  /* Stop RTSP server */
  melo_rtsp_server_stop (rtsp->server);
  rtsp->is_started = false;

  return true;
}

static bool
melo_airplay_rtsp_init_apple_response (
    MeloAirplayRtsp *rtsp, MeloRtspServerConnection *connection)
{
  const char *challenge;
  unsigned char *rsa_response;
  char *response;
  char tmp[32];
  size_t len;

  /* Get Apple challenge */
  challenge =
      melo_rtsp_server_connection_get_header (connection, "Apple-Challenge");
  if (!challenge)
    return false;

  /* Copy string and padd with '=' if missing */
  strcpy (tmp, challenge);
  if (tmp[22] == '\0') {
    tmp[22] = '=';
    tmp[23] = '=';
  } else if (tmp[23] == '\0')
    tmp[23] = '=';

  /* Decode base64 string */
  g_base64_decode_inplace (tmp, &len);
  if (len < 16)
    return false;

  /* Make the response */
  memcpy (tmp + 16, melo_rtsp_server_connection_get_server_ip (connection), 4);
  memcpy (tmp + 20, rtsp->hw_addr, 6);
  memset (tmp + 26, 0, 6);

  /* Sign response with private key */
  len = RSA_size (rtsp->pkey);
  rsa_response = g_slice_alloc (len);
  RSA_private_encrypt (
      32, (unsigned char *) tmp, rsa_response, rtsp->pkey, RSA_PKCS1_PADDING);

  /* Encode response in base64 */
  response = g_base64_encode (rsa_response, len);
  g_slice_free1 (len, rsa_response);
  len = strlen (response);

  /* Remove '=' at end */
  if (response[len - 2] == '=')
    response[len - 2] = '\0';
  else if (response[len - 1] == '=')
    response[len - 1] = '\0';

  /* Add Apple-response to RTSP response */
  melo_rtsp_server_connection_add_header (
      connection, "Apple-Response", response);
  g_free (response);

  return true;
}

static bool
melo_airplay_rtsp_request_setup (MeloAirplayRtsp *rtsp,
    MeloRtspServerConnection *connection, MeloAirplayClient *client)
{
  const char *header, *h;
  const char *hostname;
  char *transport;
  char *player_name;
  MeloTags *tags;

  /* Get Transport header */
  header = melo_rtsp_server_connection_get_header (connection, "Transport");
  if (!header)
    return false;

  /* Get transport type */
  if (strstr (header, "TCP"))
    client->transport = MELO_AIRPLAY_TRANSPORT_TCP;
  else
    client->transport = MELO_AIRPLAY_TRANSPORT_UDP;

  /* Get control port */
  h = strstr (header, "control_port=");
  if (h)
    client->control_port = strtoul (h + 13, NULL, 10);

  /* Get timing port */
  h = strstr (header, "timing_port=");
  if (h)
    client->timing_port = strtoul (h + 12, NULL, 10);

  /* Set client IP and ports */
  g_free (client->client_ip);
  client->client_ip =
      g_strdup (melo_rtsp_server_connection_get_ip_string (connection));
  client->client_control_port = client->control_port;
  client->client_timing_port = client->timing_port;

  /* Get client hostname and create player name */
  hostname = melo_rtsp_server_connection_get_hostname (connection);
  player_name =
      g_strdup_printf ("Airplay: %s", hostname ? hostname : "unknown");

  /* Set airplay icon */
  tags = melo_tags_new ();
  melo_tags_set_cover (tags, NULL, MELO_AIRPLAY_PLAYER_ICON);

  /* Set player */
  melo_playlist_play_media (MELO_AIRPLAY_PLAYER_ID, NULL, player_name, tags);
  g_free (player_name);

  /* Close current client */
  if (rtsp->current_client && rtsp->current_client != client) {
    melo_rtsp_server_connection_close (rtsp->current_client->conn);
    rtsp->current_client->player = NULL;
  }

  /* Replace client */
  client->player = rtsp->player;
  rtsp->current_client = client;

  /* Setup player */
  client->port = 6000;
  if (!melo_airplay_player_setup (client->player, client->transport,
          client->client_ip, &client->port, &client->control_port,
          &client->timing_port, client->codec, client->format, client->key,
          client->key_len, client->iv, client->iv_len)) {
    melo_rtsp_server_connection_init_response (
        connection, 500, "Internal error");
    return false;
  }

  /* Prepare response */
  melo_rtsp_server_connection_add_header (
      connection, "Audio-Jack-Status", "connected; type=analog");
  if (client->transport == MELO_AIRPLAY_TRANSPORT_TCP)
    transport = g_strdup_printf ("RTP/AVP/TCP;unicast;interleaved=0-1;"
                                 "mode=record;server_port=%d;",
        client->port);
  else
    transport = g_strdup_printf ("RTP/AVP/UDP;unicast;interleaved=0-1;"
                                 "mode=record;control_port=%d;timing_port=%d;"
                                 "server_port=%d;",
        client->control_port, client->timing_port, client->port);
  melo_rtsp_server_connection_add_header (connection, "Transport", transport);
  melo_rtsp_server_connection_add_header (connection, "Session", "1");
  g_free (transport);

  return true;
}

static void
melo_airplay_rtsp_get_rtp_info (MeloRtspServerConnection *connection,
    unsigned int *seq, unsigned int *timestamp)
{
  const char *header, *h;

  header = melo_rtsp_server_connection_get_header (connection, "RTP-Info");
  if (!header)
    return;

  /* Get next sequence number */
  h = strstr (header, "seq=");
  if (h && seq)
    *seq = strtoul (h + 4, NULL, 10);

  /* Get next timestamp */
  h = strstr (header, "rtptime=");
  if (h && timestamp)
    *timestamp = strtoul (h + 8, NULL, 10);
}

static void
melo_airplay_rtsp_request_cb (MeloRtspServerConnection *connection,
    MeloRtspMethod method, const char *url, void *user_data, void **conn_data)
{
  MeloAirplayRtsp *rtsp = MELO_AIRPLAY_RTSP (user_data);
  MeloAirplayClient *client = (MeloAirplayClient *) *conn_data;
  unsigned int seq = 0;

  /* Create new client */
  if (!client) {
    client = g_slice_new0 (MeloAirplayClient);
    client->conn = connection;
    *conn_data = client;
  }

  /* Lock mutex */
  g_mutex_lock (&rtsp->mutex);

  /* Prepare response */
  if (!client->is_auth && rtsp->password && *rtsp->password != '\0' &&
      !melo_rtsp_server_connection_digest_auth_check (
          connection, NULL, rtsp->password, rtsp->name)) {
    melo_rtsp_server_connection_digest_auth_response (
        connection, rtsp->name, NULL, 0);
    method = -1;
  } else {
    client->is_auth = true;
    melo_rtsp_server_connection_init_response (connection, 200, "OK");
  }

  /* Unlock mutex */
  g_mutex_unlock (&rtsp->mutex);

  /* Prepare Apple response */
  melo_airplay_rtsp_init_apple_response (rtsp, connection);

  /* Set common headers */
  melo_rtsp_server_connection_add_header (connection, "Server", "Melo/1.0");
  melo_rtsp_server_connection_add_header (connection, "CSeq",
      melo_rtsp_server_connection_get_header (connection, "CSeq"));

  /* Parse method */
  switch (method) {
  case MELO_RTSP_METHOD_OPTIONS:
    /* Set available methods */
    melo_rtsp_server_connection_add_header (connection, "Public",
        "ANNOUNCE, SETUP, RECORD, PAUSE,"
        "FLUSH, TEARDOWN, OPTIONS, "
        "GET_PARAMETER, SET_PARAMETER");
    break;
  case MELO_RTSP_METHOD_SETUP:
    /* Setup client and player */
    melo_airplay_rtsp_request_setup (rtsp, connection, client);
    break;
  case MELO_RTSP_METHOD_RECORD:
    /* Get first RTP sequence number */
    melo_airplay_rtsp_get_rtp_info (connection, &seq, NULL);

    /* Start player */
    melo_airplay_player_record (client->player, seq);
    break;
  case MELO_RTSP_METHOD_TEARDOWN:
    if (rtsp->current_client == client) {
      melo_airplay_player_teardown (client->player);
      rtsp->current_client = NULL;
    }
    client->player = NULL;
    break;
  case MELO_RTSP_METHOD_UNKNOWN:
    if (!g_strcmp0 (melo_rtsp_server_connection_get_method_name (connection),
            "FLUSH")) {
      /* Get RTP flush sequence number */
      melo_airplay_rtsp_get_rtp_info (connection, &seq, NULL);

      /* Pause player */
      melo_airplay_player_flush (client->player, seq);
    }
    break;
  case MELO_RTSP_METHOD_SET_PARAMETER:
  case MELO_RTSP_METHOD_GET_PARAMETER:
    /* Save content type */
    g_free (client->type);
    client->type = g_strdup (
        melo_rtsp_server_connection_get_header (connection, "Content-Type"));

    /* Reset cover */
    if (!g_strcmp0 (client->type, "image/none")) {
      /* Remove cover */
      g_free (client->cover);
      client->cover = NULL;

      /* Reset player cover */
      if (client->mper)
        melo_airplay_player_reset_cover (client->player);
    }
    break;
  default:;
  }
}

static bool
melo_airplay_rtsp_read_announce (MeloAirplayRtsp *rtsp,
    MeloAirplayClient *client, unsigned char *buffer, size_t size)
{
  const GstSDPMedia *media = NULL;
  GstSDPMessage *sdp;
  const char *rtpmap = NULL;
  unsigned int i, count;
  size_t len;
  bool ret = false;

  /* Init SDP message */
  gst_sdp_message_new (&sdp);
  gst_sdp_message_init (sdp);

  /* Parse SDP packet */
  if (gst_sdp_message_parse_buffer (buffer, size, sdp) != GST_SDP_OK)
    goto end;

  /* Get audio media */
  count = gst_sdp_message_medias_len (sdp);
  for (i = 0; i < count; i++) {
    const GstSDPMedia *m = gst_sdp_message_get_media (sdp, i);
    if (!g_strcmp0 (gst_sdp_media_get_media (m), "audio")) {
      media = m;
      break;
    }
  }
  if (!media)
    goto end;

  /* Parse all attributes */
  count = gst_sdp_media_attributes_len (media);
  for (i = 0; i < count; i++) {
    const GstSDPAttribute *attr = gst_sdp_media_get_attribute (media, i);

    /* Find rtpmap, ftmp, rsaaeskey and aesiv */
    if (!g_strcmp0 (attr->key, "rtpmap")) {
      /* Get codec */
      rtpmap = attr->value;
      const char *codec = attr->value + 3;

      /* Find codec */
      if (!strncmp (codec, "L16", 3))
        client->codec = MELO_AIRPLAY_CODEC_PCM;
      else if (!strncmp (codec, "AppleLossless", 13))
        client->codec = MELO_AIRPLAY_CODEC_ALAC;
      else if (!strncmp (codec, "mpeg4-generic", 13))
        client->codec = MELO_AIRPLAY_CODEC_AAC;
      else
        goto end;
    } else if (!g_strcmp0 (attr->key, "fmtp")) {
      /* Get format string */
      g_free (client->format);
      client->format = g_strdup (attr->value);
    } else if (!g_strcmp0 (attr->key, "rsaaeskey")) {
      unsigned char *key;

      /* Decode AES key from base64 */
      key = melo_airplay_rtsp_base64_decode (attr->value, &len);

      /* Allocate new AES key */
      client->key_len = RSA_size (rtsp->pkey);
      if (!client->key)
        client->key = g_slice_alloc (client->key_len);
      if (!client->key) {
        g_free (key);
        goto end;
      }

      /* Decrypt AES key */
      if (!RSA_private_decrypt (len, (unsigned char *) key,
              (unsigned char *) client->key, rtsp->pkey,
              RSA_PKCS1_OAEP_PADDING)) {
        g_free (key);
        goto end;
      }
      g_free (key);
    } else if (!g_strcmp0 (attr->key, "aesiv")) {
      /* Get AES IV */
      g_free (client->iv);
      client->iv =
          melo_airplay_rtsp_base64_decode (attr->value, &client->iv_len);
    }
  }

  /* Add a pseudo format for PCM */
  if (client->codec == MELO_AIRPLAY_CODEC_PCM && !client->format)
    client->format = g_strdup (rtpmap);

  /* A format and a key has been found */
  if (client->format && client->key)
    ret = true;
end:
  /* Free SDP message */
  gst_sdp_message_free (sdp);

  return ret;
}

static bool
melo_airplay_rtsp_read_params (
    MeloAirplayClient *client, unsigned char *buffer, size_t size)
{
  char *req = (char *) buffer;
  char *value;

  /* Check request content */
  if (size > 8 && !strncmp (req, "volume: ", 8)) {
    double volume;

    /* Get volume from request */
    value = g_strndup (req + 8, size - 8);
    volume = g_strtod (value, NULL);
    g_free (value);

    /* Set volume */
    melo_airplay_player_set_volume (client->player, volume);
  } else if (size > 10 && !strncmp (req, "progress: ", 10)) {
    unsigned int start, cur, end;
    char *v;

    /* Get RTP time values */
    value = g_strndup (req + 10, size - 10);
    start = strtoul (value, &v, 10);
    cur = strtoul (v + 1, &v, 10);
    end = strtoul (v + 1, NULL, 10);
    g_free (value);

    /* Set position and duration */
    melo_airplay_player_set_progress (client->player, start, cur, end);
  } else
    return false;

  return true;
}

static bool
melo_airplay_rtsp_read_tags (
    MeloAirplayClient *client, unsigned char *buffer, size_t size)
{
  bool reset = !client->mper;
  MeloTags *tags;
  size_t len;

  /* Skip first header */
  if (size > 8 && !memcmp (buffer, "mlit", 4)) {
    buffer += 8;
    size -= 8;
  }

  /* Create a new tags */
  tags = melo_tags_new ();
  if (!tags) {
    MELO_LOGE ("failed to create tags");
    return false;
  }

  /* Parse all buffer */
  while (size > 8) {
    char *tmp = (char *) (buffer + 8);

    /* Get tag length */
    len = buffer[4] << 24 | buffer[5] << 16 | buffer[6] << 8 | buffer[7];

    /* Get values */
    if (!memcmp (buffer, "minm", 4)) {
      tmp = g_strndup (tmp, len);
      melo_tags_set_title (tags, tmp);
      g_free (tmp);
    } else if (!memcmp (buffer, "asar", 4)) {
      tmp = g_strndup (tmp, len);
      melo_tags_set_artist (tags, tmp);
      g_free (tmp);
    } else if (!memcmp (buffer, "asal", 4)) {
      tmp = g_strndup (tmp, len);
      melo_tags_set_album (tags, tmp);
      g_free (tmp);
    } else if (!memcmp (buffer, "asgn", 4)) {
      tmp = g_strndup (tmp, len);
      melo_tags_set_genre (tags, tmp);
      g_free (tmp);
    } else if (!memcmp (buffer, "mper", 4) && len == 8) {
      uint64_t mper = *((uint64_t *) tmp);

      /* Item has changed */
      if (client->mper != mper)
        reset = true;
      client->mper = mper;
    }

    /* Go to next block */
    buffer += len + 8;
    size -= len + 8;
  }

  /* Set current cover */
  melo_tags_set_cover (tags, NULL, client->cover);

  /* Update tags in player */
  melo_airplay_player_take_tags (client->player, tags, reset);

  return true;
}

static bool
melo_airplay_rtsp_read_image (MeloRtspServerConnection *connection,
    MeloAirplayClient *client, unsigned char *buffer, size_t size, bool last)
{
  /* First packet */
  if (!client->img) {
    client->img_len = 0;
    client->img_size =
        melo_rtsp_server_connection_get_content_length (connection);
    client->img = g_malloc (client->img_size);
    if (!client->img)
      return false;
  }

  /* Copy data */
  memcpy (client->img + client->img_len, buffer, size);
  client->img_len += size;

  /* Last packet */
  if (last) {
    MeloTags *tags;

    /* Save cover to cache */
    g_free (client->cover);
    client->cover = melo_cover_cache_save (client->img, client->img_size,
        melo_cover_type_from_mime_type (client->type), g_free, client->img);

    /* Update cover only if meta have been received once */
    if (client->mper) {
      /* Create new tags */
      tags = melo_tags_new ();
      if (tags) {
        /* Attach cover to tags */
        melo_tags_set_cover (tags, NULL, client->cover);

        /* Send cover to player */
        melo_airplay_player_take_tags (client->player, tags, false);
      }
    }

    /* Free cover */
    client->img_size = 0;
    client->img = NULL;
  }

  return true;
}

static bool
melo_airplay_rtsp_write_params (MeloRtspServerConnection *connection,
    MeloAirplayClient *client, unsigned char *buffer, size_t size)
{
  char *req = (char *) buffer;

  /* Check request content */
  if (size > 6 && !strncmp (req, "volume", 6)) {
    double volume;
    char *packet;
    size_t len;

    /* Get volume */
    volume = melo_airplay_player_get_volume (client->player);

    /* Add headers for content type and length */
    melo_rtsp_server_connection_add_header (
        connection, "Content-Type", "text/parameters");

    /* Create and add response body */
    packet = g_strdup_printf ("volume: %.6f\r\n", volume);
    len = strlen (packet);
    melo_rtsp_server_connection_set_packet (
        connection, (unsigned char *) packet, len, (GDestroyNotify) g_free);
  } else
    return false;

  return true;
}

static void
melo_airplay_rtsp_read_cb (MeloRtspServerConnection *connection,
    unsigned char *buffer, size_t size, bool last, void *user_data,
    void **conn_data)
{
  MeloAirplayRtsp *rtsp = MELO_AIRPLAY_RTSP (user_data);
  MeloAirplayClient *client = (MeloAirplayClient *) *conn_data;

  /* Parse method */
  switch (melo_rtsp_server_connection_get_method (connection)) {
  case MELO_RTSP_METHOD_ANNOUNCE:
    melo_airplay_rtsp_read_announce (rtsp, client, buffer, size);
    break;
  case MELO_RTSP_METHOD_SET_PARAMETER:
    /* Get content type */
    if (!client->type)
      break;

    /* Parse content type */
    if (!g_strcmp0 (client->type, "text/parameters"))
      /* Get parameters (volume or progress) */
      melo_airplay_rtsp_read_params (client, buffer, size);
    else if (!g_strcmp0 (client->type, "application/x-dmap-tagged"))
      /* Get media tags */
      melo_airplay_rtsp_read_tags (client, buffer, size);
    else if (g_str_has_prefix (client->type, "image/"))
      /* Get cover art */
      melo_airplay_rtsp_read_image (connection, client, buffer, size, last);
    break;
  case MELO_RTSP_METHOD_GET_PARAMETER:
    /* Get content type */
    if (!client->type)
      break;

    /* Get volume */
    if (!g_strcmp0 (client->type, "text/parameters"))
      melo_airplay_rtsp_write_params (connection, client, buffer, size);
    break;
  default:;
  }
}

static void
melo_airplay_rtsp_close_cb (
    MeloRtspServerConnection *connection, void *user_data, void **conn_data)
{
  MeloAirplayRtsp *rtsp = (MeloAirplayRtsp *) user_data;
  MeloAirplayClient *client = (MeloAirplayClient *) *conn_data;

  if (!client)
    return;

  /* Not current client */
  if (rtsp->current_client == client) {
    melo_airplay_player_teardown (client->player);
    rtsp->current_client = NULL;
  }

  /* Free AES key */
  if (client->key)
    g_slice_free1 (client->key_len, client->key);
  g_free (client->iv);

  /* Free client data */
  g_free (client->client_ip);
  g_free (client->format);
  g_free (client->type);
  g_free (client->img);
  g_free (client->cover);
  g_slice_free (MeloAirplayClient, client);
}

static unsigned char *
melo_airplay_rtsp_base64_decode (const char *text, size_t *out_len)
{
  gint state = 0;
  guint save = 0;
  unsigned char *out;
  size_t len;

  /* Allocate output buffer */
  len = strlen (text);
  out = g_malloc ((len * 3 / 4) + 3);

  /* Decode string */
  len = g_base64_decode_step (text, len, out, &state, &save);
  while (state)
    len += g_base64_decode_step ("=", 1, out + len, &state, &save);

  /* Return values */
  *out_len = len;
  return out;
}
