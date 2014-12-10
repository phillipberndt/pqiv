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

// Utility functions for backends that do not support streams
//
// These functions assure that a file is available in-memory or as
// a local file, regardless of the GIO backend handling it. Through
// reference counting, multi-page documents can be handled with a
// single temporary copy, rather than having to keep one copy per
// page
//

#include "../pqiv.h"

// Return a bytes view on a file_t
GBytes *buffered_file_as_bytes(file_t *file, GInputStream *data, GError **error_pointer);

// Return a (possibly temporary) file for a file_t
char *buffered_file_as_local_file(file_t *file, GInputStream *data, GError **error_pointer);

// Unreference one of the above, free'ing memory if
// necessary
void buffered_file_unref(file_t *file);
