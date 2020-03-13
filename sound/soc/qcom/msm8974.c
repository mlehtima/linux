// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018, Linaro Limited

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <sound/jack.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/pcm.h>
#include <uapi/linux/input-event-codes.h>
#include "common.h"

struct msm8974_snd_data {
	struct snd_soc_jack jack;
	bool jack_setup;
	struct snd_soc_card *card;
};

static int msm8974_dai_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_component *component;
	struct snd_soc_dai_link *dai_link = rtd->dai_link;
	struct snd_soc_card *card = rtd->card;
	struct msm8974_snd_data *pdata = snd_soc_card_get_drvdata(card);
	int i, rval;

	if (!pdata->jack_setup) {
		struct snd_jack *jack;

		rval = snd_soc_card_jack_new(card, "Headset Jack",
				SND_JACK_HEADSET |
				SND_JACK_HEADPHONE |
				SND_JACK_BTN_0 | SND_JACK_BTN_1 |
				SND_JACK_BTN_2 | SND_JACK_BTN_3,
				&pdata->jack, NULL, 0);

		if (rval < 0) {
			dev_err(card->dev, "Unable to add Headphone Jack\n");
			return rval;
		}

		jack = pdata->jack.jack;

		snd_jack_set_key(jack, SND_JACK_BTN_0, KEY_PLAYPAUSE);
		snd_jack_set_key(jack, SND_JACK_BTN_1, KEY_VOICECOMMAND);
		snd_jack_set_key(jack, SND_JACK_BTN_2, KEY_VOLUMEUP);
		snd_jack_set_key(jack, SND_JACK_BTN_3, KEY_VOLUMEDOWN);
		pdata->jack_setup = true;
	}

	for (i = 0 ; i < dai_link->num_codecs; i++) {
		struct snd_soc_dai *dai = rtd->codec_dais[i];

		component = dai->component;
		rval = snd_soc_component_set_jack(
				component, &pdata->jack, NULL);
		if (rval != 0 && rval != -ENOTSUPP) {
			dev_warn(card->dev, "Failed to set jack: %d\n", rval);
			return rval;
		}
	}

	return 0;
}

static int msm8974_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
				      struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_CHANNELS);

	rate->min = rate->max = 48000;
	channels->min = channels->max = 2;

	return 0;
}

static void msm8974_add_be_ops(struct snd_soc_card *card)
{
	struct snd_soc_dai_link *link;
	int i;

	for_each_card_prelinks(card, i, link) {
		if (link->no_pcm == 1)
			link->be_hw_params_fixup = msm8974_be_hw_params_fixup;
		link->init = msm8974_dai_init;
	}
}

static int msm8974_platform_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card;
	struct msm8974_snd_data *data;
	struct device *dev = &pdev->dev;
	int ret;

		printk(KERN_ERR "msm8974_platform_probe 0 dev %p\n", dev);
	card = kzalloc(sizeof(*card), GFP_KERNEL);
	if (!card)
		return -ENOMEM;
	printk(KERN_ERR "msm8974_platform_probe 1\n");

	/* Allocate the private data */
	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	printk(KERN_ERR "msm8974_platform_probe 1.5\n");

	card->dev = dev;
	dev_set_drvdata(dev, card);
	ret = qcom_snd_parse_of(card);
	if (ret) {
		dev_err(dev, "Error parsing OF data\n");
		goto err;
	}
		printk(KERN_ERR "msm8974_platform_probe 2\n");

	data->card = card;
	snd_soc_card_set_drvdata(card, data);

	msm8974_add_be_ops(card);
	card->name = "mycard";
	ret = snd_soc_register_card(card);
	if (ret) {
		printk(KERN_ERR "msm8974_platform_probe 3\n");
		goto err_card_register;
	}

	return 0;

err_card_register:
	kfree(card->dai_link);
err:
	kfree(data);
	kfree(card);
	return ret;
}

static int msm8974_platform_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = dev_get_drvdata(&pdev->dev);
	struct msm8974_snd_data *data = snd_soc_card_get_drvdata(card);

	snd_soc_unregister_card(card);
	kfree(card->dai_link);
	kfree(data);
	kfree(card);

	return 0;
}

static const struct of_device_id msm_snd_msm8974_dt_match[] = {
	{.compatible = "qcom,msm8974-sndcard"},
	{}
};

MODULE_DEVICE_TABLE(of, msm_snd_msm8974_dt_match);

static struct platform_driver msm_snd_msm8974_driver = {
	.probe  = msm8974_platform_probe,
	.remove = msm8974_platform_remove,
	.driver = {
		.name = "msm-snd-msm8974",
		.of_match_table = msm_snd_msm8974_dt_match,
	},
};
module_platform_driver(msm_snd_msm8974_driver);
MODULE_AUTHOR("Matti Lehtimäki <matti.lehtimaki@gmail.com");
MODULE_DESCRIPTION("MSM8974 ASoC Machine Driver");
MODULE_LICENSE("GPL v2");
