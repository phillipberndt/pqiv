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

# Load config.make (created by configure)
ifeq ($(wildcard config.make),config.make)
	include config.make
endif

# Package lines for pkg-config
LIBS_GENERAL=glib-2.0 >= 2.8 cairo >= 1.6 gio-2.0 gdk-pixbuf-2.0 >= 2.2 poppler-glib
LIBS_GTK3=gtk+-3.0 gdk-3.0
LIBS_GTK2=gtk+-2.0 >= 2.6 gdk-2.0 >= 2.8

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

# Add platform specific gio library for stdin loading
ifeq ($(EXECUTABLE_EXTENSION), .exe)
	LIBS+=gio-windows-2.0
else
	LIBS+=gio-unix-2.0
endif

# This might be required if you use mingw, and is required as of
# Aug 2014 for mxe, but IMHO shouldn't be required / is a bug in
# poppler (which does not specify this dependency):
#
# ifeq ($(EXECUTABLE_EXTENSION), .exe)
#    LDLIBS+=lcms2
# endif

CFLAGS_REAL:=-std=gnu99 $(PQIV_WARNING_FLAGS) $(CFLAGS) $(shell $(PKG_CONFIG) --cflags "$(LIBS)")
LDLIBS_REAL:=$(LDLIBS) $(shell $(PKG_CONFIG) --libs "$(LIBS)")
LDFLAGS_REAL:=$(LDFLAGS)



.PHONY: get_libs clean distclean install uninstall all

all: pqiv$(EXECUTABLE_EXTENSION)

pqiv$(EXECUTABLE_EXTENSION): pqiv.c lib/strnatcmp.o lib/bostree.o
	$(CROSS)$(CC) $(CPPFLAGS) -o $@ $(CFLAGS_REAL) $+ $(LDLIBS_REAL) $(LDFLAGS_REAL)

lib/strnatcmp.o: lib/strnatcmp.c
	$(CROSS)$(CC) $(CPPFLAGS) -c -o $@ $+ $(CFLAGS_REAL)

lib/bostree.o: lib/bostree.c
	$(CROSS)$(CC) $(CPPFLAGS) -DNDEBUG -c -o $@ $+ $(CFLAGS_REAL)

install: pqiv$(EXECUTABLE_EXTENSION)
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	install pqiv$(EXECUTABLE_EXTENSION) $(DESTDIR)$(PREFIX)/bin/pqiv$(EXECUTABLE_EXTENSION)
	-mkdir -p $(DESTDIR)$(MANDIR)/man1
	-install pqiv.1 $(DESTDIR)$(MANDIR)/man1/pqiv.1

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/pqiv$(EXECUTABLE_EXTENSION)
	rm -f $(DESTDIR)$(MANDIR)/man1/pqiv.1

clean:
	rm -f pqiv$(EXECUTABLE_EXTENSION) lib/strnatcmp.o lib/bostree.o

distclean: clean
	rm -f config.make

get_libs:
	$(info $(LIBS))
	@true
