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
 *
 * ImageMagick wand backend
 *
 */

#include "../pqiv.h"
#include "../lib/filebuffer.h"
#include <stdint.h>
#include <string.h>
#include <strings.h>


#if __clang__
	// ImageMagick does throw some clang warnings
	#pragma clang diagnostic push
	#pragma clang diagnostic ignored "-Wunused-variable"
	#pragma clang diagnostic ignored "-Wunknown-attributes"
	#pragma clang diagnostic ignored "-Wkeyword-macro"
#endif

#if defined(WAND_VERSION) && WAND_VERSION > 6
	#include <MagickWand/MagickWand.h>
#else
	#include <wand/MagickWand.h>
#endif

#if __clang__
	#pragma clang diagnostic pop
#endif

#include <cairo/cairo.h>

// ImageMagick's multithreading is broken. To test this, open a multi-page
// postscript document using this backend without --low-memory and then quit
// pqiv. The backend will freeze in MagickWandTerminus() while waiting for
// a Mutex. We must do this call to allow ImageMagick to delete temporary
// files created using postscript processing (in /tmp usually).
//
// The only way around this, sadly, is to use a global mutex around all
// ImageMagick calls.
G_LOCK_DEFINE_STATIC(magick_wand_global_lock);

typedef struct {
	MagickWand *wand;
	cairo_surface_t *rendered_image_surface;

	// Starting from 1 for numbered files, 0 for unpaginated files
	unsigned int page_number;
} file_private_data_wand_t;

// Check if a (named) file has a certain extension. Used for psd fix and multi-page detection (ps, pdf, ..)
static gboolean file_type_wand_has_extension(file_t *file, const char *extension) {
	char *actual_extension;
	return (!(file->file_flags & FILE_FLAGS_MEMORY_IMAGE) && file->file_name && (actual_extension = strrchr(file->file_name, '.')) && strcasecmp(actual_extension, extension) == 0);
}

// Functions to render the Magick backend to a cairo surface via in-memory PNG export
cairo_status_t file_type_wand_read_data(void *closure, unsigned char *data, unsigned int length) {/*{{{*/
	unsigned char **pos = closure;
	memcpy(data, *pos, length);
	*pos += length;
	return CAIRO_STATUS_SUCCESS;
}/*}}}*/
void file_type_wand_update_image_surface(file_t *file) {/*{{{*/
	file_private_data_wand_t *private = file->private;

	if(private->rendered_image_surface) {
		cairo_surface_destroy(private->rendered_image_surface);
		private->rendered_image_surface = NULL;
	}

	MagickSetImageFormat(private->wand, "PNG32");

	size_t image_size;
	unsigned char *image_data = MagickGetImageBlob(private->wand, &image_size);
	unsigned char *image_data_loc = image_data;

	private->rendered_image_surface = cairo_image_surface_create_from_png_stream(file_type_wand_read_data, &image_data_loc);

	MagickRelinquishMemory(image_data);
}/*}}}*/

