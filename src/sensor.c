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
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fnmatch.h>
#include <pthread.h>
#include <math.h>
#include <stdint.h>
#include <inttypes.h>

#include "vlib/options.h"
#include "vlib/util.h"
#include "vlib/time.h"
#include "vlib/slist.h"
#include "vlib/logpool.h"
#include "vlib/avltree.h"

#include "libvsensors/sensor.h"

#include "version.h"

#include "smc.h"
#include "memory.h"
#include "disk.h"
#include "file.h"
#include "network.h"
#include "cpu.h"
#include "power.h"
#include "sensor_private.h"

/* locking */
#define SENSOR_LOCK_LOCK(_sctx)     (pthread_mutex_lock(&((_sctx)->mutex)) \
                                        == 0 ? SENSOR_SUCCESS : SENSOR_ERROR)

#define SENSOR_LOCK_UNLOCK(_sctx)   (pthread_mutex_unlock(&((_sctx)->mutex)) \
                                        == 0 ? SENSOR_SUCCESS : SENSOR_ERROR)

#define SENSOR_READ_LOCK(_sctx)     (pthread_rwlock_rdlock(&((_sctx)->rwlock)) \
                                        == 0 ? SENSOR_SUCCESS : SENSOR_ERROR)
#define SENSOR_WRITE_LOCK(_sctx)    (pthread_rwlock_wrlock(&((_sctx)->rwlock)) \
                                        == 0 ? SENSOR_SUCCESS : SENSOR_ERROR)
#define SENSOR_UNLOCK(_sctx)        (pthread_rwlock_unlock(&((_sctx)->rwlock)) \
                                        == 0 ? SENSOR_SUCCESS : SENSOR_ERROR)
#define SENSOR_NO_WRITE_OWNER       ((pthread_t) ((void*) -1L))

/* sensor names */
#define SENSOR_LABEL_SIZE           256
#define SENSOR_VALUE_BYTES_WORKSZ   512

#define SENSOR_FAM_NAME(_family)    ((_family)->info->name)

#define SENSOR_DESC_LABEL(_desc)    (STR_CHECKNULL((_desc)->label))

#define SENSOR_DESC_FAMNAME(_desc)  (SENSOR_FAM_NAME((_desc)->family))

#define SENSOR_PROP_NAME(_prop)     (STR_CHECKNULL((_prop)->name))

/* ************************************************************************ */
static const sensor_family_info_t * s_families_info[] = {
    &g_sensor_family_common,
    &g_sensor_family_cpu,
    &g_sensor_family_memory,
    &g_sensor_family_network,
    &g_sensor_family_disk,
    &g_sensor_family_file,
    &g_sensor_family_power,
    &g_sensor_family_smc,
    NULL
};

typedef enum {    
    SPF_FREE_LOGPOOL    = SIF_RESERVED << 0,
} sensors_priv_flag_t;

struct sensor_ctx_s {
    sensor_family_t *   common;
    slist_t *           families;
    slist_t *           sensorlist;
    slist_t *           watchlist;
    avltree_t *         watch_params;
    avltree_t *         watchs;
    avltree_t *         sensors;
    avltree_t *         properties;
    unsigned int        flags;
    log_t *             log;
    logpool_t *         logpool;
    pthread_rwlock_t    rwlock;
    pthread_mutex_t     mutex;
    pthread_t           write_lock_owner;
    int                 write_lock_counter;
    pthread_cond_t      cond;
    sensor_value_t      work_buffer;
    sensor_value_t      work_value;
    sensor_watch_ev_data_t * evdata;
};

typedef struct {
    sensor_watch_t  watch;
    int             use_count;
} sensor_watchparam_entry_t;

typedef struct {
    const sensor_desc_t *   desc;
    sensor_property_t *     property;
} sensor_propentry_t;

/* ************************************************************************ */
static const char * s_sensor_loading_label = "...";
#define SENSOR_LOADING_ID_TYPE      SENSOR_VALUE_UINT
typedef struct {
    const char *                                pattern;
    SENSOR_VALUE_TYPE_X(SENSOR_LOADING_ID_TYPE) id;
} sensor_loadinginfo_t; /* type of sensor_desc_t.key */
static sensor_property_t s_sensor_loading_properties[] = {
    { "flags", { .type = SENSOR_LOADING_ID_TYPE,
                 .data. SENSOR_VALUE_NAME_X(SENSOR_LOADING_ID_TYPE) = 0, } },
    { NULL, { .type = SENSOR_VALUE_NULL, } }
};

/* ************************************************************************ */
static sensor_status_t sensor_list_build(sensor_ctx_t *sctx);


/* ************************************************************************
 * TREES :  COMPARISON FUNCTIONS
 * ************************************************************************ */

/* ************************************************************************ */
static int sensorwatchparam_cmp(const void * vw1, const void * vw2) {
    sensor_watchparam_entry_t * w1 = (sensor_watchparam_entry_t *) vw1;
    sensor_watchparam_entry_t * w2 = (sensor_watchparam_entry_t *) vw2;
    long ret;

    if ((ret = (long)(w1->watch.update_interval.tv_sec
                      - w2->watch.update_interval.tv_sec)) != 0) {
        return ret;
    }
    if ((ret = (long)(w1->watch.update_interval.tv_usec
                      - w2->watch.update_interval.tv_usec)) != 0) {
        return ret;
    }
    if ((ret = memcmp(&(w1->watch.callback), &(w2->watch.callback),
                      sizeof(sensor_watch_callback_t))) != 0) {
        return ret;
    }
    for (unsigned int i = 0; i < SENSOR_LEVEL_NB; ++i) {
        if ((ret = w1->watch.update_levels[i].type
                 - w2->watch.update_levels[i].type) != 0) {
            return ret;
        }
        if ((ret = sensor_value_compare(&(w1->watch.update_levels[i]),
                                        &(w2->watch.update_levels[i]))) != 0) {
            return ret;
        }
    }
    return 0;
}

/* ************************************************************************ */
#if 0
static int sensorwatch_timercmp(const void * vw1, const void * vw2) {
    sensor_sample_t * w1 = (sensor_sample_t *) vw1;
    sensor_sample_t * w2 = (sensor_sample_t *) vw2;
    if (w1->update_interval.tv_sec == w2->update_interval.tv_sec)
        return (w1->watch->update_interval.tv_usec - w2->watch->update_interval.tv_usec);
    return (w1->watch->update_interval.tv_sec - w2->watch->update_interval.tv_sec);
}
#endif

/* ************************************************************************ */
static int sensordesc_alphacmp(const void * vd1, const void * vd2) {
    sensor_desc_t * d1 = (sensor_desc_t *) vd1;
    sensor_desc_t * d2 = (sensor_desc_t *) vd2;

    if (d1 == d2) {
        return 0;
    } else {
        int ret;
        const char * l1, * l2;

        if ((ret = strcasecmp(SENSOR_DESC_FAMNAME(d1), SENSOR_DESC_FAMNAME(d2))) != 0)
            return ret;

        if (d1->label == d2->label)
           return 0;

        l1 = SENSOR_DESC_LABEL(d1);
        l2 = SENSOR_DESC_LABEL(d2);

        ret = strcasecmp(l1, l2);
        /* trick to differentiate a search and insert/removal
         * if desc->family->sctx is NULL, we rely on string comparison for equality & order
         * if sctx not NULL, we rely on string comparison for order
         *                           and on desc ptr equality for equality
         */
        if (ret == 0 && d1 != d2) {
            if (d1->family->sctx != NULL && d2->family->sctx != NULL)
                return d1 - d2;
        }
        return ret;
    }
}

/* ************************************************************************ */
static int sensorwatch_alphacmp(const void * vw1, const void * vw2) {
    sensor_sample_t *   w1 = (sensor_sample_t *) vw1;
    sensor_sample_t *   w2 = (sensor_sample_t *) vw2;
    int                 ret;

    if (w1 == w2) {
        return 0;
    } else {
        ret = sensordesc_alphacmp(w1->desc, w2->desc);
        /* trick to differentiate a search and insert/removal
         * if sample->watch is NULL, we rely on string comparison for order & equality
         * if watch not NULL, we rely on string comparison for order
         *                            and on sample ptr equality for equality
         */
        if (ret == 0 && w1 != w2) {
            if (w1->watch != NULL && w2->watch != NULL)
                return w1 - w2;
            if (w1->desc->family->sctx != NULL && w2->desc->family->sctx != NULL)
                return w1->desc - w2->desc;
        }
        return ret;
    }
}

/* ************************************************************************ */
static int sensorprop_alphacmp(const void * vp1, const void * vp2) {
    sensor_propentry_t * p1 = (sensor_propentry_t *) vp1;
    sensor_propentry_t * p2 = (sensor_propentry_t *) vp2;

    if (p1 == p2) {
        return 0;
    } else if (p1->property == p2->property) {
        return 0;
    } else if (p1->property == NULL || p2->property == NULL) {
        return p1->property - p2->property;
    } else {
        char    label1[SENSOR_LABEL_SIZE*2];
        char    label2[SENSOR_LABEL_SIZE*2];
        int     ret;

        snprintf(label1, sizeof(label1) / sizeof(*label1), "%s/%s/%s",
                 SENSOR_DESC_FAMNAME(p1->desc),
                 SENSOR_DESC_LABEL(p1->desc),
                 SENSOR_PROP_NAME(p1->property));

        snprintf(label2, sizeof(label2) / sizeof(*label2), "%s/%s/%s",
                 SENSOR_DESC_FAMNAME(p2->desc),
                 SENSOR_DESC_LABEL(p2->desc),
                 SENSOR_PROP_NAME(p2->property));

        ret = strcasecmp(label1, label2);
        if (ret == 0 && p1 != p2
            && (p1->property->value.type != SENSOR_VALUE_NB
                && p2->property->value.type != SENSOR_VALUE_NB)) {
            /* if prop.value.type are not SENSOR_VALUE_NB, require ptr equality) */
            return p1 - p2;
        }
        return ret;
    }
}

/* ************************************************************************
 * TREES : FREEING FUNCTIONS
 * ************************************************************************ */

/* ************************************************************************ */
static void sensor_watch_free_one(void * vdata) {
    sensor_sample_t * sensor = (sensor_sample_t *) vdata;
    if (SENSOR_VALUE_IS_BUFFER(sensor->value.type)
    &&  sensor->value.data.b.maxsize > 0 && sensor->value.data.b.buf != NULL) {
        free(sensor->value.data.b.buf);
    }
    if (sensor->userfreefun != NULL) {
        sensor->userfreefun(sensor);
    }
    free(sensor);
}

/* ************************************************************************ */
static void sensor_desc_free_one(void * vdata) {
    sensor_desc_t * sensor = (sensor_desc_t *) vdata;
    if (sensor->properties == s_sensor_loading_properties) {
        if (sensor->label != NULL)
            free((void *) (sensor->label));
        if (sensor->key != NULL) {
            sensor_loadinginfo_t * info = (sensor_loadinginfo_t *) sensor->key;
            if (info->pattern != NULL)
                free((void*) info->pattern);
            free(info);
        }
        free(sensor);
    } else if (sensor->label == s_sensor_loading_label) {
        free(sensor);
    }
}


/* ************************************************************************
 * SENSOR / FAMILY INIT/FREE FUNCTIONS
 * ************************************************************************ */

/* ************************************************************************ */
static sensor_status_t sensor_family_free(sensor_family_t * fam, sensor_ctx_t * sctx) {
    sensor_status_t ret = SENSOR_SUCCESS;
    
    if (fam == NULL) {
        return SENSOR_ERROR;
    }    
    if (fam->info->free && fam->info->free(fam) != SENSOR_SUCCESS) {
        LOG_ERROR(sctx->log, "sensor family %s cannot be freed", fam->info->name);
        ret = SENSOR_NOT_SUPPORTED;
    }
    logpool_release(sctx->logpool, fam->log);
    free(fam);
    return ret;
}

