/**
 * vim:ft=c:fileencoding=utf-8:fdm=marker
 *
 * pqiv - pretty quick image viewer
 * Copyright (c) Phillip Berndt, 2007
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License ONLY.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 * 
 */
#define RELEASE "0.4"
//#define DEBUG

/* Includes {{{ */
#include <stdio.h>
#include <gtk/gtk.h>
#include <glib/gconvert.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <regex.h> 
#include <libgen.h>
#include <sys/stat.h>
#include <time.h>
/* }}} */
/* Definitions {{{ */
/* libc stuff */
extern char *optarg;
extern int optind, opterr, optopt;
extern char **environ;

/* GTK stuff */
GtkWidget *window = NULL;
GtkWidget *imageWidget = NULL;
GtkWidget *fixed;
GtkWidget *infoLabel;
GtkWidget *infoLabelBox;
GtkWidget *mouseEventBox;
GdkPixbuf *currentImage = NULL;
GdkPixbuf *scaledImage = NULL;
static char emptyCursor[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

/* Structure for file list building */
struct file {
	char *fileName;
	int nr;
	struct file *next;
	struct file *prev;
} firstFile;
struct file *currentFile = &firstFile;
struct file *lastFile = &firstFile;

/* Program settings */
char isFullscreen = FALSE;
char infoBoxVisible = 0;
float scaledAt;
float zoom;
char autoScale = TRUE;
int moveX, moveY;
int slideshowInterval = 3;
int slideshowEnabled = 0;

/* Program options */
char optionHideInfoBox = FALSE;
char optionFullScreen = FALSE;
/* }}} */
/* Error, debug and info message stuff {{{ */
/* Debugging {{{ */
#ifdef DEBUG
	#define DEBUG1(text) fprintf(stderr, "%d: %s\n", __LINE__, text);
	#define DEBUG2(text, param) fprintf(stderr, "%d: %s %s\n", __LINE__, text, param);
	#define G_ENABLE_DEBUG
#else
	#define DEBUG1(text);
	#define DEBUG2(text, param);
#endif
#define die(text) fprintf(stderr, "%s\n", text); exit(1);
/* }}} */
/* Info text (Yellow lable & title) {{{ */
char *infoText;
void setInfoText(char *text) {
	char newText[1024];

	if(text == NULL) {
		sprintf(newText, "pqiv: %s (%dx%d) %d%% [%d/%d]", g_filename_display_name(currentFile->fileName), gdk_pixbuf_get_width(scaledImage),
			gdk_pixbuf_get_height(scaledImage), (int)(zoom * 100), currentFile->nr + 1, lastFile->nr + 1);
	}
	else {
		sprintf(newText, "pqiv: %s (%dx%d) %d%% [%d/%d] (%s)", g_filename_display_name(currentFile->fileName), gdk_pixbuf_get_width(scaledImage),
			gdk_pixbuf_get_height(scaledImage), (int)(zoom * 100), currentFile->nr + 1, lastFile->nr + 1, text);
	}
	gtk_window_set_title(GTK_WINDOW(window), newText);
	gtk_label_set_text(GTK_LABEL(infoLabel), &(newText[6]));
}
/* }}} */
void helpMessage(char claim) { /* {{{ */
	/* Perl code to get bindings:
	 * perl -ne 'm/(?:OPTION|BIND): (.+?): (.+?)[{$\*]/ or next; $_=$1.(" "x(15-length($1))).$2; print "\" $_\\n\"\n";' < pqiv.c
	 */
	printf("usage: pqiv [options] <files or folders>\n"
		"(p)qiv version " RELEASE " by Phillip Berndt\n"
		"\n");
	if(claim != 0) {
		printf("I don't understand the meaning of %c\n\n", claim);
	}
	printf(
		"options:\n"
		" -i             Hide info box \n"
		" -f             Start in fullscreen mode \n"
		" -s             Activate slideshow \n"
		" -d n           Slideshow interval \n"
		" -t             Shrink image(s) larger than the screen to fit \n"
		" -r             Read additional filenames (not folders) from stdin \n"
		"\n"
		" Place any of those options into ~/.pqivrc (like you'd do here) to make it default.\n"
		"\n"
		"key bindings:\n"
		" Backspace      Previous image \n"
		" PgUp           Jump 10 images forewards \n"
		" PgDn           Jump 10 images backwards \n"
		" Escape         Quit \n"
		" Cursor keys    Move (Fullscreen) \n"
		" Space          Next image \n"
		" f              Fullscreen \n"
		" r              Reload \n"
		" +              Zoom in \n"
		" -              Zoom out \n"
		" q              Quit \n"
		" t              Toggle autoscale \n"
		" l              Rotate left \n"
		" k              Rotate right \n"
		" h              Horizontal flip \n"
		" v              Vertical flip \n"
		" i              Show/hide info box \n"
		" s              Slideshow toggle \n"
		" a              Hardlink current image to .qiv-select/ \n"
		" Drag & Drop    Move image (Fullscreen) \n"
		" Scroll         Next/previous image \n"
		"\n"
		);
	exit(0);
} /* }}} */
/* }}} */
/* File loading and file structure {{{ */
#define EXTENSIONS "\\.(png|gif|jpg|bmp|xpm)$"
regex_t extensionCompiled;
void load_files_addfile(char *file) { /*{{{*/
	if(firstFile.fileName != NULL) {
		lastFile->next = (struct file*)malloc(sizeof(struct file));
		if(lastFile->next == NULL) {
			die("Failed to allocate memory");
		}
		lastFile->next->nr = lastFile->nr + 1;
		lastFile->next->prev = lastFile;
		lastFile = lastFile->next;
	}
	lastFile->fileName = (char *)malloc(strlen(file) + 1);
	if(lastFile->fileName == NULL) {
		die("Failed to allocate memory");
	}
	strcpy(lastFile->fileName, file);
	lastFile->next = NULL;
} /*}}}*/
void load_files_helper(DIR *cwd) { /*{{{*/
	struct dirent *dirp;
	DIR *ncwd;
	int test;
	char *completeName;
	char *cwdName;
	while((dirp = readdir(cwd)) != NULL) {
		if(strcmp(dirp->d_name, ".") == 0 || strcmp(dirp->d_name, "..") == 0) {
			continue;
		}

		ncwd = opendir(dirp->d_name);
		if(ncwd != NULL) {
			chdir(dirp->d_name);
			load_files_helper(ncwd);
			chdir("..");
			closedir(ncwd);
		}
		else {
			if(regexec(&extensionCompiled, dirp->d_name, 0, 0, 0) == 0) {
				test = open(dirp->d_name, O_RDONLY);
				if(test > -1) {
					cwdName = (char*)malloc(1024);
					getcwd(cwdName, 1024);
					completeName = (char*)malloc(strlen(dirp->d_name) + strlen(cwdName) + 2);
					if(completeName == NULL) {
						die("Failed to allocate memory");
					}
					sprintf(completeName, "%s/%s", cwdName, dirp->d_name);
					load_files_addfile(completeName);
					free(cwdName);
					free(completeName);
					close(test);
				}
			}
		}
	}
} /*}}}*/
void load_files(int *argc, char **argv[]) { /*{{{*/
	int i;
	int test;
	DIR *cwd;
	char *ocwd;

	DEBUG1("Load files");
	regcomp(&extensionCompiled, EXTENSIONS, REG_ICASE | REG_EXTENDED | REG_NOSUB);
	for(i=0; i<*argc; i++) {
		cwd = opendir((*argv)[i]);
		if(cwd != NULL) {
			ocwd = (char*)malloc(1024);
			getcwd(ocwd, 1024);
			chdir((*argv)[i]);
			load_files_helper(cwd);
			chdir(ocwd);
			free(ocwd);
			closedir(cwd);
		}
		else {
			test = open((*argv)[i], O_RDONLY);
			if(test > -1) {
				load_files_addfile((*argv)[i]);
				close(test);
			}
		}
	}
} /*}}}*/
/*}}}*/
/* Load images and modify them {{{ */
char loadImage() { /*{{{*/
	GdkPixbuf *tmpImage;
	DEBUG2("loadImage", currentFile->fileName);
	tmpImage = gdk_pixbuf_new_from_file(currentFile->fileName, NULL);
	if(!tmpImage) {
		printf("Failed to load %s\n", currentFile->fileName);
		return FALSE;
	}
	if(currentImage != NULL) {
		g_object_unref(currentImage);
	}
	currentImage = tmpImage;

	zoom = 1;
	moveX = moveY = 0;
	scaledAt = -1;
	return TRUE;
} /*}}}*/
inline void scale() { /*{{{*/
	int imgx, imgy;

	if(scaledAt != zoom) {
		DEBUG1("Scale");
		imgx = gdk_pixbuf_get_width(currentImage);
		imgy = gdk_pixbuf_get_height(currentImage);
		scaledAt = zoom;
		if(scaledImage != NULL) {
			DEBUG1("Free");
			g_object_unref(scaledImage);
			scaledImage = NULL;
		}
		if(imgx * zoom < 10 || imgy * zoom < 10) {
			scaledImage = gdk_pixbuf_scale_simple(currentImage, 10, 10, GDK_INTERP_BILINEAR);
		}
		else {
			scaledImage = gdk_pixbuf_scale_simple(currentImage, (int)(imgx * zoom), (int)(imgy * zoom), GDK_INTERP_BILINEAR);
		}
		if(scaledImage == NULL) {
			die("Failed to allocate memory");
		}
	}
} /*}}}*/
void autoScaleFactor() { /*{{{*/
	int imgx, imgy, scrx, scry, rem;
	GdkScreen *screen;
	DEBUG1("autoScaleFactor");

	if(!autoScale) {
		zoom = 1;
		scale();
		return;
	}

	screen = gtk_widget_get_screen(window);
	imgx = gdk_pixbuf_get_width(currentImage);
	imgy = gdk_pixbuf_get_height(currentImage);
	scrx = gdk_screen_get_width(screen);
	scry = gdk_screen_get_height(screen);

	if(isFullscreen == TRUE) {
		rem = 0;
	}
	else {
		rem = 125;
	}

	zoom = 1;
	if(imgx > scrx - rem)
		zoom = (scrx - rem) * 1.0f / imgx;
	if(imgy * zoom > scry - rem)
		zoom = (scry - rem) * 1.0f / imgy;

	scale();
} /*}}}*/
void scaleBy(float add) { /*{{{*/
	DEBUG1("Scale by");
	zoom += add;
	scale();
} /*}}}*/
void flip(char horizontal) { /*{{{*/
	GdkPixbuf *tmp;
	DEBUG1("flip");
	tmp = gdk_pixbuf_flip(currentImage, horizontal);
	g_object_unref(currentImage);
	currentImage = tmp;

	scaledAt = -1;
} /*}}}*/
void rotate(char left) { /*{{{*/
	GdkPixbuf *tmp;
	DEBUG1("Rotate");
	tmp = gdk_pixbuf_rotate_simple(currentImage, left == TRUE ? 90 : 270);
	g_object_unref(currentImage);
	currentImage = tmp;

	scaledAt = -1;
} /*}}}*/
/* }}} */
/* Draw image to screen {{{ */
gint exposeCb(GtkWidget *widget, GdkEventExpose *event, gpointer data) { /*{{{*/
	guchar *pixels;
	int rowstride;

	DEBUG1("Expose");
	if(scaledImage != NULL) {
		rowstride = gdk_pixbuf_get_rowstride(scaledImage);
		pixels = gdk_pixbuf_get_pixels(scaledImage) + rowstride * event->area.y + event->area.x * 3;
		gdk_draw_rgb_image_dithalign(widget->window,
			widget->style->black_gc,
			event->area.x + moveX, event->area.y + moveY,
			event->area.width, event->area.height,
			GDK_RGB_DITHER_NORMAL,
			pixels, rowstride,
			event->area.x, event->area.y);
	}

	return 0;
} /*}}}*/
void setFullscreen(char fullscreen) { /*{{{*/
	GdkCursor *cursor;
	GdkPixmap *source;
	GdkScreen *screen;
	int scrx, scry;

	DEBUG1("Fullscreen");
	if(fullscreen == TRUE) {
		/* This is actually needed because of crappy window managers :/ */
		screen = gtk_widget_get_screen(window);
		scrx = gdk_screen_get_width(screen);
		scry = gdk_screen_get_height(screen);
		gtk_window_set_resizable(GTK_WINDOW(window), TRUE);
		gtk_main_iteration();
		gdk_window_fullscreen(window->window);
		gtk_main_iteration();
		gtk_widget_set_size_request(window, scrx, scry);
		gtk_window_set_resizable(GTK_WINDOW(window), FALSE);

		/* Hide cursor */
		source = gdk_bitmap_create_from_data (NULL, emptyCursor,
                                       16, 16);
		cursor = gdk_cursor_new_from_pixmap (source, source, (GdkColor*)emptyCursor, (GdkColor*)emptyCursor, 8, 8);
		gdk_pixmap_unref(source);
		gdk_window_set_cursor(window->window, cursor);
		gdk_cursor_unref(cursor);
	}
	else {
		gtk_window_set_resizable(GTK_WINDOW(window), TRUE);
		gtk_main_iteration();
		gdk_window_unfullscreen(window->window);
		gtk_main_iteration();
		gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
		gdk_window_set_cursor(window->window, NULL);
	}
	scaledAt = -1;
	isFullscreen = fullscreen;
} /*}}}*/
gboolean toFullscreenCb(gpointer data) { /*{{{*/
	GdkEventKey keyEvent;
	DEBUG1("Switch to fullscreen (callback)");

	/* We have to emulate a keypress because of some buggy wms */
	memset(&keyEvent, 0, sizeof(GdkEventKey));
	keyEvent.type = GDK_KEY_PRESS;
	keyEvent.window = window->window;
	keyEvent.time = time(NULL);
	keyEvent.keyval = 102;
	keyEvent.hardware_keycode = 41;
	keyEvent.length = 1;
	keyEvent.string = "f";
	gdk_event_put((GdkEvent*)(&keyEvent));

	return FALSE;
} /* }}} */
void resizeAndPosWindow() { /*{{{*/
	DEBUG1("Resize");
	int imgx, imgy, scrx, scry;
	GdkScreen *screen;
	imgx = gdk_pixbuf_get_width(scaledImage);
	imgy = gdk_pixbuf_get_height(scaledImage);
	screen = gtk_widget_get_screen(window);
	scrx = gdk_screen_get_width(screen);
	scry = gdk_screen_get_height(screen);

	gtk_widget_set_size_request(imageWidget, imgx, imgy);
	gtk_widget_set_size_request(mouseEventBox, imgx, imgy);

	if(!isFullscreen) {
		gtk_window_set_resizable(GTK_WINDOW(window), TRUE);
		gtk_main_iteration();
		gtk_widget_set_size_request(window, imgx, imgy);
		gtk_main_iteration();
		gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
		gtk_window_move(GTK_WINDOW(window), (scrx - imgx) / 2, (scry - imgy) / 2);
		gtk_fixed_move(GTK_FIXED(fixed), imageWidget, 0, 0);
	}
	else {
		gtk_fixed_move(GTK_FIXED(fixed), imageWidget, (scrx - imgx) / 2 + moveX, (scry - imgy) / 2 + moveY);
	}
} /*}}}*/
void displayImage() { /*{{{*/
	DEBUG1("Display");
	if(scaledAt != zoom) {
		scale();
	}
	/* Draw image */
	gtk_image_set_from_pixbuf(GTK_IMAGE(imageWidget), scaledImage);
} /*}}}*/
char reloadImage() { /*{{{*/
	if(!loadImage())
		return FALSE;
	autoScaleFactor();
	resizeAndPosWindow();
	displayImage();
	setInfoText(NULL);
	return TRUE;
} /*}}}*/
/* }}} */
/* Slideshow {{{ */
gboolean slideshowCb(gpointer data) { /*{{{*/
	GdkEventKey keyEvent;
	DEBUG1("Slideshow next");

	/* We have to emulate a keypress because of some buggy wms */
	memset(&keyEvent, 0, sizeof(GdkEventKey));
	keyEvent.type = GDK_KEY_PRESS;
	keyEvent.window = window->window;
	keyEvent.time = time(NULL);
	keyEvent.keyval = 32;
	keyEvent.hardware_keycode = 65;
	keyEvent.length = 1;
	keyEvent.string = " ";
	gdk_event_put((GdkEvent*)(&keyEvent));

	return TRUE;
} /*}}}*/
void slideshowEnable(char enable) { /*{{{*/
	DEBUG1("Slideshow switch");
	if(slideshowEnabled != 0) {
		g_source_remove(slideshowEnabled);
		slideshowEnabled = 0;
	}
	if(enable != FALSE) {
		slideshowEnabled = g_timeout_add(slideshowInterval * 1000, slideshowCb, NULL);
	}
} /*}}}*/
/* }}} */
/* Keyboard & mouse event handlers {{{ */
int mouseScrollEnabled = FALSE;
gint keyboardCb(GtkWidget *widget, GdkEventKey *event, gpointer data) { /*{{{*/
	int i = 0, n = 0;
	char *buf, *buf2;
	#ifdef DEBUG
		printf("Keyboard: '%c' (%d), %d\n",
			event->keyval,
			(int)event->keyval,
			event->hardware_keycode
		);
	#endif

	switch(event->hardware_keycode) {
		/* BIND: Backspace: Previous image {{{ */
		case 22:
			i = currentFile->nr;
			do {
				currentFile = currentFile->prev;
				if(currentFile == NULL || currentFile->fileName == NULL) {
					currentFile = lastFile;
				}
			} while((!reloadImage()) && i != currentFile->nr);
			break;
			/* }}} */
		/* BIND: PgUp: Jump 10 images forewards {{{ */
		case 99:
			i = currentFile->nr;
			do {
				for(n=0; n<10; n++) {
					currentFile = currentFile->next;
					if(currentFile == NULL) {
						currentFile = &firstFile;
					}
				}
			} while((!reloadImage()) && i != currentFile->nr);
			break;
			/* }}} */
		/* BIND: PgDn: Jump 10 images backwards {{{ */
		case 105:
			i = currentFile->nr;
			do {
				for(n=0; n<10; n++) {
					currentFile = currentFile->prev;
					if(currentFile == NULL || currentFile->fileName == NULL) {
						currentFile = lastFile;
					}
				}
			} while((!reloadImage()) && i != currentFile->nr);
			break;
			/* }}} */
		/* BIND: Escape: Quit {{{ */
		case 9:
			gtk_main_quit();
			break;
			/* }}} */
		/* BIND: Cursor keys: Move (Fullscreen) {{{ */
		case 98:
			i = (event->state != 1 ? 10 : 50);
		case 104:
			if(event->hardware_keycode == 104)
				i = -(event->state != 1 ? 10 : 50);
		case 100:
			if(event->hardware_keycode == 100)
				n = (event->state != 1 ? 10 : 50);
		case 102:
			if(event->hardware_keycode == 102)
				n = -(event->state != 1 ? 10 : 50);

			if(isFullscreen) {
				moveX += n;
				moveY += i;
				resizeAndPosWindow();
				displayImage();
			}
			break;
			/* }}} */
	}
	switch(event->keyval) {
		/* BIND: Space: Next image {{{ */
		case ' ':
			i = currentFile->nr;
			do {
				currentFile = currentFile->next;
				if(currentFile == NULL) {
					currentFile = &firstFile;
				}
			} while((!reloadImage()) && i != currentFile->nr);
			break;
			/* }}} */
		/* BIND: f: Fullscreen {{{ */
		case 'f':
			moveX = moveY = 0;
			setFullscreen(!isFullscreen);
			autoScaleFactor();
			resizeAndPosWindow();
			displayImage();
			break;
			/* }}} */
		/* BIND: r: Reload {{{ */
		case 'r':
			reloadImage();
			break;
			/* }}} */
		/* BIND: +: Zoom in {{{ */
		case '+':
			scaleBy(.05);
			resizeAndPosWindow();
			displayImage();
			setInfoText("Zoomed in");
			break;
			/* }}} */
		/* BIND: -: Zoom out {{{ */
		case '-':
			scaleBy(-.05);
			resizeAndPosWindow();
			displayImage();
			setInfoText("Zoomed out");
			break;
			/* }}} */
		/* BIND: q: Quit {{{ */
		case 'q':
			gtk_main_quit();
			break;
		/* }}} */
		/* BIND: t: Toggle autoscale {{{ */
		case 't':
			moveX = moveY = 0;
			autoScale = !autoScale;
			if(autoScale == TRUE) {
				setInfoText("Autoscale on");
				autoScaleFactor();
				resizeAndPosWindow();
				displayImage();
			}
			else {
				setInfoText("Autoscale off");
				autoScaleFactor();
				resizeAndPosWindow();
				displayImage();
			}
			
			break;
		/* }}} */
		/* BIND: l: Rotate left {{{ */
		case 'l':
			setInfoText("Rotated left");
			rotate(TRUE);
			autoScaleFactor();
			resizeAndPosWindow();
			displayImage();
			break;
			/* }}} */
		/* BIND: k: Rotate right {{{ */
		case 'k':
			setInfoText("Rotated right");
			rotate(FALSE);
			autoScaleFactor();
			resizeAndPosWindow();
			displayImage();
			break;
			/* }}} */
		/* BIND: h: Horizontal flip {{{ */
		case 'h':
			setInfoText("Flipped horizontally");
			flip(TRUE);
			displayImage();
			break;
			/* }}} */
		/* BIND: v: Vertical flip {{{ */
		case 'v':
			setInfoText("Flipped vertically");
			flip(FALSE);
			displayImage();
			break;
			/* }}} */
		/* BIND: i: Show/hide info box {{{ */
		case 'i':
			setInfoText(NULL);
			infoBoxVisible = !infoBoxVisible;
			if(infoBoxVisible == TRUE) {
				gtk_widget_show(infoLabelBox);
			}
			else {
				gtk_widget_hide(infoLabelBox);
			}
			break;
			/* }}} */
		/* BIND: s: Slideshow toggle {{{ */
		case 's':
			if(slideshowEnabled != 0) {
				setInfoText("Slideshow disabled");
				slideshowEnable(FALSE);
			}
			else {
				setInfoText("Slideshow enabled");
				slideshowEnable(TRUE);
			}
			break;
			/* }}} */
		/* BIND: a: Hardlink current image to .qiv-select/ {{{ */
		case 'a':
			mkdir("./.qiv-select", 0755);
			buf = (char*)malloc(1024);
			if(buf == NULL) {
				die("Failed to allocate memory");
			}
			buf2 = basename(currentFile->fileName);
			sprintf(buf, "./.qiv-select/%s", buf2);
			link(currentFile->fileName, buf);
			free(buf);
			setInfoText("Hardlink saved");
			/* }}} */
	}
	return 0;
} /*}}}*/
/* BIND: Drag & Drop: Move image (Fullscreen) {{{ */
gint mouseButtonCb(GtkWidget *widget, GdkEventButton *event, gpointer data) {
	GdkScreen *screen; int scrx, scry;
	if(event->type == GDK_BUTTON_PRESS && isFullscreen == TRUE) {
		mouseScrollEnabled = TRUE;
		screen = gtk_widget_get_screen(window);
		scrx = gdk_screen_get_width(screen) / 2;
		scry = gdk_screen_get_height(screen) / 2;
		gdk_display_warp_pointer(gdk_display_get_default(),
			gdk_display_get_default_screen(gdk_display_get_default()), scrx, scry);
	}
	else if(event->type == GDK_BUTTON_RELEASE) {
		mouseScrollEnabled = FALSE;
	}
	return 0;
}
gint mouseMotionCb(GtkWidget *widget, GdkEventMotion *event, gpointer data) {
	GdkScreen *screen; int scrx, scry;
	if(mouseScrollEnabled == FALSE) {
		return 0;
	}
	screen = gtk_widget_get_screen(window);
	scrx = gdk_screen_get_width(screen) / 2;
	scry = gdk_screen_get_height(screen) / 2;

	moveX += event->x - scrx;
	moveY += event->y - scry;
	gdk_display_warp_pointer(gdk_display_get_default(),
		gdk_display_get_default_screen(gdk_display_get_default()), scrx, scry);

	resizeAndPosWindow();
	displayImage();
	return 0;
}
/* }}} */
/* BIND: Scroll: Next/previous image {{{ */
gint mouseScrollCb(GtkWidget *widget, GdkEventScroll *event, gpointer data) {
	int i;
	i = currentFile->nr;
	do {
		currentFile = event->direction == 0 ? currentFile->next : currentFile->prev;
		if(currentFile == NULL) {
			currentFile = event->direction == 0 ? &firstFile : lastFile;
		}
	} while((!reloadImage()) && i != currentFile->nr);
	return 0;
	/* }}} */
}
/* }}} */

int main(int argc, char *argv[]) {
/* Variable definitions {{{ */
	GdkColor color;
	char option;
	char **envP;
	char *fileName;
	char *fileNameL;
	char optionFullScreen = FALSE;
	char optionActivateSlideshow = FALSE;
	char optionReadStdin = FALSE;
	FILE *optionsFile;
	char *options[255];
	int optionCount = 1, i;
/* }}} */
	/* Command line and configuration parsing {{{ */
	envP = environ;
	options[0] = argv[0];
	while((fileNameL = *envP++) != NULL) {
		if(strncmp(fileNameL, "HOME=", 5) != 0) {
			continue;
		}
		fileName = (char*)malloc(strlen(&(fileNameL[5])) + 9);
		sprintf(fileName, "%s/.pqivrc", &(fileNameL[5]));
		optionsFile = fopen(fileName, "r");
		
		if(optionsFile) {
			while((option = fgetc(optionsFile)) != EOF) {
				if(optionCount > 253) {
					die("Too many options");
				}
				options[optionCount] = (char*)malloc(1024);
				i = 0;
				do {
					if(option < 33) {
						break;
					}
					options[optionCount][i++] = option;
					if(i == 1024) {
						die("Option is too long");
					}
				} while((option = fgetc(optionsFile)) != EOF);
				options[optionCount][i] = 0;
				optionCount++;
			}
		}
		free(fileName);
		break;
	}
	for(i=1; i<argc; i++) {
		if(optionCount > 253) {
			die("Too many options");
		}
		options[optionCount] = argv[i];
		optionCount++;
	}

	opterr = 0;
	while((option = getopt(optionCount, options, "ifstrd:")) > 0) {
		switch(option) {
			/* OPTION: -i: Hide info box */
			case 'i':
				optionHideInfoBox = TRUE;	
				break;
			/* OPTION: -f: Start in fullscreen mode */
			case 'f':
				optionFullScreen = TRUE;
				break;
			/* OPTION: -s: Activate slideshow */
			case 's':
				optionActivateSlideshow = TRUE;
				break;
			/* OPTION: -d n: Slideshow interval */
			case 'd':
				slideshowInterval = atoi(optarg);
				if(slideshowInterval < 1) {
					helpMessage('d');
				}
				break;
			/* OPTION: -t: Shrink image(s) larger than the screen to fit */
			case 't':
				autoScale = FALSE;
				break;
			/* OPTION: -r: Read additional filenames (not folders) from stdin */
			case 'r':
				optionReadStdin = TRUE;
				break;
			case '?':
				helpMessage(optopt);
			default:
				helpMessage(0);
		}
	}
	/* }}} */
	/* Load files {{{ */
	argv++; argc--;
	if(argv[0] == 0) {
		exit(0);
	}
	memset(&firstFile, 0, sizeof(struct file));
	load_files(&argc, &argv);
	if(optionReadStdin == TRUE) {
		fileName = (char*)malloc(1025);
		do {
			memset(fileName, 0, 1024);
			fgets(fileName, 1024, stdin);
			if(strlen(fileName) == 0) {
				break;
			}
			fileNameL = &fileName[strlen(fileName) - 1];
			if(*fileNameL < 33)
				*fileNameL = 0;
			fileNameL--;
			if(*fileNameL < 33)
				*fileNameL = 0;
			load_files_addfile(fileName);
		} while(TRUE);
	}
	/* }}} */
	/* Initialize gtk {{{ */
	g_thread_init(NULL);
	gdk_threads_init();

	/* Create gtk window */
	gtk_init(&argc, &argv);
	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
	gtk_widget_set_size_request(window, 640, 480);
	gtk_window_set_title (GTK_WINDOW(window), "pqiv");
	gtk_widget_show(window);
	fixed = gtk_fixed_new();
	gtk_container_add(GTK_CONTAINER(window), fixed);
	gtk_widget_show(fixed);

	/* Create drawing area */
	imageWidget = gtk_image_new();
	color.red = 0; color.green = 0; color.blue = 0;
	gtk_widget_modify_bg(GTK_WIDGET(window), GTK_STATE_NORMAL, &color);
	gtk_fixed_put(GTK_FIXED(fixed), imageWidget, 0, 0);
	gtk_widget_show(imageWidget);

	/* Info label */
	infoLabelBox = gtk_event_box_new();
	infoLabel = gtk_label_new(NULL);
	color.red = 0xee * 255; color.green = 0xee * 255; color.blue = 0x55 * 255;
	gtk_widget_modify_bg(GTK_WIDGET(infoLabelBox), GTK_STATE_NORMAL, &color);
	gtk_widget_modify_font(infoLabel, pango_font_description_from_string("sansserif 9"));
	gtk_label_set_text(GTK_LABEL(infoLabel), "");
	gtk_misc_set_padding(GTK_MISC(infoLabel), 2, 2);
	gtk_container_add(GTK_CONTAINER(infoLabelBox), infoLabel);
	gtk_fixed_put(GTK_FIXED(fixed), infoLabelBox, 10, 10);
	gtk_widget_show(infoLabel);
	if(optionHideInfoBox != TRUE) {
		infoBoxVisible = TRUE;
		gtk_widget_show(infoLabelBox);
	}

	/* Event box for the mouse */
	mouseEventBox = gtk_event_box_new();
	gtk_event_box_set_visible_window(GTK_EVENT_BOX(mouseEventBox), FALSE);
	gtk_fixed_put(GTK_FIXED(fixed), mouseEventBox, 0, 0);
	gtk_widget_show(mouseEventBox);

	/* Signalling stuff */
	g_signal_connect(window, "key-press-event",
		G_CALLBACK(keyboardCb), NULL);
	g_signal_connect(mouseEventBox, "button-press-event",
		G_CALLBACK(mouseButtonCb), NULL);
	g_signal_connect(mouseEventBox, "button-release-event",
		G_CALLBACK(mouseButtonCb), NULL);
	g_signal_connect(mouseEventBox, "scroll-event",
		G_CALLBACK(mouseScrollCb), NULL);
	g_signal_connect(mouseEventBox, "motion-notify-event",
		G_CALLBACK(mouseMotionCb), NULL);
	g_signal_connect (window, "destroy",
		G_CALLBACK(gtk_main_quit),
	        &window);

	/* }}} */
	/* Load first image {{{ */
	while(!loadImage()) {
		currentFile = currentFile->next;
		if(currentFile == NULL) {
			die("Failed to load any of the images");
		}
	}
	if(optionFullScreen == TRUE) {
		g_timeout_add(100, toFullscreenCb, NULL);
	}
	autoScaleFactor();
	resizeAndPosWindow();
	displayImage();
	setInfoText(NULL);
	if(optionActivateSlideshow == TRUE) {
		slideshowEnable(TRUE);
	}
	/* }}} */
	gtk_main();
	return 0;
}
