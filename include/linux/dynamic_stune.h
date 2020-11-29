// SPDX-License-Identifier: GPL-2.0
/*
 * Dynamic Schedtune Driver
 * Copyright (C) 2020 Edrick Vince Sinsuan <sedrickvince@gmail.com>.
 */

#ifndef _DYNAMIC_STUNE_H_
#define _DYNAMIC_STUNE_H_

#include <linux/kthread.h>

enum dstune_struct {
	FB,
	TOPCG,
	INPUT,
	DT_MAX
};

struct dstune {
	wait_queue_head_t waitq;
	atomic_t update;
};

extern struct dstune dss[];
extern atomic_t input_lock;

static __always_inline void dynstune_trigger(enum dstune_struct ds_num)
{
	struct dstune *ds = &dss[ds_num];

	if (!atomic_cmpxchg_acquire(&ds->update, 0, 1))
		wake_up(&ds->waitq);
}

#endif /* _DYNAMIC_STUNE_H_ */