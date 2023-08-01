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
#include "sensor.h"
#ifndef LIBVSENSORS_SENSOR_COMMON_H
#define LIBVSENSORS_SENSOR_COMMON_H

typedef enum {
    CQT_NONE = 0,
    CQT_DEVICE,
    CQT_NB // last
} common_queue_type_t;

typedef enum {
    CDA_NONE = 0,
    CDA_ADD,
    CDA_REMOVE,
    CDA_CHANGE,
    CDA_NB // last
} common_device_action_t;

typedef struct { 
    char *                  name;
    char *                  type;
    common_device_action_t  action;
} common_device_t;

typedef struct {
    common_queue_type_t     type;
    union {
        common_device_t     dev;
        void *              data;
    }                       u;
} common_event_t;

#ifdef __cplusplus
extern "C" {
#endif
// ** FUNCTIONS ***************************

/** get the the 'common' family
 * This can be used by plugins/families to access common utilities. */
sensor_family_t *   sensor_family_common(sensor_ctx_t * sctx);

/** apply function to event queue events. the process function can return:
  * + SENSOR_NOT_SUPPORTED: event is kept
  * + SENSOR_ERROR: event is kept, loop is stopped
  * + SENSOR_SUCCESS: event is deleted from the queue */
sensor_status_t     sensor_common_queue_process(
                        sensor_ctx_t * sctx,
                        sensor_status_t (*fun)(common_event_t * event, void * user_data),
                        void * user_data);

/** add an event to the common event queue
 *  event must be allocated and will be freed with common_event_free(),common_queue_process() */
sensor_status_t     sensor_common_queue_add(sensor_ctx_t * sctx, common_event_t * event);


// ****************************************
#ifdef __cplusplus
}
#endif

#endif // ifdef LIBVSENSORS_SENSOR_COMMON_H

