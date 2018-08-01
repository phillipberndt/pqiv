# pqiv Makefile
#

# Default flags, overridden by values in config.make
CFLAGS?=-O2 -g
CROSS=
DESTDIR=
GTK_VERSION=0
PQIV_WARNING_FLAGS=-Wall -Wextra -Wfloat-equal -Wpointer-arith -Wcast-align -Wstrict-overflow=1 -Wwrite-strings -Waggregate-return -Wunreachable-code -Wno-unused-parameter
LDLIBS=-lm
PREFIX=/usr
EPREFIX=$(PREFIX)
LIBDIR=$(PREFIX)/lib
MANDIR=$(PREFIX)/share/man
EXECUTABLE_EXTENSION=
PKG_CONFIG=$(CROSS)pkg-config
OBJECTS=pqiv.o lib/strnatcmp.o lib/bostree.o lib/filebuffer.o lib/config_parser.o lib/thumbnailcache.o
HEADERS=pqiv.h lib/bostree.h lib/filebuffer.h lib/strnatcmp.h
BACKENDS=gdkpixbuf
EXTRA_DEFS=
BACKENDS_BUILD=static
EXTRA_CFLAGS_SHARED_OBJECTS=-fPIC
EXTRA_CFLAGS_BINARY=
EXTRA_LDFLAGS_SHARED_OBJECTS=
EXTRA_LDFLAGS_BINARY=

# Always look for source code relative to the directory of this makefile
SOURCEDIR:=$(dir $(abspath $(lastword $(MAKEFILE_LIST))))
ifeq ($(SOURCEDIR),$(CURDIR))
	SOURCEDIR=
else
	HEADERS:=$(patsubst %, $(SOURCEDIR)%, $(HEADERS))
endif

# Load config.make (created by configure)
CONFIG_MAKE_NAME=config.make
ifeq ($(wildcard $(CONFIG_MAKE_NAME)),$(CONFIG_MAKE_NAME))
	include $(CONFIG_MAKE_NAME)
	HEADERS+=$(CONFIG_MAKE_NAME)
endif

# First things first: Require at least one backend
ifeq ($(BACKENDS),)
$(error Building pqiv without any backends is unsupported.)
endif

# pkg-config lines for the main program
LIBS_GENERAL=glib-2.0 >= 2.8 cairo >= 1.6 gio-2.0
LIBS_GTK3=gtk+-3.0 gdk-3.0
LIBS_GTK2=gtk+-2.0 >= 2.6 gdk-2.0 >= 2.8

# pkg-config libraries for the backends
LIBS_gdkpixbuf=gdk-pixbuf-2.0 >= 2.2
LIBS_poppler=poppler-glib
LIBS_spectre=libspectre
LIBS_wand=MagickWand
LIBS_libav=libavformat libavcodec libswscale libavutil
LIBS_archive_cbx=libarchive gdk-pixbuf-2.0 >= 2.2
LIBS_archive=libarchive
LIBS_webp=libwebp

# This might be required if you use mingw, and is required as of
# Aug 2014 for mxe, but IMHO shouldn't be required / is a bug in
# poppler (which does not specify this dependency). If it isn't
# or throws an error for you, please report this as a bug:
#
ifeq ($(EXECUTABLE_EXTENSION),.exe)
   LDLIBS_poppler+=-llcms2 -lstdc++
endif

# If no GTK_VERSION is set, try to auto-determine, with GTK 3 preferred
ifeq ($(GTK_VERSION), 0)
	ifeq ($(shell $(PKG_CONFIG) --errors-to-stdout --print-errors "$(LIBS_GTK3)"), )
		override GTK_VERSION=3
	else
		LIBS=$(LIBS_GTK2)
		override GTK_VERSION=2
	endif
endif
ifeq ($(GTK_VERSION), 2)
	LIBS=$(LIBS_GTK2)
endif
ifeq ($(GTK_VERSION), 3)
	LIBS=$(LIBS_GTK3)
endif
LIBS+=$(LIBS_GENERAL)

# Add platform specific libraries
# GIo for stdin loading,
ifeq ($(EXECUTABLE_EXTENSION), .exe)
	LIBS+=gio-windows-2.0
else
	LIBS+=gio-unix-2.0
endif

# We need X11 to workaround a bug, see http://stackoverflow.com/questions/18647475
ifeq ($(filter x11, $(shell pkg-config --errors-to-stdout --variable=target gtk+-$(GTK_VERSION).0; pkg-config --errors-to-stdout --variable=targets gtk+-$(GTK_VERSION).0)), x11)
	LIBS+=x11 xext
