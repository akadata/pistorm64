// SPDX-License-Identifier: GPL-2.0
//
// PiStorm GPIO/GPCLK kernel backend
// Drives the CPLD-facing bus logic that was previously done via /dev/mem

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include <linux/pistorm.h>

#define DRIVER_NAME  "pistorm-kmod"
#define DEVICE_NAME  "pistorm0"

/* GPIO register offsets */
#define GPIO_GPFSEL0 0x00
#define GPIO_GPFSEL1 0x04
#define GPIO_GPFSEL2 0x08
#define GPIO_GPSET0  0x1c
#define GPIO_GPCLR0  0x28
#define GPIO_GPLEV0  0x34
#define GPIO_GPLEV1  0x38

/* Clock manager offsets (cprman) */
#define CPRMAN_GP0CTL 0x70
#define CPRMAN_GP0DIV 0x74
#define CPRMAN_PASSWD 0x5a000000

#define GPIO_FSEL_INPUT  0
#define GPIO_FSEL_OUTPUT 1
#define GPIO_FSEL_ALT0   4

#define PIN_TXN_IN_PROGRESS 0
#define PIN_IPL_ZERO 1
#define PIN_A0 2
#define PIN_A1 3
#define PIN_CLK 4
#define PIN_RESET 5
#define PIN_RD 6
#define PIN_WR 7
#define PIN_D(x) (8 + (x))

#define REG_DATA 0
#define REG_ADDR_LO 1
#define REG_ADDR_HI 2
#define REG_STATUS 3

#define STATUS_BIT_INIT 1
#define STATUS_BIT_RESET 2

/* Mask that clears all CPLD facing pins (except TXN/IPL/CLK) */
#define PISTORM_CLEAR_MASK 0x00ffffecu
#define PISTORM_MAX_TXN_WAIT 100000
#define PISTORM_MAX_BATCH_OPS 1024

struct pistorm_dev {
	struct device *dev;
	void __iomem *gpio_base;
	void __iomem *clk_base;
	struct clk *clk_gp0;
	struct miscdevice miscdev;
	struct mutex op_lock; /* serializes all bus access */
	u32 fsel_input[3];
	u32 fsel_output[3];
	bool data_out;      /* cached direction of data bus */
	bool gpclk_ready;
};

static struct platform_device *pistorm_pdev;

static inline u32 ps_readl(struct pistorm_dev *ps, u32 off)
{
	return readl(ps->gpio_base + off);
}

static inline void ps_writel(struct pistorm_dev *ps, u32 off, u32 val)
{
	writel(val, ps->gpio_base + off);
}

static inline void ps_write_set(struct pistorm_dev *ps, u32 mask)
{
	ps_writel(ps, GPIO_GPSET0, mask);
}

static inline void ps_write_clr(struct pistorm_dev *ps, u32 mask)
{
	ps_writel(ps, GPIO_GPCLR0, mask);
}

static void ps_clear_lines(struct pistorm_dev *ps)
{
	ps_write_clr(ps, PISTORM_CLEAR_MASK);
}

static int ps_wait_for_txn(struct pistorm_dev *ps)
{
	unsigned int wait = PISTORM_MAX_TXN_WAIT;

	while (wait--) {
		if (!(ps_readl(ps, GPIO_GPLEV0) & BIT(PIN_TXN_IN_PROGRESS)))
			return 0;
		cpu_relax();
	}

	return -ETIMEDOUT;
}

static u32 ps_set_fsel_value(u32 fsel, unsigned int pin, unsigned int func)
{
	unsigned int shift = (pin % 10) * 3;
	u32 mask = GENMASK(shift + 2, shift);
	fsel &= ~mask;
	fsel |= (func << shift);
	return fsel;
}

