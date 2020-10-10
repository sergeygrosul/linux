/*
 * fm1288.c -- fm1288 ALSA SoC audio driver
 *
 * Copyright 2009 Wolfson Microelectronics plc
 * Copyright 2005 Openedhand Ltd.
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/of_gpio.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/tlv.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <linux/proc_fs.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include "fm1288.h"

#if 1
#define DBG(x...) printk(x)
#else
#define DBG(x...) do { } while (0)
#endif

#define INVALID_GPIO -1

/* FM1288 register space */
#define FM1288_PWRDWN_SET         0x22F1

/*
 * fm1288 register cache
 * We can't read the fm1288 register space when we
 * are using 2 wire for device control, so we cache them instead.
 */
static struct reg_default fm1288_reg[] = {
    {0x00, 0x06},
    {0x01, 0x1C},
    {0x02, 0xC3},
    {0x03, 0xFC},

    {0x04, 0xC0},
    {0x05, 0x00},
    {0x06, 0x00},
    {0x07, 0x7C},

    {0x08, 0x80},
    {0x09, 0x00},
    {0x0A, 0x00},
    {0x0B, 0x06},

    {0x0C, 0x00},
    {0x0D, 0x06},
    {0x0E, 0x30},
    {0x0F, 0x30},

    {0x10, 0xC0},
    {0x11, 0xC0},
    {0x12, 0x38},
    {0x13, 0xB0},

    {0x14, 0x32},
    {0x15, 0x06},
    {0x16, 0x00},
    {0x17, 0x00},

    {0x18, 0x06},
    {0x19, 0x30},
    {0x1A, 0xC0},
    {0x1B, 0xC0},

    {0x1C, 0x08},
    {0x1D, 0x06},
    {0x1E, 0x1F},
    {0x1F, 0xF7},

    {0x20, 0xFD},
    {0x21, 0xFF},
    {0x22, 0x1F},
    {0x23, 0xF7},
    
    {0x24, 0xFD},
    {0x25, 0xFF},
    {0x26, 0x00},
    {0x27, 0x38},

    {0x28, 0x38},
    {0x29, 0x38},
    {0x2A, 0x38},
    {0x2B, 0x38},

    {0x2C, 0x38},
    {0x2D, 0x00},
    {0x2E, 0x00},
    {0x2D, 0x00},

    {0x30, 0x00},
    {0x31, 0x00},
    {0x32, 0x00},
    {0x33, 0x00},

    {0x34, 0x00},
    {0x35, 0x00},
    {0x36, 0x00},
    {0x37, 0x00},
    //{ 0x06, 0x1C, 0xC3, 0xFC},  /*  0 *////0x0100 0x0180
    //{ 0xC0, 0x00, 0x00, 0x7C},  /*  4 */
    //{ 0x80, 0x00, 0x00, 0x06},  /*  8 */
    //{ 0x00, 0x06, 0x30, 0x30},  /* 12 */
    //{ 0xC0, 0xC0, 0x38, 0xB0},  /* 16 */
    //{ 0x32, 0x06, 0x00, 0x00},  /* 20 */
    //{ 0x06, 0x30, 0xC0, 0xC0},  /* 24 */
    //{ 0x08, 0x06, 0x1F, 0xF7},  /* 28 */
    //{ 0xFD, 0xFF, 0x1F, 0xF7},  /* 32 */
    //{ 0xFD, 0xFF, 0x00, 0x38},  /* 36 */
    //{ 0x38, 0x38, 0x38, 0x38},  /* 40 */
    //{ 0x38, 0x00, 0x00, 0x00},  /* 44 */
    //{ 0x00, 0x00, 0x00, 0x00},  /* 48 */
    //{ 0x00, 0x00, 0x00, 0x00},  /* 52 */
};

#define INDEX_CACHE_SIZE ARRAY_SIZE(fm1288_reg)

