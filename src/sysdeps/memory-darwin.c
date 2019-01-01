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
# warning "building memory-darwin.c without __APPLE__ defined"
#endif

#include <mach/mach.h>
#include <mach/mach_error.h>

#include "memory_private.h"

/**
 * Internal memory info update.
 */
sensor_status_t memory_get(sensor_family_t * family, memory_data_t *data) {
	mach_port_t host = mach_host_self();
	if (!host) {
		LOG_ERROR(family->log, "Could not get mach reference.");
		return SENSOR_ERROR;
	}

    vm_statistics_data_t vmStats;
	mach_msg_type_number_t vmCount = HOST_VM_INFO_COUNT;
	if (host_statistics(host, HOST_VM_INFO, (host_info_t)&vmStats, &vmCount) != KERN_SUCCESS) {
        LOG_ERROR(family->log, "Could not get mach reference.");
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

