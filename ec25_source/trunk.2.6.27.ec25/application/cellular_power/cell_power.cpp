/*
 * This file contains proprietary information and is subject to the terms and
 * conditions defined in file 'OSILICENSE.txt', which is part of this source
 * code package.
 */

/*************************************************************
 * Cellular Radio Power control Utility
 * This module controls the power applied to the Radio module
 * as well as the mode (Airplane/Normal) of the device
 *
 * On bootup the power is enabled and the radio firmware
 * comes up with the radio in Airplane mode (OFF mode).
 * During operation the utility is used to switch modes as needed.
 * Status flags indicating power enabled and the mode
 * are stored in /tmp
 *
 * As part of the cell_power on feature another process is
 * initiated "cell_status_monitor". This module uses the APP port to
 * send AT commands to accumulate status information that is
 * cached in /tmp/cell_status file for use by other processes such
 * as the GUI status page. In addition this background process drives
 * the Cellular LEDs according to the current status.
 * The "cell_power off" command will kill the cell_status_monitor
 * as well as switch the radio to airplane mode
 *
 *************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <string>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <vector>

#include <debuglog.h>
#include <unistd.h>

#include "../../gui-config/util/serial_at.h"
#include "../../gui-config/util/utils.h"

using namespace std;

#define CINTERION_POWER_ON_FILE  "/tmp/cell_power_on"
#define CELL_STATUS_FILE  "/tmp/cell_status"
#define CELL_SIGNAL_FILE  "/tmp/cell_signal"
#define CELL_TOWER_FILE "/tmp/cell_tower"
#define CELL_TYPE_FILE  "/tmp/cell_type"
#define CELL_ENABLE_FILE  "/tmp/cell_enabled"
#define CELL_MONITOR_SHUTDOWN "/tmp/cell_monitor_shutdown"
#define CELL_ID_FILE "/tmp/cell_id"

#define OPT_EXTENDED_LED       0x00008000

bool power_off();

bool isExtendedMode;
int verbose;
int  test_mode = 0;
char usage[] = "usage: cell_power enable | on | off | disable\n";
char buff[500];
char error_msg[320];
char resp_buff[1000];
char tmp_resp_buff[1000];

struct stat stFileInfo;

enum enum_flag {
    IS_PXS8 = 1,
    IS_PLS8 = 2,
    IS_PLS62 = 3,
    IS_ELS31 = 4,
    IS_EC25 = 5,
    IS_TO = 0
};
enum enum_flag device_type;


void Open_App_port(char *COMM_PORT) {

  //if (  !OpenCOM( (char *)CENTERION_APP_PORT,  BAUD_115200 )  ) {
   if (  !OpenCOM( COMM_PORT,  BAUD_115200 )  ) {
         debuglog(LOG_INFO, "Failed to open App port of Radio (/dev/ttyS2) aborting cell_power!");
         if(verbose) printf("Failed to open App port of Radio (/dev/ttyS2) aborting cell_power!\n");
         exit(0);
   }

}

/***********************************************************************
*
* Description: This function wraps calls to the low level ATCommand function
*              from serial_at module. All AT commands are sent through this
*              function including those that produce URCs.
*
* Calling Arguments:
* Name               Mode      Description
* cmd                IN        A string holding the AT command with trailing \r
* urc                IN        Optional string containing the URC that is expected
* toMillSec          IN        Optional Timeout that specifies max time
*                              in millsecs to wait for a URC

* Return Value:
*    Type      Description
*    Bool      Indicate success of failure of the AT command sent to the serial port
*
* Return Codes/Exceptions:
*   True - received an OK response from the Cinterian
*   False - received ERROR or response or timed out without receiving expected response.
*
* Notes: The global resp_buff contains any response or partial response from the Cinterions
*        Application port. This can be processed by the caller.
******************************************************************************/
bool RunAT( const char * cmd, const  char* urc = NULL, DWORD toMillSec = 12000 ) {
bool ret;

      if ( verbose )   hexdump("AT Command:", (char *)cmd,  strlen(cmd ));
      resp_buff[0] = 0;
      PurgeCOM();

      if (urc == NULL) {
            ret = ATCommand( cmd, resp_buff, ARRAYSIZE( resp_buff));
      } else
           ret =  ATCommandUrc( cmd, resp_buff, ARRAYSIZE( resp_buff ),  toMillSec,  (char *)urc );
      if ( !ret ) {
          if ( verbose ) printf( "%s\n", resp_buff );
      }
      if (verbose)  hexdump("AT Response:", resp_buff, strlen(resp_buff));
      return ret;
}

/***********************************************************************
*
* Description: read the current option flags from config memory using
*              shell out to config_read command
*
* Calling Arguments:
* Name               Mode      Description
* None

* Return Value:
*    Type              Description
*    unsigned dint     the option bits from config memory
*
*
******************************************************************************/
unsigned int read_options() {
FILE *fp;
char *p;
char flags[100];
unsigned int opts;

   fp = popen("read_config -n option", "r");
   if ( fp != NULL ) {
      fgets( flags, sizeof(flags), fp );
      pclose( fp );
      p = strchr( flags, '\n' );
      if ( p == NULL ) {
          flags[0] = '\0';
      } else
      *p = '\0';
   } else
      flags[0] = '\0';

    opts = (unsigned int) strtol( flags, NULL, 0);
    return opts;
}