u8 fm1288_init_data[] = {
    0xC0,
    0xFC, 0xF3, 0x68, 0x64, 0x00,
    0xFC, 0xF3, 0x0D, 0x3F, 0x80, 0x90, 0x94, 0x3E,
    0xFC, 0xF3, 0x0D, 0x3F, 0x81, 0x93, 0x83, 0xDE,
    0xFC, 0xF3, 0x0D, 0x3F, 0x82, 0x19, 0x2A, 0xBF,
    0xFC, 0xF3, 0x0D, 0x3F, 0x83, 0x40, 0x05, 0x08,
    0xFC, 0xF3, 0x0D, 0x3F, 0x84, 0x19, 0x3E, 0x7F,
    0xFC, 0xF3, 0x0D, 0x3F, 0x85, 0x80, 0x95, 0x2A,
    0xFC, 0xF3, 0x0D, 0x3F, 0x86, 0x2A, 0x7A, 0xAA,
    0xFC, 0xF3, 0x0D, 0x3F, 0x87, 0x19, 0x28, 0x4F,
    0xFC, 0xF3, 0x0D, 0x3F, 0x88, 0x80, 0x94, 0x3A,
    0xFC, 0xF3, 0x0D, 0x3F, 0x89, 0x82, 0x32, 0xD1,
    0xFC, 0xF3, 0x0D, 0x3F, 0x8A, 0x26, 0x79, 0x0F,
    0xFC, 0xF3, 0x0D, 0x3F, 0x8B, 0x19, 0x2A, 0x80,
    0xFC, 0xF3, 0x0D, 0x3F, 0x8C, 0x80, 0x95, 0xBA,
    0xFC, 0xF3, 0x0D, 0x3F, 0x8D, 0x68, 0x00, 0xA1,
    0xFC, 0xF3, 0x0D, 0x3F, 0x8E, 0x94, 0x96, 0xD0,
    0xFC, 0xF3, 0x0D, 0x3F, 0x8F, 0x90, 0x95, 0xBE,
    0xFC, 0xF3, 0x0D, 0x3F, 0x90, 0x19, 0x2A, 0xBF,
    0xFC, 0xF3, 0x0D, 0x3F, 0x91, 0x40, 0xFA, 0x0A,
    0xFC, 0xF3, 0x0D, 0x3F, 0x92, 0x41, 0x77, 0x0C,
    0xFC, 0xF3, 0x0D, 0x3F, 0x93, 0x41, 0xF4, 0x0B,
    0xFC, 0xF3, 0x0D, 0x3F, 0x94, 0x42, 0xB1, 0x1F,
    0xFC, 0xF3, 0x0D, 0x3F, 0x95, 0x42, 0xEE, 0x0E,
    0xFC, 0xF3, 0x0D, 0x3F, 0x96, 0x82, 0x30, 0x11,
    0xFC, 0xF3, 0x0D, 0x3F, 0x97, 0x27, 0x91, 0x1F,
    0xFC, 0xF3, 0x0D, 0x3F, 0x98, 0x22, 0x7C, 0x01,
    0xFC, 0xF3, 0x0D, 0x3F, 0x99, 0x27, 0x91, 0x5F,
    0xFC, 0xF3, 0x0D, 0x3F, 0x9A, 0x22, 0x7B, 0x01,
    0xFC, 0xF3, 0x0D, 0x3F, 0x9B, 0x27, 0x91, 0x9F,
    0xFC, 0xF3, 0x0D, 0x3F, 0x9C, 0x22, 0x7F, 0x01,
    0xFC, 0xF3, 0x0D, 0x3F, 0x9D, 0x27, 0x91, 0xDF,
    0xFC, 0xF3, 0x0D, 0x3F, 0x9E, 0x22, 0x7E, 0x01,
    0xFC, 0xF3, 0x0D, 0x3F, 0x9F, 0x18, 0x34, 0xBF,
    0xFC, 0xF3, 0x0D, 0x3F, 0xA0, 0x22, 0x7F, 0x9F,
    0xFC, 0xF3, 0x0D, 0x3F, 0xA1, 0x19, 0xB0, 0x5F,
    0xFC, 0xF3, 0x0D, 0x3F, 0xA2, 0x41, 0x00, 0x0A,
    0xFC, 0xF3, 0x0D, 0x3F, 0xA3, 0x90, 0x96, 0xDA,
    0xFC, 0xF3, 0x0D, 0x3F, 0xA4, 0x82, 0x2C, 0x4A,
    0xFC, 0xF3, 0x0D, 0x3F, 0xA5, 0x23, 0xA2, 0x5F,
    0xFC, 0xF3, 0x0D, 0x3F, 0xA6, 0x92, 0x2C, 0x4A,
    0xFC, 0xF3, 0x0D, 0x3F, 0xA7, 0x82, 0x32, 0x5A,
    0xFC, 0xF3, 0x0D, 0x3F, 0xA8, 0x19, 0x76, 0x4F,
    0xFC, 0xF3, 0x0D, 0x3F, 0xA9, 0x82, 0x2F, 0x2A,
    0xFC, 0xF3, 0x0D, 0x3F, 0xAA, 0x40, 0x03, 0x85,
    0xFC, 0xF3, 0x0D, 0x3F, 0xAB, 0x26, 0xEA, 0x0F,
    0xFC, 0xF3, 0x0D, 0x3F, 0xAC, 0x22, 0x08, 0x02,
    0xFC, 0xF3, 0x0D, 0x3F, 0xAD, 0x92, 0x2F, 0x2A,
    0xFC, 0xF3, 0x0D, 0x3F, 0xAE, 0x18, 0x28, 0x3F,
    0xFC, 0xF3, 0x68, 0x64, 0x00,
    0xFC, 0xF3, 0x3B, 0x3F, 0xA0, 0x92, 0xAA,
    0xFC, 0xF3, 0x3B, 0x3F, 0xB0, 0x3F, 0x80,
    0xFC, 0xF3, 0x3B, 0x3F, 0xA1, 0x93, 0xE6,
    0xFC, 0xF3, 0x3B, 0x3F, 0xB1, 0x3F, 0x83,
    0xFC, 0xF3, 0x3B, 0x3F, 0xA2, 0x92, 0x82,
    0xFC, 0xF3, 0x3B, 0x3F, 0xB2, 0x3F, 0x85,
    0xFC, 0xF3, 0x3B, 0x3F, 0xA3, 0x92, 0xA7,
    0xFC, 0xF3, 0x3B, 0x3F, 0xB3, 0x3F, 0x88,
    0xFC, 0xF3, 0x3B, 0x3F, 0xA4, 0x83, 0x49,
    0xFC, 0xF3, 0x3B, 0x3F, 0xB4, 0x3F, 0x91,
    0xFC, 0xF3, 0x3B, 0x3F, 0xA5, 0x9B, 0x04,
    0xFC, 0xF3, 0x3B, 0x3F, 0xB5, 0x3F, 0xA0,
    0xFC, 0xF3, 0x3B, 0x3F, 0xA6, 0x97, 0x63,
    0xFC, 0xF3, 0x3B, 0x3F, 0xB6, 0x3F, 0xA2,
    0xFC, 0xF3, 0x3B, 0x3F, 0xA7, 0x82, 0x82,
    0xFC, 0xF3, 0x3B, 0x3F, 0xB7, 0x3F, 0xA9,
    0xFC, 0xF3, 0x3B, 0x22, 0xC8, 0x00, 0x26,
    0xFC, 0xF3, 0x3B, 0x22, 0xE5, 0x02, 0x77,
    0xFC, 0xF3, 0x3B, 0x22, 0xF5, 0x80, 0x00,
    0xFC, 0xF3, 0x3B, 0x22, 0xF9, 0x00, 0x7F,
    0xFC, 0xF3, 0x3B, 0x22, 0xFA, 0x00, 0x3F,
    0xFC, 0xF3, 0x3B, 0x23, 0x01, 0x00, 0x02,
    0xFC, 0xF3, 0x3B, 0x22, 0xC6, 0x00, 0x0C,
    0xFC, 0xF3, 0x3B, 0x22, 0xC7, 0x00, 0x0C,
    0xFC, 0xF3, 0x3B, 0x22, 0xF8, 0x80, 0x00,
    0xFC, 0xF3, 0x3B, 0x22, 0xF2, 0x00, 0x30,
    0xFC, 0xF3, 0x3B, 0x23, 0x6F, 0x0B, 0x00,
    0xFC, 0xF3, 0x3B, 0x23, 0x70, 0x04, 0x00,
    0xFC, 0xF3, 0x3B, 0x23, 0x0C, 0x03, 0x00,
    0xFC, 0xF3, 0x3B, 0x23, 0x03, 0x0D, 0xD1,
    0xFC, 0xF3, 0x3B, 0x22, 0xEE, 0x00, 0x01,
    0xFC, 0xF3, 0x3B, 0x23, 0x04, 0x03, 0x0F,
    0xFC, 0xF3, 0x3B, 0x23, 0x0D, 0x03, 0x00,
    0xFC, 0xF3, 0x3B, 0x23, 0x05, 0x00, 0x01,
    0xFC, 0xF3, 0x3B, 0x23, 0xDB, 0x40, 0x00,
    0xFC, 0xF3, 0x3B, 0x23, 0xDC, 0x03, 0x20,
    0xFC, 0xF3, 0x3B, 0x23, 0x2F, 0x00, 0x80,
    0xFC, 0xF3, 0x3B, 0x23, 0x39, 0x00, 0x20,
    0xFC, 0xF3, 0x3B, 0x23, 0xB3, 0x00, 0x0A,
    0xFC, 0xF3, 0x3B, 0x23, 0xB4, 0x00, 0x04,
    0xFC, 0xF3, 0x3B, 0x23, 0xE9, 0x40, 0x00,
    0xFC, 0xF3, 0x3B, 0x23, 0xBE, 0x00, 0x30,
    0xFC, 0xF3, 0x3B, 0x22, 0xC4, 0x06, 0x22,
    0xFC, 0xF3, 0x3B, 0x22, 0xE9, 0x00, 0x01,
    0xFC, 0xF3, 0x3B, 0x23, 0xEA, 0x72, 0x00,
    0xFC, 0xF3, 0x3B, 0x23, 0x10, 0x12, 0x0C,
    0xFC, 0xF3, 0x3B, 0x23, 0xBF, 0x00, 0x54,
    0xFC, 0xF3, 0x3B, 0x23, 0x28, 0x7F, 0xFF,
    0xFC, 0xF3, 0x3B, 0x23, 0x84, 0x08, 0x00,
    0xFC, 0xF3, 0x3B, 0x23, 0x33, 0x00, 0x08,
    0xFC, 0xF3, 0x3B, 0x23, 0x32, 0x00, 0x38,
    0xFC, 0xF3, 0x3B, 0x23, 0x82, 0x03, 0x40,
    0xFC, 0xF3, 0x3B, 0x23, 0x83, 0x04, 0x00,
    0xFC, 0xF3, 0x3B, 0x23, 0xE7, 0x0A, 0x00,
    0xFC, 0xF3, 0x3B, 0x23, 0xBB, 0x60, 0x00,
    0xFC, 0xF3, 0x3B, 0x23, 0x6E, 0x28, 0x00,
    0xFC, 0xF3, 0x3B, 0x23, 0x07, 0xF4, 0xF4,
    0xFC, 0xF3, 0x3B, 0x23, 0x86, 0x08, 0x00,
    0xFC, 0xF3, 0x3B, 0x23, 0xF1, 0x2F, 0x3B,
    0xFC, 0xF3, 0x3B, 0x23, 0xF2, 0xD0, 0xD2,
    0xFC, 0xF3, 0x3B, 0x23, 0xF3, 0x2F, 0x3B,
    0xFC, 0xF3, 0x3B, 0x23, 0xF4, 0xA6, 0xAB,
    0xFC, 0xF3, 0x3B, 0x23, 0xF5, 0x47, 0x7A,
    0xFC, 0xF3, 0x3B, 0x23, 0xF6, 0x6C, 0xF4,
    0xFC, 0xF3, 0x3B, 0x23, 0xF7, 0x93, 0xC5,
    0xFC, 0xF3, 0x3B, 0x23, 0xF8, 0x6C, 0xF4,
    0xFC, 0xF3, 0x3B, 0x23, 0xF9, 0x88, 0xFB,
    0xFC, 0xF3, 0x3B, 0x23, 0xFA, 0x75, 0x47,
    0xFC, 0xF3, 0x3B, 0x23, 0xFB, 0x7F, 0xFF,
    0xFC, 0xF3, 0x3B, 0x23, 0xFC, 0x81, 0x5A,
    0xFC, 0xF3, 0x3B, 0x23, 0xFD, 0x7F, 0xFF,
    0xFC, 0xF3, 0x3B, 0x23, 0xFE, 0x83, 0x8E,
    0xFC, 0xF3, 0x3B, 0x23, 0xFF, 0x7D, 0xEE,
    0xFC, 0xF3, 0x3B, 0x23, 0x49, 0x20, 0x00,
    0xFC, 0xF3, 0x3B, 0x23, 0xC0, 0x00, 0x5A,
    0xFC, 0xF3, 0x3B, 0x23, 0xC1, 0xFF, 0xB8,
    0xFC, 0xF3, 0x3B, 0x23, 0xC2, 0x00, 0x5A,
    0xFC, 0xF3, 0x3B, 0x23, 0xC3, 0x85, 0x6E,
    0xFC, 0xF3, 0x3B, 0x23, 0xC4, 0x75, 0xAC,
    0xFC, 0xF3, 0x3B, 0x23, 0xC5, 0x19, 0x38,
    0xFC, 0xF3, 0x3B, 0x23, 0xC6, 0xE7, 0xAD,
    0xFC, 0xF3, 0x3B, 0x23, 0xC7, 0x19, 0x38,
    0xFC, 0xF3, 0x3B, 0x23, 0xCA, 0x7F, 0xFF,
    0xFC, 0xF3, 0x3B, 0x23, 0xCB, 0x82, 0xE1,
    0xFC, 0xF3, 0x3B, 0x23, 0xCC, 0x7F, 0xFF,
    0xFC, 0xF3, 0x3B, 0x23, 0xCD, 0x82, 0x51,
    0xFC, 0xF3, 0x3B, 0x23, 0xCE, 0x7E, 0x64,
    0xFC, 0xF3, 0x3B, 0x23, 0xEB, 0x00, 0x00,
    0xFC, 0xF3, 0x3B, 0x23, 0xEC, 0x00, 0x00,
    0xFC, 0xF3, 0x3B, 0x23, 0xE8, 0x27, 0x40,
    0xFC, 0xF3, 0x3B, 0x23, 0x48, 0x40, 0x00,
    0xFC, 0xF3, 0x3B, 0x22, 0xFB, 0x00, 0x00
};

