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
 * Public header for sensor management.
 *
 * Usage:
 * (A) from scratch
 *    1. Get the list of supported sensors
 *       sensor_list_get()
 *         (clean with sensor_list_free)
 *
 *    2. Register one or several watchs
 *       watch_list = NULL;
 *       watch_list = sensor_watch_add(sensor, watch_properties, watch_list)
 *         (clean with sensor_watch_free(watch_list))
 *
 *    3. Optionally save the watch_list to file
 *       watch_list_save(watch_list)
 *
 * (B) from config
 *     1. Get watch list from saved config
 *        watch_list = sensor_watch_load()
 *          (clean with sensor_watch_free(watchlist))
 * (C) Watch
 *     update_list = sensor_update_get(watch_list, NULL)
 *       (clean with sensor_update_free(update_list))
 */
#ifndef SENSOR_SENSOR_H
#define SENSOR_SENSOR_H

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

#include "vlib/slist.h"
#include "vlib/log.h"
#include "vlib/logpool.h"

#ifndef PRIu64
# define PRIu64 "llu"
#endif
#ifndef PRId64
# define PRId64 "lld"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** sensor family struct to be defined below */
typedef struct sensor_family_s sensor_family_t;
/** sensor sample struct to be defined below */
typedef struct sensor_sample_s sensor_sample_t;

/**
 * Enum: Status of sensor functions
 */
typedef enum {
   SENSOR_SUCCESS           = 0,
   SENSOR_UNCHANGED         = SENSOR_SUCCESS,
   SENSOR_UPDATED           = 1,
   SENSOR_ERROR             = -1,
   SENSOR_NOT_SUPPORTED     = -2
} sensor_status_t;

