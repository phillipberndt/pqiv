PQIV README
===========

About pqiv
----------

Originally, PQIV was written as a drop-in replacement for QIV
(http://spiegl.de/qiv/), as QIV was unmaintained and used imlib, a deprecated
library, which was to be removed from Gentoo Linux. The first release was not
more than a Python script, hence the name. After some month I realized that
nobody would do a better version, so I did it myself.  Back then, PQIV became a
(modulo some small extras) full featured clone of QIV written in C using GTK 2
and GLIB 2.

When Debian decided to do the same step regarding imlib, a new developer stepped
in for QIV's klografx.net team and updated QIV to use GDK 2 and imlib 2. As of
May 2009, both programs are usable again and their features are likely to
diverge.

In the meantime, I have had some new ideas and learned (from my daily use)
about which features were really useful and which were merely overhead. In 2013
I decided to rewrite pqiv from scratch which these in mind, this time using the 
third version of GTK and its backend, cairo. The code was tested on numerous
platforms, and is also backwards compatible with GTK 2.


Features
--------

 * Command line image viewer
 * Directory traversing to view whole directories
 * Natural order sorting of the images
 * A Statusbar showing information on the current image
 * Transparency support
 * Moving, zooming, rotation, flipping
 * Slideshow
 * Highly customizable
 * Supports external image filters (e.g. `convert`)
 * Supports animated images
 * Preloads the next image in the background


Installation
------------

Usual stuff. `./configure && make && make install`. Actually, the configure script
is optional for now.

You'll need
 * gtk+-3.0 *or* gtk+-2.0
 * glib-2.0
 * cairo
 * gio-2.0

Thanks
------

This program uses Martin Pool's natsort algorithm <http://sourcefrog.net/projects/natsort/>


Contributors
------------

 Contributers to pqiv ≤ 1.0 were:

 * Alexander Sulfrian
 * Alexandros Diamantidis (Reverse-movement-direction code, fixed code typos)
 * Brandon
 * David Lindquist
 * Hanspeter Gysin
 * John Keeping
 * Nir Tzachar
 * Rene Saarsoo
 * Tinoucas
 * Yaakov (Cygwin Ports)


Changelog
---------

pqiv 2.1-dev
 * Support for watching directories for new files
 * Downstream Makefile fix: Included LDFLAGS (from Gentoo package, by Tim Harder)
 * Also included CPPFLAGS, for completeness
 * Renamed '.qiv-select' directory to '.pqiv-select'

pqiv 2.0
 * Complete rewrite from scratch
 * Based on GTK 3 and Cairo

pqiv ≤ 1.0
 * See the old GTK 2 release for information on that
   (in the **gtk2** branch on github)

pqiv ≤ 0.3
 * See the old python release for information on that
   (in the **python** branch on github)
