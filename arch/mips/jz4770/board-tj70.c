/*
 * linux/arch/mips/jz4770/board-tj70.c
 *
 * JZ4770 tj70 board setup routines.
 *
 * Copyright (c) 2006-2012  Ingenic Semiconductor Inc.
 * Author: <jlwei@ingenic.cn>
 * Author: Kage <kkshen@ingenic.cn>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/i2c-gpio.h>
#include <linux/platform_device.h>

#include <asm/cpu.h>
#include <asm/bootinfo.h>
#include <asm/mipsregs.h>
#include <asm/reboot.h>

#include <asm/jzsoc.h>
#include <linux/mmc/host.h>

#undef DEBUG
//#define DEBUG
#ifdef DEBUG
#define dprintk(x...)	printk(x)
#else
#define dprintk(x...)
#endif

/*
 *  config_gpio_on_sleep config all gpio pins when sleep.
 */
struct gpio_sleep_status {
	unsigned int input;
	unsigned int input_pull;
	unsigned int input_no_pull;
	unsigned int output;
	unsigned int output_high;
	unsigned int output_low;
	unsigned int no_operation;
};

void config_gpio_on_sleep(void)
{
	int i = 0;
	struct gpio_sleep_status gpio_sleep_st[] = {
		/* GPA */
		{
			.input_pull = BIT31 | BIT27 |
			BITS_H2L(29,28) | /* NC pin */
			BITS_H2L(19,0), /* NAND: SD0~SD15 */

			.output_high = BIT21 | BIT22 | BIT23 | BIT24 | BIT25 | BIT26, /* NAND: CS1~CS6 */
			.output_low = 0x0,
#ifndef CONFIG_JZ_SYSTEM_AT_CARD
			.no_operation = 0x0,
#else
			.no_operation = BITS_H2L(23, 18),
#endif
		},

		/* GPB */
		{
			.input_pull = BIT30 | BIT27 | BIT26 | BIT25 | BITS_H2L(24,22) | BIT20 |
			BITS_H2L(19,0), /* SA0~SA5 */

			.output_high = BIT29,
			.output_low = BIT31 | BIT28 | BIT21,
#ifndef CONFIG_JZ_SYSTEM_AT_CARD
			.no_operation = 0x0,
#else
			.no_operation = BIT0,
#endif
		},

		/* GPC */
		{
			.input_pull = BITS_H2L(31,28),
			.output_high = 0x0,
			.output_low = BITS_H2L(27,0),
			.no_operation = 0x0,
		},

		/* GPD */
		{
			.input_pull = BITS_H2L(29,26) | BITS_H2L(19,14) | BITS_H2L(13,12) || BITS_H2L(10,0) | BIT11,  // bit11 temporary input_pull
			.output_high = 0x0,
			.output_low =  BITS_H2L(25,20), // | BIT11,
			.no_operation = 0x0,
		},

		/* GPE */
		{
			.input_pull = BITS_H2L(18,11) | BITS_H2L(8,3) | BIT0,
			.output_high = BIT9,
			.output_low = BITS_H2L(29,20) | BIT10 | BIT1 | BIT2,
			.no_operation = 0x0,
		},

		/* GPF */
		{
			.input_pull = BIT11 | BITS_H2L(8,4) | BITS_H2L(2,0),
			.output_high = BIT9,
			.output_low = BIT3,
			.no_operation = 0x0,
		},
	};

	for (i = 0; i < 6; i++) {
		gpio_sleep_st[i].input_pull &= ~gpio_sleep_st[i].no_operation;
		gpio_sleep_st[i].output_high &= ~gpio_sleep_st[i].no_operation;
		gpio_sleep_st[i].output_low &= ~gpio_sleep_st[i].no_operation;
		gpio_sleep_st[i].input_no_pull = 0xffffffff &
			~(gpio_sleep_st[i].input_pull |
			  gpio_sleep_st[i].output_high |
			  gpio_sleep_st[i].output_low) &
			~gpio_sleep_st[i].no_operation;

		gpio_sleep_st[i].input = gpio_sleep_st[i].input_pull | gpio_sleep_st[i].input_no_pull;
		gpio_sleep_st[i].output = gpio_sleep_st[i].output_high | gpio_sleep_st[i].output_low;

		/* all as gpio, except interrupt pins(see @wakeup_key_setup()) */
		REG_GPIO_PXINTC(i) =  (0xffffffff & ~gpio_sleep_st[i].no_operation);
		REG_GPIO_PXMASKS(i) =  (0xffffffff & ~gpio_sleep_st[i].no_operation);
		/* input */
		REG_GPIO_PXPAT1S(i) =  gpio_sleep_st[i].input;
		/* pull */
		REG_GPIO_PXPENC(i) = gpio_sleep_st[i].input_pull;
		/* no_pull */
		REG_GPIO_PXPENS(i) =  gpio_sleep_st[i].input_no_pull;

		/* output */
		REG_GPIO_PXPAT1C(i) =  gpio_sleep_st[i].output;
		REG_GPIO_PXPENS(i)  = gpio_sleep_st[i].output; /* disable pull */
		/* high */
		REG_GPIO_PXPAT0S(i) = gpio_sleep_st[i].output_high;
		/* low */
		REG_GPIO_PXPAT0C(i) = gpio_sleep_st[i].output_low;
	}
}

