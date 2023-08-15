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
 * file monitoring - Generic Sensor Management Library.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "file_private.h"

/****************************************************************************/
static void             file_watch_free(void * vfile);
static sensor_status_t  file_watch_del(
                            sensor_family_t * family,
                            const char * path);
static sensor_status_t  file_watch_add(
                            sensor_family_t * family,
                            const char * path,
                            unsigned int flags);

/****************************************************************************/
/** family-specific free */
static sensor_status_t family_free(sensor_family_t *family) {
    if (family->priv != NULL) {
        file_priv_t * priv = (file_priv_t *) family->priv;

        slist_free(priv->files, file_watch_free);

        sysdep_file_destroy(family);

        family->priv = NULL;
        free(priv);
    }
    return SENSOR_SUCCESS;
}

/** family private data creation, including the sensor_desc_t data */
static sensor_status_t init_private_data(sensor_family_t *family) {
    file_priv_t * priv = (file_priv_t *) family->priv;;
    (void)priv;

    if (sysdep_file_init(family) != SENSOR_SUCCESS) {
        return SENSOR_ERROR;
    }

    return SENSOR_SUCCESS;
}

/** family-specific init */
static sensor_status_t family_init(sensor_family_t *family) {
    // Sanity checks done before in sensor_init()
    if (family->priv != NULL) {
        LOG_ERROR(family->log, "error: %s data already initialized", family->info->name);
        return SENSOR_ERROR;
    }
    if (sysdep_file_support(family, NULL) != SENSOR_SUCCESS) {
        return SENSOR_NOT_SUPPORTED;
    }
    if ((family->priv = calloc(1, sizeof(file_priv_t))) == NULL) {
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

/** family-specific list */
static slist_t * family_list(sensor_family_t *family) {
    file_priv_t *    priv = (file_priv_t *) family->priv;
    (void)priv;
    return NULL;
}

/** family-specific update */
static sensor_status_t family_update(sensor_sample_t *sensor, const struct timeval * now) {
    /* Sanity checks are done in sensor_init and sensor_update_check() */
    file_priv_t * priv = (file_priv_t *) sensor->desc->family->priv;
    (void)priv;
    (void)now;
    return SENSOR_ERROR;
}

/** family-specific notify */
static sensor_status_t sensor_family_notify(
                    unsigned int event, struct sensor_family_s * family,
                    struct sensor_sample_s * sample, sensor_watch_ev_data_t * ev_data) {
    sensor_status_t ret = SENSOR_SUCCESS;
    (void)sample;
    (void)ev_data;

    if ((event & (SWE_WATCH_DELETING | SWE_WATCH_REPLACED)) != 0) {
        // TODO
        ret = file_watch_del(family, NULL); // TODO
    }

    if ((event & (SWE_WATCH_ADDED | SWE_WATCH_REPLACED)) != 0) {
        // TODO
        ret = file_watch_add(family, NULL, 0); // TODO
    }

    return ret;
}

// ***************************************************************************
const sensor_family_info_t g_sensor_family_file = {
    .name = "file",
    .init = family_init,
    .free = family_free,
    .update = family_update,
    .list = family_list,
    .notify = sensor_family_notify,
    .write = NULL,
    .free_desc = NULL
};

// ***************************************************************************
static void file_watch_free(void * vfile) {
    fileinfo_t * file = (fileinfo_t *) vfile;

    if (file) {
        sysdep_file_watch_free(file);
        if (file->name)
            free(file->name);
        free(file);
    }
}

// ***************************************************************************
static sensor_status_t file_watch_add(sensor_family_t * family, const char * path, unsigned int flags) {
    file_priv_t *   priv = (family->priv);
    fileinfo_t *    info;
    (void)flags;

    if ((info = calloc(1, sizeof(*info))) == NULL) {
        return SENSOR_ERROR;
    }
    info->sysdep = NULL;
    info->flags = flags;
    info->name = NULL;

    if (path == NULL || (info->name = strdup(path)) == NULL) {
        file_watch_free(info);
        return SENSOR_ERROR;
    }
    if (sysdep_file_watch_add(family, info) != SENSOR_SUCCESS) {
        LOG_WARN(family->log, "%s(%s): %s", __func__, info->name, strerror(errno));
        file_watch_free(info);
        return SENSOR_ERROR;
    }
    if ((priv->files = slist_prepend(priv->files, info)) == NULL) {
        file_watch_free(info);
        return SENSOR_ERROR;
    }
    return SENSOR_SUCCESS;
}

// ***************************************************************************
static sensor_status_t  file_watch_del(
                            sensor_family_t * family,
                            const char * path) {
    file_priv_t *   priv = (family->priv);
    fileinfo_t      ref_info;

    ref_info.name = (char *) path;
    errno = 0;
    priv->files = slist_remove(priv->files, &ref_info, NULL, file_watch_free); // TODO
    if (errno != 0) {
        return SENSOR_ERROR;
    }

    return SENSOR_SUCCESS;
}

