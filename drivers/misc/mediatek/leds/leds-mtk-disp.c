// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 MediaTek Inc.
 * Copyright (C) 2021 XiaoMi, Inc.
 *
 */

#include <linux/ctype.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/sched/clock.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/workqueue.h>

#include "leds-mtk-disp.h"
#include <linux/kobject.h>
#include <linux/sysfs.h>

/* BSP.LCM - 2020.11.25 - modify to set brightness */
#ifdef CONFIG_LM3697_SUPPORT
#include <linux/mfd/ti-lmu-backlight.h>
#endif
/* end modify */

/* BSP.LCM - 2020.11.25 - modify to set brightness */
#ifdef CONFIG_KTD3136_SUPPORT
#include <linux/mfd/ktd3136.h>
#endif
/* end modify */

#ifdef CONFIG_DRM_MEDIATEK
extern int mtkfb_set_backlight_level(unsigned int level);
#endif

#ifdef MET_USER_EVENT_SUPPORT
#include <mt-plat/met_drv.h>
#endif

#define CONFIG_LEDS_BRIGHTNESS_CHANGED
/****************************************************************************
 * variables
 ***************************************************************************/
#undef pr_fmt
#define pr_fmt(fmt) KBUILD_MODNAME " %s(%d) :" fmt, __func__, __LINE__

struct mtk_leds_info;

static int led_level_set(struct led_classdev *led_cdev,
					 enum led_brightness brightness);

struct led_debug_info {
	unsigned long long current_t;
	unsigned long long last_t;
	char buffer[4096];
	int count;
};

struct led_desp {
	int index;
	char name[16];
};

struct leds_desp_info {
	int lens;
	struct led_desp *leds[0];
};

struct mtk_led_data {
	struct led_desp desp;
	struct led_conf_info conf;
	int last_level;
	int brightness;
	struct mtk_leds_info	*parent;
	struct led_debug_info debug;
};

struct mtk_leds_info {
	struct device *dev;
	int			nums;
	struct mtk_led_data leds[0];
};

static DEFINE_MUTEX(leds_mutex);
struct leds_desp_info *leds_info;
static BLOCKING_NOTIFIER_HEAD(mtk_leds_chain_head);

int mtk_leds_register_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&mtk_leds_chain_head, nb);
}
EXPORT_SYMBOL_GPL(mtk_leds_register_notifier);

int mtk_leds_unregister_notifier(struct notifier_block *nb)

{
	return blocking_notifier_chain_unregister(&mtk_leds_chain_head, nb);
}
EXPORT_SYMBOL_GPL(mtk_leds_unregister_notifier);

int mtk_leds_call_notifier(unsigned long action, void *data)
{
	return blocking_notifier_call_chain(&mtk_leds_chain_head, action, data);
}
EXPORT_SYMBOL_GPL(mtk_leds_call_notifier);


static int call_notifier(int event, struct mtk_led_data *led_dat)
{
	int err;

	err = mtk_leds_call_notifier(event, &led_dat->conf);
	if (err)
		pr_err("notifier_call_chain error\n");
	return err;
}

/****************************************************************************
 * DEBUG MACROS
 ***************************************************************************/


static int getLedDespIndex(char *name)
{
	int i = 0;

	while (i < leds_info->lens) {
		if (!strcmp(name, leds_info->leds[i]->name))
			return i;
		i++;
	}
	return -1;
}


/****************************************************************************
 * driver functions
 ***************************************************************************/
