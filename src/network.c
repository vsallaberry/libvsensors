/*
 * Copyright (C) 2017 Vincent Sallaberry
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
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <net/if.h>
#include <net/route.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "network.h"

/** Iternal struct where all network info is kept */
typedef struct {
    unsigned long ibytes;
    unsigned long obytes;
    unsigned long ibytespersec;
    unsigned long obytespersec;
} network_data_t;

/** private/specific network family structure */
typedef struct {
    sensor_desc_t *     sensors_desc;
    network_data_t      network_data;
    network_data_t *    iface_data;
    struct timeval      last_update_time;
} priv_t;

/* internal functions */
static sensor_status_t network_get (sensor_family_t * family,
                                    network_data_t *data, struct timeval *elapsed_time);

/** family-specific free */
static sensor_status_t family_free(sensor_family_t *family) {
    if (family->priv) {
        priv_t *priv = (priv_t *) family->priv;
        if (priv->sensors_desc)
            free (priv->sensors_desc);
        free(family->priv);
        family->priv = NULL;
    }
    return SENSOR_SUCCESS;
}

/** family private data creation, including the sensor_desc_t data */
static sensor_status_t init_private_data(sensor_family_t *family) {
    priv_t * priv = (priv_t *) family->priv;;
    // Not Pretty but allows to have an initiliazed array with dynamic values.
    sensor_desc_t sensors_desc[] = {
        { &priv->network_data.ibytes,       "network out bytes",       SENSOR_VALUE_ULONG,  family },
        { &priv->network_data.obytes,       "network in bytes",        SENSOR_VALUE_ULONG,  family },
        { &priv->network_data.ibytespersec, "network out bytes/sec",   SENSOR_VALUE_ULONG,  family },
        { &priv->network_data.obytespersec, "network in bytes/sec",    SENSOR_VALUE_ULONG,  family },
        { NULL, NULL, 0, NULL },
    };
    if ((priv->sensors_desc
            = calloc(sizeof(sensors_desc) / sizeof(*sensors_desc), sizeof(*sensors_desc))) == NULL) {
        return SENSOR_ERROR;
    }
    memcpy(priv->sensors_desc, sensors_desc, sizeof(sensors_desc));
    return SENSOR_SUCCESS;
}

/** family-specific init */
static sensor_status_t family_init(sensor_family_t *family) {
    // Sanity checks done before in sensor_init()
    if (family->priv != NULL) {
        LOG_ERROR(family->log, "error: %s data already initialized\n", family->info->name);
        family_free(family);
        return SENSOR_ERROR;
    }
    if ((family->priv = calloc(1, sizeof(priv_t))) == NULL) {
        LOG_ERROR(family->log, "cannot allocate private %s data\n", family->info->name);
        return SENSOR_ERROR;
    }
    if (init_private_data(family) != SENSOR_SUCCESS) {
        LOG_ERROR(family->log, "cannot initialize private %s data\n", family->info->name);
        free(family->priv);
        family->priv = NULL;
        return SENSOR_ERROR;
    }
    return SENSOR_SUCCESS;
}

/** family-specific list */
static slist_t * family_list(sensor_family_t *family) {
    priv_t *    priv = (priv_t *) family->priv;
    slist_t *   list = NULL;

    for (unsigned int i_desc = 0; priv->sensors_desc[i_desc].label; i_desc++) {
        list = slist_prepend(list, &priv->sensors_desc[i_desc]);
    }
    return list;
}

/** family-specific update */
static sensor_status_t family_update(sensor_sample_t *sensor, struct timeval * now) {
    // Sanity checks are done in sensor_update_get()
    priv_t * fpriv = (priv_t *) sensor->desc->family->priv;
    if (fpriv == NULL) {
       return SENSOR_ERROR;
    }
    // Because all memory datas are retrieved at once, don't repeat it for each sensor
    struct timeval elapsed;
    struct timeval limit = {
        .tv_sec     = sensor->watch.update_interval_ms / 1000,
        .tv_usec    = (sensor->watch.update_interval_ms % 1000) * 1000,
    };
    timersub(now, &fpriv->last_update_time, &elapsed);
    if (timercmp(&elapsed, &limit, >=)) {
        network_get(sensor->desc->family,
                    &fpriv->network_data, fpriv->last_update_time.tv_sec == 0 ? NULL : &elapsed);
        fpriv->last_update_time = *now;
    }
    // Always update the sensor Value;
    return sensor_value_fromraw(sensor->desc->key, &sensor->value);
}

const sensor_family_info_t g_sensor_family_network = {
    .name = "network",
    .init = family_init,
    .free = family_free,
    .update = family_update,
    .list = family_list,
};

#ifdef __APPLE__ /* FIXME BSD? */

#include <net/if_types.h>

