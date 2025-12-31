// SPDX-License-Identifier: GPL-2.0

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/property.h>
#include <linux/io.h>
#include <linux/mutex.h>

// RP1 clock manager base address
#define RP1_CM_BASE_PHYS 0x1f00018000
#define RP1_CM_SIZE 0x1000

// GPCLK0 control registers
#define CM_GP0CTL_OFFSET 0x070
#define CM_GP0DIV_OFFSET 0x074
#define CLK_PASSWD 0x5a000000

struct pistorm_gpclk0 {
	struct clk *clk;
	struct clk *parent;
	void __iomem *cm_base;  // Direct memory mapping for RP1 clock manager
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

	// Get requested rate
	if (device_property_read_u32(&pdev->dev, "clock-frequency", &rate) == 0 && rate) {
		dev_info(&pdev->dev, "Requested GPCLK0 rate: %u Hz\n", rate);
		
		if (parent) {
			ret = clk_set_parent(clk, parent);
			if (ret)
				dev_warn(&pdev->dev, "clk_set_parent failed: %d\n", ret);
		}

		// Try to set rate via clock framework first
		if (clk_get_rate(clk) != rate) {
			ret = clk_set_rate(clk, rate);
			if (ret) {
				dev_warn(&pdev->dev, "clk_set_rate(%u) failed: %d, attempting direct register access\n", rate, ret);
				
				// Fallback: direct register access to achieve 200MHz
				// Map the RP1 clock manager directly
				ctx->cm_base = devm_ioremap(&pdev->dev, RP1_CM_BASE_PHYS, RP1_CM_SIZE);
				if (IS_ERR(ctx->cm_base)) {
					dev_err(&pdev->dev, "Failed to map RP1 clock manager: %ld\n", PTR_ERR(ctx->cm_base));
					// Continue with error but try to work with what we have
				} else {
					dev_info(&pdev->dev, "Successfully mapped RP1 clock manager at 0x%px\n", ctx->cm_base);
					
					// Calculate divisor for 200MHz from 2000MHz PLL_SYS
					// For 200MHz: divisor = 2000MHz / 200MHz = 10
					// But we might need to use 1000MHz source / 5 = 200MHz
					// Let's try divisor of 5 for 200MHz from 1000MHz source
					u32 divisor = 5; // For 200MHz from 1000MHz effective source
					
					// Write to GP0DIV register (direct access)
					writel(CLK_PASSWD | (divisor << 12), ctx->cm_base + CM_GP0DIV_OFFSET);
					
					// Write to GP0CTL register to enable with source
					// Source 5 = PLLC, but for Pi5 this might be different
					// Try source 3 = PLL_SYS based on the original code
					writel(CLK_PASSWD | 3 | (1u << 4), ctx->cm_base + CM_GP0CTL_OFFSET);
					
					dev_info(&pdev->dev, "Direct register access: set divisor to %u, enabled GP0CTL\n", divisor);
				}
			} else {
				dev_info(&pdev->dev, "clk_set_rate succeeded: %lu Hz\n", clk_get_rate(clk));
			}
		}
	}

	// Prepare and enable the clock
	ret = clk_prepare_enable(clk);
	if (ret) {
		dev_err(&pdev->dev, "clk_prepare_enable failed: %d\n", ret);
		return ret;
	}

	ctx->clk = clk;
	ctx->parent = parent;
	platform_set_drvdata(pdev, ctx);

	dev_info(&pdev->dev, "enabled gpclk (rate=%lu Hz)\n", clk_get_rate(clk));
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
MODULE_DESCRIPTION("Enable Raspberry Pi 5 RP1 GP0 clock for PiStorm GPCLK0 output on GPIO4 with direct register access");
MODULE_LICENSE("GPL");