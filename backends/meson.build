backends = []
backend_dependencies = []
backend_sources = []
mimefiles = []

gdkpixbuf = dependency(
    'gdk-pixbuf-2.0',
    version: '>= 2.2',
    required: get_option('gdkpixbuf'),
)
if gdkpixbuf.found()
    backends += ['gdkpixbuf']
    backend_dependencies += gdkpixbuf
    backend_sources += files('gdkpixbuf.c')
    mimefiles += files('gdkpixbuf.mime')
endif

poppler = dependency('poppler-glib', required: get_option('poppler'))
if poppler.found()
    backends += ['poppler']
    backend_dependencies += poppler
    backend_sources += files('poppler.c')
    mimefiles += files('poppler.mime')
endif

spectre = dependency('libspectre', required: get_option('spectre'))
if spectre.found()
    backends += ['spectre']
    backend_dependencies += spectre
    backend_sources += files('spectre.c')
    mimefiles += files('spectre.mime')
endif

wand = dependency('MagickWand', required: get_option('wand'))
if wand.found()
    backends += ['wand']
    backend_dependencies += wand
    backend_sources += files('wand.c')
    add_project_arguments(
        '-DWAND_VERSION=' + wand.version().split('.')[0],
        language: 'c',
    )
    mimefiles += files('wand.mime')
endif

libavformat = dependency('libavformat', required: get_option('libav'))
libavcodec = dependency('libavcodec', required: get_option('libav'))
libswscale = dependency('libswscale', required: get_option('libav'))
libavutil = dependency('libavutil', required: get_option('libav'))

if libavformat.found() and libavcodec.found() and libswscale.found() and libavutil.found()
    backends += ['libav']
    backend_dependencies += [libavformat, libavcodec, libswscale, libavutil]
    backend_sources += files('libav.c')
    mimefiles += files('libav.mime')
endif

archive = dependency('libarchive', required: get_option('archive'))
if archive.found()
    backends += ['archive']
    backend_dependencies += archive
    backend_sources += files('archive.c')
    mimefiles += files('archive.mime')
endif

archive_cbx_feat = get_option('archive_cbx')
cbx_gdkpixbuf = dependency(
    'gdk-pixbuf-2.0',
    version: '>= 2.2',
    required: archive_cbx_feat,
)
cbx_archive = dependency('libarchive', required: archive_cbx_feat)
if archive_cbx_feat.allowed() and cbx_gdkpixbuf.found() and cbx_archive.found()
    backends += ['archive_cbx']
    backend_dependencies += [cbx_gdkpixbuf, cbx_archive]
    backend_sources += files('archive_cbx.c')
    mimefiles += files('archive_cbx.mime')
endif

webp = dependency('libwebp', required: get_option('webp'))
if webp.found()
    backends += ['webp']
    backend_dependencies += webp
    backend_sources += files('webp.c')
    mimefiles += files('webp.mime')
endif

pymod = import('python')
prog_python = pymod.find_installation('python3')

generated_sources = custom_target(
    'initializer.c',
    input: 'generate_initializer.py',
    output: 'initializer.c',
    command: [prog_python, '@INPUT@', '@OUTPUT@', backends],
)