extern char *saved_command_line;
static int led_level_disp_set(struct mtk_led_data *s_led,
	int brightness)
{
/* BSP.LCM - 2020.11.25 - modify to set brightness */
#if defined(CONFIG_KTD3136_SUPPORT) || defined(CONFIG_LM3697_SUPPORT)
	int bkl_id = 0;
	char *bkl_ptr = (char *)strnstr(saved_command_line, ":bklic=", strlen(saved_command_line));
#endif
/* end modify */

	brightness = min(brightness, s_led->conf.max_level);
	if (brightness == s_led->conf.level)
		return 0;

#ifdef MET_USER_EVENT_SUPPORT
	if (enable_met_backlight_tag())
		output_met_backlight_tag(brightness);
#endif
#ifdef CONFIG_DRM_MEDIATEK
	mtkfb_set_backlight_level(brightness);
	s_led->conf.level = brightness;
#endif

/* BSP.LCM - 2020.11.25 - modify to set brightness */
#if defined(CONFIG_KTD3136_SUPPORT) || defined(CONFIG_LM3697_SUPPORT)
	bkl_ptr += strlen(":bklic=");
	bkl_id = simple_strtol(bkl_ptr, NULL, 10);
	if (bkl_id == 24) {
		ktd3137_brightness_set(brightness);
		s_led->conf.level = brightness;
		printk("[%s]: backlight is ktd3136 contrl! brightness=%d\n", __func__, brightness);
	} else if (bkl_id == 1) {
		lm3697_set_brightness(brightness);
		s_led->conf.level = brightness;
		printk("[%s]: backlight is lm3697 contrl! brightness=%d\n", __func__, brightness);
	}
#endif
/* end modify */
	return 0;

}


/****************************************************************************
 * add API for temperature control
 ***************************************************************************/

int setMaxBrightness(char *name, int percent, bool enable)
{
	struct mtk_led_data *led_dat;
		int max_l = 0, index = -1, limit_l = 0, cur_l = 0;

	index = getLedDespIndex(name);
	if (index < 0) {
		pr_err("can not find leds by led_desp %s", name);
		return -1;
	}
	led_dat = container_of(leds_info->leds[index],
		struct mtk_led_data, desp);

	max_l = led_dat->conf.cdev.max_brightness;
	limit_l = (percent * max_l) / 100;
	pr_debug("before: name: %s, percent : %d, limit_l : %d, enable: %d",
		leds_info->leds[index]->name, percent, limit_l, enable);
	if (enable) {
		led_dat->conf.max_level = limit_l;
		cur_l = min(led_dat->last_level, limit_l);
	} else if (!enable) {
		led_dat->conf.max_level = max_l;
		cur_l = led_dat->last_level;
	}
#ifdef CONFIG_LEDS_BRIGHTNESS_CHANGED
	call_notifier(3, led_dat);
#endif
	if (led_dat->conf.cdev.brightness != 0)
		led_level_disp_set(led_dat, cur_l);

	return 0;

}
EXPORT_SYMBOL(setMaxBrightness);


int mt_leds_brightness_set(char *name, int level)
{
	struct mtk_led_data *led_dat;
	int index, led_Level;

	index = getLedDespIndex(name);
	if (index < 0) {
		pr_err("can not find leds by led_desp %s", name);
		return -1;
	}
	led_dat = container_of(leds_info->leds[index],
		struct mtk_led_data, desp);

	led_Level = (
		(((1 << led_dat->conf.led_bits) - 1) * level
		+ (((1 << led_dat->conf.trans_bits) - 1) / 2))
		/ ((1 << led_dat->conf.trans_bits) - 1));
	printk("mtk_debug %s led_level = %d\n", __func__, led_Level);

	led_level_disp_set(led_dat, led_Level);
	led_dat->last_level = led_Level;

	return 0;
}
EXPORT_SYMBOL(mt_leds_brightness_set);

static int led_level_set(struct led_classdev *led_cdev,
					  enum led_brightness brightness)
{
	int trans_level = 0;

	struct led_conf_info *led_conf =
		container_of(led_cdev, struct led_conf_info, cdev);
	struct mtk_led_data *led_dat =
		container_of(led_conf, struct mtk_led_data, conf);

	if (led_dat->brightness == brightness)
		return 0;

	trans_level = (
		(((1 << led_dat->conf.trans_bits) - 1) * brightness
		+ (((1 << led_dat->conf.led_bits) - 1) / 2))
		/ ((1 << led_dat->conf.led_bits) - 1));

