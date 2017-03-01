headers = [
  'wing.h',
  'wingversionmacros.h',
  'wingnamedpipeclient.h',
  'wingnamedpipeconnection.h',
  'wingnamedpipelistener.h',
  'wingservice.h',
  'wingservicemanager.h',
  'wingsource.h',
  'wingutils.h',
]

sources = [
  'wingnamedpipeclient.c',
  'wingnamedpipeconnection.c',
  'wingnamedpipelistener.c',
  'wingservice.c',
  'wingservice-private.h',
  'wingservicemanager.c',
  'wingsource.c',
  'wingutils.c',
]

install_headers(headers, subdir: wing_includedir_real)

platform_deps = [ glib, gobject, gio, gio_windows ]

if cc.get_id() == 'msvc'
  wing_link_args = []
else
  wing_link_args = [ '-Wl,-Bsymbolic-functions' ]
endif

wing = shared_library('wing-@0@'.format(wing_api_version),
  include_directories: core_inc,
  sources: sources,
  version: libversion,
  soversion: soversion,
  install: true,
  dependencies: platform_deps,
  c_args: extra_args + common_flags + debug_flags + [
            '-DG_LOG_DOMAIN="Wing"' ],
  link_args: wing_link_args)

# Internal dependency, for tests and benchmarks
wing_inc = include_directories([ meson.current_source_dir(), meson.current_build_dir(), meson.build_root(), meson.source_root() ])
wing_dep = declare_dependency(link_with: wing,
                              include_directories: [ wing_inc ],
                              dependencies: platform_deps)
