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
 * Public header for sensor management.
 *
 * Usage:
 * (INIT) sensor_context = sensor_init(NULL);
 * (A) from scratch
 *    1. Get the list of supported sensors
 *       list = sensor_list_get()
 *         (clean with sensor_list_free)
 *
 *    2. Register one or several watchs
 *       watch_properties = SENSOR_WATCH_INITIALIZER(timer_ms, NULL)
 *       watch_list = sensor_watch_add(sensor_context, "*", SSF_DEFAULT, watch_properties)
 *         (clean with sensor_watch_free(sensor_context))
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
 * (FREE) sensor_free(sensor_ctx);
 */
#ifndef LIBVSENSORS_SENSOR_H
#define LIBVSENSORS_SENSOR_H

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

/**
 * Enum: Status of sensor functions
 */
typedef enum {
   SENSOR_SUCCESS           = 0,
   SENSOR_UPDATED           = 1,
   SENSOR_UNCHANGED         = 2,
   SENSOR_WAIT_TIMER        = 3,
   SENSOR_RELOAD_FAMILY     = 4,
   SENSOR_LOADING           = 5,
   SENSOR_ERROR             = -1,
   SENSOR_NOT_SUPPORTED     = -2
} sensor_status_t;

/* include the sensor_value_t type and sensor_value_*() functions */
#include "sensor_value.h"

/** opaque sensor handle */
typedef struct sensor_ctx_s sensor_ctx_t;

/** sensor family struct to be defined below */
typedef struct sensor_family_s sensor_family_t;

/** sensor desc struct to be defined below */
typedef struct sensor_desc_s sensor_desc_t;

/** sensor sample struct to be defined below */
typedef struct sensor_sample_s sensor_sample_t;

/** RFU/TBD/TODO sensor sample watch events
 * for sensor_watch_callback_t and sensor_family_t.notify() */
typedef enum {
    SWE_NONE            = 0,
    SWE_FAMILY_RELOADED = 1 << 1,
    SWE_WATCH_UPDATED   = 1 << 2,
    SWE_WATCH_ADDED     = 1 << 3,
    SWE_WATCH_REPLACED  = 1 << 4,
    SWE_WATCH_DELETING  = 1 << 5,
} sensor_watch_event_t;

/** RFU/TBD event_data type for sensor_watch_callback_t and sensor_family_t.notify() */
typedef union {
    sensor_family_t *   family; /* for SWE_FAMILY_RELOADED */
    /** RFU */
    void *              data;
} sensor_watch_ev_data_t;

/**
 * Structure identifying a sensor family/plugin.
 * private to sensor files and plugins. TODO
 */
typedef struct {
    const char *        name;

    /** init(): return SENSOR_ERROR on error, SENSOR_NOT_SUPPORTED or SENSOR_SUCCESS.
     * On error, family->free() is NOT called, family has to clean resources. */
    sensor_status_t     (*init)(struct sensor_family_s *f);

    /** free(): return SENSOR_ERROR on error, or SENSOR_SUCCESS */
    sensor_status_t     (*free)(struct sensor_family_s *f);

    /** list(): return a new allocated list, which will be freed by libvsensors:
     * The content of list (sensor_desc_t*) is owned by the family and must be freed by it,
     * but the list itself belongs to libvsensors and must NOT be used anymore by the family. */
    slist_t *           (*list)(struct sensor_family_s *f);

    /** update(): called by libvsensors when the update_interval_ms has been reached.
     * Family should only update the sensor_sample->value and return:
     * - SENSOR_ERROR or SENSOR_NOT_SUPPORTED on error:
     * - SENSOR_UPDATED if the value has changed
     * - SENSOR_UNCHANGED if the value did not change
     * - SENSOR_SUCCESS if it does not know if value changed.
     * update_interval is not reset on error, now can be NULL to force an update. */
    sensor_status_t     (*update)(struct sensor_sample_s *sensor, const struct timeval * now);

    /** write(): called by libvsensors to write a value on sensors supporting it.
     * - SENSOR_ERROR or SENSOR_NOT_SUPPORTED on error:
     * - SENSOR_SUCCESS on success. */
    sensor_status_t     (*write)(const struct sensor_desc_s * sensor, const sensor_value_t * value);

    /** RFU notify(): return SENSOR_ERROR on error, or SENSOR_SUCCESS
     * event is a bit combination of sensor_watch_event_t */
    sensor_status_t     (*notify)(unsigned int event, struct sensor_family_s *f,
                                  struct sensor_sample_s *, sensor_watch_ev_data_t * ev_data);
} sensor_family_info_t;


/**
 * public or private. TODO
 */