/* ************************************************************************ */
static sensor_status_t sensor_family_register_unlocked(
                            sensor_ctx_t *              sctx,
                            const sensor_family_info_t *fam_info,
                            sensor_family_t **          p_fam) {
    sensor_family_t *   fam;
    sensor_status_t     ret;

    /* sanity check on family_info_t * and its name */
    if (fam_info == NULL || fam_info->name == NULL) {
        return SENSOR_ERROR ;
    }
    if ((fam = calloc(1, sizeof(sensor_family_t))) == NULL) {
        LOG_WARN(sctx->log, "sensor family %s cannot be allocated", fam_info->name);
        return SENSOR_ERROR;
    }
    fam->sctx = sctx;
    fam->info = fam_info;
    fam->log = logpool_getlog(sctx->logpool, fam->info->name, LPG_TRUEPREFIX);

    /* run family init() if provided */
    if (fam->info->init && (ret = fam->info->init(fam)) != SENSOR_SUCCESS) {
        if (ret == SENSOR_NOT_SUPPORTED)
            LOG_INFO(sctx->log, "%s sensors not supported on this system", fam->info->name);
        else
            LOG_ERROR(sctx->log, "sensor family %s cannot be initialized", fam->info->name);
        logpool_release(sctx->logpool, fam->log);
        free(fam);
        return ret;
    }

    /* finally register family */
    if ((sctx->families = slist_prepend(sctx->families, fam)) == NULL) {
        LOG_ERROR(sctx->log, "sensor family %s cannot be registered", fam->info->name);
        sensor_family_free(fam, sctx);
        return SENSOR_ERROR;
    }

    if (sctx->common == NULL && fam_info == &g_sensor_family_common) {
        sctx->common = fam;
    }
    
    LOG_INFO(sctx->log, "%s: loaded.", fam->info->name);
    if (p_fam != NULL)
        *p_fam = fam;

    return SENSOR_SUCCESS;
}

/* ************************************************************************ */
sensor_ctx_t * sensor_init(logpool_t * logs, unsigned int flags) {
    size_t          nb_fam  = sizeof(s_families_info) / sizeof(*s_families_info);
    sensor_ctx_t *  sctx;

    /* alloc main context */
    if ((sctx = calloc(1, sizeof(sensor_ctx_t))) == NULL) {
        return NULL;
    }
    sctx->flags = (flags & (SIF_RESERVED-1));

    /* alloc mutexes */
    if (pthread_rwlock_init(&(sctx->rwlock), NULL) != 0
    ||  (( pthread_mutex_init(&(sctx->mutex), NULL) != 0
           ||  ((pthread_cond_init(&(sctx->cond), NULL) != 0)
                && (pthread_mutex_destroy(&(sctx->mutex)) | 1)))
         && (pthread_rwlock_destroy(&(sctx->rwlock)) | 1))) {
        free(sctx);
        return NULL;
    }
    sctx->write_lock_owner = SENSOR_NO_WRITE_OWNER;
    sctx->write_lock_counter = 0;

    /* alloc sensor_value work buffer (for sensor_update_check) */
    if ((sctx->work_buffer.data.b.buf = calloc(1, SENSOR_VALUE_BYTES_WORKSZ)) == NULL
    ||  (sctx->evdata = malloc(sizeof(*(sctx->evdata)))) == NULL) {
        LOG_WARN(sctx->log, "%s(): cannot malloc sensor work buffer: %s",
                 __func__, strerror(errno));
        sensor_free(sctx);
        return NULL;
    }
    sctx->work_buffer.type = SENSOR_VALUE_BYTES;
    sctx->work_buffer.data.b.size = 0;
    sctx->work_buffer.data.b.maxsize = SENSOR_VALUE_BYTES_WORKSZ;

    /* init logpool / log */
    if (logs == NULL) {
        sctx->flags |= SPF_FREE_LOGPOOL;
        logs = logpool_create();
    }
    sctx->logpool = logs;
    sctx->log = logpool_getlog(logs, SENSOR_LOG_PREFIX, LPG_TRUEPREFIX);

    /* create trees */
    sctx->watch_params  = avltree_create(AFL_DEFAULT | AFL_SHARED_STACK
                                         | AFL_INSERT_IGNDOUBLE,
                                         sensorwatchparam_cmp, free);
    sctx->watchs        = avltree_create((AFL_DEFAULT & ~(AFL_SHARED_STACK)),
                                         sensorwatch_alphacmp, NULL);
    sctx->sensors       = avltree_create((AFL_DEFAULT & ~(AFL_SHARED_STACK)),
                                         sensordesc_alphacmp, NULL);
    sctx->properties    = avltree_create((AFL_DEFAULT & ~(AFL_SHARED_STACK)),
                                         sensorprop_alphacmp, free);

    if (sctx->logpool == NULL || sctx->log == NULL || sctx->watch_params == NULL
    ||  sctx->watchs == NULL || sctx->sensors == NULL || sctx->properties == NULL) {
        LOG_ERROR(sctx->log, "sensors initialization failed");
        sensor_free(sctx);
        return NULL;
    }
    sctx->watchs->shared = sctx->watch_params->shared;
    sctx->sensors->shared = sctx->watch_params->shared;
    sctx->properties->shared = sctx->watch_params->shared;

    /* init sensor_value info cache */
    sensor_value_info_init();

    /* init families */
    for (unsigned i_fam = 0; i_fam < nb_fam && s_families_info[i_fam]; i_fam++) {
        const sensor_family_info_t *    fam_info    = s_families_info[i_fam];

        sensor_family_register_unlocked(sctx, fam_info, NULL);
    }

    /* build sensor_list */
    sensor_list_build(sctx);

    /* end init */
    return sctx;
}

/* ************************************************************************ */
#ifdef _DEBUG
static int sensor_watch_printnode(FILE * out, const avltree_node_t * node) {
    char fam[4];
    char lab[8];
    sensor_sample_t * sample = (sensor_sample_t *) node->data;

    snprintf(fam, sizeof(fam)/sizeof(*fam), "%s",
             SENSOR_DESC_FAMNAME(sample->desc));
    snprintf(lab, sizeof(lab)/sizeof(*lab), "%s",
             SENSOR_DESC_LABEL(sample->desc));

    return fprintf(out, "%s/%s", fam, lab);
}
#endif

/* ************************************************************************ */
static int sensor_checktree_desc(
                slist_t ** plist, void * tree_elt, void * list_elt, const char * name,
                const sensor_desc_t * tree_desc, const sensor_desc_t * list_desc,
                void * list_next, const sensor_desc_t * list_desc_next) {
    log_t *             log       = tree_desc != NULL ? tree_desc->family->sctx->log
                                  : (list_desc != NULL ? list_desc->family->sctx->log : NULL);
    sensor_family_t testfamily;
    sensor_desc_t   testdesc;

    if (log == NULL || tree_elt == NULL || list_elt == NULL
    ||  tree_desc == NULL || list_desc == NULL || plist == NULL || *plist == NULL) {
        if (log != NULL) {
            LOG_VERBOSE(log, "%s(): log(%lx) or tree_%s(%lx) or list_%s(%lx) "
                        "or descs or plist/*plist NULL", __func__, (unsigned long) log,
                        name, (unsigned long) tree_elt, name, (unsigned long) list_elt);
        }
        return AVS_ERROR;
    }
    if (tree_elt == list_elt && tree_desc == list_desc) {
        LOG_SCREAM(log, "%s(): ok. ptr equality", __func__);
    }
    testfamily = *(tree_desc->family);
    testdesc = *(tree_desc);
    testdesc.family = &testfamily;
    testdesc.family->sctx = NULL;
    if (sensordesc_alphacmp(list_desc, &testdesc) != 0) {
        LOG_VERBOSE(log, "%s(): FAILED: sensordesc_alphacmp(tree, list) not 0"
                         "'%s/%s' & '%s/%s'", __func__,
                    SENSOR_DESC_FAMNAME(&testdesc), SENSOR_DESC_LABEL(&testdesc),
                    SENSOR_DESC_FAMNAME(list_desc), SENSOR_DESC_LABEL(list_desc));
        return AVS_ERROR;
    }
    if (list_next != NULL && list_desc_next != NULL) {
        char label1[SENSOR_LABEL_SIZE], label2[SENSOR_LABEL_SIZE];
        snprintf(label1, PTR_COUNT(label1), "%s/%s",
                 SENSOR_DESC_FAMNAME(list_desc), SENSOR_DESC_LABEL(list_desc));
        snprintf(label2, PTR_COUNT(label2), "%s/%s",
                 SENSOR_DESC_FAMNAME(list_desc_next),
                 SENSOR_DESC_LABEL(list_desc_next));
        if (strcasecmp(label1, label2) > 0) {
            LOG_VERBOSE(log, "%s(): FAILED: strcasecmp(list_desc, list_desc_next): '%s' > '%s'",
                        __func__, label1, label2);
            return AVS_ERROR;
        } else {
            LOG_SCREAM(log, "%s(): ok strcasecmp(list_desc, list_desc_next): '%s' <= '%s'",
                       __func__, label1, label2);
        }
    }
    LOG_SCREAM(log, "%s(): ok: tree_%s(%s/%s) == list_%s (%s)", __func__, name,
               SENSOR_DESC_FAMNAME(tree_desc), SENSOR_DESC_LABEL(tree_desc),
               name, tree_elt == list_elt ? "PTR" : "sensordesc_alphacmp");
    (*plist) = (*plist)->next;
    return AVS_CONTINUE;
}

/* ************************************************************************ */
static AVLTREE_DECLARE_VISITFUN(sensor_checkdesc_visit, tree, node, context, vdata) {
    sensor_desc_t *   tree_desc = (sensor_desc_t *) node->data;
    slist_t **        plist     = (slist_t **) vdata;
    sensor_desc_t *   list_desc = plist && *plist ? (sensor_desc_t *) (*plist)->data : NULL;
    sensor_desc_t *   list_next = plist != NULL && *plist != NULL && (*plist)->next != NULL
                                  ? (sensor_desc_t *) (*plist)->next->data : NULL;
    /*log_t *           log       = tree_desc != NULL ? tree_desc->family->sctx->log
                                  : (list_desc != NULL ? list_desc->family->sctx->log : NULL);*/
    (void) tree;
    (void) context;

    return sensor_checktree_desc(plist, tree_desc, list_desc, "desc",
                                 tree_desc, list_desc, list_next, list_next);
}

/* ************************************************************************ */
static AVLTREE_DECLARE_VISITFUN(sensor_checkwatch_visit, tree, node, context, vdata) {
    sensor_sample_t * tree_sample   = (sensor_sample_t *) node->data;
    slist_t **        plist         = (slist_t **) vdata;
    sensor_sample_t * list_sample   = plist && *plist ? (sensor_sample_t *) (*plist)->data : NULL;
    sensor_sample_t * next_sample   = plist != NULL && *plist != NULL && (*plist)->next != NULL
                                      ? (sensor_sample_t *) (*plist)->next->data : NULL;
    /*log_t *           log           = tree_sample != NULL ? tree_sample->desc->family->sctx->log
                            : (list_sample != NULL ? list_sample->desc->family->sctx->log : NULL);*/
    (void) tree;
    (void) context;

    return sensor_checktree_desc(plist, tree_sample, list_sample, "sample",
                                 tree_sample != NULL ? tree_sample->desc : NULL,
                                 list_sample != NULL ? list_sample->desc : NULL,
                                 next_sample, next_sample != NULL ? next_sample->desc : NULL);
}

