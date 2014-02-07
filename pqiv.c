/**
 * pqiv
 *
 * Copyright (c) 2013, Phillip Berndt
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#define _XOPEN_SOURCE 600

#define PQIV_VERSION "2.1.1"

#include "lib/strnatcmp.h"
#include "lib/bostree.h"
#include <cairo/cairo.h>
#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef _WIN32
	#ifndef _WIN32_WINNT
		#define _WIN32_WINNT 0x500
	#else
		#if _WIN32_WINNT < 0x800
			#ifdef __MINGW32__
				#pragma message "Microsoft Windows supported is limited to Windows 2000 and higher, but your mingw version indicates that it does not  support those versions. Building might fail."
			#else
				#error Microsoft Windows supported is limited to Windows 2000 and higher.
			#endif
		#endif
	#endif
	#include <windows.h>
#else
	#include <sys/wait.h>
	#include <gdk/gdkx.h>

	#if GTK_MAJOR_VERSION < 3
		#include <X11/Xatom.h>
	#endif
#endif

// GTK 2 does not define keyboard aliases the way we do
#if GTK_MAJOR_VERSION < 3 // {{{

#define GDK_BUTTON_PRIMARY 1
#define GDK_BUTTON_MIDDLE 2
#define GDK_BUTTON_SECONDARY 3

#define GDK_KEY_Escape 0xff1b
#define GDK_KEY_Return 0xff0d
#define GDK_KEY_Left 0xff51
#define GDK_KEY_Up 0xff52
#define GDK_KEY_Right 0xff53
#define GDK_KEY_Down 0xff54
#define GDK_KEY_KP_Left 0xff96
#define GDK_KEY_KP_Up 0xff97
#define GDK_KEY_KP_Right 0xff98
#define GDK_KEY_KP_Down 0xff99
#define GDK_KEY_plus 0x02b
#define GDK_KEY_KP_Add 0xffab
#define GDK_KEY_KP_Subtract 0xffad
#define GDK_KEY_minus 0x02d
#define GDK_KEY_space 0x020
#define GDK_KEY_KP_Page_Up 0xff9a
#define GDK_KEY_Page_Up 0xff55
#define GDK_KEY_BackSpace 0xff08
#define GDK_KEY_Page_Down 0xff56
#define GDK_KEY_KP_Page_Down 0xff9b
#define GDK_KEY_0 0x030
#define GDK_KEY_1 0x031
#define GDK_KEY_2 0x032
#define GDK_KEY_3 0x033
#define GDK_KEY_4 0x034
#define GDK_KEY_5 0x035
#define GDK_KEY_6 0x036
#define GDK_KEY_7 0x037
#define GDK_KEY_8 0x038
#define GDK_KEY_9 0x039
#define GDK_KEY_A 0x041
#define GDK_KEY_B 0x042
#define GDK_KEY_C 0x043
#define GDK_KEY_D 0x044
#define GDK_KEY_E 0x045
#define GDK_KEY_F 0x046
#define GDK_KEY_G 0x047
#define GDK_KEY_H 0x048
#define GDK_KEY_I 0x049
#define GDK_KEY_J 0x04a
#define GDK_KEY_K 0x04b
#define GDK_KEY_L 0x04c
#define GDK_KEY_M 0x04d
#define GDK_KEY_N 0x04e
#define GDK_KEY_O 0x04f
#define GDK_KEY_P 0x050
#define GDK_KEY_Q 0x051
#define GDK_KEY_R 0x052
#define GDK_KEY_S 0x053
#define GDK_KEY_T 0x054
#define GDK_KEY_U 0x055
#define GDK_KEY_V 0x056
#define GDK_KEY_W 0x057
#define GDK_KEY_X 0x058
#define GDK_KEY_Y 0x059
#define GDK_KEY_Z 0x05a
#define GDK_KEY_a 0x061
#define GDK_KEY_b 0x062
#define GDK_KEY_c 0x063
#define GDK_KEY_d 0x064
#define GDK_KEY_e 0x065
#define GDK_KEY_f 0x066
#define GDK_KEY_g 0x067
#define GDK_KEY_h 0x068
#define GDK_KEY_i 0x069
#define GDK_KEY_j 0x06a
#define GDK_KEY_k 0x06b
#define GDK_KEY_l 0x06c
#define GDK_KEY_m 0x06d
#define GDK_KEY_n 0x06e
#define GDK_KEY_o 0x06f
#define GDK_KEY_p 0x070
#define GDK_KEY_q 0x071
#define GDK_KEY_r 0x072
#define GDK_KEY_s 0x073
#define GDK_KEY_t 0x074
#define GDK_KEY_u 0x075
#define GDK_KEY_v 0x076
#define GDK_KEY_w 0x077
#define GDK_KEY_x 0x078
#define GDK_KEY_y 0x079
#define GDK_KEY_z 0x07a

#endif // }}}

// Data types and global variables {{{
#define FILE_TYPE_DEFAULT (guint)(0)
#define FILE_TYPE_ANIMATION (guint)(1)
#define FILE_TYPE_MEMORY_IMAGE (guint)(1<<1)

// The structure for images
typedef struct {
	// Type identifier. See the FILE_TYPE_* definitions above
	guint file_type;

	// Either the path to the file or the actual file data
	// if FILE_TYPE_MEMORY_IMAGE is set
	// TODO This is a GNU extension / C11 extension. Action required?
	union {
		gchar *file_name;
		guchar *file_data;
	};

	// The length of the data if this is a memory image
	gsize file_data_length;

	// The surface where the image is stored. Only non-NULL for
	// the current, previous and next image.
	cairo_surface_t *image_surface;

	// The file monitor structure is used for inotify-watching of
	// the files
	GFileMonitor *file_monitor;

	// If this flag is set, the image is reloaded even though
	// image_surface is non-null. Used by the file monitor
	// and by operations changing the image.
	gboolean surface_is_out_of_date;

	// For file_type & FILE_TYPE_ANIMATION, this stores the
	// whole animation. As with the surface, this is only non-NULL
	// for the current, previous and next image.
	union {
		GdkPixbufAnimation *pixbuf_animation;
	};
} file_t;

// Storage of the file list
BOSTree *file_tree;
BOSTree *directory_tree;
BOSNode *current_file_node = NULL;
BOSNode *image_loader_thread_currently_loading = NULL;

// We asynchroniously load images in a separate thread
GAsyncQueue *image_loader_queue;
GCancellable *image_loader_cancellable;

// Filter for path traversing upon building the file list
GtkFileFilter *load_images_file_filter;
GtkFileFilterInfo *load_images_file_filter_info;
GTimer *load_images_timer;

// Easy access to the file_t within a node
#define FILE(x) ((file_t *)(x)->data)
#define CURRENT_FILE FILE(current_file_node)
BOSNode *next_file() {
	BOSNode *ret = bostree_next_node(current_file_node);
	return ret ? ret : bostree_select(file_tree, 0);
}
BOSNode *previous_file() {
	BOSNode *ret = bostree_previous_node(current_file_node);
	return ret ? ret : bostree_select(file_tree, bostree_node_count(file_tree) - 1);
}

// We sometimes need to decide whether we have to draw the image or if it already
// is. We use this variable for that.
gboolean current_image_drawn = FALSE;

// Variables related to the window, display, etc.
GtkWindow *main_window;
gboolean main_window_visible = FALSE;

gint main_window_width = 10;
gint main_window_height = 10;
gboolean main_window_in_fullscreen = FALSE;
GdkRectangle screen_geometry = { 0, 0, 0, 0 };

cairo_pattern_t *background_checkerboard_pattern = NULL;

gboolean gui_initialized = FALSE;
int global_argc;
char **global_argv;

// Those two surfaces are here to store scaled image versions (to reduce
// processor load) and to store the last visible image to have something to
// display while fading and while the (new) current image has not been loaded
// yet.
cairo_surface_t *last_visible_image_surface = NULL;
cairo_surface_t *current_scaled_image_surface = NULL;

gchar *current_info_text = NULL;


// Current state of the displayed image and user interaction
gdouble current_scale_level = 1.0;
gint current_shift_x = 0;
gint current_shift_y = 0;
guint32 last_button_press_time = 0;
GdkPixbufAnimationIter *current_image_animation_iter = NULL;
guint current_image_animation_timeout_id = 0;

// -1 means no slideshow, 0 means active slideshow but no current timeout
// source set, anything bigger than that actually is a slideshow id.
gint slideshow_timeout_id = -1;

// User options
gchar *external_image_filter_commands[] = {
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL
};

gchar keyboard_aliases[127] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0 };

gint option_scale = 1;
gboolean scale_override = FALSE;
const gchar *option_window_title = "pqiv: $FILENAME ($WIDTHx$HEIGHT) $ZOOM% [$IMAGE_NUMBER/$IMAGE_COUNT]";
guint option_slideshow_interval = 5;
gboolean option_hide_info_box = FALSE;
gboolean option_start_fullscreen = FALSE;
gdouble option_initial_scale = 1.0;
gboolean option_initial_scale_used = FALSE;
gboolean option_start_with_slideshow_mode = FALSE;
gboolean option_sort = FALSE;
gboolean option_shuffle = FALSE;
gboolean option_reverse_cursor_keys = FALSE;
gboolean option_transparent_background = FALSE;
gboolean option_watch_directories = FALSE;
gboolean option_fading = FALSE;
gboolean option_lazy_load = FALSE;
gboolean option_lowmem = FALSE;
gboolean option_addl_from_stdin = FALSE;
double option_fading_duration = .5;

double fading_current_alpha_stage = 0;
gint64 fading_initial_time;

gboolean options_keyboard_alias_set_callback(const gchar *option_name, const gchar *value, gpointer data, GError **error);
gboolean option_window_position_callback(const gchar *option_name, const gchar *value, gpointer data, GError **error);
gboolean option_scale_level_callback(const gchar *option_name, const gchar *value, gpointer data, GError **error);
typedef enum { PARAMETER, RECURSION, INOTIFY } load_images_state_t;
void load_images_handle_parameter(char *param, load_images_state_t state);

struct {
	gint x;
	gint y;
} option_window_position = { -2, -2 };

// Hint: Only types G_OPTION_ARG_NONE, G_OPTION_ARG_STRING, G_OPTION_ARG_DOUBLE/INTEGER and G_OPTION_ARG_CALLBACK are
// implemented for option parsing.
GOptionEntry options[] = {
	{ "keyboard-alias", 'a', 0, G_OPTION_ARG_CALLBACK, (gpointer)&options_keyboard_alias_set_callback, "Define n as a keyboard alias for f", "nfnf.." },
	{ "transparent-background", 'c', 0, G_OPTION_ARG_NONE, &option_transparent_background, "Borderless transparent window", NULL },
	{ "slideshow-interval", 'd', 0, G_OPTION_ARG_INT, &option_slideshow_interval, "Set slideshow interval", "n" },
	{ "fullscreen", 'f', 0, G_OPTION_ARG_NONE, &option_start_fullscreen, "Start in fullscreen mode", NULL },
	{ "fade", 'F', 0, G_OPTION_ARG_NONE, (gpointer)&option_fading, "Fade between images", NULL },
	{ "hide-info-box", 'i', 0, G_OPTION_ARG_NONE, &option_hide_info_box, "Initially hide the info box", NULL },
	{ "lazy-load", 'l', 0, G_OPTION_ARG_NONE, &option_lazy_load, "Display the main window as soon as possible", NULL },
	{ "sort", 'n', 0, G_OPTION_ARG_NONE, &option_sort, "Sort files in natural order", NULL },
	{ "window-position", 'P', 0, G_OPTION_ARG_CALLBACK, (gpointer)&option_window_position_callback, "Set initial window position (`x,y' or `off' to not position the window at all)", "POSITION" },
	{ "reverse-cursor-keys", 'R', 0, G_OPTION_ARG_NONE, &option_reverse_cursor_keys, "Reverse the meaning of the cursor keys", NULL },
	{ "additional-from-stdin", 'r', 0, G_OPTION_ARG_NONE, &option_addl_from_stdin, "Read additional filenames/folders from stdin", NULL },
	{ "slideshow", 's', 0, G_OPTION_ARG_NONE, &option_start_with_slideshow_mode, "Activate slideshow mode", NULL },
	{ "scale-images-up", 't', G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, (gpointer)&option_scale_level_callback, "Scale images up to fill the whole screen", NULL },
	{ "window-title", 'T', 0, G_OPTION_ARG_STRING, &option_window_title, "Set the title of the window. See manpage for available variables.", "TITLE" },
	{ "zoom-level", 'z', 0, G_OPTION_ARG_DOUBLE, &option_initial_scale, "Set initial zoom level (1.0 is 100%)", "FLOAT" },

	{ "command-1", '1', 0, G_OPTION_ARG_STRING, &external_image_filter_commands[0], "Bind the external COMMAND to key 1. See manpage for extended usage (commands starting with `>' or `|'). Use 2..9 for further commands.", "COMMAND" },
	{ "command-2", '2', G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING, &external_image_filter_commands[1], NULL, NULL },
	{ "command-3", '3', G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING, &external_image_filter_commands[2], NULL, NULL },
	{ "command-4", '4', G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING, &external_image_filter_commands[3], NULL, NULL },
	{ "command-5", '5', G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING, &external_image_filter_commands[4], NULL, NULL },
	{ "command-6", '6', G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING, &external_image_filter_commands[5], NULL, NULL },
	{ "command-7", '7', G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING, &external_image_filter_commands[6], NULL, NULL },
	{ "command-8", '8', G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING, &external_image_filter_commands[7], NULL, NULL },
	{ "command-9", '9', G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING, &external_image_filter_commands[8], NULL, NULL },

	{ "disable-scaling", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, (gpointer)&option_scale_level_callback, "Disable scaling of images", NULL },
	{ "fade-duration", 0, 0, G_OPTION_ARG_DOUBLE, &option_fading_duration, "Adjust fades' duration", "SECONDS" },
	{ "low-memory", 0, 0, G_OPTION_ARG_NONE, &option_lowmem, "Try to keep memory usage to a minimum", NULL },
	{ "shuffle", 0, 0, G_OPTION_ARG_NONE, &option_shuffle, "Shuffle files", NULL },
	{ "watch-directories", 0, 0, G_OPTION_ARG_NONE, &option_watch_directories, "Watch directories for new files", NULL },

	{ NULL, 0, 0, 0, NULL, NULL, NULL }
};

const char *long_description_text = ("Keyboard & Mouse bindings:\n"
"  Backspace, Button 3, Scroll        Previous image\n"
"  PgUp/PgDown                        Jump 10 images forwards/backwards\n"
"  Escape, q, Button 2                Quit\n"
"  Cursor keys, Drag & Drop           Move\n"
"  Space, Button 1, Scroll            Next image\n"
"  f                                  Toggle fullscreen\n"
"  j                                  Jump to an image (Shows a selection box)\n"
"  r                                  Reload image\n"
"  +/-/0, Button 3 & Drag             Zoom in/out/reset zoom\n"
"  t                                  Toggle autoscale\n"
"  l/k                                Rotate left/right\n"
"  h/v                                Flip horizontally/vertically\n"
"  i                                  Toggle info box\n"
"  s                                  Toggle slideshow mode\n"
"  ctrl +/-                           Change slideshow interval\n"
"  a                                  Hardlink current image to ./.pqiv-select\n"
);


void set_scale_level_to_fit();
void update_info_text(const char *);
void queue_draw();
gboolean main_window_center();
void window_screen_changed_callback(GtkWidget *widget, GdkScreen *previous_screen, gpointer user_data);
gboolean fading_timeout_callback(gpointer user_data);
void queue_image_load(BOSNode *);
void unload_image(BOSNode *);
gboolean initialize_gui_callback(gpointer);
// }}}
/* Command line handling, creation of the image list {{{ */
gboolean options_keyboard_alias_set_callback(const gchar *option_name, const gchar *value, gpointer data, GError **error) {/*{{{*/
	if(strlen(value) % 2 != 0) {
		g_set_error(error, G_OPTION_ERROR, G_OPTION_ERROR_FAILED, "The argument to the alias option must have a multiple of two characters: Every odd one is mapped to the even one following it.");
		return FALSE;
	}

	for(size_t i=0; value[i] != 0; i+=2) {
		keyboard_aliases[(size_t)value[i]] = value[i+1];
	}

	return TRUE;
}/*}}}*/
gboolean option_window_position_callback(const gchar *option_name, const gchar *value, gpointer data, GError **error) {/*{{{*/
	if(strcmp(value, "off") == 0) {
		option_window_position.x = option_window_position.y = -1;
		return TRUE;
	}

	gchar *second;
	option_window_position.x = g_ascii_strtoll(value, &second, 10);
	if(second != value && *second == ',') {
		option_window_position.y = g_ascii_strtoll(second + 1, &second, 10);
		if(*second == 0) {
			return TRUE;
		}
	}

	g_set_error(error, G_OPTION_ERROR, G_OPTION_ERROR_FAILED, "Unexpected argument value for the -P option. Allowed formats are: `x,y' and `off'.");
	return FALSE;
}/*}}}*/
gboolean option_scale_level_callback(const gchar *option_name, const gchar *value, gpointer data, GError **error) {/*{{{*/
	if(g_strcmp0(option_name, "-t") == 0 || g_strcmp0(option_name, "--scale-images-up") == 0) {
		option_scale = 2;
	}
	else {
		option_scale = 0;
	}
	return TRUE;
}/*}}}*/
void parse_configuration_file(int *argc, char **argv[]) {/*{{{*/
	// Check for a configuration file
	gchar *config_file_name = g_build_filename(g_getenv("HOME"), ".pqivrc", NULL);
	if(!g_file_test(config_file_name, G_FILE_TEST_EXISTS)) {
		g_free(config_file_name);
		return;
	}

	// Load it
	GError *error_pointer = NULL;
	GKeyFile *key_file = g_key_file_new();

	if(!g_key_file_load_from_file(key_file, config_file_name, G_KEY_FILE_NONE, &error_pointer)) {
		g_key_file_free(key_file);

		// Backwards compatibility: Recognize the old configuration file format
		if(error_pointer->code == G_KEY_FILE_ERROR_PARSE) {
			gchar *options_contents;
			// We deliberately do not free options_contents below: Its contents are used as the
			// new values for argv.
			g_file_get_contents(config_file_name, &options_contents, NULL, NULL);
			if(options_contents == NULL) {
				g_clear_error(&error_pointer);
				g_free(config_file_name);
				return;
			}

			gint additional_arguments = 0;
			gint additional_arguments_max = 10;

			// Add configuration file contents to argument vector
			char **new_argv = (char **)g_malloc(sizeof(char *) * (*argc + additional_arguments_max + 1));
			new_argv[0] = (*argv)[0];
			char *end_of_argument;
			while(*options_contents != 0) {
				end_of_argument = strchr(options_contents, ' ');
				if(end_of_argument != NULL) {
					*end_of_argument = 0;
				}
				else {
					end_of_argument = options_contents + strlen(options_contents) - 1;
				}
				gchar *argv_val = options_contents;
				g_strstrip(argv_val);

				// Try to directly parse boolean options, to reverse their
				// meaning on the command line
				if(argv_val[0] == '-') {
					gboolean direct_parsing_successfull = FALSE;
					if(argv_val[1] == '-') {
						// Long option
						for(GOptionEntry *iter = options; iter->description != NULL; iter++) {
							if(iter->long_name != NULL && iter->arg == G_OPTION_ARG_NONE && g_strcmp0(iter->long_name, argv_val + 2) == 0) {
								*(gboolean *)(iter->arg_data) = TRUE;
								iter->flags |= G_OPTION_FLAG_REVERSE;
								direct_parsing_successfull = TRUE;
								break;
							}
						}
					}
					else {
						// Short option
						direct_parsing_successfull = TRUE;
						for(char *arg = argv_val + 1; *arg != 0; arg++) {
							gboolean found = FALSE;
							for(GOptionEntry *iter = options; iter->description != NULL && direct_parsing_successfull; iter++) {
								if(iter->short_name == *arg) {
									found = TRUE;
									if(iter->arg == G_OPTION_ARG_NONE) {
										*(gboolean *)(iter->arg_data) = TRUE;
										iter->flags |= G_OPTION_FLAG_REVERSE;
									}
									else {
										direct_parsing_successfull = FALSE;

										// We only want the remainder of the option to be
										// appended to the argument vector.
										*(arg - 1) = '-';
										argv_val = arg - 1;
									}
									break;
								}
							}
							if(!found) {
								g_printerr("Failed to parse the configuration file: Unknown option `%c'\n", *arg);
								direct_parsing_successfull = FALSE;
							}
						}
					}
					if(direct_parsing_successfull) {
						options_contents = end_of_argument + 1;
						continue;
					}
				}

				// Add to argument vector
				new_argv[1 + additional_arguments] = argv_val;
				options_contents = end_of_argument + 1;
				if(++additional_arguments > additional_arguments_max) {
					additional_arguments_max += 5;
					new_argv = g_realloc(new_argv, sizeof(char *) * (*argc + additional_arguments_max + 1));
				}
			}
			if(*options_contents != 0) {
				new_argv[additional_arguments + 1] = g_strstrip(options_contents);
				additional_arguments++;
			}

			// Add the real argument vector and make new_argv the new argv
			new_argv = g_realloc(new_argv, sizeof(char *) * (*argc + additional_arguments + 1));
			for(int i=1; i<*argc; i++) {
				new_argv[i + additional_arguments] = (*argv)[i];
			}
			new_argv[*argc + additional_arguments] = NULL;
			*argv = new_argv;
			*argc = *argc + additional_arguments;

			g_clear_error(&error_pointer);
			g_free(config_file_name);
			return;
		}

		g_printerr("Failed to load configuration file: %s\n", error_pointer->message);
		g_free(config_file_name);
		g_clear_error(&error_pointer);
		return;
	}

	for(GOptionEntry *iter = options; iter->description != NULL; iter++) {
		if(iter->long_name != NULL) {
			switch(iter->arg) {
				case G_OPTION_ARG_NONE: {
					*(gboolean *)(iter->arg_data) = g_key_file_get_boolean(key_file, "options", iter->long_name, &error_pointer);
					if(*(gboolean *)(iter->arg_data)) {
						iter->flags |= G_OPTION_FLAG_REVERSE;
					}
				} break;
				case G_OPTION_ARG_CALLBACK:
				case G_OPTION_ARG_STRING: {
					gchar *option_value = g_key_file_get_string(key_file, "options", iter->long_name, NULL);
					if(option_value != NULL) {
						if(iter->arg == G_OPTION_ARG_CALLBACK) {
							(*(GOptionArgFunc *)(iter->arg_data))(NULL, option_value, NULL, &error_pointer);
						}
						else {
							*(gchar **)(iter->arg_data) = option_value;
						}
					}
				} break;
				case G_OPTION_ARG_INT: {
					gint option_value = g_key_file_get_integer(key_file, "options", iter->long_name, &error_pointer);
					if(error_pointer == NULL) {
						*(gint *)(iter->arg_data) = option_value;
					}
				} break;
				case G_OPTION_ARG_DOUBLE: {
					gdouble option_value = g_key_file_get_double(key_file, "options", iter->long_name, &error_pointer);
					if(error_pointer == NULL) {
						*(gdouble *)(iter->arg_data) = option_value;
					}
				} break;
				default:
					// Unimplemented. See options array.
				break;
			}

			if(error_pointer != NULL) {
				if(error_pointer->code == G_KEY_FILE_ERROR_INVALID_VALUE) {
					g_printerr("Failed to load setting for `%s' from configuration file: %s\n", iter->long_name, error_pointer->message);
				}
				g_clear_error(&error_pointer);
			}
		}
	}

	g_free(config_file_name);
	g_key_file_free(key_file);

}/*}}}*/
void parse_command_line(int *argc, char *argv[]) {/*{{{*/
	GOptionContext *parser = g_option_context_new("FILES");
	g_option_context_set_summary(parser, "A minimalist image viewer\npqiv version " PQIV_VERSION " by Phillip Berndt");
	g_option_context_set_description(parser, long_description_text);
	g_option_context_set_help_enabled(parser, TRUE);
	g_option_context_set_ignore_unknown_options(parser, FALSE);
	g_option_context_add_main_entries(parser, options, NULL);
	g_option_context_add_group(parser, gtk_get_option_group(TRUE));

	GError *error_pointer = NULL;
	if(g_option_context_parse(parser, argc, &argv, &error_pointer) == FALSE) {
		g_printerr("%s\n", error_pointer->message);
		exit(1);
	}

	g_option_context_free(parser);
}/*}}}*/
void load_images_directory_watch_callback(GFileMonitor *monitor, GFile *file, GFile *other_file, GFileMonitorEvent event_type, gpointer user_data) {/*{{{*/
	// The current image holds its own file watch, so we do not have to react
	// to changes.
	if(event_type == G_FILE_MONITOR_EVENT_CREATED) {
		gchar *name = g_file_get_path(file);
		if(name != NULL) {
			// In theory, handling regular files here should suffice. But files in subdirectories
			// seem not always to be recognized correctly by file monitors, so we have to install
			// one for each directory.
			if(g_file_test(name, G_FILE_TEST_IS_REGULAR | G_FILE_TEST_IS_SYMLINK | G_FILE_TEST_IS_DIR)) {
				// Use the standard loading mechanism. If directory watches are enabled,
				// the temporary variables used therein are not freed.
				load_images_handle_parameter(name, INOTIFY);
			}
			g_free(name);
		}
	}
	else if(option_sort && event_type == G_FILE_MONITOR_EVENT_DELETED) {
		// We can react on delete events only if the file tree is sorted. If it is not,
		// the effort to search the node to delete would be too high. The node will be
		// deleted once the user tries to access it.
		gchar *name = g_file_get_path(file);
		BOSNode *node = bostree_lookup(file_tree, name);
		if(node != NULL && node != current_file_node) {
			unload_image(node);
			g_free(node->key);
			g_free(node->data);
			bostree_remove(file_tree, node);
		}
		g_free(name);
	}
}/*}}}*/
void load_images_handle_parameter(char *param, load_images_state_t state) {/*{{{*/
	file_t *file;

	// Check for memory image
	if(state == PARAMETER && g_strcmp0(param, "-") == 0) {
		file = g_new0(file_t, 1);
		file->file_type = FILE_TYPE_MEMORY_IMAGE;
		file->file_name = g_strdup("-");

		GError *error_ptr = NULL;
		GIOChannel *stdin_channel = g_io_channel_unix_new(0);
		g_io_channel_set_encoding(stdin_channel, NULL, NULL);
		if(g_io_channel_read_to_end(stdin_channel, (gchar **)&file->file_data, &file->file_data_length, &error_ptr) != G_IO_STATUS_NORMAL) {
			g_printerr("Failed to load image from stdin: %s\n", error_ptr->message);
			g_clear_error(&error_ptr);
			g_free(file);
			g_io_channel_unref(stdin_channel);
			return;
		}
		g_io_channel_unref(stdin_channel);

		// We will add the file to the file tree below
	}
	else {
		// Recurse into directories
		if(g_file_test(param, G_FILE_TEST_IS_DIR) == TRUE) {
			// Check for recursion
			char abs_path[PATH_MAX];
			if(
				#ifdef _WIN32
					GetFullPathNameA(param, PATH_MAX, abs_path, NULL) != 0
				#else
					realpath(param, abs_path) != NULL
				#endif
			) {
				if(bostree_lookup(directory_tree, abs_path) != NULL) {
					return;
				}
				bostree_insert(directory_tree, g_strdup(abs_path), NULL);
			}
			else {
				// Consider this an error
				g_printerr("Probably too many level of symlinks. Will not traverse into: %s\n", param);
				return;
			}

			// Display progress
			if(g_timer_elapsed(load_images_timer, NULL) > 5.) {
				#ifdef _WIN32
				g_print("Loading in %-50.50s ...\r", param);
				#else
				g_print("\033[s\033[JLoading in %s ...\033[u", param);
				#endif
			}

			GDir *dir_ptr = g_dir_open(param, 0, NULL);
			if(dir_ptr == NULL) {
				if(state == PARAMETER) {
					g_printerr("Failed to open directory: %s\n", param);
				}
				return;
			}
			while(TRUE) {
				const gchar *dir_entry = g_dir_read_name(dir_ptr);
				if(dir_entry == NULL) {
					break;
				}
				gchar *dir_entry_full = g_strdup_printf("%s%s%s", param, g_str_has_suffix(param, G_DIR_SEPARATOR_S) ? "" : G_DIR_SEPARATOR_S, dir_entry);
				if(dir_entry_full == NULL) {
					g_printerr("Failed to allocate memory for file name loading\n");
					g_dir_close(dir_ptr);
					return;
				}
				load_images_handle_parameter(dir_entry_full, RECURSION);
				g_free(dir_entry_full);
			}
			g_dir_close(dir_ptr);

			// Add a watch for new files in this directory
			if(option_watch_directories) {
				// Note: It does not suffice to do this once for each parameter, but this must also be
				// called for subdirectories. At least if it is not, new files in subdirectories are
				// not always recognized.
				GFile *file_ptr = g_file_new_for_path(param);
				GFileMonitor *directory_monitor = g_file_monitor_directory(file_ptr, G_FILE_MONITOR_NONE, NULL, NULL);
				if(directory_monitor != NULL) {
					g_signal_connect(directory_monitor, "changed", G_CALLBACK(load_images_directory_watch_callback), NULL);
					// We do not store the directory_monitor anywhere, because it is not used explicitly
					// again. If this should ever be needed, this is the place where this should be done.
				}
				g_object_unref(file_ptr);
			}

			return;
		}

		// Filter based on formats supported by GdkPixbuf
		if(state != PARAMETER) {
			gchar *param_lowerc = g_utf8_strdown(param, -1);
			load_images_file_filter_info->filename = load_images_file_filter_info->display_name = param_lowerc;
			if(gtk_file_filter_filter(load_images_file_filter, load_images_file_filter_info) == FALSE) {
				g_free(param_lowerc);
				return;
			}
			g_free(param_lowerc);
		}

		// Prepare file structure
		file = g_new0(file_t, 1);
		file->file_name = g_strdup(param);
		if(file->file_name == NULL) {
			g_free(file);
			g_printerr("Failed to allocate memory for file name loading\n");
			return;
		}
	}

	// Add image to images list/tree
	// We need to check if the previous/next images have changed, because they
	// might have been preloaded and need unloading if so.
	BOSNode *old_prev = NULL;
	BOSNode *old_next = NULL;
	if((option_lazy_load || state == INOTIFY) && current_file_node != NULL) {
		old_prev = previous_file();
		old_next = next_file();
	}
	if(!option_sort) {
		unsigned int *index = (unsigned int *)g_malloc(sizeof(unsigned int));
		*index = option_shuffle ? g_random_int() : bostree_node_count(file_tree);
		bostree_insert(file_tree, (void *)index, file);
	}
	else {
		bostree_insert(file_tree, file->file_name, file);
	}
	if((option_lazy_load || state == INOTIFY) && current_file_node != NULL) {
		// If the previous/next images have shifted, unload the old ones to save memory
		if(previous_file() != old_prev && current_file_node != old_prev && old_prev != NULL) {
			unload_image(old_prev);
		}
		if(next_file() != old_next && current_file_node != old_next && old_next != NULL) {
			unload_image(old_next);
		}
	}
	if(option_lazy_load && !gui_initialized) {
		// When the first image has been processed, we can show the window
		g_idle_add(initialize_gui_callback, NULL);
		gui_initialized = TRUE;
	}
}/*}}}*/
int image_tree_integer_compare(const unsigned int *a, const unsigned int *b) {/*{{{*/
	return *a > *b;
}/*}}}*/
void load_images(int *argc, char *argv[]) {/*{{{*/
	// Allocate memory for the file list (Used for unsorted and random order file lists)
	file_tree = bostree_new(
		option_sort ? (BOSTree_cmp_function)strnatcasecmp : (BOSTree_cmp_function)image_tree_integer_compare
	);

	// The directory tree is used to prevent nested-symlink loops
	directory_tree = bostree_new((BOSTree_cmp_function)g_strcmp0);

	// Allocate memory for the timer
	load_images_timer = g_timer_new();
	g_timer_start(load_images_timer);

	// Create a GTK filter for image file names
	load_images_file_filter = gtk_file_filter_new();
	gtk_file_filter_add_pixbuf_formats(load_images_file_filter);
	GSList *file_formats_iterator = gdk_pixbuf_get_formats();
	do {
			gchar **file_format_extensions_iterator = gdk_pixbuf_format_get_extensions(file_formats_iterator->data);
			while(*file_format_extensions_iterator != NULL) {
					gchar *extn = g_strdup_printf("*.%s", *file_format_extensions_iterator);
					gtk_file_filter_add_pattern(load_images_file_filter, extn);
					g_free(extn);
					++file_format_extensions_iterator;
			}
	} while((file_formats_iterator = g_slist_next(file_formats_iterator)) != NULL);
	g_slist_free(file_formats_iterator);

	load_images_file_filter_info = g_new0(GtkFileFilterInfo, 1);
	load_images_file_filter_info->contains = GTK_FILE_FILTER_FILENAME | GTK_FILE_FILTER_DISPLAY_NAME;

	// Load the images from the remaining parameters
	for(int i=1; i<*argc; i++) {
		load_images_handle_parameter(argv[i], PARAMETER);
	}

	if(option_addl_from_stdin) {
		GIOChannel *stdin_reader =
		#ifdef _WIN32
			g_io_channel_win32_new_fd(_fileno(stdin));
		#else
			g_io_channel_unix_new(STDIN_FILENO);
		#endif

		gsize line_terminator_pos;
		gchar *buffer = NULL;
		const gchar *charset = NULL;
		if(g_get_charset(&charset)) {
			g_io_channel_set_encoding(stdin_reader, charset, NULL);
		}

		while(g_io_channel_read_line(stdin_reader, &buffer, NULL, &line_terminator_pos, NULL) == G_IO_STATUS_NORMAL) {
			if (buffer == NULL) {
				continue;
			}

			buffer[line_terminator_pos] = 0;
			load_images_handle_parameter(buffer, PARAMETER);
			g_free(buffer);
		}
		g_io_channel_unref(stdin_reader);
	}

	// If we do not want to watch directories for changes, we can now drop the variables
	// we used for loading to free some space
	if(!option_watch_directories) {
		g_object_ref_sink(load_images_file_filter);
		g_free(load_images_file_filter_info);
		// Drop the directory tree
		for(BOSNode *node = bostree_select(directory_tree, 0); node; node = bostree_next_node(node)) {
			free(node->key);
		}
		bostree_destroy(directory_tree);
	}

	g_timer_destroy(load_images_timer);
}/*}}}*/
// }}}
/* (A-)synchronous image loading and image operations {{{ */
void invalidate_current_scaled_image_surface() {/*{{{*/
	if(current_scaled_image_surface != NULL) {
		cairo_surface_destroy(current_scaled_image_surface);
		current_scaled_image_surface = NULL;
	}
}/*}}}*/
gboolean image_animation_timeout_callback(gpointer user_data) {/*{{{*/
	if((BOSNode *)user_data != current_file_node) {
		return FALSE;
	}

	gdk_pixbuf_animation_iter_advance(current_image_animation_iter, NULL);
	GdkPixbuf *pixbuf = gdk_pixbuf_animation_iter_get_pixbuf(current_image_animation_iter);

	cairo_t *sf_cr = cairo_create(CURRENT_FILE->image_surface);
	cairo_save(sf_cr);
	cairo_set_source_rgba(sf_cr, 0., 0., 0., 0.);
	cairo_set_operator(sf_cr, CAIRO_OPERATOR_SOURCE);
	cairo_paint(sf_cr);
	cairo_restore(sf_cr);
	gdk_cairo_set_source_pixbuf(sf_cr, pixbuf, 0, 0);
	cairo_paint(sf_cr);
	cairo_destroy(sf_cr);

	current_image_animation_timeout_id = g_timeout_add(
		gdk_pixbuf_animation_iter_get_delay_time(current_image_animation_iter),
		image_animation_timeout_callback,
		user_data);

	invalidate_current_scaled_image_surface();
	gtk_widget_queue_draw(GTK_WIDGET(main_window));

	return FALSE;
}/*}}}*/
void image_file_updated_callback(GFileMonitor *monitor, GFile *file, GFile *other_file, GFileMonitorEvent event_type, gpointer user_data) {/*{{{*/
	BOSNode *node = (BOSNode *)user_data;

	if(event_type == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT) {
		((file_t *)node->data)->surface_is_out_of_date = TRUE;
		queue_image_load(node);
	}
}/*}}}*/
gboolean window_move_helper_callback(gpointer user_data) {/*{{{*/
	gtk_window_move(main_window, option_window_position.x, option_window_position.y);
	option_window_position.x = -1;
	return FALSE;
}/*}}}*/
gboolean set_option_initial_scale_used_callback(gpointer user_data) {/*{{{*/
	option_initial_scale_used = TRUE;
	return FALSE;
}/*}}}*/
gboolean main_window_resize_callback(gpointer user_data) {/*{{{*/
	// If there is no image loaded, abort
	if(CURRENT_FILE->image_surface == NULL) {
		return FALSE;
	}

	// In in fullscreen, also abort
	if(main_window_in_fullscreen) {
		return FALSE;
	}

	// Recalculate the required window size
	int image_width = cairo_image_surface_get_width(CURRENT_FILE->image_surface);
	int image_height = cairo_image_surface_get_height(CURRENT_FILE->image_surface);
	int new_window_width = current_scale_level * image_width;
	int new_window_height = current_scale_level * image_height;

	// Resize if this has not worked before, but accept a slight deviation (might be round-off error)
	if(main_window_width >= 0 && abs(main_window_width - new_window_width) + abs(main_window_height - new_window_height) > 1) {
		gtk_window_resize(main_window, new_window_width, new_window_height);
	}

	return FALSE;
}/*}}}*/
gboolean image_loaded_handler(gconstpointer info_text) {/*{{{*/
	// Remove any old timeouts etc.
	if(current_image_animation_iter != NULL) {
		g_object_unref(current_image_animation_iter);
		current_image_animation_iter = NULL;
		g_source_remove(current_image_animation_timeout_id);
	}

	// Sometimes when a user is hitting the next image button really fast this
	// function's execution can be delayed until CURRENT_FILE is again not loaded.
	// Return without doing anything in that case.
	if(CURRENT_FILE->image_surface == NULL) {
		return FALSE;
	}

	// Update geometry hints, calculate initial window size and place window
	int image_width = cairo_image_surface_get_width(CURRENT_FILE->image_surface);
	int image_height = cairo_image_surface_get_height(CURRENT_FILE->image_surface);
	int screen_width = screen_geometry.width;
	int screen_height = screen_geometry.height;

	// Reset shift
	current_shift_x = 0;
	current_shift_y = 0;

	if(!main_window_in_fullscreen) {
		// If not in fullscreen, resize the window such that the image will be
		// scaled ideally

		if(!current_image_drawn) {
			scale_override = FALSE;
		}

		current_scale_level = 1.0;
		if(option_initial_scale_used == FALSE) {
			current_scale_level = option_initial_scale;
		}
		else {
			if(option_scale > 1 || scale_override) {
				// Scale up to 80% screen size
				current_scale_level = screen_width * .8 / image_width;
			}
			else if(option_scale == 1 && image_width > screen_width * .8) {
				// Scale down to 80% screen size
				current_scale_level = screen_width * .8 / image_width;
			}
			// In both cases: If the height exceeds 80% screen size, scale
			// down
			if(option_scale > 0 && image_height * current_scale_level > screen_height * .8) {
				current_scale_level = screen_height * .8 / image_height;
			}
		}

		// Resize the window and update geometry hints (we enforce them below)
		int new_window_width = current_scale_level * image_width;
		int new_window_height = current_scale_level * image_height;
		GdkGeometry hints;
		hints.min_aspect = hints.max_aspect = new_window_width * 1.0 / new_window_height;
		if(main_window_width >= 0 && (main_window_width != new_window_width || main_window_height != new_window_height)) {
			if(option_window_position.x >= 0) {
				// This is upon startup. Do not attempt to move the window
				// directly to the startup position, this won't work. WMs don't
				// like being told what to do.. ;-) Wait till the window is visible,
				// then move it away.
				g_idle_add(window_move_helper_callback, NULL);
			}
			else if(option_window_position.x != -1) {
				// Tell the WM to center the window
				gtk_window_set_position(main_window, GTK_WIN_POS_CENTER_ALWAYS);
			}
			if(!main_window_visible) {
				gtk_window_set_default_size(main_window, new_window_width, new_window_height);
				gtk_window_set_geometry_hints(main_window, NULL, &hints, GDK_HINT_ASPECT);
				main_window_width = new_window_width;
				main_window_height = new_window_height;

				// Some window managers create a race here upon application startup:
				// They resize, as requested above, and afterwards apply their idea of
				// window size. To conquer that, we check for the window size again once
				// all events are handled.
				g_idle_add(main_window_resize_callback, NULL);
			}
			else {
				gtk_window_set_geometry_hints(main_window, NULL, &hints, GDK_HINT_ASPECT);
				gtk_window_resize(main_window, new_window_width, new_window_height);
			}

			// In theory, we do not need to draw manually here. The resizing will
			// trigger a configure event, which will in particular redraw the
			// window. But this does not work for tiling WMs. As _NET_WM_ACTION_RESIZE
			// is not completely reliable either, we do queue for redraw at the
			// cost of a double redraw.
			queue_draw();
		}
		else {
			// else: No configure event here, but that's fine: current_scale_level already
			// has the correct value
			queue_draw();
		}
	}
	else {
		// In fullscreen, things are different. When an image is loaded, we need to
		// recalculate the scale level. We do that here if the user has not specified
		// an override
		if(option_initial_scale_used) {
			set_scale_level_to_fit();
			queue_draw();
		}
	}
	invalidate_current_scaled_image_surface();

	// Show window, if not visible yet
	if(!main_window_visible) {
		main_window_visible = TRUE;
		gtk_widget_show_all(GTK_WIDGET(main_window));
	}

	// Reset the info text
	update_info_text(info_text);

	// Initialize animation timer if the image is animated
	if((CURRENT_FILE->file_type & FILE_TYPE_ANIMATION) != 0) {
		current_image_animation_iter = gdk_pixbuf_animation_get_iter(CURRENT_FILE->pixbuf_animation, NULL);
		current_image_animation_timeout_id = g_timeout_add(
			gdk_pixbuf_animation_iter_get_delay_time(current_image_animation_iter),
			image_animation_timeout_callback,
			(gpointer)current_file_node);
	}

	return FALSE;
}/*}}}*/
gboolean image_loader_load_single_destroy_old_image_callback(gpointer old_surface) {/*{{{*/
	cairo_surface_destroy((cairo_surface_t *)old_surface);
	return FALSE;
}/*}}}*/
gboolean image_loader_load_single_destroy_invalid_image_callback(gpointer node_p) {/*{{{*/
	BOSNode *node = (BOSNode *)node_p;
	unload_image(node);
	if(node == current_file_node) {
		current_file_node = next_file();
		queue_image_load(current_file_node);
	}
	bostree_remove(file_tree, node);
	g_free(node->key);
	g_free(node->data);
	if(bostree_node_count(file_tree) == 0) {
		g_printerr("No images left to display.\n");
		if(gtk_main_level() == 0) {
			exit(1);
		}
		gtk_main_quit();
	}
	return FALSE;
}
gboolean image_loader_load_single(BOSNode *node, gboolean called_from_main) {/*{{{*/
	// Already loaded?
	file_t *file = (file_t *)node->data;
	if(file->image_surface != NULL && !file->surface_is_out_of_date) {
		return TRUE;
	}

	GError *error_pointer = NULL;
	GdkPixbufAnimation *pixbuf_animation = NULL;

	if((file->file_type & FILE_TYPE_MEMORY_IMAGE) != 0) {
		GdkPixbufLoader *loader = gdk_pixbuf_loader_new();
		if(gdk_pixbuf_loader_write(loader, file->file_data, file->file_data_length, &error_pointer)) {
			gdk_pixbuf_loader_close(loader, &error_pointer);
			pixbuf_animation = gdk_pixbuf_loader_get_animation(loader);
		}
		if(pixbuf_animation == NULL) {
			g_printerr("Failed to load file from memory: %s\n", error_pointer->message);
			g_clear_error(&error_pointer);
		}
		else {
			// Make sure the pixbuf is not deleted throughout this (is unreferenced below)
			g_object_ref(pixbuf_animation);
		}
		g_object_unref(loader);
	}
	else {
		g_cancellable_reset(image_loader_cancellable);
		GFile *input_file;
		// Support for URIs is an extra feature. To prevent breaking compatibility,
		// always prefer existing files over URI interpretation.
		// For example, all files containing a colon cannot be read using the
		// g_file_new_for_commandline_arg command, because they are interpreted
		// as an URI with an unsupported scheme.
		if(g_file_test(file->file_name, G_FILE_TEST_EXISTS)) {
			input_file = g_file_new_for_path(file->file_name);
		}
		else {
			input_file = g_file_new_for_commandline_arg(file->file_name);
		}
		GFileInputStream *input_file_stream = g_file_read(input_file, image_loader_cancellable, &error_pointer);
		if(input_file_stream != NULL) {
			#if (GDK_PIXBUF_MAJOR > 2 || (GDK_PIXBUF_MAJOR == 2 && GDK_PIXBUF_MINOR >= 28))
				pixbuf_animation = gdk_pixbuf_animation_new_from_stream(G_INPUT_STREAM(input_file_stream), image_loader_cancellable, &error_pointer);
			#else
				#define IMAGE_LOADER_BUFFER_SIZE (1024 * 512)

				GdkPixbufLoader *loader = gdk_pixbuf_loader_new();
				guchar *buffer = g_malloc(IMAGE_LOADER_BUFFER_SIZE);
				if(buffer == NULL) {
					g_printerr("Failed to allocate memory to load image\n");
					g_object_unref(loader);
					g_object_unref(input_file_stream);
					g_object_unref(input_file);
					return FALSE;
				}
				while(TRUE) {
					gssize bytes_read = g_input_stream_read(G_INPUT_STREAM(input_file_stream), buffer, IMAGE_LOADER_BUFFER_SIZE, image_loader_cancellable, &error_pointer);
					if(bytes_read == 0) {
						// All OK, finish the image loader
						gdk_pixbuf_loader_close(loader, &error_pointer);
						pixbuf_animation = gdk_pixbuf_loader_get_animation(loader);
						if(pixbuf_animation != NULL) {
							g_object_ref(pixbuf_animation); // see above
						}
						break;
					}
					if(bytes_read == -1) {
						// Error. Handle this below.
						gdk_pixbuf_loader_close(loader, NULL);
						break;
					}
					// In all other cases, write to image loader
					if(!gdk_pixbuf_loader_write(loader, buffer, bytes_read, &error_pointer)) {
						// In case of an error, abort.
						break;
					}
				}
				g_free(buffer);
				g_object_unref(loader);
			#endif
			g_object_unref(input_file_stream);
		}
		g_object_unref(input_file);
		if(pixbuf_animation == NULL) {
			if(error_pointer->code == G_IO_ERROR_CANCELLED) {
				g_clear_error(&error_pointer);
				return FALSE;
			}
			else {
				g_printerr("Failed to open file %s: %s\n", file->file_name, error_pointer->message);
				g_clear_error(&error_pointer);
			}
		}
	}

	if(pixbuf_animation != NULL) {
		if(!gdk_pixbuf_animation_is_static_image(pixbuf_animation)) {
			if(file->pixbuf_animation != NULL) {
				g_object_unref(file->pixbuf_animation);
			}
			file->pixbuf_animation = g_object_ref(pixbuf_animation);
			file->file_type |= FILE_TYPE_ANIMATION;
		}
		else {
			file->file_type &= ~FILE_TYPE_ANIMATION;
		}

		// We apparently do not own this pixbuf!
		GdkPixbuf *pixbuf = gdk_pixbuf_animation_get_static_image(pixbuf_animation);

		if(pixbuf == NULL) {
			if((file->file_type & FILE_TYPE_MEMORY_IMAGE) == 0) {
				g_printerr("Failed to load image %s: %s\n", file->file_name, error_pointer->message);
			}
			else {
				g_printerr("Failed to load image from memory: %s\n", error_pointer->message);
			}
			g_clear_error(&error_pointer);
		}
		else {
			GdkPixbuf *new_pixbuf = gdk_pixbuf_apply_embedded_orientation(pixbuf);
			pixbuf = new_pixbuf;

			cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, gdk_pixbuf_get_width(pixbuf), gdk_pixbuf_get_height(pixbuf));
			cairo_t *sf_cr = cairo_create(surface);
			gdk_cairo_set_source_pixbuf(sf_cr, pixbuf, 0, 0);
			cairo_paint(sf_cr);
			cairo_destroy(sf_cr);

			// This is the only place outside the main thread where image_surface is changed.
			// We do the exchange atomically and use an idle function (which runs in the
			// main thread) to destroy the old one.
			//
			// The alternative would be to explicitly reference the current
			// surface in all functions and unref it afterwards. But we'd still
			// have to check if the image_surface is NULL when the callbacks
			// are invoked because the image could have been unloaded.
			cairo_surface_t *old_surface = file->image_surface;
			file->image_surface = surface;
			if(old_surface != NULL) {
				g_idle_add(image_loader_load_single_destroy_old_image_callback, old_surface);
			}
			g_object_unref(pixbuf);
		}
		g_object_unref(pixbuf_animation);
	}

	file->surface_is_out_of_date = FALSE;

	if(file->image_surface != NULL) {
		if(file->file_type == FILE_TYPE_DEFAULT) {
			GFile *the_file = g_file_new_for_path(file->file_name);
			if(the_file != NULL) {
				file->file_monitor = g_file_monitor_file(the_file, G_FILE_MONITOR_NONE, NULL, NULL);
				if(file->file_monitor != NULL) {
					g_signal_connect(file->file_monitor, "changed", G_CALLBACK(image_file_updated_callback), (gpointer)node);
				}
				g_object_unref(the_file);
			}
		}

		return TRUE;
	}
	else {
		// The node is invalid.  Unload it in the main thread to avoid race
		// conditions with image movement.
		if(called_from_main) {
			image_loader_load_single_destroy_invalid_image_callback(node);
		}
		else {
			g_idle_add(image_loader_load_single_destroy_invalid_image_callback, node);
		}
	}

	return FALSE;
}/*}}}*/
gpointer image_loader_thread(gpointer user_data) {/*{{{*/
	while(TRUE) {
		BOSNode *node = bostree_node_weak_unref(g_async_queue_pop(image_loader_queue));
		if(node == NULL) {
			continue;
		}
		if(FILE(node)->image_surface == NULL || FILE(node)->surface_is_out_of_date) {
			// Load image
			image_loader_thread_currently_loading = node;
			image_loader_load_single(node, FALSE);
			image_loader_thread_currently_loading = NULL;
		}

		if(node == current_file_node && FILE(node)->image_surface != NULL) {
			current_image_drawn = FALSE;
			g_idle_add((GSourceFunc)image_loaded_handler, NULL);
		}
	}
}/*}}}*/
gboolean initialize_image_loader() {/*{{{*/
	image_loader_queue = g_async_queue_new();
	image_loader_cancellable = g_cancellable_new();
	current_file_node = bostree_select(file_tree, 0);
	while(!image_loader_load_single(current_file_node, TRUE) && bostree_node_count(file_tree) > 0);
	if(bostree_node_count(file_tree) == 0) {
		return FALSE;
	}
	#if GLIB_CHECK_VERSION(2, 32, 0)
		g_thread_new("image-loader", image_loader_thread, NULL);
	#else
		g_thread_create(image_loader_thread, NULL, FALSE, NULL);
	#endif

	if(!option_lowmem) {
		BOSNode *next = next_file();
		if(FILE(next)->image_surface == NULL) {
			queue_image_load(next);
		}
		BOSNode *previous = previous_file();
		if(FILE(previous)->image_surface == NULL) {
			queue_image_load(previous);
		}
	}

	return TRUE;
}/*}}}*/
void abort_pending_image_loads(BOSNode *new_pos) {/*{{{*/
	BOSNode *ref;
	while((ref = g_async_queue_try_pop(image_loader_queue)) != NULL) bostree_node_weak_unref(ref);
	if(image_loader_thread_currently_loading != NULL && image_loader_thread_currently_loading != new_pos) {
		g_cancellable_cancel(image_loader_cancellable);
	}
}/*}}}*/
void queue_image_load(BOSNode *node) {/*{{{*/
	g_async_queue_push(image_loader_queue, bostree_node_weak_ref(node));
}/*}}}*/
void unload_image(BOSNode *node) {/*{{{*/
	file_t *file = FILE(node);
	if(file->image_surface != NULL) {
		cairo_surface_destroy(file->image_surface);
		file->image_surface = NULL;
	}
	if(file->pixbuf_animation != NULL) {
		g_object_unref(file->pixbuf_animation);
		file->pixbuf_animation = NULL;
	}
	if(file->file_monitor != NULL) {
		g_file_monitor_cancel(file->file_monitor);
		if(G_IS_OBJECT(file->file_monitor)) {
			g_object_unref(file->file_monitor);
		}
		file->file_monitor = NULL;
	}
}/*}}}*/
gboolean absolute_image_movement(BOSNode *ref) {/*{{{*/
	BOSNode *node = bostree_node_weak_unref(ref);
	if(!node) {
		return FALSE;
	}

	// No need to continue the other pending loads
	abort_pending_image_loads(node);

	// Check which images have to be unloaded
	BOSNode *old_prev = previous_file();
	BOSNode *old_next = next_file();

	BOSNode *new_prev = bostree_previous_node(node);
	if(!new_prev) {
		new_prev = bostree_select(file_tree, bostree_node_count(file_tree) - 1);
	}
	BOSNode *new_next = bostree_next_node(node);
	if(!new_next) {
		new_next = bostree_select(file_tree, 0);
	}

	if((old_prev != new_next && old_prev != new_prev && old_prev != node) || (old_prev != node && option_lowmem)) {
		unload_image(old_prev);
	}
	if((old_next != new_next && old_next != new_prev && old_next != node) || (old_next != node && option_lowmem)) {
		unload_image(old_next);
	}
	if((current_file_node != new_next && current_file_node != new_prev && current_file_node != node) || CURRENT_FILE->surface_is_out_of_date || (current_file_node != node && option_lowmem)) {
		unload_image(current_file_node);
	}

	// Check which images have to be loaded
	current_file_node = node;

	// If the new image has not been loaded yet, display an information message
	if(CURRENT_FILE->image_surface == NULL) {
		update_info_text(NULL);
		gtk_widget_queue_draw(GTK_WIDGET(main_window));
	}

	queue_image_load(current_file_node);
	if(!option_lowmem) {
		if(FILE(new_next)->image_surface == NULL) {
			queue_image_load(new_next);
		}
		if(FILE(new_prev)->image_surface == NULL) {
			queue_image_load(new_prev);
		}
	}

	// Activate fading
	if(option_fading) {
		// It is important to initialize this variable with a positive,
		// non-null value, as 0. is used to indicate that no fading currently
		// takes place.
		fading_current_alpha_stage = DBL_EPSILON;
		// We start the clock after the first draw, because it could take some
		// time to calculate the resized version of the image
		fading_initial_time = -1;
		g_idle_add(fading_timeout_callback, NULL);
	}

	// If there is an active slideshow, interrupt it until the image has been
	// drawn
	if(slideshow_timeout_id > 0) {
		g_source_remove(slideshow_timeout_id);
		slideshow_timeout_id = 0;
	}

	return FALSE;
}/*}}}*/
void relative_image_movement(ptrdiff_t movement) {/*{{{*/
	// Calculate new position
	size_t count = bostree_node_count(file_tree);
	ptrdiff_t pos = bostree_rank(current_file_node) + movement;
	while(pos < 0) {
		pos += count;
	}
	pos %= count;

	absolute_image_movement(bostree_node_weak_ref(bostree_select(file_tree, pos)));
}/*}}}*/
void transform_current_image(cairo_matrix_t *transformation) {/*{{{*/
	if(CURRENT_FILE->image_surface == NULL) {
		// Image not loaded yet. Abort.
		return;
	}

	double sx = cairo_image_surface_get_width(CURRENT_FILE->image_surface);
	double sy = cairo_image_surface_get_height(CURRENT_FILE->image_surface);

	cairo_matrix_transform_distance(transformation, &sx, &sy);

	cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, fabs(sx), fabs(sy));
	cairo_t *cr = cairo_create(surface);

	cairo_transform(cr, transformation);
	cairo_set_source_surface(cr, CURRENT_FILE->image_surface, 0, 0);
	cairo_paint(cr);

	cairo_destroy(cr);
	cairo_surface_t *old_surface = CURRENT_FILE->image_surface;
	CURRENT_FILE->image_surface = surface;
	cairo_surface_destroy(old_surface);

	current_image_drawn = FALSE;
	invalidate_current_scaled_image_surface();
	CURRENT_FILE->surface_is_out_of_date = TRUE;
	set_scale_level_to_fit();
	image_loaded_handler(NULL);
}/*}}}*/

