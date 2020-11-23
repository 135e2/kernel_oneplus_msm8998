// SPDX-License-Identifier: GPL-2.0
/*
 * Dynamic Schedtune Driver
 * Copyright (C) 2020 Edrick Vince Sinsuan <sedrickvince@gmail.com>.
 */

#ifndef _DYNAMIC_STUNE_H_
#define _DYNAMIC_STUNE_H_

#include <linux/kthread.h>

struct dstune {
	wait_queue_head_t waitq;
	atomic_t trigger, update;
};

extern struct dstune fb, topcg, input;

static __always_inline void dynstune_trigger(struct dstune *ds)
{
	/* Check update first as it'll be acquired the most */
	if (!atomic_cmpxchg_acquire(&ds->update, 0, 1)) {
		atomic_cmpxchg_acquire(&ds->trigger, 0, 1);
		wake_up(&ds->waitq);
	}
}

#define enable_fb() dynstune_trigger(&fb)
#define enable_topcg() dynstune_trigger(&topcg)
#define enable_input() dynstune_trigger(&input)

/* Read trigger lock for checking if within interval */
#define allow_fb() atomic_read(&input.trigger)

#endif /* _DYNAMIC_STUNE_H_ */