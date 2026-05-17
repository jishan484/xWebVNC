subdir('libs')

xwebvnc_srcs = assets_sources + [
    'main.c',
    'vncQueue.c',
    'webSocket.c',
    'vncInput.c',
    'clipboard.c',
    'audio.c',
]

# Include your own folder + DIX headers
xwebvnc_inc = include_directories(
    '.'
)

openssl_dep = dependency('openssl', static: true)
jpeg_dep = dependency('libjpeg', static: true)
lz4_dep = dependency('liblz4', static: true)
alsa_dep = dependency('alsa', static: true)
pulse_dep = dependency('libpulse', static: true)
opus_dep = dependency('opus', static: false, required: true)
ogg_dep = dependency('ogg', static: true, required: true)


# Link libxserver_dix using link_with
libxserver_xwebvnc = static_library(
    'libxserver_xwebvnc',
    xwebvnc_srcs,
    
    include_directories: [xwebvnc_inc, inc],
    link_with: [libxserver_dix],
    dependencies: [common_dep, openssl_dep, jpeg_dep, lz4_dep, alsa_dep, pulse_dep, opus_dep, ogg_dep],
    install: false
)
