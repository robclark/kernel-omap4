/* *
 *        Filename:  tda_demo.c
 * 
 *     Description:  bench and stress of CEC
 *                   features for TDA19989
 */
/*         Created:  2009-10-20
 * 
 *          Author:  Andre Lepine
 *         Company:  NXP Semiconductors Caen
 * 
 */
#define TDA_DEMO_VERSION "v0.1"

/* linux */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <assert.h>
#include <signal.h>
#include <string.h>
#include <pthread.h>
#include "tmdlHdmiTx_Types.h"
#include "tmdlHdmiCEC_Types.h"
#include "tda998x.h"
#include "tda998x_ioctl.h"
/* #include "tda998x_cec.h" */


/*
 *
 * Definitions
 * -----------
 * CHAPTER 0
 *
 */

#define USERCHECK(x,y) {if (user_request & (x)) {y;}} 
#define _MY_IOCTL(fd,prefix,io,param) {if (ioctl(fd,prefix##io, param) == -1) {oups("ioctl failed",prefix##io);}}
#define IO_CEC(io,param) _MY_IOCTL(cec,CEC_IOCTL_,io,param)
#define IO_TX(io,param) _MY_IOCTL(tx,TDA_IOCTL_,io,param)
#define IO_RX(io,param) _MY_IOCTL(rx,CEC_IOCTL_,io,param)
#define CEC_DEV "/dev/hdmicec"
#define TX_DEV "/dev/hdmitx"

typedef struct {
   unsigned char received;
   unsigned char service;
   unsigned char addr;
   unsigned char data[15];
} rx_frame;

unsigned int user_request,user_wait;
const char cec_name[]=CEC_DEV;
const char tx_name[]=TX_DEV;
int rx=0,tx=0,cec=0;
pthread_t rx_thread;
unsigned short phy_addr;
cec_string osd_name = {{78,88,80},3}; /* NXP */
tmdlHdmiCECDeviceType_t device_type = CEC_DEVICE_TYPE_PLAYBACK_DEVICE;

/*
 *
 * Internals
 * ---------
 * CHAPTER 1
 *
 */

char *cec_service(int service)
{
   switch (service)
      {
      case CEC_WAITING: {return "waiting";break;}
      case CEC_RELEASE: {return "release";break;}
      case CEC_RX_DONE: {return "new message";break;}
      case CEC_TX_DONE: {return "one message sent";break;}
      default : {return "unknown";break;}
      }
}

/* 
 * syntax for dumies
 */
void print_usage(char *exename)
{	
   printf("Usage: %s [test_numer] [loop]\n",exename);
   printf("0x001: driver open/close\n");
   printf("0x002: ...\n");
}

/*
 * We did it !
 */
void my_exit(int signum) {
   
   printf("bye\n");
   
   /*
     .last ioctl for releasing
     .munmap if needed
     .free 
   */

   if (tx) close(tx);
   if (cec) {
      ioctl(cec,CEC_IOCTL_BYEBYE, NULL);
      close(cec);
   }
   if (rx) close(rx);

   _exit(signum);
}

/*
 * Failure
 */
void oups(char *s,int io) {

   if (io) {
      printf("%s (%d)\noups...\n",s,io);
   }
   else {
   }

   /*
     .specific ioctl for releasing
   */

   if (tx) close(tx);
   if (cec) {
      ioctl(cec,CEC_IOCTL_BYEBYE, NULL);
      close(cec);
   }
   if (rx) close(rx);

   my_exit(EXIT_FAILURE);
}


/*
 *
 * Methods
 * -------
 * CHAPTER 2
 *
 */

