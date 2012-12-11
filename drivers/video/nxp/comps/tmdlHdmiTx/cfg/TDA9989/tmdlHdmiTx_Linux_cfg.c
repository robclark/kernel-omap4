/**
 * Copyright (C) 2009 NXP N.V., All Rights Reserved.
 * This source code and any compilation or derivative thereof is the proprietary
 * information of NXP N.V. and is confidential in nature. Under no circumstances
 * is this software to be  exposed to or placed under an Open Source License of
 * any type without the expressed written permission of NXP N.V.
 *
 * \file          tmdlHdmiTx_LinuxCfg.c
 *
 * \version       Revision: 1
 *
 * \date          Date: 25/03/11 11:00
 *
 * \brief         devlib driver component API for the TDA998x HDMI Transmitters
 *
 * \section refs  Reference Documents
 * HDMI Tx Driver - FRS.doc,
 *
 * \section info  Change Information
 *
 * \verbatim

   History:       tmdlHdmiTx_LinuxCfg.c
 *
 * *****************  Version 2  ***************** 
 * User: V. Vrignaud Date: March 25th, 2011
 *
 * *****************  Version 1  *****************
 * User: A. Lepine Date: October 1st, 2009
 * initial version
 *

   \endverbatim
 *
*/

/*============================================================================*/
/*                       INCLUDE FILES                                        */
/*============================================================================*/

/*============================================================================*/
/*                                MACRO                                       */
/*============================================================================*/
/* macro for quick error handling */
#define RETIF(cond, rslt) if ((cond)){return (rslt);}
#define I2C_M_WR 0

/*============================================================================*/
/*                   STATIC FUNCTION DECLARATIONS                             */
/*============================================================================*/
tmErrorCode_t TxI2cReadFunction(tmbslHdmiTxSysArgs_t *pSysArgs);
tmErrorCode_t TxI2cWriteFunction(tmbslHdmiTxSysArgs_t *pSysArgs);

/******************************************************************************
 ******************************************************************************
 *                 THIS PART CAN BE MODIFIED BY CUSTOMER                      *
 ******************************************************************************
 *****************************************************************************/
struct i2c_client *GetThisI2cClient(void);
unsigned char  my_i2c_data[255];

/* The following includes are used by I2C access function. If    */
/* you need to rewrite these functions for your own SW infrastructure, then   */
/* it can be removed                                                          */
#	include <linux/kernel.h>
#	include <linux/errno.h>
#	include <linux/string.h>
#	include <linux/types.h>
#	include <linux/i2c.h>
#	include <linux/delay.h>

#include <linux/gfp.h>

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vt_kern.h>
#include <asm/types.h>

/* I2C adress of the unit                                                     */
/* Put there the I2C slave adress of the Tx transmitter IC                    */
#define UNIT_I2C_ADDRESS_0 0x70

/* Intel CE 4100 I2C bus number                                               */
/* Put there the number of I2C bus handling the Rx transmitter IC             */
#define I2C_BUS_NUMBER_0 0 // initial:0

/* I2C Number of bytes in the data buffer.                                    */
#define SUB_ADDR_BYTE_COUNT_0 1

/* Priority of the command task                                               */
/* Command task is an internal task that handles incoming event from the IC   */
/* put there a value that will ensure a response time of ~20ms in your system */
#define COMMAND_TASK_PRIORITY_0  250
#define COMMAND_TASK_PRIORITY_1  250

/* Priority of the hdcp check tasks */
/* HDCP task is an internal task that handles periodical HDCP processing      */
/* put there a value that will ensure a response time of ~20ms in your system */
#define HDCP_CHECK_TASK_PRIORITY_0  250

/* Stack size of the command tasks */
/* This value depends of the type of CPU used, and also from the length of    */
/* the customer callbacks. Increase this value if you are making a lot of     */
/* processing (function calls & local variables) and that you experience      */
/* stack overflows                                                            */
#define COMMAND_TASK_STACKSIZE_0 128
#define COMMAND_TASK_STACKSIZE_1 128

/* stack size of the hdcp check tasks */
/* This value depends of the type of CPU used, default value should be enough */
/* for all configuration                                                      */
#define HDCP_CHECK_TASK_STACKSIZE_0 128

