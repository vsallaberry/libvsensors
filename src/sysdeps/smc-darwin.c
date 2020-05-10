/*
 * Copyright (C) 2017-2020 Vincent Sallaberry
 * libvsensors <https://github.com/vsallaberry/libvsensors>
 *
 * Portions Copyright (C) 2006 devnull
 * Portions Copyright (C) 2017 Hendrik Holtmann <https://github.com/hholtmann>
 * Portions Copyright (C) 2013 Michael Wilber
 *
 * Credits to devnull <https://github.com/hholtmann> and Michael Wilber
 * for the AppleSMC driver interface inspired from their smcFanControl (GPL 2):
 * <https://github.com/hholtmann/smcFanControl.git>
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
 * Apple SMC driver interface for Generic Sensor Management Library.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <IOKit/IOKitLib.h>

#ifdef HAVE_VERSION_H
# include "version.h"
#endif

/* ************************************************************************ */
#if defined(BUILD_VLIB) && BUILD_VLIB
# include <vlib/log.h>
# include <libvsensors/sensor.h>
#define SMC_SUCCESS                 SENSOR_SUCCESS
#define SMC_ERROR                   SENSOR_ERROR
#else
#define SMC_SUCCESS                 (0)
#define SMC_ERROR                   (-1)
struct log_s;
typedef struct log_s log_t;
typedef void sensor_family_t
# define LOG_LVL_DEBUG                  5
# define LOG_LVL_SCREAM                 6
# define INFO(...)                      (fprintf(stderr, __VA_ARGS__))
# ifdef _DEBUG
# define TRACE(...)                     INFO(__VA_ARGS__)
# else
# define TRACE(...)                     (0)
# endif
# define LOG_ERROR(log, ...)            (INFO(__VA_ARGS__))
# define LOG_WARN(log, ...)             (INFO(__VA_ARGS__))
# define LOG_INFO(log, ...)             (INFO(__VA_ARGS__))
# define LOG_VERBOSE(log, ...)          (TRACE(__VA_ARGS__))
# define LOG_DEBUG(log, ...)            (TRACE(__VA_ARGS__))
# define LOG_SCREAM(log, ...)           (TRACE(__VA_ARGS__))
# define LOG_DEBUG_BUF(log,buf,sz,...)  (TRACE(__VA_ARGS__))
# define LOG_BUFFER(lvl,log,buf,sz,...) (TRACE(__VA_ARGS__))
#endif

/* ************************************************************************ */
#define SMC_IOSERVICE_NAME          "AppleSMC"
#define SMC_IOSERVICE_KERNEL_INDEX  2

#define SMC_CMD_READ_BYTES          5
#define SMC_CMD_WRITE_BYTES         6
#define SMC_CMD_READ_INDEX          8
#define SMC_CMD_READ_KEYINFO        9
#define SMC_CMD_READ_PLIMIT         11
#define SMC_CMD_READ_VERS           12

/* ************************************************************************ */
typedef struct {
    char                    major;
    char                    minor;
    char                    build;
    char                    reserved[1];
    UInt16                  release;
} SMCKeyData_vers_t;

typedef struct {
    UInt16                  version;
    UInt16                  length;
    UInt32                  cpuPLimit;
    UInt32                  gpuPLimit;
    UInt32                  memPLimit;
} SMCKeyData_pLimitData_t;

typedef struct {
    UInt32                  dataSize;
    UInt32                  dataType;
    char                    dataAttributes;
} SMCKeyData_keyInfo_t;

typedef unsigned char       SMCBytes_t[32];

typedef struct {
    UInt32                  key;
    SMCKeyData_vers_t       vers;
    SMCKeyData_pLimitData_t pLimitData;
    SMCKeyData_keyInfo_t    keyInfo;
    char                    result;
    char                    status;
    char                    data8;
    UInt32                  data32;
    SMCBytes_t              bytes;
} SMCKeyData_t;


/* ************************************************************************ */
/* Cache the key_info to avoid multiple calls to smc_call()
 * Cache is not used when key_info is given with sysdep_smc_read{key,index}. */