struct sensor_family_s {
    const sensor_family_info_t *    info;
    log_t *                         log;
    sensor_ctx_t *                  sctx;
    void *                          priv;
};

/** sensor_property_t: misc optional sensor properties */
typedef struct {
    const char *    name;
    sensor_value_t  value;
} sensor_property_t;

/**
 * Type: Description of a single sensor
 */
struct sensor_desc_s {
    /** key  : family specific field: handled by family */
    void *                  key;
    /** label: unique identifier of sensor of given family */
    const char *            label;
    /** properties : optional sensor properties: array terminated by
     *  { NULL, (sensor_value_t) { .type = NULL, } } */
    sensor_property_t *     properties;
    /** type : if BYTES/STRING, buffer is set to NULL, families are reponsible
     *         to (re)alloc it if needed, but only libvsensors will free it */
    sensor_value_type_t     type;
    /** family: BOF TODO */
    sensor_family_t *       family;
};

/** Type: call back on sensor update : RFU/TBD/TODO */
typedef sensor_status_t (*sensor_watch_callback_t)(
            unsigned int                event, /* bit combination of sensor_watch_event_t */
            struct sensor_ctx_s *       sctx,
            struct sensor_sample_s *    sample,
            sensor_watch_ev_data_t *    event_data);

/** Sensor warning Levels for sensor_watch_t */
enum {
    SENSOR_LEVEL_THRESHOLD = 0,
    SENSOR_LEVEL_WARN,
    SENSOR_LEVEL_CRITICAL,
    SENSOR_LEVEL_NB
};

/**
 * Type: Sensor watch properties
 */
typedef struct {
    struct timeval          update_interval;
    sensor_value_t          update_levels[SENSOR_LEVEL_NB];
    sensor_watch_callback_t callback;
} sensor_watch_t;

#define SENSOR_WATCH_INITIALIZER(_interval_ms, _callback)                   \
    (sensor_watch_t) {                                                      \
        .update_interval = (struct timeval) {                               \
            .tv_sec     = _interval_ms / 1000,                              \
            .tv_usec    = (_interval_ms % 1000) * 1000 },                   \
        .callback = _callback,                                              \
        .update_levels = { (sensor_value_t) { .type = SENSOR_VALUE_NULL},   \
                           (sensor_value_t) { .type = SENSOR_VALUE_NULL},   \
                           (sensor_value_t) { .type = SENSOR_VALUE_NULL} }, \
    }

/**
 * Type: Sensor Sample
 */
struct sensor_sample_s {
    const sensor_desc_t *   desc;
    const sensor_watch_t *  watch;
    sensor_value_t          value;
    struct timeval          next_update_time;
    void *                  user_data; /* for MMIs */
    /** userfreefun() : essentially used to clean user data contained in user_data */
    void                    (*userfreefun)(void* /* (sensor_sample_t *) */);
};

/**
 * Type: sensor_{,watch_}{add*,del*} flags
 */
typedef enum {
    SSF_NONE            = 0,
    SSF_CASEFOLD        = 1 << 0,   /* case in-sensitive */
    SSF_NOPATTERN       = 1 << 1,   /* no pattern, string comparison */
    SSF_LOCK_WRITE      = 1 << 2,   /* acquire write lock rather than read lock */
    SSF_DEFAULT         = SSF_CASEFOLD
} sensor_search_flags_t;

/**
 * Types: sensor_{,watch_}visit_t
 *   Functions visiting sensors or watchs for sensor_visit() and sensor_watch_visit().
 */
typedef sensor_status_t (*sensor_visitfun_t)(const sensor_desc_t * desc, void * user_data);
typedef sensor_status_t (*sensor_watch_visitfun_t)(sensor_sample_t * sample, void * user_data);

/**
 * LOG prefix used for libvsensors
 */
#define SENSOR_LOG_PREFIX   "sensors"