/* Size of the message queues for command tasks                               */
/* This value defines the size of the message queue used to link the          */
/* the tmdlHdmiTxHandleInterrupt function and the command task. The default   */
/* value below should fit any configuration                                   */
#define COMMAND_TASK_QUEUESIZE_0 128
#define COMMAND_TASK_QUEUESIZE_1 128

/* HDCP key seed                                                              */
/* HDCP key are stored encrypted into the IC, this value allows the IC to     */
/* decrypt them. This value is provided to the customer by NXP customer       */
/* support team.                                                              */
#define KEY_SEED 0x1234

/* Video port configuration for YUV444 input                                  */
/* You can specify in this table how are connected video ports in case of     */
/* YUV444 input signal. Each line of the array corresponds to a quartet of    */
/* pins of one video port (see comment on the left to identify them). Just    */
/* change the enum to specify which signal you connected to it. See file      */
/* tmdlHdmiTx_cfg.h to get the list of possible values                        */
const tmdlHdmiTxCfgVideoSignal444 videoPortMapping_YUV444[MAX_UNITS][6] = {
    {
        TMDL_HDMITX_VID444_BU_0_TO_3,   /* Signals connected to VPA[0..3] */
        TMDL_HDMITX_VID444_BU_4_TO_7,   /* Signals connected to VPA[4..7] */
        TMDL_HDMITX_VID444_GY_0_TO_3,   /* Signals connected to VPB[0..3] */
        TMDL_HDMITX_VID444_GY_4_TO_7,   /* Signals connected to VPB[4..7] */
        TMDL_HDMITX_VID444_VR_0_TO_3,   /* Signals connected to VPC[0..3] */
        TMDL_HDMITX_VID444_VR_4_TO_7    /* Signals connected to VPC[4..7] */
    }
};

/* Video port configuration for RGB444 input                                  */
/* You can specify in this table how are connected video ports in case of     */
/* RGB444 input signal. Each line of the array corresponds to a quartet of    */
/* pins of one video port (see comment on the left to identify them). Just    */
/* change the enum to specify which signal you connected to it. See file      */
/* tmdlHdmiTx_cfg.h to get the list of possible values                        */
const tmdlHdmiTxCfgVideoSignal444 videoPortMapping_RGB444[MAX_UNITS][6] = {
    {
        TMDL_HDMITX_VID444_GY_0_TO_3,   /* Signals connected to VPA[0..3] */
        TMDL_HDMITX_VID444_GY_4_TO_7,   /* Signals connected to VPA[4..7] */
        TMDL_HDMITX_VID444_BU_0_TO_3,   /* Signals connected to VPC[0..3] */
        TMDL_HDMITX_VID444_BU_4_TO_7,   /* Signals connected to VPC[4..7] */
        TMDL_HDMITX_VID444_VR_0_TO_3,   /* Signals connected to VPB[0..3] */
        TMDL_HDMITX_VID444_VR_4_TO_7    /* Signals connected to VPB[4..7] */
    }
};

/* Video port configuration for YUV422 input                                  */
/* You can specify in this table how are connected video ports in case of     */
/* YUV422 input signal. Each line of the array corresponds to a quartet of    */
/* pins of one video port (see comment on the left to identify them). Just    */
/* change the enum to specify which signal you connected to it. See file      */
/* tmdlHdmiTx_cfg.h to get the list of possible values                        */
const tmdlHdmiTxCfgVideoSignal422 videoPortMapping_YUV422[MAX_UNITS][6] = {
    {
        TMDL_HDMITX_VID422_Y_4_TO_7,           /* Signals connected to VPA[0..3] */    
        TMDL_HDMITX_VID422_Y_8_TO_11,          /* Signals connected to VPA[4..7] */    
        TMDL_HDMITX_VID422_UV_4_TO_7,          /* Signals connected to VPB[0..3] */
        TMDL_HDMITX_VID422_UV_8_TO_11,         /* Signals connected to VPB[4..7] */
        TMDL_HDMITX_VID422_NOT_CONNECTED,      /* Signals connected to VPC[0..3] */
        TMDL_HDMITX_VID422_NOT_CONNECTED       /* Signals connected to VPC[4..7] */
    }
};

