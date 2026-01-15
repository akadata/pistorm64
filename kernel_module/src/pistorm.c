// SPDX-License-Identifier: GPL-2.0
//
// PiStorm kernel backend: owns GPIO/GPCLK and exposes /dev/pistorm

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include <linux/pistorm.h>

#ifndef PISTORM64_GIT
#define PISTORM64_GIT "unknown"
#endif

#define DEVICE_NAME "pistorm"

#ifndef PISTORM64_GIT
	#define PISTORM64_GIT "unknown"
#endif

/* GPIO register offsets */
#define GPIO_GPFSEL0 0x00
#define GPIO_GPFSEL1 0x04
#define GPIO_GPFSEL2 0x08
#define GPIO_GPSET0  0x1c
#define GPIO_GPCLR0  0x28
#define GPIO_GPLEV0  0x34
#define GPIO_GPLEV1  0x38

/* cprman offsets */
#define CPRMAN_GP0CTL 0x70
#define CPRMAN_GP0DIV 0x74
#define CPRMAN_PASSWD 0x5a000000

#define GPIO_FSEL_INPUT  0
#define GPIO_FSEL_OUTPUT 1
#define GPIO_FSEL_ALT0   4

/* PiStorm pin map (matches userspace ps_protocol.c) */
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

#define PISTORM_MAX_BATCH_OPS 1024

struct pistorm_dev {
	void __iomem *gpio_base;
	void __iomem *cprman_base;
	struct miscdevice miscdev;
	struct mutex lock;
	u32 fsel_input[3];
	u32 fsel_output[3];
	bool data_out;
	bool gpclk_ready;
};

static struct pistorm_dev *ps_dev;

static inline u32 ps_readl(u32 off)
{
	return readl(ps_dev->gpio_base + off);
}

static inline void ps_writel(u32 off, u32 val)
{
	writel(val, ps_dev->gpio_base + off);
}

static inline void ps_write_set(u32 mask)
{
	ps_writel(GPIO_GPSET0, mask);
}

static inline void ps_write_clr(u32 mask)
{
	ps_writel(GPIO_GPCLR0, mask);
}

static u32 ps_set_fsel(u32 fsel, unsigned int pin, unsigned int func)
{
	unsigned int shift = (pin % 10) * 3;
	u32 mask = GENMASK(shift + 2, shift);

	fsel &= ~mask;
	fsel |= func << shift;
	return fsel;
}

static void ps_prepare_fsel(struct pistorm_dev *ps)
{
	u32 fsel0 = ps_readl(GPIO_GPFSEL0);
	u32 fsel1 = ps_readl(GPIO_GPFSEL1);
	u32 fsel2 = ps_readl(GPIO_GPFSEL2);

	for (int pin = 0; pin <= 9; pin++)
		fsel0 = ps_set_fsel(fsel0, pin, GPIO_FSEL_INPUT);
	for (int pin = 10; pin <= 19; pin++)
		fsel1 = ps_set_fsel(fsel1, pin, GPIO_FSEL_INPUT);
	for (int pin = 20; pin <= 23; pin++)
		fsel2 = ps_set_fsel(fsel2, pin, GPIO_FSEL_INPUT);

	/* Fixed-function pins */
	fsel0 = ps_set_fsel(fsel0, PIN_TXN_IN_PROGRESS, GPIO_FSEL_INPUT);
	fsel0 = ps_set_fsel(fsel0, PIN_IPL_ZERO, GPIO_FSEL_INPUT);
	fsel0 = ps_set_fsel(fsel0, PIN_A0, GPIO_FSEL_OUTPUT);
	fsel0 = ps_set_fsel(fsel0, PIN_A1, GPIO_FSEL_OUTPUT);
	fsel0 = ps_set_fsel(fsel0, PIN_CLK, GPIO_FSEL_ALT0);
	fsel0 = ps_set_fsel(fsel0, PIN_RESET, GPIO_FSEL_OUTPUT);
	fsel0 = ps_set_fsel(fsel0, PIN_RD, GPIO_FSEL_OUTPUT);
	fsel0 = ps_set_fsel(fsel0, PIN_WR, GPIO_FSEL_OUTPUT);

	ps->fsel_input[0] = fsel0;
	ps->fsel_input[1] = fsel1;
	ps->fsel_input[2] = fsel2;

	ps->fsel_output[0] = ps_set_fsel(fsel0, PIN_D(0), GPIO_FSEL_OUTPUT);
	ps->fsel_output[0] = ps_set_fsel(ps->fsel_output[0], PIN_D(1), GPIO_FSEL_OUTPUT);

	ps->fsel_output[1] = fsel1;
	for (int pin = 10; pin <= 19; pin++)
		ps->fsel_output[1] = ps_set_fsel(ps->fsel_output[1], pin, GPIO_FSEL_OUTPUT);

	ps->fsel_output[2] = fsel2;
	for (int pin = 20; pin <= 23; pin++)
		ps->fsel_output[2] = ps_set_fsel(ps->fsel_output[2], pin, GPIO_FSEL_OUTPUT);
}