gchar *apply_external_image_filter_prepare_command(gchar *command) { /*{{{*/
		if((CURRENT_FILE->file_type & FILE_TYPE_MEMORY_IMAGE) != 0) {
			return g_strdup(command);
		}

		gchar *quoted = g_shell_quote(CURRENT_FILE->file_name);
		gchar *ins_pos;
		gchar *retval;
		if((ins_pos = g_strrstr(command, "$1")) != NULL) {
			retval = (gchar*)g_malloc(strlen(command) + strlen(quoted) + 2);

			memcpy(retval, command, ins_pos - command);
			sprintf(retval + (ins_pos - command), "%s%s", quoted, ins_pos + 2);
		}
		else {
			retval = (gchar*)g_malloc(strlen(command) + 2 + strlen(quoted));
			sprintf(retval, "%s %s", command, quoted);
		}
		g_free(quoted);

		return retval;
} /*}}}*/
gboolean window_key_press_close_handler_callback(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {/*{{{*/
	if(event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_Escape || event->keyval == GDK_KEY_q || event->keyval == GDK_KEY_Q) {
		gtk_widget_destroy(widget);
	}
	return FALSE;
}/*}}}*/
gboolean apply_external_image_filter_show_output_window(gpointer text) {/*{{{*/
	GtkWidget *output_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(output_window), "Command output");
	gtk_window_set_position(GTK_WINDOW(output_window), GTK_WIN_POS_CENTER_ON_PARENT);
	gtk_window_set_modal(GTK_WINDOW(output_window), TRUE);
	gtk_window_set_destroy_with_parent(GTK_WINDOW(output_window), TRUE);
	gtk_window_set_type_hint(GTK_WINDOW(output_window), GDK_WINDOW_TYPE_HINT_DIALOG);
	gtk_widget_set_size_request(output_window, 400, 480);
	g_signal_connect(output_window, "key-press-event",
			G_CALLBACK(window_key_press_close_handler_callback), NULL);
	GtkWidget *output_scroller = gtk_scrolled_window_new(NULL, NULL);
	gtk_container_add(GTK_CONTAINER(output_window), output_scroller);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(output_scroller),
			GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	GtkWidget *output_window_text = gtk_text_view_new();
	gtk_container_add(GTK_CONTAINER(output_scroller), output_window_text); 
	gtk_text_view_set_editable(GTK_TEXT_VIEW(output_window_text), FALSE);

	gsize output_text_length;
	gchar *output_text = g_locale_to_utf8((gchar*)text, strlen((gchar*)text), NULL, &output_text_length, NULL);
	gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(output_window_text)),
			output_text, output_text_length);
	g_free(output_text);
	gtk_widget_show_all(output_window);
	
	g_free(text);

	return FALSE;
}/*}}}*/
cairo_status_t apply_external_image_filter_thread_callback(void *closure, const unsigned char *data, unsigned int length) {/*{{{*/
	if(write(*(gint *)closure, data, length) == -1) {
		return CAIRO_STATUS_WRITE_ERROR;
	}
	else {
		return CAIRO_STATUS_SUCCESS;
	}
}/*}}}*/
gpointer apply_external_image_filter_image_writer_thread(gpointer data) {/*{{{*/
	cairo_surface_t *surface = cairo_surface_reference(CURRENT_FILE->image_surface);
	cairo_surface_write_to_png_stream(surface, apply_external_image_filter_thread_callback, data);
	close(*(gint *)data);
	cairo_surface_destroy(surface);
	return NULL;
}/*}}}*/
gboolean apply_external_image_filter_success_callback(gpointer user_data) {/*{{{*/
	cairo_surface_t *old_surface = CURRENT_FILE->image_surface;
	CURRENT_FILE->image_surface = (cairo_surface_t *)user_data;
	CURRENT_FILE->surface_is_out_of_date = TRUE;
	cairo_surface_destroy(old_surface);

	set_scale_level_to_fit();
	image_loaded_handler("External command succeeded.");
	gtk_widget_queue_draw(GTK_WIDGET(main_window));
	return FALSE;
}/*}}}*/
void apply_external_image_filter(gchar *external_filter) {/*{{{*/
	gchar *argv[4];
	argv[0] = (gchar*)"/bin/sh"; // Ok: These are not changed below
	argv[1] = (gchar*)"-c";
	argv[3] = 0;

	GError *error_pointer = NULL;

	if(external_filter[0] == '>') {
		// Pipe stdout into a new window
		argv[2] = apply_external_image_filter_prepare_command(external_filter + 1);
		gchar *child_stdout = NULL;
		gchar *child_stderr = NULL;
		if(g_spawn_sync(NULL, argv, NULL, 0, NULL, NULL, &child_stdout, &child_stderr, NULL, &error_pointer) == FALSE) {
			g_printerr("Failed execute external command `%s': %s\n", argv[2], error_pointer->message);
			g_clear_error(&error_pointer);
		}
		else {
			g_print("%s", child_stderr);
			g_free(child_stderr);

			g_idle_add(apply_external_image_filter_show_output_window, child_stdout);
		}
		// Reminder: Do not free the others, they are string constants
		g_free(argv[2]);
	}
	else if(external_filter[0] == '|') {
		// Pipe image into program, read image from its stdout
		argv[2] = external_filter + 1;
		GPid child_pid;
		gint child_stdin;
		gint child_stdout;
		BOSNode *current_file_node_at_start = current_file_node;
		if(!g_spawn_async_with_pipes(NULL, argv, NULL,
			// In win32, the G_SPAWN_DO_NOT_REAP_CHILD is required to get the process handle
			#ifdef _WIN32
			G_SPAWN_DO_NOT_REAP_CHILD,
			#else
			0,
			#endif
			NULL, NULL, &child_pid, &child_stdin, &child_stdout, NULL, &error_pointer)
		) {
			g_printerr("Failed execute external command `%s': %s\n", argv[2], error_pointer->message);
			g_clear_error(&error_pointer);
		}
		else {
			#if GLIB_CHECK_VERSION(2, 32, 0)
				g_thread_new("image-filter-writer", apply_external_image_filter_image_writer_thread, &child_stdin);
			#else
				g_thread_create(apply_external_image_filter_image_writer_thread, &child_stdin, FALSE, NULL);
			#endif

			gchar *image_data;
			gsize image_data_length;

			GIOChannel *stdin_channel = g_io_channel_unix_new(child_stdout);
			g_io_channel_set_encoding(stdin_channel, NULL, NULL);
			if(g_io_channel_read_to_end(stdin_channel, &image_data, &image_data_length, &error_pointer) != G_IO_STATUS_NORMAL) {
				g_printerr("Failed to load image from external command: %s\n", error_pointer->message);
				g_clear_error(&error_pointer);
			}
			else {
				gint status;
				#ifdef _WIN32
					WaitForSingleObject(child_pid, INFINITE);
					DWORD exit_code = 0;
					GetExitCodeProcess(child_pid, &exit_code);
					status = (gint)exit_code;
				#else
					waitpid(child_pid, &status, 0);
				#endif
				g_spawn_close_pid(child_pid);

				if(status != 0) {
					g_printerr("External command failed with exit status %d\n", status);
				}
				else {
					GdkPixbufLoader *loader = gdk_pixbuf_loader_new();
					GdkPixbuf *pixbuf = NULL;
					if(gdk_pixbuf_loader_write(loader, (const guchar *)image_data, image_data_length, &error_pointer)) {
						gdk_pixbuf_loader_close(loader, &error_pointer);
						pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
					}
					if(pixbuf == NULL) {
						g_printerr("Failed to load external command output: %s\n", error_pointer->message);
						g_clear_error(&error_pointer);
					}
					else {
						if(current_file_node_at_start == current_file_node) {
							cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, gdk_pixbuf_get_width(pixbuf), gdk_pixbuf_get_height(pixbuf));
							cairo_t *sf_cr = cairo_create(surface);
							gdk_cairo_set_source_pixbuf(sf_cr, pixbuf, 0, 0);
							cairo_paint(sf_cr);
							cairo_destroy(sf_cr);

							g_idle_add(apply_external_image_filter_success_callback, surface);
						}
					}

					g_object_unref(loader);
				}

				g_free(image_data);
			}
			g_io_channel_unref(stdin_channel);
		}
	}
	else {
		// Plain system() call
		argv[2] = apply_external_image_filter_prepare_command(external_filter);
		if(g_spawn_async(NULL, argv, NULL, 0, NULL, NULL, NULL, &error_pointer) == FALSE) {
			g_printerr("Failed execute external command `%s': %s\n", argv[2], error_pointer->message);
			g_clear_error(&error_pointer);
		}
		g_free(argv[2]);
	}
}/*}}}*/
gpointer apply_external_image_filter_thread(gpointer external_filter_ptr) {/*{{{*/
	apply_external_image_filter((gchar *)external_filter_ptr);
	return NULL;
}/*}}}*/

