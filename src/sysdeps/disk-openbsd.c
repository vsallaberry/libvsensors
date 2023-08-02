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
/* Thanks to GKrellM for hints about openbsd disk stats
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

enum {
    X_DISK_COUNT = 0,
    X_DISKLIST,
};

static struct nlist s_nl_disk[] = {
    { "_disk_count" },      // number of disks
    { "_disklist" },        // disks info
    { NULL },
};

typedef struct {
    struct disk *       dkdisks;    // kernel disk list
    kvm_t   *           kvmd;
    char                errbuf[_POSIX2_LINE_MAX];
    int                 n_disks;
} disk_sysdep_t;

// ***************************************************************************
sensor_status_t sysdep_disk_get (sensor_family_t * family,
                                 disk_data_t *data, struct timeval *elapsed) {
    disk_priv_t *   priv = (disk_priv_t *) family->priv;
    disk_sysdep_t * sysdep = (disk_sysdep_t *) priv->sysdep;
    struct disk     d, *p;
    int             i;
    char            buf[20];
    uint64_t        total_rbytes = 0, total_wbytes = 0;

    if (sysdep->kvmd == NULL) {
        return SENSOR_ERROR;
    }
    if (sysdep->n_disks <= 0) {
        return SENSOR_ERROR;
    }
    if (s_nl_disk[0].n_type == 0) {
        return SENSOR_ERROR;
    }

    for (i = 0, p = sysdep->dkdisks; i < sysdep->n_disks; p = d.dk_link.tqe_next, ++i) {
        if (kvm_read(sysdep->kvmd, (u_long)p, &d, sizeof(d)) == sizeof(d)) {
            uint64_t     rbytes, wbytes;

            if (kvm_read(sysdep->kvmd, (u_long)d.dk_name, buf, sizeof(buf)) != sizeof(buf)) {
                // fallback to default name if kvm_read failed
                snprintf(buf, sizeof(buf), "%s%02d", "Disk", i);
            }
            rbytes = d.dk_rbytes;
            wbytes = d.dk_wbytes;
            total_rbytes += rbytes;
            total_wbytes += wbytes;

            LOG_SCREAM(family->log, "DISK %d read=%zu write=%zu (%s)", i, rbytes, wbytes, buf);
        } else {
            LOG_WARN(family->log, "cannot read #%d: %s ..", i, kvm_geterr(sysdep->kvmd));
        }
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

static int get_ndisks(sensor_family_t * family) {
    disk_priv_t *           priv = (disk_priv_t *) family->priv;
    disk_sysdep_t *         sysdep = (disk_sysdep_t *) priv->sysdep;
    struct disklist_head    head;
    int                     n_disks;

    if (sysdep->kvmd == NULL) {
        n_disks = 0;
        sysdep->n_disks = n_disks;
        return n_disks;
    }

    /* get disk count */
    if (kvm_nlist(sysdep->kvmd, s_nl_disk) >= 0 && s_nl_disk[0].n_type != 0) {
        if (kvm_read(sysdep->kvmd, s_nl_disk[X_DISK_COUNT].n_value,
                    (char *)&(n_disks), sizeof(n_disks)) != sizeof(n_disks)) {
            n_disks = 0;
        }
    }

    /* get first disk */
    if (n_disks > 0) {
        if (kvm_read(sysdep->kvmd, s_nl_disk[X_DISKLIST].n_value,
                    &head, sizeof(head)) != sizeof(head)) {
            n_disks = 0;
        }
        sysdep->dkdisks = head.tqh_first;
    }

    sysdep->n_disks = n_disks;
    return n_disks;
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

    sysdep->dkdisks = NULL;
    sysdep->kvmd = NULL;
    sysdep->errbuf[0] = 0;
    sysdep->n_disks = 0;

    sysdep->kvmd = kvm_openfiles(NULL, NULL, NULL, O_RDONLY, sysdep->errbuf);
    if (sysdep->kvmd == NULL) {
        LOG_ERROR(family->log, "kvm_open failed: %s", strerror(errno));
        return SENSOR_ERROR;
    }

    get_ndisks(family);
    LOG_VERBOSE(family->log, "kvm openned, ndisks = %d.", sysdep->n_disks);

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

