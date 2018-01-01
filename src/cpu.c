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
#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysctl.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "cpu.h"

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

/* internal functions */
static sensor_status_t cpu_get(sensor_family_t * family, struct timeval *elapsed_time);

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
    priv_t * priv = (priv_t *) family->priv;
    priv->cpu_data.nb_cpus = 2; // FIXME;
    if ((priv->cpu_data.ticks = calloc(priv->cpu_data.nb_cpus + 1, sizeof(cpu_tick_t))) == NULL) { // FIXME
        LOG_ERROR(family->log, "%s/%s(): cannot allocate cpu_ticks\n", __FILE__, __func__);
        return SENSOR_ERROR;
    }
    // Not Pretty but allows to have an initiliazed array with dynamic values.
    sensor_desc_t sensors_desc[] = {
        { &priv->cpu_data.ticks[0].activity_percent, "cpus usage %", SENSOR_VALUE_UINT, family },
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
        cpu_get(sensor->desc->family, fpriv->last_update_time.tv_sec == 0 ? NULL : &elapsed);
        fpriv->last_update_time = *now;
    }
    // Always update the sensor Value;
    return sensor_value_fromraw(sensor->desc->key, &sensor->value);
}

const sensor_family_info_t g_sensor_family_cpu = {
    .name = "cpu",
    .init = family_init,
    .free = family_free,
    .update = family_update,
    .list = family_list,
};

#ifdef __APPLE__ // TODO: move sys dependent code in src/sysdeps

static sensor_status_t cpu_get2(sensor_family_t * family,
                                cpu_data_t *data, struct timeval *elapsed) {
    int mib[] = {
	    CTL_VM,
	    VM_LOADAVG,
    };
    struct loadavg loadavg;
    size_t len = sizeof(loadavg);
    if (sysctl(mib, sizeof(mib) / sizeof(*mib), &loadavg, &len, NULL, 0) < 0) {
	    LOG_INFO(family->log, "%s(): sysctl(buf): %s\n", __func__, strerror(errno));
	    return SENSOR_ERROR;
    }

    LOG_INFO(family->log, "CPU FSSCALE:%ld L0 %u(%f) L1: %u(%f) L2: %u(%f)\n",
           loadavg.fscale,
           loadavg.ldavg[0], (float)loadavg.ldavg[0]/loadavg.fscale,
           loadavg.ldavg[1], (float)loadavg.ldavg[1]/loadavg.fscale,
           loadavg.ldavg[2], (float)loadavg.ldavg[2]/loadavg.fscale);

    //data->usage = 55;
    (void)elapsed;
    (void)data;
    return SENSOR_SUCCESS;
}

#include <mach/mach_init.h>
#include <mach/mach_host.h>
#include <mach/vm_map.h>