BOSNode *file_type_wand_alloc(load_images_state_t state, file_t *file) {/*{{{*/
	G_LOCK(magick_wand_global_lock);

	if(file_type_wand_has_extension(file, ".pdf") || file_type_wand_has_extension(file, ".ps")) {
		// Multi-page document. Load number of pages and create one file_t per page
		GError *error_pointer = NULL;
		MagickWand *wand = NewMagickWand();
		GBytes *image_bytes = buffered_file_as_bytes(file, NULL, &error_pointer);
		if(!image_bytes) {
			g_printerr("Failed to read image %s: %s\n", file->file_name, error_pointer->message);
			g_clear_error(&error_pointer);
			G_UNLOCK(magick_wand_global_lock);
			file_free(file);
			return FALSE_POINTER;
		}
		size_t image_size;
		const gchar *image_data = g_bytes_get_data(image_bytes, &image_size);
		MagickBooleanType success = MagickReadImageBlob(wand, image_data, image_size);
		if(success == MagickFalse) {
			ExceptionType severity;
			char *message = MagickGetException(wand, &severity);
			g_printerr("Failed to read image %s: %s\n", file->file_name, message);
			MagickRelinquishMemory(message);
			DestroyMagickWand(wand);
			buffered_file_unref(file);
			G_UNLOCK(magick_wand_global_lock);
			file_free(file);
			return FALSE_POINTER;
		}

		int n_pages = MagickGetNumberImages(wand);
		DestroyMagickWand(wand);
		buffered_file_unref(file);

		BOSNode *first_node = FALSE_POINTER;
		for(int n=0; n<n_pages; n++) {
			file_t *new_file = image_loader_duplicate_file(file,
					NULL,
					n == 0 ? NULL :  g_strdup_printf("%s[%d]", file->display_name, n + 1),
					g_strdup_printf("%s[%d]", file->sort_name, n + 1));
			new_file->private = g_slice_new0(file_private_data_wand_t);
			((file_private_data_wand_t *)new_file->private)->page_number = n + 1;

			// Temporarily give up lock to do this: Otherwise we might see a deadlock
			// if another thread holding the file tree's lock is waiting for the wand
			// lock for another operation.
			G_UNLOCK(magick_wand_global_lock);
			if(n == 0) {
				first_node = load_images_handle_parameter_add_file(state, new_file);
			}
			else {
				load_images_handle_parameter_add_file(state, new_file);
			}
			G_LOCK(magick_wand_global_lock);
		}

		if(first_node) {
			file_free(file);
		}
		G_UNLOCK(magick_wand_global_lock);
		return first_node;
	}
	else {
		// Simple image
		file->private = g_slice_new0(file_private_data_wand_t);
		BOSNode *first_node = load_images_handle_parameter_add_file(state, file);
		G_UNLOCK(magick_wand_global_lock);
		return first_node;
	}
}/*}}}*/
void file_type_wand_free(file_t *file) {/*{{{*/
	g_slice_free(file_private_data_wand_t, file->private);
}/*}}}*/
void file_type_wand_load(file_t *file, GInputStream *data, GError **error_pointer) {/*{{{*/
	G_LOCK(magick_wand_global_lock);
	file_private_data_wand_t *private = file->private;

	private->wand = NewMagickWand();
	gsize image_size;
	GBytes *image_bytes = buffered_file_as_bytes(file, data, error_pointer);
	if(!image_bytes) {
		G_UNLOCK(magick_wand_global_lock);
		return;
	}
	const gchar *image_data = g_bytes_get_data(image_bytes, &image_size);
	MagickBooleanType success = MagickReadImageBlob(private->wand, image_data, image_size);

	if(success == MagickFalse) {
		ExceptionType severity;
		char *message = MagickGetException(private->wand, &severity);
		*error_pointer = g_error_new(g_quark_from_static_string("pqiv-wand-error"), 1, "Failed to load image %s: %s", file->file_name, message);
		MagickRelinquishMemory(message);
		DestroyMagickWand(private->wand);
		private->wand = NULL;
		buffered_file_unref(file);
		G_UNLOCK(magick_wand_global_lock);
		return;
	}

	MagickResetIterator(private->wand);
	if(private->page_number > 0) {
		// PDF/PS files are displayed one page per file_t
		MagickSetIteratorIndex(private->wand, private->page_number - 1);
	}
	else {
		// Other files are either interpreted as animated (if they have a delay
		// set) or merged down to one image (interpreted as layered, as in
		// PSD/XCF files)
		size_t delay = MagickGetImageDelay(private->wand);
		if(delay) {
			MagickWand *wand = MagickCoalesceImages(private->wand);
			DestroyMagickWand(private->wand);
			private->wand = wand;
			MagickResetIterator(wand);

			file->file_flags |= FILE_FLAGS_ANIMATION;
		}
		else if(MagickGetNumberImages(private->wand) > 1) {
			// Merge multi-page files.
			// This doesn't work as expected for .psd files. As a hack, disable
			// it for them.
			// TODO Check periodically if the problem still persists (heavily distorted images) and remove this once it has been solved
			if(!file_type_wand_has_extension(file, ".psd")) {
				MagickWand *wand = MagickMergeImageLayers(private->wand, FlattenLayer);
				DestroyMagickWand(private->wand);
				private->wand = wand;
				MagickResetIterator(private->wand);
			}
		}
		MagickNextImage(private->wand);
	}
	file_type_wand_update_image_surface(file);

	file->width = MagickGetImageWidth(private->wand);
	file->height = MagickGetImageHeight(private->wand);
	file->is_loaded = TRUE;
	G_UNLOCK(magick_wand_global_lock);
}/*}}}*/
double file_type_wand_animation_initialize(file_t *file) {/*{{{*/
	file_private_data_wand_t *private = file->private;
	// The unit of MagickGetImageDelay is "ticks-per-second"
	return 1000. / MagickGetImageDelay(private->wand);
}/*}}}*/
double file_type_wand_animation_next_frame(file_t *file) {/*{{{*/
	// ImageMagick tends to be really slow when it comes to loading frames.
	// We therefore measure the required time and subtract it from the time
	// pqiv waits before loading the next frame:
	G_LOCK(magick_wand_global_lock);
	gint64 begin_time = g_get_monotonic_time();

	file_private_data_wand_t *private = file->private;

	MagickBooleanType status = MagickNextImage(private->wand);
	if(status == MagickFalse) {
		MagickResetIterator(private->wand);
		MagickNextImage(private->wand);
	}
	file_type_wand_update_image_surface(file);

	gint64 required_time = (g_get_monotonic_time() - begin_time) / 1000;
	gint pause = 1000. / MagickGetImageDelay(private->wand);

	G_UNLOCK(magick_wand_global_lock);

	return pause + 1 > required_time ? pause - required_time : 1;
}/*}}}*/
void file_type_wand_unload(file_t *file) {/*{{{*/
	G_LOCK(magick_wand_global_lock);
	file_private_data_wand_t *private = file->private;

	if(private->rendered_image_surface) {
		cairo_surface_destroy(private->rendered_image_surface);
		private->rendered_image_surface = NULL;
	}

	if(private->wand) {
		DestroyMagickWand(private->wand);
		private->wand = NULL;

		buffered_file_unref(file);
	}
	G_UNLOCK(magick_wand_global_lock);
}/*}}}*/
void file_type_wand_draw(file_t *file, cairo_t *cr) {/*{{{*/
	file_private_data_wand_t *private = file->private;

	if(private->rendered_image_surface) {
		if(private->page_number > 0) {
			// Is multi-page document. Draw white background.
			cairo_set_source_rgb(cr, 1., 1., 1.);
			cairo_paint(cr);
			cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
		}
		cairo_set_source_surface(cr, private->rendered_image_surface, 0, 0);
		apply_interpolation_quality(cr);
		cairo_paint(cr);
	}
}/*}}}*/