/*
  do some event control here...
*/
void read_frame(cec_frame *frame) {
 
   unsigned char initiator, receiver;
   int param1,param2,param3;
   UInt32 vendor_ID;
   UInt32 vendor_CmdID0, vendor_CmdID1, vendor_CmdID2;
   char language[3] = {0x65,0x6e,0x67}; /* eng */
   tmdlHdmiCECAudioStatus_t audio;
   tmdlHdmiCecInstanceSetup_t setup;
   cec_feature_abort fa;
   int i;

   printf("CEC module says:%s\n",cec_service(frame->service));

   /*Give receive data from CEC bus*/
   if (frame->service == CEC_RX_DONE) {

      //initiator and receiver
      initiator = (frame->addr >> 4) & 0x0f;
      receiver = frame->addr & 0x0f;
      printf("[%x]->[%x] data:%02x%02x%02x%02x...\n",        \
             initiator,                                          \
             receiver,                                           \
             frame->data[0],                                        \
             frame->data[1],                                        \
             frame->data[2],                                   \
             frame->data[3]);
      

      // Particular case of Polling Message//
      if (frame->count == 0x03)
         {
         }
      else
         {
            switch(frame->data[0])
               {
                  // Standby
               case CEC_OPCODE_STANDBY :
#ifdef CEC_PW_MGT
                  IO_TX(SET_POWER,tmPowerStandby);
#endif 
                  break;

                  // Set Menu Language
               case CEC_OPCODE_GET_MENU_LANGUAGE :
                  IO_CEC(SET_MENU_LANGUAGE,language);
                  break;
			 
                  // Set Menu Language
               case CEC_OPCODE_SET_MENU_LANGUAGE :
                  param1 = frame->data[1];
                  param2 = frame->data[2];
                  param3 = frame->data[3];
                  printf(" <Language = %c%c%c>\n", param1,param2,param3);
                  break;

                  // Active Source
               case CEC_OPCODE_ACTIVE_SOURCE :
                  param1 = ((int)frame->data[1] << 8) + frame->data[2];
                  printf(" <Physical Address = %.4x>\n", param1);
                  break;

                  // Inactive Source
               case CEC_OPCODE_INACTIVE_SOURCE :
                  param1 = ((int)frame->data[1] << 8) + frame->data[2];
                  printf(" <Physical Address = %.4x>\n", param1);
                  break;

                  // CEC Version
               case CEC_OPCODE_CEC_VERSION :
                  param1 = frame->data[1];
                  printf(" <CEC Version = %x>\n", param1);
                  break;

                  // Give OSD Name
               case CEC_OPCODE_GIVE_OSD_NAME :
                  IO_CEC(SET_OSD_NAME,&osd_name); /* to be check FIXEME */
                  break;

                  // Give Device vendor_ ID
               case CEC_OPCODE_GIVE_DEVICE_VENDOR_ID :
                  IO_CEC(DEVICE_VENDOR_ID,0x000800b7);
                  break;

                  // Report Physical Address
               case CEC_OPCODE_REPORT_PHYSICAL_ADDRESS :
                  param1 = ((int)frame->data[1] << 8) + frame->data[2];
                  param2 = frame->data[3];
                  printf(" <Physical Address = %x> <Device Type = %x> \n", param1, param2);
                  break;

                  // Device vendor_ ID
               case CEC_OPCODE_DEVICE_VENDOR_ID :
                  vendor_ID = ((int)frame->data[1] << 16) + \
                     ((int)frame->data[2] << 8) + \
                     frame->data[3];
                  printf(" <vendor_ ID = 0x%lx>\n", vendor_ID);
                  break;

               case CEC_OPCODE_VENDOR_COMMAND_WITH_ID :
                  vendor_ID 	 = ((int)frame->data[1] << 16)  + \
                     ((int)frame->data[2] << 8)+ \
                     frame->data[3];
                  vendor_CmdID0 = ((int)frame->data[4] << 24)  + \
                     ((int)frame->data[5] << 16)+ \
                     ((int)frame->data[6] << 8)+ \
                     frame->data[7];
                  vendor_CmdID1 = ((int)frame->data[8] << 24)  + \
                     ((int)frame->data[9] << 16)+ \
                     ((int)frame->data[10] << 8)+ \
                     frame->data[11];
                  vendor_CmdID2 = ((int)frame->data[12] << 16) + \
                     ((int)frame->data[13] << 8)+ \
                     frame->data[14];
                  printf(" <vendor_ ID = 0x%lx> <Command ID = 0x%lx%lx%lx>\n", vendor_ID, vendor_CmdID0, vendor_CmdID1, vendor_CmdID2);
                  break;

                  // Menu Request
               case CEC_OPCODE_MENU_REQUEST :
                  param1 = frame->data[1];
                  printf(" <Menu Request Type = %x>\n", param1);
                  break;

                  // Report Power Status
               case CEC_OPCODE_REPORT_POWER_STATUS :
                  param1 =frame->data[1];
                  printf(" <Power Status = %x>\n", param1);
                  break;

                  // Set OSD Name
               case CEC_OPCODE_SET_OSD_NAME :
                  for(i=1; i <= (frame->count-4); i++)
                     printf("%c", frame->data[i]);
                  printf(" >\n ");
                  break;

                  // Abort Message
               case CEC_OPCODE_ABORT_MESSAGE :
                  fa.FeatureOpcode=CEC_OPCODE_ABORT_MESSAGE;
                  fa.AbortReason=CEC_ABORT_REFUSED;
                  printf("ABORT_MESSAGE received\n");
                  IO_CEC(FEATURE_ABORT,&fa);
                  break;

                  // Feature Abort
               case CEC_OPCODE_FEATURE_ABORT :
                  printf("FEATURE_ABORT");
                  param1 = frame->data[1];
                  param2 = frame->data[2];
                  printf(" <Opcode = %x> <Abort Reason = %x>\n", param1, param2);
                  break;

                  // Routing Change
               case CEC_OPCODE_ROUTING_CHANGE :
                  param1 = ((int)frame->data[1] << 8) + frame->data[2];
                  param2 = frame->data[3];
                  printf(" <Physical Address = %x> <New Address = %x>\n", param1, param2);
                  break;

                  //Set Stream Path
               case CEC_OPCODE_SET_STREAM_PATH :
                  param1 = ((int)frame->data[1] << 8) + frame->data[2];           	        
                  printf(" <Physical Address = %x>\n", param1);
                  if (param1 == phy_addr) {
                     /*                      IO_CEC(ACTIVE_SRC,0); Done my module itself */
                  }
                  break;

                  //Give Device Power Status
               case CEC_OPCODE_GIVE_DEVICE_POWER_STATUS :
                  IO_CEC(REPORT_POWER_STATUS,CEC_POWER_STATUS_ON);
                  break;

                  //Give Audio Status
               case CEC_OPCODE_GIVE_AUDIO_STATUS :
                  audio.audioMuteSatus = CEC_AUDIO_MUTE_OFF; 
                  audio.audioVolumeSatus = 15;
                  IO_CEC(REPORT_AUDIO_STATUS,&audio);
                  break;

               case CEC_OPCODE_PLAY:
                  switch (frame->data[1]) {
                  case CEC_MODE_PLAY_FORWARD:
                     IO_TX(SET_POWER,tmPowerOn);
                     break;
                  case CEC_MODE_PLAY_REVERSE:
                     break;
                  case CEC_MODE_FAST_FORWARD_MIN_SPEED:
                     break;
                  case CEC_MODE_FAST_FORWARD_MEDIUM_SPEED:
                     break;
                  case CEC_MODE_FAST_FORWARD_MAX_SPEED:
                     break;
                  case CEC_MODE_FAST_REVERSE_MIN_SPEED:
                     break;
                  case CEC_MODE_FAST_REVERSE_MEDIUM_SPEED:
                     break;
                  case CEC_MODE_FAST_REVERSE_MAX_SPEED:
                     break;
                  case CEC_MODE_SLOW_FORWARD_MIN_SPEED:
                     break;
                  case CEC_MODE_SLOW_FORWARD_MEDIUM_SPEED:
                     break;
                  case CEC_MODE_SLOW_FORWARD_MAX_SPEED:
                     break;
                  case CEC_MODE_SLOW_REVERSE_MIN_SPEED:
                     break;
                  case CEC_MODE_SLOW_REVERSE_MEDIUM_SPEED:
                     break;
                  case CEC_MODE_SLOW_REVERSE_MAX_SPEED:
                     break;
                  default:           	 
                     fa.FeatureOpcode=frame->data[0];
                     fa.AbortReason=CEC_ABORT_INVALID_OPERAND;
                     printf("Send feature abort::invalid operand in opcode play\n");
                     IO_CEC(FEATURE_ABORT,&fa);
                     break;
                  }
                  break;

               case CEC_OPCODE_USER_CONTROL_PRESSED:
                  switch (frame->data[1]) {
                  case CEC_REMOTE_BUTTON_SELECT:
                     break;
                  case CEC_REMOTE_BUTTON_UP:
                     break;
                  case CEC_REMOTE_BUTTON_DOWN:
                     break;
                  case CEC_REMOTE_BUTTON_LEFT:
                     break;
                  case CEC_REMOTE_BUTTON_RIGHT:
                     break;
                  case CEC_REMOTE_BUTTON_RIGHT_UP:
                     break;
                  case CEC_REMOTE_BUTTON_RIGHT_DOWN:
                     break;
                  case CEC_REMOTE_BUTTON_LEFT_UP:
                     break;
                  case CEC_REMOTE_BUTTON_LEFT_DOWN:
                     break;
                  case CEC_REMOTE_BUTTON_ROOT_MENU:
                     break;
                  case CEC_REMOTE_BUTTON_SETUP_MENU:
                     break;
                  case CEC_REMOTE_BUTTON_CONTENTS_MENU:
                     break;
                  case CEC_REMOTE_BUTTON_FAVORITE_MENU:
                     break;
                  case CEC_REMOTE_BUTTON_EXIT:
                     break;
                  case CEC_REMOTE_BUTTON_NUMBER_0:
                     break;
                  case CEC_REMOTE_BUTTON_NUMBER_1:
                     break;
                  case CEC_REMOTE_BUTTON_NUMBER_2:
                     break;
                  case CEC_REMOTE_BUTTON_NUMBER_3:
                     break;
                  case CEC_REMOTE_BUTTON_NUMBER_4:
                     break;
                  case CEC_REMOTE_BUTTON_NUMBER_5:
                     break;
                  case CEC_REMOTE_BUTTON_NUMBER_6:
                     break;
                  case CEC_REMOTE_BUTTON_NUMBER_7:
                     break;
                  case CEC_REMOTE_BUTTON_NUMBER_8:
                     break;
                  case CEC_REMOTE_BUTTON_NUMBER_9:
                     break;
                  case CEC_REMOTE_BUTTON_DOT:
                     break;
                  case CEC_REMOTE_BUTTON_ENTER:
                     break;
                  case CEC_REMOTE_BUTTON_CLEAR:
                     break;
                  case CEC_REMOTE_BUTTON_NEXT_FAVORITE:
                     break;
                  case CEC_REMOTE_BUTTON_CHANNEL_UP:
                     break;
                  case CEC_REMOTE_BUTTON_CHANNEL_DOWN:
                     break;
                  case CEC_REMOTE_BUTTON_PREVIOUS_CHANNEL:
                     break;
                  case CEC_REMOTE_BUTTON_SOUND_SELECT:
                     break;
                  case CEC_REMOTE_BUTTON_INPUT_SELECT:
                     break;
                  case CEC_REMOTE_BUTTON_DISPLAY_INFORMATION:
                     break;
                  case CEC_REMOTE_BUTTON_HELP:
                     break;
                  case CEC_REMOTE_BUTTON_PAGE_UP:
                     break;
                  case CEC_REMOTE_BUTTON_PAGE_DOWN:
                     break;
                  case CEC_REMOTE_BUTTON_POWER:
                     break;
                  case CEC_REMOTE_BUTTON_VOLUME_UP:
                     break;
                  case CEC_REMOTE_BUTTON_VOLUME_DOWN:
                     break;
                  case CEC_REMOTE_BUTTON_MUTE:
                     break;
                  case CEC_REMOTE_BUTTON_PLAY:
                     break;
                  case CEC_REMOTE_BUTTON_STOP:
                     break;
                  case CEC_REMOTE_BUTTON_PAUSE:
                     break;
                  case CEC_REMOTE_BUTTON_RECORD:
                     break;
                  case CEC_REMOTE_BUTTON_REWIND:
                     break;
                  case CEC_REMOTE_BUTTON_FAST_FORWARD:
                     break;
                  case CEC_REMOTE_BUTTON_EJECT:
                     break;
                  case CEC_REMOTE_BUTTON_FORWARD:
                     break;
                  case CEC_REMOTE_BUTTON_BACKWARD:
                     break;
                  case CEC_REMOTE_BUTTON_STOP_RECORD:
                     break;
                  case CEC_REMOTE_BUTTON_PAUSE_RECORD:
                     break;
                  case CEC_REMOTE_BUTTON_ANGLE:
                     break;
                  case CEC_REMOTE_BUTTON_SUB_PICTURE:
                     break;
                  case CEC_REMOTE_BUTTON_VIDEO_ON_DEMAND:
                     break;
                  case CEC_REMOTE_BUTTON_ELECTRONIC_PROGRAM_GUIDE:
                     break;
                  case CEC_REMOTE_BUTTON_TIMER_PROGRAMMING:
                     break;
                  case CEC_REMOTE_BUTTON_INITIAL_CONFIGURATION:
                     break;
                  case CEC_REMOTE_BUTTON_PLAY_FUNCTION:
                     break;
                  case CEC_REMOTE_BUTTON_PAUSE_PLAY_FUNCTION:
                     break;
                  case CEC_REMOTE_BUTTON_RECORD_FUNCTION:
                     break;
                  case CEC_REMOTE_BUTTON_PAUSE_RECORD_FUNCTION:
                     break;
                  case CEC_REMOTE_BUTTON_STOP_FUNCTION:
                     break;
                  case CEC_REMOTE_BUTTON_MUTE_FUNCTION:
                     break;
                  case CEC_REMOTE_BUTTON_RESTORE_VOLUME_FUNCTION:
                     break;
                  case CEC_REMOTE_BUTTON_TUNE_FUNCTION:
                     break;
                  case CEC_REMOTE_BUTTON_SELECT_MEDIA_FUNCTION:
                     break;
                  case CEC_REMOTE_BUTTON_SELECT_AV_INPUT_FUNCTION:
                     break;
                  case CEC_REMOTE_BUTTON_SELECT_AUDIO_INPUT_FUNCTION:
                     break;
                  case CEC_REMOTE_BUTTON_POWER_TOGGLE_FUNCTION:
                     break;
                  case CEC_REMOTE_BUTTON_POWER_OFF_FUNCTION:
                     break;
                  case CEC_REMOTE_BUTTON_POWER_ON_FUNCTION:
                     break;
                  case CEC_REMOTE_BUTTON_F1_BLUE:
                     break;
                  case CEC_REMOTE_BUTTON_F2_RED:
                     break;
                  case CEC_REMOTE_BUTTON_F3_GREEN:
                     break;
                  case CEC_REMOTE_BUTTON_F4_YELLOW:
                     break;
                  case CEC_REMOTE_BUTTON_F5:
                     break;
                  case CEC_REMOTE_BUTTON_DATA:
                     break;
                  default:           	 
                     fa.FeatureOpcode=frame->data[0];
                     fa.AbortReason=CEC_ABORT_INVALID_OPERAND;
                     printf("Send feature abort::invalid operand in user control pressed\n");
                     IO_CEC(FEATURE_ABORT,&fa);
                     break;
                  }
                  break;

               case CEC_OPCODE_DECK_CONTROL:
                  switch (frame->data[1]) {
                  case CEC_DECK_CONTROL_WIND:   /*!< Skip Forward / Wind   */
                     break;
                  case CEC_DECK_CONTROL_REWIND: /*!< Skip Reverse / Rewind */
                     break;
                  case CEC_DECK_CONTROL_STOP:   /*!< Stop                  */
                     break;
                  case CEC_DECK_CONTROL_EJECT:  /*!< Eject                 */
                     break;
                  default:           	 
                     fa.FeatureOpcode=frame->data[0];
                     fa.AbortReason=CEC_ABORT_INVALID_OPERAND;
                     printf("Send feature abort::invalid operand in deck control\n");
                     IO_CEC(FEATURE_ABORT,&fa);
                     break;
                  }
                  break;

               case CEC_OPCODE_DECK_STATUS:
                  switch (frame->data[1]) {
                  case CEC_DECK_INFO_PLAY:              /*!< Play                  */
                        break;
                  case CEC_DECK_INFO_RECORD:            /*!< Record                */
                        break;
                  case CEC_DECK_INFO_PLAY_REVERSE:      /*!< Play Reverse          */
                        break;
                  case CEC_DECK_INFO_STILL:             /*!< Still                 */
                        break;
                  case CEC_DECK_INFO_SLOW:              /*!< Slow                  */
                        break;
                  case CEC_DECK_INFO_SLOW_REVERSE:      /*!< Slow Reverse          */
                        break;
                  case CEC_DECK_INFO_FAST_FORWARD:      /*!< Fast Forward          */
                        break;
                  case CEC_DECK_INFO_FAST_REVERSE:      /*!< Fast Reverse          */
                        break;
                  case CEC_DECK_INFO_NO_MEDIA:          /*!< No Media              */
                        break;
                  case CEC_DECK_INFO_STOP:              /*!< Stop                  */
                        break;
                  case CEC_DECK_INFO_WIND:              /*!< Skip Forward / Wind   */
                        break;
                  case CEC_DECK_INFO_REWIND:            /*!< Skip Reverse / Rewind */
                        break;
                  case CEC_DECK_INFO_ID_SEARCH_FORWARD: /*!< Index Search Forward  */
                        break;
                  case CEC_DECK_INFO_ID_SEARCH_REVERSE: /*!< Index Search Forward  */
                        break;
                  case CEC_DECK_INFO_OTHER_STATUS:      /*!< Other Status          */
                        break;
                  default:           	 
                     fa.FeatureOpcode=frame->data[0];
                     fa.AbortReason=CEC_ABORT_INVALID_OPERAND;
                     printf("Send feature abort::invalid operand in deck status\n");
                     IO_CEC(FEATURE_ABORT,&fa);
                     break;
                  }
                  break;


               case CEC_OPCODE_USER_CONTROL_RELEASED:
                  break;

               default:           	 
                  fa.FeatureOpcode=frame->data[0];
                  fa.AbortReason=CEC_ABORT_UNKNOWN_OPCODE;
                  printf("Send feature abort::unknown opcode\n");
                  IO_CEC(FEATURE_ABORT,&fa);
                  break;
               }
         }
   }
   else if (frame->service == CEC_TX_DONE) {
      /*          ack */
   }
}


