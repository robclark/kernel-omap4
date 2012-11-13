/*============================================================================*/
/*                             INCLUDE FILES                                  */
/*============================================================================*/
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/i2c.h>
#include <linux/delay.h>

#include "tmdlHdmiCEC_IW.h"
#include "tmNxTypes.h"
#include "tmdlHdmiCEC.h"
#include "tmdlHdmiCEC_cfg.h"

struct i2c_client *GetThisI2cClient(void);
unsigned char  my_i2c_data[255];

/*============================================================================*/
/*                          MACROS                                            */
/*============================================================================*/
#define RETIF(cond, rslt) if ((cond)){return (rslt);}
#define I2C_M_WR 0

/*============================================================================*/
/*                          FUNCTIONS DECLARATIONS                            */
/*============================================================================*/


tmErrorCode_t           I2cReadFunction(tmdlHdmiCecSysArgs_t *pSysArgs);
tmErrorCode_t           I2cWriteFunction(tmdlHdmiCecSysArgs_t *pSysArgs);


/*============================================================================*/
/*                       CONSTANTS DECLARATIONS                               */
/*============================================================================*/


#define COMMAND_TASK_PRIORITY_0  250
#define COMMAND_TASK_STACKSIZE_0 128
#define COMMAND_TASK_QUEUESIZE_0 8

/* I2C adress of the unit */
#ifdef TMFL_TDA9996
	#define UNIT_I2C_ADDRESS_0 0x60 /* I2C Address of TDA9950 */
#else
	#define UNIT_I2C_ADDRESS_0 0x34 /* I2C Address of TDA9950 */
#endif



/*============================================================================*/
/*                       VARIABLES DECLARATIONS                               */
/*============================================================================*/


tmdlHdmiCecCapabilities_t CeccapabilitiesList = {TMDL_HDMICEC_DEVICE_UNKNOWN, CEC_VERSION_1_3a};

tmdlHdmiCecDriverConfigTable_t CecdriverConfigTable[MAX_UNITS] = {
    {
    COMMAND_TASK_PRIORITY_0,
    COMMAND_TASK_STACKSIZE_0,
    COMMAND_TASK_QUEUESIZE_0,
    UNIT_I2C_ADDRESS_0,
    I2cReadFunction,
    I2cWriteFunction,
    &CeccapabilitiesList
    }
};


int blockwrite_reg(struct i2c_client *client,
		   u8 reg, u16 alength, u8 *val, u16 *out_len)
{
   int err = 0,i,initiator,receiver;
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
   for (i=0; i<alength; i++) {
      msg->buf[i+1] = val[i];
/*       printk(KERN_INFO "buf[%d]=%d val[%d]=%d\n",i+1,msg->buf[i+1],i,val[i]); */
   }
   
   err = i2c_transfer(client->adapter, msg, 1);
   udelay(50);

   if (reg==7) {
      /* CEC message */
      extern char *cec_opcode(int op);
      initiator = (msg->buf[3] >> 4) & 0x0f;
      receiver = msg->buf[3] & 0x0f;
/*       printk(KERN_INFO "reg:%d alength:%d \n",reg, alength); */
      if (alength==3) {
         printk(KERN_INFO "hdmicec:polling:[%x--->%x] \n", initiator,receiver);
      }
      else {
         printk(KERN_INFO "hdmicec:Tx:[%x--->%x] %s %02x%02x%02x%02x\n", \
                initiator,receiver,cec_opcode(msg->buf[4]),msg->buf[4],msg->buf[5],msg->buf[6],msg->buf[7]);
      }
   }
/*    dev_dbg(&client->dev, "<%s> i2c Block write at 0x%x, " */
/*            "*val=%d flags=%d byte[%d] err=%d\n", */
/*            __func__, data[0], data[1], msg->flags, i, err); */
   return (err < 0?err:0);

#if 0
   int err = 0, i;
   struct i2c_msg msg[1];
   u8 data[2];

   if (!client->adapter) {
      dev_err(&client->dev, "<%s> ERROR: No HDMI Device\n", __func__);
      return -ENODEV;
   }

   msg->addr = client->addr;
   msg->flags = I2C_M_WR;
   msg->len = 2;
   msg->buf = data;

   /* high byte goes out first */
   data[0] = reg >> 8;

   for (i = 0; i < alength - 1; i++) {
      data[1] = val[i];
      err = i2c_transfer(client->adapter, msg, 1);
      udelay(50);
      dev_dbg(&client->dev, "<%s> i2c Block write at 0x%x, "
	      "*val=%d flags=%d byte[%d] err=%d\n",
	      __func__, data[0], data[1], msg->flags, i, err);
      if (err < 0)
	 break;
   }
   /* set the number of bytes written*/
   *out_len = i;

   if (err < 0) {
      dev_err(&client->dev, "<%s> ERROR:  i2c Block Write at 0x%x, "
	      "*val=%d flags=%d bytes written=%d "
	      "err=%d\n",
	      __func__, data[0], data[1], msg->flags, i, err);
      return err;
   }
   return 0;
#endif
}