void hardlink_current_image() {/*{{{*/
	if((CURRENT_FILE->file_type & FILE_TYPE_MEMORY_IMAGE) != 0) {
		g_mkdir("./.pqiv-select", 0755);
		gchar *store_target = NULL;
		do {
			if(store_target != NULL) {
				g_free(store_target);
			}
			#if(GLIB_CHECK_VERSION(2, 28, 0))
				store_target = g_strdup_printf("./.pqiv-select/memory-%" G_GINT64_FORMAT "-%u.png", g_get_real_time(), g_random_int());
			#else
				store_target = g_strdup_printf("./.pqiv-select/memory-%u.png", g_random_int());
			#endif
		}
		while(g_file_test(store_target, G_FILE_TEST_EXISTS));

		if(cairo_surface_write_to_png(CURRENT_FILE->image_surface, store_target) == CAIRO_STATUS_SUCCESS) {
			gchar *info_text = g_strdup_printf("Stored what you see into %s", store_target);
			update_info_text(info_text);
			g_free(info_text);
		}
		else {
			update_info_text("Failed to write to the .pqiv-select subdirectory");
		}

		g_free(store_target);
		return;
	}

	gchar *current_file_basename = g_path_get_basename(CURRENT_FILE->file_name);
	gchar *link_target = g_strdup_printf("./.pqiv-select/%s", current_file_basename);

	if(g_file_test(link_target, G_FILE_TEST_EXISTS)) {
		g_free(link_target);
		g_free(current_file_basename);
		update_info_text("File already exists in .pqiv-select");
		gtk_widget_queue_draw(GTK_WIDGET(main_window));
		return;
	}

	g_mkdir("./.pqiv-select", 0755);
	if(
		#ifdef _WIN32
			CreateHardLink(link_target, CURRENT_FILE->file_name, NULL) == 0
		#else
			link(CURRENT_FILE->file_name, link_target) != 0
		#endif
	) {
		gchar *dot = g_strrstr(link_target, ".");
		if(dot != NULL && dot > link_target + 2) {
			*dot = 0;
		}
		gchar *store_target = g_strdup_printf("%s.png", link_target);
		if(cairo_surface_write_to_png(CURRENT_FILE->image_surface, store_target) == CAIRO_STATUS_SUCCESS) {
			gchar *info_text = g_strdup_printf("Failed to link file, but stored what you see into %s", store_target);
			update_info_text(info_text);
			g_free(info_text);
		}
		else {
			update_info_text("Failed to write to the .pqiv-select subdirectory");
		}
		g_free(store_target);
	}
	else {
		update_info_text("Created hard-link into .pqiv-select");
	}
	g_free(link_target);
	g_free(current_file_basename);
	gtk_widget_queue_draw(GTK_WIDGET(main_window));
}/*}}}*/
gboolean slideshow_timeout_callback(gpointer user_data) {/*{{{*/
	// Always abort this source: The clock will run again as soon as the image has been loaded.
	// The draw callback addes a new timeout if we set the timeout id to zero:
	slideshow_timeout_id = 0;
	relative_image_movement(1);
	return FALSE;
}/*}}}*/
gboolean fading_timeout_callback(gpointer user_data) {/*{{{*/
	if(fading_initial_time < 0) {
		// We just started. Leave the image invisible.
		gtk_widget_queue_draw(GTK_WIDGET(main_window));
		return TRUE;
	}

	double new_stage = (g_get_monotonic_time() - fading_initial_time) / (1e6 * option_fading_duration);
	new_stage = (new_stage < 0.) ? 0. : ((new_stage > 1.) ? 1. : new_stage);
	fading_current_alpha_stage = new_stage;
	gtk_widget_queue_draw(GTK_WIDGET(main_window));
	return (fading_current_alpha_stage < 1.); // FALSE aborts the source
}/*}}}*/
// }}}
/* Jump dialog {{{ */
gboolean jump_dialog_search_list_filter_callback(GtkTreeModel *model, GtkTreeIter *iter, gpointer user_data) { /* {{{ */
	/**
	 * List filter function for the jump dialog
	 */
	gchar *entry_text = (gchar*)gtk_entry_get_text(GTK_ENTRY(user_data));

	if(entry_text[0] == 0) {
		return TRUE;
	}

	entry_text = g_ascii_strdown(entry_text, -1);
	GValue col_data;
	memset(&col_data, 0, sizeof(GValue));
	gtk_tree_model_get_value(model, iter, 1, &col_data);
	gchar *compare_in = (char*)g_value_get_string(&col_data);
	compare_in = g_ascii_strdown(compare_in, -1);
	gboolean retval = (g_strstr_len(compare_in, -1, entry_text) != NULL);
	g_free(compare_in);
	g_value_unset(&col_data);
	g_free(entry_text);

	return retval;
} /* }}} */
gint jump_dialog_entry_changed_callback(GtkWidget *entry, gpointer user_data) { /*{{{*/
	/**
	 * Refilter the list when the entry text is changed
	 */
	gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(gtk_tree_view_get_model(GTK_TREE_VIEW(user_data))));

	GtkTreeIter iter;
	memset(&iter, 0, sizeof(GtkTreeIter));
	if(gtk_tree_model_get_iter_first(gtk_tree_view_get_model(GTK_TREE_VIEW(user_data)), &iter)) {
		gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(user_data)), &iter);
	}
	return FALSE;
} /* }}} */
gint jump_dialog_exit_on_enter_callback(GtkWidget *widget, GdkEventKey *event, gpointer user_data) { /*{{{*/
	/**
	 * If return is pressed exit the dialog
	 */
	if(event->keyval == GDK_KEY_Return) {
		gtk_dialog_response(GTK_DIALOG(user_data), GTK_RESPONSE_ACCEPT);
		return TRUE;
	}
	return FALSE;
} /* }}} */
gint jump_dialog_exit_on_dbl_click_callback(GtkWidget *widget, GdkEventButton *event, gpointer user_data) { /*{{{*/
	/**
	 * If the user doubleclicks into the list box, exit
	 * the dialog
	 */
	if(event->button == 1 && event->type == GDK_2BUTTON_PRESS) {
		gtk_dialog_response(GTK_DIALOG(user_data), GTK_RESPONSE_ACCEPT);
		return TRUE;
	}
	return FALSE;
} /* }}} */
void jump_dialog_open_image_callback(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer user_data) { /* {{{ */
	/**
	 * "for each" function for the list of the jump dialog
	 * (there can't be more than one selected image)
	 * Loads the image
	 */
	GValue col_data;
	memset(&col_data, 0, sizeof(GValue));
	gtk_tree_model_get_value(model, iter, 2, &col_data);
	BOSNode *jump_to = (BOSNode *)g_value_get_pointer(&col_data);
	g_value_unset(&col_data);

	g_timeout_add(200, (GSourceFunc)absolute_image_movement, bostree_node_weak_ref(jump_to));
} /* }}} */
void do_jump_dialog() { /* {{{ */
	/**
	 * Show the jump dialog to jump directly
	 * to an image
	 */
	GtkTreeIter search_list_iter;

	// Create dialog box
	GtkWidget *dlg_window = gtk_dialog_new_with_buttons("pqiv: Jump to image",
		main_window,
		GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL,
		"_OK",
		GTK_RESPONSE_ACCEPT,
		NULL);

	GtkWidget *search_entry = gtk_entry_new();
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dlg_window))),
		search_entry,
		FALSE,
		TRUE,
		0);

	// Build list for searching
	GtkListStore *search_list = gtk_list_store_new(3, G_TYPE_LONG, G_TYPE_STRING, G_TYPE_POINTER);
	size_t id = 1;
	for(BOSNode *node = bostree_select(file_tree, 0); node; node = bostree_next_node(node)) {
		gtk_list_store_append(search_list, &search_list_iter);

		gchar *display_name;
		if((FILE(node)->file_type & FILE_TYPE_MEMORY_IMAGE) != 0) {
			display_name = g_strdup_printf("-");
		}
		else {
			display_name = g_filename_display_name(FILE(node)->file_name);
		}
		gtk_list_store_set(search_list, &search_list_iter,
			0, id++,
			1, display_name,
			2, bostree_node_weak_ref(node),
			-1);
		g_free(display_name);
	}

	GtkTreeModel *search_list_filter = gtk_tree_model_filter_new(GTK_TREE_MODEL(search_list), NULL);
	gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(search_list_filter),
		jump_dialog_search_list_filter_callback,
		search_entry,
		NULL);

	// Create tree view
	GtkWidget *search_list_box = gtk_tree_view_new_with_model(GTK_TREE_MODEL(search_list_filter));
	gtk_tree_view_set_search_column(GTK_TREE_VIEW(search_list_box), 0);
	gtk_tree_view_set_enable_search(GTK_TREE_VIEW(search_list_box), TRUE);

	GtkCellRenderer *search_list_renderer_0 = gtk_cell_renderer_text_new();
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(search_list_box),
		-1,
		"#",
		search_list_renderer_0,
		"text", 0,
		NULL);
	GtkCellRenderer *search_list_renderer_1 = gtk_cell_renderer_text_new();
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(search_list_box),
		-1,
		"File name",
		search_list_renderer_1,
		"text", 1,
		NULL);
	GtkWidget *scroll_bar = gtk_scrolled_window_new(NULL, NULL);
	gtk_container_add(GTK_CONTAINER(scroll_bar),
		search_list_box);
	gtk_box_pack_end(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dlg_window))),
		scroll_bar,
		TRUE,
		TRUE,
		0);

	// Jump to active image
	GtkTreePath *goto_active_path = gtk_tree_path_new_from_indices(bostree_rank(current_file_node), -1);
	gtk_tree_selection_select_path(
		gtk_tree_view_get_selection(GTK_TREE_VIEW(search_list_box)),
		goto_active_path);
	gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(search_list_box),
		goto_active_path,
		NULL,
		FALSE, 0, 0);
	gtk_tree_path_free(goto_active_path);

	// Show dialog
	g_signal_connect(search_entry, "changed", G_CALLBACK(jump_dialog_entry_changed_callback), search_list_box);
	g_signal_connect(search_entry, "key-press-event", G_CALLBACK(jump_dialog_exit_on_enter_callback), dlg_window);
	g_signal_connect(search_list_box, "key-press-event", G_CALLBACK(jump_dialog_exit_on_enter_callback), dlg_window);
	g_signal_connect(search_list_box, "button-press-event", G_CALLBACK(jump_dialog_exit_on_dbl_click_callback), dlg_window);
	gtk_widget_set_size_request(dlg_window, 640, 480);
	gtk_widget_show_all(dlg_window);

	if(gtk_dialog_run(GTK_DIALOG(dlg_window)) == GTK_RESPONSE_ACCEPT) {
		gtk_tree_selection_selected_foreach(
			gtk_tree_view_get_selection(GTK_TREE_VIEW(search_list_box)),
			jump_dialog_open_image_callback,
			NULL);
	}

	// Free the references again
	GtkTreeIter iter;
	memset(&iter, 0, sizeof(GtkTreeIter));
	if(gtk_tree_model_get_iter_first(gtk_tree_view_get_model(GTK_TREE_VIEW(search_list_box)), &iter)) {
		GValue col_data;
		memset(&col_data, 0, sizeof(GValue));
		gtk_tree_model_get_value(GTK_TREE_MODEL(search_list_filter), &iter, 2, &col_data);
		bostree_node_weak_unref((BOSNode *)g_value_get_pointer(&col_data));
		g_value_unset(&col_data);
	}

	gtk_widget_destroy(dlg_window);
	g_object_unref(search_list);
	g_object_unref(search_list_filter);
} /* }}} */
// }}}
/* Main window functions {{{ */
inline void queue_draw() {/*{{{*/
	if(!current_image_drawn) {
		gtk_widget_queue_draw(GTK_WIDGET(main_window));
	}
}/*}}}*/
void update_info_text(const gchar *action) {/*{{{*/
	gchar *file_name;
	if((CURRENT_FILE->file_type & FILE_TYPE_MEMORY_IMAGE) != 0) {
		file_name = g_strdup_printf("-");
	}
	else {
		file_name = g_strdup(CURRENT_FILE->file_name);
	}
	gchar *display_name = g_filename_display_name(file_name);

	// Free old info text
	if(current_info_text != NULL) {
		g_free(current_info_text);
		current_info_text = NULL;
	}

	if(CURRENT_FILE->image_surface == NULL) {
		// Image not loaded yet. Use loading information and abort.
		current_info_text = g_strdup_printf("%s (Image is still loading...)", display_name);

		g_free(file_name);
		g_free(display_name);
		return;
	}

	// Update info text
	if(!option_hide_info_box) {
		current_info_text = g_strdup_printf("%s (%dx%d) %03.2f%% [%d/%d]", display_name,
			cairo_image_surface_get_width(CURRENT_FILE->image_surface),
			cairo_image_surface_get_height(CURRENT_FILE->image_surface),
			current_scale_level * 100.,
			(unsigned int)(bostree_rank(current_file_node) + 1),
			(unsigned int)(bostree_node_count(file_tree)));

		if(action != NULL) {
			gchar *old_info_text = current_info_text;
			current_info_text = g_strdup_printf("%s (%s)", current_info_text, action);
			g_free(old_info_text);
		}
	}

	// Prepare main window title
	GString *new_window_title = g_string_new(NULL);
	const char *window_title_iter = option_window_title;
	const char *temporary_iter;
	while(*window_title_iter) {
		temporary_iter = g_strstr_len(window_title_iter, -1, "$");
		if(!temporary_iter) {
			g_string_append(new_window_title, window_title_iter);
			break;
		}
		g_string_append_len(new_window_title, window_title_iter, (gssize)(temporary_iter - window_title_iter));

		window_title_iter = temporary_iter + 1;

		if(g_strstr_len(window_title_iter, 12, "BASEFILENAME") != NULL) {
			temporary_iter = g_filename_display_basename(file_name);
			g_string_append(new_window_title, temporary_iter);
			window_title_iter += 12;
		}
		else if(g_strstr_len(window_title_iter, 8, "FILENAME") != NULL) {
			g_string_append(new_window_title, display_name);
			window_title_iter += 8;
		}
		else if(g_strstr_len(window_title_iter, 5, "WIDTH") != NULL) {
			g_string_append_printf(new_window_title, "%d", cairo_image_surface_get_width(CURRENT_FILE->image_surface));
			window_title_iter += 5;
		}
		else if(g_strstr_len(window_title_iter, 6, "HEIGHT") != NULL) {
			g_string_append_printf(new_window_title, "%d", cairo_image_surface_get_height(CURRENT_FILE->image_surface));
			window_title_iter += 6;
		}
		else if(g_strstr_len(window_title_iter, 4, "ZOOM") != NULL) {
			g_string_append_printf(new_window_title, "%02.2f", (current_scale_level * 100));
			window_title_iter += 4;
		}
		else if(g_strstr_len(window_title_iter, 12, "IMAGE_NUMBER") != NULL) {
			g_string_append_printf(new_window_title, "%d", (unsigned int)(bostree_rank(current_file_node) + 1));
			window_title_iter += 12;
		}
		else if(g_strstr_len(window_title_iter, 11, "IMAGE_COUNT") != NULL) {
			g_string_append_printf(new_window_title, "%d", (unsigned int)(bostree_node_count(file_tree)));
			window_title_iter += 11;
		}
		else {
			g_string_append_c(new_window_title, '$');
		}
	}
	g_free(file_name);
	g_free(display_name);
	gtk_window_set_title(GTK_WINDOW(main_window), new_window_title->str);
	g_string_free(new_window_title, TRUE);
}/*}}}*/
gboolean window_close_callback(GtkWidget *object, gpointer user_data) {/*{{{*/
	gtk_main_quit();

	return FALSE;
}/*}}}*/
void setup_checkerboard_pattern() {/*{{{*/
	// Create pattern
    cairo_surface_t *surface;
	surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 16, 16);
	cairo_t *ccr = cairo_create(surface);
	cairo_set_source_rgba(ccr, .5, .5, .5, 1.);
	cairo_paint(ccr);
	cairo_set_source_rgba(ccr, 1., 1., 1., 1.);
	cairo_rectangle(ccr, 0, 0, 8, 8);
	cairo_fill(ccr);
	cairo_rectangle(ccr, 8, 8, 16, 16);
	cairo_fill(ccr);
	cairo_destroy(ccr);
    background_checkerboard_pattern = cairo_pattern_create_for_surface(surface);
    cairo_surface_destroy(surface);
    cairo_pattern_set_extend(background_checkerboard_pattern, CAIRO_EXTEND_REPEAT);
    cairo_pattern_set_filter(background_checkerboard_pattern, CAIRO_FILTER_NEAREST);
}/*}}}*/
gboolean window_draw_callback(GtkWidget *widget, cairo_t *cr_arg, gpointer user_data) {/*{{{*/
	// Draw image
	int x = 0;
	int y = 0;
	if(CURRENT_FILE->image_surface != NULL) {
		// Create an image surface to draw to first
		// We use this for fading and to display the last image if the current image is
		// still unavailable
		#if CAIRO_VERSION >= CAIRO_VERSION_ENCODE(1, 12, 0)
			cairo_surface_t *temporary_image_surface = cairo_surface_create_similar_image(cairo_get_target(cr_arg), CAIRO_FORMAT_ARGB32, main_window_width, main_window_height);
		#else
			cairo_surface_t *temporary_image_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, main_window_width, main_window_height);
		#endif
		cairo_t *cr = cairo_create(temporary_image_surface);

		// Draw black background
		cairo_save(cr);
		cairo_set_source_rgba(cr, 0., 0., 0., option_transparent_background ? 0. : 1.);
		cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
		cairo_paint(cr);
		cairo_restore(cr);

		// Draw the image & background pattern
		int image_width = cairo_image_surface_get_width(CURRENT_FILE->image_surface);
		int image_height = cairo_image_surface_get_height(CURRENT_FILE->image_surface);

		if(option_scale > 0 || main_window_in_fullscreen) {
			x = (main_window_width - current_scale_level * image_width) / 2;
			y = (main_window_height - current_scale_level * image_height) / 2;
		}
		else {
			// When scaling is disabled always use the upper left corder to avoid
			// problems with window managers ignoring the large window size request.
			x = y = 0;
		}

		// Scale the image
		if(current_scaled_image_surface == NULL) {
			cairo_t *cr_scale;
			if(option_lowmem) {
				// If in low memory mode, we do not store the scaled image surface
				// separately
				cr_scale = cr;
				cairo_translate(cr, x, y);
				cairo_translate(cr, current_shift_x, current_shift_y);
			}
			else {
				#if CAIRO_VERSION >= CAIRO_VERSION_ENCODE(1, 12, 0)
					current_scaled_image_surface = cairo_surface_create_similar_image(cairo_get_target(cr_arg), CAIRO_FORMAT_ARGB32, current_scale_level * image_width, current_scale_level * image_height);
				#else
					current_scaled_image_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, current_scale_level * image_width, current_scale_level * image_height);
				#endif
				cr_scale = cairo_create(current_scaled_image_surface);
			}

			cairo_scale(cr_scale, current_scale_level, current_scale_level);
			if(background_checkerboard_pattern != NULL && !option_transparent_background) {
				cairo_save(cr_scale);
				cairo_new_path(cr_scale);
				cairo_rectangle(cr_scale, 1, 1, image_width - 2, image_height - 2);
				cairo_close_path(cr_scale);
				cairo_clip(cr_scale);
				cairo_set_source(cr_scale, background_checkerboard_pattern);
				cairo_paint(cr_scale);
				cairo_restore(cr_scale);
			}

			cairo_set_source_surface(cr_scale, CURRENT_FILE->image_surface, 0, 0);
			cairo_paint(cr_scale);

			if(!option_lowmem) {
				cairo_destroy(cr_scale);
			}
		}

		// Move to the desired coordinates, and draw
		if(current_scaled_image_surface != NULL) {
			cairo_translate(cr, x, y);
			cairo_translate(cr, current_shift_x, current_shift_y);
			cairo_set_source_surface(cr, current_scaled_image_surface, 0, 0);
			cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
			cairo_paint(cr);
		}
		cairo_destroy(cr);

		// If currently fading, draw the surface along with the old image
		if(option_fading && fading_current_alpha_stage < 1. && fading_current_alpha_stage > 0. && last_visible_image_surface != NULL) {
			cairo_set_source_surface(cr_arg, last_visible_image_surface, 0, 0);
			cairo_set_operator(cr_arg, CAIRO_OPERATOR_SOURCE);
			cairo_paint(cr_arg);

			cairo_set_source_surface(cr_arg, temporary_image_surface, 0, 0);
			cairo_paint_with_alpha(cr_arg, fading_current_alpha_stage);

			cairo_surface_destroy(temporary_image_surface);

			// If this was the first draw, start the fading clock
			if(fading_initial_time < 0) {
				fading_initial_time = g_get_monotonic_time();
			}
		}
		else {
			// Draw the temporary surface to the screen
			cairo_set_source_surface(cr_arg, temporary_image_surface, 0, 0);
			cairo_set_operator(cr_arg, CAIRO_OPERATOR_SOURCE);
			cairo_paint(cr_arg);

			// Store the surface, for fading and to have something to display if no
			// image is loaded (see below)
			if(last_visible_image_surface != NULL) {
				cairo_surface_destroy(last_visible_image_surface);
			}
			if(!option_lowmem || option_fading) {
				last_visible_image_surface = temporary_image_surface;
			}
			else {
				cairo_surface_destroy(temporary_image_surface);
				last_visible_image_surface = NULL;
			}
		}

		// If we have an active slideshow, resume now.
		if(slideshow_timeout_id == 0) {
			slideshow_timeout_id = g_timeout_add(option_slideshow_interval * 1000, slideshow_timeout_callback, NULL);
		}

		current_image_drawn = TRUE;
	}
	else {
		// The image has not yet been loaded. If available, draw from the
		// temporary image surface from the last call
		if(last_visible_image_surface != NULL) {
			cairo_set_source_surface(cr_arg, last_visible_image_surface, 0, 0);
			cairo_set_operator(cr_arg, CAIRO_OPERATOR_SOURCE);
			cairo_paint(cr_arg);
		}
	}

	// Draw info box (directly to the screen)
	if(current_info_text != NULL) {
		double x1, x2, y1, y2;
		cairo_save(cr_arg);
		// Attempt this multiple times: If it does not fit the window,
		// retry with a smaller font size
		for(int font_size=12; font_size > 6; font_size--) {
			cairo_set_font_size(cr_arg, font_size);

			if(main_window_in_fullscreen == FALSE) {
				// Tiling WMs, at least i3, react weird on our window size changing.
				// Drawing the info box on the image helps to avoid users noticing that.
				cairo_translate(cr_arg, x < 0 ? 0 : x, y < 0 ? 0 : y);
			}

			cairo_set_source_rgb(cr_arg, 1., 1., 0.);
			cairo_translate(cr_arg, 10, 20);
			cairo_text_path(cr_arg, current_info_text);
			cairo_path_t *text_path = cairo_copy_path(cr_arg);
			cairo_path_extents(cr_arg, &x1, &y1, &x2, &y2);

			if(x2 > main_window_width && !main_window_in_fullscreen) {
				cairo_new_path(cr_arg);
				cairo_restore(cr_arg);
				cairo_save(cr_arg);
				continue;
			}

			cairo_new_path(cr_arg);
			cairo_rectangle(cr_arg, -5, -(y2 - y1) - 2, x2 - x1 + 10, y2 - y1 + 8);
			cairo_close_path(cr_arg);
			cairo_fill(cr_arg);
			cairo_set_source_rgb(cr_arg, 0., 0., 0.);
			cairo_append_path(cr_arg, text_path);
			cairo_fill(cr_arg);
			cairo_path_destroy(text_path);

			break;
		}

		cairo_restore(cr_arg);
	}

	return TRUE;
}/*}}}*/
#if GTK_MAJOR_VERSION < 3
	gboolean window_expose_callback(GtkWidget *widget, GdkEvent *event, gpointer user_data) {/*{{{*/
		cairo_t *cr = gdk_cairo_create(widget->window);
		window_draw_callback(widget, cr, user_data);
		cairo_destroy(cr);
		return TRUE;
	}/*}}}*/