/* ************************************************************************ */
sensor_status_t sensor_free(sensor_ctx_t * sctx) {
    size_t n_fam;
    slist_t * checktree, * checkhead;

    if (sctx == NULL) {
        return SENSOR_ERROR;
    }
    sensor_lock(sctx, SENSOR_LOCK_WRITE);
    sensor_unlock(sctx);

    n_fam = slist_length(sctx->families);
    LOG_VERBOSE(sctx->log, "%s(): "
              "%zu familie%s (>%zu bytes), %zu sensor%s (>%zu bytes), %zu watch%s (>%zu bytes).",
              __func__, n_fam, n_fam > 1 ? "s": "",
              n_fam * (sizeof(sensor_family_t) + sizeof(sensor_family_info_t)),
              avltree_count(sctx->sensors), avltree_count(sctx->sensors) > 1 ? "s": "",
              avltree_memorysize(sctx->sensors)
                + avltree_count(sctx->sensors) * (sizeof(sensor_desc_t)),
              avltree_count(sctx->watchs), avltree_count(sctx->watchs) > 1 ? "s": "",
              avltree_memorysize(sctx->watchs)
                + avltree_count(sctx->watchs) * (sizeof(sensor_sample_t)));
    LOG_VERBOSE(sctx->log, "%s(): %zu propert%s (>%zu bytes), %zu watch-param%s (%zu bytes), ",
              __func__,
               avltree_count(sctx->properties), avltree_count(sctx->properties) > 1 ? "ies": "y",
              avltree_memorysize(sctx->properties)
                + avltree_count(sctx->properties) * (sizeof(sensor_propentry_t)),
              avltree_count(sctx->watch_params), avltree_count(sctx->watch_params) > 1 ? "s": "",
              avltree_memorysize(sctx->watch_params)
                + avltree_count(sctx->watch_params) * (sizeof(sensor_watchparam_entry_t)));
    #ifdef _DEBUG
    if (LOG_CAN_LOG(sctx->log, LOG_LVL_DEBUG)) {
        size_t ns, nw;
        ns = slist_length(sctx->sensorlist);
        nw = slist_length(sctx->watchlist);
        LOG_DEBUG(sctx->log, "%s(): %zu in sensorlist(%zu bytes), %zu in watchlist (%zu bytes)",
                  __func__, ns, ns * sizeof(slist_t), nw, nw * sizeof(slist_t));
        if (LOG_CAN_LOG(sctx->log, LOG_LVL_SCREAM)) {
            avltree_print(sctx->watchs, sensor_watch_printnode, sctx->log->out);
        }
    }
    #endif

    /* check watchs tree */
    checktree = sctx->watchlist;
    if (avltree_visit(sctx->watchs, sensor_checkwatch_visit,
                      &checktree, AVH_INFIX) != AVS_FINISHED) {
        LOG_WARN(sctx->log, "%s(): warning the watchs tree is messed up", __func__);
    } else {
        LOG_VERBOSE(sctx->log, "%s(): watchs tree is valid.", __func__);
    }

    /* check sensors tree (sensorlist is not sorted, do it) */
    checktree = NULL;
    SLIST_FOREACH_DATA(sctx->sensorlist, desc, sensor_desc_t *) {
        checktree = slist_insert_sorted(checktree, desc, sensordesc_alphacmp);
    }
    checkhead = checktree;
    if (avltree_visit(sctx->sensors, sensor_checkdesc_visit,
                      &checktree, AVH_INFIX) != AVS_FINISHED) {
        LOG_WARN(sctx->log, "%s(): warning the sensors tree is messed up", __func__);
    } else {
        LOG_VERBOSE(sctx->log, "%s(): sensors tree is valid.", __func__);
    }
    slist_free(checkhead, NULL);

    /* free trees */
    avltree_free(sctx->watchs);
    avltree_free(sctx->properties);
    avltree_free(sctx->sensors);
    avltree_free(sctx->watch_params); /* must be last tree to be freed: it is owning stack*/
    sctx->watchs = sctx->properties = sctx->sensors = sctx->watch_params = NULL;

    /* free lists */
    sensor_watch_free(sctx);
    sensor_list_free(sctx);

    /* free families */
    SLIST_FOREACH_DATA(sctx->families, fam, sensor_family_t *) {
        if (fam != sctx->common)
            sensor_family_free(fam, sctx); // free common at the end
    }
    sensor_family_free(sctx->common, sctx);
    slist_free(sctx->families, NULL);

    /* release / free logs & logpool */
    logpool_release(sctx->logpool, sctx->log);
    if ((sctx->flags & SPF_FREE_LOGPOOL) != 0) {
        logpool_free(sctx->logpool);
    }

    /* free work buffers */
    if (sctx->work_buffer.data.b.buf != NULL) {
        free(sctx->work_buffer.data.b.buf);
    }
    if (sctx->evdata != NULL) {
        free(sctx->evdata);
    }

    /* free mutexes */
    pthread_rwlock_destroy(&(sctx->rwlock));
    pthread_cond_destroy(&(sctx->cond));
    pthread_mutex_destroy(&(sctx->mutex));

    /* end free */
    free(sctx);
    return SENSOR_SUCCESS;
}

/* ************************************************************************ */
static sensor_status_t sensor_family_list_sensors(
                            sensor_family_t *       fam,
                            slist_t **              p_last) {
    sensor_ctx_t *sctx = fam->sctx;
    slist_t * last;
    if (p_last == NULL) {
        last = NULL;
        if (sctx->sensorlist != NULL) {
            /* go to last and remove the sensors of given family */
            for (slist_t * list = sctx->sensorlist; list != NULL; /*no_incr*/) {
                sensor_desc_t * sensor = (sensor_desc_t *) (list->data);
                if (sensor->family == fam) {
                    slist_t * to_free = list;

                    /* remove sensor properties from tree */
                    for (sensor_property_t * property = sensor->properties;
                           SENSOR_PROPERTY_VALID(property); ++property ) {
                        sensor_propentry_t prop;
                        prop.property = property;
                        prop.desc = sensor;
                        if (avltree_remove(sctx->properties, &prop) == NULL) {
                            LOG_WARN(sctx->log,
                                     "cannot remove property '%s/%s/%s' from the tree",
                                     SENSOR_FAM_NAME(fam), SENSOR_DESC_LABEL(sensor),
                                     SENSOR_PROP_NAME(property));
                        }
                    }
                    /* remove sensor from tree */
                    if (avltree_remove(sctx->sensors, sensor) != sensor) {
                        LOG_WARN(sctx->log, "cannot remove sensor '%s/%s' from the tree",
                                 SENSOR_FAM_NAME(fam), SENSOR_DESC_LABEL(sensor));
                    }
                    /* remove from list */
                    list = list->next;
                    if (last == NULL) {
                        sctx->sensorlist = list;
                    } else {
                        last->next = list;
                    }
                    slist_free_1(to_free, sensor_desc_free_one);
                } else {
                    last = list;
                    list = list->next;
                }
            }
        }
        p_last = &last;
    }
    if (fam->info->list != NULL) {
        slist_t * famlist = fam->info->list(fam);
        while (famlist != NULL) {
            sensor_desc_t * sensor = (sensor_desc_t *) famlist->data;
            /* check each sensor before adding it to list */
            if (sensor != NULL) {
                /* checks done: adding it */
                if (sensor->family == NULL)
                    sensor->family = fam;
                /* adding it in the tree */
                if (avltree_insert(sctx->sensors, sensor) != sensor) {
                    LOG_WARN(sctx->log, "cannot add sensor '%s/%s' in the tree",
                             SENSOR_FAM_NAME(fam), SENSOR_DESC_LABEL(sensor));
                }
                /* adding its properties in the tree */
                for (sensor_property_t * property = sensor->properties;
                        SENSOR_PROPERTY_VALID(property); ++property ) {
                    sensor_propentry_t * prop;
                    if ((prop = malloc(sizeof(*prop))) == NULL) {
                        LOG_WARN(sctx->log, "cannot malloc property tree entry !");
                        break ;
                    }
                    prop->property = property;
                    prop->desc = sensor;
                    if (avltree_insert(sctx->properties, prop) == NULL) {
                        LOG_WARN(sctx->log,
                                 "cannot add property '%s/%s/%s' to the tree",
                                 SENSOR_FAM_NAME(fam),
                                 SENSOR_DESC_LABEL(sensor), SENSOR_PROP_NAME(property));
                    }
                }

                /* adding it in the list */
                if (*(p_last) == NULL) {
                    *(p_last) = sctx->sensorlist = famlist;
                } else {
                    (*(p_last))->next = famlist;
                    *(p_last) = famlist;
                }
                famlist = famlist->next;

            } else {
                slist_t * to_free = famlist;
                LOG_WARN(sctx->log, "ignoring sensor '%s/%s': wrong data",
                         SENSOR_FAM_NAME(fam),
                         sensor == NULL ? STR_NULL : SENSOR_DESC_LABEL(sensor));
                famlist = famlist->next;
                slist_free_1(to_free, sensor_desc_free_one);
            }
        }
        if (*(p_last) != NULL)
            (*(p_last))->next = NULL;
        return SENSOR_SUCCESS;
    }
    return SENSOR_NOT_SUPPORTED;
}

/* ************************************************************************ */
sensor_status_t sensor_family_register(
                        sensor_ctx_t *                  sctx,
                        const sensor_family_info_t *    fam_info) {
    sensor_status_t     ret;
    sensor_family_t *   fam;

    if (sctx == NULL) {
        return SENSOR_ERROR;
    }

    sensor_lock(sctx, SENSOR_LOCK_WRITE);
    ret = sensor_family_register_unlocked(sctx, fam_info, &fam);
    /* if sensor_list is already initialized, new family sensors must be addee to it */
    if (ret == SENSOR_SUCCESS && sctx->sensorlist != NULL) {
        sensor_family_list_sensors(fam, NULL);
    }
    sensor_unlock(sctx);

    return ret;
}

sensor_family_t *   sensor_family_common(sensor_ctx_t * sctx) {
    return sctx ? sctx->common : NULL;
}

sensor_status_t sensor_family_signal(sensor_family_t * family) {
    int ret;
    SENSOR_LOCK_LOCK(family->sctx);
    ret = pthread_cond_signal(&family->sctx->cond);
    SENSOR_LOCK_UNLOCK(family->sctx);
    return ret == 0 ? SENSOR_SUCCESS : SENSOR_ERROR;
}

sensor_status_t sensor_family_wait(sensor_family_t * family) {
    int ret;
    SENSOR_LOCK_LOCK(family->sctx);
    ret = pthread_cond_wait(&family->sctx->cond, &family->sctx->mutex);
    SENSOR_LOCK_UNLOCK(family->sctx);
    sched_yield();
    return ret == 0 ? SENSOR_SUCCESS : SENSOR_ERROR;
}

/* ************************************************************************
 * SENSOR LOCKING FUNCTIONS
 * ************************************************************************ */

/* ************************************************************************ */
/** upgrade the current lock to WRITE / undefined behavior if not locked */
static sensor_status_t sensor_lock_upgrade(sensor_ctx_t * sctx) {
    pthread_t       self = pthread_self();
    sensor_status_t res;

    if (sctx == NULL) {
        return SENSOR_ERROR;
    }

    SENSOR_LOCK_LOCK(sctx);
    /* it is ok to read write_lock_* because we are under Lock Lock */
    if (sctx->write_lock_counter > 0) {
        if (sctx->write_lock_owner == self) {
            SENSOR_LOCK_UNLOCK(sctx);
            return SENSOR_SUCCESS;
        } else {
            /* under read lock, the counter not 0 means another thread has upgraded */
            SENSOR_LOCK_UNLOCK(sctx);
            return sensor_lock(sctx, SENSOR_LOCK_WRITE);
        }
    }
    sctx->write_lock_counter = 1;
    sctx->write_lock_owner = self;
    SENSOR_LOCK_UNLOCK(sctx);
    SENSOR_UNLOCK(sctx);

    res = SENSOR_WRITE_LOCK(sctx);
    return res;
}