endif

# Add backend-specific libraries and objects
SHARED_OBJECTS=
SHARED_BACKENDS=
HELPER_OBJECTS=
BACKENDS_INITIALIZER:=backends/initializer
define handle-backend
ifneq ($(origin LIBS_$(1)),undefined)
	ifneq ($(findstring $(1), $(BACKENDS)),)
		ifeq ($(BACKENDS_BUILD), shared)
			ifeq ($(shell $(PKG_CONFIG) --errors-to-stdout --print-errors "$(LIBS_$(1))" 2>&1), )
				SHARED_OBJECTS+=backends/pqiv-backend-$(1).so
				HELPER_OBJECTS+=backends/$(1).o
				BACKENDS_BUILD_CFLAGS_$(1):=$(shell $(PKG_CONFIG) --errors-to-stdout --print-errors --cflags "$(LIBS_$(1))" 2>&1)
				BACKENDS_BUILD_LDLIBS_$(1):=$(shell $(PKG_CONFIG) --errors-to-stdout --print-errors --libs "$(LIBS_$(1))" 2>&1)
				SHARED_BACKENDS+="$(1)",
			endif
		else
			LIBS+=$(LIBS_$(1))
			OBJECTS+=backends/$(1).o
			LDLIBS+=$(LDLIBS_$(1))
			BACKENDS_INITIALIZER:=$(BACKENDS_INITIALIZER)-$(1)
		endif
	endif
