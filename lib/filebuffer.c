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
#include "filebuffer.h"
#include <string.h>
#include <glib/gstdio.h>

struct buffered_file {
	GBytes *data;
	char *file_name;
	int ref_count;
	gboolean file_name_is_temporary;
};

GHashTable *file_buffer_table = NULL;

GBytes *buffered_file_as_bytes(file_t *file, GInputStream *data) {
	if(!file_buffer_table) {
		file_buffer_table = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	}
	struct buffered_file *buffer = g_hash_table_lookup(file_buffer_table, file->file_name);
	if(!buffer) {
		GBytes *data_bytes;

		if((file->file_flags & FILE_FLAGS_MEMORY_IMAGE)) {
			data_bytes = g_bytes_ref(file->file_data);
		}
		else {
			if(!data) {
				data = image_loader_stream_file(file, NULL);
				if(!data) {
					return NULL;
				}
				data_bytes = g_input_stream_read_completely(data, image_loader_cancellable, NULL);
				g_object_unref(data);
			}
			else {
				data_bytes = g_input_stream_read_completely(data, image_loader_cancellable, NULL);
			}
		}
		buffer = g_new0(struct buffered_file, 1);
		g_hash_table_insert(file_buffer_table, g_strdup(file->file_name), buffer);
		buffer->data = data_bytes;
	}
	buffer->ref_count++;
	return buffer->data;
}

char *buffered_file_as_local_file(file_t *file, GInputStream *data) {
	if(!file_buffer_table) {
		file_buffer_table = g_hash_table_new(g_str_hash, g_str_equal);
	}
	struct buffered_file *buffer = g_hash_table_lookup(file_buffer_table, file->file_name);
	if(buffer) {
		buffer->ref_count++;
		return buffer->file_name;
	}

	buffer = g_new0(struct buffered_file, 1);
	g_hash_table_insert(file_buffer_table, g_strdup(file->file_name), buffer);

	gchar *path = NULL;
	if(!(file->file_flags & FILE_FLAGS_MEMORY_IMAGE)) {
		GFile *input_file = g_file_new_for_commandline_arg(file->file_name);
		path = g_file_get_path(input_file);
		g_object_unref(input_file);
	}
	if(path) {
		buffer->file_name = path;
		buffer->file_name_is_temporary = FALSE;
	}
	else {
		gboolean local_data = FALSE;
		if(!data) {
			data = image_loader_stream_file(file, NULL);
			if(!data) {
				g_hash_table_remove(file_buffer_table, file->file_name);
				return NULL;
			}
			local_data = TRUE;
		}

		GFile *temporary_file;
		GFileIOStream *iostream = NULL;
		gchar *extension = strrchr(file->file_name, '.');
		if(extension) {
			gchar *name_template = g_strdup_printf("pqiv-XXXXXX%s", extension);
			temporary_file = g_file_new_tmp(name_template, &iostream, NULL);
			g_free(name_template);
		}
		else {
			temporary_file = g_file_new_tmp("pqiv-XXXXXX.ps", &iostream, NULL);
		}
		if(!temporary_file) {
			g_printerr("Failed to buffer %s: Could not create a temporary file in %s\n", file->file_name, g_get_tmp_dir());
			if(local_data) {
				g_object_unref(data);
			}
			g_hash_table_remove(file_buffer_table, file->file_name);
			return NULL;
		}

		GError *error_pointer = NULL;
		if(g_output_stream_splice(g_io_stream_get_output_stream(G_IO_STREAM(iostream)), data, G_OUTPUT_STREAM_SPLICE_CLOSE_TARGET, image_loader_cancellable, &error_pointer) < 0) {
			g_printerr("Failed to store a temporary local copy of %s: %s", file->file_name, error_pointer->message);
			g_clear_error(&error_pointer);
			g_hash_table_remove(file_buffer_table, file->file_name);
			if(local_data) {
				g_object_unref(data);
			}
			return NULL;
		}

		buffer->file_name = g_file_get_path(temporary_file);
		buffer->file_name_is_temporary = TRUE;

		g_object_unref(iostream);
		g_object_unref(temporary_file);
		if(local_data) {
			g_object_unref(data);
		}
	}

	buffer->ref_count++;
	return buffer->file_name;
}

void buffered_file_unref(file_t *file) {
	struct buffered_file *buffer = g_hash_table_lookup(file_buffer_table, file->file_name);
	if(!buffer) {
		return;
	}
	if(--buffer->ref_count == 0) {
		if(buffer->data) {
			g_bytes_unref(buffer->data);
		}
		if(buffer->file_name) {
			if(buffer->file_name_is_temporary) {
				g_unlink(buffer->file_name);
			}
			g_free(buffer->file_name);
		}
		g_hash_table_remove(file_buffer_table, file->file_name);
	}
}
