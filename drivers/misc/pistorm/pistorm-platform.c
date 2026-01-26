// SPDX-License-Identifier: GPL-2.0
//
// PiStorm kernel backend: platform driver with gpiod, clk, and pinctrl
// Designed for upstream inclusion and Pi 2/3/4/5 compatibility

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/gpio/consumer.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/version.h>

#include <linux/pistorm.h>

#define DEVICE_NAME "pistorm"

#ifndef PISTORM64_GIT
#define PISTORM64_GIT "https://github.com/akadata/pistorm64"
#endif

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
	struct device *dev;
	struct clk *gpclk;
	struct miscdevice miscdev;
	struct mutex lock;

	/* GPIO descriptors */
	struct gpio_desc *txn_in_progress_gpio;
	struct gpio_desc *ipl_zero_gpio;
	struct gpio_desc *a0_gpio;
	struct gpio_desc *a1_gpio;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *rd_gpio;
	struct gpio_desc *wr_gpio;
	struct gpio_descs *data_gpios;

	/* Pinctrl states */
	struct pinctrl *pinctrl;
	struct pinctrl_state *pinctrl_default;
	struct pinctrl_state *pinctrl_bus_out;
	struct pinctrl_state *pinctrl_bus_in;

	/* Configuration properties */
	u32 txn_timeout_ms;
	u32 reset_pulse_ms;

	/* State tracking */
	bool data_out;
	bool gpclk_ready;
};

/* Set data bus direction using pinctrl states if available, otherwise use gpiod */
static int ps_set_bus_dir(struct pistorm_dev *ps, bool data_out)
{
	int ret = 0;

	if (ps->data_out == data_out)
		return 0;

	if (ps->pinctrl) {
		/* Use pinctrl states if available */
		struct pinctrl_state *state;

		if (data_out) {
			state = ps->pinctrl_bus_out;
		} else {
			state = ps->pinctrl_bus_in;
		}

		if (IS_ERR_OR_NULL(state)) {
			dev_err(ps->dev, "Invalid pinctrl state for direction %s\n",
			        data_out ? "out" : "in");
			return -EINVAL;
		}

		ret = pinctrl_select_state(ps->pinctrl, state);
		if (ret) {
			dev_err(ps->dev, "Failed to set bus direction %s: %d\n",
			        data_out ? "out" : "in", ret);
			return ret;
		}
	} else {
		/* Fallback to gpiod direction setting */
		for (int i = 0; i < 16; i++) {
			if (data_out) {
				gpiod_direction_output(ps->data_gpios->desc[i], 0);
			} else {
				gpiod_direction_input(ps->data_gpios->desc[i]);
			}
		}
	}

	ps->data_out = data_out;
	return ret;
}

/* Clear all control lines */
static void ps_clear_lines(struct pistorm_dev *ps)
{
	gpiod_set_value_cansleep(ps->a0_gpio, 0);
	gpiod_set_value_cansleep(ps->a1_gpio, 0);
	gpiod_set_value_cansleep(ps->reset_gpio, 0);
	gpiod_set_value_cansleep(ps->rd_gpio, 0);
	gpiod_set_value_cansleep(ps->wr_gpio, 0);
	
	// Clear data lines if in output mode
	if (ps->data_out) {
		for (int i = 0; i < 16; i++) {
			gpiod_set_value_cansleep(ps->data_gpios->desc[i], 0);
		}
	}
}

/* Wait for transaction to complete */
static int ps_wait_for_txn(struct pistorm_dev *ps)
{
	unsigned long timeout_jiffies = jiffies + msecs_to_jiffies(ps->txn_timeout_ms);

	while (time_before(jiffies, timeout_jiffies)) {
		if (!gpiod_get_value_cansleep(ps->txn_in_progress_gpio))
			return 0;
		cpu_relax();
	}

	dev_err(ps->dev, "ps_wait_for_txn timed out after %u ms\n", ps->txn_timeout_ms);
	return -ETIMEDOUT;
}

static int ps_wait_for_txn_log(struct pistorm_dev *ps, const char *op)
{
	int ret = ps_wait_for_txn(ps);
	if (ret == -ETIMEDOUT)
		dev_err(ps->dev,
			"txn timeout waiting for %s (PIN_TXN_IN_PROGRESS stuck)\n", op);
	return ret;
}

