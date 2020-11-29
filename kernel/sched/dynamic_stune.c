// SPDX-License-Identifier: GPL-2.0
/*
 * Dynamic Schedtune Driver
 * Copyright (C) 2020 Edrick Vince Sinsuan <sedrickvince@gmail.com>.
 */

#include <linux/sched.h>
#include <linux/dynamic_stune.h>

#include "tune.h"

struct dstune_priv {
	char name[NAME_MAX];
	struct dstune *ds;
	unsigned long duration;
	bool perf_critical;
	void (*set_func)(bool state);
};

static struct dstune_priv dss_priv[DT_MAX];
struct dstune dss[DT_MAX];

/*
 * Framebuffer function
 */
static void set_fb(bool state)
{
	/*
	 * Enable boost and prefer_idle in order to bias migrating top-app
	 * tasks to idle big cluster cores. Also enable bias for foreground
	 * to help with jitter reduction.
	 */
	do_boost("top-app", state);
	do_prefer_idle("top-app", state);
	do_boost_bias("foreground", state);
}

/*
 * Top-app cgroup function
 */
static void set_topcg(bool state)
{
	/*
	 * Use idle cpus with the highest original capacity for top-app when it
	 * comes to app launches and transitions in order to speed up
	 * the process and efficiently consume power.
	 */
	do_crucial("top-app", state);
}

/*
 * Input function
 */
atomic_t input_lock = ATOMIC_INIT(0);

static void set_input(bool state)
{
	/* Set lock to be checked by fb structure */
	atomic_set(&input_lock, state);
}

static int dstune_thread(void *data)
{
	static const struct sched_param sched_max_rt_prio = {
		.sched_priority = MAX_RT_PRIO - 1
	};
	struct dstune_priv *dsp = data;

	sched_setscheduler_nocheck(current, SCHED_FIFO, &sched_max_rt_prio);

	while (1) {
		bool should_stop = false;

		wait_event(dsp->ds->waitq,
			atomic_read(&dsp->ds->update) ||
			(should_stop = kthread_should_stop()));

		if (should_stop)
			break;

		dsp->set_func(true);

		while (1) {
			unsigned long time = dsp->duration;

			atomic_set_release(&dsp->ds->update, 0);

			time = wait_event_timeout(dsp->ds->waitq,
						atomic_read(&dsp->ds->update) ||
						(should_stop = kthread_should_stop()), time);

			if (should_stop || !time)
				break;

			while (time)
				time = schedule_timeout_uninterruptible(time);
		}

		dsp->set_func(false);

		atomic_set_release(&dsp->ds->update, 0);
	}

	return 0;
}

static int dstune_kthread_init(struct dstune_priv *dsp)
{
	struct task_struct *thread;
	int ret = 0;

	thread = !dsp->perf_critical ? kthread_run(dstune_thread, dsp, dsp->name) :
				kthread_run_perf_critical(dstune_thread, dsp, dsp->name);
	if (IS_ERR(thread)) {
		ret = PTR_ERR(thread);
		pr_err("Failed to start stune thread, err: %d\n", ret);
	}

	return ret;
}

static int dstune_struct_init(enum dstune_struct ds_num)
{
	struct dstune_priv *dsp = &dss_priv[ds_num];
	struct dstune ds = {
		.waitq = __WAIT_QUEUE_HEAD_INITIALIZER(dss[ds_num].waitq),
		.update = ATOMIC_INIT(0)
	};

	dss[ds_num] = ds;
	dsp->ds = &dss[ds_num];

	switch (ds_num) {
		case FB:
			strcpy(dsp->name, "dstune_fb");
			dsp->set_func = &set_fb;
			dsp->perf_critical = true;
			dsp->duration =
				msecs_to_jiffies(CONFIG_FB_STUNE_DURATION);
			break;
		case TOPCG:
			strcpy(dsp->name, "dstune_topcg");
			dsp->set_func = &set_topcg;
			dsp->perf_critical = false;
			dsp->duration =
				msecs_to_jiffies(CONFIG_TOPCG_STUNE_DURATION);
			break;
		case INPUT:
			strcpy(dsp->name, "dstune_input");
			dsp->set_func = &set_input;
			dsp->perf_critical = false;
			dsp->duration =
				msecs_to_jiffies(CONFIG_INPUT_STUNE_DURATION);
			break;
		default:
			break;
	}

	return dstune_kthread_init(dsp);
}

static int __init dynamic_stune_init(void)
{
	enum dstune_struct i;
	int ret = 0;

	for (i = 0; i < DT_MAX; i++) {
		ret = dstune_struct_init(i);
		if (ret)
			break;
	}

	return ret;
}
late_initcall(dynamic_stune_init);