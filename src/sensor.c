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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "vlib/options.h"
#include "libvsensors/sensor.h"

#include "version.h"
#include "smc.h"
#include "memory.h"
#include "disk.h"
#include "network.h"
#include "cpu.h"

static const sensor_family_info_t * s_families_info[] = {
	&g_sensor_family_smc,
	&g_sensor_family_memory,
	&g_sensor_family_network,
	&g_sensor_family_disk,
	&g_sensor_family_cpu,
    NULL
};

struct sensor_ctx_s {
    slist_t *       families;
    slist_t *       watchs;
    slist_t *       sensors;
    int             flags;
    log_ctx_t *     log;
};

sensor_ctx_t * sensor_init(slist_t * logs) {
    size_t          nb_fam  = sizeof(s_families_info) / sizeof(*s_families_info);
    sensor_ctx_t *  sctx;

    (void)logs;
    if ((sctx = calloc(1, sizeof(sensor_ctx_t))) == NULL) {
        return NULL;
    }
    for (unsigned i_fam = 0; i_fam < nb_fam && s_families_info[i_fam]; i_fam++) {
        const sensor_family_info_t *    fam_info    = s_families_info[i_fam];
        sensor_family_t *               fam;

        if (!fam_info || !fam_info->name) {
            continue ;
        }
        if ((fam = calloc(1, sizeof(sensor_family_t))) == NULL) {
            LOG_ERROR(sctx->log, "sensor family %s cannot be allocated\n", fam_info->name);
            continue ;
        }
        fam->info = fam_info;
        if (fam->info->init && fam->info->init(fam) != SENSOR_SUCCESS) {
            LOG_ERROR(sctx->log, "sensor family %s cannot be initialized\n", fam->info->name);
            free(fam);
            continue ;
        }
        if ((sctx->families = slist_prepend(sctx->families, fam)) == NULL) {
            LOG_ERROR(sctx->log, "sensor family %s cannot be registered\n", fam->info->name);
            if (fam->info->free && fam->info->free(fam) != SENSOR_SUCCESS) {
                LOG_ERROR(sctx->log, "sensor family %s cannot be freed\n", fam->info->name);
            }
            free(fam);
        }
    }
    return sctx;
}

sensor_status_t sensor_free(sensor_ctx_t * sctx) {
    if (sctx == NULL) {
        return SENSOR_ERROR;
    }
    sensor_list_free(sctx);
    sensor_watch_free(sctx);
    SLIST_FOREACH_DATA(sctx->families, fam, sensor_family_t *) {
        if (fam->info->free && fam->info->free(fam) != SENSOR_SUCCESS) {
            LOG_ERROR(sctx->log, "sensor family %s cannot be freed", fam->info->name);
        }
        free(fam);
    }
    slist_free(sctx->families, NULL);
    free(sctx);
    return SENSOR_SUCCESS;
}

slist_t * sensor_list_get(sensor_ctx_t *sctx) {
    if (sctx == NULL) {
        return NULL;
    }
    if (sctx->sensors != NULL) {
        return sctx->sensors;
    }
    SLIST_FOREACH_DATA(sctx->families, fam, sensor_family_t *) {
        if (fam && fam->info->list) {
            sctx->sensors = slist_concat(sctx->sensors, fam->info->list(fam));
        }
    }
    return sctx->sensors;
}

void sensor_list_free(sensor_ctx_t *sctx) {
    if (sctx == NULL || sctx->sensors == NULL) {
        return ;
    }
    slist_free(sctx->sensors, NULL);
    sctx->sensors = NULL;
}

slist_t * sensor_watch_add(sensor_desc_t *sensor, sensor_watch_t *watch, sensor_ctx_t *sctx) {
    if (sctx == NULL) {
       return NULL;
    }
    if (watch == NULL) {
        LOG_ERROR(sctx->log, "error: watch is NULL in %s\n", __func__);
        return NULL;
    }
    SLIST_FOREACH_DATA(sctx->sensors, it_sensor, sensor_desc_t *) {
        if (sensor == NULL
        || sensor == it_sensor) { // FIXME BOF
            sensor_sample_t * sample = calloc(1, sizeof(sensor_sample_t));
            if (sample == NULL) {
                LOG_ERROR(sctx->log, "error: cannot allocate sensor sample in %s\n", __func__);
                return sctx->watchs;
            }
            sample->desc = it_sensor;
            sample->watch = *watch;
            sample->value.type = sample->desc->type;
            sctx->watchs = slist_prepend(sctx->watchs, sample);
        }
    }
    return sctx->watchs;
}

