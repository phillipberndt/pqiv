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

// This file contains the definition of files, image types and
// the plugin infrastructure. It should be included in file type
// handlers.

#ifndef _PQIV_H_INCLUDED
#define _PQIV_H_INCLUDED

#include <glib.h>
#include <gtk/gtk.h>
#include <gio/gio.h>
#include "lib/bostree.h"

#ifndef PQIV_VERSION
#define PQIV_VERSION "2.10.4"
#endif

#define FILE_FLAGS_ANIMATION      (guint)(1)
#define FILE_FLAGS_MEMORY_IMAGE   (guint)(1<<1)

#define FALSE_POINTER ((void*)-1)

// The structure for images {{{
typedef struct _file file_t;
typedef GBytes *(*file_data_loader_fn_t)(file_t *file, GError **error_pointer);

typedef struct file_type_handler_struct_t file_type_handler_t;
struct _file {
	// File type
	const file_type_handler_t *file_type;

	// Special flags
	// FILE_FLAGS_ANIMATION        -> Animation functions are invoked
	//                                Set by file type handlers
	// FILE_FLAGS_MEMORY_IMAGE     -> File lives in memory
	guint file_flags;

	// The file name to display
	// Must be different from file_name, because it is free()d seperately
	gchar *display_name;

	// The name to sort by
	// Must be set if option_sort is set; in backends the simplest approach
	// is to only touch this if it is not NULL
	gchar *sort_name;

	// The URI or file name of the file
	gchar *file_name;

	// If the file is a memory image, the actual image data _or_ data for the
	// file_data_loader callback to use to construct the _actual_ bytes object
	// to use.
	GBytes *file_data;

	// If the image is a memory image that can be generated at load time,
	// store a pointer to the generator.
	file_data_loader_fn_t file_data_loader;

	// The file monitor structure is used for inotify-watching of
	// the files
	GFileMonitor *file_monitor;

	// This flag stores whether this image is currently loaded
	// and valid. i.e. if it is set, you can assume that
	// private_data contains a representation of the image;
	// if not, you can NOT assume that it does not.
	gboolean is_loaded;

	// This flag determines whether this file should be reloaded
	// despite is_loaded being set
	gboolean force_reload;

	// Cached image size
	guint width;
	guint height;

#ifndef CONFIGURED_WITHOUT_MONTAGE_MODE
	// Cached thumbnail
	cairo_surface_t *thumbnail;
#endif

	// Lock to prevent multiple threads from accessing the backend at the same
	// time
	GMutex lock;

	// Default render, automatically unloaded with the image, not guaranteed to
	// be present, not guaranteed to have the correct scale level.
	cairo_surface_t *prerendered_view;

	// File-type specific data, allocated and freed by the file type handlers
	void *private;
};
// }}}
// Definition of the built-in file types {{{

// If you want to implement your own file type, you'll have to implement the
// following functions and a non-static initialization function named
// file_type_NAME_initializer that fills a file_type_handler_t with pointers to
// the functions. Store the file in backends/NAME.c and adjust the Makefile to
// add the required libraries if your backend is listed in the $(BACKENDS)
// variable.

typedef enum { PARAMETER, RECURSION, INOTIFY, BROWSE_ORIGINAL_PARAMETER, FILTER_OUTPUT } load_images_state_t;

// Allocation function: Allocate the ->private structure within a file and add the
// image(s) to the list of available images via load_images_handle_parameter_add_file()
// If an image is not to be loaded for any reason, the file structure should be
// deallocated using file_free()
// Returns a pointer to the first added image
// Optional, you can also set the pointer to this function to NULL.
// If new file_t structures are needed, use image_loader_duplicate_file
typedef BOSNode *(*file_type_alloc_fn_t)(load_images_state_t state, file_t *file);

// Deallocation, if a file is removed from the images list. Free the ->private structure.
// Only called if ->private is non-NULL.
typedef void (*file_type_free_fn_t)(file_t *file);

