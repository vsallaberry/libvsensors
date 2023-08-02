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
 * disk netbsd implementation for Generic Sensor Management Library.
 */
/* Thanks to GKrellM for hints about netbsd disk stats
 * |  Copyright (C) 1999-2014 Bill Wilson
 * |  Author:  Bill Wilson    billw@gkrellm.net
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/param.h>
#include <sys/dkstat.h>
#include <sys/disk.h>
#include <sys/sysctl.h>
#include <errno.h>

#include "disk_private.h"

sensor_status_t sysdep_disk_support(sensor_family_t * family, const char * label) {
    (void)family;
    (void)label;
    return SENSOR_SUCCESS;
}

#ifdef HW_IOSTATS
#define HW_DISKSTATS	HW_IOSTATS
#define disk_sysctl	io_sysctl
#define dk_rbytes	rbytes
#define dk_wbytes	wbytes
#define dk_name		name
#endif

typedef struct {
    int rfu;
} disk_sysdep_t;

// ***************************************************************************
sensor_status_t sysdep_disk_init(sensor_family_t * family) {
	int mib[3] = { CTL_HW, HW_DISKSTATS, sizeof(struct disk_sysctl) };
	size_t size;

	/* Just test if the sysctl call works */
	if (sysctl(mib, 3, NULL, &size, NULL, 0) == -1)
		return SENSOR_ERROR;
	return SENSOR_SUCCESS;
}

// ***************************************************************************
sensor_status_t sysdep_disk_get (sensor_family_t * family,
                                 disk_data_t *data, struct timeval *elapsed) {
    disk_priv_t *   priv = (disk_priv_t *) family->priv;
    disk_sysdep_t * sysdep = (disk_sysdep_t *) priv->sysdep;
    int             i, n_disks, mib[3] = { CTL_HW, HW_DISKSTATS, sizeof(struct disk_sysctl) };
	size_t          size;
	uint64_t        total_rbytes = 0, total_wbytes = 0;
	struct          disk_sysctl *dk_drives;
    (void)priv;
    (void)sysdep;

	if (sysctl(mib, 3, NULL, &size, NULL, 0) == -1) {
		return SENSOR_ERROR;
    }
	dk_drives = malloc(size);
	if (dk_drives == NULL) {
		return SENSOR_ERROR;
    }
	n_disks = size / sizeof(struct disk_sysctl);

	if (sysctl(mib, 3, dk_drives, &size, NULL, 0) == -1) {
        free(dk_drives);
		return SENSOR_ERROR;
    }

	for (i = 0; i < n_disks; i++) {
    	uint64_t rbytes, wbytes;
#      if __NetBSD_Version__ >= 106110000
		rbytes = dk_drives[i].dk_rbytes;
		wbytes = dk_drives[i].dk_wbytes;
#      else
		rbytes = dk_drives[i].dk_bytes;
		wbytes = 0;
#      endif
        total_rbytes += rbytes;
        total_wbytes += wbytes;

        LOG_SCREAM(family->log, "DISK #%d read=%zu, write=%zu (%s)", i, rbytes, wbytes, dk_drives[i].dk_name);
	}

    uint64_t phy_rbytes = total_rbytes, phy_wbytes = total_wbytes;
    if (elapsed == NULL) {
        data->ibytespersec = 0;
        data->obytespersec = 0;
        data->phy_ibytespersec = 0;
        data->phy_obytespersec = 0;
    } else {
        data->ibytespersec = (((total_rbytes - data->ibytes) * 1000)
                                / (elapsed->tv_sec * 1000 + elapsed->tv_usec / 1000));
        data->obytespersec = (((total_wbytes - data->obytes) * 1000)
                                / (elapsed->tv_sec * 1000 + elapsed->tv_usec / 1000));
        data->phy_ibytespersec = (((phy_rbytes - data->phy_ibytes) * 1000)
                                    / (elapsed->tv_sec * 1000 + elapsed->tv_usec / 1000));
        data->phy_obytespersec = (((phy_wbytes - data->phy_obytes) * 1000)
                                    / (elapsed->tv_sec * 1000 + elapsed->tv_usec / 1000));
    }

    data->ibytes = total_rbytes;
    data->obytes = total_wbytes;
    data->phy_ibytes = phy_rbytes;
    data->phy_obytes = phy_wbytes;

	free(dk_drives);
    return SENSOR_SUCCESS;
}

// ***************************************************************************
sensor_status_t sysdep_disk_destroy(sensor_family_t * family) {
    return SENSOR_SUCCESS;
}

