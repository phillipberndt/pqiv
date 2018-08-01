/**
 * pqiv
 *
 * Copyright (c) 2013-2017, Phillip Berndt
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
#include "lib/config_parser.h"
#include "lib/thumbnailcache.h"
#include "lib/strnatcmp.h"
#include <cairo/cairo.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <unistd.h>
#include <assert.h>
#include <locale.h>

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
	#include <gio/gunixinputstream.h>
#endif
#ifdef GDK_WINDOWING_X11
	#include <gdk/gdkx.h>
	#include <X11/Xlib.h>
	#include <X11/extensions/shape.h>
	#include <cairo/cairo-xlib.h>

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

#if defined(__clang__) || defined(__GNUC__)
	#define UNUSED_FUNCTION __attribute__((unused))

	#if defined(__clang__)
		#define PQIV_DISABLE_PEDANTIC _Pragma("clang diagnostic push") _Pragma("clang diagnostic ignored \"-Wpedantic\"")
		#define PQIV_ENABLE_PEDANTIC _Pragma("clang diagnostic pop")
	#elif defined(__GNUC__) || defined(__GNUG__)
		#define PQIV_DISABLE_PEDANTIC _Pragma("GCC diagnostic push") _Pragma("GCC diagnostic ignored \"-Wpedantic\"")
		#define PQIV_ENABLE_PEDANTIC _Pragma("GCC diagnostic pop")
	#endif
#else
	#define UNUSED_FUNCTION
	#define PQIV_DISABLE_PEDANTIC
	#define PQIV_ENABLE_PEDANTIC
#endif

#if !GLIB_CHECK_VERSION(2, 32, 0)
	#define g_thread_new(name, func, data) g_thread_create(func, data, FALSE, NULL)
#endif

// GTK 2 does not define keyboard aliases the way we do
#if GTK_MAJOR_VERSION < 3 // {{{

#define GDK_BUTTON_PRIMARY 1
#define GDK_BUTTON_MIDDLE 2
#define GDK_BUTTON_SECONDARY 3

#define GDK_KEY_VoidSymbol 0xffffff
#include <gdk/gdkkeysyms.h>

#endif // }}}

// Global variables and function signatures {{{

// The list of file type handlers and file type initializer function
void initialize_file_type_handlers(const gchar * const * disabled_backends);

// Storage of the file list
// These lists are accessed from multiple threads:
//  * The main thread (count, next, prev, ..)
//  * The option parser thread, if --lazy-load is used
//  * The image loader thread
// Our thread safety strategy is as follows:
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
#if 0
	#define D_LOCK(x) g_print("Waiting for lock " #x " at line %d\n", __LINE__); G_LOCK(x); g_print("  Locked " #x " at line %d\n", __LINE__)
	#define D_UNLOCK(x) g_print("Unlocked " #x " at line %d\n", __LINE__); G_UNLOCK(x);
#else
	#define D_LOCK(x) G_LOCK(x)
	#define D_UNLOCK(x) G_UNLOCK(x)
#endif
BOSTree *file_tree;
BOSNode *current_file_node = NULL;
BOSNode *earlier_file_node = NULL;
BOSNode *image_loader_thread_currently_loading = NULL;
gboolean file_tree_valid = FALSE;

// We asynchroniously load images in a separate thread
struct image_loader_queue_item {
	BOSNode *node_ref;
	int purpose;
};
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
#define is_current_file_loaded() (current_file_node && CURRENT_FILE->is_loaded)

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

// Detection of tiled WMs: They should ignore our resize events
gint requested_main_window_resize_pos_callback_id = -1;
gint requested_main_window_width = -1;
gint requested_main_window_height = -1;
gboolean wm_ignores_size_requests = FALSE;

gint main_window_width = 10;
gint main_window_height = 10;
gboolean main_window_in_fullscreen = FALSE;
int fullscreen_transition_source_id = -1;
GdkRectangle screen_geometry = { 0, 0, 0, 0 };
gint screen_scale_factor = 1;
gboolean wm_supports_fullscreen = TRUE;

// If a WM indicates no moveresize support that's a hint it's a tiling WM
gboolean wm_supports_moveresize = TRUE;

// If a WM indicates no framedrawn support it's subject to tearing effects
gboolean wm_supports_framedrawn = TRUE;

cairo_pattern_t *background_checkerboard_pattern = NULL;

gboolean gui_initialized = FALSE;
int global_argc;
char **global_argv;

// Those surfaces are here to store scaled image versions (to reduce
// processor load) and to store the last visible image to have something to
// display while fading and while the (new) current image has not been loaded
// yet.
int last_visible_surface_width = -1;
int last_visible_surface_height = -1;
cairo_surface_t *last_visible_surface = NULL;
cairo_surface_t *fading_surface = NULL;
cairo_surface_t *current_scaled_image_surface = NULL;

#if !defined(CONFIGURED_WITHOUT_INFO_TEXT) || !defined(CONFIGURED_WITHOUT_MONTAGE_MODE)
struct {
	double fg_red;
	double fg_green;
	double fg_blue;
	double bg_red;
	double bg_green;
	double bg_blue;
} option_box_colors = { 0., 0., 0., 1., 1., 0. };
#endif
#ifndef CONFIGURED_WITHOUT_INFO_TEXT
gint current_info_text_cached_font_size = -1;
gchar *current_info_text = NULL;
cairo_rectangle_int_t current_info_text_bounding_box = { 0, 0, 0, 0 };
#endif


// Current state of the displayed image and user interaction
// This matrix stores rotations and reflections (makes ui with scaling/transforming easier)
cairo_matrix_t current_transformation;
gdouble current_scale_level = 1.0;
gint current_shift_x = 0;
gint current_shift_y = 0;
guint32 last_button_press_time = 0;
guint32 last_button_release_time = 0;
guint current_image_animation_timeout_id = 0;
gdouble current_image_animation_speed_scale = 1.0;

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

#ifndef CONFIGURED_WITHOUT_EXTERNAL_COMMANDS
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
#endif

// Scaling mode, only 0-2 are available in the default UI, FIXED_SCALE can be
// set using a command line option, SCALE_TO_FIT_PX and SCALE_TO_FIT_WINDOW only using an action
enum { NO_SCALING=0, AUTO_SCALEDOWN, AUTO_SCALEUP, FIXED_SCALE, SCALE_TO_FIT_WINDOW, SCALE_TO_FIT_PX } option_scale = AUTO_SCALEDOWN;
double option_scale_screen_fraction = .8;
struct {
	guint width;
	guint height;
} scale_to_fit_size;

gboolean scale_override = FALSE;
const gchar *option_window_title = "pqiv: $FILENAME ($WIDTHx$HEIGHT) $ZOOM% [$IMAGE_NUMBER/$IMAGE_COUNT]";
gdouble option_slideshow_interval = 5.;
#ifndef CONFIGURED_WITHOUT_INFO_TEXT
gboolean option_hide_info_box = FALSE;
#endif
gboolean option_start_fullscreen = FALSE;
gdouble option_initial_scale = 1.0;
gboolean option_start_with_slideshow_mode = FALSE;
gboolean option_click_through = FALSE;
gboolean option_sort = FALSE;
enum { NAME, MTIME } option_sort_key = NAME;
gboolean option_shuffle = FALSE;
gboolean option_transparent_background = FALSE;
gboolean option_watch_directories = FALSE;
gboolean option_wait_for_images_to_appear = FALSE;
gboolean option_fading = FALSE;
gboolean option_lazy_load = FALSE;
gboolean option_allow_empty_window = FALSE;
gboolean option_lowmem = FALSE;
gboolean option_addl_from_stdin = FALSE;
gboolean option_recreate_window = FALSE;
gboolean option_enforce_window_aspect_ratio = FALSE;
gboolean cursor_visible = TRUE;
gboolean cursor_auto_hide_mode_enabled = FALSE;
gboolean option_negate = FALSE;
int cursor_auto_hide_timer_id = 0;
#ifndef CONFIGURED_WITHOUT_ACTIONS
gboolean option_actions_from_stdin = FALSE;
gboolean option_status_output = FALSE;
#else
static const gboolean option_actions_from_stdin = FALSE;
#endif
double option_fading_duration = .5;
double option_keyboard_timeout = .5;
gint option_max_depth = -1;
gboolean option_browse = FALSE;
enum { QUIT, WAIT, WRAP, WRAP_NO_RESHUFFLE } option_end_of_files_action = WRAP;
enum { ON, OFF, CHANGES_ONLY } option_watch_files = ON;
gchar *option_disable_backends;

double fading_current_alpha_stage = 0;
gint64 fading_initial_time;

#ifdef CONFIGURED_WITHOUT_ACTIONS
const
#endif
enum { AUTO, FAST, GOOD, BEST } option_interpolation_quality = AUTO;
enum { CHECKERBOARD, BLACK, WHITE } option_background_pattern = CHECKERBOARD;

gboolean options_background_pattern_callback(const gchar *option_name, const gchar *value, gpointer data, GError **error);
#ifndef CONFIGURED_WITHOUT_ACTIONS
gboolean options_bind_key_callback(const gchar *option_name, const gchar *value, gpointer data, GError **error);
char *key_binding_sequence_to_string(guint key_binding_value, gchar *prefix);
gboolean help_show_key_bindings(const gchar *option_name, const gchar *value, gpointer data, GError **error);
#endif
gboolean help_show_version(const gchar *option_name, const gchar *value, gpointer data, GError **error);
gboolean option_window_position_callback(const gchar *option_name, const gchar *value, gpointer data, GError **error);
gboolean option_thumbnail_size_callback(const gchar *option_name, const gchar *value, gpointer data, GError **error);
gboolean option_thumbnail_preload_callback(const gchar *option_name, const gchar *value, gpointer data, GError **error);
gboolean option_scale_level_callback(const gchar *option_name, const gchar *value, gpointer data, GError **error);
gboolean option_thumbnail_persistence_callback(const gchar *option_name, const gchar *value, gpointer data, GError **error);
gboolean option_end_of_files_action_callback(const gchar *option_name, const gchar *value, gpointer data, GError **error);
gboolean option_watch_files_callback(const gchar *option_name, const gchar *value, gpointer data, GError **error);
gboolean option_sort_key_callback(const gchar *option_name, const gchar *value, gpointer data, GError **error);
gboolean option_action_callback(const gchar *option_name, const gchar *value, gpointer data, GError **error);
#if !defined(CONFIGURED_WITHOUT_INFO_TEXT) || !defined(CONFIGURED_WITHOUT_MONTAGE_MODE)
gboolean option_box_colors_callback(const gchar *option_name, const gchar *value, gpointer data, GError **error);
#endif
void load_images_handle_parameter(char *param, load_images_state_t state, gint depth, GSList *recursion_folder_stack);

struct {
	gint x;
	gint y;
} option_window_position = { -2, -2 };

#ifndef CONFIGURED_WITHOUT_MONTAGE_MODE /* option --without-montage: Do not include support for a thumbnail overview */
struct {
	gboolean enabled;
	thumbnail_persist_mode_t persist;
	gchar *special_thumbnail_directory;
	gint width;
	gint height;
	gint auto_generate_for_adjacents;
} option_thumbnails = { 0, THUMBNAILS_PERSIST_RO, NULL, 128, 128, -1 };

struct {
	int scroll_y;
	BOSNode *selected_node;
	gboolean show_binding_overlays;
} montage_window_control;

enum { MONTAGE_MODE_WRAP_OFF, MONTAGE_MODE_WRAP_ROWS, MONTAGE_MODE_WRAP_FULL, _MONTAGE_MODE_WRAP_SENTINEL } option_montage_mode_wrap_mode = MONTAGE_MODE_WRAP_ROWS;
#endif

struct Point {
	int x;
	int y;
};

// The standard forbids casting object pointers to function pointers, but
// GLib requires it in its GOptionEntry structure.
PQIV_DISABLE_PEDANTIC

// Hint: Only types G_OPTION_ARG_NONE, G_OPTION_ARG_STRING, G_OPTION_ARG_DOUBLE/INTEGER and G_OPTION_ARG_CALLBACK are
// implemented for option parsing.
GOptionEntry options[] = {
	{ "transparent-background", 'c', 0, G_OPTION_ARG_NONE, &option_transparent_background, "Borderless transparent window", NULL },
	{ "slideshow-interval", 'd', 0, G_OPTION_ARG_DOUBLE, &option_slideshow_interval, "Set slideshow interval", "n" },
	{ "fullscreen", 'f', 0, G_OPTION_ARG_NONE, &option_start_fullscreen, "Start in fullscreen mode", NULL },
	{ "fade", 'F', 0, G_OPTION_ARG_NONE, (gpointer)&option_fading, "Fade between images", NULL },
#ifndef CONFIGURED_WITHOUT_INFO_TEXT
	{ "hide-info-box", 'i', 0, G_OPTION_ARG_NONE, &option_hide_info_box, "Initially hide the info box", NULL },
#endif
	{ "lazy-load", 'l', 0, G_OPTION_ARG_NONE, &option_lazy_load, "Display the main window as soon as one image is loaded", NULL },
	{ "sort", 'n', 0, G_OPTION_ARG_NONE, &option_sort, "Sort files in natural order", NULL },
	{ "window-position", 'P', 0, G_OPTION_ARG_CALLBACK, &option_window_position_callback, "Set initial window position (`x,y' or `off' to not position the window at all)", "POSITION" },
	{ "additional-from-stdin", 'r', 0, G_OPTION_ARG_NONE, &option_addl_from_stdin, "Read additional filenames/folders from stdin", NULL },
	{ "slideshow", 's', 0, G_OPTION_ARG_NONE, &option_start_with_slideshow_mode, "Activate slideshow mode", NULL },
	{ "scale-images-up", 't', G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, &option_scale_level_callback, "Scale images up to fill the whole screen", NULL },
	{ "window-title", 'T', 0, G_OPTION_ARG_STRING, &option_window_title, "Set the title of the window. See manpage for available variables.", "TITLE" },
	{ "zoom-level", 'z', 0, G_OPTION_ARG_DOUBLE, &option_initial_scale, "Set initial zoom level (1.0 is 100%)", "FLOAT" },
	{ "click-through", 0, 0, G_OPTION_ARG_NONE, &option_click_through, "Window does not accept mouse input", NULL },

#ifndef CONFIGURED_WITHOUT_EXTERNAL_COMMANDS
	{ "command-1", '1', 0, G_OPTION_ARG_STRING, &external_image_filter_commands[0], "Bind the external COMMAND to key 1. See manpage for extended usage (commands starting with `>' or `|'). Use 2..9 for further commands.", "COMMAND" },
	{ "command-2", '2', G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING, &external_image_filter_commands[1], NULL, NULL },
	{ "command-3", '3', G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING, &external_image_filter_commands[2], NULL, NULL },
	{ "command-4", '4', G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING, &external_image_filter_commands[3], NULL, NULL },
	{ "command-5", '5', G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING, &external_image_filter_commands[4], NULL, NULL },
	{ "command-6", '6', G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING, &external_image_filter_commands[5], NULL, NULL },
	{ "command-7", '7', G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING, &external_image_filter_commands[6], NULL, NULL },
	{ "command-8", '8', G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING, &external_image_filter_commands[7], NULL, NULL },
	{ "command-9", '9', G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING, &external_image_filter_commands[8], NULL, NULL },
#endif

#ifndef CONFIGURED_WITHOUT_ACTIONS
	{ "action", 0, 0, G_OPTION_ARG_CALLBACK, &option_action_callback, "Perform a given action", "ACTION" },
	{ "actions-from-stdin", 0, 0, G_OPTION_ARG_NONE, &option_actions_from_stdin, "Read actions from stdin", NULL },
	{ "allow-empty-window", 0, 0, G_OPTION_ARG_NONE, &option_allow_empty_window, "Show pqiv/do not quit even though no files are loaded", NULL },
#endif
	{ "background-pattern", 0, 0, G_OPTION_ARG_CALLBACK, &options_background_pattern_callback, "Set the background pattern to use for transparent images", "PATTERN" },
#ifndef CONFIGURED_WITHOUT_ACTIONS
	{ "bind-key", 0, 0, G_OPTION_ARG_CALLBACK, &options_bind_key_callback, "Rebind a key to another action, see manpage and --show-bindings output for details.", "KEY BINDING" },
#endif
#if !defined(CONFIGURED_WITHOUT_INFO_TEXT) || !defined(CONFIGURED_WITHOUT_MONTAGE_MODE)
	{ "box-colors", 0, 0, G_OPTION_ARG_CALLBACK, (gpointer)&option_box_colors_callback, "Set box colors", "TEXT:BACKGROUND" },
#endif
	{ "browse", 0, 0, G_OPTION_ARG_NONE, &option_browse, "For each command line argument, additionally load all images from the image's directory", NULL },
	{ "disable-backends", 0, 0, G_OPTION_ARG_STRING, &option_disable_backends, "Disable the given backends", "BACKENDS" },
	{ "disable-scaling", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, &option_scale_level_callback, "Disable scaling of images", NULL },
	{ "end-of-files-action", 0, 0, G_OPTION_ARG_CALLBACK, &option_end_of_files_action_callback, "Action to take after all images have been viewed. (`quit', `wait', `wrap', `wrap-no-reshuffle')", "ACTION" },
	{ "enforce-window-aspect-ratio", 0, 0, G_OPTION_ARG_NONE, &option_enforce_window_aspect_ratio, "Fix the aspect ratio of the window to match the current image's", NULL },
	{ "fade-duration", 0, 0, G_OPTION_ARG_DOUBLE, &option_fading_duration, "Adjust fades' duration", "SECONDS" },
	{ "low-memory", 0, 0, G_OPTION_ARG_NONE, &option_lowmem, "Try to keep memory usage to a minimum", NULL },
	{ "max-depth", 0, 0, G_OPTION_ARG_INT, &option_max_depth, "Descend at most LEVELS levels of directories below the command line arguments", "LEVELS" },
	{ "negate", 0, 0, G_OPTION_ARG_NONE, &option_negate, "Negate images: show negatives", NULL },
	{ "recreate-window", 0, 0, G_OPTION_ARG_NONE, &option_recreate_window, "Create a new window instead of resizing the old one", NULL },
	{ "scale-mode-screen-fraction", 0, 0, G_OPTION_ARG_DOUBLE, &option_scale_screen_fraction, "Screen fraction to use for auto-scaling", "FLOAT" },
	{ "shuffle", 0, 0, G_OPTION_ARG_NONE, &option_shuffle, "Shuffle files", NULL },
#ifndef CONFIGURED_WITHOUT_ACTIONS
	{ "show-bindings", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, &help_show_key_bindings, "Display the keyboard and mouse bindings and exit", NULL },
#endif
	{ "sort-key", 0, 0, G_OPTION_ARG_CALLBACK, &option_sort_key_callback, "Key to use for sorting", "PROPERTY" },
#ifndef CONFIGURED_WITHOUT_MONTAGE_MODE
	{ "thumbnail-size", 0, 0, G_OPTION_ARG_CALLBACK, &option_thumbnail_size_callback, "Set the dimensions of thumbnails in montage mode", "WIDTHxHEIGHT" },
	{ "thumbnail-preload", 0, 0, G_OPTION_ARG_CALLBACK, &option_thumbnail_preload_callback, "Preload the adjacent COUNT thumbnails", "COUNT" },
	{ "thumbnail-persistence", 0, 0, G_OPTION_ARG_CALLBACK, &option_thumbnail_persistence_callback, "Persist thumbnails to disk, to DIRECTORY.", "DIRECTORY" },
#endif
	{ "wait-for-images-to-appear", 0, 0, G_OPTION_ARG_NONE, &option_wait_for_images_to_appear, "If no images are found, wait until at least one appears", NULL },
	{ "watch-directories", 0, 0, G_OPTION_ARG_NONE, &option_watch_directories, "Watch directories for new files", NULL },
	{ "watch-files", 0, 0, G_OPTION_ARG_CALLBACK, &option_watch_files_callback, "Watch files for changes on disk (`on`, `off', `changes-only', i.e. do nothing on deletetion)", "VALUE" },
	{ "version", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, &help_show_version, "Show version information and quit", NULL },

	{ NULL, 0, 0, 0, NULL, NULL, NULL }
};

PQIV_ENABLE_PEDANTIC

/* Key bindings & actions {{{ */

#define  KEY_BINDINGS_KEY_TOKEN_BEGIN_SYMBOL           '<'
#define  KEY_BINDINGS_KEY_TOKEN_END_SYMBOL             '>'
#define  KEY_BINDINGS_COMMANDS_BEGIN_SYMBOL            '{'
#define  KEY_BINDINGS_COMMANDS_END_SYMBOL              '}'
#define  KEY_BINDINGS_COMMAND_SEPARATOR_SYMBOL         ';'
#define  KEY_BINDINGS_COMMAND_PARAMETER_BEGIN_SYMBOL   '('
#define  KEY_BINDINGS_COMMAND_PARAMETER_END_SYMBOL     ')'
#define  KEY_BINDINGS_CONTEXT_SWITCH_SYMBOL            '@'

#define KEY_BINDING_STATE_BITS 4
#define KEY_BINDING_VALUE(is_mouse, state, keycode) ((guint)((((unsigned)is_mouse & 1u) << 31) | (((state & ((1u << KEY_BINDING_STATE_BITS) - 1)) << (31 - KEY_BINDING_STATE_BITS)) | (keycode & ((1u << (31 - KEY_BINDING_STATE_BITS)) - 1u)))))

#define KEY_BINDING_CONTEXTS_COUNT 2
#ifndef CONFIGURED_WITHOUT_ACTIONS
const char * const key_binding_context_names[] = {
	"DEFAULT",
#ifndef CONFIGURED_WITHOUT_MONTAGE_MODE
	"MONTAGE",
#else
	"",
#endif
};
#endif
enum context_t { DEFAULT, MONTAGE };

#ifndef CONFIGURED_WITHOUT_MONTAGE_MODE
static char montage_mode_default_keys[] = "asdfghjkl";
#endif

static const struct default_key_bindings_struct {
	enum context_t context;
	guint key_binding_value;
	pqiv_action_t action;
	pqiv_action_parameter_t parameter;
} default_key_bindings[] = {
	{ DEFAULT, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_Up               ), ACTION_SHIFT_Y                         , { 10  }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_KP_Up            ), ACTION_SHIFT_Y                         , { 10  }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , GDK_CONTROL_MASK , GDK_KEY_Up               ), ACTION_SHIFT_Y                         , { 50  }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , GDK_CONTROL_MASK , GDK_KEY_KP_Up            ), ACTION_SHIFT_Y                         , { 50  }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_Down             ), ACTION_SHIFT_Y                         , { -10 }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_KP_Down          ), ACTION_SHIFT_Y                         , { -10 }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , GDK_CONTROL_MASK , GDK_KEY_Down             ), ACTION_SHIFT_Y                         , { -50 }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , GDK_CONTROL_MASK , GDK_KEY_KP_Down          ), ACTION_SHIFT_Y                         , { -50 }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_Left             ), ACTION_SHIFT_X                         , { 10  }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_KP_Left          ), ACTION_SHIFT_X                         , { 10  }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , GDK_CONTROL_MASK , GDK_KEY_Left             ), ACTION_SHIFT_X                         , { 50  }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , GDK_CONTROL_MASK , GDK_KEY_KP_Left          ), ACTION_SHIFT_X                         , { 50  }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_Right            ), ACTION_SHIFT_X                         , { -10 }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_KP_Right         ), ACTION_SHIFT_X                         , { -10 }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , GDK_CONTROL_MASK , GDK_KEY_Right            ), ACTION_SHIFT_X                         , { -50 }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , GDK_CONTROL_MASK , GDK_KEY_KP_Right         ), ACTION_SHIFT_X                         , { -50 }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , GDK_CONTROL_MASK , GDK_KEY_plus             ), ACTION_SET_SLIDESHOW_INTERVAL_RELATIVE , { .pdouble = 1.  }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , GDK_CONTROL_MASK , GDK_KEY_KP_Add           ), ACTION_SET_SLIDESHOW_INTERVAL_RELATIVE , { .pdouble = 1.  }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , GDK_MOD1_MASK    , GDK_KEY_KP_Add           ), ACTION_ANIMATION_SET_SPEED_RELATIVE    , { .pdouble = 1.1  }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , GDK_MOD1_MASK    , GDK_KEY_plus             ), ACTION_ANIMATION_SET_SPEED_RELATIVE    , { .pdouble = 1.1  }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_plus             ), ACTION_SET_SCALE_LEVEL_RELATIVE        , { .pdouble = 1.1 }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_KP_Add           ), ACTION_SET_SCALE_LEVEL_RELATIVE        , { .pdouble = 1.1 }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , GDK_CONTROL_MASK , GDK_KEY_minus            ), ACTION_SET_SLIDESHOW_INTERVAL_RELATIVE , { .pdouble = -1. }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , GDK_CONTROL_MASK , GDK_KEY_KP_Subtract      ), ACTION_SET_SLIDESHOW_INTERVAL_RELATIVE , { .pdouble = -1. }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , GDK_MOD1_MASK    , GDK_KEY_minus            ), ACTION_ANIMATION_SET_SPEED_RELATIVE    , { .pdouble = 0.9  }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , GDK_MOD1_MASK    , GDK_KEY_KP_Subtract      ), ACTION_ANIMATION_SET_SPEED_RELATIVE    , { .pdouble = 0.9  }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_minus            ), ACTION_SET_SCALE_LEVEL_RELATIVE        , { .pdouble = 0.9 }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_KP_Subtract      ), ACTION_SET_SCALE_LEVEL_RELATIVE        , { .pdouble = 0.9 }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_t                ), ACTION_TOGGLE_SCALE_MODE               , { 0   }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , GDK_CONTROL_MASK , GDK_KEY_t                ), ACTION_TOGGLE_SCALE_MODE               , { 4   }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , GDK_MOD1_MASK    , GDK_KEY_t                ), ACTION_TOGGLE_SCALE_MODE               , { 5   }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , GDK_CONTROL_MASK , GDK_KEY_r                ), ACTION_TOGGLE_SHUFFLE_MODE             , { 0   }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_r                ), ACTION_RELOAD                          , { 0   }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , GDK_CONTROL_MASK , GDK_KEY_p                ), ACTION_GOTO_EARLIER_FILE               , { 0   }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_0                ), ACTION_RESET_SCALE_LEVEL               , { 0   }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_f                ), ACTION_TOGGLE_FULLSCREEN               , { 0   }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_h                ), ACTION_FLIP_HORIZONTALLY               , { 0   }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_v                ), ACTION_FLIP_VERTICALLY                 , { 0   }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_l                ), ACTION_ROTATE_LEFT                     , { 0   }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_k                ), ACTION_ROTATE_RIGHT                    , { 0   }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_i                ), ACTION_TOGGLE_INFO_BOX                 , { 0   }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_j                ), ACTION_JUMP_DIALOG                     , { 0   }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_m                ), ACTION_MONTAGE_MODE_ENTER              , { 0   }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_s                ), ACTION_TOGGLE_SLIDESHOW                , { 0   }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_b                ), ACTION_TOGGLE_BACKGROUND_PATTERN       , { 0   }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_n                ), ACTION_TOGGLE_NEGATE_MODE              , { 0   }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , GDK_CONTROL_MASK , GDK_KEY_a                ), ACTION_HARDLINK_CURRENT_IMAGE          , { 0   }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_period           ), ACTION_ANIMATION_STEP                  , { 1   }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , GDK_CONTROL_MASK , GDK_KEY_period           ), ACTION_ANIMATION_CONTINUE              , { 0   }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , GDK_CONTROL_MASK , GDK_KEY_BackSpace        ), ACTION_GOTO_LOGICAL_DIRECTORY_RELATIVE , { -1  }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_BackSpace        ), ACTION_GOTO_FILE_RELATIVE              , { -1  }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , GDK_CONTROL_MASK , GDK_KEY_space            ), ACTION_GOTO_LOGICAL_DIRECTORY_RELATIVE , { 1   }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_space            ), ACTION_GOTO_FILE_RELATIVE              , { 1   }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , GDK_CONTROL_MASK , GDK_KEY_Page_Up          ), ACTION_GOTO_FILE_RELATIVE              , { 10  }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , GDK_CONTROL_MASK , GDK_KEY_KP_Page_Up       ), ACTION_GOTO_FILE_RELATIVE              , { 10  }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_Page_Down        ), ACTION_GOTO_FILE_RELATIVE              , { -10 }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_Page_Up          ), ACTION_GOTO_FILE_RELATIVE              , { 10  }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_KP_Page_Up       ), ACTION_GOTO_FILE_RELATIVE              , { 10  }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_KP_Page_Down     ), ACTION_GOTO_FILE_RELATIVE              , { -10 }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_q                ), ACTION_QUIT                            , { 0   }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_Escape           ), ACTION_QUIT                            , { 0   }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_1                ), ACTION_NUMERIC_COMMAND                 , { 1   }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_2                ), ACTION_NUMERIC_COMMAND                 , { 2   }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_3                ), ACTION_NUMERIC_COMMAND                 , { 3   }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_4                ), ACTION_NUMERIC_COMMAND                 , { 4   }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_5                ), ACTION_NUMERIC_COMMAND                 , { 5   }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_6                ), ACTION_NUMERIC_COMMAND                 , { 6   }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_7                ), ACTION_NUMERIC_COMMAND                 , { 7   }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_8                ), ACTION_NUMERIC_COMMAND                 , { 8   }},
	{ DEFAULT, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_9                ), ACTION_NUMERIC_COMMAND                 , { 9   }},

	{ DEFAULT, KEY_BINDING_VALUE(1 , 0                , GDK_BUTTON_PRIMARY       ), ACTION_GOTO_FILE_RELATIVE              , { -1  }},
	{ DEFAULT, KEY_BINDING_VALUE(1 , 0                , GDK_BUTTON_MIDDLE        ), ACTION_QUIT                            , { 0   }},
	{ DEFAULT, KEY_BINDING_VALUE(1 , 0                , GDK_BUTTON_SECONDARY     ), ACTION_GOTO_FILE_RELATIVE              , { 1   }},
	{ DEFAULT, KEY_BINDING_VALUE(1 , 0                , (GDK_SCROLL_UP+1) << 2   ), ACTION_GOTO_FILE_RELATIVE              , { 1   }},
	{ DEFAULT, KEY_BINDING_VALUE(1 , 0                , (GDK_SCROLL_DOWN+1) << 2 ), ACTION_GOTO_FILE_RELATIVE              , { -1  }},

#ifndef CONFIGURED_WITHOUT_MONTAGE_MODE
	{ MONTAGE, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_Down             ), ACTION_MONTAGE_MODE_SHIFT_Y            , { 1   }},
	{ MONTAGE, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_Up               ), ACTION_MONTAGE_MODE_SHIFT_Y            , { -1  }},
	{ MONTAGE, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_Left             ), ACTION_MONTAGE_MODE_SHIFT_X            , { -1  }},
	{ MONTAGE, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_Right            ), ACTION_MONTAGE_MODE_SHIFT_X            , { 1   }},
	{ MONTAGE, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_KP_Down          ), ACTION_MONTAGE_MODE_SHIFT_Y            , { 1   }},
	{ MONTAGE, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_KP_Up            ), ACTION_MONTAGE_MODE_SHIFT_Y            , { -1  }},
	{ MONTAGE, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_KP_Left          ), ACTION_MONTAGE_MODE_SHIFT_X            , { -1  }},
	{ MONTAGE, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_KP_Right         ), ACTION_MONTAGE_MODE_SHIFT_X            , { 1   }},
	{ MONTAGE, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_Page_Down        ), ACTION_MONTAGE_MODE_SHIFT_Y_PG         , { 1 }},
	{ MONTAGE, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_Page_Up          ), ACTION_MONTAGE_MODE_SHIFT_Y_PG         , { -1  }},
	{ MONTAGE, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_KP_Page_Up       ), ACTION_MONTAGE_MODE_SHIFT_Y_PG         , { -1  }},
	{ MONTAGE, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_KP_Page_Down     ), ACTION_MONTAGE_MODE_SHIFT_Y_PG         , { 1 }},
	{ MONTAGE, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_Home             ), ACTION_GOTO_FILE_BYINDEX               , { 0   }},
	{ MONTAGE, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_End              ), ACTION_GOTO_FILE_BYINDEX               , { -1  }},

	{ MONTAGE, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_Return           ), ACTION_MONTAGE_MODE_RETURN_PROCEED     , { 0   }},
	{ MONTAGE, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_Escape           ), ACTION_MONTAGE_MODE_RETURN_CANCEL      , { 0   }},
	{ MONTAGE, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_m                ), ACTION_MONTAGE_MODE_RETURN_CANCEL      , { 0   }},
	{ MONTAGE, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_f                ), ACTION_TOGGLE_FULLSCREEN               , { 0   }},
	{ MONTAGE, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_g                ), ACTION_MONTAGE_MODE_FOLLOW             , { .pcharptr = (char*)montage_mode_default_keys }},
	{ MONTAGE, KEY_BINDING_VALUE(0 , 0                , GDK_KEY_q                ), ACTION_QUIT                            , { 0   }},

	{ MONTAGE, KEY_BINDING_VALUE(1 , 0                , (GDK_SCROLL_UP+1) << 2   ), ACTION_MONTAGE_MODE_SHIFT_Y_ROWS       , { 1   }},
	{ MONTAGE, KEY_BINDING_VALUE(1 , 0                , (GDK_SCROLL_DOWN+1) << 2 ), ACTION_MONTAGE_MODE_SHIFT_Y_ROWS       , { -1  }},