// Actually load a file into memory
typedef void (*file_type_load_fn_t)(file_t *file, GInputStream *data, GError **error_pointer);

// Unload a file
typedef void (*file_type_unload_fn_t)(file_t *file);

// Animation support: Initialize memory for animations, return ms until first frame
// Optional, you can also set the pointer to this function to NULL.
typedef double (*file_type_animation_initialize_fn_t)(file_t *file);

// Animation support: Advance to the next frame, return ms until next frame
// Optional, you can also set the pointer to this function to NULL.
typedef double (*file_type_animation_next_frame_fn_t)(file_t *file);

// Draw the current view to a cairo context
typedef void (*file_type_draw_fn_t)(file_t *file, cairo_t *cr);

struct file_type_handler_struct_t {
	// All files will be filtered with this filter. If it lets it pass,
	// a handler is assigned to a file. If none do, the file is
	// discarded if it was found during directory traversal or
	// loaded using the first image backend if it was an explicit
	// parameter.
	GtkFileFilter *file_types_handled;

	// Pointers to the functions defined above
	file_type_alloc_fn_t alloc_fn;
	file_type_free_fn_t free_fn;
	file_type_load_fn_t load_fn;
	file_type_unload_fn_t unload_fn;
	file_type_animation_initialize_fn_t animation_initialize_fn;
	file_type_animation_next_frame_fn_t animation_next_frame_fn;
	file_type_draw_fn_t draw_fn;
};

// Initialization function: Tell pqiv about a backend
typedef void (*file_type_initializer_fn_t)(file_type_handler_t *info);

// pqiv symbols available to plugins {{{

// Global cancellable that should be used for every i/o operation
extern GCancellable *image_loader_cancellable;

// Current scale level. For backends that don't support cairo natively.
extern gdouble current_scale_level;

// Load a file from disc/memory/network
GInputStream *image_loader_stream_file(file_t *file, GError **error_pointer);

// Create a GFile for a file's name (We have a wrapper to support names with colons)
GFile *gfile_for_commandline_arg(const char *parameter);

// Duplicate a file_t; the private section does not get duplicated, only the pointer gets copied
file_t *image_loader_duplicate_file(file_t *file, gchar *custom_file_name, gchar *custom_display_name, gchar *custom_sort_name);

// Add a file to the list of loaded files
// Should be called at least once in a file_type_alloc_fn_t, with the state being
// forwarded unaltered.
BOSNode *load_images_handle_parameter_add_file(load_images_state_t state, file_t *file);

// Find a handler for a given file; useful for handler redirection, see archive
// file type
BOSNode *load_images_handle_parameter_find_handler(const char *param, load_images_state_t state, file_t *file, GtkFileFilterInfo *file_filter_info);

// Load all data from an input stream into memory, conveinience function
GBytes *g_input_stream_read_completely(GInputStream *input_stream, GCancellable *cancellable, GError **error_pointer);

// Free a file
void file_free(file_t *file);

// Set the interpolation filter in a cairo context for the current file based on the user settings
void apply_interpolation_quality(cairo_t *cr);

// Wrapper for string vector contains function
gboolean strv_contains(const gchar * const *strv, const gchar *str);

// }}}

// File type handlers, used in the initializer and file type guessing
extern file_type_handler_t file_type_handlers[];

/* }}} */

