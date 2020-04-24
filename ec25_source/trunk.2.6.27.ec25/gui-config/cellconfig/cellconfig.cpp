/* This module is called from web client
* It can also be called from the command line with two arguments
*  cmd and providerIndex
*  It is assumed that the Cinterion device is present and power is enabled
*  and the radio is in Airplain mode.
*  We will open the modem serial port and use it for AT commands
*
*
*
* For Verizon if  ppp connection does not work, try to remove user and pass from your PPP options.
* If this still doesn’t work, then you may have lost the DMU keys.
* In case DMU keys are corrupted or lost, please request for Verizon to put the accounts back into DMU RESET state.

Exit messages are sent back to the browser. They have the format <prefix>,<msg>
The <prefix> is a two char sequence that is used by the GUI to display the message in the appropriate prompt
Failed - messages starting with Failed... get displayed and only allow [Cancel]  - low level failure should not hapen in production code
OK - success display message for two seconds and then store selection
ER - failure - display message and allow user to [Retry] or {Cancel]}
*/
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <cstdlib>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <string>
#include <sys/ioctl.h>
#include <vector>
#include <sstream>
#include <debuglog.h>
#include "../util/serial_at.h"
#include "../util/utils.h"
#include "../util/inifile.h"

using namespace std;

#define CELL_STATUS_FILE "/tmp/cell_status"
#define CELL_PXS8 1              // 3G , Verizon CDMA
#define CELL_PLS8 2              // 4G LTE includes Verizon and non Verizon
#define CELL_PLS62_W 3           // 4G LTE no Verizon
#define CELL_ELS31_V 4           // 4G LTE only Verizon
#define CELL_EC25_AF 5           // 4G Quectel
#define CELL_EC25_G  6           // 4G Quectel

/*
 0     - Signal strength is -n -dBm or = 0 if signal could not be detected\n\
 1     - Network type (0=GSM/UMTS, 1=LTE, 2=CDMA-EVDO, 3 = CDMA-ix/RTT)\n\
 2     - Reg status (0=not registered, 1=reg with home, 2=reg with roaming partner, 3=data session active)\n\
 3     - SIM ID ( NO = could not read, maybe no SIM)\n\
 4     - Bearer (0 = unknown, 2= 2G, 3=3G, 4= 4G, 5=CDMA)\n\
 5     - Device ID: IMID or EMID depending on type of network\n\
 6     - SIM Phone Number - could be unknown or if tech=CDMA will be blank\n\
 7     - Provider name\n\
 8     - SIM Ready\n\
 9     - Packet Switching info\n\
*/
#define SIG_STRENGTH  0
#define REG_STATUS    2
#define DEVICE_ID     5
#define SIM_PHONE_NUM 6
#define SIM_READY     8

extern HANDLE g_hDevice;
Logger  applog("cellconfig");

bool reset_radio_module(bool report_com_err = false);
void StopLogging();
bool set_to_airplane_mode();
void store_telnum();


// App port is serial and should always be present
//#define CENTERION_APP_PORT "/dev/ttyS2"

int cell_type = 0;
char line_buff[300];
bool got_4g_sim = false;
bool AcctIsUmts = true;
bool isNormalMode = false;
bool isCDMA = false;
char resp_buff[4000];    // big enough for PLS8 and PXS8 responses
char buff[200];
char mbuff[200];
char sigStrength[10];
char mdn[20];
char msid[20];
char debugbuff[200];
char msl[10];
char current_account[50];
char current_carrier[50];
char sim_phone_num[20];
char meid[50];
char exit_msg_buff[500];
bool reset_radio = false;
bool cli = false;   // true if this module is invoked from the command line, otherwise it is called as CGI script from browser
bool  Reactivate = false;
bool  verbose = true;
char * ProvNames[] = {(char *)"GenericUMTS", (char *)"AT & T (3008)", (char *)"Telefonica", (char *)"Telecom Itiala", (char *)"T-Mobile", (char *)"Orange", (char *)"Vodafone", (char *)"Sprint", (char *)"Verizon",(char *)"AT &T(3009)"};
char * CdmaProfiles[] = {(char *)"Sprint", (char *)"Verizon"};
char *OMADM_Urc[] = {(char *)"Connect Failed\r\n", (char *)"Activation Process Complete\r\n", (char *)"PRL Write\r\n", (char *)"PRL Update Process Complete\r\n", (char *)""};
char *Airplain_mode_Urc[] = {(char *)"AIRPLANE MODE",(char *)""};
char *Sysstart_Urc[] = {(char *)"SYSSTART",(char *)""};
char *VerizonActivate[] = {(char *)"Success", (char *)"Failure", (char *)"NO CARRIER", (char *)""};
char *Sprint_activate [] = {(char *)"Server Connect Failed", (char *)"Power Cycling", (char *)""};
int ProvIndex = 0;
int registered_state = 0;   // 0 = not registered, 1 = registered with home network, 2 = registered with roaming network
vector<string> cell_status_items;  // list of cell_status fields produced by cell_status_monitor

/*
 * This just wraps a access to debuglog
 */
void logmsg(const char *text) {
    debuglog(LOG_INFO, text);
}

// close serial port, if its open
void closeAtport() {
   logmsg("Closing Serial port /dev/ttyS2.");
   CloseCOM();
}

/*
* calculate dBm from raw value returned from CSQ command

 */
char  *clac_signal_strength(int val){
  switch(val){
      case -1:
      case 99:
      case 0:
        strcpy(line_buff, "-113 dBm or less");
        break;
      case 1:
        strcpy(line_buff, "-111 dBm");
        break;
      default:
         val = 111 - (val-1) * 2;
         sprintf(line_buff,"-%d dBm", val);
        break;
  }
  return line_buff;
}