	printk("mtk_debug %s trans_level = %d\n", __func__, trans_level);

#ifdef MET_USER_EVENT_SUPPORT
	if (enable_met_backlight_tag())
		output_met_backlight_tag(brightness);
#endif

led_dat->brightness = brightness;
#ifdef CONFIG_LEDS_BRIGHTNESS_CHANGED
	call_notifier(1, led_dat);
#endif
#ifdef CONFIG_MTK_AAL_SUPPORT
	disp_pq_notify_backlight_changed(trans_level);
#else
	led_level_disp_set(led_dat, brightness);
	led_dat->last_level = brightness;
#endif
	return 0;

}

/* OPlus brightness fix */
static struct kobject *oplus_touchpanel_kobj;
static struct kobject *oplus_display_kobj;

static int oplus_backlight_cover(int value)
{
	int input_min = 222, input_max = 8191;
	int input_span = 7969, out_span = 2046;
	int output_min = 1, output_max = 2047;

	if (value <= 0)
		return 0;
	if (value <= input_min)
		return output_min;
	if (value >= input_max)
		return output_max;

	return (((value - input_min) * out_span * 2 + input_span) / (input_span * 2)) + output_min;
}

static ssize_t oplus_backlight_show(struct kobject *kobj,
				    struct kobj_attribute *attr, char *buf)
{
	int index = getLedDespIndex("lcd-backlight");
	struct mtk_led_data *led_dat;

	if (index >= 0 && leds_info && leds_info->leds[index]) {
		led_dat = container_of(leds_info->leds[index],
				       struct mtk_led_data, desp);
		return sprintf(buf, "%d\n", led_dat->brightness);
	}
	return sprintf(buf, "0\n");
}

static ssize_t oplus_backlight_store(struct kobject *kobj,
				     struct kobj_attribute *attr,
				     const char *buf, size_t count)
{
	int value = 0;
	int index;
	struct mtk_led_data *led_dat;

	if (sscanf(buf, "%d", &value) != 1)
		return -EINVAL;

	value = oplus_backlight_cover(value);

	index = getLedDespIndex("lcd-backlight");
	if (index >= 0 && leds_info && leds_info->leds[index]) {
		led_dat = container_of(leds_info->leds[index],
				       struct mtk_led_data, desp);
		led_dat->conf.cdev.brightness = value;
		led_level_set(&led_dat->conf.cdev, value);
	}

	return count;
}

static struct kobj_attribute oplus_backlight_attr =
	__ATTR(oplus_backlight, 0664, oplus_backlight_show, oplus_backlight_store);

static struct kobj_attribute oplus_panel_brightness_attr =
	__ATTR(write_panel_brightness, 0664, oplus_backlight_show, oplus_backlight_store);

static ssize_t dev_oplus_backlight_show(struct device *dev,
					struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", led_cdev->brightness);
}

static ssize_t dev_oplus_backlight_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	int value = 0;
	struct led_classdev *led_cdev = dev_get_drvdata(dev);

	if (sscanf(buf, "%d", &value) != 1)
		return -EINVAL;

	value = oplus_backlight_cover(value);
	led_cdev->brightness = value;
	led_level_set(led_cdev, value);

	return count;
}

static DEVICE_ATTR(oplus_backlight, 0664, dev_oplus_backlight_show, dev_oplus_backlight_store);

static int led_data_init(struct device *dev, struct mtk_led_data *s_led)
{
	int ret;

	s_led->conf.cdev.flags = LED_CORE_SUSPENDRESUME;
	s_led->conf.cdev.brightness_set_blocking = led_level_set;
	s_led->brightness = s_led->conf.cdev.max_brightness;
	s_led->conf.level = s_led->conf.cdev.max_brightness;
	s_led->last_level = s_led->conf.cdev.max_brightness;
	ret = devm_led_classdev_register(dev, &(s_led->conf.cdev));
	if (ret < 0) {
		pr_err("led class register fail!");
		return ret;
	}
	pr_debug("%s devm_led_classdev_register ok! ", s_led->conf.cdev.name);

	if (!strcmp(s_led->conf.cdev.name, "lcd-backlight")) {
		if (device_create_file(s_led->conf.cdev.dev, &dev_attr_oplus_backlight))
			pr_err("failed to create oplus_backlight dev attr\n");
	}

	led_level_set(&s_led->conf.cdev, s_led->conf.cdev.brightness);
	return 0;

}