#endif
void set_scale_level_to_fit() {/*{{{*/
	if(CURRENT_FILE->image_surface != NULL) {
		if(!current_image_drawn) {
			scale_override = FALSE;
		}
		int image_width = cairo_image_surface_get_width(CURRENT_FILE->image_surface);
		int image_height = cairo_image_surface_get_height(CURRENT_FILE->image_surface);

		gdouble new_scale_level = 1.0;

		// Only scale if scaling is not disabled. The alternative is to also
		// scale for no-scaling mode if (!main_window_in_fullscreen). This
		// effectively disables the no-scaling mode in non-fullscreen. I
		// implemented that this way, but changed it per user request.
		if(option_scale > 0 || scale_override) {
			if(option_scale > 1 || scale_override) {
				// Scale up
				if(image_width * new_scale_level < main_window_width) {
					new_scale_level = main_window_width * 1.0 / image_width;
				}

				if(image_height * new_scale_level < main_window_height) {
					new_scale_level = main_window_height * 1.0 / image_height;
				}
			}

			// Scale down
			if(main_window_height < new_scale_level * image_height) {
				new_scale_level = main_window_height * 1.0 / image_height;
			}
			if(main_window_width < new_scale_level * image_width) {
				new_scale_level = main_window_width * 1.0 / image_width;
			}
		}

		if(fabs(new_scale_level - current_scale_level) > DBL_EPSILON) {
			current_scale_level = new_scale_level;
			invalidate_current_scaled_image_surface();
		}
	}
}
gboolean set_scale_level_to_fit_callback(gpointer user_data) {
	set_scale_level_to_fit();
	return FALSE;
}
/*}}}*/
gboolean window_configure_callback(GtkWidget *widget, GdkEventConfigure *event, gpointer user_data) {/*{{{*/
	/*
	 * struct GdkEventConfigure {
		 GdkEventType type;
		 GdkWindow *window;
		 gint8 send_event;
		 gint x, y;
		 gint width;
		 gint height;
	 };
	 */

	static gint old_window_x, old_window_y;
	if(old_window_x != event->x || old_window_y != event->y) {
		// Window was moved. Reset automatic positioning to allow
		// user-resizing without pain.
		gtk_window_set_position(main_window, GTK_WIN_POS_NONE);

		old_window_x = event->x;
		old_window_y = event->y;

		// Execute the "screen changed" callback, because the monitor at the window might have changed
		window_screen_changed_callback(NULL, NULL, NULL);

		// In fullscreen, the position should always match the upper left point
		// of the screen. Some WMs get this wrong.
		if(main_window_in_fullscreen && (event->x != screen_geometry.x || event->y != screen_geometry.y)) {
			gtk_window_move(main_window, screen_geometry.x, screen_geometry.y);
		}
	}

	if(main_window_width != event->width || main_window_height != event->height) {
		// Update window size
		if(main_window_in_fullscreen) {
			main_window_width = screen_geometry.width;
			main_window_height = screen_geometry.height;
		}
		else {
			main_window_width = event->width;
			main_window_height = event->height;
		}

		// Rescale the image, unless overridden by the user
		if(option_initial_scale_used) {
			set_scale_level_to_fit();
			queue_draw();
		}

		// We need to redraw in old GTK versions to avoid artifacts
		#if GTK_MAJOR_VERSION < 3
			gtk_widget_queue_draw(GTK_WIDGET(main_window));
		#endif
	}

	return FALSE;
}/*}}}*/
gboolean window_key_press_callback(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {/*{{{*/
	/*
	   struct GdkEventKey {
	   GdkEventType type;
	   GdkWindow *window;
	   gint8 send_event;
	   guint32 time;
	   guint state;
	   guint keyval;
	   gint length;
	   gchar *string;
	   guint16 hardware_keycode;
	   guint8 group;
	   guint is_modifier : 1;
	   };
	 */

	if(event->keyval < 128 && keyboard_aliases[event->keyval] != 0) {
		event->keyval = keyboard_aliases[event->keyval];
	}

	// If the current image is not loaded, only allow stuff unrelated to the current image
	if(CURRENT_FILE->image_surface == NULL && (
		event->keyval != GDK_KEY_space &&
		event->keyval != GDK_KEY_Page_Up &&
		event->keyval != GDK_KEY_KP_Page_Up &&
		event->keyval != GDK_KEY_Page_Down &&
		event->keyval != GDK_KEY_KP_Page_Down &&
		event->keyval != GDK_KEY_BackSpace &&
		event->keyval != GDK_KEY_j &&
		event->keyval != GDK_KEY_J &&
		event->keyval != GDK_KEY_i &&
		event->keyval != GDK_KEY_I &&
		event->keyval != GDK_KEY_s &&
		event->keyval != GDK_KEY_S &&
		event->keyval != GDK_KEY_Q &&
		event->keyval != GDK_KEY_q &&
		event->keyval != GDK_KEY_f &&
		event->keyval != GDK_KEY_F)) {
		return FALSE;
	}

	switch(event->keyval) {
		case GDK_KEY_Up:
		case GDK_KEY_KP_Up:
			current_shift_y += (event->state & GDK_CONTROL_MASK ? 50 : 10) * (option_reverse_cursor_keys ? -1 : 1);
			gtk_widget_queue_draw(GTK_WIDGET(main_window));
			update_info_text(NULL);
			break;

		case GDK_KEY_Down:
		case GDK_KEY_KP_Down:
			current_shift_y -= (event->state & GDK_CONTROL_MASK ? 50 : 10) * (option_reverse_cursor_keys ? -1 : 1);
			gtk_widget_queue_draw(GTK_WIDGET(main_window));
			update_info_text(NULL);
			break;

		case GDK_KEY_Left:
		case GDK_KEY_KP_Left:
			current_shift_x += (event->state & GDK_CONTROL_MASK ? 50 : 10) * (option_reverse_cursor_keys ? -1 : 1);
			gtk_widget_queue_draw(GTK_WIDGET(main_window));
			update_info_text(NULL);
			break;

		case GDK_KEY_Right:
		case GDK_KEY_KP_Right:
			current_shift_x -= (event->state & GDK_CONTROL_MASK ? 50 : 10) * (option_reverse_cursor_keys ? -1 : 1);
			gtk_widget_queue_draw(GTK_WIDGET(main_window));
			update_info_text(NULL);
			break;

		case GDK_KEY_plus:
		case GDK_KEY_KP_Add:
			if(event->state & GDK_CONTROL_MASK) {
				option_slideshow_interval += 1;
				if(slideshow_timeout_id > 0) {
					g_source_remove(slideshow_timeout_id);
					slideshow_timeout_id = g_timeout_add(option_slideshow_interval * 1000, slideshow_timeout_callback, NULL);
				}
				gchar *info_text = g_strdup_printf("Slideshow interval set to %d seconds", option_slideshow_interval);
				update_info_text(info_text);
				gtk_widget_queue_draw(GTK_WIDGET(main_window));
				g_free(info_text);
				break;
			}

			current_scale_level *= 1.1;
			if((option_scale == 1 && current_scale_level > 1) || option_scale == 0) {
				scale_override = TRUE;
			}
			invalidate_current_scaled_image_surface();
			if(main_window_in_fullscreen) {
				gtk_widget_queue_draw(GTK_WIDGET(main_window));
			}
			else {
				int image_width = cairo_image_surface_get_width(CURRENT_FILE->image_surface);
				int image_height = cairo_image_surface_get_height(CURRENT_FILE->image_surface);

				gtk_window_resize(main_window, current_scale_level * image_width, current_scale_level * image_height);
			}
			update_info_text(NULL);
			break;

		case GDK_KEY_minus:
		case GDK_KEY_KP_Subtract:
			if(event->state & GDK_CONTROL_MASK) {
				if(option_slideshow_interval >= 2) {
					option_slideshow_interval -= 1;
				}
				if(slideshow_timeout_id > 0) {
					g_source_remove(slideshow_timeout_id);
					slideshow_timeout_id = g_timeout_add(option_slideshow_interval * 1000, slideshow_timeout_callback, NULL);
				}
				gchar *info_text = g_strdup_printf("Slideshow interval set to %d seconds", option_slideshow_interval);
				update_info_text(info_text);
				gtk_widget_queue_draw(GTK_WIDGET(main_window));
				g_free(info_text);
				break;
			}

			if(current_scale_level <= 0.01) {
				break;
			}
			current_scale_level /= 1.1;
			if((option_scale == 1 && current_scale_level > 1) || option_scale == 0) {
				scale_override = TRUE;
			}
			invalidate_current_scaled_image_surface();
			if(main_window_in_fullscreen) {
				gtk_widget_queue_draw(GTK_WIDGET(main_window));
			}
			else {
				int image_width = cairo_image_surface_get_width(CURRENT_FILE->image_surface);
				int image_height = cairo_image_surface_get_height(CURRENT_FILE->image_surface);

				gtk_window_resize(main_window, current_scale_level * image_width, current_scale_level * image_height);
			}
			update_info_text(NULL);
			break;

		case GDK_KEY_T:
		case GDK_KEY_t:
			if(++option_scale > 2) {
				option_scale = 0;
			}
			current_image_drawn = FALSE;
			invalidate_current_scaled_image_surface();
			set_scale_level_to_fit();
			image_loaded_handler(NULL);
			switch(option_scale) {
				case 0: update_info_text("Scaling disabled"); break;
				case 1: update_info_text("Automatic scaledown enabled"); break;
				case 2: update_info_text("Automatic scaling enabled"); break;
			}
			break;

		case GDK_KEY_r:
		case GDK_KEY_R:
			CURRENT_FILE->surface_is_out_of_date = TRUE;
			update_info_text("Reloading image..");
			queue_image_load(current_file_node);
			break;

		case GDK_KEY_0:
			current_image_drawn = FALSE;
			set_scale_level_to_fit();
			invalidate_current_scaled_image_surface();
			image_loaded_handler(NULL);
			break;

		case GDK_KEY_F:
		case GDK_KEY_f:
			if(main_window_in_fullscreen == FALSE) {
				gtk_window_fullscreen(main_window);
			}
			else {
				gtk_window_unfullscreen(main_window);
			}
			break;

		case GDK_KEY_H:
		case GDK_KEY_h:
			if((CURRENT_FILE->file_type & FILE_TYPE_ANIMATION) != 0) {
				break;
			}

			{
				cairo_matrix_t transformation = { -1., 0., 0., 1., cairo_image_surface_get_width(CURRENT_FILE->image_surface), 0 };
				transform_current_image(&transformation);
			}
			update_info_text("Image flipped horizontally");
			break;

		case GDK_KEY_V:
		case GDK_KEY_v:
			if((CURRENT_FILE->file_type & FILE_TYPE_ANIMATION) != 0) {
				break;
			}

			{
				cairo_matrix_t transformation = { 1., 0., 0., -1., 0, cairo_image_surface_get_height(CURRENT_FILE->image_surface) };
				transform_current_image(&transformation);
			}
			update_info_text("Image flipped vertically");
			break;

		case GDK_KEY_L:
		case GDK_KEY_l:
			if((CURRENT_FILE->file_type & FILE_TYPE_ANIMATION) != 0) {
				break;
			}

			{
				cairo_matrix_t transformation = { 0., -1., 1., 0., 0, cairo_image_surface_get_width(CURRENT_FILE->image_surface) };
				transform_current_image(&transformation);
			}
			update_info_text("Image rotated left");
			break;

		case GDK_KEY_K:
		case GDK_KEY_k:
			if((CURRENT_FILE->file_type & FILE_TYPE_ANIMATION) != 0) {
				break;
			}

			{
				cairo_matrix_t transformation = { 0., 1., -1., 0., cairo_image_surface_get_height(CURRENT_FILE->image_surface), 0. };
				transform_current_image(&transformation);
			}
			update_info_text("Image rotated right");
			break;

		case GDK_KEY_i:
		case GDK_KEY_I:
			option_hide_info_box = !option_hide_info_box;
			update_info_text(NULL);
			gtk_widget_queue_draw(GTK_WIDGET(main_window));
			break;

		case GDK_KEY_j:
		case GDK_KEY_J:
			do_jump_dialog();
			break;

		case GDK_KEY_s:
		case GDK_KEY_S:
			if(slideshow_timeout_id >= 0) {
				if(slideshow_timeout_id > 0) {
					g_source_remove(slideshow_timeout_id);
				}
				slideshow_timeout_id = -1;
				update_info_text("Slideshow disabled");
			}
			else {
				slideshow_timeout_id = g_timeout_add(option_slideshow_interval * 1000, slideshow_timeout_callback, NULL);
				update_info_text("Slideshow enabled");
			}
			gtk_widget_queue_draw(GTK_WIDGET(main_window));
			break;

		case GDK_KEY_a:
		case GDK_KEY_A:
			hardlink_current_image();
			gtk_widget_queue_draw(GTK_WIDGET(main_window));
			break;

		case GDK_KEY_BackSpace:
			relative_image_movement(-1);
			break;

		case GDK_KEY_space:
			relative_image_movement(1);
			break;

		case GDK_KEY_Page_Up:
		case GDK_KEY_KP_Page_Up:
			relative_image_movement(10);
			break;

		case GDK_KEY_Page_Down:
		case GDK_KEY_KP_Page_Down:
			relative_image_movement(-10);
			break;

		case GDK_KEY_Q:
		case GDK_KEY_q:
		case GDK_KEY_Escape:
			gtk_main_quit();
			break;

		case GDK_KEY_1:
		case GDK_KEY_2:
		case GDK_KEY_3:
		case GDK_KEY_4:
		case GDK_KEY_5:
		case GDK_KEY_6:
		case GDK_KEY_7:
		case GDK_KEY_8:
		case GDK_KEY_9:
			{

				gchar *command = external_image_filter_commands[event->keyval - GDK_KEY_1];

				if(command == NULL) {
					break;
				}
				else if (
					((CURRENT_FILE->file_type & FILE_TYPE_MEMORY_IMAGE) != 0 && command[0] != '|')
					|| ((CURRENT_FILE->file_type & FILE_TYPE_ANIMATION) != 0 && command[0] == '|')) {

					gchar *info = g_strdup_printf("Command %d incompatible with current file type", event->keyval - GDK_KEY_1 + 1);
					update_info_text(info);
					g_free(info);
					gtk_widget_queue_draw(GTK_WIDGET(main_window));
				}
				else {
					gchar *info = g_strdup_printf("Executing command %d", event->keyval - GDK_KEY_1 + 1);
					update_info_text(info);
					g_free(info);
					gtk_widget_queue_draw(GTK_WIDGET(main_window));

					#if GLIB_CHECK_VERSION(2, 32, 0)
						g_thread_new("image-filter", apply_external_image_filter_thread, command);
					#else
						g_thread_create(apply_external_image_filter_thread, command, FALSE, NULL);
					#endif
				}
			}
			break;
	}


	return FALSE;
}/*}}}*/
void window_center_mouse() {/*{{{*/
	GdkDisplay *display = gtk_widget_get_display(GTK_WIDGET(main_window));
	GdkScreen *screen = gtk_widget_get_screen(GTK_WIDGET(main_window));

	#if GTK_MAJOR_VERSION < 3
		gdk_display_warp_pointer(display, screen, screen_geometry.x + screen_geometry.width / 2., screen_geometry.y + screen_geometry.height / 2.);
	#else
		GdkDevice *device = gdk_device_manager_get_client_pointer(gdk_display_get_device_manager(display));
		gdk_device_warp(device, screen, screen_geometry.x + screen_geometry.width / 2., screen_geometry.y + screen_geometry.height / 2.);
	#endif
}/*}}}*/
gboolean window_motion_notify_callback(GtkWidget *widget, GdkEventMotion *event, gpointer user_data) {/*{{{*/
	/*
	   struct GdkEventMotion {
	   GdkEventType type;
	   GdkWindow *window;
	   gint8 send_event;
	   guint32 time;
	   gdouble x;
	   gdouble y;
	   gdouble *axes;
	   guint state;
	   gint16 is_hint;
	   GdkDevice *device;
	   gdouble x_root, y_root;
	   };
	 */
	if(!main_window_in_fullscreen) {
		return FALSE;
	}

	if(event->state & (GDK_BUTTON1_MASK | GDK_BUTTON2_MASK | GDK_BUTTON3_MASK)) {
		gdouble dev_x = screen_geometry.width / 2 + screen_geometry.x - event->x_root;
		gdouble dev_y = screen_geometry.height / 2 + screen_geometry.y - event->y_root;

		if(fabs(dev_x) < 5 && fabs(dev_y) < 4) {
			return FALSE;
		}

		if(event->state & GDK_BUTTON1_MASK) {
			current_shift_x += dev_x;
			current_shift_y += dev_y;
		}
		else if(event->state & GDK_BUTTON3_MASK) {
			current_scale_level += dev_y / 1000.;
			if(current_scale_level < .01) {
				current_scale_level = .01;
			}
			update_info_text(NULL);
			invalidate_current_scaled_image_surface();
		}

		gtk_widget_queue_draw(GTK_WIDGET(main_window));
		window_center_mouse();
	}


	return FALSE;
}/*}}}*/
gboolean window_button_press_callback(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {/*{{{*/
	/*
	   http://developer.gnome.org/gdk/stable/gdk-Event-Structures.html#GdkEventButton

	   struct GdkEventButton {
	   GdkEventType type;
	   GdkWindow *window;
	   gint8 send_event;
	   guint32 time;
	   gdouble x;
	   gdouble y;
	   gdouble *axes;
	   guint state;
	   guint button;
	   GdkDevice *device;
	   gdouble x_root, y_root;
	   };
	 */
	if(!main_window_in_fullscreen) {
		if(option_transparent_background) {
			gtk_window_set_decorated(main_window, !gtk_window_get_decorated(main_window));
		}
		return FALSE;
	}

	window_center_mouse();
	last_button_press_time = event->time;

	return FALSE;
}/*}}}*/
gboolean window_button_release_callback(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {/*{{{*/
	if(!main_window_in_fullscreen || event->time - last_button_press_time > 250) {
		// Do nothing if the button was pressed for a long time or if not in fullscreen
		return FALSE;
	}

	if(event->button == GDK_BUTTON_PRIMARY) {
		relative_image_movement(-1);
	}
	else if(event->button == GDK_BUTTON_MIDDLE) {
		gtk_main_quit();
	}
	else if(event->button == GDK_BUTTON_SECONDARY) {
		relative_image_movement(1);
	}
	return FALSE;
}/*}}}*/
gboolean window_scroll_callback(GtkWidget *widget, GdkEventScroll *event, gpointer user_data) {/*{{{*/
	/*
	   struct GdkEventScroll {
	   GdkEventType type;
	   GdkWindow *window;
	   gint8 send_event;
	   guint32 time;
	   gdouble x;
	   gdouble y;
	   guint state;
	   GdkScrollDirection direction;
	   GdkDevice *device;
	   gdouble x_root, y_root;
	   };
	 */
	if(event->direction == GDK_SCROLL_UP) {
		relative_image_movement(1);
	}
	else if(event->direction == GDK_SCROLL_DOWN) {
		relative_image_movement(-1);
	}


	return FALSE;
}/*}}}*/
void window_state_into_fullscreen_actions() {/*{{{*/
	GdkCursor *cursor = gdk_cursor_new(GDK_BLANK_CURSOR);
	GdkWindow *window = gtk_widget_get_window(GTK_WIDGET(main_window));
	gdk_window_set_cursor(window, cursor);
	#if GTK_MAJOR_VERSION >= 3
		g_object_unref(cursor);
	#endif

	main_window_width = screen_geometry.width;
	main_window_height = screen_geometry.height;

	if(option_initial_scale_used) {
		set_scale_level_to_fit();
	}
	invalidate_current_scaled_image_surface();
	#if GTK_MAJOR_VERSION < 3
		gtk_widget_queue_draw(GTK_WIDGET(main_window));
	#endif
}/*}}}*/
gboolean window_state_callback(GtkWidget *widget, GdkEventWindowState *event, gpointer user_data) {/*{{{*/
	/*
	   struct GdkEventWindowState {
	   GdkEventType type;
	   GdkWindow *window;
	   gint8 send_event;
	   GdkWindowState changed_mask;
	   GdkWindowState new_window_state;
	   };
	 */
	if(event->changed_mask & GDK_WINDOW_STATE_FULLSCREEN) {
		gboolean new_in_fs_state = (event->new_window_state & GDK_WINDOW_STATE_FULLSCREEN) != 0 ? TRUE : FALSE;
		if(new_in_fs_state == main_window_in_fullscreen) {
			return FALSE;
		}
		main_window_in_fullscreen = new_in_fs_state;

		if(main_window_in_fullscreen) {
			window_state_into_fullscreen_actions();
		}
		else {
			// Move the window back to the center of the screen.  We must do
			// this manually, at least Mutter ignores the position hint at this
			// point.
			if(CURRENT_FILE->image_surface != NULL) {
				gtk_window_move(main_window,
					screen_geometry.x + (screen_geometry.width - cairo_image_surface_get_width(CURRENT_FILE->image_surface) * current_scale_level) / 2,
					screen_geometry.y + (screen_geometry.height - cairo_image_surface_get_height(CURRENT_FILE->image_surface) * current_scale_level) / 2
				);
			}
			gtk_window_set_position(main_window, GTK_WIN_POS_CENTER_ALWAYS);

			GdkWindow *window = gtk_widget_get_window(GTK_WIDGET(main_window));
			gdk_window_set_cursor(window, NULL);

			current_image_drawn = FALSE;
			invalidate_current_scaled_image_surface();

			// Rescale the image and remove shift when leaving fullscreen
			current_shift_x = 0;
			current_shift_y = 0;
			gtk_window_get_size(main_window, &main_window_width, &main_window_height);
			if(option_initial_scale_used) {
				g_idle_add(set_scale_level_to_fit_callback, NULL);
			}
			g_idle_add((GSourceFunc)image_loaded_handler, NULL);
		}

		update_info_text(NULL);
	}

	return FALSE;
}/*}}}*/
void window_screen_activate_rgba() {/*{{{*/
	GdkScreen *screen = gtk_widget_get_screen(GTK_WIDGET(main_window));
	#if GTK_MAJOR_VERSION >= 3
		GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
		if (visual == NULL) {
			visual = gdk_screen_get_system_visual(screen);
		}
		gtk_widget_set_visual(GTK_WIDGET(main_window), visual);
	#else
		if(gtk_widget_get_realized(GTK_WIDGET(main_window))) {
			// In GTK2, this must not happen again after realization
			return;
		}

		GdkColormap *colormap = gdk_screen_get_rgba_colormap(screen);
		if (colormap != NULL) {
			gtk_widget_set_colormap(GTK_WIDGET(main_window), colormap);
		}
	#endif
	return;
}/*}}}*/
void window_screen_changed_callback(GtkWidget *widget, GdkScreen *previous_screen, gpointer user_data) {/*{{{*/
	GdkScreen *screen = gtk_widget_get_screen(GTK_WIDGET(main_window));
	GdkWindow *window = gtk_widget_get_window(GTK_WIDGET(main_window));
	guint monitor = gdk_screen_get_monitor_at_window(screen, window);

	static guint old_monitor = 9999;
	if(old_monitor != 9999 && option_transparent_background) {
		window_screen_activate_rgba();
	}
	if(old_monitor != monitor) {
		gdk_screen_get_monitor_geometry(screen, monitor, &screen_geometry);
	}
}/*}}}*/
void window_realize_callback(GtkWidget *widget, gpointer user_data) {/*{{{*/
	if(option_start_fullscreen) {
		#ifndef _WIN32
			GdkScreen *screen = gdk_screen_get_default();
			if(strcmp("unknown", gdk_x11_screen_get_window_manager_name(screen)) == 0) {
				// No window manager present. We need some oher means to fullscreen.
				// (Not all WMs implement _NET_WM_ACTION_FULLSCREEN, so we can not rely on that)
				main_window_in_fullscreen = TRUE;
				gtk_window_move(main_window, screen_geometry.x, screen_geometry.y);
				gtk_window_resize(main_window, screen_geometry.width, screen_geometry.height);
				window_state_into_fullscreen_actions();
			}
		#endif

		gtk_window_fullscreen(main_window);
	}

	// This would be the correct time to reset the option_initial_scale_used,
	// but compositing window managers (at least mutter) first map the
	// non-fullscreen window and then remap it before switching to fullscreen,
	// resulting in weird multiple calls to window_state_callback. On the
	// other hand, there are situations where window_state_callback is not
	// called at all.
	//
	// As a workaround, we use a timeout with a reasonalbe delay.
	//
	// TODO It would be very nice if this could be avoided. So far, I have not
	// been able to find out how it can be done (except for using override
	// redirect or waiting for the window to show up before fullscreening it,
	// which I'd both like to avoid)
	if(!option_initial_scale_used) {
		if(!option_start_fullscreen || main_window_in_fullscreen) {
			g_idle_add(set_option_initial_scale_used_callback, NULL);
		}
		else {
			g_timeout_add(300, set_option_initial_scale_used_callback, NULL);
		}
	}

	#if GTK_MAJOR_VERSION < 3
		if(option_transparent_background) {
			window_screen_activate_rgba();
		}
	#endif

	#if GTK_MAJOR_VERSION < 3 && !defined(_WIN32)
		gdk_property_change(gtk_widget_get_window(GTK_WIDGET(main_window)), gdk_atom_intern("_GTK_THEME_VARIANT", FALSE), (GdkAtom)XA_STRING, 8, GDK_PROP_MODE_REPLACE, (guchar *)"dark", 4);
	#endif
}/*}}}*/
void create_window() { /*{{{*/
	#if GTK_MAJOR_VERSION >= 3
		GtkSettings *settings = gtk_settings_get_default();
		if(settings != NULL) {
			g_object_set(G_OBJECT(settings), "gtk-application-prefer-dark-theme", TRUE, NULL);
		}
	#endif

	main_window = GTK_WINDOW(gtk_window_new(GTK_WINDOW_TOPLEVEL));

	g_signal_connect(main_window, "destroy", G_CALLBACK(window_close_callback), NULL);
	#if GTK_MAJOR_VERSION < 3
		g_signal_connect(main_window, "expose-event", G_CALLBACK(window_expose_callback), NULL);
	#else
		g_signal_connect(main_window, "draw", G_CALLBACK(window_draw_callback), NULL);
	#endif
	g_signal_connect(main_window, "configure-event", G_CALLBACK(window_configure_callback), NULL);
	g_signal_connect(main_window, "key-press-event", G_CALLBACK(window_key_press_callback), NULL);
	g_signal_connect(main_window, "scroll-event", G_CALLBACK(window_scroll_callback), NULL);
	g_signal_connect(main_window, "screen-changed", G_CALLBACK(window_screen_changed_callback), NULL);
	g_signal_connect(main_window, "motion-notify-event", G_CALLBACK(window_motion_notify_callback), NULL);
	g_signal_connect(main_window, "button-press-event", G_CALLBACK(window_button_press_callback), NULL);
	g_signal_connect(main_window, "button-release-event", G_CALLBACK(window_button_release_callback), NULL);
	g_signal_connect(main_window, "window-state-event", G_CALLBACK(window_state_callback), NULL);
	g_signal_connect(main_window, "realize", G_CALLBACK(window_realize_callback), NULL);

	gtk_widget_set_events(GTK_WIDGET(main_window),
		GDK_EXPOSURE_MASK | GDK_SCROLL_MASK | GDK_BUTTON_MOTION_MASK |
		GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_KEY_PRESS_MASK |
		GDK_PROPERTY_CHANGE_MASK | GDK_KEY_RELEASE_MASK | GDK_STRUCTURE_MASK); 

	// Initialize the screen geometry variable to the primary screen
	// Useful if no WM is present
	GdkScreen *screen = gdk_screen_get_default();
	guint monitor = gdk_screen_get_primary_monitor(screen);
	gdk_screen_get_monitor_geometry(screen, monitor, &screen_geometry);

	if(option_start_fullscreen) {
		// If no WM is present, move the window to the screen origin and
		// assume fullscreen right from the start
		#ifndef _WIN32
			if(strcmp("unknown", gdk_x11_screen_get_window_manager_name(screen)) == 0) {
				main_window_in_fullscreen = TRUE;
				gtk_window_move(main_window, screen_geometry.x, screen_geometry.y);
			}
		#endif
		gtk_window_fullscreen(main_window);
	}
	else if(option_window_position.x >= 0) {
		window_move_helper_callback(NULL);
	}
	else if(option_window_position.x != -1) {
		gtk_window_set_position(main_window, GTK_WIN_POS_CENTER);
	}

	gtk_widget_set_double_buffered(GTK_WIDGET(main_window), TRUE);
	gtk_widget_set_app_paintable(GTK_WIDGET(main_window), TRUE);

	if(option_transparent_background) {
		gtk_window_set_decorated(main_window, FALSE);
	}

	if(option_transparent_background) {
		window_screen_activate_rgba();
	}
}/*}}}*/
gboolean initialize_gui_callback(gpointer user_data) {/*{{{*/
	setup_checkerboard_pattern();
	create_window();
	initialize_image_loader();
	image_loaded_handler(NULL);

	if(option_start_with_slideshow_mode) {
		slideshow_timeout_id = g_timeout_add(option_slideshow_interval * 1000, slideshow_timeout_callback, NULL);
	}

	return FALSE;
}/*}}}*/
// }}}

gpointer load_images_thread(gpointer user_data) {/*{{{*/
	load_images(&global_argc, global_argv);

	if(bostree_node_count(file_tree) == 0) {
		exit(0);
	}

	return NULL;
}/*}}}*/

int main(int argc, char *argv[]) {
	#if (!GLIB_CHECK_VERSION(2, 32, 0))
		g_thread_init(NULL);
		gdk_threads_init();
	#endif
	gtk_init(&argc, &argv);

	parse_configuration_file(&argc, &argv);
	parse_command_line(&argc, argv);
	if(option_sort && option_shuffle) {
		g_printerr("--shuffle conflicts with --sort.\n");
		exit(1);
	}
	if(fabs(option_initial_scale - 1.0) < 2 * FLT_MIN) {
		option_initial_scale_used = TRUE;
	}
	else {
		current_scale_level = option_initial_scale;
	}

	global_argc = argc;
	global_argv = argv;
	if(option_lazy_load) {
		#if GLIB_CHECK_VERSION(2, 32, 0)
			g_thread_new("image-loader", load_images_thread, NULL);
		#else
			g_thread_create(load_images_thread, NULL, FALSE, NULL);
		#endif
	}
	else {
		load_images_thread(NULL);
		initialize_gui_callback(NULL);
	}

	gtk_main();

	return 0;
}

// vim:noet ts=4 sw=4 tw=0 fdm=marker
