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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "disk.h"

sensor_status_t sysdep_disk_support(sensor_family_t * family, const char * label);

static sensor_status_t family_free(sensor_family_t *family) {
    (void)family; //TODO
    if (1) {
       return SENSOR_SUCCESS;
    } else {
       LOG_ERROR(family->log, "%s(): failed!", __func__);
       return SENSOR_ERROR;
    }
}

static sensor_status_t family_init(sensor_family_t *family) {
    if (sysdep_disk_support(family, NULL) != SENSOR_SUCCESS) {
        family_free(family);
        return SENSOR_NOT_SUPPORTED;
    }
    if (1) {
       return SENSOR_SUCCESS;
    } else {
       LOG_ERROR(family->log, "%s(): failed!", __func__);
       family_free(family);
       return SENSOR_ERROR;
    }
}

static slist_t * family_list(sensor_family_t *family) {
    (void)family; //TODO
    if (1) {
       return NULL;
    } else {
       LOG_ERROR(family->log, "%s(): failed!", __func__);
       return NULL;
    }
}

static sensor_status_t family_update(sensor_sample_t *sensor, const struct timeval * now) {
    (void)sensor; //TODO
    (void)now; //TODO
    if (1) {
       return SENSOR_SUCCESS;
    } else {
       LOG_ERROR(sensor->desc->family->log, "%s(): failed!", __func__);
       return SENSOR_ERROR;
    }
}

const sensor_family_info_t g_sensor_family_disk = {
    .name = "disk",
    .init = family_init,
    .free = family_free,
    .update = family_update,
    .list = family_list,
};

