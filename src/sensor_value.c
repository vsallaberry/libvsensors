/*
 * Copyright (C) 2017-2020,2023 Vincent Sallaberry
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
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fnmatch.h>
#include <pthread.h>
#include <math.h>
#include <stdint.h>
#include <inttypes.h>

#include "vlib/options.h"
#include "vlib/util.h"
#include "vlib/time.h"
#include "vlib/slist.h"
#include "vlib/logpool.h"
#include "vlib/avltree.h"

#include "libvsensors/sensor.h"

#include "version.h"
#include "sensor_private.h"

/***************************************************************************
 * SENSOR_VALUE
 ***************************************************************************/
static const char * s_sensor_value_type_name[SENSOR_VALUE_NB+1] = {
    /* IN SAME ORDER AS sensor_value_type_t !! */
    "null",
    "uchar",
    "char",
    "uint16",
    "int16",
    "uint32",
    "int32",
    "uint",
    "int",
    "ulong",
    "long",
    "float",
    "double",
    "ldouble",
    "uint64",
    "int64",
    "string",
    "bytes",
    "unknown"
};

const char *    sensor_value_type_name(sensor_value_type_t type) {
    if ((unsigned int) type >= SENSOR_VALUE_NB)
        return s_sensor_value_type_name[SENSOR_VALUE_NB];
    return s_sensor_value_type_name[type];
}

typedef struct {
    size_t size;
    size_t off;
} sensor_value_info_t;
static sensor_value_info_t s_sensor_value_info[SENSOR_VALUE_NB + 1] = { { SIZE_MAX, SIZE_MAX}, };
//#ifndef __offsetof
# define voffsetof(type, field) ((size_t)(&((type *)0)->field))
//#endif
#define SENSOR_VALUE_INFO_INIT(val, field) \
    (sensor_value_info_t) { sizeof(val.field), voffsetof(sensor_value_t, field) }

// *************************************************************************************
void sensor_value_info_init() {
    sensor_value_info_t * const info = s_sensor_value_info;
    if (info[0].size != SIZE_MAX)
        return ;
    sensor_value_t v;
    memset(s_sensor_value_info, 0, sizeof(s_sensor_value_info));
    info[0]                     = (sensor_value_info_t) { 0, 0 };
    info[SENSOR_VALUE_NULL]     = (sensor_value_info_t) { 0, 0 };
    info[SENSOR_VALUE_UCHAR]    = SENSOR_VALUE_INFO_INIT(v, data.uc);
    info[SENSOR_VALUE_CHAR]     = SENSOR_VALUE_INFO_INIT(v, data.c);
    info[SENSOR_VALUE_UINT]     = SENSOR_VALUE_INFO_INIT(v, data.ui);
    info[SENSOR_VALUE_INT]      = SENSOR_VALUE_INFO_INIT(v, data.i);
    info[SENSOR_VALUE_UINT16]   = SENSOR_VALUE_INFO_INIT(v, data.u16);
    info[SENSOR_VALUE_INT16]    = SENSOR_VALUE_INFO_INIT(v, data.i16);
    info[SENSOR_VALUE_UINT32]   = SENSOR_VALUE_INFO_INIT(v, data.u32);
    info[SENSOR_VALUE_INT32]    = SENSOR_VALUE_INFO_INIT(v, data.i32);
    info[SENSOR_VALUE_ULONG]    = SENSOR_VALUE_INFO_INIT(v, data.ul);
    info[SENSOR_VALUE_LONG]     = SENSOR_VALUE_INFO_INIT(v, data.l);
    info[SENSOR_VALUE_FLOAT]    = SENSOR_VALUE_INFO_INIT(v, data.f);
    info[SENSOR_VALUE_DOUBLE]   = SENSOR_VALUE_INFO_INIT(v, data.d);
    info[SENSOR_VALUE_LDOUBLE]  = SENSOR_VALUE_INFO_INIT(v, data.ld);
    info[SENSOR_VALUE_UINT64]   = SENSOR_VALUE_INFO_INIT(v, data.u64);
    info[SENSOR_VALUE_INT64]    = SENSOR_VALUE_INFO_INIT(v, data.i64);
    info[SENSOR_VALUE_STRING]   = SENSOR_VALUE_INFO_INIT(v, data.b.buf);
    info[SENSOR_VALUE_BYTES]    = SENSOR_VALUE_INFO_INIT(v, data.b.buf);
    info[SENSOR_VALUE_NB]       = SENSOR_VALUE_INFO_INIT(v, data.c);
}

