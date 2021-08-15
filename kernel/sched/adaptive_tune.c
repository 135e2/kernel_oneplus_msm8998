// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Edrick Vince Sinsuan <sedrickvince@gmail.com>.
 */
#include <linux/fb.h>
#include <linux/input.h>
#include <linux/kthread.h>
#include <linux/pm_qos.h>
#include <linux/slab.h>

#include <linux/adaptive_tune.h>

enum {
	CURR,
	NEW,
	SUSPEND,
	PENDING,
	N_STATE
};

struct adaptune atx = {
	.state = { ATOMIC_INIT(0) },
	.update = ATOMIC_INIT(0)
};

struct adaptune_priv {
	struct adaptune *at;
	struct task_struct *thread;
	struct pm_qos_request req;
	struct notifier_block fb_notif;
	struct timer_list timer[N_ATS];
	unsigned long duration[N_ATS];
	int state[N_STATE];
};

static bool adaptune_update_state(enum adaptune_states ats,
		struct adaptune_priv *atp, int wanted_state, int *new_state)
{
	struct adaptune *at = atp->at;
	bool ret;

	/* Force disable during suspend */
	if (atp->state[SUSPEND])
		*new_state = 0;
	/* Update timer if we want the new state to be true */
	else if (*new_state && !timer_pending(&atp->timer[ats]))
		mod_timer(&atp->timer[ats], jiffies + atp->duration[ats]);

	/*
	 * Only change the state value if the current atomic state
	 * is opposite to what is wanted.
	 */
	ret = (wanted_state != atomic_read(&at->state[ats]));
	if (*new_state == wanted_state && ret)
		atomic_set(&at->state[ats], *new_state);

	return ret;
}

static void adaptune_core(struct adaptune_priv *atp, int wanted_state)
{
	int new_state = atomic_cmpxchg(&atp->at->update, 1, 0);

	if (adaptune_update_state(CORE, atp, wanted_state, &new_state)) {
		WRITE_ONCE(atp->state[NEW], new_state);
		if (new_state != READ_ONCE(atp->state[CURR]))
			wake_up_process(atp->thread);
	}
}

static void adaptune_input(struct adaptune_priv *atp, int wanted_state)
{
	int curr_pending = READ_ONCE(atp->state[PENDING]), new_pending = (wanted_state && 
			timer_pending(&atp->timer[INPUT]));

	if (new_pending != curr_pending)
		WRITE_ONCE(atp->state[PENDING], new_pending);

	if (!new_pending) {
		int new_state = (wanted_state || curr_pending);
		adaptune_update_state(INPUT, atp, wanted_state, &new_state);
	}
}

static int adaptune_thread(void *data)
{
	static const struct sched_param sched_max_rt_prio = {
		.sched_priority = MAX_RT_PRIO - 1
	};
	struct adaptune_priv *atp = data;

	sched_setscheduler_nocheck(current, SCHED_FIFO, &sched_max_rt_prio);

	while (1) {
		int new_state;

		while (atp->state[CURR] == (new_state = 
				READ_ONCE(atp->state[NEW]))) {
			set_current_state(TASK_IDLE);
			schedule();
		}

		pm_qos_update_request(&atp->req, new_state ?
				100 : PM_QOS_DEFAULT_VALUE);

		pr_debug("adaptune: set stune = %d\n", new_state);
		adaptive_schedtune_set(new_state);
		WRITE_ONCE(atp->state[CURR], new_state);
	}

	return 0;
}

static void core_timeout(unsigned long data)
{
	adaptune_core((struct adaptune_priv *)data, 0);
}

static void input_timeout(unsigned long data)
{
	adaptune_input((struct adaptune_priv *)data, 0);
}

static void adaptune_wake(struct adaptune_priv *atp)
{
	adaptune_input(atp, 1);
	adaptune_core(atp, 1);
}

void adaptune_update(struct adaptune *at)
{
    struct adaptune_priv *atp = at->priv;

	if (likely(atp))
        adaptune_wake(atp);
}

static void adaptune_input_event(struct input_handle *handle,
		unsigned int type, unsigned int code, int value)
{
	adaptune_wake(handle->handler->private);
}

