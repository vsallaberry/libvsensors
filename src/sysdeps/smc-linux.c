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
 * smc linux for Generic Sensor Management Library.
 */
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <fnmatch.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>

#include "vlib/log.h"
#include "libvsensors/sensor.h"

#include "smc_private.h"

#ifndef SMC_LINUX_DIR
# define SMC_LINUX_DIR      "/sys/devices/platform"
#endif
#define SMC_LINUX_PATTERN   "applesmc*"

#define SMC_LINUX_BUF_SZ    1024

#define SMC_TYPE2(str) (str) // TODO: old gcc does not accept bitshift in static init
static struct { /*uint32_t key*/const char * key; const char * procfile; } 
  s_smc_linux_write_map[] = {
    { SMC_TYPE2("F\x01Mn"), "fan\x01_min" }, 
    { 0, NULL } // last
};

typedef struct {
    int dir_fd;
    int keyidx_fd;
    int keydata_fd;
} priv_t;

typedef struct {
    uint32_t key_index;
} keyinfo_t;

/** sensor API sysdep_smc_support() */   
sensor_status_t sysdep_smc_support(sensor_family_t * family, const char * label) {
    (void) label;
    (void) family;
    return SENSOR_SUCCESS;
}

/** 
 * smc linux /proc,/sys utilities 
 */
static int smc_linux_dir_filter(const struct dirent * e) {
    if (e->d_type != DT_DIR) {
        return 0;
    }
    return fnmatch(SMC_LINUX_PATTERN, e->d_name, 0) == 0;
}

static int smc_linux_open_dir(log_t * log) {
    const char * const path = SMC_LINUX_DIR;
    struct dirent **namelist;
    int fd = -1, dirfd;
    int n;

    dirfd = open(path, O_RDONLY);
    n = scandir(path, &namelist, smc_linux_dir_filter, alphasort);
    if (dirfd < 0 || n < 0) {
        LOG_WARN(log, "cannot find smc in %s", path);
        close(dirfd);
        return -1;
    }

    while (n--) {
        if (fd == -1) {         
            fd = openat(dirfd, namelist[n]->d_name, O_RDONLY);
            LOG_VERBOSE(log, "openning %s/%s... %s", 
                        path, namelist[n]->d_name, fd >= 0 ? "OK" : strerror(errno));
        }
        free(namelist[n]);
    }
    free(namelist);
    close(dirfd);    
    return fd;
}

static int smc_linux_open_file(int * p_dirfd, const char * name, int write, log_t * log) {
     int fd = -1;
     if (*p_dirfd == -1 || (fd = openat(*p_dirfd, name, write ? O_WRONLY : O_RDONLY)) < 0) {
         if (*p_dirfd >= 0)
             close(*p_dirfd);
         *p_dirfd = smc_linux_open_dir(log);
         fd = openat(*p_dirfd, name, write ? O_WRONLY : O_RDONLY);
     }
     return fd;    
}

static int smc_linux_select_index(priv_t * priv, uint32_t index, log_t * log) {
    if (priv->keyidx_fd < 0 || lseek(priv->keyidx_fd, 0, SEEK_SET) == (off_t) -1) {
        if (priv->keyidx_fd >= 0)
            close(priv->keyidx_fd);
        priv->keyidx_fd = smc_linux_open_file(&priv->dir_fd, "key_at_index", 1, log);
    }
    if (priv->keyidx_fd < 0 || dprintf(priv->keyidx_fd, "%u", index) <= 0) {
        LOG_VERBOSE(log, "cannot write to %s/%s/%s: %s.", SMC_LINUX_DIR, SMC_LINUX_PATTERN, "key_at_index", strerror(errno));
        return SENSOR_ERROR;
    }
    return SENSOR_SUCCESS;
}
    
/** sensor API sysdep_smc_open() */
int sysdep_smc_open(void ** psmc_handle, log_t *log,
                    unsigned int * bufsize, unsigned int * value_offset) {
    if (psmc_handle == NULL) {
        LOG_ERROR(log, "null smc handle");
        return SENSOR_ERROR;
    }
    if ((*psmc_handle = malloc(sizeof(priv_t))) == NULL) {
        LOG_ERROR(log, "cannot malloc smc linux priv data: %s", strerror(errno));
        return SENSOR_ERROR;
    } 
    priv_t * priv = (priv_t *) *psmc_handle;
    priv->dir_fd = smc_linux_open_dir(log); 
    
    if (priv->dir_fd < 0) {
        LOG_VERBOSE(log, "Cannot open dir %s/%s: Maybe you you don't have smc, or the driver is not loaded.",
                    SMC_LINUX_DIR, SMC_LINUX_PATTERN);
        priv->keyidx_fd = -1;          
    } else if ((priv->keyidx_fd = smc_linux_open_file(&priv->dir_fd, "key_at_index", 1, log)) < 0) {
        LOG_ERROR(log, "cannot open %s/%s/%s for writing: %s",
                  SMC_LINUX_DIR, SMC_LINUX_PATTERN, "key_at_index", strerror(errno));
        LOG_INFO(log, "consider running as root or adding an udev rule in /usr/lib/udev/rules.d: ");
        LOG_INFO(log, "ACTION==\"add\", SUBSYSTEM==\"platform\", DRIVER==\"applesmc\", "
                      "RUN+=\"/bin/sh -c 'file=\\\"/sys/devices/platform/%%k/key_at_index\\\"; "
                        "/bin/chmod g+w \\\"$file\\\"; /bin/chgrp <user_group> \\\"$file\\\"'\"");
        LOG_WARN(log, "no SMC sensor can be found without write access on key_at_index");
    }
    priv->keydata_fd = -1;
    *bufsize = SMC_LINUX_BUF_SZ;
    *value_offset = 0;

    return SENSOR_SUCCESS;
}