static void ps_set_bus_dir(struct pistorm_dev *ps, bool data_out)
{
	if (ps->data_out == data_out)
		return;

	ps->data_out = data_out;
	ps_writel(GPIO_GPFSEL0, data_out ? ps->fsel_output[0] : ps->fsel_input[0]);
	ps_writel(GPIO_GPFSEL1, data_out ? ps->fsel_output[1] : ps->fsel_input[1]);
	ps_writel(GPIO_GPFSEL2, data_out ? ps->fsel_output[2] : ps->fsel_input[2]);
}

static void ps_clear_lines(void)
{
	u32 mask = GENMASK(23, 8) | BIT(PIN_A0) | BIT(PIN_A1) | BIT(PIN_RESET) |
		   BIT(PIN_RD) | BIT(PIN_WR);
	ps_write_clr(mask);
}

static int ps_wait_for_txn(void)
{
	int wait = 100000;

	while (wait--) {
		if (!(ps_readl(GPIO_GPLEV0) & BIT(PIN_TXN_IN_PROGRESS)))
			return 0;
		cpu_relax();
	}
	return -ETIMEDOUT;
}

static void ps_write_payload(u32 payload, u32 reg_sel)
{
	u32 pins = (payload & GENMASK(23, 8)) | (reg_sel << PIN_A0);

	ps_write_set(pins);
	ps_write_set(BIT(PIN_WR));
	ps_write_clr(BIT(PIN_WR));
	ps_clear_lines();
}

static int ps_setup_gpclk(struct pistorm_dev *ps)
{
	if (!ps->cprman_base)
		return -ENODEV;

	/* Disable */
	writel(CPRMAN_PASSWD | BIT(5), ps->cprman_base + CPRMAN_GP0CTL);
	udelay(10);
	for (unsigned int i = 0; i < 1000; i++) {
		if (!(readl(ps->cprman_base + CPRMAN_GP0CTL) & BIT(7)))
			break;
		udelay(1);
	}

	/* Divider: 6 -> ~200MHz on Pi3/Zero2 */
	writel(CPRMAN_PASSWD | (6 << 12), ps->cprman_base + CPRMAN_GP0DIV);
	udelay(10);
	/* Source PLLC (5) */
	writel(CPRMAN_PASSWD | 5 | BIT(4), ps->cprman_base + CPRMAN_GP0CTL);

	for (unsigned int i = 0; i < 1000; i++) {
		if (readl(ps->cprman_base + CPRMAN_GP0CTL) & BIT(7)) {
			ps->gpclk_ready = true;
			return 0;
		}
		udelay(1);
	}
	return -ETIMEDOUT;
}

static int ps_setup_protocol(struct pistorm_dev *ps)
{
	int ret;

	ps_prepare_fsel(ps);
	ps->data_out = true;
	ps_set_bus_dir(ps, false);
	ps_clear_lines();

	ret = ps_setup_gpclk(ps);
	if (ret)
		pr_warn("pistorm: gpclk not configured (%d)\n", ret);

	return 0;
}

static int ps_write16(struct pistorm_dev *ps, u32 addr, u16 data)
{
	ps_set_bus_dir(ps, true);
	ps_write_payload((data & 0xffff) << 8, REG_DATA);
	ps_write_payload((addr & 0xffff) << 8, REG_ADDR_LO);
	ps_write_payload(((0x0000 | (addr >> 16)) << 8), REG_ADDR_HI);
	ps_set_bus_dir(ps, false);
	return ps_wait_for_txn();
}

