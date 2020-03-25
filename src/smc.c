/*
 * Copyright (C) 2017-2020 Vincent Sallaberry
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
#include "libvsensors/sensor.h"

#include "smc.h"

/* ************************************************************************ */
sensor_status_t     sysdep_smc_support(sensor_family_t * family, const char * label);
int                 sysdep_smc_open(void ** psmc_handle, log_t *log, unsigned int * maxsize);
int                 sysdep_smc_close(void ** psmc_handle, log_t *log);
int                 sysdep_smc_readkey(
                        uint32_t        key,
                        uint32_t *      value_type,
                        void *          value_bytes,
                        void *          smc_handle,
                        log_t *         log);
int                 sysdep_smc_readindex(
                        uint32_t        index,
                        uint32_t *      value_key,
                        uint32_t *      value_type,
                        void *          value_bytes,
                        void *          smc_handle,
                        log_t *         log);

/* ************************************************************************ */
static sensor_status_t  smc_getvalue(
                            uint32_t            key,
                            sensor_value_t *    value,
                            sensor_family_t *   family);

static sensor_status_t  smc_list(sensor_family_t * family);
static void             smc_free_desc(void * data);

/* ************************************************************************ */
typedef struct {
    sensor_family_t *   family;
    void *              smc_handle;
    slist_t *           descs;
    unsigned int        value_maxsize;
    slist_t *           free_list;
    char *              smc_buffer;
} smc_priv_t;

/* ************************************************************************ */
static sensor_status_t smc_family_free(sensor_family_t *family) {
    if (family == NULL ||  family->priv == NULL) {
        return SENSOR_ERROR;
    }
    smc_priv_t *        priv = family->priv;
    sensor_status_t     result;

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
    if (sysdep_smc_open(&priv->smc_handle, family->log, &priv->value_maxsize) != SENSOR_SUCCESS) {
       LOG_ERROR(family->log, "SMCOpen() failed!");
       smc_family_free(family);
       return SENSOR_ERROR;
    }
    if ((priv->smc_buffer = malloc(priv->value_maxsize)) == NULL) {
        LOG_ERROR(family->log, "malloc smc bytes buffer error: %s", strerror(errno));
        smc_family_free(family);
        return SENSOR_ERROR;
    }
    return SENSOR_SUCCESS;
}

/* ************************************************************************ */
static slist_t * smc_family_list(sensor_family_t *family) {
    if (family != NULL && family->priv != NULL) {
        smc_priv_t *    priv = (smc_priv_t *) family->priv;
        slist_t *       list = NULL;

        if (smc_list(family) != SENSOR_SUCCESS) {
            return NULL;
        }

        SLIST_FOREACH_DATA(priv->descs, desc, sensor_desc_t *) {
            list = slist_prepend(list, desc);
        }
        LOG_DEBUG(family->log, "%s(): list length = %u", __func__, slist_length(list));

        return list;
    } else {
       LOG_ERROR(family->log, "smc_list() failed!");
       return NULL;
    }
}

/* ************************************************************************ */
static sensor_status_t smc_family_update(sensor_sample_t *sensor, const struct timeval * now) {
    (void)now;
    return (smc_getvalue((uint32_t)((unsigned long)(sensor->desc->key)),
                         &(sensor->value), sensor->desc->family));
}

/* ************************************************************************ */
const sensor_family_info_t g_sensor_family_smc = {
    .name = "smc",
    .init = smc_family_init,
    .free = smc_family_free,
    .update = smc_family_update,
    .list = smc_family_list,
};

/* ************************************************************************************* */

#define SMC_TYPE(str)       ((uint32_t)((unsigned) (str)[0] << 24 | (unsigned) (str)[1] << 16 \
                                        | (unsigned) (str)[2] << 8 | (unsigned) (str)[3]))

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

#define DATATYPE_FP2E       SMC_TYPE("fp2e")
#define DATATYPE_FP3D       SMC_TYPE("fp3d")

