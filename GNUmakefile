# pqiv Makefile
#

# Default flags, overridden by values in config.make
CFLAGS=-O2 -g
CROSS=
DESTDIR=
GTK_VERSION=0
PQIV_WARNING_FLAGS=-Wall -Wextra -Wfloat-equal -Wpointer-arith -Wcast-align -Wstrict-overflow=5 -Wwrite-strings -Waggregate-return -Wunreachable-code -Wno-unused-parameter
LDLIBS=-lm
PREFIX=/usr
MANDIR=$(PREFIX)/share/man
EXECUTABLE_EXTENSION=
PKG_CONFIG=$(CROSS)pkg-config
OBJECTS=pqiv.o lib/strnatcmp.o lib/bostree.o lib/filebuffer.o
HEADERS=pqiv.h lib/bostree.h lib/filebuffer.h lib/strnatcmp.h
BACKENDS=gdkpixbuf
BACKENDS_BUILD=static

# Load config.make (created by configure)
ifeq ($(wildcard config.make),config.make)
	include config.make
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
LIBS_libav=libavformat libavcodec libswscale

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
		LIBS=$(LIBS_GTK3)
	else
		LIBS=$(LIBS_GTK2)
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
# X11 to workaround a bug, see http://stackoverflow.com/questions/18647475
ifeq ($(EXECUTABLE_EXTENSION), .exe)
	LIBS+=gio-windows-2.0
else
	LIBS+=gio-unix-2.0 x11
endif

# Add backend-specific libraries and objects
SHARED_OBJECTS=
SHARED_BACKENDS=
BACKENDS_INITIALIZER:=backends/initializer
define handle-backend
ifneq ($(origin LIBS_$(1)),undefined)
	ifneq ($(findstring $(1), $(BACKENDS)),)
		ifeq ($(BACKENDS_BUILD), shared)
			ifeq ($(shell $(PKG_CONFIG) --errors-to-stdout --print-errors "$(LIBS_$(1))" 2>&1), )
				SHARED_OBJECTS+=backends/pqiv-backend-$(1).so
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
$(foreach BACKEND_C, $(wildcard backends/*.c), $(eval $(call handle-backend,$(basename $(notdir $(BACKEND_C))))))
ifeq ($(BACKENDS_BUILD), shared)
	CFLAGS_SHARED=-fPIC
	OBJECTS+=backends/shared-initializer.o
	BACKENDS_BUILD_CFLAGS_shared-initializer=-DSHARED_BACKENDS='$(SHARED_BACKENDS)'
	LIBS+=gmodule-2.0
	LDFLAGS_RPATH=-Wl,-rpath,'$$ORIGIN/backends',-rpath,'$$ORIGIN/../lib/pqiv',-rpath,'$(PREFIX)/lib/pqiv'
else
	CFLAGS_SHARED=
	OBJECTS+=$(BACKENDS_INITIALIZER).o
endif

# Add version information to builds from git
PQIV_VERSION_STRING=$(shell [ -d .git ] && (which git 2>&1 >/dev/null) && git describe --dirty --tags)
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
CFLAGS_REAL=-std=gnu99 $(PQIV_WARNING_FLAGS) $(PQIV_VERSION_FLAG) $(CFLAGS) $(CFLAGS_SHARED) $(DEBUG_CFLAGS) $(shell $(PKG_CONFIG) --cflags "$(LIBS)")
LDLIBS_REAL=$(shell $(PKG_CONFIG) --libs "$(LIBS)") $(LDLIBS)
LDFLAGS_REAL=$(LDFLAGS) $(LDFLAGS_RPATH)

all: pqiv$(EXECUTABLE_EXTENSION) $(SHARED_OBJECTS)
.PHONY: get_libs get_available_backends _build_variables clean distclean install uninstall all
.SECONDARY:

pqiv$(EXECUTABLE_EXTENSION): $(OBJECTS)
	$(SILENT_CCLD) $(CROSS)$(CC) $(CPPFLAGS) -o $@ $+ $(LDLIBS_REAL) $(LDFLAGS_REAL)

ifeq ($(BACKENDS_BUILD), shared)
backends/%.o: backends/%.c
	$(SILENT_CC) $(CROSS)$(CC) $(CPPFLAGS) -c -o $@ $(CFLAGS_REAL) $(BACKENDS_BUILD_CFLAGS_$*) $<

backends/pqiv-backend-%.so: backends/%.o
	$(SILENT_CCLD) $(CROSS)$(CC) -shared $(CPPFLAGS) -o $@ $+ $(LDLIBS_REAL) $(LDFLAGS_REAL) $(BACKENDS_BUILD_LDLIBS_$*)
endif

%.o: %.c $(HEADERS)
	$(SILENT_CC) $(CROSS)$(CC) $(CPPFLAGS) -c -o $@ $(CFLAGS_REAL) $<

$(BACKENDS_INITIALIZER).c:
	@$(foreach BACKEND, $(sort $(BACKENDS)), [ -e backends/$(BACKEND).c ] || { echo; echo "Backend $(BACKEND) not found!" >&2; exit 1; };)
	$(SILENT_GEN) ( \
		echo '/* Auto-Generated file by Make. */'; \
		echo '#include "../pqiv.h"'; \
		echo "file_type_handler_t file_type_handlers[$(words $(BACKENDS)) + 1];"; \
		$(foreach BACKEND, $(sort $(BACKENDS)), echo "void file_type_$(BACKEND)_initializer(file_type_handler_t *info);";) \
		echo "void initialize_file_type_handlers() {"; \
		echo "	int i = 0;"; \
		$(foreach BACKEND, $(sort $(BACKENDS)), echo "	file_type_$(BACKEND)_initializer(&file_type_handlers[i++]);";) \
		echo "}" \
	) > $@

install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	install pqiv$(EXECUTABLE_EXTENSION) $(DESTDIR)$(PREFIX)/bin/pqiv$(EXECUTABLE_EXTENSION)
	-mkdir -p $(DESTDIR)$(MANDIR)/man1
	-install pqiv.1 $(DESTDIR)$(MANDIR)/man1/pqiv.1
ifeq ($(BACKENDS_BUILD), shared)
	mkdir -p $(DESTDIR)$(PREFIX)/lib/pqiv
	install $(SHARED_OBJECTS) $(DESTDIR)$(PREFIX)/lib/pqiv/
endif

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/pqiv$(EXECUTABLE_EXTENSION)
	rm -f $(DESTDIR)$(MANDIR)/man1/pqiv.1
ifeq ($(BACKENDS_BUILD), shared)
	rm -f $(foreach SO_FILE, $(SHARED_OBJECTS), $(DESTDIR)$(PREFIX)/lib/pqiv/$(notdir $(SO_FILE)))
	rmdir $(DESTDIR)$(PREFIX)/lib/pqiv
endif

clean:
	rm -f pqiv$(EXECUTABLE_EXTENSION) *.o backends/*.o backends/*.so lib/*.o backends/initializer-*.c

distclean: clean
	rm -f config.make

get_libs:
	$(info LIBS: $(LIBS))
	@true

get_available_backends:
	@echo -n "BACKENDS: "; $(foreach BACKEND_C, $(wildcard backends/*.c), \
		(! grep -qE "configure hint:.*disable-auto-configure" $(BACKEND_C)) && \
		[ -n "$(LIBS_$(basename $(notdir $(BACKEND_C))))" ] && \
		$(PKG_CONFIG) --exists "$(LIBS_$(basename $(notdir $(BACKEND_C))))" \
		&& echo -n "$(basename $(notdir $(BACKEND_C))) ";) echo
	@true
