/*
 * Copyright (C) 2020 Vincent Sallaberry
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
 * disk os specific default implementation for Generic Sensor Management Library.
 */
#include "disk_private.h"

sensor_status_t sysdep_disk_support(sensor_family_t * family, const char * label) {
    (void)family;
    (void)label;
    return SENSOR_ERROR;
}

sensor_status_t sysdep_disk_get (sensor_family_t * family,
                                 disk_data_t *data, struct timeval *elapsed_time) {
    (void)family;
    (void)data;
    (void)elapsed_time;
    return SENSOR_ERROR;
}

sensor_status_t sysdep_disk_init(sensor_family_t * family) {
    (void)family;
    return SENSOR_ERROR;
}

sensor_status_t sysdep_disk_destroy(sensor_family_t * family) {
    (void)family;
    return SENSOR_ERROR;
}
