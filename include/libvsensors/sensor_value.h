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
 * Generic Sensor Management Library :
 * Public header for sensor_value_t type.
 *
 * Usage:
 *  sensor_value_t value1, value2;
 *  SENSOR_VALUE_INIT_STR(value1, "hello");
 *  SENSOR_VALUE_INIT(value2, SENSOR_VALUE_INT, 2);
 *  if (sensor_value_compare(&value1, &value2) == 0) { }
 */
#ifndef LIBVSENSORS_SENSOR_H
# include "sensor.h"
#endif
#ifndef LIBVSENSORS_SENSOR_VALUE_H
#define LIBVSENSORS_SENSOR_VALUE_H

#include <sys/types.h>
#include <sys/time.h>
#ifdef __cplusplus
# include <ctime>
# include <climits>
# include <cstdint>
# include <cinttypes>
#else
# include <time.h>
# include <limits.h>
# include <stdint.h>
# include <inttypes.h>
#endif

/* ************************************************************************ */
#ifndef PRIu64
# define PRIu64 "llu"
#endif
#ifndef PRIx64
# define PRIx64 "llx"
#endif
#ifndef PRIX64
# define PRIX64 "llX"
#endif
#ifndef PRId64
# define PRId64 "lld"
#endif

/* ************************************************************************ */

/**
 * Enum: Type of sensor value.
 */typedef enum {
    SENSOR_VALUE_NULL,
    SENSOR_VALUE_UCHAR,
    SENSOR_VALUE_CHAR,
    SENSOR_VALUE_UINT16,
    SENSOR_VALUE_INT16,
    SENSOR_VALUE_UINT32,
    SENSOR_VALUE_INT32,
    SENSOR_VALUE_UINT,
    SENSOR_VALUE_INT,
    SENSOR_VALUE_ULONG,
    SENSOR_VALUE_LONG,
    SENSOR_VALUE_FLOAT,
    SENSOR_VALUE_DOUBLE,
    SENSOR_VALUE_LDOUBLE,
    SENSOR_VALUE_UINT64,
    SENSOR_VALUE_INT64,
    SENSOR_VALUE_STRING,
    SENSOR_VALUE_BYTES,
    SENSOR_VALUE_NB // Must be Last.
} sensor_value_type_t;

#define SENSOR_VALUE_IS_FLOATING(type) \
            (   (type) == SENSOR_VALUE_FLOAT \
             || (type) == SENSOR_VALUE_DOUBLE || (type) == SENSOR_VALUE_LDOUBLE)

#define SENSOR_VALUE_IS_BUFFER(type) \
            ((type) == SENSOR_VALUE_STRING || (type) == SENSOR_VALUE_BYTES)

#define SENSOR_VALUE_INIT(_val, _type, _value) \
            do { \
                (_val).type = (_type); \
                SENSOR_VALUE_GET(_val, _type) = (_value); \
            } while (0)

#define SENSOR_VALUE_INIT_BUF(_val, _type, _buf, _maxsize) \
            do { if (!SENSOR_VALUE_IS_BUFFER((_type))) break ; \
                (_val).type = (_type); \
                (_val).data.b.buf = (_buf); \
                (_val).data.b.maxsize = (_val).data.b.size = (_maxsize); \
                if ((_val).data.b.buf == NULL && (_val).data.b.maxsize > 0) { \
                    (_val).data.b.size = 0; \
                    if (((_val).data.b.buf \
                        = malloc(((_val).data.b.maxsize) \
                                 * sizeof(*((_val).data.b.buf)))) == NULL) \
                        (_val).data.b.maxsize = 0; \
                    else memset((_val).data.b.buf, \
                                ((_val).type == SENSOR_VALUE_STRING \
                                ? 0 : 0xff), ((_val).data.b.maxsize)); \
                } \
            } while (0)

#define SENSOR_VALUE_INIT_STR(_val, _str) \
            SENSOR_VALUE_INIT_BUF(_val, SENSOR_VALUE_STRING, (char *)_str, (_str) ? strlen(_str)+1 : 0)