// *************************************************************************************
sensor_status_t sensor_value_fromraw(const void *src, sensor_value_t * value) {
    if (SENSOR_UNLIKELY(value == NULL || src == NULL || value->type >= SENSOR_VALUE_NB)) {
        return SENSOR_ERROR;
    }
    switch (value->type) {
        case SENSOR_VALUE_NULL:
            return SENSOR_UNCHANGED;
        case SENSOR_VALUE_BYTES: {
            size_t cpysz = value->data.b.size > value->data.b.maxsize
                           ? value->data.b.maxsize : value->data.b.size;
            if (memcmp(value->data.b.buf, (const char *) src, cpysz) == 0) {
                return SENSOR_UNCHANGED;
            }
            if (memcpy(value->data.b.buf, (const char *) src, cpysz) != value->data.b.buf) {
                return SENSOR_ERROR;
            }
            return SENSOR_UPDATED;
        }
        case SENSOR_VALUE_STRING:
            value->data.b.size = str0cpy(value->data.b.buf, (const char *) src,
                                         value->data.b.maxsize);
            break ;
        default: {
            const sensor_value_info_t * info;
            if (SENSOR_UNLIKELY(s_sensor_value_info[0].size == SIZE_MAX))
                sensor_value_info_init();
            info = &(s_sensor_value_info[value->type]);
            if (memcmp((unsigned char *)value + info->off, src, info->size) == 0) {
                return SENSOR_UNCHANGED;
            }
            if (memcpy((unsigned char *)value + info->off, src, info->size)
                    != (unsigned char *)value + info->off)
                return SENSOR_ERROR;
            return SENSOR_UPDATED;
        }
    }
    return SENSOR_SUCCESS;
}

// *************************************************************************************
sensor_status_t sensor_value_frombuffer(const char * src, unsigned int size,
                                        sensor_value_t * value) {
    unsigned int is_string;

    if (SENSOR_UNLIKELY(value == NULL || src == NULL)) {
        return SENSOR_ERROR;
    }
    if (value->type == SENSOR_VALUE_BYTES) {
        is_string = 0;
    } else if (value->type == SENSOR_VALUE_STRING) {
        is_string = 1;
    } else {
        return SENSOR_ERROR;
    }

    if (value->data.b.maxsize < size + is_string) {
        unsigned int new_size = (size + is_string) * 2;
        if ( (value->data.b.buf == NULL
              && (value->data.b.buf = malloc(new_size)) == NULL)
        ||  (value->data.b.buf = realloc(value->data.b.buf, new_size)) == NULL) {
            value->data.b.maxsize = value->data.b.size = 0;
            return SENSOR_ERROR;
        }
        value->data.b.maxsize = new_size;
        memset(value->data.b.buf, is_string ? 0 : 0xff, new_size);
    }

    if (is_string) {
        if (size == value->data.b.size && strcmp(src, value->data.b.buf) == 0) {
            return SENSOR_UNCHANGED;
        }
        value->data.b.size = str0cpy(value->data.b.buf, src, value->data.b.maxsize);
        return SENSOR_UPDATED;
    }
    if (value->data.b.size == size
    &&  memcmp(value->data.b.buf, src, size) == 0) {
        return SENSOR_UNCHANGED;
    }
    if (memcpy(value->data.b.buf, src, size) != value->data.b.buf) {
        return SENSOR_ERROR;
    }
    value->data.b.size = size;
    return SENSOR_UPDATED;
}

