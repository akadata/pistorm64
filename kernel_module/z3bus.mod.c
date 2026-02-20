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

KSYMTAB_FUNC(pistorm_z3_register_driver, "_gpl", "");
KSYMTAB_FUNC(pistorm_z3_unregister_driver, "_gpl", "");
KSYMTAB_FUNC(pistorm_z3_get_device_count, "_gpl", "");
KSYMTAB_FUNC(pistorm_z3_get_device, "_gpl", "");

SYMBOL_CRC(pistorm_z3_register_driver, 0x476acd7b, "_gpl");
SYMBOL_CRC(pistorm_z3_unregister_driver, 0x6a580a09, "_gpl");
SYMBOL_CRC(pistorm_z3_get_device_count, 0x68e1e0d3, "_gpl");
SYMBOL_CRC(pistorm_z3_get_device, 0x95a43edd, "_gpl");

static const struct modversion_info ____versions[]
__used __section("__versions") = {
	{ 0x74d752a0, "misc_deregister" },
	{ 0x92997ed8, "_printk" },
	{ 0xf0fdf6cb, "__stack_chk_fail" },
	{ 0x6cbbfc54, "__arch_copy_to_user" },
	{ 0x4dfa8d4b, "mutex_lock" },
	{ 0xdcb764ad, "memset" },
	{ 0x66ae298a, "misc_register" },
	{ 0x3213f038, "mutex_unlock" },
	{ 0x12a4e128, "__arch_copy_from_user" },
	{ 0x66073946, "module_layout" },
};

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "8A3ABD9C8D3D389E6BE05AE");
