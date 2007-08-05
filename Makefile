# Makefile for pqiv
#
# Dynamic settings (changed by configure script)
DESTDIR="/"
PREFIX="/usr"
OPTIONFLAGS= 

# Fixed settings
REQUIRED_PACKAGES=gtk+-2.0 gthread-2.0 pango glib

# pqiv
all: pqiv
pqiv: 
	$(CC) $(CFLAGS) `pkg-config --libs --cflags $(REQUIRED_PACKAGES)` -o qiv $(OPTIONFLAGS) pqiv.c
debug: 
	$(CC) $(CGLAGS) -Wall -ggdb -DDEBUG `pkg-config --libs --cflags $(REQUIRED_PACKAGES)` $(OPTIONFLAGS) -o qiv pqiv.c

# Cleanup
clean:
	rm -f qiv

# Installation and uninstallation
install: pqiv
	install -D qiv $(DESTDIR)$(PREFIX)/bin/qiv
	install -D qiv.1 $(DESTDIR)$(PREFIX)/share/man/man1/pqiv.1
	link $(DESTDIR)$(PREFIX)/share/man/man1/pqiv.1 $(DESTDIR)$(PREFIX)/share/man/man1/qiv.1
uninstall:
	rm $(DESTDIR)$(PREFIX)/bin/qiv
	rm $(DESTDIR)$(PREFIX)/share/man/man1/qiv.1
	rm $(DESTDIR)$(PREFIX)/share/man/man1/pqiv.1
mininstall: pqiv
	install -D qiv /usr/local/bin

# Package generation
PACKAGE_VERSION=`awk '/RELEASE/ {print $$3}' pqiv.c | tr -d \" | head -n1`$(SUFFIX)
package: 
	mkdir pqiv-$(PACKAGE_VERSION)/
	cp -r *.c qiv.1 gpl.txt configure Makefile README pqiv-$(PACKAGE_VERSION)/
	tar cjf pqiv-$(PACKAGE_VERSION).tbz pqiv-$(PACKAGE_VERSION)/
	rm -rf pqiv-$(PACKAGE_VERSION)
