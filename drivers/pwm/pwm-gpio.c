// SPDX-License-Identifier: GPL-2.0-only
/*
 * Generic software PWM for modulating GPIOs
 *
 * Copyright 2020 Nicola Di Lieto
 *
 * Author: Nicola Di Lieto <nicola.dilieto@gmail.com>
 */

#include <linux/atomic.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/hrtimer.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

struct pwm_gpio {
	struct pwm_chip chip;
	struct gpio_desc *desc;
	struct work_struct work;
	struct hrtimer timer;
	atomic_t enabled;
	spinlock_t lock;
	struct {
		u64 ton_ns;
		u64 toff_ns;
		bool invert;
	} cur, new;
	bool state;
	bool output;
};

static void pwm_gpio_work(struct work_struct *work)
{
	struct pwm_gpio *pwm_gpio = container_of(work, struct pwm_gpio, work);

	gpiod_set_value_cansleep(pwm_gpio->desc, pwm_gpio->output);
}

enum hrtimer_restart pwm_gpio_do_timer(struct hrtimer *handle)
{
	struct pwm_gpio *pwm_gpio = container_of(handle, struct pwm_gpio, timer);
	u64 ns;

	if (!atomic_read(&pwm_gpio->enabled))
		return HRTIMER_NORESTART;

	if (pwm_gpio->state) {
		ns = pwm_gpio->cur.toff_ns;
		pwm_gpio->state = false;
	} else {
		if (spin_trylock(&pwm_gpio->lock)) {
			pwm_gpio->cur = pwm_gpio->new;
			spin_unlock(&pwm_gpio->lock);
		}
		ns = pwm_gpio->cur.ton_ns;
		pwm_gpio->state = true;
	}
	pwm_gpio->output = pwm_gpio->state ^ pwm_gpio->cur.invert;

	schedule_work(&pwm_gpio->work);
	hrtimer_forward(handle, hrtimer_get_expires(handle), ns_to_ktime(ns));

	return HRTIMER_RESTART;
}

static inline struct pwm_gpio *pwm_gpio_from_chip(struct pwm_chip *_chip)
{
	return container_of(_chip, struct pwm_gpio, chip);
}

static void pwm_gpio_free(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct pwm_gpio *pwm_gpio = pwm_gpio_from_chip(chip);

	cancel_work_sync(&pwm_gpio->work);
	gpiod_set_value_cansleep(pwm_gpio->desc, pwm_gpio->cur.invert);
}

static int pwm_gpio_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			  const struct pwm_state *state)
{
	struct pwm_gpio *pwm_gpio = pwm_gpio_from_chip(chip);

	spin_lock(&pwm_gpio->lock);
	pwm_gpio->new.ton_ns = state->duty_cycle;
	pwm_gpio->new.toff_ns = state->period - state->duty_cycle;
	pwm_gpio->new.invert = (state->polarity == PWM_POLARITY_INVERSED);
	spin_unlock(&pwm_gpio->lock);

	if (state->enabled && !atomic_cmpxchg(&pwm_gpio->enabled, 0, 1)) {
		hrtimer_start(&pwm_gpio->timer, 0, HRTIMER_MODE_REL);
	} else if (!state->enabled && atomic_cmpxchg(&pwm_gpio->enabled, 1, 0)) {
		pwm_gpio->state = false;
		pwm_gpio->output = (state->polarity == PWM_POLARITY_INVERSED);
		schedule_work(&pwm_gpio->work);
	}
	return 0;
}

static void pwm_gpio_get_state(struct pwm_chip *chip, struct pwm_device *pwm,
			       struct pwm_state *state)
{
	struct pwm_gpio *pwm_gpio = pwm_gpio_from_chip(chip);

	state->duty_cycle = pwm_gpio->new.ton_ns;
	state->period = pwm_gpio->new.ton_ns + pwm_gpio->new.toff_ns;
	state->polarity = pwm_gpio->new.invert ? PWM_POLARITY_INVERSED
					       : PWM_POLARITY_NORMAL;
	state->enabled = atomic_read(&pwm_gpio->enabled);
}

static const struct pwm_ops pwm_gpio_ops = {
	.free = pwm_gpio_free,
	.apply = pwm_gpio_apply,
	.get_state = pwm_gpio_get_state,
	.owner = THIS_MODULE,
};

static int pwm_gpio_probe(struct platform_device *pdev)
{
	struct pwm_gpio *pwm_gpio;

	pwm_gpio = devm_kzalloc(&pdev->dev, sizeof(*pwm_gpio), GFP_KERNEL);
	if (!pwm_gpio)
		return -ENOMEM;

	pwm_gpio->desc = devm_gpiod_get(&pdev->dev, NULL, GPIOD_OUT_LOW);
	if (IS_ERR(pwm_gpio->desc))
		return PTR_ERR(pwm_gpio->desc);

	INIT_WORK(&pwm_gpio->work, pwm_gpio_work);

	hrtimer_init(&pwm_gpio->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	pwm_gpio->timer.function = pwm_gpio_do_timer;
	pwm_gpio->chip.dev = &pdev->dev;
	pwm_gpio->chip.ops = &pwm_gpio_ops;
	pwm_gpio->chip.npwm = 1;
	pwm_gpio->chip.base = -1;

	platform_set_drvdata(pdev, pwm_gpio);

	spin_lock_init(&pwm_gpio->lock);

	return pwmchip_add(&pwm_gpio->chip);
}

static int pwm_gpio_remove(struct platform_device *pdev)
{
	struct pwm_gpio *pwm_gpio = platform_get_drvdata(pdev);

	pwmchip_remove(&pwm_gpio->chip);

	hrtimer_cancel(&pwm_gpio->timer);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id pwm_gpio_of_match[] = {
	{ .compatible = "pwm-gpio", },
	{ }
};
MODULE_DEVICE_TABLE(of, pwm_gpio_of_match);
#endif

static struct platform_driver pwm_gpio_driver = {
	.probe = pwm_gpio_probe,
	.remove = pwm_gpio_remove,
	.driver = {
		.name = "pwm-gpio",
		.of_match_table = of_match_ptr(pwm_gpio_of_match),
	},
};
module_platform_driver(pwm_gpio_driver);

MODULE_DESCRIPTION("PWM GPIO driver");
MODULE_ALIAS("platform:pwm-gpio");
MODULE_AUTHOR("Nicola Di Lieto");
MODULE_LICENSE("GPL");
