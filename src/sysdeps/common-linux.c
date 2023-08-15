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
 * common linux implementation for Generic Sensor Management Library.
 */
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>

#ifdef SENSORS_UDEV_HEADER
#include <libudev.h>
#else
#include <dlfcn.h>
struct udev;
struct udev_monitor;
struct udev_device;
/*
#define SENSORS_UDEV_FUN(ret_type, name, args) \
    typedef ret_type (*name##fun)(args); \
    static name##_fun name;
typedef struct udev * (*udev_new_fun)();
static udev_new_fun udev_new;
typedef struct udev_monitor * (*udev_monitor_new_from_netlink_fun)(struct udev *, const char *);
static udev_monitor_new_from_netlink_fun udev_monitor_new_from_netlink;
*/
static struct udev * (*udev_new)() = NULL;
static struct udev_monitor * (*udev_monitor_new_from_netlink)(struct udev *, const char *) = NULL;
static int (*udev_monitor_filter_add_match_subsystem_devtype)(struct udev_monitor *, const char *, const char *) = NULL;
static int (*udev_monitor_filter_add_match_tag)(struct udev_monitor *, const char *) = NULL;
static int (*udev_monitor_enable_receiving)(struct udev_monitor *) = NULL;
static int (*udev_monitor_get_fd)(struct udev_monitor *) = NULL;
static struct udev_device * (*udev_monitor_receive_device)(struct udev_monitor *) = NULL;
static int (*udev_monitor_filter_update)(struct udev_monitor *) = NULL;
static const char * (*udev_device_get_devnode)(struct udev_device *) = NULL;
static const char * (*udev_device_get_action)(struct udev_device *) = NULL;
static const char * (*udev_device_get_devtype)(struct udev_device *) = NULL;
static const char * (*udev_device_get_subsystem)(struct udev_device *) = NULL;
static const char * (*udev_device_get_driver)(struct udev_device *) = NULL;
static const char * (*udev_device_get_sysattr_value)(struct udev_device *, const char *) = NULL;
static struct udev_device * (*udev_device_unref)(struct udev_device *) = NULL;
static struct udev_monitor * (*udev_monitor_unref)(struct udev_monitor *) = NULL;
static struct udev * (*udev_unref)(struct udev *) = NULL;
#endif

#include "vlib/util.h"

#include "common_private.h"

// ***************************************************************************
typedef struct {
#ifndef SENSORS_UDEV_HEADER
    void *                  udevlib;
#endif
    struct udev*            udev;
    struct udev_monitor*    udev_mon;
    int                     udev_mon_fd;
} sysdep_t;

static int common_linux_thread_devread(
                vthread_t *             vthread,
                vthread_event_t         event,
                void *                  event_data,
                void *                  callback_user_data);

#ifndef SENSORS_UDEV_HEADER
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wpedantic"
static sensor_status_t common_linux_udevlib_init(sensor_family_t * family) {
    common_priv_t * priv = (family->priv);
    sysdep_t *      sysdep = (sysdep_t *) priv->sysdep;

    if (sysdep->udevlib != NULL) {
        return SENSOR_SUCCESS;
    }

    static const char * libs[] = { "libudev.so", "libudev.so.1", "libudev.so.2", "libudev.so.3", "libudev.so.4", "libudev.so.0", NULL };

    for (const char ** lib = libs; sysdep->udevlib == NULL && *lib; ++lib) {
        sysdep->udevlib = dlopen(*lib, RTLD_LAZY);
    }
    if (sysdep->udevlib == NULL) {
        LOG_WARN(family->log, "cannot open udev library -> no dynamic device");
        return SENSOR_ERROR;
    }

    if ((udev_new = dlsym(sysdep->udevlib, "udev_new")) == NULL
    ||  (udev_monitor_new_from_netlink = dlsym(sysdep->udevlib, "udev_monitor_new_from_netlink")) == NULL
    ||  (udev_monitor_filter_add_match_subsystem_devtype = dlsym(sysdep->udevlib, "udev_monitor_filter_add_match_subsystem_devtype")) == NULL
    ||  (udev_monitor_filter_add_match_tag = dlsym(sysdep->udevlib, "udev_monitor_filter_add_match_tag")) == NULL
    ||  (udev_monitor_enable_receiving = dlsym(sysdep->udevlib, "udev_monitor_enable_receiving")) == NULL
    ||  (udev_monitor_get_fd = dlsym(sysdep->udevlib, "udev_monitor_get_fd")) == NULL
    ||  (udev_monitor_receive_device = dlsym(sysdep->udevlib, "udev_monitor_receive_device")) == NULL
    ||  (udev_monitor_filter_update = dlsym(sysdep->udevlib, "udev_monitor_filter_update")) == NULL
    ||  (udev_device_get_devnode = dlsym(sysdep->udevlib, "udev_device_get_devnode")) == NULL
    ||  (udev_device_get_action = dlsym(sysdep->udevlib, "udev_device_get_action")) == NULL
    ||  (udev_device_get_devtype = dlsym(sysdep->udevlib, "udev_device_get_devtype")) == NULL
    ||  (udev_device_get_subsystem = dlsym(sysdep->udevlib, "udev_device_get_subsystem")) == NULL
    ||  (udev_device_get_driver = dlsym(sysdep->udevlib, "udev_device_get_driver")) == NULL
    ||  (udev_device_get_sysattr_value = dlsym(sysdep->udevlib, "udev_device_get_sysattr_value")) == NULL
    ||  (udev_device_unref = dlsym(sysdep->udevlib, "udev_device_unref")) == NULL
    ||  (udev_monitor_unref = dlsym(sysdep->udevlib, "udev_monitor_unref")) == NULL
    ||  (udev_unref = dlsym(sysdep->udevlib, "udev_unref")) == NULL) {
        LOG_WARN(family->log, "cannot find symbols in udev library -> no dynamic device");
        dlclose(sysdep->udevlib);
        sysdep->udevlib = NULL;
        return SENSOR_ERROR;
    }
    LOG_VERBOSE(family->log, "udevlib loaded.");
    return SENSOR_SUCCESS;
}
# pragma GCC diagnostic pop

