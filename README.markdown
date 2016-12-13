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
 * Highly customizable and scriptable (see `--actions-from-stdin` and `--bind-key`)
 * Supports external image filters (e.g. `convert`)
 * Preloads the next image in the background
 * Fade between images
 * Optional PDF/eps/ps support (useful e.g. for scientific plots)
 * Optional video format support (e.g. for webm animations)


Installation
------------

Usual stuff. `./configure && make && make install`. The configure script is
optional if you only want gdk-pixbuf support and will autodetermine which
backends to build if invoked without parameters.

You'll need

 * gtk+ 3.0 *or* gtk+ 2.6
 * gdk-pixbuf 2.2 (included in gtk+)
 * glib 2.6
 * cairo 1.6
 * gio 2.0
 * gdk 2.8

and optionally also

 * libspectre (any version, for ps/eps support)
 * poppler (any version, for pdf support)
 * MagickWand (any version, for additional image formats like psd)
 * libarchive (for cbX comic book files)
 * ffmpeg / libav (for video support, only included if explicitly compiled in)

The backends are per default linked statically into the code, so all backend
related build-time dependencies are also run-time dependencies. If you need
a shared version of the backends, for example for separate packaging of
the binaries, use the `--backends-build=shared` option. This is only supported
on Linux platforms currently.

There are experimental, nightly [static builds available for
download](http://page.mi.fu-berlin.de/pberndt/pqiv-builds/) for Windows and
Linux: ![Build status](http://page.mi.fu-berlin.de/pberndt/pqiv-builds/ci.php)

There are experimental, [nightly packages
available](https://build.opensuse.org/package/show/home:phillipberndt/pqiv)
for Debian, Ubuntu, SUSE and Fedora thorugh the OpenSUSE build Service.

Thanks
------

This program uses Martin Pool's natsort algorithm
<https://www.github.com/sourcefrog/natsort/>.


Contributors
------------

Contributors to pqiv 2.x are:

 * J. Paul Reed

Contributors to pqiv ≤ 1.0 were:

 * Alexander Sulfrian
 * Alexandros Diamantidis
 * Brandon
 * David Lindquist
 * Hanspeter Gysin
 * John Keeping
 * Nir Tzachar
 * Rene Saarsoo
 * Tinoucas
 * Yaakov

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

* **Loading postscript files failes with `Error #12288; Unknown output format`**:
  This issue happens if your poppler and spectre libraries are linked against
  different versions of libcms. libcms and libcms2 will both be used, but
  interfere with each other. Compile using `--backends-build=shared` to
  circumvent this issue.

Changelog
---------

pqiv 2.7 (wip)
 * Fixed window decoration toggling with --transparent-background
 * Work around bug #67, poppler bug #96884
 * Added new action `set_interpolation_quality` to change interpolation/filter
   mode
 * pqiv now by default uses `nearest` interpolation for small images

pqiv 2.6
 * Added --enforce-window-aspect-ratio
 * Do not enforce the aspect ratio of the window to match the image's by default

pqiv 2.5.1
 * Prevent a crash in --lazy-load mode if many images fail to load

pqiv 2.5
 * Added a configure option to build the backends as shared libraries
 * Added a configure option to remove unneeded/unwanted features
 * Added --watch-files to make the file-changed-on-disk action configurable
 * Added support for cbz/cbr/cbt/cb7 comic books
 * Key bindings are now configurable
 * Deprecated --keyboard-alias and --reverse-cursor-keys in favor of
   --bind-key.
 * Added --actions-from-stdin to make pqiv scriptable
 * Added --recreate-window to create a new window instead of resizing the
   old one, as a workaround for buggy window managers
 * Fixed crash on reloading of images created by pipe-command output

pqiv 2.4.1
 * Fix --end-of-files-action=quit if only one file is present
 * Fixed libav backend's pkg-config dependency list (by @onodera-punpun)
 * Enable image format support in the libav backend

pqiv 2.4
 * Added --sort-key=mtime to sort by modification time instead of file name
 * Delay the "Image is still loading" message for half a second to avoid
   flickering status messages
 * Remove the "Image is still loading" message if --hide-info-box is set
 * Added [libav](https://www.ffmpeg.org/) backend for video support
 * Added --end-of-files-action=action to allow users to control what happens
   once all images have been viewed
 * Fix various minor memory allocation issues / possible race conditions

pqiv 2.3.5
 * Fix parameters in pqivrc that are handled by a callback
 * Fix reference counting if an image fails to load
 * Properly reload multi-page files if they change on disk while being viewed
 * Properly handle if a user closes pqiv while the image loader is still active

pqiv 2.3
 * Refactored an abstraction layer around the image backend
 * Added optional support for PDF-files through
   [poppler](http://poppler.freedesktop.org/)
 * Added optional support for PS-files through
   [libspectre](http://www.freedesktop.org/wiki/Software/libspectre/)
 * Added optional support for more image formats through
   [ImageMagick's MagickWand](http://www.imagemagick.org/script/magick-wand.php)
 * Support for gtk+ 3.14
 * configure/Makefile updated to support (Free-)BSD
 * Added ctrl + space/backspace hotkey for jumping to the next/previous directory
 * Improved pqiv's reaction if a file is removed
 * gtk 3.16 deprecates `gdk_cursor_new`, replaced by a different function
 * Shuffle mode is now toggleable at run-time (using Ctrl-R)

pqiv 2.2
 * Accept URLs as command line arguments
 * Revived -r for reading additional files from stdin (by J.P. Reed)
 * Display the help message if invoked without parameters (by J.P. Reed)
 * Accept floating point slideshow intervals on the command line
 * Update the info box with the current numbers if (new) images are (un)loaded
 * Added --max-depth=n to limit how deep directories are searched
 * Added --browse to load, in addition to images from the command line, also
   all other images from the containing directories
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
