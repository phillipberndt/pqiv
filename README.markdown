PQIV README
===========

About pqiv
----------

pqiv is a powerful GTK 3 based command-line image viewer with a minimal UI. It
is highly customizable, can be fully controlled from scripts, and has support
for various file formats including PDF, Postscript, video files and archives.
It is optimized to be quick and responsive.

It comes with support for animations, slideshows, transparency, VIM-like key
bindings, automated loading of new images as they appear, external image
filters, image preloading, and much more.

pqiv started as a Python rewrite of qiv avoiding imlib, but evolved into a much
more powerful tool. Today, pqiv stands for powerful quick image viewer.

Features
--------

 * Recursive loading from directories
 * Can watch files and directories for changes
 * Sorts images in natural order
 * Has a status bar showing information on the current image
 * Comes with transparency support
 * Can move/zoom/rotate/flip images
 * Can pipe images through external filters
 * Loads the next image in the background for quick response times
 * Caches zoomed images for smoother movement
 * Supports fade image transition animations
 * Supports various image and video formats through a rich set of backends
 * Comes with an interactive montage mode (a.k.a. "image grid")
 * Customizable key-bindings with support for VIM-like key sequences, action
   cycling and binding multiple actions to a single key

Installation
------------

Usual stuff. `./configure && make && make install`. The configure script is
optional if you only want gdk-pixbuf support and will auto-determine which
backends to build if invoked without parameters.