void log_cell_status() {
int i, network_type;
char buf[150];
bool cdma = false;
bool is4G = false;

         for (i=0; i < cell_status_items.size(); i++){
               if (i == 0){
                   // clac_signal_strength( atoi( cell_status_items[i].c_str()) );
                    if(cell_status_items[i][0] == '0')
                       sprintf(buf,"Signal Strength: Undetectable\n");
                    else
                         sprintf(buf,"Signal Strength: %s dBm\n", cell_status_items[i].c_str());
                    debuglog(LOG_INFO, buf);
               } else if( i == 1){
                     network_type = atoi(cell_status_items[i].c_str());
                     switch (network_type) {
                         case 0: strcpy(line_buff, "GSM/UMTS"); break;
                         case 1: strcpy(line_buff, "LTE"); break;
                         case 2: strcpy(line_buff, "CDMA-EVDO"); break;
                         case 3: strcpy(line_buff,"CDMA-IX/RTT"); break;
                         default:
                            strcpy(line_buff,"Unknown");
                   }
                    sprintf(buf,"Network type: %s\n", line_buff);
                    debuglog(LOG_INFO, buf);
               } else if(i == 2){
                         if( cell_status_items[i] == "0") {
                             strcpy(line_buff, "Not Registered - Searching");
                         } else  if( cell_status_items[i] == "1") {
                             strcpy(line_buff, "Registered on Home Network");
                         } else if (cell_status_items[i] == "2") {
                            strcpy(line_buff, "Registered on Roaming Network");
                         }
                     sprintf(buf,"Reg. Status: %s\n", line_buff);
                     debuglog(LOG_INFO, buf);
               } else if(!isCDMA && i == 3)   {    //CDMA has no SIM but 3GUMTS and 4G do
                    if ( cell_status_items[i] == "NO")  {
                          strcpy(line_buff, "Failed to read SIM");
                    } else {
                          strcpy(line_buff,cell_status_items[i].c_str() );
                    }
                    sprintf(buf,"SIM Id: %s\n", line_buff);
                    debuglog(LOG_INFO, buf);
               } else if(i == 4){
                   switch (atoi(cell_status_items[i].c_str())) {
                         case 2: strcpy(line_buff, "2G"); break;
                         case 3: strcpy(line_buff, "3G"); break;
                         case 4: strcpy(line_buff, "4G");
                                 is4G = true;
      debuglog(LOG_INFO, "4G detected");
                                 break;
                         case 5:
                            strcpy(line_buff,"CDMA");
                            cdma= true;
    debuglog(LOG_INFO, "CDMA detected");
                            break;
                         default:
                            strcpy(line_buff,"Unknown");
                   }
                  sprintf(buf,"Bearer: %s\n", line_buff);
                  debuglog(LOG_INFO, buf);
               } else if(i == 5){
                    if (cdma )
                        strcpy(line_buff, "MEID:" );
                    else
                        strcpy(line_buff,"IMEI:");
                    sprintf(buf, "%s %s\n", line_buff, cell_status_items[i].c_str());
                    debuglog(LOG_INFO, buf);
               } else if( i == 6){
                     sprintf(buf,"SIM Phone Number: %s\n", cell_status_items[i].c_str());
                     debuglog(LOG_INFO, buf);
                     #ifdef Physio
                          if(!cdma)
                               strcpy(mdn, cell_status_items[i].c_str());
                          store_telnum();
                      #endif
               } else if(i == 7){
                    sprintf(buf,"Provider Name: %s\n", cell_status_items[i].c_str());
                    debuglog(LOG_INFO, buf);
               } else if(i == 8) {
                   if (cell_status_items[i] == "1"){
                      debuglog(LOG_INFO, "SIM detected: YES");
                   } else
                      debuglog(LOG_INFO, "SIM detected: NO");
               } else if( i ==9){
                      debuglog(LOG_INFO, "Packet Switch Type: %s", cell_status_items[i].c_str() );
               }
         }
}

void app_exit(char *msg = NULL) {

    if (msg != NULL)
       printf(msg);
    else
       printf(exit_msg_buff);

    if (!cell_status_items.empty()){
         log_cell_status();
    }

    set_to_airplane_mode();
    closeAtport();
    StopLogging();
    exit(0);
}


char *covertToUpper(char *str){
    char *newstr, *p;
    p = newstr = strdup(str);
    while(*p++=toupper(*p));
    return newstr;
}

/*
 * split a string on specified delimiter nd return new vector of items parsed
 * vector <string> x = split_str("one:two:three::five")
 * x has 5 items with the forth one empty
 */
vector<string> &split(string &s, char delim, vector<string> &elems) {
    stringstream ss(s);
    string item;
    while (getline(ss, item, delim)) {
        elems.push_back(item);
    }
    return elems;
}
vector<string> split_str( string &str, char delim){
vector<string> items;
    split(str, delim, items );
    return items;
}


/*
 * Dump buffer as ASCII hex to debug log
 */
void hexdumplog(char *msg, char *ptr, int buflen) {
char *p = ptr;
char buf[2];
int i;
string sTemp = "";

    if (msg != NULL && msg[0] != '\0')
       sTemp = msg;
    buf[1] = '\0';
    for(i=0; i<buflen; i++,p++) {
         if (*p == '\x0d')
            sTemp.append("<CR>");
         else if(*p == '\x0a')
            sTemp.append("<LF>");
         else {
            buf[0] = *p;
            sTemp.append(buf);
         }
    }
    debuglog(LOG_INFO, sTemp.c_str());
}
char rbuf[20];

/*
 * parse integer value from an AT response and return a pointer
 * to a string that holds the value.
 *
 * Use for MDN, MSID, PRL etc
 * title - the name of the parameter
 * data - AT command response data
 *
 * Returns: pointer to string va;ue
 *          NULL if parsing error
 */
