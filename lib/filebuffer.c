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
#include <errno.h>
#include <string.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#ifdef _POSIX_VERSION
#define HAS_MMAP
#endif

#ifdef HAS_MMAP
#include <sys/mman.h>
#endif

struct buffered_file {
	GBytes *data;
	char *file_name;
	int ref_count;
	gboolean file_name_is_temporary;
};

GHashTable *file_buffer_table = NULL;
GRecMutex file_buffer_table_mutex;

#ifdef HAS_MMAP
extern GFile *gfile_for_commandline_arg(const char *);

struct buffered_file_mmap_info {
	void *ptr;
	int fd;
	size_t size;
};

static void buffered_file_mmap_free_helper(struct buffered_file_mmap_info *info) {
	munmap(info->ptr, info->size);
	close(info->fd);
	g_slice_free(struct buffered_file_mmap_info, info);
}
#endif

GBytes *buffered_file_as_bytes(file_t *file, GInputStream *data, GError **error_pointer) {
	g_rec_mutex_lock(&file_buffer_table_mutex);
	if(!file_buffer_table) {
		file_buffer_table = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	}
	struct buffered_file *buffer = g_hash_table_lookup(file_buffer_table, file->file_name);
	if(!buffer) {
		GBytes *data_bytes = NULL;

		if((file->file_flags & FILE_FLAGS_MEMORY_IMAGE)) {
			if(file->file_data_loader) {
				data_bytes = file->file_data_loader(file, error_pointer);
			}
			else {
				data_bytes = g_bytes_ref(file->file_data);
			}

			if(!data_bytes) {
				g_rec_mutex_unlock(&file_buffer_table_mutex);
				return NULL;
			}
		}
		else {

#ifdef HAS_MMAP
			// If this is a local file, try to mmap() it first instead of loading it completely
			GFile *input_file = gfile_for_commandline_arg(file->file_name);
			char *input_file_abspath = g_file_get_path(input_file);
			if(input_file_abspath) {
				GFileInfo *file_info = g_file_query_info(input_file, G_FILE_ATTRIBUTE_STANDARD_SIZE, G_FILE_QUERY_INFO_NONE, NULL, error_pointer);
				if(!file_info) {
					g_object_unref(input_file);
					g_rec_mutex_unlock(&file_buffer_table_mutex);
					return NULL;
				}
				goffset input_file_size = g_file_info_get_size(file_info);
				g_object_unref(file_info);

				int fd = open(input_file_abspath, O_RDONLY);
				g_free(input_file_abspath);
				if(fd < 0) {
					g_object_unref(input_file);
					g_rec_mutex_unlock(&file_buffer_table_mutex);
					*error_pointer = g_error_new(g_quark_from_static_string("pqiv-filebuffer-error"), 1, "Opening the file failed with errno=%d: %s", errno, strerror(errno));
					return NULL;
				}
				void *input_file_data = mmap(NULL, input_file_size, PROT_READ, MAP_SHARED, fd, 0);

				if(input_file_data != MAP_FAILED) {
					struct buffered_file_mmap_info *mmap_info = g_slice_new(struct buffered_file_mmap_info);
					mmap_info->ptr = input_file_data;
					mmap_info->fd = fd;
					mmap_info->size = input_file_size;

					data_bytes = g_bytes_new_with_free_func(input_file_data, input_file_size, (GDestroyNotify)buffered_file_mmap_free_helper, mmap_info);
				}
				else {
					close(fd);
				}
			}
			g_object_unref(input_file);
#endif

			if(data_bytes) {
				// mmap() above worked
			}
			else if(!data) {
				data = image_loader_stream_file(file, error_pointer);
				if(!data) {
					g_rec_mutex_unlock(&file_buffer_table_mutex);
					return NULL;
				}
				data_bytes = g_input_stream_read_completely(data, image_loader_cancellable, error_pointer);
				g_object_unref(data);
			}
			else {
				data_bytes = g_input_stream_read_completely(data, image_loader_cancellable, error_pointer);
			}

			if(!data_bytes) {
				g_rec_mutex_unlock(&file_buffer_table_mutex);
				return NULL;
			}
		}
		buffer = g_new0(struct buffered_file, 1);
		g_hash_table_insert(file_buffer_table, g_strdup(file->file_name), buffer);
		buffer->data = data_bytes;
	}
	buffer->ref_count++;
	g_rec_mutex_unlock(&file_buffer_table_mutex);
	return buffer->data;
}

char *buffered_file_as_local_file(file_t *file, GInputStream *data, GError **error_pointer) {
	g_rec_mutex_lock(&file_buffer_table_mutex);
	if(!file_buffer_table) {
		file_buffer_table = g_hash_table_new(g_str_hash, g_str_equal);
	}
	struct buffered_file *buffer = g_hash_table_lookup(file_buffer_table, file->file_name);
	if(buffer) {
		buffer->ref_count++;
		g_rec_mutex_unlock(&file_buffer_table_mutex);
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
			data = image_loader_stream_file(file, error_pointer);
			if(!data) {
				g_hash_table_remove(file_buffer_table, file->file_name);
				g_rec_mutex_unlock(&file_buffer_table_mutex);
				return NULL;
			}
			local_data = TRUE;
		}

		GFile *temporary_file;
		GFileIOStream *iostream = NULL;
		gchar *extension = strrchr(file->file_name, '.');
		if(extension) {
			gchar *name_template = g_strdup_printf("pqiv-XXXXXX%s", extension);
			temporary_file = g_file_new_tmp(name_template, &iostream, error_pointer);
			g_free(name_template);
		}
		else {
			temporary_file = g_file_new_tmp("pqiv-XXXXXX.ps", &iostream, error_pointer);
		}
		if(!temporary_file) {
			g_printerr("Failed to buffer %s: Could not create a temporary file in %s\n", file->file_name, g_get_tmp_dir());
			if(local_data) {
				g_object_unref(data);
			}
			g_hash_table_remove(file_buffer_table, file->file_name);
			g_rec_mutex_unlock(&file_buffer_table_mutex);
			return NULL;
		}

		if(g_output_stream_splice(g_io_stream_get_output_stream(G_IO_STREAM(iostream)), data, G_OUTPUT_STREAM_SPLICE_CLOSE_TARGET, image_loader_cancellable, error_pointer) < 0) {
			g_hash_table_remove(file_buffer_table, file->file_name);
			if(local_data) {
				g_object_unref(data);
			}
			g_rec_mutex_unlock(&file_buffer_table_mutex);
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
	g_rec_mutex_unlock(&file_buffer_table_mutex);
	return buffer->file_name;
}

void buffered_file_unref(file_t *file) {
	g_rec_mutex_lock(&file_buffer_table_mutex);
	struct buffered_file *buffer = g_hash_table_lookup(file_buffer_table, file->file_name);
	if(!buffer) {
		g_rec_mutex_unlock(&file_buffer_table_mutex);
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
	g_rec_mutex_unlock(&file_buffer_table_mutex);
}
