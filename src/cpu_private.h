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
#ifndef SENSOR_CPU_PRIVATE_H
#define SENSOR_CPU_PRIVATE_H

#include "cpu.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Exposed by sysdeps */
sensor_status_t sysdep_cpu_support(sensor_family_t * family, const char * label);
sensor_status_t sysdep_cpu_get(sensor_family_t * family, struct timeval *elapsed);
unsigned int    sysdep_cpu_nb(sensor_family_t * family);
void            sysdep_cpu_destroy(sensor_family_t * family);

/* Exposed to sysdeps */
#define CPU_COMPUTE_GLOBAL      INT_MAX

unsigned long   cpu_clktck();
int             cpu_store_ticks(
                    sensor_family_t *       family,
                    int                     cpu_idx, /*can be CPU_COMPUTE_GLOBAL*/
                    unsigned long           sys,
                    unsigned long           user,
                    unsigned long           activity,
                    unsigned long           total,
                    struct timeval *        elapsed);

/** Internal struct where data for one cpu is kept */
typedef struct {
    TYPE_SENSOR_VALUE_ULONG sys;
    TYPE_SENSOR_VALUE_ULONG user;
    TYPE_SENSOR_VALUE_ULONG activity;
    TYPE_SENSOR_VALUE_ULONG total;
    TYPE_SENSOR_VALUE_UCHAR sys_percent;
    TYPE_SENSOR_VALUE_UCHAR user_percent;
    TYPE_SENSOR_VALUE_UCHAR activity_percent;
} cpu_tick_t;

/** Internal struct where all cpu info is kept */
typedef struct {
    TYPE_SENSOR_VALUE_UINT16    nb_cpus;
    cpu_tick_t *                ticks;
} cpu_data_t;

/** private/specific network family structure */
typedef struct {
    sensor_desc_t *     sensors_desc;
    cpu_data_t          cpu_data;
    struct timeval      last_update_time;
    void *              sysdep;
} cpu_priv_t;

#ifdef __cplusplus
}
#endif

#endif // ifdef SENSOR_CPU_PRIVATE_H