static int adaptune_input_connect(struct input_handler *handler,
		struct input_dev *dev, const struct input_device_id *id)
{
	struct input_handle *handle;
	int error;

	handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = "adaptive_tune";

	error = input_register_handle(handle);
	if (error)
		goto err2;

	error = input_open_device(handle);
	if (error)
		goto err1;

	return 0;
err1:
	input_unregister_handle(handle);
err2:
	kfree(handle);
	return error;
}

static void adaptune_input_disconnect(struct input_handle *handle)
{
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

static const struct input_device_id adaptune_ids[] = {
	/* Multi-touch touchscreen */
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT |
			INPUT_DEVICE_ID_MATCH_ABSBIT,
		.evbit = { BIT_MASK(EV_ABS) },
		.absbit = { [BIT_WORD(ABS_MT_POSITION_X)] =
			BIT_MASK(ABS_MT_POSITION_X) |
			BIT_MASK(ABS_MT_POSITION_Y) }
	},
	{ }
};

static struct input_handler adaptune_input_handler = {
	.event		   = adaptune_input_event,
	.connect	   = adaptune_input_connect,
	.disconnect	   = adaptune_input_disconnect,
	.name		   = "adaptune_h",
	.id_table	   = adaptune_ids,
};

static int fb_notifier_cb(struct notifier_block *nb, unsigned long action,
			  void *data)
{
	struct adaptune_priv *atp = container_of(nb, typeof(*atp), fb_notif);
	int *blank = ((struct fb_event *)data)->data, state;

	/* Notify the structures as soon as possible, do not allow if blank is NULL */
	if (action != FB_EARLY_EVENT_BLANK || !blank)
		return NOTIFY_OK;

	state = (*blank == FB_BLANK_UNBLANK);
	if (state == READ_ONCE(atp->state[SUSPEND])) {
		WRITE_ONCE(atp->state[SUSPEND], !state);

		if (!state) {
			enum adaptune_states i;

			for (i = 0; i < N_ATS; i++)
				mod_timer_pending(&atp->timer[i], jiffies);
		}
	}

	return NOTIFY_OK;
}

static int __init adaptive_tune_init(void)
{
	struct adaptune_priv *atp;
	int ret = 0;

	atp = kzalloc(sizeof(*atp), GFP_KERNEL);
	if (!atp)
		return -ENOMEM;

	atp->at = &atx;

	atp->duration[CORE] = msecs_to_jiffies(CONFIG_ADAPTUNE_CORE_DURATION);
	atp->duration[INPUT] = msecs_to_jiffies(CONFIG_ADAPTUNE_INPUT_TIME_FRAME);

	setup_timer(&atp->timer[CORE], core_timeout, (unsigned long)atp);
	setup_timer(&atp->timer[INPUT], input_timeout, (unsigned long)atp);

	atp->req.type = PM_QOS_REQ_AFFINE_CORES;
	atomic_set(&atp->req.cpus_affine, *cpumask_bits(cpu_perf_mask));
	pm_qos_add_request(&atp->req, PM_QOS_CPU_DMA_LATENCY, PM_QOS_DEFAULT_VALUE);

	adaptune_input_handler.private = atp;
	ret = input_register_handler(&adaptune_input_handler);
	if (ret) {
		pr_err("Failed to register input handler, err: %d\n", ret);
		goto unregister_qos;
	}

	atp->fb_notif.notifier_call = fb_notifier_cb;
	atp->fb_notif.priority = INT_MAX;
	ret = fb_register_client(&atp->fb_notif);
	if (ret) {
		pr_err("Failed to register fb notifier, err: %d\n", ret);
		goto unregister_input;
	}

	atp->thread = kthread_run(adaptune_thread, atp, "adaptune_d");
	if (IS_ERR(atp->thread)) {
		ret = PTR_ERR(atp->thread);
		pr_err("Failed to start stune thread, err: %d\n", ret);
		goto unregister_fb;
	}

	/* Register shared priv addr after successful init */
	atx.priv = atp;
	return 0;

unregister_fb:
	fb_unregister_client(&atp->fb_notif);

unregister_input:
	input_unregister_handler(&adaptune_input_handler);

unregister_qos:
	pm_qos_remove_request(&atp->req);

	kfree(atp);
	return ret;
}
late_initcall(adaptive_tune_init);