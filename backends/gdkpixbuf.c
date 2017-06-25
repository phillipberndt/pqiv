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
 * gdk-pixbuf backend
 */

#include "../pqiv.h"
#include <math.h>

/* Default (GdkPixbuf) file type implementation {{{ */
typedef struct {
	// The surface where the image is stored. Only non-NULL for
	// the current, previous and next image.
	cairo_surface_t *image_surface;

	// For file_type & FILE_FLAGS_ANIMATION, this stores the
	// whole animation. As with the surface, this is only non-NULL
	// for the current, previous and next image.
	GdkPixbufAnimation *pixbuf_animation;
	GdkPixbufAnimationIter *animation_iter;
	GTimeVal animation_time;
} file_private_data_gdkpixbuf_t;

BOSNode *file_type_gdkpixbuf_alloc(load_images_state_t state, file_t *file) {/*{{{*/
	file->private = (void *)g_slice_new0(file_private_data_gdkpixbuf_t);
	return load_images_handle_parameter_add_file(state, file);
}/*}}}*/
void file_type_gdkpixbuf_free(file_t *file) {/*{{{*/
	g_slice_free(file_private_data_gdkpixbuf_t, file->private);
}/*}}}*/
void file_type_gdkpixbuf_unload(file_t *file) {/*{{{*/
	file_private_data_gdkpixbuf_t *private = file->private;
	if(private->pixbuf_animation != NULL) {
		g_object_unref(private->pixbuf_animation);
		private->pixbuf_animation = NULL;
	}
	if(private->image_surface != NULL) {
		cairo_surface_destroy(private->image_surface);
		private->image_surface = NULL;
	}
	if(private->animation_iter != NULL) {
		g_object_unref(private->animation_iter);
		private->animation_iter = NULL;
	}
}/*}}}*/
double file_type_gdkpixbuf_animation_initialize(file_t *file) {/*{{{*/
	file_private_data_gdkpixbuf_t *private = file->private;
	if(private->animation_iter == NULL) {
		private->animation_iter = gdk_pixbuf_animation_get_iter(private->pixbuf_animation, &private->animation_time);
	}
	return gdk_pixbuf_animation_iter_get_delay_time(private->animation_iter);
}/*}}}*/
double file_type_gdkpixbuf_animation_next_frame(file_t *file) {/*{{{*/
	file_private_data_gdkpixbuf_t *private = (file_private_data_gdkpixbuf_t *)file->private;

	cairo_surface_t *surface = cairo_surface_reference(private->image_surface);

	// We keep track of time manually to allow the user to adjust the playback speed:
	// It is assumed that this function is called exactly at the right time, each time.
	// TODO The downside from this is that animations won't play smoothly on slow X11 connections.
	// Maybe I should extend the API to allow to switch between auto and manual time?
	int millis_until_next = gdk_pixbuf_animation_iter_get_delay_time(private->animation_iter);
	if(millis_until_next > 0) {
		private->animation_time.tv_usec += millis_until_next * 1000;
		if(private->animation_time.tv_usec >= 1000000) {
			private->animation_time.tv_sec += private->animation_time.tv_usec / 1000000;
			private->animation_time.tv_usec %= 1000000;
		}
	}

	gdk_pixbuf_animation_iter_advance(private->animation_iter, &private->animation_time);
	GdkPixbuf *pixbuf = gdk_pixbuf_animation_iter_get_pixbuf(private->animation_iter);

	cairo_t *sf_cr = cairo_create(surface);
	cairo_save(sf_cr);
	cairo_set_source_rgba(sf_cr, 0., 0., 0., 0.);
	cairo_set_operator(sf_cr, CAIRO_OPERATOR_SOURCE);
	cairo_paint(sf_cr);
	cairo_restore(sf_cr);
	gdk_cairo_set_source_pixbuf(sf_cr, pixbuf, 0, 0);
	cairo_paint(sf_cr);
	cairo_destroy(sf_cr);

	cairo_surface_destroy(surface);

	return gdk_pixbuf_animation_iter_get_delay_time(private->animation_iter);
}/*}}}*/
gboolean file_type_gdkpixbuf_load_destroy_old_image_callback(gpointer old_surface) {/*{{{*/
	cairo_surface_destroy((cairo_surface_t *)old_surface);
	return FALSE;
}/*}}}*/
void file_type_gdkpixbuf_load(file_t *file, GInputStream *data, GError **error_pointer) {/*{{{*/
	file_private_data_gdkpixbuf_t *private = (file_private_data_gdkpixbuf_t *)file->private;
	GdkPixbufAnimation *pixbuf_animation = NULL;

	#if (GDK_PIXBUF_MAJOR > 2 || (GDK_PIXBUF_MAJOR == 2 && GDK_PIXBUF_MINOR >= 28))
		pixbuf_animation = gdk_pixbuf_animation_new_from_stream(data, image_loader_cancellable, error_pointer);
	#else
		#define IMAGE_LOADER_BUFFER_SIZE (1024 * 512)

		GdkPixbufLoader *loader = gdk_pixbuf_loader_new();
		guchar *buffer = g_malloc(IMAGE_LOADER_BUFFER_SIZE);
		while(TRUE) {
			gssize bytes_read = g_input_stream_read(data, buffer, IMAGE_LOADER_BUFFER_SIZE, image_loader_cancellable, error_pointer);
			if(bytes_read == 0) {
				// All OK, finish the image loader
				gdk_pixbuf_loader_close(loader, error_pointer);
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
			if(!gdk_pixbuf_loader_write(loader, buffer, bytes_read, error_pointer)) {
				// In case of an error, abort.
				break;
			}
		}
		g_free(buffer);
		g_object_unref(loader);
	#endif

	if(pixbuf_animation == NULL) {
		return;
	}

	if(!gdk_pixbuf_animation_is_static_image(pixbuf_animation)) {
		if(private->pixbuf_animation != NULL) {
			g_object_unref(private->pixbuf_animation);
		}
		private->pixbuf_animation = g_object_ref(pixbuf_animation);
		file->file_flags |= FILE_FLAGS_ANIMATION;
	}
	else {
		file->file_flags &= ~FILE_FLAGS_ANIMATION;
	}

	GdkPixbuf *pixbuf = g_object_ref(gdk_pixbuf_animation_get_static_image(pixbuf_animation));
	g_object_unref(pixbuf_animation);

	if(pixbuf != NULL) {
		GdkPixbuf *new_pixbuf = gdk_pixbuf_apply_embedded_orientation(pixbuf);
		g_object_unref(pixbuf);
		pixbuf = new_pixbuf;

		// This should never happen and is only here as a security measure
		// (glib will abort() if malloc() fails and nothing else can happen here)
		if(pixbuf == NULL) {
			return;
		}

		file->width = gdk_pixbuf_get_width(pixbuf);
		file->height = gdk_pixbuf_get_height(pixbuf);

		// Cairo cannot handle files larger than 32767x32767
		// See https://lists.freedesktop.org/archives/cairo/2009-August/017881.html
		// But actually, we might have to use a lower limit in case we are out of memory.
		double cairo_image_dimensions_limit = 30000.;

		cairo_surface_t *surface = NULL;
		do {
			if(file->width > cairo_image_dimensions_limit || file->height > cairo_image_dimensions_limit) {
				double loading_scale_factor = 1.;
				loading_scale_factor = fmin(cairo_image_dimensions_limit / file->width, cairo_image_dimensions_limit / file->height);
				file->width *= loading_scale_factor;
				file->height *= loading_scale_factor;
				g_printerr("Warning: Resizing file %s down to %dx%d due to Cairo's image size limit / insufficient memory.\n",
						file->display_name, file->width, file->height);

				new_pixbuf = gdk_pixbuf_scale_simple(new_pixbuf, file->width, file->height, GDK_INTERP_BILINEAR);
				if(!new_pixbuf) {
					if(cairo_image_dimensions_limit > 10000) {
						cairo_image_dimensions_limit -= 10000;
						continue;
					}
					g_object_unref(pixbuf);
					*error_pointer = g_error_new(g_quark_from_static_string("pqiv-pixbuf-error"), 1, "Failed to allocate memory for the resized image.\n");
					return;
				}
				else {
					g_object_unref(pixbuf);
					pixbuf = new_pixbuf;
				}
			}

			#if 0 && (GDK_MAJOR_VERSION == 3 && GDK_MINOR_VERSION >= 10) || (GDK_MAJOR_VERSION > 3)
				// This function has a bug, see
				// https://bugzilla.gnome.org/show_bug.cgi?id=736624
				// We therefore have to use the below version even if this function is available.
				surface = gdk_cairo_surface_create_from_pixbuf(pixbuf, 1., NULL);
				// TODO Once this works, manually check if surface failed with "out of memory".
			#else
				surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, file->width, file->height);
				if(cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
					g_object_unref(pixbuf);
					*error_pointer = g_error_new(g_quark_from_static_string("pqiv-pixbuf-error"), 1, "Failed to create a cairo image surface for the loaded image (cairo status %d)\n", cairo_surface_status(surface));
					return;
				}
				cairo_t *sf_cr = cairo_create(surface);
				gdk_cairo_set_source_pixbuf(sf_cr, pixbuf, 0, 0);
				cairo_paint(sf_cr);
				if(cairo_status(sf_cr) == CAIRO_STATUS_NO_MEMORY) {
					// Failed due to out of memory - retry with smaller copy of the image
					cairo_destroy(sf_cr);
					cairo_surface_destroy(surface);
					if(cairo_image_dimensions_limit > 10000) {
						cairo_image_dimensions_limit -= 10000;
						continue;
					}
					g_object_unref(pixbuf);
					*error_pointer = g_error_new(g_quark_from_static_string("pqiv-pixbuf-error"), 1, "Insufficient memory to load image");
					return;
				}
				cairo_destroy(sf_cr);
			#endif

			break;
		}
		while(TRUE); // Do not ever repeat, only on explicit "continue", see break just above.

		cairo_surface_t *old_surface = private->image_surface;
		private->image_surface = surface;
		if(old_surface != NULL) {
			g_idle_add(file_type_gdkpixbuf_load_destroy_old_image_callback, old_surface);
		}
		g_object_unref(pixbuf);

		file->is_loaded = TRUE;
	}
}/*}}}*/
void file_type_gdkpixbuf_draw(file_t *file, cairo_t *cr) {/*{{{*/
	file_private_data_gdkpixbuf_t *private = (file_private_data_gdkpixbuf_t *)file->private;

	cairo_surface_t *current_image_surface = private->image_surface;
	cairo_set_source_surface(cr, current_image_surface, 0, 0);
	apply_interpolation_quality(cr);
	cairo_paint(cr);
}/*}}}*/