/* check SIM status with AT^SIND? command and look at simstatus field
        ........ simstatus,0,n<cr><lf>.......
           n = 0  - SIM not inserted
           n = 1  - SIM inseted but not initalized
           n = 5  - SIM inseted and initialization completed
we only want to continue with the SIM if n = 5 otherwise Phonenumber is Unknown
return true if status = 5
otherwise return false
*/
bool get_sim_status (string model) {
char *p,*p1;

    if(verbose) printf("In get_sim_status() for model %s ...........\n", model.c_str());

    if(model == "ELS31-V" || model == "PLS62-W") {
          if ( RunAT( "AT+CCID?\r") ) {
               if (model == "ELS31-V" ){
                   p = strstr(resp_buff, "ID: \"");
                   if (p != NULL ){
                       p += 5;
                       p1 = strchr(p, '\"');
                       if (p1 != NULL){
                            *p1 = '\0';
                            if(strlen(p) > 6){
                                  strcpy(tmp_resp_buff, p);
                                  return true; // indicates SIM detected and tmp_resp_buff has ccid
                            } else {
                                if(verbose) printf("AT+CCID failed to return ccid, indicates SIM not detected in ELS31-V\n");
                                return false;
                            }
                       }
                   }
             } else {   // no qoutes in PLS62-W response
                   p = strstr(resp_buff, "ID: ");
                   if (p != NULL ){
                       p += 4;
                       p1 = strchr(p, '\r');
                       if (p1 != NULL){
                            *p1 = '\0';
                            if(strlen(p) > 6){
                                  strcpy(tmp_resp_buff, p);
                                  return true; // indicates SIM detected and tmp_resp_buff has ccid
                            } else {
                                if(verbose) printf("AT+CCID failed to return ccid, indicates SIM not detected in PLS62-W\n");
                                return false;
                            }
                       }
                   }
             }
          }

          if(verbose) printf("AT+CCID? Failed!\n");
          return false;
    } else if (model == "EC25AF" || model == "EC25G"){
        if ( RunAT( "AT+QCCID\r") ) {
                   p = strstr(resp_buff, "ID: ");
                   if (p != NULL ){
                       p += 4;
                       p1 = strchr(p, '\r');
                       if (p1 != NULL){
                            *p1 = '\0';
                            if(strlen(p) > 6){
                                  strcpy(tmp_resp_buff, p);
                                  return true; // indicates SIM detected and tmp_resp_buff has ccid
                            } else {
                                if(verbose) printf("AT+QCCID failed to return ccid, indicates SIM failure in EC25\n");
                                return false;
                            }
                       }
                   }
        }
        if(verbose) printf("AT+QCCID for EC25 Failed!\n");
        return false;
    } else {
        if ( RunAT( "AT^SIND?\r") ) {
            strcpy(tmp_resp_buff, resp_buff);
            p = strstr(resp_buff,"simstatus");
            if (p != NULL) {
                 p1 = strchr(p,',');   // find the first , in the string
                 p1++;
                 p = strchr(p1,',');   // find the second ,
                 p++;
                 if (*p == '5'){
                      if(verbose) printf("get_sim_status() returns true, SIM is ready\n");
                      return true;
                 }
            }
         }
         if(verbose) printf("get_sim_status() returns false, SIM is not ready\n");
         return false;
    }


}
char mdn[100];

void get_sim_telenum(string model) {
char *p;
int i;
int attemptCount = 0;
int respLen;

  if(verbose) printf("In get_sim_telenum().............\n");
  mdn[0] = 0;
  //sleep(2); // give some time for SIM to register PIN
  if( get_sim_status(model)) {
     if(verbose) printf("Use AT+CNUM to get phone number.............\n");
     // TODO: need to add retries because next command fails some times on PLS62-W with early firmware
      while(attemptCount < 5) {
          if (RunAT( "AT+CNUM\r")) {
               // check if we could have a full response such as <CR><LF>+CNUM: ,"+15194007245",145<CR><LF><CR><LF><CR><LF>OK<CR><LF>
               if( strchr(resp_buff,',') != NULL) {
                  respLen = strlen(resp_buff);
                  p = &resp_buff[0];
                  // we may have a phone number, or not
                  //AT Response:<CR><LF>+CNUM: ,"+15194007245",145<CR><LF><CR><LF><CR><LF>OK<CR><LF>
                  //logmsg("May have a Phone number in SIM");
                  /* search for the first comma, start of  telephone number field
                   * Then take all data (if any) bewteeen the quotes
                   * Could have + or - in addition to digits in this field
                   */
                  i = 0;
                  while (*p++ != '\x2C' ) i++;  // skip until a , is detected
                  i++;  // scip over the first double quote "
                  if ( i >= respLen) {
                        // no phone number in  OK response
                        break;
                  }
                  p++;
                  i = 0;
                  /* positioned at the first char in the phone field
                   * sometimes there are non digits in the number e.g: fido uses
                   * "+15197312299"
                   */
                  while(*p > '\x39' || *p < '\x30') p++;
                  while(*p != '\x22') // copy all digits up to closing quote
                      mdn[i++] = *p++;
                  if ( i > 7)  {
                     mdn[i++] = '\0';
                     break;  // Detected phone num in SIM response
                  }
                  mdn[0] = '\0';
                  break;   // No Phone number in SIM response
              } else break;
          } else {
              // Failed getTeleNum AT+CNUM call!
               // debuglog(LOG_INFO,"Failed AT+CNUM with %s", resp_buff);
                sleep(2);
          }
          attemptCount++;
      }
  }
  if (mdn[0] == '\0') {
      // Failed to read SIM phone number!;
      strcpy(mdn,"unknown");
  }

}

