/*
 * Copyright (C) 2017-2019 Vincent Sallaberry
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
#ifndef SENSOR_CPU_PRIVATE_H
#define SENSOR_CPU_PRIVATE_H

#include "cpu.h"

#ifdef __cplusplus
extern "C" {
#endif

sensor_status_t cpu_get(sensor_family_t * family, struct timeval *elapsed);

/** Internal struct where data for one cpu is kept */
typedef struct {
    unsigned long   sys;
    unsigned long   user;
    unsigned long   activity;
    unsigned long   total;
    unsigned char   sys_percent;
    unsigned char   user_percent;
    unsigned char   activity_percent;
} cpu_tick_t;

/** Internal struct where all cpu info is kept */
typedef struct {
    unsigned char   nb_cpus;
    cpu_tick_t *    ticks;
} cpu_data_t;

/** private/specific network family structure */
typedef struct {
    sensor_desc_t *     sensors_desc;
    cpu_data_t          cpu_data;
    struct timeval      last_update_time;
} priv_t;

#ifdef __cplusplus
}
#endif

#endif // ifdef SENSOR_CPU_PRIVATE_H


