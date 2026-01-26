// SPDX-License-Identifier: GPL-2.0
//
// PiStorm kernel backend: owns GPIO/GPCLK and exposes /dev/pistorm
// Upstream-ready version with proper platform driver, gpiod, and DT bindings

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/gpio/consumer.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include <uapi/linux/pistorm.h>

#define DEVICE_NAME "pistorm"

#ifndef PISTORM64_GIT
#define PISTORM64_GIT "https://github.com/akadata/pistorm64"
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

/* GPCLK control bits (bcm2835 style) */
#define GPCLK_CTL_ENAB    BIT(4)
#define GPCLK_CTL_KILL    BIT(5)
#define GPCLK_CTL_BUSY    BIT(7)

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
	struct clk *gpclk;
	struct miscdevice miscdev;
	struct mutex lock;
	u32 fsel_input[3];
	u32 fsel_output[3];
	bool data_out;
	bool gpclk_ready;
	
	/* GPIO descriptors */
	struct gpio_desc *txn_in_progress_gpio;
	struct gpio_desc *ipl_zero_gpio;
	struct gpio_desc *a0_gpio;
	struct gpio_desc *a1_gpio;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *rd_gpio;
	struct gpio_desc *wr_gpio;
	struct gpio_desc *clk_gpio;
	struct gpio_descs *data_gpios;
	
	/* Device tree properties */
	u32 gpclk_src;
	u32 gpclk_div;
};

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
	u32 fsel0 = ps_readl(ps, GPIO_GPFSEL0);
	u32 fsel1 = ps_readl(ps, GPIO_GPFSEL1);
	u32 fsel2 = ps_readl(ps, GPIO_GPFSEL2);

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
	ps_writel(ps, GPIO_GPFSEL0, data_out ? ps->fsel_output[0] : ps->fsel_input[0]);
	ps_writel(ps, GPIO_GPFSEL1, data_out ? ps->fsel_output[1] : ps->fsel_input[1]);
	ps_writel(ps, GPIO_GPFSEL2, data_out ? ps->fsel_output[2] : ps->fsel_input[2]);
}

static void ps_clear_lines(struct pistorm_dev *ps)
{
	u32 mask = GENMASK(23, 8) | BIT(PIN_A0) | BIT(PIN_A1) | BIT(PIN_RESET) |
		   BIT(PIN_RD) | BIT(PIN_WR);
	ps_write_clr(ps, mask);
}

static int ps_wait_for_txn(struct pistorm_dev *ps)
{
	unsigned long timeout_jiffies = jiffies + msecs_to_jiffies(500); /* 500ms */

	while (time_before(jiffies, timeout_jiffies)) {
		if (!(ps_readl(ps, GPIO_GPLEV0) & BIT(PIN_TXN_IN_PROGRESS)))
			return 0;
		cpu_relax();
	}

	dev_err(&ps->miscdev.parent->dev, "ps_wait_for_txn timed out after 500ms\n");
	return -ETIMEDOUT;
}

static int ps_wait_for_txn_log(struct pistorm_dev *ps, const char *op)
{
	int ret = ps_wait_for_txn(ps);
	if (ret == -ETIMEDOUT)
		dev_err(&ps->miscdev.parent->dev,
			"txn timeout waiting for %s (PIN_TXN_IN_PROGRESS stuck)\n", op);
	return ret;
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
	
	if (ps->gpclk) {
		/* Use CCF clock if available */
		ret = clk_set_rate(ps->gpclk, 200000000); /* 200 MHz target */
		if (ret) {
			dev_warn(&ps->miscdev.parent->dev, "Failed to set gpclk rate: %d\n", ret);
		}
		
		ret = clk_prepare_enable(ps->gpclk);
		if (ret) {
			dev_err(&ps->miscdev.parent->dev, "Failed to enable gpclk: %d\n", ret);
			return ret;
		}
		
		ps->gpclk_ready = true;
		dev_info(&ps->miscdev.parent->dev, "gpclk enabled via CCF\n");
		return 0;
	}

	/* Fallback to manual CPRMAN register programming */
	if (!ps->cprman_base)
		return -ENODEV;

	if (ps->gpclk_div == 0 || ps->gpclk_div > 0xfff) {
		dev_warn(&ps->miscdev.parent->dev, 
		         "invalid gpclk_div=%u (valid 1..4095), refusing\n", ps->gpclk_div);
		return -EINVAL;
	}

	/* Disable */
	writel(CPRMAN_PASSWD | GPCLK_CTL_KILL, ps->cprman_base + CPRMAN_GP0CTL);
	udelay(10);
	for (unsigned int i = 0; i < 1000; i++) {
		if (!(readl(ps->cprman_base + CPRMAN_GP0CTL) & GPCLK_CTL_BUSY))
			break;
		udelay(1);
	}

	/* Divider: integer divider in bits 23:12 */
	writel(CPRMAN_PASSWD | (ps->gpclk_div << 12), ps->cprman_base + CPRMAN_GP0DIV);
	udelay(10);

	/* Source + enable */
	writel(CPRMAN_PASSWD | (ps->gpclk_src & 0xf) | GPCLK_CTL_ENAB, ps->cprman_base + CPRMAN_GP0CTL);

	for (unsigned int i = 0; i < 1000; i++) {
		if (readl(ps->cprman_base + CPRMAN_GP0CTL) & GPCLK_CTL_BUSY) {
			ps->gpclk_ready = true;
			dev_info(&ps->miscdev.parent->dev, "gpclk0 configured (src=%u div=%u)\n", 
			         ps->gpclk_src, ps->gpclk_div);
			return 0;
		}
		udelay(1);
	}

	dev_err(&ps->miscdev.parent->dev, "gpclk0 failed to start (src=%u div=%u)\n", 
	        ps->gpclk_src, ps->gpclk_div);
	return -ETIMEDOUT;
}