static int mtk_leds_parse_dt(struct device *dev,
		struct mtk_leds_info *m_leds)
{
	struct device_node *leds_np, *child;
	struct mtk_led_data *s_led;
	int ret = 0, num = 0, level = 0;
	const char *state;

	leds_np = of_find_node_by_name(dev->of_node, "backlight");
	if (!leds_np) {
		pr_err("Error load dts node, node name error!");
		ret = -EINVAL;
		return ret;
	}

	for_each_available_child_of_node(dev->of_node, child) {

		s_led = &(m_leds->leds[num]);
		ret = of_property_read_string(child, "label",
			&s_led->conf.cdev.name);
		if (ret) {
			pr_err("Fail to read label property");
			ret = -EINVAL;
			goto out_led_dt;
		}
		ret = of_property_read_u32(child,
			"led-bits", &(s_led->conf.led_bits));
		if (ret) {
			pr_warn("No led-bits, use default value 8");
			s_led->conf.led_bits = 8;
		}
		s_led->conf.cdev.max_brightness =
			(1 << s_led->conf.led_bits) - 1;
		ret = of_property_read_u32(child,
			"trans-bits", &(s_led->conf.trans_bits));
		if (ret) {
			pr_warn("No trans-bits, use default value 10");
			s_led->conf.trans_bits = 10;
		}
		ret = of_property_read_u32(child,
			"max-brightness", &(s_led->conf.max_level));
		if (ret) {
			s_led->conf.max_level = s_led->conf.cdev.max_brightness;
			pr_warn("No max-brightness, use default: %d",
				s_led->conf.max_level);
		}
		ret = of_property_read_string(child, "default-state", &state);
		if (!ret) {
			if (!strcmp(state, "half"))
				level = s_led->conf.cdev.max_brightness / 2;
			else if (!strcmp(state, "on"))
				level = s_led->conf.cdev.max_brightness;
			else
				level = 0;
		} else {
			level = s_led->conf.cdev.max_brightness;
		}
		pr_debug("parse %d leds dt: %s, %d, %d",
			num, s_led->conf.cdev.name,
			s_led->conf.max_level,
			s_led->conf.led_bits);
		strncpy(s_led->desp.name, s_led->conf.cdev.name,
			strlen(s_led->conf.cdev.name));
		s_led->desp.index = num;
		leds_info->leds[num] = &s_led->desp;
		s_led->conf.cdev.brightness = level;
		ret = led_data_init(dev, s_led);
		if (ret)
			goto out_led_dt;
		led_level_set(&s_led->conf.cdev, level);
		num++;
	}
	return 0;
out_led_dt:
	pr_err("Error load dts node!");
	of_node_put(child);
	return ret;
}


/****************************************************************************
 * driver functions
 ***************************************************************************/

