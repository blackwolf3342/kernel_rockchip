/*
 * rk1000_tv.c 
 *
 * Driver for rockchip rk1000 tv control
 *  Copyright (C) 2009 
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *
 */
#include <linux/module.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/fb.h>
#include <linux/console.h>
#include <asm/uaccess.h>
#include "rk1000_tv.h"

#define DRV_NAME "rk1000_tvout"
#define RK1000_I2C_RATE     100*1000

struct rk1000 rk1000;

#define VIDEO_SWITCH_CVBS		GPIO_LOW
#define VIDEO_SWITCH_OTHER		GPIO_HIGH

#ifdef CONFIG_ARCH_RK29
extern int FB_Switch_Screen( struct rk29fb_screen *screen, u32 enable );
#else
#include <linux/rk_fb.h>
static int FB_Switch_Screen( struct rk29fb_screen *screen, u32 enable )
{
	return rk_fb_switch_screen(screen, enable , rk1000.video_source);
}
#endif

int rk1000_tv_control_set_reg(struct i2c_client *client, u8 reg, u8 const buf[], u8 len)
{
    int ret;

    ret = i2c_master_reg8_send(client,reg,buf,len,RK1000_I2C_RATE);
	if (ret > 0)
		ret = 0;
	return ret;
}

int rk1000_tv_write_block(u8 addr, u8 *buf, u8 len)
{
	int i;
	int ret = 0;
	
	if(rk1000.client == NULL){
		printk("rk1000_tv_i2c_client not init!\n");
		return -1;
	}

	for(i = 0; i < len; i++){
		ret = rk1000_tv_control_set_reg(rk1000.client, addr+i, buf+i, 1);
		if(ret != 0){
			printk("rk1000_tv_control_set_reg err, addr=0x%.x, val=0x%.x", addr+i, buf[i]);
			break;
		}
	}
	return ret;
}

int rk1000_switch_fb(const struct fb_videomode *modedb, int tv_mode)
{	
	struct rk29fb_screen *screen;
	
	if(modedb == NULL)
		return -1;
	screen =  kzalloc(sizeof(struct rk29fb_screen), GFP_KERNEL);
	if(screen == NULL)
		return -1;
	
	memset(screen, 0, sizeof(struct rk29fb_screen));	
	/* screen type & face */
    screen->type = SCREEN_HDMI;
    screen->face = OUT_P888;
	
	/* Screen size */
	screen->x_res = modedb->xres;
    screen->y_res = modedb->yres;
//	screen->xpos = 0;
//    screen->ypos = 0;
    /* Timing */
    screen->pixclock = modedb->pixclock;

	screen->lcdc_aclk = 500000000;
	screen->left_margin = modedb->left_margin;
	screen->right_margin = modedb->right_margin;
	screen->hsync_len = modedb->hsync_len;
	screen->upper_margin = modedb->upper_margin;
	screen->lower_margin = modedb->lower_margin;
	screen->vsync_len = modedb->vsync_len;

	/* Pin polarity */
	if(FB_SYNC_HOR_HIGH_ACT & modedb->sync)
		screen->pin_hsync = 1;
	else
		screen->pin_hsync = 0;
	if(FB_SYNC_VERT_HIGH_ACT & modedb->sync)
		screen->pin_vsync = 1;
	else
		screen->pin_vsync = 0;	
	screen->pin_den = 0;
	screen->pin_dclk = 1;

	/* Swap rule */
    screen->swap_rb = 0;
    screen->swap_rg = 0;
    screen->swap_gb = 0;
    screen->swap_delta = 0;
    screen->swap_dumy = 0;

    /* Operation function*/
    screen->init = NULL;
    screen->standby = NULL;	
    
    switch(tv_mode)
   	{
#ifdef CONFIG_RK1000_TVOUT_CVBS
   		case TVOUT_CVBS_NTSC:{
            screen->init = rk1000_tv_ntsc_init;;
		}
		break;
        
		case TVOUT_CVBS_PAL:{
            screen->init = rk1000_tv_pal_init;
		}
		break;
#endif

#ifdef CONFIG_RK1000_TVOUT_YPbPr
		case TVOUT_YPbPr_720x480p_60:{
            screen->init = rk1000_tv_Ypbpr480_init;
		}
		break;
        
		case TVOUT_YPbPr_720x576p_50:{
			screen->init = rk1000_tv_Ypbpr576_init;
		}
		break;
        
		case TVOUT_YPbPr_1280x720p_50:{
            screen->init = rk1000_tv_Ypbpr720_50_init;
		}
		break;
		
		case TVOUT_YPbPr_1280x720p_60:
			screen->init = rk1000_tv_Ypbpr720_60_init;
			break;
#endif        
		default:{
			kfree(screen);
			return -1;
   		}
   		break;
   	}
   	FB_Switch_Screen(screen, 1);
   	rk1000.mode = tv_mode;
   	kfree(screen);
   	
   	if(rk1000.io_switch_pin != INVALID_GPIO) {
   		if(tv_mode < TVOUT_YPbPr_720x480p_60)
   			gpio_direction_output(rk1000.io_switch_pin, VIDEO_SWITCH_CVBS);
   		else
   			gpio_direction_output(rk1000.io_switch_pin, VIDEO_SWITCH_OTHER);
   	}
	return 0;
}

