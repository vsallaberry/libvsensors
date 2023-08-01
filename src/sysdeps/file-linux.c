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
 * file linux implementation for Generic Sensor Management Library.
 */
#include <sys/inotify.h>
#include <sys/types.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>

#include "libvsensors/sensor.h"
#include "vlib/util.h"

#include "file_private.h"
#include "common_private.h"

/* ************************************************************************ */

typedef struct {    
    int         notify_ifd;    
} sysdep_t;

typedef struct {
    int     ifd;
} sys_fileinfo_t;

/* ************************************************************************ */
static int file_linux_thread_event_read(
                vthread_t *             vthread,
                vthread_event_t         event,
                void *                  event_data,
                void *                  callback_user_data);

/* ************************************************************************ */                
static sensor_status_t file_linux_notify_init(sensor_family_t * family) {
    file_priv_t *       priv = (family->priv);
    sysdep_t *          sysdep = (sysdep_t *) priv->sysdep;
    sensor_family_t *   common = sensor_family_common(family->sctx);
    common_priv_t *     common_priv = common ? (common_priv_t *) common->priv : NULL;
    
    if ((sysdep->notify_ifd = inotify_init1(IN_NONBLOCK)) < 0) {
        LOG_WARN(family->log, "inotify_init1(): %s", strerror(errno));
        return SENSOR_ERROR;
    }
    
    if (common_priv == NULL 
    || vthread_register_event(common_priv->thread, VTE_FD_READ, VTE_DATA_FD(sysdep->notify_ifd), 
                              file_linux_thread_event_read, family) != 0) {
        LOG_WARN(family->log, "cannot register inotify events in common thread(): %s", strerror(errno));
        return SENSOR_ERROR;                              
    }
    
    return SENSOR_SUCCESS;
}

static sensor_status_t file_linux_notify_handle_events(sensor_family_t * family) {
    file_priv_t *   priv = (family->priv);
    sysdep_t *      sysdep = (sysdep_t *) priv->sysdep;

    /* Some systems cannot read integer variables if they are not
       properly aligned. On other systems, incorrect alignment may
       decrease performance. Hence, the buffer used for reading from
       he inotify file descriptor should have the same alignment as
       struct inotify_event. */
    char buf[4096] __attribute__ ((aligned(__alignof__(struct inotify_event))));
    const struct inotify_event *event;
    ssize_t len;

    /* Loop while events can be read from inotify file descriptor. */
    for (;;) {
        /* Read some events. */
        len = read(sysdep->notify_ifd, buf, sizeof(buf));
        if (len == -1) {
            if (errno == EINTR) {
                continue ;
            } else if (errno != EAGAIN) {
                LOG_WARN(family->log, "inotify read error: %s", strerror(errno));
                return SENSOR_ERROR;
            }
        }
        /* if nonblocking read() found no events, we exit the loop. */
        if (len <= 0)
            break;

        /* Loop over all events in the buffer */
        for (char *ptr = buf; ptr < buf + len;
            ptr += sizeof(struct inotify_event) + event->len) {

            event = (const struct inotify_event *) ptr;

            /* Print event type */
            if (event->mask & IN_CREATE)
                LOG_VERBOSE(family->log, "inotify IN_CREATE '%s'", event->name);
            if (event->mask & IN_DELETE)
                LOG_VERBOSE(family->log, "inotify IN_DELETE '%s'", event->name);
        }           
    }
    return SENSOR_SUCCESS;
}

static int file_linux_thread_event_read(
                vthread_t *             vthread,
                vthread_event_t         event,
                void *                  event_data,
                void *                  callback_user_data) {
    (void)vthread;
    (void)event;
    (void)event_data;
    file_linux_notify_handle_events(callback_user_data);
    return 0;                
}

static sensor_status_t file_linux_notify_destroy(sensor_family_t * family) {
    file_priv_t *   priv = (family->priv);
    sysdep_t *      sysdep = (sysdep_t *) priv->sysdep;    
    
    if (sysdep->notify_ifd >= 0)
        close(sysdep->notify_ifd);
            
    sysdep->notify_ifd = -1;
    
    // TODO: unregister event in common thread
    
    return SENSOR_SUCCESS;
}

/* ************************************************************************ */
sensor_status_t sysdep_file_support(sensor_family_t * family, const char * label) {
    (void)family;
    (void)label;
    return SENSOR_NOT_SUPPORTED;
    //return SENSOR_SUCCESS;
}

/* ************************************************************************ */
sensor_status_t sysdep_file_destroy(sensor_family_t * family) {
    file_priv_t * priv = (file_priv_t *) family->priv;

    if (priv != NULL && priv->sysdep != NULL) {
        sysdep_t * sysdep = (sysdep_t *) priv->sysdep;

        file_linux_notify_destroy(family);
        
        priv->sysdep = NULL;
        free(sysdep);
    }
    return SENSOR_SUCCESS;
}

/* ************************************************************************ */
sensor_status_t sysdep_file_init(sensor_family_t * family) {
    file_priv_t *   priv = (family->priv);    

    if (priv->sysdep != NULL) {
        return SENSOR_SUCCESS;
    }    
        
    priv->sysdep = calloc(1, sizeof(sysdep_t));
    if (priv->sysdep == NULL) {
        LOG_ERROR(family->log, "error, cannot malloc %s sysdep data", family->info->name);
        errno=ENOMEM;
        return SENSOR_ERROR;
    }    
                
    if (file_linux_notify_init(family) != SENSOR_SUCCESS) {
        LOG_ERROR(family->log, "cannot initialize inotify");
        sysdep_file_destroy(family);
        return SENSOR_ERROR;
    }
    
    return SENSOR_SUCCESS;
}

/* ************************************************************************ */
sensor_status_t     sysdep_file_get(
                        sensor_family_t *   family,
                        void *              data,
                        struct timeval *    elapsed) {
    (void)family;
    (void)data;
    (void)elapsed;
    return SENSOR_ERROR;
}

/* ************************************************************************ */
sensor_status_t sysdep_file_watch_add(sensor_family_t * family, fileinfo_t * info) {
    file_priv_t *   priv = (family->priv);
    sysdep_t *      sysdep = (sysdep_t *) priv->sysdep;
    sys_fileinfo_t* sysinfo = calloc(1, sizeof(*sysinfo));
    
    if (sysinfo == NULL) {
        return SENSOR_ERROR;
    }
    
    if ((sysinfo->ifd = inotify_add_watch(sysdep->notify_ifd, 
                                info->name, IN_CREATE | IN_DELETE /* FIXME info->flags */)) < 0) {
        LOG_WARN(family->log, "inotify_add_watch(%s): %s", info->name, strerror(errno));
        sysdep_file_watch_free(info);
        return SENSOR_ERROR;
    }

    return SENSOR_SUCCESS;
}

/* ************************************************************************ */
void sysdep_file_watch_free(void * vfile) {
    fileinfo_t * file = (fileinfo_t *) vfile;
    sys_fileinfo_t * sysfile = (sys_fileinfo_t *) file->sysdep;

    if (sysfile) {    
        if (sysfile->ifd >= 0) {
            close(sysfile->ifd); // FIXME inotify_rm_watch() ??
        }
        file->sysdep = NULL;
        free(sysfile);
    }
}

