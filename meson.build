project(
    'pqiv',
    'c',
    meson_version: '>= 1.3.0',
    version: '2.13.3',
    default_options: ['warning_level=3', 'c_std=gnu99'],
)

cc = meson.get_compiler('c')

if host_machine.system() == 'windows'
    gio_platform = dependency('gio-windows-2.0')
else
    gio_platform = dependency('gio-unix-2.0')
endif

gtk_dep = dependency('gtk+-3.0')

dependencies = [
    dependency('glib-2.0', version: '>=2.8'),
    gtk_dep,
    dependency('gdk-3.0'),
    dependency('cairo', version: '>=1.6'),
    dependency('pango', version: '>=1.10'),
    dependency('gio-2.0'),
    gio_platform,
    cc.find_library('m', required: false),
]

# Workaround to bug, see http://stackoverflow.com/questions/18647475, fixed in libX11-1.8
if 'x11' in gtk_dep.get_variable('target', default_value: '') or 'x11' in gtk_dep.get_variable(
    'targets',
    default_value: '',
)
    dependencies += dependency('x11')
endif

subdir('backends')

lib_headers = include_directories('lib')
lib_sources = [
    'lib/strnatcmp.c',
    'lib/bostree.c',
    'lib/filebuffer.c',
    'lib/config_parser.c',
    'lib/thumbnailcache.c',
]

#deal with configurable features
conf_data = configuration_data()
without_montage = not get_option('montage')
conf_data.set('CONFIGURED_WITHOUT_MONTAGE_MODE', without_montage)
without_actions = not get_option('actions')
conf_data.set('CONFIGURED_WITHOUT_ACTIONS', without_actions)
without_commands = not get_option('external-commands')
conf_data.set('CONFIGURED_WITHOUT_EXTERNAL_COMMANDS', without_commands)
without_jump = not get_option('jump-dialog')
conf_data.set('CONFIGURED_WITHOUT_JUMP_DIALOG', without_jump)
without_info = not get_option('info-text')
conf_data.set('CONFIGURED_WITHOUT_INFO_TEXT', without_info)

configure_file(
    input: 'config.h.in',
    output: 'config.h',
    configuration: conf_data,
)

configuration_inc = include_directories('.')

exe = executable(
    'pqiv',
    'pqiv.c',
    [lib_sources, backend_sources, generated_sources],
    include_directories: [lib_headers, configuration_inc],
    dependencies: [dependencies, backend_dependencies],
    install: true,
)

custom_target(
    'pqiv.desktop',
    input: 'generate_desktop.py',
    output: 'pqiv.desktop',
    command: [prog_python, '@INPUT@', '@OUTPUT@', mimefiles],
    install: true,
    install_dir: get_option('datadir') / 'applications',
)

install_man('pqiv.1')

test('basic', exe, args: ['--version'])