int rk1000_tv_standby(int type)
{
	unsigned char val;
	int ret;
	int ypbpr = 0, cvbs = 0;
	
	if(rk1000.ypbpr)
		ypbpr = rk1000.ypbpr->enable;
	if(rk1000.cvbs)
		cvbs = rk1000.cvbs->enable;
	if(cvbs || ypbpr)
		return 0;
	
	val = 0x00;	
	ret = rk1000_control_write_block(0x03, &val, 1);
	if(ret < 0){
		printk("rk1000_control_write_block err!\n");
		return ret;
	}
	
	val = 0x07;
	ret = rk1000_tv_write_block(0x03, &val, 1);
	if(ret < 0){
		printk("rk1000_tv_write_block err!\n");
		return ret;
	}
	return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void rk1000_early_suspend(struct early_suspend *h)
{
	printk("rk1000 enter early suspend\n");
	if(rk1000.ypbpr) {
		rk1000.ypbpr->ddev->ops->setenable(rk1000.ypbpr->ddev, 0);
		rk1000.ypbpr->suspend = 1;
	}
	if(rk1000.cvbs) {
		rk1000.cvbs->ddev->ops->setenable(rk1000.cvbs->ddev, 0);
		rk1000.cvbs->suspend = 1;
	}
	return;
}

static void rk1000_early_resume(struct early_suspend *h)
{
	printk("rk1000 exit early resume\n");
	if( rk1000.cvbs) {
		rk1000.cvbs->suspend = 0;
		if(rk1000.mode < TVOUT_YPbPr_720x480p_60)
			rk_display_device_enable((rk1000.cvbs)->ddev);
	}
	else if( rk1000.ypbpr ) {
		rk1000.ypbpr->suspend = 0;
		if(rk1000.mode > TVOUT_CVBS_PAL)
			rk_display_device_enable((rk1000.ypbpr)->ddev);
	}
	return;
}
#endif

static int rk1000_tv_control_probe(struct i2c_client *client,const struct i2c_device_id *id)
{
	int rc = 0;
	u8 buff;
	struct rkdisplay_platform_data *tv_data;
	struct rk29fb_screen screen;
	
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		rc = -ENODEV;
		goto failout;
	}
	memset(&rk1000, 0, sizeof(struct rk1000));
	rk1000.io_switch_pin = INVALID_GPIO;
	rk1000.client = client;
	if(client->dev.platform_data) {
		tv_data = client->dev.platform_data;
		rk1000.video_source = tv_data->video_source;
		rk1000.property = tv_data->property;
	}
	else {
		rk1000.video_source = DISPLAY_SOURCE_LCDC0;
		rk1000.property = DISPLAY_MAIN;
	}
	if(rk1000.io_switch_pin != INVALID_GPIO) {
		rc = gpio_request(rk1000.io_switch_pin, NULL);
		if(rc) {
			gpio_free(rk1000.io_switch_pin);
			printk(KERN_ERR "rk1000 request video switch gpio error\n");
			return -1;
		}
	}
	rk1000.mode = RK1000_TVOUT_DEAULT;
	
	// RK1000 I2C Reg need dclk, so we open lcdc.
	memset(&screen, 0, sizeof(struct rk29fb_screen));
	set_lcd_info(&screen, NULL);
	FB_Switch_Screen(&screen, 2);
	//Power down RK1000 output DAC.
    buff = 0x07;  
    rc = rk1000_tv_control_set_reg(client, 0x03, &buff, 1);
    if(rc)
    {
    	dev_err(&client->dev, "rk1000_tv_control probe error %d\n", rc);
    	return -EINVAL;
    }
    
#ifdef CONFIG_RK1000_TVOUT_YPbPr
	rk1000_register_display_YPbPr(&client->dev);
#endif

#ifdef CONFIG_RK1000_TVOUT_CVBS
	rk1000_register_display_cvbs(&client->dev);
#endif
	#ifdef CONFIG_HAS_EARLYSUSPEND
	rk1000.early_suspend.suspend = rk1000_early_suspend;
	rk1000.early_suspend.resume = rk1000_early_resume;
	rk1000.early_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB - 11;
	register_early_suspend(&(rk1000.early_suspend));
	#endif
	
    printk(KERN_INFO "rk1000_tv ver 2.0 probe ok\n");
    return 0;
failout:
	kfree(client);
	return rc;
}

static int rk1000_tv_control_remove(struct i2c_client *client)
{
	return 0;
}

static int rk1000_tv_control_shutdown(struct i2c_client *client)
{
	int buffer = 0x07;
    rk1000_tv_control_set_reg(client, 0x03, &buffer, 1);
    return 0;
}

static const struct i2c_device_id rk1000_tv_control_id[] = {
	{ DRV_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, rk1000_control_id);

static struct i2c_driver rk1000_tv_control_driver = {
	.driver 	= {
		.name	= DRV_NAME,
	},
	.id_table = rk1000_tv_control_id,
	.probe = rk1000_tv_control_probe,
	.remove = rk1000_tv_control_remove,
	.shutdown = rk1000_tv_control_shutdown,
};

static int __init rk1000_tv_init(void)
{
	int ret = 0;
	ret = i2c_add_driver(&rk1000_tv_control_driver);
	if(ret < 0){
		printk("i2c_add_driver err, ret = %d\n", ret);
	}
	return ret;
}

static void __exit rk1000_tv_exit(void)
{
    i2c_del_driver(&rk1000_tv_control_driver);
}

module_init(rk1000_tv_init);
module_exit(rk1000_tv_exit);

/* Module information */
MODULE_DESCRIPTION("ROCKCHIP rk1000 tv ");
MODULE_LICENSE("GPL");