// The means to control pqiv remotely {{{
typedef enum {
	ACTION_NOP,
	ACTION_SHIFT_Y,
	ACTION_SHIFT_X,
	ACTION_SET_SLIDESHOW_INTERVAL_RELATIVE,
	ACTION_SET_SLIDESHOW_INTERVAL_ABSOLUTE,
	ACTION_SET_SCALE_LEVEL_RELATIVE,
	ACTION_SET_SCALE_LEVEL_ABSOLUTE,
	ACTION_TOGGLE_SCALE_MODE,
	ACTION_SET_SCALE_MODE_SCREEN_FRACTION,
	ACTION_TOGGLE_SHUFFLE_MODE,
	ACTION_RELOAD,
	ACTION_RESET_SCALE_LEVEL,
	ACTION_TOGGLE_FULLSCREEN,
	ACTION_FLIP_HORIZONTALLY,
	ACTION_FLIP_VERTICALLY,
	ACTION_ROTATE_LEFT,
	ACTION_ROTATE_RIGHT,
	ACTION_TOGGLE_INFO_BOX,
	ACTION_JUMP_DIALOG,
	ACTION_TOGGLE_SLIDESHOW,
	ACTION_HARDLINK_CURRENT_IMAGE,
	ACTION_GOTO_DIRECTORY_RELATIVE,
	ACTION_GOTO_LOGICAL_DIRECTORY_RELATIVE,
	ACTION_GOTO_FILE_RELATIVE,
	ACTION_QUIT,
	ACTION_NUMERIC_COMMAND,
	ACTION_COMMAND,
	ACTION_ADD_FILE,
	ACTION_GOTO_FILE_BYINDEX,
	ACTION_GOTO_FILE_BYNAME,
	ACTION_REMOVE_FILE_BYINDEX,
	ACTION_REMOVE_FILE_BYNAME,
	ACTION_OUTPUT_FILE_LIST,
	ACTION_SET_CURSOR_VISIBILITY,
	ACTION_SET_STATUS_OUTPUT,
	ACTION_SET_SCALE_MODE_FIT_PX,
	ACTION_SET_SHIFT_X,
	ACTION_SET_SHIFT_Y,
	ACTION_BIND_KEY,
	ACTION_SEND_KEYS,
	ACTION_SET_SHIFT_ALIGN_CORNER,
	ACTION_SET_INTERPOLATION_QUALITY,
	ACTION_ANIMATION_STEP,
	ACTION_ANIMATION_CONTINUE,
	ACTION_ANIMATION_SET_SPEED_ABSOLUTE,
	ACTION_ANIMATION_SET_SPEED_RELATIVE,
	ACTION_GOTO_EARLIER_FILE,
	ACTION_SET_CURSOR_AUTO_HIDE,
	ACTION_SET_FADE_DURATION,
	ACTION_SET_KEYBOARD_TIMEOUT,
	ACTION_SET_THUMBNAIL_SIZE,
	ACTION_SET_THUMBNAIL_PRELOAD,
	ACTION_MONTAGE_MODE_ENTER,
	ACTION_MONTAGE_MODE_SHIFT_X,
	ACTION_MONTAGE_MODE_SHIFT_Y,
	ACTION_MONTAGE_MODE_SET_SHIFT_X,
	ACTION_MONTAGE_MODE_SET_SHIFT_Y,
	ACTION_MONTAGE_MODE_SET_WRAP_MODE,
	ACTION_MONTAGE_MODE_SHIFT_Y_PG,
	ACTION_MONTAGE_MODE_SHIFT_Y_ROWS,
	ACTION_MONTAGE_MODE_SHOW_BINDING_OVERLAYS,
	ACTION_MONTAGE_MODE_FOLLOW,
	ACTION_MONTAGE_MODE_FOLLOW_PROCEED,
	ACTION_MONTAGE_MODE_RETURN_PROCEED,
	ACTION_MONTAGE_MODE_RETURN_CANCEL,
	ACTION_MOVE_WINDOW,
	ACTION_TOGGLE_BACKGROUND_PATTERN,
	ACTION_TOGGLE_NEGATE_MODE,
} pqiv_action_t;

typedef union {
	int pint;
	double pdouble;
	char *pcharptr;
	struct {
		short p1;
		short p2;
	} p2short;
} pqiv_action_parameter_t;
void action(pqiv_action_t action, pqiv_action_parameter_t parameter);
// }}}

#endif
