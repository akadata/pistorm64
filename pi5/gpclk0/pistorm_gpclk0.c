// SPDX-License-Identifier: GPL-2.0

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/property.h>

struct pistorm_gpclk0 {
	struct clk *clk;
	struct clk *parent;
};

static int pistorm_gpclk0_probe(struct platform_device *pdev)
{
	struct pistorm_gpclk0 *ctx;
	struct pinctrl *pctl;
	struct clk *clk;
	struct clk *parent;
	u32 rate = 0;
	int ret;

	ctx = devm_kzalloc(&pdev->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	pctl = devm_pinctrl_get_select_default(&pdev->dev);
	if (IS_ERR(pctl))
		dev_warn(&pdev->dev, "pinctrl default selection failed: %ld\n", PTR_ERR(pctl));

	clk = devm_clk_get(&pdev->dev, "gpclk");
	if (IS_ERR(clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(clk), "failed to get gpclk\n");

	parent = devm_clk_get_optional(&pdev->dev, "parent");
	if (IS_ERR(parent))
		return dev_err_probe(&pdev->dev, PTR_ERR(parent), "failed to get parent clock\n");

	if (device_property_read_u32(&pdev->dev, "clock-frequency", &rate) == 0 && rate) {
		if (parent) {
			ret = clk_set_parent(clk, parent);
			if (ret)
				dev_warn(&pdev->dev, "clk_set_parent failed: %d\n", ret);
		}

		if (clk_get_rate(clk) != rate) {
			ret = clk_set_rate(clk, rate);
			if (ret)
				dev_warn(&pdev->dev, "clk_set_rate(%u) failed: %d\n", rate, ret);
		}
	}

	ret = clk_prepare_enable(clk);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "clk_prepare_enable failed\n");

	ctx->clk = clk;
	ctx->parent = parent;
	platform_set_drvdata(pdev, ctx);

	dev_info(&pdev->dev, "enabled gpclk (rate=%lu)\n", clk_get_rate(clk));
	return 0;
}

static void pistorm_gpclk0_remove(struct platform_device *pdev)
{
	struct pistorm_gpclk0 *ctx = platform_get_drvdata(pdev);

	if (ctx && ctx->clk)
		clk_disable_unprepare(ctx->clk);
}

static const struct of_device_id pistorm_gpclk0_of_match[] = {
	{ .compatible = "pistorm,gpclk0" },
	{ }
};
MODULE_DEVICE_TABLE(of, pistorm_gpclk0_of_match);

static struct platform_driver pistorm_gpclk0_driver = {
	.probe = pistorm_gpclk0_probe,
	.remove_new = pistorm_gpclk0_remove,
	.driver = {
		.name = "pistorm-gpclk0",
		.of_match_table = pistorm_gpclk0_of_match,
	},
};
module_platform_driver(pistorm_gpclk0_driver);

MODULE_AUTHOR("OpenAI Codex CLI");
MODULE_DESCRIPTION("Enable Raspberry Pi 5 RP1 GP0 clock for PiStorm GPCLK0 output on GPIO4");
MODULE_LICENSE("GPL");