#endif

	{ DEFAULT, 0, 0, { 0 } }
};

enum context_t active_key_binding_context = DEFAULT;
enum context_t application_mode = DEFAULT;

#ifndef CONFIGURED_WITHOUT_ACTIONS
typedef struct key_binding key_binding_t;
struct key_binding {
	pqiv_action_t action;
	pqiv_action_parameter_t parameter;

	struct key_binding *next_action;    // For assinging multiple actions to one key
	GHashTable *next_key_bindings;      // For key sequences
};
GHashTable *key_bindings[KEY_BINDING_CONTEXTS_COUNT];
struct {
	key_binding_t *key_binding;
	BOSNode *associated_image;
	gint timeout_id;
} active_key_binding = { NULL, NULL, -1 };
GQueue action_queue = G_QUEUE_INIT;
gint action_queue_idle_id = -1;
void help_show_single_action(key_binding_t *current_action);

#ifndef CONFIGURED_WITHOUT_MONTAGE_MODE
key_binding_t follow_mode_key_binding = { ACTION_MONTAGE_MODE_FOLLOW_PROCEED, { .p2short = { -1, -1 } }, NULL, NULL };
#endif

#endif
void UNUSED_FUNCTION action_done();

const struct pqiv_action_descriptor {
	const char *name;
	enum { PARAMETER_INT, PARAMETER_DOUBLE, PARAMETER_CHARPTR, PARAMETER_2SHORT, PARAMETER_NONE } parameter_type;
} pqiv_action_descriptors[] = {
	{ "nop", PARAMETER_NONE },
	{ "shift_y", PARAMETER_INT },
	{ "shift_x", PARAMETER_INT },
	{ "set_slideshow_interval_relative", PARAMETER_DOUBLE },
	{ "set_slideshow_interval_absolute", PARAMETER_DOUBLE },
	{ "set_scale_level_relative", PARAMETER_DOUBLE },
	{ "set_scale_level_absolute", PARAMETER_DOUBLE },
	{ "toggle_scale_mode", PARAMETER_INT },
	{ "set_scale_mode_screen_fraction", PARAMETER_DOUBLE },
	{ "toggle_shuffle_mode", PARAMETER_INT },
	{ "reload", PARAMETER_NONE },
	{ "reset_scale_level", PARAMETER_NONE },
	{ "toggle_fullscreen", PARAMETER_INT },
	{ "flip_horizontally", PARAMETER_NONE },
	{ "flip_vertically", PARAMETER_NONE },
	{ "rotate_left", PARAMETER_NONE },
	{ "rotate_right", PARAMETER_NONE },
	{ "toggle_info_box", PARAMETER_NONE },
	{ "jump_dialog", PARAMETER_NONE },
	{ "toggle_slideshow", PARAMETER_NONE },
	{ "hardlink_current_image", PARAMETER_NONE },
	{ "goto_directory_relative", PARAMETER_INT },
	{ "goto_logical_directory_relative", PARAMETER_INT },
	{ "goto_file_relative", PARAMETER_INT },
	{ "quit", PARAMETER_NONE },
	{ "numeric_command", PARAMETER_INT },
	{ "command", PARAMETER_CHARPTR },
	{ "add_file", PARAMETER_CHARPTR },
	{ "goto_file_byindex", PARAMETER_INT },
	{ "goto_file_byname", PARAMETER_CHARPTR },
	{ "remove_file_byindex", PARAMETER_INT },
	{ "remove_file_byname", PARAMETER_CHARPTR },
	{ "output_file_list", PARAMETER_NONE },
	{ "set_cursor_visibility", PARAMETER_INT },
	{ "set_status_output", PARAMETER_INT },
	{ "set_scale_mode_fit_px", PARAMETER_2SHORT },
	{ "set_shift_x", PARAMETER_INT },
	{ "set_shift_y", PARAMETER_INT },
	{ "bind_key", PARAMETER_CHARPTR },
	{ "send_keys", PARAMETER_CHARPTR },
	{ "set_shift_align_corner", PARAMETER_CHARPTR },
	{ "set_interpolation_quality", PARAMETER_INT },
	{ "animation_step", PARAMETER_INT },
	{ "animation_continue", PARAMETER_NONE },
	{ "animation_set_speed_absolute", PARAMETER_DOUBLE },
	{ "animation_set_speed_relative", PARAMETER_DOUBLE },
	{ "goto_earlier_file", PARAMETER_NONE },
	{ "set_cursor_auto_hide", PARAMETER_INT },
	{ "set_fade_duration", PARAMETER_DOUBLE },
	{ "set_keyboard_timeout", PARAMETER_DOUBLE },
	{ "set_thumbnail_size", PARAMETER_2SHORT },
	{ "set_thumbnail_preload", PARAMETER_INT },
	{ "montage_mode_enter", PARAMETER_NONE },
	{ "montage_mode_shift_x", PARAMETER_INT },
	{ "montage_mode_shift_y", PARAMETER_INT },
	{ "montage_mode_set_shift_x", PARAMETER_INT },
	{ "montage_mode_set_shift_y", PARAMETER_INT },
	{ "montage_mode_set_wrap_mode", PARAMETER_INT },
	{ "montage_mode_shift_y_pg", PARAMETER_INT },
	{ "montage_mode_shift_y_rows", PARAMETER_INT },
	{ "montage_mode_show_binding_overlays", PARAMETER_INT },
	{ "montage_mode_follow", PARAMETER_CHARPTR },
	{ "montage_mode_follow_proceed", PARAMETER_2SHORT },
	{ "montage_mode_return_proceed", PARAMETER_NONE },
	{ "montage_mode_return_cancel", PARAMETER_NONE },
	{ "move_window", PARAMETER_2SHORT },
	{ "toggle_background_pattern", PARAMETER_INT },
	{ "toggle_negate_mode", PARAMETER_INT },
	{ NULL, 0 }
};
/* }}} */

typedef struct {
	gint depth;
	GTree *outstanding_files;
	GSList *recursion_folder_stack;
	gchar *base_param;
} directory_watch_options_t;
GHashTable *active_directory_watches;

void set_scale_level_to_fit();
void set_scale_level_for_screen();
#ifndef CONFIGURED_WITHOUT_INFO_TEXT
	void info_text_queue_redraw();
	void update_info_text(const char *);
	#define UPDATE_INFO_TEXT(fmt, ...) { \
		gchar *_info_text = g_strdup_printf(fmt, __VA_ARGS__);\
		update_info_text(_info_text); \
		g_free(_info_text); \
	}
#else
	#define info_text_queue_redraw(...)
	#define update_info_text(...)
	#define UPDATE_INFO_TEXT(fmt, ...)
