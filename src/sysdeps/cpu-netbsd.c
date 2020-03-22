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
 * netbsd cpu interface for Generic Sensor Management Library.
 */
#include <sys/sched.h>

/* USE sysdeps/cpu-freebsd.c (except sysdep_cpu_support()) */
#define __FreeBSD_version
#define sysdep_cpu_support sysdep_cpu_support_freebsd
#include "cpu-freebsd.c"
#undef sysdep_cpu_support
#undef __FreeBSD_version
/* ! USE sysdeps/cpu-freebsd.c */

#include <fnmatch.h>

sensor_status_t sysdep_cpu_support(sensor_family_t * family, const char * label) {
    (void) label;
    (void) family;
    if (label == NULL) {
        return SENSOR_SUCCESS;
    }
    if (fnmatch("*cpu[0-9]*", label, 0) == 0) {
        return SENSOR_ERROR;
    }
    return SENSOR_SUCCESS;
}