/* ************************************************************************ */
sensor_status_t sensor_lock(sensor_ctx_t * sctx, sensor_lock_type_t lock_type) {
    sensor_status_t res;
    pthread_t       self = pthread_self();

    if (sctx == NULL) {
        return SENSOR_ERROR;
    }

    SENSOR_LOCK_LOCK(sctx);
    if (sctx->write_lock_counter > 0 && sctx->write_lock_owner == self) {
        /* owner of write lock is allowed to acquire any recursive lock */
        ++(sctx->write_lock_counter);
        SENSOR_LOCK_UNLOCK(sctx);
        return SENSOR_SUCCESS;
    }

    while (1) {
        SENSOR_LOCK_UNLOCK(sctx);
        if (lock_type == SENSOR_LOCK_WRITE) {
            res = SENSOR_WRITE_LOCK(sctx);
            SENSOR_LOCK_LOCK(sctx);
            if (res == SENSOR_SUCCESS && sctx->write_lock_counter == 0) {
                sctx->write_lock_owner = self;
                sctx->write_lock_counter = 1;
                SENSOR_LOCK_UNLOCK(sctx);
                return SENSOR_SUCCESS;
            }
        } else {
            res = SENSOR_READ_LOCK(sctx);
            SENSOR_LOCK_LOCK(sctx);
            if (res == SENSOR_SUCCESS && sctx->write_lock_counter == 0) {
                SENSOR_LOCK_UNLOCK(sctx);
                return SENSOR_SUCCESS;
            }
        }
        if (res == SENSOR_SUCCESS)
            SENSOR_UNLOCK(sctx);
    }
    return SENSOR_ERROR;
}

/* ************************************************************************ */
sensor_status_t sensor_unlock(sensor_ctx_t * sctx) {
    sensor_status_t res;

    if (sctx == NULL) {
        return SENSOR_ERROR;
    }
    SENSOR_LOCK_LOCK(sctx);
    if (sctx->write_lock_counter > 0 && sctx->write_lock_owner == pthread_self()) {
        if (--(sctx->write_lock_counter) == 0) {
            sctx->write_lock_owner = SENSOR_NO_WRITE_OWNER;
        } else {
            SENSOR_LOCK_UNLOCK(sctx);
            return SENSOR_SUCCESS;
        }
    }
    res = SENSOR_UNLOCK(sctx);
    SENSOR_LOCK_UNLOCK(sctx);
    return res;
}

/* ************************************************************************ */
static sensor_status_t sensor_list_build(sensor_ctx_t *sctx) {
    slist_t * list = NULL;

    if (sctx->sensorlist != NULL) {
        return SENSOR_UNCHANGED;
    }
    SLIST_FOREACH_DATA(sctx->families, fam, sensor_family_t *) {
        sensor_family_list_sensors(fam, &list);
    }
    return SENSOR_SUCCESS;
}

/* ************************************************************************ */
const slist_t * sensor_list_get(sensor_ctx_t *sctx) {
    const slist_t * result;

    if (sctx == NULL) {
        return NULL;
    }
    result = sctx->sensorlist;

    return result;
}

/* ************************************************************************ */
void sensor_list_free(sensor_ctx_t *sctx) {
    if (sctx == NULL) {
        return ;
    }
    sensor_lock(sctx, SENSOR_LOCK_WRITE);
    sensor_watch_free(sctx);
    avltree_clear(sctx->sensors);
    if (sctx->sensorlist != NULL) {
        slist_free(sctx->sensorlist, sensor_desc_free_one);
        sctx->sensorlist = NULL;
    }
    sensor_unlock(sctx);
}

/* ************************************************************************
 * SENSOR FIND FUNCTIONS
 * ************************************************************************ */

/* ************************************************************************ */
/** for sensor_desc_match(), init with sensor_desc_match_get() */
typedef struct {
    const char *        pattern;
    unsigned int        flags;
    ssize_t             pattern_idx;
    int                 fnm_flags;
    char *              slash;
    int                 (*cmp)(const char *, const char *);
    int                 (*ncmp)(const char *, const char *, size_t);
} sensor_desc_match_t;

/** for sensor_find(), init with sensor_desc_range_get() */
typedef struct {
    /* for range computing and sensor_desc_match_unlocked() */
    sensor_desc_match_t matchdata;
    /* list of results */
    slist_t **          plist;
    /* first matching element */
    union {
        sensor_sample_t *       sample;
        sensor_desc_t *         desc;
    } first;
    /* visit() function */
    union {
        sensor_visitfun_t       desc;
        sensor_watch_visitfun_t sample;
    } visit;
    /* user_data for visit() function */
    void *              user_data;
    /* min/max sensor_desc_t */
    char fam_name_min[SENSOR_LABEL_SIZE], fam_name_max[SENSOR_LABEL_SIZE];
    char lab_name_min[SENSOR_LABEL_SIZE], lab_name_max[SENSOR_LABEL_SIZE];
    sensor_family_info_t faminfo_min;
    sensor_family_info_t faminfo_max;
    sensor_family_t fam_min;
    sensor_family_t fam_max;
    sensor_desc_t   desc_min;
    sensor_desc_t   desc_max;
} sensor_find_range_t;

/* *********************************************************************** */
static sensor_status_t sensor_desc_match_get(
                            sensor_desc_match_t *       matchdata,
                            const char *                pattern,
                            unsigned int                flags) {
    matchdata->pattern = pattern;
    matchdata->flags = flags;
    matchdata->pattern_idx = fnmatch_patternidx(pattern);
    matchdata->slash = strchr(pattern, '/');

    if ((flags & SSF_NOPATTERN) != 0) {
        matchdata->pattern_idx = -1;
    }
    if (matchdata->slash == NULL && matchdata->pattern_idx < 0) {
        return SENSOR_ERROR; /* if not pattern and if no slash-> no match possible */
    }
    if ((flags & SSF_CASEFOLD) != 0) {
        matchdata->cmp = strcasecmp;
        matchdata->ncmp = strncasecmp;
        matchdata->fnm_flags = FNM_CASEFOLD;
    } else {
        matchdata->cmp = strcmp;
        matchdata->ncmp = strncmp;
        matchdata->fnm_flags = 0;
    }
    return SENSOR_SUCCESS;
}

/* ***********************************************************************
 * try to minimize the search range by keeping the beginning of pattern */
static sensor_status_t sensor_desc_range_get(
                            sensor_ctx_t *          sctx,
                            sensor_find_range_t *   data,
                            const char *            pattern,
                            unsigned int            flags) {
    #ifndef _DEBUG
    (void) sctx;
    #endif

    static const size_t fam_name_min_len = PTR_COUNT(data->fam_name_min);
    static const size_t fam_name_max_len = PTR_COUNT(data->fam_name_max);
    static const size_t lab_name_min_len = PTR_COUNT(data->lab_name_min);
    static const size_t lab_name_max_len = PTR_COUNT(data->lab_name_max);

    char * fam_name_min = data->fam_name_min, * fam_name_max = data->fam_name_max;
    char * lab_name_min = data->lab_name_min, * lab_name_max = data->lab_name_max;

    size_t          fam_len;
    size_t          n;
    ssize_t         pattern_idx;
    char *          slash;

    /* create desc_min and desc_max with sufficient info for label comparison */
    data->faminfo_min = (sensor_family_info_t) { .name = data->fam_name_min, };
    data->faminfo_max = (sensor_family_info_t) { .name = data->fam_name_max, };
    data->fam_min = (sensor_family_t) { .info = &(data->faminfo_min), .sctx = NULL/*IMPORTANT*/ };
    data->fam_max = (sensor_family_t) { .info = &(data->faminfo_max), .sctx = NULL/*IMPORTANT*/ };
    data->desc_min = (sensor_desc_t) { .family = &(data->fam_min), .label = data->lab_name_min };
    data->desc_max = (sensor_desc_t) { .family = &(data->fam_max), .label = data->lab_name_max };

    LOG_DEBUG(sctx->log, "%s(): pattern:'%s' (flags:%u)",
              __func__, pattern, flags);

    /* check pattern/flags and build sensor_desc_match_t data */
    if (sensor_desc_match_get(&(data->matchdata), pattern, flags) != SENSOR_SUCCESS) {
        return SENSOR_ERROR;
    }
    pattern_idx = data->matchdata.pattern_idx;
    slash = data->matchdata.slash;

    /* family name stop at slash or at first pattern char if any */
    if (pattern_idx < 0) {
        fam_len = slash != NULL ? (size_t) (slash - pattern) : strlen(pattern);
    } else {
        fam_len = slash != NULL && slash < pattern + pattern_idx
                  ? slash - pattern : pattern_idx;
    }
    n = strn0cpy(fam_name_min, pattern, fam_len, fam_name_min_len - 1);
    /* max family name is min name suffixed with CHAR_MAX (if pattern) */
    n = strn0cpy(fam_name_max, fam_name_min, n, fam_name_max_len - 1);
    if (pattern_idx >= 0) {
        memset(fam_name_max + n, CHAR_MAX, fam_name_max_len - n);
        fam_name_max[fam_name_max_len - 1] = 0;
    }

    if (slash == NULL || (pattern_idx >= 0 && slash > pattern + pattern_idx)) {
        /* cannot see the '/' or / appear after a pattern char
         * --> cannot guess label, search all. */
        lab_name_min[0] = 0;
        memset(lab_name_max, CHAR_MAX, lab_name_max_len);
        lab_name_max[lab_name_max_len - 1] = 0;
    } else {
        size_t lab_len;

        /* label name starts at slash and ends at first pattern char if any */
        if (pattern_idx < 0) {
            lab_len = strlen(slash + 1);
        } else {
            lab_len = pattern_idx - (slash + 1 - pattern);
        }
        n = strn0cpy(lab_name_min, slash + 1, lab_len, lab_name_min_len - 1);
        /* max label name is min suffixed with CHAR_MAX (if pattern) */
        n = strn0cpy(lab_name_max, lab_name_min, n, lab_name_max_len - 1);
        if (pattern_idx >= 0) {
            memset(lab_name_max + n, CHAR_MAX, lab_name_max_len - n);
            lab_name_max[lab_name_max_len - 1] = 0;
        }
    }

    LOG_DEBUG_BUF(sctx->log, fam_name_min, strlen(fam_name_min)+1, "fam_name_min ");
    LOG_DEBUG_BUF(sctx->log, fam_name_max, strlen(fam_name_min)+1, "fam_name_max ");
    LOG_DEBUG_BUF(sctx->log, lab_name_min, strlen(lab_name_min)+1, "lab_name_min ");
    LOG_DEBUG_BUF(sctx->log, lab_name_max, strlen(lab_name_min)+1, "lab_name_max ");

    return SENSOR_SUCCESS;
}

