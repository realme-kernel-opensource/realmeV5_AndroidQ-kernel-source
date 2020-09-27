/* Copyright (c) 2019 The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/backlight.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/pwm.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/delay.h>

#include <linux/of_gpio.h>
#include <linux/gpio.h>

#include <soc/oppo/device_info.h>
#include <soc/oppo/oppo_project.h>
#include <soc/oppo/boot_mode.h>
#include <linux/regmap.h>

/* Shipei.Chen@Cam.Drv, 20200416,  add the ldo3 for sensor porting!*/
#ifndef VENDOR_EDIT
#define VENDOR_EDIT
#endif

/* Shipei.Chen@Cam.Drv, 20200416,  add the ldo3 for sensor porting!*/
#ifdef VENDOR_EDIT
#define FAN53870_LDO2_VOUT_REG   0x05
#define FAN53870_LDO3_VOUT_REG   0x06
#define FAN53870_LDO4_VOUT_REG   0x07
#define FAN53870_LDO34_SEQ_REG   0x0c
#define FAN53870_LDO5_VOUT_REG   0x08
#define FAN53870_LDO6_VOUT_REG   0x09
#define FAN53870_LDO7_VOUT_REG   0x0A
#define FAN53870_LDO56_SEQ_REG   0x0d
#define FAN53870_LDO7_SEQ_REG    0x0e
#endif


#define FAN53870_PRODUCT_ID_REG  0x00
#define FAN53870_LDO_ENABLE_REG  0x03
#define FAN53870_LDO1_VOUT_REG   0x04

#define FAN53870_LDO12_SEQ_REG   0x0B

struct fan53870_platform_data {
    unsigned int ldo1_min_vol;
    unsigned int ldo1_max_vol;
    unsigned int ldo1_step_vol;
};

/*
 * struct TPS65132_pw_chip
 * @dev: Parent device structure
 * @regmap: Used for I2C register access
 * @pdata: TPS65132 platform data
 */
struct fan53870_pw_chip {
    struct device *dev;
    struct fan53870_platform_data *pdata;
    struct regmap *regmap;
};
static struct fan53870_pw_chip *fan53870_pchip;
int is_fan53870_1p1 = 0;

static int fan53870_mask_write_reg(struct regmap *regmap, uint reg, uint mask, uint value)
{
    int rc;

    pr_err("Writing 0x%02x to 0x%04x with mask 0x%02x\n", value, reg, mask);
    rc = regmap_update_bits(regmap, reg, mask, value);
    if (rc < 0)
        pr_err("failed to write 0x%02x to 0x%04x with mask 0x%02x\n",
            value, reg, mask);

    return rc;
}
/*ÏòÉÏÈ¡Õû*/
/*
static int ceil(int numerator, int denominator)
{
    int result, x, y;
    x = numerator/denominator;
    y = numerator%denominator;
    result = x + (y == 0)? 0 : 1;
}
*/
/*export functions*/
int fan53870_ldo1_set_voltage(int set_uV)
{
    int ret;
    unsigned int reg_value = 0x8E; /*1.144V*/
    struct fan53870_pw_chip *pchip = fan53870_pchip;

    pr_err("fan53870_ldo1_set_voltage: voltage = %d!\n", set_uV);
    ret = regmap_write(pchip->regmap, FAN53870_LDO1_VOUT_REG, reg_value);
    pr_err("Writing 0x%02x to 0x%02x \n", reg_value, FAN53870_LDO1_VOUT_REG);
    if (ret < 0)
        goto out;

    ret = fan53870_mask_write_reg(pchip->regmap, FAN53870_LDO12_SEQ_REG, 0x07, 0x00);
    if (ret < 0)
        goto out;

    ret = fan53870_mask_write_reg(pchip->regmap, FAN53870_LDO_ENABLE_REG, 0x01, 0x01);
    if (ret < 0)
        goto out;

    return 0;
out:
    pr_err("fan53870_ldo1_set_voltagefailed!\n");
    return ret;
}
EXPORT_SYMBOL(fan53870_ldo1_set_voltage);

int fan53870_ldo1_disable(void)
{
    int ret;
    struct fan53870_pw_chip *pchip = fan53870_pchip;
    ret = fan53870_mask_write_reg(pchip->regmap, FAN53870_LDO_ENABLE_REG, 0x01, 0x00);
    pr_err("fan53870_ldo1_disable!\n");
    return ret;
}
EXPORT_SYMBOL(fan53870_ldo1_disable);

