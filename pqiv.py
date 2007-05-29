#!/bin/env python
# vim:fileencoding=utf-8:ft=python
#
# pqiv
# A (even simpler) qiv replacement for GTK2
# Copyright (c) Phillip Berndt, 2007
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
#
VERSION=0.2

import sys
try:
	from optparse import OptionParser
	import os, gtk, gobject, pango, shutil, gc
except Exception, e:
	print >> sys.stderr, "Failed to import needed libraries: %s" % e

def generateFileList(args):
	"""
		Generate a list of all image files by parsing
		a list of files and directories
	"""
	extensions = ("jpg", "gif", "png", "bmp", "xpm", "ico")

	for arg in args:
		if not os.access(arg, os.R_OK):
			print >> sys.stderr, "File not found: %s" % arg 
			continue
		if os.path.isdir(arg):
			for (root, dirs, files) in os.walk(arg):
				for file in files:
					if os.path.splitext(file)[1][1:].lower() in extensions:
						yield os.path.join(root, file)
		else:
			yield arg

def generateTransparentBackground(sizex, sizey):
	"""
		Generate this chess-board-like background
		image
	"""
	sizex += sizex % 16
	sizey += sizey % 16
	singleTileData = (
		"GdkP"
		"\0\0\0\263"
		"\2\1\0\2"
		"\0\0\0@"
		"\0\0\0\20"
		"\0\0\0\20"
		"\210jjj\377\210\233\233\233\377\210jjj\377\210\233\233\233\377\210jj"
		"j\377\210\233\233\233\377\210jjj\377\210\233\233\233\377\210jjj\377\210"
		"\233\233\233\377\210jjj\377\210\233\233\233\377\210jjj\377\210\233\233"
		"\233\377\210jjj\377\220\233\233\233\377\210jjj\377\210\233\233\233\377"
		"\210jjj\377\210\233\233\233\377\210jjj\377\210\233\233\233\377\210jj"
		"j\377\210\233\233\233\377\210jjj\377\210\233\233\233\377\210jjj\377\210"
		"\233\233\233\377\210jjj\377\210\233\233\233\377\210jjj\377"
	)
	singleTile = gtk.gdk.pixbuf_new_from_inline(len(singleTileData), singleTileData, False)
	backgroundPixbuf = gtk.gdk.Pixbuf(gtk.gdk.COLORSPACE_RGB, True, 8, sizex, sizey)
	for x in xrange(0, sizex - 8, 16):
		for y in xrange(0, sizey - 8, 16):
			singleTile.copy_area(0, 0, 16, 16, backgroundPixbuf, x, y)
	return backgroundPixbuf