static void ps_prepare_fsel(struct pistorm_dev *ps)
{
	u32 fsel0 = ps_readl(ps, GPIO_GPFSEL0);
	u32 fsel1 = ps_readl(ps, GPIO_GPFSEL1);
	u32 fsel2 = ps_readl(ps, GPIO_GPFSEL2);

	/* Clear the pins we own */
	for (int pin = 0; pin <= 9; pin++)
		fsel0 = ps_set_fsel_value(fsel0, pin, GPIO_FSEL_INPUT);
	for (int pin = 10; pin <= 19; pin++)
		fsel1 = ps_set_fsel_value(fsel1, pin, GPIO_FSEL_INPUT);
	for (int pin = 20; pin <= 23; pin++)
		fsel2 = ps_set_fsel_value(fsel2, pin, GPIO_FSEL_INPUT);

	/* Pins that never change direction */
	fsel0 = ps_set_fsel_value(fsel0, PIN_TXN_IN_PROGRESS, GPIO_FSEL_INPUT);
	fsel0 = ps_set_fsel_value(fsel0, PIN_IPL_ZERO, GPIO_FSEL_INPUT);
	fsel0 = ps_set_fsel_value(fsel0, PIN_A0, GPIO_FSEL_OUTPUT);
	fsel0 = ps_set_fsel_value(fsel0, PIN_A1, GPIO_FSEL_OUTPUT);
	fsel0 = ps_set_fsel_value(fsel0, PIN_CLK, GPIO_FSEL_ALT0);
	fsel0 = ps_set_fsel_value(fsel0, PIN_RESET, GPIO_FSEL_OUTPUT);
	fsel0 = ps_set_fsel_value(fsel0, PIN_RD, GPIO_FSEL_OUTPUT);
	fsel0 = ps_set_fsel_value(fsel0, PIN_WR, GPIO_FSEL_OUTPUT);

	/* Input state */
	ps->fsel_input[0] = fsel0;
	ps->fsel_input[0] = ps_set_fsel_value(ps->fsel_input[0], PIN_D(0), GPIO_FSEL_INPUT);
	ps->fsel_input[0] = ps_set_fsel_value(ps->fsel_input[0], PIN_D(1), GPIO_FSEL_INPUT);

	ps->fsel_input[1] = fsel1;
	ps->fsel_input[2] = fsel2;

	/* Output state */
	ps->fsel_output[0] = fsel0;
	ps->fsel_output[0] = ps_set_fsel_value(ps->fsel_output[0], PIN_D(0), GPIO_FSEL_OUTPUT);
	ps->fsel_output[0] = ps_set_fsel_value(ps->fsel_output[0], PIN_D(1), GPIO_FSEL_OUTPUT);

	ps->fsel_output[1] = fsel1;
	for (int pin = 10; pin <= 19; pin++)
		ps->fsel_output[1] = ps_set_fsel_value(ps->fsel_output[1], pin, GPIO_FSEL_OUTPUT);

	ps->fsel_output[2] = fsel2;
	for (int pin = 20; pin <= 23; pin++)
		ps->fsel_output[2] = ps_set_fsel_value(ps->fsel_output[2], pin, GPIO_FSEL_OUTPUT);
}

static void ps_set_bus_dir(struct pistorm_dev *ps, bool data_out)
{
	if (ps->data_out == data_out)
		return;

	ps->data_out = data_out;
	ps_writel(ps, GPIO_GPFSEL0, data_out ? ps->fsel_output[0] : ps->fsel_input[0]);
	ps_writel(ps, GPIO_GPFSEL1, data_out ? ps->fsel_output[1] : ps->fsel_input[1]);
	ps_writel(ps, GPIO_GPFSEL2, data_out ? ps->fsel_output[2] : ps->fsel_input[2]);
}

static void ps_write_payload(struct pistorm_dev *ps, u32 payload, u32 reg_sel)
{
	u32 pins = (payload & GENMASK(23, 8)) | (reg_sel << PIN_A0);

	ps_write_set(ps, pins);
	ps_write_set(ps, BIT(PIN_WR));
	ps_write_clr(ps, BIT(PIN_WR));
	ps_clear_lines(ps);
}