int get_quectel_info() {
FILE *fp;
char *pStart;
char *p, *p1;
string stemp,man, model, ver, imei, iccid;
int i;
int ret = 0;

  if ( RunAT( "ATI1\r") ){

     pStart = strstr(resp_buff,"sion:" );
     p = pStart + 6;
     p1 = strchr(p,'\r');
     *p1 = 0;
     ver = p;
     // parsew the revision string for type of modem
     if (ver.rfind("EC25AF", 0) == 0) {
         model = "EC25AF";
         ret = 5;
     } else {
        model = "EC25G";
        ret = 6;
     }

     pStart = strstr(resp_buff,"Quectel" );
     i = strlen(pStart);
     pStart[i-6] = 0;

     p = pStart;
     p1 = strchr(p,'\r');
     *p1 = 0;
     man = p;

     //  imei
     if ( RunAT( "AT+CGSN\r")  != NULL ){
          i = strlen(resp_buff);
          resp_buff[i-8] = 0;
          p = &resp_buff[2];
          imei = p;
     } else
        imei = "Unknown";

     if(verbose) printf("ATI1 and AT+CGSN info:\nManufacture: %s\nModel: %s\nFw Version: %s\nIMEI: %s\n",
              man.c_str(), model.c_str(), ver.c_str(), imei.c_str() );

      get_sim_telenum(model);
      iccid.assign(tmp_resp_buff);
      debuglog(LOG_INFO,"Creating /tmp/cell_id file");
      fp = fopen(CELL_ID_FILE, "wb");
      sprintf(resp_buff,"manufacture=%s\n",man.c_str());
      fwrite(resp_buff, 1, strlen(resp_buff), fp);
      sprintf(resp_buff,"model=%s\n",model.c_str());
      fwrite(resp_buff, 1, strlen(resp_buff), fp);
      sprintf(resp_buff,"fw_version=%s\n",ver.c_str());
      fwrite(resp_buff, 1, strlen(resp_buff), fp);
      //sprintf(resp_buff,"app_version=%s\n",appver.c_str());
      //fwrite(resp_buff, 1, strlen(resp_buff), fp);
      sprintf(resp_buff,"imei=%s\n",imei.c_str());
      fwrite(resp_buff, 1, strlen(resp_buff), fp);
      sprintf(resp_buff,"mdn=%s\n",mdn);
      fwrite(resp_buff, 1, strlen(resp_buff), fp);
      sprintf(resp_buff,"iccid=%s\n",iccid.c_str());
      fwrite(resp_buff, 1, strlen(resp_buff), fp);
      fclose(fp);

  } else {
      debuglog(LOG_INFO, "Failed to get response from ATI1 command!");
      if (resp_buff[0] != 0 && verbose)
          hexdump("ATI1 Response:", resp_buff, strlen(resp_buff));
  }
  return ret;

}



int get_cinterion_info() {
FILE *fp;
char *pStart;
char *p, *p1;
string stemp,man, model, ver, appver, lrev, imei, iccid;
int i;
int ret = 0;

  if ( RunAT( "ATI1\r") ){
     pStart = strstr(resp_buff,"Cinterion" );  // jump over any URC
     i = strlen(pStart);
     pStart[i-8] = 0;
     p = pStart;
     p1 = strchr(p,'\r');
     *p1 = 0;
     man = p;

     p = p1+2;
     p1 = strchr(p,'\r');
     *p1 = 0;
     model = p;

     p = p1+2;
     p1 = strchr(p,'\r');
     *p1 = 0;
     ver = p;

     p = p1+2;
     p1 = strchr(p,'\r');
     if(p1) *p1 = 0;
     appver = p;
     if (model == "ELS31-V"){
       p = p1+2;
       lrev = p;
     }

     //concatinate each of the version numbers into one string with each sub-version seperated by coma
     stemp = ver.substr(ver.find("ION ")+4);
     stemp.append(",");
     stemp.append(appver.substr(appver.find("ION ")+4));
     if (model == "ELS31-V"){
           stemp.append(",");
           stemp.append(lrev.substr(lrev.find("ION ")+4));
     }
     ver = stemp;
     if ( RunAT( "AT+CGSN\r")  != NULL ){
          i = strlen(resp_buff);
          resp_buff[i-8] = 0;
          p = &resp_buff[2];
          imei = p;
     } else
        imei = "Unknown";

if(verbose) printf("ATI1 and AT+CGSN info:\nManufacture: %s\nModel: %s\nFw Version: %s\nIMEI: %s\n",
              man.c_str(), model.c_str(), ver.c_str(), imei.c_str() );
//if(verbose) printf("model prefix = %s\n",  model.substr(0,4).c_str());
      if ( model == "PXS8")
          ret = 1;
      else if ( model.substr(0,4) == "PLS8")
          ret = 2;
      else if (model == "PLS62-W")
          ret = 3;
      else if (model == "ELS31-V"){
          ret = 4;
          //RunAT("AT^SLED=2\r");   // do this in cell_status_monitor
      }
      //if (model != "PLS62-W")  //PLS62-W takes a long time to make SIM available
           get_sim_telenum(model);  // also gets tmp_resp_buff
      if (model != "ELS31-V" && model != "PLS62-W"){
            pStart = strstr(tmp_resp_buff,"iccid,");
            if(pStart) {
                p = strstr(pStart,",\"");
                if (p) {
                    p += 2;
                    p1 = strchr(p,'\"');
                    *p1 = 0;
                    iccid = p;
                } else
                   iccid.assign("unknown");
            }else
               iccid.assign("unknown");
      } else {
          iccid.assign(tmp_resp_buff);
      }
      debuglog(LOG_INFO,"Creating /tmp/cell_id file");
      fp = fopen(CELL_ID_FILE, "wb");
      sprintf(resp_buff,"manufacture=%s\n",man.c_str());
      fwrite(resp_buff, 1, strlen(resp_buff), fp);
      sprintf(resp_buff,"model=%s\n",model.c_str());
      fwrite(resp_buff, 1, strlen(resp_buff), fp);
      sprintf(resp_buff,"fw_version=%s\n",ver.c_str());
      fwrite(resp_buff, 1, strlen(resp_buff), fp);
      //sprintf(resp_buff,"app_version=%s\n",appver.c_str());
      //fwrite(resp_buff, 1, strlen(resp_buff), fp);
      sprintf(resp_buff,"imei=%s\n",imei.c_str());
      fwrite(resp_buff, 1, strlen(resp_buff), fp);
      sprintf(resp_buff,"mdn=%s\n",mdn);
      fwrite(resp_buff, 1, strlen(resp_buff), fp);
      sprintf(resp_buff,"iccid=%s\n",iccid.c_str());
      fwrite(resp_buff, 1, strlen(resp_buff), fp);
      fclose(fp);
  } else {
      debuglog(LOG_INFO, "Failed to get response from ATI1 command!");
      if (resp_buff[0] != 0 && verbose)
          hexdump("ATI1 Response:", resp_buff, strlen(resp_buff));
  }
  return ret;
}

