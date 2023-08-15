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
 * disk linux implementation for Generic Sensor Management Library.
 * when udev is available, it handles dynamic storage device plug/unplug.
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>
#include <fnmatch.h>

#include "vlib/util.h"

#include "disk_private.h"

/* ************************************************************************ */
#define DISK_UDEV_SUBSYSTEM     "block"
#define DISK_UDEV_DEVTYPE       "disk"

#ifndef DISK_STAT_FILE
#define DISK_STAT_FILE          "/proc/diskstats"
#endif
#ifndef SYS_BLOCK_DIR
#define SYS_BLOCK_DIR           "/sys/block"
#endif
#define SYS_BLOCK_STAT_FILE     "stat"
#define SYS_BLOCK_SECTORSZ_FILE "queue/hw_sector_size"

typedef struct {
    char *      stat_line;
    size_t      stat_linesz;
    slist_t *   disks; //<diskstat_t *>  
} sysdep_t;

typedef enum {
    DSF_NONE        = 0,
    DSF_REMOVABLE   = 1 << 0,
} diskstat_flag_t;

typedef struct {
    char *          name;
    FILE *          stat;
    unsigned int    sector_sz;
    unsigned int    flags;
} diskstat_t;

// sysdeps/common-linux.c
sensor_status_t linux_common_udev_monitor_update(
                    sensor_family_t * family, 
                    const char * subsystem, 
                    const char * devtype, 
                    const char * tag);

/* ************************************************************************ */
static int disk_cmp(const void * vd1, const void * vd2) {
    const diskstat_t * d1 = (const diskstat_t *) vd1;
    const diskstat_t * d2 = (const diskstat_t *) vd2;
    if (d1->name == NULL || d2->name == NULL) {
        return d1->name - d2->name;
    }
    return strcmp(d1->name, d2->name);
}

static void disk_free(void * vd) {
    diskstat_t * d = (diskstat_t *) vd;
    if (d) {
        if (d->name) {
            free(d->name);
        }
        if (d->stat != NULL) {
            fclose(d->stat);                
        }
    }
}

static sensor_status_t disk_linux_check_stat_file(diskstat_t * disk, sensor_family_t * family) {
    disk_priv_t *   priv = (family->priv);
    sysdep_t *      sysdep = (sysdep_t *) priv->sysdep;
    ssize_t         linesz;
    
    if (disk->stat != NULL && fseek(disk->stat, 0, SEEK_SET) == 0) {
        return SENSOR_SUCCESS;
    }
    if (disk->stat != NULL)
        fclose(disk->stat);
    if (disk->name == NULL) {
        // open the generic /proc/diskstats
        disk->stat = fopen(DISK_STAT_FILE, "r");
        disk->sector_sz = 1;
    } else {
        // use per disk /sys/block/<disk>/{stat,hw_sector_size}
        char path[PATH_MAX];
        FILE * fsector_sz;
            
        snprintf(path, sizeof(path), "%s/%s/%s", 
                 SYS_BLOCK_DIR, disk->name, SYS_BLOCK_STAT_FILE);
                      
        disk->stat = fopen(path, "r");
               
        snprintf(path, sizeof(path), "%s/%s/%s", 
                 SYS_BLOCK_DIR, disk->name, SYS_BLOCK_SECTORSZ_FILE);
                         
        if ((fsector_sz = fopen(path, "r")) != NULL) {
            if ((linesz = getline(&sysdep->stat_line, &sysdep->stat_linesz, fsector_sz)) > 0) {
                if (sysdep->stat_line[linesz] == '\n')
                    sysdep->stat_line[linesz-1] = 0;
                disk->sector_sz = strtol(sysdep->stat_line, NULL, 10);
            }
            fclose(fsector_sz);
        } else {
            disk->sector_sz = 1;
        }                
    }
    
    if (disk->stat == NULL) {
        LOG_VERBOSE(family->log, "cannot open stat file %s", disk->name ? disk->name : DISK_STAT_FILE);
        return SENSOR_ERROR;
    }
    
    LOG_VERBOSE(family->log, "%s/%s openned, sector size: %u",
                disk->name ? SYS_BLOCK_DIR : DISK_STAT_FILE, disk->name ? disk->name : "",
                disk->sector_sz);

    return SENSOR_SUCCESS;
}