/* Shipei.Chen@Cam.Drv, 20200416,  add the ldo3 for sensor porting!*/
#ifdef VENDOR_EDIT
int fan53870_cam_ldo_set_voltage(int LDO_NUM, int set_mv)
{
    int ret;
    unsigned int reg_value = 0; /*2.804V*/
    struct fan53870_pw_chip *pchip = fan53870_pchip;
	switch(LDO_NUM){
		case 1:
			if (is_project(OPPO_19040) || is_project(OPPO_20601) || is_project(OPPO_20660)){
				reg_value =0x83;	/*1.05V*/
			} else if (is_project(OPPO_20630)|| is_project(OPPO_20631)|| is_project(OPPO_20632)){
				reg_value =0x83;	/*1.05V*/
			}
			ret = regmap_write(pchip->regmap, FAN53870_LDO1_VOUT_REG, reg_value);
			pr_err("Writing 0x%02x to 0x%02x \n", reg_value, FAN53870_LDO1_VOUT_REG);
			if (ret < 0)
				goto out;

			ret = fan53870_mask_write_reg(pchip->regmap, FAN53870_LDO12_SEQ_REG, 0x07, 0x00);
			if (ret < 0)
				goto out;

			ret = fan53870_mask_write_reg(pchip->regmap, FAN53870_LDO_ENABLE_REG, 0x01, 0x01);
			if (ret < 0)
				goto out;
		break;
		case 2:
			reg_value =0x95;	/*1.200V*/
			ret = regmap_write(pchip->regmap, FAN53870_LDO2_VOUT_REG, reg_value);
			pr_err("Writing 0x%02x to 0x%02x \n", reg_value, FAN53870_LDO2_VOUT_REG);
			if (ret < 0)
				goto out;

			ret = fan53870_mask_write_reg(pchip->regmap, FAN53870_LDO12_SEQ_REG, 0x38, 0x00);
			if (ret < 0)
				goto out;

			ret = fan53870_mask_write_reg(pchip->regmap, FAN53870_LDO_ENABLE_REG, 0x02, 0x02);
			if (ret < 0)
				goto out;
		break;
		case 3:
			reg_value =0xb3;/*2.804V*/
			if(set_mv == 2900)
				reg_value=0xBF;
			ret = regmap_write(pchip->regmap, FAN53870_LDO3_VOUT_REG, reg_value);
			pr_err("Writing 0x%02x to 0x%02x \n", reg_value, FAN53870_LDO3_VOUT_REG);
			if (ret < 0)
				goto out;

			ret = fan53870_mask_write_reg(pchip->regmap, FAN53870_LDO34_SEQ_REG, 0x07, 0x00);
			if (ret < 0)
				goto out;

			ret = fan53870_mask_write_reg(pchip->regmap, FAN53870_LDO_ENABLE_REG, 0x04, 0x04);
			if (ret < 0)
				goto out;
		break;
		case 4:
			reg_value =0xb3;	/*2.804V*/
			ret = regmap_write(pchip->regmap, FAN53870_LDO4_VOUT_REG, reg_value);
			pr_err("Writing 0x%02x to 0x%02x \n", reg_value, FAN53870_LDO4_VOUT_REG);
			if (ret < 0)
				goto out;

			ret = fan53870_mask_write_reg(pchip->regmap, FAN53870_LDO34_SEQ_REG, 0x38, 0x00);
			if (ret < 0)
				goto out;

			ret = fan53870_mask_write_reg(pchip->regmap, FAN53870_LDO_ENABLE_REG, 0x08, 0x08);
			if (ret < 0)
				goto out;
		break;
		case 5:
			if (is_project(OPPO_19040) || is_project(OPPO_20601) || is_project(OPPO_20660)){
				reg_value =0x36;	/*1.804V*/
			} else if (is_project(OPPO_20630)|| is_project(OPPO_20631)|| is_project(OPPO_20632)){
				reg_value =0xb3;	/*2.804V*/
			}
			ret = regmap_write(pchip->regmap, FAN53870_LDO5_VOUT_REG, reg_value);
			pr_err("Writing 0x%02x to 0x%02x \n", reg_value, FAN53870_LDO5_VOUT_REG);
			if (ret < 0)
				goto out;

			ret = fan53870_mask_write_reg(pchip->regmap, FAN53870_LDO56_SEQ_REG, 0x07, 0x00);
			if (ret < 0)
				goto out;

			ret = fan53870_mask_write_reg(pchip->regmap, FAN53870_LDO_ENABLE_REG, 0x10, 0x10);
			if (ret < 0)
				goto out;
		break;
		case 6:
			if (is_project(OPPO_19040) || is_project(OPPO_20601) || is_project(OPPO_20660)){
				reg_value =0x36;	/*1.804V*/
			} else if (is_project(OPPO_20630)|| is_project(OPPO_20631)|| is_project(OPPO_20632)){
				reg_value =0xb3;	/*2.804V*/
			}
			ret = regmap_write(pchip->regmap, FAN53870_LDO6_VOUT_REG, reg_value);
			pr_err("Writing 0x%02x to 0x%02x \n", reg_value, FAN53870_LDO6_VOUT_REG);
			if (ret < 0)
				goto out;

			ret = fan53870_mask_write_reg(pchip->regmap, FAN53870_LDO56_SEQ_REG, 0x38, 0x00);
			if (ret < 0)
				goto out;

			ret = fan53870_mask_write_reg(pchip->regmap, FAN53870_LDO_ENABLE_REG, 0x20, 0x20);
			if (ret < 0)
				goto out;
		break;
		case 7:
			reg_value =0xf1;	/*3.3V*/
			ret = regmap_write(pchip->regmap, FAN53870_LDO7_VOUT_REG, reg_value);
			pr_err("Writing 0x%02x to 0x%02x \n", reg_value, FAN53870_LDO7_VOUT_REG);
			if (ret < 0)
				goto out;
                        //slot 7
			ret = fan53870_mask_write_reg(pchip->regmap, FAN53870_LDO7_SEQ_REG, 0x07, 0x00);
			pr_err("Writing 0x00 to 0x0e \n");
                         
			if (ret < 0)
				goto out;
                        //bit6 enable ldo7
			ret = fan53870_mask_write_reg(pchip->regmap, FAN53870_LDO_ENABLE_REG, 0x40, 0x40);
			pr_err("Writing 0x40 to 0x03 \n");
			if (ret < 0)
				goto out;
		break;
		default:
			printk("error ldo number\n");
			ret = -1;
			goto out;
		break;
	}
    return 0;
out:
    pr_err("fan53870_cam_ldo_set_voltage error!\n");
    return ret;
}
EXPORT_SYMBOL(fan53870_cam_ldo_set_voltage);