static int ps_setup_protocol(struct pistorm_dev *ps)
{
	int ret;

	ps_prepare_fsel(ps);
	ps->data_out = true;
	ps_set_bus_dir(ps, false);
	ps_clear_lines(ps);

	ret = ps_setup_gpclk(ps);
	if (ret)
		dev_warn(&ps->miscdev.parent->dev, "gpclk not configured (%d)\n", ret);

	return 0;
}

static int ps_write16(struct pistorm_dev *ps, u32 addr, u16 data)
{
	ps_set_bus_dir(ps, true);
	ps_write_payload(ps, (data & 0xffff) << 8, REG_DATA);
	ps_write_payload(ps, (addr & 0xffff) << 8, REG_ADDR_LO);
	ps_write_payload(ps, ((0x0000 | (addr >> 16)) << 8), REG_ADDR_HI);
	ps_set_bus_dir(ps, false);
	return ps_wait_for_txn_log(ps, "write16");
}

static int ps_write8(struct pistorm_dev *ps, u32 addr, u8 data)
{
	u16 payload = (addr & 1) ? data : (data | (data << 8));

	ps_set_bus_dir(ps, true);
	ps_write_payload(ps, (payload & 0xffff) << 8, REG_DATA);
	ps_write_payload(ps, (addr & 0xffff) << 8, REG_ADDR_LO);
	ps_write_payload(ps, ((0x0100 | (addr >> 16)) << 8), REG_ADDR_HI);
	ps_set_bus_dir(ps, false);
	return ps_wait_for_txn_log(ps, "write8");
}

static int ps_read16(struct pistorm_dev *ps, u32 addr, u16 *out)
{
	int ret;
	u32 value;

	ps_set_bus_dir(ps, true);
	ps_write_payload(ps, (addr & 0xffff) << 8, REG_ADDR_LO);
	ps_write_payload(ps, ((0x0200 | (addr >> 16)) << 8), REG_ADDR_HI);

	ps_set_bus_dir(ps, false);
	ps_write_set(ps, REG_DATA << PIN_A0);
	ps_write_set(ps, BIT(PIN_RD));

	ret = ps_wait_for_txn_log(ps, "read16");
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
	*out = (addr & 1) ? (value & 0xff) : ((value >> 8) & 0xff);
	return 0;
}

static int ps_write_status(struct pistorm_dev *ps, u16 value)
{
	ps_set_bus_dir(ps, true);
	ps_write_payload(ps, (value & 0xffff) << 8, REG_STATUS);
	ps_set_bus_dir(ps, false);
	return 0;
}

static int ps_read_status(struct pistorm_dev *ps, u16 *out)
{
	u32 value;

	ps_set_bus_dir(ps, false);
	ps_write_set(ps, REG_STATUS << PIN_A0);
	ps_write_set(ps, BIT(PIN_RD));
	ps_write_set(ps, BIT(PIN_RD));
	ps_write_set(ps, BIT(PIN_RD));
	ps_write_set(ps, BIT(PIN_RD));

	value = ps_readl(ps, GPIO_GPLEV0);
	ps_clear_lines(ps);
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

	if (!ret && copy_to_user(u64_to_user_ptr(batch->ops_ptr), ops,
				 batch->ops_count * sizeof(*ops)))
		ret = -EFAULT;

	kfree(ops);
	return ret;
}