#define SMC_KEYINFO_CACHE_SIZE 100
static struct {
    UInt32                  key;
    SMCKeyData_keyInfo_t    key_info;
} s_smc_keyinfo_cache[SMC_KEYINFO_CACHE_SIZE] = { { .key = 0, }, };

static int          s_keyinfo_cache_count   = 0;
static int          s_keyinfo_cache_index   = 0;

#define SMC_LOCK_TYPE 1
#if SMC_LOCK_TYPE == 1
# include <pthread.h>
# define SMC_LOCK(lock)      pthread_mutex_lock(&(lock))
# define SMC_UNLOCK(lock)    pthread_mutex_unlock(&(lock))
pthread_mutex_t     s_keyinfo_lock = PTHREAD_MUTEX_INITIALIZER;
#elif SMC_LOCK_TYPE == 2
#include <libkern/OSAtomic.h>
# define SMC_LOCK(lock)      OSSpinLockLock(&(lock))
# define SMC_UNLOCK(lock)    OSSpinLockUnlock(&(lock))
static OSSpinLock   s_keyinfo_lock = OS_SPINLOCK_INIT;
#else
# define SMC_LOCK(lock)
# define SMC_UNLOCK(lock)
#endif


/* ************************************************************************ */
int sysdep_smc_support(sensor_family_t * family, const char * label) {
    (void) label;
    (void) family;
    return SMC_SUCCESS;
}

/* ************************************************************************ */
int sysdep_smc_open(void ** psmc_handle, log_t * log,
                    unsigned int * bufsize, unsigned int * value_offset) {
    io_connect_t    io_connection;
    kern_return_t   result;
    io_iterator_t   iterator;
    io_object_t     device;
    /*mach_port_t     masterPort;*/

    if (bufsize == NULL || value_offset == NULL) {
        LOG_ERROR(log, "error: bufsize or value_offset pointers NULL !");
        return SMC_ERROR;
    }

    /* IOMasterPort(MACH_PORT_NULL, &masterPort); */
    CFMutableDictionaryRef matchingDictionary = IOServiceMatching(SMC_IOSERVICE_NAME);
    result = IOServiceGetMatchingServices(kIOMasterPortDefault, matchingDictionary, &iterator);
    if (result != kIOReturnSuccess) {
        LOG_ERROR(log, "Error: IOServiceGetMatchingServices(%s) = %08x",
                SMC_IOSERVICE_NAME, result);
        return SMC_ERROR;
    }

    device = IOIteratorNext(iterator);
    IOObjectRelease(iterator);
    if (device == 0) {
        LOG_ERROR(log, "Error: IOService %s not found!", SMC_IOSERVICE_NAME);
        return SMC_ERROR;
    }

    result = IOServiceOpen(device, mach_task_self(), 0, &io_connection);
    IOObjectRelease(device);
    if (result != kIOReturnSuccess) {
        LOG_ERROR(log, "Error: IOServiceOpen() = %08x", result);
        return SMC_ERROR;
    }

    *bufsize = sizeof(SMCKeyData_t);
    *value_offset = (unsigned int) ((unsigned long) &(((SMCKeyData_t *)0)->bytes));
    *psmc_handle = (void *)((unsigned long)io_connection);

    return SMC_SUCCESS;
}

/* ************************************************************************ */
int sysdep_smc_close(void * smc_handle, log_t * log) {
    io_connect_t    io_connection = (io_connect_t)((unsigned long)smc_handle);
    kern_return_t   result;

    result = IOServiceClose(io_connection);
    if (result != kIOReturnSuccess) {
        LOG_ERROR(log, "IOServiceClose() error");
        return SMC_ERROR;
    }
    return SMC_SUCCESS;
}

/* ************************************************************************ */
static inline kern_return_t sysdep_smc_call(
                                int                 index,
                                SMCKeyData_t *      input_data,
                                SMCKeyData_t *      output_data,
                                io_connect_t        io_connection,
                                log_t *             log)
{
    const size_t    input_size  = sizeof(*input_data);
    size_t          output_size = sizeof(*output_data);
    (void) log;

#   if MAC_OS_X_VERSION_10_5
    return IOConnectCallStructMethod(
                io_connection, index, input_data, input_size, output_data, &output_size);
#   else
    return IOConnectMethodStructureIStructureO(
                io_connection, index, input_size, &output_size, input_data, output_data);
#   endif
}

