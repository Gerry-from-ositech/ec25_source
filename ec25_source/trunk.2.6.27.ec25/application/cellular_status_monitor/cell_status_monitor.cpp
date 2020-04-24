/*************************************************************
 * Cellular Status Monitor
 *
 * This background demean is started when "cell_power on" is called.
 * It initially opens the Cellular APP port ttyS2 and uses this to
 * send AT commands to the radio to do the following:
 * 1. switch to normal mode and create the /tmp/cell_pwoer_on flag file
 * 2. determine the tech the radio is configured for GSM/UMTS or CDMA
 * 3  if GSM/UMTS determine if a SIM is installed
 * 4. read status using various AT commands and parse the data into a tab delimited string
 * 5. set LED according to registration status or if data session is in progress
 * 6. write the RSSI value to /tmp/cell_signal file 1 byte binary
 * 7. write the status string into /tmp/cell_status
 * 8. wait  specified time (3 seconds)
 * 9. repeat steps 4..8
 *
 * Normal Titan cellular LEDS
 *       Sig   Cell   Philips only has Cell LED showing through the faceplate
                             OPT_ENABLE_SIG_LED byte 2 bit 6 not set
*  application/medtronic/log.c
 *************************************************************/
#include <string.h>
#include <string>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <algorithm>
#include <debuglog.h>
#include <sys/time.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <vector>
#include <sstream>
#include "../../gui-config/util/serial_at.h"
#include "../../gui-config/util/utils.h"
#include "../../gui-config/util/inifile.h"


using namespace std;

bool connect_to_radio();
void write_status_file(char *txt);
bool wait_for_enumeration(int enumerate_to);

#define CINTERION_POWER_ON_FILE "/tmp/cell_power_on"
#define CELL_ENABLE_FILE "/tmp/cell_enabled"
#define CELL_STATUS_FILE "/tmp/cell_status"
#define CELL_STATUS_FILE_RAW "/tmp/cell_status_raw"
#define CELL_TYPE_FILE "/tmp/cell_type"
#define CELL_TOWER_FILE "/tmp/cell_tower"
#define CELL_STATUS_LOG "/tmp/log/cell_status_log"
#define CELL_STATUS_LOG_ENABLE "/mnt/flash/titan-data/cell_status_log_enable"
#define CELL_MONITOR_SHUTDOWN "/tmp/cell_monitor_shutdown"
#define CELL_MONITOR_STOP "/tmp/cell_monitor_stop"
#define CHAT_RUNNING "/tmp/chat_running"
#define WAIT_AFTER_ATC           50    // WaitMs time (in ms) after first byte of ATC response is received
#define WAIT_BEFORE_ATC        50    // WaitMs time (in ms) before an AT command is sent
#define RETRY_ATC                       5    // Number of retries for an AT command if there was no response
#define WAIT_BETWEEN_RESPONSE_CHARS   10    // WaitMs time (in ms) between two characters of an AT-command response
#define SCAN_TIME                     3000    // Timeout between status scans 3 seconds
#define WAIT_INC                      500
#define TERMINATE_CHECK_TIME          300
#define MILLSEC 1000

#define UMTS 0
#define CDMA 1
#define LTE  2

#define OPT_ENABLE_SIG_LED  0x00400000
#define OPT_EXTENDED_LED    0x00008000

#define CELL_PXS8 1
#define CELL_PLS8 2
#define CELL_PLS62_W 3
#define CELL_ELS31_V 4
#define CELL_EC25_AF 5
#define CELL_EC25_G 6


char status_description[] = "\n\n===================== Status Fields =================================\n\
Field  Value\n\
 1     - Signal strength in -dBm   0 = Undetectable\n\
 2     - Network type (0=GSM/UMTS, 1=LTE, 2=CDMA-EVDO, 3 = CDMA-ix/RTT)\n\
 3     - Reg status (0=not registered, 1=reg with home, 2=reg with roaming partner)\n\
 4     - SIM ID ( NO = could not read, maybe no SIM)\n\
 5     - Bearer (0 = unknown, 2= 2G, 3=3G, 4= 4G, 5=CDMA)\n\
 6     - Device ID: IMEI International Mobile Equipment Identity - like the modems serial number\n\
 7     - SIM Phone Number - could be unknown or if tech=CDMA will be blank\n\
 8     - Provider name\n\
 9     - SIM Ready\n\
 10    - Packet Switching info\n\
========================================================================\n";

void app_shutdown();
void app_exit(const char *msg);
void reset_radio_module();


int regState = 0;  // 0 = null value 1 = not registered, 2 = registered
int debug_display_count = 0;
bool disable_cell_status_update = false;
bool modem_busy = false;
bool cell_status_log = false;
bool isExtendedMode = false;
bool data_session_status_running = false;
bool sim_access = false;
int cell_type = 0;
char szResponse[2000];
int tech = UMTS;
bool terminate_flag = false;
string status_buff;
int good_threshold,  fair_threshold;
int sig_strength = 0;
unsigned int  enable_sig_led = 0;
bool isNormalMode = false;
bool verbose = false;
bool show_status = false;
bool page = false;
char status_copy[500];
char cdma_carrier[20];

char comm_port[30];

