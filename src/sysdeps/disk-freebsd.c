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
 * disk freebsd implementation for Generic Sensor Management Library.
 */
/* Thanks to GKrellM for hints about freebsd disk stats
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
#include <kvm.h>
#include <errno.h>

#include "disk_private.h"

sensor_status_t sysdep_disk_support(sensor_family_t * family, const char * label) {
    (void)family;
    (void)label;
    return SENSOR_SUCCESS;
}

#if __FreeBSD_version >= 300000
# define DISK_HAVE_DEVSTAT 1
#endif

#ifdef DISK_HAVE_DEVSTAT
# include <devstat.h>
#endif

//#if __FreeBSD_version < 300000
#ifndef DISK_HAVE_DEVSTAT
enum {
    N_DK_NDRIVE     = 0,
    N_DK_XFER,
};

static struct nlist s_nl_list[] = {
    { "_dk_ndrive" },
    { "_dk_xfer" },
    { "" }
};

typedef struct {
    kvm_t   *           kvmd;
    char                errbuf[_POSIX2_LINE_MAX];
} disk_sysdep_t;

// ***************************************************************************
sensor_status_t sysdep_disk_get (sensor_family_t * family,
                                 disk_data_t *data, struct timeval *elapsed) {
    disk_priv_t *   priv = (disk_priv_t *) family->priv;
    disk_sysdep_t * sysdep = (disk_sysdep_t *) priv->sysdep;
    int             ndevs;
    long*           cur_dk_xfer;
    uint64_t        total_rbytes = 0, total_wbytes = 0;

    if (sysdep->kvmd == NULL) {
		return -1;
    }
	if (kvm_nlist(sysdep->kvmd, s_nl_list) < 0 || s_nl_list[0].n_type == 0) {
		LOG_WARN(family->log, "kvm_nlist failed: %s", kvm_geterr(sysdep->kvmd));
        return SENSOR_ERROR;
    }
    kvm_read(sysdep->kvmd, s_nl_list[N_DK_NDRIVE].n_value, (char *)&ndevs, sizeof(ndevs));
	if (ndevs <= 0) {
		LOG_WARN(family->log, "kvm_read ndrive failed: %s", kvm_geterr(sysdep->kvmd));
		return SENSOR_ERROR;
    }
	if ((cur_dk_xfer = calloc(ndevs, sizeof(long))) == NULL) {
		return SENSOR_ERROR;
    }
	if (kvm_read(sysdep->kvmd, s_nl_list[N_DK_XFER].n_value, (char *)cur_dk_xfer,
		         ndevs * sizeof(long)) == ndevs * sizeof(long)) {
		for (int dn = 0; dn < ndevs; ++dn) {
            uint64_t    rbytes, wbytes;

            rbytes = cur_dk_xfer[dn];
            wbytes = 0;
            total_rbytes += rbytes;
            total_wbytes += wbytes;

            LOG_SCREAM(family->log, "DISK #%d write=%zu read=%zu", dn, wbytes, rbytes);
        }
    } else {
		LOG_WARN(family->log, "kvm_read drives failed: %s", kvm_geterr(sysdep->kvmd));
    }
	free(cur_dk_xfer);

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

    return SENSOR_SUCCESS;
}

// ***************************************************************************
sensor_status_t sysdep_disk_init(sensor_family_t * family) {
    disk_priv_t *   priv = (family->priv);
    disk_sysdep_t * sysdep;

    if (priv->sysdep != NULL) {
        return SENSOR_SUCCESS;
    }

    sysdep = priv->sysdep = calloc(1, sizeof(disk_sysdep_t));
    if (priv->sysdep == NULL) {
        LOG_ERROR(family->log, "error, cannot malloc %s sysdep data", family->info->name);
        errno=ENOMEM;
        return SENSOR_ERROR;
    }

    sysdep->kvmd = NULL;
    sysdep->errbuf[0] = 0;

    sysdep->kvmd = kvm_openfiles(NULL, NULL, NULL, O_RDONLY, sysdep->errbuf);
    if (sysdep->kvmd == NULL) {
        LOG_WARN(family->log, "kvm_open failed: %s", strerror(errno));
        return SENSOR_ERROR;
    }
    //disk_units = BLOCK;
    LOG_VERBOSE(family->log, "kvm openned.\n");

    return SENSOR_SUCCESS;
}

// ***************************************************************************
sensor_status_t sysdep_disk_destroy(sensor_family_t * family) {
    disk_priv_t *   priv = (family->priv);

    if (priv != NULL && priv->sysdep != NULL) {
        disk_sysdep_t * sysdep = (disk_sysdep_t *) priv->sysdep;

        if (sysdep->kvmd != NULL) {
            kvm_close(sysdep->kvmd);
            sysdep->kvmd = NULL;
        }
        priv->sysdep = NULL;
        free(sysdep);
    }
    return SENSOR_SUCCESS;
}

#else // ! #if __FreeBSD_version < 300000

typedef struct {
    struct statinfo statinfo_cur;
} disk_sysdep_t;


#if __FreeBSD_version >= 500107
# define getdevs(stats)	devstat_getdevs(NULL, stats)
# define selectdevs	devstat_selectdevs
# define bytes_read	bytes[DEVSTAT_READ]
# define bytes_written	bytes[DEVSTAT_WRITE]
#endif

// ***************************************************************************
sensor_status_t sysdep_disk_get (sensor_family_t * family,
                                 disk_data_t *data, struct timeval *elapsed) {
    disk_priv_t *   priv = (disk_priv_t *) family->priv;
    disk_sysdep_t * sysdep = (disk_sysdep_t *) priv->sysdep;
    int             ndevs;
    int             num_selected;
    int             num_selections;
    int             maxshowdevs = 10;
    struct device_selection	*dev_select = NULL;
    long            select_generation;
    char            name[32];
    uint64_t        total_rbytes = 0, total_wbytes = 0;

    if (getdevs(&sysdep->statinfo_cur) < 0) {
        return SENSOR_ERROR;
    }

    ndevs = sysdep->statinfo_cur.dinfo->numdevs;
    if (selectdevs(&dev_select, &num_selected, &num_selections,
                   &select_generation, sysdep->statinfo_cur.dinfo->generation,
                   sysdep->statinfo_cur.dinfo->devices, ndevs,
                   NULL, 0, NULL, 0,
                   DS_SELECT_ONLY, maxshowdevs, 1) >= 0) {
        for (int dn = 0; dn < ndevs; ++dn) {
            int		di;
            struct devstat	*dev;
            uint64_t    rbytes, wbytes;
            //int block_size;
            //int blocks_read, blocks_written;

            di = dev_select[dn].position;
            dev = &sysdep->statinfo_cur.dinfo->devices[di];
            // block_size = (dev->block_size > 0) ? dev->block_size : 512;
            // blocks_read = dev->bytes_read / block_size;
            // blocks_written = dev->bytes_written / block_size;

            rbytes = dev->bytes_read;
            wbytes = dev->bytes_written;
            total_rbytes += rbytes;
            total_wbytes += wbytes;

            snprintf(name, sizeof(name), "%s%d", dev->device_name,
                    dev->unit_number);
            LOG_SCREAM(family->log, "DISK #%d read=%zu write=%zu (%s)", dn, rbytes, wbytes, name);
        }
        free(dev_select);
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

    return SENSOR_SUCCESS;
}

// ***************************************************************************
sensor_status_t sysdep_disk_init(sensor_family_t * family) {
    disk_priv_t *   priv = (family->priv);
    disk_sysdep_t * sysdep;

    if (priv->sysdep != NULL) {
        return SENSOR_SUCCESS;
    }

    sysdep = priv->sysdep = calloc(1, sizeof(disk_sysdep_t));
    if (priv->sysdep == NULL) {
        LOG_ERROR(family->log, "error, cannot malloc %s sysdep data", family->info->name);
        errno=ENOMEM;
        return SENSOR_ERROR;
    }

    memset(&sysdep->statinfo_cur, 0, sizeof(sysdep->statinfo_cur));
    if ((sysdep->statinfo_cur.dinfo = (struct devinfo *) malloc(sizeof(struct devinfo))) == NULL) {
        return SENSOR_ERROR;
    }
    memset(sysdep->statinfo_cur.dinfo, 0, sizeof(struct devinfo));
    return SENSOR_SUCCESS;
}

// ***************************************************************************
sensor_status_t sysdep_disk_destroy(sensor_family_t * family) {
    disk_priv_t *   priv = (family->priv);

    if (priv != NULL && priv->sysdep != NULL) {
        disk_sysdep_t * sysdep = (disk_sysdep_t *) priv->sysdep;

        if (sysdep->statinfo_cur.dinfo != NULL) {
            free(sysdep->statinfo_cur.dinfo);
            sysdep->statinfo_cur.dinfo = NULL;
        }
        priv->sysdep = NULL;
        free(sysdep);
    }
    return SENSOR_SUCCESS;
}

#endif