/* Write a payload to the bus */
static void ps_write_payload(struct pistorm_dev *ps, u32 payload, u32 reg_sel)
{
	// Set address lines
	gpiod_set_value_cansleep(ps->a0_gpio, (reg_sel & 1));
	gpiod_set_value_cansleep(ps->a1_gpio, ((reg_sel >> 1) & 1));

	// Set data lines (output mode assumed)
	for (int i = 0; i < 16; i++) {
		gpiod_set_value_cansleep(ps->data_gpios->desc[i], (payload >> (8 + i)) & 1);
	}

	// Pulse write strobe
	gpiod_set_value_cansleep(ps->wr_gpio, 1);
	ndelay(10); // Small delay for setup time
	gpiod_set_value_cansleep(ps->wr_gpio, 0);
	ndelay(10); // Small delay for hold time

	ps_clear_lines(ps);
}

static int ps_setup_gpclk(struct pistorm_dev *ps)
{
	int ret;
	
	if (!ps->gpclk) {
		dev_warn(ps->dev, "GPCLK not available, continuing without clock\n");
		return 0;
	}
	
	/* Try to set a reasonable rate for GPCLK0 */
	ret = clk_set_rate(ps->gpclk, 200000000); /* 200 MHz target */
	if (ret) {
		dev_warn(ps->dev, "Failed to set gpclk rate: %d, continuing anyway\n", ret);
		/* Continue anyway - some systems might not allow rate changes */
	}
	
	ret = clk_prepare_enable(ps->gpclk);
	if (ret) {
		dev_err(ps->dev, "Failed to enable gpclk: %d\n", ret);
		return ret;
	}
	
	ps->gpclk_ready = true;
	dev_info(ps->dev, "gpclk enabled at %lu Hz\n", clk_get_rate(ps->gpclk));
	return 0;
}

static int ps_setup_protocol(struct pistorm_dev *ps)
{
	int ret;

	/* Set default pinctrl state if available */
	if (ps->pinctrl && ps->pinctrl_default) {
		ret = pinctrl_select_state(ps->pinctrl, ps->pinctrl_default);
		if (ret) {
			dev_err(ps->dev, "Failed to set default pinctrl state: %d\n", ret);
			return ret;
		}
	}

	/* Set initial direction to input */
	ps->data_out = false;
	ps_set_bus_dir(ps, false);
	ps_clear_lines(ps);

	ret = ps_setup_gpclk(ps);
	if (ret)
		dev_warn(ps->dev, "gpclk not configured (%d)\n", ret);

	return 0;
}

static int ps_write16(struct pistorm_dev *ps, u32 addr, u16 data)
{
	int ret;

	ps_set_bus_dir(ps, true);
	ps_write_payload(ps, (data & 0xffff) << 8, REG_DATA);
	ps_write_payload(ps, (addr & 0xffff) << 8, REG_ADDR_LO);
	ps_write_payload(ps, ((0x0000 | (addr >> 16)) << 8), REG_ADDR_HI);
	ps_set_bus_dir(ps, false);
	
	ret = ps_wait_for_txn_log(ps, "write16");
	return ret;
}

static int ps_write8(struct pistorm_dev *ps, u32 addr, u8 data)
{
	u16 payload = (addr & 1) ? data : (data | (data << 8));
	int ret;

	ps_set_bus_dir(ps, true);
	ps_write_payload(ps, (payload & 0xffff) << 8, REG_DATA);
	ps_write_payload(ps, (addr & 0xffff) << 8, REG_ADDR_LO);
	ps_write_payload(ps, ((0x0100 | (addr >> 16)) << 8), REG_ADDR_HI);
	ps_set_bus_dir(ps, false);
	
	ret = ps_wait_for_txn_log(ps, "write8");
	return ret;
}

