melolib_LTLIBRARIES = libmelo_airplay.la

libmelo_airplay_la_SOURCES = \
	melo_config_airplay.c \
	melo_player_airplay.c \
	gstrtpraopdepay.c \
	gstrtpraop.c \
	gsttcpraop.c \
	melo_airplay.c \
	melo_airplay_plugin.c

libmelo_airplay_la_CFLAGS = \
	$(LIBSSL_CFLAGS) \
	$(GST_SDP_CFLAGS) \
	$(GST_RTP_CFLAGS) \
	$(LIBMELO_CFLAGS)

libmelo_airplay_la_LIBADD = \
	$(LIBSSL_LIBS) \
	$(GST_SDP_LIBS) \
	$(GST_RTP_LIBS) \
	$(LIBMELO_LIBS)

noinst_HEADERS = \
	melo_airplay.h \
	melo_airplay_pkey.h \
	gsttcpraop.h \
	gstrtpraop.h \
	gstrtpraopdepay.h \
	melo_player_airplay.h \
	melo_config_airplay.h