void *rx_main( void *ptr ) {

   cec_frame frame;
   memset(&frame,0,sizeof(cec_frame));

   printf("%s is alive\n",__func__);

   /* another cec for event */
   if ((rx = open(cec_name, O_RDWR)) == -1) {
      perror(cec_name);
      oups("can not open hdmicec driver\n",0);
   }

   /* main loop */
   while(frame.service!=CEC_RELEASE) {
      IO_RX(WAIT_FRAME,&frame);
      read_frame(&frame);
   }

   close(rx);
   pthread_exit(0);

   return NULL;
}

/*
 *
 * Bench
 * ---------
 * CHAPTER 3
 *
 */

/* 0x001: driver open/close */
void bench0001(void) {

   unsigned long /* tda_power */ power; 

   printf("%s\n",__func__);


   /*
    * init
    */

   if ((tx = open(tx_name, O_RDWR)) == -1) {
      perror(tx_name);
      oups("can not open hdmicec driver (evt mgr)\n",0);
   }

   printf("Power on device\n");
   power = tmPowerOn;
   IO_TX(SET_POWER,&power);

   if ((cec = open(cec_name, O_RDWR)) == -1) {
      perror(cec_name);
      oups("can not open hdmicec driver\n",0);
   }

   IO_CEC(RX_ADDR,CEC_LOGICAL_ADDRESS_PLAYBACK_DEVICE_1);
   IO_CEC(POLLING_MSG,NULL);

   /* Create independent threads each of which will execute function */
   if (pthread_create( &rx_thread, NULL, rx_main, NULL)) {
      oups("can not create rx_thread\n",0);
   }

   /*
    * idle
    */
   
   sleep(user_wait);
   /*    IO_CEC(ACTIVE_SRC,0); */
   /*    IO_CEC(IMAGE_VIEW_ON,0); */


   /*
    * deinit
    */

   /* stop rx_thread */
   printf("rx_thread release request\n");
   IO_CEC(BYEBYE,0);
   pthread_join(rx_thread, NULL);

   /* bye bye */
   close(cec);
   close(tx);
}

/* 0x002: ... */
void bench0002(void) {

   printf("%s\n",__func__);

}

/*
 *
 * Entry point
 * -----------
 * CHAPTER 4
 *
 */

int main(int argc, char *argv[]) {

   printf("tda_demo, %s, %s %s\n",TDA_DEMO_VERSION,__DATE__,__TIME__);
   printf("any feedback welcome - andre.lepine@nxp.com\n");

   user_wait = 3;
   user_request=0xFFFF;
   /* Check command line parameters and init framebuffer */
   if (argc > 1) user_request = strtol(argv[1],NULL,16);
   if (argc > 2) user_wait = strtol(argv[2],NULL,16);
   
   printf("User request:%x user_wait:%d\n",user_request,user_wait);

   /* hook up our exit handler */
   signal(SIGINT|SIGTERM|SIGKILL|SIGQUIT, my_exit);

   /*
    * BENCH 
    */
   printf("\nSW raster%s",			\
          "\n---------\n");
   USERCHECK(0x01,bench0001());
   USERCHECK(0x02,bench0002());

   /*
     Exit with success
   */
   print_usage(argv[0]);
   raise(SIGTERM);
   return 0;
}
