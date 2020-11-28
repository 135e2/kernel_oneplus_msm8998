// SPDX-License-Identifier: GPL-2.0
/*
 * Dynamic Schedtune Driver
 * Copyright (C) 2020 Edrick Vince Sinsuan <sedrickvince@gmail.com>.
 */

#include <linux/sched.h>
#include <linux/dynamic_stune.h>

#include "tune.h"

struct dstune_priv {
	struct dstune *ds;
	unsigned long duration;
	void (*set)(bool state);
};

/*
 * Framebuffer structure
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

struct dstune fb = {
	.waitq = __WAIT_QUEUE_HEAD_INITIALIZER(fb.waitq),
	.update = ATOMIC_INIT(0)
};

static struct dstune_priv fb_priv = {
	.ds = &fb,
	.set = &set_fb
};

/*
 * Top-app cgroup structure
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

struct dstune topcg = {
	.waitq = __WAIT_QUEUE_HEAD_INITIALIZER(topcg.waitq),
	.update = ATOMIC_INIT(0)
};

static struct dstune_priv topcg_priv = {
	.ds = &topcg,
	.set = &set_topcg
};

/*
 * Input structure
 */
atomic_t input_lock = ATOMIC_INIT(0);

static void set_input(bool state)
{
	/* Set lock to be checked by fb structure */
	atomic_set(&input_lock, state);
}

struct dstune input = {
	.waitq = __WAIT_QUEUE_HEAD_INITIALIZER(input.waitq),
	.update = ATOMIC_INIT(0)
};

static struct dstune_priv input_priv = {
	.ds = &input,
	.set = &set_input
};

static int dstune_thread(void *data)
{
	static const struct sched_param sched_max_rt_prio = {
		.sched_priority = MAX_RT_PRIO - 1
	};
	struct dstune_priv *ds_priv = data;

	sched_setscheduler_nocheck(current, SCHED_FIFO, &sched_max_rt_prio);

	while (1) {
		bool should_stop = false;

		wait_event(ds_priv->ds->waitq,
			atomic_read(&ds_priv->ds->update) ||
			(should_stop = kthread_should_stop()));

		if (should_stop)
			break;

		ds_priv->set(true);

		while (1) {
			unsigned long time = ds_priv->duration;

			atomic_set_release(&ds_priv->ds->update, 0);

			time = wait_event_timeout(ds_priv->ds->waitq,
				atomic_read(&ds_priv->ds->update) ||
				(should_stop = kthread_should_stop()), time);

			if (should_stop || !time)
				break;

			while (time)
				time = schedule_timeout_uninterruptible(time);
		}

		ds_priv->set(false);

		atomic_set_release(&ds_priv->ds->update, 0);
	}

	return 0;
}

static int dstune_kthread_init(struct dstune_priv *ds_priv, const char namefmt[],
	bool perf_critical)
{
	struct task_struct *thread;
	int ret = 0;

	thread = !perf_critical ? kthread_run(dstune_thread, ds_priv, namefmt) :
		kthread_run_perf_critical(dstune_thread, ds_priv, namefmt);
	if (IS_ERR(thread)) {
		ret = PTR_ERR(thread);
		pr_err("Failed to start stune thread, err: %d\n", ret);
	}

	return ret;
}

static void init_durations(void)
{
	fb_priv.duration =
		msecs_to_jiffies(CONFIG_FB_STUNE_DURATION);
	topcg_priv.duration =
		msecs_to_jiffies(CONFIG_TOPCG_STUNE_DURATION);
	input_priv.duration =
		msecs_to_jiffies(CONFIG_INPUT_STUNE_DURATION);
}

static int __init dynamic_stune_init(void)
{
	int ret = 0;

	ret = dstune_kthread_init(&fb_priv, "dstune_fbd", true);
	if (ret)
		goto err;

	ret = dstune_kthread_init(&topcg_priv, "dstune_topcgd", false);
	if (ret)
		goto err;

	ret = dstune_kthread_init(&input_priv, "dstune_inputd", false);
	if (ret)
		goto err;

	init_durations();
err:
	return ret;
}
late_initcall(dynamic_stune_init);