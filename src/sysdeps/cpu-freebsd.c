/*
 * Copyright (C) 2020 Vincent Sallaberry
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
 * freebsd cpu interface for Generic Sensor Management Library.
 */
#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/resource.h>

#ifndef __FreeBSD_version
# warning "building file without __FreeBSD_version defined"
#endif

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "vlib/log.h"

#include "cpu_private.h"

/* ************************************************************************ */

typedef struct {
    int             cp_time_mib[6];
    size_t          cp_time_mib_sz;
    int             cp_times_mib[6];
    size_t          cp_times_mib_sz;
    long *          cp_times;
} sysdep_cpu_data_t;


/* ************************************************************************ */
sensor_status_t sysdep_cpu_support(sensor_family_t * family, const char * label) {
    (void) label;
    (void) family;
    return SENSOR_SUCCESS;
}

/* ************************************************************************ */
unsigned int    sysdep_cpu_nb(sensor_family_t * family) {
    cpu_priv_t *        priv = (cpu_priv_t *) family->priv;
    sysdep_cpu_data_t * sysdep;
    int mib[] = {
	    CTL_HW,
	    HW_NCPU /* hw.npus, kern.smp.maxcpus */
    };
    unsigned int    n_cpus;
    size_t          size = sizeof(n_cpus);

    if (sysctl(mib, sizeof(mib) / sizeof(*mib), &n_cpus, &size, NULL, 0) < 0) {
	    LOG_WARN(family->log, "%s(): sysctl(buf): %s", __func__, strerror(errno));
        errno = EAGAIN;
	    return 0;
    }

    if (priv->sysdep == NULL) {
        if ((sysdep = priv->sysdep = malloc(sizeof(sysdep_cpu_data_t))) != NULL) {
            sysdep->cp_times = calloc(n_cpus * CPUSTATES, sizeof(*sysdep->cp_times));
            sysdep->cp_time_mib_sz = sizeof(sysdep->cp_time_mib) / sizeof(*sysdep->cp_time_mib);
            sysdep->cp_times_mib_sz = sizeof(sysdep->cp_times_mib) / sizeof(*sysdep->cp_times_mib);
            if (sysctlnametomib("kern.cp_time",  sysdep->cp_time_mib, &sysdep->cp_time_mib_sz) != 0) {
                LOG_WARN(family->log, "error: sysctlnametomib('kern.cp_time')");
                sysdep->cp_time_mib_sz = 0;
            }
            if (sysctlnametomib("kern.cp_times", sysdep->cp_times_mib, &sysdep->cp_times_mib_sz) != 0) {
                LOG_WARN(family->log, "error: sysctlnametomib('kern.cp_times')");
                sysdep->cp_times_mib_sz = 0;
            }
        }
    }

    return n_cpus;
}

/* ************************************************************************ */
void            sysdep_cpu_destroy(sensor_family_t * family) {
    cpu_priv_t *        priv = (cpu_priv_t *) family->priv;

    if (priv != NULL && priv->sysdep != NULL) {
        sysdep_cpu_data_t * sysdep = (sysdep_cpu_data_t *) priv->sysdep;

        if (sysdep->cp_times != NULL)
            free(sysdep->cp_times);
        priv->sysdep = NULL;
        free(sysdep);
    }
}

/* ************************************************************************ */
sensor_status_t sysdep_cpu_get(sensor_family_t * family, struct timeval *elapsed) {
    cpu_priv_t *                priv = (cpu_priv_t *) family->priv;
    cpu_data_t *                data = &priv->cpu_data;
    sysdep_cpu_data_t *         sysdep = (sysdep_cpu_data_t *) priv->sysdep;
    unsigned int                i;
    size_t                      size;

    if (data->nb_cpus == 0 || sysdep == NULL || sysdep->cp_times == NULL)
        return SENSOR_ERROR;

    /* "kern.cp_time" -> all cpus */
    /* "kern.cp_times" -> specific cpu */

    if (data->nb_cpus > 1 && sysdep->cp_times_mib_sz > 0) {
        size = sizeof(sysdep->cp_times) * CPUSTATES * data->nb_cpus;
        if (sysctl(sysdep->cp_times_mib, sysdep->cp_times_mib_sz,
                   sysdep->cp_times, &size, NULL, 0) < 0) {
            LOG_WARN(family->log, "%s(): sysctl(cp_times): %s", __func__, strerror(errno));
            errno = EAGAIN;
            return SENSOR_ERROR;
        }
    } else if (sysdep->cp_time_mib_sz > 0) {
        size = sizeof(sysdep->cp_times) * CPUSTATES * data->nb_cpus;
        if (sysctl(sysdep->cp_time_mib, sysdep->cp_time_mib_sz,
                   sysdep->cp_times, &size, NULL, 0) < 0) {
            LOG_WARN(family->log, "%s(): sysctl(cp_time): %s", __func__, strerror(errno));
            errno = EAGAIN;
            return SENSOR_ERROR;
        }
        for (i = 1; i < data->nb_cpus; ++i) {
            memcpy(sysdep->cp_times + (i * CPUSTATES), sysdep->cp_times,
                   CPUSTATES * sizeof(*sysdep->cp_times));
        }
    } else {
        errno = ENOENT;
        return SENSOR_ERROR;
    }

    for (i = 1; i <= data->nb_cpus; i++) {
        unsigned long sys = (sysdep->cp_times[(i-1) * CPUSTATES + CP_SYS]);
        unsigned long user = (sysdep->cp_times[(i-1) * CPUSTATES + CP_USER])
                           + (sysdep->cp_times[(i-1) * CPUSTATES + CP_NICE]);
        unsigned long activity = sys + user;
        unsigned long total = activity + (sysdep->cp_times[(i-1) * CPUSTATES + CP_IDLE]);

        cpu_store_ticks(family, i, sys, user, activity, total, elapsed);

        LOG_DEBUG(family->log,
            "CPU%u %u%% (usr:%u%% sys:%u%%) "
            "user:%ld nice:%ld sys:%ld idle:%ld CLK_TCK:%lu",
            i,
            data->ticks[i].activity_percent,
            data->ticks[i].user_percent,
            data->ticks[i].sys_percent,
            sysdep->cp_times[(i-1) * CPUSTATES + CP_USER],
            sysdep->cp_times[(i-1) * CPUSTATES + CP_NICE],
            sysdep->cp_times[(i-1) * CPUSTATES + CP_SYS],
            sysdep->cp_times[(i-1) * CPUSTATES + CP_IDLE], cpu_clktck());
    }

    cpu_store_ticks(family, CPU_COMPUTE_GLOBAL, 0, 0, 0, 0, elapsed);

    LOG_DEBUG(family->log,
                "CPU %u%% (usr:%u sys:%u)",
                 data->ticks[0].activity_percent,
                 data->ticks[0].user_percent,
                 data->ticks[0].sys_percent);

    return SENSOR_SUCCESS;
}

