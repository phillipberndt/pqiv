# Makefile for pqiv
#
# Dynamic settings (changed by configure script)
DESTDIR="/"
PREFIX="/usr"
OPTIONFLAGS= lib/strnatcmp.c      
BINARY_NAME="qiv"

# Fixed settings
REQUIRED_PACKAGES=gtk+-2.0 gthread-2.0 pango glib-2.0
SHELL=bash
PACKAGE_VERSION:=$(shell awk '/RELEASE/ {print $$3}' pqiv.c | tr -d \" | head -n1)$(SUFFIX)
OPTIONFLAGS+=-DBINARY_NAME='$(BINARY_NAME)'

# pqiv
all: pqiv manpage
pqiv: 
	$(CC) $(LDFLAGS) $(CFLAGS) `pkg-config --cflags $(REQUIRED_PACKAGES)` $(OPTIONFLAGS) pqiv.c `pkg-config --libs $(REQUIRED_PACKAGES)` -o pqiv
debug:
	$(CC) $(LDFLAGS) $(CFLAGS) `pkg-config --cflags $(REQUIRED_PACKAGES)` $(OPTIONFLAGS) -Wall -ggdb pqiv.c `pkg-config --libs $(REQUIRED_PACKAGES)` -o pqiv
vdebug: 
	$(CC) $(LDFLAGS) $(CGLAGS) `pkg-config --cflags $(REQUIRED_PACKAGES)` -Wall -ggdb -DDEBUG `pkg-config --libs $(REQUIRED_PACKAGES)` $(OPTIONFLAGS) -o pqiv pqiv.c

# The manpage stuff is kind of hackish, but it seems that I can't rely on the C
# preprocessor (drac from gentoo reported "missing terminating ' character"
# error messages).
# The shell script strips code between '.\" unless FOO' and '.\" end' if FOO is
# defined. It's compatible with sh in POSIX mode.
manpage:
	@( IN=0; while read -r LINE; do \
		if [ "$${IN}" == "1" ]; then \
			if echo "$${LINE}" | egrep -q '.\" end'; then IN=0; continue; fi; \
			[ "$${MAT}" != "1" ] && echo "$${LINE}"; \
			continue; \
		fi; \
		REQ=$$(echo "$${LINE}" | egrep '.\" unless ' | awk '{print $$3}'); \
		[ "$${REQ}" == "" ] && { echo "$${LINE}"; continue; }; \
		MAT=$$(echo "$(OPTIONFLAGS)" | grep -q -- "-D$${REQ}" && echo 1;); \
		if [ "$${MAT}" == "1" ]; then ACT=Stripping; else ACT=Ignoring; fi; \
		echo "$${ACT} $${REQ}" >&2; \
		IN=1; \
	done < pqiv.1.template ) | sed -e 's/$$PACKAGE_VERSION/$(PACKAGE_VERSION)/' -e 's/$$BINARY_NAME/$(BINARY_NAME)/' > pqiv.1

# Cleanup
clean:
	rm -f pqiv pqiv.1

# Installation and uninstallation
install:
	install -D pqiv $(DESTDIR)$(PREFIX)/bin/$(BINARY_NAME)
	install -D pqiv.1 $(DESTDIR)$(PREFIX)/share/man/man1/$(BINARY_NAME).1
uninstall:
	rm $(DESTDIR)$(PREFIX)/bin/$(BINARY_NAME)
	rm $(DESTDIR)$(PREFIX)/share/man/man1/$(BINARY_NAME).1
mininstall:
	install -Ds pqiv /usr/local/bin/$(BINARY_NAME)

# Package generation
package: 
	mkdir pqiv-$(PACKAGE_VERSION)/
	cp -r lib *.c pqiv.1.template gpl.txt configure Makefile README pqiv-$(PACKAGE_VERSION)/
	tar cjf pqiv-$(PACKAGE_VERSION).tbz pqiv-$(PACKAGE_VERSION)/
	rm -rf pqiv-$(PACKAGE_VERSION)
deb:
	mkdir deb-pqiv-$(PACKAGE_VERSION)/
	install -D pqiv deb-pqiv-$(PACKAGE_VERSION)/$(PREFIX)/bin/$(BINARY_NAME)
	install -D pqiv.1 deb-pqiv-$(PACKAGE_VERSION)/$(PREFIX)/share/man/man1/$(BINARY_NAME).1
	mkdir deb-pqiv-$(PACKAGE_VERSION)/DEBIAN
	(echo -e "Package: pqiv\nVersion: $(PACKAGE_VERSION)\nSection: graphics\nPriority: optional"; \
	 echo -ne "Architecture: "; uname -m | sed -e 's/686/386/'; echo -e "Essential: no"; \
	 echo -e "Depends: libgtk2.0-0 (>= 2.12.0), libglib2.0-0 (>= 2.12.0)"; \
	 echo -ne "Installed-Size: "; du -s 'deb-pqiv-$(PACKAGE_VERSION)' | awk '{print $$1}'; \
	 echo -e "Maintainer: Phillip Berndt <mail at pberndt.com>\nConflicts: qiv"; \
	 echo -e "Description: A minimalistic graphics viewer.\n Modern rewrite of qiv using gtk+-2.0"; \
	 ) > deb-pqiv-$(PACKAGE_VERSION)/DEBIAN/control
	dpkg -b deb-pqiv-$(PACKAGE_VERSION) pqiv-$(PACKAGE_VERSION).deb
	rm -rf deb-pqiv-$(PACKAGE_VERSION)/