/* ************************************************************************ */
#ifdef __cplusplus
extern "C" {
#endif
/* ************************************************************************ */

/* ************************************************************************
 * SENSOR_CONTEXT : sensor context operations(init/free/lock/...)
 * ************************************************************************ */

/**
 * Initialize sensor module. Must be called prior to all other operations.
 * User must clean it with sensor_free().
 * @param logs the preinitialized per-module log pool. can be NULL.
 * @return sensor handle
 */
sensor_ctx_t *  sensor_init(logpool_t * logs);

/** Clean the sensor handle */
sensor_status_t sensor_free(sensor_ctx_t * sctx);

/** wait until all sensors have been loaded */
sensor_status_t sensor_init_wait(sensor_ctx_t * sctx);

/** locking stuff */
typedef enum {
    SENSOR_LOCK_READ    = 0,
    SENSOR_LOCK_WRITE   = 1,
} sensor_lock_type_t;

/** Acquire a READ or WRITE lock.
 * Owner of write lock is allowed to make recursive calls to sensor_lock()
 * (with corresponding calls to sensor_unlock()) */
sensor_status_t sensor_lock(sensor_ctx_t * sctx, sensor_lock_type_t lock_type);

/** release lock / undefined behavior if not locked */
sensor_status_t sensor_unlock(sensor_ctx_t * sctx);

/* ************************************************************************
 * SENSOR_DESCS : operation on sensors desc list
 * ************************************************************************ */

/**
 * get the list of supported sensors.
 * User has to bound call between [sensor_lock(),sensor_unlock()] if needed.
 * @param sctx the sensor context
 * @return slist_t * <sensor_desc_t *>
 */
const slist_t * sensor_list_get(sensor_ctx_t *sctx);

/** Clean the list of supported sensors */
void            sensor_list_free(sensor_ctx_t * sctx);

/**
 * look for a sensor in sensor list.
 * @param sctx the sensor context
 * @param pattern fnmatch(3) pattern with format '<family>/<label>'.
 * @param flags, bit combination of sensor_search_flags_t
 * @param matchs, if not NULL, list of <sensor_desc_t *> filled with results,
 *        to be freed with slist_free(*matchs, NULL).
 * @return the first matching element or NULL if not found.
 */
sensor_desc_t * sensor_find(
                        sensor_ctx_t *          sctx,
                        const char *            pattern,
                        unsigned int            flags,
                        slist_t **              matchs);

/**
 * Visit a set of sensors.
 * @param sctx the sensor context
 * @param pattern fnmatch(3) pattern with format '<family>/<label>'.
 * @param flags, bit combination of sensor_search_flags_t
 * @param visit, the function called on each matching sensor.
 * @param user_data, user data given to visit()
 * @return SENSOR_SUCCESS or SENSOR_ERROR.
 */
sensor_status_t sensor_visit(
                        sensor_ctx_t *          sctx,
                        const char *            pattern,
                        unsigned int            flags,
                        sensor_visitfun_t       visit,
                        void *                  user_data);

/** check wheter the pattern matches a given sensor
 * @param sensor the sensor to be checked.
 * @param pattern fnmatch(3) pattern with format '<family>/<label>'.
 * @param flags, bit combination of sensor_search_flags_t
 * @return SENSOR_SUCCESS if matching, SENSOR_ERROR otherwise */
sensor_status_t sensor_desc_match(
                        sensor_ctx_t *          sctx,
                        const char *            pattern,
                        unsigned int            flags,
                        const sensor_desc_t *   sensor);

/* ************************************************************************
 * SENSOR_WATCHS : operation on sensors watch list
 * ************************************************************************ */

/**
 * Add or replace a sensor to be watched.
 * User can clean the watch_list with sensor_watch_free() and watchs will be canceled.
 * @param sctx the sensor context
 * @param sensor the sensor to watch or NULL to watch all sensors available.
 * @param watch the watch properties (interval, ...)
 * @param flags, bit combination of sensor_search_flags_t, but SSF_CASEFOLD is ignored.
 * @return SENSOR_SUCCESS or SENSOR_ERROR
 */
sensor_status_t sensor_watch_add_desc(
                        sensor_ctx_t *          sctx,
                        const sensor_desc_t *   sensor,
                        unsigned int            flags,
                        sensor_watch_t *        watch);

/**
 * Add or replaced sensors to be watched.
 * User can clean the watch_list with sensor_watch_free() and watchs will be canceled.
 * @param sctx the sensor context
 * @param pattern fnmatch(3) pattern with format '<family>/<label>'.
 * @param watch the watch properties (interval, ...) - will be duplicated.
 * @param flags, bit combination of sensor_search_flags_t
 * @return SENSOR_SUCCESS or SENSOR_ERROR
 */
sensor_status_t sensor_watch_add(
                        sensor_ctx_t *          sctx,
                        const char *            pattern,
                        unsigned int            flags,
                        sensor_watch_t *        watch);

/**
 * delete sensors from watch list.
 * User can clean the watch_list with sensor_watch_free() and watchs will be canceled.
 * @param sctx the sensor context
 * @param pattern fnmatch(3) pattern with format '<family>/<label>'.
 * @param flags, bit combination of sensor_search_flags_t
 * @return SENSOR_SUCCESS or SENSOR_ERROR
 */
sensor_status_t sensor_watch_del(
                        sensor_ctx_t *          sctx,
                        const char *            pattern,
                        unsigned int            flags);

/**
 * look for a sensor in watch list.
 * @param sctx the sensor context
 * @param pattern fnmatch(3) pattern with format '<family>/<label>'.
 * @param flags, bit combination of sensor_search_flags_t
 * @param matchs, if not NULL, list of <sensor_sample_t*> filled with results,
 *        to be freed with slist_free(*matchs, NULL).
 * @return the first element matching or NULL if not found.
 */
sensor_sample_t * sensor_watch_find(
                        sensor_ctx_t *          sctx,
                        const char *            pattern,
                        unsigned int            flags,
                        slist_t **              matchs);

/**
 * Visit a set of watched sensors.
 * @param sctx the sensor context
 * @param pattern fnmatch(3) pattern with format '<family>/<label>'.
 * @param flags, bit combination of sensor_search_flags_t
 * @param visit, the function called on each matching sensor.
 * @param user_data, user data given to visit()
 * @return the first element matching or NULL if not found.
 */
sensor_status_t sensor_watch_visit(
                        sensor_ctx_t *          sctx,
                        const char *            pattern,
                        unsigned int            flags,
                        sensor_watch_visitfun_t visit,
                        void *                  user_data);

/** return watch list.
 * User has to bound call between [sensor_lock(),sensor_unlock()] if needed.
 * @return list of watched sensors (slist_t of sensor_sample_t *) */
const slist_t * sensor_watch_list_get(sensor_ctx_t *sctx);

/** Clean the list of watchs
 * @param sctx the sensor context */
void            sensor_watch_free(sensor_ctx_t *sctx);

/** get update interval (milli-seconds) of given sensor
 * @notes: unlocked call, should be done under sensor lock */
unsigned long   sensor_watch_timerms(sensor_sample_t * sample);

/** Get the Greatest Common Divisor of watchs intervals
 * @param p_precision the pointer to divisor of values before applying pgcd.
 *        *p_precision is updated before return, to be used for a next call
 *        if NULL, default values are used: precision = 1, min_precision=1
 * @return rounded pgcd or 0 on error or if timer1==timer2==0 */
unsigned long   sensor_watch_pgcd(sensor_ctx_t *sctx,
                                  double * p_precision, double min_precision);

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
const slist_t * sensor_watch_load(const char * path);


/* ************************************************************************
 * SENSOR_UPDATES : handle and check sensors updates
 * ************************************************************************ */

/** get the current time.
 * this is not mandatory to use this time, user can have its own way to get
 * current time, it important thing is to have realistic relative time
 * between calls */
sensor_status_t sensor_now(struct timeval * now);

/**
 * Update a given sensor, according to its update interval.
 * @param sensor the sensor to update: undefined behavior if NULL.
 * @param now a struct timeval pointer to indicate current time,
 *        NULL to force the update.
 * @return SENSOR_ERROR or SENSOR_NOT_SUPPORTED on error
 *         SENSOR_UPDATED   if updated
 *         SENSOR_UNCHANGED if not updated
 *         SENSOR_WAIT_TIMER if watch->update_interval not reached
 *         SENSOR_RELOAD_FAMILY if family has been reloaded (descs and watchs changed)
 * @notes: unlocked call, should be called at least under a read lock
 *         (sensor_lock(sctx, SENSOR_LOCK_READ)).
 * @warning: It is MANDATORY to check RELOAD_FAMILY return value AND if got,
 * STOP looping and call again sensor_watch_list_get() or sensor_list_get() !!
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


/* ************************************************************************
 * SENSOR_PLUGIN : internal helpers for families/plugins
 * ************************************************************************ */

/** register a new sensor family: called from plugins */
sensor_status_t sensor_family_register(
                    sensor_ctx_t *                  sctx,
                    const sensor_family_info_t *    fam_info);

/** loading family requests libvsensors to create a temporary family desc list
 * When family will be loaded its 'update()' function will
 * return SENSOR_RELOAD_FAMILY */
slist_t *       sensor_family_loading_list(sensor_family_t * family);

/* ************************************************************************
 * SENSOR_PROPERTY : internal helpers for families/plugins
 * ************************************************************************ */

#define SENSOR_PROPERTY_VALID(_property)                                      \
    ((_property) != NULL && ((_property)->name != NULL                        \
                             || (_property)->value.type != SENSOR_VALUE_NULL))
sensor_property_t * sensor_property_create();
sensor_property_t * sensor_properties_create(unsigned int count);
void                sensor_property_free(sensor_property_t * property);
void                sensor_properties_free(sensor_property_t * properties);
sensor_status_t     sensor_property_init(sensor_property_t * property, const char * name);


/* ************************************************************************
 * SENSOR_VALUE: see sensor_value.h
 * ************************************************************************ */

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