static int ps_read16(struct pistorm_dev *ps, u32 addr, u16 *out)
{
	int ret;
	u16 value = 0;

	ps_set_bus_dir(ps, true);
	ps_write_payload(ps, (addr & 0xffff) << 8, REG_ADDR_LO);
	ps_write_payload(ps, ((0x0200 | (addr >> 16)) << 8), REG_ADDR_HI);

	ps_set_bus_dir(ps, false);
	
	// Set address lines for data register
	gpiod_set_value_cansleep(ps->a0_gpio, (REG_DATA & 1));
	gpiod_set_value_cansleep(ps->a1_gpio, ((REG_DATA >> 1) & 1));
	
	// Enable read
	gpiod_set_value_cansleep(ps->rd_gpio, 1);
	ndelay(10); // Small delay for setup time

	ret = ps_wait_for_txn_log(ps, "read16");
	
	// Read data lines
	for (int i = 0; i < 16; i++) {
		if (gpiod_get_value_cansleep(ps->data_gpios->desc[i]))
			value |= (1 << i);
	}
	
	gpiod_set_value_cansleep(ps->rd_gpio, 0);
	ps_clear_lines(ps);

	if (ret)
		return ret;
	
	*out = value;
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
	u16 value = 0;
	int ret;

	ps_set_bus_dir(ps, false);
	
	// Set address lines for status register
	gpiod_set_value_cansleep(ps->a0_gpio, (REG_STATUS & 1));
	gpiod_set_value_cansleep(ps->a1_gpio, ((REG_STATUS >> 1) & 1));
	
	// Enable read
	gpiod_set_value_cansleep(ps->rd_gpio, 1);
	ndelay(10); // Small delay for setup time
	
	// Brief delay to allow data to settle
	ndelay(50);
	
	// Read data lines
	for (int i = 0; i < 16; i++) {
		if (gpiod_get_value_cansleep(ps->data_gpios->desc[i]))
			value |= (1 << i);
	}
	
	gpiod_set_value_cansleep(ps->rd_gpio, 0);
	ps_clear_lines(ps);
	
	*out = value;
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
	int ret;
	
	// Assert reset
	ret = ps_write_status(ps, 0);
	if (ret)
		return ret;
	
	// Hold for reset pulse duration
	msleep(ps->reset_pulse_ms);
	
	// Deassert reset - this sets the RESET bit which typically deasserts the reset line
	// (the CPLD logic interprets this as "deassert reset")
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
	struct pistorm_dev *ps = file->private_data;  // Get from file->private_data set in ps_open
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
		// Pack GPIO values into gplev0/gplev1 format similar to BCM GPIO registers
		pins.gplev0 = 0;
		pins.gplev1 = 0;

		// Pack first 32 GPIOs (0-31) into gplev0 and gplev1
		// For this driver, we'll pack the control pins and first 16 data pins
		if (gpiod_get_value_cansleep(ps->txn_in_progress_gpio))
			pins.gplev0 |= BIT(0);
		if (gpiod_get_value_cansleep(ps->ipl_zero_gpio))
			pins.gplev0 |= BIT(1);
		if (gpiod_get_value_cansleep(ps->a0_gpio))
			pins.gplev0 |= BIT(2);
		if (gpiod_get_value_cansleep(ps->a1_gpio))
			pins.gplev0 |= BIT(3);
		if (gpiod_get_value_cansleep(ps->reset_gpio))
			pins.gplev0 |= BIT(5);
		if (gpiod_get_value_cansleep(ps->rd_gpio))
			pins.gplev0 |= BIT(6);
		if (gpiod_get_value_cansleep(ps->wr_gpio))
			pins.gplev0 |= BIT(7);

		// Pack data pins D0-D15 into bits 8-23 of gplev0
		for (int i = 0; i < 16; i++) {
			if (gpiod_get_value_cansleep(ps->data_gpios->desc[i]))
				pins.gplev0 |= BIT(8 + i);
		}

		if (copy_to_user(argp, &pins, sizeof(pins)))
			ret = -EFAULT;
		break;
	case PISTORM_IOC_QUERY:
		query.abi_version = PISTORM_ABI_VERSION;
		query.capabilities = PISTORM_CAP_BUSOP | PISTORM_CAP_BATCH | 
		                    PISTORM_CAP_STATUS | PISTORM_CAP_RESET;
		memset(query.reserved, 0, sizeof(query.reserved));
		if (copy_to_user(argp, &query, sizeof(query)))
			ret = -EFAULT;
		break;
	case PISTORM_IOC_BUSOP:
		if (copy_from_user(&busop, argp, sizeof(busop))) {
			ret = -EFAULT;
			break;
		}
		dev_dbg(ps->dev,
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
		dev_dbg(ps->dev,
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
	struct miscdevice *mdev = file->private_data;
	struct pistorm_dev *ps = container_of(mdev, struct pistorm_dev, miscdev);
	file->private_data = ps;
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
	
	/* Parse transaction timeout */
	ret = of_property_read_u32(np, "txn-timeout-ms", &ps->txn_timeout_ms);
	if (ret < 0)
		ps->txn_timeout_ms = 500; /* Default to 500ms */
	
	/* Parse reset pulse duration */
	ret = of_property_read_u32(np, "reset-pulse-ms", &ps->reset_pulse_ms);
	if (ret < 0)
		ps->reset_pulse_ms = 100; /* Default to 100ms */
	
	return 0;
}

static int ps_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct pistorm_dev *ps;
	int ret;

	ps = devm_kzalloc(dev, sizeof(*ps), GFP_KERNEL);
	if (!ps)
		return -ENOMEM;

	ps->dev = dev;

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

	ps->data_gpios = devm_gpiod_get_array(dev, "data", GPIOD_OUT_LOW);
	if (IS_ERR(ps->data_gpios)) {
		ret = PTR_ERR(ps->data_gpios);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Failed to get data GPIOs: %d\n", ret);
		return ret;
	}

	if (ps->data_gpios->ndescs != 16) {
		dev_err(dev, "Expected 16 data GPIOs, got %d\n", ps->data_gpios->ndescs);
		return -EINVAL;
	}

	/* Get clock if available */
	ps->gpclk = devm_clk_get_optional(dev, "gpclk");
	if (IS_ERR(ps->gpclk)) {
		ret = PTR_ERR(ps->gpclk);
		dev_warn(dev, "Failed to get gpclk: %d (continuing without clock)\n", ret);
		ps->gpclk = NULL;
	}

	/* Get pinctrl states - optional but recommended for proper operation */
	ps->pinctrl = devm_pinctrl_get(dev);
	if (IS_ERR(ps->pinctrl)) {
		ret = PTR_ERR(ps->pinctrl);
		if (ret == -EPROBE_DEFER) {
			dev_err(dev, "Cannot get pinctrl: %d\n", ret);
			return ret;
		}
		dev_warn(dev, "Cannot get pinctrl: %d (continuing without pinctrl)\n", ret);
		ps->pinctrl = NULL;
	} else {
		ps->pinctrl_default = pinctrl_lookup_state(ps->pinctrl, "default");
		if (IS_ERR(ps->pinctrl_default)) {
			dev_warn(dev, "Cannot get default pinctrl state (continuing without pinctrl)\n");
			ps->pinctrl = NULL; /* Disable pinctrl if states are missing */
		} else {
			ps->pinctrl_bus_out = pinctrl_lookup_state(ps->pinctrl, "bus-out");
			if (IS_ERR(ps->pinctrl_bus_out)) {
				dev_warn(dev, "Cannot get bus-out pinctrl state (continuing without pinctrl)\n");
				ps->pinctrl = NULL;
			} else {
				ps->pinctrl_bus_in = pinctrl_lookup_state(ps->pinctrl, "bus-in");
				if (IS_ERR(ps->pinctrl_bus_in)) {
					dev_warn(dev, "Cannot get bus-in pinctrl state (continuing without pinctrl)\n");
					ps->pinctrl = NULL;
				}
			}
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

	dev_info(dev, "/dev/%s ready (gpclk %s, timeout %ums)\n",
	         DEVICE_NAME,
	         ps->gpclk_ready ? "on" : "off",
	         ps->txn_timeout_ms);

	return 0;
}

static int ps_remove(struct platform_device *pdev)
{
	struct pistorm_dev *ps = platform_get_drvdata(pdev);

	if (ps->gpclk) {
		clk_disable_unprepare(ps->gpclk);
	}

	// Put the device in a safe state
	gpiod_set_value_cansleep(ps->reset_gpio, 1);  // Assert reset
	ps_clear_lines(ps);

	return 0;
}

static const struct of_device_id ps_of_match[] = {
	{ .compatible = "akadata,pistorm", },
	{ .compatible = "akadata,pistorm-v1", },
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

MODULE_AUTHOR("Andrew Smalley <andrew@akadata.ltd>");
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
MODULE_INFO(targets, "BCM2836 (Pi 2), BCM2837 (Pi 3-class), BCM2711 (Pi 4-class), RP1 (Pi 5-class)");
MODULE_INFO(intree, "Y"); /* in-tree */
MODULE_INFO(pistorm64, "GPIO/GPCLK backend only; userspace CPU stays userspace");
MODULE_INFO(git, PISTORM64_GIT);
MODULE_INFO(clock, "GPCLK0 via clock framework, pinctrl manages pinmux");
MODULE_INFO(gpio, "Using gpiod framework: TXN_IN_PROGRESS(in), IPL_ZERO(in), A0(out), A1(out), RESET(out), RD(out), WR(out), D0..D15(bidir)");