static sensor_status_t disk_linux_add_device(sensor_family_t * family, const char * name) {
    char            path[PATH_MAX];
    disk_priv_t *   priv = (family->priv);
    sysdep_t *      sysdep = (sysdep_t *) priv->sysdep;
    struct stat     st;
    diskstat_t      disk;
    FILE *          fremovable;
    
    // older linux 2.6 have ramXX under /sys/block without subdir 'device' -> ignore them.
    snprintf(path, sizeof(path), "%s/%s/device", SYS_BLOCK_DIR, name);
    if (stat(path, &st) < 0 || ((st.st_mode & S_IFMT) & (S_IFLNK | S_IFDIR)) == 0) {
        return SENSOR_NOT_SUPPORTED;
    }
    
    disk.stat = NULL;
    disk.name = strdup(name);
    disk.flags = 0;
    
    // check whether the device is removable
    snprintf(path, sizeof(path), "%s/%s/removable", SYS_BLOCK_DIR, name);
    if ((fremovable = fopen(path, "r")) != NULL) {
        char buf[64];
        if (fread(buf, 1, sizeof(buf), fremovable) > 0) {
            int bremovable = strtol(buf, NULL, 10);
            if (bremovable) {
                disk.flags |= DSF_REMOVABLE;
            }
        }
        fclose(fremovable);
    }           
    
    if (disk.name != NULL && (sysdep->disks = slist_prepend_sized(sysdep->disks, &disk, sizeof(disk))) == NULL) {
        LOG_ERROR(family->log, "cannot alloc list for '%s': %s", name, strerror(errno));
        if (disk.name != NULL)
           free(disk.name);
        return SENSOR_ERROR;
    }
    
    return SENSOR_SUCCESS;
}

static sensor_status_t disk_linux_handle_event(sensor_common_event_t * event, void * user_data) {
    sensor_family_t *   family = (sensor_family_t *) user_data;
    disk_priv_t *       priv = (family->priv);
    sysdep_t *          sysdep = (sysdep_t *) priv->sysdep;
    
    // Only block disk devices are processed. Others (including partitions) are ignored.
    // Please ensure that the call to linux_common_udev_monitor_update() in
    // sysdep_disk_init() below matches with this check, otherwise the queue won't be emptied.
    if (event->type != CQT_DEVICE
    ||  fnmatch(DISK_UDEV_SUBSYSTEM "/" DISK_UDEV_DEVTYPE "/*", event->u.dev.type, FNM_CASEFOLD) != 0) {
        return SENSOR_NOT_SUPPORTED;
    }

    const char * diskname = event->u.dev.name;

    if (!strncasecmp(diskname, "/dev/", 5))
        diskname += 5;

    LOG_DEBUG(family->log, "queue: processing device %s event: %s (%s)",
              event->u.dev.action == CDA_ADD ? "add" : "remove", event->u.dev.name, event->u.dev.type);

    if (event->u.dev.action == CDA_ADD) {
        char        path[PATH_MAX];
        struct stat st;

        snprintf(path, sizeof(path), "%s/%s", SYS_BLOCK_DIR, diskname);
        if (stat(path, &st) == 0 && ((st.st_mode & S_IFMT) & (S_IFLNK | S_IFDIR)) != 0) {
            if (disk_linux_add_device(family, diskname) == SENSOR_SUCCESS) {
                LOG_VERBOSE(family->log, "added block %s", diskname);
            }
        }
    } else if (event->u.dev.action == CDA_REMOVE) {
        diskstat_t disk;
            
        disk.stat = NULL;
        disk.name = (char *) diskname;
        errno = 0;
        sysdep->disks = slist_remove_sized(sysdep->disks, &disk, disk_cmp, disk_free);
        if (errno == 0)
            LOG_VERBOSE(family->log, "removed block %s", disk.name);
    }
    
    return SENSOR_SUCCESS;
}

/* ************************************************************************ */
sensor_status_t sysdep_disk_support(sensor_family_t * family, const char * label) {
    (void)family;
    (void)label;
    return SENSOR_SUCCESS;
}

