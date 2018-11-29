headers = [
  'wing.h',
  'wingcredentials.h',
  'wingeventwindow.h',
  'winginputstream.h',
  'wingversionmacros.h',
  'wingnamedpipeclient.h',
  'wingnamedpipeconnection.h',
  'wingnamedpipelistener.h',
  'wingoutputstream.h',
  'wingservice.h',
  'wingservicemanager.h',
  'wingsource.h',
  'wingutils.h',
]

sources = [
  'wingcredentials.c',
  'wingeventwindow.c',
  'wing-init.c',
  'winginputstream.c',
  'wingnamedpipeclient.c',
  'wingnamedpipeconnection.c',
  'wingnamedpipelistener.c',
  'wingoutputstream.c',
  'wingservice.c',
  'wingservice-private.h',
  'wingservicemanager.c',
  'wingsource.c',
  'wingutils.c',
]

install_headers(headers, subdir: wing_includedir_real)

platform_deps = [ glib, gobject, gmodule, gio, gio_windows ]

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
                      c_args: extra_args + common_flags + [ '-DG_LOG_DOMAIN="Wing"' ],
                      link_args: wing_link_args)

# Generate pc file
pkgg = import('pkgconfig')

pkgg.generate(libraries: [ wing ],
              version: libversion,
              name: 'wing',
              filebase: wing_api_name,
              subdirs: wing_api_name,
              description: 'Wing is a library which provides GLib-like API to some Windows API.',
              requires: 'glib-2.0 >= ' + glib_req + ', gobject-2.0 >= ' + glib_req + ', gio-2.0 >= ' + glib_req)

# Internal dependency, for tests and benchmarks
wing_inc = include_directories([ '.', '..' ])
wing_dep = declare_dependency(link_with: wing,
                              include_directories: [ wing_inc ],
                              dependencies: platform_deps)
