/*
 * Copyright (C) 2023 Vincent Sallaberry
 * libvsensors <https://github.com/vsallaberry/libvsensors>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
/* ------------------------------------------------------------------------
 * Generic Sensor Management Library.
 */
#ifndef SENSOR_FILE_PRIVATE_H
#define SENSOR_FILE_PRIVATE_H

#include "vlib/slist.h"

#include "file.h"

/** per file watch info */
typedef struct {
    char *          name;
    unsigned int    flags;
    void *          sysdep;
} fileinfo_t;

/** private/specific file family structure */
typedef struct {
    slist_t *   files; /* of fileinfo_t * */
    void *      sysdep;
} file_priv_t;

#ifdef __cplusplus
extern "C" {
#endif

sensor_status_t sysdep_file_support(sensor_family_t * family, const char * label);
sensor_status_t sysdep_file_init(sensor_family_t * family);
sensor_status_t sysdep_file_destroy(sensor_family_t * family);
void            sysdep_file_watch_free(void * vfile);
sensor_status_t sysdep_file_watch_add(sensor_family_t * family, fileinfo_t * file);
sensor_status_t sysdep_file_watch_del(sensor_family_t * family, fileinfo_t * file);

#ifdef __cplusplus
}
#endif

#endif // ifdef SENSOR_FILE_PRIVATE_H