static int mtk_leds_probe(struct platform_device *pdev)
{

	struct device *dev = &pdev->dev;
	struct mtk_leds_info *m_leds;
	int ret, nums;


	nums = of_get_child_count(dev->of_node);
	pr_debug("Load dts node nums: %d", nums);
	m_leds = devm_kzalloc(dev, (sizeof(struct mtk_leds_info) +
		(sizeof(struct mtk_led_data) * (nums))), GFP_KERNEL);
	if (!m_leds) {
		ret = -ENOMEM;
		goto err;
	}
	leds_info = devm_kzalloc(dev, (sizeof(struct leds_desp_info) +
		sizeof(struct led_desp *) * (nums)),
		GFP_KERNEL);
	leds_info->lens = nums;
	if (!leds_info) {
		ret = -ENOMEM;
		goto err;
	}

	ret = mtk_leds_parse_dt(&(pdev->dev), m_leds);
	if (ret) {
		pr_err("Failed to parse devicetree!\n");
		goto err;
	}

	platform_set_drvdata(pdev, m_leds);
	m_leds->dev = dev;

	oplus_touchpanel_kobj = kobject_create_and_add("touchpanel", NULL);
	if (oplus_touchpanel_kobj) {
		if (sysfs_create_file(oplus_touchpanel_kobj, &oplus_backlight_attr.attr))
			pr_err("Failed to create oplus_backlight in touchpanel\n");
	}
	oplus_display_kobj = kobject_create_and_add("oplus_display", kernel_kobj);
	if (oplus_display_kobj) {
		if (sysfs_create_file(oplus_display_kobj, &oplus_panel_brightness_attr.attr))
			pr_err("Failed to create write_panel_brightness in oplus_display\n");
	}

	return ret;
 err:
	pr_notice("Failed to probe!");
	return ret;
}

static int mtk_leds_remove(struct platform_device *pdev)
{
	int i;
	struct mtk_leds_info *m_leds = dev_get_platdata(&pdev->dev);

	if (m_leds)
		return 0;
	for (i = 0; i < m_leds->nums; i++) {
		if (!m_leds->leds[i].parent)
			continue;
		led_classdev_unregister(&m_leds->leds[i].conf.cdev);
		m_leds->leds[i].parent = NULL;
	}
	if (oplus_touchpanel_kobj) {
		sysfs_remove_file(oplus_touchpanel_kobj, &oplus_backlight_attr.attr);
		kobject_put(oplus_touchpanel_kobj);
		oplus_touchpanel_kobj = NULL;
	}
	if (oplus_display_kobj) {
		sysfs_remove_file(oplus_display_kobj, &oplus_panel_brightness_attr.attr);
		kobject_put(oplus_display_kobj);
		oplus_display_kobj = NULL;
	}

	kfree(m_leds);
	m_leds = NULL;

	return 0;
}

static void mtk_leds_shutdown(struct platform_device *pdev)
{
	int i;
	struct mtk_leds_info *m_leds = dev_get_platdata(&pdev->dev);


	for (i = 0; m_leds && i < m_leds->nums; i++) {
		if (!&(m_leds->leds[i]))
			continue;
#ifdef CONFIG_LEDS_BRIGHTNESS_CHANGED
		call_notifier(2, &m_leds->leds[i]);
#ifdef CONFIG_MTK_AAL_SUPPORT
		continue;
#endif
#endif
		led_level_disp_set(&m_leds->leds[i], 0);
	}
}

static const struct of_device_id of_mtk_disp_leds_match[] = {
	{ .compatible = "mediatek,disp-leds", },
	{},
};
MODULE_DEVICE_TABLE(of, of_mtk_disp_leds_match);

static struct platform_driver mtk_disp_leds_driver = {
	.driver = {
		   .name = "mtk-disp-leds",
		   .of_match_table = of_mtk_disp_leds_match,
		   },
	.probe = mtk_leds_probe,
	.remove = mtk_leds_remove,
	.shutdown = mtk_leds_shutdown,
};

static int __init mtk_leds_init(void)
{
	int ret;

	ret = platform_driver_register(&mtk_disp_leds_driver);

	if (ret) {
		pr_err("driver register error: %d", ret);
		return ret;
	}

	return ret;
}

static void __exit mtk_leds_exit(void)
{
	platform_driver_unregister(&mtk_disp_leds_driver);
}

/* delay leds init, for (1)display has delayed to use clock upstream.
 * (2)to fix repeat switch battary and power supply caused BL KE issue,
 * battary calling bl .shutdown whitch need to call disp_pwm and display
 * function and they not yet probe.
 */
late_initcall(mtk_leds_init);
module_exit(mtk_leds_exit);

MODULE_AUTHOR("Mediatek Corporation");
MODULE_DESCRIPTION("MTK Display Backlight Driver");
MODULE_LICENSE("GPL");