/* ************************************************************************ */
sensor_status_t sysdep_disk_init(sensor_family_t * family) {
    disk_priv_t *   priv = (family->priv);    

    if (priv->sysdep != NULL) {
        return SENSOR_SUCCESS;
    }
    
    sysdep_t *      sysdep;
    int             fd = -1;
    DIR *           dir;
        
    priv->sysdep = calloc(1, sizeof(sysdep_t));
    if (priv->sysdep == NULL) {
        LOG_ERROR(family->log, "error, cannot malloc %s sysdep data", family->info->name);
        errno=ENOMEM;
        return SENSOR_ERROR;
    }

    sysdep = priv->sysdep;
    sysdep->stat_line = NULL;
    sysdep->stat_linesz = 0;

    if ((dir = opendir(SYS_BLOCK_DIR)) != NULL) {
        struct dirent * dirent;

        if (linux_common_udev_monitor_update(
                sensor_family_common(family->sctx), 
                DISK_UDEV_SUBSYSTEM, 
                DISK_UDEV_DEVTYPE, 
                NULL) != SENSOR_SUCCESS) {
            LOG_WARN(family->log, "cannot monitor udev %s/%s for dir '%s'", 
                     DISK_UDEV_SUBSYSTEM, DISK_UDEV_DEVTYPE, SYS_BLOCK_DIR);
        }
   
        while ((dirent = readdir(dir)) != NULL) {
            if ((dirent->d_type == DT_DIR || dirent->d_type == DT_LNK) 
            &&  strcmp(dirent->d_name, ".") && strcmp(dirent->d_name, "..")) {               
                if (disk_linux_add_device(family, dirent->d_name) == SENSOR_SUCCESS) {
                    LOG_VERBOSE(family->log, "added block %s", dirent->d_name);
                }                
            }
        }
        closedir(dir);
    } else {            
        diskstat_t disk = { .name = NULL, .stat = NULL, .flags = 0 };
        
        if ((sysdep->disks = slist_prepend_sized(sysdep->disks, &disk, sizeof(disk))) == NULL) {
            LOG_ERROR(family->log, "error while openning %s", DISK_STAT_FILE);
            errno=ENOENT;
            return SENSOR_ERROR;
        }
    }
    
    if (fd >= 0)
        close(fd);

    return SENSOR_SUCCESS;
}

/* ************************************************************************ */
sensor_status_t sysdep_disk_destroy(sensor_family_t * family) {
    disk_priv_t * priv = (disk_priv_t *) family->priv;

    if (priv != NULL && priv->sysdep != NULL) {
        sysdep_t * sysdep = (sysdep_t *) priv->sysdep;

        slist_free_sized(sysdep->disks, disk_free);
        sysdep->disks = NULL;
        
        if (sysdep->stat_line != NULL)
            free(sysdep->stat_line);
        sysdep->stat_line = NULL;

        priv->sysdep = NULL;
        free(sysdep);
    }
    return SENSOR_SUCCESS;
}