endif
endef
$(foreach BACKEND_C, $(wildcard $(SOURCEDIR)backends/*.c), $(eval $(call handle-backend,$(basename $(notdir $(BACKEND_C))))))
PIXBUF_FILTER="gdkpixbuf",
ifeq ($(BACKENDS_BUILD), shared)
	OBJECTS+=backends/shared-initializer.o
	BACKENDS_BUILD_CFLAGS_shared-initializer=-DSHARED_BACKENDS='$(filter $(PIXBUF_FILTER), $(SHARED_BACKENDS)) $(filter-out $(PIXBUF_FILTER), $(SHARED_BACKENDS))' -DSEARCH_PATHS='"backends", "../$(subst $(PREFIX),,$(LIBDIR))/pqiv", "$(LIBDIR)/pqiv",'
	LIBS+=gmodule-2.0
else
	OBJECTS+=$(BACKENDS_INITIALIZER).o
endif

# MagickWand changed their directory structure with version 7, pass the version
# to the build
ifneq ($(findstring wand, $(BACKENDS)),)
backends/wand.o: CFLAGS_REAL+=-DWAND_VERSION=$(shell $(PKG_CONFIG) --modversion MagickWand | awk 'BEGIN { FS="." } { print $$1 }')
endif

# Add version information to builds from git
PQIV_VERSION_STRING=$(shell [ -d $(SOURCEDIR).git ] && (which git 2>&1 >/dev/null) && git -C "$(SOURCEDIR)" describe --dirty --tags 2>/dev/null)
ifneq ($(PQIV_VERSION_STRING),)
	PQIV_VERSION_FLAG=-DPQIV_VERSION=\"$(PQIV_VERSION_STRING)\"
endif
ifdef DEBUG
	DEBUG_CFLAGS=-DDEBUG
else
	DEBUG_CFLAGS=-DNDEBUG
endif

# Less verbose output
ifndef VERBOSE
	SILENT_CC=@echo   " CC  " $@;
	SILENT_CCLD=@echo " CCLD" $@;
	SILENT_GEN=@echo  " GEN " $@;
endif

# Assemble final compiler flags
CFLAGS_REAL=-std=gnu99 $(PQIV_WARNING_FLAGS) $(PQIV_VERSION_FLAG) $(CFLAGS) $(DEBUG_CFLAGS) $(EXTRA_DEFS) $(shell $(PKG_CONFIG) --cflags "$(LIBS)")
LDLIBS_REAL=$(shell $(PKG_CONFIG) --libs "$(LIBS)") $(LDLIBS)
LDFLAGS_REAL=$(LDFLAGS)

all: pqiv$(EXECUTABLE_EXTENSION) pqiv.desktop $(SHARED_OBJECTS)
.PHONY: get_libs get_available_backends _build_variables clean distclean install uninstall all
.SECONDARY:

pqiv$(EXECUTABLE_EXTENSION): $(OBJECTS)
	$(SILENT_CCLD) $(CROSS)$(CC) $(CPPFLAGS) $(EXTRA_CFLAGS_BINARY) -o $@ $+ $(LDLIBS_REAL) $(LDFLAGS_REAL) $(EXTRA_LDFLAGS_BINARY)

ifeq ($(BACKENDS_BUILD), shared)
backends/%.o: CFLAGS_REAL+=$(BACKENDS_BUILD_CFLAGS_$(notdir $*)) $(EXTRA_CFLAGS_SHARED_OBJECTS)

$(SHARED_OBJECTS): backends/pqiv-backend-%.so: backends/%.o
	@[ -d backends ] || mkdir -p backends || true
	$(SILENT_CCLD) $(CROSS)$(CC) $(CPPFLAGS) $(EXTRA_CFLAGS_SHARED_OBJECTS) -o $@ $+ $(LDLIBS_REAL) $(LDFLAGS_REAL) $(BACKENDS_BUILD_LDLIBS_$*) $(EXTRA_LDFLAGS_SHARED_OBJECTS) -shared
endif

$(filter-out $(BACKENDS_INITIALIZER).o, $(OBJECTS)) $(HELPER_OBJECTS): %.o: $(SOURCEDIR)%.c $(HEADERS)
	@[ -d $(dir $@) ] || mkdir -p $(dir $@) || true
	$(SILENT_CC) $(CROSS)$(CC) $(CPPFLAGS) -c -o $@ $(CFLAGS_REAL) $<

$(BACKENDS_INITIALIZER).o: $(BACKENDS_INITIALIZER).c $(HEADERS)
	@[ -d $(dir $@) ] || mkdir -p $(dir $@) || true
	$(SILENT_CC) $(CROSS)$(CC) $(CPPFLAGS) -I"$(SOURCEDIR)/lib" -c -o $@ $(CFLAGS_REAL) $<

$(BACKENDS_INITIALIZER).c:
	@[ -d $(dir $(BACKENDS_INITIALIZER)) ] || mkdir -p $(dir $(BACKENDS_INITIALIZER)) || true
	@$(foreach BACKEND, $(sort $(BACKENDS)), [ -e $(SOURCEDIR)backends/$(BACKEND).c ] || { echo; echo "Backend $(BACKEND) not found!" >&2; exit 1; };)
	$(SILENT_GEN) ( \
		echo '/* Auto-Generated file by Make. */'; \
		echo '#include "../pqiv.h"'; \
		echo "file_type_handler_t file_type_handlers[$(words $(BACKENDS)) + 1];"; \
		$(foreach BACKEND, $(sort $(BACKENDS)), echo "void file_type_$(BACKEND)_initializer(file_type_handler_t *info);";) \
		echo "void initialize_file_type_handlers(const gchar * const * disabled_backends) {"; \
		echo "	int i = 0;"; \
		$(foreach BACKEND, $(filter gdkpixbuf, $(BACKENDS)), echo "	if(!strv_contains(disabled_backends, \"$(BACKEND)\")) file_type_$(BACKEND)_initializer(&file_type_handlers[i++]);";) \
		$(foreach BACKEND, $(sort $(filter-out gdkpixbuf, $(BACKENDS))), echo "	if(!strv_contains(disabled_backends, \"$(BACKEND)\")) file_type_$(BACKEND)_initializer(&file_type_handlers[i++]);";) \
		echo "}" \
	) > $@

pqiv.desktop: $(HEADERS)
	$(SILENT_GEN) ( \
		echo "[Desktop Entry]"; \
		echo "Version=1.0"; \
		echo "Type=Application"; \
		echo "Comment=Powerful quick image viewer"; \
		echo "Name=pqiv"; \
		echo "NoDisplay=true"; \
		echo "Icon=emblem-photos"; \
		echo "TryExec=$(PREFIX)/bin/pqiv"; \
		echo "Exec=$(PREFIX)/bin/pqiv %F"; \
		echo "MimeType=$(shell cat $(foreach BACKEND, $(sort $(BACKENDS)), $(SOURCEDIR)backends/$(BACKEND).mime) /dev/null | sort | uniq | awk 'ORS=";"')"; \
		echo "Categories=Graphics;"; \
		echo "Keywords=Viewer;" \
	) > $@

install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	install pqiv$(EXECUTABLE_EXTENSION) $(DESTDIR)$(PREFIX)/bin/pqiv$(EXECUTABLE_EXTENSION)
	-mkdir -p $(DESTDIR)$(MANDIR)/man1
	-install -m 644 $(SOURCEDIR)pqiv.1 $(DESTDIR)$(MANDIR)/man1/pqiv.1
	-mkdir -p $(DESTDIR)$(PREFIX)/share/applications
	-install -m 644 pqiv.desktop $(DESTDIR)$(PREFIX)/share/applications/pqiv.desktop
ifeq ($(BACKENDS_BUILD), shared)
	mkdir -p $(DESTDIR)$(LIBDIR)/pqiv
	install $(SHARED_OBJECTS) $(DESTDIR)$(LIBDIR)/pqiv/
endif

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/pqiv$(EXECUTABLE_EXTENSION)
	rm -f $(DESTDIR)$(MANDIR)/man1/pqiv.1
	rm -f $(DESTDIR)$(PREFIX)/share/applications/pqiv.desktop
ifeq ($(BACKENDS_BUILD), shared)
	rm -f $(foreach SO_FILE, $(SHARED_OBJECTS), $(DESTDIR)$(LIBDIR)/pqiv/$(notdir $(SO_FILE)))
	rmdir $(DESTDIR)$(LIBDIR)/pqiv
endif

# Rudimentary MacOS bundling
# Only really useful for opening pqiv using "open pqiv.app --args ..." from the
# command line right now, but that already has the benefit that the application
# window will be visible right away
pqiv.app: pqiv.app.tmp
	rm -f ../$@
	cd pqiv.app.tmp && zip -9r ../$@ .

pqiv.app.tmp: pqiv.app.tmp/Contents/MacOS/pqiv pqiv.app.tmp/Contents/Info.plist pqiv.app.tmp/Contents/PkgInfo

pqiv.app.tmp/Contents/MacOS/pqiv:
	-mkdir -p pqiv.app.tmp/Contents/MacOS
	install pqiv$(EXECUTABLE_EXTENSION) $@

pqiv.app.tmp/Contents/PkgInfo:
	-mkdir -p pqiv.app.tmp/Contents
	$(SILENT_GEN) ( \
		echo -n "APPL????"; \
	) > $@

pqiv.app.tmp/Contents/Info.plist: $(HEADERS)
	-mkdir -p pqiv.app.tmp/Contents
	$(SILENT_GEN) ( \
		echo '<?xml version="1.0" encoding="UTF-8"?><!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">'; \
		echo '<plist version="1.0"><dict><key>CFBundleName</key><string>pqiv</string><key>CFBundleDisplayName</key><string>pqiv</string>'; \
		echo '<key>CFBundleIdentifier</key><string>com.pberndt.pqiv</string><key>CFBundleVersion</key><string>$(PQIV_VERSION_STRING)</string>'; \
		echo '<key>CFBundlePackageType</key><string>APPL</string><key>CFBundleExecutable</key><string>pqiv</string><key>LSMinimumSystemVersion</key>'; \
		echo '<string>10.4</string><key>CFBundleDocumentTypes</key><array><dict><key>CFBundleTypeMIMETypes</key><array>'; \
		cat $(foreach BACKEND, $(sort $(BACKENDS)), $(SOURCEDIR)backends/$(BACKEND).mime) /dev/null | sort | uniq | awk '{print "<string>" $$0 "</string>"}'; \
		echo '</array></dict></array></dict></plist>'; \
	) > $@

clean:
	rm -f pqiv$(EXECUTABLE_EXTENSION) *.o backends/*.o backends/*.so lib/*.o backends/initializer-*.c pqiv.desktop

distclean: clean
	rm -f config.make

get_libs:
	$(info LIBS: $(LIBS))
	@true

get_available_backends:
	@OUT=; $(foreach BACKEND_C, $(wildcard $(SOURCEDIR)backends/*.c), \
		[ "$(DISABLE_AUTOMATED_BUILD_$(basename $(notdir $(BACKEND_C))))" != "yes" ] && \
		[ -n "$(LIBS_$(basename $(notdir $(BACKEND_C))))" ] && \
		$(PKG_CONFIG) --exists "$(LIBS_$(basename $(notdir $(BACKEND_C))))" \
		&& OUT="$$OUT $(basename $(notdir $(BACKEND_C))) ";) echo BACKENDS: $$OUT
	@true