static int ps_setup_gpclk(struct pistorm_dev *ps)
{
	int ret;

	if (ps->gpclk_ready)
		return 0;

	if (ps->clk_gp0) {
		ret = clk_set_rate(ps->clk_gp0, 200000000);
		if (ret)
			dev_warn(ps->dev, "clk_set_rate(gp0) failed: %d\n", ret);
		else
			ret = clk_prepare_enable(ps->clk_gp0);
		if (!ret) {
			ps->gpclk_ready = true;
			return 0;
		}
	}

	if (!ps->clk_base)
		return -ENODEV;

	/* Disable and wait for not busy */
	writel(CPRMAN_PASSWD | BIT(5), ps->clk_base + CPRMAN_GP0CTL);
	udelay(10);
	ret = 0;
	for (unsigned int i = 0; i < 1000; i++) {
		if (!(readl(ps->clk_base + CPRMAN_GP0CTL) & BIT(7)))
			break;
		udelay(1);
		if (i == 999)
			ret = -ETIMEDOUT;
	}
	if (ret)
		return ret;

	writel(CPRMAN_PASSWD | (6 << 12), ps->clk_base + CPRMAN_GP0DIV);
	udelay(10);
	writel(CPRMAN_PASSWD | 5 | BIT(4), ps->clk_base + CPRMAN_GP0CTL);

	for (unsigned int i = 0; i < 1000; i++) {
		if (readl(ps->clk_base + CPRMAN_GP0CTL) & BIT(7)) {
			ps->gpclk_ready = true;
			return 0;
		}
		udelay(1);
	}

	return -ETIMEDOUT;
}

static void ps_disable_gpclk(struct pistorm_dev *ps)
{
	if (ps->clk_gp0) {
		clk_disable_unprepare(ps->clk_gp0);
		ps->gpclk_ready = false;
		return;
	}

	if (ps->clk_base) {
		writel(CPRMAN_PASSWD | BIT(5), ps->clk_base + CPRMAN_GP0CTL);
		udelay(10);
	}
	ps->gpclk_ready = false;
}

static int ps_write16(struct pistorm_dev *ps, u32 addr, u16 data)
{
	ps_set_bus_dir(ps, true);

	ps_write_payload(ps, (data & 0xffff) << 8, REG_DATA);
	ps_write_payload(ps, (addr & 0xffff) << 8, REG_ADDR_LO);
	ps_write_payload(ps, ((0x0000 | (addr >> 16)) << 8), REG_ADDR_HI);

	ps_set_bus_dir(ps, false);
	return ps_wait_for_txn(ps);
}

static int ps_write8(struct pistorm_dev *ps, u32 addr, u8 data)
{
	u16 payload = (addr & 1) ? data : (data | (data << 8));

	ps_set_bus_dir(ps, true);

	ps_write_payload(ps, (payload & 0xffff) << 8, REG_DATA);
	ps_write_payload(ps, (addr & 0xffff) << 8, REG_ADDR_LO);
	ps_write_payload(ps, ((0x0100 | (addr >> 16)) << 8), REG_ADDR_HI);

	ps_set_bus_dir(ps, false);
	return ps_wait_for_txn(ps);
}

static int ps_read16(struct pistorm_dev *ps, u32 addr, u16 *out)
{
	int ret;
	u32 value;

	ps_set_bus_dir(ps, true);
	ps_write_payload(ps, (addr & 0xffff) << 8, REG_ADDR_LO);
	ps_write_payload(ps, ((0x0200 | (addr >> 16)) << 8), REG_ADDR_HI);

	ps_set_bus_dir(ps, false);
	ps_write_set(ps, (REG_DATA << PIN_A0));
	ps_write_set(ps, BIT(PIN_RD));

	ret = ps_wait_for_txn(ps);
	value = ps_readl(ps, GPIO_GPLEV0);
	ps_clear_lines(ps);

	if (ret)
		return ret;
	*out = (value >> 8) & 0xffff;
	return 0;
}

static int ps_read8(struct pistorm_dev *ps, u32 addr, u8 *out)
{
	int ret;
	u16 value = 0;

	ret = ps_read16(ps, addr, &value);
	if (ret)
		return ret;

	if (addr & 1)
		*out = value & 0xff;
	else
		*out = (value >> 8) & 0xff;
	return 0;
}