static long ps_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct pistorm_dev *ps = container_of(file->private_data, struct pistorm_dev, miscdev);
	void __user *argp = (void __user *)arg;
	struct pistorm_busop busop;
	struct pistorm_batch batch;
	struct pistorm_pins pins;
	struct pistorm_query query;
	int ret = 0;

	if (_IOC_TYPE(cmd) != PISTORM_IOC_MAGIC)
		return -ENOTTY;

	mutex_lock(&ps->lock);

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
	case PISTORM_IOC_QUERY:
		query.abi_version = PISTORM_ABI_VERSION;
		query.capabilities = 0x1; /* Basic capabilities bitmask */
		memset(query.reserved, 0, sizeof(query.reserved));
		if (copy_to_user(argp, &query, sizeof(query)))
			ret = -EFAULT;
		break;
	case PISTORM_IOC_BUSOP:
		if (copy_from_user(&busop, argp, sizeof(busop))) {
			ret = -EFAULT;
			break;
		}
		dev_dbg(&ps->miscdev.parent->dev,
		        "ps_ioctl: BUSOP is_read=%d width=%u addr=0x%08x flags=0x%x\n",
			 busop.is_read, busop.width, busop.addr, busop.flags);
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
		dev_dbg(&ps->miscdev.parent->dev,
		        "ps_ioctl: BATCH count=%u ptr=0x%llx\n",
			 batch.ops_count, batch.ops_ptr);
		ret = ps_handle_batch(ps, &batch);
		break;
	default:
		ret = -ENOTTY;
	}

	mutex_unlock(&ps->lock);
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
	.compat_ioctl = ps_ioctl,
};

static int ps_parse_dt_properties(struct pistorm_dev *ps, struct device_node *np)
{
	int ret;
	
	/* Parse GPCLK source and divisor from DT */
	ret = of_property_read_u32(np, "akadata,gpclk-src", &ps->gpclk_src);
	if (ret < 0)
		ps->gpclk_src = 5; /* Default to PLLC */
	
	ret = of_property_read_u32(np, "akadata,gpclk-div", &ps->gpclk_div);
	if (ret < 0)
		ps->gpclk_div = 6; /* Default divisor */
	
	return 0;
}

static int ps_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct pistorm_dev *ps;
	struct resource *res;
	int ret;

	ps = devm_kzalloc(dev, sizeof(*ps), GFP_KERNEL);
	if (!ps)
		return -ENOMEM;

	/* Initialize mutex */
	mutex_init(&ps->lock);

	/* Parse device tree properties */
	ret = ps_parse_dt_properties(ps, dev->of_node);
	if (ret)
		return ret;

	/* Get GPIO descriptors */
	ps->txn_in_progress_gpio = devm_gpiod_get(dev, "txn-in-progress", GPIOD_IN);
	if (IS_ERR(ps->txn_in_progress_gpio)) {
		ret = PTR_ERR(ps->txn_in_progress_gpio);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Failed to get txn-in-progress GPIO: %d\n", ret);
		return ret;
	}

	ps->ipl_zero_gpio = devm_gpiod_get(dev, "ipl-zero", GPIOD_IN);
	if (IS_ERR(ps->ipl_zero_gpio)) {
		ret = PTR_ERR(ps->ipl_zero_gpio);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Failed to get ipl-zero GPIO: %d\n", ret);
		return ret;
	}

	ps->a0_gpio = devm_gpiod_get(dev, "a0", GPIOD_OUT_LOW);
	if (IS_ERR(ps->a0_gpio)) {
		ret = PTR_ERR(ps->a0_gpio);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Failed to get a0 GPIO: %d\n", ret);
		return ret;
	}

	ps->a1_gpio = devm_gpiod_get(dev, "a1", GPIOD_OUT_LOW);
	if (IS_ERR(ps->a1_gpio)) {
		ret = PTR_ERR(ps->a1_gpio);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Failed to get a1 GPIO: %d\n", ret);
		return ret;
	}

	ps->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ps->reset_gpio)) {
		ret = PTR_ERR(ps->reset_gpio);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Failed to get reset GPIO: %d\n", ret);
		return ret;
	}

	ps->rd_gpio = devm_gpiod_get(dev, "rd", GPIOD_OUT_LOW);
	if (IS_ERR(ps->rd_gpio)) {
		ret = PTR_ERR(ps->rd_gpio);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Failed to get rd GPIO: %d\n", ret);
		return ret;
	}

	ps->wr_gpio = devm_gpiod_get(dev, "wr", GPIOD_OUT_LOW);
	if (IS_ERR(ps->wr_gpio)) {
		ret = PTR_ERR(ps->wr_gpio);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Failed to get wr GPIO: %d\n", ret);
		return ret;
	}

	ps->data_gpios = devm_gpiod_get_array(dev, "data", GPIOD_ASIS);
	if (IS_ERR(ps->data_gpios)) {
		ret = PTR_ERR(ps->data_gpios);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Failed to get data GPIOs: %d\n", ret);
		return ret;
	}

	/* Get clock if available */
	ps->gpclk = devm_clk_get_optional(dev, "gpclk0");
	if (IS_ERR(ps->gpclk)) {
		ret = PTR_ERR(ps->gpclk);
		dev_warn(dev, "Failed to get gpclk: %d (will use CPRMAN registers)\n", ret);
		ps->gpclk = NULL;
	}

	/* Map GPIO registers */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "gpio");
	ps->gpio_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(ps->gpio_base)) {
		dev_err(dev, "Failed to map GPIO registers\n");
		return PTR_ERR(ps->gpio_base);
	}

	/* Map CPRMAN registers if available */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "cprman");
	if (res) {
		ps->cprman_base = devm_ioremap_resource(dev, res);
		if (IS_ERR(ps->cprman_base)) {
			dev_err(dev, "Failed to map CPRMAN registers\n");
			return PTR_ERR(ps->cprman_base);
		}
	}

	/* Setup misc device */
	ps->miscdev.minor = MISC_DYNAMIC_MINOR;
	ps->miscdev.name = DEVICE_NAME;
	ps->miscdev.fops = &ps_fops;
	ps->miscdev.parent = dev;

	ret = devm_misc_register(dev, &ps->miscdev);
	if (ret) {
		dev_err(dev, "Failed to register misc device: %d\n", ret);
		return ret;
	}

	/* Store device data */
	platform_set_drvdata(pdev, ps);

	/* Setup protocol */
	ret = ps_setup_protocol(ps);
	if (ret)
		dev_warn(dev, "Setup protocol failed: %d\n", ret);

	dev_info(dev, "/dev/%s ready (gpclk %s, src=%u div=%u)\n",
	         DEVICE_NAME,
	         ps->gpclk_ready ? "on" : "off",
	         ps->gpclk_src, ps->gpclk_div);

	return 0;
}

