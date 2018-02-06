/*
 * Copyright (C) 2017-2018 Vincent Sallaberry
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

#include "network_private.h"

sensor_status_t network_get(sensor_family_t * family,
                            network_data_t *data, struct timeval *elapsed) {
    int     mib[] = { CTL_NET, PF_ROUTE, 0, 0, NET_RT_IFLIST2, 0 };
    size_t  len;
    char *  buf;

    if (sysctl(mib, sizeof(mib) / sizeof(*mib), NULL, &len, NULL, 0) < 0) {
	    LOG_ERROR(family->log, "%s(): sysctl(null): %s", __func__, strerror(errno));
	    return SENSOR_ERROR;
    }

    buf = (char*) malloc(len);
    if (buf == NULL || sysctl(mib, sizeof(mib) / sizeof(*mib), buf, &len, NULL, 0) < 0) {
        LOG_ERROR(family->log, "%s(): sysctl(buf): %s", __func__, strerror(errno));
	    if (buf)
            free(buf);
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

    free(buf);
    return SENSOR_SUCCESS;
}