/***********************************************************************
*
* Description: Read the Cinterion type from the radio via ATI command and
*              return an integer indicating the type
*
* Calling Arguments:
* Name               Mode      Description
* None

* Return     Value:
*    int      0 - if not a Cinterion PLSX8 or PLS8_x
*             1 - is PXS8
*             2 - if PLS8
*
*
******************************************************************************/
int get_cinterion_type() {
char *p;

  if ( RunAT( "ATI\r") && strstr(resp_buff,"Cinterion" ) != NULL ) {
      if ( strstr(resp_buff,"PXS8"))
           return 1;
      if ( strstr(resp_buff,"PLS8"))
           return 2;
      else {
         sprintf(buff, "Wrong Radio type detected!\n%s", resp_buff);
         debuglog(LOG_INFO, buff);
      }
  } else {
      debuglog(LOG_INFO, "Failed to get response from ATI command!");
      if (resp_buff[0] != 0 && verbose)
          hexdump("ATI Response:", resp_buff, strlen(resp_buff));
  }
  return 0;
}


/***********************************************************************
*
* Description: Us AT command CFUN? to check if in normal mode and return
*              bool value indicating if so or not
*
* Calling Arguments:
* Name               Mode      Description
* None

* Return     Value:
*    bool    True - in Normal mode
*            False - in Airplane mode
*
******************************************************************************/
bool in_normal_mode() {
  if ( RunAT( "AT+CFUN?\r")){
      if( strstr(  resp_buff, ": 1") != NULL )
            return true;
  }
  return false;
}


/***********************************************************************
*
* Description: Set th Radio to Airplane mode
*
* Calling Arguments:
* Name               Mode      Description
* None
*
* Return     Value:
*    bool    True - AT command returned OK in response
*            False - AT command returned ERROR or failed to respond
*
* Note: the URC is expected however if already in Airplane mode there will be no URC
******************************************************************************/
bool set_to_airplane_mode(){
int i;

   if(verbose) printf("Set to Airplane mode...\n");
   if ( !RunAT("AT+CFUN=4\r", "AIRPLANE MODE")) {
           if(verbose) printf("Failed sending AT+CFUN=4...\n");
           return false;
   }
   debuglog(LOG_INFO, "cell_power: Set to Airplane mode");
   return true;
}


bool wait_for_sysstart() {
      resp_buff[0] = 0;

//      if ( !item_exists( "/tmp/wifi_led_status_monitor.pid") ){
           if ( !ReadComPort(resp_buff, 300, 60000) ){
                if( verbose ) printf("Failed to get URC after power up!\n");
                debuglog(LOG_INFO, "cell_power enable: Failed to get URC after power up!");
                return false;
           }
           if (strstr(resp_buff,"^SYSSTART")){
                debuglog(LOG_INFO, "cell_power enable: Detected ^SYSSTART from cellular modem");
                return true;
           }
//       }
       PurgeCOM();
       if( !wait_for_connection( 60 ) ){
             if( verbose ) printf("Failed to connect to cellular Radio after power up!\n");
             debuglog(LOG_INFO, "cell_power enable: Failed to connect to cellular Radio after power up!");
             return false;
       }
       debuglog(LOG_INFO, "cell_power enable: Cellular modem responding to AT commands.");
       return true;

}

/***********************************************************************
*
* Description: Function called after power enabled and before opening APP serial port.
*              Wait for the Cinterion to enumerate its I/O ports up to enumerate_to seconds.
*              The tty IDProduct  number for each of the expected ports is checked.
*
* Calling Arguments:
* Name               Mode      Description
* None

* Return Value:
*    None
* Note: depending on radio type and how the radio is configured the ports enumerated
*       and thus their product IDs can be different. In this function we look for any
*       of the possible product IDs that can appear for PXS8 or PLS8-X, ELS31-V and PLS62-W
Ositech configures the modem to use specifc ports
===========================================================================================
PXS8
Bus 001 Device 002: ID 1e2d:0053
=============================================================================================
PLS8-X
Bus 001 Device 002: ID 1e2d:0061
==============================================================================================
PLS62-W  - no Verizon
Bus 001 Device 002: ID 1e2d:005b
==================================================================================================
ELS31-V   -  Verizon only
Bus 001 Device 002: ID 1e2d:00a0


*/
bool depricated_wait_for_enumeration(int enumerate_to){
FILE *fp;
char buff[200];
int i;

   for (i=0; i < enumerate_to; i++ ) {
         fp = popen("lsusb", "r");
         if (fp != NULL) {
               while ( fgets(buff, sizeof(buff), fp) ) {
                   if ( strstr(buff, ":0053") || strstr(buff,":0061") || strstr(buff, ":005b") || strstr(buff, ":00a0"))
                        return true;
                }
         }
         pclose(fp);
         WaitMs(1000);
   }
   return false;

}