class ImageViewer(gtk.Window):
	"""
		An image viewer class. This is the main application
	"""
	position = 0
	fullscreenToggle = False
	infoVisible = True
	autoscaleToggle = True
	slideshowRef = None
	imgPos = [0, 0]
	slideInterval = 1500
	scaleFactor = 1
	title = ""
	scaleCache = [ "", 0, None ]
	_progResize = False

	def __init__(self, fileList, options=None):
		"""
			fileList must be a list of all images to be
			displayed
		"""
		gtk.Window.__init__(self)
		
		if "infovisible" in dir(options):
			self.infoVisible = options.infovisible
		if "fullscreen" in dir(options) and options.fullscreen == True:
			self.fullscreenToggle = True
			self.fullscreen()
		if "slide" in dir(options) and options.slide != None:
			self.slideInterval = options.slide * 1000
			self.slideshowRef = gobject.timeout_add(self.slideInterval, self.next)

		self.set_title("pqiv")
		self.modify_bg(gtk.STATE_NORMAL, gtk.gdk.Color(0))
		self._addWidgets()
		self._connectSignals()

		self.fileList = fileList
		if "thumbnail" in dir(options) and options.thumbnail == True:
			self.bigThumbnail()
			self.setTitle()
		self.loadImage()

	def fullscreen(self):
		super(ImageViewer, self).fullscreen()
		self._progResize = True

	### EVENT HANDLERS ##########################################

	def _addWidgets(self):
		self.fixed = gtk.Fixed()
		self.add(self.fixed)
		self.fixed.show()

		# Transparency background image
		self.imgTrans = gtk.Image()
		self.imgTrans.set_size_request(self.get_size()[0], self.get_size()[1])
		self.imgTrans.show()
		self.imgTrans.bgOn = False
		self.fixed.add(self.imgTrans)

		# Image
		self.imgDisplay = gtk.Image()
		self.fixed.add(self.imgDisplay)
		self.imgDisplay.show()

		# Infobox
		self.infoLabelBox = gtk.EventBox()
		self.infoLabelBox.modify_bg(gtk.STATE_NORMAL, gtk.gdk.color_parse("#EEEE55"))
		self.infoLabel = gtk.Label()
		self.infoLabel.set_padding(2, 2)
		self.infoLabel.set_single_line_mode(True)
		self.infoLabel.modify_font(pango.FontDescription("sansserif 9"))
		self.infoLabelBox.add(self.infoLabel)
		self.infoLabel.show()
		if self.infoVisible:
			self.infoLabelBox.show()
		self.fixed.put(self.infoLabelBox, 10, 10)

	def _connectSignals(self):
		self.connect("key-press-event", self._onKeyPress)
		self.connect("hide", self.exit)
		self.connect("size_allocate", self._onSizeRequest)


	def _onSizeRequest(self, widget, event):
		width, height = event.width, event.height
		isfullscreen = self.fullscreenToggle
		if self._progResize == True and isfullscreen:
			# Position image
			self._progResize = False
			self.autoResize()

	def _onKeyPress(self, widget, event):
		"""
			Handle the KeyPress event for the window
		"""
		#print event.hardware_keycode
		if event.hardware_keycode == 102 and self.fullscreenToggle:
			# Binding: Right: Move right (Fs)
			self.imgPos[0] += 15
			self.display()
		elif event.hardware_keycode == 100 and self.fullscreenToggle:
			# Binding: Left: Move left (Fs)
			if self.imgPos[0] > 0:
				self.imgPos[0] -= 15
			self.display()
		elif event.hardware_keycode == 98 and self.fullscreenToggle:
			# Binding: Top: Move up (Fs)
			if self.imgPos[1] > 0:
				self.imgPos[1] -= 15
			self.display()
		elif event.hardware_keycode == 104 and self.fullscreenToggle:
			# Binding: Down: Move down (Fs)
			self.imgPos[1] += 15
			self.display()
		elif event.hardware_keycode == 36:
			# Binding: Return: Next image
			self.next()
		elif event.hardware_keycode == 99:
			# Binding: PgUp: +10 images
			self.position = (self.position + 9) % len(self.fileList)
			self.next()
		elif event.hardware_keycode == 105:
			# Binding: PgDwn: -10 images
			self.position = (self.position - 9) % len(self.fileList)
			self.previous()
		elif event.hardware_keycode == 22:
			# Binding: Backspace: Previous image
			self.previous()
		elif event.hardware_keycode == 9:
			# Binding: Escape: Quit
			self.exit()
		elif event.keyval < 256:
			if chr(event.keyval) == " ":
				# Binding: Space: Next image
				self.next()
			elif chr(event.keyval) == "q":
				# Binding: q: quit
				self.exit()
			elif chr(event.keyval) == "f":
				# Binding: f: Toggle fullscreen
				self.fullscreenToggle = not self.fullscreenToggle
				if self.fullscreenToggle:
					self.showCursor(False)
					self.fullscreen()
				else:
					self.showCursor(True)
					self.unfullscreen()
				self.autoScale()
				def reload():
					self.loadImage()
				gobject.timeout_add(5, reload)
			elif chr(event.keyval) == "+":
				# Binding: +: Zoom in
				self.scaleFactor += 0.1
				self.display()
				self.autoResize()
				gobject.timeout_add(10, self.display)
				self.setTitle("Zoom +")
			elif chr(event.keyval) == "-":
				# Binding: -: Zoom out
				if self.scaleFactor > 0.1:
					self.scaleFactor -= 0.1
				self.display()
				self.autoResize()
				gobject.timeout_add(10, self.display)
				self.setTitle("Zoom -")
			elif chr(event.keyval) == "r":
				# Binding: r: Reload
				self.loadImage()
			elif chr(event.keyval) == "m":
				# Binding: m: Toggle autoscale
				self.autoscaleToggle = not self.autoscaleToggle
				self.loadImage()
				gobject.timeout_add(5, self.display)
				if self.autoscaleToggle:
					self.setTitle("Autoscale")
				else:
					self.setTitle("Autoscale disabled")
			elif chr(event.keyval) == "l":
				# Binding: l: Rotate left
				self.rotate(gtk.gdk.PIXBUF_ROTATE_COUNTERCLOCKWISE)
				self.display()
				self.setTitle("Rotate left")
			elif chr(event.keyval) == "k":
				# Binding: k: Rotate right
				self.rotate(gtk.gdk.PIXBUF_ROTATE_CLOCKWISE)
				self.display()
				self.setTitle("rotate right")
			elif chr(event.keyval) == "s":
				# Binding: s: Toggle slideshow
				if self.slideshowRef:
					gobject.source_remove(self.slideshowRef)
					self.slideshowRef = None
					self.setTitle("Slideshow disabled")
				else:
					self.slideshowRef = gobject.timeout_add(self.slideInterval, self.next)
					self.setTitle("Slideshow")
			elif chr(event.keyval) == "h":
				# Binding: h: Flip horizontally
				self.flip(True)
				self.display()
				self.setTitle("Flip horizontal")
			elif chr(event.keyval) == "v":
				# Binding: v: Flip vertically
				self.flip(False)
				self.display()
				self.setTitle("Flip vertical")
			elif chr(event.keyval) == "a":
				# Binding: a: Copy image to .qiv-select
				if not os.path.isdir(".qiv-select"):
					os.mkdir(".qiv-select")
				shutil.copyfile(self.fileList[self.position], 
					os.path.join(".qiv-select/", os.path.basename(self.fileList[self.position])))
				self.setTitle("Copy saved")
			elif chr(event.keyval) == "i":
				# Binding: i: show infoscreen
				self.infoVisible = not self.infoVisible
				if self.infoVisible:
					self.infoLabelBox.show()
				else:
					self.infoLabelBox.hide()

	## FORM ACTIONS ##############################################
	def exit(self, *arg):
		self.hide()
		gtk.main_quit()

	def setTitle(self, infoString=None):
		displayFilename = self.fileName.decode(sys.getfilesystemencoding(), "replace")
		title = "pqiv: %s (%dx%d) %d%% [%d/%d]" % (displayFilename, self.currentPixbuf.get_width(), \
			self.currentPixbuf.get_height(), int(self.scaleFactor * 100), self.position + 1, len(self.fileList))
		if infoString != None:
			title += " (%s)" % infoString
		self.set_title(title)
		self.infoLabel.set_text(title)

	def showCursor(self, show):
		if not show:
			data = """/* XPM */
			static char * xpm[] = {
			"1 1 1 1",
			"       c None",
			" "};"""
			pixmap = gtk.gdk.pixmap_create_from_data(None, data, 1, 1, 1, gtk.gdk.Color(), gtk.gdk.Color())
			invisible = gtk.gdk.Cursor(pixmap, pixmap, gtk.gdk.Color(), gtk.gdk.Color(), 0, 0)
			self.window.set_cursor(invisible)
		else:
			self.window.set_cursor(None)
		

	## APPLICATION LOGIC #########################################
	def bigThumbnail(self):
		"""
			Generate a big thumbnail from all images
		"""
		fileCount   = len(self.fileList)
		thumbSize   = (200, 200)
		imgHoriz    = int(self.get_screen().get_width() / (thumbSize[1] + 20))
		imgSize     = (self.get_screen().get_width(), (thumbSize[1] + 20) * (int(fileCount / imgHoriz) + 2))

		pixbuf = gtk.gdk.Pixbuf(gtk.gdk.COLORSPACE_RGB, True, 8, imgSize[0], imgSize[1])
		for file in range(len(self.fileList)):
			try:
				timg = gtk.gdk.pixbuf_new_from_file(self.fileList[file])
			except:
				print >> sys.stderr, "Failed to load image %s" % self.fileList[file]
				continue
			timgSize = [timg.get_width(), timg.get_height()]
			if timgSize[0] > thumbSize[0] or timgSize[1] > thumbSize[1]:
				scaleFactor = 1.0 * thumbSize[0] / timgSize[0]
				if timgSize[1] * scaleFactor > thumbSize[1]:
					scaleFactor = 1.0 * thumbSize[1] / timgSize[1]
				self.scaleFactor = scaleFactor
				timgSize[0] = int(timgSize[0] * scaleFactor)
				timgSize[1] = int(timgSize[1] * scaleFactor)
				timg = timg.scale_simple(timgSize[0], timgSize[1], gtk.gdk.INTERP_BILINEAR)
			pos = ( (file % imgHoriz) * (thumbSize[0] + 20) + 10 + (thumbSize[0] - timgSize[0]) / 2,
				int(file / imgHoriz) * (thumbSize[1] + 20) + 10)

			print " Rendering thumbnails; %d of %d\r" % (file, len(self.fileList)),
			sys.stdout.flush()

			timg.copy_area(0, 0, timgSize[0], timgSize[1], pixbuf, pos[0], pos[1])
			del timg
			gc.collect()
		print
		self.currentPixbuf = pixbuf
		self.fileList = [ "#" ]
		self.fileName = "#"
		self.autoScale()
		self.display()

	def next(self):
		"""
			Goto next image
		"""
		if len(self.fileList) == 1:
			return
		oldPos = self.position
		while True:
			if self.position == len(self.fileList) - 1:
				self.position = -1
			self.position += 1
			if oldPos == self.position:
				break
			if self.loadImage():
				break

		# Return true for slideshow timeout
		return True

	def previous(self):
		"""
			Goto previous image
		"""
		if len(self.fileList) == 1:
			return
		oldPos = self.position
		while True:
			if self.position == 0:
				self.position = len(self.fileList)
			self.position -= 1
			if oldPos == self.position:
				break
			if self.loadImage():
				break

	def loadImage(self, imageNumber = None):
		"""
			Load a specific image
		"""
		if self.fileList[0] == "#" and len(self.fileList) == 1:
			return True
		if imageNumber:
			self.position = imageNumber
		fileName = self.fileList[self.position]

		try:
			self.currentPixbuf = gtk.gdk.pixbuf_new_from_file(fileName)
		except:
			print >> sys.stderr, "Failed to load image %s" % fileName
			return False

		if self.currentPixbuf.get_has_alpha() and self.imgTrans.bgOn == False:
			# Generate transparent image
			background = generateTransparentBackground(self.get_screen().get_width(), self.get_screen().get_height())
			self.imgTrans.set_from_pixbuf(background)
			self.imgTrans.bgOn = True
			del background

		self.fileName = fileName

		self.imgPos        = [0, 0]
		self.scaleFactor   = 1
		self.autoScale()
		self.autoResize()
		self.display()
		self.setTitle()
		gobject.timeout_add(10, self.display)
		return True

	def autoScale(self):
		"""
			Fit the loaded image into the window and/or
			resize the window
		"""
		if self.autoscaleToggle:
			if not self.fullscreenToggle:
				maxSize = (self.get_screen().get_width() - 100, self.get_screen().get_height() - 100)
			else:
				maxSize = (self.get_screen().get_width(), self.get_screen().get_height())
			imgSize = [self.currentPixbuf.get_width(), self.currentPixbuf.get_height()]

			if imgSize[0] > maxSize[0] or imgSize[1] > maxSize[1]:
				scaleFactor = 1.0 * maxSize[0] / imgSize[0]
				if imgSize[1] * scaleFactor > maxSize[1]:
					scaleFactor = 1.0 * maxSize[1] / imgSize[1]
				self.scaleFactor = scaleFactor
				imgSize[0] = int(imgSize[0] * scaleFactor)
				imgSize[1] = int(imgSize[1] * scaleFactor)

	def autoResize(self):
		"""
			Resize the window to the optimal size for the
			loaded image. Center the image in fullscreen.
		"""
		imgSize = [self.currentPixbuf.get_width() * self.scaleFactor, self.currentPixbuf.get_height() * self.scaleFactor]
		imgSize = map(lambda x: max(int(x), 1), imgSize)
		if not self.fullscreenToggle:
			self.resize(imgSize[0], imgSize[1])
			position = ( int(0.5 * (self.get_screen().get_width() - imgSize[0])),
				int(0.5 * (self.get_screen().get_height() - imgSize[1])))
			self.move(position[0], position[1])
			self.fixed.move(self.imgDisplay, 0, 0)
			if self.imgTrans.bgOn:
				self.imgTrans.set_size_request(imgSize[0], imgSize[1])
		else:
			self.fixed.move(self.imgDisplay, max(0, int((self.get_size()[0] - imgSize[0]) / 2)),
				max(0, int((self.get_size()[1] - imgSize[1]) / 2)))
			if self.imgTrans.bgOn:
				self.imgTrans.set_size_request(int(self.get_size()[0]), int(self.get_size()[1]))

	def getScaled(self, scaleFactor=None):
		"""
			Scale the loaded image by factor scaleFactor
		"""
		if scaleFactor != None:
			self.scaleFactor = scaleFactor

		scaledSize = (self.currentPixbuf.get_width() * self.scaleFactor, self.currentPixbuf.get_height() * self.scaleFactor)
		scaledSize = map(lambda x: int(x), scaledSize)

		if min(scaledSize) > 0:
			return self.currentPixbuf.scale_simple(scaledSize[0],
				scaledSize[1], gtk.gdk.INTERP_BILINEAR)
		else:
			return gtk.gdk.Pixbuf(gtk.gdk.COLORSPACE_RGB, True, 8, 1, 1)
			

	def getVisible(self):
		"""
			Get the currently visible part of an image
		"""
		if self.scaleCache[0] == self.fileName and self.scaleCache[1] == self.scaleFactor:
			scaled = self.scaleCache[2]
		else:
			scaled = self.getScaled()
			self.scaleCache = [ self.fileName, self.scaleFactor, scaled ]

		if not self.fullscreenToggle:
			return scaled
		size = [ min(x) for x in zip(self.get_size(), (scaled.get_width() - self.imgPos[0],
			scaled.get_height() - self.imgPos[1])) ]
		pixbuf = gtk.gdk.Pixbuf(gtk.gdk.COLORSPACE_RGB, True, 8, size[0], size[1])
		scaled.copy_area(self.imgPos[0], self.imgPos[1], size[0], size[1], pixbuf, 0, 0)
		del scaled
		gc.collect()
		return pixbuf
	
	def rotate(self, angle):
		"""
			Rotate the image by an GTK Angle
		"""
		self.currentPixbuf = self.currentPixbuf.rotate_simple(angle)
		self.scaleCache[1] = 0
		gc.collect()
		self.autoScale()

	def flip(self, horizontally):
		"""
			Flip the image. Set horizontally to True to flip
			horizontally.
		"""
		self.currentPixbuf = self.currentPixbuf.flip(horizontally)
		self.scaleCache[1] = 0
		gc.collect()
		
	def display(self):
		"""
			Show the loaded image
		"""
		self.imgDisplay.set_from_pixbuf(self.getVisible())
		gc.collect()