static int ps_read_status(struct pistorm_dev *ps, u16 *out)
{
	u32 value;

	ps_set_bus_dir(ps, false);

	ps_write_set(ps, (REG_STATUS << PIN_A0));
	ps_write_set(ps, BIT(PIN_RD));
	ps_write_set(ps, BIT(PIN_RD));
	ps_write_set(ps, BIT(PIN_RD));
	ps_write_set(ps, BIT(PIN_RD));

	value = ps_readl(ps, GPIO_GPLEV0);
	ps_clear_lines(ps);

	*out = (value >> 8) & 0xffff;
	return 0;
}

static int ps_write_status(struct pistorm_dev *ps, u16 value)
{
	ps_set_bus_dir(ps, true);
	ps_write_payload(ps, (value & 0xffff) << 8, REG_STATUS);
	ps_set_bus_dir(ps, false);
	return 0;
}

static int ps_reset_sm(struct pistorm_dev *ps)
{
	int ret;

	ret = ps_write_status(ps, STATUS_BIT_INIT);
	if (ret)
		return ret;
	usleep_range(1500, 2000);
	ret = ps_write_status(ps, 0);
	usleep_range(100, 200);
	return ret;
}

static int ps_pulse_reset(struct pistorm_dev *ps)
{
	int ret = ps_write_status(ps, 0);
	if (ret)
		return ret;
	msleep(100);
	return ps_write_status(ps, STATUS_BIT_RESET);
}

static int ps_setup_protocol(struct pistorm_dev *ps)
{
	int ret;

	ps_prepare_fsel(ps);
	ps->data_out = true; /* force initial program */
	ps_set_bus_dir(ps, false);
	ps_clear_lines(ps);

	ret = ps_setup_gpclk(ps);
	if (ret)
		return ret;

	return 0;
}

static int ps_handle_busop(struct pistorm_dev *ps, struct pistorm_busop *op)
{
	if (op->flags & PISTORM_BUSOP_F_STATUS) {
		if (op->is_read) {
			u16 status;
			int ret = ps_read_status(ps, &status);

			if (!ret)
				op->value = status;
			return ret;
		}
		return ps_write_status(ps, (u16)op->value);
	}

	switch (op->width) {
	case PISTORM_W8:
		if (op->is_read)
			return ps_read8(ps, op->addr, (u8 *)&op->value);
		return ps_write8(ps, op->addr, (u8)op->value);
	case PISTORM_W16:
		if (op->is_read)
			return ps_read16(ps, op->addr, (u16 *)&op->value);
		return ps_write16(ps, op->addr, (u16)op->value);
	case PISTORM_W32:
		if (op->is_read) {
			u16 hi, lo;
			int ret = ps_read16(ps, op->addr, &hi);
			if (ret)
				return ret;
			ret = ps_read16(ps, op->addr + 2, &lo);
			if (ret)
				return ret;
			op->value = ((u32)hi << 16) | lo;
			return 0;
		}
		ret = ps_write16(ps, op->addr, (u16)(op->value >> 16));
		if (ret)
			return ret;
		return ps_write16(ps, op->addr + 2, (u16)op->value);
	default:
		return -EINVAL;
	}
}

static int ps_handle_batch(struct pistorm_dev *ps, struct pistorm_batch *batch)
{
	struct pistorm_busop *ops;
	int ret = 0;

	if (!batch->ops_count || batch->ops_count > PISTORM_MAX_BATCH_OPS)
		return -EINVAL;

	ops = memdup_user(u64_to_user_ptr(batch->ops_ptr),
			  batch->ops_count * sizeof(*ops));
	if (IS_ERR(ops))
		return PTR_ERR(ops);

	for (u32 i = 0; i < batch->ops_count; i++) {
		ret = ps_handle_busop(ps, &ops[i]);
		if (ret)
			break;
	}

	if (!ret) {
		if (copy_to_user(u64_to_user_ptr(batch->ops_ptr), ops,
				 batch->ops_count * sizeof(*ops)))
			ret = -EFAULT;
	}

	kfree(ops);
	return ret;
}

