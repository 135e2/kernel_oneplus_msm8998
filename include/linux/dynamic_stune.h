// SPDX-License-Identifier: GPL-2.0
/*
 * Dynamic Schedtune Driver
 * Copyright (C) 2020 Edrick Vince Sinsuan <sedrickvince@gmail.com>.
 */

#ifndef _DYNAMIC_STUNE_H_
#define _DYNAMIC_STUNE_H_

#include <linux/kthread.h>

extern unsigned long last_input_time, input_margin;
#define INPUT_INTERVAL (last_input_time + input_margin)

struct dstune {
	wait_queue_head_t waitq;
	atomic_t lock;
	bool state;
};

extern struct dstune boost, crucial;

static __always_inline void dynstune_trigger(struct dstune *ds, bool enable)
{
	if (!atomic_cmpxchg_acquire(&ds->lock, 0, 1)) {
        ds->state = enable;
        wake_up(&ds->waitq);
    }
}

#define enable_boost() dynstune_trigger(&boost, true)
#define enable_crucial() dynstune_trigger(&crucial, true)

#endif /* _DYNAMIC_STUNE_H_ */