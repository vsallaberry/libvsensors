/*
 * Copyright (C) 2018-2020 Vincent Sallaberry
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
#include "cpu_private.h"

sensor_status_t sysdep_cpu_support(sensor_family_t * family, const char * label) {
    (void) label;
    (void) family;
    return SENSOR_ERROR;
}

unsigned int    sysdep_cpu_nb(sensor_family_t * family) {
    (void) family;
    return 0;
}

void            sysdep_cpu_destroy(sensor_family_t * family) {
    (void) family;
}

sensor_status_t sysdep_cpu_get(sensor_family_t * family, struct timeval *elapsed) {
    (void) family;
    (void) elapsed;
    return SENSOR_ERROR;
}

