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
	{ 0x4652d3db, "misc_deregister" },
	{ 0x3abe82c1, "param_ops_uint" },
	{ 0x106ba12c, "of_node_put" },
	{ 0x1c9e978f, "memdup_user" },
	{ 0xedc03953, "iounmap" },
	{ 0x4829a47e, "memcpy" },
	{ 0x037a0cba, "kfree" },
	{ 0xc3055d20, "usleep_range_state" },
	{ 0x4afd8e68, "of_find_compatible_node" },
	{ 0x92997ed8, "_printk" },
	{ 0xf0fdf6cb, "__stack_chk_fail" },
	{ 0x6cbbfc54, "__arch_copy_to_user" },
	{ 0x37448204, "noop_llseek" },
	{ 0x4dfa8d4b, "mutex_lock" },
	{ 0xfc14c2eb, "of_iomap" },
	{ 0xcefb0c9f, "__mutex_init" },
	{ 0xdcb764ad, "memset" },
	{ 0x314d80cf, "misc_register" },
	{ 0x15ba50a6, "jiffies" },
	{ 0x3213f038, "mutex_unlock" },
	{ 0xeae3dfd6, "__const_udelay" },
	{ 0x78f8e43a, "__kmalloc_cache_noprof" },
	{ 0x12a4e128, "__arch_copy_from_user" },
	{ 0x47229b5c, "gpio_request" },
	{ 0xf9a482f9, "msleep" },
	{ 0x871ab1c1, "kmalloc_caches" },
	{ 0x0596a05f, "module_layout" },
};

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "71F562AC8F62805E37C3109");