enum enum_flag wait_for_enumeration(int enumerate_to){
FILE *fp;
char buff[200];

   for (int i=0; i < enumerate_to; i++ ) {
         fp = popen("lsusb", "r");
         if (fp != NULL) {
               while ( fgets(buff, sizeof(buff), fp) ) {
                   if ( strstr(buff, ":0053") ){

                   } else if (strstr(buff,":0061")){
                       return IS_PXS8;

                   } else if (strstr(buff, ":005b")){
                       return  IS_ELS31;

                   } else if ( strstr(buff, ":00a0")){
                        return IS_PLS8;
                                           // 2c7c:0125
                                           // 05C6:0125
                   } else if( strstr(buff, ":0125")){
                       return IS_EC25;
                   }
                }
         }
         pclose(fp);
         WaitMs(1000);
   }
   return IS_TO;

}

/***********************************************************************
*
* Description: Send low level system commands to apply power to the Cellular
*              device. This includes sending the power pulse via tiney_monitor.
*
* Calling Arguments:
* Name               Mode      Description
* None

* Return Value:
*    Type      Description
*    Bool       Success or failure
*
*
*
******************************************************************************/
bool power_enable() {
FILE *fp;
int i;
int to = 30;
char *chrp;
unsigned int opts;


    if( verbose ) printf("Sending on > /sys/gobi/state\n");
    i = 1;
    i = system("echo on >/sys/gobi/state");
debuglog(LOG_INFO, "cell_power enable: echo on >/sys/gobi/state returned %d", i);
    if (i != 0){
        // assume Quectel modem
        debuglog(LOG_INFO, "cell_power enable: Quectel modem ...");
        i = system("echo 1 >/sys/class/i2c-adapter/i2c-0/0-0020/output1");
        if ( i == -1){
                debuglog(LOG_INFO, "cell_power enable: Failed to enable Power for Quectel modem.");
                return false;
        }
        to = 20;

    } else {
        debuglog(LOG_INFO, "cell_power enable: Sent on >/sys/gobi/state...");
        // send ignition pulse to turn cinterion power on
        if( verbose ) printf("Sending power pulse...\n");

        /* when running after the tiny2313_monitor process has been started, we can
           use the monitor to access the tiny processor and send the ignition pulse to the Cinterion
           NOTE: this works for both PXS8 and PLS8
        */

        debuglog(LOG_INFO, "cell_power enable: Sending ignition pulse via tiny2313_monitor call...");
        system("tiny2313_client -f /mnt/flash/titan-appl/bin/gobi_power_on.txt -r 2 > /dev/null");
        debuglog(LOG_INFO, "cell_power: End of pulse---------------");
    }

    if(!test_mode){ // test mode skips wait_for_enumeration call
        if(verbose) printf("cell_power enable: Wait for cellular modem to enumerate I/O ports...\n");
        device_type = wait_for_enumeration(to);
        if (device_type == IS_TO) {
             if(verbose) printf("Failed to enumerate after %d seconds!\n", to);
              debuglog(LOG_INFO,"cell_power enable: Failed to enumerate after %d seconds!", to);
              return false ;
        } else {
              system("rm -f /tmp/cell_not_mounted");
              if(verbose) printf("Enumeration successful! remove cell_not_mounted flag\n");
              debuglog(LOG_INFO,"cell_power enable: Enumeration successful with ordinal %d!", device_type);
        }
    }
    if (device_type >  IS_ELS31){
        Open_App_port(QUECTEL_COMM_PORT);
        //???? port may need flow control via AT+IFC +ICF, +IPR
        // ???? what type of logon message does EC25 send when port is first opened after bootup UC
        //???? what does EC25 need as setup - assumee device was setup for Ositech
        // switch to airplane mode
        set_to_airplane_mode();
        // ATE turn on echo
        // at+qindcfg="all",0,1 - turn off  URCs
    } else {
      Open_App_port(CENTERION_APP_PORT);
      if( verbose ) printf("Waiting for ^SYSSTART using App port\n");
      debuglog(LOG_INFO, "cell_power enable: Wait for SYSSTART on App port /dev/ttyS2");
      if (!wait_for_sysstart())
         return false ;
   }

  PurgeCOM();
  debuglog(LOG_INFO, "cell_power enable: connected to modem OK");

   /*
    if (in_normal_mode()){
        if (!set_to_airplane_mode()){
           strcpy(error_msg, "Failed to set to Airplane mode!");
           if(verbose) printf("Failed to set to Airplane mode!\n");
           return false;
        } else if(verbose) printf("Radio set to Airplane mode\n");
    }
    */

    RunAT("AT+CMEE=2\r");
    // determine type of Cinterian radio
    if (device_type == IS_EC25)
        i = get_quectel_info();
    else
       i = get_cinterion_info();    //get_cinterion_type();
    if ( i != 0 ) {

       fp = fopen(CELL_TYPE_FILE, "wb");
       fputc( i,fp);
       fclose(fp);
       if ( i == 1 )
           debuglog(LOG_INFO,"Found PXS8 Cinterion Radio.");
       else if (i == 2)
           debuglog(LOG_INFO,"Found PLS8-X Cinterion Radio.");
       else if (i == 3)
           debuglog(LOG_INFO,"Found PLS62-W Cinterion Radio.");
       else if (i == 4)
           debuglog(LOG_INFO,"Found ELS31-V Cinterion Radio.");
       else if(i == 5)
        debuglog(LOG_INFO,"Found EC25-AF Quectel Radio.");
       else if (i == 6)
        debuglog(LOG_INFO,"Found EC25-G Quectel Radio.");
    } else {
      if(verbose) printf("Failed to determine Radio modem type!\n");
       debuglog(LOG_INFO,"Failed to determine Radio modem type!");
       CloseCOM();
       return false;
    }

    if ( i < 4){
        debuglog(LOG_INFO, "cell_power: set LED to indicate airplane mode");
        // Set LED to indicate Airplane mode
        if ( isExtendedMode )
            system("titan3_led cell 2 > /dev/null");
        else
            system("titan3_led cell 0 > /dev/null");
    }

    chrp =  read_config(CFG_MEM_OPTIONS, "-n" );
    opts = (unsigned int)strtol(chrp, NULL, 0);
    if( opts & OPT_GPS_FROM_CELL) {
        if (i > 4)
            strcpy(buff, "AT+QGPSCFG,1\r");
        else
            strcpy(buff, "AT^SGPSC=ENGINE,1\r");

        if ( RunAT( buff)){
             debuglog(LOG_INFO,"Setting GPS from cellular.");
             if (verbose) printf("Setting GPS from cellular");
        } else {
             debuglog(LOG_INFO,"FAILED: Setting GPS from cellular.");
             if (verbose) printf("FAILED: Setting GPS from cellular");
        }
    }
    CloseCOM();
    return true;
}

