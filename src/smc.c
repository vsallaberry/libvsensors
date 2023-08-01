/*
 * Copyright (C) 2017-2020,2023 Vincent Sallaberry
 * libvsensors <https://github.com/vsallaberry/libvsensors>
 *
 * For smc_get_value() data conversion and macros DATATYPE_*:
 * + Portions Copyright (C) 2006 devnull
 * + Portions Copyright (C) 2017 Hendrik Holtmann <https://github.com/hholtmann>
 * + Portions Copyright (C) 2013 Michael Wilber
 *
 * Credits to devnull <https://github.com/hholtmann> and Michael Wilber
 * for the AppleSMC driver interface (src/sysdeps/smc-darwin.[ch])
 * inspired from their smcFanControl (GPL 2):
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
 * SMC interface for Generic Sensor Management Library.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdint.h>
#include <inttypes.h>
#include <arpa/inet.h>
#include <errno.h>

#include "vlib/log.h"
#include "vlib/job.h"
#include "vlib/slist.h"

#include "libvsensors/sensor.h"

#include "smc.h"

/* ************************************************************************ */
sensor_status_t     sysdep_smc_support(sensor_family_t * family, const char * label);
int                 sysdep_smc_open(void ** psmc_handle, log_t *log,
                                    unsigned int * bufsize, unsigned int * value_offset);
int                 sysdep_smc_close(void * smc_handle, log_t *log);
int                 sysdep_smc_readkey(
                        uint32_t        key,
                        uint32_t *      value_type,
                        void **         key_info,
                        void *          output_buffer,
                        void *          smc_handle,
                        log_t *         log);
int                 sysdep_smc_readindex(
                        uint32_t        index,
                        uint32_t *      value_key,
                        uint32_t *      value_type,
                        void **         key_info,
                        void *          output_buffer,
                        void *          smc_handle,
                        log_t *         log);
int                 sysdep_smc_writekey(
                        uint32_t        key,
                        uint32_t *      value_type,
                        void **         key_info,
                        void *          input_buffer,
                        uint32_t        input_size,
                        void *          smc_handle,
                        log_t *         log);

/* ************************************************************************ */
typedef struct {
    sensor_family_t *   family;
    void *              smc_handle;
    slist_t *           descs;
    unsigned int        output_bufsz;
    unsigned int        value_offset;
    slist_t *           free_list;
    char *              smc_buffer;
    slist_t *           jobs;
} smc_priv_t;

typedef sensor_status_t (*smc_format_fun_t)(
                                uint32_t type, uint32_t size,
                                char * bytes, sensor_value_t * value,
                                sensor_family_t * family);

typedef sensor_status_t (*smc_write_fun_t)(
                                uint32_t type, uint32_t size,
                                char * bytes, const sensor_value_t * value,
                                sensor_family_t * family);

typedef struct {
    uint32_t            value_key;
    uint32_t            value_type;
    uint32_t            value_size;
    uint32_t            value_index;
    smc_format_fun_t    format_fun;
    smc_write_fun_t     write_fun;
    void *              key_info;
} smc_desc_key_t;

/* ************************************************************************ */
static sensor_status_t  smc_getsensorvalue(
                            smc_desc_key_t *    key,
                            sensor_value_t *    value,
                            sensor_family_t *   family);

static sensor_status_t  smc_putsensorvalue(
                            smc_desc_key_t *    key,
                            const sensor_value_t *    value,
                            sensor_family_t *   family);

static sensor_status_t  smc_list(sensor_family_t * family);
static void             smc_free_desc(void * data);

/* ************************************************************************ */
static sensor_status_t smc_family_free(sensor_family_t *family) {
    if (family == NULL ||  family->priv == NULL) {
        return SENSOR_ERROR;
    }
    smc_priv_t *        priv = family->priv;
    sensor_status_t     result;

    SLIST_FOREACH_DATA(priv->jobs, job, vjob_t *) {
        vjob_killandfree(job);
    }
    slist_free(priv->jobs, NULL);

    if (sysdep_smc_close(priv->smc_handle, family->log) == 0) {
        result = SENSOR_SUCCESS;
    } else {
       LOG_ERROR(family->log, "SMCClose() failed!");
       result = SENSOR_ERROR;
    }

    slist_free(priv->descs, smc_free_desc);
    slist_free(priv->free_list, free);
    if (priv->smc_buffer != NULL) {
        free(priv->smc_buffer);
    }
    family->priv = NULL;
    free(priv);

    return result;
}

/* ************************************************************************ */
static sensor_status_t smc_family_init(sensor_family_t *family) {
    if (sysdep_smc_support(family, NULL) != SENSOR_SUCCESS) {
        smc_family_free(family);
        return SENSOR_NOT_SUPPORTED;
    }
    if ((family->priv = calloc(1, sizeof(smc_priv_t))) == NULL) {
        smc_family_free(family);
        return SENSOR_ERROR;
    }
    smc_priv_t * priv = family->priv;
    priv->smc_buffer = NULL;
    priv->free_list = NULL;
    priv->smc_handle = NULL;
    priv->descs = NULL;
    if (sysdep_smc_open(&priv->smc_handle, family->log,
                        &(priv->output_bufsz), &(priv->value_offset)) != SENSOR_SUCCESS) {
       LOG_ERROR(family->log, "SMCOpen() failed!");
       smc_family_free(family);
       return SENSOR_ERROR;
    }
    if ((priv->smc_buffer = calloc(1, priv->output_bufsz)) == NULL) {
        LOG_ERROR(family->log, "malloc smc bytes buffer error: %s", strerror(errno));
        smc_family_free(family);
        return SENSOR_ERROR;
    }
    return SENSOR_SUCCESS;
}

/* ************************************************************************ */
static void * smc_list_job(void * vdata) {
    sensor_family_t *  family = (sensor_family_t *) vdata;
    sensor_status_t ret;
    int             old_ena, old_asy;

    /* disable vjob_kill without vjob_testkill */
    vjob_killmode(0, 0, &old_ena, &old_asy);

    /* do it */
    ret = smc_list(family);

    /* restore killmode */
    vjob_killmode(old_ena, old_asy, NULL, NULL);

    if (ret == SENSOR_SUCCESS) {
        return (void*)0;
    }
    return VJOB_ERR_RESULT;
}

static slist_t * smc_family_list(sensor_family_t *family) {
    smc_priv_t *    priv = (smc_priv_t *) family->priv;
    slist_t *       list = NULL, * last = NULL;

    if (priv->descs == NULL || priv->jobs != NULL) {
        if (priv->jobs == NULL) {
            priv->jobs = slist_prepend(priv->jobs, vjob_run(smc_list_job, family));
            if (priv->jobs == NULL || priv->jobs->data == NULL) {
                LOG_WARN(family->log, "cannot run listing job");
                priv->jobs = slist_remove_ptr(priv->jobs, priv->jobs->data);
                return NULL;
            }
        }
        return sensor_family_loading_list(family);
    }

    SLIST_FOREACH_DATA(priv->descs, desc, sensor_desc_t *) {
        last = slist_append(last, desc);
        if (list == NULL)
            list = last;
        else if (last != NULL)
            last = last->next;
    }
    LOG_DEBUG(family->log, "%s(): list length = %u", __func__, slist_length(list));

    return list;
}

/* ************************************************************************ */
static sensor_status_t smc_family_update(sensor_sample_t *sensor, const struct timeval * now) {
    (void)now;
    return (smc_getsensorvalue((smc_desc_key_t *) sensor->desc->key,
                               &(sensor->value), sensor->desc->family));
}

/* ************************************************************************ */
static sensor_status_t smc_family_write(const sensor_desc_t * sensordesc, const sensor_value_t * value) {
    return smc_putsensorvalue((smc_desc_key_t *) sensordesc->key, value, sensordesc->family);
}

/* ************************************************************************ */
extern const sensor_family_info_t g_sensor_family_smc_loaded;
static sensor_status_t smc_family_loading_update(sensor_sample_t *sensor, const struct timeval * now) {
    smc_priv_t * priv = (smc_priv_t *) sensor->desc->family->priv;
    (void)now;
    if (priv->jobs != NULL) {
        if (vjob_done(priv->jobs->data)) {
            slist_t * to_free = priv->jobs;
            priv->jobs = priv->jobs->next;
            vjob_free(to_free->data);
            slist_free_1(to_free, NULL);
            sensor->desc->family->info = &g_sensor_family_smc_loaded;
            LOG_VERBOSE(sensor->desc->family->log, "RELOAD_FAMILY");
            return SENSOR_RELOAD_FAMILY;
        } else {
            return SENSOR_LOADING;
        }
    }
    return SENSOR_ERROR;
}

/* ************************************************************************ */
static sensor_status_t sensor_family_loading_notify(
                    unsigned int event, struct sensor_family_s * family,
                    struct sensor_sample_s * sample, sensor_watch_ev_data_t * ev_data) {
    smc_priv_t * priv = (smc_priv_t *) family->priv;
    (void)sample;
    (void)ev_data;
    
    if ((event & SWE_FAMILY_WAIT_LOAD) != 0) {
        SLISTC_FOREACH_DATA(priv->jobs, job, vjob_t *) {
            vjob_wait(job);
        }
    }
    return SENSOR_SUCCESS;                                     
}
                                  
