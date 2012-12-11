/*****************************************************************************/
/* Copyright (c) 2009 NXP Semiconductors BV                                  */
/*                                                                           */
/* This program is free software; you can redistribute it and/or modify      */
/* it under the terms of the GNU General Public License as published by      */
/* the Free Software Foundation, using version 2 of the License.             */
/*                                                                           */
/* This program is distributed in the hope that it will be useful,           */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of            */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the              */
/* GNU General Public License for more details.                              */
/*                                                                           */
/* You should have received a copy of the GNU General Public License         */
/* along with this program; if not, write to the Free Software               */
/* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307       */
/* USA.                                                                      */
/*                                                                           */
/*****************************************************************************/

#ifndef __cec_h__
#define __cec_h__

#include "tda998x_ioctl.h"

#define HDMICEC_NAME "hdmicec"

#define CEC_MAJOR 234 /* old-style interval of device numbers */
#define MAX_MINOR 1 /* 1 minor but 2 access : 1 more for pooling */

/* common I2C define with kernel */
/* should be the same as arch/arm/mach-omap2/board-zoom2.c */
#define CEC_NAME "tda99Xcec"
#define TDA99XCEC_I2C_SLAVEADDRESS 0x34

#define TDA_IRQ_CALIB 107
#define POLLING_LENGTH 3

#define EDID_BLOCK_COUNT    4
#define EDID_BLOCK_SIZE     128

#ifdef GPL
#define CHECK_EVERY_XX_MS 500 /* ms */
#else
#define CHECK_EVERY_XX_MS 10 /* ms */
#endif

#define LOG(type,fmt,args...) {if (this->param.verbose) {printk(type HDMICEC_NAME":%s:" fmt, __func__, ## args);}}
/* not found the kernel "strerror" one! If someone knows, please replace it */
#define ERR_TO_STR(e)((e == -ENODATA)?"ENODATA, no data available":\
                      (e == -ENOMEM)? "ENOMEM, no memory available":\
                      (e == -EINVAL)? "EINVAL, invalid argument":\
                      (e == -EIO)? "EIO, input/output error":\
                      (e == -ETIMEDOUT)? "ETIMEOUT, timeout has expired":\
                      (e == -EBUSY)? "EBUSY, device or resource busy":\
                      (e == -ENOENT)? "ENOENT, no such file or directory":\
                      (e == -EACCES)? "EACCES, permission denied":\
                      (e == 0)?       "":\
                      "!UNKNOWN!")

#define TRY(fct) { \
      err=(fct); \
      if (err) { \
	 printk(KERN_ERR "%s? in %s line %d\n",hdmi_cec_err_string(err),__func__,__LINE__); \
	 goto TRY_DONE; \
      } \
   }

typedef void (*cec_callback_t) (struct work_struct *dummy);

typedef struct {
   /* module params */
   struct { 
      int verbose;
      int major;
      int minor;
   } param;
   /* driver */
   struct {
      struct class *class;
      struct device *dev;
      int devno;
      struct i2c_client *i2c_client;
      struct semaphore sem;
      int user_counter;
      int minor;
      wait_queue_head_t wait;
      bool poll_done;
      int deinit_req;
      struct timer_list timer;
   } driver;
   /* cec */
   struct {
      int inst;
      unsigned char rx_addr;
      unsigned short phy_addr;
      unsigned char initiator;
      cec_version version;
      cec_sw_version sw_version;
      cec_power power;
      cec_setup setup;
      cec_clock clock;
      cec_analogue_timer analog_timer;
      cec_digital_timer digital_timer;
      cec_ext_timer_with_ext_plug etwep;
      cec_ext_timer_with_phy_addr etwpa;
      cec_deck_ctrl deck_ctrl;
      cec_deck_status deck_status;
      unsigned long vendor_id;
      cec_feature_abort feature_abort;
      cec_status_request satus_request;
      cec_menu_request menu_request;
      cec_menu_status menu_status;
      cec_play play;
      cec_analogue_service analog_service;
      cec_digital_service digital_service;
      cec_ext_plug ext_plug;
      cec_rec_status rec_status;
      cec_audio_status audio_status;
      cec_device_type device_type;
      cec_power_status source_status;
      cec_audio_rate audio_rate;
      cec_sys_audio_status sys_audio_status;
      cec_timer_cleared_status timer_cleared_status;
      cec_timer_status timer_status;
      cec_tuner_device_status_analogue tdsa;
      cec_tuner_device_status_digital tdsd;
      cec_user_ctrl user_ctrl;
      unsigned char select;
      cec_user_ctrl_tune user_ctrl_tune;
      cec_frame frame;
      bool byebye;
      cec_string string;
      cec_string osd_name;
      cec_osd_string osd_string;
      cec_vendor_command_with_id vcwi;
   } cec;
} cec_instance;

#endif /* __cec_h__ */