/* codec private data */
struct fm1288_priv {
    unsigned int sysclk;
//    enum snd_soc_control_type control_type;
    struct reg_default *index_cache;
    int index_cache_size;
    struct regmap *regmap;
    
    struct snd_pcm_hw_constraint_list *sysclk_constraints;
    struct i2c_client *client;

    int spk_ctl_gpio;
    int spk_gpio_level;
    int reset_gpio;
    int reset_gpio_level;
};
struct fm1288_priv *fm1288_private;
uint32_t master_volume = 1;

static short FM1288_FastMemoryRead(struct i2c_client *client, u8 uc_addressHightByte, u8 uc_addressLowByte );
static int FM1288_BurstModeMemoryWrite(struct i2c_client *client, s32 len, u8 *buf);
static int FM1288_SingleMemoryWrite(struct i2c_client *client, u8 uc_addressHightByte, u8 uc_addressLowByte, u8 uc_dataHightByte, u8 uc_dataLowByte);


static int fm1288_set_spk_gpio(int level)
{
    struct fm1288_priv *fm1288 = fm1288_private;

    if (!fm1288) {
	printk("%s: pointer fm1288_priv is NULL!\n", __func__);
	return 0;
    }

    DBG("%s: set speaker ctl gpio %s\n", __func__,
	level ? "HIGH" : "LOW");

    if (fm1288->spk_ctl_gpio != INVALID_GPIO)
	gpio_set_value(fm1288->spk_ctl_gpio, level);

    return 0;
}

