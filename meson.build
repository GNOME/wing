project('wing', 'c',
        version: '0.1.0',
        default_options: [
          'buildtype=debugoptimized',
          'warning_level=2'
        ],
        license: 'LGPLv2.1+',
        meson_version: '>= 0.39.0')

wing_version = meson.project_version()
version_array = wing_version.split('.')
wing_major_version = version_array[0].to_int()
wing_minor_version = version_array[1].to_int()
wing_micro_version = version_array[2].to_int()

wing_api_version = '1.0'
wing_api_name = '@0@-@1@'.format(meson.project_name(), wing_api_version)

# The interface age is reset every time we add new API; this
# should only happen during development cycles, otherwise the
# interface age is the same as the micro version
if wing_minor_version.is_odd()
  wing_interface_age = 0
else
  wing_interface_age = wing_micro_version
endif

soversion = 0
# maintaining compatibility with the previous libtool versioning
# current = minor * 100 + micro - interface
# revision = interface
current = wing_minor_version * 100 + wing_micro_version - wing_interface_age
revision = wing_interface_age
libversion = '@0@.@1@.@2@'.format(soversion, current, revision)

wing_prefix = get_option('prefix')
wing_libdir = join_paths(wing_prefix, get_option('libdir'))
wing_includedir = join_paths(wing_prefix, get_option('includedir'))
wing_includedir_real = join_paths(wing_includedir,
                                  wing_api_name,
                                  meson.project_name())
wing_datadir = join_paths(wing_prefix, get_option('datadir'))

cc = meson.get_compiler('c')
host_system = host_machine.system()

conf = configuration_data()

# Compat variables for pkgconfig
conf.set('prefix', wing_prefix)
conf.set('exec_prefix', wing_prefix)
conf.set('libdir', wing_libdir)
conf.set('includedir', wing_includedir)

# Version macros
conf.set('VERSION', wing_version)

add_project_arguments('-DG_LOG_USE_STRUCTURED=1', language: 'c')

# Compiler flags
if cc.get_id() == 'msvc'
  # Compiler options taken from msvc_recommended_pragmas.h
  # in GLib, based on _Win32_Programming_ by Rector and Newcomer
  test_cflags = []
  add_project_arguments('-FImsvc_recommended_pragmas.h', language: 'c')
elif cc.get_id() == 'gcc' or cc.get_id() == 'clang'
  test_cflags = [
    '-Wpointer-arith',
    '-Wmissing-declarations',
    '-Wformat=2',
    '-Wstrict-prototypes',
    '-Wmissing-prototypes',
    '-Wnested-externs',
    '-Wold-style-definition',
    '-Wdeclaration-after-statement',
    '-Wunused',
    '-Wno-uninitialized',
    '-Wshadow',
    '-Wcast-align',
    '-Wmissing-noreturn',
    '-Wmissing-format-attribute',
    '-Wlogical-op',
    '-Wno-discarded-qualifiers',
    '-Werror=implicit',
    '-Werror=nonnull',
    '-Werror=init-self',
    '-Werror=main',
    '-Werror=missing-braces',
    '-Werror=sequence-point',
    '-Werror=return-type',
    '-Werror=trigraphs',
    '-Werror=array-bounds',
    '-Werror=write-strings',
    '-Werror=address',
    '-Werror=int-to-pointer-cast',
    '-Werror=pointer-to-int-cast',
    '-Werror=empty-body',
    '-fno-strict-aliasing',
    '-Wno-int-conversion',
  ]
else
  test_cflags = []
endif
common_flags = []
foreach cflag: test_cflags
  if cc.has_argument(cflag)
    common_flags += [ cflag ]
  endif
endforeach

extra_args= []
# Detect and set symbol visibility
if cc.get_id() == 'msvc'
  extra_args += ['-D_WING_EXTERN=__declspec (dllexport) extern']
else
  extra_args += ['-D_WING_EXTERN=__attribute__((visibility("default"))) __declspec(dllexport) extern', '-fvisibility=hidden']
endif

core_inc = include_directories('.')

# Required dependencies
glib_req = '2.44.0'
conf.set('GLIB_REQUIRED', glib_req)
glib = dependency('glib-2.0', version: '>=' + glib_req, fallback: ['glib', 'libglib_dep'])
gobject = dependency('gobject-2.0', version: '>=' + glib_req, fallback: ['glib', 'libgobject_dep'])
gmodule = dependency('gmodule-2.0', version: '>=' + glib_req, fallback: ['glib', 'libgmodule_dep'])
gio = dependency('gio-2.0', version: '>=' + glib_req, fallback: ['glib', 'libgio_dep'])
gio_windows = dependency('gio-windows-2.0', version: '>=' + glib_req, fallback: ['glib', 'libgiowin32_dep'])

subdir('wing')
subdir('tests')
