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
#ifndef SENSOR_COMMON_PRIVATE_H
#define SENSOR_COMMON_PRIVATE_H

#include <pthread.h>

#include "vlib/slist.h"
#include "vlib/thread.h"
#include "libvsensors/sensor.h"

/** internal common queue type */
typedef slist_t * sensor_common_queue_t;

typedef struct {
    void *                  sysdep;
    vthread_t *             thread;
    sensor_common_queue_t   event_queue;
    pthread_mutex_t         mutex;
} common_priv_t;

#ifdef __cplusplus
extern "C" {
#endif

sensor_status_t sysdep_common_init(sensor_family_t * family);
sensor_status_t sysdep_common_destroy(sensor_family_t * family);

#ifdef __cplusplus
}
#endif

#endif // ifdef SENSOR_COMMON_PRIVATE_H