#endif
void queue_draw();
gboolean main_window_center();
void window_screen_changed_callback(GtkWidget *widget, GdkScreen *previous_screen, gpointer user_data);
typedef int image_loader_purpose_t;
gboolean test_and_invalidate_thumbnail(file_t *file);
gboolean image_loader_load_single(BOSNode *node, gboolean called_from_main);
gboolean fading_timeout_callback(gpointer user_data);
void queue_image_load(BOSNode *);
#ifndef CONFIGURED_WITHOUT_MONTAGE_MODE
void queue_thumbnail_load(BOSNode *);
#endif
void unload_image(BOSNode *);
void remove_image(BOSNode *);
gboolean initialize_gui_callback(gpointer);
gboolean initialize_image_loader();
void window_hide_cursor();
void window_show_cursor();
void preload_adjacent_images();
void window_center_mouse();
double calculate_scale_level_to_fit(int image_width, int image_height, int window_width, int window_height);
gboolean main_window_calculate_ideal_size(int *new_window_width, int *new_window_height);
void calculate_current_image_transformed_size(int *image_width, int *image_height);
double calculate_auto_scale_level_for_screen(int image_width, int image_height);
cairo_surface_t *get_scaled_image_surface_for_current_image();
gboolean window_state_into_fullscreen_actions(gpointer user_data);
gboolean window_state_out_of_fullscreen_actions(gpointer user_data);
gboolean window_draw_callback(GtkWidget *widget, cairo_t *cr_arg, gpointer user_data);
void window_prerender_background_pixmap(int window_width, int window_height, double scale_level, gboolean fullscreen);
void window_clear_background_pixmap();
gboolean window_show_background_pixmap_cb(gpointer user_data);
BOSNode *image_pointer_by_name(gchar *display_name);
BOSNode *relative_image_pointer(ptrdiff_t movement);
void file_tree_free_helper(BOSNode *node);
gint relative_image_pointer_shuffle_list_cmp(shuffled_image_ref_t *ref, BOSNode *node);
void relative_image_pointer_shuffle_list_unref_fn(shuffled_image_ref_t *ref);
gboolean slideshow_timeout_callback(gpointer user_data);
gboolean absolute_image_movement(BOSNode *ref);
#ifndef CONFIGURED_WITHOUT_ACTIONS
void parse_key_bindings(const gchar *bindings);
gboolean read_commands_thread_helper(gpointer command);
#endif
void recreate_window();
static void status_output();
void handle_input_event(guint key_binding_value);
void draw_current_image_to_context(cairo_t *cr);
gboolean window_auto_hide_cursor_callback(gpointer user_data);
#ifndef CONFIGURED_WITHOUT_ACTIONS
gboolean handle_input_event_timeout_callback(gpointer user_data);
#endif
#ifndef CONFIGURED_WITHOUT_MONTAGE_MODE
gboolean montage_window_get_move_cursor_target(int, int, int, int*, int*, int*, BOSNode **);
void montage_window_move_cursor(int, int, int);
#endif
// }}}
/* Helper functions {{{ */
gboolean strv_contains(const gchar * const *strv, const gchar *str) {
	#if GLIB_CHECK_VERSION(2, 44, 0)
		return g_strv_contains(strv, str);
	#else
		while(*strv) {
			if(g_strcmp0(*strv, str) == 0) {
				return TRUE;
			}
			strv++;
		}
		return FALSE;
	#endif
}
/* }}} */
/* Command line handling, creation of the image list {{{ */
gboolean options_background_pattern_callback(const gchar *option_name, const gchar *value, gpointer data, GError **error) {/*{{{*/
	if(strcasecmp(value, "checkerboard") == 0) {
		option_background_pattern = CHECKERBOARD;
	}
	else if(strcasecmp(value, "black") == 0) {
		option_background_pattern = BLACK;
	}
	else if(strcasecmp(value, "white") == 0) {
		option_background_pattern = WHITE;
	}
	else {
		g_set_error(error, G_OPTION_ERROR, G_OPTION_ERROR_FAILED, "Unexpected argument value for the --background-pattern option. Allowed values are BLACK, WHITE and CHECKERBOARD.");
		return FALSE;
	}

	return TRUE;
}/*}}}*/
#ifndef CONFIGURED_WITHOUT_ACTIONS /* option --without-actions: Do not include support for configurable key/mouse bindings and actions */
gboolean options_bind_key_callback(const gchar *option_name, const gchar *value, gpointer data, GError **error) {/*{{{*/
	// Format for value:
	//  {key sequence description, special keys as <name>} { {action}({parameter});[...] } [...]
	//
	// Special names are: <Shift>, <Control>, <Alt> (GDK_MOD1_MASK), <Mouse-%d> and any other must be fed to gdk_keyval_from_name
	// String parameters must be given in quotes
	// To set an error:
	// g_set_error(error, G_OPTION_ERROR, G_OPTION_ERROR_FAILED, "The argument to the alias option must have a multiple of two characters: Every odd one is mapped to the even one following it.");
	//
	parse_key_bindings(value);

	return TRUE;
}/*}}}*/
#endif
#ifndef CONFIGURED_WITHOUT_MONTAGE_MODE
gboolean option_thumbnail_size_callback(const gchar *option_name, const gchar *value, gpointer data, GError **error) {/*{{{*/
	gchar *second;
	option_thumbnails.width = g_ascii_strtoll(value, &second, 10);
	if(second != value && (*second == 'x' || *second == ',')) {
		option_thumbnails.height = g_ascii_strtoll(second + 1, &second, 10);
		if(*second == 0) {
			return TRUE;
		}
	}

	g_set_error(error, G_OPTION_ERROR, G_OPTION_ERROR_FAILED, "Unexpected argument value for the --thumbnail-size option. Format must be e.g. `320x240'.");
	return FALSE;
}/*}}}*/
gboolean option_thumbnail_preload_callback(const gchar *option_name, const gchar *value, gpointer data, GError **error) {/*{{{*/
	option_thumbnails.enabled = 1;
	option_thumbnails.auto_generate_for_adjacents = g_ascii_strtoll(value, NULL, 10);

	if(errno == EINVAL || errno == ERANGE) {
		g_set_error(error, G_OPTION_ERROR, G_OPTION_ERROR_FAILED, "Unexpected argument value for the --thumbnail-preload option.");
		return FALSE;
	}
	return TRUE;
}/*}}}*/
gboolean option_thumbnail_persistence_callback(const gchar *option_name, const gchar *value, gpointer data, GError **error) {/*{{{*/
	if(option_thumbnails.special_thumbnail_directory != NULL) {
		g_free(option_thumbnails.special_thumbnail_directory);
		option_thumbnails.special_thumbnail_directory = NULL;
	}
	if(value == NULL || !*value || strcasecmp(value, "yes") == 0 || strcasecmp(value, "true") == 0 || strcasecmp(value, "1") == 0 || strcasecmp(value, "on") == 0) {
		option_thumbnails.persist = THUMBNAILS_PERSIST_ON;
	}
	else if(strcasecmp(value, "read-only") == 0) {
		option_thumbnails.persist = THUMBNAILS_PERSIST_RO;
	}
	else if(strcasecmp(value, "standard") == 0) {
		option_thumbnails.persist = THUMBNAILS_PERSIST_STANDARD;
	}
	else if(strcasecmp(value, "no") == 0 || strcasecmp(value, "false") == 0 || strcasecmp(value, "1") == 0 || strcasecmp(value, "off") == 0) {
		option_thumbnails.persist = THUMBNAILS_PERSIST_OFF;
	}
	else if(strcasecmp(value, "local") == 0) {
		option_thumbnails.persist = THUMBNAILS_PERSIST_LOCAL;
	}
	else if(value[0] == '/') {
		option_thumbnails.persist = THUMBNAILS_PERSIST_ON;
		option_thumbnails.special_thumbnail_directory = g_strdup(value);
	}
	else {
		g_set_error(error, G_OPTION_ERROR, G_OPTION_ERROR_FAILED, "Unexpected argument value for the --thumbnail-persistence option.");
		return FALSE;
	}
	return TRUE;
}/*}}}*/
#endif
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
		option_scale = AUTO_SCALEUP;
	}
	else {
		option_scale = NO_SCALING;
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
#if !defined(CONFIGURED_WITHOUT_INFO_TEXT) || !defined(CONFIGURED_WITHOUT_MONTAGE_MODE)
gboolean option_box_colors_callback(const gchar *option_name, const gchar *value, gpointer data, GError **error) {/*{{{*/
	// Parse manually rather than with sscanf to have the flexibility to use
	// hex notation without doing black magic
	unsigned char pos;
	for(pos=0; pos < 6 && value && *value; pos++) {
		while(*value == ' ') {
			value++;
		}
		if(pos % 3 == 0 && *value == '#') {
			value++;
			unsigned char ipos = pos + 3;
			for(; pos<ipos && value; pos++) {
				unsigned char mchar = 0;
				for(int i=0; i<2; i++) {
					mchar = mchar << 4;
					if(!*value) {
						break;
					}
					if(*value >= 'a' && *value <= 'f') {
						mchar |= (*value - 'a') + 10;
					}
					else if(*value >= 'A' && *value <= 'F') {
						mchar |= (*value - 'A') + 10;
					}
					else if(*value >= '0' && *value <= '9') {
						mchar |= *value - '0';
					}
					else {
						value = NULL;
						break;
					}

					value++;
				}
				*((double *)&option_box_colors + pos) = mchar / 255.;
			}
			pos--;
		}
		else {
			unsigned ivalue = 0;
			while(*value && *value >= '0' && *value <= '9') {
				ivalue = ivalue * 10 + (*value - '0');
				value++;
			}
			*((double *)&option_box_colors + pos) = ivalue / 255.;
			if(pos != 2) {
				while(*value == ' ') {
					value++;
				}
				if(*value == ',') {
					value++;
				}
				else {
					value = NULL;
				}
			}
		}

		if(pos == 2) {
			while(*value == ' ') {
				value++;
			}
			if(*value == ':') {
				value++;
			}
			else {
				value = NULL;
			}
		}
	}

	if(pos != 6) {
		g_set_error(error, G_OPTION_ERROR, G_OPTION_ERROR_FAILED, "Unexpected argument value for the --box-colors option. Syntax is foreground-color:background-color, colors either given as a r,g,b pair or #aabbcc hex code.");
		return FALSE;
	}
	return TRUE;
}/*}}}*/
#endif
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
#ifndef CONFIGURED_WITHOUT_ACTIONS
gboolean option_action_callback(const gchar *option_name, const gchar *value, gpointer data, GError **error) {/*{{{*/
	gdk_threads_add_idle(read_commands_thread_helper, g_strdup(value));
	return TRUE;
}/*}}}*/
#endif
void parse_configuration_file_callback(char *section, char *key, config_parser_value_t *value) {
	int * const argc = &global_argc;
	char *** const argv = &global_argv;

	config_parser_tolower(section);
	config_parser_tolower(key);

	if(!section && !key) {
		// Classic pqiv 1.x configuration file: Append to argv {{{
		config_parser_strip_comments(value->chrpval);
		char *options_contents = value->chrpval;

		gint additional_arguments = 0;
		gint additional_arguments_max = 10;

		// Add configuration file contents to argument vector
		char **new_argv = (char **)g_malloc(sizeof(char *) * (*argc + additional_arguments_max + 1));
		new_argv[0] = (*argv)[0];
		char *end_of_argument;
		while(*options_contents != 0) {
			end_of_argument = strpbrk(options_contents, " \n\t");
			if(end_of_argument == options_contents) {
				options_contents++;
				continue;
			}
			else if(end_of_argument != NULL) {
				*end_of_argument = 0;
			}
			else {
				end_of_argument = options_contents + strlen(options_contents);
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
							iter->description = g_strdup_printf("[Set to do not/disable:] %s", iter->description);
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
									iter->description = g_strdup_printf("[Set to do not/disable:] %s", iter->description);
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
			if(!argv_val[0]) {
				continue;
			}

			// Add to argument vector
			new_argv[1 + additional_arguments] = g_strdup(argv_val);
			options_contents = end_of_argument;
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

		return;
		// }}}
	}
	else if(!section) {
		// Key/value entries in a non-section.
		g_printerr("Failed to handle non-section entry `%s' in configuration file.\n", key);
	}
	else if(strcmp(section, "options") == 0 && key) {
		// pqiv 2.x configuration setting {{{
		GError *error_pointer = NULL;
		for(GOptionEntry *iter = options; iter->arg_data != NULL; iter++) {
			if(iter->long_name != NULL && strcmp(iter->long_name, key) == 0) {
				switch(iter->arg) {
					case G_OPTION_ARG_NONE: {
						*(gboolean *)(iter->arg_data) = !!value->intval;
						if(value->intval) {
							iter->flags |= G_OPTION_FLAG_REVERSE;
							iter->description = g_strdup_printf("[Set to do not/disable:] %s", iter->description);
						}
					} break;
					case G_OPTION_ARG_CALLBACK:
					case G_OPTION_ARG_STRING:
						if(value->chrpval != NULL) {
							if(iter->arg == G_OPTION_ARG_CALLBACK) {
								gchar long_name[64];
								g_snprintf(long_name, 64, "--%s", iter->long_name);
								PQIV_DISABLE_PEDANTIC
								((GOptionArgFunc)(iter->arg_data))(long_name, value->chrpval, NULL, &error_pointer);
								PQIV_ENABLE_PEDANTIC
							}
							else {
								*(gchar **)(iter->arg_data) = g_strdup(value->chrpval);
							}
						}
						break;
					case G_OPTION_ARG_INT:
						*(gint *)(iter->arg_data) = value->intval;
						break;
					case G_OPTION_ARG_DOUBLE:
						*(gdouble *)(iter->arg_data) = value->doubleval;
						break;
					default:
						// Unimplemented. See options array.
					break;
				}
			}
		}
		if(error_pointer != NULL) {
			if(error_pointer->code == G_KEY_FILE_ERROR_INVALID_VALUE) {
				g_printerr("Failed to load setting for `%s' from configuration file: %s\n", key, error_pointer->message);
			}
			g_clear_error(&error_pointer);
		}
		// }}}
	}
#ifndef CONFIGURED_WITHOUT_ACTIONS
	else if(strcmp(section, "keybindings") == 0 && !key) {
		config_parser_strip_comments(value->chrpval);
		parse_key_bindings(value->chrpval);
	}
	else if(strcmp(section, "actions") == 0 && !key) {
		config_parser_strip_comments(value->chrpval);

		gchar *actions = g_strdup(value->chrpval);
		gdk_threads_add_idle(read_commands_thread_helper, actions);
	}
#endif
}
void parse_configuration_file() {/*{{{*/
	// Check for a configuration file
	const gchar *env_config_file = g_getenv("PQIVRC_PATH");
	if(env_config_file) {
		if(g_file_test(env_config_file, G_FILE_TEST_EXISTS)) {
			config_parser_parse_file(env_config_file, parse_configuration_file_callback);
		}
		return;
	}

	GQueue *test_dirs = g_queue_new();
	const gchar *config_dir = g_getenv("XDG_CONFIG_HOME");
	if(!config_dir) {
		g_queue_push_tail(test_dirs, g_build_filename(g_getenv("HOME"), ".config", "pqivrc", NULL));
	}
	else {
		g_queue_push_tail(test_dirs, g_build_filename(config_dir, "pqivrc", NULL));
	}
	g_queue_push_tail(test_dirs, g_build_filename(g_getenv("HOME"), ".pqivrc", NULL));
	const gchar *system_config_dirs = g_getenv("XDG_CONFIG_DIRS");
	if(system_config_dirs) {
		gchar **split_system_config_dirs = g_strsplit(system_config_dirs, ":", 0);
		for(gchar **system_dir = split_system_config_dirs; *system_dir; system_dir++) {
			g_queue_push_tail(test_dirs, g_build_filename(*system_dir, "pqivrc", NULL));
		}
		g_strfreev(split_system_config_dirs);
	}
	g_queue_push_tail(test_dirs, g_build_filename(G_DIR_SEPARATOR_S "etc", "pqivrc", NULL));

	gchar *config_file_name;
	while((config_file_name = g_queue_pop_head(test_dirs))) {
		if(g_file_test(config_file_name, G_FILE_TEST_EXISTS)) {
			config_parser_parse_file(config_file_name, parse_configuration_file_callback);
			g_free(config_file_name);
			break;
		}
		g_free(config_file_name);
	}

	while((config_file_name = g_queue_pop_head(test_dirs))) {
		g_free(config_file_name);
	}
	g_queue_free(test_dirs);
}/*}}}*/
void parse_command_line() {/*{{{*/
	GOptionContext *parser = g_option_context_new("FILES");
	g_option_context_set_summary(parser, "Powerful quick image viewer\npqiv version " PQIV_VERSION PQIV_VERSION_DEBUG " by Phillip Berndt");
	g_option_context_set_help_enabled(parser, TRUE);
	g_option_context_set_ignore_unknown_options(parser, FALSE);
	g_option_context_add_main_entries(parser, options, NULL);
	g_option_context_add_group(parser, gtk_get_option_group(TRUE));

	GError *error_pointer = NULL;
	if(g_option_context_parse(parser, &global_argc, &global_argv, &error_pointer) == FALSE) {
		g_printerr("%s\n", error_pointer->message);
		exit(1);
	}

	// User didn't specify any files to load; perhaps some help on how to use
	// pqiv would be useful...
	if (global_argc == 1 && !option_addl_from_stdin && !option_actions_from_stdin) {
		g_printerr("%s", g_option_context_get_help(parser, TRUE, NULL));
		exit(0);
	}

	g_option_context_free(parser);
}/*}}}*/
void load_images_directory_watch_callback(GFileMonitor *monitor, GFile *file, GFile *other_file, GFileMonitorEvent event_type, directory_watch_options_t *options) {/*{{{*/
	// The current image holds its own file watch, so we do not have to react
	// to changes. Only handle creation.

	// Canonicalize the name of the object. GFileMonitor reports absolute file names, use relative paths
	// instead if the user did as well on the command line.
	gchar *name = NULL;
	if(event_type == G_FILE_MONITOR_EVENT_CREATED || event_type == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT) {
		name = g_file_get_path(file);
		if(options->recursion_folder_stack && options->base_param) {
			size_t original_path_length = strlen(options->recursion_folder_stack->data);
			if(original_path_length && original_path_length < strlen(name)) {
				gchar *new_name = g_strdup_printf("%s%s", options->base_param, name + original_path_length);
				g_free(name);
				name = new_name;
			}
		}
	}

	// Skip .sh_thumbnails directories
	if(name && strstr(name, G_DIR_SEPARATOR_S ".sh_thumbnails" G_DIR_SEPARATOR_S) != NULL) {
		g_free(name);
		return;
	}

	if(event_type == G_FILE_MONITOR_EVENT_CREATED && name != NULL) {
		// In theory, handling regular files here should suffice. But files in subdirectories
		// seem not always to be recognized correctly by file monitors, so we have to install
		// one for each directory.
		//
		// One catch is that we should not handle files right away, because they might not
		// be completely written to disk at this point.
		//
		if(g_file_test(name, G_FILE_TEST_IS_DIR)) {
			// Use the standard loading mechanism. If directory watches are enabled,
			// the temporary variables used therein are not freed.
			load_images_handle_parameter(name, INOTIFY, options->depth, options->recursion_folder_stack);
		}
		else if(g_file_test(name, G_FILE_TEST_IS_REGULAR | G_FILE_TEST_IS_SYMLINK)) {
			// Defer call to load_images_handle_parameter.
			// name now belongs to the hash table, do not free it here.
			// TODO Option for later: Insert time as value and regularly cleanup the hash.
			g_tree_replace(options->outstanding_files, name, NULL);
			name = NULL;
		}
	}
	else if(event_type == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT) {
		if(g_tree_remove(options->outstanding_files, name)) {
			// The file was in the "new files" hash. Add it now.
			load_images_handle_parameter(name, INOTIFY, options->depth, options->recursion_folder_stack);
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

	if(name != NULL) {
		g_free(name);
	}
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

	if(bostree_node_count(file_tree) >= INT_MAX) {
		// This is a safegoard. Most image operations should actually have
		// ULONG_MAX as a limit, but sometimes, I cast to an integer type.
		g_printerr("Cannot add image %s: Maximum number of images reached.\n", file->display_name);
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
		#ifndef CONFIGURED_WITHOUT_MONTAGE_MODE
		if(application_mode == MONTAGE) {
			D_LOCK(file_tree);
			montage_window_move_cursor(0, 0,  0);
			D_UNLOCK(file_tree);
			gtk_widget_queue_draw(GTK_WIDGET(main_window));
		}
		#endif
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
BOSNode *load_images_handle_parameter_find_handler(const char *param, load_images_state_t state, file_t *file, GtkFileFilterInfo *file_filter_info) {/*{{{*/
	// Check if one of the file type handlers can handle this file
	file_type_handler_t *file_type_handler = &file_type_handlers[0];
	while(file_type_handler->file_types_handled) {
		if(gtk_file_filter_filter(file_type_handler->file_types_handled, file_filter_info) == TRUE) {
			file->file_type = file_type_handler;

			// Handle using this handler
			if(file_type_handler->alloc_fn != NULL) {
				return file_type_handler->alloc_fn(state, file);
			}
			else {
				return load_images_handle_parameter_add_file(state, file);
			}
		}

		file_type_handler++;
	}

	return NULL;
}/*}}}*/
void gfree_with_dummy_arg(void *pointer, void *dummy) {/*{{{*/
	g_free(pointer);
}/*}}}*/
gpointer load_images_handle_parameter_thread(char *param) {/*{{{*/
	// Thread version of load_images_handle_parameter
	// Free()s param after run
	load_images_handle_parameter(param, PARAMETER, 0, NULL);
	g_free(param);
	if(main_window) {
		gtk_widget_queue_draw(GTK_WIDGET(main_window));
	}
	return NULL;
}/*}}}*/
int pqiv_utility_strcmp0_data(const void *data1, const void *data2, void *user_data) {/*{{{*/
	return g_strcmp0(data1, data2);
}/*}}}*/
void load_images_handle_parameter(char *param, load_images_state_t state, gint depth, GSList *recursion_folder_stack) {/*{{{*/
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
		g_mutex_init(&file->lock);

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

		BOSNode *mime_guess_result = load_images_handle_parameter_find_handler(param, state, file, &mime_guesser);
		if(mime_guess_result == FALSE_POINTER) {
			return;
		}
		if(!mime_guess_result) {
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
			load_images_handle_parameter(param, BROWSE_ORIGINAL_PARAMETER, 0, recursion_folder_stack);

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
				if(original_parameter != NULL) {
					g_free(param);
				}
				return;
			}

			// Check for loops
			// Note: PATH_MAX might be too small. However, GIO fails to work
			// with long file names as well. In fact, it even fails to work
			// with relative file names in directorys that happen to have a
			// long absolute name. See
			// https://bugzilla.gnome.org/show_bug.cgi?id=778798
			// TODO As soon as this is resolved upstream, it'll make sense to
			// exchange the realpath() with something else as well.
			char abs_path[PATH_MAX];
			if(
				#ifdef _WIN32
					GetFullPathNameA(param, PATH_MAX, abs_path, NULL) != 0
				#else
					realpath(param, abs_path) != NULL
				#endif
			) {
				if(g_slist_find_custom(recursion_folder_stack, abs_path, (GCompareFunc)g_strcmp0)) {
					if(original_parameter != NULL) {
						g_free(param);
					}
					return;
				}
				recursion_folder_stack = g_slist_prepend(recursion_folder_stack, g_strdup(abs_path));
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
			if(load_images_timer && g_timer_elapsed(load_images_timer, NULL) > 5.) {
				#ifdef _WIN32
					g_print("Loading in %-50.50s ...\r", param);
				#else
					g_print("\033[s\033[?7lLoading in %s ...\033[J\033[u\033[?7h", param);
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
				if(strcmp(dir_entry, ".sh_thumbnails") == 0) {
					// Do not traverse into local thumbnail directories
					continue;;
				}

				gchar *dir_entry_full = g_strdup_printf("%s%s%s", param, g_str_has_suffix(param, G_DIR_SEPARATOR_S) ? "" : G_DIR_SEPARATOR_S, dir_entry);
				if(!(original_parameter != NULL && g_strcmp0(dir_entry_full, original_parameter) == 0)) {
					// Skip if we are in --browse mode and this is the file which we have already added above.
					load_images_handle_parameter(dir_entry_full, RECURSION, depth + 1, recursion_folder_stack);
				}
				g_free(dir_entry_full);

				// If the file tree has been invalidated, cancel.
				// Do not bother to free stuff, because pqiv will exit anyway.
				if(!file_tree_valid) {
					return;
				}
			}
			g_dir_close(dir_ptr);

			// Add a watch for new files in this directory
			if(option_watch_directories && !g_hash_table_lookup(active_directory_watches, abs_path)) {
				// Note: It does not suffice to do this once for each parameter, but this must also be
				// called for subdirectories. At least if it is not, new files in subdirectories are
				// not always recognized.
				GFile *file_ptr = g_file_new_for_path(param);
				GFileMonitor *directory_monitor = g_file_monitor_directory(file_ptr, G_FILE_MONITOR_NONE, NULL, NULL);
				if(directory_monitor != NULL) {
					directory_watch_options_t *options = g_new0(directory_watch_options_t, 1);
					options->outstanding_files = g_tree_new_full(pqiv_utility_strcmp0_data, NULL, g_free, NULL);
					options->depth = depth;
					options->base_param = g_strdup(param);
					// Remove trailing '/', just for optics of filenames.
					for(char *iter = options->base_param; *iter; iter++) {
						if(!iter[1] && iter[0] == '/') {
							*iter = 0;
							break;
						}
					}
					options->recursion_folder_stack = recursion_folder_stack;
					g_signal_connect_data(directory_monitor, "changed", G_CALLBACK(load_images_directory_watch_callback), options, (GClosureNotify)gfree_with_dummy_arg, 0);
					g_hash_table_insert(active_directory_watches, g_strdup(abs_path), directory_monitor);
				}
				g_object_unref(file_ptr);
			}
			else {
				// If we do not use directory watches then there is no use in
				// maintaining the directory recursion stack. Remove the first
				// entry (current directory) again.
				g_free(recursion_folder_stack->data);
				recursion_folder_stack = g_slist_delete_link(recursion_folder_stack, recursion_folder_stack);
				// Since the net effect is that we didn't modify recursion_folder_stack,
				// it is fine that the result of recursion_folder_stack is lost (and not passed back
				// outside of this function)
				(void)recursion_folder_stack;
			}

			if(original_parameter != NULL) {
				g_free(param);
			}
			return;
		}

		// Prepare file structure
		file = g_slice_new0(file_t);
		file->file_name = g_strdup(param);
		file->display_name = g_filename_display_name(param);
		g_mutex_init(&file->lock);
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

		// Filter based on formats supported by the different handlers
		gchar *param_lowerc = g_utf8_strdown(param, -1);
		load_images_file_filter_info->filename = load_images_file_filter_info->display_name = param_lowerc;

		// Check if one of the file type handlers can handle this file
		BOSNode *new_node = load_images_handle_parameter_find_handler(param, state, file, load_images_file_filter_info);
		if(new_node && new_node != FALSE_POINTER) {
			if(!current_file_node && main_window_visible) {
				current_file_node = bostree_node_weak_ref(new_node);
				g_idle_add((GSourceFunc)absolute_image_movement, bostree_node_weak_ref(new_node));
			}
			g_free(param_lowerc);
			return;
		}
		g_free(param_lowerc);
		if(new_node == FALSE_POINTER) {
			return;
		}

		if(state != PARAMETER && state != BROWSE_ORIGINAL_PARAMETER) {
			// At this point, if the file was not mentioned explicitly by the user,
			// abort.
			file_free(file);
			return;
		}

		// Make a final attempt to guess the file type by mime type
		GFile *param_file = gfile_for_commandline_arg(param);
		if(!param_file) {
			file_free(file);
			return;
		}

		GFileInfo *file_info = g_file_query_info(param_file, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE, G_FILE_QUERY_INFO_NONE, NULL, NULL);
		if(file_info) {
			gchar *param_file_mime_type = g_content_type_get_mime_type(g_file_info_get_content_type(file_info));
			if(param_file_mime_type) {
				GtkFileFilterInfo mime_guesser;
				mime_guesser.contains = GTK_FILE_FILTER_MIME_TYPE;
				mime_guesser.mime_type = param_file_mime_type;

				new_node = load_images_handle_parameter_find_handler(param, state, file, &mime_guesser);
				if(new_node && new_node != FALSE_POINTER) {
					if(!current_file_node && main_window_visible) {
						current_file_node = bostree_node_weak_ref(new_node);
						g_idle_add((GSourceFunc)absolute_image_movement, bostree_node_weak_ref(new_node));
					}
					g_free(param_file_mime_type);
					g_object_unref(param_file);
					g_object_unref(file_info);
					return;
				}
				else {
					g_printerr("Didn't recognize file `%s': Both its extension and MIME-type `%s' are unknown. Fall-back to default file handler.\n", param, param_file_mime_type);
					g_free(param_file_mime_type);
					g_object_unref(param_file);
					if(new_node == FALSE_POINTER) {
						return;
					}
				}
			}
			g_object_unref(file_info);
		}

		// If nothing else worked, assume that this file is handled by the default handler

		// Prepare file structure
		file->file_type = &file_type_handlers[0];
		if(!file->file_type) {
			g_printerr("No default file handler available.\n");
			file_free(file);
			return;
		}
		new_node = file_type_handlers[0].alloc_fn(state, file);
		if(!current_file_node && main_window_visible) {
			current_file_node = bostree_node_weak_ref(new_node);
			g_idle_add((GSourceFunc)absolute_image_movement, bostree_node_weak_ref(new_node));
		}
	}
}/*}}}*/
int image_tree_float_compare(const float *a, const float *b) {/*{{{*/
	return *a > *b;
}/*}}}*/
void file_free(file_t *file) {/*{{{*/
	if(file->file_type && file->file_type->free_fn != NULL && file->private) {
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
#ifndef CONFIGURED_WITHOUT_MONTAGE_MODE
	if(file->thumbnail) {
		cairo_surface_destroy(file->thumbnail);
		file->thumbnail = NULL;
	}
#endif
	g_mutex_clear(&file->lock);
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
void load_images() {/*{{{*/
	int * const argc = &global_argc;
	char ** const argv = global_argv;

	// Allocate memory for the file list (Used for unsorted and random order file lists)
	file_tree = bostree_new(
		option_sort ? (BOSTree_cmp_function)strnatcasecmp : (BOSTree_cmp_function)image_tree_float_compare,
		file_tree_free_helper
	);
	file_tree_valid = TRUE;

	// Allocate memory for the timer
	if(!option_actions_from_stdin) {
		load_images_timer = g_timer_new();
		g_timer_start(load_images_timer);
	}

	// Prepare the file filter info structure used for handler detection
	load_images_file_filter_info = g_new0(GtkFileFilterInfo, 1);
	load_images_file_filter_info->contains = GTK_FILE_FILTER_FILENAME | GTK_FILE_FILTER_DISPLAY_NAME;

	// Initialize structure to hold directory watches
	if(option_watch_directories && active_directory_watches == NULL) {
		active_directory_watches = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_object_unref);
	}

	// Load the images from the remaining parameters
	for(int i=1; i<*argc; i++) {
		if(argv[i][0]) {
			load_images_handle_parameter(argv[i], PARAMETER, 0, NULL);
		}
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
			load_images_handle_parameter(buffer, PARAMETER, 0, NULL);
			g_free(buffer);
		}
		g_io_channel_unref(stdin_reader);
	}

	if(load_images_timer) {
		g_timer_destroy(load_images_timer);
	}
}/*}}}*/
// }}}
/* (A-)synchronous image loading and image operations {{{ */
#ifndef CONFIGURED_WITHOUT_MONTAGE_MODE
gboolean test_and_invalidate_thumbnail(file_t *file) {/*{{{*/
	// Must be called with an active lock!
	if(file->thumbnail) {
		const int thumb_width = cairo_image_surface_get_width(file->thumbnail);
		const int thumb_height = cairo_image_surface_get_height(file->thumbnail);
		if(!((thumb_width == option_thumbnails.width && thumb_height <= option_thumbnails.height) ||
			  (thumb_width <= option_thumbnails.width && thumb_height == option_thumbnails.height) ||
			  (thumb_width == (int)file->width && thumb_height == (int)file->height))) {
			cairo_surface_destroy(file->thumbnail);
			file->thumbnail = NULL;
		}
	}
	return !!file->thumbnail;
}/*}}}*/
#endif
void invalidate_current_scaled_image_surface() {/*{{{*/
	if(current_scaled_image_surface != NULL) {
		cairo_surface_destroy(current_scaled_image_surface);
		current_scaled_image_surface = NULL;
	}
}/*}}}*/
gboolean image_animation_timeout_callback(gpointer user_data) {/*{{{*/
	D_LOCK(file_tree);
	if(!file_tree_valid || (BOSNode *)user_data != current_file_node || FILE(current_file_node)->force_reload || !FILE(current_file_node)->is_loaded) {
		D_UNLOCK(file_tree);
		current_image_animation_timeout_id = 0;
		return FALSE;
	}
	if(CURRENT_FILE->file_type->animation_next_frame_fn == NULL) {
		D_UNLOCK(file_tree);
		current_image_animation_timeout_id = 0;
		return FALSE;
	}

	g_mutex_lock(&CURRENT_FILE->lock);
	double delay = (1./current_image_animation_speed_scale) * CURRENT_FILE->file_type->animation_next_frame_fn(CURRENT_FILE);
	g_mutex_unlock(&CURRENT_FILE->lock);
	D_UNLOCK(file_tree);

	if(delay >= 0 && current_image_animation_speed_scale > 0) {
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
		queue_image_load(bostree_node_weak_ref(node));
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
		//
		// Single exception: With the --allow-empty-window option, it does make
		// sense to unload even the last image.
		if(option_watch_files == ON) {
			FILE(node)->force_reload = TRUE;
			if(bostree_node_count(file_tree) > 1 || option_allow_empty_window) {
				queue_image_load(bostree_node_weak_ref(node));
			}
		}
	}
	D_UNLOCK(file_tree);
}/*}}}*/
gboolean window_move_helper_callback(gpointer user_data) {/*{{{*/
	gtk_window_move(main_window, option_window_position.x / screen_scale_factor, option_window_position.y / screen_scale_factor);
	option_window_position.x = -1;
	return FALSE;
}/*}}}*/
gboolean main_window_resize_callback(gpointer user_data) {/*{{{*/
	// Only used once at application startup, this is not a callback invoked
	// each time the window size changes!

	D_LOCK(file_tree);
	// If there is no image loaded, abort
	if(!is_current_file_loaded()) {
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
		requested_main_window_width = new_window_width;
		requested_main_window_height = new_window_height;
		gtk_window_resize(main_window, new_window_width / screen_scale_factor, new_window_height / screen_scale_factor);
	}

	return FALSE;
}/*}}}*/
gboolean main_window_calculate_ideal_size(int *new_window_width, int *new_window_height) {/*{{{*/
	if(!current_file_node) {
		return FALSE;
	}

	// We only need to adjust the window if it is not in fullscreen
	if(main_window_in_fullscreen) {
		return FALSE;
	}

	if(application_mode == DEFAULT) {
		int image_width, image_height;
		calculate_current_image_transformed_size(&image_width, &image_height);

		*new_window_width = current_scale_level * image_width + 0.5;
		*new_window_height = current_scale_level * image_height + 0.5;
	}
	#ifndef CONFIGURED_WITHOUT_MONTAGE_MODE
	else if(application_mode == MONTAGE) {
		const int screen_width = screen_geometry.width;
		const int screen_height = screen_geometry.height;

		*new_window_width = screen_width * option_scale_screen_fraction;
		*new_window_height = screen_height * option_scale_screen_fraction;
	}
	#endif
	else {
		*new_window_width = *new_window_height = 0;
	}

	if(*new_window_height <= 0) {
		*new_window_height = 1;
	}
	if(*new_window_width <= 0) {
		*new_window_width = 1;
	}

	return TRUE;
}/*}}}*/
gboolean main_window_reset_pos_callback(gpointer user_data) {/*{{{*/
	gtk_window_set_position(main_window, GTK_WIN_POS_NONE);
	requested_main_window_resize_pos_callback_id = -1;
	return FALSE;
}/*}}}*/
void main_window_adjust_for_image() {/*{{{*/
	if(!current_file_node) {
		return;
	}

	// We only need to adjust the window if it is not in fullscreen
	if(main_window_in_fullscreen) {
		queue_draw();
		return;
	}

	// In SCALE_TO_FIT_WINDOW mode, do never resize
	if(option_scale == SCALE_TO_FIT_WINDOW) {
		queue_draw();
		return;
	}

	int new_window_width, new_window_height;
	if(!main_window_calculate_ideal_size(&new_window_width, &new_window_height)) {
		return;
	}

	GdkGeometry hints;
	if(option_enforce_window_aspect_ratio) {
#if GTK_MAJOR_VERSION >= 3
		hints.min_aspect = hints.max_aspect = new_window_width * 1.0 / new_window_height;
#else
		// Fix for issue #57: Some WMs calculate aspect ratios slightly different
		// than GTK 2, resulting in an off-by-1px result for gtk_window_resize(),
		// which creates a loop shrinking the window until it eventually vanishes.
		// This is mitigated by temporarily removing the aspect constraint,
		// resizing, and reenabling it afterwards.
		hints.min_aspect = hints.max_aspect = 0;
#endif
	}

	if(main_window_width >= 0 && (main_window_width != new_window_width || main_window_height != new_window_height || requested_main_window_width != -1)) {
		if(option_recreate_window && main_window_visible) {
			recreate_window();
		}

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

			// We need to reset the centering eventually to allow users to
			// resize pqiv without having the window jumping around. But we
			// cannot do that right after resizing, because resizing is
			// actually not done *right* know, only when GTK returns to idle.
			// Resetting on idle with very low priority does not work either,
			// because resizing might be further delayed due to other pending
			// configure events. This used to be done upon receiving the
			// configure event. But that does not work at least with i3,
			// unfortunately. See #92 in Github. Long story short, a resonable
			// timeout seems to be the best idea right now.
			if(requested_main_window_resize_pos_callback_id > -1) {
				g_source_remove(requested_main_window_resize_pos_callback_id);
			}
			requested_main_window_resize_pos_callback_id = g_timeout_add(500, main_window_reset_pos_callback, NULL);
		}
		if(!main_window_visible) {
			gtk_window_set_default_size(main_window, new_window_width / screen_scale_factor, new_window_height / screen_scale_factor);
			if(option_enforce_window_aspect_ratio) {
				gtk_window_set_geometry_hints(main_window, NULL, &hints, GDK_HINT_ASPECT);
			}
			main_window_width = new_window_width;
			main_window_height = new_window_height;
			requested_main_window_width = new_window_width;
			requested_main_window_height = new_window_height;

			// Some window managers create a race here upon application startup:
			// They resize, as requested above, and afterwards apply their idea of
			// window size. To conquer that, we check for the window size again once
			// all events are handled.
			gdk_threads_add_idle(main_window_resize_callback, NULL);
		}
		else {
			// Required to avoid tearing
			window_prerender_background_pixmap(new_window_width, new_window_height, current_scale_level, main_window_in_fullscreen);

			requested_main_window_width = new_window_width;
			requested_main_window_height = new_window_height;
			if(option_enforce_window_aspect_ratio) {
				gtk_window_set_geometry_hints(main_window, NULL, &hints, GDK_HINT_ASPECT);
			}

			gtk_window_resize(main_window, new_window_width / screen_scale_factor, new_window_height / screen_scale_factor);
#if GTK_MAJOR_VERSION < 3
			if(option_enforce_window_aspect_ratio) {
				int image_width, image_height;
				calculate_current_image_transformed_size(&image_width, &image_height);
				hints.min_aspect = hints.max_aspect = image_width * 1.0 / image_height;
				gtk_window_set_geometry_hints(main_window, NULL, &hints, GDK_HINT_ASPECT);
			}
#endif
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
	// Execute logic below only if the loaded image is the current one
	if(node != NULL && node != current_file_node) {
#ifndef CONFIGURED_WITHOUT_MONTAGE_MODE
		if(application_mode == MONTAGE) {
			gtk_widget_queue_draw(GTK_WIDGET(main_window));
		}
#endif
		return FALSE;
	}

	// Note: This might mean as well that the *thumbnail* has been loaded,
	// but not the image itself. So check ->is_loaded in any case!

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

	// Activate fading
	if(option_fading) {
		if(fading_surface) {
			cairo_surface_destroy(fading_surface);
			fading_surface = NULL;
		}
		if(last_visible_surface) {
			fading_surface = cairo_surface_reference(last_visible_surface);
		}

		if(fading_current_alpha_stage > 0 && fading_current_alpha_stage < 1.) {
			// If another fade was already active, don't start another one.
			fading_current_alpha_stage = DBL_EPSILON;
			fading_initial_time = -1;
		}
		else {
			// It is important to initialize this variable with a positive,
			// non-null value, as 0. is used to indicate that no fading currently
			// takes place.
			fading_current_alpha_stage = DBL_EPSILON;
			// We start the clock after the first draw, because it could take some
			// time to calculate the resized version of the image
			fading_initial_time = -1;
			gdk_threads_add_idle(fading_timeout_callback, NULL);
		}
	}

	// Initialize animation timer if the image is animated
	if((CURRENT_FILE->file_flags & FILE_FLAGS_ANIMATION) != 0 && CURRENT_FILE->file_type->animation_initialize_fn != NULL) {
		g_mutex_lock(&CURRENT_FILE->lock);
		current_image_animation_timeout_id = gdk_threads_add_timeout(
			CURRENT_FILE->file_type->animation_initialize_fn(CURRENT_FILE),
			image_animation_timeout_callback,
			(gpointer)current_file_node);
		g_mutex_unlock(&CURRENT_FILE->lock);
		current_image_animation_speed_scale = 1.0;
	}

	// Update geometry hints, calculate initial window size and place window
	D_UNLOCK(file_tree);

	// Reset shift
	current_shift_x = 0;
	current_shift_y = 0;

	// Reset rotation
	cairo_matrix_init_identity(&current_transformation);

	// Adjust scale level, resize, set aspect ratio and place window,
	// but only if not currently in the process of changing state
	if(fullscreen_transition_source_id < 0) {
		if(!current_image_drawn) {
			scale_override = FALSE;
		}
		invalidate_current_scaled_image_surface();
		set_scale_level_for_screen();
		main_window_adjust_for_image();
	}
	current_image_drawn = FALSE;
	queue_draw();

	// Show window, if not visible yet
	if(!main_window_visible) {
		main_window_visible = TRUE;
		gtk_widget_show_all(GTK_WIDGET(main_window));
	}

	// Reset the info text
	update_info_text(NULL);

	// Output status for scripts
	status_output();

	return FALSE;
}/*}}}*/
GInputStream *image_loader_stream_file(file_t *file, GError **error_pointer) {/*{{{*/
	GInputStream *data;

	if((file->file_flags & FILE_FLAGS_MEMORY_IMAGE) != 0) {
		// Memory view on a memory image
		GBytes *bytes_source;
		if(file->file_data_loader) {
			bytes_source = file->file_data_loader(file, error_pointer);

			if(!bytes_source) {
				return NULL;
			}
		}
		else {
			bytes_source = g_bytes_ref(file->file_data);
		}

		#if GLIB_CHECK_VERSION(2, 34, 0)
			data = g_memory_input_stream_new_from_bytes(bytes_source);
		#else
			gsize size = 0;
			gpointer *mem_data = g_memdup(g_bytes_get_data(bytes_source, &size), size);
			data = g_memory_input_stream_new_from_data(mem_data, size, g_free);
		#endif

		g_bytes_unref(bytes_source);
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
file_t *image_loader_duplicate_file(file_t *file, gchar *custom_file_name, gchar *custom_display_name, gchar *custom_sort_name) {/*{{{*/
	file_t *new_file = g_slice_new(file_t);
	*new_file = *file;

	if(file->file_data) {
		g_bytes_ref(new_file->file_data);
	}
	new_file->file_name = custom_file_name ? custom_file_name : g_strdup(file->file_name);
	new_file->display_name = custom_display_name ? custom_display_name : g_strdup(file->display_name);
	new_file->sort_name = custom_sort_name ? custom_sort_name : (file->sort_name ? g_strdup(file->sort_name) : NULL);

	new_file->private = NULL;
	new_file->file_monitor = NULL;
	new_file->is_loaded = FALSE;

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
			g_mutex_lock(&file->lock);
			file->file_type->load_fn(file, data, &error_pointer);
			g_mutex_unlock(&file->lock);
			g_object_unref(data);
		}
	}

	if(file->is_loaded) {
		if(error_pointer) {
			g_printerr("A recoverable error occurred: %s\n", error_pointer->message);
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
				if(bostree_node_count(file_tree) > 1) {
					// This can be triggered in shuffle mode if images are deleted and the end of
					// a shuffle cycle is reached, such that next_file() starts a new one. Fall
					// back to display the first image. See bug #35 in github.
					current_file_node = bostree_node_weak_ref(bostree_select(file_tree, 0));
					queue_image_load(bostree_node_weak_ref(current_file_node));
				}
				else {
					current_file_node = NULL;
				}
			}
			else {
				current_file_node = bostree_node_weak_ref(current_file_node);
				queue_image_load(bostree_node_weak_ref(current_file_node));
			}
			bostree_remove(file_tree, node);
			bostree_node_weak_unref(file_tree, node);
		}
		else {
			bostree_remove(file_tree, node);
		}
		if(!called_from_main && bostree_node_count(file_tree) == 0) {
			if(option_allow_empty_window) {
				D_UNLOCK(file_tree);
				current_file_node = NULL;
				earlier_file_node = NULL;
				invalidate_current_scaled_image_surface();
				if(last_visible_surface) {
					cairo_surface_destroy(last_visible_surface);
					last_visible_surface = NULL;
				}
				current_image_drawn = FALSE;
				update_info_text(NULL);
				queue_draw();
				return FALSE;
			}
			else {
				g_printerr("No images left to display.\n");
				if(gtk_main_level() == 0) {
					exit(1);
				}
				gtk_main_quit();
			}
		}
		D_UNLOCK(file_tree);
	}

	return FALSE;
}/*}}}*/
#ifndef CONFIGURED_WITHOUT_MONTAGE_MODE
void image_loader_create_thumbnail(file_t *file) {/*{{{*/
	const double scale_level_w = option_thumbnails.width * 1.0 / file->width;
	const double scale_level_h = option_thumbnails.height * 1.0 / file->height;
	double scale_level = scale_level_w > scale_level_h ? scale_level_h : scale_level_w;
	if(scale_level > 1.) {
		scale_level = 1.;
	}

	cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, scale_level * file->width + .5, scale_level * file->height + .5);
	if(cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(surf);
		return;
	}

	cairo_t *cr = cairo_create(surf);

	// Draw black background
	cairo_save(cr);
	cairo_set_source_rgba(cr, 0., 0., 0., option_transparent_background ? 0. : 1.);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_paint(cr);
	cairo_restore(cr);

	// From here on, draw centered
	cairo_translate(cr, (cairo_image_surface_get_width(surf) - scale_level * file->width) / 2, (cairo_image_surface_get_height(surf) - scale_level * file->height) / 2);
	cairo_scale(cr, scale_level, scale_level);

	// Draw background pattern
	if(background_checkerboard_pattern != NULL && !option_transparent_background) {
		cairo_save(cr);
		cairo_new_path(cr);
		unsigned skip_px = (unsigned)(1./scale_level);
		if(skip_px == 0) {
			skip_px = 1;
		}
		cairo_rectangle(cr, skip_px, skip_px, file->width - 2*skip_px, file->height - 2*skip_px);
		cairo_close_path(cr);
		cairo_clip(cr);
		cairo_set_source(cr, background_checkerboard_pattern);
		cairo_paint(cr);
		cairo_restore(cr);
	}

	cairo_rectangle(cr, 0, 0, file->width, file->height);
	cairo_clip(cr);
	if(file->file_type->draw_fn != NULL) {
		g_mutex_lock(&file->lock);
		file->file_type->draw_fn(file, cr);
		g_mutex_unlock(&file->lock);
	}

	cairo_destroy(cr);
	file->thumbnail = surf;
}/*}}}*/
#endif
void image_generate_prerendered_view(file_t *file, gboolean force, double scale_level) {/*{{{*/
	if(option_lowmem) {
		return;
	}
	if(file->file_flags && FILE_FLAGS_ANIMATION) {
		return;
	}
	if(force && file->prerendered_view) {
		cairo_surface_destroy(file->prerendered_view);
		file->prerendered_view = NULL;
	}
	if(scale_level < 0) {
		scale_level = calculate_auto_scale_level_for_screen(file->width, file->height);
	}
	int width = scale_level * file->width + .5;
	int height = scale_level * file->height + .5;

	if(file->prerendered_view) {
		int old_width = cairo_image_surface_get_width(file->prerendered_view);
		int old_height = cairo_image_surface_get_height(file->prerendered_view);

		if(old_width != width || old_height != height) {
			cairo_surface_destroy(file->prerendered_view);
			file->prerendered_view = NULL;
		}
	}

	if(!file->prerendered_view) {
		cairo_surface_t *prerendered_view = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
		if(cairo_surface_status(prerendered_view) == CAIRO_STATUS_SUCCESS) {
			cairo_t *cr = cairo_create(prerendered_view);
			cairo_scale(cr, scale_level, scale_level);
			cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
			if(file->file_type->draw_fn != NULL) {
				g_mutex_lock(&file->lock);
				file->file_type->draw_fn(file, cr);
				g_mutex_unlock(&file->lock);
			}
			cairo_destroy(cr);
			file->prerendered_view = cairo_surface_reference(prerendered_view);
		}
		cairo_surface_destroy(prerendered_view);
	}
}/*}}}*/
gpointer image_loader_thread(gpointer user_data) {/*{{{*/
	while(TRUE) {
		// Handle new queued image load
		struct image_loader_queue_item *it = g_async_queue_pop(image_loader_queue);
		BOSNode *node = it->node_ref;
		#ifndef CONFIGURED_WITHOUT_MONTAGE_MODE
		image_loader_purpose_t purpose = it->purpose;
		#endif
		g_slice_free(struct image_loader_queue_item, it);

		// The image might still be in the loader queue though it has already
		// been invalidated. In this case, skip it.
		if(!bostree_node_weak_unref(file_tree, bostree_node_weak_ref(node))) {
			D_LOCK(file_tree);
			bostree_node_weak_unref(file_tree, node);
			D_UNLOCK(file_tree);
			continue;
		}

		// Short-circuit: If we want to load this image for its thumbnail, check the cache first.
		// We might not have to load it at all.
		#ifndef CONFIGURED_WITHOUT_MONTAGE_MODE
		if(purpose == MONTAGE) {
			// Unload an old thumbnail if it does not have the correct size
			D_LOCK(file_tree);
			test_and_invalidate_thumbnail(FILE(node));
			if(!FILE(node)->thumbnail && (option_thumbnails.enabled || application_mode == MONTAGE) && option_thumbnails.persist != THUMBNAILS_PERSIST_OFF) {
				if(load_thumbnail_from_cache(FILE(node), option_thumbnails.width, option_thumbnails.height, option_thumbnails.persist, option_thumbnails.special_thumbnail_directory) == TRUE) {
					// Loading the thumbnail succeeded. We may break here.
					bostree_node_weak_unref(file_tree, node);
					D_UNLOCK(file_tree);

					// Notify the main thread about this.
					gdk_threads_add_idle((GSourceFunc)image_loaded_handler, node);
					continue;
				}
			}
			D_UNLOCK(file_tree);
		}
		#endif

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
						queue_image_load(bostree_node_weak_ref(node));
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
		if(FILE(node)->is_loaded) {
#ifndef CONFIGURED_WITHOUT_MONTAGE_MODE
			D_LOCK(file_tree);
			test_and_invalidate_thumbnail(FILE(node));
			if(!FILE(node)->thumbnail && (option_thumbnails.enabled || application_mode == MONTAGE)) {
				if(option_thumbnails.persist == THUMBNAILS_PERSIST_OFF || load_thumbnail_from_cache(FILE(node), option_thumbnails.width, option_thumbnails.height, option_thumbnails.persist, option_thumbnails.special_thumbnail_directory) == FALSE) {
					D_UNLOCK(file_tree);
					image_loader_create_thumbnail(FILE(node));
					D_LOCK(file_tree);
					if(FILE(node)->thumbnail && option_thumbnails.persist != THUMBNAILS_PERSIST_OFF && option_thumbnails.persist != THUMBNAILS_PERSIST_RO) {
						store_thumbnail_to_cache(FILE(node), option_thumbnails.width, option_thumbnails.height, option_thumbnails.persist, option_thumbnails.special_thumbnail_directory);
					}
				}
			}
			D_UNLOCK(file_tree);
#endif
			if(node == current_file_node) {
				current_image_drawn = FALSE;
			}

			// Prerender the default scaled view of the image for faster image transitions
			image_generate_prerendered_view(FILE(node), FALSE, -1);

			gdk_threads_add_idle((GSourceFunc)image_loaded_handler, node);
		}

		D_LOCK(file_tree);
		bostree_node_weak_unref(file_tree, node);
		D_UNLOCK(file_tree);
	}
}/*}}}*/
void image_loader_queue_destroy(gpointer data) {/*{{{*/
	bostree_node_weak_unref(file_tree, ((struct image_loader_queue_item *)data)->node_ref);
	g_slice_free(struct image_loader_queue_item, data);
}/*}}}*/
gboolean initialize_image_loader() {/*{{{*/
	if(image_loader_initialization_succeeded) {
		return TRUE;
	}
	if(image_loader_queue == NULL) {
		image_loader_queue = g_async_queue_new_full(image_loader_queue_destroy);
		image_loader_cancellable = g_cancellable_new();
	}
	D_LOCK(file_tree);
	if(current_file_node != NULL) {
		// If this has previously been ref'ed for any reason, unref here.
		bostree_node_weak_unref(file_tree, current_file_node);
	}
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
	while(!image_loader_load_single(current_file_node, TRUE) && bostree_node_count(file_tree) > 0) usleep(10000);
	if(bostree_node_count(file_tree) == 0) {
		return FALSE;
	}
	g_thread_new("image-loader", image_loader_thread, NULL);

	preload_adjacent_images();

	image_loader_initialization_succeeded = TRUE;
	return TRUE;
}/*}}}*/
void abort_pending_image_loads(BOSNode *new_pos) {/*{{{*/
	struct image_loader_queue_item *ref;
	if(image_loader_queue == NULL) {
		return;
	}

	while((ref = g_async_queue_try_pop(image_loader_queue)) != NULL) {
		bostree_node_weak_unref(file_tree, ref->node_ref);
		g_slice_free(struct image_loader_queue_item, ref);
	}
	if(image_loader_thread_currently_loading != NULL && image_loader_thread_currently_loading != new_pos) {
		g_cancellable_cancel(image_loader_cancellable);
	}
}/*}}}*/
void queue_image_load(BOSNode *node) {/*{{{*/
	struct image_loader_queue_item *it = g_slice_new(struct image_loader_queue_item);
	it->node_ref = node; // Must be weak_ref'ed by caller. (Simplifies thread safety.)
	it->purpose = DEFAULT;
	g_async_queue_push(image_loader_queue, it);
}/*}}}*/
#ifndef CONFIGURED_WITHOUT_MONTAGE_MODE
void queue_thumbnail_load(BOSNode *node) {/*{{{*/
	struct image_loader_queue_item *it = g_slice_new(struct image_loader_queue_item);
	it->node_ref = node; // Must be weak_ref'ed by caller.
	it->purpose = MONTAGE;
	g_async_queue_push(image_loader_queue, it);
}/*}}}*/
#endif
void unload_image(BOSNode *node) {/*{{{*/
	if(!node) {
		return;
	}
	file_t *file = FILE(node);
	if(file->file_type->unload_fn != NULL) {
		g_mutex_lock(&file->lock);
		file->file_type->unload_fn(file);
		g_mutex_unlock(&file->lock);
	}
	if(file->prerendered_view) {
		cairo_surface_destroy(file->prerendered_view);
		file->prerendered_view = NULL;
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
void remove_image(BOSNode *node) {/*{{{*/
	D_LOCK(file_tree);

	node = bostree_node_weak_unref(file_tree, node);
	if(!node) {
		D_UNLOCK(file_tree);
		return;
	}

	if(node == current_file_node) {
		// Cheat the image loader into thinking that the file is no longer
		// available, and force a reload. This is an easy way to use the
		// mechanism from the loader thread to handle this situation.
		CURRENT_FILE->force_reload = TRUE;
		if(CURRENT_FILE->file_name) {
			CURRENT_FILE->file_name[0] = 0;
		}
		if(CURRENT_FILE->file_data) {
			g_bytes_unref(CURRENT_FILE->file_data);
			CURRENT_FILE->file_data = NULL;
		}
		queue_image_load(bostree_node_weak_ref(current_file_node));
	}
	else {
		bostree_remove(file_tree, node);
	}

	D_UNLOCK(file_tree);
}/*}}}*/
void preload_adjacent_images() {/*{{{*/
	if(!option_lowmem) {
		D_LOCK(file_tree);
		BOSNode *new_prev = previous_file();
		BOSNode *new_next = next_file();

		if(!FILE(new_next)->is_loaded) {
			queue_image_load(bostree_node_weak_ref(new_next));
		}
		if(!FILE(new_prev)->is_loaded) {
			queue_image_load(bostree_node_weak_ref(new_prev));
		}
		D_UNLOCK(file_tree);
	}

#ifndef CONFIGURED_WITHOUT_MONTAGE_MODE
	if(option_thumbnails.enabled && option_thumbnails.auto_generate_for_adjacents > 0) {
		D_LOCK(file_tree);
		size_t thumbnail_rank = bostree_rank(current_file_node);
		size_t count;
		if(thumbnail_rank > (unsigned)option_thumbnails.auto_generate_for_adjacents) {
			count = 2 * option_thumbnails.auto_generate_for_adjacents + 2;
			thumbnail_rank -= option_thumbnails.auto_generate_for_adjacents;
		}
		else {
			count = thumbnail_rank + option_thumbnails.auto_generate_for_adjacents + 1;
			thumbnail_rank = 0;
		}
		BOSNode *thumbnail_node = bostree_select(file_tree, thumbnail_rank);
		for(; thumbnail_node && count > 0; thumbnail_node = bostree_next_node(thumbnail_node), count--) {
			if(!test_and_invalidate_thumbnail(FILE(thumbnail_node))) {
				queue_thumbnail_load(bostree_node_weak_ref(thumbnail_node));
			}
		}
		D_UNLOCK(file_tree);
	}
#endif

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

#ifndef CONFIGURED_WITHOUT_ACTIONS
	// Set the new image as current
	if(earlier_file_node != NULL) {
		bostree_node_weak_unref(file_tree, earlier_file_node);
	}
	earlier_file_node = current_file_node;
	current_file_node = bostree_node_weak_ref(node);
	if(current_file_node != earlier_file_node) {
		invalidate_current_scaled_image_surface();
	}
#else
	if(current_file_node != NULL) {
		bostree_node_weak_unref(file_tree, current_file_node);
	}
	current_file_node = bostree_node_weak_ref(node);
#endif

#ifndef CONFIGURED_WITHOUT_INFO_TEXT
	// If the new image has not been loaded yet, prepare to display an information message
	// after some grace period
	if(!CURRENT_FILE->is_loaded && !option_hide_info_box) {
		gdk_threads_add_timeout(500, absolute_image_movement_still_unloaded_timer_callback, current_file_node);
	}
#endif

	// Load it
	queue_image_load(bostree_node_weak_ref(current_file_node));

	D_UNLOCK(file_tree);

	// Preload the adjacent images
	preload_adjacent_images();

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
BOSNode *image_pointer_by_name(gchar *display_name) {/*{{{*/
	// Obtain a pointer to the image that has a given display_name
	// Note that this is only fast (O(log n)) if the file tree is sorted,
	// elsewise a linear search is used!
	if(option_sort && option_sort_key == NAME) {
		return bostree_lookup(file_tree, display_name);
	}
	else {
		for(BOSNode *iter = bostree_select(file_tree, 0); iter; iter = bostree_next_node(iter)) {
			if(strcasecmp(FILE(iter)->display_name, display_name) == 0) {
				return iter;
			}
		}
		return NULL;
	}
}/*}}}*/
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
		if(movement > 0) {
			while(movement && g_list_next(current_shuffled_image)) {
				current_shuffled_image = g_list_next(current_shuffled_image);
				movement--;
			}
		}
		else if(movement < 0) {
			while(movement && g_list_previous(current_shuffled_image)) {
				current_shuffled_image = g_list_previous(current_shuffled_image);
				movement++;
			}
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

				if(movement > 0) {
					while(movement && g_list_next(current_shuffled_image)) {
						current_shuffled_image = g_list_next(current_shuffled_image);
						movement--;
					}
				}
				else if(movement < 0) {
					while(movement && g_list_previous(current_shuffled_image)) {
						current_shuffled_image = g_list_previous(current_shuffled_image);
						movement++;
					}
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
	if(!file_tree_valid || !bostree_node_count(file_tree)) {
		D_UNLOCK(file_tree);
		return;
	}
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

	#ifndef CONFIGURED_WITHOUT_MONTAGE_MODE
	if(application_mode == MONTAGE) {
		D_LOCK(file_tree);
		if(montage_window_control.selected_node != NULL) {
			bostree_node_weak_unref(file_tree, montage_window_control.selected_node);
		}
		montage_window_control.selected_node = target;
		montage_window_move_cursor(0, 0,  0);
		D_UNLOCK(file_tree);
		gtk_widget_queue_draw(GTK_WIDGET(main_window));
		return;
	}
	#endif

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
BOSNode *directory_image_movement_find_different_directory(BOSNode *current, int direction, gboolean logical_directories) {/*{{{*/
	// Return a reference to the first image with a different directory than current
	// when searching in direction direction (-1 or 1)
	//
	// This function does not perform any locking!
	BOSNode *target = current;

	if(!logical_directories && FILE(current)->file_flags & FILE_FLAGS_MEMORY_IMAGE) {
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
			if(target == current || (!logical_directories && FILE(target)->file_flags & FILE_FLAGS_MEMORY_IMAGE)) {
				break;
			}

			const char *target_name  = logical_directories ? FILE(target)->display_name  : FILE(target)->file_name;
			const char *current_name = logical_directories ? FILE(current)->display_name : FILE(current)->file_name;

			// Check if the directory changed. If it did, abort the search.
			// Search for the first byte where the file names differ
			unsigned int pos = 0;
			while(target_name[pos] && current_name[pos] && target_name[pos] == current_name[pos]) {
				pos++;
			}

			// The physical path changed if either
			//  * the target file name contains a slash at or after pos
			//    (e.g. current -> ./foo/bar.png, target -> ./foo2/baz.png)
			//  * the current file name contains a slash at or after pos
			//    (e.g. current -> ./foo/bar.png, target -> ./baz.png
			// The logical path changed if the same holds, only that the
			// special character '#' (used to separate entries from the archive
			// name in archives) counts as a directory separator, too.
			gboolean directory_changed = FALSE;
			for(unsigned int i=pos; target_name[i]; i++) {
				if(target_name[i] == G_DIR_SEPARATOR || (logical_directories && target_name[i] == '#')) {
					// Gotcha.
					directory_changed = TRUE;
					break;
				}
			}
			if(!directory_changed) {
				for(unsigned int i=pos; current_name[i]; i++) {
					if(current_name[i] == G_DIR_SEPARATOR || (logical_directories && current_name[i] == '#')) {
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
BOSNode *relative_image_pointer_directory(int direction, gboolean logical_directories) {/*{{{*/
	// Directory movement
	//
	// This should be consistent, i.e. movements in different directions should
	// be inverse operations of each other. This makes this function slightly
	// complex.
	//
	BOSNode *target;
	BOSNode *current = current_file_node;

	if(direction == 1) {
		// Forward searches are trivial
		target = directory_image_movement_find_different_directory(current, 1, logical_directories);
	}
	else {
		// Bardward searches are more involved, because we want to end up at the first image
		// of the previous directory, not at the last one. The trick is to
		// search backwards twice and then again go forward by one image.
		target = directory_image_movement_find_different_directory(current, -1, logical_directories);
		target = directory_image_movement_find_different_directory(target, -1, logical_directories);

		if(target != current) {
			target = bostree_next_node(target);
			if(!target) {
				target = bostree_select(file_tree, 0);
			}
		}
	}

	return target;
}/*}}}*/
void directory_image_movement(int direction, gboolean logical_directories) {/*{{{*/
	// Directory movement
	//
	// This should be consistent, i.e. movements in different directions should
	// be inverse operations of each other. This makes this function slightly
	// complex.

	D_LOCK(file_tree);
	BOSNode *target = bostree_node_weak_ref(relative_image_pointer_directory(direction, logical_directories));
	D_UNLOCK(file_tree);

	if(application_mode == DEFAULT) {
		absolute_image_movement(target);
	}
	#ifndef CONFIGURED_WITHOUT_MONTAGE_MODE
	else if(application_mode == MONTAGE) {
		D_LOCK(file_tree);
		if(montage_window_control.selected_node != NULL) {
			bostree_node_weak_unref(file_tree, montage_window_control.selected_node);
		}
		montage_window_control.selected_node = target;
		montage_window_move_cursor(0, 0,  0);
		D_UNLOCK(file_tree);
	}
	#endif
}/*}}}*/
void transform_current_image(cairo_matrix_t *transformation) {/*{{{*/
	// Apply the transformation to the transformation matrix
	cairo_matrix_t operand = current_transformation;
	cairo_matrix_multiply(&current_transformation, &operand, transformation);

	// Resize and queue a redraw
	double old_scale_level = current_scale_level;
	set_scale_level_for_screen();
	if(fabs(old_scale_level - current_scale_level) > DBL_EPSILON) {
		invalidate_current_scaled_image_surface();
		image_generate_prerendered_view(CURRENT_FILE, FALSE, current_scale_level);
	}
	main_window_adjust_for_image();

	gtk_widget_queue_draw(GTK_WIDGET(main_window));
}/*}}}*/
#ifndef CONFIGURED_WITHOUT_EXTERNAL_COMMANDS /* option --without-external-commands: Do not include support for calling external programs */
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
			g_thread_new("image-filter-writer", apply_external_image_filter_image_writer_thread, &child_stdin);

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
					g_mutex_init(&new_image->lock);

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
	g_free(external_filter_ptr);
	return NULL;
}/*}}}*/
#endif
void hardlink_current_image() {/*{{{*/
	BOSNode *the_file = bostree_node_weak_ref(current_file_node);

	if((FILE(the_file)->file_flags & FILE_FLAGS_MEMORY_IMAGE) != 0) {
		g_mkdir("./.pqiv-select", 0755);
		gchar *store_target = NULL;
		do {
			if(store_target != NULL) {
				g_free(store_target);
			}
			#if(GLIB_CHECK_VERSION(2, 28, 0) && !defined(_WIN32))
				// Note: Win32 GLib uses I64 for G_GINT64_FORMAT, which isn't
				// supported by the standard.
				store_target = g_strdup_printf("./.pqiv-select/memory-%" G_GINT64_FORMAT "-%u.png", g_get_real_time(), g_random_int());
			#else
				store_target = g_strdup_printf("./.pqiv-select/memory-%u.png", g_random_int());
			#endif
		}
		while(g_file_test(store_target, G_FILE_TEST_EXISTS));

		cairo_surface_t *surface = get_scaled_image_surface_for_current_image();
		if(surface) {
			if(cairo_surface_write_to_png(surface, store_target) == CAIRO_STATUS_SUCCESS) {
				UPDATE_INFO_TEXT("Stored what you see into %s", store_target);
			}
			else {
				update_info_text("Failed to write to the .pqiv-select subdirectory");
			}
			info_text_queue_redraw();
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
		info_text_queue_redraw();
		bostree_node_weak_unref(file_tree, the_file);
		return;
	}

	// Intentionally ignoring mkdir return value -- the error case is handled below, and this
	// saves one extra access(2) call.
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
				UPDATE_INFO_TEXT("Failed to link file, but stored what you see into %s", store_target);
			}
			else {
				update_info_text("Failed to write to the .pqiv-select subdirectory");
			}
			cairo_surface_destroy(surface);
			info_text_queue_redraw();
		}
		g_free(store_target);
	}
	else {
		update_info_text("Created hard-link into .pqiv-select");
		info_text_queue_redraw();
	}
	g_free(link_target);
	g_free(current_file_basename);
	bostree_node_weak_unref(file_tree, the_file);
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

	if(fading_current_alpha_stage < 1.) {
		double new_stage = (g_get_monotonic_time() - fading_initial_time) / (1e6 * option_fading_duration);
		new_stage = (new_stage < 0.) ? 0. : ((new_stage > 1.) ? 1. : new_stage);
		fading_current_alpha_stage = new_stage;
	}
	gtk_widget_queue_draw(GTK_WIDGET(main_window));
	if(fading_current_alpha_stage < 1.) {
		return TRUE;
	}
	else {
		if(fading_surface) {
			cairo_surface_destroy(fading_surface);
			fading_surface = NULL;
		}
		return FALSE;
	}
}/*}}}*/
void calculate_current_image_transformed_size(int *image_width, int *image_height) {/*{{{*/
	double transform_width = (double)CURRENT_FILE->width;
	double transform_height = (double)CURRENT_FILE->height;
	cairo_matrix_transform_distance(&current_transformation, &transform_width, &transform_height);
	*image_width = (int)fabs(transform_width);
	*image_height = (int)fabs(transform_height);
}/*}}}*/
void apply_interpolation_quality(cairo_t *cr) {/*{{{*/
	switch(option_interpolation_quality) {
		case AUTO:
			if(CURRENT_FILE->width < 100 || CURRENT_FILE->height < 100) {
				cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
			}
			else {
				cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
			}
			break;
		case FAST:
			cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_FAST);
			break;
		case GOOD:
			cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
			break;
		case BEST:
			cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
			break;
	}
}/*}}}*/
void draw_current_image_to_context(cairo_t *cr) {/*{{{*/
	if(CURRENT_FILE->file_type->draw_fn != NULL) {
		g_mutex_lock(&CURRENT_FILE->lock);
		CURRENT_FILE->file_type->draw_fn(CURRENT_FILE, cr);
		g_mutex_unlock(&CURRENT_FILE->lock);
	}
}/*}}}*/
void setup_checkerboard_pattern() {/*{{{*/
	// Create pattern
	if(background_checkerboard_pattern != NULL) {
		return;
	}
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
	if(CURRENT_FILE->prerendered_view &&
			fabs(current_scale_level * CURRENT_FILE->width + .5 - cairo_image_surface_get_width(CURRENT_FILE->prerendered_view)) < 2 &&
			fabs(current_scale_level * CURRENT_FILE->height + .5 - cairo_image_surface_get_height(CURRENT_FILE->prerendered_view)) < 2) {
		// If the file has a prerender at the correct size attached, we can reuse it here.
		cairo_surface_t *retval = cairo_surface_reference(CURRENT_FILE->prerendered_view);
		if(!option_lowmem) {
			current_scaled_image_surface = cairo_surface_reference(retval);
		}
		return retval;
	}
	/*
	else if(CURRENT_FILE->prerendered_view) {
		printf("Info: Cache miss! %dx%d (cached) vs %dx%d (requested)\n", cairo_image_surface_get_width(CURRENT_FILE->prerendered_view), cairo_image_surface_get_height(CURRENT_FILE->prerendered_view),
			(int)(current_scale_level * CURRENT_FILE->width + .5), (int)(current_scale_level * CURRENT_FILE->height + .5));
	}
	else {
		printf("Info: Cache miss! Nothing present %dx%d (requested)\n", (int)(current_scale_level * CURRENT_FILE->width + .5), (int)(current_scale_level * CURRENT_FILE->height + .5));
	}
	*/

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
static void status_output() {/*{{{*/
#ifndef CONFIGURED_WITHOUT_ACTIONS
	if(!option_status_output) {
		return;
	}
	D_LOCK(file_tree);
	if(file_tree_valid && current_file_node) {
		printf("CURRENT_FILE_NAME=\"%s\"\nCURRENT_FILE_INDEX=%d\n\n", CURRENT_FILE->file_name, bostree_rank(current_file_node));
		fflush(stdout);
	}
	D_UNLOCK(file_tree);
#endif
}/*}}}*/
// }}}
/* Jump dialog {{{ */
#ifndef CONFIGURED_WITHOUT_JUMP_DIALOG /* option --without-jump-dialog: Do not build with -j support */
gboolean jump_dialog_search_list_filter_callback(GtkTreeModel *model, GtkTreeIter *iter, gpointer user_data) { /* {{{ */
	/**
	 * List filter function for the jump dialog
	 */
	gchar *entry_text = (gchar*)gtk_entry_get_text(GTK_ENTRY(user_data));

	if(entry_text[0] == 0) {
		return TRUE;
	}

	gboolean retval;
	if(entry_text[0] == '#') {
		ssize_t desired_index = atoi(&entry_text[1]);

		GValue col_data;
		memset(&col_data, 0, sizeof(GValue));
		gtk_tree_model_get_value(model, iter, 0, &col_data);
		retval = g_value_get_long(&col_data) == desired_index;
		g_value_unset(&col_data);
	}
	else {
		entry_text = g_ascii_strdown(entry_text, -1);
		GValue col_data;
		memset(&col_data, 0, sizeof(GValue));
		gtk_tree_model_get_value(model, iter, 1, &col_data);
		gchar *compare_in = (char*)g_value_get_string(&col_data);
		compare_in = g_ascii_strdown(compare_in, -1);
		retval = (g_strstr_len(compare_in, -1, entry_text) != NULL);
		g_free(compare_in);
		g_value_unset(&col_data);
		g_free(entry_text);
	}

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

	g_idle_add((GSourceFunc)absolute_image_movement, bostree_node_weak_ref(jump_to));
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
		window_show_cursor();
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
		window_hide_cursor();
	}

	gtk_widget_destroy(dlg_window);
	g_object_unref(search_list);
	g_object_unref(search_list_filter);
} /* }}} */
#endif
// }}}
/* Main window functions {{{ */
gboolean window_fullscreen_helper_reset_transition_id() {/*{{{*/
	action_done();
	fullscreen_transition_source_id = -1;
	return FALSE;
}/*}}}*/
void window_fullscreen() {/*{{{*/
	if(is_current_file_loaded()) {
		main_window_in_fullscreen = TRUE;
		image_generate_prerendered_view(CURRENT_FILE, FALSE, -1);
		main_window_in_fullscreen = FALSE;
	}

	// Bugfix for Awesome WM: If hints are active, windows are fullscreen'ed honoring the aspect ratio
	if(option_enforce_window_aspect_ratio) {
		gtk_window_set_geometry_hints(main_window, NULL, NULL, 0);
	}

	// Required to avoid tearing
	if(is_current_file_loaded() && main_window_visible) {
		// This calls only the 2nd part of window_show_background_pixmap, which
		// blanks the window.
		window_clear_background_pixmap();
		window_show_background_pixmap_cb(NULL);
	}

	#ifndef _WIN32
		if(!wm_supports_fullscreen) {
			// WM does not support _NET_WM_ACTION_FULLSCREEN or no WM present
			main_window_in_fullscreen = TRUE;
			gtk_window_move(main_window, screen_geometry.x / screen_scale_factor, screen_geometry.y / screen_scale_factor);
			gtk_window_resize(main_window, screen_geometry.width / screen_scale_factor, screen_geometry.height / screen_scale_factor);
			requested_main_window_width = screen_geometry.width;
			requested_main_window_height = screen_geometry.height;
			window_state_into_fullscreen_actions(NULL);
			return;
		}
	#endif

	if(fullscreen_transition_source_id >= 0) {
		g_source_remove(fullscreen_transition_source_id);
	}
	fullscreen_transition_source_id = g_timeout_add(500, window_fullscreen_helper_reset_transition_id, NULL);
	gtk_window_fullscreen(main_window);
}/*}}}*/
void window_unfullscreen() {/*{{{*/
	if(is_current_file_loaded()) {
		main_window_in_fullscreen = FALSE;
		image_generate_prerendered_view(CURRENT_FILE, FALSE, -1);
		main_window_in_fullscreen = TRUE;
	}

	// Required to avoid tearing
	if(is_current_file_loaded() && main_window_visible) {
		// This calls only the 2nd part of window_show_background_pixmap, which
		// blanks the window.
		window_clear_background_pixmap();
		window_show_background_pixmap_cb(NULL);
	}

	// Ensure that the unfullscreened window will be centered again
	if(option_window_position.x != -1) {
		gtk_window_set_position(main_window, GTK_WIN_POS_CENTER_ALWAYS);
		if(requested_main_window_resize_pos_callback_id > -1) {
			g_source_remove(requested_main_window_resize_pos_callback_id);
		}
		requested_main_window_resize_pos_callback_id = g_timeout_add(500, main_window_reset_pos_callback, NULL);
	}

	#ifndef _WIN32
		if(!wm_supports_fullscreen) {
			// WM does not support _NET_WM_ACTION_FULLSCREEN or no WM present
			main_window_in_fullscreen = FALSE;
			window_state_out_of_fullscreen_actions(NULL);
			return;
		}
	#endif

	if(fullscreen_transition_source_id >= 0) {
		g_source_remove(fullscreen_transition_source_id);
	}
	fullscreen_transition_source_id = g_timeout_add(500, window_fullscreen_helper_reset_transition_id, NULL);
	gtk_window_unfullscreen(main_window);
}/*}}}*/
inline void queue_draw() {/*{{{*/
	if(!current_image_drawn) {
		gtk_widget_queue_draw(GTK_WIDGET(main_window));
	}
}/*}}}*/
#ifndef CONFIGURED_WITHOUT_INFO_TEXT /* option --without-info-text: Build without support for the info text */
inline void info_text_queue_redraw() {/*{{{*/
	if(!option_hide_info_box && main_window_visible) {
		gtk_widget_queue_draw_area(GTK_WIDGET(main_window),
			current_info_text_bounding_box.x,
			current_info_text_bounding_box.y,
			main_window_width - current_info_text_bounding_box.x,
			current_info_text_bounding_box.height
		);
	}
}/*}}}*/
void update_info_text(const gchar *action) {/*{{{*/
	D_LOCK(file_tree);
	current_info_text_cached_font_size = -1;

	#ifndef CONFIGURED_WITHOUT_MONTAGE_MODE
	if(application_mode == MONTAGE) {
		if(!option_hide_info_box) {
			if(current_info_text != NULL) {
				g_free(current_info_text);
			}
			current_info_text = g_strdup("Montage mode");
		}
		gtk_window_set_title(GTK_WINDOW(main_window), "pqiv: Montage mode");
		D_UNLOCK(file_tree);
		return;
	}
	#endif

	if(!current_file_node) {
		const char *none_loaded = "No image loaded";
		if(!option_hide_info_box) {
			if(current_info_text != NULL) {
				g_free(current_info_text);
			}
			if(action) {
				current_info_text = g_strdup_printf("%s - %s", action, none_loaded);
			}
			else {
				current_info_text = g_strdup(none_loaded);
			}
		}
		gtk_window_set_title(GTK_WINDOW(main_window), "pqiv: No image loaded");
		D_UNLOCK(file_tree);
		return;
	}

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
		if(!option_hide_info_box) {
			current_info_text = g_strdup_printf("%s (Image is still loading...)", display_name);
		}
		gtk_window_set_title(GTK_WINDOW(main_window), "pqiv");

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
#endif
gboolean window_close_callback(GtkWidget *object, gpointer user_data) {/*{{{*/
	gtk_main_quit();

	return FALSE;
}/*}}}*/
void calculate_base_draw_pos_and_size(int *image_transform_width, int *image_transform_height, int *x, int *y) {/*{{{*/
	calculate_current_image_transformed_size(image_transform_width, image_transform_height);
	if(option_scale != NO_SCALING || main_window_in_fullscreen) {
		*x = (main_window_width - current_scale_level * *image_transform_width) / 2;
		*y = (main_window_height - current_scale_level * *image_transform_height) / 2;
	}
	else {
		// When scaling is disabled always use the upper left corder to avoid
		// problems with window managers ignoring the large window size request.
		*x = *y = 0;
	}
}/*}}}*/
#ifndef CONFIGURED_WITHOUT_MONTAGE_MODE
void montage_window_set_cursor(int pos_x, int pos_y) {/*{{{*/
	const unsigned n_thumbs_x = main_window_width / (option_thumbnails.width + 10);
	const unsigned n_thumbs_y = main_window_height / (option_thumbnails.height + 10);
	const size_t number_of_images = (ptrdiff_t)bostree_node_count(file_tree);

	if(!montage_window_control.selected_node) {
		return;
	}

	BOSNode *selected_node = bostree_node_weak_unref(file_tree, montage_window_control.selected_node);
	if(!selected_node) {
		selected_node = bostree_select(file_tree, montage_window_control.scroll_y * n_thumbs_x);
		if(!selected_node) {
			selected_node = bostree_select(file_tree, 0);
		}
		if(!selected_node) {
			montage_window_control.selected_node = NULL;
			return;
		}
	}
	size_t old_selection = bostree_rank(selected_node);

	if(pos_x < 0) {
		pos_x = old_selection % n_thumbs_x;
	}
	if(pos_y < 0) {
		pos_y = old_selection / n_thumbs_x;
	}
	if((unsigned)pos_y >= n_thumbs_y) {
		pos_y = n_thumbs_y - 1;
	}

	size_t new_selection = montage_window_control.scroll_y * n_thumbs_x + pos_x + pos_y * n_thumbs_x;

	if(new_selection > number_of_images) {
		new_selection = number_of_images - 1;
	}

	BOSNode *new_selected_node = bostree_select(file_tree, new_selection);
	if(!new_selected_node) {
		new_selected_node = selected_node;
	}
	montage_window_control.selected_node = bostree_node_weak_ref(new_selected_node);
}/*}}}*/
gboolean montage_window_get_move_cursor_target(int pos_x, int pos_y, int move_y_pages, int *target_x, int *target_y, int *target_scroll_y, BOSNode **target_node) {/*{{{*/
	/* The idea to call this function with a possibly invalid pair of on-screen coordinates
	   (pos_x, pos_y) and an amount of pages to scroll move_y_pages. The function will
	   calculate a valid set of coordinates based on the wrapping rules and store them in
	   the output pointers. It returns whether the target is visible on the screen without
	   scrolling
	 */

	const int n_thumbs_x = main_window_width / (option_thumbnails.width + 10);
	const int n_thumbs_y = main_window_height / (option_thumbnails.height + 10);
	const ptrdiff_t number_of_images = (ptrdiff_t)bostree_node_count(file_tree);
	const int n_rows_total       = (number_of_images + n_thumbs_x - 1) / n_thumbs_x;
	const int last_row_n_thumbs  = (number_of_images % n_thumbs_x == 0) ? n_thumbs_x : number_of_images % n_thumbs_x;

	int scroll_y = montage_window_control.scroll_y;
	int original_scroll_y = scroll_y;

	// Use absolute pos_y coordinates
	pos_y += scroll_y;

	// Adjust x position to fit, ignoring the end of the file list for now
	if(option_montage_mode_wrap_mode == MONTAGE_MODE_WRAP_OFF) {
		if(pos_x < 0) pos_x = 0;
		if(pos_x >= n_thumbs_x) pos_x = n_thumbs_x - 1;
	}
	else {
		if(pos_x <= -n_thumbs_x || pos_x >= n_thumbs_x) {
			pos_y += pos_x / n_thumbs_x;
			pos_x %= n_thumbs_x;
		}
		if(pos_x < 0) {
			pos_y--;
			pos_x += n_thumbs_x;
		}
	}

	// Scroll pages
	if(move_y_pages) {
		pos_y += move_y_pages * n_thumbs_y;
		scroll_y += move_y_pages * n_thumbs_y;
	}

	// Adjust y position to fit
	int wrap = 0;
	if(pos_y < 0) {
		if(option_montage_mode_wrap_mode != MONTAGE_MODE_WRAP_FULL) {
			pos_y = 0;
			pos_x = 0;
		}
		else {
			while(pos_y < 0) {
				pos_y += n_rows_total;
			}
			wrap = 1;
		}
	}
	if(pos_y >= n_rows_total) {
		if(option_montage_mode_wrap_mode != MONTAGE_MODE_WRAP_FULL) {
			pos_y = n_rows_total - 1;
			pos_x = last_row_n_thumbs - 1;
		}
		else {
			while(pos_y >= n_rows_total) {
				pos_y -= n_rows_total;
			}
			wrap = -1;
		}
	}
	if(pos_y == n_rows_total - 1) {
		if(pos_x >= last_row_n_thumbs) {
			if(option_montage_mode_wrap_mode != MONTAGE_MODE_WRAP_FULL) {
				pos_x = last_row_n_thumbs - 1;
			}
			else {
				if(wrap == 1) {
					pos_x -= (n_thumbs_x - last_row_n_thumbs);
				}
				else {
					pos_y = 0;
					pos_x -= last_row_n_thumbs;
				}
			}
		}
	}

	// Fixup scroll position if necessary
	if(scroll_y < 0) {
		scroll_y = 0;
	}
	int upper_bound = n_rows_total > n_thumbs_y ? n_rows_total - n_thumbs_y : n_rows_total;
	if(scroll_y > upper_bound) {
		scroll_y = upper_bound;
	}
	if(scroll_y > pos_y) {
		scroll_y = pos_y;
	}
	if(scroll_y + n_thumbs_y <= pos_y) {
		scroll_y = pos_y - n_thumbs_y + 1;
	}

	// Return to page coordinates
	pos_y -= scroll_y;

	if(target_x) {
		*target_x = pos_x;
	}
	if(target_y) {
		*target_y = pos_y;
	}
	if(target_scroll_y) {
		*target_scroll_y = scroll_y;
	}
	if(target_node) {
		*target_node = bostree_select(file_tree, (scroll_y + pos_y) * n_thumbs_x + pos_x);
	}

	return scroll_y == original_scroll_y;
}/*}}}*/
void montage_window_move_cursor(int move_x, int move_y, int move_y_pages) {/*{{{*/
	// Must be called with an active lock.
	const int n_thumbs_x = main_window_width / (option_thumbnails.width + 10);
	const int n_thumbs_y = main_window_height / (option_thumbnails.height + 10);

	if(n_thumbs_x == 0 || n_thumbs_y == 0) {
		return;
	}

	BOSNode *selected_node = bostree_node_weak_unref(file_tree, montage_window_control.selected_node);
	if(!selected_node) {
		selected_node = bostree_select(file_tree, montage_window_control.scroll_y * n_thumbs_x);
		if(!selected_node) {
			selected_node = bostree_select(file_tree, 0);
		}
		if(!selected_node) {
			montage_window_control.selected_node = NULL;
			return;
		}
	}

	size_t old_selection = bostree_rank(selected_node);
	int pos_x = old_selection % n_thumbs_x;
	int pos_y = old_selection / n_thumbs_x;
	if(montage_window_control.scroll_y + n_thumbs_y <= pos_y) {
		montage_window_control.scroll_y = pos_y - n_thumbs_y + 1;
	}
	else if(montage_window_control.scroll_y > pos_y) {
		montage_window_control.scroll_y = pos_y;
	}
	pos_y -= montage_window_control.scroll_y;

	if(move_x != 0 || move_y != 0 || move_y_pages != 0) {
		selected_node = NULL;
		montage_window_get_move_cursor_target(pos_x + move_x, pos_y + move_y, move_y_pages, &pos_x, &pos_y, &montage_window_control.scroll_y, &selected_node);
	}

	montage_window_control.selected_node = bostree_node_weak_ref(selected_node);

	// Queue loading of thumbnails
	abort_pending_image_loads(selected_node);

	int thumb_node_fwd_ctr = (n_thumbs_y - pos_y - 1) * n_thumbs_x + (n_thumbs_x - pos_x - 1) + (option_thumbnails.auto_generate_for_adjacents > 0 ? option_thumbnails.auto_generate_for_adjacents : 0) + 1;
	BOSNode *thumb_node_fwd = selected_node;

	int thumb_node_bwd_ctr = pos_y  * n_thumbs_x + pos_x + (option_thumbnails.auto_generate_for_adjacents > 0 ? option_thumbnails.auto_generate_for_adjacents : 0);
	BOSNode *thumb_node_bwd = bostree_previous_node(selected_node);

	while(TRUE) {
		gboolean did_something = FALSE;
		if(thumb_node_fwd && thumb_node_fwd_ctr) {
			if(!test_and_invalidate_thumbnail(FILE(thumb_node_fwd))) {
				queue_thumbnail_load(bostree_node_weak_ref(thumb_node_fwd));
			}
			thumb_node_fwd = bostree_next_node(thumb_node_fwd);
			thumb_node_fwd_ctr--;
			did_something = TRUE;
		}
		if(thumb_node_bwd && thumb_node_bwd_ctr) {
			if(!test_and_invalidate_thumbnail(FILE(thumb_node_bwd))) {
				queue_thumbnail_load(bostree_node_weak_ref(thumb_node_bwd));
			}
			thumb_node_bwd = bostree_previous_node(thumb_node_bwd);
			thumb_node_bwd_ctr--;
			did_something = TRUE;
		}
		if(!did_something) {
			break;
		}
	}
}/*}}}*/
#ifndef CONFIGURED_WITHOUT_ACTIONS
struct window_draw_thumbnail_montage_show_binding_overlays_data {
	cairo_t *cr;
	int current_x;
	int current_y;
	char *active_prefix;
};
void window_draw_thumbnail_montage_show_binding_overlays_looper(gpointer key, gpointer value, gpointer user_data) {/*{{{*/
	const int n_thumbs_x = main_window_width / (option_thumbnails.width + 10);
	const int n_thumbs_y = main_window_height / (option_thumbnails.height + 10);
	const ptrdiff_t number_of_images = (ptrdiff_t)bostree_node_count(file_tree);
	const int n_rows_total       = (number_of_images + n_thumbs_x - 1) / n_thumbs_x;
	const int last_row_n_thumbs  = (number_of_images % n_thumbs_x == 0) ? n_thumbs_x : number_of_images % n_thumbs_x;

	struct window_draw_thumbnail_montage_show_binding_overlays_data data = *(struct window_draw_thumbnail_montage_show_binding_overlays_data *)user_data;
	guint key_binding_value = GPOINTER_TO_UINT(key);
	key_binding_t *binding = value;
	data.active_prefix = key_binding_sequence_to_string(key_binding_value, data.active_prefix);

	if(binding->next_key_bindings) {
		g_hash_table_foreach(binding->next_key_bindings, window_draw_thumbnail_montage_show_binding_overlays_looper, &data);
	}

	ptrdiff_t target_index;
	BOSNode *target_node;
	for(; binding; binding = binding->next_action) {
		switch(binding->action) {
			case ACTION_MONTAGE_MODE_SET_SHIFT_X:
				data.current_x = binding->parameter.pint;
				break;
			case ACTION_MONTAGE_MODE_SET_SHIFT_Y:
				data.current_y = binding->parameter.pint;
				break;
			case ACTION_MONTAGE_MODE_FOLLOW_PROCEED:
				if(binding->parameter.p2short.p1 >= 0) {
					data.current_x = binding->parameter.p2short.p1;
				}
				if(binding->parameter.p2short.p2 >= 0) {
					data.current_y = binding->parameter.p2short.p2;
				}
				break;
			case ACTION_MONTAGE_MODE_SHIFT_X:
				if(!montage_window_get_move_cursor_target(data.current_x + binding->parameter.pint, data.current_y, 0, &data.current_x, &data.current_y, NULL, NULL)) {
					data.current_y = -1;
					while(binding->next_action) binding = binding->next_action;
					break;
				}
				break;
			case ACTION_MONTAGE_MODE_SHIFT_Y:
				if(!montage_window_get_move_cursor_target(data.current_x, data.current_y + binding->parameter.pint, 0, &data.current_x, &data.current_y, NULL, NULL)) {
					data.current_y = -1;
					while(binding->next_action) binding = binding->next_action;
					break;
				}
				break;
			case ACTION_MONTAGE_MODE_SHIFT_Y_PG:
				if(!montage_window_get_move_cursor_target(data.current_x, data.current_y, binding->parameter.pint, &data.current_x, &data.current_y, NULL, NULL)) {
					data.current_y = -1;
					while(binding->next_action) binding = binding->next_action;
					break;
				}
				break;
			case ACTION_GOTO_FILE_RELATIVE:
				target_index = bostree_rank(relative_image_pointer(binding->parameter.pint));
				data.current_y = target_index / n_thumbs_x - montage_window_control.scroll_y;
				data.current_x = target_index % n_thumbs_x;
				break;
			case ACTION_GOTO_FILE_BYINDEX:
				target_index = binding->parameter.pint;
				if(target_index < 0 || target_index > (int)bostree_node_count(file_tree) - 1) {
					target_index = bostree_node_count(file_tree) - 1;
				}
				data.current_y = target_index / n_thumbs_x - montage_window_control.scroll_y;
				data.current_x = target_index % n_thumbs_x;
				break;
			case ACTION_GOTO_FILE_BYNAME:
				target_node = image_pointer_by_name(binding->parameter.pcharptr);
				if(target_node) {
					target_index = bostree_rank(target_node);
					data.current_y = target_index / n_thumbs_x - montage_window_control.scroll_y;
					data.current_x = target_index % n_thumbs_x;
				}
				break;
			case ACTION_GOTO_DIRECTORY_RELATIVE:
				target_node = relative_image_pointer_directory(binding->parameter.pint, FALSE);
				if(target_node) {
					target_index = bostree_rank(target_node);
					data.current_y = target_index / n_thumbs_x - montage_window_control.scroll_y;
					data.current_x = target_index % n_thumbs_x;
				}
				break;
			case ACTION_GOTO_LOGICAL_DIRECTORY_RELATIVE:
				target_node = relative_image_pointer_directory(binding->parameter.pint, TRUE);
				if(target_node) {
					target_index = bostree_rank(target_node);
					data.current_y = target_index / n_thumbs_x - montage_window_control.scroll_y;
					data.current_x = target_index % n_thumbs_x;
				}
				break;
			default:
				break;
		}
	}

	if(data.current_x >= 0 && data.current_x < n_thumbs_x && data.current_y >= 0 && data.current_y < n_thumbs_y &&
			(data.current_y + montage_window_control.scroll_y != n_rows_total - 1 || data.current_x < last_row_n_thumbs) &&
			((data.current_x != ((struct window_draw_thumbnail_montage_show_binding_overlays_data *)user_data)->current_x ||
			  data.current_y != ((struct window_draw_thumbnail_montage_show_binding_overlays_data *)user_data)->current_y))) {

		cairo_t *cr_arg = data.cr;

		cairo_save(cr_arg);

		cairo_translate(cr_arg,
			(main_window_width  - n_thumbs_x * (option_thumbnails.width + 10)) / 2  + data.current_x * (option_thumbnails.width + 10),
			(main_window_height - n_thumbs_y * (option_thumbnails.height + 10)) / 2 + data.current_y * (option_thumbnails.height + 10)
		);

		BOSNode *node = bostree_select(file_tree, (montage_window_control.scroll_y + data.current_y) * n_thumbs_x + data.current_x);
		if(node && FILE(node)->thumbnail) {
			cairo_translate(cr_arg,
					(option_thumbnails.width  - cairo_image_surface_get_width(FILE(node)->thumbnail)) / 2 + 5,
					(option_thumbnails.height - cairo_image_surface_get_height(FILE(node)->thumbnail)) / 2 + 5
			);
		}

		double x1, y1, x2, y2;
		cairo_set_font_size(cr_arg, 12);
		cairo_text_path(cr_arg, data.active_prefix);
		cairo_path_extents(cr_arg, &x1, &y1, &x2, &y2);
		cairo_path_t *text_path = cairo_copy_path(cr_arg);
		cairo_new_path(cr_arg);
		cairo_rectangle(cr_arg, -5, -(y2 - y1) - 2, x2 - x1 + 10, y2 - y1 + 8);
		cairo_close_path(cr_arg);
		cairo_set_source_rgb(cr_arg, option_box_colors.bg_red, option_box_colors.bg_green, option_box_colors.bg_blue);
		cairo_fill(cr_arg);

		cairo_new_path(cr_arg);
		cairo_append_path(cr_arg, text_path);
		cairo_set_source_rgb(cr_arg, option_box_colors.fg_red, option_box_colors.fg_green, option_box_colors.fg_blue);
		cairo_fill(cr_arg);
		cairo_path_destroy(text_path);

		cairo_restore(cr_arg);
	}

	free(data.active_prefix);
}/*}}}*/
#endif
gboolean window_draw_thumbnail_montage(cairo_t *cr_arg) {/*{{{*/
	D_LOCK(file_tree);

	// Draw black background
	cairo_save(cr_arg);
	cairo_set_source_rgba(cr_arg, 0., 0., 0., option_transparent_background ? 0. : 1.);
	cairo_set_operator(cr_arg, CAIRO_OPERATOR_SOURCE);
	cairo_paint(cr_arg);
	cairo_restore(cr_arg);

	// Calculate how many thumbnails to draw
	const unsigned n_thumbs_x = main_window_width / (option_thumbnails.width + 10);
	const unsigned n_thumbs_y = main_window_height / (option_thumbnails.height + 10);
	size_t top_left_id = montage_window_control.scroll_y * n_thumbs_x;

	BOSNode *selected_node = bostree_node_weak_unref(file_tree, bostree_node_weak_ref(montage_window_control.selected_node));
	size_t selection_rank;
	if(!selected_node) {
		selected_node = NULL;
		selection_rank = (size_t)-1;
	}
	else {
		selection_rank = bostree_rank(selected_node);
	}

	// Do a check if the selected image is out of bounds. Fix if it is.
	if(top_left_id > selection_rank || top_left_id + n_thumbs_x * n_thumbs_y < selection_rank) {
		montage_window_move_cursor(0, 0,  0);
		top_left_id = montage_window_control.scroll_y * n_thumbs_x;
	}

	if(!file_tree_valid) {
		D_UNLOCK(file_tree);
		return FALSE;
	}
	BOSNode *thumb_node = bostree_select(file_tree, top_left_id);
	for(size_t draw_now = 0; draw_now < n_thumbs_x * n_thumbs_y && thumb_node; draw_now++, thumb_node = bostree_next_node(thumb_node)) {
		if(!file_tree_valid || !thumb_node) {
			break;
		}
		file_t *thumb_file = FILE(thumb_node);

		/*/ Debug: Draw a red box around the thumbnail box
		cairo_save(cr_arg);
		cairo_translate(cr_arg,
			(main_window_width - n_thumbs_x * (option_thumbnails.width + 10)) / 2   + (draw_now % n_thumbs_x) * (option_thumbnails.width + 10),
			(main_window_height - n_thumbs_y * (option_thumbnails.height + 10)) / 2 + (draw_now / n_thumbs_x) * (option_thumbnails.height + 10)
		);
		cairo_set_source_rgb(cr_arg, 1., 0, 0);
		cairo_rectangle(cr_arg, 0, 0, option_thumbnails.width + 10, option_thumbnails.height + 10);
		cairo_stroke(cr_arg);
		cairo_restore(cr_arg);*/

		if(thumb_file->thumbnail) {
			cairo_save(cr_arg);
			cairo_translate(cr_arg,
				(main_window_width - n_thumbs_x * (option_thumbnails.width + 10)) / 2   + (draw_now % n_thumbs_x) * (option_thumbnails.width + 10)  + (option_thumbnails.width + 10 - cairo_image_surface_get_width(thumb_file->thumbnail))/2,
				(main_window_height - n_thumbs_y * (option_thumbnails.height + 10)) / 2 + (draw_now / n_thumbs_x) * (option_thumbnails.height + 10) + (option_thumbnails.height + 10 - cairo_image_surface_get_height(thumb_file->thumbnail))/2
			);
			cairo_set_source_surface(cr_arg, thumb_file->thumbnail, 0, 0);
			cairo_new_path(cr_arg);
			cairo_rectangle(cr_arg, 0, 0, cairo_image_surface_get_width(thumb_file->thumbnail), cairo_image_surface_get_height(thumb_file->thumbnail));
			cairo_close_path(cr_arg);
			cairo_clip(cr_arg);
			cairo_paint(cr_arg);

			if(top_left_id + draw_now == selection_rank) {
				cairo_rectangle(cr_arg, 0, 0, cairo_image_surface_get_width(thumb_file->thumbnail), cairo_image_surface_get_height(thumb_file->thumbnail));
				cairo_set_source_rgb(cr_arg, option_box_colors.bg_red, option_box_colors.bg_green, option_box_colors.bg_blue);
				cairo_set_line_width(cr_arg, 8.);
				cairo_stroke(cr_arg);
			}

			cairo_restore(cr_arg);
		}
		else if(top_left_id + draw_now == selection_rank) {
			cairo_save(cr_arg);
			cairo_translate(cr_arg,
				(main_window_width - n_thumbs_x * (option_thumbnails.width + 10)) / 2   + (draw_now % n_thumbs_x) * (option_thumbnails.width + 10) + (option_thumbnails.width - 5)/2,
				(main_window_height - n_thumbs_y * (option_thumbnails.height + 10)) / 2 + (draw_now / n_thumbs_x) * (option_thumbnails.height + 10) + (option_thumbnails.height - 5)/2
			);
			cairo_rectangle(cr_arg, 0, 0, 5, 5);
			cairo_set_source_rgb(cr_arg, option_box_colors.bg_red, option_box_colors.bg_green, option_box_colors.bg_blue);
			cairo_set_line_width(cr_arg, 8.);
			cairo_stroke(cr_arg);
			cairo_restore(cr_arg);
		}
	}

#ifndef CONFIGURED_WITHOUT_ACTIONS
	// In follow mode, draw the key mappings on top of the images
	if(montage_window_control.show_binding_overlays) {
		const int selected_x = selection_rank % n_thumbs_x;
		const int selected_y = selection_rank / n_thumbs_x - montage_window_control.scroll_y;

		struct window_draw_thumbnail_montage_show_binding_overlays_data data = {
			cr_arg, selected_x, selected_y, (char*)""
		};

		g_hash_table_foreach(
				active_key_binding.key_binding && active_key_binding.key_binding->next_key_bindings ?
					active_key_binding.key_binding->next_key_bindings :
					key_bindings[active_key_binding_context],
				window_draw_thumbnail_montage_show_binding_overlays_looper,
				&data);
	}
#endif

	D_UNLOCK(file_tree);
	return TRUE;
}/*}}}*/
#endif
void window_clear_background_pixmap() {/*{{{*/
	if(wm_supports_moveresize) {
		// There's no need for that here.
		return;
	}
	#if defined(GDK_WINDOWING_X11)
		GdkScreen *screen = gdk_screen_get_default();
		#if GTK_MAJOR_VERSION >= 3
			if(!GDK_IS_X11_SCREEN(screen)) {
				return;
			}
		#endif

		Display *display = GDK_SCREEN_XDISPLAY(screen);
		GdkWindow *window = gtk_widget_get_window(GTK_WIDGET(main_window));

		#if GTK_MAJOR_VERSION >= 3
			unsigned long window_xid = gdk_x11_window_get_xid(window);
		#else
			unsigned long window_xid = GDK_WINDOW_XID(window);
		#endif

		XSetWindowBackground(display, window_xid, 0);
	#endif
}/*}}}*/
void window_prerender_background_pixmap(int window_width, int window_height, double scale_level, gboolean fullscreen) {/*{{{*/
	/*
		This function is for old X11 environments that do not support
		moveresize. One will typically see tearing effects there, because the
		time between resizing the window, pqiv receiving an expose event and
		actually drawing is to large to be unnoticable. This function resolves
		the issue by assigning a background pixmap to the window containing the
		new contents of the window. X11 will have something to display until
		the actual drawing pass is done, and things look better.

		The downside is that everything is drawn twice. This isn't a huge problem
		unless --low-memory is set, where, due to the disabled cache, the scaled
		image must be rendered twice.
	*/
	if(wm_supports_moveresize) {
		// There's no need for that here.
		return;
	}
	if(wm_ignores_size_requests) {
		// Tiling WM, do nothing
		return;
	}

	#if defined(GDK_WINDOWING_X11)
		GdkScreen *screen = gdk_screen_get_default();
		#if GTK_MAJOR_VERSION >= 3
			if(!GDK_IS_X11_SCREEN(screen)) {
				return;
			}
		#endif

		Display *display = GDK_SCREEN_XDISPLAY(screen);
		GdkWindow *window = gtk_widget_get_window(GTK_WIDGET(main_window));

		#if GTK_MAJOR_VERSION >= 3
			unsigned long window_xid = gdk_x11_window_get_xid(window);
		#else
			unsigned long window_xid = GDK_WINDOW_XID(window);
		#endif

		if(fullscreen_transition_source_id != -1) {
			// In progress of transitioning fullscreen state. Do nothing.
			XSetWindowBackground(display, window_xid, 0);
			return;
		}

		if(main_window_width == window_width && main_window_height == window_height) {
			// There will be no tearing, do nothing.
			XSetWindowBackground(display, window_xid, 0);
			return;
		}

		XWindowAttributes window_attributes;
		if(XGetWindowAttributes(display, window_xid, &window_attributes) == 0) {
			// Failure, abort.
			return;
		}
		Pixmap pixmap = XCreatePixmap(display, window_xid, window_width * screen_scale_factor, window_height * screen_scale_factor, window_attributes.visual->bits_per_rgb * (3 + !!option_transparent_background));
		cairo_surface_t *pixmap_surface = cairo_xlib_surface_create(display, pixmap, window_attributes.visual, window_width * screen_scale_factor, window_height * screen_scale_factor);

		int ow = main_window_width, oh = main_window_height;
		double osl = current_scale_level;
		gboolean ofs = main_window_in_fullscreen;
		main_window_width = window_width;
		main_window_height = window_height;
		current_scale_level = scale_level;
		main_window_in_fullscreen = fullscreen;
		#ifndef CONFIGURED_WITHOUT_INFO_TEXT
			current_info_text_cached_font_size = -1;
		#endif

		cairo_t *cr = cairo_create(pixmap_surface);
		cairo_save(cr);
		cairo_scale(cr, screen_scale_factor, screen_scale_factor);
		window_draw_callback(GTK_WIDGET(main_window), cr, GUINT_TO_POINTER(1));
		cairo_restore(cr);
		/*cairo_set_source_rgba(cr, 1., 0, 0, .5);
		cairo_set_operator(cr, CAIRO_OPERATOR_OVERLAY);
		cairo_paint(cr);*/
		cairo_surface_flush(cairo_get_target(cr));
		cairo_destroy(cr);

		main_window_width = ow;
		main_window_height = oh;
		current_scale_level = osl;
		main_window_in_fullscreen = ofs;
		#ifndef CONFIGURED_WITHOUT_INFO_TEXT
			current_info_text_cached_font_size = -1;
		#endif

		XSetWindowBackgroundPixmap(display, window_xid, pixmap);
		XFreePixmap(display, pixmap);

		g_idle_add(window_show_background_pixmap_cb, NULL);
	#endif
}/*}}}*/
gboolean window_show_background_pixmap_cb(gpointer user_data) {/*{{{*/
	if(wm_supports_moveresize) {
		// There's no need for that here.
		return FALSE;
	}

	#if defined(GDK_WINDOWING_X11)
		GdkScreen *screen = gdk_screen_get_default();
		#if GTK_MAJOR_VERSION >= 3
			if(!GDK_IS_X11_SCREEN(screen)) {
				return FALSE;
			}
		#endif
		Display *display = GDK_SCREEN_XDISPLAY(screen);
		GdkWindow *window = gtk_widget_get_window(GTK_WIDGET(main_window));
		#if GTK_MAJOR_VERSION >= 3
			unsigned long window_xid = gdk_x11_window_get_xid(window);
		#else
			unsigned long window_xid = GDK_WINDOW_XID(window);
		#endif

		XClearWindow(display, window_xid);
	#endif

	return FALSE;
}/*}}}*/
gboolean window_draw_callback(GtkWidget *widget, cairo_t *cr_arg, gpointer user_data) {/*{{{*/
	// Drawing can generally mean that we succeeded in performing some action.
	// Resume the action queue
	action_done();

	// We have different drawing modes. The default, below, is to draw a single
	// image.
#ifndef CONFIGURED_WITHOUT_MONTAGE_MODE
	if(application_mode == MONTAGE) {
		return window_draw_thumbnail_montage(cr_arg);
	}
#endif

	// Draw image
	int x = 0;
	int y = 0;
	D_LOCK(file_tree);

	// We draw ignoring GDK_SCALE
	// Note that there also is cairo_surface_get_device_scale(). We
	// deliberately do not temper with that to keep the gtk <-> pqiv
	// coordinate transformations consistent in all places.
	cairo_scale(cr_arg, 1./screen_scale_factor, 1./screen_scale_factor);

	if(is_current_file_loaded()) {
		// Calculate where to draw the image and the transformation matrix to use
		int image_transform_width, image_transform_height;
		calculate_base_draw_pos_and_size(&image_transform_width, &image_transform_height, &x, &y);
		cairo_matrix_t apply_transformation = current_transformation;
		apply_transformation.x0 *= current_scale_level;
		apply_transformation.y0 *= current_scale_level;

		// Create a temporary surface to render to first.
		//
		// We use this for fading and to display the last image if the current image is
		// still unavailable
		//
		// The temporary surface contains the image as it is displayed on the
		// screen later, with all transformations applied.

		cairo_surface_t *temporary_surface = cairo_surface_create_similar(cairo_get_target(cr_arg), CAIRO_CONTENT_COLOR_ALPHA, main_window_width, main_window_height);
		cairo_t *cr = NULL;
		if(cairo_surface_status(temporary_surface) != CAIRO_STATUS_SUCCESS) {
			// This image is too large to be rendered into a temorary image surface
			// As a best effort solution, render directly to the window instead
			cairo_save(cr_arg);
			cr = cr_arg;
			cairo_surface_destroy(temporary_surface);
			temporary_surface = NULL;
		}
		else {
			cr = cairo_create(temporary_surface);
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
			unsigned skip_px = (unsigned)(1./current_scale_level);
			if(skip_px == 0) {
				skip_px = 1;
			}
			if(CURRENT_FILE->width > 2*skip_px && CURRENT_FILE->height > 2*skip_px) {
				cairo_rectangle(cr, skip_px, skip_px, CURRENT_FILE->width - 2*skip_px, CURRENT_FILE->height - 2*skip_px);
			}
			else {
				cairo_rectangle(cr, 0, 0, CURRENT_FILE->width, CURRENT_FILE->height);
			}
			cairo_close_path(cr);
			cairo_clip(cr);
			if(option_background_pattern == CHECKERBOARD) {
				cairo_set_source(cr, background_checkerboard_pattern);
			}
			else if(option_background_pattern == WHITE) {
				cairo_set_source_rgba(cr, 1., 1., 1., 1.);
			}
			else {
				cairo_set_source_rgba(cr, 0., 0., 0., 1.);
			}
			cairo_paint(cr);
			cairo_restore(cr);
		}

		// Draw the scaled image.
		if(option_negate) {
			// Negated color mode: The drawing operation is more complex; to do
			// alpha channels correctly we _need_ to have a image surface copy
			// of the image, regardless of lowmem mode. So this drawing mode comes
			// before the option_lowmem special case.
			cairo_surface_t *temporary_scaled_image_surface = get_scaled_image_surface_for_current_image();
			cairo_save(cr);

			// Draw white using the image's alpha channel as a mask.
			// Note that cairo_mask_surface already paints, despite the name.
			cairo_set_source_rgb(cr, 1., 1., 1.);
			cairo_mask_surface(cr, temporary_scaled_image_surface, 0, 0);
			cairo_restore(cr);

			// Now take the difference to the image: This will invert the colors.
			cairo_save(cr);
			cairo_set_operator(cr, CAIRO_OPERATOR_DIFFERENCE);
			cairo_set_source_surface(cr, temporary_scaled_image_surface, 0, 0);
			cairo_paint(cr);
			cairo_restore(cr);

			cairo_surface_destroy(temporary_scaled_image_surface);
		}
		else if(option_lowmem || cr == cr_arg) {
			// In low memory mode, we scale here and draw on the fly
			// The other situation where we do this is if creating the temporary
			// image surface failed, because if this failed creating the temporary
			// image surface will likely also fail.
			cairo_save(cr);
			cairo_scale(cr, current_scale_level, current_scale_level);
			cairo_rectangle(cr, 0, 0, CURRENT_FILE->width + 0.5, CURRENT_FILE->height + 0.5);
			cairo_clip(cr);
			draw_current_image_to_context(cr);
			cairo_restore(cr);
		}
		else {
			// Elsewise, we cache a scaled copy in a separate image surface
			// to speed up movement/redraws of scaled images
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
			if(option_fading && fading_current_alpha_stage < 1. && fading_current_alpha_stage > 0. && fading_surface != NULL) {
				cairo_set_source_surface(cr_arg, fading_surface, 0, 0);
				cairo_set_operator(cr_arg, CAIRO_OPERATOR_SOURCE);
				cairo_paint(cr_arg);

				cairo_set_source_surface(cr_arg, temporary_surface, 0, 0);
				cairo_paint_with_alpha(cr_arg, fading_current_alpha_stage);

				// If this was the first draw, start the fading clock
				if(fading_initial_time < 0) {
					fading_initial_time = g_get_monotonic_time();
				}
			}
			else {
				// Draw the temporary surface to the screen
				cairo_set_source_surface(cr_arg, temporary_surface, 0, 0);
				cairo_set_operator(cr_arg, CAIRO_OPERATOR_SOURCE);
				cairo_paint(cr_arg);
			}

			// Store the surface, for fading and to have something to display if no
			// image is loaded (see below)
			if(last_visible_surface != NULL) {
				cairo_surface_destroy(last_visible_surface);
			}
			if(!option_lowmem || option_fading) {
				last_visible_surface = temporary_surface;
				last_visible_surface_width = main_window_width;
				last_visible_surface_height = main_window_height;
			}
			else {
				cairo_surface_destroy(temporary_surface);
				last_visible_surface = NULL;
			}
		}
		else {
			cairo_restore(cr_arg);
			if(last_visible_surface) {
				cairo_surface_destroy(last_visible_surface);
				last_visible_surface = NULL;
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
		if(last_visible_surface != NULL) {
			// But only do it if the window size hasn't changed. It looks weird
			// to have an image drawn somewhere into the window.
			// TODO An overall neater solution would be to have
			// last_visible_surface store only the image part, and do the
			// centering here.
			if(last_visible_surface_width != main_window_width || last_visible_surface_height != main_window_height) {
				cairo_surface_destroy(last_visible_surface);
				last_visible_surface = NULL;
			}
			else {
				cairo_set_source_surface(cr_arg, last_visible_surface, 0, 0);
				cairo_set_operator(cr_arg, CAIRO_OPERATOR_SOURCE);
				cairo_paint(cr_arg);
			}
		}
		else {
			// Draw black background
			// This must be done explicitly in GTK2, otherwise the background will be white.
			cairo_save(cr_arg);
			cairo_set_source_rgba(cr_arg, 0., 0., 0., option_transparent_background ? 0. : 1.);
			cairo_set_operator(cr_arg, CAIRO_OPERATOR_SOURCE);
			cairo_paint(cr_arg);
			cairo_restore(cr_arg);
		}
	}
	D_UNLOCK(file_tree);

	// Draw info box (directly to the screen)
#ifndef CONFIGURED_WITHOUT_INFO_TEXT
	if(current_info_text != NULL) {
		double x1 = 0., x2 = 0., y1 = 0., y2 = 0.;
		cairo_save(cr_arg);
		// Attempt this multiple times: If it does not fit the window,
		// retry with a smaller font size
		int font_size;
		if(current_info_text_cached_font_size < 0) {
			font_size = 12*screen_scale_factor;
			current_info_text_cached_font_size = 0;
		}
		else {
			font_size = current_info_text_cached_font_size;
		}
		for(; font_size > 6; font_size--) {
			cairo_set_font_size(cr_arg, font_size);

			if(main_window_in_fullscreen == FALSE) {
				// Tiling WMs, at least i3, react weird on our window size changing.
				// Drawing the info box on the image helps to avoid users noticing that.
				cairo_translate(cr_arg, x < 0 ? 0 : x, y < 0 ? 0 : y);
			}

			cairo_set_source_rgb(cr_arg, option_box_colors.bg_red, option_box_colors.bg_green, option_box_colors.bg_blue);
			cairo_translate(cr_arg, 10 * screen_scale_factor, 20 * screen_scale_factor);
			cairo_text_path(cr_arg, current_info_text);
			cairo_path_extents(cr_arg, &x1, &y1, &x2, &y2);

			if(x2 > main_window_width - 10 * screen_scale_factor && !main_window_in_fullscreen) {
				cairo_new_path(cr_arg);
				cairo_restore(cr_arg);
				cairo_save(cr_arg);
				continue;
			}

			current_info_text_cached_font_size = font_size;
			cairo_path_t *text_path = cairo_copy_path(cr_arg);
			cairo_new_path(cr_arg);
			cairo_rectangle(cr_arg, -5, -(y2 - y1) - 2, x2 - x1 + 10, y2 - y1 + 8);
			cairo_close_path(cr_arg);
			cairo_fill(cr_arg);
			cairo_set_source_rgb(cr_arg, option_box_colors.fg_red, option_box_colors.fg_green, option_box_colors.fg_blue);
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
#endif

	// TODO Maybe this will need to be changed someday; the GDK Wayland backend
	// currently does not draw window borders if the draw callback reports
	// success. Anyway, it also draws the borders at the wrong place (well
	// within the window rather than around it), so I'll leave things as they
	// are for the time being.
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
double calculate_scale_level_to_fit(int image_width, int image_height, int window_width, int window_height) {/*{{{*/
	if(scale_override || option_scale == FIXED_SCALE) {
		return current_scale_level;
	}

	// Calculate display width/heights with rotation, but without scaling, applied
	gdouble scale_level = 1.0;

	// Only scale if scaling is not disabled. The alternative is to also
	// scale for no-scaling mode if (!main_window_in_fullscreen). This
	// effectively disables the no-scaling mode in non-fullscreen. I
	// implemented that this way, but changed it per user request.
	if(option_scale == AUTO_SCALEUP || option_scale == AUTO_SCALEDOWN) {
		if(option_scale == AUTO_SCALEUP) {
			// Scale up
			if(image_width * scale_level < window_width) {
				scale_level = window_width * 1.0 / image_width;
			}

			if(image_height * scale_level < window_height) {
				scale_level = window_height * 1.0 / image_height;
			}
		}

		// Scale down
		if(window_height < scale_level * image_height) {
			scale_level = window_height * 1.0 / image_height;
		}
		if(window_width < scale_level * image_width) {
			scale_level = window_width * 1.0 / image_width;
		}
	}
	else if(option_scale == SCALE_TO_FIT_PX) {
		scale_level = fmin(scale_to_fit_size.width * 1. / image_width, scale_to_fit_size.height * 1. / image_height);
	}
	else if(option_scale == SCALE_TO_FIT_WINDOW) {
		scale_level = fmin(window_width * 1. / image_width, window_height * 1. / image_height);
	}

	return scale_level;
}/*}}}*/
double calculate_auto_scale_level_for_screen(int image_width, int image_height) {/*{{{*/
	double scale_level = current_scale_level;

	if(!main_window_in_fullscreen) {
		const int screen_width = screen_geometry.width;
		const int screen_height = screen_geometry.height;

		if(option_scale != FIXED_SCALE && !scale_override) {
			scale_level = 1.0;
			if(option_scale == AUTO_SCALEUP) {
				// Scale up to screen size
				scale_level = screen_width * option_scale_screen_fraction / image_width;
			}
			else if(option_scale == SCALE_TO_FIT_PX) {
				scale_level = fmin(scale_to_fit_size.width * 1. / image_width, scale_to_fit_size.height * 1. / image_height);
			}
			else if(option_scale == SCALE_TO_FIT_WINDOW) {
				scale_level = fmin(main_window_width * 1. / image_width, main_window_height * 1. / image_height);
			}
			else if(option_scale == AUTO_SCALEDOWN && image_width > screen_width * option_scale_screen_fraction) {
				// Scale down to screen size
				scale_level = screen_width * option_scale_screen_fraction / image_width;
			}
			if((option_scale == AUTO_SCALEUP || option_scale == AUTO_SCALEDOWN) && image_height * scale_level > screen_height * option_scale_screen_fraction) {
				// If the height exceeds screen size, scale down
				scale_level = screen_height * option_scale_screen_fraction / image_height;
			}
		}
	}
	else {
		scale_level = calculate_scale_level_to_fit(image_width, image_height, screen_geometry.width, screen_geometry.height);
	}

	return scale_level;
}/*}}}*/
void set_scale_level_for_screen() {/*{{{*/
	if(!current_file_node) {
		return;
	}
	if(!main_window_in_fullscreen) {
		// Calculate diplay width/heights with rotation, but without scaling, applied
		int image_width, image_height;
		calculate_current_image_transformed_size(&image_width, &image_height);
		current_scale_level = calculate_auto_scale_level_for_screen(image_width, image_height);
	}
	else {
		// In fullscreen, the screen size and window size match, so the
		// function to adjust to the window size works just fine (and does
		// not come with the option_scale_screen_fraction limitation,
		// as users would expect in fullscreen)
		set_scale_level_to_fit();
	}
}/*}}}*/
void set_scale_level_to_fit() {/*{{{*/
	if(scale_override || option_scale == FIXED_SCALE) {
		return;
	}

	D_LOCK(file_tree);
	if(is_current_file_loaded()) {
		if(!current_image_drawn) {
			scale_override = FALSE;
		}

		// Calculate diplay width/heights with rotation, but without scaling, applied
		int image_width, image_height;
		calculate_current_image_transformed_size(&image_width, &image_height);

		double new_scale_level = calculate_scale_level_to_fit(image_width, image_height, main_window_width, main_window_height);

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
void set_cursor_auto_hide_mode(int auto_hide) {/*{{{*/
	cursor_auto_hide_mode_enabled = auto_hide;

	// We only enable the motion mask when it is absolutely necessary, because
	// communication with X11 becomes quite expensive when it is active: Every
	// movement of the mouse will trigger an event. The callback disables the
	// event once it is invoked - instead, a callback will query the mouse
	// position some time later to see if it changed.
	//
	// In theory, there is GDK_POINTER_MOTION_HINT_MASK to achieve this.
	// However, it is completely broken, according to my experiments and other
	// people's reports from both the GTK mailing list and Bugzilla.
	if(cursor_auto_hide_mode_enabled) {
		gtk_widget_add_events(GTK_WIDGET(main_window), GDK_POINTER_MOTION_MASK);
	}
	else {
		if(gtk_widget_get_realized(GTK_WIDGET(main_window))) {
			GdkWindow *window = gtk_widget_get_window(GTK_WIDGET(main_window));
			gdk_window_set_events(window, gdk_window_get_events(window) & ~GDK_POINTER_MOTION_MASK);
		}
		else {
			gtk_widget_set_events(GTK_WIDGET(main_window), gtk_widget_get_events(GTK_WIDGET(main_window)) & ~GDK_POINTER_MOTION_MASK);
		}
	}

	if(cursor_auto_hide_timer_id) {
		g_source_remove(cursor_auto_hide_timer_id);
	}
	cursor_auto_hide_timer_id = g_idle_add(window_auto_hide_cursor_callback, NULL);
}/*}}}*/
#ifndef CONFIGURED_WITHOUT_ACTIONS
key_binding_t *key_binding_t_duplicate(key_binding_t *binding) {/*{{{*/
	key_binding_t *retval = g_slice_new(key_binding_t);
	retval->action = binding->action;
	retval->parameter = binding->parameter;
	if(pqiv_action_descriptors[binding->action].parameter_type == PARAMETER_CHARPTR) {
		retval->parameter.pcharptr = g_strdup(retval->parameter.pcharptr);
	}
	retval->next_action = binding->next_action ? key_binding_t_duplicate(binding->next_action) : NULL;
	retval->next_key_bindings = binding->next_key_bindings ? g_hash_table_ref(binding->next_key_bindings) : NULL;

	return retval;
}/*}}}*/
void key_binding_t_destroy_callback(gpointer data) {/*{{{*/
	key_binding_t *binding = (key_binding_t *)data;
	if(binding->next_action) {
		key_binding_t_destroy_callback(binding->next_action);
	}
	if(pqiv_action_descriptors[binding->action].parameter_type == PARAMETER_CHARPTR) {
		g_free(binding->parameter.pcharptr);
	}
	if(binding->next_key_bindings) {
		g_hash_table_unref(binding->next_key_bindings);
	}
	g_slice_free(key_binding_t, binding);
}/*}}}*/
gboolean queue_action_callback(gpointer user_data) {/*{{{*/
	key_binding_t *binding = g_queue_pop_head(&action_queue);
	/* Reset action_queue_idle_id here because action() might want to add a new idle callback. */
	action_queue_idle_id = -1;
	if(!binding) {
		return FALSE;
	}

	// Debug: printf("Queue length is %d. Now at: ", g_queue_get_length(&action_queue) + 1); help_show_single_action(binding); printf("\n");

	action(binding->action, binding->parameter);
	key_binding_t_destroy_callback(binding);

	return FALSE;
}/*}}}*/
void queue_action(pqiv_action_t action_id, pqiv_action_parameter_t parameter) {/*{{{*/
	key_binding_t temporary_binding = {
		.action = action_id,
		.parameter = parameter,
		.next_action = NULL,
		.next_key_bindings = NULL
	};
	g_queue_push_tail(&action_queue, key_binding_t_duplicate(&temporary_binding));

	// Debug: printf("Queue length is %d after adding: ", g_queue_get_length(&action_queue)); help_show_single_action(&temporary_binding); printf("\n");

	if(action_queue_idle_id == -1) {
		action_queue_idle_id = g_idle_add(queue_action_callback, NULL);
	}
}/*}}}*/
void queue_action_from_binding(key_binding_t *binding) {/*{{{*/
	while(binding) {
		queue_action(binding->action, binding->parameter);
		binding = binding->next_action;
	}
}/*}}}*/
#endif
void UNUSED_FUNCTION action_done() {/*{{{*/
#ifndef CONFIGURED_WITHOUT_ACTIONS
	if(!g_queue_is_empty(&action_queue) && action_queue_idle_id == -1) {
		action_queue_idle_id = g_idle_add(queue_action_callback, NULL);
	}
#endif
}/*}}}*/
void action(pqiv_action_t action_id, pqiv_action_parameter_t parameter) {/*{{{*/
	switch(action_id) {
		case ACTION_NOP:
			break;

		case ACTION_SHIFT_Y:
			if(!main_window_visible) return;
			if(!is_current_file_loaded()) return;
			current_shift_y += parameter.pint;
			gtk_widget_queue_draw(GTK_WIDGET(main_window));
			update_info_text(NULL);
			break;

		case ACTION_SHIFT_X:
			if(!is_current_file_loaded()) return;
			current_shift_x += parameter.pint;
			gtk_widget_queue_draw(GTK_WIDGET(main_window));
			update_info_text(NULL);
			break;

		case ACTION_SET_SLIDESHOW_INTERVAL_RELATIVE:
		case ACTION_SET_SLIDESHOW_INTERVAL_ABSOLUTE:
			if(action_id == ACTION_SET_SLIDESHOW_INTERVAL_ABSOLUTE) {
				option_slideshow_interval = fmax(parameter.pdouble, 1e-3);
			}
			else {
				option_slideshow_interval = fmax(1., option_slideshow_interval + parameter.pdouble);
			}
			if(slideshow_timeout_id > 0) {
				g_source_remove(slideshow_timeout_id);
				slideshow_timeout_id = gdk_threads_add_timeout(option_slideshow_interval * 1000, slideshow_timeout_callback, NULL);
			}
			UPDATE_INFO_TEXT("Slideshow interval set to %d seconds", (int)option_slideshow_interval);
			info_text_queue_redraw();
			break;

		case ACTION_SET_SCALE_LEVEL_RELATIVE:
		case ACTION_SET_SCALE_LEVEL_ABSOLUTE:
			if(!is_current_file_loaded()) return;
			if(action_id == ACTION_SET_SCALE_LEVEL_ABSOLUTE) {
				current_scale_level = parameter.pdouble;
			}
			else {
				current_scale_level *= parameter.pdouble;
			}
			current_scale_level = round(current_scale_level * 100.) / 100.;
			if((option_scale == AUTO_SCALEDOWN && current_scale_level > 1) || option_scale == NO_SCALING) {
				scale_override = TRUE;
			}
			invalidate_current_scaled_image_surface();
			image_generate_prerendered_view(CURRENT_FILE, FALSE, current_scale_level);
			current_image_drawn = FALSE;
			if(main_window_in_fullscreen) {
				gtk_widget_queue_draw(GTK_WIDGET(main_window));
			}
			else {
				int image_width, image_height;
				calculate_current_image_transformed_size(&image_width, &image_height);

				// Required to avoid tearing
				requested_main_window_width = current_scale_level * image_width;
				requested_main_window_height = current_scale_level * image_height;
				window_prerender_background_pixmap(requested_main_window_width, requested_main_window_height, current_scale_level, main_window_in_fullscreen);
				gtk_window_resize(main_window, current_scale_level * image_width / screen_scale_factor, current_scale_level * image_height / screen_scale_factor);
				if(!wm_supports_moveresize) {
					queue_draw();
				}
			}
			update_info_text(NULL);
			break;

		case ACTION_TOGGLE_SCALE_MODE:
			if(!is_current_file_loaded()) return;
			if(parameter.pint == 0) {
				if(++option_scale > AUTO_SCALEUP) {
					option_scale = NO_SCALING;
				}
			}
			else {
				option_scale = (parameter.pint - 1) % 5;
			}
			scale_override = FALSE;
			current_image_drawn = FALSE;
			current_shift_x = 0;
			current_shift_y = 0;
			invalidate_current_scaled_image_surface();
			set_scale_level_for_screen();
			main_window_adjust_for_image();
			gtk_widget_queue_draw(GTK_WIDGET(main_window));
			switch(option_scale) {
				case NO_SCALING: update_info_text("Scaling disabled"); break;
				case AUTO_SCALEDOWN: update_info_text("Automatic scaledown enabled"); break;
				case AUTO_SCALEUP: update_info_text("Automatic scaling enabled"); break;
				case FIXED_SCALE: update_info_text("Maintaining current scale level"); break;
				case SCALE_TO_FIT_WINDOW: update_info_text("Maintaining window size"); break;
				default: break;
			}
			break;

		case ACTION_SET_SCALE_MODE_SCREEN_FRACTION:
			if(parameter.pdouble <= 0) {
				g_printerr("Invalid parameter for set_scale_mode_screen_fraction()\n");
				break;
			}
			option_scale_screen_fraction = parameter.pdouble;

			scale_override = FALSE;
			current_image_drawn = FALSE;
			current_shift_x = 0;
			current_shift_y = 0;
			invalidate_current_scaled_image_surface();
			image_generate_prerendered_view(CURRENT_FILE, FALSE, -1);
			set_scale_level_for_screen();
			main_window_adjust_for_image();
			gtk_widget_queue_draw(GTK_WIDGET(main_window));
			break;

		case ACTION_TOGGLE_SHUFFLE_MODE:
			if(parameter.pint == 0) {
				option_shuffle = !option_shuffle;
			}
			else {
				option_shuffle = parameter.pint == 1;
			}
			preload_adjacent_images();
			update_info_text(option_shuffle ? "Shuffle mode enabled" : "Shuffle mode disabled");
			info_text_queue_redraw();
			break;

		case ACTION_RELOAD:
			if(!is_current_file_loaded()) return;
			CURRENT_FILE->force_reload = TRUE;
			update_info_text("Reloading image..");
			info_text_queue_redraw();
			D_LOCK(file_tree);
			queue_image_load(bostree_node_weak_ref(relative_image_pointer(0)));
			D_UNLOCK(file_tree);
			return;
			break;

		case ACTION_RESET_SCALE_LEVEL:
			if(!is_current_file_loaded()) return;
			current_image_drawn = FALSE;
			scale_override = FALSE;
			invalidate_current_scaled_image_surface();
			image_generate_prerendered_view(CURRENT_FILE, FALSE, -1);
			set_scale_level_for_screen();
			main_window_adjust_for_image();
			gtk_widget_queue_draw(GTK_WIDGET(main_window));
			update_info_text(NULL);
			break;

		case ACTION_TOGGLE_FULLSCREEN:
			if(parameter.pint == 1 || (parameter.pint == 0 && main_window_in_fullscreen == FALSE)) {
				window_fullscreen();
			}
			else {
				window_unfullscreen();
			}
			return;
			break;

		case ACTION_FLIP_HORIZONTALLY:
			if(!is_current_file_loaded()) return;
			{
				int image_width, image_height;
				calculate_current_image_transformed_size(&image_width, &image_height);

				cairo_matrix_t transformation = { -1., 0., 0., 1., image_width, 0 };
				transform_current_image(&transformation);
			}
			update_info_text("Image flipped horizontally");
			break;

		case ACTION_FLIP_VERTICALLY:
			if(!is_current_file_loaded()) return;
			{
				int image_width, image_height;
				calculate_current_image_transformed_size(&image_width, &image_height);

				cairo_matrix_t transformation = { 1., 0., 0., -1., 0, image_height };
				transform_current_image(&transformation);
			}
			update_info_text("Image flipped vertically");
			break;

		case ACTION_ROTATE_LEFT:
			if(!is_current_file_loaded()) return;
			{
				int image_width, image_height;
				calculate_current_image_transformed_size(&image_width, &image_height);

				cairo_matrix_t transformation = { 0., -1., 1., 0., 0, image_width };
				transform_current_image(&transformation);
			}
			update_info_text("Image rotated left");
			break;

		case ACTION_ROTATE_RIGHT:
			if(!is_current_file_loaded()) return;
			{
				int image_width, image_height;
				calculate_current_image_transformed_size(&image_width, &image_height);

				cairo_matrix_t transformation = { 0., 1., -1., 0., image_height, 0. };
				transform_current_image(&transformation);
			}
			update_info_text("Image rotated right");
			break;

#ifndef CONFIGURED_WITHOUT_INFO_TEXT
		case ACTION_TOGGLE_INFO_BOX:
			option_hide_info_box = !option_hide_info_box;
			update_info_text(NULL);
			gtk_widget_queue_draw(GTK_WIDGET(main_window));
			break;
#endif

#ifndef CONFIGURED_WITHOUT_JUMP_DIALOG
		case ACTION_JUMP_DIALOG:
			if(!is_current_file_loaded()) return;
			do_jump_dialog();
			return;
			break;
#endif

		case ACTION_TOGGLE_SLIDESHOW:
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
			info_text_queue_redraw();
			break;

		case ACTION_HARDLINK_CURRENT_IMAGE:
			if(!is_current_file_loaded()) return;
			hardlink_current_image();
			gtk_widget_queue_draw(GTK_WIDGET(main_window));
			break;

		case ACTION_GOTO_DIRECTORY_RELATIVE:
			directory_image_movement(parameter.pint, FALSE);
			return;
			break;

		case ACTION_GOTO_LOGICAL_DIRECTORY_RELATIVE:
			directory_image_movement(parameter.pint, TRUE);
			return;
			break;

		case ACTION_GOTO_FILE_RELATIVE:
			relative_image_movement(parameter.pint);
			return;
			break;

		case ACTION_QUIT:
			if(!main_window_visible) {
				gtk_main_quit();
			}
			else {
				gtk_widget_destroy(GTK_WIDGET(main_window));
			}
			return;
			break;

#ifndef CONFIGURED_WITHOUT_EXTERNAL_COMMANDS
		case ACTION_NUMERIC_COMMAND:
			{
				if(parameter.pint < 1 || parameter.pint > 9) {
					g_printerr("Only commands 1..9 are supported.\n");
					return;
				}
				gchar *command = external_image_filter_commands[parameter.pint - 1];
				pqiv_action_parameter_t action_parameter = { .pcharptr = command };
				action(ACTION_COMMAND, action_parameter);
				return;
			}
			break;

		case ACTION_COMMAND:
			if(!is_current_file_loaded()) return;
			{
				char *command = parameter.pcharptr;
				if(command == NULL) {
					break;
				}
				else if (
					((CURRENT_FILE->file_flags & FILE_FLAGS_MEMORY_IMAGE) != 0 && command[0] != '|')
					|| ((CURRENT_FILE->file_flags & FILE_FLAGS_ANIMATION) != 0 && command[0] == '|')) {

					update_info_text("Command incompatible with current file type");
					info_text_queue_redraw();
				}
				else {
					UPDATE_INFO_TEXT("Executing command %s", command);
					info_text_queue_redraw();
					gtk_widget_queue_draw(GTK_WIDGET(main_window));

					command = g_strdup(command);

					g_thread_new("image-filter", apply_external_image_filter_thread, command);
					return;
				}
			}
			break;
#endif

#ifndef CONFIGURED_WITHOUT_ACTIONS
		case ACTION_ADD_FILE:
			g_thread_new("image-loader-from-action", (GThreadFunc)load_images_handle_parameter_thread, g_strdup(parameter.pcharptr));
			break;

		case ACTION_GOTO_FILE_BYINDEX:
		case ACTION_REMOVE_FILE_BYINDEX:
			{
				D_LOCK(file_tree);
				if(parameter.pint < 0) {
					parameter.pint += bostree_node_count(file_tree);
				}
				BOSNode *node = bostree_select(file_tree, parameter.pint);
				if(node) {
					node = bostree_node_weak_ref(node);
				}
				D_UNLOCK(file_tree);

				if(!node) {
					g_printerr("Image #%d not found.\n", parameter.pint);
				}
				else {
					if(action_id == ACTION_GOTO_FILE_BYINDEX) {
						if(application_mode == DEFAULT) {
							absolute_image_movement(node);
						}
						#ifndef CONFIGURED_WITHOUT_MONTAGE_MODE
						else if(application_mode == MONTAGE) {
							D_LOCK(file_tree);
							if(montage_window_control.selected_node != NULL) {
								bostree_node_weak_unref(file_tree, montage_window_control.selected_node);
							}
							montage_window_control.selected_node = node;
							montage_window_move_cursor(0, 0,  0);
							D_UNLOCK(file_tree);
							gtk_widget_queue_draw(GTK_WIDGET(main_window));
						}
						#endif
					}
					else {
						remove_image(node);
						gtk_widget_queue_draw(GTK_WIDGET(main_window));
					}
				}
			}

			return;
			break;

		case ACTION_GOTO_FILE_BYNAME:
		case ACTION_REMOVE_FILE_BYNAME:
			{
				D_LOCK(file_tree);
				BOSNode *node = image_pointer_by_name(parameter.pcharptr);
				if(node) {
					node = bostree_node_weak_ref(node);
				}
				D_UNLOCK(file_tree);

				if(!node) {
					g_printerr("Image `%s' not found.\n", parameter.pcharptr);
				}
				else {
					if(action_id == ACTION_GOTO_FILE_BYNAME) {
						if(application_mode == DEFAULT) {
							absolute_image_movement(node);
						}
						#ifndef CONFIGURED_WITHOUT_MONTAGE_MODE
						else if(application_mode == MONTAGE) {
							D_LOCK(file_tree);
							if(montage_window_control.selected_node != NULL) {
								bostree_node_weak_unref(file_tree, montage_window_control.selected_node);
							}
							montage_window_control.selected_node = node;
							montage_window_move_cursor(0, 0, 0);
							D_UNLOCK(file_tree);
							gtk_widget_queue_draw(GTK_WIDGET(main_window));
						}
						#endif
					}
					else {
						remove_image(node);
						gtk_widget_queue_draw(GTK_WIDGET(main_window));
					}
				}
			}

			return;
			break;

		case ACTION_OUTPUT_FILE_LIST:
			{
				D_LOCK(file_tree);
				for(BOSNode *iter = bostree_select(file_tree, 0); iter; iter = bostree_next_node(iter)) {
					g_print("%s\n", FILE(iter)->display_name);
				}
				D_UNLOCK(file_tree);
			}

			break;

		case ACTION_SET_CURSOR_VISIBILITY:
			if(parameter.pint) {
				window_show_cursor();
			}
			else {
				window_hide_cursor();
			}
			break;

		case ACTION_SET_STATUS_OUTPUT:
			option_status_output = !!parameter.pint;
			status_output();
			break;

		case ACTION_SET_SCALE_MODE_FIT_PX:
			option_scale = SCALE_TO_FIT_PX;
			scale_to_fit_size.width = parameter.p2short.p1;
			scale_to_fit_size.height = parameter.p2short.p2;

			invalidate_current_scaled_image_surface();
			set_scale_level_for_screen();
			main_window_adjust_for_image();
			UPDATE_INFO_TEXT("Scale level adjusted to fit %dx%d px", scale_to_fit_size.width, scale_to_fit_size.height);
			gtk_widget_queue_draw(GTK_WIDGET(main_window));
			break;

		case ACTION_SET_SHIFT_X:
			if(!is_current_file_loaded()) return;
			current_shift_x = parameter.pint;
			gtk_widget_queue_draw(GTK_WIDGET(main_window));
			update_info_text(NULL);
			break;

		case ACTION_SET_SHIFT_Y:
			if(!is_current_file_loaded()) return;
			current_shift_y = parameter.pint;
			gtk_widget_queue_draw(GTK_WIDGET(main_window));
			update_info_text(NULL);
			break;

		case ACTION_BIND_KEY:
			parse_key_bindings(parameter.pcharptr);
			break;

		case ACTION_SEND_KEYS:
			for(char *i=parameter.pcharptr; *i; i++) {
				handle_input_event(KEY_BINDING_VALUE(0, 0, *i));
			}
			break;

		case ACTION_SET_SHIFT_ALIGN_CORNER:
			{
				int flags = 0;
				int x, y;
				int image_width, image_height;
				calculate_base_draw_pos_and_size(&image_width, &image_height, &x, &y);
				image_width *= current_scale_level;
				image_height *= current_scale_level;

				for(char *direction = parameter.pcharptr; *direction; direction++) {
					switch(*direction) {
						case 'C':
							flags = 1; // Prefer centering
							current_shift_x = 0;
							current_shift_y = 0;
							break;
						case 'N':
							if(flags == 0 || image_height > main_window_height) {
								current_shift_y = -y;
							}
							break;
						case 'S':
							if(flags == 0 || image_height > main_window_height) {
								current_shift_y = -y - image_height + main_window_height;
							}
							break;
						case 'E':
							if(flags == 0 || image_width > main_window_width) {
								current_shift_x = -x - image_width + main_window_width;
							}
							break;
						case 'W':
							if(flags == 0 || image_width > main_window_width) {
								current_shift_x = -x;
							}
							break;
					}
				}

				gtk_widget_queue_draw(GTK_WIDGET(main_window));
				update_info_text(NULL);
			}
			break;

		case ACTION_SET_INTERPOLATION_QUALITY:
			if(parameter.pint > BEST || parameter.pint < 0) {
				g_printerr("Interpolation quality `%d' not supported.\n", parameter.pint);
			}
			else if(parameter.pint == 0) {
				if(++option_interpolation_quality > BEST) {
					option_interpolation_quality = AUTO;
				}
			}
			else {
				option_interpolation_quality = parameter.pint - 1;
			}

			switch(option_interpolation_quality) {
				case AUTO:
					update_info_text("Interpolation quality set to auto-determine.");
					break;
				case FAST:
					update_info_text("Interpolation quality set to fast.");
					break;
				case GOOD:
					update_info_text("Interpolation quality set to good.");
					break;
				case BEST:
					update_info_text("Interpolation quality set to best.");
					break;
			}

			invalidate_current_scaled_image_surface();
			gtk_widget_queue_draw(GTK_WIDGET(main_window));
			break;

		case ACTION_ANIMATION_STEP:
			if(!(CURRENT_FILE->file_flags & FILE_FLAGS_ANIMATION)) {
				break;
			}
			if(current_image_animation_timeout_id > 0) {
				g_source_remove(current_image_animation_timeout_id);
				current_image_animation_timeout_id = 0;
			}
			current_image_animation_speed_scale = 0;
			D_LOCK(file_tree);
			if(parameter.pint > 0 && CURRENT_FILE->file_type->animation_next_frame_fn != NULL) {
				// Skip all but one frame here, the last frame progression
				// happens in image_animation_timeout_callback
				for(int i = 0; i < parameter.pint - 1; i++) {
					CURRENT_FILE->file_type->animation_next_frame_fn(CURRENT_FILE);
				}
			}
			D_UNLOCK(file_tree);
			image_animation_timeout_callback(current_file_node);
			update_info_text(NULL);
			break;

		case ACTION_ANIMATION_CONTINUE:
			if(!(CURRENT_FILE->file_flags & FILE_FLAGS_ANIMATION)) {
				break;
			}
			current_image_animation_speed_scale = 1.0;
			if(current_image_animation_timeout_id == 0
					&& (CURRENT_FILE->file_flags & FILE_FLAGS_ANIMATION) != 0
					&& CURRENT_FILE->file_type->animation_initialize_fn != NULL) {
				current_image_animation_timeout_id = gdk_threads_add_timeout(
					CURRENT_FILE->file_type->animation_initialize_fn(CURRENT_FILE),
					image_animation_timeout_callback,
					(gpointer)current_file_node);
			}
			update_info_text(NULL);
			break;

		case ACTION_ANIMATION_SET_SPEED_ABSOLUTE:
			if(!(CURRENT_FILE->file_flags & FILE_FLAGS_ANIMATION)) {
				break;
			}
			current_image_animation_speed_scale = parameter.pdouble;
			if(current_image_animation_speed_scale < 0) {
				current_image_animation_speed_scale = 0;
			}

			UPDATE_INFO_TEXT("Animation speed adjusted to %03.1f%%", current_image_animation_speed_scale * 100.);
			info_text_queue_redraw();
			break;

		case ACTION_ANIMATION_SET_SPEED_RELATIVE:
			if(!(CURRENT_FILE->file_flags & FILE_FLAGS_ANIMATION)) {
				break;
			}
			current_image_animation_speed_scale *= parameter.pdouble;
			if(current_image_animation_speed_scale < 0) {
				current_image_animation_speed_scale = 0;
			}

			UPDATE_INFO_TEXT("Animation speed adjusted to %03.1f%%", current_image_animation_speed_scale * 100.);
			info_text_queue_redraw();
			break;

		case ACTION_GOTO_EARLIER_FILE:
			if(earlier_file_node != NULL) {
				absolute_image_movement(bostree_node_weak_ref(earlier_file_node));
			}
			break;

		case ACTION_SET_CURSOR_AUTO_HIDE:
			set_cursor_auto_hide_mode(!!parameter.pint);
			break;

		case ACTION_SET_FADE_DURATION:
			option_fading_duration = parameter.pdouble;
			option_fading = option_fading_duration > 0;
			UPDATE_INFO_TEXT("Fade duration adjusted to %2.2f seconds", option_fading_duration);
			info_text_queue_redraw();
			break;

		case ACTION_SET_KEYBOARD_TIMEOUT:
			option_keyboard_timeout = parameter.pdouble;
			break;
#endif

#ifndef CONFIGURED_WITHOUT_MONTAGE_MODE
		case ACTION_SET_THUMBNAIL_SIZE:
			option_thumbnails.width = parameter.p2short.p1;
			option_thumbnails.height = parameter.p2short.p2;

			if(application_mode == MONTAGE) {
				abort_pending_image_loads(NULL);
			}

			D_LOCK(file_tree);
			for(BOSNode *node = bostree_select(file_tree, 0); node; node = bostree_next_node(node)) {
				if(FILE(node)->thumbnail) {
					cairo_surface_destroy(FILE(node)->thumbnail);
					FILE(node)->thumbnail = NULL;
				}
			}
			D_UNLOCK(file_tree);

			if(application_mode == MONTAGE) {
				D_LOCK(file_tree);
				montage_window_move_cursor(0, 0, 0);
				D_UNLOCK(file_tree);
				gtk_widget_queue_draw(GTK_WIDGET(main_window));
			}
			break;

		case ACTION_SET_THUMBNAIL_PRELOAD:
			option_thumbnails.enabled = parameter.pint > 0;
			option_thumbnails.auto_generate_for_adjacents = parameter.pint;
			if(parameter.pint > 0) {
				preload_adjacent_images();
				UPDATE_INFO_TEXT("Thumbnail generation enabled for %d adjacent images", parameter.pint);
			}
			else {
				update_info_text("Thumbnail generation disabled");
			}
			info_text_queue_redraw();
			break;

		case ACTION_MONTAGE_MODE_ENTER:
			if(slideshow_timeout_id > 0) {
				g_source_remove(slideshow_timeout_id);
				slideshow_timeout_id = 0;
			}
			if(current_image_animation_timeout_id > 0) {
				g_source_remove(current_image_animation_timeout_id);
				current_image_animation_timeout_id = 0;
			}
			if(last_visible_surface) {
				cairo_surface_destroy(last_visible_surface);
				last_visible_surface = NULL;
			}
			invalidate_current_scaled_image_surface();

			application_mode = MONTAGE;
			active_key_binding_context = MONTAGE;
			D_LOCK(file_tree);
			if(montage_window_control.selected_node) {
				bostree_node_weak_unref(file_tree, montage_window_control.selected_node);
			}
			montage_window_control.selected_node = bostree_node_weak_ref(current_file_node);
			montage_window_move_cursor(0, 0, 0);
			D_UNLOCK(file_tree);
			update_info_text(NULL);
			main_window_adjust_for_image();
			set_cursor_auto_hide_mode(TRUE);

			gtk_widget_queue_draw(GTK_WIDGET(main_window));
			return;
			break;

		case ACTION_MONTAGE_MODE_SHIFT_X:
			if(application_mode != MONTAGE) {
				break;
			}
			D_LOCK(file_tree);
			montage_window_move_cursor(parameter.pint, 0, 0);
			D_UNLOCK(file_tree);
			gtk_widget_queue_draw(GTK_WIDGET(main_window));
			break;

		case ACTION_MONTAGE_MODE_SHIFT_Y:
			if(application_mode != MONTAGE) {
				break;
			}
			D_LOCK(file_tree);
			montage_window_move_cursor(0, parameter.pint, 0);
			D_UNLOCK(file_tree);
			gtk_widget_queue_draw(GTK_WIDGET(main_window));
			break;

		case ACTION_MONTAGE_MODE_SET_WRAP_MODE:
			if(parameter.pint < 0 || parameter.pint >= _MONTAGE_MODE_WRAP_SENTINEL) {
				g_printerr("Invalid parameter for montage_mode_set_wrap_mode()\n");
				break;
			}
			option_montage_mode_wrap_mode = parameter.pint;
			break;

		case ACTION_MONTAGE_MODE_SET_SHIFT_X:
			if(application_mode != MONTAGE) {
				break;
			}
			montage_window_set_cursor(parameter.pint, -1);
			gtk_widget_queue_draw(GTK_WIDGET(main_window));
			break;

		case ACTION_MONTAGE_MODE_SET_SHIFT_Y:
			if(application_mode != MONTAGE) {
				break;
			}
			montage_window_set_cursor(-1, parameter.pint);
			gtk_widget_queue_draw(GTK_WIDGET(main_window));
			break;

		case ACTION_MONTAGE_MODE_SHIFT_Y_PG:
			if(application_mode != MONTAGE) {
				break;
			}
			D_LOCK(file_tree);
			montage_window_move_cursor(0, 0, parameter.pint);
			D_UNLOCK(file_tree);
			gtk_widget_queue_draw(GTK_WIDGET(main_window));
			break;

		case ACTION_MONTAGE_MODE_SHOW_BINDING_OVERLAYS:
			montage_window_control.show_binding_overlays = !!parameter.pint;
			gtk_widget_queue_draw(GTK_WIDGET(main_window));
			break;

#ifndef CONFIGURED_WITHOUT_ACTIONS
		case ACTION_MONTAGE_MODE_FOLLOW:
			if(application_mode != MONTAGE) {
				break;
			}
			if(parameter.pcharptr[0] == 0 || parameter.pcharptr[1] == 0) {
				g_printerr("Error: montage_mode_follow requires at least two characters to work with.\n");
				break;
			}

			montage_window_control.show_binding_overlays = 1;
			option_keyboard_timeout = 5;

			if(follow_mode_key_binding.next_key_bindings) {
				g_hash_table_unref(follow_mode_key_binding.next_key_bindings);
				follow_mode_key_binding.next_key_bindings = NULL;
			}
			follow_mode_key_binding.next_key_bindings = g_hash_table_new_full((GHashFunc)g_direct_hash, (GEqualFunc)g_direct_equal, NULL, key_binding_t_destroy_callback);

			{
				const int n_thumbs_x = main_window_width / (option_thumbnails.width + 10);
				const int n_thumbs_y = main_window_height / (option_thumbnails.height + 10);
				const ptrdiff_t number_of_images = (ptrdiff_t)bostree_node_count(file_tree);
				const ptrdiff_t visible_thumbnails = ((montage_window_control.scroll_y + n_thumbs_y) * n_thumbs_x > number_of_images ? number_of_images - montage_window_control.scroll_y * n_thumbs_x : n_thumbs_x * n_thumbs_y);
				const int number_of_characters = strlen(parameter.pcharptr);

				/*
					On the algorithm used for generating follow mode key bindings:

					The problem of finding a prefix code in base m (I have m keys) for integers up to n (I have n integers) such that the total length
					of the encoding of the entire alphabet is minimized can be solved easily by looking at the base m representation of a number.

					Let l = log(n) / log(m). Observe how the problem is trivial if l is integer: Represent all images by their index in base m with
					leading "zeros" (that is, the first key). And you're done, there can't be a better representation. The nontrivial case is if we
					are between two powers of the basis. Let's look in detail at that case:

					Let k = m^(floor(l))-1; k is the largest number which can be represented using l-1 symbols, and the largest one that has a leading
					zero which could be omitted. The issue obviously is that this would destroy the prefix property. But note the following: If the
					leading digit of n in base m is d<m-1, then for all digits between d+1 and m-1, there actually isn't any ambiguity. This property
					is what the following code exploits to form short prefixes: It determines how many digits exactly have to be reserved in the leading
					position to represent all numbers larger than k, uses up all other ones (by iterating over numbers of length l-1) and then proceeds
					to count with length l.

					As an example, if m = 4 and n = 12, then l = 1.79, k = 03. We can index images starting at 0, so the largest one is going to be
					represented by 11b10 = 23b4. Hence, we will never see numbers in base 4 that start with 3, so we can use one symbol as a single digit
					without ambiguity. We start counting with a single digit instead of two, count only to one (since we are only allowed to use one
					symbol), and then continue with two digits. We end up with the numbers:

					0, 10, 11, 12, 13, 20, 21, 22, 23, 30, 31, 32
				*/

				const int binding_length = (int)ceil(log(visible_thumbnails) / log(number_of_characters));
				const int most_significant_power = (int)pow(number_of_characters, binding_length - 1);
				int high_image_digit = number_of_characters - (visible_thumbnails / most_significant_power);

				// We have one special case:
				// Due to the integer arithmetic in the formulation above we
				// can be off by one. An easy alternative to working in double
				// precision floats entirely is to check whether this is the
				// case and decrease high_image_digit if so.
				//
				// The following statement is mathematically equivalent to
				// "most_significant_power < visible_thumbnails/number_of_characters"
				// which is never true.
				if(high_image_digit * (most_significant_power / number_of_characters) + (number_of_characters - high_image_digit) * most_significant_power < visible_thumbnails) {
					high_image_digit--;
				}

				// 0 means "end of string", any other number is an index (starting from 1) into parameter.pcharptr
				unsigned char key_sequence[binding_length+1];
				if(binding_length > 1) {
					memset(key_sequence, 1, binding_length);
					if(high_image_digit > 0) {
						key_sequence[binding_length-1] = 0;
					}
				}
				else {
					key_sequence[0] = 1;
				}
				key_sequence[binding_length] = 0;

				// Walk through the grid
				for(short y=0; y<n_thumbs_y; y++) {
					for(short x=0; x<n_thumbs_x; x++) {
						// Maintain a running counter
						const int image_id = n_thumbs_x * y + x;
						if(image_id >= visible_thumbnails) {
							break;
						}

						// Now just bind the goto (x,y) command to the sequence in key_sequence
						key_binding_t *active_binding = &follow_mode_key_binding;

						int binding_pos;
						for(binding_pos=0; binding_pos<binding_length-1; binding_pos++) {
							if(key_sequence[binding_pos+1] == 0) {
								break;
							}
							guint key_binding_value = KEY_BINDING_VALUE(0, 0, parameter.pcharptr[key_sequence[binding_pos] - 1]);

							key_binding_t *binding = g_hash_table_lookup(active_binding->next_key_bindings, GUINT_TO_POINTER(key_binding_value));

							if(!binding) {
								binding = g_slice_new(key_binding_t);
								binding->action = ACTION_MONTAGE_MODE_FOLLOW_PROCEED;
								binding->parameter.p2short.p1 = -1;
								binding->parameter.p2short.p2 = -1;
								binding->next_action = NULL;
								binding->next_key_bindings = g_hash_table_new_full((GHashFunc)g_direct_hash, (GEqualFunc)g_direct_equal, NULL, key_binding_t_destroy_callback);
								g_hash_table_insert(active_binding->next_key_bindings, GUINT_TO_POINTER(key_binding_value), binding);
							}
							else if(!binding->next_key_bindings) {
								binding->next_key_bindings = g_hash_table_new_full((GHashFunc)g_direct_hash, (GEqualFunc)g_direct_equal, NULL, key_binding_t_destroy_callback);
							}

							active_binding = binding;
						}

						key_binding_t *binding = g_slice_new0(key_binding_t);
						binding->action = ACTION_MONTAGE_MODE_FOLLOW_PROCEED;
						binding->parameter.p2short.p1 = x;
						binding->parameter.p2short.p2 = y;

						// We bind the continuation of the current action chain
						// as the next action.
						if(active_key_binding.key_binding) {
							binding->next_action = key_binding_t_duplicate(active_key_binding.key_binding);
						}

						guint key_binding_value = KEY_BINDING_VALUE(0, 0, parameter.pcharptr[key_sequence[binding_pos] - 1]);
						g_hash_table_insert(active_binding->next_key_bindings, GUINT_TO_POINTER(key_binding_value), binding);

						// Increase the key_sequence "number"
						int pos = binding_length;
						while(key_sequence[pos] == 0) {
							pos--;
						}
						while((++key_sequence[pos]) > number_of_characters) {
							key_sequence[pos] = 1;
							pos--;
						}
						if(pos == 0 && key_sequence[0] - 1 == high_image_digit) {
							// We have transitioned into the regime starting
							// from which we need to add another digit.
							memset(key_sequence + 1, 1, binding_length - 1);
						}
					}
				}
			}

			active_key_binding.key_binding = &follow_mode_key_binding;
			active_key_binding.associated_image = current_file_node;
			active_key_binding.timeout_id = gdk_threads_add_timeout((size_t)(option_keyboard_timeout * 1000), handle_input_event_timeout_callback, NULL);

			gtk_widget_queue_draw(GTK_WIDGET(main_window));

			return;
			break;

		case ACTION_MONTAGE_MODE_FOLLOW_PROCEED:
			if(application_mode != MONTAGE) {
				break;
			}
			option_keyboard_timeout = .5;
			montage_window_control.show_binding_overlays = 0;
			if(parameter.p2short.p1 >= 0 || parameter.p2short.p2 >= 0) {
				montage_window_set_cursor(parameter.p2short.p1, parameter.p2short.p2);
			}
			gtk_widget_queue_draw(GTK_WIDGET(main_window));
			if(follow_mode_key_binding.next_key_bindings) {
				g_hash_table_unref(follow_mode_key_binding.next_key_bindings);
				follow_mode_key_binding.next_key_bindings = NULL;
			}
			active_key_binding.key_binding = NULL;
			return;
			break;
#endif // without actions

		case ACTION_MONTAGE_MODE_RETURN_PROCEED:
		case ACTION_MONTAGE_MODE_RETURN_CANCEL:
			if(application_mode != MONTAGE) {
				break;
			}
			application_mode = DEFAULT;
			active_key_binding_context = DEFAULT;
			main_window_adjust_for_image();
			gtk_widget_queue_draw(GTK_WIDGET(main_window));
			if(main_window_in_fullscreen) {
				window_hide_cursor();
				set_cursor_auto_hide_mode(FALSE);
			}

			D_LOCK(file_tree);
			BOSNode *target;
			if(action_id == ACTION_MONTAGE_MODE_RETURN_PROCEED) {
				target = montage_window_control.selected_node;
				montage_window_control.selected_node = NULL;
			}
			else if(current_file_node) {
				target = bostree_node_weak_ref(current_file_node);
				bostree_node_weak_unref(file_tree, montage_window_control.selected_node);
				montage_window_control.selected_node = NULL;
			}
			else {
				bostree_node_weak_unref(file_tree, montage_window_control.selected_node);
				montage_window_control.selected_node = NULL;
				if(!file_tree_valid) {
					break;
				}
				target = bostree_select(file_tree, 0);
				if(!target) {
					break;
				}
				target = bostree_node_weak_ref(target);
			}
			D_UNLOCK(file_tree);

			absolute_image_movement(target);

			if(option_lowmem) {
				D_LOCK(file_tree);
				// TODO
				// This currently is a linear search. Given that most users requiring lowmem mode will
				// probably not have many images loaded, this might suffice. But an asymptotically better
				// approach would be neat.
				for(BOSNode *node = bostree_select(file_tree, 0); node; node = bostree_next_node(node)) {
					if(FILE(node)->thumbnail) {
						cairo_surface_destroy(FILE(node)->thumbnail);
						FILE(node)->thumbnail = NULL;
					}
				}
				D_UNLOCK(file_tree);
			}

			update_info_text(NULL);
			return;
			break;
#endif // without montage

		case ACTION_MOVE_WINDOW:
			if(!main_window_in_fullscreen) {
				if(parameter.p2short.p1 < 0) {
					parameter.p2short.p1 = screen_geometry.x + (screen_geometry.width - main_window_width) / 2;
				}
				if(parameter.p2short.p2 < 0) {
					parameter.p2short.p2 = screen_geometry.y + (screen_geometry.height - main_window_height) / 2;
				}
				gtk_window_move(main_window, parameter.p2short.p1, parameter.p2short.p2);
			}
			break;

		case ACTION_TOGGLE_BACKGROUND_PATTERN:
			if(parameter.pint == 0) {
				option_background_pattern++;
			}
			else {
				option_background_pattern = parameter.pint - 1;
			}
			if(option_background_pattern > WHITE || option_background_pattern < CHECKERBOARD) {
				option_background_pattern = CHECKERBOARD;
			}
			UPDATE_INFO_TEXT("Background pattern set to %s", option_background_pattern == BLACK ? "black" :
															 option_background_pattern == WHITE ? "white" :
															 "checkerboard");
			gtk_widget_queue_draw(GTK_WIDGET(main_window));
			break;

		case ACTION_TOGGLE_NEGATE_MODE:
			if(parameter.pint == 0) {
				option_negate = !option_negate;
			}
			else {
				option_negate = parameter.pint - 2;
			}
			UPDATE_INFO_TEXT("Negate mode %s", option_negate ? "enabled" : "disabled");
			gtk_widget_queue_draw(GTK_WIDGET(main_window));
			break;

#ifndef CONFIGURED_WITHOUT_MONTAGE_MODE
		case ACTION_MONTAGE_MODE_SHIFT_Y_ROWS:
			if(application_mode != MONTAGE) {
				break;
			}
			D_LOCK(file_tree);
			int old_scroll_y = montage_window_control.scroll_y;
			montage_window_move_cursor(0, parameter.pint, 0);
			montage_window_control.scroll_y = old_scroll_y + parameter.pint;
			if(montage_window_control.scroll_y < 0) {
				montage_window_control.scroll_y = 0;
			}
			montage_window_move_cursor(0, 0, 0);
			D_UNLOCK(file_tree);
			gtk_widget_queue_draw(GTK_WIDGET(main_window));
			break;
#endif // without montage

		default:
			break;
	}

	// The current action is done, and the function wasn't explicitly returned
	// from. Issue the next action, if one's in the queue, to be run.
	action_done();
}/*}}}*/
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

	// Revert scale factor
	if(screen_scale_factor != 1) {
		event->x *= screen_scale_factor;
		event->y *= screen_scale_factor;
		event->width *= screen_scale_factor;
		event->height *= screen_scale_factor;
	}

	static gint old_window_x, old_window_y;
	if(old_window_x != event->x || old_window_y != event->y) {
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

	// Check whether the WM completely ignored our size request to detect tiling WMs
	if(requested_main_window_width >= 0) {
		wm_ignores_size_requests = !(abs(event->width - requested_main_window_width) < 3 && abs(event->height - requested_main_window_height) < 3);
		requested_main_window_width = -1;
	}

	if(wm_ignores_size_requests || (main_window_width != event->width || main_window_height != event->height)) {
		// Reset cached font size for info text
		#ifndef CONFIGURED_WITHOUT_INFO_TEXT
			current_info_text_cached_font_size = -1;
		#endif

		// Update window size
		if(main_window_in_fullscreen) {
			main_window_width = screen_geometry.width;
			main_window_height = screen_geometry.height;
		}
		else {
			main_window_width = event->width;
			main_window_height = event->height;
		}

		// If the fullscreen state just changed execute the post-change callbacks here
		if(fullscreen_transition_source_id >= 0) {
			g_source_remove(fullscreen_transition_source_id);
			if(main_window_in_fullscreen) {
				window_state_into_fullscreen_actions(main_window);
			}
			else {
				window_state_out_of_fullscreen_actions(main_window);
			}
		}

		// Rescale the image
		if(main_window_width != event->width || main_window_height != event->height) {
			set_scale_level_to_fit();
		}
		queue_draw();

		// We need to redraw in old GTK versions to avoid artifacts
		#if GTK_MAJOR_VERSION < 3
			gtk_widget_queue_draw(GTK_WIDGET(main_window));
		#endif
	}

	#ifndef CONFIGURED_WITHOUT_MONTAGE_MODE
	if(application_mode == MONTAGE) {
		// Make sure that the currently selected image stays in view & that all
		// visible thumbnails are loaded
		D_LOCK(file_tree);
		montage_window_move_cursor(0, 0, 0);
		D_UNLOCK(file_tree);
	}
	#endif

	if(option_click_through) {
		Display *display = GDK_SCREEN_XDISPLAY(gdk_screen_get_default());
		unsigned long window_xid = gdk_x11_window_get_xid(gtk_widget_get_window(GTK_WIDGET(main_window)));
		Region region = XCreateRegion();
		XRectangle rectangle = { 0, 0, 1, 1 };
		XUnionRectWithRegion(&rectangle, region, region);
		XShapeCombineRegion(display, window_xid, ShapeInput, 0, 0, region, ShapeSet);
		XDestroyRegion(region);	
	}

	return FALSE;
}/*}}}*/
void handle_input_event(guint key_binding_value);
#ifndef CONFIGURED_WITHOUT_ACTIONS
gboolean handle_input_event_timeout_callback(gpointer user_data) {/*{{{*/
	handle_input_event(0);
	active_key_binding.key_binding = NULL;
	active_key_binding.timeout_id = -1;
	return FALSE;
}/*}}}*/
#endif
void handle_input_event(guint key_binding_value) {/*{{{*/
	/* Debug
	char *debug_keybinding = key_binding_sequence_to_string(key_binding_value, NULL);
	printf("You pressed %s\n", debug_keybinding);
	free(debug_keybinding);
	// */

	gboolean is_mouse = (key_binding_value >> 31) & 1;
	guint state = (key_binding_value >> (31 - KEY_BINDING_STATE_BITS)) & ((1 << KEY_BINDING_STATE_BITS) - 1);
	guint keycode = key_binding_value & ((1 << (31 - KEY_BINDING_STATE_BITS)) - 1);

	// Filter unwanted state variables out
	state &= gtk_accelerator_get_default_mod_mask();
	state &= ~GDK_SHIFT_MASK;
	key_binding_value = KEY_BINDING_VALUE(is_mouse, state, keycode);

#ifndef CONFIGURED_WITHOUT_ACTIONS
	key_binding_t *binding = NULL;

	if(active_key_binding.timeout_id >= 0 && active_key_binding.key_binding) {
		g_source_remove(active_key_binding.timeout_id);
		active_key_binding.timeout_id = -1;
		if(active_key_binding.key_binding->next_key_bindings) {
			binding = g_hash_table_lookup(active_key_binding.key_binding->next_key_bindings, GUINT_TO_POINTER(key_binding_value));

			if(!binding && !is_mouse && gdk_keyval_is_upper(keycode) && !gdk_keyval_is_lower(keycode)) {
				guint alternate_value = KEY_BINDING_VALUE(is_mouse, state & ~GDK_SHIFT_MASK, gdk_keyval_to_lower(keycode));
				binding = g_hash_table_lookup(active_key_binding.key_binding->next_key_bindings, GUINT_TO_POINTER(alternate_value));
			}
		}
		if(!binding) {
			key_binding_t *binding = active_key_binding.key_binding;
			active_key_binding.key_binding = binding->next_action;
			queue_action_from_binding(binding);
			return;
		}
		active_key_binding.key_binding = NULL;
	}

	if(!key_binding_value) {
		return;
	}

	if(!binding) {
			binding = g_hash_table_lookup(key_bindings[active_key_binding_context], GUINT_TO_POINTER(key_binding_value));

			if(!binding && !is_mouse && gdk_keyval_is_upper(keycode) && !gdk_keyval_is_lower(keycode)) {
				guint alternate_value = KEY_BINDING_VALUE(is_mouse, state & ~GDK_SHIFT_MASK, gdk_keyval_to_lower(keycode));
				binding = g_hash_table_lookup(key_bindings[active_key_binding_context], GUINT_TO_POINTER(alternate_value));
			}
	}

	if(binding) {
		if(binding->next_key_bindings) {
			active_key_binding.key_binding = binding;
			active_key_binding.associated_image = current_file_node;
			active_key_binding.timeout_id = gdk_threads_add_timeout((size_t)(option_keyboard_timeout * 1000), handle_input_event_timeout_callback, NULL);

			#ifndef CONFIGURED_WITHOUT_MONTAGE_MODE
			if(application_mode == MONTAGE && montage_window_control.show_binding_overlays) {
				gtk_widget_queue_draw(GTK_WIDGET(main_window));
			}
			#endif
		}
		else {
			active_key_binding.key_binding = binding->next_action;
			active_key_binding.associated_image = current_file_node;
			queue_action_from_binding(binding);
		}
	}

#else
	for(const struct default_key_bindings_struct *kb = default_key_bindings; kb->key_binding_value; kb++) {
		if(kb->context == active_key_binding_context && kb->key_binding_value == key_binding_value) {
			action(kb->action, kb->parameter);
			break;
		}
	}
#endif
}/*}}}*/
gboolean window_key_press_callback(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {/*{{{*/
	handle_input_event(KEY_BINDING_VALUE(0, event->state, event->keyval));
	return FALSE;
}/*}}}*/
void window_center_mouse() {/*{{{*/
	GdkDisplay *display = gtk_widget_get_display(GTK_WIDGET(main_window));
	GdkScreen *screen = gtk_widget_get_screen(GTK_WIDGET(main_window));

	#if GTK_MAJOR_VERSION < 3
		gdk_display_warp_pointer(display, screen, (screen_geometry.x + screen_geometry.width / 2.) / screen_scale_factor, (screen_geometry.y + screen_geometry.height / 2.) / screen_scale_factor);
	#else
		#if GTK_MAJOR_VERSION == 3 && GTK_MINOR_VERSION < 20
			GdkDevice *device = gdk_device_manager_get_client_pointer(gdk_display_get_device_manager(display));
		#else
			GdkDevice *device = gdk_seat_get_pointer(gdk_display_get_default_seat(display));
		#endif
		gdk_device_warp(device, screen, (screen_geometry.x + screen_geometry.width / 2.) / screen_scale_factor, (screen_geometry.y + screen_geometry.height / 2.) / screen_scale_factor);

	#endif
}/*}}}*/
gboolean window_auto_hide_cursor_callback(gpointer user_data) {/*{{{*/
	struct Point *cursor_pos = (struct Point *)user_data;

	if(!main_window_visible) {
		return FALSE;
	}

	gboolean default_retval = TRUE;
	if(!cursor_pos) {
		// This function has been called in an improper way. But on purpose:
		// We are to allocate cursor_pos ourselves, and setup a timeout.
		// Note that it'll always appear as if the cursor position changed below.
		cursor_pos = g_malloc(sizeof(struct Point));
		cursor_pos->x = -1;
		cursor_pos->y = -1;
		cursor_auto_hide_timer_id = g_timeout_add_full(G_PRIORITY_DEFAULT, 1000, window_auto_hide_cursor_callback, cursor_pos, g_free);
		default_retval = FALSE;
	}

	// Get current mouse position
	int x, y;
	#if GTK_MAJOR_VERSION >= 3
		GdkDisplay *display = gtk_widget_get_display(GTK_WIDGET(main_window));
		#if GTK_MAJOR_VERSION == 3 && GTK_MINOR_VERSION < 20
			GdkDevice *device = gdk_device_manager_get_client_pointer(gdk_display_get_device_manager(display));
		#else
			GdkDevice *device = gdk_seat_get_pointer(gdk_display_get_default_seat(display));
		#endif
		gdk_window_get_device_position(gtk_widget_get_window(GTK_WIDGET(main_window)), device, &x, &y, NULL);
	#else
		gdk_window_get_pointer(gtk_widget_get_window(GTK_WIDGET(main_window)), &x, &y, NULL);
	#endif

	// Check if the mouse has been moved. If it has, try again in 2s.
	if(x != cursor_pos->x || y != cursor_pos->y) {
		cursor_pos->x = x;
		cursor_pos->y = y;
		return default_retval;
	}

	// This source is going to be deleted, since we'll return FALSE
	cursor_auto_hide_timer_id = 0;

	// Hide the cursor
	window_hide_cursor();

	// Resubscripe to motion events
	gtk_widget_add_events(GTK_WIDGET(main_window), GDK_POINTER_MOTION_MASK);

	return FALSE;
}/*}}}*/
gboolean window_motion_notify_callback(GtkWidget *widget, GdkEventMotion *event, gpointer user_data) {/*{{{*/
	if(!(event->state & (GDK_BUTTON1_MASK | GDK_BUTTON2_MASK | GDK_BUTTON3_MASK))) {
		// Receiving pointer motion events is expensive. We are really only interested in being
		// informed when the user starts to move the mouse. So ask the display
		// server to stop sending motion events.
		// In theory, GDK_POINTER_MOTION_HINT_MASK could do this job, but, alas, it's broken.
		GdkWindow *window = gtk_widget_get_window(GTK_WIDGET(main_window));
		gdk_window_set_events(window, gdk_window_get_events(window) & ~GDK_POINTER_MOTION_MASK);
	}

	if(cursor_auto_hide_mode_enabled) {
		// Show the cursor
		window_show_cursor();

		// Set up a callback to check whether the cursor has been moved in 2s
		if(cursor_auto_hide_timer_id) {
			g_source_remove(cursor_auto_hide_timer_id);
		}
		struct Point *cursor_pos = g_malloc(sizeof(struct Point));
		cursor_pos->x = event->x;
		cursor_pos->y = event->y;
		cursor_auto_hide_timer_id = g_timeout_add_full(G_PRIORITY_DEFAULT, 1000, window_auto_hide_cursor_callback, cursor_pos, g_free);
	}

	if(!main_window_in_fullscreen) {
		return FALSE;
	}

	if(application_mode == DEFAULT && event->state & (GDK_BUTTON1_MASK | GDK_BUTTON2_MASK | GDK_BUTTON3_MASK)) {
		gdouble dev_x = screen_geometry.width / 2 + screen_geometry.x - event->x_root * screen_scale_factor;
		gdouble dev_y = screen_geometry.height / 2 + screen_geometry.y - event->y_root * screen_scale_factor;

		if(fabs(dev_x) < 5 && fabs(dev_y) < 4) {
			return FALSE;
		}

		if(event->state & GDK_BUTTON1_MASK) {
			if(application_mode == DEFAULT) {
				current_shift_x += dev_x;
				current_shift_y += dev_y;
			}
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
	if(event->time - last_button_press_time < 250 && (application_mode != MONTAGE || event->type != GDK_2BUTTON_PRESS)) {
		// GTK double-reported this. Ignore.
		return FALSE;
	}
	last_button_press_time = event->time;

#ifndef CONFIGURED_WITHOUT_MONTAGE_MODE
	if(application_mode == MONTAGE && cursor_visible && event->button == 1) {
		// In montage mode, the mouse may be used to select thumbnails

		// Thumbnails are drawn such that the whole mosaique is centered. Undo
		// that to find the correct index.
		//
		event->x -= (main_window_width % (option_thumbnails.width + 10)) / 2;
		event->y -= (main_window_height % (option_thumbnails.height + 10)) / 2;
		if(event->x < 0) event->x = 0;
		if(event->y < 0) event->y = 0;

		montage_window_set_cursor((int)(event->x / (option_thumbnails.width + 10)), (int)(event->y / (option_thumbnails.height + 10)));
		gtk_widget_queue_draw(GTK_WIDGET(main_window));
		if(event->type == GDK_2BUTTON_PRESS) {
			pqiv_action_parameter_t empty_param = { .pint = 0 };
#ifndef CONFIGURED_WITHOUT_ACTIONS
			queue_action(ACTION_MONTAGE_MODE_RETURN_PROCEED, empty_param);
#else
			action(ACTION_MONTAGE_MODE_RETURN_PROCEED, empty_param);
#endif
			// Prevent the release handler from handling this event again
			last_button_press_time = 0;
		}

		return FALSE;
	}
#endif // CONFIGURED_WITHOUT_MONTAGE_MODE

	if(main_window_in_fullscreen) {
		window_center_mouse();
	}

	return FALSE;
}/*}}}*/
gboolean window_button_release_callback(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {/*{{{*/
	if(event->time - last_button_press_time > 250 || (event->time == last_button_release_time && last_button_release_time > 0)) {
		// Do nothing if the button was pressed for a long time
		// Also, fix a bug where GTK reports the same release event twice -- by
		// assuming that no user would ever be able to press and release
		// buttons sufficiently fast for time to have the same (millis) value.
		return FALSE;
	}
	last_button_release_time = event->time;

	if(!main_window_in_fullscreen) {
		if(option_transparent_background) {
			gtk_window_set_decorated(main_window, !gtk_window_get_decorated(main_window));
		}

		// All other bindings are only handled in fullscreen.
		return FALSE;
	}

	handle_input_event(KEY_BINDING_VALUE(1, event->state, event->button));
	return FALSE;
}/*}}}*/
gboolean window_scroll_callback(GtkWidget *widget, GdkEventScroll *event, gpointer user_data) {/*{{{*/
	handle_input_event(KEY_BINDING_VALUE(1, event->state, (event->direction + 1) << 2));
	return FALSE;
}/*}}}*/
void window_hide_cursor() {/*{{{*/
	if(!main_window_visible) {
		return;
	}
	GdkDisplay *display = gtk_widget_get_display(GTK_WIDGET(main_window));
	GdkCursor *cursor = gdk_cursor_new_for_display(display, GDK_BLANK_CURSOR);
	GdkWindow *window = gtk_widget_get_window(GTK_WIDGET(main_window));
	if(window) {
		gdk_window_set_cursor(window, cursor);
		cursor_visible = FALSE;
	}
	#if GTK_MAJOR_VERSION >= 3
		g_object_unref(cursor);
	#endif
}/*}}}*/
void window_show_cursor() {/*{{{*/
	if(!main_window_visible) {
		return;
	}
	GdkWindow *window = gtk_widget_get_window(GTK_WIDGET(main_window));
	if(window) {
		gdk_window_set_cursor(window, NULL);
		cursor_visible = TRUE;
	}
}/*}}}*/
gboolean window_state_into_fullscreen_actions(gpointer user_data) {/*{{{*/
	if(user_data == NULL) {
		current_shift_x = 0;
		current_shift_y = 0;
		if(application_mode == DEFAULT) {
			window_hide_cursor();
			set_cursor_auto_hide_mode(FALSE);
		}
		update_info_text(NULL);
	}

	main_window_width = screen_geometry.width;
	main_window_height = screen_geometry.height;

	set_scale_level_to_fit();
	invalidate_current_scaled_image_surface();
	#if GTK_MAJOR_VERSION < 3
		gtk_widget_queue_draw(GTK_WIDGET(main_window));
	#endif
	fullscreen_transition_source_id = -1;
	action_done();
	return FALSE;
}/*}}}*/
gboolean window_state_out_of_fullscreen_actions(gpointer user_data) {/*{{{*/
	if(user_data == NULL) {
		current_shift_x = 0;
		current_shift_y = 0;
		window_show_cursor();
		set_cursor_auto_hide_mode(TRUE);
		update_info_text(NULL);
	}

	// If the fullscreen state is left, readjust image placement/size/..
	scale_override = FALSE;
	set_scale_level_for_screen();
	main_window_adjust_for_image();
	if(!main_window_visible) {
		main_window_visible = TRUE;
		gtk_widget_show_all(GTK_WIDGET(main_window));
	}
	invalidate_current_scaled_image_surface();
	fullscreen_transition_source_id = -1;
	action_done();
	return FALSE;
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

		if(fullscreen_transition_source_id >= 0) {
			g_source_remove(fullscreen_transition_source_id);
		}
		if(main_window_in_fullscreen) {
			window_state_into_fullscreen_actions(NULL);
			fullscreen_transition_source_id = g_timeout_add(500, window_state_into_fullscreen_actions, main_window);
		}
		else {
			window_state_out_of_fullscreen_actions(NULL);
			fullscreen_transition_source_id = g_timeout_add(500, window_state_out_of_fullscreen_actions, main_window);
		}
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
	#if defined(GDK_WINDOWING_X11)
		GdkScreen *screen = GDK_SCREEN(user_data);

		// TODO Would _NET_WM_ALLOWED_ACTIONS -> _NET_WM_ACTION_RESIZE and _NET_WM_ACTION_FULLSCREEN  be a better choice here?
		#if GTK_MAJOR_VERSION >= 3
			if(GDK_IS_X11_SCREEN(screen)) {
				wm_supports_fullscreen = gdk_x11_screen_supports_net_wm_hint(screen, gdk_x11_xatom_to_atom(gdk_x11_get_xatom_by_name("_NET_WM_STATE_FULLSCREEN")));
				wm_supports_moveresize = gdk_x11_screen_supports_net_wm_hint(screen, gdk_x11_xatom_to_atom(gdk_x11_get_xatom_by_name("_NET_MOVERESIZE_WINDOW")));
				wm_supports_framedrawn = gdk_x11_screen_supports_net_wm_hint(screen, gdk_x11_xatom_to_atom(gdk_x11_get_xatom_by_name("_NET_WM_FRAME_DRAWN")));
			}
		#else
			wm_supports_fullscreen = gdk_x11_screen_supports_net_wm_hint(screen, gdk_x11_xatom_to_atom(gdk_x11_get_xatom_by_name("_NET_WM_STATE_FULLSCREEN")));
			wm_supports_moveresize = gdk_x11_screen_supports_net_wm_hint(screen, gdk_x11_xatom_to_atom(gdk_x11_get_xatom_by_name("_NET_MOVERESIZE_WINDOW")));
			wm_supports_framedrawn = gdk_x11_screen_supports_net_wm_hint(screen, gdk_x11_xatom_to_atom(gdk_x11_get_xatom_by_name("_NET_WM_FRAME_DRAWN")));
		#endif
	#endif
}/*}}}*/
void window_screen_changed_callback(GtkWidget *widget, GdkScreen *previous_screen, gpointer user_data) {/*{{{*/
	GdkScreen *screen = gtk_widget_get_screen(GTK_WIDGET(main_window));
	GdkWindow *window = gtk_widget_get_window(GTK_WIDGET(main_window));

	#if defined(GDK_WINDOWING_X11)
		#if GTK_CHECK_VERSION(3, 0, 0)
			if(GDK_IS_X11_DISPLAY(gdk_screen_get_display(screen))) {
				g_signal_connect(screen, "window-manager-changed", G_CALLBACK(window_screen_window_manager_changed_callback), screen);
			}
		#else
			g_signal_connect(screen, "window-manager-changed", G_CALLBACK(window_screen_window_manager_changed_callback), screen);
		#endif
	#endif
	window_screen_window_manager_changed_callback(screen);

	#if GTK_CHECK_VERSION(3, 22, 0)
		GdkDisplay *display = gdk_window_get_display(window);
		GdkMonitor *monitor = gdk_display_get_monitor_at_window(display, window);

		static GdkMonitor *old_monitor = NULL;
		if(old_monitor != NULL && option_transparent_background) {
			window_screen_activate_rgba();
		}
		if(old_monitor != monitor) {
			gdk_monitor_get_geometry(monitor, &screen_geometry);
			old_monitor = monitor;
			screen_scale_factor = gdk_monitor_get_scale_factor(monitor);
			if(screen_scale_factor != 1) {
				screen_geometry.x *= screen_scale_factor;
				screen_geometry.y *= screen_scale_factor;
				screen_geometry.width *= screen_scale_factor;
				screen_geometry.height *= screen_scale_factor;
			}
		}
	#else
		guint monitor = gdk_screen_get_monitor_at_window(screen, window);

		static guint old_monitor = 9999;
		if(old_monitor != 9999 && option_transparent_background) {
			window_screen_activate_rgba();
		}
		if(old_monitor != monitor) {
			gdk_screen_get_monitor_geometry(screen, monitor, &screen_geometry);
			old_monitor = monitor;
			#if GTK_CHECK_VERSION(3, 10, 0)
				screen_scale_factor = gdk_screen_get_monitor_scale_factor(screen, monitor);
				if(screen_scale_factor != 1) {
					screen_geometry.x *= screen_scale_factor;
					screen_geometry.y *= screen_scale_factor;
					screen_geometry.width *= screen_scale_factor;
					screen_geometry.height *= screen_scale_factor;
				}
			#endif
		}
	#endif
}/*}}}*/
void window_realize_callback(GtkWidget *widget, gpointer user_data) {/*{{{*/
	if(option_start_fullscreen) {
		window_fullscreen();
	}

	// Execute the screen-changed callback, to assign the correct screen
	// to the window (if it's not the primary one, which we assigned in
	// create_window)
	window_screen_changed_callback(NULL, NULL, NULL);

	#if GTK_MAJOR_VERSION < 3
		if(option_transparent_background) {
			window_screen_activate_rgba();
		}
	#endif

	#if GTK_MAJOR_VERSION < 3 && !defined(_WIN32)
		gdk_property_change(gtk_widget_get_window(GTK_WIDGET(main_window)), gdk_atom_intern("_GTK_THEME_VARIANT", FALSE), (GdkAtom)XA_STRING, 8, GDK_PROP_MODE_REPLACE, (guchar *)"dark", 4);
	#endif

	if(!option_transparent_background) {
		// Ensure that extra pixels (shown e.g. while resizing the window) are black
		window_clear_background_pixmap();
	}

	if(!main_window_in_fullscreen) {
		// Start the timer to hide the cursor
		set_cursor_auto_hide_mode(TRUE);
	}
}/*}}}*/
void create_window() { /*{{{*/
	if(main_window != NULL) {
		return;
	}

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
		GDK_POINTER_MOTION_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
		GDK_KEY_PRESS_MASK | GDK_PROPERTY_CHANGE_MASK | GDK_KEY_RELEASE_MASK | GDK_STRUCTURE_MASK);

	// Initialize the screen geometry variable to the primary screen
	// Useful if no WM is present
	GdkScreen *screen = gdk_screen_get_default();
	#if GTK_CHECK_VERSION(3, 22, 0)
		GdkDisplay *display = gdk_screen_get_display(screen);
		GdkMonitor *monitor = gdk_display_get_primary_monitor(display);
		if(!monitor) {
			monitor = gdk_display_get_monitor(display, 0);
		}
		gdk_monitor_get_geometry(monitor, &screen_geometry);
		screen_scale_factor = gdk_monitor_get_scale_factor(monitor);
	#else
		guint monitor = gdk_screen_get_primary_monitor(screen);
		gdk_screen_get_monitor_geometry(screen, monitor, &screen_geometry);
		#if GTK_CHECK_VERSION(3, 10, 0)
			screen_scale_factor = gdk_screen_get_monitor_scale_factor(screen, monitor);
		#endif
	#endif
	if(screen_scale_factor != 1) {
		screen_geometry.x *= screen_scale_factor;
		screen_geometry.y *= screen_scale_factor;
		screen_geometry.width *= screen_scale_factor;
		screen_geometry.height *= screen_scale_factor;
	}
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
		gtk_main_quit();
	}

	return FALSE;
}/*}}}*/
gboolean help_show_version(const gchar *option_name, const gchar *value, gpointer data, GError **error) {/*{{{*/
	g_print("pqiv " PQIV_VERSION "\n");
	exit(0);
	return FALSE;
}
#ifndef CONFIGURED_WITHOUT_ACTIONS
char *key_binding_sequence_to_string(guint key_binding_value, gchar *prefix) {/*{{{*/
	gboolean is_mouse = (key_binding_value >> 31) & 1;
	guint state = (key_binding_value >> (31 - KEY_BINDING_STATE_BITS)) & ((1 << KEY_BINDING_STATE_BITS) - 1);
	guint keycode = key_binding_value & ((1 << (31 - KEY_BINDING_STATE_BITS)) - 1);

	gchar *str_key;
	gchar modifier[255];
	snprintf(modifier, 255, "%s%s%s", state & GDK_SHIFT_MASK ? "<Shift>" : "", state & GDK_CONTROL_MASK ? "<Control>" : "", state & GDK_MOD1_MASK ? "<Alt>" : "");
	if(is_mouse) {
		if(keycode >> 2 == 0) {
			str_key = g_strdup_printf("%s%s<Mouse-%d> ", prefix ? prefix : "", modifier, keycode);
		}
		else {
			str_key = g_strdup_printf("%s%s<Mouse-Scroll-%d> ", prefix ? prefix : "", modifier, keycode >> 2);
		}
	}
	else {
		char *keyval_name = gdk_keyval_name(keycode);
		str_key = g_strdup_printf("%s%s%s%s%s ", prefix ? prefix : "", modifier, keyval_name && keyval_name[0] && !keyval_name[1] ? "" : "<", keyval_name, keyval_name && keyval_name[0] && !keyval_name[1] ? "" : ">");
	}

	return str_key;
}/*}}}*/
void help_show_single_action(key_binding_t *current_action) {/*{{{*/
	g_print("%s%c", pqiv_action_descriptors[current_action->action].name, KEY_BINDINGS_COMMAND_PARAMETER_BEGIN_SYMBOL);
	switch(pqiv_action_descriptors[current_action->action].parameter_type) {
		case PARAMETER_NONE:
			g_print("%c ", KEY_BINDINGS_COMMAND_PARAMETER_END_SYMBOL);
			break;
		case PARAMETER_INT:
			g_print("%d%c ", current_action->parameter.pint, KEY_BINDINGS_COMMAND_PARAMETER_END_SYMBOL);
			break;
		case PARAMETER_DOUBLE:
			g_print("%g%c ", current_action->parameter.pdouble, KEY_BINDINGS_COMMAND_PARAMETER_END_SYMBOL);
			break;
		case PARAMETER_CHARPTR:
			for(const char *p = current_action->parameter.pcharptr; *p; p++) {
				if(*p == KEY_BINDINGS_COMMAND_PARAMETER_END_SYMBOL || *p == '\\') {
					g_print("\\");
				}
				g_print("%c", *p);
			}
			g_print("%c ", KEY_BINDINGS_COMMAND_PARAMETER_END_SYMBOL);
			break;
		case PARAMETER_2SHORT:
			g_print("%d, %d%c ", current_action->parameter.p2short.p1, current_action->parameter.p2short.p2, KEY_BINDINGS_COMMAND_PARAMETER_END_SYMBOL);
	}
}/*}}}*/
void help_show_key_bindings_helper(gpointer key, gpointer value, gpointer user_data) {/*{{{*/
	guint key_binding_value = GPOINTER_TO_UINT(key);
	key_binding_t *key_binding = (key_binding_t *)value;

	char *str_key = key_binding_sequence_to_string(key_binding_value, user_data);

	if(key_binding->next_key_bindings) {
		g_hash_table_foreach(key_binding->next_key_bindings, help_show_key_bindings_helper, str_key);
	}

	g_print("%30s %c ", str_key, KEY_BINDINGS_COMMANDS_BEGIN_SYMBOL);
	for(key_binding_t *current_action = key_binding; current_action; current_action = current_action->next_action) {
		help_show_single_action(current_action);
	}
	g_print("%c \n", KEY_BINDINGS_COMMANDS_END_SYMBOL);

	g_free(str_key);
}/*}}}*/
gboolean help_show_key_bindings(const gchar *option_name, const gchar *value, gpointer data, GError **error) {/*{{{*/
	gchar *old_locale = g_strdup(setlocale(LC_NUMERIC, NULL));
	setlocale(LC_NUMERIC, "C");

	g_hash_table_foreach(key_bindings[0], help_show_key_bindings_helper, (gpointer)"");
	for(int i=1; i<KEY_BINDING_CONTEXTS_COUNT; i++) {
		if(!*key_binding_context_names[i]) {
			// Feature was disabled at compile time
			continue;
		}

		g_print("%c%s %c\n", KEY_BINDINGS_CONTEXT_SWITCH_SYMBOL, key_binding_context_names[i], KEY_BINDINGS_COMMANDS_BEGIN_SYMBOL);
		g_hash_table_foreach(key_bindings[i], help_show_key_bindings_helper, (gpointer)"");
		g_print("%c\n", KEY_BINDINGS_COMMANDS_END_SYMBOL);
	}

	setlocale(LC_NUMERIC, old_locale);
	g_free(old_locale);

	exit(0);
	return FALSE;
}/*}}}*/
void parse_key_bindings(const gchar *bindings) {/*{{{*/
	/*
	 * This is a simple state machine based parser for the same format that the help function outputs:
	 *  shortcut { command(parameter); command(parameter); }
	 *
	 * The states are
	 *  0 initial state, expecting keyboard shortcuts, context switch or EOF
	 *  1 keyboard shortcut entering started, expecting more or start of commands
	 *  2 expecting identifier inside <..>, e.g. <Mouse-1>
	 *  3 inside command list, e.g. after {. Expecting identifier of command.
	 *  4 inside command parameters, e.g. after (. Expecting parameter.
	 *  5 inside command list after state 4, same as 3 except that more commands
	 *    add to the list instead of overwriting the old binding.
	 *  6 context switch initialized, expecting identifier & open parenthesis
	 */
	GHashTable **active_key_bindings_table = &key_bindings[DEFAULT];
	enum context_t current_context = DEFAULT;

	int state = 0;
	const gchar *token_start = NULL;
	gchar *identifier;
	ptrdiff_t identifier_length;
	int keyboard_state = 0;
	unsigned int keyboard_key_value;
	key_binding_t *binding = NULL;
	int parameter_type = 0;
	const gchar *current_command_start = bindings;
	gchar *error_message = NULL;
	const gchar *scan;

	gchar *old_locale = g_strdup(setlocale(LC_NUMERIC, NULL));
	setlocale(LC_NUMERIC, "C");

	for(scan = bindings; *scan; scan++) {
		if(*scan == '\n' || *scan == '\r' || *scan == ' ' || *scan == '\t') {
			if(token_start == scan) token_start++;
			continue;
		}
		switch(state) {
			case 0: // Expecting key description
				if(current_context == DEFAULT && *scan == KEY_BINDINGS_CONTEXT_SWITCH_SYMBOL /* @ */) {
					// Expect name of a context
					token_start = scan+1;
					state = 6;
					break;
				}
				if(current_context != DEFAULT && *scan == KEY_BINDINGS_COMMANDS_END_SYMBOL /* } */) {
					current_context = DEFAULT;
					active_key_bindings_table = &key_bindings[current_context];
					state = 0;
					break;
				}

				current_command_start = scan;
				// Missing break is intentional, fall through to case 1.

			case 1: // Expecting continuation of key description or start of command
				switch(*scan) {
					case KEY_BINDINGS_KEY_TOKEN_BEGIN_SYMBOL: /* < */
						token_start = scan + 1;
						state = 2;
						break;

					case KEY_BINDINGS_COMMANDS_BEGIN_SYMBOL: /* { */
						if(state == 0) {
							error_message = g_strdup_printf("Unallowed %c before keyboard binding was given", KEY_BINDINGS_COMMANDS_END_SYMBOL);
							state = -1;
							break;
						}
						token_start = scan + 1;
						state = 3;
						break;

					default:
						if(scan[0] == '\\' && scan[1]) {
							scan++;
						}

						guint keyval = *scan;
						if(keyboard_state & GDK_SHIFT_MASK) {
							keyval = gdk_keyval_to_upper(keyval);
							keyboard_state &= ~GDK_SHIFT_MASK;
						}
						keyboard_key_value = KEY_BINDING_VALUE(0, keyboard_state, keyval);
						#define PARSE_KEY_BINDINGS_BIND(keyboard_key_value) \
							keyboard_state = 0; \
							if(!*active_key_bindings_table) { \
								*active_key_bindings_table = g_hash_table_new_full((GHashFunc)g_direct_hash, (GEqualFunc)g_direct_equal, NULL, key_binding_t_destroy_callback); \
							} \
							binding = g_hash_table_lookup(*active_key_bindings_table, GUINT_TO_POINTER(keyboard_key_value)); \
							if(!binding) { \
								binding = g_slice_new0(key_binding_t); \
								g_hash_table_insert(*active_key_bindings_table, GUINT_TO_POINTER(keyboard_key_value), binding); \
							} \
							active_key_bindings_table = &(binding->next_key_bindings);
						PARSE_KEY_BINDINGS_BIND(keyboard_key_value);
						state = 1;
				}
				break;

			case 2: // Expecting identifier identifying a special key
				// That's either Shift, Control, Alt, Mouse-%d, Mouse-Scroll-%d or gdk_keyval_name
				// Closed by `>'
				if(*scan == KEY_BINDINGS_KEY_TOKEN_END_SYMBOL) {  /* > */
					identifier_length = scan - token_start;
					if(identifier_length == 7 && g_ascii_strncasecmp(token_start, "mouse-", 6) == 0) {
						// Is Mouse-
						keyboard_key_value = KEY_BINDING_VALUE(1, keyboard_state, (token_start[6] - '0'));
						PARSE_KEY_BINDINGS_BIND(keyboard_key_value);
					}
					else if(identifier_length == 14 && g_ascii_strncasecmp(token_start, "mouse-scroll-", 13) == 0) {
						// Is Mouse-Scroll-
						keyboard_key_value = KEY_BINDING_VALUE(1, keyboard_state, ((token_start[13] - '0') << 2));
						PARSE_KEY_BINDINGS_BIND(keyboard_key_value);
					}
					else if(identifier_length == 5 && g_ascii_strncasecmp(token_start, "shift", 5) == 0) {
						keyboard_state |= GDK_SHIFT_MASK;
					}
					else if(identifier_length == 7 && g_ascii_strncasecmp(token_start, "control", 7) == 0) {
						keyboard_state |= GDK_CONTROL_MASK;
					}
					else if(identifier_length == 3 && g_ascii_strncasecmp(token_start, "alt", 3) == 0) {
						keyboard_state |= GDK_MOD1_MASK;
					}
					else {
						identifier = g_malloc(identifier_length + 1);
						memcpy(identifier, token_start, identifier_length);
						identifier[identifier_length] = 0;
						guint keyval = gdk_keyval_from_name(identifier);
						if(keyval == GDK_KEY_VoidSymbol) {
							error_message = g_strdup_printf("Failed to parse key: `%s' is not a known GDK keyval name", identifier);
							state = -1;
							break;
						}
						if(keyboard_state & GDK_SHIFT_MASK) {
							keyval = gdk_keyval_to_upper(keyval);
							keyboard_state &= ~GDK_SHIFT_MASK;
						}
						keyboard_key_value = KEY_BINDING_VALUE(0, keyboard_state, keyval);
						PARSE_KEY_BINDINGS_BIND(keyboard_key_value);
						g_free(identifier);
					}
					token_start = NULL;
					state = 1;
				}
				break;

			case 3: // Expecting command identifier, ended by `(', or closing parenthesis
			case 5: // Expecting further commands
				if(token_start == scan && *scan == KEY_BINDINGS_COMMAND_SEPARATOR_SYMBOL) {
					token_start = scan + 1;
					continue;
				}

				switch(*scan) {
					case KEY_BINDINGS_COMMAND_PARAMETER_BEGIN_SYMBOL: /* ( */
						identifier_length = scan - token_start;
						identifier = g_malloc(identifier_length + 1);
						memcpy(identifier, token_start, identifier_length);
						identifier[identifier_length] = 0;

						if(binding->action && state == 5) {
							binding->next_action = g_slice_new0(key_binding_t);
							binding = binding->next_action;
						}

						state = -1;
						unsigned int action_id = 0;
						for(const struct pqiv_action_descriptor *descriptor = pqiv_action_descriptors; descriptor->name; descriptor++) {
							if((ptrdiff_t)strlen(descriptor->name) == identifier_length && g_ascii_strncasecmp(descriptor->name, identifier, identifier_length) == 0) {
								binding->action = action_id;
								if (binding->next_action) {
									key_binding_t_destroy_callback(binding->next_action);
									binding->next_action = NULL;
								}
								parameter_type = descriptor->parameter_type;
								token_start = scan + 1;
								state = 4;
								break;
							}
							action_id++;
						}

						if(state != 4) {
							error_message = g_strdup_printf("Unknown action: `%s'", identifier);
							state = -1;
							break;
						}

						g_free(identifier);

						break;

					case KEY_BINDINGS_COMMANDS_END_SYMBOL: /* } */
						active_key_bindings_table = &key_bindings[current_context];
						binding = NULL;
						state = 0;
						break;
				}
				break;

			case 4: // Expecting action parameter, ended by `)'
				if(parameter_type == PARAMETER_CHARPTR && *scan == '\\' && scan[1]) {
					scan++;
					continue;
				}

				if(*scan == KEY_BINDINGS_COMMAND_PARAMETER_END_SYMBOL) { /* ) */
					identifier_length = scan - token_start;
					identifier = g_malloc(identifier_length + 1);
					int identifier_end, identifier_pos;
					for(identifier_end=0, identifier_pos=0; identifier_pos<identifier_length; identifier_end++, identifier_pos++) {
						if(token_start[identifier_pos] == '\\') {
							if(++identifier_pos > identifier_length) {
								break;
							}
						}
						identifier[identifier_end] = token_start[identifier_pos];
					}
					identifier[identifier_end] = 0;

					switch(parameter_type) {
						case PARAMETER_NONE:
							if(identifier_length > 0) {
								error_message = g_strdup("This function does not expect a parameter");
								state = -1;
								break;
							}
							break;
						case PARAMETER_INT:
							binding->parameter.pint = atoi(identifier);
							break;
						case PARAMETER_DOUBLE:
							binding->parameter.pdouble = atof(identifier);
							break;
						case PARAMETER_CHARPTR:
							binding->parameter.pcharptr = g_strndup(identifier, identifier_pos);
							break;
						case PARAMETER_2SHORT:
							{
								char *comma_pos = strchr(identifier, ',');
								if(comma_pos) {
									if(!strchr(comma_pos + 1, ',')) {
										*comma_pos = 0;
										for(comma_pos++; *comma_pos == '\t' || *comma_pos == '\n' || *comma_pos == ' '; comma_pos++);
										binding->parameter.p2short.p1 = (short)atoi(identifier);
										binding->parameter.p2short.p2 = (short)atoi(comma_pos);
										break;
									}
								}
								error_message = g_strdup("This function expects two parameters");
								state = -1;
							}
							break;
					}

					g_free(identifier);

					if(state == -1) {
						break;
					}

					token_start = scan + 1;
					state = 5;
				}
				break;

			case 6: /* Context switch - expect name & opening parenthesis */
				if(*scan == KEY_BINDINGS_COMMANDS_BEGIN_SYMBOL) {
					identifier_length = 0;
					const char *i;
					for(i=token_start; i<scan; i++) {
						if(*i == ' ' || *i == '\n' || *i == '\t') {
							break;
						}
						identifier_length++;
					}
					for(; i<scan; i++) {
						if(*i != ' ' && *i != '\n' && *i != '\t') {
							error_message = g_strdup("Unexpected input after context switch initializer (@..)");
							state = -1;
							break;
						}
					}
					if(state == -1) {
						break;
					}
					gboolean context_set = FALSE;
					for(int j=1; j<KEY_BINDING_CONTEXTS_COUNT; j++) {
						if(strncasecmp(token_start, key_binding_context_names[j], identifier_length) == 0) {
							current_context = j;
							active_key_bindings_table = &key_bindings[current_context];
							context_set = TRUE;
							break;
						}
					}
					if(!context_set) {
						error_message = g_strdup_printf("Invalid context name after context switch initializer: %.*s", (int)identifier_length, token_start);
						state = -1;
						break;
					}
					token_start = NULL;
					state = 0;
					break;
				}
				// Do nothing; all the handling is deferred until we find the opening parenthesis
				break;

			default:
				error_message = g_strdup("Unexpected input");
				state = -1;
				break;
		}

		if(state == -1) {
			break;
		}
	}

	setlocale(LC_NUMERIC, old_locale);
	g_free(old_locale);

	if(state != 0) {
		if(state != -1) {
			error_message = g_strdup("Unexpected end of key binding definition");
		}

		g_printerr("Failed to parse key bindings. Error in definition:\n");
		int error_pos = scan - current_command_start;
		int print_after = strlen(scan);
		if(print_after > 20) print_after = 20;
		g_printerr("%*s\n", error_pos + print_after, current_command_start);
		for(int i=0; i<error_pos; i++) g_printerr(" ");
		g_printerr("^-- here\nError was: %s\n\n", error_message);
		g_free(error_message);

		exit(1);
	}
}/*}}}*/
gboolean perform_string_action(const gchar *string_action) {/*{{{*/
	const gchar *action_name_start = NULL;
	const gchar *action_name_end = NULL;
	const gchar *parameter_start = NULL;
	const gchar *parameter_end = NULL;
	const gchar *scan = string_action;

	while(*scan == '\t' || *scan == '\n' || *scan == ' ' || *scan == ';') scan++;
	action_name_start = scan;
	if(!*scan) return TRUE;
	while(*scan && *scan != '(') scan++;
	if(*scan != '(') {
		g_printerr("Invalid command: Missing parenthesis after command specifier.\n");
		return FALSE;
	}
	action_name_end = scan - 1;
	scan++;
	while(*scan == '\t' || *scan == '\n' || *scan == ' ') scan++;
	parameter_start = scan;
	if(!*scan) {
		g_printerr("Invalid command: Missing parameter list.\n");
		return FALSE;
	}
	while(*scan && *scan != ')') {
		if(*scan == '\\' && scan[1]) {
			scan++;
		}
		scan++;
	}
	parameter_end = scan - 1;
	if(*scan != ')') {
		g_printerr("Invalid command: Missing closing parenthesis.\n");
		return FALSE;
	}
	scan++;
	while(*scan == '\t' || *scan == '\n' || *scan == ' ') scan++;

	ptrdiff_t identifier_length = action_name_end - action_name_start + 1;
	ptrdiff_t parameter_length = parameter_end - parameter_start + 1;

	gboolean command_found = FALSE;
	int action_id = 0;
	for(const struct pqiv_action_descriptor *descriptor = pqiv_action_descriptors; descriptor->name; descriptor++) {
		if((ptrdiff_t)strlen(descriptor->name) == identifier_length && g_ascii_strncasecmp(descriptor->name, action_name_start, identifier_length) == 0) {
			command_found = TRUE;

			gchar *parameter = g_malloc(parameter_length + 1);
			for(int i=0, j=0; j<parameter_length; i++, j++) {
				if(parameter_start[j] == '\\') {
					if(++j > parameter_length) {
						break;
					}
				}
				parameter[i] = parameter_start[j];
			}
			parameter[parameter_length] = 0;

			pqiv_action_parameter_t parsed_parameter;
			switch(descriptor->parameter_type) {
				case PARAMETER_NONE:
					if(parameter_length > 0) {
						g_printerr("Invalid command: This command does not expect a parameter\n");
						g_free(parameter);
						return FALSE;
					}
					parsed_parameter.pint = 0; // To calm clang
					break;

				case PARAMETER_INT:
					parsed_parameter.pint = atoi(parameter);
					break;

				case PARAMETER_DOUBLE:
					parsed_parameter.pdouble = atof(parameter);
					break;

				case PARAMETER_CHARPTR:
					parsed_parameter.pcharptr = parameter;
					break;

				case PARAMETER_2SHORT:
					{
						char *comma_pos = strchr(parameter, ',');
						if(comma_pos) {
							if(!strchr(comma_pos + 1, ',')) {
								*comma_pos = 0;
								for(comma_pos++; *comma_pos == '\t' || *comma_pos == '\n' || *comma_pos == ' '; comma_pos++);
								parsed_parameter.p2short.p1 = (short)atoi(parameter);
								parsed_parameter.p2short.p2 = (short)atoi(comma_pos);
								break;
							}
						}
						g_printerr("Invalid command: This command expects two parameters\n");
						g_free(parameter);
						return FALSE;
					}
					break;
			}

			queue_action(action_id, parsed_parameter);
			g_free(parameter);
		}
		action_id++;
	}
	if(!command_found) {
		g_printerr("Invalid command: Unknown command.\n");
		return FALSE;
	}

	if(*scan) {
		return perform_string_action(scan);
	}

	return TRUE;
}/*}}}*/
gboolean read_commands_thread_helper(gpointer command) {/*{{{*/
	perform_string_action((gchar *)command);
	g_free(command);
	return FALSE;
}/*}}}*/
gpointer read_commands_thread(gpointer user_data) {/*{{{*/
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
			gdk_threads_add_idle(read_commands_thread_helper, buffer);
		}
		g_io_channel_unref(stdin_reader);
		return NULL;
}/*}}}*/
void initialize_key_bindings() {/*{{{*/
	for(int i=0; i<KEY_BINDING_CONTEXTS_COUNT; i++) {
		key_bindings[i] = g_hash_table_new_full((GHashFunc)g_direct_hash, (GEqualFunc)g_direct_equal, NULL, key_binding_t_destroy_callback);
	}

	for(const struct default_key_bindings_struct *kb = default_key_bindings; kb->key_binding_value; kb++) {
		key_binding_t *nkb = g_slice_new(key_binding_t);
		nkb->action = kb->action;
		nkb->parameter = kb->parameter;
		nkb->next_action = NULL;
		nkb->next_key_bindings = NULL;
		g_hash_table_insert(key_bindings[kb->context], GUINT_TO_POINTER(kb->key_binding_value), nkb);
	}
}/*}}}*/
#endif
void recreate_window() {/*{{{*/
	if(!main_window_visible) {
		return;
	}
	PQIV_DISABLE_PEDANTIC
	g_signal_handlers_disconnect_by_func(main_window, G_CALLBACK(window_close_callback), NULL);
	PQIV_ENABLE_PEDANTIC
	gtk_widget_destroy(GTK_WIDGET(main_window));
	main_window = NULL;
	option_start_fullscreen = main_window_in_fullscreen;
	main_window_visible = FALSE;
	create_window();
}/*}}}*/
// }}}

#ifndef CONFIGURED_WITHOUT_INFO_TEXT
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
#endif
gpointer load_images_thread(gpointer user_data) {/*{{{*/
#ifndef CONFIGURED_WITHOUT_INFO_TEXT
	guint event_source;
	if(user_data != NULL) {
		// Use the info text updater only if this function was called in a separate
		// thread (--lazy-load option)
		event_source = gdk_threads_add_timeout(1000, load_images_thread_update_info_text, NULL);
	}
#endif

	load_images();

	gboolean tree_empty = TRUE;
	if(file_tree_valid) {
		tree_empty = bostree_node_count(file_tree) == 0;
	}

	if(!option_wait_for_images_to_appear) {
		if(tree_empty) {
			g_printerr("No images left to display.\n");
			g_idle_add((GSourceFunc)gtk_main_quit, NULL);
			return NULL;
		}

		if(option_lazy_load) {
			gdk_threads_add_idle(initialize_gui_or_quit_callback, NULL);
		}
	}

#ifndef CONFIGURED_WITHOUT_INFO_TEXT
	if(user_data != NULL) {
		g_source_remove(event_source);
		load_images_thread_update_info_text(NULL);
	}
#endif
	return NULL;
}/*}}}*/
gboolean inner_main(void *user_data) {/*{{{*/
	if(option_lazy_load) {
		if(option_allow_empty_window) {
			create_window();
			gtk_widget_show_all(GTK_WIDGET(main_window));
			main_window_visible = TRUE;
		}

		g_thread_new("image-loader", load_images_thread, GINT_TO_POINTER(1));
	}
	else {
		load_images_thread(NULL);
		if(!initialize_gui()) {
			g_printerr("No images left to display.\n");
			gtk_main_quit();
		}
	}

#ifndef CONFIGURED_WITHOUT_ACTIONS
	if(option_actions_from_stdin) {
		g_thread_new("command-reader", read_commands_thread, NULL);
	}
#endif

	return FALSE;
}/*}}}*/
int main(int argc, char *argv[]) {
	#ifdef DEBUG
		#ifndef _WIN32
			struct rlimit core_limits;
			core_limits.rlim_cur = core_limits.rlim_max = RLIM_INFINITY;
			setrlimit(RLIMIT_CORE, &core_limits);
		#endif
	#endif

	#if defined(GDK_WINDOWING_X11)
		XInitThreads();
	#endif
	#if (!GLIB_CHECK_VERSION(2, 32, 0))
		g_thread_init(NULL);
		gdk_threads_init();
	#endif
	gboolean windowing_available = gtk_init_check(&argc, &argv); // fyi, this generates a MemorySanitizer warning currently

#ifndef CONFIGURED_WITHOUT_ACTIONS
	initialize_key_bindings();
#endif

	global_argc = argc;
	global_argv = argv;

	parse_configuration_file();
	parse_command_line();

	if(!windowing_available) {
		g_warn_if_reached(); // this should never be called because parse_command_line() calls gtk_init() again.
		return 1;
	}

	if(option_disable_backends) {
		gchar **disabled_backends = g_strsplit(option_disable_backends, ",", 0);
		initialize_file_type_handlers((const gchar * const *)disabled_backends);
		g_strfreev(disabled_backends);
	}
	else {
		const gchar * disabled_backends[] = { NULL };
		initialize_file_type_handlers(disabled_backends);
	}

	if(fabs(option_initial_scale - 1.0) > 2 * FLT_MIN) {
		option_scale = FIXED_SCALE;
		current_scale_level = option_initial_scale;
	}
	cairo_matrix_init_identity(&current_transformation);

	if(option_fading_duration > option_slideshow_interval) {
		g_printerr("Warning: Fade durations larger than the slideslow interval won't work as expected.\n");
	}

#ifndef CONFIGURED_WITHOUT_ACTIONS
	if(option_actions_from_stdin && global_argc == 1 && !option_wait_for_images_to_appear) {
		g_printerr("Warning: --actions-from-stdin with no files given implies --wait-for-images-to-appear.\n");
		option_wait_for_images_to_appear = TRUE;
	}

	if(option_actions_from_stdin && option_addl_from_stdin) {
			g_printerr("Error: --additional-from-stdin conflicts with --actions-from-stdin.\n");
			exit(1);
	}
#endif

	if(option_wait_for_images_to_appear) {
		if(!option_watch_directories) {
			g_printerr("Warning: --wait-for-images-to-appear implies --watch-directories.\n");
			option_watch_directories = TRUE;
		}
		if(!option_lazy_load) {
			g_printerr("Warning: --wait-for-images-to-appear implies --lazy-load.\n");
			option_lazy_load = TRUE;
		}
	}

	// Start image loader & show window inside main loop, in order to have
	// gtk_main_quit() available.
	gdk_threads_add_idle(inner_main, NULL);

	gtk_main();

	// We are outside of the main loop again, so we can unload the remaining images
	// We need to do this, because some file types create temporary files
	//
	// Note: If we locked the file_tree here, unload_image() could dead-lock
	// (The wand backend has a global mutex and calls a function that locks file_tree)
	// Instead, accept that in the worst case, some images might not be unloaded properly.
	// At least, after file_tree_valid = FALSE, no new images will be inserted.
	file_tree_valid = FALSE;
	D_LOCK(file_tree);
	abort_pending_image_loads(NULL);
	D_UNLOCK(file_tree);
	for(BOSNode *node = bostree_select(file_tree, 0); node; node = bostree_next_node(node)) {
		// Iterate over the images ourselves, because there might be open weak references which
		// prevent this to be called from bostree_destroy.
		unload_image(node);
	}
	D_LOCK(file_tree);
	// Note: This still won't free all the data, because we hold weak
	// references. But that doesn't matter, the unloading is the important
	// thing as it removes any temporary files.
	bostree_destroy(file_tree);
	D_UNLOCK(file_tree);

	return 0;
}

// vim:noet ts=4 sw=4 tw=0 fdm=marker