static sensor_status_t common_linux_udevlib_destroy(sensor_family_t * family) {
    common_priv_t * priv = (family->priv);
    sysdep_t *      sysdep = (sysdep_t *) priv->sysdep;

    if (sysdep->udevlib != NULL) {
        dlclose(sysdep->udevlib);
        sysdep->udevlib = NULL;
    }

    return SENSOR_SUCCESS;
}
#else
static sensor_status_t common_linux_udevlib_init(sensor_family_t * family) {
    (void)family;
    return SENSOR_SUCCESS;
}
static sensor_status_t common_linux_udevlib_destroy(sensor_family_t * family) {
    (void)family;
    return SENSOR_SUCCESS;
}
#endif

static sensor_status_t common_linux_udev_init(sensor_family_t * family) {
    common_priv_t * priv = (family->priv);
    sysdep_t *      sysdep = (sysdep_t *) priv->sysdep;

    if (common_linux_udevlib_init(family) != SENSOR_SUCCESS) {
        LOG_WARN(family->log, "cannot load udev library -> no dynamic device");
        return SENSOR_ERROR;
    }

    sysdep->udev = udev_new();

    if (!sysdep->udev) {
        LOG_ERROR(family->log, "udev_new() failed\n");
        return SENSOR_ERROR;
    }

    sysdep->udev_mon = udev_monitor_new_from_netlink(sysdep->udev, "udev");

    if (sysdep->udev_mon == NULL) {
        LOG_ERROR(family->log, "udev_monitor_new() failed\n");
        return SENSOR_ERROR;
    }

    udev_monitor_enable_receiving(sysdep->udev_mon);

    sysdep->udev_mon_fd = udev_monitor_get_fd(sysdep->udev_mon);

    vthread_register_event(priv->thread, VTE_FD_READ, VTE_DATA_FD(sysdep->udev_mon_fd), common_linux_thread_devread, family);

    LOG_VERBOSE(family->log, "udev initialized.");

    return SENSOR_SUCCESS;
}

static sensor_status_t common_linux_udev_destroy(sensor_family_t * family) {
    common_priv_t * priv = (family->priv);
    sysdep_t *      sysdep = (sysdep_t *) priv->sysdep;
    sensor_status_t ret;

    if (sysdep->udev_mon != NULL) {
        udev_monitor_unref(sysdep->udev_mon);
        sysdep->udev_mon = NULL;
        sysdep->udev_mon_fd = -1;
    }

    if (sysdep->udev != NULL) {
        udev_unref(sysdep->udev);
        sysdep->udev = NULL;
    }

    ret = common_linux_udevlib_destroy(family);

    return ret;
}