static void file_type_wand_exit_handler() {/*{{{*/
	G_LOCK(magick_wand_global_lock);
	MagickWandTerminus();
	G_UNLOCK(magick_wand_global_lock);
}/*}}}*/

void file_type_wand_initializer(file_type_handler_t *info) {/*{{{*/
	// Fill the file filter pattern
	MagickWandGenesis();
	info->file_types_handled = gtk_file_filter_new();
	size_t count, i;
	char **formats = MagickQueryFormats("*", &count);
	for(i=0; i<count; i++) {
		// Skip some broken formats
		const char disabled_extensions[] =
			"DJVU\0"        // DJVU crashes my development PC
			"BIN\0"         // BIN is not necessarily an image; skip those files.
			"TXT\0"         // Ridiculous formats for an image viewer
			"HTML\0"
			"HTM\0"
			"SHTML\0"
			"MAT\0\0";
		int skip = 0;
		for(const char *extension = disabled_extensions; *extension; extension = strchr(extension, '\0') + 1) {
			if((skip = (strcmp(formats[i], extension) == 0))) {
				break;
			}
		}
		if(skip) continue;
		if(formats[i][0] != 0 && formats[i][1] == 0) continue; // One letter extensions are too random to be sure it's an image,
		                                                       // and hence they are raw format's would always succeed to load

		gchar *ext = g_ascii_strdown(formats[i], -1);
		gchar *format = g_strdup_printf("*.%s", ext);
		g_free(ext);
		gtk_file_filter_add_pattern(info->file_types_handled, format);
		g_free(format);
	}
	MagickRelinquishMemory(formats);

	// We need to register MagickWandTerminus(), imageMagick's exit handler, to
	// cleanup temporary files when pqiv exits.
	atexit(file_type_wand_exit_handler);

	// Magick Wand does not give us MIME types. Manually add the most interesting one:
	gtk_file_filter_add_mime_type(info->file_types_handled, "image/vnd.adobe.photoshop");

	// Assign the handlers
	info->alloc_fn                 =  file_type_wand_alloc;
	info->free_fn                  =  file_type_wand_free;
	info->load_fn                  =  file_type_wand_load;
	info->unload_fn                =  file_type_wand_unload;
	info->draw_fn                  =  file_type_wand_draw;
	info->animation_initialize_fn  =  file_type_wand_animation_initialize;
	info->animation_next_frame_fn  =  file_type_wand_animation_next_frame;
}/*}}}*/
