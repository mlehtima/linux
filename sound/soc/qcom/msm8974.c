// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018, Linaro Limited

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/pcm.h>
#include "common.h"

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
	}
}

static int msm8974_platform_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card;
	struct device *dev = &pdev->dev;
	int ret;

	card = kzalloc(sizeof(*card), GFP_KERNEL);
	if (!card)
		return -ENOMEM;

	card->dev = dev;
	dev_set_drvdata(dev, card);
	ret = qcom_snd_parse_of(card);
	if (ret) {
		dev_err(dev, "Error parsing OF data\n");
		goto err;
	}

	msm8974_add_be_ops(card);
	ret = snd_soc_register_card(card);
	if (ret)
		goto err_card_register;

	return 0;

err_card_register:
	kfree(card->dai_link);
err:
	kfree(card);
	return ret;
}

static int msm8974_platform_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = dev_get_drvdata(&pdev->dev);

	snd_soc_unregister_card(card);
	kfree(card->dai_link);
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