static long ps_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct pistorm_dev *ps = container_of(file->private_data,
					      struct pistorm_dev, miscdev);
	void __user *argp = (void __user *)arg;
	struct pistorm_busop busop;
	struct pistorm_batch batch;
	struct pistorm_pins pins;
	int ret = 0;

	if (_IOC_TYPE(cmd) != PISTORM_IOC_MAGIC)
		return -ENOTTY;

	mutex_lock(&ps->op_lock);

	switch (cmd) {
	case PISTORM_IOC_SETUP:
		ret = ps_setup_protocol(ps);
		break;
	case PISTORM_IOC_RESET_SM:
		ret = ps_reset_sm(ps);
		break;
	case PISTORM_IOC_PULSE_RESET:
		ret = ps_pulse_reset(ps);
		break;
	case PISTORM_IOC_GET_PINS:
		pins.gplev0 = ps_readl(ps, GPIO_GPLEV0);
		pins.gplev1 = ps_readl(ps, GPIO_GPLEV1);
		if (copy_to_user(argp, &pins, sizeof(pins)))
			ret = -EFAULT;
		break;
	case PISTORM_IOC_BUSOP:
		if (copy_from_user(&busop, argp, sizeof(busop))) {
			ret = -EFAULT;
			break;
		}
		ret = ps_handle_busop(ps, &busop);
		if (!ret && busop.is_read) {
			if (copy_to_user(argp, &busop, sizeof(busop)))
				ret = -EFAULT;
		}
		break;
	case PISTORM_IOC_BATCH:
		if (copy_from_user(&batch, argp, sizeof(batch))) {
			ret = -EFAULT;
			break;
		}
		ret = ps_handle_batch(ps, &batch);
		break;
	default:
		ret = -ENOTTY;
	}

	mutex_unlock(&ps->op_lock);
	return ret;
}

static int ps_open(struct inode *inode, struct file *file)
{
	return 0;
}

static const struct file_operations ps_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = ps_ioctl,
	.open = ps_open,
	.llseek = no_llseek,
};

static void __iomem *ps_map_from_compat(struct device *dev, const char *compat,
					const char *name)
{
	struct device_node *np;
	struct resource res;
	void __iomem *base;

	np = of_find_compatible_node(NULL, NULL, compat);
	if (!np)
		return ERR_PTR(-ENODEV);

	if (of_address_to_resource(np, 0, &res)) {
		of_node_put(np);
		return ERR_PTR(-ENODEV);
	}
	of_node_put(np);

	base = devm_ioremap(dev, res.start, resource_size(&res));
	if (!base)
		return ERR_PTR(-ENOMEM);

	dev_info(dev, "mapped %s via %pa (size %lu)\n", name, &res.start,
		 (unsigned long)resource_size(&res));
	return base;
}

static void __iomem *ps_map_resource(struct platform_device *pdev, int index,
				     const char *compat, const char *name)
{
	struct device *dev = &pdev->dev;
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, index);
	void __iomem *base;

	if (res) {
		base = devm_platform_ioremap_resource(pdev, index);
		if (!IS_ERR(base))
			return base;
		if (PTR_ERR(base) != -EBUSY)
			return base;

		dev_warn(dev, "%s resource busy, using shared mapping\n", name);
		base = devm_ioremap(dev, res->start, resource_size(res));
		if (!base)
			return ERR_PTR(-ENOMEM);
		return base;
	}

	return ps_map_from_compat(dev, compat, name);
}

static void ps_try_get_clock(struct pistorm_dev *ps)
{
	ps->clk_gp0 = devm_clk_get_optional(ps->dev, "gpclk");
	if (IS_ERR(ps->clk_gp0))
		ps->clk_gp0 = NULL;
	if (ps->clk_gp0)
		return;

	ps->clk_gp0 = devm_clk_get_optional(ps->dev, "gp0");
	if (IS_ERR(ps->clk_gp0))
		ps->clk_gp0 = NULL;
}

static void ps_request_pins(struct pistorm_dev *ps)
{
	static const unsigned int pins[] = {
		0, 1, 2, 3, 4, 5, 6, 7,
		8, 9, 10, 11, 12, 13, 14, 15,
		16, 17, 18, 19, 20, 21, 22, 23,
	};

	for (size_t i = 0; i < ARRAY_SIZE(pins); i++) {
		int ret = devm_gpio_request(ps->dev, pins[i], "pistorm-bus");

		if (ret && ret != -EBUSY && ret != -ENOENT)
			dev_warn(ps->dev, "gpio %u request failed: %d\n",
				 pins[i], ret);
	}
}