struct wakeup_key_s {
	int gpio;       /* gpio pin number */
	int active_low; /* the key interrupt pin is low voltage
                           or fall edge acitve */
};

/* add wakeup keys here */
static struct wakeup_key_s wakeup_key[] = {
	{
		.gpio = GPIO_POWER_ON,
		.active_low = ACTIVE_LOW_WAKE_UP,
	},
/*
#ifndef CONFIG_JZ_SYSTEM_AT_CARD
	{
		.gpio = MSC0_HOTPLUG_PIN,
		.active_low = ACTIVE_LOW_MSC0_CD,
	},
#endif
	{
		.gpio = MSC1_HOTPLUG_PIN,
		.active_low = ACTIVE_LOW_MSC1_CD,
	},
*/
};

static void wakeup_key_setup(void)
{
	int i;
	int num = sizeof(wakeup_key) / sizeof(wakeup_key[0]);

	for(i = 0; i < num; i++) {
		if(wakeup_key[i].active_low)
			__gpio_as_irq_fall_edge(wakeup_key[i].gpio);
		else
			__gpio_as_irq_rise_edge(wakeup_key[i].gpio);

		__gpio_ack_irq(wakeup_key[i].gpio);
		__gpio_unmask_irq(wakeup_key[i].gpio);
		__intc_unmask_irq(IRQ_GPIO0 - (wakeup_key[i].gpio/32));  /* unmask IRQ_GPIOn */
	}
}


/* NOTES:
 * 1: Pins that are floated (NC) should be set as input and pull-enable.
 * 2: Pins that are pull-up or pull-down by outside should be set as input
 *    and pull-disable.
 * 3: Pins that are connected to a chip except sdram and nand flash
 *    should be set as input and pull-disable, too.
 */
void jz_board_do_sleep(unsigned long *ptr)
{
	unsigned char i;

        /* Print messages of GPIO registers for debug */
	for(i=0;i<GPIO_PORT_NUM;i++) {
		dprintk("run int:%x mask:%x pat1:%x pat0:%x pen:%x flg:%x\n",        \
			REG_GPIO_PXINT(i),REG_GPIO_PXMASK(i),REG_GPIO_PXPAT1(i),REG_GPIO_PXPAT0(i), \
			REG_GPIO_PXPEN(i),REG_GPIO_PXFLG(i));
	}

        /* Save GPIO registers */
	for(i = 1; i < GPIO_PORT_NUM; i++) {
		*ptr++ = REG_GPIO_PXINT(i);
		*ptr++ = REG_GPIO_PXMASK(i);
		*ptr++ = REG_GPIO_PXPAT0(i);
		*ptr++ = REG_GPIO_PXPAT1(i);
		*ptr++ = REG_GPIO_PXFLG(i);
		*ptr++ = REG_GPIO_PXPEN(i);
	}

        /*
         * Set all pins to pull-disable, and set all pins as input except
         * sdram and the pins which can be used as CS1_N to CS4_N for chip select.
         */
	config_gpio_on_sleep();

        /*
	 * Set proper status for GPC21 to GPC24 which can be used as CS1_N to CS4_N.
	 * Keep the pins' function used for chip select(CS) here according to your
         * system to avoid chip select crashing with sdram when resuming from sleep mode.
         */

	/*
         * If you must set some GPIOs as output to high level or low level,
         * you can set them here, using:
         * __gpio_as_output(n);
         * __gpio_set_pin(n); or  __gpio_clear_pin(n);
	 */

	if (!console_suspend_enabled)
		__gpio_as_uart2();

	wakeup_key_setup();
}

