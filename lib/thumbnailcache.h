/*
 * This file is part of pqiv
 * Copyright (c) 2017, Phillip Berndt
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
 * This implements thumbnail caching as specified in
 * https://specifications.freedesktop.org/thumbnail-spec/thumbnail-spec-latest.html
 */

#include "../pqiv.h"

#ifndef CONFIGURED_WITHOUT_MONTAGE_MODE
typedef enum { THUMBNAILS_PERSIST_OFF, THUMBNAILS_PERSIST_ON, THUMBNAILS_PERSIST_STANDARD, THUMBNAILS_PERSIST_RO, THUMBNAILS_PERSIST_LOCAL } thumbnail_persist_mode_t;

gboolean load_thumbnail_from_cache(file_t *file, unsigned width, unsigned height, thumbnail_persist_mode_t persist_mode, char *special_thumbnail_directory);
gboolean store_thumbnail_to_cache(file_t *file, unsigned width, unsigned height, thumbnail_persist_mode_t persist_mode, char *special_thumbnail_directory);
#endif
