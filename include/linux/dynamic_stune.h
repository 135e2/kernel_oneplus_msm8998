// SPDX-License-Identifier: GPL-2.0
/*
 * Dynamic Schedtune Driver
 * Copyright (C) 2020 Edrick Vince Sinsuan <sedrickvince@gmail.com>.
 */

#ifndef _DYNAMIC_STUNE_H_
#define _DYNAMIC_STUNE_H_

#include <linux/cpumask.h>
#include <linux/workqueue.h>

#define BOOST_DURATION msecs_to_jiffies(CONFIG_STUNE_BOOST_DURATION)
#define CRUCIAL_DURATION msecs_to_jiffies(CONFIG_STUNE_CRUCIAL_DURATION)
#define INPUT_DURATION msecs_to_jiffies(CONFIG_INPUT_INTERVAL_DURATION)

extern unsigned long last_input_time, last_boost_time, last_crucial_time;
#define INPUT_INTERVAL (last_input_time + INPUT_DURATION)
#define BOOST_CLEARANCE (last_boost_time + (BOOST_DURATION >> 1))
#define CRUCIAL_CLEARANCE (last_crucial_time + (CRUCIAL_DURATION >> 1))

#define DT_CPU cpumask_first_and(cpu_perf_mask, cpu_active_mask)
#define DD_CPU cpumask_first_and(cpu_lp_mask, cpu_active_mask)

struct dstune_val {
	unsigned long *last_time;
	struct workqueue_struct *wq;
	struct work_struct enable;
	struct delayed_work disable;
	atomic_t lock;
};

extern struct dstune_val boost, crucial;

static __always_inline 
void dynstune_trigger(struct dstune_val *ds, unsigned short duration)
{
	if (atomic_cmpxchg(&ds->lock, 0, 1))
		return;

	/* Queue disable_boost on LP cluster */
	if (!mod_delayed_work_on(DD_CPU, ds->wq, &ds->disable, duration))
		queue_work_on(DT_CPU, ds->wq, &ds->enable);

	/* Update last_time after queueing */
	*ds->last_time = jiffies;

	atomic_set_release(&ds->lock, 0);
}

#endif /* _DYNAMIC_STUNE_H_ */