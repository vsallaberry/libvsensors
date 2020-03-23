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
 * openbsd cpu interface for Generic Sensor Management Library.
 */
#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/dkstat.h>
#include <sys/resource.h>
#include <fcntl.h>

#ifndef OpenBSD
# warning "building file without OpenBSD defined"
#endif

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fnmatch.h>

#define SENSOR_CPU_KVM 0

#if SENSOR_CPU_KVM
#include <kvm.h>
#endif

#include "vlib/log.h"

#include "cpu_private.h"

/* ************************************************************************ */

typedef struct {
#if SENSOR_CPU_KVM
    kvm_t *         kvm;
#endif
    long            *cp_times;
} sysdep_cpu_data_t;

/* ************************************************************************ */
sensor_status_t sysdep_cpu_support(sensor_family_t * family, const char * label) {
    (void) label;
    (void) family;
    if (label == NULL) {
        return SENSOR_SUCCESS;
    }
#ifndef KERN_CPTIME2
    if (fnmatch("*cpu[0-9]*", label, 0) == 0) {
        return SENSOR_ERROR;
    }
#endif
    return SENSOR_SUCCESS;
}

/* ************************************************************************ */
unsigned int    sysdep_cpu_nb(sensor_family_t * family) {
    cpu_priv_t *        priv = (cpu_priv_t *) family->priv;
    sysdep_cpu_data_t * sysdep;
    int                 mib[] = { CTL_HW, HW_NCPU };

    unsigned int    n_cpus;
    size_t          size = sizeof(n_cpus);

    if (sysctl(mib, sizeof(mib) / sizeof(*mib), &n_cpus, &size, NULL, 0) < 0) {
	    LOG_WARN(family->log, "%s(): sysctl(buf): %s", __func__, strerror(errno));
        errno = EAGAIN;
	    return 0;
    }

    if (priv->sysdep == NULL) {
        if ((sysdep = priv->sysdep = malloc(sizeof(sysdep_cpu_data_t))) != NULL) {
#if SENSOR_CPU_KVM
            char    errbuf[_POSIX2_LINE_MAX];

            sysdep->kvm = kvm_openfiles(NULL, NULL, NULL, O_RDONLY, errbuf);
            if (sysdep->kvm == NULL) {
                LOG_ERROR(family->log, "%s(): error cannot access kvm: %s",
                          __func__, strerror(errno));
            }
#endif
            if ((sysdep->cp_times = malloc(sizeof(*(sysdep->cp_times))
                                           * CPUSTATES * (n_cpus + 1))) == NULL) {
                LOG_ERROR(family->log, "%s(): cannot malloc cp_times: %s",
                    __func__, strerror(errno));
            }
        }
    }

    return n_cpus;
}

/* ************************************************************************ */
unsigned int    sysdep_cpu_sysctl(sensor_family_t * family, long * cp_time, int n) {

    //cpu_priv_t *        priv = (cpu_priv_t *) family->priv;
    (void) family;
#ifdef KERN_CPTIME2
    int                 mib[] = { CTL_KERN, KERN_CPTIME2, 0 };
#endif
    int                 mib0[] = { CTL_KERN, KERN_CPTIME };

    size_t          	size;;

    size=0;
	size=sizeof(*cp_time)*CPUSTATES;

#ifdef KERN_CPTIME2
    if (n >= 0) {
	    mib[2] = n;
        if (sysctl(mib, sizeof(mib) / sizeof(*mib), cp_time, &size, NULL, 0) < 0) {
		    return SENSOR_ERROR;
        }
    } else
#endif
    {
	    if (sysctl(mib0, sizeof(mib0)/sizeof(*mib0), cp_time, &size, NULL, 0) < 0) {
		    return SENSOR_ERROR;
        }
    }
	return SENSOR_SUCCESS;
}

