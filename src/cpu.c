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
 * cpu family for Generic Sensor Management Library.
 */
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>

#include "vlib/util.h"

#include "cpu.h"
#include "cpu_private.h"

/* ************************************************************************ */
/* use sysconf instead of deprecated CLK_TCK */
static unsigned long s_clk_tck = 0;

unsigned long cpu_clktck() {
    if (s_clk_tck > 0)
        return s_clk_tck;

    long clk_tck = sysconf(_SC_CLK_TCK);
    if (clk_tck <= 0) {
        #ifdef CLK_TCK
        s_clk_tck = CLK_TCK;
        #elif defined(CLOCKS_PER_SEC)
        s_clk_tck = CLOCKS_PER_SEC;
        #else
        #warning "NO CLK_TCK or CLOCKS_PER_SEC"
        s_clk_tck = 100;
        #endif
    } else
        s_clk_tck = clk_tck;
    return s_clk_tck;
}

/* ************************************************************************ */
/** family-specific free */
static sensor_status_t family_free(sensor_family_t *family) {
    if (family->priv != NULL) {
        cpu_priv_t *priv = (cpu_priv_t *) family->priv;
        if (priv->cpu_data.ticks != NULL)
            free(priv->cpu_data.ticks);
        if (priv->sensors_desc != NULL) {
            sensor_desc_t * desc;
            for (desc = priv->sensors_desc; desc->label != NULL; ++desc)
                free(desc->label);
            free(priv->sensors_desc);
        }
        sysdep_cpu_destroy(family);
        family->priv = NULL;
        free(priv);
    }
    return SENSOR_SUCCESS;
}

/* ************************************************************************ */
static sensor_status_t init_one_desc(
                            sensor_family_t *       family,
                            sensor_desc_t *         desc,
                            sensor_value_type_t     type,
                            void *                  key,
                            const char *            fmt_label,
                            ...) __attribute__((format(printf, 5, 6)));

static sensor_status_t init_one_desc(
                            sensor_family_t *       family,
                            sensor_desc_t *         desc,
                            sensor_value_type_t     type,
                            void *                  key,
                            const char *            fmt_label,
                            ...) {
    va_list valist;

    va_start(valist, fmt_label);

    vasprintf(&desc->label, fmt_label, valist);

    if (sysdep_cpu_support(family, desc->label) != SENSOR_SUCCESS) {
        if (desc->label != NULL)
            free(desc->label);
        va_end(valist);
        return SENSOR_ERROR;
    }

    desc->type = type;
    desc->family = family;
    desc->key = key;

    va_end(valist);

    return SENSOR_SUCCESS;
}