/* ************************************************************************ */
static int smc_get_keyinfo(
                uint32_t                    key,
                SMCKeyData_keyInfo_t *      key_info,
                io_connect_t                io_connection,
                log_t *                     log,
                int                         use_cache)
{
    SMCKeyData_t    input_data;
    SMCKeyData_t    output_data;
    int             i;
    kern_return_t   result;

    if (use_cache) {
        SMC_LOCK(s_keyinfo_lock);
        for (i = 0; i < s_keyinfo_cache_count; ++i) {
            if (s_smc_keyinfo_cache[i].key != 0 && key == s_smc_keyinfo_cache[i].key) {
                *key_info = (s_smc_keyinfo_cache[i].key_info);
                SMC_UNLOCK(s_keyinfo_lock);
                LOG_SCREAM(log, "SMC KEY %08x : Found in cache (idx:%d)", key, i);
                return SMC_SUCCESS;
            }
        }
        /* not found in cache */
        LOG_SCREAM(log, "SMC KEY %08x : not found in cache (sz:%d,idx:%d)",
                   key, s_keyinfo_cache_count, s_keyinfo_cache_index);
    }

    memset(&input_data, 0, sizeof(input_data));
    memset(&output_data, 0, sizeof(output_data));

    input_data.key = key;
    input_data.data8 = SMC_CMD_READ_KEYINFO;

    result = sysdep_smc_call(SMC_IOSERVICE_KERNEL_INDEX,
                             &input_data, &output_data, io_connection, log);
    if (result != kIOReturnSuccess) {
        if (use_cache)
            SMC_UNLOCK(s_keyinfo_lock);
        LOG_WARN(log, "SMC KEY %08x : cannot read key info ! (ret %lx)",
                  key, (unsigned long) result);
        return SMC_ERROR;
    }
    *key_info = output_data.keyInfo;

    if (use_cache) {
        s_smc_keyinfo_cache[s_keyinfo_cache_index].key_info = output_data.keyInfo;
        s_smc_keyinfo_cache[s_keyinfo_cache_index].key = key;
        if (++s_keyinfo_cache_index >= SMC_KEYINFO_CACHE_SIZE) {
            s_keyinfo_cache_index = 0;
        } else if (s_keyinfo_cache_count < SMC_KEYINFO_CACHE_SIZE) {
            ++s_keyinfo_cache_count;
        }
        LOG_SCREAM(log, "SMC KEY %08x : added in cache nextidx=%d, sz=%d",
                   key, s_keyinfo_cache_index, s_keyinfo_cache_count);

        SMC_UNLOCK(s_keyinfo_lock);
    }

    return SMC_SUCCESS;
}