static int ps_probe(struct platform_device *pdev)
{
	struct pistorm_dev *ps;
	int ret;

	ps = devm_kzalloc(&pdev->dev, sizeof(*ps), GFP_KERNEL);
	if (!ps)
		return -ENOMEM;

	ps->dev = &pdev->dev;
	mutex_init(&ps->op_lock);
	platform_set_drvdata(pdev, ps);

	ps->gpio_base = ps_map_resource(pdev, 0, "brcm,bcm2835-gpio", "gpio");
	if (IS_ERR(ps->gpio_base))
		return PTR_ERR(ps->gpio_base);

	ps->clk_base = ps_map_resource(pdev, 1, "brcm,bcm2835-cprman", "cprman");
	if (IS_ERR(ps->clk_base))
		ps->clk_base = NULL;

	ps_try_get_clock(ps);
	ps_request_pins(ps);

	ps->miscdev.minor = MISC_DYNAMIC_MINOR;
	ps->miscdev.name = DEVICE_NAME;
	ps->miscdev.fops = &ps_fops;
	ps->miscdev.parent = &pdev->dev;

	ret = misc_register(&ps->miscdev);
	if (ret) {
		dev_err(&pdev->dev, "misc_register failed: %d\n", ret);
		return ret;
	}

	ret = ps_setup_protocol(ps);
	if (ret)
		dev_warn(&pdev->dev, "setup_protocol failed: %d\n", ret);

	dev_info(&pdev->dev, "/dev/%s ready (gpclk %s)\n", DEVICE_NAME,
		 ps->gpclk_ready ? "on" : "not configured");
	return 0;
}

static int ps_remove(struct platform_device *pdev)
{
	struct pistorm_dev *ps = platform_get_drvdata(pdev);

	ps_disable_gpclk(ps);
	misc_deregister(&ps->miscdev);
	return 0;
}

static const struct of_device_id ps_of_match[] = {
	{ .compatible = "pistorm,pistorm-kmod" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, ps_of_match);

static struct platform_driver ps_driver = {
	.probe = ps_probe,
	.remove = ps_remove,
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = ps_of_match,
	},
};

static int ps_fill_resource(struct resource *res, const char *compat)
{
	struct device_node *np;
	int ret;

	np = of_find_compatible_node(NULL, NULL, compat);
	if (!np)
		return -ENODEV;
	ret = of_address_to_resource(np, 0, res);
	of_node_put(np);
	return ret;
}

static int __init ps_init(void)
{
	struct resource res[2];
	int res_count = 0;
	int ret;
	struct device_node *np;

	memset(res, 0, sizeof(res));

	if (!ps_fill_resource(&res[res_count], "brcm,bcm2835-gpio"))
		res_count++;
	if (!ps_fill_resource(&res[res_count], "brcm,bcm2835-cprman"))
		res_count++;

	ret = platform_driver_register(&ps_driver);
	if (ret)
		return ret;

	np = of_find_matching_node(NULL, ps_of_match);
	if (np) {
		of_node_put(np);
		return 0;
	}

	pistorm_pdev = platform_device_register_resndata(NULL, DRIVER_NAME,
							 PLATFORM_DEVID_NONE,
							 res_count ? res : NULL,
							 res_count, NULL, 0);
	if (IS_ERR(pistorm_pdev)) {
		ret = PTR_ERR(pistorm_pdev);
		platform_driver_unregister(&ps_driver);
		return ret;
	}

	return 0;
}

static void __exit ps_exit(void)
{
	if (pistorm_pdev)
		platform_device_unregister(pistorm_pdev);
	platform_driver_unregister(&ps_driver);
}

module_init(ps_init);
module_exit(ps_exit);

MODULE_AUTHOR("PiStorm maintainers");
MODULE_DESCRIPTION("PiStorm GPIO/GPCLK kernel backend");
MODULE_LICENSE("GPL");
