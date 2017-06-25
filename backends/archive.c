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
 * libarchive backend
 *
 * This is the non-comicbook variant that handles arbitrary archives
 * (recursively, if necessary).
 *
 */

#include "../pqiv.h"
#include "../lib/filebuffer.h"
#include <archive.h>
#include <archive_entry.h>
#include <string.h>

typedef struct {
	// The source archive
	file_t *source_archive;

	// The path to the target file within the archive
	gchar *entry_name;
} file_loader_delegate_archive_t;

static struct archive *file_type_archive_gen_archive(GBytes *data) {/*{{{*/
	struct archive *archive = archive_read_new();
	archive_read_support_format_zip(archive);
	archive_read_support_format_rar(archive);
	archive_read_support_format_7zip(archive);
	archive_read_support_format_tar(archive);
	archive_read_support_filter_all(archive);

	gsize data_size;
	char *data_ptr = (char *)g_bytes_get_data(data, &data_size);

	if(archive_read_open_memory(archive, data_ptr, data_size) != ARCHIVE_OK) {
		g_printerr("Failed to load archive: %s\n", archive_error_string(archive));
		archive_read_free(archive);
		return NULL;
	}

	return archive;
}/*}}}*/

void file_type_archive_data_free(file_loader_delegate_archive_t *data) {/*{{{*/
	if(data->source_archive) {
		file_free(data->source_archive);
		data->source_archive = NULL;
	}
	g_free(data);
}/*}}}*/

GBytes *file_type_archive_data_loader(file_t *file, GError **error_pointer) {/*{{{*/
	const file_loader_delegate_archive_t *archive_data = g_bytes_get_data(file->file_data, NULL);

	GBytes *data = buffered_file_as_bytes(archive_data->source_archive, NULL, error_pointer);
	if(!data) {
		g_printerr("Failed to load archive %s: %s\n", file->display_name, error_pointer && *error_pointer ? (*error_pointer)->message : "Unknown error");
		g_clear_error(error_pointer);
		return NULL;
	}

	struct archive *archive = file_type_archive_gen_archive(data);
	if(!archive) {
		buffered_file_unref(file);
		return NULL;
	}

	// Find the proper entry
	size_t entry_size = 0;
	void *entry_data = NULL;

	struct archive_entry *entry;
	while(archive_read_next_header(archive, &entry) == ARCHIVE_OK) {
		if(archive_data->entry_name && strcmp(archive_data->entry_name, archive_entry_pathname(entry)) == 0) {
			entry_size = archive_entry_size(entry);
			entry_data = g_malloc(entry_size);

			if(archive_read_data(archive, entry_data, entry_size) != (ssize_t)entry_size) {
				archive_read_free(archive);
				buffered_file_unref(file);
				*error_pointer = g_error_new(g_quark_from_static_string("pqiv-archive-error"), 1, "The file had an unexpected size");
				return NULL;
			}

			break;
		}
	}

	archive_read_free(archive);
	buffered_file_unref(archive_data->source_archive);
	if(!entry_size) {
		*error_pointer = g_error_new(g_quark_from_static_string("pqiv-archive-error"), 1, "The file has gone within the archive");
		return NULL;
	}

	return g_bytes_new_take(entry_data, entry_size);
}/*}}}*/