/* Video port configuration for CCIR656 input                                 */
/* You can specify in this table how are connected video ports in case of     */
/* CCIR656 input signal. Each line of the array corresponds to a quartet of   */
/* pins of one video port (see comment on the left to identify them). Just    */
/* change the enum to specify which signal you connected to it. See file      */
/* tmdlHdmiTx_cfg.h to get the list of possible values                        */
const tmdlHdmiTxCfgVideoSignalCCIR656 videoPortMapping_CCIR656[MAX_UNITS][6] = {
    {
        TMDL_HDMITX_VIDCCIR_4_TO_7,         /* Signals connected to VPA[0..3] */
        TMDL_HDMITX_VIDCCIR_8_TO_11,        /* Signals connected to VPA[4..7] */
        TMDL_HDMITX_VIDCCIR_NOT_CONNECTED,  /* Signals connected to VPB[0..3] */
        TMDL_HDMITX_VIDCCIR_NOT_CONNECTED,  /* Signals connected to VPB[4..7] */
        TMDL_HDMITX_VIDCCIR_NOT_CONNECTED,  /* Signals connected to VPC[0..3] */
        TMDL_HDMITX_VIDCCIR_NOT_CONNECTED   /* Signals connected to VPC[4..7] */
    }
}; 

/*
 *
 * Linux wrapping starts here...............................
 *
 */ 
/* 
 *  Write a bloc to a register in Tx device.
 */
int blockwrite_reg(struct i2c_client *client, u8 reg, u16 alength, u8 *val)
{
   int err = 0;   
   int i;
   struct i2c_msg msg[1];
    
   if (!client->adapter) {
      dev_err(&client->dev, "<%s> ERROR: No HDMI Device\n", __func__);
      return -ENODEV;
   }
   
   msg->addr = client->addr;
   msg->flags = I2C_M_WR;
   msg->len = alength+1;
   msg->buf = my_i2c_data;
   
   msg->buf[0] = reg;   
   for (i=1; i<=alength; i++) msg->buf[i] = (*val++);
   
   err = i2c_transfer(client->adapter, msg, 1);
   udelay(50);


/*    printk(KERN_INFO "DBG blockwrite_reg addr:%x reg:%d data:%x %s\n",msg->addr,reg,val,(err<0?"ERROR":"")); */

/*    dev_dbg(&client->dev, "<%s> i2c Block write at 0x%x, " */
/*            "*val=%d flags=%d byte[%d] err=%d\n", */
/*            __func__, data[0], data[1], msg->flags, i, err); */

   return (err < 0?err:0);
}

/* 
 *  Read a bloc to a register in Tx device.
 */
int blockread_reg(struct i2c_client *client, u8 reg, u16 alength, u8 *val)
{
   int err = 0;
   struct i2c_msg msg[1];
   u8 data[2];

   if (!client->adapter) {
      dev_err(&client->dev, "<%s> ERROR: No HDMI Device\n", __func__);
      return -ENODEV;
   }

   msg->addr = client->addr;
   msg->flags = I2C_M_WR;
   msg->len = 1;
   msg->buf = data;
   data[0] = reg; /* High byte goes out first */
   err = i2c_transfer(client->adapter, msg, 1);
/*    printk(KERN_INFO "DBG blockread_reg #1 addr:%x len:%d buf:%02x%02x%02x%02x %s\n",msg->addr,msg->len,\ */
/*           msg->buf[0],msg->buf[1],msg->buf[2],msg->buf[3],(err<0?"ERROR":"")); */
   if (err<0) goto BLOCK_READ_OUPS;

   msg->flags = I2C_M_RD;
   msg->len = alength; 
   msg->buf = val;
   err = i2c_transfer(client->adapter, msg, 1);
/*    printk(KERN_INFO "DBG blockread_reg #2 addr:%x len:%d buf:%02x%02x%02x%02x %s\n",msg->addr,msg->len,\ */
/*           msg->buf[0],msg->buf[1],msg->buf[2],msg->buf[3],(err<0?"ERROR":"")); */

   if (err<0) goto BLOCK_READ_OUPS;

   return 0;
   
 BLOCK_READ_OUPS:
   dev_err(&client->dev, "<%s> ERROR:  i2c Read at 0x%x, "
           "*val=%d flags=%d bytes err=%d\n",
           __func__, reg, *val, msg->flags, err);
		   
   return err;
}

/* 
 *  Write a byte to a register in Tx device.
 */
