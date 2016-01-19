/**
 * pqiv
 *
 * Copyright (c) 2013-2014, Phillip Berndt
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

#include "pqiv.h"

#include "lib/strnatcmp.h"
#include <cairo/cairo.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <assert.h>

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
	#include <gio/gwin32inputstream.h>
#else
	#include <sys/wait.h>
	#include <gdk/gdkx.h>
	#include <gio/gunixinputstream.h>
	#include <X11/Xlib.h>

	#if GTK_MAJOR_VERSION < 3
		#include <X11/Xatom.h>
	#endif
#endif

#ifdef DEBUG
	#ifndef _WIN32
		#include <sys/resource.h>
	#endif
	#define PQIV_VERSION_DEBUG "-debug"
#else
	#define PQIV_VERSION_DEBUG ""
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

// Global variables and function signatures {{{

// The list of file type handlers and file type initializer function
void initialize_file_type_handlers();

// Storage of the file list
// These lists are accessed from multiple threads:
//  * The main thread (count, next, prev, ..)
//  * The option parser thread, if --lazy-load is used
//  * The image loader thread
// Our thread safety strategy is as follows:
//  * Access directory_tree only from the option parser
//  * Wrap all file_tree operations with mutexes
//  * Use weak references for any operation during which the image might
//    invalidate.
//  * If a weak reference is invalid, abort the pending operation
//  * If an operation can't be aborted, lock the mutex from the start
//    until it completes
//  * If an operation takes too long for this to work, redesign the
//    operation
G_LOCK_DEFINE_STATIC(file_tree);
// In case of trouble:
// #define D_LOCK(x) g_print("Waiting for lock " #x " at line %d\n", __LINE__); G_LOCK(x); g_print("  Locked " #x " at line %d\n", __LINE__)
// #define D_UNLOCK(x) g_print("Unlocked " #x " at line %d\n", __LINE__); G_UNLOCK(x);
#define D_LOCK(x) G_LOCK(x)
#define D_UNLOCK(x) G_UNLOCK(x)
BOSTree *file_tree;
BOSTree *directory_tree;
BOSNode *current_file_node = NULL;
BOSNode *image_loader_thread_currently_loading = NULL;
gboolean file_tree_valid = FALSE;

// We asynchroniously load images in a separate thread
GAsyncQueue *image_loader_queue = NULL;
GCancellable *image_loader_cancellable = NULL;

// Unloading of files is also handled by that thread, in a GC fashion
// For that, we keep a list of loaded files
GList *loaded_files_list = NULL;

// Filter for path traversing upon building the file list
GHashTable *load_images_file_filter_hash_table;
GtkFileFilterInfo *load_images_file_filter_info;
GTimer *load_images_timer;

// Easy access to the file_t within a node
// Remember to always lock file_tree!
#define FILE(x) ((file_t *)(x)->data)
#define CURRENT_FILE FILE(current_file_node)
#define next_file() relative_image_pointer(1)
#define previous_file() relative_image_pointer(-1)

// The node to be displayed first, used in conjunction with --browse
BOSNode *browse_startup_node = NULL;

// When loading additional images via the -r option, we need to know whether the
// image loader initialization succeeded, because we can't just cancel if it does
// not (it checks if any image is loadable and fails if not)
gboolean image_loader_initialization_succeeded = FALSE;

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
gboolean wm_supports_fullscreen = TRUE;

// If a WM indicates no moveresize support that's a hint it's a tiling WM
gboolean wm_supports_moveresize = TRUE;

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
cairo_rectangle_int_t current_info_text_bounding_box = { 0, 0, 0, 0 };


// Current state of the displayed image and user interaction
// This matrix stores rotations and reflections (makes ui with scaling/transforming easier)
cairo_matrix_t current_transformation;
gdouble current_scale_level = 1.0;
gint current_shift_x = 0;
gint current_shift_y = 0;
guint32 last_button_press_time = 0;
guint current_image_animation_timeout_id = 0;

// -1 means no slideshow, 0 means active slideshow but no current timeout
// source set, anything bigger than that actually is a slideshow id.
gint slideshow_timeout_id = -1;

// A list containing references to the images in shuffled order
typedef struct {
	gboolean viewed;
	BOSNode *node;
} shuffled_image_ref_t;
guint shuffled_images_visited_count = 0;
guint shuffled_images_list_length = 0;
GList *shuffled_images_list = NULL;
#define LIST_SHUFFLED_IMAGE(x) (((shuffled_image_ref_t *)x->data))

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
gdouble option_slideshow_interval = 5.;
gboolean option_hide_info_box = FALSE;
gboolean option_start_fullscreen = FALSE;
gdouble option_initial_scale = 1.0;
gboolean option_initial_scale_used = FALSE;
gboolean option_start_with_slideshow_mode = FALSE;
gboolean option_sort = FALSE;
enum { NAME, MTIME } option_sort_key = NAME;
gboolean option_shuffle = FALSE;
gboolean option_reverse_cursor_keys = FALSE;
gboolean option_reverse_scroll = FALSE;
gboolean option_transparent_background = FALSE;
gboolean option_watch_directories = FALSE;
gboolean option_fading = FALSE;
gboolean option_lazy_load = FALSE;
gboolean option_lowmem = FALSE;
gboolean option_addl_from_stdin = FALSE;
double option_fading_duration = .5;
gint option_max_depth = -1;
gboolean option_browse = FALSE;
enum { QUIT, WAIT, WRAP, WRAP_NO_RESHUFFLE } option_end_of_files_action = WRAP;
enum { ON, OFF, CHANGES_ONLY } option_watch_files = ON;

double fading_current_alpha_stage = 0;
gint64 fading_initial_time;

gboolean options_keyboard_alias_set_callback(const gchar *option_name, const gchar *value, gpointer data, GError **error);
gboolean option_window_position_callback(const gchar *option_name, const gchar *value, gpointer data, GError **error);
gboolean option_scale_level_callback(const gchar *option_name, const gchar *value, gpointer data, GError **error);
gboolean option_end_of_files_action_callback(const gchar *option_name, const gchar *value, gpointer data, GError **error);
gboolean option_watch_files_callback(const gchar *option_name, const gchar *value, gpointer data, GError **error);
gboolean option_sort_key_callback(const gchar *option_name, const gchar *value, gpointer data, GError **error);
void load_images_handle_parameter(char *param, load_images_state_t state, gint depth);

struct {
	gint x;
	gint y;
} option_window_position = { -2, -2 };

// Hint: Only types G_OPTION_ARG_NONE, G_OPTION_ARG_STRING, G_OPTION_ARG_DOUBLE/INTEGER and G_OPTION_ARG_CALLBACK are
// implemented for option parsing.
GOptionEntry options[] = {
	{ "keyboard-alias", 'a', 0, G_OPTION_ARG_CALLBACK, (gpointer)&options_keyboard_alias_set_callback, "Define n as a keyboard alias for f", "nfnf.." },
	{ "transparent-background", 'c', 0, G_OPTION_ARG_NONE, &option_transparent_background, "Borderless transparent window", NULL },
	{ "slideshow-interval", 'd', 0, G_OPTION_ARG_DOUBLE, &option_slideshow_interval, "Set slideshow interval", "n" },
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

	{ "browse", 0, 0, G_OPTION_ARG_NONE, &option_browse, "For each command line argument, additionally load all images from the image's directory", NULL },
	{ "disable-scaling", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, (gpointer)&option_scale_level_callback, "Disable scaling of images", NULL },
	{ "end-of-files-action", 0, 0, G_OPTION_ARG_CALLBACK, (gpointer)&option_end_of_files_action_callback, "Action to take after all images have been viewed. (`quit', `wait', `wrap', `wrap-no-reshuffle')", "ACTION" },
	{ "fade-duration", 0, 0, G_OPTION_ARG_DOUBLE, &option_fading_duration, "Adjust fades' duration", "SECONDS" },
	{ "low-memory", 0, 0, G_OPTION_ARG_NONE, &option_lowmem, "Try to keep memory usage to a minimum", NULL },
	{ "max-depth", 0, 0, G_OPTION_ARG_INT, &option_max_depth, "Descend at most LEVELS levels of directories below the command line arguments", "LEVELS" },
	{ "reverse-scroll", 0, 0, G_OPTION_ARG_NONE, &option_reverse_scroll, "Reverse the meaning of scroll wheel", NULL },
	{ "shuffle", 0, 0, G_OPTION_ARG_NONE, &option_shuffle, "Shuffle files", NULL },
	{ "sort-key", 0, 0, G_OPTION_ARG_CALLBACK, (gpointer)&option_sort_key_callback, "Key to use for sorting", "PROPERTY" },
	{ "watch-directories", 0, 0, G_OPTION_ARG_NONE, &option_watch_directories, "Watch directories for new files", NULL },
	{ "watch-files", 0, 0, G_OPTION_ARG_CALLBACK, (gpointer)&option_watch_files_callback, "Watch files for changes on disk (`on`, `off', `changes-only', i.e. do nothing on deletetion)", "VALUE" },

	{ NULL, 0, 0, 0, NULL, NULL, NULL }
};

const char *long_description_text = ("Keyboard & Mouse bindings:\n"
"  Backspace, Button 1, Scroll, p     Previous image\n"
"  PgUp/PgDown                        Jump 10 images forwards/backwards\n"
"  Escape, q, Button 2                Quit\n"
"  Cursor keys, Drag & Drop           Move (CTRL to move faster)\n"
"  Space, Button 3, Scroll, n         Next image\n"
"  CTRL Space/Backspace               Move to first image in next/previous directory\n"
"  f                                  Toggle fullscreen\n"
"  j                                  Jump to an image (Shows a selection box)\n"
"  r                                  Reload image\n"
"  CTRL r                             Toggle shuffle mode\n"
"  +/-/0, Button 3 & Drag             Zoom in/out/reset zoom\n"
"  t                                  Toggle autoscale\n"
"  l/k                                Rotate left/right\n"
"  h/v                                Flip horizontally/vertically\n"
"  i                                  Toggle info box\n"
"  s                                  Toggle slideshow mode\n"
"  CTRL +/-                           Change slideshow interval\n"
"  a                                  Hardlink current image to ./.pqiv-select\n"
);

typedef struct {
	gint depth;
} directory_watch_options_t;

void set_scale_level_to_fit();
void set_scale_level_for_screen();
void info_text_queue_redraw();
void update_info_text(const char *);
void queue_draw();
gboolean main_window_center();
void window_screen_changed_callback(GtkWidget *widget, GdkScreen *previous_screen, gpointer user_data);
gboolean image_loader_load_single(BOSNode *node, gboolean called_from_main);
gboolean fading_timeout_callback(gpointer user_data);
void queue_image_load(BOSNode *);
void unload_image(BOSNode *);
gboolean initialize_gui_callback(gpointer);
gboolean initialize_image_loader();
void fullscreen_hide_cursor();
void fullscreen_show_cursor();
void window_center_mouse();
void calculate_current_image_transformed_size(int *image_width, int *image_height);
cairo_surface_t *get_scaled_image_surface_for_current_image();
void window_state_into_fullscreen_actions();
void window_state_out_of_fullscreen_actions();
BOSNode *relative_image_pointer(ptrdiff_t movement);
void file_tree_free_helper(BOSNode *node);
gint relative_image_pointer_shuffle_list_cmp(shuffled_image_ref_t *ref, BOSNode *node);
void relative_image_pointer_shuffle_list_unref_fn(shuffled_image_ref_t *ref);
gboolean slideshow_timeout_callback(gpointer user_data);
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
gboolean option_watch_files_callback(const gchar *option_name, const gchar *value, gpointer data, GError **error) {/*{{{*/
	if(strcmp(value, "off") == 0) {
		option_watch_files = OFF;
	}
	else if(strcmp(value, "on") == 0) {
		option_watch_files = ON;
	}
	else if(strcmp(value, "changes-only") == 0) {
		option_watch_files = CHANGES_ONLY;
	}
	else {
		g_set_error(error, G_OPTION_ERROR, G_OPTION_ERROR_FAILED, "Unexpected argument value for the --watch-files option. Allowed values are: on, off and changes-only.");
		return FALSE;
	}
	return TRUE;
}/*}}}*/
gboolean option_end_of_files_action_callback(const gchar *option_name, const gchar *value, gpointer data, GError **error) {/*{{{*/
	if(strcmp(value, "quit") == 0) {
		option_end_of_files_action = QUIT;
	}
	else if(strcmp(value, "wait") == 0) {
		option_end_of_files_action = WAIT;
	}
	else if(strcmp(value, "wrap") == 0) {
		option_end_of_files_action = WRAP;
	}
	else if(strcmp(value, "wrap-no-reshuffle") == 0) {
		option_end_of_files_action = WRAP_NO_RESHUFFLE;
	}
	else {
		g_set_error(error, G_OPTION_ERROR, G_OPTION_ERROR_FAILED, "Unexpected argument value for the --end-of-files-action option. Allowed values are: quit, wait, wrap (default) and wrap-no-reshuffle.");
		return FALSE;
	}
	return TRUE;
}/*}}}*/
gboolean option_sort_key_callback(const gchar *option_name, const gchar *value, gpointer data, GError **error) {/*{{{*/
	if(strcmp(value, "name") == 0) {
		option_sort_key = NAME;
	}
	else if(strcmp(value, "mtime") == 0) {
		option_sort_key = MTIME;
	}
	else {
		g_set_error(error, G_OPTION_ERROR, G_OPTION_ERROR_FAILED, "Unexpected argument value for the --sort-key option. Allowed keys are: name, mtime.");
		return FALSE;
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
							gchar long_name[64];
							g_snprintf(long_name, 64, "--%s", iter->long_name);
							((GOptionArgFunc)(iter->arg_data))(long_name, option_value, NULL, &error_pointer);
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
	g_option_context_set_summary(parser, "A minimalist image viewer\npqiv version " PQIV_VERSION PQIV_VERSION_DEBUG " by Phillip Berndt");
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

	// User didn't specify any files to load; perhaps some help on how to use
	// pqiv would be useful...
	if (*argc == 1 && !option_addl_from_stdin) {
		g_printerr("%s", g_option_context_get_help(parser, TRUE, NULL));
		exit(0);
	}

	g_option_context_free(parser);
}/*}}}*/
void load_images_directory_watch_callback(GFileMonitor *monitor, GFile *file, GFile *other_file, GFileMonitorEvent event_type, directory_watch_options_t *options) {/*{{{*/
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
				load_images_handle_parameter(name, INOTIFY, options->depth);
			}
			g_free(name);
		}
	}
	// We cannot reliably react on G_FILE_MONITOR_EVENT_DELETED here, because either the tree
	// is unsorted, in which case it is indexed by numbers, or it is sorted by the display
	// name (important for multi-page documents!), which can be a relative name that is not
	// lookupable as well.
	//
	// Therefore we do not remove files here, but instead rely on nodes being deleted once the
	// user tries to access then. For already loaded files (i.e. also the next/previous one),
	// the file watch is used to remove the files.
}/*}}}*/
BOSNode *load_images_handle_parameter_add_file(load_images_state_t state, file_t *file) {/*{{{*/
	// Add image to images list/tree
	// We need to check if the previous/next images have changed, because they
	// might have been preloaded and need unloading if so.
	D_LOCK(file_tree);

	// The file tree might have been invalidated if the user exited pqiv while a loader
	// was processing a file. In that case, just cancel and free the file.
	if(!file_tree_valid) {
		file_free(file);
		D_UNLOCK(file_tree);
		return NULL;
	}

	BOSNode *new_node = NULL;
	if(!option_sort) {
		float *index = g_slice_new0(float);
		if(state == FILTER_OUTPUT) {
			// As index, use
			//  min(index(current) + .001, .5 index(current) + .5 index(next))
			*index = 1.001f * *(float *)current_file_node->key;

			BOSNode *next_node = bostree_next_node(current_file_node);
			if(next_node) {
				float alternative = .5f * (*(float *)current_file_node->key + *(float *)next_node->key);
				*index = fminf(*index, alternative);
			}
		}
		else {
			*index = (float)bostree_node_count(file_tree);
		}
		new_node = bostree_insert(file_tree, (void *)index, file);
	}
	else {
		new_node = bostree_insert(file_tree, file->sort_name, file);
	}
	if(state == BROWSE_ORIGINAL_PARAMETER && browse_startup_node == NULL) {
		browse_startup_node = bostree_node_weak_ref(new_node);
	}
	D_UNLOCK(file_tree);
	if(option_lazy_load && !gui_initialized) {
		// When the first image has been processed, we can show the window
		// Since it might not successfully load, we might need to call this
		// multiple times. We cannot load the image in this thread because some
		// backends have a global mutex and would call this function with
		// the mutex locked.
		if(!gui_initialized) {
			gdk_threads_add_idle(initialize_gui_callback, NULL);
		}
	}
	if(state == INOTIFY) {
		// If this image was loaded via the INOTIFY handler, we need to update
		// the info text. We do not update it here for images loaded via the
		// --lazy-load function (i.e. check for main_window_visible /
		// gui_initialized), because the high frequency of Xlib calls crashes
		// the app (with an Xlib resource unavailable error) at least on my
		// development machine.
		update_info_text(NULL);
		info_text_queue_redraw();
	}
	return new_node;
}/*}}}*/
GBytes *g_input_stream_read_completely(GInputStream *input_stream, GCancellable *cancellable, GError **error_pointer) {/*{{{*/
	size_t data_length = 0;
	char *data = g_malloc(1<<23); // + 8 Mib
	while(TRUE) {
		gsize bytes_read;
		if(!g_input_stream_read_all(input_stream, &data[data_length], 1<<23, &bytes_read, cancellable, error_pointer)) {
			g_free(data);
			return 0;
		}
		data_length += bytes_read;
		if(bytes_read < 1<<23) {
			data = g_realloc(data, data_length);
			break;
		}
		else {
			data = g_realloc(data, data_length + (1<<23));
		}
	}
	return g_bytes_new_take((guint8*)data, data_length);
}/*}}}*/
GFile *gfile_for_commandline_arg(const char *parameter) {/*{{{*/
		// Support for URIs is an extra feature. To prevent breaking compatibility,
		// always prefer existing files over URI interpretation.
		// For example, all files containing a colon cannot be read using the
		// g_file_new_for_commandline_arg command, because they are interpreted
		// as an URI with an unsupported scheme.
		if(g_file_test(parameter, G_FILE_TEST_EXISTS)) {
			return g_file_new_for_path(parameter);
		}
		else {
			return g_file_new_for_commandline_arg(parameter);
		}
}/*}}}*/
gboolean load_images_handle_parameter_find_handler(const char *param, load_images_state_t state, file_t *file, GtkFileFilterInfo *file_filter_info) {/*{{{*/
	// Check if one of the file type handlers can handle this file
	file_type_handler_t *file_type_handler = &file_type_handlers[0];
	while(file_type_handler->file_types_handled) {
		if(gtk_file_filter_filter(file_type_handler->file_types_handled, file_filter_info) == TRUE) {
			file->file_type = file_type_handler;

			// Handle using this handler
			if(file_type_handler->alloc_fn != NULL) {
				file_type_handler->alloc_fn(state, file);
			}
			else {
				load_images_handle_parameter_add_file(state, file);
			}
			return TRUE;
		}

		file_type_handler++;
	}

	return FALSE;
}/*}}}*/
void load_images_handle_parameter(char *param, load_images_state_t state, gint depth) {/*{{{*/
	file_t *file;

	// If the file tree has been invalidated, cancel.
	if(!file_tree_valid) {
		return;
	}

	// Check for memory image
	if(state == PARAMETER && g_strcmp0(param, "-") == 0) {
		file = g_slice_new0(file_t);
		file->file_flags = FILE_FLAGS_MEMORY_IMAGE;
		file->display_name = g_strdup("-");
		if(option_sort) {
			file->sort_name = g_strdup("-");
		}
		file->file_name = g_strdup("-");

		GError *error_ptr = NULL;
		#ifdef _WIN32
			GInputStream *stdin_stream = g_win32_input_stream_new(GetStdHandle(STD_INPUT_HANDLE), FALSE);
		#else
			GInputStream *stdin_stream = g_unix_input_stream_new(0, FALSE);
		#endif
		file->file_data = g_input_stream_read_completely(stdin_stream, NULL, &error_ptr);
		if(!file->file_data) {
			g_printerr("Failed to load image from stdin: %s\n", error_ptr->message);
			g_clear_error(&error_ptr);
			g_slice_free(file_t, file);
			g_object_unref(stdin_stream);
			return;
		}
		g_object_unref(stdin_stream);

		// Based on the file data, make a guess on the mime type
		gsize file_content_size;
		gconstpointer file_content = g_bytes_get_data(file->file_data, &file_content_size);
		gchar *file_content_type = g_content_type_guess(NULL, file_content, file_content_size, NULL);
		gchar *file_mime_type = g_content_type_get_mime_type(file_content_type);
		g_free(file_content_type);

		GtkFileFilterInfo mime_guesser;
		mime_guesser.contains = GTK_FILE_FILTER_MIME_TYPE;
		mime_guesser.mime_type = file_mime_type;

		if(!load_images_handle_parameter_find_handler(param, state, file, &mime_guesser)) {
			// As a last resort, use the default file type handler
			g_printerr("Didn't recognize memory file: Its MIME-type `%s' is unknown. Fall-back to default file handler.\n", file_mime_type);
			file->file_type = &file_type_handlers[0];
			file->file_type->alloc_fn(state, file);
		}

		g_free(file_mime_type);
	}
	else {
		// If the browse option is enabled, add the containing directory's images instead of the parameter itself
		gchar *original_parameter = NULL;
		if(state == PARAMETER && option_browse && g_file_test(param, G_FILE_TEST_IS_SYMLINK | G_FILE_TEST_IS_REGULAR) == TRUE) {
			// Handle the actual parameter first, such that it is displayed
			// first (unless sorting is enabled)
			load_images_handle_parameter(param, BROWSE_ORIGINAL_PARAMETER, 0);

			// Decrease depth such that the following recursive invocations
			// will again have depth 0 (this is the base directory, after all)
			depth -= 1;

			// Replace param with the containing directory's name
			original_parameter = param;
			param = g_path_get_dirname(param);
		}

		// Recurse into directories
		if(g_file_test(param, G_FILE_TEST_IS_DIR) == TRUE) {
			if(option_max_depth >= 0 && option_max_depth <= depth) {
				// Maximum depth exceeded, abort.
				return;
			}

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
					if(original_parameter != NULL) {
						g_free(param);
					}
					return;
				}
				bostree_insert(directory_tree, g_strdup(abs_path), NULL);
			}
			else {
				// Consider this an error
				if(original_parameter != NULL) {
					g_free(param);
				}
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
				if(original_parameter != NULL) {
					g_free(param);
				}
				return;
			}
			while(TRUE) {
				const gchar *dir_entry = g_dir_read_name(dir_ptr);
				if(dir_entry == NULL) {
					break;
				}
				gchar *dir_entry_full = g_strdup_printf("%s%s%s", param, g_str_has_suffix(param, G_DIR_SEPARATOR_S) ? "" : G_DIR_SEPARATOR_S, dir_entry);
				if(!(original_parameter != NULL && g_strcmp0(dir_entry_full, original_parameter) == 0)) {
					// Skip if we are in --browse mode and this is the file which we have already added above.
					load_images_handle_parameter(dir_entry_full, RECURSION, depth + 1);
				}
				g_free(dir_entry_full);

				// If the file tree has been invalidated, cancel.
				if(!file_tree_valid) {
					return;
				}
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
					directory_watch_options_t *options = g_new0(directory_watch_options_t, 1);
					options->depth = depth;
					g_signal_connect(directory_monitor, "changed", G_CALLBACK(load_images_directory_watch_callback), options);
					// We do not store the directory_monitor anywhere, because it is not used explicitly
					// again. If this should ever be needed, this is the place where this should be done.
				}
				g_object_unref(file_ptr);
			}

			if(original_parameter != NULL) {
				g_free(param);
			}
			return;
		}

		// Prepare file structure
		file = g_slice_new0(file_t);
		file->display_name = g_filename_display_name(param);
		if(option_sort) {
			if(option_sort_key == MTIME) {
				// Prepend the modification time to the display name
				GFile *param_file = gfile_for_commandline_arg(param);
				if(param_file) {
					GFileInfo *file_info = g_file_query_info(param_file, G_FILE_ATTRIBUTE_TIME_MODIFIED, G_FILE_QUERY_INFO_NONE, NULL, NULL);
					if(file_info) {
						GTimeVal result;
						g_file_info_get_modification_time(file_info, &result);
						g_object_unref(file_info);
						file->sort_name = g_strdup_printf("%lu;%s", result.tv_sec, file->display_name);
					}
					g_object_unref(param_file);
				}
			}
			if(file->sort_name == NULL) {
				file->sort_name = g_strdup(file->display_name);
			}
		}

		// In sorting/watch-directories mode, we store the full path to the file in file_name, to be able
		// to identify the file if it is deleted
		if(option_watch_directories && option_sort) {
			char abs_path[PATH_MAX];
			if(
				#ifdef _WIN32
					GetFullPathNameA(param, PATH_MAX, abs_path, NULL) != 0
				#else
					realpath(param, abs_path) != NULL
				#endif
			) {
				file->file_name = g_strdup(abs_path);
			}
			else {
				file->file_name = g_strdup(param);
			}
		}
		else {
			file->file_name = g_strdup(param);
		}

		// Filter based on formats supported by the different handlers
		gchar *param_lowerc = g_utf8_strdown(param, -1);
		load_images_file_filter_info->filename = load_images_file_filter_info->display_name = param_lowerc;

		// Check if one of the file type handlers can handle this file
		if(load_images_handle_parameter_find_handler(param, state, file, load_images_file_filter_info)) {
			g_free(param_lowerc);
			return;
		}
		g_free(param_lowerc);

		if(state != PARAMETER && state != BROWSE_ORIGINAL_PARAMETER) {
			// At this point, if the file was not mentioned explicitly by the user,
			// abort.
			return;
		}

		// Make a final attempt to guess the file type by mime type
		GFile *param_file = gfile_for_commandline_arg(param);
		if(!param_file) {
			return;
		}

		GFileInfo *file_info = g_file_query_info(param_file, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE, G_FILE_QUERY_INFO_NONE, NULL, NULL);
		if(file_info) {
			gchar *param_file_mime_type = g_content_type_get_mime_type(g_file_info_get_content_type(file_info));
			if(param_file_mime_type) {
				GtkFileFilterInfo mime_guesser;
				mime_guesser.contains = GTK_FILE_FILTER_MIME_TYPE;
				mime_guesser.mime_type = param_file_mime_type;

				if(load_images_handle_parameter_find_handler(param, state, file, &mime_guesser)) {
					g_free(param_file_mime_type);
					g_object_unref(param_file);
					g_object_unref(file_info);
					return;
				}
				else {
					g_printerr("Didn't recognize file `%s': Both its extension and MIME-type `%s' are unknown. Fall-back to default file handler.\n", param, param_file_mime_type);
					g_free(param_file_mime_type);
					g_object_unref(param_file);
				}
			}
			g_object_unref(file_info);
		}

		// If nothing else worked, assume that this file is handled by the default handler

		// Prepare file structure
		file->file_type = &file_type_handlers[0];
		file_type_handlers[0].alloc_fn(state, file);
	}
}/*}}}*/
int image_tree_float_compare(const float *a, const float *b) {/*{{{*/
	return *a > *b;
}/*}}}*/
void file_free(file_t *file) {/*{{{*/
	if(file->file_type->free_fn != NULL && file->private) {
		file->file_type->free_fn(file);
	}
	g_free(file->display_name);
	g_free(file->file_name);
	if(file->sort_name) {
		g_free(file->sort_name);
	}
	if(file->file_data) {
		g_bytes_unref(file->file_data);
		file->file_data = NULL;
	}
	g_slice_free(file_t, file);
}/*}}}*/
void file_tree_free_helper(BOSNode *node) {
	// This helper function is only called once a node is eventually freed,
	// which happens only after the last weak reference to it is dropped,
	// which happens only if an image is not in the list of loaded images
	// anymore, which happens only if it never was loaded or was just
	// unloaded. So the call to unload_image should have no side effects,
	// and is only there for redundancy to be absolutely sure..
	unload_image(node);

	file_free(FILE(node));
	if(!option_sort) {
		g_slice_free(float, node->key);
	}
}
void directory_tree_free_helper(BOSNode *node) {
	free(node->key);
	// value is NULL
}
void load_images(int *argc, char *argv[]) {/*{{{*/
	// Allocate memory for the file list (Used for unsorted and random order file lists)
	file_tree = bostree_new(
		option_sort ? (BOSTree_cmp_function)strnatcasecmp : (BOSTree_cmp_function)image_tree_float_compare,
		file_tree_free_helper
	);
	file_tree_valid = TRUE;

	// The directory tree is used to prevent nested-symlink loops
	directory_tree = bostree_new((BOSTree_cmp_function)g_strcmp0, directory_tree_free_helper);

	// Allocate memory for the timer
	load_images_timer = g_timer_new();
	g_timer_start(load_images_timer);

	// Prepare the file filter info structure used for handler detection
	load_images_file_filter_info = g_new0(GtkFileFilterInfo, 1);
	load_images_file_filter_info->contains = GTK_FILE_FILTER_FILENAME | GTK_FILE_FILTER_DISPLAY_NAME;

	// Load the images from the remaining parameters
	for(int i=1; i<*argc; i++) {
		load_images_handle_parameter(argv[i], PARAMETER, 0);
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
			load_images_handle_parameter(buffer, PARAMETER, 0);
			g_free(buffer);
		}
		g_io_channel_unref(stdin_reader);
	}

	// If we do not want to watch directories for changes, we can now drop the variables
	// we used for loading to free some space
	if(!option_watch_directories) {
		// TODO 
		// g_object_ref_sink(load_images_file_filter);
		g_free(load_images_file_filter_info);
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
	D_LOCK(file_tree);
	if((BOSNode *)user_data != current_file_node || FILE(current_file_node)->force_reload) {
		D_UNLOCK(file_tree);
		current_image_animation_timeout_id = 0;
		return FALSE;
	}
	if(CURRENT_FILE->file_type->animation_next_frame_fn == NULL) {
		D_UNLOCK(file_tree);
		current_image_animation_timeout_id = 0;
		return FALSE;
	}

	double delay = CURRENT_FILE->file_type->animation_next_frame_fn(CURRENT_FILE);
	D_UNLOCK(file_tree);

	if(delay >= 0) {
		current_image_animation_timeout_id = gdk_threads_add_timeout(
			delay,
			image_animation_timeout_callback,
			user_data);
	}
	else {
		current_image_animation_timeout_id = 0;
	}

	invalidate_current_scaled_image_surface();
	gtk_widget_queue_draw(GTK_WIDGET(main_window));

	return FALSE;
}/*}}}*/
void image_file_updated_callback(GFileMonitor *monitor, GFile *file, GFile *other_file, GFileMonitorEvent event_type, gpointer user_data) {/*{{{*/
	BOSNode *node = (BOSNode *)user_data;

	if(option_watch_files == OFF) {
		return;
	}

	D_LOCK(file_tree);
	if(event_type == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT) {
		FILE(node)->force_reload = TRUE;
		queue_image_load(node);
	}
	if(event_type == G_FILE_MONITOR_EVENT_DELETED) {
		// It is a difficult decision what to do here. We could either unload the deleted
		// image and jump to the next one, or ignore this event. I see use-cases for both.
		// Ultimatively, it feels more consistent to unload it: The same reasons why a user
		// wouldn't want pqiv to unload an image if it was deleted would also apply to
		// reloading upon changes. The one exception I see to this rule is when the current
		// image is the last in the directory: Unloading it would cause pqiv to leave.
		// A typical use case for pqiv is a picture frame on an Raspberry Pi, where users
		// periodically exchange images using scp. There might be a race condition if a user
		// is not aware that he should first move the new images to a folder and then remove
		// the old ones. Therefore, if there is only one image remaining, pqiv does nothing.
		// But as new images are added (if --watch-directories is set), the old one should
		// be removed eventually. Hence, force_reload is still set on the deleted image.
		//
		// There's another race if a user deletes all files at once. --watch-files=ignore
		// has been added for such situations, to disable this functionality
		if(option_watch_files == ON) {
			FILE(node)->force_reload = TRUE;
			if(bostree_node_count(file_tree) > 1) {
				queue_image_load(node);
			}
		}
	}
	D_UNLOCK(file_tree);
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
	D_LOCK(file_tree);
	// If there is no image loaded, abort
	if(!CURRENT_FILE->is_loaded) {
		D_UNLOCK(file_tree);
		return FALSE;
	}
	// Get the image's size
	int image_width, image_height;
	calculate_current_image_transformed_size(&image_width, &image_height);
	D_UNLOCK(file_tree);

	// In in fullscreen, also abort
	if(main_window_in_fullscreen) {
		return FALSE;
	}

	// Recalculate the required window size
	int new_window_width = current_scale_level * image_width;
	int new_window_height = current_scale_level * image_height;

	// Resize if this has not worked before, but accept a slight deviation (might be round-off error)
	if(main_window_width >= 0 && abs(main_window_width - new_window_width) + abs(main_window_height - new_window_height) > 1) {
		gtk_window_resize(main_window, new_window_width, new_window_height);
	}

	return FALSE;
}/*}}}*/
void main_window_adjust_for_image() {/*{{{*/
	// We only need to adjust the window if it is not in fullscreen
	if(main_window_in_fullscreen) {
		queue_draw();
		return;
	}

	int image_width, image_height;
	calculate_current_image_transformed_size(&image_width, &image_height);

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
			gdk_threads_add_idle(window_move_helper_callback, NULL);
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
			gdk_threads_add_idle(main_window_resize_callback, NULL);
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
}/*}}}*/
gboolean image_loaded_handler(gconstpointer node) {/*{{{*/
	D_LOCK(file_tree);

	// Remove any old timeouts etc.
	if(current_image_animation_timeout_id > 0) {
		g_source_remove(current_image_animation_timeout_id);
		current_image_animation_timeout_id = 0;
	}

	// Only react if the loaded node is still current
	if(node && node != current_file_node) {
		D_UNLOCK(file_tree);
		return FALSE;
	}

	// If in shuffle mode, mark the current image as viewed, and possibly
	// reset the list once all images have been
	if(option_shuffle) {
		GList *current_shuffled_image = g_list_find_custom(shuffled_images_list, current_file_node, (GCompareFunc)relative_image_pointer_shuffle_list_cmp);
		if(current_shuffled_image) {
			if(!LIST_SHUFFLED_IMAGE(current_shuffled_image)->viewed) {
				LIST_SHUFFLED_IMAGE(current_shuffled_image)->viewed = 1;
				if(++shuffled_images_visited_count == bostree_node_count(file_tree)) {
					if(option_end_of_files_action == WRAP) {
						g_list_free_full(shuffled_images_list, (GDestroyNotify)relative_image_pointer_shuffle_list_unref_fn);
						shuffled_images_list = NULL;
						shuffled_images_visited_count = 0;
						shuffled_images_list_length = 0;
					}
				}
			}
		}
	}

	// Sometimes when a user is hitting the next image button really fast this
	// function's execution can be delayed until CURRENT_FILE is again not loaded.
	// Return without doing anything in that case.
	if(!CURRENT_FILE->is_loaded) {
		D_UNLOCK(file_tree);
		return FALSE;
	}

	// Initialize animation timer if the image is animated
	if((CURRENT_FILE->file_flags & FILE_FLAGS_ANIMATION) != 0 && CURRENT_FILE->file_type->animation_initialize_fn != NULL) {
		current_image_animation_timeout_id = gdk_threads_add_timeout(
			CURRENT_FILE->file_type->animation_initialize_fn(CURRENT_FILE),
			image_animation_timeout_callback,
			(gpointer)current_file_node);
	}

	// Update geometry hints, calculate initial window size and place window
	D_UNLOCK(file_tree);

	// Reset shift
	current_shift_x = 0;
	current_shift_y = 0;

	// Reset rotation
	cairo_matrix_init_identity(&current_transformation);

	// Adjust scale level, resize, set aspect ratio and place window
	if(!current_image_drawn) {
		scale_override = FALSE;
	}
	set_scale_level_for_screen();
	main_window_adjust_for_image();
	invalidate_current_scaled_image_surface();
	current_image_drawn = FALSE;
	queue_draw();

	// Show window, if not visible yet
	if(!main_window_visible) {
		main_window_visible = TRUE;
		gtk_widget_show_all(GTK_WIDGET(main_window));
	}

	// Reset the info text
	update_info_text(NULL);

	return FALSE;
}/*}}}*/
GInputStream *image_loader_stream_file(file_t *file, GError **error_pointer) {/*{{{*/
	GInputStream *data;

	if((file->file_flags & FILE_FLAGS_MEMORY_IMAGE) != 0) {
		// Memory view on a memory image
		#if GLIB_CHECK_VERSION(2, 34, 0)
		data = g_memory_input_stream_new_from_bytes(file->file_data);
		#else
		gsize size = 0;
		// TODO Is it possible to use this fallback and still refcount file_data?
		data = g_memory_input_stream_new_from_data(g_bytes_get_data(file->file_data, &size), size, NULL);;
		#endif
	}
	else {
		// Classical file or URI

		if(image_loader_cancellable) {
			g_cancellable_reset(image_loader_cancellable);
		}

		GFile *input_file = gfile_for_commandline_arg(file->file_name);

		if(!input_file) {
			return NULL;
		}

		data = G_INPUT_STREAM(g_file_read(input_file, image_loader_cancellable, error_pointer));

		g_object_unref(input_file);
	}

	return data;
}/*}}}*/
file_t *image_loader_duplicate_file(file_t *file, gchar *custom_display_name, gchar *custom_sort_name) {/*{{{*/
	file_t *new_file = g_slice_new(file_t);
	*new_file = *file;

	if((file->file_flags & FILE_FLAGS_MEMORY_IMAGE)) {
		g_bytes_ref(new_file->file_data);
	}
	new_file->file_name = g_strdup(file->file_name);
	new_file->display_name = custom_display_name ? custom_display_name : g_strdup(file->display_name);
	new_file->sort_name = custom_sort_name ? custom_sort_name : (file->sort_name ? g_strdup(file->sort_name) : NULL);

	return new_file;
}/*}}}*/
gboolean image_loader_load_single(BOSNode *node, gboolean called_from_main) {/*{{{*/
	// Sanity check
	assert(bostree_node_weak_unref(file_tree, bostree_node_weak_ref(node)) != NULL);

	// Already loaded?
	file_t *file = (file_t *)node->data;
	if(file->is_loaded) {
		return TRUE;
	}

	GError *error_pointer = NULL;

	if(file->file_type->load_fn != NULL) {
		// Create an input stream for the image to be loaded
		GInputStream *data = image_loader_stream_file(file, &error_pointer);

		if(data) {
			// Let the file type handler handle the details
			file->file_type->load_fn(file, data, &error_pointer);
			g_object_unref(data);
		}
	}

	if(file->is_loaded) {
		if(error_pointer) {
			g_printerr("A recoverable error occoured: %s\n", error_pointer->message);
			g_clear_error(&error_pointer);
		}

		if((file->file_flags & FILE_FLAGS_MEMORY_IMAGE) == 0) {
			GFile *the_file = g_file_new_for_path(file->file_name);
			if(the_file != NULL) {
				file->file_monitor = g_file_monitor_file(the_file, G_FILE_MONITOR_NONE, NULL, NULL);
				if(file->file_monitor != NULL) {
					g_signal_connect(file->file_monitor, "changed", G_CALLBACK(image_file_updated_callback), (gpointer)node);
				}
				g_object_unref(the_file);
			}
		}

		// Mark the image as loaded for the GC
		D_LOCK(file_tree);
		loaded_files_list = g_list_prepend(loaded_files_list, bostree_node_weak_ref(node));
		D_UNLOCK(file_tree);

		return TRUE;
	}
	else {
		if(error_pointer) {
			if(error_pointer->code == G_IO_ERROR_CANCELLED) {
				g_clear_error(&error_pointer);
				return FALSE;
			}
			g_printerr("Failed to load image %s: %s\n", file->display_name, error_pointer->message);
			g_clear_error(&error_pointer);
		}
		else {
			if(g_cancellable_is_cancelled(image_loader_cancellable)) {
				return FALSE;
			}
			g_printerr("Failed to load image %s: Reason unknown\n", file->display_name);
		}

		// The node is invalid.  Unload it.
		D_LOCK(file_tree);
		if(node == current_file_node) {
			current_file_node = next_file();
			if(current_file_node == node) {
				if(bostree_node_count(file_tree) > 0) {
					// This can be triggered in shuffle mode if images are deleted and the end of
					// a shuffle cycle is reached, such that next_file() starts a new one. Fall
					// back to display the first image. See bug #35 in github.
					current_file_node = bostree_node_weak_ref(bostree_select(file_tree, 0));
					queue_image_load(current_file_node);
				}
				else {
					current_file_node = NULL;
				}
			}
			else {
				current_file_node = bostree_node_weak_ref(current_file_node);
				queue_image_load(current_file_node);
			}
			bostree_remove(file_tree, node);
			bostree_node_weak_unref(file_tree, node);
		}
		else {
			bostree_remove(file_tree, node);
		}
		if(!called_from_main && bostree_node_count(file_tree) == 0) {
			g_printerr("No images left to display.\n");
			if(gtk_main_level() == 0) {
				exit(1);
			}
			gtk_main_quit();
		}
		D_UNLOCK(file_tree);
	}

	return FALSE;
}/*}}}*/
gpointer image_loader_thread(gpointer user_data) {/*{{{*/
	while(TRUE) {
		// Handle new queued image load
		BOSNode *node = g_async_queue_pop(image_loader_queue);
		D_LOCK(file_tree);
		if(bostree_node_weak_unref(file_tree, bostree_node_weak_ref(node)) == NULL) {
			bostree_node_weak_unref(file_tree, node);
			D_UNLOCK(file_tree);
			continue;
		}
		D_UNLOCK(file_tree);

		// It is a hard decision whether to first load the new image or whether
		// to GC the old ones first: The former minimizes I/O for multi-page
		// images, the latter is better if memory is low.
		// As a compromise, load the new image first unless option_lowmem is
		// set.  Note that a node that has force_reload set will not be loaded
		// here, because it still is_loaded.
		if(!option_lowmem && !FILE(node)->is_loaded) {
			// Load image
			image_loader_thread_currently_loading = node;
			image_loader_load_single(node, FALSE);
			image_loader_thread_currently_loading = NULL;
		}

		// Before trying to load the image, unload the old ones to free
		// up memory.
		// We do that here to avoid a race condition with the image loaders
		D_LOCK(file_tree);
		for(GList *node_list = loaded_files_list; node_list; ) {
			GList *next = g_list_next(node_list);

			BOSNode *loaded_node = bostree_node_weak_unref(file_tree, bostree_node_weak_ref((BOSNode *)node_list->data));
			if(!loaded_node) {
				bostree_node_weak_unref(file_tree, (BOSNode *)node_list->data);
				loaded_files_list = g_list_delete_link(loaded_files_list, node_list);
			}
			else {
				// If the image to be loaded has force_reload set and this has the same file name, also set force_reload
				if(FILE(node)->force_reload && strcmp(FILE(node)->file_name, FILE(loaded_node)->file_name) == 0) {
					FILE(loaded_node)->force_reload = TRUE;
				}

				if(
					// Unloading due to force_reload being set on either this image
					// This is required because an image can be in a filebuffer, and would thus not be reloaded even if it changed on disk.
					FILE(loaded_node)->force_reload ||
					// Regular unloading: The image will not be seen by the user in the foreseeable feature
					(loaded_node != node && loaded_node != current_file_node && (option_lowmem || (loaded_node != previous_file() && loaded_node != next_file())))
				) {
					// If this node had force_reload set, we must reload it to populate the cache
					if(FILE(loaded_node)->force_reload && loaded_node == node) {
						queue_image_load(node);
					}

					unload_image(loaded_node);
					// It is important to unref after unloading, because the image data structure
					// might be reduced to zero if it has been deleted before!
					bostree_node_weak_unref(file_tree, (BOSNode *)node_list->data);
					loaded_files_list = g_list_delete_link(loaded_files_list, node_list);
				}
			}

			node_list = next;
		}
		D_UNLOCK(file_tree);

		// Now take care of the queued image, unless it has been loaded above
		if(option_lowmem && !FILE(node)->is_loaded) {
			// Load image
			image_loader_thread_currently_loading = node;
			image_loader_load_single(node, FALSE);
			image_loader_thread_currently_loading = NULL;
		}
		if(node == current_file_node && FILE(node)->is_loaded) {
			current_image_drawn = FALSE;
			gdk_threads_add_idle((GSourceFunc)image_loaded_handler, node);
		}
		D_LOCK(file_tree);
		bostree_node_weak_unref(file_tree, node);
		D_UNLOCK(file_tree);
	}
}/*}}}*/
gboolean initialize_image_loader() {/*{{{*/
	if(image_loader_initialization_succeeded) {
		return TRUE;
	}
	if(image_loader_queue == NULL) {
		image_loader_queue = g_async_queue_new();
		image_loader_cancellable = g_cancellable_new();
	}
	D_LOCK(file_tree);
	if(browse_startup_node != NULL) {
		current_file_node = bostree_node_weak_unref(file_tree, browse_startup_node);
		browse_startup_node = NULL;
		if(!current_file_node) {
			current_file_node = relative_image_pointer(0);
		}
	}
	else {
		current_file_node = relative_image_pointer(0);
	}
	if(!current_file_node) {
		D_UNLOCK(file_tree);
		return FALSE;
	}
	current_file_node = bostree_node_weak_ref(current_file_node);
	D_UNLOCK(file_tree);
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
		D_LOCK(file_tree);
		BOSNode *next = next_file();
		if(!FILE(next)->is_loaded) {
			queue_image_load(next);
		}
		BOSNode *previous = previous_file();
		if(!FILE(previous)->is_loaded) {
			queue_image_load(previous);
		}
		D_UNLOCK(file_tree);
	}

	image_loader_initialization_succeeded = TRUE;
	return TRUE;
}/*}}}*/
void abort_pending_image_loads(BOSNode *new_pos) {/*{{{*/
	BOSNode *ref;
	while((ref = g_async_queue_try_pop(image_loader_queue)) != NULL) bostree_node_weak_unref(file_tree, ref);
	if(image_loader_thread_currently_loading != NULL && image_loader_thread_currently_loading != new_pos) {
		g_cancellable_cancel(image_loader_cancellable);
	}
}/*}}}*/
void queue_image_load(BOSNode *node) {/*{{{*/
	g_async_queue_push(image_loader_queue, bostree_node_weak_ref(node));
}/*}}}*/
void unload_image(BOSNode *node) {/*{{{*/
	if(!node) {
		return;
	}
	file_t *file = FILE(node);
	if(file->file_type->unload_fn != NULL) {
		file->file_type->unload_fn(file);
	}
	file->is_loaded = FALSE;
	file->force_reload = FALSE;
	if(file->file_monitor != NULL) {
		g_file_monitor_cancel(file->file_monitor);
		if(G_IS_OBJECT(file->file_monitor)) {
			g_object_unref(file->file_monitor);
		}
		file->file_monitor = NULL;
	}
}/*}}}*/
void preload_adjacent_images() {/*{{{*/
	if(!option_lowmem) {
		D_LOCK(file_tree);
		BOSNode *new_prev = previous_file();
		BOSNode *new_next = next_file();

		if(!FILE(new_next)->is_loaded) {
			queue_image_load(new_next);
		}
		if(!FILE(new_prev)->is_loaded) {
			queue_image_load(new_prev);
		}
		D_UNLOCK(file_tree);
	}
}/*}}}*/
gboolean absolute_image_movement_still_unloaded_timer_callback(gpointer user_data) {/*{{{*/
	if(user_data == (void *)current_file_node && !CURRENT_FILE->is_loaded) {
		update_info_text(NULL);
		gtk_widget_queue_draw(GTK_WIDGET(main_window));
	}
	return FALSE;
}/*}}}*/
gboolean absolute_image_movement(BOSNode *ref) {/*{{{*/
	D_LOCK(file_tree);
	BOSNode *node = bostree_node_weak_unref(file_tree, ref);
	if(!node) {
		D_UNLOCK(file_tree);
		return FALSE;
	}

	// No need to continue the other pending loads
	abort_pending_image_loads(node);

	// Set the new image as current
	if(current_file_node != NULL) {
		bostree_node_weak_unref(file_tree, current_file_node);
	}
	current_file_node = bostree_node_weak_ref(node);
	D_UNLOCK(file_tree);

	// If the new image has not been loaded yet, prepare to display an information message
	// after some grace period
	if(!CURRENT_FILE->is_loaded && !option_hide_info_box) {
		gdk_threads_add_timeout(500, absolute_image_movement_still_unloaded_timer_callback, current_file_node);
	}

	// Load it
	queue_image_load(current_file_node);

	// Preload the adjacent images
	preload_adjacent_images();

	// Activate fading
	if(option_fading) {
		// It is important to initialize this variable with a positive,
		// non-null value, as 0. is used to indicate that no fading currently
		// takes place.
		fading_current_alpha_stage = DBL_EPSILON;
		// We start the clock after the first draw, because it could take some
		// time to calculate the resized version of the image
		fading_initial_time = -1;
		gdk_threads_add_idle(fading_timeout_callback, NULL);
	}

	// If there is an active slideshow, interrupt it until the image has been
	// drawn
	if(slideshow_timeout_id > 0) {
		g_source_remove(slideshow_timeout_id);
		slideshow_timeout_id = 0;
	}

	return FALSE;
}/*}}}*/
void relative_image_pointer_shuffle_list_unref_fn(shuffled_image_ref_t *ref) {
	bostree_node_weak_unref(file_tree, ref->node);
	g_slice_free(shuffled_image_ref_t, ref);
}
gint relative_image_pointer_shuffle_list_cmp(shuffled_image_ref_t *ref, BOSNode *node) {
	if(node == ref->node) return 0;
	return 1;
}
shuffled_image_ref_t *relative_image_pointer_shuffle_list_create(BOSNode *node) {
	assert(node != NULL);
	shuffled_image_ref_t *retval = g_slice_new(shuffled_image_ref_t);
	retval->node = bostree_node_weak_ref(node);
	retval->viewed = FALSE;
	return retval;
}
BOSNode *relative_image_pointer(ptrdiff_t movement) {/*{{{*/
	// Obtain a pointer to the image that is +movement away from the current image
	// This function behaves differently depending on whether shuffle mode is
	// enabled. It implements the actual shuffling.
	// It does not lock the file tree. This should be done before calling this
	// function. Also, it does not return a weak reference to the found node.
	//
	size_t count = bostree_node_count(file_tree);

	if(option_shuffle) {
#if 0
		// Output some debug info
		GList *aa = g_list_find_custom(shuffled_images_list, current_file_node, (GCompareFunc)relative_image_pointer_shuffle_list_cmp);
		g_print("Current shuffle list: ");
		for(GList *e = g_list_first(shuffled_images_list); e; e = e->next) {
			BOSNode *n = bostree_node_weak_unref(file_tree, bostree_node_weak_ref(LIST_SHUFFLED_IMAGE(e)->node));
			if(n) {
				if(e == aa) {
					g_print("*%02d* ", bostree_rank(n)+1);
				}
				else {
					g_print(" %02d  ", bostree_rank(n)+1);
				}
			}
			else {
				g_print(" ??  ");
			}
		}
		g_print("\n");
#endif

		// First, check if the relative movement is already possible within the existing list
		GList *current_shuffled_image = g_list_find_custom(shuffled_images_list, current_file_node, (GCompareFunc)relative_image_pointer_shuffle_list_cmp);
		if(!current_shuffled_image) {
			current_shuffled_image = g_list_last(shuffled_images_list);

			// This also happens if the user switched off random mode, moved a
			// little, and reenabled it. The image that the user saw last is,
			// expect if lowmem is used, the 2nd last in the list, because
			// there already is a preloaded next one. Correct that.
			if(!option_lowmem && current_shuffled_image && g_list_previous(current_shuffled_image)) {
				current_shuffled_image = g_list_previous(current_shuffled_image);
			}
		}
		while(((int)movement > 0) && g_list_next(current_shuffled_image)) {
			current_shuffled_image = g_list_next(current_shuffled_image);
			movement--;
		}
		while(((int)movement < 0) && g_list_previous(current_shuffled_image)) {
			current_shuffled_image = g_list_previous(current_shuffled_image);
			movement++;
		}

		// The list isn't long enough to provide us with the desired image.
		if(shuffled_images_list_length < bostree_node_count(file_tree)) {
			// If not all images have been viewed, expand it
			while(movement != 0) {
				BOSNode *next_candidate, *chosen_candidate;
				// We select one random list element and then choose the sequentially next
				// until we find one that has not been chosen yet. Walking sequentially
				// after chosing one random integer index still generates a
				// equidistributed permutation.
				// This is O(n^2), since we must in the worst case lookup n-1 elements
				// in a list of already chosen ones, but I think that this still is a
				// better choice than to store an additional boolean in each file_t,
				// which would make this O(n).
				next_candidate = chosen_candidate = bostree_select(file_tree, g_random_int_range(0, count));
				if(!next_candidate) {
					// All images have gone.
					return current_file_node;
				}
				while(g_list_find_custom(shuffled_images_list, next_candidate, (GCompareFunc)relative_image_pointer_shuffle_list_cmp)) {
					next_candidate = bostree_next_node(next_candidate);
					if(!next_candidate) {
						next_candidate = bostree_select(file_tree, 0);
					}
					if(next_candidate == chosen_candidate) {
						// This ought not happen :/
						g_warn_if_reached();
						current_shuffled_image = NULL;
						movement = 0;
					}
				}

				// If this is the start of a cycle and the current image has
				// been selected again by chance, jump one image ahead.
				if((shuffled_images_list == NULL || shuffled_images_list->data == NULL) && next_candidate == current_file_node && bostree_node_count(file_tree) > 1) {
					next_candidate = bostree_next_node(next_candidate);
					if(!next_candidate) {
						next_candidate = bostree_select(file_tree, 0);
					}
				}

				if(movement > 0) {
					shuffled_images_list = g_list_append(shuffled_images_list, relative_image_pointer_shuffle_list_create(next_candidate));
					movement--;
					shuffled_images_list_length++;
					current_shuffled_image = g_list_last(shuffled_images_list);
				}
				else if(movement < 0) {
					shuffled_images_list = g_list_prepend(shuffled_images_list, relative_image_pointer_shuffle_list_create(next_candidate));
					movement++;
					shuffled_images_list_length++;
					current_shuffled_image = g_list_first(shuffled_images_list);
				}
			}
		}
		else {
			// If all images have been used, wrap around the list's end
			while(movement) {
				current_shuffled_image = movement > 0 ? g_list_first(shuffled_images_list) : g_list_last(shuffled_images_list);
				movement = movement > 0 ? movement - 1 : movement + 1;

				while(((int)movement > 0) && g_list_next(current_shuffled_image)) {
					current_shuffled_image = g_list_next(current_shuffled_image);
					movement--;
				}
				while(((int)movement < 0) && g_list_previous(current_shuffled_image)) {
					current_shuffled_image = g_list_previous(current_shuffled_image);
					movement++;
				}
			}
		}

		if(!current_shuffled_image) {
			// Either the list was empty, or something went horribly wrong. Restart over.
			BOSNode *chosen_candidate = bostree_select(file_tree, g_random_int_range(0, count));
			if(!chosen_candidate) {
				// All images have gone.
				return current_file_node;
			}
			g_list_free_full(shuffled_images_list, (GDestroyNotify)relative_image_pointer_shuffle_list_unref_fn);
			shuffled_images_list = g_list_append(NULL, relative_image_pointer_shuffle_list_create(chosen_candidate));
			shuffled_images_visited_count = 0;
			shuffled_images_list_length = 1;
			return chosen_candidate;
		}

		// We found an image. Dereference the weak reference, and walk the list until a valid reference
		// is found if it is invalid, removing all invalid references along the way.
		BOSNode *image = bostree_node_weak_unref(file_tree, bostree_node_weak_ref(LIST_SHUFFLED_IMAGE(current_shuffled_image)->node));
		while(!image && shuffled_images_list) {
			GList *new_shuffled_image = g_list_next(current_shuffled_image);
			shuffled_images_list_length--;
			if(LIST_SHUFFLED_IMAGE(current_shuffled_image)->viewed) {
				shuffled_images_visited_count--;
			}
			relative_image_pointer_shuffle_list_unref_fn(LIST_SHUFFLED_IMAGE(current_shuffled_image));
			shuffled_images_list = g_list_delete_link(shuffled_images_list, current_shuffled_image);

			current_shuffled_image = new_shuffled_image ? new_shuffled_image : g_list_last(shuffled_images_list);
			if(current_shuffled_image) {
				image = bostree_node_weak_unref(file_tree, bostree_node_weak_ref(LIST_SHUFFLED_IMAGE(current_shuffled_image)->node));
			}
			else {
				// All images have gone. This _is_ a problem, and should not
				// happen. pqiv will likely exit. But return the current image,
				// just to be sure that nothing breaks.
				g_warn_if_reached();
				return current_file_node;
			}
		}
		return image;
	}
	else {
		// Sequential movement. This is the simple stuff:

		if(movement == 0) {
			// Only used for initialization, current_file_node might be 0
			return current_file_node ? current_file_node : bostree_select(file_tree, 0);
		}
		else if(movement == 1) {
			BOSNode *ret = bostree_next_node(current_file_node);
			return ret ? ret : bostree_select(file_tree, 0);
		}
		else if(movement == -1) {
			BOSNode *ret = bostree_previous_node(current_file_node);
			return ret ? ret : bostree_select(file_tree, bostree_node_count(file_tree) - 1);
		}
		else {
			ptrdiff_t pos = bostree_rank(current_file_node) + movement;
			while(pos < 0) {
				pos += count;
			}
			pos %= count;

			return bostree_select(file_tree, pos);
		}
	}
}/*}}}*/
void relative_image_movement(ptrdiff_t movement) {/*{{{*/
	// Calculate new position
	D_LOCK(file_tree);
	BOSNode *target = bostree_node_weak_ref(relative_image_pointer(movement));
	D_UNLOCK(file_tree);

	// Check if this movement is allowed
	if((option_shuffle && shuffled_images_visited_count == bostree_node_count(file_tree)) ||
		   (!option_shuffle && movement > 0 && bostree_rank(target) <= bostree_rank(current_file_node))) {
		if(option_end_of_files_action == QUIT) {
			bostree_node_weak_unref(file_tree, target);
			gtk_main_quit();
		}
		else if(option_end_of_files_action == WAIT) {
			bostree_node_weak_unref(file_tree, target);
			return;
		}
	}

	// Only perform the movement if the file actually changed.
	// Important for slideshows if only one file was available and said file has been deleted.
	if(movement == 0 || target != current_file_node) {
		absolute_image_movement(target);
	}
	else {
		bostree_node_weak_unref(file_tree, target);

		// If a slideshow called relative_image_movement, it has already stopped the slideshow
		// callback at this point. It might be that target == current_file_node because the
		// old slideshow cycle ended, and the new one started off with the same image.
		// Reinitialize the slideshow in that case.
		if(slideshow_timeout_id == 0) {
			slideshow_timeout_id = gdk_threads_add_timeout(option_slideshow_interval * 1000, slideshow_timeout_callback, NULL);
		}
	}
}/*}}}*/
BOSNode *directory_image_movement_find_different_directory(BOSNode *current, int direction) {/*{{{*/
	// Return a reference to the first image with a different directory than current
	// when searching in direction direction (-1 or 1)
	//
	// This function does not perform any locking!
	BOSNode *target = current;

	if(FILE(current)->file_flags & FILE_FLAGS_MEMORY_IMAGE) {
		target = direction > 0 ? bostree_next_node(target) : bostree_previous_node(target);
		if(!target) {
			target = direction > 0 ? bostree_select(file_tree, 0) : bostree_select(file_tree, bostree_node_count(file_tree) - 1);
		}
	}
	else {
		while(TRUE) {
			// Select next image
			target = direction > 0 ? bostree_next_node(target) : bostree_previous_node(target);
			if(!target) {
				target = direction > 0 ? bostree_select(file_tree, 0) : bostree_select(file_tree, bostree_node_count(file_tree) - 1);
			}

			// Check for special abort conditions: Again at first image (no different directory found),
			// or memory image
			if(target == current || (FILE(target)->file_flags & FILE_FLAGS_MEMORY_IMAGE)) {
				break;
			}

			// Check if the directory changed. If it did, abort the search.
			// Search for the first byte where the file names differ
			unsigned int pos = 0;
			while(FILE(target)->file_name[pos] && FILE(current)->file_name[pos] && FILE(target)->file_name[pos] == FILE(current)->file_name[pos]) {
				pos++;
			}

			// The path changed if either
			//  * the target file name contains a slash at or after pos
			//    (e.g. current -> ./foo/bar.png, target -> ./foo2/baz.png)
			//  * the current file name contains a slash at or after pos
			//    (e.g. current -> ./foo/bar.png, target -> ./baz.png
			gboolean directory_changed = FALSE;
			for(unsigned int i=pos; FILE(target)->file_name[i]; i++) {
				if(FILE(target)->file_name[i] == G_DIR_SEPARATOR) {
					// Gotcha.
					directory_changed = TRUE;
					break;
				}
			}
			if(!directory_changed) {
				for(unsigned int i=pos; FILE(current)->file_name[i]; i++) {
					if(FILE(current)->file_name[i] == G_DIR_SEPARATOR) {
						directory_changed = TRUE;
						break;
					}
				}
			}
			if(directory_changed) {
				break;
			}
		}
	}

	return target;
}/*}}}*/
void directory_image_movement(int direction) {/*{{{*/
	// Directory movement
	//
	// This should be consistent, i.e. movements in different directions should
	// be inverse operations of each other. This makes this function slightly
	// complex.

	D_LOCK(file_tree);
	BOSNode *target;
	BOSNode *current = current_file_node;

	if(direction == 1) {
		// Forward searches are trivial
		target = directory_image_movement_find_different_directory(current, 1);
	}
	else {
		// Bardward searches are more involved, because we want to end up at the first image
		// of the previous directory, not at the last one. The trick is to
		// search backwards twice and then again go forward by one image.
		target = directory_image_movement_find_different_directory(current, -1);
		target = directory_image_movement_find_different_directory(target, -1);

		if(target != current) {
			target = bostree_next_node(target);
			if(!target) {
				target = bostree_select(file_tree, 0);
			}
		}
	}

	target = bostree_node_weak_ref(target);
	D_UNLOCK(file_tree);
	absolute_image_movement(target);
}/*}}}*/
void transform_current_image(cairo_matrix_t *transformation) {/*{{{*/
	// Apply the transformation to the transformation matrix
	cairo_matrix_t operand = current_transformation;
	cairo_matrix_multiply(&current_transformation, &operand, transformation);

	// Resize and queue a redraw
	main_window_adjust_for_image();
	invalidate_current_scaled_image_surface();
	gtk_widget_queue_draw(GTK_WIDGET(main_window));
}/*}}}*/
gchar *apply_external_image_filter_prepare_command(gchar *command) { /*{{{*/
		D_LOCK(file_tree);
		if((CURRENT_FILE->file_flags & FILE_FLAGS_MEMORY_IMAGE) != 0) {
			D_UNLOCK(file_tree);
			return g_strdup(command);
		}
		gchar *quoted = g_shell_quote(CURRENT_FILE->file_name);
		D_UNLOCK(file_tree);

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
	D_LOCK(file_tree);
	cairo_surface_t *surface = get_scaled_image_surface_for_current_image();
	if(!surface) {
		D_UNLOCK(file_tree);
		close(*(gint *)data);
		return NULL;
	}
	D_UNLOCK(file_tree);

	cairo_surface_write_to_png_stream(surface, apply_external_image_filter_thread_callback, data);
	close(*(gint *)data);
	cairo_surface_destroy(surface);
	return NULL;
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

			gdk_threads_add_idle(apply_external_image_filter_show_output_window, child_stdout);
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
		BOSNode *current_file_node_at_start = bostree_node_weak_ref(current_file_node);
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

				if(current_file_node_at_start != current_file_node) {
					// The user navigated away from this image. Abort.
					g_free(image_data);
				}
				else if(status != 0) {
					g_printerr("External command failed with exit status %d\n", status);
					g_free(image_data);
				}
				else {
					// We now have a new image in memory in the char buffer image_data. Construct a new file
					// for the result, and load it
					//
					file_t *new_image = g_slice_new0(file_t);
					new_image->display_name = g_strdup_printf("%s [Output of `%s`]", CURRENT_FILE->display_name, argv[2]);
					if(option_sort) {
						new_image->sort_name = g_strdup_printf("%s;%s", CURRENT_FILE->sort_name, argv[2]);
					}
					new_image->file_name = g_strdup("-");
					new_image->file_type = &file_type_handlers[0];
					new_image->file_flags = FILE_FLAGS_MEMORY_IMAGE;
					new_image->file_data = g_bytes_new_take(image_data, image_data_length);

					BOSNode *loaded_file = new_image->file_type->alloc_fn(FILTER_OUTPUT, new_image);
					absolute_image_movement(bostree_node_weak_ref(loaded_file));
				}
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
	BOSNode *the_file = bostree_node_weak_ref(current_file_node);

	if((FILE(the_file)->file_flags & FILE_FLAGS_MEMORY_IMAGE) != 0) {
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

		cairo_surface_t *surface = get_scaled_image_surface_for_current_image();
		if(surface) {
			if(cairo_surface_write_to_png(surface, store_target) == CAIRO_STATUS_SUCCESS) {
				gchar *info_text = g_strdup_printf("Stored what you see into %s", store_target);
				update_info_text(info_text);
				g_free(info_text);
			}
			else {
				update_info_text("Failed to write to the .pqiv-select subdirectory");
			}
			cairo_surface_destroy(surface);
		}

		g_free(store_target);
		bostree_node_weak_unref(file_tree, the_file);
		return;
	}

	gchar *current_file_basename = g_path_get_basename(FILE(the_file)->file_name);
	gchar *link_target = g_strdup_printf("./.pqiv-select/%s", current_file_basename);

	if(g_file_test(link_target, G_FILE_TEST_EXISTS)) {
		g_free(link_target);
		g_free(current_file_basename);
		update_info_text("File already exists in .pqiv-select");
		gtk_widget_queue_draw(GTK_WIDGET(main_window));
		bostree_node_weak_unref(file_tree, the_file);
		return;
	}

	g_mkdir("./.pqiv-select", 0755);
	if(
		#ifdef _WIN32
			CreateHardLink(link_target, FILE(the_file)->file_name, NULL) == 0
		#else
			link(FILE(the_file)->file_name, link_target) != 0
		#endif
	) {
		gchar *dot = g_strrstr(link_target, ".");
		if(dot != NULL && dot > link_target + 2) {
			*dot = 0;
		}
		gchar *store_target = g_strdup_printf("%s.png", link_target);


		cairo_surface_t *surface = get_scaled_image_surface_for_current_image();
		if(surface) {
			if(cairo_surface_write_to_png(surface, store_target) == CAIRO_STATUS_SUCCESS) {
				gchar *info_text = g_strdup_printf("Failed to link file, but stored what you see into %s", store_target);
				update_info_text(info_text);
				g_free(info_text);
			}
			else {
				update_info_text("Failed to write to the .pqiv-select subdirectory");
			}
			cairo_surface_destroy(surface);
		}
		g_free(store_target);
	}
	else {
		update_info_text("Created hard-link into .pqiv-select");
	}
	g_free(link_target);
	g_free(current_file_basename);
	bostree_node_weak_unref(file_tree, the_file);
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
void calculate_current_image_transformed_size(int *image_width, int *image_height) {/*{{{*/
	double transform_width = (double)CURRENT_FILE->width;
	double transform_height = (double)CURRENT_FILE->height;
	cairo_matrix_transform_distance(&current_transformation, &transform_width, &transform_height);
	*image_width = (int)fabs(transform_width);
	*image_height = (int)fabs(transform_height);
}/*}}}*/
void draw_current_image_to_context(cairo_t *cr) {/*{{{*/
	if(CURRENT_FILE->file_type->draw_fn != NULL) {
		CURRENT_FILE->file_type->draw_fn(CURRENT_FILE, cr);
	}
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
cairo_surface_t *get_scaled_image_surface_for_current_image() {/*{{{*/
	if(current_scaled_image_surface != NULL) {
		return cairo_surface_reference(current_scaled_image_surface);
	}
	if(!CURRENT_FILE->is_loaded) {
		return NULL;
	}

	cairo_surface_t *retval = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, current_scale_level * CURRENT_FILE->width + .5, current_scale_level * CURRENT_FILE->height + .5);
	if(cairo_surface_status(retval) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(retval);
		return NULL;
	}

	cairo_t *cr = cairo_create(retval);
	cairo_scale(cr, current_scale_level, current_scale_level);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	draw_current_image_to_context(cr);
	cairo_destroy(cr);

	if(!option_lowmem) {
		current_scaled_image_surface = cairo_surface_reference(retval);
	}

	return retval;
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

	gdk_threads_add_timeout(200, (GSourceFunc)absolute_image_movement, bostree_node_weak_ref(jump_to));
} /* }}} */
void do_jump_dialog() { /* {{{ */
	/**
	 * Show the jump dialog to jump directly
	 * to an image
	 */
	GtkTreeIter search_list_iter;

	// If in fullscreen, show the cursor again
	if(main_window_in_fullscreen) {
		window_center_mouse();
		fullscreen_show_cursor();
	}

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
	D_LOCK(file_tree);
	for(BOSNode *node = bostree_select(file_tree, 0); node; node = bostree_next_node(node)) {
		gtk_list_store_append(search_list, &search_list_iter);

		gtk_list_store_set(search_list, &search_list_iter,
			0, id++,
			1, FILE(node)->display_name,
			2, bostree_node_weak_ref(node),
			-1);
	}
	D_UNLOCK(file_tree);

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
		bostree_node_weak_unref(file_tree, (BOSNode *)g_value_get_pointer(&col_data));
		g_value_unset(&col_data);
	}

	if(main_window_in_fullscreen) {
		fullscreen_hide_cursor();
	}

	gtk_widget_destroy(dlg_window);
	g_object_unref(search_list);
	g_object_unref(search_list_filter);
} /* }}} */
// }}}
/* Main window functions {{{ */
void window_fullscreen() {
	// Bugfix for Awesome WM: If hints are active, windows are fullscreen'ed honoring the aspect ratio
	gtk_window_set_geometry_hints(main_window, NULL, NULL, 0);

	#ifndef _WIN32
		if(!wm_supports_fullscreen) {
			// WM does not support _NET_WM_ACTION_FULLSCREEN or no WM present
			main_window_in_fullscreen = TRUE;
			gtk_window_move(main_window, screen_geometry.x, screen_geometry.y);
			gtk_window_resize(main_window, screen_geometry.width, screen_geometry.height);
			window_state_into_fullscreen_actions();
			return;
		}
	#endif

	gtk_window_fullscreen(main_window);
}
void window_unfullscreen() {
	#ifndef _WIN32
		if(!wm_supports_fullscreen) {
			// WM does not support _NET_WM_ACTION_FULLSCREEN or no WM present
			main_window_in_fullscreen = FALSE;
			window_state_out_of_fullscreen_actions();
			return;
		}
	#endif

	gtk_window_unfullscreen(main_window);
}
inline void queue_draw() {/*{{{*/
	if(!current_image_drawn) {
		gtk_widget_queue_draw(GTK_WIDGET(main_window));
	}
}/*}}}*/
inline void info_text_queue_redraw() {/*{{{*/
	if(!option_hide_info_box) {
		gtk_widget_queue_draw_area(GTK_WIDGET(main_window),
			current_info_text_bounding_box.x,
			current_info_text_bounding_box.y,
			current_info_text_bounding_box.width,
			current_info_text_bounding_box.height
		);
	}
}/*}}}*/
void update_info_text(const gchar *action) {/*{{{*/
	D_LOCK(file_tree);

	gchar *file_name;
	if((CURRENT_FILE->file_flags & FILE_FLAGS_MEMORY_IMAGE) != 0) {
		file_name = g_strdup_printf("-");
	}
	else {
		file_name = g_strdup(CURRENT_FILE->file_name);
	}
	const gchar *display_name = CURRENT_FILE->display_name;

	// Free old info text
	if(current_info_text != NULL) {
		g_free(current_info_text);
		current_info_text = NULL;
	}

	if(!CURRENT_FILE->is_loaded) {
		// Image not loaded yet. Use loading information and abort.
		current_info_text = g_strdup_printf("%s (Image is still loading...)", display_name);

		g_free(file_name);
		D_UNLOCK(file_tree);
		return;
	}

	// Update info text
	if(!option_hide_info_box) {
		current_info_text = g_strdup_printf("%s (%dx%d) %03.2f%% [%d/%d]", display_name,
			CURRENT_FILE->width,
			CURRENT_FILE->height,
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
			g_string_append_printf(new_window_title, "%d", CURRENT_FILE->width);
			window_title_iter += 5;
		}
		else if(g_strstr_len(window_title_iter, 6, "HEIGHT") != NULL) {
			g_string_append_printf(new_window_title, "%d", CURRENT_FILE->height);
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
	D_UNLOCK(file_tree);
	g_free(file_name);
	gtk_window_set_title(GTK_WINDOW(main_window), new_window_title->str);
	g_string_free(new_window_title, TRUE);
}/*}}}*/
gboolean window_close_callback(GtkWidget *object, gpointer user_data) {/*{{{*/
	gtk_main_quit();

	return FALSE;
}/*}}}*/
gboolean window_draw_callback(GtkWidget *widget, cairo_t *cr_arg, gpointer user_data) {/*{{{*/
	// Draw image
	int x = 0;
	int y = 0;
	D_LOCK(file_tree);

	if(CURRENT_FILE->is_loaded) {
		// Calculate where to draw the image and the transformation matrix to use
		int image_transform_width, image_transform_height;
		calculate_current_image_transformed_size(&image_transform_width, &image_transform_height);
		if(option_scale > 0 || main_window_in_fullscreen) {
			x = (main_window_width - current_scale_level * image_transform_width) / 2;
			y = (main_window_height - current_scale_level * image_transform_height) / 2;
		}
		else {
			// When scaling is disabled always use the upper left corder to avoid
			// problems with window managers ignoring the large window size request.
			x = y = 0;
		}
		cairo_matrix_t apply_transformation = current_transformation;
		apply_transformation.x0 *= current_scale_level;
		apply_transformation.y0 *= current_scale_level;

		// Create a temporary image surface to render to first.
		//
		// We use this for fading and to display the last image if the current image is
		// still unavailable
		//
		// The temporary image surface contains the image as it
		// is displayed on the screen later, with all transformations applied.

		#if CAIRO_VERSION >= CAIRO_VERSION_ENCODE(1, 12, 0)
			cairo_surface_t *temporary_image_surface = cairo_surface_create_similar_image(cairo_get_target(cr_arg), CAIRO_FORMAT_ARGB32, main_window_width, main_window_height);
		#else
			cairo_surface_t *temporary_image_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, main_window_width, main_window_height);
		#endif
		cairo_t *cr = NULL;
		if(cairo_surface_status(temporary_image_surface) != CAIRO_STATUS_SUCCESS) {
			// This image is too large to be rendered into a temorary image surface
			// As a best effort solution, render directly to the window instead
			cr = cr_arg;
		}
		else {
			cr = cairo_create(temporary_image_surface);
		}

		// Draw black background
		cairo_save(cr);
		cairo_set_source_rgba(cr, 0., 0., 0., option_transparent_background ? 0. : 1.);
		cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
		cairo_paint(cr);
		cairo_restore(cr);

		// From here on, draw at the target position
		cairo_translate(cr, current_shift_x + x, current_shift_y + y);
		cairo_transform(cr, &apply_transformation);

		// Draw background pattern
		if(background_checkerboard_pattern != NULL && !option_transparent_background) {
			cairo_save(cr);
			cairo_scale(cr, current_scale_level, current_scale_level);
			cairo_new_path(cr);
			// Cairo or gdkpixbuf, I don't know which, feather the border of images, leading
			// to the background pattern overlaying images, which doesn't look nice at all.
			// TODO The current workaround is to draw the background pattern 1px into the image
			//      if in fullscreen mode, because that's where the pattern irretates most –
			//      but I'd prefer a cleaner solution.
			if(main_window_in_fullscreen) {
				cairo_rectangle(cr, 1, 1, CURRENT_FILE->width - 2, CURRENT_FILE->height - 2);
			}
			else {
				cairo_rectangle(cr, 0, 0, CURRENT_FILE->width, CURRENT_FILE->height);
			}
			cairo_close_path(cr);
			cairo_clip(cr);
			cairo_set_source(cr, background_checkerboard_pattern);
			cairo_paint(cr);
			cairo_restore(cr);
		}

		// Draw the scaled image.
		if(option_lowmem || cr == cr_arg) {
			// In low memory mode, we scale here and draw on the fly
			// The other situation where we do this is if creating the temporary
			// image surface failed, because if this failed creating the temporary
			// image surface will likely also fail.
			cairo_save(cr);
			cairo_scale(cr, current_scale_level, current_scale_level);
			cairo_rectangle(cr, 0, 0, image_transform_width, image_transform_height);
			cairo_clip(cr);
			draw_current_image_to_context(cr);
			cairo_restore(cr);
		}
		else {
			// Elsewise, we cache a scaled copy in a separate image surface
			// to speed up rotations on scaled images
			cairo_surface_t *temporary_scaled_image_surface = get_scaled_image_surface_for_current_image();
			cairo_set_source_surface(cr, temporary_scaled_image_surface, 0, 0);
			cairo_paint(cr);
			cairo_surface_destroy(temporary_scaled_image_surface);
		}

		// If we drew to an off-screen buffer before, render to the window now
		if(cr != cr_arg) {
			// The temporary image surface is now complete.
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
		}
		else {
			if(last_visible_image_surface) {
				cairo_surface_destroy(last_visible_image_surface);
				last_visible_image_surface = NULL;
			}
		}

		// If we have an active slideshow, resume now.
		if(slideshow_timeout_id == 0) {
			slideshow_timeout_id = gdk_threads_add_timeout(option_slideshow_interval * 1000, slideshow_timeout_callback, NULL);
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
	D_UNLOCK(file_tree);

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

		// Store where the box was drawn to allow for partial updates of the screen
		current_info_text_bounding_box.x = (main_window_in_fullscreen == TRUE ? 0 : (x < 0 ? 0 : x)) + 10 - 5;
		current_info_text_bounding_box.y = (main_window_in_fullscreen == TRUE ? 0 : (y < 0 ? 0 : y)) + 20 -(y2 - y1) - 2;

		 // Redraw some extra pixels to make sure a wider new box would be covered:
		current_info_text_bounding_box.width = x2 - x1 + 10 + 30;
		current_info_text_bounding_box.height = y2 - y1 + 8;
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
void set_scale_level_for_screen() {/*{{{*/
	if(!main_window_in_fullscreen) {
		// Calculate diplay width/heights with rotation, but without scaling, applied
		int image_width, image_height;
		calculate_current_image_transformed_size(&image_width, &image_height);
		const int screen_width = screen_geometry.width;
		const int screen_height = screen_geometry.height;

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
		current_scale_level = round(current_scale_level * 100.) / 100.;
	}
	else {
		// In fullscreen, the screen size and window size match, so the
		// function to adjust to the window size works just fine (and does
		// not come with the 80%, as users would expect in fullscreen)
		set_scale_level_to_fit();
	}
}/*}}}*/
void set_scale_level_to_fit() {/*{{{*/
	D_LOCK(file_tree);
	if(CURRENT_FILE->is_loaded) {
		if(!current_image_drawn) {
			scale_override = FALSE;
		}

		// Calculate diplay width/heights with rotation, but without scaling, applied
		int image_width, image_height;
		calculate_current_image_transformed_size(&image_width, &image_height);

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
	D_UNLOCK(file_tree);
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
	if(!CURRENT_FILE->is_loaded && (
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
		event->keyval != GDK_KEY_n &&
		event->keyval != GDK_KEY_N &&
		event->keyval != GDK_KEY_p &&
		event->keyval != GDK_KEY_P &&
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
				option_slideshow_interval = (double)((int)option_slideshow_interval + 1);
				if(slideshow_timeout_id > 0) {
					g_source_remove(slideshow_timeout_id);
					slideshow_timeout_id = gdk_threads_add_timeout(option_slideshow_interval * 1000, slideshow_timeout_callback, NULL);
				}
				gchar *info_text = g_strdup_printf("Slideshow interval set to %d seconds", (int)option_slideshow_interval);
				update_info_text(info_text);
				gtk_widget_queue_draw(GTK_WIDGET(main_window));
				g_free(info_text);
				break;
			}

			current_scale_level *= 1.1;
			current_scale_level = round(current_scale_level * 100.) / 100.;
			if((option_scale == 1 && current_scale_level > 1) || option_scale == 0) {
				scale_override = TRUE;
			}
			invalidate_current_scaled_image_surface();
			current_image_drawn = FALSE;
			if(main_window_in_fullscreen) {
				gtk_widget_queue_draw(GTK_WIDGET(main_window));
			}
			else {
				int image_width, image_height;
				calculate_current_image_transformed_size(&image_width, &image_height);

				gtk_window_resize(main_window, current_scale_level * image_width, current_scale_level * image_height);
				if(!wm_supports_moveresize) {
					queue_draw();
				}
			}
			update_info_text(NULL);
			break;

		case GDK_KEY_minus:
		case GDK_KEY_KP_Subtract:
			if(event->state & GDK_CONTROL_MASK) {
				if(option_slideshow_interval >= 2.) {
					option_slideshow_interval = (double)((int)option_slideshow_interval - 1);
				}
				if(slideshow_timeout_id > 0) {
					g_source_remove(slideshow_timeout_id);
					slideshow_timeout_id = gdk_threads_add_timeout(option_slideshow_interval * 1000, slideshow_timeout_callback, NULL);
				}
				gchar *info_text = g_strdup_printf("Slideshow interval set to %d seconds", (int)option_slideshow_interval);
				update_info_text(info_text);
				gtk_widget_queue_draw(GTK_WIDGET(main_window));
				g_free(info_text);
				break;
			}

			if(current_scale_level <= 0.01) {
				break;
			}
			current_scale_level /= 1.1;
			current_scale_level = round(current_scale_level * 100.) / 100.;
			if((option_scale == 1 && current_scale_level > 1) || option_scale == 0) {
				scale_override = TRUE;
			}
			invalidate_current_scaled_image_surface();
			current_image_drawn = FALSE;
			if(main_window_in_fullscreen) {
				gtk_widget_queue_draw(GTK_WIDGET(main_window));
			}
			else {
				int image_width, image_height;
				calculate_current_image_transformed_size(&image_width, &image_height);
				image_width = abs(image_width);
				image_height = abs(image_height);

				gtk_window_resize(main_window, current_scale_level * image_width, current_scale_level * image_height);
				if(!wm_supports_moveresize) {
					queue_draw();
				}
			}
			update_info_text(NULL);
			break;

		case GDK_KEY_T:
		case GDK_KEY_t:
			if(++option_scale > 2) {
				option_scale = 0;
			}
			current_image_drawn = FALSE;
			current_shift_x = 0;
			current_shift_y = 0;
			set_scale_level_for_screen();
			main_window_adjust_for_image();
			invalidate_current_scaled_image_surface();
			gtk_widget_queue_draw(GTK_WIDGET(main_window));
			switch(option_scale) {
				case 0: update_info_text("Scaling disabled"); break;
				case 1: update_info_text("Automatic scaledown enabled"); break;
				case 2: update_info_text("Automatic scaling enabled"); break;
			}
			break;

		case GDK_KEY_r:
		case GDK_KEY_R:
			if(event->state & GDK_CONTROL_MASK) {
				option_shuffle = !option_shuffle;
				preload_adjacent_images();
				update_info_text(option_shuffle ? "Shuffle mode enabled" : "Shuffle mode disabled");
				gtk_widget_queue_draw(GTK_WIDGET(main_window));
				break;
			}

			CURRENT_FILE->force_reload = TRUE;
			update_info_text("Reloading image..");
			queue_image_load(relative_image_pointer(0));
			break;

		case GDK_KEY_0:
			current_image_drawn = FALSE;
			scale_override = FALSE;
			set_scale_level_for_screen();
			main_window_adjust_for_image();
			invalidate_current_scaled_image_surface();
			gtk_widget_queue_draw(GTK_WIDGET(main_window));
			update_info_text(NULL);
			break;

		case GDK_KEY_F:
		case GDK_KEY_f:
			if(main_window_in_fullscreen == FALSE) {
				window_fullscreen();
			}
			else {
				window_unfullscreen();
			}
			break;

		case GDK_KEY_H:
		case GDK_KEY_h:
			{
				int image_width, image_height;
				calculate_current_image_transformed_size(&image_width, &image_height);

				cairo_matrix_t transformation = { -1., 0., 0., 1., image_width, 0 };
				transform_current_image(&transformation);
			}
			update_info_text("Image flipped horizontally");
			break;

		case GDK_KEY_V:
		case GDK_KEY_v:
			{
				int image_width, image_height;
				calculate_current_image_transformed_size(&image_width, &image_height);

				cairo_matrix_t transformation = { 1., 0., 0., -1., 0, image_height };
				transform_current_image(&transformation);
			}
			update_info_text("Image flipped vertically");
			break;

		case GDK_KEY_L:
		case GDK_KEY_l:
			{
				int image_width, image_height;
				calculate_current_image_transformed_size(&image_width, &image_height);

				cairo_matrix_t transformation = { 0., -1., 1., 0., 0, image_width };
				transform_current_image(&transformation);
			}
			update_info_text("Image rotated left");
			break;

		case GDK_KEY_K:
		case GDK_KEY_k:
			{
				int image_width, image_height;
				calculate_current_image_transformed_size(&image_width, &image_height);

				cairo_matrix_t transformation = { 0., 1., -1., 0., image_height, 0. };
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
				slideshow_timeout_id = gdk_threads_add_timeout(option_slideshow_interval * 1000, slideshow_timeout_callback, NULL);
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
		case GDK_KEY_p:
		case GDK_KEY_P:
			if(event->state & GDK_CONTROL_MASK) {
				directory_image_movement(-1);
			}
			else {
				relative_image_movement(-1);
			}
			break;

		case GDK_KEY_space:
		case GDK_KEY_n:
		case GDK_KEY_N:
			if(event->state & GDK_CONTROL_MASK) {
				directory_image_movement(1);
			}
			else {
				relative_image_movement(1);
			}
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
			gtk_widget_destroy(GTK_WIDGET(main_window));
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
					((CURRENT_FILE->file_flags & FILE_FLAGS_MEMORY_IMAGE) != 0 && command[0] != '|')
					|| ((CURRENT_FILE->file_flags & FILE_FLAGS_ANIMATION) != 0 && command[0] == '|')) {

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
		#if GTK_MAJOR_VERSION == 3 && GTK_MINOR_VERSION < 20
			GdkDevice *device = gdk_device_manager_get_client_pointer(gdk_display_get_device_manager(display));
		#else
			GdkDevice *device = gdk_seat_get_pointer(gdk_display_get_default_seat(display));
		#endif
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
		if(option_reverse_scroll == FALSE) {
			relative_image_movement(1);
		}
		else {
			relative_image_movement(-1);
		}
	}
	else if(event->direction == GDK_SCROLL_DOWN) {
		if(option_reverse_scroll == FALSE) {
			relative_image_movement(-1);
		}
		else {
			relative_image_movement(1);
		}
	}


	return FALSE;
}/*}}}*/
void fullscreen_hide_cursor() {/*{{{*/
	GdkDisplay *display = gtk_widget_get_display(GTK_WIDGET(main_window));
	GdkCursor *cursor = gdk_cursor_new_for_display(display, GDK_BLANK_CURSOR);
	GdkWindow *window = gtk_widget_get_window(GTK_WIDGET(main_window));
	gdk_window_set_cursor(window, cursor);
	#if GTK_MAJOR_VERSION >= 3
		g_object_unref(cursor);
	#endif
}/*}}}*/
void fullscreen_show_cursor() {/*{{{*/
	GdkWindow *window = gtk_widget_get_window(GTK_WIDGET(main_window));
	gdk_window_set_cursor(window, NULL);
}/*}}}*/
void window_state_into_fullscreen_actions() {/*{{{*/
	current_shift_x = 0;
	current_shift_y = 0;

	fullscreen_hide_cursor();

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
void window_state_out_of_fullscreen_actions() {/*{{{*/
	current_shift_x = 0;
	current_shift_y = 0;

	// If the fullscreen state is left, readjust image placement/size/..
	scale_override = FALSE;
	set_scale_level_for_screen();
	main_window_adjust_for_image();
	invalidate_current_scaled_image_surface();
	fullscreen_show_cursor();
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
			window_state_out_of_fullscreen_actions();
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
void window_screen_window_manager_changed_callback(gpointer user_data) {/*{{{*/
	#ifndef _WIN32
		GdkScreen *screen = GDK_SCREEN(user_data);

		// TODO Would _NET_WM_ALLOWED_ACTIONS -> _NET_WM_ACTION_RESIZE and _NET_WM_ACTION_FULLSCREEN  be a better choice here?
		#if GTK_MAJOR_VERSION >= 3
			if(GDK_IS_X11_SCREEN(screen)) {
				wm_supports_fullscreen = gdk_x11_screen_supports_net_wm_hint(screen, gdk_x11_xatom_to_atom(gdk_x11_get_xatom_by_name("_NET_WM_STATE_FULLSCREEN")));
				wm_supports_moveresize = gdk_x11_screen_supports_net_wm_hint(screen, gdk_x11_xatom_to_atom(gdk_x11_get_xatom_by_name("_NET_MOVERESIZE_WINDOW")));
			}
		#else
			wm_supports_fullscreen = gdk_x11_screen_supports_net_wm_hint(screen, gdk_x11_xatom_to_atom(gdk_x11_get_xatom_by_name("_NET_WM_STATE_FULLSCREEN")));
			wm_supports_moveresize = gdk_x11_screen_supports_net_wm_hint(screen, gdk_x11_xatom_to_atom(gdk_x11_get_xatom_by_name("_NET_MOVERESIZE_WINDOW")));
		#endif
	#endif
}/*}}}*/
void window_screen_changed_callback(GtkWidget *widget, GdkScreen *previous_screen, gpointer user_data) {/*{{{*/
	GdkScreen *screen = gtk_widget_get_screen(GTK_WIDGET(main_window));
	GdkWindow *window = gtk_widget_get_window(GTK_WIDGET(main_window));
	guint monitor = gdk_screen_get_monitor_at_window(screen, window);

	#ifndef _WIN32
	g_signal_connect(screen, "window-manager-changed", G_CALLBACK(window_screen_window_manager_changed_callback), screen);
	#endif
	window_screen_window_manager_changed_callback(screen);

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
		window_fullscreen();
	}

	// Execute the screen-changed callback, to assign the correct screen
	// to the window (if it's not the primary one, which we assigned in
	// create_window)
	window_screen_changed_callback(NULL, NULL, NULL);

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
			gdk_threads_add_idle(set_option_initial_scale_used_callback, NULL);
		}
		else {
			gdk_threads_add_timeout(300, set_option_initial_scale_used_callback, NULL);
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
	window_screen_window_manager_changed_callback(screen);

	if(option_start_fullscreen) {
		// If no WM is present, move the window to the screen origin and
		// assume fullscreen right from the start
		window_fullscreen();
	}
	else if(option_window_position.x >= 0) {
		window_move_helper_callback(NULL);
	}
	else if(option_window_position.x != -1) {
		gtk_window_set_position(main_window, GTK_WIN_POS_CENTER);
	}

#if GTK_MAJOR_VERSION < 3
	gtk_widget_set_double_buffered(GTK_WIDGET(main_window), TRUE);
#endif
	gtk_widget_set_app_paintable(GTK_WIDGET(main_window), TRUE);

	if(option_transparent_background) {
		gtk_window_set_decorated(main_window, FALSE);
	}

	if(option_transparent_background) {
		window_screen_activate_rgba();
	}
}/*}}}*/
gboolean initialize_gui() {/*{{{*/
	setup_checkerboard_pattern();
	create_window();
	if(initialize_image_loader()) {
		image_loaded_handler(NULL);

		if(option_start_with_slideshow_mode) {
			slideshow_timeout_id = gdk_threads_add_timeout(option_slideshow_interval * 1000, slideshow_timeout_callback, NULL);
		}
		return TRUE;
	}
	return FALSE;
}/*}}}*/
gboolean initialize_gui_callback(gpointer user_data) {/*{{{*/
	if(!gui_initialized && initialize_image_loader()) {
		initialize_gui();
		gui_initialized = TRUE;
	}

	return FALSE;
}/*}}}*/
gboolean initialize_gui_or_quit_callback(gpointer user_data) {/*{{{*/
	if(!gui_initialized && initialize_image_loader()) {
		initialize_gui();
		gui_initialized = TRUE;
	}

	if(!file_tree_valid || bostree_node_count(file_tree) == 0) {
		g_printerr("No images left to display.\n");
		exit(1);
	}

	return FALSE;
}/*}}}*/
// }}}

gboolean load_images_thread_update_info_text(gpointer user_data) {/*{{{*/
	// If the window is already visible and new files have been found, update
	// the info text every second
	static gsize last_image_count = 0;
	if(main_window_visible == TRUE) {
		D_LOCK(file_tree);
		gsize image_count = bostree_node_count(file_tree);
		D_UNLOCK(file_tree);

		if(image_count != last_image_count) {
			last_image_count = image_count;

			update_info_text(NULL);
			info_text_queue_redraw();
		}
	}
	return TRUE;
}/*}}}*/
gpointer load_images_thread(gpointer user_data) {/*{{{*/
	guint event_source;
	if(user_data != NULL) {
		// Use the info text updater only if this function was called in a separate
		// thread (--lazy-load option)
		event_source = gdk_threads_add_timeout(1000, load_images_thread_update_info_text, NULL);
	}

	load_images(&global_argc, global_argv);

	if(file_tree_valid) {
		if(bostree_node_count(file_tree) == 0) {
			g_printerr("No images left to display.\n");
			exit(1);
		}
	}

	if(option_lazy_load) {
		gdk_threads_add_idle(initialize_gui_or_quit_callback, NULL);
	}

	if(user_data != NULL) {
		g_source_remove(event_source);
	}
	return NULL;
}/*}}}*/

int main(int argc, char *argv[]) {
	#ifdef DEBUG
		#ifndef _WIN32
			struct rlimit core_limits;
			core_limits.rlim_cur = core_limits.rlim_max = RLIM_INFINITY;
			setrlimit(RLIMIT_CORE, &core_limits);
		#endif
	#endif

	#ifndef _WIN32
		XInitThreads();
	#endif
	#if (!GLIB_CHECK_VERSION(2, 32, 0))
		g_thread_init(NULL);
		gdk_threads_init();
	#endif
	gtk_init(&argc, &argv); // fyi, this generates a MemorySanitizer warning currently

	initialize_file_type_handlers();

	parse_configuration_file(&argc, &argv);
	parse_command_line(&argc, argv);
	if(fabs(option_initial_scale - 1.0) < 2 * FLT_MIN) {
		option_initial_scale_used = TRUE;
	}
	else {
		current_scale_level = option_initial_scale;
	}
	cairo_matrix_init_identity(&current_transformation);

	global_argc = argc;
	global_argv = argv;
	if(option_lazy_load) {
		#if GLIB_CHECK_VERSION(2, 32, 0)
			g_thread_new("image-loader", load_images_thread, GINT_TO_POINTER(1));
		#else
			g_thread_create(load_images_thread, NULL, FALSE, GINT_TO_POINTER(1));
		#endif
	}
	else {
		load_images_thread(NULL);
		if(!initialize_gui()) {
			g_printerr("No images left to display.\n");
			exit(1);
		}
	}

	gtk_main();

	// We are outside of the main thread again, so we can unload the remaining images
	// We need to do this, because some file types create temporary files
	//
	// Note: If we locked the file_tree here, unload_image() could dead-lock
	// (The wand backend has a global mutex and calls a function that locks file_tree)
	// Instead, accept that in the worst case, some images might not be unloaded properly.
	// At least, after file_tree_valid = FALSE, no new images will be inserted.
	file_tree_valid = FALSE;
	D_LOCK(file_tree);
	D_UNLOCK(file_tree);
	abort_pending_image_loads(NULL);
	for(BOSNode *node = bostree_select(file_tree, 0); node; node = bostree_next_node(node)) {
		// Iterate over the images ourselves, because there might be open weak references which
		// prevent this to be called from bostree_destroy.
		unload_image(node);
	}
	D_LOCK(file_tree);
	bostree_destroy(file_tree);
	D_UNLOCK(file_tree);

	return 0;
}

// vim:noet ts=4 sw=4 tw=0 fdm=marker