// *************************************************************************************
int sensor_value_tostring(const sensor_value_t * value, char *dst, size_t maxlen) {
    int ret;

    if (SENSOR_UNLIKELY(dst == NULL || maxlen <= 1 || value == NULL)) {
        if (dst == NULL || maxlen == 0) {
            return -1;
        }
        *dst = 0;
        if (value == NULL || value->type >= SENSOR_VALUE_NB)
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
        case SENSOR_VALUE_UINT16:
            return VLIB_SNPRINTF(ret, dst, maxlen, "%u",
                                 SENSOR_VALUEP_GET(value, SENSOR_VALUE_UINT16));
        case SENSOR_VALUE_INT16:
            return VLIB_SNPRINTF(ret, dst, maxlen, "%d",
                                 SENSOR_VALUEP_GET(value, SENSOR_VALUE_INT16));
        case SENSOR_VALUE_UINT32:
            return VLIB_SNPRINTF(ret, dst, maxlen, "%u",
                                 SENSOR_VALUEP_GET(value, SENSOR_VALUE_UINT32));
        case SENSOR_VALUE_INT32:
            return VLIB_SNPRINTF(ret, dst, maxlen, "%d",
                                 SENSOR_VALUEP_GET(value, SENSOR_VALUE_INT32));
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

// *************************************************************************************
long double sensor_value_todouble(const sensor_value_t * value) {
    long double result = 0.0L;

    if (SENSOR_UNLIKELY(value == NULL)) {
        errno = EFAULT;
        return 0.0L;
    }
    switch (value->type) {
        case SENSOR_VALUE_INT:
            result = (value->data.i);   break ;
        case SENSOR_VALUE_LONG:
            result = (value->data.l);   break ;
        case SENSOR_VALUE_INT64:
            result = (value->data.i64);  break ;
        case SENSOR_VALUE_CHAR:
            result = (value->data.c);   break ;
        case SENSOR_VALUE_UINT:
            result = (value->data.ui);  break ;
        case SENSOR_VALUE_ULONG:
            result = (value->data.ul);  break ;
        case SENSOR_VALUE_UINT64:
            result = (value->data.u64); break ;
        case SENSOR_VALUE_UCHAR:
            result = (value->data.uc);  break ;
        case SENSOR_VALUE_FLOAT:
            result = (value->data.f);   break ;
        case SENSOR_VALUE_INT16:
            result = (value->data.i16);  break ;
        case SENSOR_VALUE_UINT16:
            result = (value->data.u16);  break ;
        case SENSOR_VALUE_INT32:
            result = (value->data.i32);  break ;
        case SENSOR_VALUE_UINT32:
            result = (value->data.u32);  break ;
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
            if (value->data.b.size < value->data.b.maxsize) {
                if (value->data.b.buf[value->data.b.size] != 0)
                    value->data.b.buf[value->data.b.size] = 0;
            } else if (value->data.b.buf[value->data.b.size - 1] != 0) {
                value->data.b.buf[value->data.b.size - 1] = 0;
            }
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

// *************************************************************************************
intmax_t sensor_value_toint(const sensor_value_t * value) {
    intmax_t    result = 0;

    if (SENSOR_UNLIKELY(value == NULL)) {
        errno = EFAULT;
        return INTMAX_C(0);
    }

    switch (value->type) {
        case SENSOR_VALUE_INT:
            result = (value->data.i); break ;
        case SENSOR_VALUE_LONG:
            result = (value->data.l); break ;
        case SENSOR_VALUE_INT64:
            result = (value->data.i64); break ;
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
            uintmax_t uresult = (value->data.u64);
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
        case SENSOR_VALUE_INT16:
            result = (value->data.i16); break ;
        case SENSOR_VALUE_UINT16:
            result = (value->data.u16); break ;
        case SENSOR_VALUE_INT32:
            result = (value->data.i32); break ;
        case SENSOR_VALUE_UINT32:
            result = (value->data.u32); break ;
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
            if (value->data.b.size < value->data.b.maxsize) {
                if (value->data.b.buf[value->data.b.size] != 0)
                    value->data.b.buf[value->data.b.size] = 0;
            } else if (value->data.b.buf[value->data.b.size - 1] != 0) {
                value->data.b.buf[value->data.b.size - 1] = 0;
            }
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

// *************************************************************************************
int sensor_value_equal(const sensor_value_t * v1, const sensor_value_t * v2) {
    if (v1 == v2)
        return 1;
    if (SENSOR_UNLIKELY(v1 == NULL || v2 == NULL || v1->type >= SENSOR_VALUE_NB))
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
            return v1->data.u64 == v2->data.u64;
        case SENSOR_VALUE_INT16:
        case SENSOR_VALUE_UINT16:
            return v1->data.u16 == v2->data.u16;
        case SENSOR_VALUE_INT32:
        case SENSOR_VALUE_UINT32:
            return v1->data.u32 == v2->data.u32;
        /* generic memory compare for remaining types */
        default: {
            const sensor_value_info_t * info;
            if (SENSOR_UNLIKELY(s_sensor_value_info[0].size == SIZE_MAX))
                sensor_value_info_init();
            info = &(s_sensor_value_info[v1->type]);
            return 0 == memcmp((unsigned char *)v1 + info->off,
                               (unsigned char *)v2 + info->off, info->size);
        }
    }
}

// *************************************************************************************
int sensor_value_compare(const sensor_value_t * v1, const sensor_value_t * v2) {
    if (v1 == v2)
        return 0;
    if (SENSOR_UNLIKELY(v1 == NULL || v2 == NULL))
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

// *************************************************************************************
sensor_status_t sensor_value_copy(sensor_value_t * dst, const sensor_value_t * src) {
    if (SENSOR_UNLIKELY(dst == NULL || src == NULL || src->type >= SENSOR_VALUE_NB))
        return SENSOR_ERROR;

    dst->type = src->type;
    if (SENSOR_VALUE_IS_BUFFER(src->type)) {
        unsigned int size;

        if (SENSOR_UNLIKELY(src->data.b.buf == NULL || dst->data.b.buf == NULL))
            return SENSOR_ERROR;
        size = src->data.b.size;
        if (size > dst->data.b.maxsize)
            size = dst->data.b.maxsize;
        dst->data.b.size = size;
        if (src->type == SENSOR_VALUE_STRING && size < dst->data.b.maxsize)
            ++size;
        if (memcpy(dst->data.b.buf, src->data.b.buf, size) != dst->data.b.buf)
            return SENSOR_ERROR;
        return SENSOR_SUCCESS;
    } else {
        const sensor_value_info_t * info;
        if (SENSOR_UNLIKELY(s_sensor_value_info[0].size == SIZE_MAX))
            sensor_value_info_init();
        info = &(s_sensor_value_info[src->type]);

        if (memcpy((unsigned char *) dst + info->off, (unsigned char *) src + info->off,
                   info->size) != (unsigned char *) dst + info->off)
            return SENSOR_ERROR;
        return SENSOR_SUCCESS;
    }
}

// *************************************************************************************
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

