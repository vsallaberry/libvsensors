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
 * common uitilities not showing any sensors - Generic Sensor Management Library.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>

#include "common_private.h"

/** function freeing a single common_event_t */
static void common_event_free(void * vevent) {
    common_event_t * event = (common_event_t *) vevent;
    if (event) {
        switch (event->type) {
        case CQT_DEVICE:
            if (event->u.dev.name)
                free(event->u.dev.name);            
            if (event->u.dev.type)
                free(event->u.dev.type);            
            break ;
        default:
            break ;
        }
        free(event);
    }
}

/** family-specific free */
static sensor_status_t family_free(sensor_family_t *family) {
    if (family->priv != NULL) {
        common_priv_t * priv = (common_priv_t *) family->priv;

        sysdep_common_destroy(family);
        
        if (priv->thread != NULL) {
            vthread_stop(priv->thread);
        }
        slist_free(priv->event_queue, common_event_free);
        
        family->priv = NULL;
        free(priv);
    }
    return SENSOR_SUCCESS;
}

/** family-specific init */
static sensor_status_t family_init(sensor_family_t *family) {
    common_priv_t * priv;
    
    // Sanity checks done before in sensor_init()
    if (family->priv != NULL) {
        LOG_ERROR(family->log, "error: %s data already initialized", family->info->name);
        return SENSOR_ERROR;
    }
    if ((priv = family->priv = calloc(1, sizeof(common_priv_t))) == NULL) {
        LOG_ERROR(family->log, "cannot allocate private %s data", family->info->name);
        family_free(family);
        return SENSOR_ERROR;
    }
    if ((priv->thread = vthread_create(0, family->log)) == NULL) {
        LOG_ERROR(family->log, "cannot create the %s thread", family->info->name);
        family_free(family);
        return SENSOR_ERROR;
    }
    if (pthread_mutex_init(&priv->mutex, NULL) != 0) {
        LOG_ERROR(family->log, "cannot initialize the %s mutex", family->info->name);
        family_free(family);
        return SENSOR_ERROR;
    }    
    if (sysdep_common_init(family) != SENSOR_SUCCESS) {
        LOG_ERROR(family->log, "cannot initialize system specific %s", family->info->name);
        family_free(family);
        return SENSOR_ERROR;
    }
    if (vthread_start(priv->thread) != 0) {
        return SENSOR_ERROR;
    }
    return SENSOR_SUCCESS;
}

/** family info definition */
const sensor_family_info_t g_sensor_family_common = {
    .name = "common",
    .init = family_init,
    .free = family_free,
    .update = NULL,
    .list = NULL,
    .notify = NULL,
    .write = NULL
};

/** get common event queue under lock 
  * when queue is not NULL, common_queue_release() must be called */
static common_queue_t common_queue_get(sensor_family_t * common_fam) {
    if (common_fam != NULL && common_fam->priv != NULL) {
        common_priv_t * priv = (common_priv_t *) common_fam->priv;
        if (priv->event_queue != NULL) {
            pthread_mutex_lock(&priv->mutex);
            if (priv->event_queue == NULL) {
                pthread_mutex_unlock(&priv->mutex);
                return NULL;
            }
            return priv->event_queue;
        }
    }
    return NULL;
}

/** set common event queue : (no lock done here)) */
static sensor_status_t common_queue_set(sensor_family_t * common_fam, common_queue_t queue) {
    if (common_fam != NULL && common_fam->priv != NULL) {
        common_priv_t * priv = (common_priv_t *) common_fam->priv;
        
        priv->event_queue = queue;        
        return SENSOR_SUCCESS;
    }
    return SENSOR_ERROR;
}

/** release common event queue given by common_queue_get() */
static sensor_status_t common_queue_release(sensor_family_t * common_fam) {
    if (common_fam != NULL && common_fam->priv != NULL) {
        common_priv_t * priv = (common_priv_t *) common_fam->priv;
        int             ret;       
        
        ret = pthread_mutex_unlock(&priv->mutex);
        return ret == 0 ? SENSOR_SUCCESS : SENSOR_ERROR;
    }
    return SENSOR_ERROR;
}

/** add an event to the common event queue
 *  event must be allocated and will be freed with common_event_free() */
sensor_status_t     sensor_common_queue_add(sensor_ctx_t * sctx, common_event_t * event) {
    sensor_family_t * common = sensor_family_common(sctx);
    
    if (common == NULL || common->priv == NULL || event == NULL) {
        return SENSOR_ERROR;
    }
    common_priv_t *     priv = (common_priv_t *) common->priv;
    sensor_status_t     ret;    
    
    pthread_mutex_lock(&priv->mutex);
    // LOCKED
    priv->event_queue = slist_append(priv->event_queue, event);
    ret = priv->event_queue != NULL ? SENSOR_SUCCESS : SENSOR_ERROR;
    // UNLOCKING
    pthread_mutex_unlock(&priv->mutex);
  
    return ret;
}

/** apply function to event queue events. the process function can return:
  * + SENSOR_NOT_SUPPORTED: event is skipped and kept
  * + SENSOR_ERROR: event skipped and kept, loop stopped
  * + SENSOR_SUCCESS: event is deleted from the queue */
sensor_status_t sensor_common_queue_process(
                    sensor_ctx_t * sctx,
                    sensor_status_t (*fun)(common_event_t * event, void * user_data),
                    void * user_data) {    
    sensor_family_t *   common = sensor_family_common(sctx);
    common_queue_t      queue; 
    sensor_status_t     ret = SENSOR_SUCCESS;
       
    // get common queue and LOCK IT
    queue = common_queue_get(common);   
     
    if (queue == NULL) {
        return SENSOR_SUCCESS;
    }
    
    LOG_SCREAM(common->log, "QUEUE size: %u", slist_length(queue));
    
    for (common_queue_t elt = queue, prev = NULL, to_free; elt != NULL; /* no incr */) {
        common_event_t *    event = elt->data;        
        
        LOG_SCREAM(common->log, "checking QUEUE: type %d, %s EVENT: %s", event->type, 
                   event->u.dev.action == CDA_ADD ? "add" : "remove", event->u.dev.name);
        
        ret = fun(event, user_data);
        if (ret == SENSOR_ERROR) {
            break ;
        } else if (ret == SENSOR_NOT_SUPPORTED) {
            ret = SENSOR_SUCCESS;
            prev = elt;
            elt = elt->next;
            continue ;
        }
                    
        if (prev == NULL) {
            queue = elt->next;    
        } else {
            prev->next = elt->next;
        }
        if (elt == queue) {
            prev = NULL;
        }
        to_free = elt;
        elt = elt->next;
        slist_free_1(to_free, common_event_free);                   
    }  
    common_queue_set(common, queue);  
    common_queue_release(common);
    return ret;
}