/* ************************************************************************ */
static int sensor_desc_match_unlocked(const sensor_desc_t *  sensor,
                                      sensor_desc_match_t *  data) {
    int matched = 0;

    if (data->pattern_idx >= 0) {
        char    label[SENSOR_LABEL_SIZE];

        snprintf(label, PTR_COUNT(label), "%s/%s",
                 SENSOR_DESC_FAMNAME(sensor), SENSOR_DESC_LABEL(sensor));
        matched = (fnmatch(data->pattern, label, data->fnm_flags) == 0);
    } else {
        /* sensor_desc_match_get() ensures that slash != NULL when pattern_idx < 0 */
        const size_t        fam_len     = data->slash - data->pattern;
        const char * const  fam_name    = SENSOR_DESC_FAMNAME(sensor);
        matched =
            (data->ncmp(fam_name, data->pattern, fam_len) == 0
             && fam_name[fam_len] == 0
             && data->cmp(SENSOR_DESC_LABEL(sensor), data->slash + 1) == 0);
    }
    if ( ! matched && sensor->properties == s_sensor_loading_properties && sensor->key != NULL) {
        sensor_loadinginfo_t * info = (sensor_loadinginfo_t *) sensor->key;
        if (info->pattern != NULL) {
            // invert the fnmatch arguments -> sensor_desc is pattern, pattern is searched string
            matched = fnmatch(info->pattern, data->pattern, data->fnm_flags) == 0;
        }
    }
    return matched;
}

/* ************************************************************************ */
AVLTREE_DECLARE_VISITFUN(sensor_watch_visit_find, tree, node, context, vdata) {
    sensor_sample_t *       sample  = (sensor_sample_t *) node->data;
    sensor_find_range_t *   data    = (sensor_find_range_t *) vdata;
    (void) tree;
    (void) context;

    LOG_DEBUG(sample->desc->family->sctx->log, "sensor_watch_find(): check '%s/%s'",
              SENSOR_DESC_FAMNAME(sample->desc), SENSOR_DESC_LABEL(sample->desc));

    if (sensor_desc_match_unlocked(sample->desc, &(data->matchdata))) {
        if (data->first.sample == NULL) {
            data->first.sample = sample;
            if (data->visit.sample == NULL && data->plist == NULL)
                return AVS_FINISHED;
        }
        if (data->plist != NULL) {
            *(data->plist) = slist_prepend(*(data->plist), sample);
        }
        if (data->visit.sample != NULL) {
            int ret = data->visit.sample(sample, data->user_data);
            if (ret == SENSOR_ERROR) {
                return AVS_ERROR;
            }
            if (ret == SENSOR_RELOAD_FAMILY) {
                return AVS_FINISHED;
            }
        }
    }

    return AVS_CONTINUE;
}

/* ************************************************************************ */
AVLTREE_DECLARE_VISITFUN(sensor_desc_visit_find, tree, node, context, vdata) {
    sensor_desc_t *         desc = (sensor_desc_t *) node->data;
    sensor_find_range_t *   data = (sensor_find_range_t *) vdata;
    (void) tree;
    (void) context;

    LOG_DEBUG(desc->family->sctx->log, "sensor_desc_find(): check '%s/%s'",
              SENSOR_DESC_FAMNAME(desc), SENSOR_DESC_LABEL(desc));

    if (sensor_desc_match_unlocked(desc, &(data->matchdata))) {
        if (data->first.desc == NULL) {
            data->first.desc = desc;
            if (data->visit.desc == NULL && data->plist == NULL)
                return AVS_FINISHED;
        }
        if (data->plist != NULL) {
            *(data->plist) = slist_prepend(*(data->plist), desc);
        }
        if (data->visit.desc != NULL) {
            int ret = data->visit.desc(desc, data->user_data);
            if (ret == SENSOR_ERROR) {
                return AVS_ERROR;
            }
            if (ret == SENSOR_RELOAD_FAMILY) {
                return AVS_FINISHED;
            }
        }
    }

    return AVS_CONTINUE;
}

/* ************************************************************************ */
static sensor_sample_t * sensor_watch_find_unlocked(
                        sensor_ctx_t *          sctx,
                        const char *            pattern,
                        unsigned int            flags,
                        slist_t **              matchs,
                        sensor_watch_visitfun_t visit,
                        void *                  user_data) {
    sensor_find_range_t data = {
        .plist = matchs, .first.sample = NULL,
        .visit.sample = visit, .user_data = user_data,
    };
    sensor_sample_t min = { .desc = &(data.desc_min), .watch = NULL/*IMPORTANT*/ };
    sensor_sample_t max = { .desc = &(data.desc_max), .watch = NULL/*IMPORTANT*/ };

    LOG_DEBUG(sctx->log, "FINDING WATCHS, pattern:'%s' (flags:%u)",
              pattern, flags);

    /* compute min and max range values from pattern */
    if (sensor_desc_range_get(sctx, &data, pattern, flags) != SENSOR_SUCCESS) {
        if (matchs != NULL) {
            *matchs = NULL;
        }
        return NULL;
    }

    if (avltree_visit_range(sctx->watchs, &min, &max,
                            sensor_watch_visit_find, &data, AVH_INFIX) != AVS_FINISHED) {
        if (matchs != NULL) {
            slist_free(*matchs, NULL);
            *matchs = NULL;
        }
        return NULL;
    }

    return data.first.sample;
}

/* ************************************************************************ */
static sensor_desc_t * sensor_find_unlocked(
                        sensor_ctx_t *          sctx,
                        const char *            pattern,
                        unsigned int            flags,
                        slist_t **              matchs,
                        sensor_visitfun_t       visit,
                        void *                  user_data) {
    sensor_find_range_t data = {
        .plist = matchs, .first.desc = NULL,
        .visit.desc = visit, .user_data = user_data,
    };

    LOG_DEBUG(sctx->log, "FINDING DESCS, pattern:'%s' (flags:%u)",
                pattern, flags);

    /* compute min and max range values from pattern */
    if (sensor_desc_range_get(sctx, &data, pattern, flags) != SENSOR_SUCCESS) {
        if (matchs != NULL) {
            *matchs = NULL;
        }
        return NULL;
    }

    if (avltree_visit_range(sctx->sensors, &(data.desc_min), &(data.desc_max),
                            sensor_desc_visit_find, &data, AVH_INFIX) != AVS_FINISHED) {
        if (matchs != NULL) {
            slist_free(*matchs, NULL);
            *matchs = NULL;
        }
        return NULL;
    }

    return data.first.desc;
}

/* ************************************************************************ */
sensor_status_t     sensor_desc_match(
                        sensor_ctx_t *          sctx,
                        const char *            pattern,
                        unsigned int            flags,
                        const sensor_desc_t *   sensor) {

    sensor_desc_match_t data;
    sensor_status_t     result = SENSOR_ERROR;

    if (sensor == NULL || pattern == NULL ||  sensor->family == NULL
    || sensor->family->info == NULL || sensor->family->info->name == NULL) {
        return SENSOR_ERROR;
    }
    sensor_lock(sctx, SENSOR_LOCK_READ);

    if (sensor_desc_match_get(&data, pattern, flags) == SENSOR_SUCCESS
    &&  sensor_desc_match_unlocked(sensor, &data)) {
        result = SENSOR_SUCCESS;
    }

    sensor_unlock(sctx);
    return result;
}

/* ************************************************************************ */
sensor_desc_t * sensor_find(
                        sensor_ctx_t *      sctx,
                        const char *        pattern,
                        unsigned int        flags,
                        slist_t **          matchs) {
    sensor_desc_t * result;

    if (sctx == NULL || pattern == NULL) {
        return NULL;
    }
    sensor_lock(sctx, (flags & SSF_LOCK_WRITE) != 0
                      ? SENSOR_LOCK_WRITE : SENSOR_LOCK_READ);

    result = sensor_find_unlocked(sctx, pattern, flags, matchs, NULL, NULL);

    sensor_unlock(sctx);

    return result;
}

/* ************************************************************************ */
sensor_status_t sensor_visit(
                        sensor_ctx_t *          sctx,
                        const char *            pattern,
                        unsigned int            flags,
                        sensor_visitfun_t       visit,
                        void *                  user_data) {
    sensor_desc_t * result;

    if (sctx == NULL || pattern == NULL || visit == NULL) {
        return SENSOR_ERROR;
    }

    sensor_lock(sctx, (flags & SSF_LOCK_WRITE) != 0
                      ? SENSOR_LOCK_WRITE : SENSOR_LOCK_READ);

    result = sensor_find_unlocked(sctx, pattern, flags, NULL, visit, user_data);

    sensor_unlock(sctx);

    return result != NULL ? SENSOR_SUCCESS : SENSOR_ERROR;
}

/* ************************************************************************ */
sensor_sample_t * sensor_watch_find(
                    sensor_ctx_t *          sctx,
                    const char *            pattern,
                    unsigned int            flags,
                    slist_t **              matchs) {
    sensor_sample_t * result;

    if (sctx == NULL || pattern == NULL) {
        return NULL;
    }

    sensor_lock(sctx, (flags & SSF_LOCK_WRITE) != 0
                      ? SENSOR_LOCK_WRITE : SENSOR_LOCK_READ);

    result = sensor_watch_find_unlocked(sctx, pattern, flags, matchs, NULL, NULL);

    sensor_unlock(sctx);

    return result;
}

/* ************************************************************************ */
sensor_status_t sensor_watch_visit(
                        sensor_ctx_t *          sctx,
                        const char *            pattern,
                        unsigned int            flags,
                        sensor_watch_visitfun_t visit,
                        void *                  user_data) {
    sensor_sample_t * result;

    if (sctx == NULL || pattern == NULL || visit == NULL) {
        return SENSOR_ERROR;
    }
    sensor_lock(sctx, (flags & SSF_LOCK_WRITE) != 0
                      ? SENSOR_LOCK_WRITE : SENSOR_LOCK_READ);

    result = sensor_watch_find_unlocked(sctx, pattern, flags, NULL, visit, user_data);

    sensor_unlock(sctx);

    return result != NULL ? SENSOR_SUCCESS : SENSOR_ERROR;
}


/* ************************************************************************
 * SENSOR WATCH ADD / DELETE / LIST FUNCTIONS
 * ************************************************************************ */

/* ************************************************************************ */
static sensor_status_t sensor_watch_del_unlocked(
                    sensor_ctx_t *          sctx,
                    const char *            pattern,
                    unsigned int            flags) {
    sensor_status_t     result = SENSOR_ERROR;
    sensor_desc_match_t data;

    LOG_VERBOSE(sctx->log, "REMOVING WATCHS, pattern:'%s' (flags:%u)", pattern, flags);

    if (sensor_desc_match_get(&data, pattern, flags) != SENSOR_SUCCESS) {
        return SENSOR_ERROR;
    }

    for (slist_t * list = sctx->watchlist, *prev = NULL; list != NULL; /* no_incr */) {
        sensor_sample_t *           watch = (sensor_sample_t *) list->data;
        sensor_watchparam_entry_t * watchparam;

        if (sensor_desc_match_unlocked(watch->desc, &data)) {
            slist_t *                   to_free = list;
            sensor_watchparam_entry_t   testwatchparam = { .watch = *(watch->watch) };
            const sensor_desc_t *       desc = watch->desc;

            /* watch to delete found, check if its watch param is still used */
            watchparam = avltree_find(sctx->watch_params, &testwatchparam);

            LOG_DEBUG(sctx->log, "-> REMOVING Watch '%s/%s', param_usecount:%d",
                      SENSOR_DESC_FAMNAME(desc), SENSOR_DESC_LABEL(desc),
                      watchparam == NULL ? -1 : watchparam->use_count);

            /* notify family that watch will be deleted, while its data are still valid */
            if (desc->family->info->notify != NULL) {
                desc->family->info->notify(SWE_WATCH_DELETING,
                                           desc->family, watch, NULL);
            }

            if (watchparam != NULL && --(watchparam->use_count) == 0) {
                /* watch param no longer used, delete it */
                LOG_DEBUG(sctx->log, "-> %s/%s: REMOVING unused WATCH ENTRY (t=%lu.%03lu)",
                    SENSOR_DESC_FAMNAME(desc), SENSOR_DESC_LABEL(desc),
                    (unsigned long)(watchparam->watch.update_interval.tv_sec),
                    (unsigned long)(watchparam->watch.update_interval.tv_usec / 1000));

                avltree_remove(sctx->watch_params, watchparam);
            }

            /* remove watch from tree */
            if (avltree_remove(sctx->watchs, watch) != watch) {
                LOG_WARN(sctx->log, "-> cannot remove '%s/%s' from tree",
                         SENSOR_DESC_FAMNAME(desc), SENSOR_DESC_LABEL(desc));
            }
            /* remove watch from list and go to next */
            if (to_free == sctx->watchlist)
                sctx->watchlist = to_free->next;
            list = list->next;
            slist_free_1(to_free, sensor_watch_free_one);
            if (prev != NULL) {
                prev->next = list;
            }
            result = SENSOR_SUCCESS;
        } else {
            prev = list;
            list = list->next;
        }
    }
    return result;
}