static int fm1288_read_reg_cache(void *codec,
		     unsigned int reg, unsigned int *val)
{
    switch (reg)
    {
        case FM1288_MVOL_REG:
        *val = master_volume;
        break;
    }

    DBG("%s: [0x%02x] = 0x%02x \n", __func__,reg, *val);
    return 0;
}

static int fm1288_write(void * icodec, unsigned int reg,
	         unsigned int value)
{
    int ret;
//    struct snd_soc_component *codec = icodec;
    struct fm1288_priv *fm1288 = fm1288_private;    // Probably it could be get from codec?
    DBG("%s: [0x%02x] = 0x%02x \n", __func__,reg, value);


    switch (reg)
    {
        case FM1288_MVOL_REG:
            master_volume = value*0x100/100;
            if (fm1288->client==NULL)
            {
                DBG("%s: Error, not i2c client! \n", __func__);
                return -1;
            }

            ret = FM1288_SingleMemoryWrite(fm1288->client, reg>>8, reg &0xFF, master_volume >> 8, master_volume & 0xFF);
            DBG("%s: Set volume res = %d \n", __func__,ret);
        return ret;
    }

	return 0;//ret;    // Return always OK
}

// =================


static s32 FM1288_singleReadReg(struct i2c_client *client)
{
    s32 ret = -1;
    
    ret = FM1288_FastMemoryRead(client, 0x23, 0x90);
        
    return ret;
}

static s32 FM1288_singleWriteReg(struct i2c_client *client)
{
    s32 ret = -1;
    
    ret = FM1288_SingleMemoryWrite(client, 0x23, 0x6E, 0x55, 0x55);
        
    return ret;
}

static int FM1288_SingleMemoryWrite(struct i2c_client *client,
                                     u8 uc_addressHightByte,
                                     u8 uc_addressLowByte,
                                     u8 uc_dataHightByte,
                                     u8 uc_dataLowByte)
{
    struct i2c_msg msg;
    s32 ret = -1;
    s32 retries = 0;
    u8 tmp[7] = {0xFC, 0xF3, 0x3B, uc_addressHightByte, uc_addressLowByte, uc_dataHightByte, uc_dataLowByte};

    msg.flags = !I2C_M_RD;
    msg.addr  = client->addr;
    msg.len   = 7;
    msg.buf   = tmp;

    while(retries < 5)
    {
        ret = i2c_transfer(client->adapter, &msg, 1);
        if (ret == 1)break;
        retries++;
    }
    if (retries >= 5)
    {
        FM1288_ERROR("FM1288 single write fail %s %d\n", __FUNCTION__, __LINE__);  
    }

    DBG("%s: write 0x%02x%02x, data 0x%02x%02x\n ", __func__, tmp[3], tmp[4], tmp[5], tmp[6]);
    return ret;
}

static int FM1288_BurstModeMemoryWrite(struct i2c_client *client, s32 len, u8 *buf)
{
    struct i2c_msg msg;
    s32 ret = -1;
    s32 retries = 0;

    msg.flags = !I2C_M_RD;
    msg.addr  = client->addr;
    msg.len   = len;
    msg.buf   = buf;

    while(retries < 5)
    {
        printk("i2c cicle %d\n", retries);
        ret = i2c_transfer(client->adapter, &msg, 1);
        if (ret == 1)break;
        retries++;
    }
    if (retries >= 5)
    {
        FM1288_ERROR("FM1288 burst mode write fail %s %d\n", __FUNCTION__, __LINE__);  
    }
    return ret;
}