char *parse_int_value(char *title, char *data){
char *p;
int i, length;

  p = strstr(data,title);
  if ( p == NULL) return NULL;
  i = 0;
  memset(rbuf,0, sizeof(rbuf));
  length = strlen(data);
  // look for first digit
  while(!isdigit(*p)) {
      p++;
      if(++i >= length) break;
  }
  if (i < length) {
      i = 0;
      while(isdigit(*p)) rbuf[i++] = *p++;
  } else return NULL;

  sprintf(debugbuff,"%s: = %s", title, rbuf);
  debuglog(LOG_INFO, debugbuff);
  return rbuf;

}


bool RunAT( const char * cmd, const  char* urc = NULL, DWORD toMillSec = 12000 ) {
bool ret;

       if ( verbose )  hexdumplog((char *)"Send AT Command:", (char *)cmd, strlen(cmd));
      resp_buff[0] = 0;
      PurgeCOM();
      if (urc == NULL) {
            ret = ATCommand(cmd, resp_buff, ARRAYSIZE(resp_buff));
      } else
           ret =  ATCommandUrc(cmd, resp_buff, ARRAYSIZE(resp_buff),  toMillSec,  (char *)urc);
      if (verbose)   hexdumplog((char *)"AT Response:", (char *)resp_buff, strlen(resp_buff));
      return ret;
}


/* parse specified integer value from an AT response
 * and return a pointer to a string that holds the value.
 *
 * Use for MDN, MSID, PRL etc
 * title - the name of the parameter as it is in the response ie: "CSQ: "
 * data - AT command response data
 * num - the integer to be parsed 0...n
 *
 * Assumes integers are separated by ","
 *
 * Returns: pointer to string value
 *          NULL if parsing error
 */
char *parse_int_value(char *title, char *data, int num){
char *p, *p1;
int i, length;

  p = strstr(data,title);
  if ( p == NULL) return NULL;

  p += strlen(title);
  // find first integer digit

  i = 0;
  while(!isdigit(*p)) {
     p++;
     if(++i > 20) return NULL;  // should find digit
  }
  p1 = p;

  for (i=0; i < num; i++){
     p = strchr(p1,',');
     if (p == NULL) return NULL;
     p++;
     p1 = p;
  }

  // at this point p points to start of field we want
  i = 0;
  memset(rbuf,0, sizeof(rbuf));
  while(isdigit(*p)) rbuf[i++] = *p++;
  return rbuf;

}

void openAtport() {
    if (cell_type > 4){
           logmsg("Opening Serial port /dev/ttyUSB2 to access Cinterion AT interface.");
           if ( !OpenCOM((char *)QUECTEL_COMM_PORT , BAUD_115200 )) {
                logmsg("Failed Open Quectel Comm Port ttyUSB2!");
                exit(1);
           }
           sprintf(debugbuff, "Opened /dev/ttyUSB2 successfully");          
    } else {
           logmsg("Opening Serial port /dev/ttyS2 to access Cinterion AT interface.");
           if ( !OpenCOM((char *)CENTERION_APP_PORT, BAUD_115200 )) {
                logmsg("Failed Open Cinterion Comm Port ttyS2!");
                exit(1);
           }
           sprintf(debugbuff, "Opened /dev/ttyS2 successfully");
    }
    logmsg(debugbuff);

}

bool get_cell_status(){
FILE *fp;
char line[200];
string content;

    fp = fopen(CELL_STATUS_FILE, "rb");
    if (fp != NULL) {
       cell_status_items.clear();
       fread(line, 1, 200, fp);
       fclose(fp);
       content = line;
       cell_status_items = split_str( content, '\t');
       return true;
    }else
         debuglog(LOG_INFO,"cellconfig: Failed to find %s", CELL_STATUS_FILE);
    return false;
}

void shutdown_cell_status_monitor(){
 int count;
     debuglog(LOG_INFO,"cellconfig: Terminate cell_status_monitor...");
     system("touch /tmp/cell_monitor_shutdown"); // cell_status_monitor terminates with last raw scan in cell_status_raw, set to airplane mode
     system("sync");
      // wait for cell_status_monitor to terminate
      sleep(3);
      for (count=0;count < 10;count++) {
          WaitMs(2000);
          if ( !item_exists("/tmp/cell_status") ) {
            break;
          }
      }
      if (count == 10)
         debuglog(LOG_INFO,"cellconfig: cell_status_monitor did not terminate normally!");


}

/* note: Time the call because  the ELS31-V takes a long time to switch from Airplane to Normal */
bool check_for_sim(){  // depricated we now check for SIM in this module before starting cell_status_monitor
int i = 0;
int x =0;

   for(i=0; i < 30; i++){
         if (item_exists("/tmp/cell_status") ) {
              get_cell_status();
              if( cell_status_items[SIM_READY] == "1")
                     return true;
              if (++x > 10) break;
         }
         WaitMs(1000);
   }
   return false;
}