void file_type_gdkpixbuf_initializer(file_type_handler_t *info) {/*{{{*/
	// Fill the file filter pattern
	info->file_types_handled = gtk_file_filter_new();
	gtk_file_filter_add_pixbuf_formats(info->file_types_handled);
	GSList *file_formats_list = gdk_pixbuf_get_formats();
	for(GSList *file_formats_iterator = file_formats_list; file_formats_iterator; file_formats_iterator = g_slist_next(file_formats_iterator)) {
			gchar **file_format_extensions_iterator = gdk_pixbuf_format_get_extensions(file_formats_iterator->data);
			while(*file_format_extensions_iterator != NULL) {
					gchar *extn = g_strdup_printf("*.%s", *file_format_extensions_iterator);
					gtk_file_filter_add_pattern(info->file_types_handled, extn);
					g_free(extn);
					++file_format_extensions_iterator;
			}
	};
	g_slist_free(file_formats_list);

	// Assign the handlers
	info->alloc_fn                 =  file_type_gdkpixbuf_alloc;
	info->free_fn                  =  file_type_gdkpixbuf_free;
	info->load_fn                  =  file_type_gdkpixbuf_load;
	info->unload_fn                =  file_type_gdkpixbuf_unload;
	info->animation_initialize_fn  =  file_type_gdkpixbuf_animation_initialize;
	info->animation_next_frame_fn  =  file_type_gdkpixbuf_animation_next_frame;
	info->draw_fn                  =  file_type_gdkpixbuf_draw;
}/*}}}*/
/* }}} */