static short FM1288_FastMemoryRead(struct i2c_client *client, u8 uc_addressHightByte, u8 uc_addressLowByte )
{
    struct i2c_msg msg;
    s32 ret = -1;
    s32 retries = 0;
    s16 regValue = 0x00;
    u8 tmp[5] = {0xFC, 0xF3, 0x37, uc_addressHightByte, uc_addressLowByte};
    u8 val[2] = {0x00, 0x00};

    msg.flags = !I2C_M_RD;
    msg.addr  = client->addr;
    msg.len   = 5;
    msg.buf   = tmp;

    while(retries < 5)
    {
        ret = i2c_transfer(client->adapter, &msg, 1);
        if (ret == 1)break;
        retries++;
    }
    if((retries >= 5))
    {
        FM1288_ERROR("FM1288 single write fail %s %d\n", __FUNCTION__, __LINE__);  
        return -1;
    }

    struct i2c_msg read_msgs[2];
    u8 tmp_readArrayOne[] = {0xFC, 0xF3, 0x60, 0x25,};
    read_msgs[0].flags = !I2C_M_RD;
    read_msgs[0].addr  = client->addr;
    read_msgs[0].len   = 4;
    read_msgs[0].buf   = tmp_readArrayOne;

    read_msgs[1].flags = I2C_M_RD;
    read_msgs[1].addr  = client->addr;
    read_msgs[1].len   = 1;
    read_msgs[1].buf   = &val[0];//low byte

    while(retries < 5)
    {
        ret = i2c_transfer(client->adapter, read_msgs, 2);
        if(ret == 2)break;
        retries++;
    }
    if((retries >= 5))
    {
        FM1288_ERROR("FM1288 single write fail %s %d\n", __FUNCTION__, __LINE__);  
    }

    u8 tmp_readArrayTwo[] = {0xFC, 0xF3, 0x60, 0x26,};
    read_msgs[0].flags = !I2C_M_RD;
    read_msgs[0].addr  = client->addr;
    read_msgs[0].len   = 4;
    read_msgs[0].buf   = tmp_readArrayTwo;

    read_msgs[1].flags = I2C_M_RD;
    read_msgs[1].addr  = client->addr;
    read_msgs[1].len   = 1;
    read_msgs[1].buf   = &val[1];//high byte

    while(retries < 5)
    {
        ret = i2c_transfer(client->adapter, read_msgs, 2);
        if(ret == 2)break;
        retries++;
    }
    if((retries >= 5))
    {
        FM1288_ERROR("FM1288 single write fail %s %d\n", __FUNCTION__, __LINE__);  
    }

    DBG("FM1288 read 0x%02x%02x data 0x%02x%02x\n", tmp[3], tmp[4], val[1], val[0]);
    regValue = val[1];
    regValue = ((regValue << 8) | (val[0]));

    return regValue;
}


// =================

static int fm1288_reset(struct snd_soc_component *codec)
{
    struct fm1288_priv *fm1288 = fm1288_private;    // Probably it could be get from codec?
    DBG("%s \n", __func__);
//    FM1288_SingleMemoryWrite(fm1288->client, FM1288_PWRDWN_SET>>8, FM1288_PWRDWN_SET&0xFF, 0xD0, 0x00);
//    return FM1288_SingleMemoryWrite(fm1288->client, FM1288_PWRDWN_SET>>8, FM1288_PWRDWN_SET&0xFF, 0xD0, 0x00);

    //snd_soc_component_write(codec, FM1288_PWRDWN_SET, 0xD000);      // TS! Check this
    //return snd_soc_component_write(codec, FM1288_PWRDWN_SET, 0xD000); // TS: Check this
}

struct _coeff_div {
    u32 mclk;
    u32 rate;
    u16 fs;
    u8 sr:4;
    u8 usb:1;
};

/* codec hifi mclk clock divider coefficients */
static const struct _coeff_div coeff_div[] = {
    /* 8k */
    {12288000, 8000, 1536, 0xa, 0x0},
    {11289600, 8000, 1408, 0x9, 0x0},
    {18432000, 8000, 2304, 0xc, 0x0},
    {16934400, 8000, 2112, 0xb, 0x0},
    {12000000, 8000, 1500, 0xb, 0x1},

    /* 11.025k */
    {11289600, 11025, 1024, 0x7, 0x0},
    {16934400, 11025, 1536, 0xa, 0x0},
    {12000000, 11025, 1088, 0x9, 0x1},

    /* 16k */
    {12288000, 16000, 768, 0x6, 0x0},
    {18432000, 16000, 1152, 0x8, 0x0},
    {12000000, 16000, 750, 0x7, 0x1},

    /* 22.05k */
    {11289600, 22050, 512, 0x4, 0x0},
    {16934400, 22050, 768, 0x6, 0x0},
    {12000000, 22050, 544, 0x6, 0x1},

    /* 32k */
    {12288000, 32000, 384, 0x3, 0x0},
    {18432000, 32000, 576, 0x5, 0x0},
    {12000000, 32000, 375, 0x4, 0x1},

    /* 44.1k */
    {11289600, 44100, 256, 0x2, 0x0},
    {16934400, 44100, 384, 0x3, 0x0},
    {12000000, 44100, 272, 0x3, 0x1},

    /* 48k */
    {12288000, 48000, 256, 0x2, 0x0},
    {18432000, 48000, 384, 0x3, 0x0},
    {12000000, 48000, 250, 0x2, 0x1},

    /* 88.2k */
    {11289600, 88200, 128, 0x0, 0x0},
    {16934400, 88200, 192, 0x1, 0x0},
    {12000000, 88200, 136, 0x1, 0x1},

    /* 96k */
    {12288000, 96000, 128, 0x0, 0x0},
    {18432000, 96000, 192, 0x1, 0x0},
    {12000000, 96000, 125, 0x0, 0x1},
};

static inline int get_coeff(int mclk, int rate)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(coeff_div); i++) {
	if (coeff_div[i].rate == rate && coeff_div[i].mclk == mclk)
	    return i;
    }

    return -EINVAL;
}

/* The set of rates we can generate from the above for each SYSCLK */
static unsigned int rates_12288[] = {
    8000, 12000, 16000, 24000, 24000, 32000, 48000, 96000,
};

static struct snd_pcm_hw_constraint_list constraints_12288 = {
    .count = ARRAY_SIZE(rates_12288),
    .list = rates_12288,
};

static unsigned int rates_112896[] = {
    8000, 11025, 22050, 44100,
};

static struct snd_pcm_hw_constraint_list constraints_112896 = {
    .count = ARRAY_SIZE(rates_112896),
    .list = rates_112896,
};

