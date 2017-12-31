/*
 * Copyright (C) 2017 Vincent Sallaberry
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
#include <stdio.h>
#include <string.h>

#include "smc.h"

#ifdef __APPLE__ //FIXME: move sys dependent code in src/sysdeps

typedef struct {
    const char *key;
    const char *label;
    const char *fmt;
} smc_sensor_info_t;

static sensor_status_t SMCOpen() {
    return SENSOR_ERROR;
}

static sensor_status_t SMCClose() {
    return SENSOR_ERROR;
}

sensor_status_t smc_family_init(sensor_family_t *family) {
    (void)family;
    if (SMCOpen() == SENSOR_SUCCESS) {
       return SENSOR_SUCCESS;
    } else {
       fprintf(stderr, "%s(): SMCOpen() failed!\n", __func__);
       return SENSOR_ERROR;
    }
}

sensor_status_t smc_family_free(sensor_family_t *family) {
    (void)family;
    if (SMCClose() == SENSOR_SUCCESS) {
       return SENSOR_SUCCESS;
    } else {
       fprintf(stderr, "%s(): SMCClose() failed!\n", __func__);
       return SENSOR_ERROR;
    }
}

slist_t * smc_family_list(sensor_family_t *family) {
    (void)family;
    if (1) {
       return NULL;
    } else {
       fprintf(stderr, "%s(): SMCClose() failed!\n", __func__);
       return NULL;
    }
}

sensor_status_t smc_family_update(sensor_sample_t *sensor, struct timeval * now) {
    (void)sensor;
    (void)now;
    return SENSOR_SUCCESS;
}

const sensor_family_info_t g_sensor_family_smc = {
    .name = "SMC",
    .init = smc_family_init,
    .free = smc_family_free,
    .update = smc_family_update,
    .list = smc_family_list,
};

#else

const sensor_family_info_t g_sensor_family_smc = {
    .name = "SMC NOT IMPLEMENTED ON THIS SYSTEM",
    .init = NULL,
    .free = NULL,
    .update = NULL,
    .list = NULL,
};

#endif /* ! ifdef *APPLE */

