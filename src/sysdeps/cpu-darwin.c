/*
 * Copyright (C) 2017-2019 Vincent Sallaberry
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
#ifndef __APPLE__
# warning "building cpu-darwin.c without __APPLE__ defined"
#endif
#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysctl.h>

#include <mach/mach_init.h>
#include <mach/mach_host.h>
#include <mach/vm_map.h>

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "vlib/log.h"

#include "cpu_private.h"

static sensor_status_t cpu_get2(sensor_family_t * family,
                                cpu_data_t *data, struct timeval *elapsed) {
    int mib[] = {
	    CTL_VM,
	    VM_LOADAVG,
    };
    struct loadavg loadavg;
    size_t len = sizeof(loadavg);
    if (sysctl(mib, sizeof(mib) / sizeof(*mib), &loadavg, &len, NULL, 0) < 0) {
	    LOG_INFO(family->log, "%s(): sysctl(buf): %s", __func__, strerror(errno));
	    return SENSOR_ERROR;
    }

    LOG_INFO(family->log, "CPU FSSCALE:%ld L0 %u(%f) L1: %u(%f) L2: %u(%f)",
           loadavg.fscale,
           loadavg.ldavg[0], (float)loadavg.ldavg[0]/loadavg.fscale,
           loadavg.ldavg[1], (float)loadavg.ldavg[1]/loadavg.fscale,
           loadavg.ldavg[2], (float)loadavg.ldavg[2]/loadavg.fscale);

    //data->usage = 55;
    (void)elapsed;
    (void)data;
    return SENSOR_SUCCESS;
}

sensor_status_t cpu_get(sensor_family_t * family, struct timeval *elapsed) {
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
        LOG_ERROR(family->log, "%s/%s(): error host_processo_info", __FILE__, __func__);
        return SENSOR_ERROR;
    }
    if (data->nb_cpus == 0) {
        // array size = 1 global measure + 1 per cpu.
        if ((data->ticks = calloc(n_cpus + 1, sizeof(cpu_tick_t))) == NULL) {
            LOG_ERROR(family->log, "%s/%s(): cannot allocate cpu_ticks", __FILE__, __func__);
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
                              "user:%u nice:%u sys:%u idle:%u CLK_TCK:%u",
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