static unsigned int rates_12[] = {
    8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000,
    48000, 88235, 96000,
};

static struct snd_pcm_hw_constraint_list constraints_12 = {
    .count = ARRAY_SIZE(rates_12),
    .list = rates_12,
};

/*
 * Note that this should be called from init rather than from hw_params.
 */
static int fm1288_set_dai_sysclk(struct snd_soc_dai *codec_dai,
	int clk_id, unsigned int freq, int dir)
{
    struct snd_soc_component *codec = codec_dai->component;
    struct fm1288_priv *fm1288 = snd_soc_component_get_drvdata(codec);

    DBG("%s: enter, line = %d, freq = %u\n",
	__func__, __LINE__, freq);

    switch (freq) 
    {
        case 11289600:
        case 18432000:
        case 22579200:
        case 36864000:
        	fm1288->sysclk_constraints = &constraints_112896;
        	fm1288->sysclk = freq;
    	return 0;

        case 12288000:
        case 16934400:
        case 24576000:
        case 33868800:
        	fm1288->sysclk_constraints = &constraints_12288;
        	fm1288->sysclk = freq;
    	return 0;

        case 12000000:
        case 24000000:
        	fm1288->sysclk_constraints = &constraints_12;
        	fm1288->sysclk = freq;
    	return 0;
    }
    // Default val // DEBUG!
    fm1288->sysclk_constraints = &constraints_12;
    fm1288->sysclk = freq;

    return 0;//-EINVAL;
}

static int fm1288_set_dai_fmt(struct snd_soc_dai *codec_dai,
	unsigned int fmt)
{
    u8 iface = 0;
    u8 adciface = 0;
    u8 daciface = 0;

    DBG("%s: fmt[%02x]\n", __func__, fmt);

    /* set master/slave audio interface */
    switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
    case SND_SOC_DAIFMT_CBM_CFM: // MASTER MODE
	DBG("%s: fm1288 in master mode\n", __func__);
	iface |= 0x80;
	break;
    case SND_SOC_DAIFMT_CBS_CFS: // SLAVE MODE
	DBG("%s: fm1288 in slave mode\n", __func__);
	iface &= 0x7F;
	break;
    default:
	return -EINVAL;
    }

    /* interface format */
    switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
    case SND_SOC_DAIFMT_I2S:
	adciface &= 0xFC;
	//daciface &= 0xF9; //updated by david-everest, 5-25
	daciface &= 0xF9;
	break;
    case SND_SOC_DAIFMT_RIGHT_J:
	break;
    case SND_SOC_DAIFMT_LEFT_J:
	break;
    case SND_SOC_DAIFMT_DSP_A:
	break;
    case SND_SOC_DAIFMT_DSP_B:
	break;
    default:
	return -EINVAL;
    }

    /* clock inversion */
    switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
    case SND_SOC_DAIFMT_NB_NF:
	iface    &= 0xDF;
	adciface &= 0xDF;
	daciface &= 0xBF;
	break;
    case SND_SOC_DAIFMT_IB_IF:
	iface    |= 0x20;
	adciface |= 0x20;
	daciface |= 0x40;
	break;
    case SND_SOC_DAIFMT_IB_NF:
	iface    |= 0x20;
	adciface &= 0xDF;
	daciface &= 0xBF;
	break;
    case SND_SOC_DAIFMT_NB_IF:
	iface    &= 0xDF;
	adciface |= 0x20;
	daciface |= 0x40;
	break;
    default:
	return -EINVAL;
    }

    return 0;
}

static int fm1288_pcm_startup(struct snd_pcm_substream *substream,
	          struct snd_soc_dai *dai)
{
    struct snd_soc_component *codec = dai->component;
    struct fm1288_priv *fm1288 = snd_soc_component_get_drvdata(codec);


    fm1288_set_dai_sysclk(dai, 0, 22579200, 0); // TS - manually call sysclk set


    DBG("%s: fm1288->sysclk = %d\n", __func__, fm1288->sysclk);

    /*
     * The set of sample rates that can be supported depends on the
     * MCLK supplied to the CODEC - enforce this.
     */

    if (!fm1288->sysclk) {
	dev_err(codec->dev,
	    "No MCLK configured, call set_sysclk() on init\n");
	return -EINVAL;
    }

    snd_pcm_hw_constraint_list(substream->runtime, 0,
		   SNDRV_PCM_HW_PARAM_RATE,
		   fm1288->sysclk_constraints);

    return 0;
}

static int fm1288_pcm_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params,
		struct snd_soc_dai *dai)
{
    return 0;
}

static int fm1288_mute(struct snd_soc_dai *dai, int mute)
{
    struct fm1288_priv *fm1288 = fm1288_private;

    if (!fm1288) 
    {
	   DBG("%s: pointer fm1288_priv is NULL!\n", __func__);
	   return 0;
    }

    DBG("%s: mute = %d\n", __func__, mute);

    if (mute) {
        fm1288_set_spk_gpio(!fm1288->spk_gpio_level);
        msleep(100);
    } 
    else 
    {
    	fm1288_set_spk_gpio(fm1288->spk_gpio_level);
    	msleep(150);
    }

    return 0;
}

// =============================================
// Userspace routines
// =============================================
static int usioLastReadVal = 0;
// .open
int usio_open(struct inode *inode, struct file *file)
{
    printk("%s: Open from userspace\n", __func__);

    return 0;//nonseekable_open(inode,file); //Notify the kernel that your device does not support llseek
}

// .release
int usio_release(struct inode *inode, struct file *file)
{
    printk("%s: Release from userspace\n", __func__);
    return 0;
}

char read_msg[10];
char * read_msg_ptr = NULL;