static sensor_status_t network_get(sensor_family_t * family,
                                   network_data_t *data, struct timeval *elapsed) {
    int mib[] = {
	    CTL_NET,
	    PF_ROUTE,
	    0,
	    0,
	    NET_RT_IFLIST2,
	    0
    };
    size_t len;
    if (sysctl(mib, sizeof(mib) / sizeof(*mib), NULL, &len, NULL, 0) < 0) {
	    LOG_ERROR(family->log, "%s(): sysctl(null): %s\n", __func__, strerror(errno));
	    return SENSOR_ERROR;
    }
    char *buf = (char*) malloc(len);
    if (sysctl(mib, sizeof(mib) / sizeof(*mib), buf, &len, NULL, 0) < 0) {
	    LOG_ERROR(family->log, "%s(): sysctl(buf): %s\n", __func__, strerror(errno));
    	return SENSOR_ERROR;
    }

    char *lim = buf + len;
    char *next = NULL;
    u_int64_t total_ibytes = 0;
    u_int64_t total_obytes = 0;
    u_int64_t lo_ibytes = 0;
    u_int64_t lo_obytes = 0;
    u_int64_t phy_ibytes = 0;
    u_int64_t phy_obytes = 0;
    u_int64_t ibytes = 0;
    u_int64_t obytes = 0;
    u_char ifi_type;
    u_short ifm_index;
    int ifm_flags;
    for (next = buf; next < lim; ) {
        struct if_msghdr *ifm = (struct if_msghdr *) next;
	    next += ifm->ifm_msglen;
        switch (ifm->ifm_type) {
        case RTM_IFINFO: {
            ifm_index = ifm->ifm_index;
            ifm_flags = ifm->ifm_flags;
            ifi_type = ifm->ifm_data.ifi_type;
            ibytes = ifm->ifm_data.ifi_ibytes;
            obytes = ifm->ifm_data.ifi_obytes;
            break ;
       }
       case RTM_IFINFO2: {
            struct if_msghdr2 *ifm2 = (struct if_msghdr2 *) ifm;
            ifm_index = ifm2->ifm_index;
            ifm_flags = ifm2->ifm_flags;
            ifi_type = ifm2->ifm_data.ifi_type;
            ibytes = ifm2->ifm_data.ifi_ibytes;
            obytes = ifm2->ifm_data.ifi_obytes;
            break ;
        }
       /*case RTM_NEWADDR:
       case RTM_NEWMADDR:
       case RTM_NEWMADDR2: {
            struct ifa_msghdr * ifa = (struct ifa_msghdr *) ifm;
            printf("RTM_NEWMADDR%d %x\n", ifm->ifm_type, ifa->ifam_addrs);
            break ;
        }*/
        default:
            //fprintf(stderr, "%s(): unreconized ifm data type: %d\n", __func__, ifm->ifm_type);
            continue ;
        }
        char if_name[IF_NAMESIZE+1] = {0, };
        if_name[IF_NAMESIZE] = 0;
        if_indextoname(ifm_index, if_name);
        LOG_INFO(family->log, "RTM_IFINFO%u #%d %s TYPE:%u UP:%d LO:%d I:%llu O:%llu FLAGS:%d"
                              " OACT:%d BCST:%d DBG:%d PPP:%d NOTR:%d RUNN:%d NOARP:%d PRO:%d"
                              " ALLM:%d SIMP:%d APH:%d MCST:%d\n",
                 ifm->ifm_type, ifm_index, if_name, ifi_type,
                 (ifm_flags & IFF_UP) != 0, (ifm_flags & IFF_LOOPBACK) != 0,
                 ibytes, obytes, ifm_flags,
                 (ifm_flags & IFF_OACTIVE) != 0, (ifm_flags & IFF_BROADCAST) != 0,
                 (ifm_flags & IFF_DEBUG) != 0, (ifm_flags & IFF_POINTOPOINT) != 0,
                 (ifm_flags & IFF_NOTRAILERS) != 0, (ifm_flags & IFF_RUNNING) != 0,
                 (ifm_flags & IFF_NOARP) != 0, (ifm_flags & IFF_PROMISC) != 0,
                 (ifm_flags & IFF_ALLMULTI) != 0, (ifm_flags & IFF_SIMPLEX) != 0,
                 (ifm_flags & IFF_ALTPHYS) != 0, (ifm_flags & IFF_MULTICAST) != 0);

        total_ibytes += ibytes;
		total_obytes += obytes;

        if ((ifm_flags & IFF_LOOPBACK) != 0) {
            lo_ibytes += ibytes;
            lo_obytes += obytes;
        } else {
            switch (ifm->ifm_data.ifi_type) {
            case IFT_PFLOG:
                break ;
            default:
                phy_ibytes += ibytes;
                phy_obytes += obytes;
                break ;
            }
	    }
    }

    if (elapsed == NULL || (elapsed->tv_sec == 0 && elapsed->tv_usec == 0)) {
        data->ibytespersec = 0;
        data->obytespersec = 0;
    } else {
        data->ibytespersec = (((total_ibytes - data->ibytes) * 1000)
                                / (elapsed->tv_sec * 1000 + elapsed->tv_usec / 1000));
        data->obytespersec = (((total_obytes - data->obytes) * 1000)
                                / (elapsed->tv_sec * 1000 + elapsed->tv_usec / 1000));
    }
    data->ibytes = total_ibytes;
    data->obytes = total_obytes;
    return SENSOR_SUCCESS;
}

#else /* ifdef APPLE */

static sensor_status_t network_get(sensor_family_t * family,
                                   network_data_t *data, struct timeval *elapsed) {
    (void)data;
    (void)elapsed;
    LOG_ERROR(family->log, "%s/%s(): NOT IMPLEMENTED ON THIS SYSTEM.\n", __FILE__, __func__);
    return SENSOR_ERROR;
}

#endif /* ifdef APPLE */

int network_print() {
    sensor_family_t family = { .log = NULL };
    network_data_t data;
    struct timeval elapsed = { .tv_sec = 1, .tv_usec = 0 };
    network_get(&family, &data, &elapsed);
    return fprintf(stdout, "network_bytes_in %lu\nnetwork_bytes_out %lu\n",
                           data.ibytes, data.obytes);
}