void jz_board_do_resume(unsigned long *ptr)
{
	unsigned char i;

	/* Restore GPIO registers */
		for(i = 1; i < GPIO_PORT_NUM; i++) {
		 REG_GPIO_PXINTS(i) = *ptr;
		 REG_GPIO_PXINTC(i) = ~(*ptr++);

		 REG_GPIO_PXMASKS(i) = *ptr;
		 REG_GPIO_PXMASKC(i) = ~(*ptr++);

		 REG_GPIO_PXPAT0S(i) = *ptr;
		 REG_GPIO_PXPAT0C(i) = ~(*ptr++);

		 REG_GPIO_PXPAT1S(i) = *ptr;
		 REG_GPIO_PXPAT1C(i) = ~(*ptr++);

//		 REG_GPIO_PXFLGS(i)=*ptr;
		 REG_GPIO_PXFLGC(i)=~(*ptr++);

		 REG_GPIO_PXPENS(i)=*ptr;
		 REG_GPIO_PXPENC(i)=~(*ptr++);
	}

        /* Print messages of GPIO registers for debug */
	for(i=0;i<GPIO_PORT_NUM;i++) {
		dprintk("run int:%x mask:%x pat1:%x pat0:%x pen:%x flg:%x\n",        \
			REG_GPIO_PXINT(i),REG_GPIO_PXMASK(i),REG_GPIO_PXPAT1(i),REG_GPIO_PXPAT0(i), \
			REG_GPIO_PXPEN(i),REG_GPIO_PXFLG(i));
	}
}

#define WM831X_LDO_MAX_NAME 6

extern void (*jz_timer_callback)(void);
extern int __init jz_add_msc_devices(unsigned int controller, struct jz_mmc_platform_data *plat);

static void dancing(void)
{
	static unsigned char slash[] = "\\|/-";
//	static volatile unsigned char *p = (unsigned char *)0xb6000058;
	static volatile unsigned char *p = (unsigned char *)0xb6000016;
	static unsigned int count = 0;
	*p = slash[count++];
	count &= 3;
}

/* MSC SETUP */
static void tj70_inand_gpio_init(struct device *dev)
{
#if defined (CONFIG_JZ_SYSTEM_AT_CARD)
	__gpio_as_msc0_boot();
#else
	__gpio_as_msc0_8bit();
#endif
	__gpio_as_output1(GPIO_SD0_VCC_EN_N); /* poweroff */
	__gpio_as_input(GPIO_SD0_CD_N);
}

static void tj70_inand_power_on(struct device *dev)
{
	__gpio_as_output0(GPIO_SD0_VCC_EN_N);
}

static void tj70_inand_power_off(struct device *dev)
{
	__gpio_as_output1(GPIO_SD0_VCC_EN_N);
}

static void tj70_inand_cpm_start(struct device *dev)
{
	cpm_set_clock(CGU_MSC0CLK, 25 * 1000 * 1000);
	cpm_start_clock(CGM_MSC0);
}

static unsigned int tj70_inand_status(struct device *dev)
{
	unsigned int status;

 	status = 1;
 	return (status);
}

#define KBYTE  (1024)
#define MBYTE  ((KBYTE)*(KBYTE))
#define UINT32_MAX             (0xffffffffU)

struct mmc_partition_info tj70_partitions[] = {
	[0] = {"mbr",           0,       512, 0}, 	//0 - 512KB
	[1] = {"xboot",		0,     2*MBYTE, 0}, 	//0 - 2MB
	[2] = {"boot",      3*MBYTE,   8*MBYTE, 0}, 	//3MB - 8MB
	[3] = {"recovery", 12*MBYTE,   8*MBYTE, 0}, 	//12MB - 8MB
	[4] = {"misc",     21*MBYTE,   4*MBYTE, 0}, 	//21MB - 4MB
	[5] = {"battery",  26*MBYTE,   1*MBYTE, 0}, 	//26MB - 1MB
	[6] = {"cache",    28*MBYTE,  30*MBYTE, 1}, 	//28MB - 30MB
	[7] = {"device_id",59*MBYTE,   2*MBYTE, 0},	//59MB - 2MB
	[8] = {"rootfs",   64*MBYTE, 256*MBYTE, 1}, 	//64MB - 256MB
	[9] = {"data",    321*MBYTE, 512*MBYTE, 1}, 	//321MB - 512MB
	[10]= {"test_0",         0, UINT32_MAX, 0},
};

