/*
 * Copyright (C) 2023 Vincent Sallaberry
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
 * memory open-bsd implementation for Generic Sensor Management Library.
 */
#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/resource.h>
#include <uvm/uvm_extern.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <stdint.h>
#include <errno.h>

#ifndef PRIu64
# define PRIu64 "llu"
#endif

#if defined(__NetBSD__)
# define MEM_SYSCTL_VM_UVMEXP	VM_UVMEXP2
typedef struct uvmexp_sysctl mem_uvmexp_t;
#else
# define MEM_SYSCTL_VM_UVMEXP	VM_UVMEXP
typedef struct uvmexp mem_uvmexp_t;
#endif

#include <fnmatch.h>

#include "memory_private.h"

sensor_status_t     sysdep_memory_support(sensor_family_t * family, const char * label) {
    (void) family;
    (void) label;
    /*if (label != NULL) {
        if (fnmatch("*swap*", label, FNM_CASEFOLD) == 0) {
            return SENSOR_ERROR;
        }
    }*/
    return SENSOR_SUCCESS;
}

sensor_status_t     sysdep_memory_init(sensor_family_t * family) {
    (void) family;
    return SENSOR_SUCCESS;
}

void                sysdep_memory_destroy(sensor_family_t * family) {
    (void) family;
}

/**
 * Internal memory info update.
 */
sensor_status_t sysdep_memory_get(sensor_family_t * family, memory_data_t *data) {
	mem_uvmexp_t        uvmexp;
	int                 mib[] = { CTL_VM,  MEM_SYSCTL_VM_UVMEXP };
    size_t              size = sizeof(uvmexp);
    unsigned int pgsz;

    if (sysctl(mib, sizeof(mib) / sizeof(*mib), &uvmexp, &size, NULL, 0) < 0) {
        LOG_WARN(family->log, "sysctl(CTL_VM) error: %s", strerror(errno));
        return SENSOR_ERROR;
    }

    pgsz = uvmexp.pagesize;

    data->active = uvmexp.active * pgsz;
    data->inactive = uvmexp.inactive * pgsz;
    data->wired = uvmexp.wired * pgsz;
    data->free = uvmexp.free * pgsz;
    data->used = data->active + data->wired;
    data->total = uvmexp.npages * pgsz;
    if (data->total == 0)
        data->used_percent = 100;
    else
        data->used_percent = ((data->used/1024.0) / (data->total/1024.0)) * 100;

    data->total_swap = uvmexp.swpages * pgsz;
    data->free_swap = uvmexp.swpginuse * pgsz;
    data->used_swap = data->total_swap - data->free_swap;
    if (data->total == 0)
        data->used_swap_percent = 100;
    else
        data->used_swap_percent = ((data->used_swap/1024.0) / (data->total_swap/1024.0)) * 100;

    return SENSOR_SUCCESS;
}