void sensor_watch_free(sensor_ctx_t *sctx) {
    if (sctx == NULL) {
        return ;
    }
    slist_free(sctx->watchs, free);
    sctx->watchs = NULL;
}

static unsigned long pgcd(unsigned long a, unsigned long b) {
    unsigned long r;
    while (b > 0) {
        r = a % b;
        a = b;
        b = r;
    }
    return a;
}

time_t sensor_watch_pgcd(sensor_ctx_t *sctx) {
    if (sctx == NULL || sctx->watchs == NULL) {
        return 1;
    }
    time_t inter_pgcd = ((sensor_sample_t *)(sctx->watchs->data))->watch.update_interval_ms;
    SLIST_FOREACH_DATA(sctx->watchs->next, sensor, sensor_sample_t *) {
        inter_pgcd = pgcd(inter_pgcd, sensor->watch.update_interval_ms);
    }
    return inter_pgcd;
}

sensor_status_t sensor_watch_save(slist_t * watch_list, const char * path) {
    (void)watch_list;
    (void)path;
    return SENSOR_SUCCESS;
}

slist_t * sensor_watch_load(const char * path) {
    (void)path;
    return NULL;
}

slist_t * sensor_update_get(sensor_ctx_t *sctx) {
    slist_t *updates = NULL;
    struct timeval now;

    if (sctx == NULL) {
        return NULL;
    }
    if (sctx->watchs == NULL) {
        LOG_VERBOSE(sctx->log, "warning in %s(): watch list is empty\n", __func__);
        return NULL;
    }
    if (gettimeofday(&now, NULL) != 0) {
        LOG_ERROR(sctx->log, "error gettimeofday: %s, in %s\n", strerror(errno), __func__);
        return NULL;
    }
    SLIST_FOREACH_DATA(sctx->watchs, sensor, sensor_sample_t *) {
        if (sensor && sensor->desc && sensor->desc->family && sensor->desc->family->info
        && sensor->desc->family->info->update) {
            struct timeval nexttick = {
                .tv_sec     = sensor->watch.update_interval_ms / 1000,
                .tv_usec    = (sensor->watch.update_interval_ms % 1000) * 1000,
            };
            timeradd(&nexttick, &sensor->last_update_time, &nexttick);
            if (timercmp(&now, &nexttick, >=)) {
                sensor_value_t prev_value = sensor->value; // FIXME perfs ? + FIXME initial value
                if (sensor->desc->family->info->update(sensor, &now) == SENSOR_SUCCESS) {
                    sensor->last_update_time = now;
                    if (sensor_value_compare(&prev_value, &sensor->value) != 0) {
                        updates = slist_prepend(updates, sensor);
                    }
                } else {
                    LOG_ERROR(sctx->log, "sensor '%s' update error\n", sensor->desc->label);
                }
            }
        }
    }
    return updates;
}

void sensor_update_free(slist_t * updates) {
    slist_free(updates, NULL);
}

typedef struct {
    size_t size;
    const char *fmt;
} sensor_value_info_t;

static const sensor_value_info_t * sensor_value_info(unsigned int type) {
    static sensor_value_info_t info[SENSOR_VALUE_NB + 1] = { { 0, NULL }, };
    if (type >= SENSOR_VALUE_NB)
        return &info[SENSOR_VALUE_NB];
    if (info[0].fmt == NULL) {
        sensor_value_t v;
        info[SENSOR_VALUE_UINT]     = (sensor_value_info_t) { sizeof(v.data.ui),      "%u" };
        info[SENSOR_VALUE_INT]      = (sensor_value_info_t) { sizeof(v.data.i),       "%d" };
        info[SENSOR_VALUE_ULONG]    = (sensor_value_info_t) { sizeof(v.data.ul),      "%lu" };
        info[SENSOR_VALUE_LONG]     = (sensor_value_info_t) { sizeof(v.data.l),       "%ld" };
        info[SENSOR_VALUE_FLOAT]    = (sensor_value_info_t) { sizeof(v.data.f),       "%f" };
        info[SENSOR_VALUE_DOUBLE]   = (sensor_value_info_t) { sizeof(v.data.d),       "%lf" };
        info[SENSOR_VALUE_INT64]    = (sensor_value_info_t) { sizeof(v.data.ll),     "%lld" };
        info[SENSOR_VALUE_UINT64]   = (sensor_value_info_t) { sizeof(v.data.ull),    "%llu" };
        info[SENSOR_VALUE_BYTES]    = (sensor_value_info_t) { 0,                      "%s" };
        info[SENSOR_VALUE_NB]       = (sensor_value_info_t) { 0,                      "%s" };
    }
    return &info[type];
}