static struct jz_mmc_platform_data tj70_inand_data = {
#ifndef CONFIG_MSC0_SDIO_SUPPORT
	.support_sdio   = 0,
#else
	.support_sdio   = 1,
#endif
	.ocr_mask	= MMC_VDD_32_33 | MMC_VDD_33_34,
	.init           = tj70_inand_gpio_init,
	.power_on       = tj70_inand_power_on,
	.power_off      = tj70_inand_power_off,
	.cpm_start      = tj70_inand_cpm_start,
	.status		= tj70_inand_status,
	.max_bus_width  = MMC_CAP_SD_HIGHSPEED | MMC_CAP_MMC_HIGHSPEED | MMC_CAP_4_BIT_DATA,
//	.max_bus_width  = MMC_CAP_4_BIT_DATA,
#ifdef CONFIG_MSC0_BUS_1
	.bus_width      = 1,
#elif defined  CONFIG_MSC0_BUS_4
	.bus_width      = 4,
#elif defined  CONFIG_MSC0_BUS_8
	.bus_width      = 8,
#else
	.bus_width      = 4,
#endif
//	.protect_boundary = 21*MBYTE,
	.partitions = tj70_partitions,
	.num_partitions = ARRAY_SIZE(tj70_partitions),

};


static void tj70_tf_gpio_init(struct device *dev)
{
	__gpio_as_msc1_4bit();
	__gpio_as_output1(GPIO_SD1_VCC_EN_N); /* poweroff */
	__gpio_as_input(GPIO_SD1_CD_N);
}

static void tj70_tf_power_on(struct device *dev)
{
	__gpio_as_output0(GPIO_SD1_VCC_EN_N);
}

static void tj70_tf_power_off(struct device *dev)
{
	__gpio_as_output1(GPIO_SD1_VCC_EN_N);
}

static void tj70_tf_cpm_start(struct device *dev)
{
	cpm_start_clock(CGM_MSC1);
}

static unsigned int tj70_tf_status(struct device *dev)
{
	unsigned int status = 0;
	status = (unsigned int) __gpio_get_pin(GPIO_SD1_CD_N);
#if ACTIVE_LOW_MSC1_CD
	return !status;
#else
	return status;
#endif
}

static void tj70_tf_plug_change(int state)
{
	if(state == CARD_INSERTED) /* wait for remove */
#if ACTIVE_LOW_MSC1_CD
		__gpio_as_irq_high_level(MSC1_HOTPLUG_PIN);
#else
	__gpio_as_irq_low_level(MSC1_HOTPLUG_PIN);
#endif
	else		      /* wait for insert */
#if ACTIVE_LOW_MSC1_CD
		__gpio_as_irq_low_level(MSC1_HOTPLUG_PIN);
#else
	__gpio_as_irq_high_level(MSC1_HOTPLUG_PIN);
#endif
}

struct jz_mmc_platform_data tj70_tf_data = {
#ifndef CONFIG_JZ_MSC1_SDIO_SUPPORT
	.support_sdio   = 0,
#else
	.support_sdio   = 1,
#endif
	.ocr_mask	= MMC_VDD_32_33 | MMC_VDD_33_34,
	.status_irq	= MSC1_HOTPLUG_IRQ,
	.detect_pin     = GPIO_SD1_CD_N,
	.init           = tj70_tf_gpio_init,
	.power_on       = tj70_tf_power_on,
	.power_off      = tj70_tf_power_off,
	.cpm_start	= tj70_tf_cpm_start,
	.status		= tj70_tf_status,
	.plug_change	= tj70_tf_plug_change,
	.max_bus_width  = MMC_CAP_SD_HIGHSPEED
#ifdef CONFIG_JZ_MSC1_BUS_4
	| MMC_CAP_4_BIT_DATA
#endif
	,
#ifdef CONFIG_JZ_MSC1_BUS_1
	.bus_width      = 1,
#else
	.bus_width      = 4,
#endif
};

static void tj70_msc2_gpio_init(struct device *dev)
{
	__gpio_as_msc2_8bit();
	__gpio_as_output1(GPIO_SD0_VCC_EN_N); /* poweroff */
	__gpio_as_input(GPIO_SD0_CD_N);

	return;
}

static void tj70_msc2_power_on(struct device *dev)
{
	__gpio_as_output0(GPIO_SD0_VCC_EN_N);

	return;
}

static void tj70_msc2_power_off(struct device *dev)
{
	__gpio_as_output1(GPIO_SD0_VCC_EN_N);

	return;
}

static void tj70_msc2_cpm_start(struct device *dev)
{
	cpm_start_clock(CGM_MSC2);
}

static unsigned int tj70_msc2_status(struct device *dev)
{
	unsigned int status;

	status = (unsigned int) __gpio_get_pin(GPIO_SD0_CD_N);
#if ACTIVE_LOW_MSC0_CD
	return !status;
#else
	return status;
#endif
}