/* ************************************************************************ */
#define SMC_FAMILY_NAME "smc"
const sensor_family_info_t g_sensor_family_smc = {
    .name = SMC_FAMILY_NAME,
    .init = smc_family_init,
    .free = smc_family_free,
    .update = smc_family_loading_update,
    .list = smc_family_list,
    .notify = sensor_family_loading_notify,
    .write = smc_family_write
};
const sensor_family_info_t g_sensor_family_smc_loaded = {
    .name = SMC_FAMILY_NAME,
    .init = smc_family_init,
    .free = smc_family_free,
    .update = smc_family_update,
    .list = smc_family_list,
    .notify = NULL,
    .write = smc_family_write
};

/* ************************************************************************************* */

#define SMC_TYPE(str)       ((uint32_t)(((unsigned) ((str)[0])) << 24 | ((unsigned) ((str)[1])) << 16 \
                                        | ((unsigned) ((str)[2])) << 8 | (unsigned) ((str)[3])))

#define DATATYPE_FP1F       SMC_TYPE("fp1f")
#define DATATYPE_FP4C       SMC_TYPE("fp4c")
#define DATATYPE_FP5B       SMC_TYPE("fp5b")
#define DATATYPE_FP6A       SMC_TYPE("fp6a")
#define DATATYPE_FP79       SMC_TYPE("fp79")
#define DATATYPE_FP88       SMC_TYPE("fp88")
#define DATATYPE_FPA6       SMC_TYPE("fpa6")
#define DATATYPE_FPC4       SMC_TYPE("fpc4")
#define DATATYPE_FPE2       SMC_TYPE("fpe2")

#define DATATYPE_SP1E       SMC_TYPE("sp1e")
#define DATATYPE_SP3C       SMC_TYPE("sp3c")
#define DATATYPE_SP4B       SMC_TYPE("sp4b")
#define DATATYPE_SP5A       SMC_TYPE("sp5a")
#define DATATYPE_SP69       SMC_TYPE("sp69")
#define DATATYPE_SP78       SMC_TYPE("sp78")
#define DATATYPE_SP87       SMC_TYPE("sp87")
#define DATATYPE_SP96       SMC_TYPE("sp96")
#define DATATYPE_SPB4       SMC_TYPE("spb4")
#define DATATYPE_SPF0       SMC_TYPE("spf0")

#define DATATYPE_UI8        SMC_TYPE("ui8 ")
#define DATATYPE_UI16       SMC_TYPE("ui16")
#define DATATYPE_UI32       SMC_TYPE("ui32")

#define DATATYPE_SI8        SMC_TYPE("si8 ")
#define DATATYPE_SI16       SMC_TYPE("si16")
#define DATATYPE_SI32       SMC_TYPE("si32")

#define DATATYPE_PWM        SMC_TYPE("{pwm")

#define DATATYPE_FP00       SMC_TYPE("fp00")
#define DATATYPE_FP2E       SMC_TYPE("fp2e")
#define DATATYPE_FP3D       SMC_TYPE("fp3d")
#define DATATYPE_FP97       SMC_TYPE("fp97")
#define DATATYPE_FPB5       SMC_TYPE("fpb5")
#define DATATYPE_FPD3       SMC_TYPE("fpd3")
#define DATATYPE_FPF1       SMC_TYPE("fpf1")
#define DATATYPE_SP0F       SMC_TYPE("sp0f")
#define DATATYPE_SP2D       SMC_TYPE("sp2d")
#define DATATYPE_SPA5       SMC_TYPE("spa5")
#define DATATYPE_SPC3       SMC_TYPE("spc3")
#define DATATYPE_SPD2       SMC_TYPE("spd2")
#define DATATYPE_SPE1       SMC_TYPE("spe1")

#define DATATYPE_UI8_       SMC_TYPE("ui8")
#define DATATYPE_SI8_       SMC_TYPE("si8")
#define DATATYPE_FLAG       SMC_TYPE("flag")
#define DATATYPE_CHAR       SMC_TYPE("char")

#define DATATYPE_PCH8       SMC_TYPE("ch8*")

/* ************************************************************************ */
typedef struct {
    const char *key; //TODO would be better with uint32 but warnings on gcc
    const char *label;
    const char *fmt;
} smc_sensor_info_t;

#define SMC_TYPE2(str) (str)    // TODO
/* Smc sensors list is built dynamically (see smc_list()),
 * this is just to get description of known sensors */