/** sensor API sysdep_smc_close() */
int                 sysdep_smc_close(void * smc_handle, log_t *log) { 
    priv_t * priv = (priv_t *) smc_handle;
    (void)log;

    if (priv == NULL)
        return SENSOR_SUCCESS;
        
    if (priv->dir_fd >= 0)
        close(priv->dir_fd);
    if (priv->keyidx_fd >= 0)
        close(priv->keyidx_fd);
    if (priv->keydata_fd >= 0)
        close(priv->keydata_fd);
    
    free(priv);
    
    return SENSOR_SUCCESS;
}

/** sensor API sysdep_smc_readindex() */
int                 sysdep_smc_readindex(
                        uint32_t        index,
                        uint32_t *      value_key,
                        uint32_t *      value_type,
                        void **         key_info,
                        void *          output_buffer,
                        void *          smc_handle,
                        log_t *         log) {
    priv_t *        priv = (priv_t *) smc_handle;
    ssize_t         n;
    int             key_fd;
    int             ret = -1;
    char *          buf = output_buffer;
    const size_t    bufsz = SMC_LINUX_BUF_SZ;

    if (key_info != NULL && *key_info == NULL) {
        keyinfo_t * pinfo = *key_info = malloc(sizeof(keyinfo_t));
        if (pinfo == NULL) {
            LOG_WARN(log, "cannot malloc smc linux keyinfo_t");
        } else {
            pinfo->key_index = index;
        }
    }
    
    if (smc_linux_select_index(priv, index, log) == SENSOR_ERROR) {
        LOG_VERBOSE(log, "cannot select index %u", index);
        return SENSOR_NOT_SUPPORTED;
    }
    
    if (value_key != NULL) {
        key_fd = smc_linux_open_file(&priv->dir_fd, "key_at_index_name", 0, log);
        if (key_fd < 0 || (n = read(key_fd, buf, bufsz-1)) <= 0) {
            close(key_fd);
            LOG_VERBOSE(log, "cannot open key_at_index_name for index %u", index);
            return SENSOR_ERROR;
        }
        buf[n] = 0;
        if (buf[n-1] == '\n') {
            buf[--n] = 0;
        }
        *value_key = _str32toul(buf, n, 10);
        close(key_fd);
    }
    
    if (value_type != NULL) {
        key_fd = smc_linux_open_file(&priv->dir_fd, "key_at_index_type", 0, log);
        if (key_fd < 0 || (n = read(key_fd, buf, bufsz-1)) <= 0) {
            close(key_fd);
            LOG_VERBOSE(log, "cannot open key_at_index_type for index %u", index);
            return SENSOR_ERROR;
        }
        buf[n] = 0;
        if (buf[n-1] == '\n') {
            buf[--n] = 0;
        }
        *value_type = _str32toul(buf, n, 10);
        close(key_fd);
    }
        
    if (priv->keydata_fd < 0 || lseek(priv->keydata_fd, 0, SEEK_SET) == (off_t) -1) {
        close(priv->keydata_fd);
        priv->keydata_fd = smc_linux_open_file(&priv->dir_fd, "key_at_index_data", 0, log);
    }
    if (priv->keydata_fd < 0 || (n = read(priv->keydata_fd, buf, bufsz-1)) <= 0) {
        LOG_VERBOSE(log, "cannot read data for index %u: %s", index, strerror(errno));
        return SENSOR_ERROR;
    }
    if (buf[n-1] == '\n') {
        buf[--n] = 0;
    }
    ret = n;
    
    (void)key_info;
    
    return ret;
}