void run_cell_status_monitor(){
CIniFile  iniFile;
char *p;
int i;
int reg_wait_count = 60;
int act_wait_count = 300;   // 5 min
int low_threshold;
bool found = false;
bool has_phone_num = false;
int sig_strength = 0;

     closeAtport();
     iniFile.OpenIniFile("/tmp/tmp.ini");
     p =  (char *)iniFile.ReadString("TEMP", "cell_reg_timeout", "60");
     reg_wait_count = atoi(p);
     p =  (char *)iniFile.ReadString("TEMP", "cell_act_timeout", "30");
     act_wait_count = atoi(p);
     p =  (char *)iniFile.ReadString("TEMP","cellular_poor_threshold","90");
     low_threshold = atoi(p);
     iniFile.CloseIniFile();

     debuglog(LOG_INFO,"Closing comport and starting up cell_status_monitor with cell_reg_timeout =%d, cell_act_timeout =%d, ...", reg_wait_count, act_wait_count);

     system("cell_status_monitor &");
//     if(cell_type == CELL_PXS8)
//        ; // We check PXs8 UMTS SIM before calling this function
//     else  if(!check_for_sim()){
//          debuglog(LOG_INFO,"Failed to access SIM!");
//          get_cell_status();
//          shutdown_cell_status_monitor();
//          app_exit("SIM invalid or missing, Insert SIM.");
//      }


     // wait for registration status
      debuglog(LOG_INFO,"Waiting for network registration...");
      // wait for cell_reg_timeout for /tmp/cell_tower to appear
      //debuglog(LOG_INFO,"Waiting for cell_tower info...");
      for (i=0; i < reg_wait_count; i++) {
           sleep(1);
           if ( item_exists("/tmp/cell_tower") ) {
                found = true;
                debuglog(LOG_INFO,"Found cell_tower!");
                break;
           }
      }

      if (found){  // locked on to a cell tower
          for(i=0;i < reg_wait_count; i++){
               get_cell_status();
               if (!cell_status_items.empty()){
                  if ( cell_status_items[REG_STATUS] != "0") break;
               }
               sleep(1);
          }
          if (i == reg_wait_count){
               debuglog(LOG_INFO,"Failed to register with the network!");
               get_cell_status();
               shutdown_cell_status_monitor();
               app_exit("Failed to register with the network!");
          } else {
              if( cell_status_items[REG_STATUS] == "1"){
                   registered_state = 1;
                   debuglog(LOG_INFO,"Device is registered on Home network!");
              } else {
                   registered_state = 2;
                   debuglog(LOG_INFO,"Device is registered with roaming partner!");
              }

              // if 4G look for Phone number in SIM
//   applog.log("check if we have 4G");
               if(cell_type > CELL_PXS8){
 //  applog.log("Check for 4G SIM phone number...");
                   debuglog(LOG_INFO,"Check for 4G SIM phone number...");
                   for(i=0; i < act_wait_count/5; i++){
//             applog.log("Call get_cell_status()...");
                       get_cell_status();
                       if (!cell_status_items.empty()){
//        applog.log("Got cell status data");
                            strcpy(mdn, cell_status_items[SIM_PHONE_NUM].c_str());
                            if (strlen(mdn) > 1 && strcmp(mdn,"Unknown")){
                                has_phone_num = true;
//applog.log("Found phone number in data = '%s', exit wait loop!",mdn);
                                break;
                            }
                       }
                       if (i == 0){
                            debuglog(LOG_INFO,"No SIM Phone number detected, wait up to  %d seconds for activation from the network....", act_wait_count);
                       }
                       sleep(5);
                   }
//       applog.log("Out of wait loop!");
                   if(!has_phone_num){
                       applog.log("Failed to activate with the network. A phone number has not been provisioned for this device.");
                       debuglog(LOG_INFO,"Failed to activate with the network. A phone number has not been provisioned for this device.");
                   }
              }
          }

      } else {
         debuglog(LOG_INFO,"Failed to register with the Network (cell_tower info not found) in %d seconds", reg_wait_count);
         get_cell_status();
         shutdown_cell_status_monitor();
         app_exit("Failed to register with the Network");
      }

 applog.log("Selection OK, Shut down cell_status_monitor...");
      strcpy(exit_msg_buff,"OK");
      shutdown_cell_status_monitor();
      openAtport();
}

bool in_normal_mode( ){
  if ( RunAT( "AT+CFUN?\r") ){
        if ( strstr(resp_buff,"FUN: 1")  != NULL) return true;
  } else {
    app_exit("Failed to connect to cellular Radio!");
  }
  return false;
}

/* note: if already in Airplane mode there will be no URCs */
bool set_to_airplane_mode() {
bool ret = true;

     // check if we are already in Airplane mode
     if ( !isNormalMode ) return true;

     if (cell_type == CELL_ELS31_V)
        ret = RunAT("AT+CFUN=4\r");
     else
        ret = RunAT("AT+CFUN=4\r", "AIRPLANE MODE\r");

     if ( !ret ) {
         logmsg("Failed to set to Airplane mode!");
         isNormalMode = true;
    } else isNormalMode = false;
     return ret;
}

/* call this function after set_to_airplain_mod was called
 * expect a SYSSTART URC
 * NOTE: if already in normal mode there will be no URC
 */
bool set_to_normal_mode(){
bool ret = true;

     if (  isNormalMode  ) return true;

     if (cell_type == CELL_ELS31_V)
        ret = RunAT("AT+CFUN=1\r");
     else
        ret = RunAT("AT+CFUN=1\r", "SYSSTART\r");

     if( !ret ){
          if  ( in_normal_mode( ) )
             ret = true;
     }
     if  ( !ret ){
            logmsg("Failed to set to Normal mode!");
            isNormalMode = false;
    } else
            isNormalMode = true;
     return ret;

}

/*
 * Set PXS8 MEopMode/AccT for UMTS or CDMA
 * must be in Normal
 */
void set_account(bool set_to_UMTS) {
bool ret;

   if (set_to_UMTS)
        ret = RunAT( "AT^SCFG=\"MEopMode/AccT\",\"GSM/UMTS\"\r");  // Normal mode
   else
        ret =  RunAT( "AT^SCFG=\"MEopMode/AccT\",\"cdma2000\"\r" ); // Normal mode

}


/* check if CDMA carrier is activated
* For Cinterian it is recommended that three AT commands be used to
* verify if a CDMA carrier is active.
*
* AT$QCMIPNAI?
* AT$MDN?      <- not Airplane mode
* AT$MSID?     <- not Airplane mode
*
* The factory defaults for these are:
* Verizon:
* AT$QCNIPNAI?   0000001211@vzw3g.com,1
* AT$MDN?        $MDN: 0000001211
* AT$MSID?       $MSID: 0000001211
*
* Sprint:
* AT$QCNIPNAI?   99000218994706@hcm.sprintpcs.com,1
* AT$MDN?        $MDN: 0000007563
* AT$MSID?       $MSID: 0000007563
*/
bool check_activation_status() {
char  *p;

  mdn[0] = '\0';
  msid[0] = '\0';

  // we have to be in Normal mode in order to get MDN
  set_to_normal_mode();

  // assume we are in normal mode AT+cfun=1
  if ( RunAT("AT$MDN?\r") ) {
     p = parse_int_value((char *)"MDN: ", resp_buff, 0);
     strcpy(mdn,p);
  }


/*
  if (RunAT("AT$MSID?\r")) {
     p = parse_int_value((char *)"MSID: ", resp_buff, 0);
     strcpy(msid,p);
  }
*/
  if (strstr(mdn,"000000") != NULL ){
     return false;
 } else
      return true;

}

