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
 * Boost structure
 */
static void set_boost(bool state)
{
	/*
	 * Enable boost and prefer_idle in order to bias migrating top-app
	 * (also for foreground) tasks to idle big cluster cores.
	 */
	do_boost("top-app", state);
	do_prefer_idle("top-app", state);
	do_prefer_idle("foreground", state);
}

struct dstune boost = {
	.stage_1 = __WAIT_QUEUE_HEAD_INITIALIZER(boost.stage_1),
	.stage_2 = __WAIT_QUEUE_HEAD_INITIALIZER(boost.stage_2),
	.trigger = ATOMIC_INIT(0),
	.update = ATOMIC_INIT(0)
};

static struct dstune_priv boost_priv = {
	.ds = &boost,
	.set = &set_boost
};

/*
 * Crucial structure
 */
static void set_crucial(bool state)
{
	/*
	 * Use idle cpus with the highest original capacity for top-app when it
	 * comes to app launches and transitions in order to speed up
	 * the process and efficiently consume power.
	 */
	do_crucial("top-app", state);
}

struct dstune crucial = {
	.stage_1 = __WAIT_QUEUE_HEAD_INITIALIZER(crucial.stage_1),
	.stage_2 = __WAIT_QUEUE_HEAD_INITIALIZER(crucial.stage_2),
	.trigger = ATOMIC_INIT(0),
	.update = ATOMIC_INIT(0)
};

static struct dstune_priv crucial_priv = {
	.ds = &crucial,
	.set = &set_crucial
};

/*
 * Input structure
 */
static void set_input(bool state) 
{
	/*
	 * Enable bias when an input is received to bias transfer of
	 * top-app and foreground tasks to big cluster.
	 */
	do_boost_bias("top-app", state);
	do_boost_bias("foreground", state);	
}

struct dstune input = {
	.stage_1 = __WAIT_QUEUE_HEAD_INITIALIZER(input.stage_1),
	.stage_2 = __WAIT_QUEUE_HEAD_INITIALIZER(input.stage_2),
	.trigger = ATOMIC_INIT(0),
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

		wait_event(ds_priv->ds->stage_1,
			atomic_read(&ds_priv->ds->trigger) ||
			(should_stop = kthread_should_stop()));

		if (should_stop)
			break;

		ds_priv->set(true);

		while (1) {
			unsigned long time;

			time = wait_event_timeout(ds_priv->ds->stage_2,
				atomic_read(&ds_priv->ds->update) ||
				(should_stop = kthread_should_stop()),
				ds_priv->duration);

			/* Continue to loop until !time */
			if (should_stop || !time)
				break;

			/* Delay release with the remaining time */
			while (time)
				time = schedule_timeout_uninterruptible(time);

			/* Allow update atomic to be acquired */
			atomic_set_release(&ds_priv->ds->update, 0);
		}

		ds_priv->set(false);

		/* Release all atomic */
		atomic_set_release(&ds_priv->ds->trigger, 0);
		atomic_set_release(&ds_priv->ds->update, 0);
	}

	return 0;
}

static int dstune_kthread_init(struct dstune_priv *ds_priv, const char namefmt[])
{
	struct task_struct *thread;
	int ret = 0;

	thread = kthread_run(dstune_thread, ds_priv, namefmt);
	if (IS_ERR(thread)) {
		ret = PTR_ERR(thread);
		pr_err("Failed to start stune thread, err: %d\n", ret);
	}

	return ret;
}

static void init_durations(void)
{
	boost_priv.duration =
		msecs_to_jiffies(CONFIG_STUNE_BOOST_DURATION);
	crucial_priv.duration =
		msecs_to_jiffies(CONFIG_STUNE_CRUCIAL_DURATION);
	input_priv.duration =
		msecs_to_jiffies(CONFIG_INPUT_INTERVAL_DURATION);
}

static int __init dynamic_stune_init(void)
{
	int ret = 0;

	ret = dstune_kthread_init(&boost_priv, "dstune_boostd");
	if (ret)
		goto err;

	ret = dstune_kthread_init(&crucial_priv, "dstune_cruciald");
	if (ret)
		goto err;

	ret = dstune_kthread_init(&input_priv, "dstune_inputd");
	if (ret)
		goto err;

	/* Initialize duration values */
	init_durations();
err:
	return ret;
}
late_initcall(dynamic_stune_init);