/* ************************************************************************ */
static sensor_sample_t * sensor_watch_add_desc_unlocked(
                        sensor_ctx_t *          sctx,
                        const sensor_desc_t *   sensor,
                        unsigned int            flags,
                        sensor_watch_t *        watch) {

    sensor_watchparam_entry_t   testwatchparam = { .watch = *watch };
    sensor_sample_t *           sample = NULL, testsample;
    sensor_watchparam_entry_t * watchparam;
    sensor_watch_event_t        event = SWE_NONE;
    (void) flags;

    /* look for an already ready watch config entry */
    if ((watchparam = avltree_find(sctx->watch_params, &testwatchparam)) != NULL) {
        ++(watchparam->use_count);

        LOG_DEBUG(sctx->log, "  %s/%s: reusing watch_param entry (t=%lu.%03lu)",
                  SENSOR_DESC_FAMNAME(sensor), SENSOR_DESC_LABEL(sensor),
                  (unsigned long)(watchparam->watch.update_interval.tv_sec),
                  (unsigned long)(watchparam->watch.update_interval.tv_usec / 1000));
    } else {
        if ((watchparam = calloc(1, sizeof(*watchparam))) != NULL) {
            watchparam->use_count = 1;
            watchparam->watch = *watch;
        }
        if (watchparam == NULL || avltree_insert(sctx->watch_params, watchparam) == NULL) {
            LOG_ERROR(sctx->log, "error: cannot allocate/insert sensor watch entry in %s", __func__);
            if (watchparam != NULL)
                free(watchparam);
            return NULL;
        }
        LOG_DEBUG(sctx->log, "  %s/%s: adding new watch_param entry (t=%lu.%03lu)",
                  SENSOR_DESC_FAMNAME(sensor), SENSOR_DESC_LABEL(sensor),
                  (unsigned long)(watchparam->watch.update_interval.tv_sec),
                  (unsigned long)(watchparam->watch.update_interval.tv_usec / 1000));
    }

    testsample = (sensor_sample_t) { .desc = sensor, .watch = NULL };
    if ((sample = avltree_find(sctx->watchs, &testsample)) != NULL) {
        /* DON't REMOVE SENSOR from watch list as it is
         * currently sorted alphabeticaly: the order is not changed */

        LOG_DEBUG(sctx->log, "  replace previous watch param for '%s/%s', param_usecount:%d",
                  SENSOR_DESC_FAMNAME(sensor), SENSOR_DESC_LABEL(sensor),
                  watchparam->use_count);

        if (sample->watch == &(watchparam->watch)) {
            /* same watch properties, decrement counter incremented above */
            --(watchparam->use_count);
        } else {
            /* different watch properties, check if old one is still used */
            sensor_watchparam_entry_t   testwatchparam = { .watch = *(sample->watch) };
            sensor_watchparam_entry_t * oldwatchparam;

            if ((oldwatchparam = avltree_find(sctx->watch_params, &testwatchparam)) != NULL
            &&  --(oldwatchparam->use_count) == 0) {

                LOG_DEBUG(sctx->log, "  %s/%s: REMOVING unused WATCH ENTRY (t=%lu.%03lu)",
                    SENSOR_DESC_FAMNAME(sensor), SENSOR_DESC_LABEL(sensor),
                    (unsigned long)(watchparam->watch.update_interval.tv_sec),
                    (unsigned long)(watchparam->watch.update_interval.tv_usec / 1000));

                avltree_remove(sctx->watch_params, oldwatchparam);
            }
        }
        /* reset next_update_time in order to take into account possible new timer */
        memset(&(sample->next_update_time), 0, sizeof(sample->next_update_time));
        /* set event value */
        event |= SWE_WATCH_REPLACED;
    } else {
        /* alloc a new sample if not already watched */
        if ((sample = calloc(1, sizeof(sensor_sample_t))) == NULL) {
            LOG_WARN(sctx->log, "error: cannot allocate sensor sample in %s", __func__);
            return NULL;
        }
        event |= SWE_WATCH_ADDED;
    }

    sample->desc = sensor;
    sample->watch = &(watchparam->watch);

    if ((event & SWE_WATCH_ADDED) != 0) {
        /* sensor added, not replaced */
        memset(&(sample->value), 0xff, sizeof(sample->value));
        sample->value.type = sample->desc->type;
        if (SENSOR_VALUE_IS_BUFFER(sample->value.type)) {
            SENSOR_VALUE_INIT_BUF(sample->value, sample->value.type, NULL, 0);
        }
        /* add to list */
        sctx->watchlist = slist_insert_sorted(sctx->watchlist, sample, sensorwatch_alphacmp);
        /* add to tree */
        if (avltree_insert(sctx->watchs, sample) == NULL) {
            LOG_WARN(sctx->log, "cannot insert '%s/%s' in tree",
                     SENSOR_DESC_FAMNAME(sensor), SENSOR_DESC_LABEL(sensor));
        }
    } else if (sample->desc->properties != s_sensor_loading_properties) {
        if (SENSOR_VALUE_IS_BUFFER(sample->value.type)) {
            sample->value.data.b.size = 0;
            memset(sample->value.data.b.buf, sample->value.type == SENSOR_VALUE_STRING
                                             ? 0 : 0xff, sample->value.data.b.maxsize);
        } else {
            sensor_value_type_t type = sample->value.type;
            memset(&(sample->value), 0xff, sizeof(sample->value));
            sample->value.type = type;
        }
    }

    LOG_DEBUG(sctx->log, "WATCH %s: '%s/%s' (T:%lu.%03lu, param_usecount:%d)",
              (event & SWE_WATCH_ADDED) != 0 ? "ADDED" : "REPLACED",
              SENSOR_DESC_FAMNAME(sensor), SENSOR_DESC_LABEL(sensor),
              (unsigned long)(watchparam->watch.update_interval.tv_sec),
              (unsigned long)(watchparam->watch.update_interval.tv_usec / 1000),
              watchparam->use_count);

    /* notify family */
    if (sensor->family->info->notify != NULL) {
        sensor->family->info->notify(event, sensor->family, sample, NULL);
    }

    return sample;
}

/* ************************************************************************ */
static sensor_status_t sensor_watch_add_unlocked(
                        sensor_ctx_t *      sctx,
                        const char *        pattern,
                        unsigned int        flags,
                        sensor_watch_t *    watch) {
    sensor_status_t         result      = SENSOR_ERROR;
    sensor_desc_match_t     matchdata;
    int                     has_loading = 0;

    LOG_VERBOSE(sctx->log, "ADDING new watches, pattern:'%s' (T:%lu.%03lu, flags:%u)",
                pattern, (unsigned long) watch->update_interval.tv_sec,
                         (unsigned long) (watch->update_interval.tv_usec / 1000UL), flags);

    if (sensor_desc_match_get(&matchdata, pattern, flags) != SENSOR_SUCCESS) {
        return SENSOR_ERROR;
    }

    SLIST_FOREACH_DATA(sctx->sensorlist, sensor, sensor_desc_t *) {
        if (sensor->label == s_sensor_loading_label) {
            has_loading = 1;
            continue ;
        }
        if (sensor_desc_match_unlocked(sensor, &matchdata)
        &&  sensor_watch_add_desc_unlocked(sctx, sensor, flags, watch) != NULL) {
            result = SENSOR_SUCCESS;
        }
    }
    if ( ! has_loading ) {
        return result;
    } else {
        sensor_find_range_t     range;

        LOG_DEBUG(sctx->log, "LOOKING for a match for '%s' in loading sensors", pattern);

        if (sensor_desc_range_get(sctx, &range, pattern, flags) != SENSOR_SUCCESS) {
            return result;
        }
        /* keep only the family part of the range */
        *range.lab_name_min = 0;
        memset(range.lab_name_max, CHAR_MAX, PTR_COUNT(range.lab_name_max));
        range.lab_name_max[PTR_COUNT(range.lab_name_max) - 1] = 0;

        /* special case for a sensor which is loading but not ready
         * add the pattern as it is, and SENSOR_RELOAD_FAMILY will do the job */
        SLIST_FOREACH_DATA(sctx->sensorlist, sensor, sensor_desc_t *) {
            if (sensor->label == s_sensor_loading_label
            &&  sensordesc_alphacmp(&range.desc_min, sensor) <= 0
            &&  sensordesc_alphacmp(&range.desc_max, sensor) >= 0) {
                sensor_desc_t *     newdesc;
                sensor_sample_t *   sample;
                const char *        loadpattern;
                int                 found = 0;

                SLIST_FOREACH_DATA(sctx->sensorlist, load, sensor_desc_t *) {
                    if (sensor->family == load->family
                    &&  load->properties == s_sensor_loading_properties
                    &&  load->key != NULL
                    &&  (loadpattern = ((sensor_loadinginfo_t *)load->key)->pattern) != NULL
                    &&  (strcmp(loadpattern, pattern) == 0
                         || fnmatch(loadpattern, pattern, range.matchdata.fnm_flags) == 0
                         //|| sensor_desc_match_unlocked(load, &(range.matchdata))
                    )) {
                        LOG_DEBUG(sctx->log, "%s(): '%s' is already handled by '%s'", __func__,
                                  pattern, loadpattern);
                        found = 1;
                        break ;
                    }
                }
                if (found)
                    continue ;

                LOG_VERBOSE(sctx->log, "ADDING TEMPORARY WATCH '%s' from template '%s/%s' idx %d",
                            pattern, SENSOR_DESC_FAMNAME(sensor), SENSOR_DESC_LABEL(sensor),
                            SENSOR_VALUE_GET(s_sensor_loading_properties[0].value,
                                             SENSOR_LOADING_ID_TYPE));

                if ((newdesc = malloc(sizeof(*newdesc))) == NULL) {
                    LOG_WARN(sctx->log, "%s(): malloc loading-desc failed: %s",
                             __func__, strerror(errno));
                    break ;
                }
                *newdesc = *sensor;
                newdesc->properties = s_sensor_loading_properties;
                newdesc->label = strdup(range.matchdata.slash != NULL
                                        ? range.matchdata.slash + 1 : pattern);
                if ((newdesc->key = malloc(sizeof(sensor_loadinginfo_t))) != NULL) {
                    sensor_loadinginfo_t * info = (sensor_loadinginfo_t *) newdesc->key;
                    info->pattern = strdup(pattern);
                    info->id = SENSOR_VALUE_GET(s_sensor_loading_properties[0].value,
                                                SENSOR_LOADING_ID_TYPE);
                }
                ++(SENSOR_VALUE_GET(s_sensor_loading_properties[0].value,
                                    SENSOR_LOADING_ID_TYPE));

                if (avltree_insert(sctx->sensors, newdesc) != newdesc) {
                    LOG_WARN(sctx->log, "cannot add temporary desc in the tree");
                }
                sctx->sensorlist = slist_prepend(sctx->sensorlist, newdesc);
                if ((sample = sensor_watch_add_desc_unlocked(sctx, newdesc, flags, watch))
                        != NULL) {
                    if (!SENSOR_VALUE_IS_BUFFER(sample->value.type)) {
                        SENSOR_VALUE_INIT_BUF(sample->value, SENSOR_VALUE_STRING, NULL, 0);
                    }
                    sensor_value_frombuffer("Loading...", strlen("Loading...") + 1,
                                            &(sample->value));
                    result = SENSOR_SUCCESS;
                } else {
                    LOG_WARN(sctx->log, "cannot add temporary watch '%s/%s'",
                             SENSOR_DESC_FAMNAME(newdesc), SENSOR_DESC_LABEL(newdesc));
                }
            }
        }
    }
    return result;
}