/* ************************************************************************ */
void            sysdep_cpu_destroy(sensor_family_t * family) {
    cpu_priv_t *        priv = (cpu_priv_t *) family->priv;

    if (priv != NULL && priv->sysdep != NULL) {
        sysdep_cpu_data_t * sysdep = (sysdep_cpu_data_t *) priv->sysdep;
#if SENSOR_CPU_KVM
        if (sysdep->kvm != NULL)
            kvm_close(sysdep->kvm);
        sysdep->kvm = NULL;
#endif
        if (sysdep->cp_times != NULL)
            free(sysdep->cp_times);
        sysdep->cp_times = NULL;
        priv->sysdep = NULL;
        free(sysdep);
    }
}

/* ************************************************************************ */
sensor_status_t sysdep_cpu_get(sensor_family_t * family, struct timeval *elapsed) {
    cpu_priv_t *                        priv = (cpu_priv_t *) family->priv;
    cpu_data_t *                        data = &priv->cpu_data;
    sysdep_cpu_data_t *                 sysdep = (sysdep_cpu_data_t *) priv->sysdep;
    unsigned int                        i;
#if SENSOR_CPU_KVM
    static struct nlist                 nl[] = {
                                            { .n_name = "_cp_time", .n_type = 0}, { .n_name = NULL, }
                                        };
#endif

    if (data->nb_cpus == 0 || sysdep == NULL
    || sysdep->cp_times == NULL)
        return SENSOR_ERROR;

#if SENSOR_CPU_KVM
    if (sysdep->kvm != NULL) {
        static int mib[] = { CTL_KERN, KERN_CPTIME };
        size_t size = 0;
        sysctl(mib, sizeof(mib) / sizeof(*mib), NULL, &sz2, NULL, 0);
        if ((nl[0].n_type == 0
            &&  (kvm_nlist(sysdep->kvm, nl) < 0 || nl[0].n_type == 0))
        ||  kvm_read(sysdep->kvm, nl[0].n_value,
                     (char *)sysdep->cp_times, sizeof(*(sysdep->cp_times))*CPUSTATES)
                != sizeof(*(sysdep->cp_times)) * CPUSTATES) {
            LOG_VERBOSE(family->log, "%s(): cannot read kvm data: %s", __func__, strerror(errno));
            return SENSOR_ERROR;
        }
        //for (i = 1; i <= data->nb_cpus; ++i) {
        //   memcpy(sysdep->cp_times + i*CPUSTATES, sysdep->cp_times,
        //          sizeof(*(sysdep->cp_times)) * CPUSTATES);
        //}
    }
#endif

    /* cp_time[CP_USER], cp_time[CP_NICE],cp_time[CP_SYS], cp_time[CP_IDLE] */
    /* no data per cpu on this system */
    for (i = 0; i <= data->nb_cpus; ++i) {
        sysdep_cpu_sysctl(family, sysdep->cp_times + i*CPUSTATES,i == 0 ? -1 : (int)i - 1);
        unsigned long sys = (sysdep->cp_times[i*CPUSTATES+CP_SYS]);
        unsigned long user = (sysdep->cp_times[i*CPUSTATES+CP_USER])
                           + (sysdep->cp_times[i*CPUSTATES+CP_NICE]);
        unsigned long activity = sys + user;
        unsigned long total = activity
                              + (sysdep->cp_times[i*CPUSTATES+CP_IDLE]);

        cpu_store_ticks(family, i, sys, user, activity, total, elapsed);

        LOG_DEBUG(family->log,
            "CPU%u %u%% (usr:%u%% sys:%u%%) "
            "user:%ld nice:%ld sys:%ld idle:%ld CLK_TCK:%lu",
            i,
            data->ticks[i].activity_percent,
            data->ticks[i].user_percent,
            data->ticks[i].sys_percent,
            sysdep->cp_times[i*CPUSTATES+CP_USER],
            sysdep->cp_times[i*CPUSTATES+CP_NICE],
            sysdep->cp_times[i*CPUSTATES+CP_SYS],
            sysdep->cp_times[i*CPUSTATES+CP_IDLE], cpu_clktck());
    }

    return SENSOR_SUCCESS;
}

