/*
 * Copyright (C) 2017-2020 Vincent Sallaberry
 * libvsensors <https://github.com/vsallaberry/libvsensors>
 *
 * Credits to Bill Wilson, Ben Hines and other gkrellm developers
 * (gkrellm, GPLv3, https://git.srcbox.net/gkrellm) for some hints
 * about the way to retrieve some os-specific system informations.
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

/* ************************************************************************ */
typedef struct {
    processor_cpu_load_info_data_t *    pinfo;
    mach_msg_type_number_t              info_count;
} cpu_sysdep_t;

/* ************************************************************************ */
sensor_status_t sysdep_cpu_support(sensor_family_t * family, const char * label) {
    (void) label;
    (void) family;
    return SENSOR_SUCCESS;
}

/* ************************************************************************ */
unsigned int    sysdep_cpu_nb(sensor_family_t * family) {
    int mib[] = {
	    CTL_HW,
	    HW_NCPU
    };
    unsigned int    n_cpus;
    size_t          size = sizeof(n_cpus);
    cpu_priv_t *    priv = (cpu_priv_t *) family->priv;

    if (sysctl(mib, sizeof(mib) / sizeof(*mib), &n_cpus, &size, NULL, 0) < 0) {
	    LOG_WARN(family->log, "%s(): sysctl(buf): %s", __func__, strerror(errno));
        errno = EAGAIN;
	    return 0;
    }
    if (priv != NULL && priv->sysdep == NULL) {
        if ((priv->sysdep = malloc(sizeof(cpu_sysdep_t))) == NULL) {
            LOG_WARN(family->log, "%s(): malloc(cpu_sysdep_t): %s", __func__, strerror(errno));
        }
        else {
            cpu_sysdep_t * sysdep = (cpu_sysdep_t *) priv->sysdep;
            sysdep->pinfo = NULL;
            sysdep->info_count = 0;
        }
    }
    return n_cpus;
}

/* ************************************************************************ */
void            sysdep_cpu_destroy(sensor_family_t * family) {
    cpu_priv_t *    priv    = (cpu_priv_t *) family->priv;

    if (priv != NULL && priv->sysdep != NULL) {
        cpu_sysdep_t *  sysdep  = (cpu_sysdep_t *) priv->sysdep;

        if (sysdep->pinfo != NULL) {
            vm_deallocate(mach_task_self(), (vm_address_t) sysdep->pinfo, sysdep->info_count);
            sysdep->pinfo = NULL;
        }
        priv->sysdep = NULL;
        free(sysdep);
    }
}


