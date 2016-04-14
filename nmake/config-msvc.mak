# NMake Makefile portion for enabling features for Windows builds

# GLib is required for all utility programs and tests
WING_GLIB_LIBS = gio-2.0.lib gobject-2.0.lib glib-2.0.lib

# Please do not change anything beneath this line unless maintaining the NMake Makefiles
# Bare minimum features and sources built into HarfBuzz on Windows
WING_DEFINES =

WING_CFLAGS = \
	/FImsvc_recommended_pragmas.h \
	/I$(PREFIX)\include\glib-2.0 \
	/I$(PREFIX)\include\gio-win32-2.0 \
	/I$(PREFIX)\lib\glib-2.0\include

WING_SOURCES = \
	$(WING_BASE_sources)

WING_HEADERS =	\
	$(WING_BASE_headers)

# Minimal set of (system) libraries needed for the Wing DLL
WING_DEP_LIBS = user32.lib $(WING_GLIB_LIBS)

# We build the Wing DLL/LIB at least
WING_LIBS = $(CFG)\$(PLAT)\wing.lib

# Note: All the utility and test programs require GLib support to be present!
WING_TESTS =
WING_TESTS_DEP_LIBS = $(WING_GLIB_LIBS)

# Use libtool-style DLL names, if desired
!if "$(LIBTOOL_DLL_NAME)" == "1"
WING_DLL_FILENAME = $(CFG)\$(PLAT)\libwing-0
!else
WING_DLL_FILENAME = $(CFG)\$(PLAT)\wing-vs$(VSVER)
!endif

EXTRA_TARGETS =

WING_TESTS = \
	$(CFG)\$(PLAT)\named-pipes.exe

WING_LIB_CFLAGS = $(WING_CFLAGS) /D_WING_EXTERN="__declspec (dllexport) extern"