/* ************************************************************************ */
/** family private data creation, including the sensor_desc_t data */
static sensor_status_t init_private_data(sensor_family_t *family) {
    cpu_priv_t *    priv = (cpu_priv_t *) family->priv;
    unsigned        nb_cpus;
    unsigned        i;
    const unsigned  nb_desc_per_cpu = 7;
    sensor_desc_t * desc;

    priv->cpu_data.nb_cpus = nb_cpus = sysdep_cpu_nb(family);

    if ((priv->cpu_data.ticks = calloc(nb_cpus + 1, sizeof(cpu_tick_t))) == NULL) {
        LOG_ERROR(family->log, "%s/%s(): cannot allocate cpu_ticks", __FILE__, __func__);
        return SENSOR_ERROR;
    }

    if ((priv->sensors_desc
            = calloc(nb_desc_per_cpu * (nb_cpus + 1) + 1,
                     sizeof(*priv->sensors_desc))) == NULL) {
        return SENSOR_ERROR;
    }

    desc = priv->sensors_desc;
    for (i = 0; i <= priv->cpu_data.nb_cpus; i++) {
        char cpu_name[10];

        if (i == 0)
            str0cpy(cpu_name, "s", sizeof(cpu_name) / sizeof(*cpu_name));
        else
            snprintf(cpu_name, sizeof(cpu_name) / sizeof(*cpu_name), "%d", i);

        if (init_one_desc(
                    family, desc,
                    SENSOR_VALUE_ULONG,
                    &priv->cpu_data.ticks[i].sys,
                    "cpu%s sys usage", cpu_name) == SENSOR_SUCCESS)
            ++desc;

        if (init_one_desc(
                    family, desc,
                    SENSOR_VALUE_ULONG,
                    &priv->cpu_data.ticks[i].user,
                    "cpu%s user usage", cpu_name) == SENSOR_SUCCESS)
            ++desc;

        if (init_one_desc(
                    family, desc,
                    SENSOR_VALUE_ULONG,
                    &priv->cpu_data.ticks[i].activity,
                    "cpu%s activity usage", cpu_name) == SENSOR_SUCCESS)
            ++desc;

        if (init_one_desc(
                    family, desc,
                    SENSOR_VALUE_ULONG,
                    &priv->cpu_data.ticks[i].total,
                    "cpu%s total usage", cpu_name) == SENSOR_SUCCESS)
            ++desc;

        if (init_one_desc(
                    family, desc,
                    SENSOR_VALUE_UCHAR,
                    &priv->cpu_data.ticks[i].sys_percent,
                    "cpu%s sys usage %%", cpu_name) == SENSOR_SUCCESS)
            ++desc;

        if (init_one_desc(
                    family, desc,
                    SENSOR_VALUE_UCHAR,
                    &priv->cpu_data.ticks[i].user_percent,
                    "cpu%s user usage %%", cpu_name) == SENSOR_SUCCESS)
            ++desc;

        if (init_one_desc(
                    family, desc,
                    SENSOR_VALUE_UCHAR,
                    &priv->cpu_data.ticks[i].activity_percent,
                    "cpu%s total usage %%", cpu_name) == SENSOR_SUCCESS)
            ++desc;
    }
    desc->label = NULL;
    desc->key = NULL;
    desc->family = NULL;

    return SENSOR_SUCCESS;
}

/* ************************************************************************ */
/** family-specific init */
static sensor_status_t family_init(sensor_family_t *family) {
    // Sanity checks done before in sensor_init()
    if (family->priv != NULL) {
        LOG_ERROR(family->log, "error: %s data already initialized", family->info->name);
        return SENSOR_ERROR;
    }
    if (sysdep_cpu_support(family, NULL) != SENSOR_SUCCESS) {
        return SENSOR_NOT_SUPPORTED;
    }
    if ((family->priv = calloc(1, sizeof(cpu_priv_t))) == NULL) {
        LOG_ERROR(family->log, "cannot allocate private %s data", family->info->name);
        family_free(family);
        return SENSOR_ERROR;
    }
    if (init_private_data(family) != SENSOR_SUCCESS) {
        LOG_ERROR(family->log, "cannot initialize private %s data", family->info->name);
        family_free(family);
        return SENSOR_ERROR;
    }
    return SENSOR_SUCCESS;
}

/* ************************************************************************ */
/** family-specific list */
static slist_t * family_list(sensor_family_t *family) {
    cpu_priv_t *    priv = (cpu_priv_t *) family->priv;
    slist_t *       list = NULL;

    for (unsigned int i_desc = 0; priv->sensors_desc[i_desc].label; i_desc++) {
        list = slist_prepend(list, &priv->sensors_desc[i_desc]);
    }
    return list;
}