// .write
ssize_t usio_write(struct file *file, const char __user * buf, size_t count, loff_t *ppos)
{
    int reg = 0;
    int val = 0;
    char msg[30];
    struct fm1288_priv *fm1288 = fm1288_private;
    

    copy_from_user( msg, buf, count );

    // read data
    if (msg[0] == 'r')
    {
        int cnv = sscanf(msg,"r 0x%x",&reg);

        if ( cnv!= 1 ) 
        {
            printk("%s: Did not convert the value\n", __func__);
            return -EINVAL;
        }

        usioLastReadVal = FM1288_FastMemoryRead(fm1288->client, reg>>8, reg&0xFF); 
        sprintf(read_msg, "0x%02x\n", usioLastReadVal);
        read_msg_ptr = read_msg;

        printk("%s: Read from userspace Reg[0x%02x] = 0x%02x\n", __func__, reg, usioLastReadVal);
    }
    else // Write data
    {

        int cnv = sscanf(msg,"0x%x 0x%x",&reg, &val);

        if ( cnv!= 2 ) 
        {
            printk("%s: Did not convert the value\n", __func__);
            return -EINVAL;
        }

        printk("%s: Write from userspace Reg[0x%02x] = 0x%02x\n", __func__, reg, val);

        FM1288_SingleMemoryWrite(fm1288->client, reg>>8, reg&0xFF, val>>8, val&0xFF);
    }

    return count;
}

// .read
ssize_t usio_read( struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
//    printk("Read from userspace\n");

//    copy_to_user( buf, msg, count);

    int i = 0;
    while (read_msg_ptr!=NULL && count-- && *read_msg_ptr)
    {
        put_user(*(read_msg_ptr++), buf++);
        i++;
    }


    return i;
}


static const struct file_operations usio_accel_device_fops = {
    .read = usio_read,
    .write = usio_write,
    .open = usio_open,
    .release = usio_release,
};

static struct miscdevice usio_accel_device = {
    MISC_DYNAMIC_MINOR, "fm1288_i2c_ctrl", &usio_accel_device_fops
};


// =============================================
// END: Userspace routines
// =============================================

static const DECLARE_TLV_DB_SCALE(fm1288_volume_tlv, -600, 50, 1);

static const struct snd_kcontrol_new fm1288_snd_controls[] = {
        SOC_SINGLE_TLV("Master Volume", FM1288_MVOL_REG, 0, 100, 0, fm1288_volume_tlv),
//        SOC_DOUBLE_R_TLV("Speaker Volume", FM1288_CH1_VOL_REG, FM1288_CH2_VOL_REG, 0, 0xff, 1, fm1288_volume_tlv),
//        SOC_DOUBLE("Speaker Switch", FM1288_SOFT_MUTE_REG, FM1288_SOFT_MUTE_CH1_SHIFT, TAS571X_SOFT_MUTE_CH2_SHIFT,1, 1),
};


#define FM1288_RATES SNDRV_PCM_RATE_8000_96000
#define FM1288_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
    SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_ops fm1288_ops = {
    .startup = fm1288_pcm_startup,
    .hw_params = fm1288_pcm_hw_params,
    .set_fmt = fm1288_set_dai_fmt,
    .set_sysclk = fm1288_set_dai_sysclk,
    .digital_mute = fm1288_mute,
};

static struct snd_soc_dai_driver fm1288_dai = {
    .name = "FM1288 HiFi",
    .playback = {
	.stream_name = "Playback",
	.channels_min = 1,
	.channels_max = 2,
	.rates = FM1288_RATES,
	.formats = FM1288_FORMATS,
    },
    .capture = {
	.stream_name = "Capture",
	.channels_min = 1,
	.channels_max = 8,
	.rates = FM1288_RATES,
	.formats = FM1288_FORMATS,
     },
    .ops = &fm1288_ops,
    .symmetric_rates = 1,
};

static int fm1288_suspend(struct snd_soc_component *codec)
{
    return 0;
}

static int fm1288_resume(struct snd_soc_component *codec)
{
    return 0;
}

static int fm1288_probe(struct snd_soc_component *codec)
{
    int ret = 0;

    DBG("%s: enter, line = %d\n", __func__, __LINE__);

    if (codec == NULL) 
    {
    	dev_err(codec->dev, "Codec device not registered\n");
    	return -ENODEV;
    }

//    codec->driver->read  = fm1288_read_reg_cache;
//    codec->driver->write = fm1288_write;
//    codec->driver->hw_write = (hw_write_t)i2c_master_send;    // TS!
//    codec->driver->control_data = container_of(codec->dev, struct i2c_client, dev); // TS!

    ret = fm1288_reset(codec);
    if (ret < 0) 
    {
	   dev_err(codec->dev, "Failed to issue reset\n");
	   return ret;
    }


    return 0;
}

static void fm1288_remove(struct snd_soc_component *codec)
{
    
}

static struct snd_soc_component_driver soc_codec_dev_fm1288 = {
    .probe = fm1288_probe,
    .remove = fm1288_remove,
    .suspend = fm1288_suspend,
    .resume = fm1288_resume,
    .controls = fm1288_snd_controls,
    .num_controls = ARRAY_SIZE(fm1288_snd_controls),

//    .reg_cache_size = ARRAY_SIZE(fm1288_reg), // TS!
//    .reg_word_size = sizeof(u16),// TS!
//    .reg_cache_default = fm1288_reg,// TS!
//    .reg_cache_step = 1,// TS!
};

static const struct regmap_config fm1288_regmap = {
    .reg_bits = 32,
    .val_bits = 32,
    .max_register = 0x05bfffff,
//    .volatile_reg = fm1288_volatile_register,
//    .readable_reg = fm1288_readable_register,
    .reg_write = fm1288_write,
    .reg_read = fm1288_read_reg_cache,
    .cache_type = REGCACHE_RBTREE,
    .reg_defaults = fm1288_reg,
    .num_reg_defaults = ARRAY_SIZE(fm1288_reg),
};


static int fm1288_i2c_init(struct i2c_client *i2c)
{
    int i = 0, ret = -1;
    struct i2c_msg msgs[] = {
	{
	     .addr = 0x60,
	     .flags = 0,
	     .len = sizeof(_mode0),//sizeof(fm1288_init_data),
	     .buf = _mode0, //fm1288_init_data,
	     // .scl_rate = 100 * 1000, //100kHZ // TS fix
	 },
    };

    for(i = 0; i < 3; i++) 
    {
	   ret = i2c_transfer(i2c->adapter, msgs, 1);
	   if (ret < 0)
       {
	        DBG("%s: i2c write error, ret = %d, i = %d\n", __func__, ret, i);
       }
    }
    DBG("%s: ret = %d, i = %d\n", __func__, ret, i);

    return ret;
}

