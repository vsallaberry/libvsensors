/*
 * Copyright (C) 2020 Vincent Sallaberry
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
 * smc default system specific routines for Generic Sensor Management Library.
 */
#include "vlib/log.h"
#include "libvsensors/sensor.h"

#include "smc.h"

sensor_status_t sysdep_smc_support(sensor_family_t * family, const char * label) {
    (void) label;
    (void) family;
    return SENSOR_ERROR;
}

int                 sysdep_smc_open(void ** psmc_handle, log_t *log, unsigned int * maxsize) {
    (void)psmc_handle;
    (void)log;
    (void)maxsize;
    return SENSOR_ERROR;
}

int                 sysdep_smc_close(void ** psmc_handle, log_t *log) {
    (void)psmc_handle;
    (void)log;
    return SENSOR_ERROR;
}

int                 sysdep_smc_readkey(
                        uint32_t        key,
                        uint32_t *      value_type,
                        void *          value_bytes,
                        void *          smc_handle,
                        log_t *         log) {
    (void)key;
    (void)value_type;
    (void)value_bytes;
    (void)smc_handle;
    (void)log;
    return SENSOR_ERROR;
}

int                 sysdep_smc_readindex(
                        uint32_t        index,
                        uint32_t *      value_type,
                        void *          value_bytes,
                        void *          smc_handle,
                        log_t *         log) {
    (void)index;
    (void)value_type;
    (void)value_bytes;
    (void)smc_handle;
    (void)log;
    return SENSOR_ERROR;
}

