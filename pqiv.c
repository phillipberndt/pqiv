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

#define PQIV_VERSION "2.0-rc1"

#include "lib/strnatcmp.h"
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
GPtrArray *file_list;
size_t current_image = 0;

// We asynchroniously load images in a separate thread
GAsyncQueue *image_loader_queue;

// Filter for path traversing upon building the file list
GtkFileFilter *load_images_file_filter;
GtkFileFilterInfo *load_images_file_filter_info;
GTree *load_images_known_paths_tree;
GTimer *load_images_timer;

// Access to the file list file_list
#define FILE_LIST_ENTRY(i) ((file_t*)(g_ptr_array_index(file_list, i)))
#define CURRENT_FILE FILE_LIST_ENTRY(current_image)
#define PREVIOUS_FILE FILE_LIST_ENTRY(current_image > 0 ? current_image - 1 : file_list->len - 1)
#define NEXT_FILE FILE_LIST_ENTRY(current_image < file_list->len - 1 ? current_image + 1 : 0)

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
gchar *current_info_text = NULL;


// Current state of the displayed image and user interaction
gdouble current_scale_level = 1.0;
gint current_shift_x = 0;
gint current_shift_y = 0;
guint32 last_button_press_time = 0;
GdkPixbufAnimationIter *current_image_animation_iter = NULL;
guint current_image_animation_timeout_id = 0;
guint slideshow_timeout_id = 0;

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
gboolean option_show_info_text = TRUE;
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

gboolean options_keyboard_alias_set_callback(const gchar *option_name, const gchar *value, gpointer data, GError **error);
gboolean option_window_position_callback(const gchar *option_name, const gchar *value, gpointer data, GError **error);
gboolean option_scale_level_callback(const gchar *option_name, const gchar *value, gpointer data, GError **error);

struct {
	gint x;
	gint y;
} option_window_position = { -2, -2 };

// Hint: Only types G_OPTION_ARG_NONE, G_OPTION_ARG_STRING, G_OPTION_ARG_DOUBLE/INTEGER and G_OPTION_ARG_CALLBACK are
// implemented for option parsing.
GOptionEntry options[] = {
	{ NULL, 0, 0, 0, NULL, "main", "Main application options" },

	{ "fullscreen", 'f', 0, G_OPTION_ARG_NONE, &option_start_fullscreen, "Start in fullscreen mode", NULL },
	{ "sort", 'n', 0, G_OPTION_ARG_NONE, &option_sort, "Sort files in natural order", NULL },
	{ "slideshow", 's', 0, G_OPTION_ARG_NONE, &option_start_with_slideshow_mode, "Activate slideshow mode", NULL },
	{ "shuffle", 0, 0, G_OPTION_ARG_NONE, &option_shuffle, "Shuffle files", NULL },

	{ NULL, 0, 0, 0, NULL, "advanced", "Setup the behavior of pqiv" },
	{ "keyboard-alias", 'a', 0, G_OPTION_ARG_CALLBACK, (gpointer)&options_keyboard_alias_set_callback, "Define n as a keyboard alias for f", "nf" },
	{ "transparent-background", 'c', 0, G_OPTION_ARG_NONE, &option_transparent_background, "Borderless transparent window", NULL },
	{ "slideshow-interval", 'd', 0, G_OPTION_ARG_INT, &option_slideshow_interval, "Set slideshow interval", "n" },
	{ "hide-info-box", 'i', 0, G_OPTION_ARG_NONE, &option_hide_info_box, "Initially hide the info box", NULL },
	{ "window-position", 'P', 0, G_OPTION_ARG_CALLBACK, (gpointer)&option_window_position_callback, "Set initial window position (`x,y' or `off' to not position the window at all)", "POSITION" },
	{ "reverse-cursor-keys", 'R', 0, G_OPTION_ARG_NONE, &option_reverse_cursor_keys, "Reverse the meaning of the cursor keys", NULL },
	{ "window-title", 'T', 0, G_OPTION_ARG_STRING, &option_window_title, "Set the title of the window. See manpage for available variables.", "TITLE" },
	{ "scale-images-up", 't', G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, (gpointer)&option_scale_level_callback, "Scale images up to fill the whole screen", NULL },
	{ "zoom-level", 'z', 0, G_OPTION_ARG_DOUBLE, &option_initial_scale, "Set initial zoom level (1.0 is 100%)", "FLOAT" },
	{ "disable-scaling", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, (gpointer)&option_scale_level_callback, "Disable scaling of images", NULL },

	{ "command-1", '1', 0, G_OPTION_ARG_STRING, &external_image_filter_commands[0], "Bind the external COMMAND to key 1. See manpage for extended usage (commands starting with `>' or `|'). Use 2..9 for further commands.", "COMMAND" },
	{ "command-2", '2', G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING, &external_image_filter_commands[1], NULL, NULL },
	{ "command-3", '3', G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING, &external_image_filter_commands[2], NULL, NULL },
	{ "command-4", '4', G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING, &external_image_filter_commands[3], NULL, NULL },
	{ "command-5", '5', G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING, &external_image_filter_commands[4], NULL, NULL },
	{ "command-6", '6', G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING, &external_image_filter_commands[5], NULL, NULL },
	{ "command-7", '7', G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING, &external_image_filter_commands[6], NULL, NULL },
	{ "command-8", '8', G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING, &external_image_filter_commands[7], NULL, NULL },
	{ "command-9", '9', G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING, &external_image_filter_commands[8], NULL, NULL },

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
"  a                                  Hardlink current image to ./.qiv-select\n"
);