int write_reg(struct i2c_client *client, u8 reg, u8 val)
{
   int err = 0;
   struct i2c_msg msg[1];
   u8 data[2];
   int retries = 0;   

   if (!client->adapter) {
      dev_err(&client->dev, "<%s> ERROR: No HDMI Device\n", __func__);
      return -ENODEV;
   }

 retry:
   msg->addr = client->addr;
   msg->flags = I2C_M_WR;
   msg->len = 2;
   msg->buf = data;

   data[0] = reg;
   data[1] = val;

   err = i2c_transfer(client->adapter, msg, 1);
   dev_dbg(&client->dev, "<%s> i2c write at=%x "
	   "val=%x flags=%d err=%d\n",
	   __func__, data[0], data[1], msg->flags, err);
   udelay(50);

/*    printk(KERN_INFO "DBG write_reg addr:%x reg:%d data:%x %s\n",msg->addr,reg,val,(err<0?"ERROR":"")); */

   if (err >= 0)
      return 0;

   dev_err(&client->dev, "<%s> ERROR: i2c write at=%x "
	   "val=%x flags=%d err=%d\n",
	   __func__, data[0], data[1], msg->flags, err);
   if (retries <= 5) {
      dev_info(&client->dev, "Retrying I2C... %d\n", retries);
      retries++;
      set_current_state(TASK_UNINTERRUPTIBLE);
      schedule_timeout(msecs_to_jiffies(20));
      goto retry;
   }
   
   return err;
}

/*
 *  Read a byte from a register in Tx device.
 */
int read_reg(struct i2c_client *client, u16 data_length, u8 reg, u8 *val)
{
   int err = 0;  
   struct i2c_msg msg[1];
   u8 data[2];

   if (!client->adapter) {
      dev_err(&client->dev, "<%s> ERROR: No HDMI Device\n", __func__);
      return -ENODEV;
   }

   msg->addr = client->addr;
   msg->flags = I2C_M_WR;
   msg->len = 1;
   msg->buf = data;

   data[0] = reg;
   err = i2c_transfer(client->adapter, msg, 1);
   dev_dbg(&client->dev, "<%s> i2c Read1 reg=%x val=%d "
	   "flags=%d err=%d\n",
	   __func__, reg, data[1], msg->flags, err);

   if (err >= 0) {
      mdelay(3);
      msg->flags = I2C_M_RD;
      msg->len = data_length;
      err = i2c_transfer(client->adapter, msg, 1);
   }

   if (err >= 0) {
      *val = 0;
      if (data_length == 1)
	 *val = data[0];
      else if (data_length == 2)
	 *val = data[1] + (data[0] << 8);
      dev_dbg(&client->dev, "<%s> i2c Read2 at 0x%x, *val=%d "
	      "flags=%d err=%d\n",
	      __func__, reg, *val, msg->flags, err);
      return 0;
   }

   dev_err(&client->dev, "<%s> ERROR: i2c Read at 0x%x, "
	   "*val=%d flags=%d err=%d\n",
	   __func__, reg, *val, msg->flags, err);

   return err;   
}
/*
 *
 * Linux wrapping end...............................
 *
 */

/* The following function must be rewritten by the customer to fit its own    */
/* SW infrastructure. This function allows reading through I2C bus.           */
/* tmbslHdmiTxSysArgs_t definition is located into tmbslHdmiTx_type.h file.   */
tmErrorCode_t TxI2cReadFunction(tmbslHdmiTxSysArgs_t *pSysArgs)
{
   tmErrorCode_t errCode = TM_OK;
   struct i2c_client *client=GetThisI2cClient();
   u32 client_main_addr=client->addr;

   /* DevLib needs address control, so let it be */ 
   client->addr=pSysArgs->slaveAddr;

   if (pSysArgs->lenData == 1) {
      /* single byte */
      errCode = read_reg(GetThisI2cClient(),1,pSysArgs->firstRegister,pSysArgs->pData);
   }
   else {
      /* block */
      errCode = blockread_reg(GetThisI2cClient(), \
			      pSysArgs->firstRegister, \
			      pSysArgs->lenData, \
			      pSysArgs->pData);
   }
   
   /* restore default client address */
   client->addr=client_main_addr;

    return errCode;
}

