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
	struct delayed_work disable;
	unsigned long duration;
	void (*set_stune)(bool state);
};

/* Initialize input_margin */
unsigned long input_margin;

/*
 * Boost structure
 */
static void set_stune_boost(bool state)
{
	/*
	 * Enable boost and prefer_idle in order to bias migrating top-app 
	 * (also for foreground) tasks to idle big cluster cores.
	 */
	do_boost("top-app", state);
	do_prefer_idle("top-app", state);
	do_prefer_idle("foreground", state);
}

static void disable_boost(struct work_struct *work)
{
	dynstune_trigger(&boost, false);
}

struct dstune boost = {
	.waitq = __WAIT_QUEUE_HEAD_INITIALIZER(boost.waitq),
	.lock = ATOMIC_INIT(0)
};

static struct dstune_priv boost_priv = {
	.ds = &boost,
	.disable = __DELAYED_WORK_INITIALIZER(boost_priv.disable, disable_boost, 0),
	.set_stune = &set_stune_boost
};

/*
 * Crucial structure
 */
static void set_stune_crucial(bool state)
{
	/*
	 * Use idle cpus with the highest original capacity for top-app when it
	 * comes to app launches and transitions in order to speed up 
	 * the process and efficiently consume power.
	 */
	do_crucial("top-app", state);
}

static void disable_crucial(struct work_struct *work)
{
	dynstune_trigger(&crucial, false);
}

struct dstune crucial = {
	.waitq = __WAIT_QUEUE_HEAD_INITIALIZER(crucial.waitq),
	.lock = ATOMIC_INIT(0)
};

static struct dstune_priv crucial_priv = {
	.ds = &crucial,
	.disable = __DELAYED_WORK_INITIALIZER(crucial_priv.disable, disable_crucial, 0),
	.set_stune = &set_stune_crucial
};

static int dstune_thread(void *data)
{
	static const struct sched_param sched_max_rt_prio = {
		.sched_priority = MAX_RT_PRIO - 1
	};
	struct dstune_priv *ds_priv = data;
	bool old = false;

	sched_setscheduler_nocheck(current, SCHED_FIFO, &sched_max_rt_prio);

	while (1) {
		unsigned long duration;
		bool should_stop = false, curr;

		wait_event(ds_priv->ds->waitq, 
			atomic_read(&ds_priv->ds->lock) || 
			(should_stop = kthread_should_stop()));

		if (should_stop)
			break;

		curr = ds_priv->ds->state;

		if (curr != old)
			ds_priv->set_stune(curr);

		if (curr) {
			duration = ds_priv->duration;
			mod_delayed_work(system_unbound_wq, &ds_priv->disable, duration);
			schedule_timeout_uninterruptible(duration >> 1);
		}

		old = curr;
		atomic_set_release(&ds_priv->ds->lock, 0);
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
	input_margin =
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

	/* Initialize duration values */
	init_durations();
err:
	return ret;
}
late_initcall(dynamic_stune_init);