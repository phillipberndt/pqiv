CFLAGS=-O2 -g
CROSS=
DESTDIR=
GTK_VERSION=0
PQIV_WARNING_FLAGS=-Wall -Wextra -Wfloat-equal -Wpointer-arith -Wcast-align -Wstrict-overflow=5 -Wwrite-strings -Waggregate-return -Wunreachable-code -Wno-unused-parameter
LD_LIBS=-lm
PREFIX=/usr
MANDIR=$(PREFIX)/share/man
EXECUTABLE_EXTENSION=
PKG_CONFIG=$(CROSS)pkg-config

ifeq ($(wildcard config.make),config.make)
	include config.make
endif

# GTK3/2 chooser
LIBS_GTK3=gtk+-3.0 gdk-3.0 glib-2.0 >= 2.8 cairo >= 1.6 gio-2.0 gdk-pixbuf-2.0 >= 2.2 poppler-glib
LIBS_GTK2=gtk+-2.0 >= 2.6 gdk-2.0 >= 2.8 glib-2.0 >= 2.8 cairo >= 1.6 gio-2.0 gdk-pixbuf-2.0 >= 2.2 poppler-glib

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

# Add platform specific gio for stdin loading
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
#    LIBS+=lcms2
# endif

all: pqiv$(EXECUTABLE_EXTENSION)

pqiv$(EXECUTABLE_EXTENSION): pqiv.c lib/strnatcmp.o lib/bostree.o
	$(CROSS)$(CC) $(CPPFLAGS) $(PQIV_WARNING_FLAGS) -std=gnu99 -o $@ `$(PKG_CONFIG) --cflags "$(LIBS)"` $+ `$(PKG_CONFIG) --libs "$(LIBS)"` $(CFLAGS) $(LD_LIBS) $(LDFLAGS)

lib/strnatcmp.o: lib/strnatcmp.c
	$(CROSS)$(CC) $(CPPFLAGS) -c -o $@ $+ $(CFLAGS)

lib/bostree.o: lib/bostree.c
	$(CROSS)$(CC) $(CPPFLAGS) -DNDEBUG -c -o $@ $+ $(CFLAGS)

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
