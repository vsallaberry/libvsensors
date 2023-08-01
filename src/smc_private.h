/*
 * Copyright (C) 2023 Vincent Sallaberry
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
#ifndef SENSOR_SMC_PRIVATE_H
#define SENSOR_SMC_PRIVATE_H

#include "libvsensors/sensor.h"

#define SMC_TYPE(str)       ((uint32_t)(((unsigned) ((str)[0])) << 24 | ((unsigned) ((str)[1])) << 16 \
                                        | ((unsigned) ((str)[2])) << 8 | (unsigned) ((str)[3])))
                                        
#ifdef __cplusplus
extern "C" {
#endif

// ************************************************************************
// Functions declaration
unsigned long       _str32toul(const char * int32, unsigned int size, int base);
unsigned int        _ultostr32(char * str32, unsigned int maxsize,
                               unsigned long ul, unsigned int size);

#ifdef __cplusplus
}
#endif

#endif // !ifdef SENSOR_SMC_PRIVATE_H

