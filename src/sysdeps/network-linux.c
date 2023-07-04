/*
 * Copyright (C) 2020,2023 Vincent Sallaberry
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
 * network-linux for Generic Sensor Management Library.
 */
#include <unistd.h>

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#include "vlib/util.h"

#include "network_private.h"

/* ************************************************************************ */

#ifndef NET_DEV_FILE
#define NET_DEV_FILE   "/proc/net/dev"
#endif

typedef struct {
    FILE *  stat;
    char *  stat_line;
    size_t  stat_linesz;
} net_linux_t;


/* ************************************************************************ */
sensor_status_t sysdep_network_support(sensor_family_t * family, const char * label) {
    (void) label;
    (void) family;
    return SENSOR_SUCCESS;
}

/* ************************************************************************ */
sensor_status_t sysdep_network_init(sensor_family_t * family) {
    network_priv_t *priv = (family->priv);
    net_linux_t *   sysdep;

    if (priv->sysdep == NULL) {
        priv->sysdep = calloc(1, sizeof(net_linux_t));
        if (priv->sysdep == NULL) {
            LOG_ERROR(family->log, "error, cannot malloc %s sysdep data", family->info->name);
            errno=ENOMEM;
            return SENSOR_ERROR;
        }

        sysdep = priv->sysdep;
        sysdep->stat_line = NULL;
        sysdep->stat_linesz = 0;
        if ((sysdep->stat = fopen(NET_DEV_FILE, "r")) == NULL) {
            LOG_ERROR(family->log, "error while openning %s", NET_DEV_FILE);
            errno=ENOENT;
            return SENSOR_ERROR;
        }
    } else {
        sysdep = priv->sysdep;
    }

    fseek(sysdep->stat, 0, SEEK_SET);

    return SENSOR_SUCCESS;
}

/* ************************************************************************ */
sensor_status_t sysdep_network_destroy(sensor_family_t * family) {
    network_priv_t * priv = (network_priv_t *) family->priv;

    if (priv != NULL && priv->sysdep != NULL) {
        net_linux_t * sysdep = (net_linux_t *) priv->sysdep;

        if (sysdep->stat != NULL) {
            fclose(sysdep->stat);
            sysdep->stat = NULL;
        }
        if (sysdep->stat_line != NULL)
            free(sysdep->stat_line);
        sysdep->stat_line = NULL;
        priv->sysdep = NULL;
        free(sysdep);
    }
    return SENSOR_SUCCESS;
}

/* ************************************************************************ */
sensor_status_t     sysdep_network_get(
                        sensor_family_t *   family,
                        network_data_t *    data,
                        struct timeval *    elapsed) {
    network_priv_t *priv = (family->priv);
    net_linux_t *   sysdep = priv->sysdep;
    ssize_t         linesz;
    uint64_t total_ibytes = 0;
    uint64_t total_obytes = 0;
    uint64_t phy_ibytes = 0;
    uint64_t phy_obytes = 0;

    if (priv->sysdep == NULL) {
        LOG_ERROR(family->log, "error, bad %s sysdep data", family->info->name);
        errno = EFAULT;
        return SENSOR_ERROR;
    }

    if (sysdep->stat == NULL || fseek(sysdep->stat, 0, SEEK_SET) != 0) {
        if (sysdep->stat != NULL)
            fclose(sysdep->stat);
        if ((sysdep->stat = fopen(NET_DEV_FILE, "r")) == NULL) {
            return SENSOR_ERROR;
        }
    }

    while ((linesz = getline(&sysdep->stat_line, &sysdep->stat_linesz, sysdep->stat)) > 0) {
        char * line = sysdep->stat_line;
        const char * token, * value, * next = line;
        ssize_t tok_len, val_len;
        size_t maxlen = linesz;
        unsigned int val_idx = 0;
        int phys;

        if (sysdep->stat_line == NULL)
            break ;

        /* /proc/net/dev format: {
         * Inter-|   Receive                                                |  Transmit
         * face  |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed
         * lo:    536913    2398    0    0    0     0          0         0   532713    2398    0    0    0     0       0          0
         * enp6s9:387310     731    0    0    0     0          0         0    51693     645    0    0    0     0       0          0
         *  ... */
        LOG_DEBUG(family->log, "%s LINE (sz:%zu) %s", NET_DEV_FILE,
                  linesz, line);

        if (linesz <= 1)
            continue ;

        /* get interface name */
        while (*next == ' ' || *next == '\t') {
            ++next;
            --maxlen;
        }
        tok_len = strtok_ro_r(&token, ":", &next, &maxlen, 0);
        if (tok_len == 0 || *next == 0)
            continue ;

        phys = (strncasecmp(token, "lo", tok_len) != 0);

        /* get values */
        while((val_len = strtok_ro_r(&value, " ", &next, &maxlen, 0)) > 0 || *next) {
            //uint64_t lo_ibytes = 0;
            //uint64_t lo_obytes = 0;
            uint64_t ibytes;
            uint64_t obytes;

            if (val_len == 0)
                continue ;
            switch (val_idx) {
                case 0:
                    /* received bytes */
                    ibytes = strtoul(value, NULL, 0);
                    total_ibytes += ibytes;
                    if (phys)
                        phy_ibytes += ibytes;
                    break ;
                case 1:
                    /* received packets */
                    break ;
                case 2:
                    /* receive errors */
                    break ;
                case 3:
                    /* receive drops */
                    break ;
                case 4:
                    /* receive fifo */
                    break ;
                case 5:
                    /* receive frame */
                    break ;
                case 6:
                    /* receive compressed */
                    break ;
                case 7:
                    /* receive multicast */
                    break ;
                case 8:
                    /* sent bytes */
                    obytes = strtoul(value, NULL, 0);
                    total_obytes += obytes;
                    if (phys)
                        phy_obytes += obytes;
                    break ;
                case 9:
                    /* sent packets */
                    break ;
                case 10:
                    /* send errs */
                    break ;
                case 11:
                    /* send drops */
                    break ;
                case 12:
                    /* send fifos */
                    break ;
                case 13:
                    /* send colls */
                    break ;
                case 14:
                    /* send carrier */
                    break ;
                case 15:
                    /* send compressed */
                    break ;
            }

            ++val_idx;
        }
            ; /* nothing */
        if (val_len == 0)
            continue ;
    }

    if (elapsed == NULL) {
        data->ibytespersec = 0;
        data->obytespersec = 0;
        data->phy_ibytespersec = 0;
        data->phy_obytespersec = 0;
    } else {
        data->ibytespersec = (((total_ibytes - data->ibytes) * 1000)
                                / (elapsed->tv_sec * 1000 + elapsed->tv_usec / 1000));
        data->obytespersec = (((total_obytes - data->obytes) * 1000)
                                / (elapsed->tv_sec * 1000 + elapsed->tv_usec / 1000));
        data->phy_ibytespersec = (((phy_ibytes - data->phy_ibytes) * 1000)
                                    / (elapsed->tv_sec * 1000 + elapsed->tv_usec / 1000));
        data->phy_obytespersec = (((phy_obytes - data->phy_obytes) * 1000)
                                    / (elapsed->tv_sec * 1000 + elapsed->tv_usec / 1000));
    }

    data->ibytes = total_ibytes;
    data->obytes = total_obytes;
    data->phy_ibytes = phy_ibytes;
    data->phy_obytes = phy_obytes;

    return SENSOR_SUCCESS;
}

