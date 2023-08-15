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
//#import <Foundation/Foundation.h>
#include <sys/types.h>
#include <sys/param.h>

/*# if defined(__linux__) || defined(BUILD_SYS_linux)
#  include <linux/sysctl.h>
# else
#  include <sys/sysctl.h>
# endif*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "memory.h"
#include "memory_private.h"

/** family-specific free */
static sensor_status_t family_free(sensor_family_t *family) {
    if (family->priv != NULL) {
        memory_priv_t * priv = (memory_priv_t *) family->priv;

        sysdep_memory_destroy(family);
        if (priv->sensors_desc != NULL)
            free(priv->sensors_desc);
        family->priv = NULL;
        free(priv);
    }
    return SENSOR_SUCCESS;
}

/** family private data creation, including the sensor_desc_t data */
static sensor_status_t init_private_data(sensor_family_t *family) {
    memory_priv_t * priv = (memory_priv_t *) family->priv;
    unsigned int    n_desc = 0;
    // Not Pretty but allows to have an initiliazed array with dynamic values.
    sensor_desc_t   sensors_desc[] = {
        { &priv->memory_data.active,    "active memory",    NULL, SENSOR_VALUE_ULONG, family },
        { &priv->memory_data.inactive,  "inactive memory",  NULL, SENSOR_VALUE_ULONG, family },
        { &priv->memory_data.wired,     "wired memory",     NULL, SENSOR_VALUE_ULONG, family },
        { &priv->memory_data.free,      "free memory",      NULL, SENSOR_VALUE_ULONG, family },
        { &priv->memory_data.used,      "used memory",      NULL, SENSOR_VALUE_ULONG, family },
        { &priv->memory_data.total,     "total memory",     NULL, SENSOR_VALUE_ULONG, family },
        { &priv->memory_data.used_percent,"used memory %",  NULL, SENSOR_VALUE_UCHAR, family },
        { &priv->memory_data.total_swap,"swap total",       NULL, SENSOR_VALUE_ULONG, family },
        { &priv->memory_data.used_swap, "swap used",        NULL, SENSOR_VALUE_ULONG, family },
        { &priv->memory_data.free_swap, "swap free",        NULL, SENSOR_VALUE_ULONG, family },
        { &priv->memory_data.used_swap_percent,"swap used %",NULL,SENSOR_VALUE_UCHAR, family },
        { NULL, NULL, NULL, 0, NULL },
    };
    priv->last_update_time.tv_usec = INT_MAX;

    if ((priv->sensors_desc
            = calloc(sizeof(sensors_desc) / sizeof(*sensors_desc), sizeof(*sensors_desc))) == NULL) {
        return SENSOR_ERROR;
    }
    for (unsigned i = 0; i < sizeof(sensors_desc) / sizeof(*sensors_desc)
                && sensors_desc[i].label != NULL; ++i) {
        if (sysdep_memory_support(family, sensors_desc[i].label) == SENSOR_SUCCESS) {
            priv->sensors_desc[n_desc++] = sensors_desc[i];
        }
    }
    memset(&(priv->sensors_desc[n_desc]), 0, sizeof(priv->sensors_desc[n_desc]));

    if (sysdep_memory_init(family) != SENSOR_SUCCESS) {
        LOG_ERROR(family->log, "cannot initialize sysdep %s data", family->info->name);
        family_free(family);
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
    if (sysdep_memory_support(family, NULL) != SENSOR_SUCCESS) {
        return SENSOR_NOT_SUPPORTED;
    }
    if ((family->priv = calloc(1, sizeof(memory_priv_t))) == NULL) {
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
    memory_priv_t * priv = (memory_priv_t *) family->priv;
    slist_t *       list = NULL;

    for (unsigned int i_desc = 0; priv->sensors_desc[i_desc].label; i_desc++) {
        list = slist_prepend(list, &priv->sensors_desc[i_desc]);
    }
    return list;
}

/** family-specific update */
static sensor_status_t family_update(sensor_sample_t *sensor,
                                     const struct timeval * now) {
    /* Sanity checks are done in sensor_init() and sensor_update_check() */
    memory_priv_t * priv = (memory_priv_t *) sensor->desc->family->priv;

    if (now == NULL) {
        sysdep_memory_get(sensor->desc->family, &(priv->memory_data));
    } else if (priv->last_update_time.tv_usec == INT_MAX) {
        sysdep_memory_get(sensor->desc->family, &(priv->memory_data));
        priv->last_update_time = *now;
    } else {
        /* Because all memory datas are retrieved at once, don't repeat it for each sensor */
        struct timeval  elapsed;

        timersub(now, &(priv->last_update_time), &elapsed);
        if (timercmp(&elapsed, &(sensor->watch->update_interval), >=)) {
            sysdep_memory_get(sensor->desc->family, &(priv->memory_data));
            priv->last_update_time = *now;
        }
    }

    /* Always update the sensor Value: we are called because sensor timeout expired.
     * and another sensor with different timeout could have updated global data */
    return sensor_value_fromraw(sensor->desc->key, &sensor->value);
}

/** family-specific information */
const sensor_family_info_t g_sensor_family_memory = {
    .name = "memory",
    .init = family_init,
    .free = family_free,
    .update = family_update,
    .list = family_list,
    .notify = NULL,
    .write = NULL,
    .free_desc = NULL
};

#if 0
/** to be removed */
int memory_print () {
	memory_data_t data;
    sensor_family_t family = { .log = NULL };
    if (sysdep_memory_get(&family, &data) != SENSOR_SUCCESS)
        return 0;
    return fprintf(stdout, "memory_active %f\nmemory_wired %f\nmemory_inactive %f\nmemory_free "
                           "%f\nmemory_total %f\nmemory_utilization_perc %u\n",
                   (float)(data.active/1048576.0), (float)(data.wired/1048576.0),
                   (float)(data.inactive/1048576.0), (float)(data.free/1048576.0),
                   (float)(data.total/1048576.0), data.used_percent);
}
#endif