static sensor_status_t cpu_get(sensor_family_t * family, struct timeval *elapsed) {
    priv_t *                            priv = (priv_t *) family->priv;
    cpu_data_t *                        data = &priv->cpu_data;
    processor_cpu_load_info_data_t *    pinfo = NULL;
    mach_msg_type_number_t              info_count;
    unsigned int                        i;
    unsigned int                        n_cpus;

    if (host_processor_info (mach_host_self(),
                       PROCESSOR_CPU_LOAD_INFO,
                       &n_cpus,
                       (processor_info_array_t*)&pinfo,
                       &info_count) != KERN_SUCCESS) {
        LOG_ERROR(family->log, "%s/%s(): error host_processo_info\n", __FILE__, __func__);
        return SENSOR_ERROR;
    }
    if (data->nb_cpus == 0) {
        // array size = 1 global measure + 1 per cpu.
        if ((data->ticks = calloc(n_cpus + 1, sizeof(cpu_tick_t))) == NULL) {
            LOG_ERROR(family->log, "%s/%s(): cannot allocate cpu_ticks\n", __FILE__, __func__);
            return SENSOR_ERROR;
        }
        data->nb_cpus = n_cpus;
    }

    unsigned long global_total = 0;
    unsigned long global_activity = 0;
    unsigned long global_user = 0;
    unsigned long global_sys = 0;
    for (i = 1; i <= n_cpus; i++) {
        unsigned long sys = (pinfo[i-1].cpu_ticks[CPU_STATE_SYSTEM]);
        unsigned long user = (pinfo[i-1].cpu_ticks[CPU_STATE_USER])
                           + (pinfo[i-1].cpu_ticks[CPU_STATE_NICE]);
        unsigned long activity = sys + user;
        unsigned long total = activity + (pinfo[i-1].cpu_ticks [CPU_STATE_IDLE]);
        global_total += total;
        global_activity += activity;
        global_sys += sys;
        global_user += user;
        if (elapsed != NULL) {
            data->ticks[i].activity_percent = (100 * (activity - data->ticks[i].activity) + 1) / CLK_TCK;
            data->ticks[i].user_percent = (100 * (user - data->ticks[i].user) + 1) / CLK_TCK;
            data->ticks[i].sys_percent = (100 * (sys - data->ticks[i].sys) + 1) / CLK_TCK;

            data->ticks[i].activity_percent
                = (1000 * data->ticks[i].activity_percent) / (elapsed->tv_sec * 1000 + elapsed->tv_usec / 1000);
            data->ticks[i].user_percent
                = (1000 * data->ticks[i].user_percent) / (elapsed->tv_sec * 1000 + elapsed->tv_usec / 1000);
            data->ticks[i].sys_percent
                = (1000 * data->ticks[i].sys_percent) / (elapsed->tv_sec * 1000 + elapsed->tv_usec / 1000);
        }
        data->ticks[i].activity = activity;
        data->ticks[i].user = user;
        data->ticks[i].sys = sys;
        LOG_INFO(family->log, "CPU%u %u%% (usr:%u%% sys:%u%%) "
                              "user:%u nice:%u sys:%u idle:%u CLK_TCK:%u\n",
               i,
               data->ticks[i].activity_percent,
               data->ticks[i].user_percent,
               data->ticks[i].sys_percent,
               pinfo[i-1].cpu_ticks[CPU_STATE_USER],
               pinfo[i-1].cpu_ticks[CPU_STATE_NICE],
               pinfo[i-1].cpu_ticks[CPU_STATE_SYSTEM],
               pinfo[i-1].cpu_ticks[CPU_STATE_IDLE], CLK_TCK);

    }
    vm_deallocate (mach_task_self(), (vm_address_t) pinfo, info_count);
    if (elapsed != NULL) {
        data->ticks[0].activity_percent = (100 * (global_activity - data->ticks[0].activity) + 1) / CLK_TCK;
        data->ticks[0].user_percent = (100 * (global_user - data->ticks[0].user) + 1) / CLK_TCK;
        data->ticks[0].sys_percent = (100 * (global_sys - data->ticks[0].sys) + 1) / CLK_TCK;

        data->ticks[0].activity_percent
            = (1000 * data->ticks[0].activity_percent) / (elapsed->tv_sec * 1000 + elapsed->tv_usec / 1000)
              / data->nb_cpus;
        data->ticks[0].user_percent
            = (1000 * data->ticks[0].user_percent) / (elapsed->tv_sec * 1000 + elapsed->tv_usec / 1000)
              / data->nb_cpus;
        data->ticks[0].sys_percent
            = (1000 * data->ticks[0].sys_percent) / (elapsed->tv_sec * 1000 + elapsed->tv_usec / 1000)
              / data->nb_cpus;
    }
    data->ticks[0].activity = global_activity;
    data->ticks[0].user = global_user;
    data->ticks[0].sys = global_sys;
    printf("CPU %u%% (usr:%u sys:%u)\n",
               data->ticks[0].activity_percent,
               data->ticks[0].user_percent,
               data->ticks[0].sys_percent);
    cpu_get2(family, NULL, NULL);
    return SENSOR_SUCCESS;
}

#else
static sensor_status_t cpu_get(sensor_family_t * family, struct timeval *elapsed) {
    priv_t * priv = (priv_t *) family->priv;
    (void)priv;
    (void)elapsed;
    (void)priv;
    LOG_ERROR(family->priv, "%s/%s(): NOT IMPLEMENTED ON THIS SYSTEM.\n", __FILE__, __func__);
    return SENSOR_ERROR;
}
#endif /* #ifdef *APPLE */