sensor_status_t sensor_value_fromraw(const void *src, sensor_value_t * value) {
    if (value == NULL || src == NULL) {
        return SENSOR_ERROR;
    }
    switch (value->type) {
        case SENSOR_VALUE_BYTES:
            snprintf(value->data.b.buf, value->data.b.size, "%s", (const char *) src); //FIXME size uninitialized?
            return SENSOR_SUCCESS;
        default:
            memcpy(&value->data, src, sensor_value_info(value->type)->size);
            return SENSOR_SUCCESS;
    }
}

sensor_status_t sensor_value_tostring(const sensor_value_t * value, char *dst, size_t maxlen) {
    if (value == NULL || dst == NULL || maxlen <= 0) {
        return SENSOR_ERROR;
    }
    switch (value->type) {
       case SENSOR_VALUE_BYTES:
            if (value->data.b.size < maxlen)
                maxlen = value->data.b.size + 1;
            snprintf(dst, maxlen, "%s", value->data.b.buf);
            return SENSOR_SUCCESS;
       case SENSOR_VALUE_FLOAT:
            /* Need a particular case for Float as new C libraries (from c99 i think)
             * do not support float, and always promote them to double before conversion */
            snprintf(dst, maxlen, "%f", value->data.f);
            return SENSOR_SUCCESS;
       default:
            snprintf(dst, maxlen, sensor_value_info(value->type)->fmt, value->data);
            return SENSOR_SUCCESS;
    }
}

double sensor_value_todouble(const sensor_value_t * value) {
    if (value == NULL) {
        return SENSOR_ERROR;
    }
    switch (value->type) {
        case SENSOR_VALUE_ULONG:
            return (value->data.ul);
        case SENSOR_VALUE_LONG:
            return (value->data.l);
        case SENSOR_VALUE_UINT:
            return (value->data.ui);
        case SENSOR_VALUE_INT:
            return (value->data.i);
        case SENSOR_VALUE_UINT64:
            return (value->data.ull);
        case SENSOR_VALUE_INT64:
            return (value->data.ll);
        case SENSOR_VALUE_FLOAT:
            return (value->data.f);
        case SENSOR_VALUE_DOUBLE:
            return (value->data.d);
        case SENSOR_VALUE_BYTES:
            if (value->data.b.size <= 0)
                return 0.0;
            value->data.b.buf[value->data.b.size-1] = 0; // FIXME, we loose info
            return strtod(value->data.b.buf, NULL);
        default:
            return 0.0;
    }
}

int sensor_value_compare(const sensor_value_t * v1, const sensor_value_t * v2) { //FIXME <, > not supported
    if (v1 == NULL || v2 == NULL)
        return (v1 == v2) ? 0 : 1;
    if (v1->type != v2->type)
        return 1;
    switch (v1->type) {
        case SENSOR_VALUE_BYTES:
            if (v1->data.b.size != v2->data.b.size)
                return 1;
            if (v1->data.b.buf == NULL || v2->data.b.buf == NULL)
                return (v1->data.b.buf == v2->data.b.buf ? 0 : 1);
            return memcmp(v1->data.b.buf, v2->data.b.buf, v1->data.b.size);
        default:
            return memcmp(&v1->data, &v2->data, sensor_value_info(v1->type)->size);
    }
}

const char * libvsensors_get_version() {
    return BUILD_APPNAME " v" APP_VERSION " built on " __DATE__ ", " __TIME__ \
           " from git-rev " BUILD_GITREV;
}

#ifndef APP_INCLUDE_SOURCE
const char *const* libvsensors_get_source() {
    static const char * const source[] = { "libvsensors source not included in this build.\n", NULL };
    return source;
}
#endif