/**
 * Enum: Type of sensor value.
 */typedef enum {
    SENSOR_VALUE_NULL,
    SENSOR_VALUE_UCHAR,
    SENSOR_VALUE_CHAR,
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
            do { \
                (_val).type = (_type); \
                SENSOR_VALUE_GET(_val, _type) = (_buf); \
                (_val).data.b.maxsize = (_val).data.b.size = (_maxsize); \
            } while (0)

/**
 * Sensor values internal types
 */
#define TYPE_SENSOR_VALUE_NULL      void *
#define TYPE_SENSOR_VALUE_UCHAR     unsigned char
#define TYPE_SENSOR_VALUE_CHAR      char
#define TYPE_SENSOR_VALUE_UINT      unsigned int
#define TYPE_SENSOR_VALUE_INT       int
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
        TYPE_SENSOR_VALUE_ULONG     ul;
        TYPE_SENSOR_VALUE_LONG      l;
        TYPE_SENSOR_VALUE_FLOAT     f;
        TYPE_SENSOR_VALUE_DOUBLE    d;
        TYPE_SENSOR_VALUE_LDOUBLE   ld SENSOR_PACKED; /*TODO: pack because of struct boundary
                                                     longdouble 16 --> + type --> boundary 32*/
        TYPE_SENSOR_VALUE_UINT64    ull;
        TYPE_SENSOR_VALUE_INT64     ll;
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
#define SENSOR_VALUE_TYPE(type)     TYPE_##type
#define SENSOR_VALUE_GET(_v,_type)  ((_v).data. SENSOR_VALUE_NAME(_type))
#define SENSOR_VALUEP_GET(_v,_type) ((_v)->data. SENSOR_VALUE_NAME(_type))

#define GET_SENSOR_VALUE_UCHAR      uc
#define GET_SENSOR_VALUE_CHAR       c
#define GET_SENSOR_VALUE_UINT       ui
#define GET_SENSOR_VALUE_INT        i
#define GET_SENSOR_VALUE_ULONG      ul
#define GET_SENSOR_VALUE_LONG       l
#define GET_SENSOR_VALUE_FLOAT      f
#define GET_SENSOR_VALUE_DOUBLE     d
#define GET_SENSOR_VALUE_LDOUBLE    ld
#define GET_SENSOR_VALUE_UINT64     ull
#define GET_SENSOR_VALUE_INT64      ll
#define GET_SENSOR_VALUE_BYTES      b.buf
#define GET_SENSOR_VALUE_STRING     b.buf
#define GET_SENSOR_VALUE_NULL       b.buf

/**
 * Structure identifying a sensor family/plugin.
 * private to sensor files. TODO
 */
typedef struct {
    const char *        name;
    sensor_status_t     (*init)(struct sensor_family_s *f);
    sensor_status_t     (*free)(struct sensor_family_s *f);
    slist_t *           (*list)(struct sensor_family_s *f);
    sensor_status_t     (*update)(struct sensor_sample_s *sensor, const struct timeval * now);
} sensor_family_info_t;


/**
 * public or private. FIXME TODO
 */
struct sensor_family_s {
    const sensor_family_info_t *    info;
    log_t *                         log;
    void *                          priv;
};

/**
 * Type: Description of a single sensor
 */
typedef struct {
    void *                  key;
    char *                  label;
    sensor_value_type_t     type;
    sensor_family_t *       family; // BOF TODO
} sensor_desc_t;

/**
 * Type: call back on sensor update
 * TBD/RFU/TODO
 */
typedef sensor_status_t * (*sensor_watch_callback_t)(struct sensor_sample_s *);

/** Sensor warning Levels */
enum {
    SENSOR_LEVEL_WARN = 0,
    SENSOR_LEVEL_CRITICAL,
    SENSOR_LEVEL_NB
};

/**
 * Type: Sensor watch properties
 */
typedef struct {
    time_t                  update_interval_ms;
    sensor_value_t          warn_levels[SENSOR_LEVEL_NB];
    sensor_value_t          update_threshold;
    sensor_watch_callback_t callback;
} sensor_watch_t;

/**
 * Type: Sensor Sample
 */
struct sensor_sample_s {
    sensor_desc_t *     desc;
    sensor_watch_t      watch;
    sensor_value_t      value;
    struct timeval      last_update_time;
    void *              priv;
};

/** opaque sensor handle */
typedef struct sensor_ctx_s sensor_ctx_t;

/** prefix used for libvsensors */
#define SENSOR_LOG_PREFIX   "sensors"

/**
 * Initialize sensor module. Must be called prior to all other operations.
 * User must clean it with sensor_free().
 * @param logs the preinitialized per-module log pool. can be NULL.
 * @return sensor handle
 */
sensor_ctx_t *   sensor_init(logpool_t * logs);

/** Clean the sensor handle */
sensor_status_t  sensor_free(sensor_ctx_t * sctx);

/**
 * Get the list of supported sensors
 * User must clean it with sensor_list_free().
 * @param sctx the sensor context
 * @return slist_t *<sensor_desc_t>
 */
slist_t *       sensor_list_get(sensor_ctx_t *sctx);

/** Clean the list of supported sensors */
void            sensor_list_free(sensor_ctx_t * sctx);

/**
 * Add a sensor to watch.
 * User must clean the watch_list with sensor_watch_free().
 * @param sensor the sensor to watch or NULL to watch all sensors available.
 * @param watch the watch properties (interval, ...)
 * @param sctx the sensor context
 * @return slist_t *<sensor_sample_t*>
 */
slist_t *       sensor_watch_add(sensor_desc_t *sensor, sensor_watch_t *watch, sensor_ctx_t *sctx);

/** Clean the list of watchs */
void            sensor_watch_free(sensor_ctx_t *sctx);

/** Get the Greatest Common Divisor of watchs intervals */
time_t          sensor_watch_pgcd(sensor_ctx_t *sctx);

/**
 * Save the list of watchs to file
 * @return status of operation
 */
sensor_status_t sensor_watch_save(slist_t * watch_list, const char * path);

/**
 * Load the list of watchs from File.
 * User must clean the watch_list with sensor_watch_free().
 * @return slist_t *<sensor_sample_t*>
 */
slist_t *       sensor_watch_load(const char * path);

/**
 * Update a given sensor, according to its update interval.
 * @param sensor the sensor to update
 * @param now a struct timeval pointer to indicate current time, or NULL to let
 *            libvsensors evaluate time. now is a correct relative time, it can
 *            be the real date, but it is not required.
 * @return SENSOR_ERROR or SENSOR_NOT_SUPPORTED on error
 *         SENSOR_UPDATED   if updated
 *         SENSOR_UNCHANGED if not updated
 */
sensor_status_t sensor_update_check(sensor_sample_t * sensor, const struct timeval * now);

/**
 * Get the list of updated sensors, among watch list, according to update interval.
 * User must clean the updated_list with sensor_update_free() once
 * it has been used, independently of watch_list.
 * @param sctx the sensor context
 * @param now a struct timeval pointer to indicate current time, or NULL to let
 *            libvsensors evaluate time. now is a correct relative time, it can
 *            be the real date, but it is not required.
 * @return slist_t *<sensor_sample_t*>
 * @note: Prefer sensor_update_check() to avoid mallocs/frees .
 *        Calling this function will require <nb_updates> malloc and free.
 */
slist_t *       sensor_update_get(sensor_ctx_t * sctx, const struct timeval * now);

/** Clean the list of updated watched sensors */
void            sensor_update_free(slist_t * updates);

/**
 * Copy a raw value in the sensor value.
 * (*src) has the type of value->type, eg: int for SENSOR_VALUE_INT.
 * @notes: for the types SENSOR_VALUE_STRING and SENSOR_VALUE_BYTES,
 * value.data.b.buf must point to a valid buffer of max size value->data.b.maxsize.
 * @notes: for the type SENSOR_VALUE_BYTES, value->data.b.size must be set
 * to the desired amount of bytes to copy from src, before calling this function.
 */
sensor_status_t sensor_value_fromraw(const void *src, sensor_value_t * value);

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
intmax_t         sensor_value_toint(const sensor_value_t * value);

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

/**
 * Get libvsensors version
 * @return array of const char *, terminated by NULL.
 */
const char *    libvsensors_get_version();

/**
 * Get libvsensors source code
 */
int libvsensors_get_source(FILE * out, char * buffer, unsigned int buffer_size, void ** ctx);

#ifdef __cplusplus
}
#endif

#endif // !ifdef H
