/*
 * Copyright (C) 2020,2023 Vincent Sallaberry
 * libvsensors <https://github.com/vsallaberry/libvsensors>
 *
 * Credits to Bill Wilson, Ben Hines and other gkrellm developers
 * (gkrellm, GPLv3, https://git.srcbox.net/gkrellm) for some hints
 * about the way to retrieve some os-specific system informations.
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
 * memory-linux for Generic Sensor Management Library.
 */
#include <unistd.h>

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <fnmatch.h>

#include "vlib/util.h"

#include "memory_private.h"

/* ************************************************************************ */

#ifndef MEM_MEMINFO_FILE
#define MEM_MEMINFO_FILE   "/proc/meminfo"
#endif

typedef struct {
    FILE *  stat;
    char *  stat_line;
    size_t  stat_linesz;
} mem_linux_t;


/* ************************************************************************ */
sensor_status_t sysdep_memory_support(sensor_family_t * family, const char * label) {
    (void) family;
    (void) label;
    /* if (label != NULL && (fnmatch("*SomethingNotSupported*", label, FNM_CASEFOLD) == 0)) {
     *     return SENSOR_ERROR;
     * } */
    return SENSOR_SUCCESS;
}

/* ************************************************************************ */
sensor_status_t sysdep_memory_init(sensor_family_t * family) {
    memory_priv_t * priv = (family->priv);
    mem_linux_t *   sysdep;
    //char *          line = NULL;
    //size_t          lineallocsz = 0;
    //ssize_t         linesz;

    if (priv->sysdep == NULL) {
        priv->sysdep = calloc(1, sizeof(mem_linux_t));
        if (priv->sysdep == NULL) {
            LOG_ERROR(family->log, "error, cannot malloc %s sysdep data", family->info->name);
            errno=ENOMEM;
            return SENSOR_ERROR;
        }

        sysdep = priv->sysdep;
        sysdep->stat_line = NULL;
        sysdep->stat_linesz = 0;
        if ((sysdep->stat = fopen(MEM_MEMINFO_FILE, "r")) == NULL) {
            LOG_ERROR(family->log, "error while openning %s", MEM_MEMINFO_FILE);
            errno=ENOENT;
            return SENSOR_ERROR;
        }
    } else {
        sysdep = priv->sysdep;
    }

    fseek(sysdep->stat, 0, SEEK_SET);
    /*
    while ((linesz = getline(&line, &lineallocsz, sysdep->stat)) >= 0) {
        LOG_DEBUG(family->log, "%s LINE (sz:%zu) %s", MEM_MEMINFO_FILE, linesz, line);
        if (!strncmp(line, "cpu", 3) && (line[3] != ' ' && line[3] != '\t'))
            ++n_cpus;
    }

    if (line != NULL)
        free(line);
    */

    return SENSOR_SUCCESS;
}

/* ************************************************************************ */
void            sysdep_memory_destroy(sensor_family_t * family) {
    memory_priv_t * priv = (memory_priv_t *) family->priv;

    if (priv != NULL && priv->sysdep != NULL) {
        mem_linux_t * sysdep = (mem_linux_t *) priv->sysdep;

        if (sysdep->stat != NULL) {
            fclose(sysdep->stat);
            sysdep->stat = NULL;
        }
        if (sysdep->stat_line != NULL)
            free(sysdep->stat_line);
        sysdep->stat_line = NULL;
        priv->sysdep = NULL;
        free(sysdep);
    }
}

/* ************************************************************************ */
sensor_status_t sysdep_memory_get(sensor_family_t * family, memory_data_t * data) {
    memory_priv_t * priv = (family->priv);
    mem_linux_t *   sysdep = priv->sysdep;
    ssize_t         linesz;

    if (priv->sysdep == NULL) {
        LOG_ERROR(family->log, "error, bad %s sysdep data", family->info->name);
        errno = EFAULT;
        return SENSOR_ERROR;
    }

    if (sysdep->stat == NULL || fseek(sysdep->stat, 0, SEEK_SET) != 0) {
        if (sysdep->stat != NULL)
            fclose(sysdep->stat);
        if ((sysdep->stat = fopen(MEM_MEMINFO_FILE, "r")) == NULL) {
            return SENSOR_ERROR;
        }
    }

    data->wired = 0; //((natural_t)vmStats.wire_count) * (unsigned long)((natural_t)vm_page_size);

    while ((linesz = getline(&sysdep->stat_line, &sysdep->stat_linesz, sysdep->stat)) > 0) {
        char * line = sysdep->stat_line;
        const char * token, * value, * next = line;
        ssize_t tok_len, val_len;
        size_t maxlen;

        if (sysdep->stat_line == NULL)
            break ;

        if (sysdep->stat_line[linesz - 1] == '\n')
            sysdep->stat_line[--linesz] = 0;
            
        /* /proc/meminfo format: {
         *  <keyword>:      <value> <unit/info> // For historical reason the unit kB is used but data is actually KiB.
         *  TotalMemory:    <number> kB
         *  ...
         */
        LOG_SCREAM(family->log, "%s LINE (sz:%zu) %s", MEM_MEMINFO_FILE,
                   linesz, line);

        if (linesz <= 1)
            continue ;
            
        maxlen = linesz;
        /* get value name */
        while (*next == ' ' || *next == '\t') {
            ++next;
            --maxlen;
        }
        tok_len = strtok_ro_r(&token, ":", &next, &maxlen, 0);
        if (tok_len == 0 || *next == 0)
            continue ;

        /* get value value */
        while((val_len = strtok_ro_r(&value, " ", &next, &maxlen, 0)) == 0 && *next)
            ; /* nothing */
        if (val_len == 0)
            continue ;

        if (strncasecmp(token, "MemTotal", tok_len) == 0) {
            data->total = strtoul(value, NULL, 10) * 1024;
        } else if (strncasecmp(token, "MemFree", tok_len) == 0) {
            data->free = strtoul(value, NULL, 10) * 1024;
        } else if (strncasecmp(token, "SwapTotal", tok_len) == 0) {
            data->total_swap = strtoul(value, NULL, 10) * 1024;
        } else if (strncasecmp(token, "SwapFree", tok_len) == 0) {
            data->free_swap = strtoul(value, NULL, 10) * 1024;
        } else if (strncasecmp(token, "Active:", tok_len + 1) == 0) {
            data->active = strtoul(value, NULL, 10) * 1024;
        } else if (strncasecmp(token, "Inactive:", tok_len + 1) == 0) {
            data->inactive = strtoul(value, NULL, 10) * 1024;
        } else if (strncasecmp(token, "Unevictable", tok_len) == 0) {
            data->wired += strtoul(value, NULL, 10) * 1024;
        } else if (strncasecmp(token, "Mlocked", tok_len) == 0) {
            data->wired += strtoul(value, NULL, 10) * 1024;
        }
    }

    //data->free = 0; //((natural_t)vmStats.free_count) * (unsigned long)((natural_t)vm_page_size);

    data->used = data->total - data->free; //data->active + data->wired;
    data->used_swap = data->total_swap - data->free_swap;
    //data->total = data->active + data->inactive + data->free + data->wired;

    if (data->total == 0)
        data->used_percent = 100;
    else
        data->used_percent = ((data->used/1024.0) / (data->total/1024.0)) * 100;

    if (data->total_swap == 0)
        data->used_swap_percent = 100;
    else
        data->used_swap_percent = ((data->used_swap/1024.0) / (data->total_swap/1024.0)) * 100;

    return SENSOR_SUCCESS;
}