/* ************************************************************************ */
sensor_status_t sensor_watch_add(
                        sensor_ctx_t *      sctx,
                        const char *        pattern,
                        unsigned int        flags,
                        sensor_watch_t *    watch) {
    sensor_status_t result;

    if (sctx == NULL)
       return SENSOR_ERROR;

    if (pattern == NULL || watch == NULL) {
        LOG_VERBOSE(sctx->log, "error: watch or pattern is NULL in %s", __func__);
        return SENSOR_ERROR;
    }

    sensor_lock(sctx, SENSOR_LOCK_WRITE);
    result = sensor_watch_add_unlocked(sctx, pattern, flags, watch);
    sensor_unlock(sctx);

    return result;
}

/* ************************************************************************ */
sensor_status_t sensor_watch_add_desc(
                    sensor_ctx_t *          sctx,
                    const sensor_desc_t *   sensor,
                    unsigned int            flags,
                    sensor_watch_t *        watch) {
    sensor_status_t result;

    /* sanity checks and error logging done in sensor_watch_add() */
    if (sensor == NULL || watch == NULL) {
        return sensor_watch_add(sctx, "*", flags, watch);
    }
    if (sctx == NULL || sensor->family == NULL || sensor->family->info == NULL
    ||  sensor->family->info->name == NULL) {
        return SENSOR_ERROR;
    }

    LOG_VERBOSE(sctx->log, "ADDING new watch '%s/%s' (%lu.%03lus)",
                SENSOR_DESC_FAMNAME(sensor), SENSOR_DESC_LABEL(sensor),
                (unsigned long) watch->update_interval.tv_sec,
                (unsigned long) (watch->update_interval.tv_usec / 1000UL));

    sensor_lock(sctx, SENSOR_LOCK_WRITE);
    result = sensor_watch_add_desc_unlocked(sctx, sensor, flags, watch) != NULL
             ? SENSOR_SUCCESS : SENSOR_ERROR;
    sensor_unlock(sctx);

    return result;
}

/* ************************************************************************ */
sensor_status_t sensor_watch_del(
                    sensor_ctx_t *          sctx,
                    const char *            pattern,
                    unsigned int            flags) {
    sensor_status_t result;

    if (sctx == NULL || pattern == NULL) {
        return SENSOR_ERROR;
    }

    sensor_lock(sctx, SENSOR_LOCK_WRITE);
    result = sensor_watch_del_unlocked(sctx, pattern, flags);
    sensor_unlock(sctx);

    return result;
}

/* ************************************************************************ */
const slist_t * sensor_watch_list_get(sensor_ctx_t *sctx) {
    const slist_t * result;

    if (sctx == NULL) {
        return NULL;
    }
    result = sctx->watchlist;

    return result;
}

/* ************************************************************************ */
unsigned long sensor_watch_timerms(sensor_sample_t * sample) {
    if (sample == NULL) {
        errno = EFAULT;
        return 0UL;
    }
    return sample->watch->update_interval.tv_sec * 1000UL
           + sample->watch->update_interval.tv_usec / 1000UL;
}


/* ************************************************************************ */
void sensor_watch_free(sensor_ctx_t *sctx) {
    if (sctx == NULL) {
        return ;
    }
    sensor_lock(sctx, SENSOR_LOCK_WRITE);
    avltree_clear(sctx->watchs);
    slist_free(sctx->watchlist, sensor_watch_free_one);
    sctx->watchlist = NULL;
    avltree_clear(sctx->watch_params);
    sensor_unlock(sctx);
}

/* ************************************************************************ */
typedef struct {
    sensor_ctx_t *  sctx;
    unsigned long   pgcd;
    double *        p_precision;
    double          min_precision;
} sensor_watchpgcd_visit_t;
static AVLTREE_DECLARE_VISITFUN(sensor_watchpgcd_visit, tree, node, context, user_data) {
    sensor_watchparam_entry_t * watchparam = (sensor_watchparam_entry_t *) node->data;
    sensor_watchpgcd_visit_t *  data = (sensor_watchpgcd_visit_t *) user_data;
    (void) tree;
    (void) context;

    unsigned long interval_ms = watchparam->watch.update_interval.tv_sec * 1000
                              + watchparam->watch.update_interval.tv_usec / 1000;

    data->pgcd = pgcd_rounded(data->pgcd, interval_ms, data->p_precision, data->min_precision);

    LOG_DEBUG(data->sctx->log, "loop. new pgcd=%lu, current=%lu, precision=%f",
              data->pgcd, interval_ms, *(data->p_precision));

    return AVS_CONTINUE;
}

/* ************************************************************************ */
unsigned long sensor_watch_pgcd(sensor_ctx_t *sctx,
                                double * p_precision, double min_precision) {
    double                      precision;
    sensor_watchpgcd_visit_t    data;
    int                         ret;

    if (sctx == NULL) {
        return 0;
    }
    if (p_precision == NULL) {
        precision = 1.0L;
        p_precision = &precision;
        min_precision = 1.0L;
    }
    if (min_precision <= 0.0L) {
        min_precision = 1.0L;
    }
    if (*p_precision <= 0.0L) {
        *p_precision = 1.0L;
    }
    data.p_precision = p_precision;
    data.min_precision = min_precision;
    data.pgcd = 0;
    data.sctx = sctx;

    sensor_lock(sctx, SENSOR_LOCK_READ);

    ret = avltree_visit(sctx->watch_params, sensor_watchpgcd_visit, &data, AVH_INFIX | AVH_RIGHT);

    LOG_DEBUG(sctx->log, "END. pgcd=%lu, precision=%f, ret=%d",
              data.pgcd, *(data.p_precision), ret);

    if (ret != AVS_FINISHED) {
        LOG_WARN(sctx->log, "watch_params tree pgcd error");
    }

    sensor_unlock(sctx);

    return data.pgcd;
}

/* ************************************************************************ */
sensor_status_t sensor_watch_save(slist_t * watch_list, const char * path) {
    (void)watch_list;
    (void)path;
    return SENSOR_NOT_SUPPORTED;
}

/* ************************************************************************ */
const slist_t * sensor_watch_load(const char * path) {
    (void)path;
    return NULL;
}


/* ************************************************************************
 * SENSOR_UPDATE FUNCTIONS
 * ************************************************************************ */

/* ************************************************************************ */
slist_t * sensor_family_loading_list(sensor_family_t * family) {
    sensor_desc_t *     desc;

    if (family == NULL || family->sctx == NULL) {
        return NULL;
    }
    if ((desc = malloc(sizeof(*desc))) == NULL) {
        LOG_WARN(family->sctx->log, "%s(): cannot allocate loading-desc: %s",
                 __func__, strerror(errno));
        return NULL;
    }
    desc->label = s_sensor_loading_label;
    desc->type = SENSOR_VALUE_NULL;
    desc->family = family;
    desc->properties = NULL;
    desc->key = NULL; /* NULL pattern will never match */;

    return slist_prepend(NULL, desc);
}

/* ************************************************************************ */
typedef struct sensor_family_reload_s {
    sensor_watch_t                  watchparam;
    //const sensor_desc_t *           desc;
    sensor_family_t *               family;
    const char *                    pattern;
    unsigned int                    id;
    struct sensor_family_reload_s * next;
} sensor_family_reload_t;

/* ************************************************************************ */
static sensor_status_t sensor_family_reload_visit(sensor_sample_t * sample, void * vdata) {
    sensor_family_reload_t **   pdata = (sensor_family_reload_t **) vdata;
    sensor_family_reload_t *    new, * list;

    if ((new  = malloc(sizeof(*new))) == NULL) {
        LOG_WARN(sample->desc->family->sctx->log, "%s(): cannot malloc reload data: %s",
                 __func__, strerror(errno));
        return SENSOR_ERROR;
    }
    LOG_DEBUG(sample->desc->family->sctx->log, "%s(): BACKUP %s/%s", __func__,
              SENSOR_DESC_FAMNAME(sample->desc), SENSOR_DESC_LABEL(sample->desc));

    new->watchparam = *(sample->watch);
    new->family     = sample->desc->family;

    if (sample->desc->label == s_sensor_loading_label) {
        new->pattern = NULL;
    } else if (sample->desc->properties == s_sensor_loading_properties) {
        if (sample->desc->key == NULL) {
            new->pattern = NULL;
            new->id = 0;
        } else {
            new->pattern =
                strdup(STR_CHECKNULL(((sensor_loadinginfo_t *)sample->desc->key)->pattern));
            new->id = ((sensor_loadinginfo_t *)sample->desc->key)->id;
        }
    } else {
        char * pattern = NULL;
        asprintf(&pattern, "%s/%s",
                 SENSOR_DESC_FAMNAME(sample->desc), SENSOR_DESC_LABEL(sample->desc));
        new->pattern = pattern;
        new->id = 0;
    }

    if (*pdata == NULL || new->id <= (*pdata)->id) {
        new->next = *pdata;
        *pdata = new;
        return SENSOR_SUCCESS;
    }
    for (list = *pdata; list->next != NULL && new->id > list->next->id; list = list->next)
        ; /* nothing but loop */
    new->next = list->next;
    list->next = new;

    return SENSOR_SUCCESS;
}

/* ************************************************************************ */
static sensor_status_t sensor_family_reload(
                            sensor_family_t *       family) {
    char                        pattern[SENSOR_LABEL_SIZE];
    sensor_family_reload_t *    data = NULL;

    snprintf(pattern, sizeof(pattern) / sizeof(*pattern), "%s/*", family->info->name);

    /* keep the watched sensors and their watch parameters, then un-watch them
     * (they are patterns, they must be expanded with sensor_watch_add_unlocked) */
    sensor_watch_find_unlocked(family->sctx, pattern, SSF_DEFAULT, NULL,
                               sensor_family_reload_visit, &data);
    sensor_watch_del(family->sctx, pattern, SSF_DEFAULT);

    /* ask family for a new list of <sensor_desc_t *> */
    sensor_family_list_sensors(family, NULL);

    /* re-add the previous watches, hopefully some sensors will match this time
     * and free the 'fake' descs */
    while (data != NULL) {
        sensor_desc_match_t         matchdata;
        sensor_family_reload_t *    to_free = data;
        data = data->next;
        if (to_free->pattern != NULL
        && sensor_desc_match_get(&matchdata, to_free->pattern, SSF_DEFAULT) == SENSOR_SUCCESS) {
            SLIST_FOREACH_DATA(family->sctx->sensorlist, desc, sensor_desc_t *) {
                if (to_free->family == desc->family
                &&  sensor_desc_match_unlocked(desc, &matchdata)) {
                    LOG_DEBUG(family->sctx->log, "%s(): RESTORE family %s, pattern:%s",
                              __func__, SENSOR_FAM_NAME(to_free->family), to_free->pattern);
                    sensor_watch_add_desc_unlocked(family->sctx, desc,
                                                   SSF_DEFAULT, &(to_free->watchparam));
                }
            }
            free((void *) (to_free->pattern));
        }
        free(to_free);
    }
    /* notify families about reload */
    SLIST_FOREACH_DATA(family->sctx->families, it_fam, sensor_family_t *) {
        if (it_fam->info->notify != NULL) {
            family->sctx->evdata->family = family;
            it_fam->info->notify(SWE_FAMILY_RELOADED, it_fam, NULL, family->sctx->evdata);
        }
    }
    return SENSOR_SUCCESS;
}