/**
 * Sensor values internal types
 */
#define TYPE_SENSOR_VALUE_NULL      void *
#define TYPE_SENSOR_VALUE_UCHAR     unsigned char
#define TYPE_SENSOR_VALUE_CHAR      char
#define TYPE_SENSOR_VALUE_UINT      unsigned int
#define TYPE_SENSOR_VALUE_INT       int
#define TYPE_SENSOR_VALUE_UINT16    uint16_t
#define TYPE_SENSOR_VALUE_INT16     int16_t
#define TYPE_SENSOR_VALUE_UINT32    uint32_t
#define TYPE_SENSOR_VALUE_INT32     int32_t
#define TYPE_SENSOR_VALUE_ULONG     unsigned long
#define TYPE_SENSOR_VALUE_LONG      long
#define TYPE_SENSOR_VALUE_FLOAT     float
#define TYPE_SENSOR_VALUE_DOUBLE    double
#define TYPE_SENSOR_VALUE_LDOUBLE   long double
#define TYPE_SENSOR_VALUE_UINT64    uint64_t
#define TYPE_SENSOR_VALUE_INT64     int64_t
#define TYPE_SENSOR_VALUE_BYTES     unsigned char *
#define TYPE_SENSOR_VALUE_STRING    char *

#define SENSOR_PACKED __attribute__((__packed__))

/**
 * Type: a sensor value.
 */
typedef struct {
    union {
        TYPE_SENSOR_VALUE_UCHAR     uc;
        TYPE_SENSOR_VALUE_CHAR      c;
        TYPE_SENSOR_VALUE_UINT      ui;
        TYPE_SENSOR_VALUE_INT       i;
        TYPE_SENSOR_VALUE_UINT16    u16;
        TYPE_SENSOR_VALUE_INT16     i16;
        TYPE_SENSOR_VALUE_UINT32    u32;
        TYPE_SENSOR_VALUE_INT32     i32;
        TYPE_SENSOR_VALUE_ULONG     ul;
        TYPE_SENSOR_VALUE_LONG      l;
        TYPE_SENSOR_VALUE_FLOAT     f;
        TYPE_SENSOR_VALUE_DOUBLE    d;
        TYPE_SENSOR_VALUE_LDOUBLE   ld SENSOR_PACKED; /*TODO: pack because of struct boundary
                                                     longdouble 16 --> + type --> boundary 32*/
        TYPE_SENSOR_VALUE_UINT64    u64;
        TYPE_SENSOR_VALUE_INT64     i64;
        struct {
            char *              buf;
            unsigned int        size;
            unsigned int        maxsize;
        } b;
    } data;
    unsigned int            type:5;
    unsigned int            reserved:27;
} sensor_value_t;

#define SENSOR_VALUE_NAME(type)     GET_##type
#define SENSOR_VALUE_NAME_X(type)   SENSOR_VALUE_NAME(type)
#define SENSOR_VALUE_TYPE(type)     TYPE_##type
#define SENSOR_VALUE_TYPE_X(type)   SENSOR_VALUE_TYPE(type)
#define SENSOR_VALUE_GET(_v,_type)  ((_v).data. SENSOR_VALUE_NAME(_type))
#define SENSOR_VALUEP_GET(_v,_type) ((_v)->data. SENSOR_VALUE_NAME(_type))

