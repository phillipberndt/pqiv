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
 * Watch files and directories for changes
 * Natural order sorting of the images
 * A status bar showing information on the current image
 * Transparency and animation support
 * Moving, zooming, rotation, flipping
 * Slideshows
 * Highly customizable
 * Supports external image filters (e.g. `convert`)
 * Preloads the next image in the background
 * Fade between images


Installation
------------

Usual stuff. `./configure && make && make install`. Actually, the configure
script is optional for now.

You'll need
 * gtk+ 3.0 *or* gtk+ 2.8
 * gdk-pixbuf 2.2 (included in gtk+)
 * glib 2.8
 * cairo 1.6
 * gio 2.0
 * gdk 2.8

Thanks
------

This program uses Martin Pool's natsort algorithm
<http://sourcefrog.net/projects/natsort/>


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

Known bugs
----------

* **The window is centered in between two monitors in old multi-head setups**:
  This happens if you have the RandR extension enabled, but configured
  incorrectly. GTK is programmed to first try RandR and use Xinerama only as
  a fallback if that fails. (See `gdkscreen-x11.c`.) So if your video drivers
  for some reason detect your multiple monitors as one big screen you can not
  simply use fakexinerama to fix things. This might also apply to nvidia drivers
  older than version 304. I believe that I can not fix this without breaking
  functionality for other users or maintaining a blacklist, so you should
  deactivate RandR completely until your driver is able to provide correct
  information, or use a fake xrand (like
  [mine](https://github.com/phillipberndt/fakexrandr), for example)

Changelog
---------

pqiv 2.2 (work in progress)
 * Accept URLs as command line arguments

pqiv 2.1.1
 * Bugfix: Fixed handling of non-image command line arguments

pqiv 2.1
 * Support for watching directories for new files
 * Downstream Makefile fix: Included LDFLAGS (from Gentoo package, by Tim
   Harder), updated for clean builds on OpenBSD (by jca[at]wxcvbn[dot]org,
   reported by github user @clod89)
 * Also included CPPFLAGS, for completeness
 * Renamed '.qiv-select' directory to '.pqiv-select'
 * Added a certain level of autoconf compatibility to the configure script, for
   automated building
 * gtk 3.10 stock icon deprecation issue fixed
 * Reimplemented fading between images
 * Display the last image while the current image has not been loaded
 * Gave users the option to abort the loading of huge images
 * Respect --shuffle and --sort with --watch-directories, i.e. insert keeping
   order, not always at the end
 * New option --lazy-load to display the main window while still traversing
   paths, searching for images
 * New option --low-memory to disable memory hungry features
 * Detect nested symlinks without preventing users from loading the same image
   multiple times
 * Improved cross-compilation support with mingw64

pqiv 2.0
 * Complete rewrite from scratch
 * Based on GTK 3 and Cairo

pqiv ≤ 1.0
 * See the old GTK 2 release for information on that
   (in the **gtk2** branch on github)

pqiv ≤ 0.3
 * See the old python release for information on that
   (in the **python** branch on github)
