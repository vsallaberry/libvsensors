/*
 * Copyright (C) 2020 Vincent Sallaberry
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
 * Generic Sensor Management Library.
 */
#include <unistd.h>

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#include "vlib/util.h"

#include "cpu_private.h"

/* ************************************************************************ */

#ifndef CPU_PROC_FILE
#define CPU_PROC_FILE   "/proc/stat"
#endif

typedef struct {
    FILE *  stat;
    char *  stat_line;
    size_t  stat_linesz;
} cpu_linux_t;


/* ************************************************************************ */
sensor_status_t sysdep_cpu_support(sensor_family_t * family, const char * label) {
    (void) label;
    (void) family;
    return SENSOR_SUCCESS;
}

/* ************************************************************************ */
unsigned int    sysdep_cpu_nb(sensor_family_t * family) {
    cpu_priv_t *    priv = (family->priv);
    cpu_linux_t *   sysdep;
    unsigned int    n_cpus;
    char            *line = NULL;
    size_t          lineallocsz = 0;
    ssize_t         linesz;

    if (priv->sysdep == NULL) {
        priv->sysdep = calloc(1, sizeof(cpu_linux_t));
        if (priv->sysdep == NULL) {
            LOG_ERROR(family->log, "error, cannot malloc %s sysdep data", family->info->name);
            errno=ENOMEM;
            return 0;
        }

        sysdep = priv->sysdep;
        sysdep->stat_line = NULL;
        sysdep->stat_linesz = 0;
        if ((sysdep->stat = fopen(CPU_PROC_FILE, "r")) == NULL) {
            LOG_ERROR(family->log, "error while openning %s", CPU_PROC_FILE);
            errno=ENOENT;
            return 0;
        }
    } else {
        sysdep = priv->sysdep;
    }

    fseek(sysdep->stat, 0, SEEK_SET);
    n_cpus = 0;
    while ((linesz = getline(&line, &lineallocsz, sysdep->stat)) >= 0) {
        LOG_DEBUG(family->log, "%s LINE (sz:%zu) %s", CPU_PROC_FILE, linesz, line);
        if (!strncmp(line, "cpu", 3) && (line[3] != ' ' && line[3] != '\t'))
            ++n_cpus;
    }

    if (line != NULL)
        free(line);
    return n_cpus;
}

/* ************************************************************************ */
void            sysdep_cpu_destroy(sensor_family_t * family) {
    cpu_priv_t *        priv = (cpu_priv_t *) family->priv;

    if (priv != NULL && priv->sysdep != NULL) {
        cpu_linux_t * sysdep = (cpu_linux_t *) priv->sysdep;

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
sensor_status_t sysdep_cpu_get(sensor_family_t * family, struct timeval *elapsed) {
    cpu_priv_t *    priv = (family->priv);
    cpu_linux_t *   sysdep = priv->sysdep;
    ssize_t         linesz;
    unsigned long   n;

    if (priv->sysdep == NULL || sysdep->stat == NULL) {
        LOG_ERROR(family->log, "error, cannot malloc %s sysdep data", family->info->name);
        errno = EFAULT;
        return SENSOR_ERROR;
    }

    fseek(sysdep->stat, 0, SEEK_SET);

    while ((linesz = getline(&sysdep->stat_line, &sysdep->stat_linesz, sysdep->stat)) > 0) {
        char * line = sysdep->stat_line;

        if (line == NULL)
            break ;

        /* /proc/stat format: {
         *  cpu    total_user  total_nice  total_sys   total_idle
         *  cpu0   cpu0_user   cpu0_nice   cpu0_sys    cpu0_idle
         *  ...}
         * ticks for cpu are jiffies * smp_num_cpus
         * ticks for cpu[i] are jiffies (1/CLK_TCK)
         */
        LOG_DEBUG(family->log, "%s LINE (sz:%zu) %s", CPU_PROC_FILE,
                  linesz, line);

        while (*line == ' ' || *line == '\t') {
            ++line;
            --linesz;
        }
        if (!strncmp(line, "cpu", 3)) {
            if (line[3] == ' ' || line[3] == '\t') {
                n = 0;
            } else {
                char * endp = NULL;
                n = strtoul(line + 3, &endp, 10) + 1;
            }

            if (n <= priv->cpu_data.nb_cpus) {
                const char * token, * next = line;
                ssize_t tok_len;
                size_t maxlen = linesz;
                unsigned int tok_idx = 0;
                unsigned long user = 0, sys = 0, activity = 0, total = 0;
                cpu_data_t * data = &priv->cpu_data;

                while ((tok_len = strtok_ro_r(&token, " \t", &next, &maxlen, 0)) >= 0 && *next) {
                    if (tok_len == 0) continue ;
                    if (tok_idx == 1) /* user */
                        user = strtoul(token, NULL, 0);
                    if (tok_idx == 2) /* nice */
                        user += strtoul(token, NULL, 0);
                    if (tok_idx == 3) /* sys */
                        sys = strtoul(token, NULL, 0);
                    if (tok_idx == 4) { /* idle */
                        activity =
                            + user
                            + sys;

                        total = strtoul(token, NULL, 0)
                            + activity;

                        if (n == 0) {
                            user /= data->nb_cpus;
                            sys /= data->nb_cpus;
                            activity /= data->nb_cpus;
                            total /= data->nb_cpus;
                        }

                        cpu_store_ticks(family, n, sys, user, activity, total, elapsed);

                        LOG_DEBUG(family->log,
                            "CPU%lu %u%% (usr:%u sys:%u)",
                            n,
                            data->ticks[n].activity_percent,
                            data->ticks[n].user_percent,
                            data->ticks[n].sys_percent);

                        break ;
                    }
                    ++tok_idx;
                }
            }
        }
    }
    return SENSOR_SUCCESS;
}