/* The following function must be rewritten by the customer to fit its own    */
/* SW infrastructure. This function allows writing through I2C bus.           */
/* tmbslHdmiTxSysArgs_t definition is located into tmbslHdmiTx_type.h file.   */
tmErrorCode_t TxI2cWriteFunction(tmbslHdmiTxSysArgs_t *pSysArgs)
{
   tmErrorCode_t  errCode = TM_OK;
   struct i2c_client *client=GetThisI2cClient();
   u32 client_main_addr=client->addr;

   /* DevLib needs address control, so let it be */ 
   client->addr=pSysArgs->slaveAddr;
   
   if (pSysArgs->lenData == 1) {
      /* single byte */
      errCode = write_reg(GetThisI2cClient(),pSysArgs->firstRegister,*pSysArgs->pData);
   }
   else {
      /* block */
      errCode = blockwrite_reg(GetThisI2cClient(),  \
                               pSysArgs->firstRegister, \
                               pSysArgs->lenData,       \
                               pSysArgs->pData);
   }
   
   /* restore default client address */
   client->addr=client_main_addr;

    return errCode;
}


/******************************************************************************
    \brief  This function blocks the current task for the specified amount time. 
            This is a passive wait.

    \param  Duration    Duration of the task blocking in milliseconds.

    \return The call result:
            - TM_OK: the call was successful
            - TMDL_ERR_DLHDMITX_NO_RESOURCES: the resource is not available

******************************************************************************/
tmErrorCode_t tmdlHdmiTxIWWait
(
    UInt16 duration
)
{
	mdelay((unsigned long)duration);

    return(TM_OK);
}

/******************************************************************************
    \brief  This function creates a semaphore.

    \param  pHandle Pointer to the handle buffer.

    \return The call result:
            - TM_OK: the call was successful
            - TMDL_ERR_DLHDMITX_NO_RESOURCES: the resource is not available
            - TMDL_ERR_DLHDMITX_INCONSISTENT_PARAMS: an input parameter is
              inconsistent

******************************************************************************/
tmErrorCode_t tmdlHdmiTxIWSemaphoreCreate
(
    tmdlHdmiTxIWSemHandle_t *pHandle
)
{
    struct semaphore * mutex;
    
    /* check that input pointer is not NULL */
    RETIF(pHandle == Null, TMDL_ERR_DLHDMITX_INCONSISTENT_PARAMS)

    mutex = (struct semaphore *)kmalloc(sizeof(struct semaphore),GFP_KERNEL);
    if (!mutex) {
       printk(KERN_ERR "malloc failed in %s\n",__func__);
       return TMDL_ERR_DLHDMITX_NO_RESOURCES;
    }
    
    sema_init(mutex, 1);
    *pHandle = (tmdlHdmiTxIWSemHandle_t)mutex;

    RETIF(pHandle == NULL, TMDL_ERR_DLHDMITX_NO_RESOURCES)

    return(TM_OK);
}

/******************************************************************************
    \brief  This function destroys an existing semaphore.

    \param  Handle  Handle of the semaphore to be destroyed.

    \return The call result:
            - TM_OK: the call was successful
            - TMDL_ERR_DLHDMITX_BAD_HANDLE: the handle number is wrong

******************************************************************************/
tmErrorCode_t tmdlHdmiTxIWSemaphoreDestroy
(
    tmdlHdmiTxIWSemHandle_t handle
)
{
   RETIF(handle == False, TMDL_ERR_DLHDMITX_BAD_HANDLE);
   
   if (atomic_read((atomic_t*)&((struct semaphore *)handle)->count) < 1) {
      printk(KERN_ERR "release catched semaphore");
   }
   
   kfree((void*)handle);
   
   return(TM_OK);
}

/******************************************************************************
    \brief  This function acquires the specified semaphore.

    \param  Handle  Handle of the semaphore to be acquired.

    \return The call result:
            - TM_OK: the call was successful
            - TMDL_ERR_DLHDMITX_BAD_HANDLE: the handle number is wrong

******************************************************************************/
tmErrorCode_t tmdlHdmiTxIWSemaphoreP
(
    tmdlHdmiTxIWSemHandle_t handle
)
{
    down((struct semaphore *)handle);

    return(TM_OK);
}

/******************************************************************************
    \brief  This function releases the specified semaphore.

    \param  Handle  Handle of the semaphore to be released.

    \return The call result:
            - TM_OK: the call was successful
            - TMDL_ERR_DLHDMITX_BAD_HANDLE: the handle number is wrong

******************************************************************************/
tmErrorCode_t tmdlHdmiTxIWSemaphoreV
(
    tmdlHdmiTxIWSemHandle_t handle
)
{
    up((struct semaphore *)handle);

    return(TM_OK);
}

/*============================================================================*/
/*                            END OF FILE                                     */
/*============================================================================*/

