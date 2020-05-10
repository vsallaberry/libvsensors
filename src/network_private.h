/*
 * Copyright (C) 2017-2020 Vincent Sallaberry
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
#ifndef SENSOR_NETWORK_PRIVATE_H
#define SENSOR_NETWORK_PRIVATE_H

#include "network.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Iternal struct where all network info is kept */
typedef struct {
    TYPE_SENSOR_VALUE_ULONG     ibytes;
    TYPE_SENSOR_VALUE_ULONG     obytes;
    TYPE_SENSOR_VALUE_ULONG     phy_ibytes;
    TYPE_SENSOR_VALUE_ULONG     phy_obytes;
    TYPE_SENSOR_VALUE_ULONG     ibytespersec;
    TYPE_SENSOR_VALUE_ULONG     obytespersec;
    TYPE_SENSOR_VALUE_ULONG     phy_ibytespersec;
    TYPE_SENSOR_VALUE_ULONG     phy_obytespersec;
} network_data_t;

/** private/specific network family structure */
typedef struct {
    sensor_desc_t *     sensors_desc;
    network_data_t      network_data;
    network_data_t *    iface_data;
    struct timeval      last_update_time;
    void *              sysdep;
} network_priv_t;

sensor_status_t sysdep_network_support(sensor_family_t * family, const char * label);
sensor_status_t sysdep_network_get (sensor_family_t * family,
                             network_data_t *data, struct timeval *elapsed_time);
sensor_status_t sysdep_network_init(sensor_family_t * family);
sensor_status_t sysdep_network_destroy(sensor_family_t * family);

#ifdef __cplusplus
}
#endif

#endif // ifdef SENSOR_NETWORK_PRIVATE_H

