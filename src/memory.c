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
//#import <Foundation/Foundation.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysctl.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "memory.h"

/** Iternal struct where all memory info is kept */
typedef struct {
    unsigned long   active;
    unsigned long   inactive;
    unsigned long   wired;
    unsigned long   free;
    unsigned long   used;
    unsigned long   total;
    unsigned int    used_percent;
} memory_data_t;

/** private/specific family structure */
typedef struct {
    sensor_desc_t *     sensors_desc;
    memory_data_t       memory_data;
    struct timeval      last_update_time;
} priv_t;

/* internal functions */
static sensor_status_t memory_get (sensor_family_t * family, memory_data_t *data);

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
        { &priv->memory_data.active,          "active memory",      SENSOR_VALUE_ULONG, family },
        { &priv->memory_data.inactive,        "inactive memory",    SENSOR_VALUE_ULONG, family },
        { &priv->memory_data.wired,           "wired memory",       SENSOR_VALUE_ULONG, family },
        { &priv->memory_data.free,            "free memory",        SENSOR_VALUE_ULONG, family },
        { &priv->memory_data.used,            "used memory",        SENSOR_VALUE_ULONG, family },
        { &priv->memory_data.total,           "total memory",       SENSOR_VALUE_ULONG, family },
        { &priv->memory_data.used_percent,    "used memory %",      SENSOR_VALUE_UINT,  family },
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
        LOG_ERROR(family->log, "error: %s data already initialized\n", family->info->name);
        family_free(family);
        return SENSOR_ERROR;
    }
    if ((family->priv = calloc(1, sizeof(priv_t))) == NULL) {
        LOG_ERROR(family->log, "cannot allocate private %s data\n", family->info->name);
        return SENSOR_ERROR;
    }
    if (init_private_data(family) != SENSOR_SUCCESS) {
        LOG_ERROR(family->log, "cannot initialize private %s data\n", family->info->name);
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
static sensor_status_t family_update(sensor_sample_t *sensor,
                                     struct timeval * now) {
    // Sanity checks are done in sensor_update_get()
    priv_t * fpriv = (priv_t *) sensor->desc->family->priv;
    if (fpriv == NULL) {
       return SENSOR_ERROR;
    }
    // Because all memory datas are retrieved at once, don't repeat it for each sensor.
    struct timeval elapsed;
    struct timeval limit = {
        .tv_sec     = sensor->watch.update_interval_ms / 1000,
        .tv_usec    = (sensor->watch.update_interval_ms % 1000) * 1000,
    };
    timersub(now, &fpriv->last_update_time, &elapsed);
    if (timercmp(&elapsed, &limit, >=)) {
        memory_get(sensor->desc->family, &fpriv->memory_data);
        fpriv->last_update_time = *now;
    }
    // Always update the sensor Value;
    return sensor_value_fromraw(sensor->desc->key, &sensor->value);
}

/** family-specific information */
const sensor_family_info_t g_sensor_family_memory = {
    .name = "memory",
    .init = family_init,
    .free = family_free,
    .update = family_update,
    .list = family_list,
};

#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/mach_error.h>

/**
 * Internal memory info update.
 * From:
 *  	check_osx_mem
 *  	by Jedda Wignall
 *	    http://jedda.me
 *	    Tool to report OSX memory utilization to nagios and Groundwork server.
 */
static sensor_status_t memory_get (sensor_family_t * family, memory_data_t *data) {
	mach_port_t host = mach_host_self();
	if (!host) {
		LOG_ERROR(family->log, "Could not get mach reference.\n");
		return SENSOR_ERROR;
	}

    vm_statistics_data_t vmStats;
	mach_msg_type_number_t vmCount = HOST_VM_INFO_COUNT;
	if (host_statistics(host, HOST_VM_INFO, (host_info_t)&vmStats, &vmCount) != KERN_SUCCESS) {
        LOG_ERROR(family->log, "Could not get mach reference.\n");
        return SENSOR_ERROR;
	}

    data->active = ((natural_t)vmStats.active_count) * (unsigned long)((natural_t)vm_page_size);
    data->inactive = ((natural_t)vmStats.inactive_count) * (unsigned long)((natural_t)vm_page_size);
    data->wired = ((natural_t)vmStats.wire_count) * (unsigned long)((natural_t)vm_page_size);
    data->free = ((natural_t)vmStats.free_count) * (unsigned long)((natural_t)vm_page_size);
    data->used = data->active + data->wired;
    data->total = data->active + data->inactive + data->free + data->wired;
    data->used_percent = ((data->used/1024.0) / (data->total/1024.0)) * 100;

    return SENSOR_SUCCESS;
}
#else
static sensor_status_t memory_get (sensor_family_t * family, memory_data_t *data) {
    (void)data;
    LOG_ERROR(family->log, "%s/%s(): NOT IMPLEMENTED ON THIS SYSTEM.\n", __FILE__, __func__);
    return SENSOR_ERROR;
}
#endif /* ifdef *APPLE */

/** to be removed */
int memory_print () {
	memory_data_t data;
    sensor_family_t family = { .log = NULL };
    if (memory_get(&family, &data) != SENSOR_SUCCESS)
        return 0;
    return fprintf(stdout, "memory_active %f\nmemory_wired %f\nmemory_inactive %f\nmemory_free "
                           "%f\nmemory_total %f\nmemory_utilization_perc %u\n",
                   (float)(data.active/1048576.0), (float)(data.wired/1048576.0),
                   (float)(data.inactive/1048576.0), (float)(data.free/1048576.0),
                   (float)(data.total/1048576.0), data.used_percent);
}