if __name__ == "__main__":
	# Parse command line
	parser = OptionParser("%%prog [options] <files or folders>\n(p)qiv version %s by Phillip Berndt" % VERSION)
	parser.add_option("-i", "--info", action="store_false", default=True, dest="infovisible", help="Hide infobox")
	parser.add_option("-f", "--fullscreen", action="store_true", dest="fullscreen", help="Start in fullscreen")
	parser.add_option("-b", "--bindings", action="store_true", dest="bindings", help="Show keybindings")
	parser.add_option("-s", "--slideshow", type="int", dest="slide", help="Activate slideshow with interval n")
	parser.add_option("-t", "--thumbnail", action="store_true", dest="thumbnail", help="Create one big thumbnail")
	(options, args) = parser.parse_args()

	# Show bindings
	if options.bindings:
		import re
		print "Key bindings for pqiv:"
		for line in open(sys.argv[0]).readlines():
			match = re.search("Bi{1}nding: (.+:) (.+)$", line)
			if match:
				print " ", match.group(1).ljust(15), match.group(2)
		print
		sys.exit(0)
		
	# Create a list of all files
	fileList = list(generateFileList(args))
	if len(fileList) == 0:
		sys.exit(0)

	# Display them
	iv = ImageViewer(fileList, options)
	iv.show()
	gtk.main()