#define GET_SENSOR_VALUE_UCHAR      uc
#define GET_SENSOR_VALUE_CHAR       c
#define GET_SENSOR_VALUE_UINT       ui
#define GET_SENSOR_VALUE_INT        i
#define GET_SENSOR_VALUE_UINT16     u16
#define GET_SENSOR_VALUE_INT16      i16
#define GET_SENSOR_VALUE_UINT32     u32
#define GET_SENSOR_VALUE_INT32      i32
#define GET_SENSOR_VALUE_ULONG      ul
#define GET_SENSOR_VALUE_LONG       l
#define GET_SENSOR_VALUE_FLOAT      f
#define GET_SENSOR_VALUE_DOUBLE     d
#define GET_SENSOR_VALUE_LDOUBLE    ld
#define GET_SENSOR_VALUE_UINT64     u64
#define GET_SENSOR_VALUE_INT64      i64
#define GET_SENSOR_VALUE_BYTES      b.buf
#define GET_SENSOR_VALUE_STRING     b.buf
#define GET_SENSOR_VALUE_NULL       b.buf

/* ************************************************************************ */
#ifdef __cplusplus
extern "C" {
#endif
/* ************************************************************************ */

/* ************************************************************************
 * SENSOR_VALUE
 * ************************************************************************ */

/** get the string name of a given sensor value type */
const char *    sensor_value_type_name(sensor_value_type_t type);

/**
 * Copy a raw value in the sensor value.
 * (*src) has the type of value->type, eg: int for SENSOR_VALUE_INT.
 * @notes: for the types SENSOR_VALUE_STRING and SENSOR_VALUE_BYTES,
 * value.data.b.buf must point to a valid buffer of max size value->data.b.maxsize.
 * @notes: for the type SENSOR_VALUE_BYTES, value->data.b.size must be set
 * to the desired amount of bytes to copy from src, before calling this function.
 * @return SENSOR_SUCCESS on successful operation without knowing if value was changed
 *         SENSOR_UPDATED, if the value changed
 *         SENSOR_UNCHANGED, if the value was not changed
 *         SENSOR_ERROR on error.
 */
sensor_status_t sensor_value_fromraw(const void *src, sensor_value_t * value);

/**
 * Copy a buffer in the sensor value.
 * The internal buffer of sensor value is allocated or expanded if needed.
 * It should then be initialized to NULL or maxsize to 0.
 * @return SENSOR_SUCCESS on successful operation without knowing if value was changed
 *         SENSOR_UPDATED, if the value changed
 *         SENSOR_UNCHANGED, if the value was not changed
 *         SENSOR_ERROR on error or if value is neither SENSOR_VALUE_BYTES
 *                                  nor SENSOR_VALUE_STRING.
 */
sensor_status_t sensor_value_frombuffer(const char * src, unsigned int size,
                                        sensor_value_t * value);

/** Put a sensor value into a string
 * @param value the sensor value to stringize
 * @param maxlen behaves as snprintf (dst always 0 terminated)
 * @return * the length of result string on success
 *         the string can be truncated depending on maxlen.
 *         * -1 or negative value on error */
int             sensor_value_tostring(const sensor_value_t * value, char *dst, size_t maxlen);

/** convert a sensor value to greatest floating point type */
long double     sensor_value_todouble(const sensor_value_t * value);

/** convert a sensor value to greatest supported signed integer type */
intmax_t        sensor_value_toint(const sensor_value_t * value);

/**
 * Check equality of two sensor values
 * They are not equal if their type is different.
 * @return non-0 on equality, or 0 if values or type are different.
 */
int             sensor_value_equal(const sensor_value_t * v1, const sensor_value_t * v2);

/**
 * Compare two sensor values
 * If you don't need to compare different types and don't need order
 * information (v1>v2,...), you must use sensor_value_equal() which is faster.
 * @return 0 on equality, < 0 if v1 < v2, > 0 if v1 > v2.
 */
int             sensor_value_compare(const sensor_value_t * v1, const sensor_value_t * v2);

/** Copy a sensor value to another.
 * notes: for buffers and strings, data.b.buf and data.b.maxsize must be valid.
 * @return SENSOR_SUCCESS if ok or SENSOR_ERROR on error. */
sensor_status_t sensor_value_copy(sensor_value_t * dst, const sensor_value_t * src);

/* ************************************************************************ */
#ifdef __cplusplus
}
#endif
/* ************************************************************************ */

#endif // !ifdef H