static int ps_write8(struct pistorm_dev *ps, u32 addr, u8 data)
{
	u16 payload = (addr & 1) ? data : (data | (data << 8));

	ps_set_bus_dir(ps, true);
	ps_write_payload((payload & 0xffff) << 8, REG_DATA);
	ps_write_payload((addr & 0xffff) << 8, REG_ADDR_LO);
	ps_write_payload(((0x0100 | (addr >> 16)) << 8), REG_ADDR_HI);
	ps_set_bus_dir(ps, false);
	return ps_wait_for_txn();
}

static int ps_read16(struct pistorm_dev *ps, u32 addr, u16 *out)
{
	int ret;
	u32 value;

	ps_set_bus_dir(ps, true);
	ps_write_payload((addr & 0xffff) << 8, REG_ADDR_LO);
	ps_write_payload(((0x0200 | (addr >> 16)) << 8), REG_ADDR_HI);

	ps_set_bus_dir(ps, false);
	ps_write_set(REG_DATA << PIN_A0);
	ps_write_set(BIT(PIN_RD));

	ret = ps_wait_for_txn();
	value = ps_readl(GPIO_GPLEV0);
	ps_clear_lines();

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
	*out = (addr & 1) ? (value & 0xff) : ((value >> 8) & 0xff);
	return 0;
}

static int ps_write_status(struct pistorm_dev *ps, u16 value)
{
	ps_set_bus_dir(ps, true);
	ps_write_payload((value & 0xffff) << 8, REG_STATUS);
	ps_set_bus_dir(ps, false);
	return 0;
}

static int ps_read_status(struct pistorm_dev *ps, u16 *out)
{
	u32 value;

	ps_set_bus_dir(ps, false);
	ps_write_set(REG_STATUS << PIN_A0);
	ps_write_set(BIT(PIN_RD));
	ps_write_set(BIT(PIN_RD));
	ps_write_set(BIT(PIN_RD));
	ps_write_set(BIT(PIN_RD));

	value = ps_readl(GPIO_GPLEV0);
	ps_clear_lines();
	*out = (value >> 8) & 0xffff;
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
		/* write high then low */
		if (ps_write16(ps, op->addr, (u16)(op->value >> 16)))
			return -EIO;
		return ps_write16(ps, op->addr + 2, (u16)op->value);
	default:
		return -EINVAL;
	}
}

static int ps_handle_batch(struct pistorm_batch *batch)
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
		ret = ps_handle_busop(ps_dev, &ops[i]);
		if (ret)
			break;
	}

	if (!ret && copy_to_user(u64_to_user_ptr(batch->ops_ptr), ops,
				 batch->ops_count * sizeof(*ops)))
		ret = -EFAULT;

	kfree(ops);
	return ret;
}

static long ps_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	struct pistorm_busop busop;
	struct pistorm_batch batch;
	struct pistorm_pins pins;
	int ret = 0;

	if (_IOC_TYPE(cmd) != PISTORM_IOC_MAGIC)
		return -ENOTTY;

	mutex_lock(&ps_dev->lock);

	switch (cmd) {
	case PISTORM_IOC_SETUP:
		ret = ps_setup_protocol(ps_dev);
		break;
	case PISTORM_IOC_RESET_SM:
		ret = ps_reset_sm(ps_dev);
		break;
	case PISTORM_IOC_PULSE_RESET:
		ret = ps_pulse_reset(ps_dev);
		break;
	case PISTORM_IOC_GET_PINS:
		pins.gplev0 = ps_readl(GPIO_GPLEV0);
		pins.gplev1 = ps_readl(GPIO_GPLEV1);
		if (copy_to_user(argp, &pins, sizeof(pins)))
			ret = -EFAULT;
		break;
	case PISTORM_IOC_BUSOP:
		if (copy_from_user(&busop, argp, sizeof(busop))) {
			ret = -EFAULT;
			break;
		}
		ret = ps_handle_busop(ps_dev, &busop);
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
		ret = ps_handle_batch(&batch);
		break;
	default:
		ret = -ENOTTY;
	}

	mutex_unlock(&ps_dev->lock);
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
	.llseek = noop_llseek,
};