/* ************************************************************************ */
/** called by sysdeps to store cpu ticks and compute percents or global values */
int cpu_store_ticks(sensor_family_t * family,
                    int cpu_idx, unsigned long sys, unsigned long user,
                    unsigned long activity, unsigned long total,
                    struct timeval * elapsed) {

    cpu_priv_t *    priv = family != NULL ? (cpu_priv_t *) family->priv : NULL;
    cpu_tick_t *    ticks;

    if (priv == NULL || cpu_idx < 0 || cpu_idx > priv->cpu_data.nb_cpus) {
        return -1;
    }

    /* compute global (all cpus) values */
    if (cpu_idx == 0 && (   sys == CPU_COMPUTE_GLOBAL || user == CPU_COMPUTE_GLOBAL
                         || activity == CPU_COMPUTE_GLOBAL || total == CPU_COMPUTE_GLOBAL)) {
        unsigned long   global_total = 0;
        unsigned long   global_activity = 0;
        unsigned long   global_user = 0;
        unsigned long   global_sys = 0;

        for (int i_cpu = priv->cpu_data.nb_cpus; i_cpu > 0; --i_cpu) {
            ticks = &(priv->cpu_data.ticks[i_cpu]);

            /* accumulate global cpu data */
            global_total    += ticks->total;
            global_activity += ticks->activity;
            global_sys      += ticks->sys;
            global_user     += ticks->user;
        }

        /* set global cpu data */
        total       = global_total      / priv->cpu_data.nb_cpus;
        activity    = global_activity   / priv->cpu_data.nb_cpus;
        user        = global_user       / priv->cpu_data.nb_cpus;
        sys         = global_sys        / priv->cpu_data.nb_cpus;
    }

    ticks = &(priv->cpu_data.ticks[cpu_idx]);

    if (elapsed != NULL && (elapsed->tv_sec > 0 || elapsed->tv_usec >= 1000)) {
        unsigned long   clk_tck = cpu_clktck();

        unsigned long   activity_percent
                            = (10 * 100 * (activity - ticks->activity) + 1) / clk_tck;

        unsigned long   user_percent
                            = (10 * 100 * (user - ticks->user) + 1) / clk_tck;

        unsigned long   sys_percent
                            = (10 * 100 * (sys - ticks->sys) + 1) / clk_tck;

        ticks->activity_percent
                    = (100 * activity_percent)
                    / (elapsed->tv_sec * 1000 + elapsed->tv_usec / 1000);

        ticks->user_percent
                    = (100 * user_percent)
                    / (elapsed->tv_sec * 1000 + elapsed->tv_usec / 1000);

        ticks->sys_percent
                    = (100 * sys_percent)
                    / (elapsed->tv_sec * 1000 + elapsed->tv_usec / 1000);
    }

    /* finally stores the tick values */
    ticks->activity    = activity;
    ticks->user        = user;
    ticks->sys         = sys;
    ticks->total       = total;

    return 0;
}

/* ************************************************************************ */
/** family-specific update */
static sensor_status_t family_update(sensor_sample_t *sensor, const struct timeval * now) {
    /* Sanity checks are done in sensor_update_get() */
    cpu_priv_t * fpriv = (cpu_priv_t *) sensor->desc->family->priv;

    if (fpriv == NULL) {
       return SENSOR_ERROR;
    }

    /* Because all cpu datas are retrieved at once, don't repeat it for each sensor */
    int bfirst = fpriv->last_update_time.tv_sec == 0
                 && fpriv->last_update_time.tv_usec < 1000;
    struct timeval elapsed;
    struct timeval limit = {
        .tv_sec     = sensor->watch.update_interval_ms / 1000,
        .tv_usec    = (sensor->watch.update_interval_ms % 1000) * 1000,
    };

    timersub(now, &fpriv->last_update_time, &elapsed);
    if (bfirst || timercmp(&elapsed, &limit, >=)) {
        if (sysdep_cpu_get(sensor->desc->family, bfirst ? NULL : &elapsed) == SENSOR_SUCCESS) {
            /* nothing to do */
        }
        fpriv->last_update_time = *now;
    }

    /* Always update the sensor Value; */
    return sensor_value_fromraw(sensor->desc->key, &sensor->value);
}

/* ************************************************************************ */
const sensor_family_info_t g_sensor_family_cpu = {
    .name = "cpu",
    .init = family_init,
    .free = family_free,
    .update = family_update,
    .list = family_list,
};