/* when called mdn could be blank, contain "unknown" or
have a valid phone number
*/

void store_telnum() {
CIniFile  iniFile;
    iniFile.OpenIniFile("/tmp/tmp.ini");
    if (mdn[0] == 0)
      iniFile.WriteString("TEMP", "autoTellNumValue", "");
    else
       iniFile.WriteString("TEMP", "autoTellNumValue", mdn);
   logmsg( "Writing autoTellNumValue to tmp.ini...");
   iniFile.WriteIniFile("/tmp/tmp.ini");
   iniFile.CloseIniFile();

}

void log_pdp_context(){
char *p, *p1;

  resp_buff[0] = 0;
  PurgeCOM();
  if ( ATCommand("AT+CGDCONT?\r", resp_buff, ARRAYSIZE(resp_buff)) ){
  //if ( RunAT("AT+CGDCONT?\r") ){
       for(p = strstr(resp_buff, "CGDCONT:");p != NULL; p = strstr(p1,"CGDCONT:")) {
          p1 = strstr(p,"\r");
          *p1 = '\0';
          logmsg(p);
          p1++;
       }
  }

}

void ActivateFailed() {
int i;
char *p, *p1;
applog.log("Activation failed");
      if (registered_state == 1)
           strcpy(debugbuff, "Failed to activate on Home Network! ");
      else if (registered_state == 2)
          strcpy(debugbuff, "Failed to activate on Roaming Network! ");

 //TODO: ELS31-V returns numeric values not strings, two possible numeric values  - +CEER: <lastEmmCause>, <lastEsmCause>
      if ( RunAT("AT+CEER\r") ) {
         // response example: +CEER: No cause information available
         p = strchr(resp_buff,'\r');
         *p = 0;
         p1 = strstr(p, "No cause");

         if (p1 != NULL){
              strcat(debugbuff, resp_buff);
         } else {
             p1 = strstr(p, "No report");
             if (p1 != NULL){
                  strcat(debugbuff, resp_buff);
             }
         }
      }

      if ( RunAT("AT$MIPERR?\r") ) {
          p = parse_int_value((char *)"MIPERR: ", resp_buff, 0);
          i = atoi(p);
          if (i > 0) {
             if (i == 67 || i == 68 || i == 131 || i == 132)
                strcat(debugbuff, "Account provisioning issue.");
             else{
                sprintf(mbuff," MIP Error %d", i );
                strcat(debugbuff,mbuff);
             }
          }
      }

      debuglog( LOG_INFO, debugbuff );
      app_exit( debugbuff );
}

/*
  Mobile Equipment Identifier (MEID), hexadecimal format if currently used access technology is "cdma2000".
*/
void ActivateSuccess() {
  applog.log("Activation success");
    // check that the activation process finished correctly
    if(ProvIndex == 7)
        sprintf(debugbuff, "Sprint CDMA radio is activated with the network with MDN = %s", mdn);
    else
        sprintf(debugbuff, "Verizon CDMA radio is activated with the network with MDN = %s", mdn);
    logmsg(debugbuff);


//#ifdef Physio
//  store_telnum();
//#endif
     app_exit("OK");

}

void get_4g_sim_state() {
char *p;
int i;
char sim_tag_buff[20];
char sim_cmd[20];


if ( cell_type > 4){
     strcpy(sim_cmd,"AT+QCCID\r");
     strcpy(sim_tag_buff, "QCCID: ");
}else if( cell_type == CELL_PLS62_W){
     strcpy(sim_cmd,"AT+CCID?\r");
     strcpy(sim_tag_buff, "CCID: ");
}else {
     strcpy(sim_cmd,"AT^SCID\r");
     strcpy(sim_tag_buff,"SCID: ");

}

//  for(i=0;i< 5;i++){
     if (RunAT(sim_cmd) ) {
           p = strstr( resp_buff, sim_tag_buff);
           if (p != NULL) return;
     }
   //  sleep(1);
//  }
  debuglog(LOG_INFO,"Failed to read 4G SIM, may not be present!");
  app_exit("Failed to read SIM!");

}

/* check SIM status with AT^SIND? command and look at simstatus field
        ........ simstatus,0,n<cr><lf>.......
           n = 0  - SIM not inserted
           n = 1  - SIM inserted but not initialized
           n = 5  - SIM inserted and initialization completed
we only want to continue with the SIM if n = 5 otherwise Phone number is Unknown
return true if status = 5
otherwise return false
*/
void get_umts_sim_state () {
char *p,*p1;
int count;
int state = 0;
bool end = false;

    for(count=0; count < 6; count++){
           if ( RunAT( "AT^SIND?\r") ) {
               p = strstr(resp_buff,"simstatus");
               if (p != NULL) {
                     p1 = strchr(p,',');   // find the first , in the string
                     p1++;
                     p = strchr(p1,',');   // find the second ,
                     p++;
                     if (*p == '0') state = 0;
                     else if( *p == '1') state = 1;
                     else if  (*p == '5'){
                          state = 5;
                          end = true;
                     }
               } else {
                     debuglog(LOG_INFO,"AT^SIND did not return simstatus!");
                     return;
               }

          }
          if(end) break;
          sleep(1);
    }
    if (state == 0) {  // no SIM detected
          debuglog(LOG_INFO,"Failed to detect 3G SIM");
          app_exit("No SIM detected.");
    } else if(state == 1){ // SIM detected but SIM error
          debuglog(LOG_INFO,"3G SIM error");
          app_exit("SIM error - SIM not ready");
    }
    debuglog(LOG_INFO,"SIM detected and Ready");

}