You can also use precompiled and packaged versions of pqiv. Note that the
distribution packages are usually somewhat out of date:

 * Statically linked
   [nightly builds for Linux and Windows](https://intern.pberndt.com/pqiv_builds/)
   ![Build status](https://intern.pberndt.com/pqiv_builds/ci.php)
 * Dynamically linked
   [nightly builds for Debian, Ubuntu, SUSE and Fedora](https://build.opensuse.org/package/show/home:phillipberndt/pqiv)
   thanks to the OpenSUSE build service
 * [Arch AUR package](https://aur.archlinux.org/packages/pqiv/)
   ([Git version](https://aur.archlinux.org/packages/pqiv-git/))
 * [CRUX port](https://crux.nu/portdb/?a=search&q=pqiv)
 * [Debian package](https://packages.debian.org/en/sid/pqiv)
 * [FreeBSD port](https://www.freshports.org/graphics/pqiv/)
 * [Gentoo ebuild](https://packages.gentoo.org/packages/media-gfx/pqiv)
 * [macOS brew](http://braumeister.org/formula/pqiv)
 * [NixOS package](https://github.com/NixOS/nixpkgs/blob/HEAD/pkgs/applications/graphics/pqiv/default.nix)
 * [OpenBSD port](http://cvsweb.openbsd.org/cgi-bin/cvsweb/ports/graphics/pqiv/)

If you'd like to compile pqiv manually, you'll need

 * gtk+ 3.0 *or* gtk+ 2.6
 * gdk-pixbuf 2.2 (included in gtk+)
 * glib 2.32 (with gvfs for opening URLs)
 * cairo 1.6
 * gio 2.0
 * gdk 2.8

and optionally also

 * ffmpeg / libav (for video support)
 * libarchive (for images in archives and cbX comic book files)
 * libspectre (any version, for ps/eps support)
 * libwebp (for WebP support)
 * MagickWand (any version, for additional image formats like psd)
 * poppler (any version, for pdf support)

The backends are per default linked statically into the code, so all backend
related build-time dependencies are also run-time dependencies. If you need a
shared version of the backends, for example for separate packaging of the
binaries or to make the run-time dependencies optional, use the
`--backends-build=shared` configure option.

For macOS, have a look at the `pqiv.app` target of the Makefile, too.

Thanks
------

This program uses Martin Pool's natsort algorithm
<https://www.github.com/sourcefrog/natsort/>.


Contributors
------------

Contributors to pqiv 2.x are:

 * J. Paul Reed
 * Chen Jonh L
 * Anton Älgmyr

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

Examples
--------

Basic usage of pqiv is very straightforward, call

    pqiv <files or directories>

and then use space, backspace, `f` (for fullscreen), `q` (to quit), and `m` for
the montage overview to navigate through your images. To see all key bindings,
see the `DEFAULT KEY BINDINGS` section of the man-page, or run
`pqiv --show-bindings`.

For some advanced uses of pqiv, take a look at these resouces:

* [Play music while looking at specific images](https://github.com/phillipberndt/pqiv/issues/100#issuecomment-320651190)
* <details><summary>Bind keys to cycle through panels of a 2x2 comic</summary>

  Store this in your `.pqivrc`:
  ```
  # Bind c to act as if "#c1" was typed
  c { send_keys(#c1); }
  # If "#c1" is typed, shift the current image to it's north west corner, and
  # rebind "c" to act as if "#c2" was typed
  <numbersign>c1 { set_shift_align_corner(NW); bind_key(c { send_keys(#c2\); }); }
  # ..etc..
  <numbersign>c2 { set_shift_align_corner(NE); bind_key(c { send_keys(#c3\); }); }
  <numbersign>c3 { set_shift_align_corner(SW); bind_key(c { send_keys(#c4\); }); }
  # The last binding closes the cycle by rebinding "c" to act as if "#c1" was typed
  <numbersign>c4 { set_shift_align_corner(SE); bind_key(c { send_keys(#c1\); }); }
  ```

  </details>


Changelog
---------

pqiv 2.11 (dev)
 * Added negate (color inversion) mode (bound to `n`, `--negate`)
 * Rebound `a` (hardlink image) to `c-a` by default (See #124)
 * Improved key bindings documentation (See #127)
 * Add `--actions-from-stdin` and let it block until actions are completed
   (See #118/#119)
 * Fix zooming on tiling WMs (See #129)

pqiv 2.10.4
 * Fix output of `montage_mode_shift_y_rows()` in key bindings
 * Update the info text when the background pattern is cycled
 * Prevent potential crashes in poppler backend for rapid image movements
 * Fix processing of dangling symlinks in the file buffer
 * Removed possible deadlock in ImageMagick wand backend
 * Fix --command-9 shortcut
 * Makefile: Move -shared compiler flag to the end of the command line

pqiv 2.10
 * Enable cursor auto-hide by default
 * Enable mouse navigation in montage mode
 * Added `toggle_background_pattern()` (bound to `b`) and
   `--background-pattern`.
 * Added support for alternate pqivrc paths, changed recommended location to
   ./.config/pqivrc.
 * Sped up `--low-memory` mode (using native- instead of image-surfaces)
 * Fixed graphical issues with fading mode and quick image transition
 * Fixed support for platforms with `sizeof(time_t) != sizeof(int)`
 * Fixed a race condition in the file buffer map

<details>
<summary>Click to expand changelog for old pqiv versions</summary>

pqiv 2.9
 * Added a montage/image grid mode (bound to `m` by default)
 * Added a [WebP](https://developers.google.com/speed/webp/) backend
   (by @john0312)
 * Added the means to skip over "logical" directories, such as archive files
   (bound to `ctrl+space` and `ctrl+backspace` by default)
 * Improved responsivity by caching pre-scaled copies of images
 * Removed tearing/flickering in WMs without extended frame sync support
 * Fixed support for huge images (>32,767px) in the GdkPixbuf backend
 * Added option --info-box-colors to customize the colors used in the info box
 * It is now possible to view --help even if no display is available
 * Added --version
 * Added an auto scale mode that maintains window size
 * Bound `Control+t` to switch to "maintain scale level" by default
 * Bound `Alt+t` to switch to "maintain window size" by default
 * Added action `move_window()` to explicitly move pqiv's main window around

pqiv 2.8.5
 * Fixed an issue where the checkerboard pattern sometimes was visible at image
   borders
 * Fixed image rotation in low-memory mode
 * Fix a memory leak (leaking a few bytes each time an image is drawn)
 * Correctly handle string arguments from the configuration file
 * Fix building with old glib versions that do not expose their x11 dependency
   in pkgconfig
 * Fix support for duplicate files in sorted mode
 * Fix MagickWand exit handler code

pqiv 2.8
 * Added option --allow-empty-window: Show pqiv even if no images can be loaded
 * Explicitly allow to load all files from a directory multiple times
 * Allow to use --libdir option in configure to override .so-files location
 * Fix shared-backend-pqiv in environments that compile with --enable-new-dtags
 * Enable the libav backend by default
 * Add option --disable-backends to disable backends at runtime

pqiv 2.7.4
 * Fix GTK 2 compilation
 * Fix backends list in configure script
 * Fix race condition upon reloading animations
 * Fix Ctrl-R default binding (move `goto_earlier_file()` to Ctrl-P)

pqiv 2.7
 * Fixed window decoration toggling with --transparent-background
 * Work around bug #67, poppler bug #96884
 * Added new action `set_interpolation_quality` to change interpolation/filter
   mode
 * pqiv now by default uses `nearest` interpolation for small images
 * Added actions and key bindings to control animation playback speed
 * Added a general archive backend for reading images from archives
 * Added a new action `goto_earlier_file()` to return to the image that was
   shown before the current one
 * Added a new action `set_cursor_auto_hide()` to automatically hide the pointer
   when it is not moved for some time
 * Support an `actions` section in the configuration file for default actions
 * Create and install a desktop file for pqiv during install
 * Disable GTK's transparent scaling on HiDpi monitors
 * New option --wait-for-images-to-appear to wait for images to appear if none
   are found

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

</details>
