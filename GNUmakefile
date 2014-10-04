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
BACKENDS=gdkpixbuf

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
BACKENDS_INITIALIZER:=backends/initializer
define handle-backend
ifneq ($(origin LIBS_$(1)),undefined)
	ifneq ($(findstring $(1), $(BACKENDS)),)
		LIBS+=$(LIBS_$(1))
		OBJECTS+=backends/$(1).o
		LDLIBS+=$(LDLIBS_$(1))
		BACKENDS_INITIALIZER:=$(BACKENDS_INITIALIZER)-$(1)
	endif
endif
endef
$(foreach BACKEND_C, $(wildcard backends/*.c), $(eval $(call handle-backend,$(basename $(notdir $(BACKEND_C))))))
OBJECTS+=$(BACKENDS_INITIALIZER).o

# Add version information to builds from git
PQIV_VERSION_STRING=$(shell [ -d .git ] && (which git 2>&1 >/dev/null) && git describe --dirty --tags)
ifneq ($(PQIV_VERSION_STRING),)
	PQIV_VERSION_FLAG=-DPQIV_VERSION=\"$(PQIV_VERSION_STRING)\"
endif

# Less verbose output
ifndef VERBOSE
	SILENT_CC=@echo   " CC  " $@;
	SILENT_CCLD=@echo " CCLD" $@;
	SILENT_GEN=@echo  " GEN " $@;
endif

# Assemble final compiler flags
CFLAGS_REAL=-std=gnu99 $(PQIV_WARNING_FLAGS) $(PQIV_VERSION_FLAG) $(CFLAGS) $(shell $(PKG_CONFIG) --cflags "$(LIBS)")
LDLIBS_REAL=$(shell $(PKG_CONFIG) --libs "$(LIBS)") $(LDLIBS)
LDFLAGS_REAL=$(LDFLAGS)

all: pqiv$(EXECUTABLE_EXTENSION)
.PHONY: get_libs get_available_backends _build_variables clean distclean install uninstall all

pqiv$(EXECUTABLE_EXTENSION): $(OBJECTS)
	$(SILENT_CCLD) $(CROSS)$(CC) $(CPPFLAGS) -o $@ $+ $(LDLIBS_REAL) $(LDFLAGS_REAL)

%.o: %.c
	$(SILENT_CC) $(CROSS)$(CC) $(CPPFLAGS) -c -o $@ $(CFLAGS_REAL) $+

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

install: pqiv$(EXECUTABLE_EXTENSION)
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	install pqiv$(EXECUTABLE_EXTENSION) $(DESTDIR)$(PREFIX)/bin/pqiv$(EXECUTABLE_EXTENSION)
	-mkdir -p $(DESTDIR)$(MANDIR)/man1
	-install pqiv.1 $(DESTDIR)$(MANDIR)/man1/pqiv.1

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/pqiv$(EXECUTABLE_EXTENSION)
	rm -f $(DESTDIR)$(MANDIR)/man1/pqiv.1

clean:
	rm -f pqiv$(EXECUTABLE_EXTENSION) $(OBJECTS) $(BACKENDS_INITIALIZER).c

distclean: clean
	rm -f config.make backends/initializer*

get_libs:
	$(info LIBS: $(LIBS))
	@true

get_available_backends:
	@echo -n "BACKENDS: "; $(foreach BACKEND_C, $(wildcard backends/*.c), [ -n "$(LIBS_$(basename $(notdir $(BACKEND_C))))" ] && $(PKG_CONFIG) --exists "$(LIBS_$(basename $(notdir $(BACKEND_C))))" && echo -n "$(basename $(notdir $(BACKEND_C))) ";) echo
	@true
