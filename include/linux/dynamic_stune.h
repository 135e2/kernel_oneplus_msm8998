// SPDX-License-Identifier: GPL-2.0
/*
 * Dynamic Schedtune Driver
 * Copyright (C) 2020 Edrick Vince Sinsuan <sedrickvince@gmail.com>.
 */

#ifndef _DYNAMIC_STUNE_H_
#define _DYNAMIC_STUNE_H_

#include <linux/kthread.h>

#define BOOST_DURATION msecs_to_jiffies(CONFIG_STUNE_BOOST_DURATION)
#define CRUCIAL_DURATION msecs_to_jiffies(CONFIG_STUNE_CRUCIAL_DURATION)
#define INPUT_DURATION msecs_to_jiffies(CONFIG_INPUT_INTERVAL_DURATION)

extern unsigned long last_input_time, last_boost_time, last_crucial_time;
#define INPUT_INTERVAL (last_input_time + INPUT_DURATION)
#define BOOST_CLEARANCE (last_boost_time + (BOOST_DURATION >> 1))
#define CRUCIAL_CLEARANCE (last_crucial_time + (CRUCIAL_DURATION >> 1))

#define STATE_BIT BIT(0)

struct dstune {
	wait_queue_head_t waitq;
	atomic_t lock;
	unsigned int state;
};

extern struct dstune boost, crucial;

static __always_inline void dynstune_trigger(struct dstune *ds, bool enable)
{
    bool state;

	if (atomic_cmpxchg_acquire(&ds->lock, 0, 1))
		return;

    state = ds->state & STATE_BIT;

    if (!state && enable)
        ds->state |= STATE_BIT;
    else if (state && !enable)
        ds->state &= ~STATE_BIT;

	wake_up(&ds->waitq);
}

#define enable_boost() dynstune_trigger(&boost, true)
#define enable_crucial() dynstune_trigger(&crucial, true)

#endif /* _DYNAMIC_STUNE_H_ */