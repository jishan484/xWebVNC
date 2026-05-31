subdir('libs')
cc = meson.get_compiler('c')

xwebvnc_srcs = assets_sources + [
    'main.c',
    'vncQueue.c',
    'webSocket.c',
    'vncInput.c',
    'clipboard.c',
    'audio.c',
    'auth.c'
]

# Include your own folder + DIX headers
xwebvnc_inc = include_directories(
    '.'
)

openssl_dep = dependency('openssl', required: true, static: true)
jpeg_dep = dependency('libjpeg', required: true, static: true)
alsa_dep = dependency('alsa', required: true, static: true)
pulse_dep = dependency('libpulse', required: true, static: true)
pam_dep = cc.find_library('pam', required: true)
pam_misc_dep = cc.find_library('pam_misc', required: true)


# Link libxserver_dix using link_with
libxserver_xwebvnc = static_library(
    'libxserver_xwebvnc',
    xwebvnc_srcs,
    
    include_directories: [xwebvnc_inc, inc],
    link_with: [libxserver_dix],
    dependencies: [common_dep, openssl_dep, jpeg_dep, alsa_dep, pulse_dep, pam_dep, pam_misc_dep],
    install: false
)