/* ************************************************************************ */
static inline sensor_status_t sensor_update_check_internal(
                                sensor_sample_t *           sensor,
                                const struct timeval *      now) {
    if (sensor->desc->family->info->update != NULL) {
        if (now == NULL || timercmp(now, &(sensor->next_update_time), >=)) {
            sensor_status_t ret;
            sensor_value_t  *p_prev_value;

            if (SENSOR_VALUE_IS_BUFFER(sensor->value.type)) {
                p_prev_value = &(sensor->desc->family->sctx->work_buffer);
            } else {
                p_prev_value = &(sensor->desc->family->sctx->work_value);
            }
            sensor_value_copy(p_prev_value, &(sensor->value)); // FIXME perfs
            if ((ret = sensor->desc->family->info->update(sensor, now)) == SENSOR_UNCHANGED) {
                /* nothing */
            } else if (ret == SENSOR_UPDATED) {
                /* nothing except callback */
                if (sensor->watch->callback != NULL) {
                    sensor->watch->callback(SWE_WATCH_UPDATED, sensor->desc->family->sctx,
                                            sensor, NULL);
                }
            }
            else if (ret == SENSOR_SUCCESS || ret == SENSOR_LOADING) {
                LOG_SCREAM(sensor->desc->family->log, "%s/%s: forced comparison",
                           SENSOR_DESC_FAMNAME(sensor->desc), SENSOR_DESC_LABEL(sensor->desc));
                if (ret == SENSOR_LOADING) {
                    now = NULL; /* we return without updating the timer in order to reload asap */
                }
                if ((sensor->next_update_time.tv_sec == 0
                     && sensor->next_update_time.tv_usec == 0)
                ||  sensor_value_equal(p_prev_value, &sensor->value) == 0) {
                    ret = SENSOR_UPDATED;
                } else {
                    ret = SENSOR_UNCHANGED;
                }
            } else {
                if (ret == SENSOR_RELOAD_FAMILY) {
                    sensor_family_t *       family      = sensor->desc->family;
                    sensor_watch_callback_t callback    = sensor->watch->callback;
                    sensor_lock_upgrade(family->sctx);
                    sensor_family_reload(family);
                    if (callback != NULL) {
                        family->sctx->evdata->family = family;
                        callback(SWE_FAMILY_RELOADED, family->sctx, NULL, family->sctx->evdata);
                    }
                    return ret;
                }
                return SENSOR_ERROR;
            }
            if (now != NULL) {
                timeradd(&(sensor->watch->update_interval), now, &(sensor->next_update_time));
            }
            return ret;
        }
        return SENSOR_WAIT_TIMER;
    }
    return SENSOR_NOT_SUPPORTED;
}


/* ************************************************************************ */
static sensor_status_t sensor_init_wait_desc_unlocked(sensor_desc_t * desc, int b_onlywatched) {
    sensor_status_t     ret;
    sensor_sample_t *   sample;
    sensor_watch_t      watch = SENSOR_WATCH_INITIALIZER(1000, NULL);
    char                label[SENSOR_LABEL_SIZE];
    int                 bdelete = 0;
    
    if (desc->label != s_sensor_loading_label) {
        return SENSOR_SUCCESS;
    }
    snprintf(label, PTR_COUNT(label), "%s/*", SENSOR_DESC_FAMNAME(desc));
    if ((sample = sensor_watch_find_unlocked(desc->family->sctx, label, SSF_NONE,
                                             NULL, NULL, NULL)) == NULL) {
        if (b_onlywatched)
            return SENSOR_NOT_SUPPORTED;                                     
        if ((sample = sensor_watch_add_desc_unlocked(desc->family->sctx, desc, SSF_NOPATTERN, &watch))
                == NULL) {
            return SENSOR_ERROR;
        }
        bdelete = 1;
        snprintf(label, PTR_COUNT(label), "%s/%s",
                 SENSOR_DESC_FAMNAME(desc), SENSOR_DESC_LABEL(desc));
    }

    LOG_INFO(desc->family->sctx->log, "waiting until %s is loaded...", SENSOR_DESC_FAMNAME(desc));
    
    /* event SWE_FAMILY_WAIT_LOAD will ask family to wait until it is fully loaded */
    if (desc->family->info->notify != NULL) {
        desc->family->info->notify(SWE_FAMILY_WAIT_LOAD, desc->family, NULL, NULL);
    }
    while ((ret = desc->family->info->update(sample, NULL)) == SENSOR_LOADING) {
        LOG_DEBUG(desc->family->sctx->log, "waiting for %s", SENSOR_DESC_FAMNAME(desc));
        usleep(1000);
    }

    if (bdelete) {
        sensor_watch_del_unlocked(desc->family->sctx, label, SSF_NOPATTERN);
    }
    if (ret == SENSOR_RELOAD_FAMILY) {
        sensor_family_reload(desc->family);
    }
    return ret;
}

/* ************************************************************************ */
sensor_status_t sensor_init_wait_desc(sensor_desc_t * desc, int b_onlywatched) {
    sensor_status_t ret;
    if (desc == NULL)
        return SENSOR_ERROR;
    sensor_lock(desc->family->sctx, SENSOR_LOCK_WRITE);
    ret = sensor_init_wait_desc_unlocked(desc, b_onlywatched);
    sensor_unlock(desc->family->sctx);    
    return ret;
}

/* ************************************************************************ */
sensor_status_t sensor_init_wait(sensor_ctx_t * sctx, int b_onlywatched) {
    int                 breload;

    if (sctx == NULL) {
        return SENSOR_ERROR;
    }
    sensor_lock(sctx, SENSOR_LOCK_WRITE);

    LOG_INFO(sctx->log, "waiting until%s sensors are loaded...", b_onlywatched ? " watched" : "");

    do {
        breload = 0;
        SLISTC_FOREACH_DATA(sctx->sensorlist, desc, sensor_desc_t *) {
            if (sensor_init_wait_desc_unlocked(desc, b_onlywatched) == SENSOR_RELOAD_FAMILY) {
                breload = 1;
                break ;
            }
        }
    } while (breload);

    sensor_unlock(sctx);
    return SENSOR_SUCCESS;
}

/* ************************************************************************ */
sensor_status_t sensor_now(struct timeval * now) {
    struct timespec ts;

    if (now == NULL) {
        return SENSOR_ERROR;
    }

    if (vclock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0) {
        return SENSOR_ERROR;
    }
    now->tv_sec = ts.tv_sec;
    now->tv_usec = ts.tv_nsec / 1000;
    return SENSOR_SUCCESS;
}

/* ************************************************************************ */
sensor_status_t sensor_update_check(sensor_sample_t * sensor, const struct timeval * now) {
    return sensor_update_check_internal(sensor, now);
}

/* ************************************************************************ */
slist_t * sensor_update_get(sensor_ctx_t *sctx, const struct timeval * now) {
    slist_t *updates = NULL;
    struct timeval snow;

    if (sctx == NULL) {
        return NULL;
    }
    if (now == NULL) {
        struct timespec ts;
        if (vclock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0) {
            LOG_ERROR(sctx->log, "error vclock_gettime: %s, in %s", strerror(errno), __func__);
            return NULL;
        }
        snow.tv_sec = ts.tv_sec;
        snow.tv_usec = ts.tv_nsec / 1000;
        now = &snow;
    }

    sensor_lock(sctx, SENSOR_LOCK_READ);
    if (sctx->watchlist == NULL) {
        LOG_VERBOSE(sctx->log, "warning in %s(): watch list is empty", __func__);
        sensor_unlock(sctx);
        return NULL;
    }
    SLIST_FOREACH_DATA(sctx->watchlist, sensor, sensor_sample_t *) {
        sensor_status_t ret = sensor_update_check_internal(sensor, now);
        if (ret == SENSOR_UPDATED) {
            updates = slist_prepend(updates, sensor);
        } else if (ret == SENSOR_UNCHANGED) {
            continue ;
        } else if (ret == SENSOR_RELOAD_FAMILY) {
            sensor_update_free(updates);
            updates = NULL;
            break ;
        } else if (ret == SENSOR_ERROR) {
            LOG_ERROR(sctx->log, "sensor '%s' update error",
                      SENSOR_DESC_LABEL(sensor->desc));
        }
    }
    sensor_unlock(sctx);
    return updates;
}

/* ************************************************************************ */
void sensor_update_free(slist_t * updates) {
    slist_free(updates, NULL);
}


/***************************************************************************
 * SENSOR_PROPERTIES
 ***************************************************************************/
sensor_property_t * sensor_properties_create(unsigned int count) {
    sensor_property_t * properties;

    if ((properties = malloc((count + 1) * sizeof(*properties))) == NULL) {
        return NULL;
    }
    for (unsigned int i = 0; i <= count; ++i) {
        properties[i].name = NULL;
        properties[i].value.type = SENSOR_VALUE_NULL;
    }
    return properties;
}

sensor_property_t * sensor_property_create() {
    return sensor_properties_create(0);
}

static inline void sensor_property_free_data(sensor_property_t * property) {
    if (property->name != NULL) {
        free((void *) (property->name));
    }
    if (SENSOR_VALUE_IS_BUFFER(property->value.type)
    &&  property->value.data.b.buf != NULL && property->value.data.b.maxsize > 0) {
        free(property->value.data.b.buf);
    }
}

void sensor_properties_free(sensor_property_t * properties) {
    if (properties != NULL) {
        for (sensor_property_t * property = properties;
                SENSOR_PROPERTY_VALID(property); ++property) {
            sensor_property_free_data(property);
        }
        free(properties);
    }
}

void sensor_property_free(sensor_property_t * property) {
    if (property != NULL) {
        sensor_property_free_data(property);
        free(property);
    }
}

sensor_status_t sensor_property_init(sensor_property_t * property, const char * name) {
    if (property != NULL) {
        property->name = strdup(STR_CHECKNULL(name));
        memset(&(property->value), 0xff, sizeof(property->value));
        property->value.type = SENSOR_VALUE_NULL;
    }
    return SENSOR_SUCCESS;
}

/***************************************************************************
 * SENSOR_VALUE : src/sensor_value.c
 ***************************************************************************/


/***************************************************************************
 * SENSOR_PRIVATE :
 ***************************************************************************/


/* ************************************************************************
 * getversion() / getsource()
 * ************************************************************************ */

/************************************************************************** */
static const char * s_libvsensors_version
    = OPT_VERSION_STRING(BUILD_APPNAME, APP_VERSION, "git:" BUILD_GITREV);

const char * libvsensors_get_version() {
    return s_libvsensors_version;
}

/************************************************************************** */
#ifndef APP_INCLUDE_SOURCE
static const char * s_libvsensors_no_source_string
    = "\n/* #@@# FILE #@@# " BUILD_APPNAME "/* */\n" \
      BUILD_APPNAME " source not included in this build.\n";

int libvsensors_get_source(FILE * out, char * buffer, unsigned int buffer_size, void ** ctx) {
    return vdecode_buffer(out, buffer, buffer_size, ctx, s_libvsensors_no_source_string,
                          strlen(s_libvsensors_no_source_string));
}
#endif