static const smc_sensor_info_t s_smc_known_sensors[] = {
    /* BCTP = 1 on battery ? = 0, on AC ? */
    { SMC_TYPE2("BNum"), "Battery number", "%d" },
    { SMC_TYPE2("B0CT"), "Battery cycles", "%d" },
    { SMC_TYPE2("B0AC"), "Battery current (mA)", "%d" },
    { SMC_TYPE2("B0AV"), "Battery tension (mV)", "%d" },
    { SMC_TYPE2("B0FC"), "Battery Full capacity (mAh)", "%d" },
    { SMC_TYPE2("B0RM"), "Battery capacity (mAh)", "%d" },
    { SMC_TYPE2("FNum"), "Fan number", "%d" },
    { SMC_TYPE2("F0Ac"), "Fan0 CPU/RAM (rpm)", "%d" },
    { SMC_TYPE2("F0Tg"), "Fan0 target (rpm)", "%d" },
    { SMC_TYPE2("F0Mn"), "Fan0 min (rpm)", "%d" },
    { SMC_TYPE2("F0Mx"), "Fan0 max (rpm)", "%d" },
    { SMC_TYPE2("F1Ac"), "Fan1 Exhaust (rpm)", "%d" },
    { SMC_TYPE2("F1Tg"), "Fan1 target (rpm)", "%d" },
    { SMC_TYPE2("F1Mn"), "Fan1 min (rpm)", "%d" },
    { SMC_TYPE2("F1Mx"), "Fan1 max (rpm)", "%d" },
    { SMC_TYPE2("F2Ac"), "Fan2 Expansion (rpm)", "%d" },
    { SMC_TYPE2("F2Tg"), "Fan2 target (rpm)", "%d" },
    { SMC_TYPE2("F2Mn"), "Fan2 min (rpm)", "%d" },
    { SMC_TYPE2("F2Mx"), "Fan2 max (rpm)", "%d" },
    { SMC_TYPE2("F3Ac"), "Fan3 Power Supply (rpm)", "%d" },
    { SMC_TYPE2("F3Tg"), "Fan3 target (rpm)", "%d" },
    { SMC_TYPE2("F3Mn"), "Fan3 min (rpm)", "%d" },
    { SMC_TYPE2("F3Mx"), "Fan3 max (rpm)", "%d" },
    { SMC_TYPE2("FS! "), "Fan speed mode", "%d" },
    { SMC_TYPE2("IB0R"), "I Battery Rail", "%d" },
    { SMC_TYPE2("IC0C"), "I CPU Core 1", "%d" },
    { SMC_TYPE2("IC0G"), "I CPU GFX 1", "%d" },
    { SMC_TYPE2("IC0M"), "I CPU Memory 1", "%d" },
    { SMC_TYPE2("IC0R"), "I CPU 1 Rail", "%d" },
    { SMC_TYPE2("IC1R"), "I CPU 2 Rail", "%d" },
    { SMC_TYPE2("IC1C"), "I CPU Core 2 (VccIO)", "%d" },
    { SMC_TYPE2("IC2C"), "I CPU Core 3 (VccSA)", "%d" },
    { SMC_TYPE2("IC5R"), "I CPU DRAM", "%d" },
    { SMC_TYPE2("IC8R"), "I CPU PLL", "%d" },
    { SMC_TYPE2("ID0R"), "I Mainboard S0 Rail", "%d" },
    { SMC_TYPE2("ID1R"), "I Mainboard S1 Rail", "%d" },
    { SMC_TYPE2("ID5R"), "I Mainboard S5 Rail", "%d" },
    { SMC_TYPE2("IG0C"), "I GPU Rail", "%d" },
    { SMC_TYPE2("IM0C"), "I Memory Controller", "%d" },
    { SMC_TYPE2("IM0R"), "I Memory Rail", "%d" },
    { SMC_TYPE2("IN0C"), "I MCH", "%d" },
    { SMC_TYPE2("IO0R"), "I Misc. Rail", "%d" },
    { SMC_TYPE2("IPBR"), "I Charger BMON", "%d" },
    { SMC_TYPE2("PB0R"), "W Battery Rail", "%d" },
    { SMC_TYPE2("PBLC"), "W Battery Rail", "%d" },
    { SMC_TYPE2("PC0R"), "W CPU S0 Rail", "%d" },
    { SMC_TYPE2("PC1R"), "W CPU S1 Rail", "%d" },
    { SMC_TYPE2("PC2R"), "W CPU S2 Rail", "%d" },
    { SMC_TYPE2("PC3R"), "W CPU S3 Rail", "%d" },
    { SMC_TYPE2("PC4R"), "W CPU S4 Rail", "%d" },
    { SMC_TYPE2("PC5R"), "W CPU S5 Rail", "%d" },
    { SMC_TYPE2("PC0C"), "W CPU Core 1", "%d" },
    { SMC_TYPE2("PC1C"), "W CPU Core 2", "%d" },
    { SMC_TYPE2("PC2C"), "W CPU Core 3", "%d" },
    { SMC_TYPE2("PC3C"), "W CPU Core 4", "%d" },
    { SMC_TYPE2("PC4C"), "W CPU Core 5", "%d" },
    { SMC_TYPE2("PC5C"), "W CPU Core 6", "%d" },
    { SMC_TYPE2("PC6C"), "W CPU Core 7", "%d" },
    { SMC_TYPE2("PC7C"), "W CPU Core 8", "%d" },
    { SMC_TYPE2("PCPC"), "W CPU Cores", "%d" },
    { SMC_TYPE2("PCPD"), "W CPU DRAM", "%d" },
    { SMC_TYPE2("PCPG"), "W CPU GFX", "%d" },
    { SMC_TYPE2("PCPL"), "W CPU Total", "%d" },
    { SMC_TYPE2("PCTR"), "W CPU Total", "%d" },
    { SMC_TYPE2("PD0R"), "W Mainboard S0 Rail", "%d" },
    { SMC_TYPE2("PD1R"), "W Mainboard S1 Rail", "%d" },
    { SMC_TYPE2("PD2R"), "W Mainboard 12V Rail", "%d" },
    { SMC_TYPE2("PD5R"), "W Mainboard S5 Rail", "%d" },
    { SMC_TYPE2("PDTR"), "W DC In Total", "%d" },
    { SMC_TYPE2("PG0R"), "W GPU Rail", "%d" },
    { SMC_TYPE2("PGTR"), "W GPU Total", "%d" },
    { SMC_TYPE2("PH02"), "W Main 3.3V Rail", "%d" },
    { SMC_TYPE2("PH05"), "W Main 5V Rail", "%d" },
    { SMC_TYPE2("PM0R"), "W Memory Rail", "%d" },
    { SMC_TYPE2("PN0C"), "W MCH", "%d" },
    { SMC_TYPE2("PN1R"), "W PCH Rail", "%d" },
    { SMC_TYPE2("PO0R"), "W Misc. Rail", "%d" },
    { SMC_TYPE2("PSTR"), "W System Total", "%d" },
    { SMC_TYPE2("Pp0R"), "W 12V Rail", "%d" },
    { SMC_TYPE2("TA0P"), "Temp Airflow 1", "%d" },
    { SMC_TYPE2("TA0S"), "Temp PCI Slot 1 Pos 1", "%d" },
    { SMC_TYPE2("TA1P"), "Temp Airflow 2", "%d" },
    { SMC_TYPE2("TA1S"), "Temp PCI Slot 1 Pos 2", "%d" },
    { SMC_TYPE2("TA2S"), "Temp PCI Slot 2 Pos 1", "%d" },
    { SMC_TYPE2("TA3S"), "Temp PCI Slot 2 Pos 2", "%d" },
    { SMC_TYPE2("TB0T"), "Temp Battery TS_MAX", "%d" },
    { SMC_TYPE2("TB1T"), "Temp Battery 1", "%d" },
    { SMC_TYPE2("TB2T"), "Temp Battery 2", "%d" },
    { SMC_TYPE2("TB3T"), "Temp Battery", "%d" },
    { SMC_TYPE2("TC0C"), "Temp CPU A Core 1", "%d" },
    { SMC_TYPE2("TC0D"), "Temp CPU 1 Package", "%d" },
    { SMC_TYPE2("TC0E"), "Temp CPU 1 E", "%d" },
    { SMC_TYPE2("TC0F"), "Temp CPU 1 F", "%d" },
    { SMC_TYPE2("TC0H"), "Temp CPU 1 Heatsink", "%d" },
    { SMC_TYPE2("TC0P"), "Temp CPU 1 Proximity", "%d" },
    { SMC_TYPE2("TC1C"), "Temp CPU A Core 2", "%d" },
    { SMC_TYPE2("TC1D"), "Temp CPU 2 Package", "%d" },
    { SMC_TYPE2("TC1E"), "Temp CPU 2 E", "%d" },
    { SMC_TYPE2("TC1F"), "Temp CPU 2 F", "%d" },
    { SMC_TYPE2("TC1H"), "Temp CPU 2 Heatsink", "%d" },
    { SMC_TYPE2("TC1P"), "Temp CPU 2 Proximity", "%d" },
    { SMC_TYPE2("TC2C"), "Temp CPU B Core 1", "%d" },
    { SMC_TYPE2("TC3C"), "Temp CPU B Core 2", "%d" },
    { SMC_TYPE2("TC4C"), "Temp CPU Core 4", "%d" },
    { SMC_TYPE2("TC5C"), "Temp CPU Core 5", "%d" },
    { SMC_TYPE2("TC6C"), "Temp CPU Core 6", "%d" },
    { SMC_TYPE2("TC7C"), "Temp CPU Core 7", "%d" },
    { SMC_TYPE2("TC8C"), "Temp CPU Core 8", "%d" },
    { SMC_TYPE2("TCAD"), "Temp CPU 1 Package Alt.", "%d" },
    { SMC_TYPE2("TCAH"), "Temp CPU 1 Heatsink Alt.", "%d" },
    { SMC_TYPE2("TCBD"), "Temp CPU 2 Package Alt.", "%d" },
    { SMC_TYPE2("TCBH"), "Temp CPU 2 Heatsink Alt.", "%d" },
    { SMC_TYPE2("TCGC"), "Temp PECI GPU", "%d" },
    { SMC_TYPE2("TCGc"), "Temp PECI GPU", "%d" },
    { SMC_TYPE2("TCSA"), "Temp PECI SA", "%d" },
    { SMC_TYPE2("TCSC"), "Temp PECI SA", "%d" },
    { SMC_TYPE2("TCSc"), "Temp PECI SA", "%d" },
    { SMC_TYPE2("TCXC"), "Temp PECI CPU", "%d" },
    { SMC_TYPE2("TCXc"), "Temp PECI CPU", "%d" },
    { SMC_TYPE2("TG0D"), "Temp GPU Die", "%d" },
    { SMC_TYPE2("TG0H"), "Temp GPU Heatsink", "%d" },
    { SMC_TYPE2("TG0P"), "Temp GPU Proximity", "%d" },
    { SMC_TYPE2("TG1D"), "Temp GPU Die", "%d" },
    { SMC_TYPE2("TG1H"), "Temp GPU Heatsink", "%d" },
    { SMC_TYPE2("TH0P"), "Temp Drive Bay 1", "%d" },
    { SMC_TYPE2("TH1P"), "Temp Drive Bay 2", "%d" },
    { SMC_TYPE2("TH2P"), "Temp Drive Bay 3", "%d" },
    { SMC_TYPE2("TH3P"), "Temp Drive Bay 4", "%d" },
    { SMC_TYPE2("TI0P"), "Temp Thunderbolt 1 Proximity", "%d" },
    { SMC_TYPE2("TI1P"), "Temp Thunderbolt 2 Proximity", "%d" },
    { SMC_TYPE2("TL0P"), "Temp LCD Proximity", "%d" },
    { SMC_TYPE2("TM0P"), "Temp Mem Bank A1", "%d" },
    { SMC_TYPE2("TM0S"), "Temp Mem Module A1", "%d" },
    { SMC_TYPE2("TM1P"), "Temp Mem Bank A2", "%d" },
    { SMC_TYPE2("TM1S"), "Temp Mem Module A2", "%d" },
    { SMC_TYPE2("TM8P"), "Temp Mem Bank B1", "%d" },
    { SMC_TYPE2("TM8S"), "Temp Mem Module B1", "%d" },
    { SMC_TYPE2("TM9P"), "Temp Mem Bank B2", "%d" },
    { SMC_TYPE2("TM9S"), "Temp Mem Module B2", "%d" },
    { SMC_TYPE2("TMBS"), "Temp Memory Slot 2", "%d" },
    { SMC_TYPE2("TN0C"), "Temp MCH Die", "%d" },
    { SMC_TYPE2("TN0D"), "Temp Northbridge Die", "%d" },
    { SMC_TYPE2("TN0H"), "Temp MCH Heatsink", "%d" },
    { SMC_TYPE2("TN0P"), "Temp Northbridge Proximity", "%d" },
    { SMC_TYPE2("TN1P"), "Temp Northbridge Proximity 2", "%d" },
    { SMC_TYPE2("TO0P"), "Temp Optical Drive", "%d" },
    { SMC_TYPE2("TP0D"), "Temp PCH Die", "%d" },
    { SMC_TYPE2("TP0P"), "Temp PCH Proximity", "%d" },
    { SMC_TYPE2("TPCD"), "Temp PCH Die", "%d" },
    { SMC_TYPE2("TS0C"), "Temp Expansion Slots", "%d" },
    { SMC_TYPE2("TW0P"), "Temp Airport Proximity", "%d" },
    { SMC_TYPE2("Tb0P"), "Temp BLC Proximity", "%d" },
    { SMC_TYPE2("Th0H"), "Temp Heatpipe 1", "%d" },
    { SMC_TYPE2("Th1H"), "Temp Heatpipe 2", "%d" },
    { SMC_TYPE2("Th2H"), "Temp Heatpipe 3", "%d" },
    { SMC_TYPE2("Tm0P"), "Temp Mainboard Proximity", "%d" },
    { SMC_TYPE2("Tp0C"), "Temp Power Supply", "%d" },
    { SMC_TYPE2("Tp0P"), "Temp Power Supply 1", "%d" },
    { SMC_TYPE2("Tp1C"), "Temp Power Supply 2 Alt.", "%d" },
    { SMC_TYPE2("Tp1P"), "Temp Power Supply 2", "%d" },
    { SMC_TYPE2("Tp2P"), "Temp Power Supply 3", "%d" },
    { SMC_TYPE2("Tp3P"), "Temp Power Supply 4", "%d" },
    { SMC_TYPE2("Tp4P"), "Temp Power Supply 5", "%d" },
    { SMC_TYPE2("Tp5P"), "Temp Power Supply 6", "%d" },
    { SMC_TYPE2("Ts0P"), "Temp Palm Rest", "%d" },
    { SMC_TYPE2("Ts0S"), "Temp Memory Proximity", "%d" },
    { SMC_TYPE2("TCXC"), "Temp PECI CPU", "%d" },
    { SMC_TYPE2("VBAT"), "V Battery", "%d" },
    { SMC_TYPE2("VC0C"), "V CPU Core 1", "%d" },
    { SMC_TYPE2("VC1C"), "V CPU Core 2", "%d" },
    { SMC_TYPE2("VC2C"), "V CPU Core 3", "%d" },
    { SMC_TYPE2("VC3C"), "V CPU Core 4", "%d" },
    { SMC_TYPE2("VC4C"), "V CPU Core 5", "%d" },
    { SMC_TYPE2("VC5C"), "V CPU Core 6", "%d" },
    { SMC_TYPE2("VC6C"), "V CPU Core 7", "%d" },
    { SMC_TYPE2("VC7C"), "V CPU Core 8", "%d" },
    { SMC_TYPE2("VD0R"), "V Mainboard S0 Rail", "%d" },
    { SMC_TYPE2("VD2R"), "V Main 12V", "%d" },
    { SMC_TYPE2("VD5R"), "V Mainboard S5 Rail", "%d" },
    { SMC_TYPE2("VG0C"), "V GPU Core", "%d" },
    { SMC_TYPE2("VH05"), "V Main 5V", "%d" },
    { SMC_TYPE2("VM0R"), "V Memory", "%d" },
    { SMC_TYPE2("VN0C"), "V MCH", "%d" },
    { SMC_TYPE2("VN1R"), "V PCH", "%d" },
    { SMC_TYPE2("VP0R"), "V 12V Rail", "%d" },
    { SMC_TYPE2("VR3R"), "V Main 3.3V", "%d" },
    { SMC_TYPE2("VV1R"), "V CPU VTT", "%d" },
    { SMC_TYPE2("VV1S"), "V Main 5V", "%d" },
    { SMC_TYPE2("VV2S"), "V Main 3V", "%d" },
    { SMC_TYPE2("VV3S"), "V Standby 3V", "%d" },
    { SMC_TYPE2("VV7S"), "V Auxiliary 3V", "%d" },
    { SMC_TYPE2("VV8S"), "V Standby 5V", "%d" },
    { SMC_TYPE2("VV9S"), "V Main 12V", "%d" },
    { SMC_TYPE2("Vb0R"), "V CMOS Battery", "%d" },
    { SMC_TYPE2("VeES"), "V PCIe 12V", "%d" },
    { SMC_TYPE2("Vp0C"), "V 12V Vcc", "%d" },
    { 0, NULL, NULL },
};