/* ************************************************************************ */
int             sysdep_smc_readkey(
                    uint32_t        key,
                    uint32_t *      value_type,
                    void **         key_info,
                    void *          output_buffer,
                    void *          smc_handle,
                    log_t *         log)
{
    io_connect_t            io_connection = (io_connect_t)((unsigned long)smc_handle);
    kern_return_t           result;
    SMCKeyData_t            input_data;
    SMCKeyData_t *          output_data = (SMCKeyData_t *) output_buffer;
    unsigned int            value_size;

    memset(&input_data, 0, sizeof(SMCKeyData_t));
    //memset(output_data, 0, sizeof(SMCKeyData_t));

    if (key_info != NULL && *key_info != NULL) {
        input_data.keyInfo = *((SMCKeyData_keyInfo_t*)*key_info);
    } else if (smc_get_keyinfo(key, &(input_data.keyInfo), io_connection, log,
                               key_info == NULL) == SMC_SUCCESS) {
        if (key_info != NULL && (*key_info = malloc(sizeof(input_data.keyInfo))) != NULL) {
            memcpy(*key_info, &(input_data.keyInfo), sizeof(input_data.keyInfo));
        }
    } else {
        LOG_WARN(log, "key '%x': cannot get key info !", key);
        return SMC_ERROR;
    }
    value_size = input_data.keyInfo.dataSize;

    #ifdef _DEBUG
    LOG_BUFFER(LOG_LVL_SCREAM, log, &key, sizeof(key), "KEY sz:%2u ", value_size);
    #endif

    if (value_type != NULL) {
        *value_type = input_data.keyInfo.dataType;
        #ifdef _DEBUG
        LOG_BUFFER(LOG_LVL_SCREAM, log, value_type, sizeof(*value_type), "  TYPE    ");
        #endif
    }

    input_data.data8 = SMC_CMD_READ_BYTES;
    input_data.key = key;

    result = sysdep_smc_call(SMC_IOSERVICE_KERNEL_INDEX, &input_data,
                             output_data, io_connection, log);
    if (result != kIOReturnSuccess) {
        LOG_DEBUG(log, "key '%x': cannot read bytes ! (ret %lx)",
                  key, (unsigned long) result);
        return SMC_ERROR;
    }
    #ifdef _DEBUG
    LOG_BUFFER(LOG_LVL_SCREAM, log, output_data->bytes, value_size, "  BYTES   ");
    #endif

    return value_size;
}

/* ************************************************************************ */
int sysdep_smc_readindex(
        uint32_t    index,
        uint32_t *  value_key,
        uint32_t *  value_type,
        void **     key_info,
        void *      output_buffer,
        void *      smc_handle,
        log_t *     log) {

    io_connect_t    io_connection = (io_connect_t)((unsigned long)smc_handle);
    int             result;
    SMCKeyData_t    input_data;
    SMCKeyData_t *  output_data = (SMCKeyData_t *) output_buffer;

    memset(&input_data, 0, sizeof(SMCKeyData_t));
    memset(output_data, 0, sizeof(SMCKeyData_t));

    input_data.data8 = SMC_CMD_READ_INDEX;
    input_data.data32 = index;

    if (key_info != NULL && *key_info != NULL) {
        input_data.keyInfo = *((SMCKeyData_keyInfo_t *)*key_info);
    }
    if (value_key != NULL) {
        input_data.key = *value_key;
    }

    result = sysdep_smc_call(SMC_IOSERVICE_KERNEL_INDEX,
                             &input_data, output_data, io_connection, log);
    if (result != kIOReturnSuccess) {
        LOG_DEBUG(log, "%s() smc_call error", __func__);
        return SMC_ERROR;
    }
    #ifdef _DEBUG
    LOG_BUFFER(LOG_LVL_SCREAM, log, &output_data->key,
               sizeof(output_data->key), "#%04d KEY   ", index);
    #endif

    if (value_key != NULL) {
        *value_key = output_data->key;
    }
    #ifdef _DEBUG
    LOG_BUFFER(LOG_LVL_SCREAM, log, output_data->bytes, sizeof(output_data->bytes),
               "   %02u BYTES ", input_data.keyInfo.dataSize);
    #endif

    if (key_info != NULL && *key_info != NULL && value_type != NULL) {
        *value_type = ((SMCKeyData_keyInfo_t *)*key_info)->dataType;
    } else if (value_type != NULL || key_info != NULL) {
        if (smc_get_keyinfo(output_data->key, &(output_data->keyInfo), io_connection, log,
                            key_info == NULL)
                == SMC_SUCCESS) {
            input_data.keyInfo = output_data->keyInfo;
            if (value_type != NULL)
                *value_type = output_data->keyInfo.dataType;
            if (key_info != NULL) {
                if (*key_info != NULL
                ||  (*key_info = malloc(sizeof(SMCKeyData_keyInfo_t))) != NULL) {
                    memcpy(*key_info, &(output_data->keyInfo), sizeof(output_data->keyInfo));
                }
            }
        }
        #ifdef _DEBUG
        LOG_BUFFER(LOG_LVL_SCREAM, log, value_type, sizeof(*value_type), "      TYPE  ");
        #endif
    }

    return input_data.keyInfo.dataSize;
}

