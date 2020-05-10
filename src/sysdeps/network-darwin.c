/*
 * Copyright (C) 2017-2020 Vincent Sallaberry
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
# warning "building network-darwin.c without __APPLE__ defined"
#endif

#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <net/if.h>
#include <net/route.h>
#include <net/if_types.h>

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>

#include "network_private.h"

#ifndef IFF_NOTRAILERS
# define IFF_NOTRAILERS 0
#endif
#ifndef IFF_ALTPHYS
# define IFF_ALTPHYS 0
#endif

typedef struct {
    char *      buf;
    size_t      buf_size;
    size_t      sysctl_len;
} network_sysdep_t;

#if ( defined(CTL_NET) && defined(PF_ROUTE)                         \
      && (   (defined(NET_RT_IFLIST2) && defined(RTM_IFINFO2))      \
          || (defined(NET_RT_IFLIST)  && defined(RTM_IFINFO)) ) )
# define SENSOR_NETWORK_SUPPORTED 1
#else
# define SENSOR_NETWORK_SUPPORTED 0
#endif

sensor_status_t sysdep_network_support(sensor_family_t * family, const char * label) {
    (void) label;
    (void) family;
#if SENSOR_NETWORK_SUPPORTED
    return SENSOR_SUCCESS;
#else
    return SENSOR_ERROR;
#endif
}

sensor_status_t sysdep_network_init(sensor_family_t * family) {
    if (family->priv == NULL) {
        return SENSOR_ERROR;
    }

    network_priv_t * priv = (network_priv_t *) family->priv;

    if (priv->sysdep == NULL) {
        if ((priv->sysdep = calloc(1, sizeof(network_sysdep_t))) == NULL) {
            return SENSOR_ERROR;
        }
        network_sysdep_t * sysdep = (network_sysdep_t *) priv->sysdep;

        sysdep->buf = NULL;
        sysdep->buf_size = 0;
        sysdep->sysctl_len = 0;
    }
    return SENSOR_SUCCESS;
}

sensor_status_t sysdep_network_destroy(sensor_family_t * family) {
    if (family->priv != NULL) {
        network_priv_t * priv = (network_priv_t *) family->priv;

        if (priv->sysdep != NULL) {
            network_sysdep_t * sysdep = (network_sysdep_t *) priv->sysdep;

            if (sysdep->buf != NULL) {
                free(sysdep->buf);
            }
            priv->sysdep = NULL;
            free(sysdep);
        }
    }
    return SENSOR_SUCCESS;
}

sensor_status_t sysdep_network_get(
                    sensor_family_t *   family,
                    network_data_t *    data,
                    struct timeval *    elapsed) {
#if ! SENSOR_NETWORK_SUPPORTED
    (void) family;
    (void) data;
    (void) elapsed;
    return SENSOR_ERROR;
#else
# if defined(NET_RT_IFLIST2) && defined(RTM_IFINFO2)
    int                 mib[] = { CTL_NET, PF_ROUTE, 0, 0, NET_RT_IFLIST2, 0 };
# else
    int                 mib[] = { CTL_NET, PF_ROUTE, 0, 0, NET_RT_IFLIST, 0 };
# endif
    network_priv_t *    priv = (network_priv_t *) family->priv;
    network_sysdep_t *  sysdep = (network_sysdep_t *) priv->sysdep;
    size_t  len;

    if (sysdep == NULL) {
        return SENSOR_ERROR;
    }

    if (sysctl(mib, sizeof(mib) / sizeof(*mib), NULL, &len, NULL, 0) < 0) {
	    LOG_ERROR(family->log, "%s(): sysctl(null): %s", __func__, strerror(errno));
	    return SENSOR_ERROR;
    }

    if (len > sysdep->buf_size) {
        if ((sysdep->buf = realloc(sysdep->buf, len)) == NULL) {
            return SENSOR_ERROR;
        }
        sysdep->buf_size = len;
    }
    if (sysctl(mib, sizeof(mib) / sizeof(*mib), sysdep->buf, &len, NULL, 0) < 0) {
        LOG_ERROR(family->log, "%s(): sysctl(buf): %s", __func__, strerror(errno));
        return SENSOR_ERROR;
    }

    char * buf = sysdep->buf;
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
# if defined(NET_RT_IFLIST2) && defined(RTM_IFINFO2)
       case RTM_IFINFO2: {
            struct if_msghdr2 *ifm2 = (struct if_msghdr2 *) ifm;
            ifm_index = ifm2->ifm_index;
            ifm_flags = ifm2->ifm_flags;
            ifi_type = ifm2->ifm_data.ifi_type;
            ibytes = ifm2->ifm_data.ifi_ibytes;
            obytes = ifm2->ifm_data.ifi_obytes;
            break ;
        }
# endif
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
        #ifndef _DEBUG
        (void) ifi_type;
        #endif
        LOG_DEBUG(
            family->log,
            "RTM_IFINFO%u #%d %s TYPE:%u UP:%d LO:%d I:%" PRIu64 " O:%" PRIu64 " FLAGS:%d"
            " OACT:%d BCST:%d DBG:%d PPP:%d NOTR:%d RUNN:%d NOARP:%d PRO:%d"
            " ALLM:%d SIMP:%d APH:%d MCST:%d",
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
#endif
}