void set_scale_level_to_fit();
void update_info_text(const char *);
void queue_draw();
gboolean main_window_center();
void window_screen_changed_callback(GtkWidget *widget, GdkScreen *previous_screen, gpointer user_data);
void queue_image_load(size_t);
void unload_image(file_t *image);
// }}}
/* Command line handling, creation of the image list {{{ */
gboolean options_keyboard_alias_set_callback(const gchar *option_name, const gchar *value, gpointer data, GError **error) {/*{{{*/
	if(strlen(value) != 2) {
		g_set_error(error, G_OPTION_ERROR, G_OPTION_ERROR_FAILED, "The argument to the alias option must have exactly two characters: The one to be mapped from and the one to be mapped to.");
		return FALSE;
	}

	keyboard_aliases[(size_t)value[0]] = value[1];
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
		//
		// This really is not completely compatible: -ff will still be fullscreen with this
		// implementation. We will suggest users to migrate to the new configuration file
		// format in the manual page.
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

			char **new_argv = (char **)g_malloc(sizeof(char *) * (*argc + additional_arguments_max + 1));
			new_argv[0] = (*argv)[0];
			char *end_of_argument = strchr(options_contents, ' ');
			while(*options_contents != 0 && (end_of_argument = strchr(options_contents, ' ')) != NULL) {
				*end_of_argument = 0;
				gchar *argv_val = options_contents;
				g_strstrip(argv_val);
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

	for(GOptionEntry *iter = options; iter->description != NULL || iter->long_name != NULL; iter++) {
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

	GOptionGroup *group = NULL;
	for(GOptionEntry *iter = options; iter->description != NULL || iter->long_name != NULL; iter++) {
		if(iter->long_name == NULL) {
			// Our hack to indicate a new option group
			GOptionGroup *new_group = g_option_group_new(iter->description, iter->arg_description, iter->arg_description, NULL, NULL);
			if(group == NULL) {
				g_option_context_set_main_group(parser, new_group);
			}
			else {
				g_option_context_add_group(parser, new_group);
			}
			group = new_group;
			g_option_group_add_entries(group, iter + 1);
		}
	}

	g_option_context_add_group(parser, gtk_get_option_group(TRUE));

	GError *error_pointer = NULL;
	if(g_option_context_parse(parser, argc, &argv, &error_pointer) == FALSE) {
		g_printerr("%s\n", error_pointer->message);
		exit(1);
	}

	g_option_context_free(parser);
}/*}}}*/
void load_images_handle_parameter(char *param, int level) {/*{{{*/
	// Check for memory image
	if(level == 0 && g_strcmp0(param, "-") == 0) {
		file_t *file = g_new0(file_t, 1);
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

		g_ptr_array_add(file_list, file);
		return;
	}

	// Check if we already loaded this
	char *absPathPtr = g_malloc(PATH_MAX);
	if(
		#ifdef _WIN32
			GetFullPathNameA(param, PATH_MAX, absPathPtr, NULL) != 0
		#else
			realpath(param, absPathPtr) != NULL
		#endif
	) {
		if(g_tree_lookup(load_images_known_paths_tree, absPathPtr) != NULL) {
			g_free(absPathPtr);
			return;
		}
		g_tree_insert(load_images_known_paths_tree, absPathPtr, (gpointer)1);
	}
	else {
		g_free(absPathPtr);
	}

	// Check if the file exists
	if(g_file_test(param, G_FILE_TEST_EXISTS) == FALSE) {
		if(level == 0) {
			g_printerr("File not found: %s\n", param);
		}
		return;
	}

	// Recurse into directories
	if(g_file_test(param, G_FILE_TEST_IS_DIR) == TRUE) {
		// Display progress
		if(g_timer_elapsed(load_images_timer, NULL) > 5.) {
			g_print("\033[s\033[JLoading in %s ...\033[u", param);
		}

		GDir *dir_ptr = g_dir_open(param, 0, NULL);
		if(dir_ptr == NULL) {
			if(level == 0) {
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
			load_images_handle_parameter(dir_entry_full, level + 1);
			g_free(dir_entry_full);
		}
		g_dir_close(dir_ptr);
		return;
	}

	// Filter based on formats supported by GdkPixbuf 
	if(level > 0) {
		gchar *param_lowerc = g_utf8_strdown(param, -1);
		load_images_file_filter_info->filename = load_images_file_filter_info->display_name = param_lowerc;
		if(gtk_file_filter_filter(load_images_file_filter, load_images_file_filter_info) == FALSE) {
			g_free(param_lowerc);
			return;
		}
		g_free(param_lowerc);
	}

	// Add image to images list
	file_t *file = g_new0(file_t, 1);
	file->file_name = g_strdup(param);
	if(file->file_name == NULL) {
		g_free(file);
		g_printerr("Failed to allocate memory for file name loading\n");
		return;
	}
	g_ptr_array_add(file_list, file);
}/*}}}*/
gint load_images_sorter_function(file_t **a, file_t **b) {/*{{{*/
	if(option_shuffle) {
		return g_random_boolean() == TRUE ? 1 : -1;
	}
	if(option_sort) {
		return strnatcasecmp((*a)->file_name, (*b)->file_name);
	}
	return 0;
}/*}}}*/
gint load_images_tree_compare_func(gconstpointer a, gconstpointer b, gpointer user_data) {/*{{{*/
	return g_strcmp0((const char*)a, (const char*)b);
}/*}}}*/
void load_images(int *argc, char *argv[]) {/*{{{*/
	// Allocate memory for the file list
	file_list =  g_ptr_array_new();

	// Allocate memory for the temporary tree
	load_images_known_paths_tree = g_tree_new_full((GCompareDataFunc)load_images_tree_compare_func, NULL, g_free, NULL);

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
		load_images_handle_parameter(argv[i], 0);
	}

	// Free the temporary stuff
	g_object_ref_sink(load_images_file_filter);
	g_free(load_images_file_filter_info);
	g_tree_unref(load_images_known_paths_tree);
	g_timer_destroy(load_images_timer);

	if(option_sort || option_shuffle) {
		g_ptr_array_sort(file_list, (GCompareFunc)load_images_sorter_function);
	}
}/*}}}*/
// }}}
/* (A-)synchronous image loading and image operations {{{ */
gboolean image_animation_timeout_callback(gpointer user_data) {/*{{{*/
	if((size_t)user_data != current_image) {
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

	gtk_widget_queue_draw(GTK_WIDGET(main_window));

	return FALSE;
}/*}}}*/
void image_file_updated_callback(GFileMonitor *monitor, GFile *file, GFile *other_file, GFileMonitorEvent event_type, gpointer user_data) {/*{{{*/
	size_t id = (size_t)user_data;

	if(event_type == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT) {
		FILE_LIST_ENTRY(id)->surface_is_out_of_date = TRUE;
		queue_image_load(id);
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
			if(image_height * current_scale_level > screen_height * .8) {
				current_scale_level = screen_height * .8 / image_height;
			}
		}

		// Update geometry hints
		GdkGeometry hints;
		hints.min_aspect = hints.max_aspect = image_width * 1.0 / image_height;
		gtk_window_set_geometry_hints(main_window, NULL, &hints, GDK_HINT_ASPECT);

		// Resize the window
		int new_window_width = current_scale_level * image_width;
		int new_window_height = current_scale_level * image_height;
		if(main_window_width >= 0 && (main_window_width != new_window_width || main_window_height != new_window_height)) {
			if(option_window_position.x >= 0) {
				g_idle_add(window_move_helper_callback, NULL);
			}
			else if(option_window_position.x != -1) {
				gtk_window_set_position(main_window, GTK_WIN_POS_CENTER_ALWAYS);
			}
			gtk_window_resize(main_window, new_window_width, new_window_height);
			
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
			(gpointer)current_image);
	}

	return FALSE;
}/*}}}*/
gboolean image_loader_load_single_destroy_old_image_callback(gpointer old_surface) {/*{{{*/
	cairo_surface_destroy((cairo_surface_t *)old_surface);
	return FALSE;
}/*}}}*/
gboolean image_loader_load_single(size_t id) {/*{{{*/
	if(FILE_LIST_ENTRY(id)->image_surface != NULL && !FILE_LIST_ENTRY(id)->surface_is_out_of_date) {
		return TRUE;
	}

	GError *error_pointer = NULL;
	GdkPixbufAnimation *pixbuf_animation = NULL;
	
	if((FILE_LIST_ENTRY(id)->file_type & FILE_TYPE_MEMORY_IMAGE) != 0) {
		GdkPixbufLoader *loader = gdk_pixbuf_loader_new();
		if(gdk_pixbuf_loader_write(loader, FILE_LIST_ENTRY(id)->file_data, FILE_LIST_ENTRY(id)->file_data_length, &error_pointer)) {
			gdk_pixbuf_loader_close(loader, &error_pointer);
			pixbuf_animation = gdk_pixbuf_loader_get_animation(loader);
		}
		if(pixbuf_animation == NULL) {
			g_printerr("Failed to load file from memory: %s\n", error_pointer->message);
			g_clear_error(&error_pointer);
		}
		g_object_ref(pixbuf_animation);
		g_object_unref(loader);
	}
	else {
		pixbuf_animation = gdk_pixbuf_animation_new_from_file(FILE_LIST_ENTRY(id)->file_name, &error_pointer);
		if(pixbuf_animation == NULL) {
			g_printerr("Failed to open file %s: %s\n", FILE_LIST_ENTRY(id)->file_name, error_pointer->message);
			g_clear_error(&error_pointer);
		}
	}

	if(pixbuf_animation != NULL) {
		if(!gdk_pixbuf_animation_is_static_image(pixbuf_animation)) {
			if(FILE_LIST_ENTRY(id)->pixbuf_animation != NULL) {
				g_object_unref(FILE_LIST_ENTRY(id)->pixbuf_animation);
			}
			FILE_LIST_ENTRY(id)->pixbuf_animation = g_object_ref(pixbuf_animation);
			FILE_LIST_ENTRY(id)->file_type |= FILE_TYPE_ANIMATION;
		}
		else {
			FILE_LIST_ENTRY(id)->file_type &= ~FILE_TYPE_ANIMATION;
		}

		// We apparently do not own this pixbuf!
		GdkPixbuf *pixbuf = gdk_pixbuf_animation_get_static_image(pixbuf_animation);

		if(pixbuf == NULL) {
			if((FILE_LIST_ENTRY(id)->file_type & FILE_TYPE_MEMORY_IMAGE) == 0) {
				g_printerr("Failed to load image %s: %s\n", FILE_LIST_ENTRY(id)->file_name, error_pointer->message);
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
			cairo_surface_t *old_surface = FILE_LIST_ENTRY(id)->image_surface;
			FILE_LIST_ENTRY(id)->image_surface = surface;
			if(old_surface != NULL) {
				g_idle_add(image_loader_load_single_destroy_old_image_callback, old_surface);
			}
			g_object_unref(pixbuf);
		}
		g_object_unref(pixbuf_animation);
	}

	FILE_LIST_ENTRY(id)->surface_is_out_of_date = FALSE;

	if(FILE_LIST_ENTRY(id)->image_surface != NULL) {
		if(FILE_LIST_ENTRY(id)->file_type == FILE_TYPE_DEFAULT) {
			GFile *the_file = g_file_new_for_path(FILE_LIST_ENTRY(id)->file_name);
			if(the_file != NULL) {
				FILE_LIST_ENTRY(id)->file_monitor = g_file_monitor_file(the_file, G_FILE_MONITOR_NONE, NULL, NULL);
				if(FILE_LIST_ENTRY(id)->file_monitor != NULL) {
					g_signal_connect(FILE_LIST_ENTRY(id)->file_monitor, "changed", G_CALLBACK(image_file_updated_callback), (gpointer)id);
				}
				g_object_unref(the_file);
			}
		}

		return TRUE;
	}
	else {
		unload_image(FILE_LIST_ENTRY(id));
		g_ptr_array_remove_index(file_list, id);
		gboolean was_current_image = id == current_image;
		if(id <= current_image) {
			if(--current_image >= file_list->len) {
				current_image = 0;
				if(file_list->len == 0) {
					g_printerr("No images left to display.\n");
					if(gtk_main_level() == 0) {
						exit(1);
					}
					gtk_main_quit();
				}
			}
		}
		if(was_current_image) {
			queue_image_load(current_image);
		}
	}

	return FALSE;
}/*}}}*/
gpointer image_loader_thread(gpointer user_data) {/*{{{*/
	while(TRUE) {
		size_t *id_ptr = g_async_queue_pop(image_loader_queue);
		if(FILE_LIST_ENTRY(*id_ptr)->image_surface == NULL || FILE_LIST_ENTRY(*id_ptr)->surface_is_out_of_date) {
			// Load image
			image_loader_load_single(*id_ptr);
		}

		if(*id_ptr == current_image && FILE_LIST_ENTRY(*id_ptr)->image_surface != NULL) {
			current_image_drawn = FALSE;

			g_idle_add((GSourceFunc)image_loaded_handler, NULL);
		}

		g_free(id_ptr);
	}
}/*}}}*/
gboolean initialize_image_loader() {/*{{{*/
	while(!image_loader_load_single(current_image) && file_list->len > 0);
	if(file_list->len == 0) {
		return FALSE;
	}
	image_loader_queue = g_async_queue_new();
	#if GLIB_CHECK_VERSION(2, 32, 0)
		g_thread_new("image-loader", image_loader_thread, NULL);
	#else
		g_thread_create(image_loader_thread, NULL, FALSE, NULL);
	#endif
	return TRUE;
}/*}}}*/
void queue_image_load(size_t id) {/*{{{*/
	size_t *id_ptr = g_new(size_t, 1);
	*id_ptr = id;
	g_async_queue_push(image_loader_queue, id_ptr);
}/*}}}*/
void unload_image(file_t *image) {/*{{{*/
	if(image->image_surface != NULL) {
		cairo_surface_destroy(image->image_surface);
		image->image_surface = NULL;
	}
	if(image->pixbuf_animation != NULL) {
		g_object_unref(image->pixbuf_animation);
		image->pixbuf_animation = NULL;
	}
	if(image->file_monitor != NULL) {
		g_file_monitor_cancel(image->file_monitor);
		if(G_IS_OBJECT(image->file_monitor)) {
			g_object_unref(image->file_monitor);
		}
		image->file_monitor = NULL;
	}
}/*}}}*/
void absolute_image_movement(size_t pos) {/*{{{*/
	// Check which images have to be unloaded
	size_t old_prev = current_image > 0 ? current_image - 1 : file_list->len - 1;
	size_t old_next = current_image < file_list->len - 1 ? current_image + 1 : 0;

	size_t new_prev = pos > 0 ? pos - 1 : file_list->len - 1;
	size_t new_next = pos < file_list->len - 1 ? pos + 1 : 0;

	if(old_prev != new_next && old_prev != new_prev && old_prev != pos) {
		unload_image(PREVIOUS_FILE);
	}
	if(old_next != new_next && old_next != new_prev && old_next != pos) {
		unload_image(NEXT_FILE);
	}
	if((current_image != new_next && current_image != new_prev && current_image != pos) || CURRENT_FILE->surface_is_out_of_date) {
		unload_image(CURRENT_FILE);
	}

	// Check which images have to be loaded
	current_image = pos;

	queue_image_load(current_image);
	if(NEXT_FILE->image_surface == NULL) {
		queue_image_load(new_next);
	}
	if(PREVIOUS_FILE->image_surface == NULL) {
		queue_image_load(new_prev);
	}
}/*}}}*/
gboolean absolute_image_movement_callback(gpointer user_data) {/*{{{*/
	absolute_image_movement((size_t)user_data);
	return FALSE;
}/*}}}*/
void relative_image_movement(ptrdiff_t movement) {/*{{{*/
	// Calculate new position
	ptrdiff_t pos = current_image + movement;
	while(pos > file_list->len - 1) {
		pos -= file_list->len;
	}
	while(pos < 0) {
		pos += file_list->len;
	}

	absolute_image_movement(pos);
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
		gsize current_image_at_start = current_image;
		if(!g_spawn_async_with_pipes(NULL, argv, NULL, 0, NULL, NULL, &child_pid, &child_stdin, &child_stdout, NULL, &error_pointer)) {
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
						if(current_image_at_start == current_image) {
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
		g_mkdir("./.qiv-select", 0755);
		gchar *store_target = NULL;
		do {
			if(store_target != NULL) {
				g_free(store_target);
			}
			#if(GLIB_CHECK_VERSION(2, 28, 0))
				store_target = g_strdup_printf("./.qiv-select/memory-%" G_GINT64_FORMAT "-%u.png", g_get_real_time(), g_random_int());
			#else
				store_target = g_strdup_printf("./.qiv-select/memory-%u.png", g_random_int());
			#endif
		}
		while(g_file_test(store_target, G_FILE_TEST_EXISTS));

		if(cairo_surface_write_to_png(CURRENT_FILE->image_surface, store_target) == CAIRO_STATUS_SUCCESS) {
			gchar *info_text = g_strdup_printf("Stored what you see into %s", store_target);
			update_info_text(info_text);
			g_free(info_text);
		}
		else {
			update_info_text("Failed to write to the .qiv-select subdirectory");
		}

		g_free(store_target);
		return;
	}

	gchar *current_file_basename = g_path_get_basename(CURRENT_FILE->file_name);
	gchar *link_target = g_strdup_printf("./.qiv-select/%s", current_file_basename);

	if(g_file_test(link_target, G_FILE_TEST_EXISTS)) {
		g_free(link_target);
		g_free(current_file_basename);
		update_info_text("File already exists in .qiv-select");
		gtk_widget_queue_draw(GTK_WIDGET(main_window));
		return;
	}

	g_mkdir("./.qiv-select", 0755);
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
			update_info_text("Failed to write to the .qiv-select subdirectory");
		}
		g_free(store_target);
	}
	else {
		update_info_text("Created hard-link into .qiv-select");
	}
	g_free(link_target);
	g_free(current_file_basename);
	gtk_widget_queue_draw(GTK_WIDGET(main_window));
}/*}}}*/
gboolean slideshow_timeout_callback(gpointer user_data) {/*{{{*/
	relative_image_movement(1);
	return TRUE;
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
	gtk_tree_model_get_value(model, iter, 0, &col_data);
	size_t jump_to = g_value_get_long(&col_data);
	g_value_unset(&col_data);

	g_timeout_add(200, absolute_image_movement_callback, (gpointer)(jump_to - 1));
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
		GTK_STOCK_OK,
		GTK_RESPONSE_ACCEPT,
		NULL);
	
	GtkWidget *search_entry = gtk_entry_new();
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dlg_window))),
		search_entry,
		FALSE,
		TRUE,
		0);

	// Build list for searching
	GtkListStore *search_list = gtk_list_store_new(2, G_TYPE_LONG, G_TYPE_STRING);
	for(size_t id=0; id<file_list->len; id++) {
		gtk_list_store_append(search_list, &search_list_iter);

		gchar *display_name;
		if((FILE_LIST_ENTRY(id)->file_type & FILE_TYPE_MEMORY_IMAGE) != 0) {
			display_name = g_strdup_printf("-");
		}
		else {
			display_name = g_filename_display_name(FILE_LIST_ENTRY(id)->file_name);
		}
		gtk_list_store_set(search_list, &search_list_iter,
			0, id + 1,
			1, display_name,
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
	GtkTreePath *goto_active_path = gtk_tree_path_new_from_indices(current_image, -1);
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

	if(CURRENT_FILE->image_surface == NULL) {
		// Image not loaded yet. Abort.
		if(current_info_text == NULL) {
			current_info_text = g_strdup("");
		}
		return;
	}

	// Free old info text
	if(current_info_text != NULL) {
		g_free(current_info_text);
		current_info_text = NULL;
	}

	// Update info text
	if(option_show_info_text) {
		current_info_text = g_strdup_printf("%s (%dx%d) %03.2f%% [%d/%d]", display_name,
			cairo_image_surface_get_width(CURRENT_FILE->image_surface),
			cairo_image_surface_get_height(CURRENT_FILE->image_surface),
			current_scale_level * 100.,
			(unsigned int)(current_image + 1),
			(unsigned int)(file_list->len));

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
			g_string_append_printf(new_window_title, "%d", (unsigned int)(current_image + 1));
			window_title_iter += 12;
		}
		else if(g_strstr_len(window_title_iter, 11, "IMAGE_COUNT") != NULL) {
			g_string_append_printf(new_window_title, "%d", (unsigned int)(file_list->len));
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
gboolean window_draw_callback(GtkWidget *widget, cairo_t *cr, gpointer user_data) {/*{{{*/
	// Draw black background
	cairo_save(cr);
	cairo_set_source_rgba(cr, 0., 0., 0., option_transparent_background ? 0. : 1.);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_paint(cr);
	cairo_restore(cr);

	// Draw image
	if(CURRENT_FILE->image_surface != NULL) {
		current_image_drawn = TRUE;

		int image_width = cairo_image_surface_get_width(CURRENT_FILE->image_surface);
		int image_height = cairo_image_surface_get_height(CURRENT_FILE->image_surface);

		int x = (main_window_width - current_scale_level * image_width) / 2;
		int y = (main_window_height - current_scale_level * image_height) / 2;

		cairo_save(cr);

		cairo_translate(cr, x, y);
		cairo_translate(cr, current_shift_x, current_shift_y);
		cairo_scale(cr, current_scale_level, current_scale_level);

		if(background_checkerboard_pattern != NULL && !option_transparent_background) {
			cairo_save(cr);
			cairo_new_path(cr);
			cairo_rectangle(cr, 1, 1, image_width - 2, image_height - 2);
			cairo_close_path(cr);
			cairo_clip(cr);
			cairo_set_source(cr, background_checkerboard_pattern);
			cairo_paint(cr);
			cairo_restore(cr);
		}

		cairo_set_source_surface(cr, CURRENT_FILE->image_surface, 0, 0);
		cairo_paint(cr);

		cairo_restore(cr);
	
		// Draw info box
		if(current_info_text != NULL) {
			double x1, x2, y1, y2;
			cairo_save(cr);
			cairo_set_font_size(cr, 12);

			if(main_window_in_fullscreen == FALSE) {
				// Tiling WMs, at least i3, react weird on our window size changing.
				// Drawing the info box on the image helps to avoid users noticing that.
				cairo_translate(cr, x < 0 ? 0 : x, y < 0 ? 0 : y);
			}

			cairo_set_source_rgb(cr, 1., 1., 0.);
			cairo_translate(cr, 10, 20);

			cairo_text_path(cr, current_info_text);
			cairo_path_extents(cr, &x1, &y1, &x2, &y2);
			cairo_new_path(cr);
			cairo_rectangle(cr, -5, -15, x2 - x1 + 10, y2 - y1 + 10);
			cairo_close_path(cr);
			cairo_fill(cr);
			
			cairo_set_source_rgb(cr, 0., 0., 0.);
			cairo_show_text(cr, current_info_text);
			cairo_restore(cr);
		}
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

		current_scale_level = 1.0;
		if(option_scale > 0 || !main_window_in_fullscreen || scale_override) {
			if(option_scale > 1 || scale_override) {
				// Scale up
				if(image_width * current_scale_level < main_window_width) {
					current_scale_level = main_window_width * 1.0 / image_width;
				}

				if(image_height * current_scale_level < main_window_height) {
					current_scale_level = main_window_height * 1.0 / image_height;
				}
			}

			// Scale down
			if(main_window_height < current_scale_level * image_height) {
				current_scale_level = main_window_height * 1.0 / image_height;
			}
			if(main_window_width < current_scale_level * image_width) {
				current_scale_level = main_window_width * 1.0 / image_width;
			}
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
	}

	if(main_window_width != event->width || main_window_height != event->height) {
		// Update window size
		main_window_width = event->width;
		main_window_height = event->height;

		// Rescale the image, unless overridden by the user
		if(option_initial_scale_used) {
			set_scale_level_to_fit();
			update_info_text(NULL);
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

	if(CURRENT_FILE->image_surface == NULL) {
		// Do not handle anything if the image is not loaded yet
		return FALSE;
	}

	if(event->keyval < 128 && keyboard_aliases[event->keyval] != 0) {
		event->keyval = keyboard_aliases[event->keyval];
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
			current_scale_level *= 1.1;
			if(current_scale_level > 1 && option_scale != 2) {
				scale_override = TRUE;
			}
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
			if(current_scale_level <= 0.01) {
				break;
			}
			current_scale_level /= 1.1;
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
			queue_image_load(current_image);
			break;

		case GDK_KEY_0:
			current_image_drawn = FALSE;
			set_scale_level_to_fit();
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
			option_show_info_text = !option_show_info_text;
			update_info_text(NULL);
			gtk_widget_queue_draw(GTK_WIDGET(main_window));
			break;

		case GDK_KEY_j:
		case GDK_KEY_J:
			do_jump_dialog();
			break;

		case GDK_KEY_s:
		case GDK_KEY_S:
			if(slideshow_timeout_id != 0) {
				g_source_remove(slideshow_timeout_id);
				slideshow_timeout_id = 0;
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
			GdkWindow *window = gtk_widget_get_window(GTK_WIDGET(main_window));
			gdk_window_set_cursor(window, NULL);

			current_image_drawn = FALSE;

			// Rescale the image and remove shift when leaving fullscreen
			current_shift_x = 0;
			current_shift_y = 0;
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
				gtk_window_move(main_window, 0, 0);
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

	if(option_start_fullscreen) {
		// Assume fullscreen right from the start. This allows for correct positioning
		// in X servers without WMs.
		/*main_window_in_fullscreen = TRUE;
		gtk_window_move(main_window, 0, 0);*/ // TODO Is this required?
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

	GdkScreen *screen = gdk_screen_get_default();
	guint monitor = gdk_screen_get_primary_monitor(screen);
	gdk_screen_get_monitor_geometry(screen, monitor, &screen_geometry);

	if(option_transparent_background) {
		window_screen_activate_rgba();
	}
}/*}}}*/
// }}}

int main(int argc, char *argv[]) {
	#if (!GLIB_CHECK_VERSION(2, 32, 0))
		g_thread_init(NULL);
		gdk_threads_init();
	#endif
	gtk_init(&argc, &argv);

	parse_configuration_file(&argc, &argv);
	parse_command_line(&argc, argv);
	if(fabs(option_initial_scale - 1.0) < 2 * FLT_MIN) {
		option_initial_scale_used = TRUE;
	}
	else {
		current_scale_level = option_initial_scale;
	}

	load_images(&argc, argv);

	if(file_list->len == 0) {
		return 0;
	}

	setup_checkerboard_pattern();
	initialize_image_loader();

	create_window();

	absolute_image_movement(current_image);

	if(option_start_with_slideshow_mode) {
		slideshow_timeout_id = g_timeout_add(option_slideshow_interval * 1000, slideshow_timeout_callback, NULL);
	}

	gtk_main();

	return 0;
}

// vim:noet ts=4 sw=4 tw=0 fdm=marker
