/*
 * Copyright (C) 2020,2023 Vincent Sallaberry
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

int sysdep_smc_open(void ** psmc_handle, log_t *log,
                    unsigned int * bufsize, unsigned int * value_offset) {
    (void)psmc_handle;
    (void)log;
    (void)bufsize;
    (void)value_offset;
    return SENSOR_ERROR;
}

int                 sysdep_smc_close(void * smc_handle, log_t *log) {
    (void)smc_handle;
    (void)log;
    return SENSOR_ERROR;
}

int                 sysdep_smc_readkey(
                        uint32_t        key,
                        uint32_t *      value_type,
                        void **         key_info,
                        void *          output_buffer,
                        void *          smc_handle,
                        log_t *         log) {
    (void)key;
    (void)value_type;
    (void)key_info;
    (void)output_buffer;
    (void)smc_handle;
    (void)log;
    return SENSOR_ERROR;
}

int                 sysdep_smc_readindex(
                        uint32_t        index,
                        uint32_t *      value_key,
                        uint32_t *      value_type,
                        void **         key_info,
                        void *          output_buffer,
                        void *          smc_handle,
                        log_t *         log) {
    (void)index;
    (void)value_type;
    (void)value_key;
    (void)key_info;
    (void)output_buffer;
    (void)smc_handle;
    (void)log;
    return SENSOR_ERROR;
}

int             sysdep_smc_writekey(
                    uint32_t        key,
                    uint32_t *      value_type,
                    void **         key_info,
                    void *          input_buffer,
                    uint32_t        input_size,
                    const sensor_value_t* value,
                    void *          smc_handle,
                    log_t *         log) {
    (void)key;
    (void)value_type;
    (void)key_info;
    (void)input_buffer;
    (void)input_size;
    (void)value;
    (void)smc_handle;
    (void)log;
    return SENSOR_ERROR;
}
