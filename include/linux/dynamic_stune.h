// SPDX-License-Identifier: GPL-2.0
/*
 * Dynamic Schedtune Driver
 * Copyright (C) 2021 Edrick Vince Sinsuan <sedrickvince@gmail.com>.
 */

#ifndef _DYNAMIC_STUNE_H_
#define _DYNAMIC_STUNE_H_

#include <linux/kthread.h>

enum dstune_struct {
	FB,
	INPUT,
	DT_MAX
};

struct dstune {
	wait_queue_head_t waitq;
	atomic_t update, state;
};

extern struct dstune dss[];

#define dynstune_read_state(_dsnum) atomic_read(&dss[_dsnum].state)
#define dynstune_acquire_update(_dsnum) \
			atomic_cmpxchg_acquire(&dss[_dsnum].update, 0, 1)
#define dynstune_wake(_dsnum) wake_up(&dss[_dsnum].waitq)
#endif /* _DYNAMIC_STUNE_H_ */