bool cell_status_monitor_running() {
FILE *fp;

   // make sure only one instance of cell_status_monitor  is running in the system
   fp = popen( "ps | grep cell_status_monitor", "r" );
   if (fp != NULL) {
         while ( fgets( buff, sizeof(buff), fp) ) {
              if( strstr( buff, "grep")  != NULL ) continue;
              if( strstr( buff, "cell_status_monitor" ) != NULL) {
                   pclose( fp );
                   return true;
              }
         }
         pclose( fp );
   }
   return false;
}

/***********************************************************************
*
* Description: This function does not access the Radio directly. Instead it launches
*              the cell_status_monitor daemon which will switch the Radio from
*              Airplane to Normal mode
*
* Calling Arguments:
* Name               Mode      Description
* None
*
* Return Value:
*    Type      Description
*    Bool       Success or failure
*
*
* Note: a check is made to see if the cell_status_monitor is already running
*       if so then do not launch again.
*
******************************************************************************/
bool power_on() {
struct stat stFileInfo;
DWORD i;
   if (cell_status_monitor_running()) {
       if( verbose )  printf( "cell_status_monitor already running, assume radio is in Normal mode!\n" );
       //debuglog( LOG_INFO, "cell_status_monitor already running, assume radio is in Normal mode!" );
       return false;
   }
   remove(CELL_MONITOR_SHUTDOWN);
  if (verbose) printf( "Power on radio, invoking cell_status_monitor...\n");
  debuglog(LOG_INFO, "cell_power on: Start cell_status_monitor to switch to Normal mode and start monitoring.");

  if(stat("/mnt/flash/titan-data/log_cell_monitor", &stFileInfo) == 0 || stat("/tmp/log_cell_monitor", &stFileInfo) == 0 )
       system("cell_status_monitor -v > /tmp/cell_monitor.log 2>&1 &");
   else
       system("cell_status_monitor &");

  /* wait for /tmp/cell_power_on to show up */
  i = 0;
  while(i < 80){   //  wait approx 60 seconds
      if ( item_exists( CINTERION_POWER_ON_FILE) ){
           /* wait for /tmp/cell_status to appear */
           i = 0;
           while(i < 80){  //  wait approx 60 seconds
                if(stat("/tmp/cell_status", &stFileInfo) == 0)
                     return true;
                WaitMs(200);
                i++;
           }

           if (verbose) printf( "cell_power on: the /tmp/cell_status failed to be created!\n");
           debuglog(LOG_INFO, "cell_power on: the /tmp/cell_status failed to be created!");
           power_off();
           return false;
      }
      WaitMs(200);
      i++;
  }

  if (verbose) printf( "cell_power on: the /tmp/cell_power_on flag failed to be created!\n");
  debuglog(LOG_INFO, "cell_power on: the /tmp/cell_power_on flag failed to be created!");
  power_off();
  return false;

}


