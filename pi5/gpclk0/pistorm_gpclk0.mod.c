#include <linux/module.h>
#include <linux/export-internal.h>
#include <linux/compiler.h>

MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(".gnu.linkonce.this_module") = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};



static const struct modversion_info ____versions[]
__used __section("__versions") = {
	{ 0xfa474811, "__platform_driver_register" },
	{ 0xb6e6d99d, "clk_disable" },
	{ 0xb077e70a, "clk_unprepare" },
	{ 0x61fd46a9, "platform_driver_unregister" },
	{ 0x36a78de3, "devm_kmalloc" },
	{ 0x3034b00c, "devm_pinctrl_get" },
	{ 0xe6b525d4, "pinctrl_lookup_state" },
	{ 0x2862394e, "pinctrl_select_state" },
	{ 0xe856a90, "devm_clk_get" },
	{ 0xef54673c, "devm_clk_get_optional" },
	{ 0x72109e18, "device_property_read_u32_array" },
	{ 0x7c9a7371, "clk_prepare" },
	{ 0x815588a6, "clk_enable" },
	{ 0x556e4390, "clk_get_rate" },
	{ 0xbebbdd25, "_dev_info" },
	{ 0xea6c080b, "devm_pinctrl_put" },
	{ 0x3ed097c3, "_dev_err" },
	{ 0x6e706ab, "dev_err_probe" },
	{ 0x40dd8548, "_dev_warn" },
	{ 0xf0fdf6cb, "__stack_chk_fail" },
	{ 0x2396c7f0, "clk_set_parent" },
	{ 0x76d9b876, "clk_set_rate" },
	{ 0xcae3b64b, "devm_ioremap_resource" },
	{ 0xeae3dfd6, "__const_udelay" },
	{ 0x474e54d2, "module_layout" },
};

MODULE_INFO(depends, "");

MODULE_ALIAS("of:N*T*Cpistorm,gpclk0");
MODULE_ALIAS("of:N*T*Cpistorm,gpclk0C*");

MODULE_INFO(srcversion, "3CF3AA04C022DD8499680B9");