int blockread_reg(struct i2c_client *client, u16 data_length,
		  u8 reg, u16 alength, u8 *val, u16 *out_len)
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
   if (err<0) goto BLOCK_READ_OUPS;

   msg->flags = I2C_M_RD;
   msg->len = alength; 
   msg->buf = val;
   err = i2c_transfer(client->adapter, msg, 1);
   if (err<0) goto BLOCK_READ_OUPS;

/*    printk(KERN_INFO "DBG blockread_reg addr:%x len:%d buf:%02x%02x%02x%02x\n",msg->addr,msg->len,\ */
/*           msg->buf[0],msg->buf[1],msg->buf[2],msg->buf[3]); */

   return 0;
   
 BLOCK_READ_OUPS:
/*    printk(KERN_INFO "DBG blockread_reg addr:%x len:%d ERROR\n",msg->addr,msg->len); */
   dev_err(&client->dev, "<%s> ERROR:  i2c Read at 0x%x, "
           "*val=%d flags=%d bytes err=%d\n",
           __func__, reg, *val, msg->flags, err);
   return err;

#if 0
   int err = 0, i;
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

   /* High byte goes out first */
   data[0] = reg;

   for (i = 0; i < alength; i++) {
      err = i2c_transfer(client->adapter, msg, 1);
      dev_dbg(&client->dev, "<%s> i2c Block Read1 at 0x%x, "
	      "*val=%d flags=%d err=%d\n",
	      __func__, data[0], data[1], msg->flags, err);
      if (err >= 0) {
	 mdelay(3);
	 msg->flags = I2C_M_RD;
	 msg->len = data_length;
	 err = i2c_transfer(client->adapter, msg, 1);
      } else
	 break;
      if (err >= 0) {
	 val[i] = 0;
	 /* High byte comes first */
	 if (data_length == 1)
	    val[i] = data[0];
	 else if (data_length == 2)
	    val[i] = data[1] + (data[0] << 8);
	 dev_dbg(&client->dev, "<%s> i2c Block Read2 at 0x%x, "
		 "*val=%d flags=%d byte=%d "
		 "err=%d\n",
		 __func__, reg, val[i], msg->flags, i, err);
      } else
	 break;
   }
   *out_len = i;
   dev_info(&client->dev, "<%s> i2c Block Read at 0x%x, bytes read = %d\n",
	    __func__, reg, *out_len);

   if (err < 0) {
      dev_err(&client->dev, "<%s> ERROR:  i2c Read at 0x%x, "
	      "*val=%d flags=%d bytes read=%d err=%d\n",
	      __func__, reg, *val, msg->flags, i, err);
      return err;
   }
   return 0;
#endif
}


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


tmErrorCode_t I2cReadFunction (tmdlHdmiCecSysArgs_t *pSysArgs)
{
   tmErrorCode_t errCode = TM_OK;
   u16 outLenght=0;
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
      errCode = blockread_reg(GetThisI2cClient(),1, \
			      pSysArgs->firstRegister, \
			      pSysArgs->lenData, \
			      pSysArgs->pData, &outLenght);
   }
   
   /* restore default client address */
   client->addr=client_main_addr;

   return errCode;
}


tmErrorCode_t I2cWriteFunction(tmdlHdmiCecSysArgs_t *pSysArgs)
{

   tmErrorCode_t errCode = TM_OK;
   u16 outLenght=0;
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
                               pSysArgs->pData,&outLenght);
   }
   
   /* restore default client address */
   client->addr=client_main_addr;

   return errCode;

}

tmErrorCode_t tmdlHdmiTxIWWait
(
    UInt16 duration
)
{

    mdelay((unsigned long)duration);

    return(TM_OK);
}

tmErrorCode_t tmdlHdmiCecCfgGetConfig
(
    tmUnitSelect_t                 unit,
    tmdlHdmiCecDriverConfigTable_t *pConfig
)
{
    /* check if unit number is in range */
    RETIF((unit < 0) || (unit >= MAX_UNITS), TMDL_ERR_DLHDMICEC_BAD_UNIT_NUMBER)

    /* check if pointer is Null */
    RETIF(pConfig == Null, TMDL_ERR_DLHDMICEC_INCONSISTENT_PARAMS)

    *pConfig = CecdriverConfigTable[unit];

    return(TM_OK);
};
/*============================================================================*/
/*                            END OF FILE                                     */
/*============================================================================*/
