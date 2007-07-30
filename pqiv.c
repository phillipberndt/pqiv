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

/* Includes {{{ */
#include <stdio.h>
#include <gtk/gtk.h>
#include <glib/gconvert.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
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
static GtkWidget *window = NULL;
static GtkWidget *imageWidget = NULL;
static GtkWidget *fixed;
static GtkWidget *infoLabel;
static GtkWidget *infoLabelBox;
static GtkWidget *mouseEventBox;
static GdkPixbuf *currentImage = NULL;
static GdkPixbuf *scaledImage = NULL;
static GdkPixbuf *memoryArgImage = NULL;
static char emptyCursor[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
static char *chessBoard = 
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
		"\233\233\233\377\210jjj\377\210\233\233\233\377\210jjj\377";


/* Structure for file list building */
static struct file {
	char *fileName;
	int nr;
	struct file *next;
	struct file *prev;
} firstFile;
static struct file *currentFile = &firstFile;
static struct file *lastFile = &firstFile;
GSList *fileExtensions;

/* Program settings */
static char isFullscreen = FALSE;
static char infoBoxVisible = 0;
static float scaledAt;
static float zoom;
static char autoScale = TRUE;
static int moveX, moveY;
static int slideshowInterval = 3;
static int slideshowEnabled = 0;

/* Program options */
static char optionHideInfoBox = FALSE;
static char optionDoChessboard = TRUE;
static char *optionCommands[] = { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };
static GdkInterpType optionInterpolation = GDK_INTERP_BILINEAR;

/* Functions */
char reloadImage();
void autoScaleFactor();
void resizeAndPosWindow();
void displayImage();
/* }}} */
/* Error, debug and info message stuff {{{ */
/* Debugging {{{ */
#ifdef DEBUG
	#define DEBUG1(text) g_printerr("(%04d) %-20s %s\n", __LINE__, G_STRFUNC, text);
	#define DEBUG2(text, param) g_printerr("(%04d) %-20s %s %s\n", __LINE__, G_STRFUNC, text, param);
	#define G_ENABLE_DEBUG
#else
	#define DEBUG1(text);
	#define DEBUG2(text, param);
#endif
#define die(text) g_printerr("%s\n", text); exit(1);
/* }}} */
/* Info text (Yellow lable & title) {{{ */
char *infoText;
void setInfoText(char *text) {
	char newText[1024];
	if(text == NULL) {
		sprintf(newText, "pqiv: %s (%dx%d) %d%% [%d/%d]", g_filename_display_name(currentFile->fileName),
			gdk_pixbuf_get_width(scaledImage),
			gdk_pixbuf_get_height(scaledImage), (int)(zoom * 100), currentFile->nr + 1, lastFile->nr + 1);
	}
	else {
		sprintf(newText, "pqiv: %s (%dx%d) %d%% [%d/%d] (%s)", g_filename_display_name(currentFile->fileName),
			gdk_pixbuf_get_width(scaledImage),
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
	g_print("usage: pqiv [options] <files or folders>\n"
		"(p)qiv version " RELEASE " by Phillip Berndt\n"
		"\n");
	if(claim != 0) {
		g_print("I don't understand the meaning of %c\n\n", claim);
	}
	g_print(
		"options:\n"
		" -i             Hide info box \n"
		" -f             Start in fullscreen mode \n"
		" -s             Activate slideshow \n"
		" -d n           Slideshow interval \n"
		" -t             Shrink image(s) larger than the screen to fit \n"
		" -r             Read additional filenames (not folders) from stdin \n"
		" -c             Disable the background for transparent images \n"
		" -p             Interpolation quality level (1-4, defaults to 3)\n"
		" -<n> s         Set command number n (1-9) to s \n"
		"                Prepend command with | to pipe image->command->image\n"
		"                Prepend command with > to display the output of the command\n"
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
		" 0              Autoscale \n"
		" q              Quit \n"
		" t              Toggle autoscale \n"
		" l              Rotate left \n"
		" k              Rotate right \n"
		" h              Horizontal flip \n"
		" v              Vertical flip \n"
		" i              Show/hide info box \n"
		" s              Slideshow toggle \n"
		" a              Hardlink current image to .qiv-select/ \n"
		" <n>            Run command n (1-9) \n"
		" Drag & Drop    Move image (Fullscreen) \n"
		" Scroll         Next/previous image \n"
		"\n"
		);
	exit(0);
} /* }}} */
/* }}} */
/* File loading and file structure {{{ */
void loadFilesAddFile(char *file) { /*{{{*/
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
gint loadFilesFileExtensionsCompareCb(gconstpointer extension, gconstpointer file) { /*{{{*/
	int i;

	if(extension == NULL) {
		return 1;
	}
	i = strlen((char*)file) - strlen((char*)extension);
	if(i <= 0) {
		return 1;
	}
	return strcasecmp(file + i, extension);
} /*}}}*/
void loadFilesHelper(DIR *cwd) { /*{{{*/
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
			loadFilesHelper(ncwd);
			chdir("..");
			closedir(ncwd);
		}
		else {
			if(g_slist_find_custom(fileExtensions, (gpointer)dirp->d_name, loadFilesFileExtensionsCompareCb) != NULL) {
				test = open(dirp->d_name, O_RDONLY);
				if(test > -1) {
					cwdName = (char*)malloc(1024);
					getcwd(cwdName, 1024);
					completeName = (char*)malloc(strlen(dirp->d_name) + strlen(cwdName) + 2);
					if(completeName == NULL) {
						die("Failed to allocate memory");
					}
					sprintf(completeName, "%s/%s", cwdName, dirp->d_name);
					loadFilesAddFile(completeName);
					free(cwdName);
					free(completeName);
					close(test);
				}
			}
		}
	}
} /*}}}*/
void loadFiles(int *argc, char **argv[]) { /*{{{*/
	int i;
	int test;
	DIR *cwd;
	char *ocwd;
	char *buf;
	GError *loadError = NULL;
	GdkPixbufLoader *memoryImageLoader = NULL;

	/* Load files */
	DEBUG1("Load files");
	for(i=0; i<*argc; i++) {
		if(strcmp((*argv)[i], "-") == 0) {
			/* Load image from stdin {{{ */
			if(memoryArgImage != NULL) {
				g_printerr("You can't specify more than one image to be read from stdin.\n");
				continue;
			}
			memoryImageLoader = gdk_pixbuf_loader_new();
			buf = (char*)malloc(1024);
			while(TRUE) {
				i = fread(buf, 1, 1024, stdin);
				if(i == 0) {
					break;
				}
				if(gdk_pixbuf_loader_write(memoryImageLoader, (unsigned char*)buf, i, &loadError) == FALSE) {
					g_printerr("Failed to load the image from stdin: %s\n", loadError->message);
					loadError->message = NULL;
					g_error_free(loadError);
					g_object_unref(memoryImageLoader);
					loadError = NULL;
					memoryArgImage = (GdkPixbuf*)1; /* Ignore further attempts to load an image from stdin */
					break;
				}
			}
			if(gdk_pixbuf_loader_close(memoryImageLoader, &loadError) == TRUE) {
				memoryArgImage = gdk_pixbuf_copy(gdk_pixbuf_loader_get_pixbuf(memoryImageLoader));
				g_object_unref(memoryImageLoader);
				loadFilesAddFile("-");
			}
			else {
				g_printerr("Failed to load the image from stdin: %s\n", loadError->message);
				g_error_free(loadError);
				g_object_unref(memoryImageLoader);
				memoryArgImage = (GdkPixbuf*)1; /* Ignore further attempts to load an image from stdin */
			}
			free(buf);
			continue;
			/* }}} */
		}
		cwd = opendir((*argv)[i]);
		if(cwd != NULL) {
			ocwd = (char*)malloc(1024);
			getcwd(ocwd, 1024);
			chdir((*argv)[i]);
			loadFilesHelper(cwd);
			chdir(ocwd);
			free(ocwd);
			closedir(cwd);
		}
		else {
			test = open((*argv)[i], O_RDONLY);
			if(test > -1) {
				loadFilesAddFile((*argv)[i]);
				close(test);
			}
		}
	}
} /*}}}*/
gint windowCloseOnlyCb(GtkWidget *widget, GdkEventKey *event, gpointer data) { /*{{{*/
	if(event->keyval == 'q') {
		gtk_widget_destroy(widget);
	}
	return 0;
} /* }}} */
gboolean storeImageCb(const char *buf, gsize count, GError **error, gpointer data) { /*{{{*/
	/* Write data to stdout */
	if(write(*(int*)data, buf, count) != -1) {
		return TRUE;
	}
	else {
		close(*(int*)data);
		return FALSE;
	}
} /*}}}*/
void runProgram(char *command) { /*{{{*/
	char *buf4, *buf3, *buf2, *buf;
	GtkWidget *tmpWindow, *tmpScroller, *tmpText;
	FILE *readInformation;
	GString *infoString;
	gsize uniTextLength;
	GdkPixbufLoader *memoryImageLoader = NULL;
	GdkPixbuf *tmpImage = NULL;
	int i, child;
	GError *loadError = NULL;
	int tmpFileDescriptorsTo[] = {0, 0};
	int tmpFileDescriptorsFrom[] = {0, 0};
	if(command[0] == '>') {
		/* Pipe information {{{ */
		command = &command[1];
		buf2 = g_shell_quote(currentFile->fileName);
		buf = (char*)malloc(strlen(command) + 2 + strlen(buf2));
		sprintf(buf, "%s %s", command, buf2);
		readInformation = popen(buf, "r");
		if(readInformation == NULL) {
			g_printerr("Command execution failed for %s\n", command);
			free(buf);
			return;
		}
		infoString = g_string_new(NULL);
		buf3 = (char*)malloc(1024);
		while(fgets(buf3, 1024, readInformation) != NULL) {
			g_string_append(infoString, buf3);
		}
		pclose(readInformation);
		g_free(buf2);
		free(buf3);
		free(buf);
	
		/* Display information in a window */
		tmpWindow = gtk_window_new(GTK_WINDOW_TOPLEVEL);
		gtk_window_set_title(GTK_WINDOW(tmpWindow), command);
		gtk_window_set_position(GTK_WINDOW(tmpWindow), GTK_WIN_POS_CENTER_ON_PARENT);
		gtk_window_set_modal(GTK_WINDOW(tmpWindow), TRUE);
		gtk_window_set_destroy_with_parent(GTK_WINDOW(tmpWindow), TRUE);
		gtk_window_set_type_hint(GTK_WINDOW(tmpWindow), GDK_WINDOW_TYPE_HINT_DIALOG);
		gtk_widget_set_size_request(tmpWindow, 400, 480);
		g_signal_connect(tmpWindow, "key-press-event",
			G_CALLBACK(windowCloseOnlyCb), NULL);
		tmpScroller = gtk_scrolled_window_new(NULL, NULL);
		gtk_container_add(GTK_CONTAINER(tmpWindow), tmpScroller);
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(tmpScroller), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
		tmpText = gtk_text_view_new();
		gtk_container_add(GTK_CONTAINER(tmpScroller), tmpText); 
		gtk_text_view_set_editable(GTK_TEXT_VIEW(tmpText), FALSE);
		if(g_utf8_validate(infoString->str, infoString->len, NULL) == FALSE) {
			buf4 = g_convert(infoString->str, infoString->len, "utf8", "iso8859-1", NULL, &uniTextLength, NULL);
			gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(tmpText)), buf4, uniTextLength);
			free(buf4);
		}
		else {
			gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(tmpText)), infoString->str,
				infoString->len);
		}
		gtk_widget_show(tmpText);
		gtk_widget_show(tmpScroller);
		gtk_widget_show(tmpWindow);

		g_string_free(infoString, TRUE);
		/* }}} */
	}
	else if(command[0] == '|') {
		/* Pipe data {{{ */
		command = &command[1];
		/* Create a pipe */
		if(pipe(tmpFileDescriptorsTo) == -1) {
			g_printerr("Failed to create pipes for data exchange\n");
			setInfoText("Failure");
			return;
		}
		if(pipe(tmpFileDescriptorsFrom) == -1) {
			g_printerr("Failed to create pipes for data exchange\n");
			setInfoText("Failure");
			close(tmpFileDescriptorsTo[0]);
			close(tmpFileDescriptorsTo[1]);
			return;
		}
		/* Spawn child process */
		if((child = fork()) == 0) {
			dup2(tmpFileDescriptorsFrom[1], STDOUT_FILENO);
			dup2(tmpFileDescriptorsTo[0], STDIN_FILENO);
			close(tmpFileDescriptorsFrom[0]); close(tmpFileDescriptorsFrom[1]);
			close(tmpFileDescriptorsTo[0]); close(tmpFileDescriptorsTo[1]);
			_exit(system(command));
		}
		/* Store currentImage to the child processes stdin */
		if(fork() == 0) {
			close(tmpFileDescriptorsFrom[0]); close(tmpFileDescriptorsFrom[1]);
			close(tmpFileDescriptorsTo[0]); 
			if(gdk_pixbuf_save_to_callback(currentImage, storeImageCb, &tmpFileDescriptorsTo[1],
					"png", NULL, NULL) == FALSE) {
				g_printerr("Failed to save image\n");
				close(tmpFileDescriptorsFrom[0]); close(tmpFileDescriptorsFrom[1]);
				close(tmpFileDescriptorsTo[0]); close(tmpFileDescriptorsTo[1]);
				setInfoText("Failure");
				_exit(1);
			}
			close(tmpFileDescriptorsTo[1]);
			_exit(0);
		}
		fsync(tmpFileDescriptorsTo[1]);
		close(tmpFileDescriptorsFrom[1]); close(tmpFileDescriptorsTo[0]);
		close(tmpFileDescriptorsTo[1]);
		/* Load new image from the child processes stdout */
		memoryImageLoader = gdk_pixbuf_loader_new();
		buf = (char*)malloc(1024);
		while(TRUE) {
			if((i = read(tmpFileDescriptorsFrom[0], buf, 1024)) < 1) {
				break;
			}
			if(gdk_pixbuf_loader_write(memoryImageLoader, (unsigned char*)buf, i, &loadError) == FALSE) {
				kill(child, SIGTERM);
				g_printerr("Failed to load output image: %s\n", loadError->message);
				g_object_unref(memoryImageLoader);
				close(tmpFileDescriptorsFrom[0]);
				free(buf);
				setInfoText("Failure");
				return;
			}
		}
		close(tmpFileDescriptorsFrom[0]);
		free(buf);
		wait(NULL);

		if(gdk_pixbuf_loader_close(memoryImageLoader, NULL) == TRUE) {
			g_object_unref(currentImage);
			tmpImage = gdk_pixbuf_loader_get_pixbuf(memoryImageLoader);
			currentImage = gdk_pixbuf_copy(tmpImage);
			g_object_unref(memoryImageLoader);
			scaledAt = -1;
			autoScaleFactor();
			resizeAndPosWindow();
			displayImage();
			setInfoText("Success");
		}
		else {
			g_object_unref(memoryImageLoader);
			setInfoText("Failure");
		}
		/* }}} */
	}
	else {
		/* Run program {{{ */
		if(fork() == 0) {
			buf2 = g_shell_quote(currentFile->fileName);
			buf = (char*)malloc(strlen(command) + 2 + strlen(buf2));
			sprintf(buf, "%s %s", command, buf2);
			system(buf);
			free(buf);
			g_free(buf2);
			exit(1);
		} /* }}} */
	}
} /*}}}*/
/*}}}*/
/* Load images and modify them {{{ */
char loadImage() { /*{{{*/
	GdkPixbuf *tmpImage;
	GdkPixbuf *chessBoardBuf;
	int i, n, o, p;

	DEBUG2("loadImage", currentFile->fileName);
	if(strcmp(currentFile->fileName, "-") == 0) {
		if(memoryArgImage == NULL) {
			return FALSE;
		}
		tmpImage = g_object_ref(memoryArgImage);
	}
	else {
		tmpImage = gdk_pixbuf_new_from_file(currentFile->fileName, NULL);
		if(!tmpImage) {
			g_printerr("Failed to load %s\n", currentFile->fileName);
			return FALSE;
		}
	}
	if(currentImage != NULL) {
		g_object_unref(currentImage);
	}
	if(optionDoChessboard == TRUE && gdk_pixbuf_get_has_alpha(tmpImage)) {
		/* Draw chessboard */
		DEBUG1("Creating chessboard");
		chessBoardBuf = gdk_pixbuf_new_from_inline(159, (const guint8 *)chessBoard, FALSE, NULL);
		currentImage = gdk_pixbuf_copy(tmpImage);
		o = gdk_pixbuf_get_width(currentImage);
		p = gdk_pixbuf_get_height(currentImage);
		for(i=0; i<=o; i+=16) {
			for(n=0; n<=p; n+=16) {
				gdk_pixbuf_copy_area(chessBoardBuf, 0, 0, (o-i<16)?o-i:16, (p-n<16)?p-n:16,
					currentImage, i, n);
			}
		}
		gdk_pixbuf_composite(tmpImage, currentImage, 0, 0, o, p, 0, 0, 1, 1, GDK_INTERP_BILINEAR, 255);
		g_object_unref(tmpImage);
		g_object_unref(chessBoardBuf);
	}
	else {
		currentImage = tmpImage;
	}

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
			scaledImage = gdk_pixbuf_scale_simple(currentImage, (int)(imgx * zoom), (int)(imgy * zoom), optionInterpolation);
		}
		if(scaledImage == NULL) {
			die("Failed to allocate memory");
		}
	}
} /*}}}*/
void forceAutoScaleFactor() { /*{{{*/
	int imgx, imgy, scrx, scry, rem;
	GdkScreen *screen;

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
void autoScaleFactor() { /*{{{*/
	DEBUG1("autoScaleFactor");

	if(!autoScale) {
		zoom = 1;
		scale();
		return;
	}
	
	forceAutoScaleFactor();
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

	if(!isFullscreen) {
		gtk_widget_set_size_request(mouseEventBox, imgx, imgy);
		gtk_window_set_resizable(GTK_WINDOW(window), TRUE);
		gtk_main_iteration();
		gtk_widget_set_size_request(window, imgx, imgy);
		gtk_main_iteration();
		gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
		gtk_window_move(GTK_WINDOW(window), (scrx - imgx) / 2, (scry - imgy) / 2);
		gtk_fixed_move(GTK_FIXED(fixed), imageWidget, 0, 0);
	}
	else {
		gtk_widget_set_size_request(mouseEventBox, scrx, scry);
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
		g_print("(%04d) %-20s Keyboard: '%c' (%d), %d\n",
			__LINE__, G_STRFUNC,
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
		/* BIND: 0: Autoscale {{{ */
		case '0':
			forceAutoScaleFactor();
			moveX = moveY = 0;
			resizeAndPosWindow();
			displayImage();
			setInfoText("Autoscaled");
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
		/* BIND: <n>: Run command n (1-3) {{{ */
		case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8':
		case '9':
			i = event->keyval - '0';
			if(optionCommands[i] != NULL) {
				buf = (char*)malloc(15);
				sprintf(buf, "Run command %c", event->keyval);
				setInfoText(buf);
				gtk_main_iteration();
				free(buf);
				runProgram(optionCommands[i]);
			}
			break;
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
	char **fileFormatExtensionsIterator;
	GSList *fileFormatsIterator;
/* }}} */
/* glib & threads initialization {{{ */
	DEBUG1("Debug mode enabled");
	g_type_init();
	g_thread_init(NULL);
	gdk_threads_init();
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
	while((option = getopt(optionCount, options, "ifsthrcp:d:1:2:3:4:5:6:7:8:9:")) > 0) {
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
				memoryArgImage = (GdkPixbuf*)1; /* Don't allow - files */
				break;
			/* OPTION: -c: Disable the background for transparent images */
			case 'c':
				optionDoChessboard = FALSE;
				break;
			/* OPTION: -p: Interpolation quality level (1-4, defaults to 3) */
			case 'p':
				switch(*optarg) {
					case '1': optionInterpolation = GDK_INTERP_NEAREST; break;
					case '2': optionInterpolation = GDK_INTERP_TILES; break;
					case '3': optionInterpolation = GDK_INTERP_BILINEAR; break;
					case '4': optionInterpolation = GDK_INTERP_HYPER; break;
					default:  helpMessage(0);
				}
				break;
			/* OPTION: -<n> s: Set command number n (1-9) to s */
			case '1': case '2': case '3': case '4':
			case '5': case '6': case '7': case '8':
			case '9':
				i = option - '0';
				optionCommands[i] = (char*)malloc(strlen(optarg) + 1);
				strcpy(optionCommands[i], optarg);
				break;
			case '?':
				helpMessage(optopt);
			case 'h':
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
	if(strcmp(argv[0], "--") == 0) {
		argv++; argc--;
	}
	if(argv[0] == 0) {
		exit(0);
	}
	/* Load available file formats */
	fileExtensions = g_slist_alloc();
	fileFormatsIterator = gdk_pixbuf_get_formats();
	do {
		fileFormatExtensionsIterator = gdk_pixbuf_format_get_extensions(fileFormatsIterator->data);
		while(*fileFormatExtensionsIterator != NULL) {
			fileExtensions = g_slist_prepend(fileExtensions, *fileFormatExtensionsIterator);
			++fileFormatExtensionsIterator;
		}
	} while((fileFormatsIterator = g_slist_next(fileFormatsIterator)) != NULL);
	/* Load files */
	memset(&firstFile, 0, sizeof(struct file));
	loadFiles(&argc, &argv);
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
			loadFilesAddFile(fileName);
		} while(TRUE);
	}
	/* }}} */
	/* Initialize gtk {{{ */
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
	if(currentFile->fileName == NULL) {
		die("Failed to load any of the images");
	}
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