void hexdumpprint(char *msg, void *ptr, int buflen) {
unsigned char *buf = (unsigned char*)ptr;
int i, j;
int width = 20;
    printf("%s\n", msg);
    printf("------------ HexDump (%d bytes)-------------\n", buflen);
    for (i=0; i<buflen; i+=width) {
         printf("%06d: ", i);
         for (j=0; j<width; j++)
            if (i+j < buflen)
               printf("%02x ", buf[i+j]);
            else
               printf("   ");
         printf(" ");
         for (j=0; j<width; j++)
            if (i+j < buflen)
              printf("%c", isprint(buf[i+j]) ? buf[i+j] : '.');
         printf("\n");
    }
    printf("----------------------------------\n");

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



/*
0-4: "GPRS"
5,6: "WCDMA"
7-10: "UMTS"
11: "CDMA1xRTT"
12-13: "CDMA1xEVDO"
16,17: "LTE"
*/
void map_packet_switch_info(int index, char txt[]){
   if(index < 5)
     strcpy(txt, "GPRS\t");
   else if (index < 7)
      strcpy(txt, "WCDMA\t");
   else if( index <11)
      strcpy(txt, "UMTS\t");
   else if(index == 11)
      strcpy(txt, "CDMA1xRTT\t");
   else if (index < 14)
      strcpy(txt, "CDMA1xEVDO\t");
   else
      strcpy(txt, "LTE\t");

}


char *trim_response(char *resp){
char *p;
char *start;

   if ( strstr(resp, "ERROR"))
      return resp;
   start = strstr(resp, "\r\n");
   start += 2;
   p = strstr(start, "\r\nOK");
   p += 2;
   *p = '\0';
   return start;

}

void log_msg(char *msg){
FILE *fp;
int i;

//   if( access( CELL_STATUS_LOG, F_OK ) == -1 ) {
       // file doesn't exist
//       fp = fopen(CELL_STATUS_LOG,"wb");
//       fwrite(status_description, 1, strlen(status_description), fp);
//   }else
    fp = fopen(CELL_STATUS_LOG,"ab");
    i = strlen(msg)-1;
    if (msg[i] != '\x0a')
        fprintf(fp,"%s\n", msg);
    else
       fprintf(fp,"%s", msg);
    fflush(fp);
    fsync(fileno(fp));
    fclose(fp);
}


/////////////////////////////////////////////////////////////////////////////////////
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

void modem_not_responding(const char *cmd){
     debuglog(LOG_INFO, "cell_status_monitor: modem not responding");
     system("touch /tmp/cell_modem_not_responding");
     if ( verbose )  printf("Modem not responding, waiting for shutdown...");

      for (;;){
         if ( item_exists( CELL_MONITOR_SHUTDOWN)) app_shutdown();
         sleep(1);
      }

}
bool RunAT( const char * cmd, const  char* urc = NULL, DWORD toMillSec = 12000 ) {
bool ret;

   PurgeCOM();   // clear serial buffers, may have garbage or URC
//TODO: could prep for AT request -response by setting szResponse[0] = '\0', and maybe PurgeCOM()
   if ( verbose )   hexdumpprint((char *)"Send AT Command:", (char *)cmd, strlen(cmd));
     // printf("Send AT Command: %s\n", cmd);
//hexdumplog((char *)"Send AT Command:", (char *)cmd, strlen(cmd));
    szResponse[0] = 0;
   if (urc == NULL) {
         ret = ATCommand(cmd, szResponse, ARRAYSIZE(szResponse));
   } else
        ret =  ATCommandUrc(cmd, szResponse, ARRAYSIZE(szResponse),  toMillSec,  (char *)urc);
   if (verbose)   hexdumpprint((char *)"AT Response:", (char *)szResponse, strlen(szResponse));
 //     printf("AT Response: %s", szResponse);
//hexdumplog((char *)"AT Response:", (char *)szResponse, strlen(szResponse));
//TODO: if ret = false and /tmp/chat_running exists global bool modem_busy to true
  if ( !ret && item_exists( CHAT_RUNNING)) {
       if (verbose) printf("Modem not responding, could be dialing...\n");
       modem_busy = true;
   }
   if( !ret && szResponse[0] == 0)
      modem_not_responding(cmd);
   return ret;
}

bool in_normal_mode() {
  if ( RunAT( "AT+CFUN?\r")){
      if( strstr(  szResponse, ": 1") != NULL )
            return true;
  }
  return false;
}


void parse_status_str( string &sta) {
vector<string> fields;
int i;

   fields = split_str( sta, '\t');
   printf( "Num of fields = %d\n", fields.size());
   for (i=0; i < fields.size(); i++){
        printf( "%d - %s\n", i,fields[i].c_str());
   }
}



bool wait_for_sysstart() {
int count = 0;

    for(; strstr(szResponse, "^SYSSTART") != NULL ; WaitMs(500)){
          ReadResponse(" ", szResponse, 120, "^SYSSTART");
          count++;
          if (count > 30) return false;
    }
    WaitMs(1000);
    return true;
}

void app_shutdown() {

   debuglog(LOG_INFO, "cell_status_monitor: shutdown Set to Airplane mode");
   if(verbose) printf("cell_status_monitor: shutdown Set to Airplane mode...\n");

   if (cell_type == CELL_PLS62_W )  {//|| cell_type == CELL_PLS8) {
       debuglog(LOG_INFO, "cell_status_monitor: For PLS62, reset module into Airplane mode...");
       RunAT("AT+CFUN=1,1\r");
   } else   RunAT("AT+CFUN=4\r");

   remove(CINTERION_POWER_ON_FILE);
   remove(CELL_STATUS_FILE);
   remove(CELL_TOWER_FILE);
   remove(CELL_MONITOR_SHUTDOWN);
   system("sync");

   if (cell_type < 5){
       if(verbose) printf("Set LED to indicate Airplane mode\n");
       if ( isExtendedMode )
           system("titan3_led cell 2 > /dev/null");
       else
           system("titan3_led cell 0 > /dev/null");
       system("titan3_led cell-sig 0 0 > /dev/null");   //turn off cell-sig
   }
   if(verbose) printf("cleanup cell power control files..\n");
   CloseCOM();
   debuglog(LOG_INFO, "cell_status_monitor: shutdown.");
   exit(0);
}

/*

A fatal error has occurred and this program has to exit

Try to cleanup and leave the system in a known state

*/
void app_exit(const char *msg){
    if( verbose ) printf("Exit cell_status_monitor with error: %s\n", msg);
    if (msg != NULL)
       debuglog(LOG_INFO, "cell_status_monitor aborted: %s", msg);
    app_shutdown();

}

static void sig_terminate(int signo) {
    // do as little as possible inside this function
    terminate_flag = true;
    CloseCOM();
    exit(1);
}


/***********************************************************************
* Description: Parse response from AT+CREG? command.
*
* Calling Arguments:
* data    - the response from the command. Typically this argument points
*           to the start of one line from the response such as +CREG: 0,0
*           The function will parse out the last field "0" and return a pointer
*           to it.
*
* Returns:
* char *  - points to a NULL terminated string containing the value
*
*
*************************************************************************/
char *get_creg_field(char *data){
char *p;
      data = strchr(data,',');
      data++;
      p = strchr(data,'\n');
      *p = '\0';
      return data;
}

/***********************************************************************
* Description: Parse response from AT^SREG? command.
*
* Calling Arguments:
* data    - the response from the command. Typically this argument points
*           to the start of one line from the response such as ^SREG: 0,2,2
*           The function will parse out the second last field "2" and return a
*           pointer to it.
*
* Returns:
* char *  - points to a NULL terminated string containing the value
*************************************************************************/
char *get_sreg_field(char *data){
char *p, *p1;
      p = strchr(data, ',');
      p++;
      p1 = strchr(p,',');
      *p1 = '\0';
      return p;
}

/***********************************************************************
* Description: Parse response from AT command containing a single field or
*              a comma delimited set of field values e.g: 0,0,1\n
*
* Calling Arguments:
* data    - the response from the command. e.g: AT+CREG? or AT+SREG?
*           one line from the response such as ^SREG: 0,2,2<cr><lf>
*           The function will parse out the field based on the index argument
*           and return a pointer to the field.
*
* index   - the field to be returned.  fields are numbered 0...n
*
* Returns:
* char *  - points to a NULL terminated string containing the value
*
* Note: the incoming data buffer is changed, has nulls inserted
*************************************************************************/
char *get_field_value(char *data, int index){
char *p, *p1;

    p = strstr(data, ": "); // point to start of resp fields
    p += 2;                 // point to start of first field
    p1 = p;
    while(index) {
         p = strchr(p1,',');
         if (p == NULL) {
           //printf("Index of field in response out-of-range!\n");
           return NULL;  // should not happen in production
         }
         p += 1;
         p1 = p;
         index--;
    }
    if ( *p == '\"'){  // do not want leading or trailing quotes
       p++;
       p1 = p;
    }
    while ( *p1 != ',' && *p1 != '\r' && *p1 != '\n' && *p1 != '\"') p1++;
    *p1 = '\0';
    if (strlen(p) == 0){
       return "";
    }
    return p;

}

bool ec25_connection_state(){
bool ret = false;

     if (RunAT("AT+QENG=\"servingcell\"\r") ) {
         // look for  "CONN\",""
         if(strstr(szResponse,"CONN"))
             ret = true;

     }
     return ret;
}

/* For 4G modems check somon connexction state to confirm we have a connection before  indicating registartion with the network
^SMONI: 2G,71,-61,262,02,0143,83BA,33,33,3,6,G,NOCONN
^SMONI: 3G,10564,296,-7.5,-79,262,02,0143,00228FF,-92,-78,NOCONN
^SMONI: 4G,6300,20,10,10,FDD,262,02,BF75,0345103,350,33,-94,-7,NOCONN
*/
bool connection_state(){
int    ch = ',';
int    cr = '\r';
char *ptr = NULL;
bool ret = false;

     if (RunAT("AT^SMONI\r") ) {
         // find last field in response, looking for "NOCONN or "CONN"
         ptr = strrchr( szResponse, cr );
         *ptr = 0;
         ptr = strrchr( szResponse, ch );
         ptr++;
     }
     if (ptr != NULL){
         if ( strcmp(ptr, "NOCONN") == 0 || strcmp(ptr, "CONN"))
               ret = true;
     }
     return ret;
}


/*
Get the Registration status from the device. And set LED accordingly

Radio technology field:
    "0" - GSM/UMTS
    "1" - LTE
    "2" - CDMA- EVDO
    "3" - CDMA-  IX/RTT

Registration Status string formats:
     "0" - indicates not registered (possibly  searching for network)
     "1" - registered with home network
     "2" - registered while roaming
     "3" - data session in progress

 SIM id for GSM/UMTS or LTE
    "no" - no SIM detected  also if tech is CDMA
    "89302370201001963552"


return true - radio registered, can read type of network connection (2G,3G,4G)


^SREG: <urcMode>, <regStatus1X>, <regStatusEVDO>
          0,         0              0
                     1              1
                     2              2
                     3              5
                     4
                     5
                  1x/RTT            EVDO

parse out 1x/RTT value

if 1x/RTT != 0:
  then tech = 1x/RTT
  if 1x/RYY = 1 - reg on home network
  elif 1x/RTT = 5 - reg on roaming network
  else: 2,3,4 - not reg, searching
else  parse out EVDO value
  then tech = EVDO
  if EVDO = 1 - reg on home network.
  elif EVDO = 5 - reg on roaming network
  else 0, 2 - not reg, searching


*/
bool get_network_reg_status(char *status_info) {
FILE *fp;
char *field = NULL;
char buff[100];
char tag_buff[20];
char cmd[20];
char *p;
bool at_ret;
static int prev_led_flash_state = 0;   // 0=not flashing, 1=flashing -searching, 2=flashing-registered
static bool reg_state = false;  // default to indicate searching status for LED
static bool data_session_running = false;
static char prev_status[10] = {0};

   memset(buff, 0, sizeof(buff));

   if (cell_type == CELL_PXS8){
       if(verbose) printf("Check for 3G data session - ppp0...\n");
       // check if data session is in progress
       fp = popen("ifconfig ppp0 2>&1","r");
       fgets( buff, sizeof(buff), fp) ;
       if(verbose) printf(buff);
       if ( strstr(buff, "ppp0: error") != NULL) {
              data_session_running = false;
              if(verbose) printf("ppp0 not detected, data session not running\n");
       } else {
             fgets( buff, sizeof(buff), fp) ;
             if(verbose) printf(buff);
             if ( strstr(buff, "inet addr:") != NULL) {
                  data_session_running = true;
                  if(verbose) printf("ppp0 IP assigned, data session running\n");
             }
       }
       pclose(fp);
 } else if(cell_type < CELL_EC25_AF){
 //TODO Quectel check for data_session_running
       if(verbose) printf("Check for 4G data session - USB1...\n");
       fp = popen("ifconfig usb1 2>&1","r");
       fgets( buff, sizeof(buff), fp) ;
       if(verbose) printf(buff);
       if ( strstr(buff, "usb1: error") != NULL) {
              data_session_running = false;
              if(verbose) printf("usb1 not detected, data session not running\n");
       } else {
             fgets( buff, sizeof(buff), fp) ;
             if(verbose) printf(buff);
             if ( strstr(buff, "inet addr:") != NULL) {
                  data_session_running = true;
                  if(verbose) printf("usb1 IP assigned, data session running\n");
             }
       }
       pclose(fp);
 }

   // get Network Registration Status
   if (tech == CDMA) {
      if(verbose) printf("Get network reg status for CDMA ...\n");
      at_ret = RunAT("AT^SREG?\r");
      if(!at_ret){
          if(modem_busy) {
               if(verbose) printf("Modem busy, skip status scan.\n");
               return false;
          }
      }
      if(modem_busy){
          modem_busy = false;
          if(verbose) printf("Modem finished dialing and is now responding.\n");
      }
      p = strstr(szResponse,"SREG: ");
      if (p != NULL){
           field = get_field_value(p,1);
           if (field != NULL){
               if( *field != '0') {
                   strcpy(status_info,"3\t");     // CDMA - ix/RTT network
               }else {
                   strcpy(status_info,"2\t");     // CDMA - EVDO network
                   field += 2 ;  // move to next field in response, regStatusEVDO
               }
               if(!data_session_running){
                   // if not running data session see if we are at least registered
                   if (*field == '1' || *field == '5'){
                       reg_state = true;
                       if (*field == '1')
                          strcat(status_info,"1\t" );  // registered with home network
                       else
                           strcat(status_info, "2\t");  // registered while roaming
                   } else {
                       strcat(status_info, "0\t" );    // not registered
                       reg_state = false;
                   }
               }
          } else
            p = NULL;
      }
      if( p == NULL) {  // failed to get AT response of find field in response
             if (prev_status[0] == '\0')
                 strcpy(status_info, "2\t0\t");
             else
                 strcpy(status_info, prev_status);
             reg_state = false;
      }
   } else {
       if(verbose) printf("Get network reg status for UMTS/LTE ...\n");
       if( cell_type == CELL_ELS31_V ){
            strcpy(cmd, "AT+CEREG?\r");
            strcpy(tag_buff, "CEREG: ");
       } else if( cell_type == CELL_PLS8 ){  // PXS8's GSM/UMTS network registration CREG  is unreliable (at least for Verizon)
            strcpy(cmd, "AT+CGREG?\r");
            strcpy(tag_buff, "CGREG: ");
       }else {
            strcpy(cmd, "AT+CREG?\r");
            strcpy(tag_buff, "CREG: ");
       }
       if (RunAT(cmd) ) {
          if (cell_status_log){
                 p = trim_response(szResponse);
                 log_msg(p);
          }

          if (tech == LTE)
             strcpy(status_info, "1\t");  // LTE tech
          else
             strcpy(status_info,"0\t");    // GSM/UMTS tech

           // sometimes we get OK but no status info, check for valid status in the response
           if ( strstr(szResponse, tag_buff) == NULL){
                 strcat(status_info, "0\t" );  // not registered
                 reg_state = false;
//printf("CREG command did not return data!\n");
           } else {
                  p = strstr(szResponse, tag_buff) ;
                  if( p != NULL)
                        field = get_field_value(p,1);
                  if (field != NULL && (*field == '1' || *field == '5')){
                       reg_state = true;
                       if (*field == '1')
                           buff[0] = '1';
                       else
                          buff[0] = '5';
                       if (cell_type > 4){
                          if(!ec25_connection_state()){
                              strcat(status_info, "0\t" );    // not registered
                              reg_state = false;
                              data_session_running = false;
                          }
                       } else if (cell_type != CELL_PXS8 && !connection_state()){
                            strcat(status_info, "0\t" );    // not registered
                            reg_state = false;
                            data_session_running = false;
                       }
                       if (reg_state){
                          if (buff[0] == '1')
                               strcat(status_info,"1\t" );  // registered with home network
                          else
                               strcat(status_info, "2\t");  // registered while roaming
                       }
                  } else if (field != NULL) {
                      strcat(status_info, "0\t" );    // not registered
                      reg_state = false;
                      data_session_running = false;
                  } else {  // failed to find field in response
                       strcpy(status_info, prev_status);
                       reg_state = false;
                  }
          }

      } else {
          // AT+CGREG? could return +CME ERROR: SIM not inserted
          if (cell_status_log){
                log_msg(szResponse);
          }

          if (prev_status[0] == '\0'){
              if(tech == LTE)  // PLS8 requires SIM in order to read CREG and CSQ
                 strcpy(status_info, "1\t0\t");
              else
                   strcpy(status_info, "0\t0\t");
         } else
             strcpy(status_info, prev_status);    // AT command failed not registered
      }
   }


   strcpy(prev_status, status_info);

   // get SIM id if GSM/UMTS or LTE
     if ( tech != CDMA ){
            if(verbose) printf("Get SIM id if GSM/UMTS or LTE radio...\n");
        //    if ( reg_state && RunAT( "AT^SCID\r") ) { reg_state check removed to address issue with testbed
            if (cell_type == CELL_PLS62_W || cell_type == CELL_ELS31_V)
               at_ret = RunAT( "AT+CCID\r");
            else if(cell_type > 4)
              at_ret = RunAT( "AT+QCCID\r");
            else
               at_ret = RunAT( "AT^SCID\r");

            if ( at_ret ) {
                p = strstr( szResponse,"CID: " );
                if (p == NULL){
                    if(verbose) printf("Failed to read SIM id set status field to 'NO'\n");
                    strcat( status_info,"NO\t" );
                    sim_access = false;
                } else {
                   sim_access = true;
                   field = get_field_value( p, 0 );
                   sprintf( buff,"%s\t", field );
                   strcat( status_info, buff );
                   if(verbose) printf("SIM id is %s\n", buff);
                }
          } else {
             strcat(status_info,"NO\t");
             sim_access = false;
          }
     } else {
         strcat(status_info,"NO\t");
     }

     if (cell_type < 5){
         // set LED based on registration status
         if (data_session_running ) {
             if(verbose) printf("Data session running : titan3_led cell 1\n");
             system("titan3_led cell 1 > /dev/null"); // set led to data session in progress
             prev_led_flash_state = 0; // led is on, not flashing
        }  else  {
             if(verbose) printf("Data session not running : just registered or still searching...\n");
             if (reg_state &&  prev_led_flash_state  !=  2 ) {
                  system("titan3_led cell 4 > /dev/null"); // set lead to registered status
                  if (verbose) printf("CELL Led set to registered state");
                  prev_led_flash_state  =  2 ;
            } else  if( reg_state == false  &&  prev_led_flash_state  !=  1 ) {// searching
                 system("titan3_led cell 3 > /dev/null"); // set led to searching status
                 if (verbose) printf("CELL Led set to searching state");
                 prev_led_flash_state  =  1;
           }
       }
   }
   if(data_session_running || reg_state)
        return true;
   return false;
}

int  count_markers(char *data, char mark) {
int count = 0;
   while (*data != '\0') {
       if( *data++ == mark) count++;
   }
   return count;
}


int twoG[] = {0,3,4,5,6};  // ACT,ARFCN,BCCH,MCC,MNC,LAC,cell,C1,C2,NCC,BCC,GPRS,ARFCN,TS,timAdv,dBm,Q,ChMod
//                             *              *   *   *    *
int threeG[] = {0,5,6,7,8};  // ACT,UARFCN,PSC,EC/n0,RSCP,MCC,MNC,LAC,cell,SQual,SRxLev,CSGid,TransportCh, SF,Slot,EC/n0,RSCP,ComMod,HSUPA,HSDPA
//                               *                         *   *   *   *
int fourG[] = {0,6,7,9,10};  // ACT,EARFCN,Band,DL bandwidth,UL bandwidth,Mode,MCC,MNC,TAC,Global Cell ID,Physical Cell ID,TX_power,RSRP,RSRQ,Conn_state
//                               *                                              *   *           *              *

void extract_cell_tower_info(char *in_data) {
FILE *fp;
char buff[300];
vector<string> fields;
int *index;
string data = in_data;

   if(verbose) printf("Extracting cell_tower info from SMONI response...\n");
   if ( strlen(in_data) < 25)
       if ( strstr(in_data, "Searching") || strstr(in_data,"SEARCH") ){
           if(verbose) printf("SMONI reports: Searching for a Pilot Channel, no cell_tower file created!\n");
       return;
   }

   fields = split_str( data, ',');
   if ( fields[0] == "2G"){
          index = &twoG[0];
   } else if (fields[0] == "3G"){
         index = &threeG[0];
   } else {
         index = &fourG[0];
   }
   buff[0] = 0;
   for (int i=0;i < 5; i++){
      strcat(buff,fields[index[i]].c_str());
      if (i < 4)
         strcat(buff,",");
   }

   fp = fopen(CELL_TOWER_FILE, "wb");
   fwrite(buff, 1, strlen(buff), fp);

   fflush(fp);  // flush data to OS buffer
   fsync(fileno(fp)); // flush OS buffer to disk
   fclose(fp);

}

void set_signal_leds(char bearer){
char led_cmd[100];

     /*  calls to control the cell-sig  LED as per docs - will only be visible if dual LEDs
          system("titan3_led cell-sig 0 0 > /dev/null");   turn off cell-sig
                             cell-sig 1 2
                             cell-sig 1 3
                             cell-sig 1 4
                             cell-sig 2 2
                             cell-sig 2 3
                             cell-sig 2 4
     xtract_cell_tower_info                        cell-sig 3 2
                             cell-sig 3 3
                             cell-sig 3 4
                                      | |
                                      | +---- tech 2G, 3G, 4G
                                      +------ Sig strength 1 bar, 2 bar, 3 bar
     */
     if ( bearer == '0') { // if bearer is Unknown
            if(verbose) printf("Network type is unknown!\n");
            system("titan3_led cell-sig 0 0 > /dev/null");  // turn off cell-sig
     } else  if( sig_strength == 113 ) {
            if (verbose) printf( "sig_strength >= 113 , turn LED off: titan3_led cell-sig 0 0\n");
            system("titan3_led cell-sig 0 0 > /dev/null");   //turn off cell-sig
     }  else  if (enable_sig_led) {
           // translate sig strength into number of bars to show on LED
           if( sig_strength < good_threshold) {
                 strcpy(led_cmd, "titan3_led cell-sig 3 ");
                 if (verbose) printf( "%d indicates GOOD status (3bars)\n", sig_strength);
           } else if ( sig_strength >= good_threshold && sig_strength  < fair_threshold  ){
                 strcpy(led_cmd, "titan3_led cell-sig 2 ");
                 if (verbose) printf( "%d <=  fair_threshold(%d) and %d >= good_threshold(%d) LED indcates FAIR status (2Bars)\n", sig_strength, fair_threshold, sig_strength, good_threshold);
           } else {
                 strcpy(led_cmd, "titan3_led cell-sig  1 " );
                 if (verbose) printf( "%d >  fair_threshold(%d) LED indcates POOR status (1Bar)\n",sig_strength, fair_threshold);
           }
           switch ( bearer ){
                case  '2':
                     strcat(led_cmd, "2 > /dev/null");
                     break;
                case  '3':
                case  '5':
                      strcat(led_cmd, "3 > /dev/null");
                      break;
                case  '4':
                     strcat(led_cmd, "4 > /dev/null");
                     break;
           }
           if(verbose) {
               sprintf( szResponse, "Sig LED control cmd: %s\n", led_cmd);
               printf(szResponse);
           }
           system(led_cmd);    // set LED to show sig strength and tech
    } else if (verbose)
         printf( "Sig LED is disabled\n");
}


/*

Get RAN  Radio Access Network  type- type of radio access network we are registered on
Unknown,CDMA,UMTS,LTE
see value in  radio_network_type

 AT^SMONI is used  differently depending on type of Radio

GSM/UMTS or LTE - could be 2G (GSM) or 3G (UMTS)
^SMONI: 2G,71,-61,262,02,0143,83BA,33,33,3,6,G,NOCONN
^SMONI: 3G,10564,296,-7.5,-79,262,02,0143,00228FF,-92,-78,NOCONN

CDMA - could be  2G (1xRTT) 3G (EvDO)

1xRTT
^SMONI: 507,0,108,310,0,6,8,4850,0,5,0,0,6,-70,11   - 14 ,

EvDo
925,1,108,-76                                       -  3 ,

Both 1xRTT and EvDO
^SMONI: 507,0,108,310,0,6,8,4850,0,5,0,0,6,-70,11
925,1,108,-76                                       - 17 ,


status_info -hetwork type
0 - Unknown
 2  - 2G   GSM
 3  - 3G   UMTS
 4  - 4G  - LTE
 5  - CDMA both 1xRTT and EvDO

SMONI fields differ accoding to the serving Cell type
ACT                        (2/3/4G)
MCC                        (2/3/4G)
MNC                        (2/3/4G)
LAC                        (2/3G)
Cell ID                    (2/3G)
Global Cell ID             (4G)
Physical Cell ID           (4G)

2G and 3G tower map:
When registered and  "camping on the cell" that will provide service each type has the folloing fields

PXS8 - 2G ^SMONI: ACT,ARFCN,BCCH,MCC,MNC,LAC,cell,C1,C2,NCC,BCC,GPRS,ARFCN,TS,timAdv,dBm,Q,ChMod
PLS8 - 2G ^SMONI: ACT,ARFCN,BCCH,MCC,MNC,LAC,cell,C1,C2,NCC,BCC,GPRS,ARFCN,TS,timAdv,dBm,Q,ChMod
(0,3,4,5,6)

PXS8 - 3G ^SMONI: ACT,UARFCN,PSC,EC/n0,RSCP,MCC,MNC,LAC,cell,SQual,SRxLev,PhysCh, SF,Slot,EC/n0,RSCP,ComMod,HSUPA,HSDPA
PLS8 - 3G ^SMONI: ACT,UARFCN,PSC,EC/n0,RSCP,MCC,MNC,LAC,cell,SQual,SRxLev,CSGid,TransportCh,SF,Slot,EC/n0,RSCP,ComMod,HSUPA,HSDPA
(0,5,6,7,8)

PLS8 - 4G ^SMONI: ACT,EARFCN,Band,DL bandwidth,UL bandwidth,Mode,MCC,MNC,TAC,Global Cell ID,Physical Cell ID,Srxlev,RSRP,RSRQ,Conn_state
(0,6,7,9,10)
4G,2025,4,15,15,FDD,302,720,76CC,0901002,17,-32768,-96,-12,CONN  = "4G,302,720,0901002,17"


*/

void  get_bearer_and_cell_tower_info(char *status_info, bool isRegistered) {
int count;

    if(verbose) printf("Get network type ...\n");
    strcpy(status_info , "0\t");  // Init to Unknown
    if (RunAT("AT^SMONI\r") ) {
         if( tech == CDMA) {
             strcpy(status_info, "5\t");
         } else {
             if (strstr(szResponse, "2G,") != NULL) {
                strcpy(status_info, "2\t");
             } else if (strstr(szResponse, "3G,") != NULL){
                strcpy(status_info, "3\t");
             } else if (strstr(szResponse, "4G,") != NULL){
                 strcpy(status_info, "4\t");
             }
         }
         if(isRegistered)
             extract_cell_tower_info(&szResponse[10]);
         else  if ( item_exists( CELL_TOWER_FILE))
             remove(CELL_TOWER_FILE);
    }
    set_signal_leds(status_info[0]);
}



// +QENG: "servingcell","NOCONN","LTE","FDD",302,610,844D92A,248,900,2,5,5,D6E7,-89,-8,-61,17,38
//                                 *     *    *   *    *
void  Qget_bearer_and_cell_tower_info(char *status_info) {
FILE *fp;
char buff[300];
char *p, *p1;
string bar, mcc, mnc, lac, cid;

   strcpy(status_info , "0\t");  // unknown
   if (RunAT("AT+QENG=\"servingcell\"\r")){
       if(strstr(szResponse,"CONN")){
           // we are registered so get beared and cell tower info
           if (strstr(szResponse,"GSM"))
              bar = "2\t";
           else if(strstr(szResponse,"LTE"))
              bar = "4\t";
           else
              bar = "3\t";
       }
       strcpy(status_info, bar.c_str());
       bar[1] = 'G';
       cid = get_field_value(szResponse, 6);
       lac = get_field_value(szResponse, 5);
       mnc = get_field_value(szResponse, 4);
       mcc = get_field_value(szResponse, 3);
       sprintf(buff,"%s,%s,%s,%s,%s", bar.c_str(),mcc.c_str(), mnc.c_str(), lac.c_str(),cid.c_str());
       fp = fopen(CELL_TOWER_FILE, "wb");
       if(verbose) printf("Creating cell_tower file with %s\n", buff);
       fwrite(buff, 1, strlen(buff), fp);
       fflush(fp);  // flush data to OS buffer
       fsync(fileno(fp)); // flush OS buffer to disk
       fclose(fp);

   }

//   set_signal_leds(status_info[0]);


}

/*
<Act> Access technology selected
0 GSM        2G
2 UTRAN      3G
3 GSM W/EGPRS        2G
4 UTRAN W/HSDPA      3G
5 UTRAN W/HSUPA      3G
6 UTRAN W/HSDPA and HSUPA  3G
7 E-UTRAN    4G
*/

//char * bar_types[] = {(char *)"2\t", (char *)"2\t",  (char *)"3\t", (char *)"2\t", (char *)"3\t", (char *)"3\t", (char *)"4\t"};
//void  xQget_bearer_and_cell_tower_info(char *status_info) {
//FILE *fp;
//char *p, *p1;
//
//int i;
//char buff[300];
//vector<string> fields;
//string temp, data, bar, cid, lac;
///*
//AT+CGREG?
//+CGREG: 0,1   <- see that we are registerd with home and tower info is disabled
//AT+CGREG=2  enables us to see tower info
//AT+CGREG?
//+CGREG: 2,1,"D6E7","844D92A",7
//*/
///* Quectel does not show:
//MCC Mobile Country Code (first part of the PLMN code)
//MNC Mobile Network Code (second part of the PLMN code)
//*/
//
//    if (RunAT("AT+CGREG=2\r")){
//       if( RunAT("AT+CGREG?\r")){
//
//  // should it be CREG not CGREG CREG is used in scan ?????????
//  // also AT^SMONI is called ??????????????? should not
//          p = strstr(szResponse,"REG: ");
//          p += 5;
//          p1 = strchr(p,'\r');
//          *p1 = '\0';
//          data = p;
//          // remove embedded quotes
//          data.erase(std::remove(data.begin(), data.end(), '\"'), data.end());
//          if(verbose) printf("Split data '%s'\n", data.c_str());
//          fields = split_str( data, ',');
//          if(verbose) printf("Split response data with %d fields!\n", fields.size());
//
//          if ( fields.size() == 5){
//               if(verbose) printf("Found %d fields\n", fields.size());
//               if(verbose) printf("Field 2 = %s\n", fields[2].c_str());
//               if(verbose) printf("Field 3 = %s\n", fields[3].c_str());
//               if(verbose) printf("Field 4 = %s\n", fields[4].c_str());
//               i = atoi(fields[4].c_str());
//               printf("Int value = %d\n", i);
//               if (i == 0 || i == 3)
//                  bar = "2\t";
//               else if (i == 1)
//                  bar = "0\t";
//               else if (i == 7)
//                  bar = "4\t";
//               else
//                  bar = "3\t";
//
//               if(verbose) printf("Field maps to '%s'\n", bar.c_str());
//               strcpy(status_info, bar.c_str());
//               if(verbose) printf("%s\n", status_info);
//               bar[1] = 'G';
//               lac = fields[2];
//               cid = fields[3];
//               if(verbose) printf("%s\n",bar.c_str());
//               if(verbose) printf("%s\n",lac.c_str());
//               if(verbose) printf("%s\n",cid.c_str());
//               sprintf(buff,"%s,,,%s,%s", bar.c_str(),lac.c_str(),cid.c_str());
//               fp = fopen(CELL_TOWER_FILE, "wb");
//               if(verbose) printf("Creating cell_tower file with %s\n", buff);
//               fwrite(buff, 1, strlen(buff), fp);
//               fflush(fp);  // flush data to OS buffer
//               fsync(fileno(fp)); // flush OS buffer to disk
//               fclose(fp);
//          } else {
//             printf("No fields found in response!\n");
//             strcpy(status_info, "0\t");
//          }
//if(verbose) printf("Exit function Qget_bearer_and_cell_tower_info() 1\n");
//          return;
//       }
//    }
//    if(!modem_busy){
//        if(verbose) printf("Network type is unknown!\n");
//        strcpy(status_info , "0\t");  // unknown
//   }
//if(verbose) printf("Exit function Qget_bearer_and_cell_tower_info() 2\n");
//}

void get_sim_telenum() {
char *p;
char mdn[150];
int i;
int respLen;

    mdn[0] = '\0';
                  if ( i > 7)  {
                      mdn[i++] = '\0';
                  } else
                      mdn[0] = '\0';
     if (RunAT( "AT+CNUM\r")) {
         // check if we could have a full response such as <CR><LF>+CNUM: ,"+15194007245",145<CR><LF><CR><LF><CR><LF>OK<CR><LF>
         if( strchr(szResponse,',') != NULL) {
            respLen = strlen(szResponse);
            p = &szResponse[0];
            // we may have a phone number, or not
            //AT Response:<CR><LF>+CNUM: ,"+15194007245",145<CR><LF><CR><LF><CR><LF>OK<CR><LF>

            /* search for the first comma, start of  telephone number field
             * Then take all data (if any) bewteeen the quotes
             * Could have + or - in addition to digits in this field
             */
            i = 0;
            while (*p++ != '\x2C' ) i++;  // skip until a , is detected
            i++;  // scip over the first double quote "
            if ( i < respLen) {
                  p++;
                  i = 0;
                  /* positioned at the first char in the phone field
                   * sometimes there are non digits in the number e.g: fido uses
                   * "+15197312299"
                   */
                  while(*p > '\x39' || *p < '\x30') p++;
                  while(*p != '\x22') // copy all digits up to closing quote
                      mdn[i++] = *p++;
           }
         }
    }
    if (mdn[0] == '\0')
        strcpy(szResponse, "Unknown");
    else
        strcpy(szResponse, mdn);
    if(verbose){
        sprintf(mdn, "SIM Phone Number: %s\n", szResponse);
        printf(mdn);
    }
}

/*
Get status info from radio:
Get device id using AT+GSN returns:
               (IMEI) for UMTS and LTE
               (MEID)  for CDMA
AT+GSN
990002189432683

OK

*/
void get_radio_params( string &data, bool registered) {
char buff[50];
int i, index;
char *p, *p1;

    if(verbose){
       printf("Get Radio parameters...\n");
       if(registered)printf("Registered with the network: \n");
       else printf("Not registered with newtwork: \n");
    }

    if (RunAT("AT+GSN\r") ) {
            p = strstr(szResponse, "\r\n");
            p += 2;
            p1 = strchr(p,'\r');
            *p1 = '\0';
            if( tech == CDMA && strncmp(p, "0x00",4) == 0){
                p += 4;
            }
            data.append(p);
            data.append("\t");

            if (tech != CDMA) {
                get_sim_telenum();
                data.append(szResponse);
            }
            data.append("\t");

            if (RunAT("AT^SIND?\r") ) {
                 p = strstr(szResponse, "D: psinfo,");
                 p =  get_field_value(p, 2);
                 index = atoi(p);

                 // parse out Provider name from eons field
                 if (tech != CDMA){
                    if (cell_type == CELL_ELS31_V) {
                         strcpy(buff, "Verizon");  // hard-coded for ELS31_V
                         data.append(buff);
                    } else {
                        p = strstr(szResponse, "D: eons,");
                        // ^SIND: eons,0,3,"Ositech LTE Network","USIM",1
                        // ^SIND: eons,0,3,"",,"",0
                        if(p != NULL){
                             p = get_field_value(p, 3);
                             if (p != NULL){
                              data.append(p);
                             //   i = strlen(p);
                             //   if ( i != 1) {
                             //        memset(buff, 0, 50);
                             //        strncpy(buff,p, i-1);
                             //        data.append(buff);
                             //   }
                            }
                        }
                    }
                 } else if (cdma_carrier[0] != 0){
                     data.append(cdma_carrier);
                 }

                 data.append("\t");
                 // add SIM ready state
                 if(sim_access)
                    data.append("1\t");
                 else
                    data.append("0\t");

                 map_packet_switch_info(index, &buff[0]);
                 data.append(buff);
            } else {
                 data.append("\t\t\t\t");
            }
      }  else
         data.append("\t\t\t\t\t");
}

/*
0 GSM        2G
2 UTRAN      3G
3 GSM W/EGPRS        2G
4 UTRAN W/HSDPA      3G
5 UTRAN W/HSUPA      3G
6 UTRAN W/HSDPA and HSUPA  3G
7 E-UTRAN    4G
*/


void Qmap_packet_switch_info(int index, char txt[]){

  if (index == 7)
        strcpy(txt, "LTE\t");
  else if (index == 3)
        strcpy(txt, "GPRS\t");
  else if (index == 0)
       strcpy(txt, "GSM\t");
  else
       strcpy(txt, "UMTS\t");

}

// char *switch_tech[] = {(char *)"GSM\t", (char *)"UNKNOWN\t", (char *)"UTRAN\t", (char *)"GSM W/EGPRS\t", (char *)"UTRAN W/HSDPA\t", (char *)"UTRAN W/HSUPA\t", (char *)"UTRAN W/HSDPA\t",  (char *)"E-UTRAN\t"};

/*
 6     - Device ID: IMID or EMID depending on type of network\n\
 7     - SIM Phone Number - could be unknown or if tech=CDMA will be blank\n\
 8     - Provider name\n\
 9     - SIM Ready\n\
 10    - Packet Switching info\n\

*/
void Qget_radio_params( string &data, bool registered) {
char tech_buff[50];

int i, index;
char *p, *p1;

  if(verbose){
       printf("Get Radio parameters...\n");
       if(registered)printf("Registered with the network: \n");
       else printf("Not registered with newtwork: \n");
  }


  if (RunAT("AT+GSN\r") ) {
     p = strstr(szResponse, "\r\n");
     p += 2;
     p1 = strchr(p,'\r');
     *p1 = '\0';
     data.append(p);
     data.append("\t");
     get_sim_telenum();
     data.append(szResponse);
     data.append("\t");
     if (RunAT("AT+COPS?\r") ) {  //can tSake up to 180s, determined by network.
     //  +COPS: 0,0,"Bell",7
          p = get_field_value(szResponse, 3);
          if(p){
                index = atoi(p);
                Qmap_packet_switch_info(index, &tech_buff[0]);
          } else index = -1;

          p = get_field_value(szResponse, 2);
          if (p){
             data.append(p);
             data.append("\t");
             // add SIM ready state
             if(sim_access)
                data.append("1\t");
             else
                data.append("0\t");
             if(index != -1){
                data.append(tech_buff);  // last field in cell_status
             }
      } else
          data.append("\t\t\t");
    }
  }   else
     data.append("\t\t\t\t\t");
}


// +CSQ: 16,99
int get_PXS8_PLS62_modem_signal_strength(){  // PXS8
string str = strstr(szResponse,": ");
int i;

   if(str.length() == 0) return 0;
   i = str.find(",");
   str = str.substr(2, i-2);
   if(verbose) printf("In get_PXS8_PLS62_modem_signal_strength() with raw value %s....\n", str.c_str());
   i = atoi(str.c_str());
   if (i == 99) return 0;
   i = (i-1) * 2  - 111;
   return i;

}

int get_EC25_modem_signal_strength(){
string str = strstr(szResponse,": ");
int i;
   if(verbose)printf("In get_EC25_modem_signal_strength()...\n");
   if(str.length() == 0) return 0;
   i = str.find(",");
   str = str.substr(2, i-2);
   if(verbose) printf("In get_EC25_modem_signal_strength() with raw value %s....\n", str.c_str());
   i = atoi(str.c_str());
   if (i < 100) {
       if (i == 99) return 0;
       i = (i-1) * 2  - 111;
       return i;
   } else {
       // range indicates TD-SCDMA mode
       if (i == 199) return 0;
       i = i - 216;
       return i;
   }

}

// +CESQ: 99,99,255,255,20,32\r\n...
int get_PLS8_modem_signal_strength(){  //PLS8
std::string segment;
std::vector<std::string> seglist;
string str = strstr(szResponse,": ");
int i;
if(verbose) printf("In get_PLS8_modem_signal_strength()....\n");
   i = str.find("\r");
   str = str.substr(2,i-1);
   seglist = split_str( str, ',');

   // check for 2G, 3G or 4G signal
   if (seglist[0] != "99" && seglist[1] != "99" ){   //2G
          i = atoi(seglist[0].c_str());
          if (i == 0)
             i = -109;
          else
             i = i - 111;
          if(verbose) printf("cell_status_monitor: 2G signal detected = %d", i);
   } else  if (seglist[2] != "255" && seglist[3] != "255" ){   //3G
          i = atoi(seglist[2].c_str());
          if (i == 0)
             i = -119;
          else
             i = i - 121;
         if(verbose) printf("cell_status_monitor: 3G signal detected = %d", i);
   } else  if (seglist[4] != "255" && seglist[5] != "255" ){    //4G
           i = atoi(seglist[5].c_str());
           i = i - 141;
          if(verbose) printf("cell_status_monitor: 4G signal detected = %d", i);
   } else
      return 0;
   return i;
}

int read_signal_strength_value(){
int i = 0;

    if(verbose) printf("Getting signal strength...\n");
    memset(szResponse,0,sizeof(szResponse));
    if (tech == CDMA) {
        if (RunAT("AT+CSQ?\r") )
           i = get_PXS8_PLS62_modem_signal_strength();
    } else if (tech == LTE) {
        if (cell_type == CELL_PLS62_W) {
          if (RunAT("AT+CSQ\r") )
             i = get_PXS8_PLS62_modem_signal_strength();
        } else if(cell_type > 4){
          if (RunAT("AT+CSQ\r") )
              i = get_EC25_modem_signal_strength();
        } else {
          if (RunAT("AT+CESQ\r") )
             i = get_PLS8_modem_signal_strength();
        }
    } else {   // PXS8 UMTS or Quectel modems
         if (RunAT("AT+CSQ\r") )
            i = get_PXS8_PLS62_modem_signal_strength();
    }
    if(verbose)printf("Got signal strength of %d\n",i);
    return i;
}
/*
Signal Strength string format:
  The string version of the raw value returned from the AT+CSQ command. A integer value
  from 0 .. 99

For PXS8 UMTS AT+CSQ needs to have SIM installed and be in Normal mode or the command will fail
*/
void  get_signal_strength(char *status_info, bool isRegistered) {
char buff[100];

int i = 0;
FILE *fp;
char *p = NULL;

//  if(isRegistered)
    i = read_signal_strength_value();
  sig_strength = abs(i);
  sprintf(buff,"%d", i);
  sprintf(status_info,"%s\t", buff) ;

}

char *field_names[] = {(char *)"\nSignal strength:       ",
                       (char *)"Network type:          ",
                       (char *)"Reg Status:            ",
                       (char *)"SIM ID:                ",
                       (char *)"Bearer:                ",
                       (char *)"Device ID:             ",
                       (char *)"SIM Phone Number:      ",
                       (char *)"Provider name:         ",
                       (char *)"SIM Ready:             ",
                       (char *)"Packet Switch Status:  "};


char *field_notes[] = { (char *)"0=Undetectable or -xxx for dBm value",
                        (char *)"0=GSM/UMTS, 1=LTE, 2=CDMA-EVDO, 3 = CDMA-ix/RTT)",
                        (char *)"0=not registered, 1=reg with home, 2=reg with roaming partner, 3=data session active",
                        (char *)"NO= could not read, maybe no SIM",
                        (char *)"0=unknown, 2=2G, 3=3G, 4=4G, 5=CDMA",
                        (char *)"IMID or EMID depending network type",
                        (char *)"SIM Phone Number",
                        (char *)"Provider name",
                        (char *)"SIM Ready status 0=No, 1=Yes",
                        (char *)"Packet Switching Info"};

void show_status_file(char *buff){
vector<string> fields;
int i;
string sta = buff;

   fields = split_str( sta, '\t');
   for (i=0; i < fields.size(); i++){
       printf("%s%c[1;33;40m%s%c[0;37;40m -> %s\n", field_names[i], 0x1B, fields[i].c_str(), 0x1B, field_notes[i]);

   }


}

void status_scan() {
FILE *fp;
int i;
char buff[150];
char reg_info[25];
bool isRegistered = false;
struct timeval Now;

  if( verbose ){
       if (page) {
          gettimeofday(&Now, NULL);
          printf("\nStart status scan at %d Secs .............................................................................\n", Now.tv_sec);
        } else
          printf("\nStart status scan.........................................................................................................\n");
  }
  status_buff.clear();
  if (cell_status_log){
     // check if we still have a serial port connection with Radio modem
     if ( !wait_for_connection(3)) {
          log_msg("----- Lost communications with modem, sleep 2 seconds to recover\n");
          sleep(2);
          if ( item_exists( CELL_MONITOR_SHUTDOWN)) app_shutdown();
          //CloseCOM();
          //TODO: reset cell_status_monitor by system("/usr/sbin/reset_cell_status_monitor &")
          // reset_cell_status_monitor could be a script that sleeps(2); /usr/sbin/cell_status_monitor &
          // of a c program - wait for current cell_status_monitor process to terminate and then invoke cell_status_monitor
          //log_msg("----- Attempt to rebuild the connection with the modem\n");
          //if ( !connect_to_radio() ) {
          //   log_msg("----- Failed to reconnect with the modem (all AT commands will fail !!!!!!!!!!!\n");
          //}
     } else
       log_msg("----- Serial connection with modem is GOOD\n");
  //   RunAT( "AT+CFUN?\r");
  //   log_msg(szResponse);
   //    if ( !in_normal_mode() )
   //        log_msg("----- At start of status scan Radio is not in Normal mode!\n");
  }

  isRegistered = get_network_reg_status(reg_info);
  if(isRegistered){
           if(regState != 2){
               debuglog(LOG_INFO,"cell_status_monitor: Detected network registration!");
               regState = 2;
          }
  } else {
       if (regState != 1){
           debuglog(LOG_INFO,"cell_status_monitor: Not registered with network!");
           regState = 1;
        }
  }

  if(modem_busy) return;   // abort scan
  get_signal_strength(buff, isRegistered);
   if(modem_busy) return;

  status_buff.append(buff);
  status_buff.append(reg_info);

  if ( item_exists( CELL_MONITOR_SHUTDOWN)) app_shutdown();

  if (cell_type > 4)
     Qget_bearer_and_cell_tower_info(buff);
  else
      get_bearer_and_cell_tower_info(buff, isRegistered);

  if(modem_busy) return;
  status_buff.append(buff);

  if (cell_type > 4)
       Qget_radio_params( status_buff, isRegistered);
  else
       get_radio_params( status_buff, isRegistered);
 // if(modem_busy) return;

  if ( item_exists( CELL_MONITOR_SHUTDOWN)) app_shutdown();

  /* if we have lost registation we can fake a 0 signal strength to force commlib to rebuild connection */
// TBD
//  if (!isRegistered && regState == 2 && status_buff[0] != "0"){
//       debuglog(LOG_INFO,"cell_status_monitor: Lost registration with network. force signal strength to 0!");
//       i = status_buff.find('\t');
//       status_buff.replace(0, i, "0\t");
//  }

  // save status info to /tmp/cell_status
  write_status_file((char *)status_buff.c_str());

  if (!isRegistered && regState == 2){
     debuglog(LOG_INFO,"cell_status_monitor: Lost registration with network. cell_status update disabled!");
     debuglog(LOG_INFO,"cell_status_monitor: last cell_status: %s", status_buff.c_str());
     disable_cell_status_update = true;
  }


//  fp = fopen("/tmp/tmp_cell_status", "wb");
//  fwrite(status_buff.c_str(), 1, status_buff.length(), fp);
//  fflush(fp);  // flush data to OS buffer
//  fsync(fileno(fp)); // flush OS buffer to disk
//  fclose(fp);
//  system("mv /tmp/tmp_cell_status /tmp/cell_status");

 // if(verbose) hexdumpprint("cell_status results:", (char *)status_buff.c_str(), status_buff.length());

  if(cell_status_log)
      log_msg((char *)status_buff.c_str());

  if (show_status){
     if ( page || (status_buff != status_copy)){
          strcpy(status_copy, status_buff.c_str());
          show_status_file(status_copy);
     }
  }

//parse_status_str( status_buff);
  status_buff.clear();
}


  /*  get the signal strength thresholds from titanlib.conf
  *  and store for later use when determining number of bars
     to show on LED.

  Must read titanlib.conf to get current signal strength ranges.
  Lower the sig_strength value the better the signal strength.

cellular_poor_threshold = 90
cellular_fair_threshold   = 80     81 or >   =  1 bar     weaker
                                               80 to 71  =  2 bar    fair has the shortest range
cellular_good_threshold = 70     72  or <   = 3 bar    good range from 72 .... 0

   if sig_strength > fair_threshold then show 1 bar
   if sig_strength  >= fair_threshold and sig_strength <= good_threshold then show 2 bars
   if sig_strength < goodThreshold then show 3 bars

  */
void  get_signal_strength_thresholds (){
CIniFile titanlib;
CCHR  *pCChar;

     if( verbose ) printf("Getting threshold values from titanlib.conf...\n");
     titanlib.OpenIniFile("/mnt/flash/config/conf/titanlib.conf");
     pCChar =  titanlib.ReadString("General","cellular_good_threshold", "70");
     good_threshold = atoi(pCChar);
     pCChar =  titanlib.ReadString("General","cellular_fair_threshold", "80");
     fair_threshold = atoi(pCChar);
     titanlib.CloseIniFile();
 }

unsigned int read_options() {
FILE *fp;
char *p;
char flags[100];
unsigned int opts;

   fp = popen("read_config -n option", "r");
   if (fp != NULL){
      fgets(flags, sizeof(flags), fp);
      pclose(fp);
      p = strchr(flags, '\n');
      if (p == NULL){
         flags[0] = '\0';
      } else
      *p = '\0';
   } else
      flags[0] = '\0';

    opts = (unsigned int)strtol(flags, NULL, 0);
    if ( verbose ) printf("Read options = 0x%0X\n", opts);
    isExtendedMode = ( opts & OPT_EXTENDED_LED );
    enable_sig_led = (opts & OPT_ENABLE_SIG_LED);
    if(enable_sig_led)
          if( verbose ) printf("Sig LED activation enabled");
    else if(verbose)
          printf("Sig LED is dectivated (allways off)");
    return opts;
}

void recover_cellular_connection() {
   if ( verbose )
      printf("Radio not responding to AT commands, attempt to reboot radio...");
   debuglog(LOG_INFO,"Radio not responding to AT commands, attempt to reboot radio...");
   system("cell_power disable");
   sleep(2);
   system("cell_power enable");
   if ( !item_exists( CELL_ENABLE_FILE))
       app_exit("Failed to recover radio, aborting cell_status_monitor!");
}

// open ttyS2 and connect to device
bool connect_to_radio() {

  // open ttyS2 and connect to device
  if (OpenCOM(comm_port, 115200)) {
        PurgeCOM();
        if( verbose ) printf("Com Port opened, wait for connection to radio...\n");
        if (cell_status_log)  log_msg("Opened com port, attempt communications with Radio...\n");
        if ( wait_for_connection(15000) ) {
            return true;
        } else {
            if (cell_status_log)  log_msg("Radio modem is not responding!\n");
           CloseCOM();
        }
  } else {
      if (cell_status_log)  log_msg("Failed to open Cellular APP port /dev/ttyS2! abort");
      app_exit("Failed to open Cellular APP port /dev/ttyS2!");
  }

  return false;
}


// simulate when we lose communication with radio, no response fom AT commands
bool connect_to_radio_test() {

  if (!wait_for_enumeration(60)) {
        app_exit("Cellular Modem failed to enumerate!");
  }

  // open ttyS2 and connect to device
  if (OpenCOM(comm_port, 115200)) {
        PurgeCOM();
        if( verbose ) printf("Opening com port...\n");
        wait_for_connection(15000);
        CloseCOM();
if( verbose ) printf("Closing port after simulated err, radio not responding\n");
  } else {
      app_exit("Failed to open Cellular APP port /dev/ttyS2!");
  }

  return false;
}



/* call this function after set_to_airplain_mod was called
 * expect a SYSSTART URC
 * NOTE: if already in normal mode there will be no URC
 */
bool set_to_normal_mode(){
bool ret = true;

     if ( isNormalMode  ) return true;
     // wait for URC  ^SYSSTART after command
//ELS31-V does not produce ^SYSSTART   just the OK
     if (cell_type > 3)
          ret = RunAT("AT+CFUN=1\r");
     else
          ret = RunAT("AT+CFUN=1\r", "SYSSTART\r");
     if( !ret )
        if ( !in_normal_mode( ) )
            ret = false;
     if  ( !ret ){
        isNormalMode = false;
     } else
        isNormalMode = true;
     return ret;
}
void wait_before_scan() {
DWORD dwTime;
    dwTime = 0;
    while (true)  {
        if ( item_exists( CELL_MONITOR_SHUTDOWN)) app_shutdown();
        WaitMs(WAIT_INC);
        dwTime += WAIT_INC;
        if (dwTime >= SCAN_TIME)  break;
    }
}

void write_status_file(char *txt){
FILE *fp;
//log_msg(txt);  // debug

  /* if registration is lost we stop updating the cell_status file
     it is up to the user of cell_status to notice the unregistered status
     and stop cell_status_monitor via cell_power off
  */
  if (disable_cell_status_update)   return;

   // save status info to /tmp/cell_status
  fp = fopen("/tmp/tmp_cell_status", "wb");
  fwrite(txt, 1, strlen(txt), fp);
  fflush(fp);  // flush data to OS buffer
  fsync(fileno(fp)); // flush OS buffer to disk
  fclose(fp);
  system("mv /tmp/tmp_cell_status /tmp/cell_status");
  if (debug_display_count < 4){
     debuglog(LOG_INFO, "cell_status_monitor: write cell_status: %s", txt);
     debug_display_count++;
  }

  if(verbose) {
     printf("SignalStrength,NetworkType,RegStatus,SimId,Bearer,DeviceId,PhoneNum,Provider,SimReady,PacketSwitch\n");
     //hexdumpprint("cell_status results:", (char *)txt, strlen(txt));
     printf("cell_status: %s\n", txt);
  }
}

/*
AT^SIND "simstatus" only exists in PXS8 The 4G AT docs indicate it exists but these modmes do not have this field, instead they have undocumented  "simtray"
*/

void pre_scan(){
FILE *fp;
int i,x;
char *p;
char *field = NULL;
char buff[100];
char reg_tag_buff[20];
char reg_cmd[20];
char sim_tag_buff[20];
char sim_cmd[20];
int registered_state = 0; // set to not registered
bool indicateNoSim = false;

   if( verbose )  printf("\nStart 4G pre-scan for SIM detection and Network registration ....\n");
  //TODO ELS31 does not need LED control
   if (cell_type < 5){
       system("titan3_led cell 3 > /dev/null"); // set led to searching status
       if (verbose) printf("CELL Led set to searching state");
   }

  if( cell_type == CELL_ELS31_V ){
     strcpy(reg_cmd, "AT+CEREG?\r");
     strcpy(reg_tag_buff, "CEREG: ");
     strcpy(sim_cmd,"AT+CCID\r");
     strcpy(sim_tag_buff, "CCID: ");
  } else if( cell_type == CELL_PLS8){  // PXS8's GSM/UMTS network registration CREG  is unreliable (at least for Verizon)
     strcpy(reg_cmd, "AT+CGREG?\r");
     strcpy(reg_tag_buff, "CGREG: ");
     strcpy(sim_cmd,"AT^SCID\r");
     strcpy(sim_tag_buff,"SCID: ");
  } else if( cell_type == CELL_PLS62_W){
     strcpy(reg_cmd, "AT+CREG?\r");
     strcpy(reg_tag_buff, "CREG: ");
     strcpy(sim_cmd,"AT+CCID?\r");
     strcpy(sim_tag_buff, "CCID: ");
  } else if ( cell_type > 4){
     strcpy(sim_cmd,"AT+QCCID\r");
     strcpy(sim_tag_buff, "QCCID: ");
  }



//  for(;;){
//       if ( item_exists( CELL_MONITOR_SHUTDOWN)) app_shutdown();
//       if (RunAT(sim_cmd) ) {
//             p = strstr( szResponse,sim_tag_buff);
//             if (p != NULL){
//                 i = read_signal_strength_value();  // wait for a signal strength value
//                 if (i != 0) break;
//                 WaitMs(500);
//
//                 for(x=0;;x++){
//                    i = read_signal_strength_value();  // wait for a signal strength value
//                    if (i != 0) break;
//                    if ( item_exists( CELL_MONITOR_SHUTDOWN)) app_shutdown();
//                    if (x == 3)
//                       write_status_file("0\t\t\t\t\t\t\t\t1\t\t");
//                    else if (x < 3)
//                      sleep(1);
//                 }
//                 break;
//             }
//       }
//       WaitMs(500);
//       if(!indicateNoSim ){
//           write_status_file("0\t\t\t\t\t\t\t\t0\t\t");
//           indicateNoSim = true;
//       }
//  }


for(i=0; i < 6; i++){
       if ( item_exists( CELL_MONITOR_SHUTDOWN)) app_shutdown();
       if (RunAT(sim_cmd) ) {
             p = strstr( szResponse,sim_tag_buff);
             if (p != NULL) break;
       }
       sleep(1);
}
if (p == NULL ){
    write_status_file("0\t\t\t\t\t\t\t\t0\t\t");
    for(;;){
       if ( item_exists( CELL_MONITOR_SHUTDOWN)) app_shutdown();
       WaitMs(500);
    }
}
// NOTE: removed the following line which would create the cell_status file
//       The presensce of this file with no signal strength causes the wireless_timesync to hang
//       showing cmd line message: check_ppp_reg_timeout:423 signal=0x0, type=0x0, rstatus=0x0
//       If cell_status is not created the wireless_timesync will timeout
write_status_file("0\t\t\t\t\t\t\t\t1\t\t"); // indicate the SIM was detected

for(;;){
   i = read_signal_strength_value();  // wait for a signal strength value
   if (i != 0) break;
   WaitMs(500);
   if ( item_exists( CELL_MONITOR_SHUTDOWN)) app_shutdown();
}


  //debuglog(LOG_INFO, "cell_status_monitor: exit 4G pre-scan with signal strength: %d", i);
  sig_strength = abs(i);
  sprintf(buff,"%d", i);
  sprintf(buff, "%s\t\t0\t\t\t\t\t\t1\t\t", buff);
  write_status_file(buff);
  //debuglog(LOG_INFO,"cell_status_monitor: First scan: %s", buff);
//  write_status_file("0\t\t0\t\t\t\t\t\t1\t\t");   // set SIM ready only


 //  for(;; ){
 //      if ( item_exists( CELL_MONITOR_SHUTDOWN)) app_shutdown();
 //      if (RunAT(reg_cmd) ) {
 //            // sometimes we get OK but no status info, check for valid status in the response
 //            if ( strstr(szResponse, reg_tag_buff) != NULL){
 //                p = strstr(szResponse, reg_tag_buff) ;
 //                if( p != NULL){
 //                    field = get_field_value(p,1);
 //                    if (*field == '1')
 //                         registered_state = 1;  // registered with home network
 //                    else if (*field == '5')
 //                         registered_state = 2;  // registered while roaming
 //                }
 //            }
 //            if (registered_state != 0) break;
 //     }
 // }
 // sprintf(buff, "0\t\t%d\t\t\t\t\t\t1\t\t", registered_state);
 // write_status_file(buff);
 // system("titan3_led cell 4 > /dev/null"); // set lead to registered status
 // if (verbose) printf("CELL Led set to registered state");


}

//TODO - move this to utils.cpp since it is used in several modules
bool wait_for_enumeration(int enumerate_to){
FILE *fp;
char buff[200];
int i;

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

void reset_radio_module() {

     RunAT("AT+CFUN=1,1\r");
     PurgeCOM();
     szResponse[0] = 0;

     if (!wait_for_enumeration(60)) {
          app_exit("Device failed to enumerate after reset!");
     }

     if ( !ReadComPort(szResponse, 300, 90000) )  { // wait 60 secs for URC
          app_exit("Failed to get URC after reset!");
     }

     while (strstr(szResponse, "SYSLOADING")) {
           szResponse[0] = 0;
           if ( !ReadComPort(szResponse, 300, 90000) )
              app_exit("Failed to get URC after reset!!");
     }

     if (strstr(szResponse,"^SYSSTART") == NULL)
         debuglog(LOG_INFO,"Failed to confirm URC after reset!!!");
     PurgeCOM();
     if( !wait_for_connection( 35 ) )
         app_exit("Failed to connect to cellular Radio after reset!");

     if( !set_to_normal_mode())
         app_exit("Failed to set radio to Normal mode");

}


int main (int argc, char *argv[] ){
FILE *fp;
struct sigaction sa;
int i;
unsigned long scanTime;
unsigned int opts;
char commkey;

  log_msg("cell_status_monitor starting up.........................");

  cdma_carrier[0] = 0;
  if (argc == 2){
       if( strcmp(argv[1], "-v") == 0)
          verbose = true;
       if( strcmp(argv[1], "-vp") == 0){
          verbose = true;
          page = true;
          show_status = true;
       }
       if( strcmp(argv[1], "-s") == 0)
          show_status = true;

   }
   sa.sa_handler = &sig_terminate;
   sa.sa_flags = SA_RESTART;   // Restart the system call, if at all possible
   sigfillset(&sa.sa_mask);  // Block every signal during the handler
   if (sigaction(SIGTERM, &sa, NULL) == -1) {
       perror("Error: cannot handle SIGTERM"); // Should not happen
   }
   if (sigaction(SIGUSR1, &sa, NULL) == -1) {
       perror("Error: cannot handle SIGUSR1"); // Should not happen
   }
   debuglog(LOG_INFO,"cell_status_monitor running...");
   if ( item_exists( CELL_STATUS_LOG_ENABLE)){
    debuglog(LOG_INFO,"cell_status_monitor logging enabled.");
       cell_status_log = true;
       log_msg(status_description);
   }

   opts = read_options();
   get_signal_strength_thresholds();

   if ( (fp = fopen(CELL_TYPE_FILE, "rb")) != NULL) {
     cell_type = fgetc(fp);
     fclose(fp);
     if (cell_type > 4)
        strcpy(comm_port, QUECTEL_COMM_PORT);
     else
        strcpy(comm_port, CENTERION_APP_PORT);

     if (cell_type == CELL_PXS8) {
        if( verbose ) printf("Recognize PXS8 Cinterion radio\n");
     } else if (cell_type == CELL_PLS8) {
         if( verbose ) printf("Recognize PLS8 Cinterion radio\n");
     } else if (cell_type == CELL_PLS62_W) {
         if( verbose ) printf("Recognize PLS62-W Cinterion radio\n");
     } else if (cell_type == CELL_ELS31_V) {
         if( verbose ) printf("Recognize ELS31-V Cinterion radio\n");
     } else if( cell_type == CELL_EC25_AF) {
         if( verbose ) printf("Recognize EC25-AF Quectel radio\n");
     } else if( cell_type == CELL_EC25_G){
         if( verbose ) printf("Recognize EC25-G Quectel radio\n");
     } else {
        if(verbose) printf("Failed value from cell_type = %d\n", cell_type);
        if( verbose ) printf("Exit cell_status_monitor with error: Unknown Cellular type\n");
        debuglog(LOG_INFO, "cell_status_monitor aborted: Unknown Cellular type");
        exit(1);
     }
  } else {
      app_exit("Failed to open /tmp/cell_type!");
      if( verbose ) printf("Exit cell_status_monitor with error: Failed to open /tmp/cell_type\n");
      debuglog(LOG_INFO, "cell_status_monitor aborted: Failed to open /tmp/cell_type");
      exit(1);
  }


   if ( !connect_to_radio()){
 //if ( !connect_to_radio_test() ){
        // if no response from Radio via serial portAT command
        // then we need to take drastic action to establish connection
        recover_cellular_connection();
        if(!connect_to_radio()) {
            app_exit("Failed to connect to radio after radio firmware reboot!");
        }
   }
   if (verbose) printf("Made connection with Radio via serial AT port\n");


//  if (cell_type == CELL_PLS62_W) {   NOTE: this fix was put in cell_powe off
//       reset_radio_module();
//
//  } else {
        isNormalMode = in_normal_mode();
        if (isNormalMode) {
             if(verbose) printf("Found Radio in Normal mode, expected Airplane mode!?\n");
        } else {
            if( verbose ) printf("Connected to radio module, switch to Normal mode...\n");
            if( !set_to_normal_mode())
               app_exit("Failed to set radio to Normal mode");
        }
 // }
  remove(CELL_MONITOR_SHUTDOWN);
  system("touch /tmp/cell_power_on");
  system("sync");
  // get the type of tech in use
  if (cell_type == CELL_PXS8) {   // 3G Radio
       if (RunAT("AT^SCFG=\"MEopMode/AccT\"\r")) {
         if ( strstr( szResponse, "GSM/UMTS") != NULL) {
             tech = UMTS;
             if( verbose ) printf("Detected UMTS tech\n");
         } else {
            tech = CDMA;
            if (RunAT("AT^SCFG=\"CDMA/Operator/Store\"\r")){
               if ( strstr( szResponse, "Verizon") != NULL)
                  strcpy(cdma_carrier, "Verizon");
               else
                  strcpy(cdma_carrier, "Sprint");
            }
            if( verbose ) printf("Detected CDMA tech\n");
        }
      }
   } else {   // 4G radio
      tech = LTE;
      if (cell_type == CELL_ELS31_V)
           RunAT("AT^SLED=2\r");
      else if (cell_type == CELL_PLS62_W ) { //|| cell_type == CELL_PLS8){
           RunAT("AT+COPS=0\r");
      }
//TODO - will Quectel need something after switching to Normal mode ????////
      if( verbose ) printf("Detected LTE tech\n");
//TODO if PLS8-X then do a prescan just looking for SIM detection and if detected get reg status
      // if sim detected create cell_status with just that info
      // when and if we get registered update cell_status with reg info and then exit pre_scan to perfrom normal scan
      // AT^SCID
      // AT+CGREG?
      // create cell_status file with just those to fields populated
      // Stay in loop until we arg registered, then continue to status_scan
      pre_scan();

   }

  status_scan();
  while(true) {
      // WaitMs(SCAN_TIME);
      if (page) {
          printf("\nPress Enter to repeat status scan (or q+Enter to Quit: ");
          commkey = getchar();
          if (commkey == 'q' || commkey == 'Q')  app_shutdown();
      } else
          wait_before_scan();

      if(!disable_cell_status_update)
         status_scan();
      else
         debuglog(LOG_INFO, "cell_status_monitor: registration dropped, cell_status frozen!");
  }

   return 0;
}
