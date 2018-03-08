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
#include <ctime>
#else
#include <time.h>
#endif

#include "vlib/slist.h"
#include "vlib/log.h"

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
   SENSOR_SUCCESS = 0,
   SENSOR_ERROR = -1
} sensor_status_t;

/**
 * Enum: Type of sensor value.
 */
typedef enum {
    SENSOR_VALUE_UINT,
    SENSOR_VALUE_INT,
    SENSOR_VALUE_ULONG,
    SENSOR_VALUE_LONG,
    SENSOR_VALUE_FLOAT,
    SENSOR_VALUE_DOUBLE,
    SENSOR_VALUE_INT64,
    SENSOR_VALUE_UINT64,
    SENSOR_VALUE_BYTES,
    SENSOR_VALUE_NB // Must be Last.
} sensor_value_type_t;

/**
 * Type: a sensor value.
 */
typedef struct {
    sensor_value_type_t     type;
    union {
        int                 i;
        unsigned int        ui;
        long                l;
        unsigned long       ul;
        float               f;
        double              d;
        long long           ll;
        unsigned long long  ull;
        struct {
            char *              buf;
            size_t              size;
        } b;
    } data;
} sensor_value_t;

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
    const char *            label;
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

/**
 * Initialize sensor module. Must be called prior to all other operations.
 * User must clean it with sensor_free().
 * @param logs the preinitialized per-module log list. can be NULL.
 * @return sensor handle
 */
sensor_ctx_t *   sensor_init(slist_t * logs);

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
 * Get the list of updated sensors, among watch list, according to update interval.
 * User must clean the updated_list with sensor_update_free() once
 * it has been used, independently of watch_list.
 * @param sctx the sensor context
 * @param now a struct timeval pointer to indicate current time, or NULL to let
 *            libvsensors evaluate time. now is a correct relative time, it can
 *            be the real date, but it is not required.
 * @return slist_t *<sensor_sample_t*>
 */
slist_t *       sensor_update_get(sensor_ctx_t * sctx, const struct timeval * now);

/** Clean the list of updated watched sensors */
void            sensor_update_free(slist_t * updates);

/**
 * Copy a raw value in the sensor value.
 * (*src) has the type of sensor->value.type, eg: int for SENSOR_VALUE_INT.
 */
sensor_status_t sensor_value_fromraw(const void *src, sensor_value_t * value);

/** Put a sensor value into a string */
sensor_status_t sensor_value_tostring(const sensor_value_t * value, char *dst, size_t maxlen);

/** convert a sensor value to double */
double          sensor_value_todouble(const sensor_value_t * value);

/**
 * Compare two sensor values
 * @return 0 on equality, non 0 if different in type or value.
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
const char *const* libvsensors_get_source();


#ifdef __cplusplus
}
#endif

#endif // !ifdef H