/***********************************************************************
*
* Description: Power off radio by killing cell_status_monitor daemon then
*              if necessaries switch radio to Airplane mode and cleanup tag files
*              and clear Cell LED.
*
*
* Calling Arguments:
* Name               Mode      Description
* None
*
* Return Value:
*    Type      Description
*    Bool       Success or failure
*
*
******************************************************************************/
//bool power_off_old() {
//int i;
//bool ret = false;
//
//  debuglog(LOG_INFO, "cell_power: Power off radio...");
//  if (verbose) printf( "killall cell_status_monitor...\n");
//  system("killall cell_status_monitor");
//
//  sleep(2);
//  if( cell_status_monitor_running()) {
//     if (verbose) printf( "cell_status_monitor still running killall cell_status_monitor...\n");
//     debuglog(LOG_INFO, "cell_status_monitor still running killall cell_status_monitor...");
//     system("killall cell_status_monitor");
//
//  }
//
//  Open_App_port();
//  RunAT( "AT^SMSO\r") ;
//  if ( in_normal_mode())
//         set_to_airplane_mode();
//
//   CloseCOM();
//  // Set LED to indicate Airplane mode
//   if(verbose) printf("Set LED to indicate Airplane mode\n");
//   if ( isExtendedMode )
//      system("titan3_led cell 2 > /dev/null");
//   else
//      system("titan3_led cell 0 > /dev/null");
//   system("titan3_led cell-sig 0 0 > /dev/null");   //turn off cell-sig
//
//   //    sleep(1);  // TODO: could use ps | grep cell_status_monitor and wait for process to disappear
//    if(verbose) printf("cleanup cell power control files..\n");
//    remove(CINTERION_POWER_ON_FILE);
//    remove(CELL_STATUS_FILE);
//    remove(CELL_TOWER_FILE);
//    remove(CELL_SIGNAL_FILE);
//    system("sync");
//    return true;
//}

bool reset_radio_module(){
     debuglog(LOG_INFO, "cell_power off For PLS62 reset modem...");
     RunAT("AT+CFUN=1,1\r");
     PurgeCOM();
     resp_buff[0] = 0;
     if(verbose) printf("cell_power off: PLS62 reboot, wait for restart...\n");
     if ( !wait_for_sysstart()){
            if(verbose) printf("cell_power off: PLS62 reboot, wait for restart...\n");
            return false;
     }
     return true;

}

bool power_off() {
FILE *fp;
int i;
int cell_type = -1;
bool ret = true;


//  if ( (fp = fopen(CELL_TYPE_FILE, "rb")) != NULL) {
//     cell_type = fgetc(fp);
//     fclose(fp);
//  }

  if(verbose) printf("cell_power off: Power off radio...");

  if(verbose) printf("cell_power off: check if cell_status_monitor running...\n");
  if( cell_status_monitor_running()) {
      if(verbose) printf("cell_power off: signal cell_status_monitor to shutdown\n");
      system("touch /tmp/cell_monitor_shutdown");
      system("sync");
     // debuglog(LOG_INFO, "cell_power off: set cell_monitor_shutdown flag, wait for cell_status_monitor to terminate...");
      if(verbose) printf("cell_power off: wait for cell_status_monitor to shutdown\n");
      while( cell_status_monitor_running());
      if(verbose) printf("cell_power off: detected cell_status_monitor termination.\n");
  }  else {
     //debuglog(LOG_INFO, "cell_power off: cell_status_monitor not running.");
     if(verbose) printf("cell_power off: cell_status_monitor not running.\n");
 debuglog(LOG_INFO, "cell_power off: cell_status_monitor not running.");
// TODO - the cellular radio may be in normal mode. We should make sure that it is in ariplane
//        mode. Need to detect type EC25 or other to get the port to open. (lsusb or cell_type)
//        open the port and send  switch to airplane mode and close port

// Assume the radio is set  to Airplane mode - should be the case for production
 // PLS62 uses AT+CFUN=1,1 which may not have finished  yet
//     Open_App_port();
//
//     // if (cell_type == 3){
//     //     ret = reset_radio_module();
//     // }
//     if ( in_normal_mode())
//         set_to_airplane_mode();
//     else  if(verbose) printf("cell_power off: Radio is already in Airplane mode, no action taken!");
//     CloseCOM();
     if(verbose) printf("cell_power off: clean up cell_power flag files.\n");
     if ( item_exists( CINTERION_POWER_ON_FILE) )  remove(CINTERION_POWER_ON_FILE);
     if ( item_exists( CELL_STATUS_FILE) )  remove(CELL_STATUS_FILE);
     if ( item_exists( CELL_TOWER_FILE) )   remove(CELL_TOWER_FILE);
     if ( item_exists( CELL_SIGNAL_FILE) )  remove(CELL_SIGNAL_FILE);
     if ( item_exists( CELL_MONITOR_SHUTDOWN) ) remove(CELL_MONITOR_SHUTDOWN);
     system("sync");

  }
  return ret;
}
/***********************************************************************
*
* Description: Power down radio by removing the power from the Radio module.
*              Cleanup tag files and clear Cell LED.
*
* Calling Arguments:
* Name               Mode      Description
* None
*
* Return Value:
*    Type      Description
*    Bool       Success or failure
*
*
******************************************************************************/
bool power_disable() {
FILE *fp;
int i;

  if( cell_status_monitor_running()) {
     if(verbose) printf("cell_status_monitor running, killing it!\n");
     system("killall cell_status_monitor");
  }

  // Quectel - gracefully shut down the modem
  // TODO: this could take some time The maximum time for unregistering network is 60 seconds.
  //       could look for the POWERED DOWN URC if URCs are not disabled
  /*if ( FileExists(CELL_TYPE_FILE) ){
             fp = fopen(CELL_TYPE_FILE, "rb");
             if(fp != NULL) {
               i = fgetc(fp);
               if (i > 4){
                    Open_App_port(QUECTEL_COMM_PORT);
                    RunAT("AT+QPOWD=0\r");
                    CloseCOM();
               }
               fclose(fp);
            }
  }
*/
   if (device_type > IS_ELS31){
      if(verbose) printf("disable power with echo off >/sys/gobi/state\n");
      system("echo off >/sys/gobi/state");
   } else {
       // TODO can use a AT command to power of Quectel gracefully if AT port open ???????
       system("echo 9 >/sys/class/i2c-adapter/i2c-0/0-0020/output1");
   }
//   CloseCOM();
   //sleep(1);
    if(verbose) printf("Clean up cell power flags and turn LED off\n");
    remove(CINTERION_POWER_ON_FILE);
    remove(CELL_STATUS_FILE);
    remove(CELL_TOWER_FILE);
    remove(CELL_SIGNAL_FILE);
    remove(CELL_TYPE_FILE);
    remove(CELL_ID_FILE);
    system("titan3_led cell 0 > /dev/null");
    system("titan3_led cell-sig 0 0 > /dev/null");   //turn off cell-sig
    if(verbose) printf("Power disabled from radio!\n")    ;
    return true;
}