BOSNode *file_type_archive_alloc(load_images_state_t state, file_t *file) {/*{{{*/
	GError *error_pointer = NULL;
	GBytes *data = buffered_file_as_bytes(file, NULL, &error_pointer);
	if(!data) {
		g_printerr("Failed to load archive %s: %s\n", file->display_name, error_pointer ? error_pointer->message : "Unknown error");
		g_clear_error(&error_pointer);
		file_free(file);
		return FALSE_POINTER;
	}

	struct archive *archive = file_type_archive_gen_archive(data);
	if(!archive) {
		buffered_file_unref(file);
		file_free(file);
		return FALSE_POINTER;
	}

	GtkFileFilterInfo file_filter_info;
	file_filter_info.contains = GTK_FILE_FILTER_FILENAME | GTK_FILE_FILTER_DISPLAY_NAME;

	BOSNode *first_node = FALSE_POINTER;

	struct archive_entry *entry;
	while(archive_read_next_header(archive, &entry) == ARCHIVE_OK) {
		const gchar *entry_name = archive_entry_pathname(entry);

		#if ARCHIVE_VERSION_NUMBER < 3003002
			// Affected by libarchive bug #869
			if(archive_entry_size(entry) == 0) {
				const char *archive_format = archive_format_name(archive);
				if(strncmp("ZIP", archive_format, 3) == 0) {
					g_printerr("Failed to load archive %s: This ZIP file is affected by libarchive bug #869, which was fixed in v3.3.2. Skipping file.\n", file->display_name);
					archive_read_free(archive);
					buffered_file_unref(file);
					file_free(file);
					return FALSE_POINTER;
				}
			}
		#endif


		// Prepare a new file_t for this entry
		gchar *sub_name = g_strdup_printf("%s#%s", file->display_name, entry_name);
		file_t *new_file = image_loader_duplicate_file(file, g_strdup(sub_name), g_strdup(sub_name), sub_name);
		if(new_file->file_data) {
			g_bytes_unref(new_file->file_data);
			new_file->file_data = NULL;
		}
		size_t delegate_struct_alloc_size = sizeof(file_loader_delegate_archive_t) + strlen(entry_name) + 2;
		file_loader_delegate_archive_t *new_file_data = g_malloc(delegate_struct_alloc_size);
		new_file_data->source_archive = image_loader_duplicate_file(file, NULL, NULL, NULL);
		new_file_data->entry_name     = (char *)(new_file_data) + sizeof(file_loader_delegate_archive_t) + 1;
		memcpy(new_file_data->entry_name, entry_name, strlen(entry_name) + 1);
		new_file->file_data = g_bytes_new_with_free_func(new_file_data, delegate_struct_alloc_size, (GDestroyNotify)file_type_archive_data_free, new_file_data);
		new_file->file_flags |= FILE_FLAGS_MEMORY_IMAGE;
		new_file->file_data_loader = file_type_archive_data_loader;

		// Find an appropriate handler for this file
		gchar *name_lowerc = g_utf8_strdown(entry_name, -1);
		file_filter_info.filename = file_filter_info.display_name = name_lowerc;

		// Check if one of the file type handlers can handle this file
		BOSNode *node = load_images_handle_parameter_find_handler(entry_name, state, new_file, &file_filter_info);
		if(node == NULL) {
			// No handler found. We could fall back to using a default. Free new_file instead.
			file_free(new_file);
		}
		else if(node == FALSE_POINTER) {
			// File type is known, but loading failed; new_file has already been free()d
			node = NULL;
		}
		else if(first_node == FALSE_POINTER) {
			first_node = node;
		}

		g_free(name_lowerc);

		archive_read_data_skip(archive);
	}

	archive_read_free(archive);
	buffered_file_unref(file);
	file_free(file);
	return first_node;
}/*}}}*/

void file_type_archive_initializer(file_type_handler_t *info) {/*{{{*/
	// Fill the file filter pattern
	info->file_types_handled = gtk_file_filter_new();

	// Mime types for archives
	gtk_file_filter_add_mime_type(info->file_types_handled, "application/x-tar");
	gtk_file_filter_add_mime_type(info->file_types_handled, "application/x-zip");
	gtk_file_filter_add_mime_type(info->file_types_handled, "application/x-rar");

	// Arbitrary archive files
	gtk_file_filter_add_pattern(info->file_types_handled, "*.zip");
	gtk_file_filter_add_pattern(info->file_types_handled, "*.rar");
	gtk_file_filter_add_pattern(info->file_types_handled, "*.7z");
	gtk_file_filter_add_pattern(info->file_types_handled, "*.tar");
	gtk_file_filter_add_pattern(info->file_types_handled, "*.tbz");
	gtk_file_filter_add_pattern(info->file_types_handled, "*.tgz");
	gtk_file_filter_add_pattern(info->file_types_handled, "*.tar.bz2");
	gtk_file_filter_add_pattern(info->file_types_handled, "*.tar.gz");

	// Assign the handlers
	info->alloc_fn                 =  file_type_archive_alloc;
}/*}}}*/