int fan53870_cam_ldo_disable(int LDO_NUM)
{
    int ret;
    struct fan53870_pw_chip *pchip = fan53870_pchip;
	switch (LDO_NUM){
		case 1:
			ret = fan53870_mask_write_reg(pchip->regmap, FAN53870_LDO_ENABLE_REG, 0x01, 0x00);
			pr_err("fan53870_cam_ldo_disable!\n");
		break;
		case 2:
			ret = fan53870_mask_write_reg(pchip->regmap, FAN53870_LDO_ENABLE_REG, 0x02, 0x00);
			pr_err("fan53870_cam_ldo_disable!\n");
		break;
		case 3:
			ret = fan53870_mask_write_reg(pchip->regmap, FAN53870_LDO_ENABLE_REG, 0x04, 0x00);
			pr_err("fan53870_cam_ldo_disable!\n");
		break;
		case 4:
			ret = fan53870_mask_write_reg(pchip->regmap, FAN53870_LDO_ENABLE_REG, 0x08, 0x00);
			pr_err("fan53870_cam_ldo_disable!\n");
		break;
		case 5:
			ret = fan53870_mask_write_reg(pchip->regmap, FAN53870_LDO_ENABLE_REG, 0x10, 0x00);
			pr_err("fan53870_cam_ldo_disable!\n");
		break;
		case 6:
			ret = fan53870_mask_write_reg(pchip->regmap, FAN53870_LDO_ENABLE_REG, 0x20, 0x00);
			pr_err("fan53870_cam_ldo_disable!\n");
		break;
		default:
			ret = -1;
		break;
	}
    return ret;
}
EXPORT_SYMBOL(fan53870_cam_ldo_disable);
#endif


int is_fan53870_pmic(void)
{
    pr_err("[hjz_debug]probe pmic, is_fan53870_1p1: %d \n", is_fan53870_1p1);
    return is_fan53870_1p1;
}
EXPORT_SYMBOL(is_fan53870_pmic);

