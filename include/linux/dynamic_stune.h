// SPDX-License-Identifier: GPL-2.0
/*
 * Dynamic Schedtune Driver
 * Copyright (C) 2020 Edrick Vince Sinsuan <sedrickvince@gmail.com>.
 */

#ifndef _DYNAMIC_STUNE_H_
#define _DYNAMIC_STUNE_H_

#include <linux/workqueue.h>

/*
 * Configurable variables
 */
#define BOOST_DURATION msecs_to_jiffies(CONFIG_STUNE_BOOST_DURATION)
#define CRUCIAL_DURATION msecs_to_jiffies(CONFIG_STUNE_CRUCIAL_DURATION)
#define INPUT_DURATION msecs_to_jiffies(CONFIG_INPUT_INTERVAL_DURATION)

/*
 * Time prohibitions for triggering boosts
 */
extern unsigned long last_input_time, last_boost_time, last_crucial_time;
#define INPUT_INTERVAL (last_input_time + INPUT_DURATION)
#define BOOST_CLEARANCE (last_boost_time + (BOOST_DURATION >> 1))
#define CRUCIAL_CLEARANCE (last_crucial_time + (CRUCIAL_DURATION >> 1))

/*
 * Check if pending before allowing execution
 */
#define dynstune_allowed(_ds) !work_pending(&_ds.enable)

/*
 * Trigger function
 */
#define dynstune_trigger(_ds) queue_work(_ds.wq, &_ds.enable)

/*
 * Driver variables and structures
 */
struct dstune_val {
	unsigned long *last_time;
	struct workqueue_struct *wq;
	struct work_struct enable;
	struct delayed_work disable;
	void (*set_stune)(bool state);
};

/*
 * Allow access to other objects
 */
extern struct dstune_val boost, crucial;

#endif /* _DYNAMIC_STUNE_H_ */