void select_umts() {

    logmsg("Selecting 3G carrier GSM/UMTS...");
    if ( RunAT("AT^SCFG=\"MEopMode/AccT\"\r") ) {
         if ( strstr(resp_buff,"GSM/UMTS" ) == NULL){
             if ( !set_to_normal_mode() ) {
                  app_exit("Failed to set normal mode");
             }
             logmsg("Switching account to GSM/UMTS...");
             if ( RunAT( "AT^SCFG=\"MEopMode/AccT\",\"GSM/UMTS\"\r") ) {
                 reset_radio_module();
             }
         }
     } else
         app_exit("Failed AT command!");

    get_umts_sim_state();

     // test that we can register with the network with current configuration
     run_cell_status_monitor();
     app_exit();
}

void reset_radio_power(){

  logmsg("Close modem App port and perform radio power reset...");
  closeAtport();
  system("cell_power disable > /dev/null");
  system("cell_power enable > /dev/null");
  openAtport();
  if(!wait_for_connection( 10 ) ) {
       app_exit("Failed to connect to Radio after radio power reset!");
  }
  /* assume the Radio is initially in Airplane mode, config_start was previously called */
  isNormalMode = in_normal_mode( );
  if(isNormalMode){
     logmsg("Detected Normal mode but expected Airplane Mode!");
     set_to_airplane_mode();
  }
}

bool select4g(int ProvIndex){
char cmd[50];
bool selectNonVerizon = false;
 applog.log("Select 4G provider with provider id = %d...", ProvIndex);
for(int i = 0; i < 2; i++){
  cmd[0] = '\0';

  if (cell_type > CELL_ELS31_V){
      ;
  } else if( cell_type != CELL_ELS31_V && cell_type != CELL_PLS62_W) {
       RunAT("AT^SCFG=\"MEopMode/prov/cfg\"\r");  // check the current opmode
       if (ProvIndex == 8){
            if (strstr(resp_buff,"\"2\"")) {
                logmsg("Verizon LTE previously selected.");
            } else {
               logmsg("Selecting Cinterion Verizon LTE provider...");
               strcpy(cmd, "AT^SCFG=\"MEopMode/prov/cfg\",\"2\"\r");
          }
       } else {
            selectNonVerizon = true;
            if (strstr(resp_buff,"\"1\"")){
                 logmsg("GSM LTE previously selected.");
            } else {
               logmsg("Selecting Cinterion Non-Verizon LTE provider...");
               strcpy(cmd, "AT^SCFG=\"MEopMode/prov/cfg\",\"1\"\r");
            }
       }
       if( cmd[0] != '\0'){
           if( !RunAT(cmd))  // run the command to set the mode we want
              app_exit("Failed to select 4G carrier!");
           if ( !reset_radio_module(true)) {
               reset_radio_power();
               continue;
           }
       }

       if(selectNonVerizon ){
           RunAT("AT+CGDCONT=1,\"IPV4V6\",\"\"\r");
       } else {
           RunAT("AT+CGDCONT=1,\"IPV4V6\",\"vzwims\",\"\"\r");
       }
  } else if (cell_type == CELL_PLS62_W){
          RunAT("AT+CGDCONT=1,\"IPV4V6\",\"\"\r");
     //   reset_radio_module();
  }
  break;
}
  // test for SIM and  we can register with the network with current configuration
  run_cell_status_monitor();

  //TODO: if there is no SIM phone number monitor and wait for it to be received by the activation process
  //      if not received within 120 secs the it was not activated - indicate this in log
  //      see function get_sim_telenum() from cell_status_monitor.cpp

 // debuglog(LOG_INFO, "Device MEID: \"%s\"", cell_status_items[DEVICE_ID].c_str());

 // #ifdef Physio
 //    store_telnum();
 // #endif

 // debuglog(LOG_INFO, "SIM Phone Number: %s", mdn);

  log_pdp_context();
  app_exit();

}