/*
dts:
    codec@10 {
	compatible = "fm1288";
	reg = <0x10>;
	spk-con-gpio = <&gpio2 GPIO_D7 GPIO_ACTIVE_HIGH>;
	hp-con-gpio = <&gpio2 GPIO_D7 GPIO_ACTIVE_HIGH>;
	hp-det-gpio = <&gpio0 GPIO_B5 GPIO_ACTIVE_HIGH>;
    };
*/
static int fm1288_i2c_probe(struct i2c_client *i2c,
                  const struct i2c_device_id *id)
{
    int ret = -1;
    enum of_gpio_flags flags;
    struct fm1288_priv *fm1288;
    struct i2c_adapter *adapter = to_i2c_adapter(i2c->dev.parent);

    if (!i2c_check_functionality(adapter, I2C_FUNC_I2C)) {
	dev_warn(&adapter->dev,
	    "I2C-Adapter doesn't support I2C_FUNC_I2C\n");
	return -EIO;
    }

    /* Allocate memory for driver data */
    fm1288 = devm_kzalloc(&i2c->dev, sizeof(struct fm1288_priv),
                          GFP_KERNEL);
    if (fm1288 == NULL)
	return -ENOMEM;


    fm1288->regmap = devm_regmap_init(&i2c->dev, NULL, i2c, &fm1288_regmap);
    if (IS_ERR(fm1288->regmap)) {
        ret = PTR_ERR(fm1288->regmap);
        dev_err(&i2c->dev, "Failed to allocate register map: %d\n",
            ret);
        return ret;
    }

/*    fm1288->index_cache = devm_kmemdup(&i2c->dev, fm1288_index_def,
                      sizeof(fm1288_index_def), GFP_KERNEL);
    if (!fm1288->index_cache)
        return -ENOMEM;
*/
    fm1288->index_cache_size = INDEX_CACHE_SIZE;

    i2c_set_clientdata(i2c, fm1288);
//    fm1288->control_type = SND_SOC_I2C;
    fm1288_private = fm1288;
    fm1288_private->client = i2c;


    // Register in userspace
    usio_accel_device.parent = &i2c->dev;
    ret = misc_register(&usio_accel_device);
    if (ret < 0)
    {
        printk("%s: error %d registering device\n", __func__, ret);
    }

    // Reset GPIO connect to main RESET line, so we do not need this code
/*    fm1288->reset_gpio =
	of_get_named_gpio_flags(i2c->dev.of_node, "reset-gpio", 0, &flags);
    if (fm1288->reset_gpio < 0) {
	   DBG("%s: Can not read property reset-gpio\n", __func__);
	   fm1288->reset_gpio = INVALID_GPIO;
    } else {
	   fm1288->reset_gpio_level = (flags & OF_GPIO_ACTIVE_LOW) ? 0 : 1;
	   ret = gpio_request(fm1288->reset_gpio, NULL);
	   if (ret != 0) {
	       DBG("%s: request reset_gpio error!\n", __func__);
	       return ret;
	   }
	   gpio_direction_output(fm1288->reset_gpio, 0);
    }
    msleep(20);
    gpio_set_value(fm1288->reset_gpio, 1);
    msleep(20);
    */

    /*  // Do not needed too
    fm1288->spk_ctl_gpio =	of_get_named_gpio_flags(i2c->dev.of_node, "spk-con-gpio", 0, &flags);
    if (fm1288->spk_ctl_gpio < 0) 
    {
	   DBG("%s: Can not read property spk-con-gpio\n", __func__);
	   fm1288->spk_ctl_gpio = INVALID_GPIO;
    } 
    else 
    {
    	fm1288->spk_gpio_level = (flags & OF_GPIO_ACTIVE_LOW) ? 0 : 1;
    	ret = gpio_request(fm1288->spk_ctl_gpio, NULL);
    	if (ret != 0) 
        {
	       DBG("%s: request spk_ctl_gpio error", __func__);
	       return ret;
	    }
	    gpio_direction_output(fm1288->spk_ctl_gpio, fm1288->spk_gpio_level);
    }
*/

    ret = fm1288_i2c_init(i2c);
    if(ret < 0)
    {
	   DBG("%s: fm1288 init failed, ret = %d\n", __func__, ret);
	   return ret;
    }

    ret = snd_soc_register_component(&i2c->dev, &soc_codec_dev_fm1288,
                                 &fm1288_dai, 1);
    if (ret < 0) 
    {
    	DBG("%s: failed to register codec!\n", __func__);
    	return ret;
    }

    DBG("%s: fm1288 i2c probe ok\n", __func__);
    return ret;
}

static int fm1288_i2c_remove(struct i2c_client *client)
{
    snd_soc_unregister_component(&client->dev);
    kfree(i2c_get_clientdata(client));
    return 0;
}

static const struct i2c_device_id fm1288_i2c_id[] = {
    { "fm1288", 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, fm1288_i2c_id);

void fm1288_i2c_shutdown(struct i2c_client *client)
{
    struct fm1288_priv *fm1288 = fm1288_private;
    fm1288_set_spk_gpio(!fm1288->spk_gpio_level);
}

static struct i2c_driver fm1288_i2c_driver = {
    .driver = {
	.name = "FM1288",
	.owner = THIS_MODULE,
    },
    .shutdown = fm1288_i2c_shutdown,
    .probe = fm1288_i2c_probe,
    .remove = fm1288_i2c_remove,
    .id_table = fm1288_i2c_id,
};

static int __init fm1288_init(void)
{
    return i2c_add_driver(&fm1288_i2c_driver);
}

static void __exit fm1288_exit(void)
{
    i2c_del_driver(&fm1288_i2c_driver);
}
module_init(fm1288_init);
module_exit(fm1288_exit);

MODULE_DESCRIPTION("ASoC fm1288 driver");
MODULE_AUTHOR("Mark Brown <will@everset-semi.com>");
MODULE_LICENSE("GPL");