static sensor_status_t common_linux_handle_device(sensor_family_t * family, int fd) {
    common_priv_t *     priv = (family->priv);
    sysdep_t *          sysdep = (sysdep_t *) priv->sysdep;
    struct udev_device* dev;
    (void)fd;

    if (sysdep == NULL || sysdep->udev_mon == NULL
    ||  (dev = udev_monitor_receive_device(sysdep->udev_mon)) == NULL) {
        return SENSOR_ERROR;
    }
    const char * devnode = udev_device_get_devnode(dev);
    const char * saction = udev_device_get_action(dev);
    const char * devtype = udev_device_get_devtype(dev);
    const char * devsubsys = udev_device_get_subsystem(dev);
    const char * devdrv = udev_device_get_driver(dev);

    if (!devnode) {
        return SENSOR_ERROR;
    }

    sensor_common_device_action_t action = CDA_NONE;
    if (! saction)
        saction = "exists";

    LOG_DEBUG(family->log, "UDEV %s EVENT: %s (%s/%s/%s)", saction, devnode, devsubsys, devtype, devdrv);

    if (!strcasecmp(saction, "add")) {
        action = CDA_ADD;
    } else if (!strcasecmp(saction, "remove")) {
        action = CDA_REMOVE;
    } /* else if (!strcasecmp(saction, "change")) { // not needed
        action = CDA_CHANGE;
    } */
    if (action != CDA_NONE) {
        sensor_common_event_t * event = (sensor_common_event_t *) calloc(1, sizeof(sensor_common_event_t));
        if (event != NULL) {
            event->type = CQT_DEVICE;
            event->u.dev.type = NULL;
            asprintf(&event->u.dev.type, "%s/%s/%s", devsubsys, devtype, devdrv);
            event->u.dev.action = action;
            event->u.dev.name = strdup(devnode);
            event->sysdep = NULL;
            sensor_common_queue_add(family->sctx, event);
        }
    }

    udev_device_unref(dev);

    return SENSOR_SUCCESS;
}

static int common_linux_thread_devread(
                vthread_t *             vthread,
                vthread_event_t         event,
                void *                  event_data,
                void *                  callback_user_data) {
    (void)vthread;
    (void)event;
    common_linux_handle_device(callback_user_data, VTE_FD_DATA(event_data));
    return 0;
}

sensor_status_t common_linux_udev_handle_events(sensor_family_t * family) {
    common_priv_t * priv = (family->priv);
    sysdep_t *      sysdep = (sysdep_t *) priv->sysdep;
    struct pollfd   pollfd;

    pollfd.events = POLLIN;
    pollfd.fd = sysdep->udev_mon_fd;

    while (poll(&pollfd, 1, 0) > 0) {
        if ((pollfd.revents & POLLIN) == 0) {
            continue ;
        }
        common_linux_handle_device(family, pollfd.fd);
    }

    return SENSOR_SUCCESS;
}

/* ************************************************************************ */
sensor_status_t sysdep_common_init(sensor_family_t * family) {
    common_priv_t *   priv = (family->priv);

    if (priv->sysdep != NULL) {
        return SENSOR_SUCCESS;
    }

    sysdep_t *      sysdep;

    priv->sysdep = calloc(1, sizeof(sysdep_t));
    if (priv->sysdep == NULL) {
        LOG_ERROR(family->log, "error, cannot malloc %s sysdep data", family->info->name);
        errno=ENOMEM;
        return SENSOR_ERROR;
    }

    sysdep = priv->sysdep;

    sysdep->udev = NULL;
#  ifndef SENSORS_UDEV_HEADER
    sysdep->udevlib = NULL;
#  endif

    if (common_linux_udev_init(family) != SENSOR_SUCCESS) {
        LOG_WARN(family->log, "cannot initialize udev");
    }

    return SENSOR_SUCCESS;
}

/* ************************************************************************ */
sensor_status_t sysdep_common_destroy(sensor_family_t * family) {
    common_priv_t * priv = (common_priv_t *) family->priv;

    if (priv != NULL && priv->sysdep != NULL) {
        sysdep_t * sysdep = (sysdep_t *) priv->sysdep;

        common_linux_udev_destroy(family);

        priv->sysdep = NULL;
        free(sysdep);
    }
    return SENSOR_SUCCESS;
}

/* ************************************************************************ */
sensor_status_t linux_common_udev_monitor_update(
                        sensor_family_t * family,
                        const char * subsystem,
                        const char * devtype,
                        const char * tag) {
    common_priv_t * priv = (family->priv);
    sysdep_t *      sysdep = (sysdep_t *) (priv ? priv->sysdep : NULL);
    sensor_status_t ret = SENSOR_SUCCESS;

    if (sysdep == NULL || sysdep->udev_mon == NULL) {
        LOG_WARN(family->log, "udev_monitor_update(): udev not initialized");
        return SENSOR_ERROR;
    }

    if (subsystem != NULL
    &&  udev_monitor_filter_add_match_subsystem_devtype(sysdep->udev_mon, subsystem, devtype) < 0) {
        ret = SENSOR_ERROR;
    }
    if (tag != NULL
    &&  udev_monitor_filter_add_match_tag(sysdep->udev_mon, tag) < 0) {
        ret = SENSOR_ERROR;
    }
    if (udev_monitor_filter_update(sysdep->udev_mon) < 0) {
        ret = SENSOR_ERROR;
    }

    return ret;
}