#define DATATYPE_UI8_       SMC_TYPE("ui8")
#define DATATYPE_SI8_       SMC_TYPE("si8")
#define DATATYPE_FLAG       SMC_TYPE("flag")
#define DATATYPE_CHAR       SMC_TYPE("char")

/* ************************************************************************ */
typedef struct {
    const char *key; //TODO would be better with uint32 but warnings on gcc
    const char *label;
    const char *fmt;
} smc_sensor_info_t;

#define SMC_TYPE2(str) (str)    // TODO
/* Smc sensors list is built dynamically (see smc_list()),
 * this is just to get description of known sensors */
static const smc_sensor_info_t s_smc_sensors[] = {
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
    { SMC_TYPE2("IC0C"), "I CPU Core", "%d" },
    { SMC_TYPE2("IC0G"), "I CPU GFX", "%d" },
    { SMC_TYPE2("IC0M"), "I CPU Memory", "%d" },
    { SMC_TYPE2("IC0R"), "I CPU Rail", "%d" },
    { SMC_TYPE2("IC1C"), "I CPU VccIO", "%d" },
    { SMC_TYPE2("IC2C"), "I CPU VccSA", "%d" },
    { SMC_TYPE2("IC5R"), "I CPU DRAM", "%d" },
    { SMC_TYPE2("IC8R"), "I CPU PLL", "%d" },
    { SMC_TYPE2("ID0R"), "I Mainboard S0 Rail", "%d" },
    { SMC_TYPE2("ID5R"), "I Mainboard S5 Rail", "%d" },
    { SMC_TYPE2("IG0C"), "I GPU Rail", "%d" },
    { SMC_TYPE2("IM0C"), "I Memory Controller", "%d" },
    { SMC_TYPE2("IM0R"), "I Memory Rail", "%d" },
    { SMC_TYPE2("IN0C"), "I MCH", "%d" },
    { SMC_TYPE2("IO0R"), "I Misc. Rail", "%d" },
    { SMC_TYPE2("IPBR"), "I Charger BMON", "%d" },
    { SMC_TYPE2("PB0R"), "W Battery Rail", "%d" },
    { SMC_TYPE2("PBLC"), "W Battery Rail", "%d" },
    { SMC_TYPE2("PC0C"), "W CPU Core 1", "%d" },
    { SMC_TYPE2("PC0R"), "W Mainboard S0 Rail", "%d" },
    { SMC_TYPE2("PC1C"), "W CPU Core 2", "%d" },
    { SMC_TYPE2("PC1R"), "W CPU Rail", "%d" },
    { SMC_TYPE2("PC2C"), "W CPU Core 3", "%d" },
    { SMC_TYPE2("PC3C"), "W CPU Core 4", "%d" },
    { SMC_TYPE2("PC4C"), "W CPU Core 5", "%d" },
    { SMC_TYPE2("PC5C"), "W CPU Core 6", "%d" },
    { SMC_TYPE2("PC5R"), "W CPU S0 Rail", "%d" },
    { SMC_TYPE2("PC6C"), "W CPU Core 7", "%d" },
    { SMC_TYPE2("PC7C"), "W CPU Core 8", "%d" },
    { SMC_TYPE2("PCPC"), "W CPU Cores", "%d" },
    { SMC_TYPE2("PCPD"), "W CPU DRAM", "%d" },
    { SMC_TYPE2("PCPG"), "W CPU GFX", "%d" },
    { SMC_TYPE2("PCPL"), "W CPU Total", "%d" },
    { SMC_TYPE2("PCTR"), "W CPU Total", "%d" },
    { SMC_TYPE2("PD0R"), "W Mainboard S0 Rail", "%d" },
    { SMC_TYPE2("PD2R"), "W Main 12V Rail", "%d" },
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
    { SMC_TYPE2("TC0E"), "Temp CPU 1", "%d" },
    { SMC_TYPE2("TC0F"), "Temp CPU 1", "%d" },
    { SMC_TYPE2("TC0H"), "Temp CPU 1 Heatsink", "%d" },
    { SMC_TYPE2("TC0P"), "Temp CPU 1 Proximity", "%d" },
    { SMC_TYPE2("TC1C"), "Temp CPU A Core 2", "%d" },
    { SMC_TYPE2("TC1D"), "Temp CPU 2 Package", "%d" },
    { SMC_TYPE2("TC1E"), "Temp CPU 2", "%d" },
    { SMC_TYPE2("TC1F"), "Temp CPU 2", "%d" },
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
static sensor_status_t smc_getvalue(
                            uint32_t            key,
                            sensor_value_t *    value,
                            sensor_family_t *   family)
{
    smc_priv_t *    priv = (smc_priv_t *) family->priv;
    uint32_t        value_type;
    int             value_size;
    char *          value_bytes = priv->smc_buffer;

    value_size = sysdep_smc_readkey(key, &value_type, value_bytes, priv->smc_handle, family->log);

    if (value_size < 0) {
        LOG_ERROR(family->log, "cannot read SMC key '%08x'", key);
        return SENSOR_ERROR;
    } else if (value_size == 0) {
        LOG_WARN(family->log, "SMC key '%08x': length <= 0", key);
        return SENSOR_ERROR;
    }

    /* smc read ok, check value and format it */
    if (value_size == 2 && value_type == DATATYPE_FP1F) {
        SENSOR_VALUE_INIT(*value, SENSOR_VALUE_DOUBLE,
                ntohs(*((uint16_t *) value_bytes)) / 32768.0);
    } else if (value_size == 2 && value_type == DATATYPE_FP4C) {
        SENSOR_VALUE_INIT(*value, SENSOR_VALUE_DOUBLE,
                ntohs(*((uint16_t *) value_bytes)) / 4096.0);
    } else if (value_size == 2 && value_type == DATATYPE_FP5B) {
        SENSOR_VALUE_INIT(*value, SENSOR_VALUE_DOUBLE,
                ntohs(*((uint16_t *) value_bytes)) / 2048.0);
    } else if (value_size == 2 && value_type == DATATYPE_FP6A) {
        SENSOR_VALUE_INIT(*value, SENSOR_VALUE_DOUBLE,
                ntohs(*((uint16_t *) value_bytes)) / 1024.0);
    } else if (value_size == 2 && value_type == DATATYPE_FP79) {
        SENSOR_VALUE_INIT(*value, SENSOR_VALUE_DOUBLE,
                ntohs(*((uint16_t *) value_bytes)) / 512.0);
    } else if (value_size == 2 && value_type == DATATYPE_FP88) {
        SENSOR_VALUE_INIT(*value, SENSOR_VALUE_DOUBLE,
                ntohs(*((uint16_t *) value_bytes)) / 256.0);
    } else if (value_size == 2 && value_type == DATATYPE_FPA6) {
        SENSOR_VALUE_INIT(*value, SENSOR_VALUE_DOUBLE,
                ntohs(*((uint16_t *) value_bytes)) / 64.0);
    } else if (value_size == 2 && value_type == DATATYPE_FPC4) {
        SENSOR_VALUE_INIT(*value, SENSOR_VALUE_DOUBLE,
                ntohs(*((uint16_t *) value_bytes)) / 16.0);
    } else if (value_size == 2 && value_type == DATATYPE_FPE2) {
        SENSOR_VALUE_INIT(*value, SENSOR_VALUE_DOUBLE,
                ntohs(*((uint16_t *) value_bytes)) / 4.0);
    } else if (value_size > 0 && value_size <= 4
    && (value_type == DATATYPE_UI8 || value_type == DATATYPE_UI8_
        || value_type == DATATYPE_UI16 || value_type == DATATYPE_UI32)) {
        SENSOR_VALUE_INIT(*value, SENSOR_VALUE_UINT,
                (unsigned int) _str32toul((char *)(value_bytes), value_size, 10));
    } else if (value_size == 2 && value_type == DATATYPE_SP1E) {
        SENSOR_VALUE_INIT(*value, SENSOR_VALUE_DOUBLE,
                ((int16_t) ntohs(*((uint16_t *) value_bytes))) / 16384.0);
    } else if (value_size == 2 && value_type == DATATYPE_SP3C) {
        SENSOR_VALUE_INIT(*value, SENSOR_VALUE_DOUBLE,
                ((int16_t) ntohs(*((uint16_t *) value_bytes))) / 4096.0);
    } else if (value_size == 2 && value_type == DATATYPE_SP4B) {
        SENSOR_VALUE_INIT(*value, SENSOR_VALUE_DOUBLE,
                ((int16_t) ntohs(*((uint16_t *) value_bytes))) / 2048.0);
    } else if (value_size == 2 && value_type == DATATYPE_SP5A) {
        SENSOR_VALUE_INIT(*value, SENSOR_VALUE_DOUBLE,
                ((int16_t) ntohs(*((uint16_t *) value_bytes))) / 1024.0);
    } else if (value_size == 2 && value_type == DATATYPE_SP69) {
        SENSOR_VALUE_INIT(*value, SENSOR_VALUE_DOUBLE,
                ((int16_t) ntohs(*((uint16_t *) value_bytes))) / 512.0);
    } else if (value_size == 2 && value_type == DATATYPE_SP78) {
        SENSOR_VALUE_INIT(*value, SENSOR_VALUE_DOUBLE,
                ((int16_t) ntohs(*((uint16_t *) value_bytes))) / 256.0);
    } else if (value_size == 2 && value_type == DATATYPE_SP87) {
        SENSOR_VALUE_INIT(*value, SENSOR_VALUE_DOUBLE,
                ((int16_t) ntohs(*((uint16_t *) value_bytes))) / 128.0);
    } else if (value_size == 2 && value_type == DATATYPE_SP96) {
        SENSOR_VALUE_INIT(*value, SENSOR_VALUE_DOUBLE,
                ((int16_t) ntohs(*((uint16_t *) value_bytes))) / 64.0);
    } else if (value_size == 2 && value_type == DATATYPE_SPB4) {
        SENSOR_VALUE_INIT(*value, SENSOR_VALUE_DOUBLE,
                ((int16_t) ntohs(*((uint16_t *) value_bytes))) / 16.0);
    } else if (value_size == 2 && value_type == DATATYPE_SPF0) {
        SENSOR_VALUE_INIT(*value, SENSOR_VALUE_INT,
                (int16_t) ntohs(*((uint16_t *) value_bytes)));
    } else if (value_size > 0 && value_size <= 4
    && (value_type == DATATYPE_SI8 || value_type == DATATYPE_SI8_ || value_type == DATATYPE_CHAR
        || value_type == DATATYPE_SI16 || value_type == DATATYPE_SI32)) {
        SENSOR_VALUE_INIT(*value, SENSOR_VALUE_INT,
                (int) _str32toul((char *)(value_bytes), value_size, 10));
    } else if (value_size == 1 && value_type == DATATYPE_FLAG) {
        SENSOR_VALUE_INIT(*value, SENSOR_VALUE_UCHAR, (unsigned char)*(value_bytes));
    } else if (value_size == 2 && value_type == DATATYPE_PWM) {
        SENSOR_VALUE_INIT(*value, SENSOR_VALUE_DOUBLE,
                ntohs(*((uint16_t *)value_bytes)) * 100 / 65536.0);
    /*} else if (value_type == DATATYPE_PCH8) {
        //TODO
        SENSOR_VALUE_INIT_BUF(*value, SENSOR_VALUE_STRING, value_bytes, value_size);*/
    /*} else if (value_type == DATATYPE_FP2E) {
        // TODO
        SENSOR_VALUE_INIT(*value, SENSOR_VALUE_DOUBLE,
                ntohs(*((uint16_t *) value_bytes)) / 4.0); */
    /*} else if (value_type == DATATYPE_FP3D) {
        // TODO
        SENSOR_VALUE_INIT(*value, SENSOR_VALUE_DOUBLE,
                ntohs(*((uint16_t *) value_bytes)) / 4.0); */
    } else {
        /* TODO expand BYTES buffer ? */
        if (value->type == SENSOR_VALUE_NULL) {
            if ((value->data.b.buf = malloc(value_size * 2)) == NULL) {
                return SENSOR_ERROR;
            }
            priv->free_list = slist_prepend(priv->free_list, value->data.b.buf);
            value->data.b.maxsize = value_size * 2;
            value->type = SENSOR_VALUE_BYTES;
        } else if (value->type != SENSOR_VALUE_BYTES) {
            return SENSOR_ERROR;
        }
        value->data.b.size = value_size;
        if (sensor_value_fromraw(value_bytes, value) != SENSOR_SUCCESS) {
            return SENSOR_ERROR;
        }
    }

    return SENSOR_SUCCESS;
}

/* ************************************************************************ */
static void smc_free_desc(void * data) {
    if (data != NULL) {
        sensor_desc_t * desc = data;

        if (desc->label != NULL) {
            free(desc->label);
        }
        free(desc);
    }
}

/* ************************************************************************ */
static sensor_status_t smc_list(sensor_family_t * family) {
    sensor_status_t ret;
    smc_priv_t *    priv = (smc_priv_t *) family->priv;
    int             value_size;
    uint32_t        value_type;
    uint32_t        value_key;
    sensor_value_t  value;
    int             total_keys, i;
    sensor_desc_t * desc;

    /* don't rebuild list */
    if (priv->descs != NULL) {
        return SENSOR_SUCCESS;
    }

    /* Get Number of SMC Keys */
    if ((ret = smc_getvalue(SMC_TYPE("#KEY"), &value, family)) != SENSOR_SUCCESS) {
        LOG_WARN(family->log, "warning: cannot get number of smc keys");
        return SENSOR_ERROR;
    }
    total_keys = sensor_value_toint(&value);
    LOG_VERBOSE(family->log, "%s(): nb_keys = %d", __func__, total_keys);

    /* Scan each Key */
    for (i = 0; i < total_keys; i++) {
        value_size = sysdep_smc_readindex(i, &value_key, &value_type, NULL,
                        priv->smc_handle, family->log);

        if (value_size < 0) {
            continue;
        }

        if ((desc = malloc(sizeof(*desc))) == NULL) {
            continue ;
        }

        desc->type = SENSOR_VALUE_NULL;
        desc->label = NULL;
        for (unsigned int i_desc = 0; i_desc < sizeof(s_smc_sensors)
                                               / sizeof(*s_smc_sensors); ++i_desc) {
            if (s_smc_sensors[i_desc].label != NULL) {
                if (value_key == SMC_TYPE(s_smc_sensors[i_desc].key)) {
                    desc->label = strdup(s_smc_sensors[i_desc].label);
                    break ;
                }
            }
        }

        if (desc->label == NULL
        &&  (desc->label = malloc((sizeof(value_key) + 1) * sizeof(char))) != NULL) {
            _ultostr32(desc->label, sizeof(value_key) + 1, value_key, sizeof(value_key));
        }
        desc->family = family;
        desc->key = (void *)((unsigned long) value_key);

        if ((priv->descs = slist_prepend(priv->descs, desc)) == NULL
        ||  priv->descs->data != desc) {
            LOG_WARN(family->log, "cannot append '%s' to smc sensor list!", desc->label);
            smc_free_desc(desc);
        }
    }

    return SENSOR_SUCCESS;
}