void select_verizon( bool reactivate) {
bool loadAccount = true;
bool loadOperator = true;
bool activate_success = false;
bool ret;

  isCDMA = true;
 applog.log("Select 3G Verizon CDMA...");
  if ( RunAT("AT^SCFG=\"MEopMode/AccT\"\r") ) {
     if (strstr(resp_buff,"cdma2000") != NULL)
         loadAccount = false;
  }  else {
      app_exit("Failed to read current account!");
  }
   // determine the  current CDMA carrier */
  if ( RunAT( "AT^SCFG=\"CDMA/Operator/Store\"\r") ) {
      if (strstr(resp_buff,"Verizon") != NULL)
              loadOperator = false;
  } else {
      app_exit("Failed to read CDMA profile from Radio!");
  }

  if( loadOperator ) {
      if ( ! loadAccount ) { // if Sprint CDMA was previously active
          ret = RunAT( "AT^SCFG=\"CDMA/Operator/Store\",1,\"Sprint\"\r");
          if (!ret ) {
               app_exit("Failed to save CDMA profile in Radio!");
          }
      }
      ret = RunAT( "AT^SCFG=\"CDMA/Operator/Store\",0,\"Verizon\"\r");
      if (!ret ) {
           app_exit("Failed to switch to CDMA profile in Radio!");
      }
  }

  if( loadAccount ) {
      if ( !set_to_normal_mode() ) {
            app_exit("Failed to set normal mode");
      }
      ret =  RunAT( "AT^SCFG=\"MEopMode/AccT\",\"cdma2000\"\r" );
  }

  if ( loadAccount || loadOperator ) {
        // if we had to switch carriers a rest is required
        reset_radio_module();  // assume we reset into Airplane mode
  }


//##########################################################################################################################################################
// test that we can register with the network with current configuration
  run_cell_status_monitor();

 if (!check_activation_status()) {
      debuglog(LOG_INFO,  "CDMA device has not been activated!");
      mdn[0] = '\0';
 } else {
      debuglog(LOG_INFO,  "CDMA device has previously been activated!");
      if (!reactivate) {
           debuglog(LOG_INFO,"Radio was previously activated, MDN is valid");
           ActivateSuccess();
      } else
           debuglog(LOG_INFO,  "CDMA device will be re-activated...");
 }

  // check activation status
  if ( !isNormalMode) {
      if ( !set_to_normal_mode() ) {
          app_exit("Failed to switch to Normal mode");
      }
      sleep(1);  // had to add this after updating firmware to 03.320 other wise next command would not receive response just <CR><LF>
  }

  debuglog(LOG_INFO,  "Starting CDMA activation sequence for Verizon...");

  RunAT("AT+CEER=0\r");  // reset extended error

  // dial to start activation process, wait for up to 2 mins
  resp_buff[0] = '\0' ;
  RunAT ("ATD*22899\r",  "NO CARRIER\r\r", 120000);
  if (resp_buff[0] != '\0') {
      if ( strstr(resp_buff,"ACTIVATION: Success") != NULL ) {
              // check MDN to see if we are truly Activated
              if ( check_activation_status() ) {
                  ret = RunAT("AT^SICO=201\r");
                  if (!ret){
                      if (strstr(resp_buff, "OK") != NULL){
                         ret = true;
                      }
                      sleep(1);
                      ret = RunAT("AT^SICO=201\r");
                  }
                  if (ret) {
                     // we need to be in Airplane mode for next AT command
                     set_to_airplane_mode();
                     debuglog(LOG_INFO, "Store new carrier profile after activation.");
                     ret = RunAT( "AT^SCFG=\"CDMA/Operator/Store\",1,\"Verizon\"\r");
                     if (!ret) {
                          debuglog(LOG_INFO, "Failed to save Verizon Profile after Successful Activation!");
                          app_exit("Failed to save Verizon Profile after Successful Activation!");
                     }
                     activate_success = true;
                 }
              }
      }  else  if ( strstr(resp_buff,"ACTIVATION: Failure") != NULL ) {
           activate_success = false;
      } else {
          debuglog(LOG_INFO, "No meaningful URC found!");
      }
  } else {
       debuglog(LOG_INFO, "Failed to get Verizon activate URC!");
  }
  if (activate_success){
       ActivateSuccess();
  } else {
       sprintf(debugbuff, "Activation Failed!");
      ActivateFailed();
  }
}

/* initially PXS8 Cinterion could be configured for UMTS, Verizon or Sprint */
void reset_cdma() {
char buff[100];
bool ret;

     logmsg("CDMA reset...");
     buff[0] = 0;
     for (;;) {
         if ( RunAT("AT^SCFG=\"MEopMode/AccT\"\r") ) {
            if (strstr(resp_buff,"cdma2000") == NULL) {
               // we are currently in UMTS account
               // switch Account to cdma2000
               if ( !set_to_normal_mode() ) {
                     strcpy(buff,"Failed to set normal mode");
                     break;
                }
                ret =  RunAT( "AT^SCFG=\"MEopMode/AccT\",\"cdma2000\"\r" );
                reset_radio_module();
            }
         }  else {
              strcpy(buff,"Failed to read account!");
              break;
         }
         //  load factory settings for CDMA carriers
         ret = RunAT( "AT^SCFG=\"CDMA/Operator/Store\",2,\"Sprint\"\r");
         if (!ret ) {
             strcpy(buff,"Failed to load factory settings for Sprint CDMA profile!");
             break;
         } else {
            logmsg("Restore Sprint to factory settings for CDMA activation.");
            reset_radio_module();
         }
         ret = RunAT( "AT^SCFG=\"CDMA/Operator/Store\",2,\"Verizon\"\r");
         if (!ret ) {
             strcpy(buff,"Failed to load factory settings for Verizon CDMA profile!");
             break;
         }
         logmsg("Restore Verizon to factory settings for CDMA activation.");
         reset_radio_module();
         #ifdef Physio
           mdn[0] = 0;
           store_telnum();
         #endif
         printf("OK");
         break;
     }
     if (buff[0] != 0){
          logmsg(buff);
          app_exit(buff);
     }
}

void StartLogging() {
    //system("rm usr/sbin/http/cgi-bin/SvContent/configapp.log");
    logmsg("########### CELLULAR CONFIG STARTED ###########");
}

void StopLogging() {
char line[300];
FILE *fp;

    logmsg("########### CELLULAR CONFIG FINISHED ############");
    sleep(1);
}

char * DecodeURL(char *URL){
  int i,x;
  int len = strlen(URL);

  for (i = 0, x =0; i < len; i++) {
    int j = i ;
    char ch = URL[i];
    if (ch == '%'){
      char tmpstr[] = "0x0__";
      int chnum;
      tmpstr[3] = URL[j+1];
      tmpstr[4] = URL[j+2];
      chnum = strtol(tmpstr, NULL, 16);
      mbuff[x++] = chnum;
      i += 2;
    }
    else
      if(ch == '+')
        mbuff[x++] = ' ';
        else {
         mbuff[x++] = ch;
        }
  }
  return mbuff;
}


/* Monitor CTS which will be dropped when the Radio resets
 TIOCM_CTS = 0x20
 */