/* ************************************************************************ */
sensor_status_t     sysdep_disk_get(
                        sensor_family_t *   family,
                        disk_data_t *       data,
                        struct timeval *    elapsed) {
    disk_priv_t *   priv = (family->priv);
    sysdep_t *      sysdep = priv->sysdep;
    ssize_t  linesz;
    uint64_t total_ibytes = 0;
    uint64_t total_obytes = 0;
    uint64_t phy_ibytes = 0;
    uint64_t phy_obytes = 0;

    if (priv->sysdep == NULL) {
        LOG_ERROR(family->log, "error, bad %s sysdep data", family->info->name);
        errno = EFAULT;
        return SENSOR_ERROR;
    }

    sensor_common_queue_process(family->sctx, disk_linux_handle_event, family);
    
    SLISTC_FOREACH_PDATA(sysdep->disks, disk, diskstat_t *) {
        
        if (disk_linux_check_stat_file(disk, family) != SENSOR_SUCCESS) {
            continue ;
        }
        
        while ((linesz = getline(&sysdep->stat_line, &sysdep->stat_linesz, disk->stat)) > 0) {
            char * line = sysdep->stat_line;
            const char * value, * next = line;
            ssize_t val_len;
            size_t maxlen = linesz;
            unsigned int val_idx;
            int phys = (disk->flags & DSF_REMOVABLE) == 0;
            int loop = 1;
            
            if (sysdep->stat_line == NULL)
                break ;
            
            /* /proc/diskstats format: (dev maj/min/name skipped for /sys/block/<disk>/stat) {
             *    dev      |   Read                       |  Write                    | I/O                 | Discarded           |Flush
             * maj min name| #done #merged #sector  ms    |#done #merged #sector ms   | nb-wip ms  weight-ms| # #merged #sector ms| 
             * 8   0  sda   46601  23234   1909873  482125 7665  10324   327984  102709 0    166912 614449    0 0       0       0  652 29614 
             * 8   1  sda1    ...
             *  ... */
            
            if (linesz <= 1)
                continue ;

            if (sysdep->stat_line[maxlen - 1] == '\n')
                sysdep->stat_line[--maxlen] = 0;

            LOG_SCREAM(family->log, "%s LINE (sz:%zu) %s", 
                       disk->name == NULL ? DISK_STAT_FILE : disk->name, linesz, line);
                
            /* skip leading spaces */
            while (*next == ' ' || *next == '\t') {
                ++next;
                --maxlen;
            }
            
            // start at idx 0 (dev name) for /proc/diskstats, 
            // or at idx 3 (# read) for /sys/block/<disk>/stat.
            val_idx = disk->name == NULL ? 0 : 3;
            
            /* get values */
            while(loop && ((val_len = strtok_ro_r(&value, " \t\n", &next, &maxlen, 0)) > 0 || *next)) {
                //uint64_t lo_ibytes = 0;
                //uint64_t lo_obytes = 0;
                uint64_t ibytes;
                uint64_t obytes;

                if (val_len == 0)
                    continue ;

                //fwrite(value, 1, val_len, family->log->out); fputc('\n', family->log->out);
               
                switch (val_idx) {
                    case 0:
                        /* device major */
                        break ;
                    case 1:
                        /* device minor */
                        break ;
                    case 2:
                        /* device name */
                        if (isdigit(value[val_len-1]) && strncmp(value, "sr", 2) 
                        &&  (!strncmp(value,"cd", 2) || !strncmp(value, "dvd", 3) 
                             || !strncmp(value, "bd", 2) || !strncmp(value, "ram", 3)))
                            loop = 0; // stop processing this line
                        break ;
                    case 3:
                        /* # reads completed */                    
                        break ;
                    case 4:
                        /* # reads merged */
                        break ;
                    case 5:
                        /* # sectors read */
                        ibytes = strtoul(value, NULL, 10) * disk->sector_sz;                        
                        total_ibytes += ibytes;
                        if (phys)
                            phy_ibytes += ibytes;
                        break ;
                    case 6:
                        /* # ms spent reading */
                        break ;
                    case 7:
                        /* # writes completed */
                        break ;
                    case 8:
                        /* writes merged */
                        break ;
                    case 9:
                        /* # sectors written */
                        obytes = strtoul(value, NULL, 10) * disk->sector_sz;
                        total_obytes += obytes;
                        if (phys)
                            phy_obytes += obytes;
                        // THIS IS THE LAST TOKEN WE ARE INTERESTED IN.
                        loop = 0;
                        break ;
                    case 10:
                        /* # ms spent writing */
                        break ;
                    case 11:
                        /* # I/O in progress */                    
                        break ;
                    case 12:
                        /* # ms spent doing I/O */
                        break ;
                    case 13:
                        /* # ms weight spent doing I/O */
                        break ;
                    case 14:
                        /* # discarded */
                        break ;
                    case 15:
                        /* # discarded merged */
                        break ;
                    case 16:
                        /* # sectors discarded */
                        break ;
                    case 17:
                        /* # ms spent discarding */
                        break ;
                    case 18:
                        /* # flush */
                        break ;
                    case 19:
                        /* # ms spent doing flush */
                        break ;
                }

                ++val_idx;
            }
                ; /* nothing */
            if (val_len == 0)
                continue ;
        }
    }

    if (elapsed == NULL) {
        data->ibytespersec = 0;
        data->obytespersec = 0;
        data->phy_ibytespersec = 0;
        data->phy_obytespersec = 0;
    } else {
        data->ibytespersec = (((total_ibytes - data->ibytes) * 1000)
                                / (elapsed->tv_sec * 1000 + elapsed->tv_usec / 1000));
        data->obytespersec = (((total_obytes - data->obytes) * 1000)
                                / (elapsed->tv_sec * 1000 + elapsed->tv_usec / 1000));
        data->phy_ibytespersec = (((phy_ibytes - data->phy_ibytes) * 1000)
                                    / (elapsed->tv_sec * 1000 + elapsed->tv_usec / 1000));
        data->phy_obytespersec = (((phy_obytes - data->phy_obytes) * 1000)
                                    / (elapsed->tv_sec * 1000 + elapsed->tv_usec / 1000));
    }

    data->ibytes = total_ibytes;
    data->obytes = total_obytes;
    data->phy_ibytes = phy_ibytes;
    data->phy_obytes = phy_obytes;

    return SENSOR_SUCCESS;
}