static void tj70_msc2_plug_change(int state)
{
	if(state == CARD_INSERTED) /* wait for remove */
#if ACTIVE_LOW_MSC0_CD
		__gpio_as_irq_high_level(MSC0_HOTPLUG_PIN);
#else
	__gpio_as_irq_low_level(MSC0_HOTPLUG_PIN);
#endif
	else		      /* wait for insert */
#if ACTIVE_LOW_MSC0_CD
		__gpio_as_irq_low_level(MSC0_HOTPLUG_PIN);
#else
	__gpio_as_irq_high_level(MSC0_HOTPLUG_PIN);
#endif

	return;
}

struct jz_mmc_platform_data tj70_msc2_data = {
#ifndef CONFIG_JZ_MSC2_SDIO_SUPPORT
	.support_sdio   = 0,
#else
	.support_sdio   = 1,
#endif
	.ocr_mask	= MMC_VDD_32_33 | MMC_VDD_33_34,
	.status_irq	= MSC0_HOTPLUG_IRQ,
	.detect_pin     = GPIO_SD0_CD_N,
	.init           = tj70_msc2_gpio_init,
	.power_on       = tj70_msc2_power_on,
	.power_off      = tj70_msc2_power_off,
	.cpm_start	= tj70_msc2_cpm_start,
	.status		= tj70_msc2_status,
	.plug_change	= tj70_msc2_plug_change,
	.max_bus_width  = MMC_CAP_SD_HIGHSPEED
#ifdef CONFIG_JZ_MSC2_BUS_4
	| MMC_CAP_4_BIT_DATA
#endif
	,
#ifdef CONFIG_JZ_MSC2_BUS_1
	.bus_width      = 1,
#else
	.bus_width      = 4,
#endif
};

void __init board_msc_init(void)
{
	jz_add_msc_devices(0, &tj70_inand_data);
	jz_add_msc_devices(1, &tj70_tf_data);
	jz_add_msc_devices(2, &tj70_msc2_data);
}

static void tj70_timer_callback(void)
{
	static unsigned long count = 0;

	if ((++count) % 50 == 0) {
		dancing();
		count = 0;
	}
}

static void __init board_cpm_setup(void)
{
	/* Stop unused module clocks here.
	 * We have started all module clocks at arch/mips/jz4760/setup.c.
	 */
}

static void __init board_gpio_setup(void)
{
	/*
	 * Initialize SDRAM pins
	 */
}

static struct i2c_board_info tj70_i2c1_devs[] __initdata = {
};

static struct i2c_board_info tj70_i2c0_devs[] __initdata = {
};

#if defined(CONFIG_I2C_GPIO)
static struct i2c_board_info tj70_gpio_i2c_devs[] __initdata = {
};

static struct i2c_gpio_platform_data tj70_i2c_gpio_data = {
        .sda_pin        = GPIO_I2C1_SDA,
        .scl_pin        = GPIO_I2C1_SCK,
};

static struct platform_device tj70_i2c_gpio_device = {
        .name   = "i2c-gpio",
        .id     = 3,
        .dev    = {
                .platform_data = &tj70_i2c_gpio_data,
        },
};


static struct platform_device *tj70_platform_devices[] __initdata = {
//#if defined(CONFIG_I2C_GPIO)
        &tj70_i2c_gpio_device,
//#endif
};
#endif


void __init board_i2c_init(void) {
        //i2c_register_board_info(0, tj70_i2c0_devs, ARRAY_SIZE(tj70_i2c0_devs));
        //i2c_register_board_info(1, tj70_i2c1_devs, ARRAY_SIZE(tj70_i2c1_devs));
#if defined(CONFIG_I2C_GPIO)
        //i2c_register_board_info(3, tj70_gpio_i2c_devs, ARRAY_SIZE(tj70_gpio_i2c_devs));
        //platform_add_devices(tj70_platform_devices, ARRAY_SIZE(tj70_platform_devices));
#endif
}

void __init jz_board_setup(void)
{
	printk("JZ4770 Reference Board (TJ70) setup\n");
//	jz_restart(NULL);
	board_cpm_setup();
	board_gpio_setup();

	jz_timer_callback = tj70_timer_callback;
}

/**
 * Called by arch/mips/kernel/proc.c when 'cat /proc/cpuinfo'.
 * Android requires the 'Hardware:' field in cpuinfo to setup the init.%hardware%.rc.
 */
const char *get_board_type(void)
{
	return "TJ70";
}