void  MonitorCTS(DWORD dwTimeOut) {
int iSerStat;
ULONG64 ullTime;

 ullTime = GetTickCount();
 while (ioctl(g_hDevice, TIOCMGET, &iSerStat) == 0) {
    if (iSerStat & TIOCM_CTS) {
          WaitMs(WAIT_AFTER_CTS_ON);
          printf("CTS  true\n");
    }
    if (!(iSerStat & TIOCM_CTS)) {
        WaitMs(WAIT_AFTER_CTS_OFF);
        printf("CTS false\n");

    }
    if ((DWORD)(GetTickCount() - ullTime) > dwTimeOut) {
         break;
    }
    WaitMs(WAIT_DUMMY);
  }
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
bool wait_for_enumeration(int enumerate_to){
FILE *fp;
char buff[200];
int i;
   debuglog(LOG_INFO,"Waiting for Enumeration...");
   for (i=0; i < enumerate_to; i++ ) {
         fp = popen("lsusb", "r");
         if (fp != NULL) {
               while ( fgets(buff, sizeof(buff), fp) ) {
                   if ( strstr(buff, ":0053") || strstr(buff,":0061") || strstr(buff, ":005b") || strstr(buff, ":00a0") || strstr(buff, ":0125"))
                        return true;
                }
         }
         pclose(fp);
         WaitMs(1000);
   }
   return false;

}

bool reset_radio_module(bool report_com_err) {
int i;
     logmsg("Resetting Radio Module...");
     RunAT("AT+CFUN=1,1\r");
     logmsg("Waiting for Radio reset...");
     PurgeCOM();
     resp_buff[0] = 0;

     if (!wait_for_enumeration(60)) {
          debuglog(LOG_INFO,"Failed to enumerate after 60 seconds!");
          app_exit("Device failed to enumerate after reset!");
     } else {
          debuglog(LOG_INFO,"Enumeration successful!");
     }

     if ( !ReadComPort(resp_buff, 300, 90000) )  { // wait 60 secs for URC
          if (report_com_err){
                debuglog(LOG_INFO,"Failed to get URC after reset! Will attempt radio power reset.");
                return false;
          }
          app_exit("Failed to get URC after reset!");
     }

     while (strstr(resp_buff, "SYSLOADING")) {
           resp_buff[0] = 0;
           if ( !ReadComPort(resp_buff, 300, 90000) )
              app_exit("Failed to get URC after reset!!");
     }

     logmsg(resp_buff);
     if (strstr(resp_buff,"^SYSSTART") == NULL)
        app_exit("Failed to confirm URC after reset!!!");
     PurgeCOM();
     if( !wait_for_connection( 35 ) )
         app_exit("Failed to connect to cellular Radio after reset!");
     isNormalMode = false;
     if (strstr(resp_buff, "AIRPLANE") == NULL )
       set_to_airplane_mode();
    return true;
}


/*

 input command formate from web client
 cmd=<operation>,<carrier_id>
 operation select, activate, reactivate
 carrier_id  0...8

 input command from command line (has two args)
 <operation> <carrier_id>
*/
int main ( int argc, char *argv[] ){
FILE *fp;
char cmd[30];
char  *qsp, *off;
int i, fd;
char in_cmd_str[100];
bool ret;

      mdn[0] = '\0';
      //logmsg("Starting cellconfig...");
      // typical strings: cmd=select,1,n&AjaxRequestId=125525255255520
      //   cmd=select,8&AjaxRequestId=125525255255520  - PXS8
      exit_msg_buff[0] = '\0';
      StartLogging();
      if (argc > 1){
          if (argc == 3){  // expect to have two arguments like select 0 or select 8 or select4g 0
              logmsg("cellconfig: called from command line with two args.");
              strcpy(cmd, argv[2]);
              ProvIndex  =  atoi(cmd);
              strcpy(cmd, argv[1]);
              cli = true;
          } else {
              logmsg("cellconfig: Invalid command from client, issued from command line!");
              return 0;
          }
      } else {

          // We don't want to see errors
          FILE *stream ;
          stream = freopen("/tmp/file.txt", "w", stderr);
          qsp = getenv("QUERY_STRING");
          if (qsp != NULL) {
              logmsg("Called from browser with QUERY_STRING") ;
              qsp = DecodeURL(qsp);
              qsp += 4;   // skip cmd= at start of query string
              strcpy(in_cmd_str,qsp);
              off = strchr(in_cmd_str,'&');
              *off = '\0';
              off = strchr(in_cmd_str,',');
              *off = '\0';
              strcpy(cmd, in_cmd_str);
              ProvIndex = atoi(&in_cmd_str[strlen(cmd)+1]);
              printf("Content-Type: text/plain\n\n");
          } else {
              logmsg("cellconfig: Failed parsing QUERY_STRING!");
              return 0;
          }
      }

      fp = fopen(CELL_TYPE_FILE, "rb");
      if(fp != NULL) {
          cell_type = fgetc(fp);
          fclose(fp);
      }

      sprintf(mbuff, "cellconfig:  cmd = %s, index = %d, cell_type = %d", cmd, ProvIndex, cell_type);
      logmsg(mbuff);
      openAtport();

      logmsg("cellconfig: Test connection with Cellular Radio...");
      if(!wait_for_connection( 10 ) ) {
            logmsg("cellconfig: Failed to connect to Radio! Perform radio power reset...");
            reset_radio_power();
            //app_exit("Failed to connect to Radio!");
      }
      logmsg("cellconfig: Connected to Cellular Radio");

     /* assume the Radio is initially in Airplane mode, config_start was previously called */
     isNormalMode = in_normal_mode( );
     if(isNormalMode){
        logmsg("Detected Normal mode but expected Airplane Mode!");
        set_to_airplane_mode();
     }

    if (strcmp(cmd,"select4g") == 0) {
         /* execute PLS8 commands */
         get_4g_sim_state();  // if no SIM then no use continuing,  terminate
         got_4g_sim = true;
         select4g(ProvIndex);
    } else if (strcmp(cmd,"select") == 0 || strcmp(cmd,"reactivate") == 0) {
        /* execute PXS8 commands */
        if(  ProvIndex < 7) {
             select_umts();
        }  else {
           if( ProvIndex == 7)
                app_exit("Fatal error: Sprint (CDMA) is not supported!");
            else if(strcmp(cmd,"select") == 0 )
                select_verizon(false);
            else
                select_verizon(true);
        }
    } else if (strcmp(cmd,"reset") == 0 )
          reset_cdma();

    set_to_airplane_mode();
    closeAtport();
    StopLogging();
    return 0;

}