/***********************************************************************
*
* Description: Module entry point. This module implements cell_power
*              command run from the commend line. Command line arg indicates
*              the operation to perform.
*
* Calling Arguments:
* Name               Mode      Description
* argv[1] may be "on", "off" or "disable"
*
* NOTE: optional option -v can be used to printout debug trace
*/
int main (int argc, char *argv[] ){
struct stat stFileInfo;
unsigned int ui;
char cmd[20];
bool status;

   //TODO Quectel board has no Tiny so we could get a false cell_not_mounted indicator
   if(stat("/tmp/cell_not_mounted", &stFileInfo) == 0){
      printf("Detected /tmp/cell_not_mounted but it is ignored.");
      //       return 0;
   }

   if (argc < 2 || argc > 3){
      printf("Error in number of arguments\n");
      printf(usage);
   } else {
      if (argc == 3) {
          if ( strcmp(argv[1], "-v") == 0) {
             strcpy(cmd, argv[2]);
             verbose = 1;
          } else if( strcmp(argv[1], "-t") == 0) {
             strcpy(cmd, argv[2]);
             test_mode = 1;
          } else {
              printf("Error in arguments\n");
              printf(usage);
              return 1;
          }
      } else {
          strcpy(cmd, argv[1]);
          if ( strcmp(cmd,"test") && strcmp(cmd,"enable") && strcmp(cmd,"enable1") && strcmp(cmd,"on") && strcmp(cmd,"off") && strcmp(cmd,"disable") && strcmp(cmd,"reset") ) {
             printf("Error in argv[1] = %s\n", argv[1]);
             printf(usage);
             return 1;
          }

      // We don't want to see errors
      FILE *stream ;
      stream = freopen("/tmp/file.txt", "w", stderr);
      }

      // if command is anything but 'enable' , check that enable was previously run successfully
      if ( strcmp(cmd,"enable") != 0) {
           if ( !item_exists( CELL_ENABLE_FILE)){
                debuglog(LOG_INFO, "cell_power %s - Was called but aborted because /tmp/cell_enabled is missing!",cmd);
                return 1;
           }
      }

      // check if the flag file indicates we are already in the required mode, if so exit without doing anything
      if ( strcmp(cmd,"enable") == 0 && item_exists(CELL_ENABLE_FILE)) {
            if(verbose) printf("cell_power enable: cell power already enabled, do nothing!");
           return 0;
      }
      if( strcmp(cmd,"on") == 0 &&  item_exists(CINTERION_POWER_ON_FILE)){
           if(verbose) printf("cell_power on: /tmp/cell_power_on flag detected, Power is on, do nothing!");
           return 0;
      }
  /*
      if ( strcmp(cmd,"off") == 0 && !item_exists(CINTERION_POWER_ON_FILE)) {
          if(verbose) printf("cell_power off: /tmp/cell_power_on flag Not detected, Power is off, do nothing!");
          return 0;
      }
*/
       if( strcmp( cmd,"enable" ) == 0 || strcmp( cmd, "off" ) == 0 )   {
             ui =  read_options();
             isExtendedMode = ( ui & OPT_EXTENDED_LED );
      }

     if ( strcmp(cmd,"enable") == 0) {
           debuglog(LOG_INFO, "cell_power enable START: Enabling cellular radio power...");
           // Open_App_port(); cant open port without knowing the type of modem PXS8 or EC25
           if ( power_enable() ){
                 if( verbose ) printf( "Enabled Power on Radio.\n" );
                 debuglog(LOG_INFO,"cell_power enable success");
           }else{
                 if( verbose ) printf("Failed to Enable Cellular power\n");
                 debuglog(LOG_INFO,"Failed cell_power enable.");
                 cmd[0] = '\0';
          }


      } else if ( strcmp( cmd,"on" ) == 0) {
         if( verbose ) printf("cell_power on START: Setting cellular radio to normal mode...");
          if ( power_on() ){
              debuglog(LOG_INFO, "cell_power: Powered on radio");
              if( verbose ) printf("Radio ready for use.\n");
          } else
              if(verbose) printf("Failed to start radio to normal mode!\n");
      } else if  (strcmp( cmd, "off" ) == 0) {
          if( verbose ) printf("cell_power off START: Setting cellular Radio to airplane mode...");
          if ( power_off() )
             if ( verbose ) printf("Radio in Airplane mode.\n");
      } else {  // disable power
          debuglog(LOG_INFO, "cell_power disable START: Remove power from cellular Radio...");
          if ( power_disable() )
                if ( verbose ) printf("Removed power from Radio!\n");
      }

//     CloseCOM();
      if ( error_msg[0] != '\0' ) {
           debuglog(LOG_INFO, error_msg);
      }
   }
   if ( strcmp(cmd,"enable") == 0)
         system("touch /tmp/cell_enabled");
   else if ( strcmp(cmd,"disable") == 0)
         system("rm /tmp/cell_enabled");
   system("sync");
   return 0;
}
