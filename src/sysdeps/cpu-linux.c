/*
 * Copyright (C) 2018-2019 Vincent Sallaberry
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
 */
#include "cpu_private.h"

sensor_status_t cpu_get(sensor_family_t * family, struct timeval *elapsed) {
    priv_t * priv = (priv_t *) family->priv;
    (void)priv;
    (void)elapsed;
    (void)priv;
    LOG_ERROR(family->log, "%s/%s(): NOT IMPLEMENTED ON LINUX.", __FILE__, __func__);
    return SENSOR_ERROR;
}

