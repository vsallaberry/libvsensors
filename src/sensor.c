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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <inttypes.h>

#include "vlib/options.h"
#include "vlib/util.h"
#include "vlib/slist.h"
#include "vlib/logpool.h"
#include "libvsensors/sensor.h"

#include "version.h"

#include "smc.h"
#include "memory.h"
#include "disk.h"
#include "network.h"
#include "cpu.h"
#include "battery.h"

static const sensor_family_info_t * s_families_info[] = {
	&g_sensor_family_smc,
	&g_sensor_family_battery,
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
    log_t *         log;
};

sensor_ctx_t * sensor_init(logpool_t * logs) {
    size_t          nb_fam  = sizeof(s_families_info) / sizeof(*s_families_info);
    sensor_ctx_t *  sctx;
    sensor_status_t ret;

    if ((sctx = calloc(1, sizeof(sensor_ctx_t))) == NULL) {
        return NULL;
    }
    sctx->log = logpool_getlog(logs, "sensors", LPG_TRUEPREFIX);
    for (unsigned i_fam = 0; i_fam < nb_fam && s_families_info[i_fam]; i_fam++) {
        const sensor_family_info_t *    fam_info    = s_families_info[i_fam];
        sensor_family_t *               fam;

        if (!fam_info || !fam_info->name) {
            continue ;
        }
        if ((fam = calloc(1, sizeof(sensor_family_t))) == NULL) {
            LOG_ERROR(sctx->log, "sensor family %s cannot be allocated", fam_info->name);
            continue ;
        }
        fam->info = fam_info;
        fam->log = logpool_getlog(logs, fam->info->name, LPG_TRUEPREFIX);
        ret = SENSOR_ERROR;
        if (fam->info->init && (ret = fam->info->init(fam)) != SENSOR_SUCCESS) {
            if (ret == SENSOR_NOT_SUPPORTED)
                LOG_INFO(sctx->log, "%s sensors not supported on this system", fam->info->name);
            else
                LOG_ERROR(sctx->log, "sensor family %s cannot be initialized", fam->info->name);
            free(fam);
            continue ;
        }
        LOG_INFO(sctx->log, "%s: loaded.", fam->info->name);
        if ((sctx->families = slist_prepend(sctx->families, fam)) == NULL) {
            LOG_ERROR(sctx->log, "sensor family %s cannot be registered", fam->info->name);
            if (fam->info->free && fam->info->free(fam) != SENSOR_SUCCESS) {
                LOG_ERROR(sctx->log, "sensor family %s cannot be freed", fam->info->name);
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
        LOG_ERROR(sctx->log, "error: watch is NULL in %s", __func__);
        return NULL;
    }
    SLIST_FOREACH_DATA(sctx->sensors, it_sensor, sensor_desc_t *) {
        if (sensor == NULL
        || sensor == it_sensor) { // FIXME BOF
            sensor_sample_t * sample = calloc(1, sizeof(sensor_sample_t));
            if (sample == NULL) {
                LOG_ERROR(sctx->log, "error: cannot allocate sensor sample in %s", __func__);
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
        return INT_MAX;
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
    return SENSOR_NOT_SUPPORTED;
}

slist_t * sensor_watch_load(const char * path) {
    (void)path;
    return NULL;
}

static inline sensor_status_t sensor_update_check_internal(
                                sensor_sample_t *           sensor,
                                const struct timeval *      now) {
    if (sensor->desc && sensor->desc->family && sensor->desc->family->info
    && sensor->desc->family->info->update) {
        struct timeval nexttick = {
            .tv_sec     = sensor->watch.update_interval_ms / 1000,
            .tv_usec    = (sensor->watch.update_interval_ms % 1000) * 1000,
        };
        timeradd(&nexttick, &sensor->last_update_time, &nexttick);
        if (timercmp(now, &nexttick, >=)) {
            sensor_value_t prev_value = sensor->value; // FIXME perfs ? + FIXME initial value
            if (sensor->desc->family->info->update(sensor, now) != SENSOR_SUCCESS) {
                return SENSOR_ERROR;
            }
            sensor->last_update_time = *now;
            if (sensor_value_equal(&prev_value, &sensor->value) == 0) {
                return SENSOR_UPDATED;
            }
        }
        return SENSOR_UNCHANGED;
    }
    return SENSOR_NOT_SUPPORTED;
}

sensor_status_t sensor_update_check(sensor_sample_t * sensor, const struct timeval * now) {
    struct timeval snow;

    if (sensor == NULL) {
        return SENSOR_NOT_SUPPORTED;
    }
    if (now == NULL) {
        if (gettimeofday(&snow, NULL) != 0) {
            return SENSOR_ERROR;
        }
        now = &snow;
    }
    return sensor_update_check_internal(sensor, now);
}

slist_t * sensor_update_get(sensor_ctx_t *sctx, const struct timeval * now) {
    slist_t *updates = NULL;
    struct timeval snow;

    if (sctx == NULL) {
        return NULL;
    }
    if (sctx->watchs == NULL) {
        LOG_VERBOSE(sctx->log, "warning in %s(): watch list is empty", __func__);
        return NULL;
    }
    if (now == NULL) {
        if (gettimeofday(&snow, NULL) != 0) {
            LOG_ERROR(sctx->log, "error gettimeofday: %s, in %s", strerror(errno), __func__);
            return NULL;
        }
        now = &snow;
    }
    SLIST_FOREACH_DATA(sctx->watchs, sensor, sensor_sample_t *) {
        if (sensor) {
            sensor_status_t ret = sensor_update_check_internal(sensor, now);
            if (ret == SENSOR_UPDATED) {
                updates = slist_prepend(updates, sensor);
            } else if (ret == SENSOR_ERROR) {
                LOG_ERROR(sctx->log, "sensor '%s' update error", sensor->desc->label);
            }
        }
    }
    return updates;
}

void sensor_update_free(slist_t * updates) {
    slist_free(updates, NULL);
}


/***************************************************************************
 * SENSOR_VALUE
 ***************************************************************************/
typedef struct {
    size_t size;
    size_t off;
} sensor_value_info_t;
//#ifndef __offsetof
# define voffsetof(type, field) ((size_t)(&((type *)0)->field))
//#endif
#define SENSOR_VALUE_INFO_INIT(val, field) \
    (sensor_value_info_t) { sizeof(val.field), voffsetof(sensor_value_t, field) }

static const sensor_value_info_t * sensor_value_info(unsigned int type) {
    static sensor_value_info_t info[SENSOR_VALUE_NB + 1] = { { SIZE_MAX, SIZE_MAX }, };
    if (info[0].size == SIZE_MAX) {
        sensor_value_t v;
        info[0]                     = (sensor_value_info_t) { 0, 0 };
        info[SENSOR_VALUE_NULL]     = (sensor_value_info_t) { 0, 0 };
        info[SENSOR_VALUE_UCHAR]    = SENSOR_VALUE_INFO_INIT(v, data.uc);
        info[SENSOR_VALUE_CHAR]     = SENSOR_VALUE_INFO_INIT(v, data.c);
        info[SENSOR_VALUE_UINT]     = SENSOR_VALUE_INFO_INIT(v, data.ui);
        info[SENSOR_VALUE_INT]      = SENSOR_VALUE_INFO_INIT(v, data.i);
        info[SENSOR_VALUE_ULONG]    = SENSOR_VALUE_INFO_INIT(v, data.ul);
        info[SENSOR_VALUE_LONG]     = SENSOR_VALUE_INFO_INIT(v, data.l);
        info[SENSOR_VALUE_FLOAT]    = SENSOR_VALUE_INFO_INIT(v, data.f);
        info[SENSOR_VALUE_DOUBLE]   = SENSOR_VALUE_INFO_INIT(v, data.d);
        info[SENSOR_VALUE_LDOUBLE]  = SENSOR_VALUE_INFO_INIT(v, data.ld);
        info[SENSOR_VALUE_UINT64]   = SENSOR_VALUE_INFO_INIT(v, data.ull);
        info[SENSOR_VALUE_INT64]    = SENSOR_VALUE_INFO_INIT(v, data.ll);
        info[SENSOR_VALUE_STRING]   = SENSOR_VALUE_INFO_INIT(v, data.b.buf);
        info[SENSOR_VALUE_BYTES]    = SENSOR_VALUE_INFO_INIT(v, data.b.buf);
        info[SENSOR_VALUE_NB]       = SENSOR_VALUE_INFO_INIT(v, data.c);
    }
    if (type >= SENSOR_VALUE_NB)
        return &info[SENSOR_VALUE_NB];
    return &info[type];
}

sensor_status_t sensor_value_fromraw(const void *src, sensor_value_t * value) {
    if (value == NULL || src == NULL) {
        return SENSOR_ERROR;
    }
    switch (value->type) {
        case SENSOR_VALUE_NB:
            return SENSOR_ERROR;
        case SENSOR_VALUE_NULL:
            return SENSOR_SUCCESS;
        case SENSOR_VALUE_BYTES: {
            size_t cpysz = value->data.b.size > value->data.b.maxsize
                           ? value->data.b.maxsize : value->data.b.size;
            if (memcpy(value->data.b.buf, (const char *) src, cpysz) != value->data.b.buf)
                return SENSOR_ERROR;
            break ;
        }
        case SENSOR_VALUE_STRING:
            value->data.b.size = str0cpy(value->data.b.buf, (const char *) src,
                                         value->data.b.maxsize);
            break ;
        default: {
            const sensor_value_info_t * info = sensor_value_info(value->type);
            if (memcpy((unsigned char *)value + info->off, src, info->size)
                    != (unsigned char *)value + info->off)
                return SENSOR_ERROR;
        }
    }
    return SENSOR_SUCCESS;
}

int sensor_value_tostring(const sensor_value_t * value, char *dst, size_t maxlen) {
    int ret;

    if (dst == NULL || maxlen <= 0) {
        return -1;
    }
    if (value == NULL) {
        *dst = 0;
        return 0;
    }
    if (maxlen == 1) {
        *dst = 0;
        if (value->type >= SENSOR_VALUE_NB)
            return -1;
        return 0;
    }
    switch (value->type) {
        case SENSOR_VALUE_UCHAR:
            return VLIB_SNPRINTF(ret, dst, maxlen, "%u",
                                 SENSOR_VALUEP_GET(value, SENSOR_VALUE_UCHAR));
        case SENSOR_VALUE_CHAR:
            return VLIB_SNPRINTF(ret, dst, maxlen, "%d",
                                 SENSOR_VALUEP_GET(value, SENSOR_VALUE_CHAR));
        case SENSOR_VALUE_UINT:
            return VLIB_SNPRINTF(ret, dst, maxlen, "%u",
                                 SENSOR_VALUEP_GET(value, SENSOR_VALUE_UINT));
        case SENSOR_VALUE_INT:
            return VLIB_SNPRINTF(ret, dst, maxlen, "%d",
                                 SENSOR_VALUEP_GET(value, SENSOR_VALUE_INT));
        case SENSOR_VALUE_ULONG:
            return VLIB_SNPRINTF(ret, dst, maxlen, "%lu",
                                 SENSOR_VALUEP_GET(value, SENSOR_VALUE_ULONG));
        case SENSOR_VALUE_LONG:
            return VLIB_SNPRINTF(ret, dst, maxlen, "%ld",
                                 SENSOR_VALUEP_GET(value, SENSOR_VALUE_LONG));
        case SENSOR_VALUE_UINT64:
            return VLIB_SNPRINTF(ret, dst, maxlen, "%" PRIu64,
                                 SENSOR_VALUEP_GET(value, SENSOR_VALUE_UINT64));
        case SENSOR_VALUE_INT64:
            return VLIB_SNPRINTF(ret, dst, maxlen, "%" PRId64,
                                 SENSOR_VALUEP_GET(value, SENSOR_VALUE_INT64));
        case SENSOR_VALUE_FLOAT:
           /* Need a particular case for Float as new C libraries (from c99 i think)
             * do not support float, and always promote them to double before conversion */
            return VLIB_SNPRINTF(ret, dst, maxlen, "%f",
                                 SENSOR_VALUEP_GET(value, SENSOR_VALUE_FLOAT));
        case SENSOR_VALUE_DOUBLE:
            return VLIB_SNPRINTF(ret, dst, maxlen, "%lf",
                                 SENSOR_VALUEP_GET(value, SENSOR_VALUE_DOUBLE));
        case SENSOR_VALUE_LDOUBLE:
            return VLIB_SNPRINTF(ret, dst, maxlen, "%Lf",
                                 SENSOR_VALUEP_GET(value, SENSOR_VALUE_LDOUBLE));
        case SENSOR_VALUE_BYTES: {
            char * dstcur = dst;
            if (value->data.b.size > 0) {
                size_t i;
                dstcur[maxlen-1] = 0;
                for (i = 0; maxlen > 0 && i < value->data.b.size; i++) {
                    snprintf(dstcur, maxlen, "%02x ", value->data.b.buf[i]&0xFF);
                    if (maxlen > 3) {
                        dstcur += 3;
                        maxlen -= 3;
                    } else {
                        dstcur += maxlen;
                        maxlen = 0;
                    }
                }
                *(--dstcur) = 0;
            } else
                *dstcur = 0;
            return (dstcur - dst);
        }
        case SENSOR_VALUE_STRING:
            return str0cpy(dst, value->data.b.buf, maxlen);
        case SENSOR_VALUE_NULL:
            *dst = 0;
            return 0;
        case SENSOR_VALUE_NB:
            return -1;
    }
    LOG_ERROR(NULL, "%s(type:%u): UNREACHABLE CODE", __func__, value->type);
    return -1; /* should not be reached */
}

long double sensor_value_todouble(const sensor_value_t * value) {
    long double result = 0.0L;

    if (value == NULL) {
        errno = EFAULT;
        return 0.0L;
    }
    switch (value->type) {
        case SENSOR_VALUE_INT:
            result = (value->data.i);   break ;
        case SENSOR_VALUE_LONG:
            result = (value->data.l);   break ;
        case SENSOR_VALUE_INT64:
            result = (value->data.ll);  break ;
        case SENSOR_VALUE_CHAR:
            result = (value->data.c);   break ;
        case SENSOR_VALUE_UINT:
            result = (value->data.ui);  break ;
        case SENSOR_VALUE_ULONG:
            result = (value->data.ul);  break ;
        case SENSOR_VALUE_UINT64:
            result = (value->data.ull); break ;
        case SENSOR_VALUE_UCHAR:
            result = (value->data.uc);  break ;
        case SENSOR_VALUE_FLOAT:
            result = (value->data.f);   break ;
        case SENSOR_VALUE_DOUBLE:
            result = (value->data.d);   break ;
        case SENSOR_VALUE_LDOUBLE:
            result = (value->data.ld);  break ;
        case SENSOR_VALUE_STRING: {
            char *  endptr;
            if (value->data.b.size <= 0) {
                errno = EINVAL;
                return 0.0L;
            }
            if (value->data.b.size < value->data.b.maxsize)
                value->data.b.buf[value->data.b.size] = 0;
            else
                value->data.b.buf[value->data.b.size - 1] = 0;
            errno = 0;
            result = strtold(value->data.b.buf, &endptr);
            if (!endptr || *endptr || errno != 0) {
                errno = EINVAL;
                return 0.0L;
            }
            return result;
        }
        case SENSOR_VALUE_BYTES:
            errno = EINVAL;
            return 0.0L;
        case SENSOR_VALUE_NULL:
        case SENSOR_VALUE_NB:
            errno = EINVAL;
            return 0.0L;
    }
    if (result == 0.0L)
        errno = 0;
    return result;
}

intmax_t sensor_value_toint(const sensor_value_t * value) {
    intmax_t    result = 0;

    if (value == NULL) {
        errno = EFAULT;
        return INTMAX_C(0);
    }

    switch (value->type) {
        case SENSOR_VALUE_INT:
            result = (value->data.i); break ;
        case SENSOR_VALUE_LONG:
            result = (value->data.l); break ;
        case SENSOR_VALUE_INT64:
            result = (value->data.ll); break ;
        case SENSOR_VALUE_CHAR:
            result = (value->data.c); break ;
        case SENSOR_VALUE_UINT:
            result = (value->data.ui); break ;
        case SENSOR_VALUE_ULONG: {
            uintmax_t uresult = (value->data.ul);
            if (uresult > INTMAX_MAX) {
                errno = EOVERFLOW;
                return (uresult - INTMAX_MAX);
            }
            errno = 0;
            return uresult;
        }
        case SENSOR_VALUE_UINT64: {
            uintmax_t uresult = (value->data.ull);
            if (uresult > INTMAX_MAX) {
                errno = EOVERFLOW;
                return (uresult - INTMAX_MAX);
            }
            errno = 0;
            return uresult;
        }
        case SENSOR_VALUE_UCHAR:
            result = (value->data.uc); break ;
        case SENSOR_VALUE_FLOAT:
            result = (value->data.f); break ;
        case SENSOR_VALUE_DOUBLE:
            result = (value->data.d); break ;
        case SENSOR_VALUE_LDOUBLE:
            if (value->data.ld > INTMAX_MAX) {
                errno = EOVERFLOW;
                return ((uintmax_t)value->data.ld - INTMAX_MAX);
            }
            if (value->data.ld < INTMAX_MIN) {
                errno = ERANGE;
                return (value->data.ld + INTMAX_MIN);
            }
            errno = 0;
            return (value->data.ld);
        case SENSOR_VALUE_BYTES:
            errno = EINVAL;
            return INTMAX_C(0);
        case SENSOR_VALUE_STRING: {
            char *      endptr;
            if (value->data.b.size <= 0) {
                errno = EINVAL;
                return INTMAX_C(0);
            }
            if (value->data.b.size < value->data.b.maxsize)
                value->data.b.buf[value->data.b.size] = 0;
            else
                value->data.b.buf[value->data.b.size - 1] = 0;
            errno = 0;
            result = strtoimax(value->data.b.buf, &endptr, 0);
            if (!endptr || *endptr || errno != 0) {
                errno = EINVAL;
                return INTMAX_C(0);
            }
            return result;
        }
        case SENSOR_VALUE_NULL:
        case SENSOR_VALUE_NB:
            errno = EINVAL;
            return INTMAX_C(0);
    }

    if (result == INTMAX_C(0))
        errno = 0;

    return result;
}

int sensor_value_equal(const sensor_value_t * v1, const sensor_value_t * v2) {
    if (v1 == v2)
        return 1;
    if (v1 == NULL || v2 == NULL)
        return 0;
    if (v1->type != v2->type)
        return 0;
    switch (v1->type) {
        case SENSOR_VALUE_BYTES:
        case SENSOR_VALUE_STRING:
            if (v1->data.b.size != v2->data.b.size)
                return 0;
            if (v1->data.b.buf == v2->data.b.buf)
                return 1;
            if (v1->data.b.buf == NULL || v2->data.b.buf == NULL)
                return 0;
            return 0 == memcmp(v1->data.b.buf, v2->data.b.buf, v1->data.b.size);
        case SENSOR_VALUE_NB:
        case SENSOR_VALUE_NULL:
            return 1;
        /* need specific for floating as same nb can have different representations */
        case SENSOR_VALUE_DOUBLE:
            return v1->data.d == v2->data.d;
        case SENSOR_VALUE_LDOUBLE:
            return v1->data.ld == v2->data.ld;
        case SENSOR_VALUE_FLOAT:
            return v1->data.f == v2->data.f;
        /* specific case for ints to speed up function */
        case SENSOR_VALUE_CHAR:
        case SENSOR_VALUE_UCHAR:
            return v1->data.uc == v2->data.uc;
        case SENSOR_VALUE_INT:
        case SENSOR_VALUE_UINT:
            return v1->data.ui == v2->data.ui;
        case SENSOR_VALUE_LONG:
        case SENSOR_VALUE_ULONG:
            return v1->data.ul == v2->data.ul;
        case SENSOR_VALUE_INT64:
        case SENSOR_VALUE_UINT64:
            return v1->data.ull == v2->data.ull;
        /* generic memory compare for remaining types */
        default: {
            const sensor_value_info_t * info = sensor_value_info(v1->type);
            return 0 == memcmp((unsigned char *)v1 + info->off,
                               (unsigned char *)v2 + info->off, info->size);
        }
    }
}

int sensor_value_compare(const sensor_value_t * v1, const sensor_value_t * v2) {
    if (v1 == v2)
        return 0;
    if (v1 == NULL || v2 == NULL)
        return (int) (v1 - v2);
    if (v1->type == SENSOR_VALUE_NULL || v2->type == SENSOR_VALUE_NULL) {
        if (v1->type == v2->type)
            return 0;
        return v1->type == SENSOR_VALUE_NULL ? -1 : +1;
    }
    if (SENSOR_VALUE_IS_BUFFER(v1->type) || SENSOR_VALUE_IS_BUFFER(v2->type)) {
        char    sconv[64];
        char *  s1  = v1->data.b.buf;
        size_t  sz1 = v1->data.b.size;
        char *  s2  = v2->data.b.buf;
        size_t  sz2 = v2->data.b.size;
        if (v1->type != v2->type) {
            if (!SENSOR_VALUE_IS_BUFFER(v1->type)) {
            //if (v1->type != SENSOR_VALUE_STRING) {
                sz1 = sensor_value_tostring(v1, sconv, sizeof(sconv) / sizeof(*sconv));
                s1 = sconv;
            }
            else if (!SENSOR_VALUE_IS_BUFFER(v2->type)) {
                sz2 = sensor_value_tostring(v2, sconv, sizeof(sconv) / sizeof(*sconv));
                s2 = sconv;
            }
        }
        if (sz1 != sz2) {
            return (sz1 - sz2);
        }
        return memcmp(s1, s2, sz1);
    }
    return (int)(ceill(sensor_value_todouble(v1) - sensor_value_todouble(v2)));
}

int sensor_value_compare_fallback(const sensor_value_t * v1, const sensor_value_t * v2) {
    if (SENSOR_VALUE_IS_FLOATING(v1->type) && SENSOR_VALUE_IS_FLOATING(v2->type)) {
        double d1 = sensor_value_todouble(v1);
        double d2 = sensor_value_todouble(v2);
        if (d1 == d2) return 0;
        else if (d1 < d2) return -1;
        else return 1;
    }

    char s1[43]; s1[sizeof(s1)/sizeof(*s1)-1] = 0;
    char s2[43]; s2[sizeof(s2)/sizeof(*s2)-1] = 0;
    char sign1 = 1, sign2 = 1;
    char *s;
    size_t l1, l2, off1, off2;
    sensor_value_tostring(v1, s1, sizeof(s1)/sizeof(*s1));
    sensor_value_tostring(v2, s2, sizeof(s2)/sizeof(*s2));
    if (*s1 == '-') { sign1 = -1; *s1 = '0'; }
    if (*s2 == '-') { sign2 = -1; *s2 = '0'; }
    if (sign1 != sign2)
        return sign1 - sign2;
    l1 = strlen(s1);
    l2 = strlen(s2);
    if (SENSOR_VALUE_IS_FLOATING(v1->type))
        s = strrchr(s1, '.');
    else s = NULL;
    if (s != NULL) { s[2] = 0; if (s[1] == 0) s[1] = '0'; }
    else { strncpy(s1 + l1, ".0", 3); l1 += 2; }
    if (SENSOR_VALUE_IS_FLOATING(v2->type))
        s = strrchr(s2, '.');
    else s = NULL;
    if (s != NULL) { s[2] = 0; if (s[1] == 0) s[1] = '0'; }
    else { strncpy(s2 + l2, ".0", 3); l2 += 2; }
    off1 = sizeof(s1)/sizeof(*s1)-1 - l1;
    off2 = sizeof(s2)/sizeof(*s2)-1 - l2;
    memmove(s1 + off1, s1, l1);
    memmove(s2 + off2, s2, l2);
    memset(s1, '0', off1);
    memset(s2, '0', off2);

    return memcmp(s1, s2, sizeof(s1) / sizeof(*s1)) * sign1;
}

const char * libvsensors_get_version() {
    return OPT_VERSION_STRING(BUILD_APPNAME, APP_VERSION, "git:" BUILD_GITREV);
}

#ifndef APP_INCLUDE_SOURCE
# define APP_NO_SOURCE_STRING "\n/* #@@# FILE #@@# " BUILD_APPNAME "/* */\n" \
                              BUILD_APPNAME " source not included in this build.\n"
int libvsensors_get_source(FILE * out, char * buffer, unsigned int buffer_size, void ** ctx) {
    return vdecode_buffer(out, buffer, buffer_size, ctx,
                          APP_NO_SOURCE_STRING, sizeof(APP_NO_SOURCE_STRING) - 1);
}
#endif