/* ************************************************************************ */
sensor_status_t sysdep_cpu_get(sensor_family_t * family, struct timeval *elapsed) {
    cpu_priv_t *                        priv        = (cpu_priv_t *) family->priv;
    cpu_sysdep_t *                      sysdep      = (cpu_sysdep_t *) priv->sysdep;
    cpu_data_t *                        data        = &(priv->cpu_data);
    unsigned int                        i;
    unsigned int                        n_cpus      = data->nb_cpus;
    processor_cpu_load_info_data_t *    pinfo       = sysdep->pinfo;
    mach_msg_type_number_t              info_count  = sysdep->info_count;

    if (sysdep == NULL) {
        return SENSOR_ERROR;
    }

    if (host_processor_info (mach_host_self(),
                       PROCESSOR_CPU_LOAD_INFO,
                       &n_cpus,
                       (processor_info_array_t *) &(pinfo),
                       &info_count) != KERN_SUCCESS
    ||  pinfo == NULL) {
        LOG_ERROR(family->log, "%s/%s(): error host_processo_info", __FILE__, __func__);
        return SENSOR_ERROR;
    }
    if (pinfo != sysdep->pinfo || info_count != sysdep->info_count || n_cpus != data->nb_cpus) {
        LOG_SCREAM(family->log, "cpu-darwin: pinfo reallocated = %lx", (unsigned long) pinfo);
        if (sysdep->pinfo != NULL)
            vm_deallocate(mach_task_self(), (vm_address_t) sysdep->pinfo, sysdep->info_count);
        sysdep->pinfo = pinfo;
        sysdep->info_count = info_count;
    }

    if (n_cpus > data->nb_cpus) {
        // array size = 1 global measure + 1 per cpu.
        LOG_VERBOSE(family->log, "number of CPUs changed ! old:%u new:%u", data->nb_cpus, n_cpus);
        if ((data->ticks = realloc(data->ticks, (n_cpus + 1) * sizeof(cpu_tick_t))) == NULL) {
            data->nb_cpus = 0;
            LOG_ERROR(family->log, "%s/%s(): cannot allocate cpu_ticks", __FILE__, __func__);
            return SENSOR_ERROR;
        } else {
            memset(data->ticks + data->nb_cpus + 1, 0,
                   (n_cpus - data->nb_cpus) * sizeof(cpu_tick_t));
        }
        data->nb_cpus = n_cpus;
    }

    for (i = 1; i <= n_cpus; i++) {
        unsigned long sys   = (pinfo[i-1].cpu_ticks[CPU_STATE_SYSTEM]);
        unsigned long user  = (pinfo[i-1].cpu_ticks[CPU_STATE_USER])
                            + (pinfo[i-1].cpu_ticks[CPU_STATE_NICE]);
        unsigned long activity = sys + user;
        unsigned long total = activity + (pinfo[i-1].cpu_ticks [CPU_STATE_IDLE]);

        cpu_store_ticks(family, i, sys, user, activity, total, elapsed);

        LOG_DEBUG(family->log,
            "CPU%u %u%% (usr:%u%% sys:%u%%) "
            "user:%u nice:%u sys:%u idle:%u CLK_TCK:%lu",
            i,
            data->ticks[i].activity_percent,
            data->ticks[i].user_percent,
            data->ticks[i].sys_percent,
            pinfo[i-1].cpu_ticks[CPU_STATE_USER],
            pinfo[i-1].cpu_ticks[CPU_STATE_NICE],
            pinfo[i-1].cpu_ticks[CPU_STATE_SYSTEM],
            pinfo[i-1].cpu_ticks[CPU_STATE_IDLE], cpu_clktck());
    }

    cpu_store_ticks(family, CPU_COMPUTE_GLOBAL, 0, 0, 0, 0, elapsed);

    LOG_DEBUG(family->log,
                    "CPU %u%% (usr:%u sys:%u)",
                    data->ticks[0].activity_percent,
                    data->ticks[0].user_percent,
                    data->ticks[0].sys_percent);

    return SENSOR_SUCCESS;
}

#if 0 // TODO to be removed
/* ************************************************************************ */
static sensor_status_t cpu_get2(sensor_family_t * family,
                                cpu_data_t *data, struct timeval *elapsed) {
    int mib[] = {
	    CTL_VM,
	    VM_LOADAVG,
    };
    struct loadavg loadavg;
    size_t len = sizeof(loadavg);
    if (sysctl(mib, sizeof(mib) / sizeof(*mib), &loadavg, &len, NULL, 0) < 0) {
	    LOG_WARN(family->log, "%s(): sysctl(buf): %s", __func__, strerror(errno));
	    return SENSOR_ERROR;
    }

    LOG_DEBUG(family->log, "CPU FSSCALE:%ld L0 %u(%f) L1: %u(%f) L2: %u(%f)",
           loadavg.fscale,
           loadavg.ldavg[0], (float)loadavg.ldavg[0]/loadavg.fscale,
           loadavg.ldavg[1], (float)loadavg.ldavg[1]/loadavg.fscale,
           loadavg.ldavg[2], (float)loadavg.ldavg[2]/loadavg.fscale);

    //data->usage = 55;
    (void)elapsed;
    (void)data;
    return SENSOR_SUCCESS;
}
#endif