static void __iomem *ps_map_resource(const char *compat, const char *name)
{
	struct device_node *np;
	void __iomem *base;

	np = of_find_compatible_node(NULL, NULL, compat);
	if (!np)
		return NULL;

	base = of_iomap(np, 0);
	of_node_put(np);
	if (!base)
		pr_err("pistorm: failed to map %s\n", name);
	return base;
}

static void ps_request_pins(void)
{
	unsigned int pins[] = {
		0, 1, 2, 3, 4, 5, 6, 7,
		8, 9, 10, 11, 12, 13, 14, 15,
		16, 17, 18, 19, 20, 21, 22, 23,
	};

	for (size_t i = 0; i < ARRAY_SIZE(pins); i++) {
		int ret = gpio_request(pins[i], "pistorm");

		if (ret && ret != -EBUSY)
			pr_warn("pistorm: gpio %u request failed: %d\n", pins[i], ret);
	}
}

static void ps_disable_gpclk(struct pistorm_dev *ps)
{
	if (!ps->cprman_base)
		return;
	writel(CPRMAN_PASSWD | BIT(5), ps->cprman_base + CPRMAN_GP0CTL);
	udelay(10);
	ps->gpclk_ready = false;
}

static int __init pistorm_init(void)
{
	int ret;

	ps_dev = kzalloc(sizeof(*ps_dev), GFP_KERNEL);
	if (!ps_dev)
		return -ENOMEM;

	ps_dev->gpio_base = ps_map_resource("brcm,bcm2835-gpio", "gpio");
	if (!ps_dev->gpio_base) {
		ret = -ENODEV;
		goto err_free;
	}

	ps_dev->cprman_base = ps_map_resource("brcm,bcm2835-cprman", "cprman");
	mutex_init(&ps_dev->lock);
	ps_request_pins();

	ps_dev->miscdev.minor = MISC_DYNAMIC_MINOR;
	ps_dev->miscdev.name = DEVICE_NAME;
	ps_dev->miscdev.fops = &ps_fops;

	ret = misc_register(&ps_dev->miscdev);
	if (ret) {
		pr_err("pistorm: misc_register failed %d\n", ret);
		goto err_free;
	}

	ret = ps_setup_protocol(ps_dev);
	if (ret)
		pr_warn("pistorm: setup_protocol failed: %d\n", ret);

	pr_info("pistorm: /dev/%s ready (gpclk %s)\n",
		DEVICE_NAME, ps_dev->gpclk_ready ? "on" : "off");
	return 0;

err_free:
	kfree(ps_dev);
	return ret;
}

static void __exit pistorm_exit(void)
{
	if (ps_dev) {
		ps_disable_gpclk(ps_dev);
		misc_deregister(&ps_dev->miscdev);
		if (ps_dev->gpio_base)
			iounmap(ps_dev->gpio_base);
		if (ps_dev->cprman_base)
			iounmap(ps_dev->cprman_base);
		kfree(ps_dev);
	}
}

module_init(pistorm_init);
module_exit(pistorm_exit);

MODULE_AUTHOR("AKADATA LIMITED (PiStorm64)");
MODULE_DESCRIPTION("PiStorm64 kernel backend: GPIO + GPCLK bus engine for PiStorm CPLD");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1.0");

MODULE_INFO(firmware, "N/A");
MODULE_INFO(name, "pistorm64");
MODULE_INFO(alias, "pistorm");
MODULE_INFO(url, "https://github.com/akadata/pistorm");
MODULE_INFO(supported, "Pi Zero 2 W (BCM2837), Pi 4-class");
MODULE_INFO(intree, "N");   /* out-of-tree */
MODULE_INFO(pistorm64, "GPIO/GPCLK backend only; userspace CPU stays userspace");
MODULE_INFO(git, PISTORM64_GIT);
MODULE_INFO(clock, "GPCLK0 alt0 on GPIO4, PLLC source, divider=6 (~200MHz target)");
MODULE_INFO(gpio, "Pins: 0 TXN_IN_PROGRESS(in), 1 IPL_ZERO(in), 2 A0(out), 3 A1(out), 4 GPCLK0(alt0), 5 RESET(out), 6 RD(out), 7 WR(out), 8-23 D0..D15(bidir)");
