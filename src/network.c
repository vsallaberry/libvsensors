/*
 * Copyright (C) 2017-2018 Vincent Sallaberry
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
#include <errno.h>

#include "network.h"
#include "network_private.h"

/** family-specific free */
static sensor_status_t family_free(sensor_family_t *family) {
    if (family->priv) {
        priv_t *priv = (priv_t *) family->priv;
        if (priv->sensors_desc)
            free (priv->sensors_desc);
        free(family->priv);
        family->priv = NULL;
    }
    return SENSOR_SUCCESS;
}

/** family private data creation, including the sensor_desc_t data */
static sensor_status_t init_private_data(sensor_family_t *family) {
    priv_t * priv = (priv_t *) family->priv;;
    // Not Pretty but allows to have an initiliazed array with dynamic values.
    sensor_desc_t sensors_desc[] = {
        { &priv->network_data.ibytes,       "network out bytes",       SENSOR_VALUE_ULONG,  family },
        { &priv->network_data.obytes,       "network in bytes",        SENSOR_VALUE_ULONG,  family },
        { &priv->network_data.ibytespersec, "network out bytes/sec",   SENSOR_VALUE_ULONG,  family },
        { &priv->network_data.obytespersec, "network in bytes/sec",    SENSOR_VALUE_ULONG,  family },
        { NULL, NULL, 0, NULL },
    };
    if ((priv->sensors_desc
            = calloc(sizeof(sensors_desc) / sizeof(*sensors_desc), sizeof(*sensors_desc))) == NULL) {
        return SENSOR_ERROR;
    }
    memcpy(priv->sensors_desc, sensors_desc, sizeof(sensors_desc));
    return SENSOR_SUCCESS;
}

/** family-specific init */
static sensor_status_t family_init(sensor_family_t *family) {
    // Sanity checks done before in sensor_init()
    if (family->priv != NULL) {
        LOG_ERROR(family->log, "error: %s data already initialized", family->info->name);
        family_free(family);
        return SENSOR_ERROR;
    }
    if ((family->priv = calloc(1, sizeof(priv_t))) == NULL) {
        LOG_ERROR(family->log, "cannot allocate private %s data", family->info->name);
        return SENSOR_ERROR;
    }
    if (init_private_data(family) != SENSOR_SUCCESS) {
        LOG_ERROR(family->log, "cannot initialize private %s data", family->info->name);
        free(family->priv);
        family->priv = NULL;
        return SENSOR_ERROR;
    }
    return SENSOR_SUCCESS;
}

/** family-specific list */
static slist_t * family_list(sensor_family_t *family) {
    priv_t *    priv = (priv_t *) family->priv;
    slist_t *   list = NULL;

    for (unsigned int i_desc = 0; priv->sensors_desc[i_desc].label; i_desc++) {
        list = slist_prepend(list, &priv->sensors_desc[i_desc]);
    }
    return list;
}

/** family-specific update */
static sensor_status_t family_update(sensor_sample_t *sensor, struct timeval * now) {
    // Sanity checks are done in sensor_update_get()
    priv_t * fpriv = (priv_t *) sensor->desc->family->priv;
    if (fpriv == NULL) {
       return SENSOR_ERROR;
    }
    // Because all memory datas are retrieved at once, don't repeat it for each sensor
    struct timeval elapsed;
    struct timeval limit = {
        .tv_sec     = sensor->watch.update_interval_ms / 1000,
        .tv_usec    = (sensor->watch.update_interval_ms % 1000) * 1000,
    };
    timersub(now, &fpriv->last_update_time, &elapsed);
    if (timercmp(&elapsed, &limit, >=)) {
        network_get(sensor->desc->family,
                    &fpriv->network_data, fpriv->last_update_time.tv_sec == 0 ? NULL : &elapsed);
        fpriv->last_update_time = *now;
    }
    // Always update the sensor Value;
    return sensor_value_fromraw(sensor->desc->key, &sensor->value);
}

const sensor_family_info_t g_sensor_family_network = {
    .name = "network",
    .init = family_init,
    .free = family_free,
    .update = family_update,
    .list = family_list,
};

