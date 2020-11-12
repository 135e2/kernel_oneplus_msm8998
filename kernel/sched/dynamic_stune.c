// SPDX-License-Identifier: GPL-2.0
/*
 * Dynamic Schedtune Driver
 * Copyright (C) 2020 Edrick Vince Sinsuan <sedrickvince@gmail.com>.
 */

#include <linux/dynamic_stune.h>

#include "tune.h"

unsigned long last_boost_time, last_crucial_time;

static __always_inline void set_stune_boost(bool state)
{
	/*
	 * Enable boost and prefer_idle in order to bias migrating top-app 
	 * (also for foreground) tasks to idle big cluster cores.
	 */
	do_boost("top-app", state);
	do_prefer_idle("top-app", state);
	do_prefer_idle("foreground", state);
}

static void enable_boost(struct work_struct *work)
{
	set_stune_boost(true);
}

static void disable_boost(struct work_struct *work)
{
	set_stune_boost(false);
}

struct dstune_val boost = {
	.last_time = &last_boost_time,
	.enable = __WORK_INITIALIZER(boost.enable, enable_boost),
	.disable = __DELAYED_WORK_INITIALIZER(boost.disable, disable_boost, 0),
	.lock = ATOMIC_INIT(0)
};

static __always_inline void set_stune_crucial(bool state)
{
	/*
	 * Use idle cpus with the highest original capacity for top-app when it
	 * comes to app launches and transitions in order to speed up 
	 * the process and efficiently consume power.
	 */
	do_crucial("top-app", state);
}

static void enable_crucial(struct work_struct *work)
{
	set_stune_crucial(true);
}

static void disable_crucial(struct work_struct *work)
{
	set_stune_crucial(false);
}

struct dstune_val crucial = {
	.last_time = &last_crucial_time,
	.enable = __WORK_INITIALIZER(crucial.enable, enable_crucial),
	.disable = __DELAYED_WORK_INITIALIZER(crucial.disable, disable_crucial, 0),
	.lock = ATOMIC_INIT(0)
};

static __always_inline 
int init_dstune_workqueue(struct dstune_val *ds, const char namefmt[])
{
	ds->wq = alloc_workqueue(namefmt, WQ_HIGHPRI | WQ_FREEZABLE, 1);
	if (!ds->wq)
		return -ENOMEM;

	return 0;
}

static int __init dynamic_stune_init(void)
{
	int ret = 0;

	ret = init_dstune_workqueue(&boost, "dstune_boost_wq");
	if (ret)
		return ret;

	ret = init_dstune_workqueue(&crucial, "dstune_crucial_wq");
	if (ret)
		destroy_workqueue(boost.wq);

	return ret;
}
late_initcall(dynamic_stune_init);