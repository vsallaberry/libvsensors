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
 */
#ifndef LIBVSENSORS_SENSOR_PRIVATE_H
# define LIBVSENSORS_SENSOR_PRIVATE_H

# include <stdlib.h>

# include "vlib/util.h"

# include "libvsensors/sensor.h"

/* ************************************************************************ */
# if defined (SENSOR_ENABLE_LIKELY_SENSOR) || defined(SENSOR_ENABLE_LIKELY_ALL)
#  define SENSOR_LIKELY(cond)    VLIB_LIKELY(cond)
#  define SENSOR_UNLIKELY(cond)  VLIB_UNLIKELY(cond)
# else
#  define SENSOR_LIKELY(cond)    (cond)
#  define SENSOR_UNLIKELY(cond)  (cond)
# endif

/* ************************************************************************ */
# ifdef __cplusplus
extern "C" {
# endif

void sensor_value_info_init();

# ifdef __cplusplus
}
# endif
/* ************************************************************************ */

#endif // ! #ifdef LIBVSENSORS_SENSOR_PRIVATE_H