/* ************************************************************************ */
static unsigned long _str32toul(const char * int32, unsigned int size, int base) {
    unsigned long   total = 0;
    unsigned int    i;

    for (i = 0; i < size; i++) {
        if (base == 16) {
            total += (((unsigned char)(int32[i])) << (size - 1 - i) * 8);
        } else {
            total += (((unsigned char)(int32[i])) << (size - 1 - i) * 8);
        }
    }
    return total;
}

/* ************************************************************************ */
static unsigned int _ultostr32(char * str32, unsigned int maxsize,
                               unsigned long ul, unsigned int size) {
    unsigned int len;

    if (maxsize == 0) {
        return 0;
    }
    for (len = 0; len < size && len < maxsize - 1; ++len) {
        str32[len] = (ul >> (size - len - 1) * 8) & 0xff;
    }
    str32[len] = 0;

    return len;
}

/* ************************************************************************ */
#define SMC_DEFINE_FORMAT_FUN(_TYPE_SUF, _VALUE_EXPR)                       \
static sensor_status_t smc_format##_TYPE_SUF(                               \
                            uint32_t type,                                  \
                            uint32_t size,                                  \
                            char * bytes,                                   \
                            sensor_value_t * value,                         \
                            sensor_family_t * family) {                     \
    (void)size;                                                             \
    (void)type;                                                             \
    (void)family;                                                           \
    SENSOR_VALUE_TYPE_X(SMC_SV_TYPE##_TYPE_SUF) newvalue = (_VALUE_EXPR);   \
    if (newvalue == SENSOR_VALUEP_GET(value, SMC_SV_TYPE##_TYPE_SUF))       \
        return SENSOR_UNCHANGED;                                            \
    SENSOR_VALUEP_GET(value, SMC_SV_TYPE##_TYPE_SUF) = newvalue;            \
    return SENSOR_UPDATED;                                                  \
}

/* ************************************************************************ */
#define SMC_DEFINE_WRITE_FUN(_TYPE_SUF, _VALUE_EXPR)                        \
static sensor_status_t smc_write##_TYPE_SUF(                                \
                            uint32_t type,                                  \
                            uint32_t size,                                  \
                            char * bytes,                                   \
                            const sensor_value_t * value,                   \
                            sensor_family_t * family) {                     \
    (void)size;                                                             \
    (void)type;                                                             \
    (void)family;                                                           \
    _VALUE_EXPR;                                                            \
    return SENSOR_UPDATED;                                                  \
}

/* from what i guessed:
 * DATATYPE = IDxy
 *
 * ID| Desc           | x     | y:                   | Result
 * --|----------------|-------|----------------------|--------------------------
 * FP| unsigned float,| ?,    | bit shift for divisor| ((uint16_t)V)/(1<< y)
 * FP| signed float,  | ?,    | bit shift for divisor| ((int16_t)V)/(1<< y)
 *   |                |       |                      |
 *   |                |       |                      |
 */
#define SMC_SV_TYPE_FP1F    SENSOR_VALUE_FLOAT
SMC_DEFINE_FORMAT_FUN(_FP1F, (ntohs(*((uint16_t *) bytes)) / 32768.0))
SMC_DEFINE_WRITE_FUN(_FP1F, *((uint16_t *) bytes) = htons((unsigned)(sensor_value_todouble(value) * 32768.0)))

/* FP2E guessed conversion from name and retro-engineering with P=UI */
#define SMC_SV_TYPE_FP2E    SENSOR_VALUE_FLOAT
SMC_DEFINE_FORMAT_FUN(_FP2E, ntohs(*((uint16_t *) bytes)) / 16384.0)
SMC_DEFINE_WRITE_FUN(_FP2E, *((uint16_t *) bytes) = htons((unsigned)(sensor_value_todouble(value) * 16384.0)))

/* FP3D guessed conversion from name and from retro-engineering with P=UI */
#define SMC_SV_TYPE_FP3D    SENSOR_VALUE_FLOAT
SMC_DEFINE_FORMAT_FUN(_FP3D, ntohs(*((uint16_t *) bytes)) / 8192.0)
SMC_DEFINE_WRITE_FUN(_FP3D, *((uint16_t *) bytes) = htons((unsigned)(sensor_value_todouble(value) * 8192.0)))

#define SMC_SV_TYPE_FP4C    SENSOR_VALUE_FLOAT
SMC_DEFINE_FORMAT_FUN(_FP4C, ntohs(*((uint16_t *) bytes)) / 4096.0)
SMC_DEFINE_WRITE_FUN(_FP4C, *((uint16_t *) bytes) = htons((unsigned)(sensor_value_todouble(value) * 4096.0)))

#define SMC_SV_TYPE_FP5B    SENSOR_VALUE_FLOAT
SMC_DEFINE_FORMAT_FUN(_FP5B, ntohs(*((uint16_t *) bytes)) / 2048.0)
SMC_DEFINE_WRITE_FUN(_FP5B, *((uint16_t *) bytes) = htons((unsigned)(sensor_value_todouble(value) * 2048.0)))

#define SMC_SV_TYPE_FP6A    SENSOR_VALUE_FLOAT
SMC_DEFINE_FORMAT_FUN(_FP6A, ntohs(*((uint16_t *) bytes)) / 1024.0)
SMC_DEFINE_WRITE_FUN(_FP6A, *((uint16_t *) bytes) = htons((unsigned)(sensor_value_todouble(value) * 1024.0)))

#define SMC_SV_TYPE_FP79    SENSOR_VALUE_FLOAT
SMC_DEFINE_FORMAT_FUN(_FP79, ntohs(*((uint16_t *) bytes)) / 512.0)
SMC_DEFINE_WRITE_FUN(_FP79, *((uint16_t *) bytes) = htons((unsigned)(sensor_value_todouble(value) * 512.0)))

#define SMC_SV_TYPE_FP88    SENSOR_VALUE_FLOAT
SMC_DEFINE_FORMAT_FUN(_FP88, ntohs(*((uint16_t *) bytes)) / 256.0)
SMC_DEFINE_WRITE_FUN(_FP88, *((uint16_t *) bytes) = htons((unsigned)(sensor_value_todouble(value) * 256.0)))

/* FP97 guessed from name */
#define SMC_SV_TYPE_FP97    SENSOR_VALUE_FLOAT
SMC_DEFINE_FORMAT_FUN(_FP97, ntohs(*((uint16_t *) bytes)) / 128.0)
SMC_DEFINE_WRITE_FUN(_FP97, *((uint16_t *) bytes) = htons((unsigned)(sensor_value_todouble(value) * 128.0)))

#define SMC_SV_TYPE_FPA6    SENSOR_VALUE_FLOAT
SMC_DEFINE_FORMAT_FUN(_FPA6, ntohs(*((uint16_t *) bytes)) / 64.0)
SMC_DEFINE_WRITE_FUN(_FPA6, *((uint16_t *) bytes) = htons((unsigned)(sensor_value_todouble(value) * 64.0)))

/* FPB5 guessed from name */
#define SMC_SV_TYPE_FPB5    SENSOR_VALUE_FLOAT
SMC_DEFINE_FORMAT_FUN(_FPB5, ntohs(*((uint16_t *) bytes)) / 32.0)
SMC_DEFINE_WRITE_FUN(_FPB5, *((uint16_t *) bytes) = htons((unsigned)(sensor_value_todouble(value) * 32.0)))

#define SMC_SV_TYPE_FPC4    SENSOR_VALUE_FLOAT
SMC_DEFINE_FORMAT_FUN(_FPC4, ntohs(*((uint16_t *) bytes)) / 16.0)
SMC_DEFINE_WRITE_FUN(_FPC4, *((uint16_t *) bytes) = htons((unsigned)(sensor_value_todouble(value) * 16.0)))

/* FPD3 guessed from name */
#define SMC_SV_TYPE_FPD3    SENSOR_VALUE_FLOAT
SMC_DEFINE_FORMAT_FUN(_FPD3, ntohs(*((uint16_t *) bytes)) / 8.0)
SMC_DEFINE_WRITE_FUN(_FPD3, *((uint16_t *) bytes) = htons((unsigned)(sensor_value_todouble(value) * 8.0)))

#define SMC_SV_TYPE_FPE2    SENSOR_VALUE_FLOAT
SMC_DEFINE_FORMAT_FUN(_FPE2, ntohs(*((uint16_t *) bytes)) / 4.0)
SMC_DEFINE_WRITE_FUN(_FPE2, *((uint16_t *) bytes) = htons((unsigned)(sensor_value_todouble(value) * 4.0)))

/* FPF1 guessed from name */
#define SMC_SV_TYPE_FPF1    SENSOR_VALUE_FLOAT
SMC_DEFINE_FORMAT_FUN(_FPF1, ntohs(*((uint16_t *) bytes)) / 2.0)
SMC_DEFINE_WRITE_FUN(_FPF1, *((uint16_t *) bytes) = htons((unsigned)(sensor_value_todouble(value) * 2.0)))

/* FP00 NOT sure, guessed from name */
#define SMC_SV_TYPE_FP00    SENSOR_VALUE_FLOAT
SMC_DEFINE_FORMAT_FUN(_FP00, ntohs(*((uint16_t *) bytes)))
SMC_DEFINE_WRITE_FUN(_FP00, *((uint16_t *) bytes) = htons((unsigned)(sensor_value_todouble(value))))

#define SMC_SV_TYPE_UI8     SENSOR_VALUE_UCHAR
SMC_DEFINE_FORMAT_FUN(_UI8, *((unsigned char *) bytes))
SMC_DEFINE_WRITE_FUN(_UI8, *((unsigned char *) bytes) = (char)sensor_value_todouble(value))

#define SMC_SV_TYPE_UI16    SENSOR_VALUE_UINT16
SMC_DEFINE_FORMAT_FUN(_UI16, (uint16_t) ntohs(*((uint16_t *) bytes)))
SMC_DEFINE_WRITE_FUN(_UI16, *((uint16_t *) bytes) = htons((unsigned)((unsigned)sensor_value_todouble(value))))

#define SMC_SV_TYPE_UI32    SENSOR_VALUE_UINT32
SMC_DEFINE_FORMAT_FUN(_UI32, (unsigned int) _str32toul((char *)(bytes), size, 10))
SMC_DEFINE_WRITE_FUN(_UI32, _ultostr32((char *) bytes, 4, (unsigned)(sensor_value_todouble(value)), 4)) //*((uint32_t *) bytes) = htons((unsigned)(sensor_value_todouble(value))))

/* SP0F guessed from name */
#define SMC_SV_TYPE_SP0F    SENSOR_VALUE_FLOAT
SMC_DEFINE_FORMAT_FUN(_SP0F, ((int16_t) ntohs(*((uint16_t *) bytes))) / 32768.0)
SMC_DEFINE_WRITE_FUN(_SP0F, *((uint16_t *) bytes) = htons((unsigned)(sensor_value_todouble(value) * 32768.0)))

#define SMC_SV_TYPE_SP1E    SENSOR_VALUE_FLOAT
SMC_DEFINE_FORMAT_FUN(_SP1E, ((int16_t) ntohs(*((uint16_t *) bytes))) / 16384.0)
SMC_DEFINE_WRITE_FUN(_SP1E, *((uint16_t *) bytes) = htons((unsigned)(sensor_value_todouble(value) * 16384.0)))

/* SP2D guessed from name */
#define SMC_SV_TYPE_SP2D    SENSOR_VALUE_FLOAT
SMC_DEFINE_FORMAT_FUN(_SP2D, ((int16_t) ntohs(*((uint16_t *) bytes))) / 8192.0)
SMC_DEFINE_WRITE_FUN(_SP2D, *((uint16_t *) bytes) = htons((unsigned)(sensor_value_todouble(value) * 8192.0)))

#define SMC_SV_TYPE_SP3C    SENSOR_VALUE_FLOAT
SMC_DEFINE_FORMAT_FUN(_SP3C, ((int16_t) ntohs(*((uint16_t *) bytes))) / 4096.0)
SMC_DEFINE_WRITE_FUN(_SP3C, *((uint16_t *) bytes) = htons((unsigned)(sensor_value_todouble(value) * 4096.0)))

#define SMC_SV_TYPE_SP4B    SENSOR_VALUE_FLOAT
SMC_DEFINE_FORMAT_FUN(_SP4B, ((int16_t) ntohs(*((uint16_t *) bytes))) / 2048.0)
SMC_DEFINE_WRITE_FUN(_SP4B, *((uint16_t *) bytes) = htons((unsigned)(sensor_value_todouble(value) * 2048.0)))

#define SMC_SV_TYPE_SP5A    SENSOR_VALUE_FLOAT
SMC_DEFINE_FORMAT_FUN(_SP5A, ((int16_t) ntohs(*((uint16_t *) bytes))) / 1024.0)
SMC_DEFINE_WRITE_FUN(_SP5A, *((uint16_t *) bytes) = htons((unsigned)(sensor_value_todouble(value) * 1024.0)))

#define SMC_SV_TYPE_SP69    SENSOR_VALUE_FLOAT
SMC_DEFINE_FORMAT_FUN(_SP69, ((int16_t) ntohs(*((uint16_t *) bytes))) / 512.0)
SMC_DEFINE_WRITE_FUN(_SP69, *((uint16_t *) bytes) = htons((unsigned)(sensor_value_todouble(value) * 512.0)))

#define SMC_SV_TYPE_SP78    SENSOR_VALUE_FLOAT
SMC_DEFINE_FORMAT_FUN(_SP78, ((int16_t) ntohs(*((uint16_t *) bytes))) / 256.0)
SMC_DEFINE_WRITE_FUN(_SP78, *((uint16_t *) bytes) = htons((unsigned)(sensor_value_todouble(value) * 256.0)))

#define SMC_SV_TYPE_SP87    SENSOR_VALUE_FLOAT
SMC_DEFINE_FORMAT_FUN(_SP87, ((int16_t) ntohs(*((uint16_t *) bytes))) / 128.0)
SMC_DEFINE_WRITE_FUN(_SP87, *((uint16_t *) bytes) = htons((unsigned)(sensor_value_todouble(value) * 128.0)))

#define SMC_SV_TYPE_SP96    SENSOR_VALUE_FLOAT
SMC_DEFINE_FORMAT_FUN(_SP96, ((int16_t) ntohs(*((uint16_t *) bytes))) / 64.0)
SMC_DEFINE_WRITE_FUN(_SP96, *((uint16_t *) bytes) = htons((unsigned)(sensor_value_todouble(value) * 64.0)))

/* SPA5 guessed */
#define SMC_SV_TYPE_SPA5    SENSOR_VALUE_FLOAT
SMC_DEFINE_FORMAT_FUN(_SPA5, ((int16_t) ntohs(*((uint16_t *) bytes))) / 32.0)
SMC_DEFINE_WRITE_FUN(_SPA5, *((uint16_t *) bytes) = htons((unsigned)(sensor_value_todouble(value) * 32.0)))

#define SMC_SV_TYPE_SPB4    SENSOR_VALUE_FLOAT
SMC_DEFINE_FORMAT_FUN(_SPB4, ((int16_t) ntohs(*((uint16_t *) bytes))) / 16.0)
SMC_DEFINE_WRITE_FUN(_SPB4, *((uint16_t *) bytes) = htons((unsigned)(sensor_value_todouble(value) * 16.0)))

/* SPC3 guessed */
#define SMC_SV_TYPE_SPC3    SENSOR_VALUE_FLOAT
SMC_DEFINE_FORMAT_FUN(_SPC3, ((int16_t) ntohs(*((uint16_t *) bytes))) / 8.0)
SMC_DEFINE_WRITE_FUN(_SPC3, *((uint16_t *) bytes) = htons((unsigned)(sensor_value_todouble(value) * 8.0)))

/* SPD2 guessed */
#define SMC_SV_TYPE_SPD2    SENSOR_VALUE_FLOAT
SMC_DEFINE_FORMAT_FUN(_SPD2, ((int16_t) ntohs(*((uint16_t *) bytes))) / 4.0)
SMC_DEFINE_WRITE_FUN(_SPD2, *((uint16_t *) bytes) = htons((unsigned)(sensor_value_todouble(value) * 4.0)))

/* SPE1 guessed */
#define SMC_SV_TYPE_SPE1    SENSOR_VALUE_FLOAT
SMC_DEFINE_FORMAT_FUN(_SPE1, ((int16_t) ntohs(*((uint16_t *) bytes))) / 2.0)
SMC_DEFINE_WRITE_FUN(_SPE1, *((uint16_t *) bytes) = htons((unsigned)(sensor_value_todouble(value) * 2.0)))

#define SMC_SV_TYPE_SPF0    SENSOR_VALUE_INT16
SMC_DEFINE_FORMAT_FUN(_SPF0, (int16_t) ntohs(*((uint16_t *) bytes)))
SMC_DEFINE_WRITE_FUN(_SPF0, *((uint16_t *) bytes) = htons((unsigned)(sensor_value_todouble(value))))

#define SMC_SV_TYPE_SI8     SENSOR_VALUE_CHAR
SMC_DEFINE_FORMAT_FUN(_SI8, (char) (*bytes))
SMC_DEFINE_WRITE_FUN(_SI8, *((char *) bytes) = (char)sensor_value_todouble(value))

#define SMC_SV_TYPE_SI16    SENSOR_VALUE_INT16
SMC_DEFINE_FORMAT_FUN(_SI16, (int16_t) ntohs(*((uint16_t *) bytes)))
SMC_DEFINE_WRITE_FUN(_SI16, *((int16_t *) bytes) = htons((unsigned)sensor_value_todouble(value)))

#define SMC_SV_TYPE_SI32   SENSOR_VALUE_INT32
SMC_DEFINE_FORMAT_FUN(_SI32, (int32_t) _str32toul((char *)(bytes), size, 10))
SMC_DEFINE_WRITE_FUN(_SI32, _ultostr32((char *) bytes, 4, (unsigned)(sensor_value_todouble(value)), 4))

#define SMC_SV_TYPE_FLAG    SENSOR_VALUE_UCHAR
SMC_DEFINE_FORMAT_FUN(_FLAG, (unsigned char)*(bytes))
SMC_DEFINE_WRITE_FUN(_FLAG, *((unsigned char *) bytes) = (char)sensor_value_todouble(value))

#define SMC_SV_TYPE_PWM     SENSOR_VALUE_FLOAT
SMC_DEFINE_FORMAT_FUN(_PWM, ntohs(*((uint16_t *)bytes)) * 100 / 65536.0)
SMC_DEFINE_WRITE_FUN(_PWM, *((uint16_t *) bytes) = htons((unsigned)(sensor_value_todouble(value) * 100 / 65536.0)))

#define SMC_SV_TYPE_BYTES   SENSOR_VALUE_BYTES
static sensor_status_t smc_format_bytes(uint32_t type, uint32_t size, char * bytes,
                     sensor_value_t * value, sensor_family_t * family) {
    (void)family;
    (void)type;
    value->data.b.size = size;
    return sensor_value_frombuffer(bytes, size, value);
}
static sensor_status_t smc_write_bytes(uint32_t type, uint32_t size, char * bytes,
                     const sensor_value_t * value, sensor_family_t * family) {
    (void)family;
    (void)type;
    return sensor_value_tostring(value, bytes, size);
}

/*} else if (value_type == DATATYPE_PCH8) {
//TODO
SENSOR_VALUE_INIT_BUF(*value, SENSOR_VALUE_STRING, value_bytes, value_size);*/

static sensor_status_t smc_getformatfun(smc_desc_key_t * key, uint32_t value_type, uint32_t value_size,
                                        sensor_value_t * value, sensor_family_t * family) {
    (void)family;

    if (value_size == 2 && value_type == DATATYPE_FP00) {
        value->type = SMC_SV_TYPE_FP00;
        key->format_fun = smc_format_FP00;
        key->write_fun = smc_write_FP00;
    } else if (value_size == 2 && value_type == DATATYPE_FP1F) {
        value->type = SMC_SV_TYPE_FP1F;
        key->format_fun = smc_format_FP1F; key->write_fun = smc_write_FP1F;
    } else if (value_size == 2 && value_type == DATATYPE_FP2E) {
        value->type = SMC_SV_TYPE_FP2E;
        key->format_fun = smc_format_FP2E; key->write_fun = smc_write_FP2E;
    } else if (value_size == 2 && value_type == DATATYPE_FP3D) {
        value->type = SMC_SV_TYPE_FP3D;
        key->format_fun = smc_format_FP3D; key->write_fun = smc_write_FP3D;
    } else if (value_size == 2 && value_type == DATATYPE_FP4C) {
        value->type = SMC_SV_TYPE_FP4C;
        key->format_fun = smc_format_FP4C; key->write_fun = smc_write_FP4C;
    } else if (value_size == 2 && value_type == DATATYPE_FP5B) {
        value->type = SMC_SV_TYPE_FP5B;
        key->format_fun = smc_format_FP5B; key->write_fun = smc_write_FP5B;
    } else if (value_size == 2 && value_type == DATATYPE_FP6A) {
        value->type = SMC_SV_TYPE_FP6A;
        key->format_fun = smc_format_FP6A; key->write_fun = smc_write_FP6A;
    } else if (value_size == 2 && value_type == DATATYPE_FP79) {
        value->type = SMC_SV_TYPE_FP79;
        key->format_fun = smc_format_FP79; key->write_fun = smc_write_FP79;
    } else if (value_size == 2 && value_type == DATATYPE_FP88) {
        value->type = SMC_SV_TYPE_FP88;
        key->format_fun = smc_format_FP88; key->write_fun = smc_write_FP88;
    } else if (value_size == 2 && value_type == DATATYPE_FP97) {
        value->type = SMC_SV_TYPE_FP97;
        key->format_fun = smc_format_FP97; key->write_fun = smc_write_FP97;
    } else if (value_size == 2 && value_type == DATATYPE_FPA6) {
        value->type = SMC_SV_TYPE_FPA6;
        key->format_fun = smc_format_FPA6; key->write_fun = smc_write_FPA6;
    } else if (value_size == 2 && value_type == DATATYPE_FPB5) {
        value->type = SMC_SV_TYPE_FPB5;
        key->format_fun = smc_format_FPB5; key->write_fun = smc_write_FPB5;
    } else if (value_size == 2 && value_type == DATATYPE_FPC4) {
        value->type = SMC_SV_TYPE_FPC4;
        key->format_fun = smc_format_FPC4; key->write_fun = smc_write_FPC4;
    } else if (value_size == 2 && value_type == DATATYPE_FPD3) {
        value->type = SMC_SV_TYPE_FPD3;
        key->format_fun = smc_format_FPD3; key->write_fun = smc_write_FPD3;
    } else if (value_size == 2 && value_type == DATATYPE_FPE2) {
        value->type = SMC_SV_TYPE_FPE2;
        key->format_fun = smc_format_FPE2; key->write_fun = smc_write_FPE2;
    } else if (value_size == 2 && value_type == DATATYPE_FPF1) {
        value->type = SMC_SV_TYPE_FPF1;
        key->format_fun = smc_format_FPF1; key->write_fun = smc_write_FPF1;
    } else if (value_size == 1
               && (value_type == DATATYPE_UI8 || value_type == DATATYPE_UI8_)) {
        value->type = SMC_SV_TYPE_UI8;
        key->format_fun = smc_format_UI8; key->write_fun = smc_write_UI8;
    } else if (value_size == 2 && value_type == DATATYPE_UI16) {
        value->type = SMC_SV_TYPE_UI16;
        key->format_fun = smc_format_UI16; key->write_fun = smc_write_UI16;
    } else if (value_size == 4 && value_type == DATATYPE_UI32) {
        value->type = SMC_SV_TYPE_UI32;
        key->format_fun = smc_format_UI32; key->write_fun = smc_write_UI32;
    } else if (value_size == 2 && value_type == DATATYPE_SP0F) {
        value->type = SMC_SV_TYPE_SP0F;
        key->format_fun = smc_format_SP0F; key->write_fun = smc_write_SP0F;
    } else if (value_size == 2 && value_type == DATATYPE_SP1E) {
        value->type = SMC_SV_TYPE_SP1E;
        key->format_fun = smc_format_SP1E; key->write_fun = smc_write_SP1E;
    } else if (value_size == 2 && value_type == DATATYPE_SP2D) {
        value->type = SMC_SV_TYPE_SP2D;
        key->format_fun = smc_format_SP2D; key->write_fun = smc_write_SP2D;
    } else if (value_size == 2 && value_type == DATATYPE_SP3C) {
        value->type = SMC_SV_TYPE_SP3C;
        key->format_fun = smc_format_SP3C; key->write_fun = smc_write_SP3C;
    } else if (value_size == 2 && value_type == DATATYPE_SP4B) {
        value->type = SMC_SV_TYPE_SP4B;
        key->format_fun = smc_format_SP4B; key->write_fun = smc_write_SP4B;
    } else if (value_size == 2 && value_type == DATATYPE_SP5A) {
        value->type = SMC_SV_TYPE_SP5A;
        key->format_fun = smc_format_SP5A; key->write_fun = smc_write_SP5A;
    } else if (value_size == 2 && value_type == DATATYPE_SP69) {
        value->type = SMC_SV_TYPE_SP69;
        key->format_fun = smc_format_SP69; key->write_fun = smc_write_SP69;
    } else if (value_size == 2 && value_type == DATATYPE_SP78) {
        value->type = SMC_SV_TYPE_SP78;
        key->format_fun = smc_format_SP78; key->write_fun = smc_write_SP78;
    } else if (value_size == 2 && value_type == DATATYPE_SP87) {
        value->type = SMC_SV_TYPE_SP87;
        key->format_fun = smc_format_SP87; key->write_fun = smc_write_SP87;
    } else if (value_size == 2 && value_type == DATATYPE_SP96) {
        value->type = SMC_SV_TYPE_SP96;
        key->format_fun = smc_format_SP96; key->write_fun = smc_write_SP96;
    } else if (value_size == 2 && value_type == DATATYPE_SPA5) {
        value->type = SMC_SV_TYPE_SPA5;
        key->format_fun = smc_format_SPA5; key->write_fun = smc_write_SPA5;
    } else if (value_size == 2 && value_type == DATATYPE_SPB4) {
        value->type = SMC_SV_TYPE_SPB4;
        key->format_fun = smc_format_SPB4; key->write_fun = smc_write_SPB4;
    } else if (value_size == 2 && value_type == DATATYPE_SPC3) {
        value->type = SMC_SV_TYPE_SPC3;
        key->format_fun = smc_format_SPC3; key->write_fun = smc_write_SPC3;
    } else if (value_size == 2 && value_type == DATATYPE_SPD2) {
        value->type = SMC_SV_TYPE_SPD2;
        key->format_fun = smc_format_SPD2; key->write_fun = smc_write_SPD2;
     } else if (value_size == 2 && value_type == DATATYPE_SPE1) {
        value->type = SMC_SV_TYPE_SPE1;
        key->format_fun = smc_format_SPE1; key->write_fun = smc_write_SPE1;
    } else if (value_size == 2 && value_type == DATATYPE_SPF0) {
        value->type = SMC_SV_TYPE_SPF0;
        key->format_fun = smc_format_SPF0; key->write_fun = smc_write_SPF0;
    } else if (value_size == 1 && (value_type == DATATYPE_SI8
                || value_type == DATATYPE_SI8_ || value_type == DATATYPE_CHAR)) {
        value->type = SMC_SV_TYPE_SI8;
        key->format_fun = smc_format_SI8; key->write_fun = smc_write_SI8;
    } else if (value_size == 2 && value_type == DATATYPE_SI16) {
        value->type = SMC_SV_TYPE_SI16;
        key->format_fun = smc_format_SI16; key->write_fun = smc_write_SI16;
    } else if (value_size == 4 && value_type == DATATYPE_SI32) {
        value->type = SMC_SV_TYPE_SI32;
        key->format_fun = smc_format_SI32; key->write_fun = smc_write_SI32;
    } else if (value_size == 1 && value_type == DATATYPE_FLAG) {
        value->type = SMC_SV_TYPE_FLAG;
        key->format_fun = smc_format_FLAG; key->write_fun = smc_write_FLAG;
    } else if (value_size == 2 && value_type == DATATYPE_PWM) {
        value->type = SMC_SV_TYPE_PWM;
        key->format_fun = smc_format_PWM; key->write_fun = smc_write_PWM;
    /*} else if (value_type == DATATYPE_PCH8) {
        //TODO
        SENSOR_VALUE_INIT_BUF(*value, SENSOR_VALUE_STRING, value_bytes, value_size);*/
    } else {
        #ifdef _DEBUG
        if (value_type != DATATYPE_PCH8) {
            char stype[5];
            _ultostr32(stype, sizeof(stype) / sizeof(*stype), value_type, sizeof(value_type));
            LOG_DEBUG(family->log, "warning: datatype '%s' not supported, using bytes", stype);
        }
        #endif
        value->data.b.size = 0;
        if (SENSOR_VALUE_IS_BUFFER(value->type) && value->data.b.buf != NULL) {
            if ((value->data.b.buf = realloc(value->data.b.buf, value_size * 2)) == NULL) {
                value->data.b.maxsize = 0;
            } else {
                value->data.b.maxsize = value_size * 2;
                memset(value->data.b.buf, 0xff, value->data.b.maxsize);
            }
        } else {
            value->data.b.buf = NULL;
            value->data.b.maxsize = 0;
        }
        value->type = SMC_SV_TYPE_BYTES;
        key->format_fun = smc_format_bytes; key->write_fun = smc_write_bytes;
    }
    return SENSOR_SUCCESS;
}

/* this is basically used only to get #KEY. smc_getsensorvalue() is prefered as it uses cached data */
static sensor_status_t smc_getvalue(
                            uint32_t            key,
                            sensor_value_t *    value,
                            sensor_family_t *   family)
{
    smc_priv_t *        priv = (smc_priv_t *) family->priv;
    uint32_t            value_type;
    int                 value_size;
    char *              value_bytes = priv->smc_buffer + priv->value_offset;
    smc_desc_key_t      outkey;

    value_size = sysdep_smc_readkey(key, &value_type, NULL, priv->smc_buffer,
                                    priv->smc_handle, family->log);

    if (value_size < 0) {
        LOG_ERROR(family->log, "cannot read SMC key '%08x' (bytes:%lx)",
                  key, (unsigned long) value_bytes);
        return SENSOR_ERROR;
    } else if (value_size == 0) {
        LOG_WARN(family->log, "SMC key '%08x': length <= 0", key);
        return SENSOR_ERROR;
    }

    /* smc read ok, check value and format it */
    if (smc_getformatfun(&outkey, value_type, value_size, value, family) != SENSOR_SUCCESS
    ||  outkey.format_fun == NULL) {
        return SENSOR_ERROR;
    }

    return outkey.format_fun(value_type, value_size, value_bytes, value, family);
}

static sensor_status_t smc_getsensorvalue(
                            smc_desc_key_t *    key,
                            sensor_value_t *    value,
                            sensor_family_t *   family)
{
    smc_priv_t *    priv = (smc_priv_t *) family->priv;
    unsigned int    value_size;
    char *          value_bytes = priv->smc_buffer + priv->value_offset;

    value_size = sysdep_smc_readkey(key->value_key, NULL, &(key->key_info),
                                    priv->smc_buffer, priv->smc_handle, family->log);

    if (value_size != key->value_size) {
        LOG_VERBOSE(family->log, "cannot read SMC key '%08x' (bytes:%lx,key_info:%lx,sz:%u,refsz:%u",
                 key->value_key, (unsigned long) value_bytes, (unsigned long)key->key_info,
                 value_size, key->value_size);
        return SENSOR_ERROR;
    }

    /* smc read ok, format value */
    return key->format_fun(key->value_type, value_size, value_bytes, value, family);
}

static sensor_status_t  smc_putsensorvalue(
                            smc_desc_key_t *    key,
                            const sensor_value_t *    value,
                            sensor_family_t *   family) {
    smc_priv_t *    priv = (smc_priv_t *) family->priv;
    unsigned int    value_size = key->value_size;
    char *          value_bytes = priv->smc_buffer + priv->value_offset;
    int             ret;

    if (key->write_fun(key->value_type, value_size, value_bytes, value, family) != SENSOR_UPDATED) {
        LOG_VERBOSE(family->log, "cannot convert value for SMC key '%08x' (bytes:%lx,key_info:%lx,sz:%u,refsz:%u",
                 key->value_key, (unsigned long) value_bytes, (unsigned long)key->key_info,
                 value_size, key->value_size);
        return SENSOR_ERROR;
    }

    LOG_BUFFER(LOG_LVL_DEBUG, family->log, value_bytes, value_size, " ");
    ret = sysdep_smc_writekey(key->value_key, NULL, &(key->key_info),
                              /*priv->smc_buffer*/value_bytes, key->value_size, priv->smc_handle, family->log);

    if (ret != SENSOR_SUCCESS) {
        LOG_VERBOSE(family->log, "cannot write SMC key '%08x' (bytes:%lx,key_info:%lx,sz:%u,refsz:%u",
                 key->value_key, (unsigned long) value_bytes, (unsigned long)key->key_info,
                 value_size, key->value_size);
        return SENSOR_ERROR;
    }

    return SENSOR_SUCCESS;
}

/* ************************************************************************ */
static void smc_free_desc(void * data) {
    if (data != NULL) {
        sensor_desc_t * desc = data;

        if (desc->label != NULL) {
            free((void*) (desc->label));
        }
        if (desc->key != NULL) {
            smc_desc_key_t * key = (smc_desc_key_t *) desc->key;
            desc->key = NULL;
            if (key->key_info != NULL) {
                free(key->key_info);
            }
            free(key);
        }
        sensor_properties_free(desc->properties);
        free(desc);
    }
}

/* ************************************************************************ */
enum {
    SMC_PROP_TYPE = 0,
    SMC_PROP_SIZE,
    SMC_PROP_KEY,
    SMC_PROP_INDEX,
    SMC_PROP_NB /* must be last */
};

/* ************************************************************************ */
static sensor_status_t smc_list(sensor_family_t * family) {
    sensor_status_t ret;
    smc_priv_t *    priv = (smc_priv_t *) family->priv;
    slist_t *       last, * new;
    int             value_size;
    uint32_t        value_type;
    uint32_t        value_key;
    sensor_value_t  value;
    int             total_keys, i;
    sensor_desc_t * desc;
    smc_desc_key_t* key;

    /* don't rebuild list */
    if (priv->descs != NULL) {
        return SENSOR_SUCCESS;
    }

    /* Get Number of SMC Keys */
    memset(&value, 0, sizeof(value));
    ret = smc_getvalue(SMC_TYPE("#KEY"), &value, family);

    if (ret != SENSOR_SUCCESS && ret != SENSOR_UPDATED
    &&  ret != SENSOR_UNCHANGED && ret != SENSOR_WAIT_TIMER) {
        LOG_WARN(family->log, "warning: cannot get number of smc keys");
        return SENSOR_ERROR;
    }
    total_keys = sensor_value_toint(&value);
    LOG_VERBOSE(family->log, "%s(): nb_keys = %d", __func__, total_keys);

    /* Scan each Key */
    last = NULL;
    for (i = 0; i < total_keys; i++) {
        char * label = NULL;

        /* cancelation point */
        vjob_testkill();

        /* alloc sensor_desc_t */
        if ((desc = malloc(sizeof(*desc))) == NULL) {
            LOG_WARN(family->log, "cannot allocate smc sensor desc for #%u", i);
            continue ;
        }
        desc->type = SENSOR_VALUE_NULL;
        desc->label = NULL;
        desc->family = family;

        /* alloc sensor_property_t */
        if ((desc->properties = sensor_properties_create(SMC_PROP_NB)) == NULL) {
            LOG_WARN(family->log, "cannot allocate smc properties for key #%u'", i);
            smc_free_desc(desc);
            continue ;
        }

        /* alloc smc_desc_key_t */
        if ((key = desc->key = malloc(sizeof(smc_desc_key_t))) == NULL) {
            LOG_WARN(family->log, "cannot allocate smc cache for key #%u'", i);
            smc_free_desc(desc);
            continue ;
        }
        key->key_info = NULL;
        key->format_fun = NULL;
        key->write_fun = NULL;

        /* ask key info to smc driver */
        value_size = sysdep_smc_readindex(i, &value_key, &value_type,
                        &(key->key_info), priv->smc_buffer, priv->smc_handle, family->log);

        if (value_size < 0) {
            LOG_WARN(family->log, "cannot get smc key info for #%u", i);
            smc_free_desc(desc);
            continue ;
        }
        key->value_key = value_key;
        key->value_type = value_type;
        key->value_index = i;
        key->value_size = value_size;

        /* get known human readable sensor label if exisiting */
        for (unsigned int i_desc = 0; i_desc < sizeof(s_smc_known_sensors)
                                               / sizeof(*s_smc_known_sensors); ++i_desc) {
            if (s_smc_known_sensors[i_desc].label != NULL) {
                if (value_key == SMC_TYPE(s_smc_known_sensors[i_desc].key)) {
                    size_t len = strlen(s_smc_known_sensors[i_desc].label) + 1 /*0*/ + 7 /*' {abcd}'*/;
                    if ((label = malloc(len * sizeof(char))) != NULL) {
                        snprintf(label, len, "%s {%s}",
                                 s_smc_known_sensors[i_desc].label, s_smc_known_sensors[i_desc].key);
                    }
                    break ;
                }
            }
        }

        /* init sensor value and its formating function */
        value.data.b.buf = NULL;
        if (smc_getformatfun(key, value_type, value_size, &value, family) != SENSOR_SUCCESS
        ||  key->format_fun == NULL) {
            LOG_WARN(family->log, "cannot decode SMC key '%08x', skipping...", value_key);
            smc_free_desc(desc);
            continue ;
        }
        desc->type = value.type;

        /* attribute a default label if not known */
        if (label == NULL) {
            if ((label = malloc((sizeof(value_key) + 3) * sizeof(char))) == NULL) {
                LOG_WARN(family->log, "cannot allocate smc sensor label for '%08x'", value_key);
                smc_free_desc(desc);
                continue ;
            }
            _ultostr32(label + 1, sizeof(value_key) + 1, value_key, sizeof(value_key));
            *(label) = '{';
            label[sizeof(value_key) + 1] = '}';
            label[sizeof(value_key) + 2] = 0;
        }
        desc->label = label;

        /* set sensor_properties */
        sensor_property_init(&(desc->properties[SMC_PROP_TYPE]), "smc-type");
        SENSOR_VALUE_INIT_BUF(desc->properties[SMC_PROP_TYPE].value, SENSOR_VALUE_STRING, NULL, 16);
        _ultostr32(desc->properties[SMC_PROP_TYPE].value.data.b.buf,
                   sizeof(value_type) + 1, value_type, sizeof(value_type));
        sensor_property_init(&(desc->properties[SMC_PROP_SIZE]), "smc-size");
        SENSOR_VALUE_INIT(desc->properties[SMC_PROP_SIZE].value, SENSOR_VALUE_UINT16, value_size);
        sensor_property_init(&(desc->properties[SMC_PROP_KEY]), "smc-key");
        SENSOR_VALUE_INIT_BUF(desc->properties[SMC_PROP_KEY].value, SENSOR_VALUE_STRING, NULL, 16);
        _ultostr32(desc->properties[SMC_PROP_KEY].value.data.b.buf,
                   sizeof(value_key) + 1, value_key, sizeof(value_key));
        sensor_property_init(&(desc->properties[SMC_PROP_INDEX]), "smc-index");
        SENSOR_VALUE_INIT(desc->properties[SMC_PROP_INDEX].value, SENSOR_VALUE_UINT32, i);

        /* finally add desc to list */
        if ((new = slist_prepend(NULL, desc)) == NULL) {
            LOG_WARN(family->log, "cannot append '%s' to smc sensor list!", desc->label);
            smc_free_desc(desc);
            continue ;
        }
        if (priv->descs == NULL) {
            priv->descs = new;
        } else {
            last->next = new;
        }
        last = new;
    }
    LOG_INFO(family->log, "sensors loaded");
    return SENSOR_SUCCESS;
}

