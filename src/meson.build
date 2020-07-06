# Melo AirPlay module

# Module sources
src = [
	'gstrtpraop.c',
	'gstrtpraopdepay.c',
	'gsttcpraop.c',
	'melo_airplay_player.c',
	'melo_airplay_rtsp.c',
	'melo_airplay.c'
]

# Library dependencies
libmelo_dep = dependency('melo', version : '>=1.0.0')
libmelo_proto_dep = dependency('melo_proto', version : '>=1.0.0')
gstreamer_sdp_dep = dependency('gstreamer-sdp-1.0', version : '>=1.8.3')
gstreamer_rtp_dep = dependency('gstreamer-rtp-1.0', version : '>=1.8.3')
libcrypto_dep = dependency('libcrypto', version : '>=1.1.1d')

# Generate module
shared_library(
	'melo_airplay',
	src,
	dependencies : [
		libmelo_dep,
		libmelo_proto_dep,
		gstreamer_sdp_dep,
		gstreamer_rtp_dep,
		libcrypto_dep
	],
	install : true,
	install_dir : libmelo_dep.get_pkgconfig_variable('moduledir'))