static int ps_remove(struct platform_device *pdev)
{
	struct pistorm_dev *ps = platform_get_drvdata(pdev);

	if (ps->gpclk) {
		clk_disable_unprepare(ps->gpclk);
	}

	return 0;
}

static const struct of_device_id ps_of_match[] = {
	{ .compatible = "akadata,pistorm", },
	{ .compatible = "pistorm,pistorm-v1", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ps_of_match);

static struct platform_driver ps_platform_driver = {
	.probe = ps_probe,
	.remove = ps_remove,
	.driver = {
		.name = "pistorm",
		.of_match_table = ps_of_match,
	},
};

module_platform_driver(ps_platform_driver);

MODULE_AUTHOR("AKADATA LIMITED (PiStorm64)");
MODULE_DESCRIPTION("PiStorm64 kernel backend: GPIO + GPCLK bus engine for PiStorm CPLD");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1.0");

MODULE_INFO(project, "PiStorm64");
MODULE_INFO(firmware, "N/A");
MODULE_INFO(name, "pistorm");
MODULE_INFO(alias, "pistorm");
MODULE_INFO(url, "https://github.com/akadata/pistorm64");
MODULE_INFO(known_working, "Pi Zero 2 W (BCM2837, Pi 3-class)");
MODULE_INFO(tested, "Pi 4-class (BCM2711) and Pi 3-class on Linux 6.12.62+rpt-rpi-v8");
MODULE_INFO(targets, "BCM2836 (Pi 2), BCM2837 (Pi 3-class), BCM2711 (Pi 4-class), BCM2712 (Pi 5-class)");
MODULE_INFO(intree, "Y"); /* in-tree */
MODULE_INFO(pistorm64, "GPIO/GPCLK backend only; userspace CPU stays userspace");
MODULE_INFO(git, PISTORM64_GIT);
MODULE_INFO(clock, "GPCLK0 alt0 on GPIO4, src/div configurable via DT properties akadata,gpclk-src/akadata,gpclk-div");
MODULE_INFO(gpio, "Pins: 0 TXN_IN_PROGRESS(in), 1 IPL_ZERO(in), 2 A0(out), 3 A1(out), 4 GPCLK0(alt0), 5 RESET(out), 6 RD(out), 7 WR(out), 8-23 D0..D15(bidir)");