static int fan53870_dt(struct device *dev, struct fan53870_platform_data *pdata)
{
    int rc = 0;
    struct device_node *np = dev->of_node;

    rc = of_property_read_u32(np, "ldo1_min_vol", &pdata->ldo1_min_vol);
    if (rc < 0) {
        pr_err("%s: failed to get regulator base rc=%d\n", "ldo1_min_vol", rc);
        return rc;
    }
    rc = of_property_read_u32(np, "ldo1_max_vol", &pdata->ldo1_max_vol);
    if (rc < 0) {
        pr_err("%s: failed to get regulator base rc=%d\n", "ldo1_max_vol", rc);
        return rc;
    }
    rc = of_property_read_u32(np, "ldo1_step_vol", &pdata->ldo1_step_vol);
    if (rc < 0) {
        pr_err("%s: failed to get regulator base rc=%d\n", "ldo1_step_vol", rc);
        return rc;
    }
    pr_err("%s: ldo1_min_vol=%d, ldo1_max_vol=%d, ldo1_step_vol=%d\n", __func__,
           pdata->ldo1_min_vol, pdata->ldo1_max_vol, pdata->ldo1_step_vol);

    return 0;
}

static struct regmap_config fan53870_regmap = {
    .reg_bits = 8,
    .val_bits = 8,
};

static int fan53870_pmic_probe(struct i2c_client *client,
               const struct i2c_device_id *id)
{
    struct fan53870_platform_data *pdata = client->dev.platform_data;
    struct fan53870_pw_chip *pchip;
    //unsigned int product_id;
    int ret = 0;

    pr_err("%s Enter\n", __func__);

    if (client->dev.of_node) {
        pdata = devm_kzalloc(&client->dev,
            sizeof(struct fan53870_platform_data), GFP_KERNEL);
        if (!pdata) {
            dev_err(&client->dev, "Failed to allocate memory\n");
            return -ENOMEM;
        }
        ret = fan53870_dt(&client->dev, pdata);
        if (ret)
            return ret;
    } else
        pdata = client->dev.platform_data;

    if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
        dev_err(&client->dev, "fail : i2c functionality check...\n");
        return -EOPNOTSUPP;
    }

    if (pdata == NULL) {
        dev_err(&client->dev, "fail : no platform data.\n");
        return -ENODATA;
    }
    pchip = devm_kzalloc(&client->dev, sizeof(struct fan53870_pw_chip),
                 GFP_KERNEL);
    if (!pchip) {
		dev_err(&client->dev, "no memory to alloc struct fan53870_pw_chip\n");
        return -ENOMEM;
	}
    fan53870_pchip = pchip;


    pchip->pdata = pdata;
    pchip->dev = &client->dev;

    pchip->regmap = devm_regmap_init_i2c(client, &fan53870_regmap);
    if (IS_ERR(pchip->regmap)) {
        ret = PTR_ERR(pchip->regmap);
        dev_err(&client->dev, "fail : allocate register map: %d\n",
            ret);
        goto error_enable;
    }

    i2c_set_clientdata(client, pchip);

    /*
    ret = regmap_read(pchip->regmap, FAN53870_PRODUCT_ID_REG, &product_id);
    if (ret == 0 && product_id == 0x01) {
        is_fan53870_1p1 = 1;
    } else {
        is_fan53870_1p1 = 0;
    }
    */
    is_fan53870_1p1 = 1;

    pr_err("%s : probe done\n", __func__);

    return 0;

error_enable:

    devm_kfree(&client->dev, pchip->pdata);
    devm_kfree(&client->dev, pchip);
    pr_err("%s : probe failed\n", __func__);
    return ret;
}

static int fan53870_pmic_remove(struct i2c_client *client)
{
    struct fan53870_pw_chip *pchip = i2c_get_clientdata(client);
    int ret = 0;

	if(!pchip || !pchip->regmap || !pchip->pdata) {
		dev_err(&client->dev, "%s, have null pointer!\n", __func__);
		return -1;
	}

    dev_info(pchip->dev, "%s :  failed\n", __func__);

    ret = regmap_write(pchip->regmap, FAN53870_LDO_ENABLE_REG, 0x00);
    if (ret < 0)
        dev_err(pchip->dev, "i2c failed to access register\n");

    return 0;
}

static const struct i2c_device_id fan53870_pmic_ids[] = {
    { "fan53870", 0 },
    { }
};

static struct i2c_driver fan53870_i2c_driver = {
    .probe = fan53870_pmic_probe,
    .remove = fan53870_pmic_remove,
    .driver = {
        .name = "fan53870",
        .owner = THIS_MODULE,
    },
    .id_table = fan53870_pmic_ids,
};

module_i2c_driver(fan53870_i2c_driver);

MODULE_DESCRIPTION("FAN53870 Power Driver");
MODULE_AUTHOR("xxx");
MODULE_LICENSE("GPL");