/** sensor API sysdep_smc_readkey() */
int                 sysdep_smc_readkey(
                        uint32_t        key,
                        uint32_t *      value_type,
                        void **         key_info,
                        void *          output_buffer,
                        void *          smc_handle,
                        log_t *         log) {
    priv_t * priv = (priv_t *) smc_handle;

    if (key_info != NULL && *key_info != NULL) {
        return sysdep_smc_readindex(((keyinfo_t *) *key_info)->key_index, NULL, NULL, key_info, output_buffer, smc_handle, log);
    }
    
    if (key == SMC_TYPE("#KEY")) {
        int fd = smc_linux_open_file(&priv->dir_fd, "key_count", 0, log);
        FILE * f;
        char * line = NULL;
        size_t line_cap = 0;
        ssize_t len;
        
        if (fd < 0 || (f = fdopen(fd, "r")) == NULL) {
            close(fd);
            return SENSOR_ERROR;
        }
        if ((len = getline(&line, &line_cap, f)) > 0) {
            if (line[len-1] == '\n')
                line[--len] = 0;
            uint16_t u16;
            u16 = htons(strtol(line, NULL, 10));
            memcpy(output_buffer, &u16, sizeof(u16));
            *value_type = SMC_TYPE("ui16");
            len = sizeof(u16);
            //strncpy(output_buffer, line, len);
            //*value_type = SMC_TYPE("ch8*");
        }
        if (line != NULL)
            free(line);
        fclose(f);
        return len;
    }
    (void)key_info;
    return SENSOR_ERROR;
}

/** sensor API sysdep_smc_writekey() */
int             sysdep_smc_writekey(
                    uint32_t        key,
                    uint32_t *      value_type,
                    void **         key_info,
                    void *          input_buffer,
                    uint32_t        input_size,
                    const sensor_value_t * value,
                    void *          smc_handle,
                    log_t *         log) {
    priv_t *    priv = (priv_t *) smc_handle;

#if 0 // linux applesmc driver does not seem to support write-at-index    
    (void)key;
    (void)value_type;

    int         key_fd;
    ssize_t     n;

    if (key_info == NULL || *key_info == NULL) {
        return SENSOR_ERROR;
    }
    
    if (smc_linux_select_index(priv, ((keyinfo_t *) *key_info)->key_index, log) == SENSOR_ERROR) {
        return SENSOR_ERROR;
    }
    
    key_fd = smc_linux_open_file(&priv->dir_fd, "key_at_index_data", 1, log);
    if (key_fd < 0 || (n = write(key_fd, input_buffer, input_size)) <= 0) {
        close(key_fd);
        return SENSOR_ERROR;
    }

    close(key_fd);
        
    return SENSOR_SUCCESS;
#else
    (void) input_buffer;
    (void) input_size;
    (void) key_info;
    (void) value_type;

    LOG_DEBUG(log, "check write key: %x", key);
    
    // search for a matching key in the s_smc_linux_write_map array.
    for (size_t i = 0; i < sizeof(s_smc_linux_write_map) / sizeof(*s_smc_linux_write_map); ++i) {
        char        map_procfile[128]; map_procfile[sizeof(map_procfile) - 1] = 0;
        uint32_t    map_key;
        size_t      ikey;
        
        if (s_smc_linux_write_map[i].key == NULL)
            continue ;
        
        map_key = SMC_TYPE(s_smc_linux_write_map[i].key);
        if (s_smc_linux_write_map[i].procfile == NULL || map_key == 0)
            continue ;

        strncpy(map_procfile, s_smc_linux_write_map[i].procfile, sizeof(map_procfile) - 1);
        
        for (ikey = 0; ikey < sizeof(key); ++ikey) {
            char key_char = (key >> (8 * (sizeof(key) - ikey))) & 0xff;
            char map_char = (map_key >> (8 * (sizeof(key) - ikey))) & 0xff;
            
            LOG_DEBUG(log, "check key_char %x / map_char %x", key_char & 0xff, map_char & 0xff);
            
            if (map_char >= 0 && map_char <= 9) {
                // replace the \x0? in map_procfile by the actual number in key
                for (size_t istr = 0; map_procfile[istr] != 0; ++istr) {
                    if (map_procfile[istr] == map_char)
                        map_procfile[istr] = key_char + 1;
                }
            } else if (key_char != map_char) {
                break ;
            }
        }
        if (ikey == sizeof(key)) {
            LOG_DEBUG(log, "FOUND %s", map_procfile);
            
            int ret;
            int fd = smc_linux_open_file(&priv->dir_fd, map_procfile, 1, log);
            
            if (fd < 0) {
                return SENSOR_ERROR;
            }
            size_t len = sensor_value_tostring(value, map_procfile, sizeof(map_procfile));
            int errno_save;
            
            ret = write(fd, map_procfile, len);
            errno_save = errno;
            
            close(fd);
            errno = errno_save;
            
            return ret > 0 ? SENSOR_SUCCESS : SENSOR_ERROR;
        } 
    }
    errno = EIO;
    return SENSOR_ERROR;
#endif
}

