PREFIX=/

pqiv:
	gcc $(CFLAGS) `pkg-config --libs --cflags gtk+-2.0 gthread-2.0 pango glib` -o qiv pqiv.c

debug:
	gcc $(CGLAGS) -Wall -ggdb -DDEBUG `pkg-config --libs --cflags gtk+-2.0 gthread-2.0 pango glib` -o qiv pqiv.c

install: pqiv
	install -Ds qiv $(PREFIX)/usr/bin/qiv
	install -D brightd.1 $(PREFIX)/usr/share/man/man1/qiv.1

uninstall:
	rm $(PREFIX)/usr/bin/qiv
	rm $(PREFIX)/usr/share/man/man1/qiv.1

clean:
	rm -f qiv

# Package generation
PACKAGE_VERSION=`awk '/RELEASE/ {print $$3}' pqiv.c | tr -d \" | head -n1`
package: pqiv
	mkdir pqiv-$(PACKAGE_VERSION)/
	cp pqiv.{1,c} ChangeLog gpl.txt Makefile README pqiv-$(PACKAGE_VERSION)/
	tar cjf pqiv-$(PACKAGE_VERSION).tar.bz2 pqiv-$(PACKAGE_VERSION)/
	rm -rf pqiv-$(PACKAGE_VERSION)
