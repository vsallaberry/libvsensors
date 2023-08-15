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
#include <errno.h>

#include "network.h"
#include "network_private.h"

/** family-specific free */
static sensor_status_t family_free(sensor_family_t *family) {
    if (family->priv != NULL) {
        network_priv_t * priv = (network_priv_t *) family->priv;

        sysdep_network_destroy(family);
        if (priv->sensors_desc != NULL)
            free (priv->sensors_desc);
        family->priv = NULL;
        free(priv);
    }
    return SENSOR_SUCCESS;
}

/** family private data creation, including the sensor_desc_t data */
static sensor_status_t init_private_data(sensor_family_t *family) {
    network_priv_t * priv = (network_priv_t *) family->priv;;
    // Not Pretty but allows to have an initiliazed array with dynamic values.
    sensor_desc_t sensors_desc[] = {
        { &priv->network_data.obytes,           "network all out bytes",
          NULL, SENSOR_VALUE_ULONG,  family },
        { &priv->network_data.ibytes,           "network all in bytes",
          NULL, SENSOR_VALUE_ULONG,  family },
        { &priv->network_data.phy_obytes,       "network out bytes",
          NULL, SENSOR_VALUE_ULONG,  family },
        { &priv->network_data.phy_ibytes,       "network in bytes",
          NULL, SENSOR_VALUE_ULONG,  family },
        { &priv->network_data.obytespersec,     "network all out bytes/sec",
          NULL, SENSOR_VALUE_ULONG,  family },
        { &priv->network_data.ibytespersec,     "network all in bytes/sec",
          NULL, SENSOR_VALUE_ULONG,  family },
        { &priv->network_data.phy_obytespersec, "network out bytes/sec",
          NULL, SENSOR_VALUE_ULONG,  family },
        { &priv->network_data.phy_ibytespersec, "network in bytes/sec",
          NULL, SENSOR_VALUE_ULONG,  family },
        { NULL, NULL, NULL, 0, NULL },
    };
    priv->last_update_time.tv_usec = INT_MAX;
    if ((priv->sensors_desc
            = calloc(sizeof(sensors_desc) / sizeof(*sensors_desc), sizeof(*sensors_desc))) == NULL) {
        return SENSOR_ERROR;
    }
    memcpy(priv->sensors_desc, sensors_desc, sizeof(sensors_desc));

    if (sysdep_network_init(family) != SENSOR_SUCCESS) {
        return SENSOR_ERROR;
    }

    return SENSOR_SUCCESS;
}

/** family-specific init */
static sensor_status_t family_init(sensor_family_t *family) {
    // Sanity checks done before in sensor_init()
    if (family->priv != NULL) {
        LOG_ERROR(family->log, "error: %s data already initialized", family->info->name);
        return SENSOR_ERROR;
    }
    if (sysdep_network_support(family, NULL) != SENSOR_SUCCESS) {
        return SENSOR_NOT_SUPPORTED;
    }
    if ((family->priv = calloc(1, sizeof(network_priv_t))) == NULL) {
        LOG_ERROR(family->log, "cannot allocate private %s data", family->info->name);
        family_free(family);
        return SENSOR_ERROR;
    }
    if (init_private_data(family) != SENSOR_SUCCESS) {
        LOG_ERROR(family->log, "cannot initialize private %s data", family->info->name);
        family_free(family);
        return SENSOR_ERROR;
    }
    return SENSOR_SUCCESS;
}

/** family-specific list */
static slist_t * family_list(sensor_family_t *family) {
    network_priv_t *    priv = (network_priv_t *) family->priv;
    slist_t *           list = NULL;

    for (unsigned int i_desc = 0; priv->sensors_desc[i_desc].label; i_desc++) {
        list = slist_prepend(list, &priv->sensors_desc[i_desc]);
    }
    return list;
}

/** family-specific update */
static sensor_status_t family_update(sensor_sample_t *sensor, const struct timeval * now) {
    /* Sanity checks are done in sensor_init and sensor_update_check() */
    network_priv_t * priv = (network_priv_t *) sensor->desc->family->priv;

    if (now == NULL) {
        sysdep_network_get(sensor->desc->family, &(priv->network_data), NULL);
    } else if (priv->last_update_time.tv_usec == INT_MAX) {
        sysdep_network_get(sensor->desc->family, &(priv->network_data), NULL);
        priv->last_update_time = *now;
    } else {
        /* Because all network datas are retrieved at once, don't repeat it for each sensor */
        struct timeval  elapsed, * pelapsed = &elapsed;

        timersub(now, &(priv->last_update_time), &elapsed);
        if (elapsed.tv_sec == 0 && elapsed.tv_usec < 1000)
            pelapsed = NULL;
        if (timercmp(&elapsed, &(sensor->watch->update_interval), >=)) {
            sysdep_network_get(sensor->desc->family, &(priv->network_data), pelapsed);
            priv->last_update_time = *now;
        }
    }

    /* Always update the sensor Value: we are called because sensor timeout expired.
     * and another sensor with different timeout could have updated global data */
    return sensor_value_fromraw(sensor->desc->key, &sensor->value);
}

const sensor_family_info_t g_sensor_family_network = {
    .name = "network",
    .init = family_init,
    .free = family_free,
    .update = family_update,
    .list = family_list,
    .notify = NULL,
    .write = NULL,
    .free_desc = NULL
};

