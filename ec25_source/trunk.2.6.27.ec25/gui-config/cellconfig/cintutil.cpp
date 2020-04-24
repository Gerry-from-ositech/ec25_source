/*
 * This file contains proprietary information and is subject to the terms and
 * conditions defined in file 'OSILICENSE.txt', which is part of this source
 * code package.
 */

/* The cintutil module provides a Cinterion access utility that presents a menu and allows the user
 * to perform some basic operations such as reading status, switching carriers etc. An AT command
 * processor is also included.
 *
 * Note: Since AT commands are sent through ttyS2 serial port, this program must not be used if other
 *       processes that use this serial port are in operation. These include:1
 *        1 - a minicom session with ttyS2 open
 *        2 - the configurator when it is configurating the cellular connection
 *        3 - cell_status_monitor - this daemon runs when "cell_power on" was called to
 *          st the radio to Normal mode for communications.
 *
 *    A check is made for item 3 above and if found running the cell_status_monitor will be shut down and
 *    config_start will be called.
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
#include <sys/select.h>
#include <termios.h>
#include "../util/serial_at.h"
#include "../util/utils.h"

using namespace std;
extern char line_buff[300];

#define Version "Ver 2.1.0"
#define PLS8_MDM_PORT   "/dev/ttyACM3"

#define CELL_TYPE_FILE  "/tmp/cell_type"
#define CELL_ID_FILE    "/tmp/cell_id"

#define CELL_PXS8 1
#define CELL_PLS8 2
#define CELL_PLS62_W 3
#define CELL_ELS31_V 4
#define CELL_EC25_AF 5
#define CELL_EC25_G  6

int verbose = false;
bool normal_mode = false;
bool is_umts = false;
bool is_4g = false;
char szResponse[3000];
char buff[200];
char mbuff[200];
char current_account[50];
char current_carrier[50];
char sim_phone_num[20];
char sim_id[30];
char imi[60];
char current_mdn[20];
char commkey;
char radio_description[300];
char meid[50];
char comm_port[50];


char rbuf[20];
int _kbhit();

void power_on_radio() ;
void power_off_radio() ;

struct stat stFileInfo;

int cell_type = 0;


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
bool RunAT( const char * cmd, const  char* urc = NULL, DWORD toMillSec = 20000 ) {
bool ret;
      if ( verbose )   hexdump("AT Command:", (char *)cmd,  strlen(cmd ));
      szResponse[0] = 0;
       PurgeCOM();
      if (urc == NULL) {
            ret = ATCommand(cmd, szResponse, ARRAYSIZE(szResponse));
      } else {
           ret =  ATCommandUrc(cmd, szResponse, ARRAYSIZE(szResponse),  toMillSec,  (char *)urc);
      }
      if (verbose)  hexdump("AT Response:", szResponse, strlen(szResponse));
      return ret;
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
      if( strstr(  szResponse  , ": 1") != NULL )
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
bool set_to_airplane_mode() {
bool ret = false;
     // check if we are already in Airplane mode
    if ( !normal_mode ) return true;
    if (cell_type > 4)
      ret = RunAT("AT+CFUN=4\r");
    else
      ret = RunAT("AT+CFUN=4\r","AIRPLANE MODE\r");
    normal_mode = in_normal_mode();
    if ( !ret ){
         if ( !normal_mode) {
               printf("Failed response to AT+CFUN=4 but succesfully set to Airplane mode!\n");
               ret = true;
         } else
             printf("Failed to set Airplane mode!\n");
    } else if (normal_mode) {
         printf("Failed to switch to Airplane mode even though command was succesfull!\n");
         ret = false;
    }
    return ret;
}

bool set_to_normal_mode() {
bool ret = false;
     // check if we are already in Airplane mode
    if ( normal_mode ) return true;
    if (cell_type > 4)
        ret = RunAT("AT+CFUN=1\r");
    else
        ret = RunAT("AT+CFUN=1\r","SYSSTART");
    normal_mode = in_normal_mode();
    if ( !ret ){
         if ( normal_mode) {
               printf("Failed response to AT+CFUN=1 but succesfully set to Normal mode!\n");
               ret = true;
         } else
             printf("Failed to set Normal mode!\n");
    } else if (!normal_mode) {
         printf("Failed to switch to Normal mode even though command was succesfull!\n");
         ret = false;
    }
    return ret;
}


/* parse specified integer value from an AT response
 * and return a pointer to a string that holds the value.
 *
 * Use for MDN, MSID, PRL etc
 * title - the name of the parameter as it is in the response ie: "CSQ: "
 * data - AT command response data
 * num - the integer to be paresed 0...n
 *
 * Assumes integeres are seperated by ","
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

int _kbhit() {
    static const int STDIN = 0;
    static bool initialized = false;

    if (! initialized) {
        // Use termios to turn off line buffering
        termios term;
        tcgetattr(STDIN, &term);
        term.c_lflag &= ~ICANON;
        tcsetattr(STDIN, TCSANOW, &term);
        setbuf(stdin, NULL);
        initialized = true;
    }

    int bytesWaiting;
    ioctl(STDIN, FIONREAD, &bytesWaiting);
    return bytesWaiting;
}

/* Frame error rate is associated with AT_CSQ? */
void display_frame_error_rate(int val) {

  switch(val) {
     case 0:
       printf("Frame Error Rate less than 0.01%%\n");
       break;
     case 1:
       printf("Frame Error Rate 0.01%% to less than 0.1%%\n");
       break;
     case 2:
       printf("Frame Error Rate 0.1%% to less than 0.5%%\n");
       break;
     case 3:
       printf("Frame Error Rate 0.5%% to less than 1.0%%\n");
       break;
     case 4:
       printf("Frame Error Rate 1.0%% to less than 2.0%%\n");
       break;
     case 5:
       printf("Frame Error Rate 2.0%% to less than 4.0%%\n");
       break;
     case 6:
       printf("Frame Error Rate 4.0%% to less than 8.0%%\n");
       break;
     case 7:
       printf("Frame Error Rate greater than 8.0%%\n");
       break;
     default:
       printf("Frame Error Rate (%d) Unknown!\n", val);
  }

}

/* display signal strength value from AT+CSQ?
 * as a dBm value based on the PXS8 AT Command doc
 */
void display_signal_strength(int val){

  switch(val){
      case -1:
        printf("Failed to get Signal Strength with AT+CSQ?\n");
        break;
      case 0:
        printf("Signal Strength very low: -113 dBm or less\n");
        break;
      case 1:
        printf("Signal Strength = -111 dBm\n");
        break;
      case 99:
        printf("Signal Strength not known or not detectable\n");
        break;
      default:
         val = 111 - (val-1) * 2;
         printf("Signal strength = -%d dBm\n", val);
        break;
  }
}
char *covertToUpper(char *str){
    char *newstr, *p;
    p = newstr = strdup(str);
    while(*p = toupper(*p)) p++;

    return newstr;
}
// split a string on specified delimiter nd return new vector of items parsed
// vector <string> x = split_str("one:two:three::five")
//  x has 5 items with the forth one empty
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


/* Use AT_CSQ to get current signal strength
 * and return as integer value
 * For UMTS use AT+CSQ
 * For CDMA use AT+CSQ?
 */
int frame_error_rate;
int get_signal_strength(){
int i,v;
char *p;
char cmd[25];
 printf("Attempting to get signal strength...\n");
   if(is_umts) {
        strcpy(cmd, "AT+CSQ\r");
   } else
        strcpy(cmd, "AT+CSQ?\r");

   if ( !RunAT( cmd) )
       return -1;
    p = parse_int_value((char *)"CSQ: ",szResponse, 0);
    if (p == NULL)
        return -1;
    v = atoi(p);
    p = parse_int_value((char *)"CSQ: ",szResponse, 1);
    frame_error_rate = atoi(p);
    return v;
}

/*
 * Get PRL version - works in normal mode
 * AT$PRL
 * $PRL: 58015
 */
char *get_prl_version() {
char *prl = NULL;

    // assume we are in normal mode AT+cfun=1
    if ( RunAT( "AT$PRL?\r")){
          prl = parse_int_value((char *)"PRL: ", szResponse, 0);
    }
    return prl;
}

/* Display current Cellular radio type and carrier
 * If UMTS attempt to get info from SIM if inserted
 */
void  show_current_config() {
char *resp;
    printf("\nCurrent account type is %s\n", current_account);
    if (strstr(current_account,  "cdma") != NULL){
          printf("Current carrier is %s\n", current_carrier);
          if (current_mdn[0]  !=  '\0')
              printf("MDN: %s\n", current_mdn);
          if( meid[0] != '\0')
              printf("MEID: %s\n", meid);
          // get PRL version
          //resp = get_prl_version();
          //if(resp != NULL)
          //   printf("Current PRL version = %s\n", resp);
    } else{
         if (strstr(current_carrier, "No sim") != NULL)
                printf("No Sim detected!\n");
         else
              printf("Current carrier is %s\n", current_carrier);
         if (sim_id[0] != 0)
              printf("SIM inserted %s\n", sim_id);

        if (cell_type > 4) {
            if (imi[0] == '\0') ;
            else
              printf("International Mobile Subscriber Id: %s\n", imi);
        }

         if (sim_phone_num[0] != '\0')
              printf("Subscriber phone number: %s\n", sim_phone_num);
         else
              printf("Subscriber phone number could not be read!\n");
    }

//    display_signal_strength(get_signal_strength());
//    display_frame_error_rate(frame_error_rate);
}


/* return -1 - if not a Cinterion PLSX8 or PLS8_x
 * return 1 - is PXS8
 * return 2 - if PLS8
 * return 3 - if PLS62
 */
int get_cinterion_type() {
char *resp;
char *p;

  if ( RunAT("ATI1\r")   &&  strstr( szResponse, "Cinterion") != NULL) {
      strcpy(radio_description, trim_response(szResponse));
      if (strstr(szResponse,"PXS8")){
          // printf("Found PXS8 Cinterion Radio\n");
           return 1;
      }
      if (strstr(szResponse,"PLS8")){
         // printf("Found PLS8 Cinterion Radio\n");
           return 2;
      }
      if (strstr(szResponse,"PLS62")){
         // printf("Found PLS8 Cinterion Radio\n");
           return 3;
      }
      printf("Wrong Radio type detected!\n%s",szResponse);
  }
  return -1;

}

void read_PLS62_sim() {
char *p1, *p2;
string str, s;
vector<string> items;

   current_carrier[0] = '\0';
   sim_phone_num[0] = '\0';
   sim_id[0] = '\0';

  if ( RunAT( "AT+CCID?\r") ){
         if (strstr(szResponse,"ERROR")){
              strcpy(current_carrier, "No sim");
              return;
         }
         p1 = strstr(szResponse, "CCID: ");
         p1 = p1+6;
         p2 = strchr(p1, '\r');
         *p2 = '\0';
         strcpy(sim_id,p1);

         if ( RunAT( "AT+CNUM\r") ){
                 // we can get optional Carrier name/or and phone number from response
                 str =szResponse;
                 items.clear();
                 items = split_str( str, ',');
                 if (items.size() > 2) {
                     // printf("%d  %s, %s, %s\n", items.size(), items[0].c_str(), items[1].c_str(), items[2].c_str());
                     if (current_carrier[0] == '\0'){
                          // this may have carrier name , SIMs return  various data , could have blank fields           ;
                          size_t found = items[0].find("\"");
                          if (found != string::npos ){
                                found++;
                                s = items[0].substr(found, items[0].length()-(found+1));
                                strcpy(current_carrier, s.c_str());
                          }
                     }
                     s = items[1].substr(1, items[1].length()-2);
                     strcpy(sim_phone_num, s.c_str());
                 }
          } else
              strcpy(sim_phone_num, "Not provided!") ;
          if (current_carrier[0] == '\0')
               strcpy(current_carrier, "Unknown");
  }

}


/* return False if no SIM detected, otherwise return True */
bool read_EC25_sim(){
bool ret = true;
int i, respLen;
char mdn[100];
char *p, *p1;

  imi[0] = '\0';
  sim_id[0] = 0;
  if ( RunAT( "AT+CPIN?\r") ) {
       if(strstr(szResponse, "READY")){
            if ( RunAT("AT+CIMI\r") ) {
                p = &szResponse[2];
                //printf("%s", p);
                p1 = strchr(p, '\r');
                *p1 = 0;
                //printf("%s\n", p);
                strcpy(imi, p);

            } else
                printf(szResponse);

            if ( RunAT("AT+QCCID\r")){
                 p = strstr(szResponse, "CID:");
                 p1 = get_field_value(p, 0);
                strcpy(sim_id,p1);

            } else
                printf("%S",szResponse);
            mdn[0] = 0;
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
                           mdn[i] = 0;
                    }
                }
                if (mdn[0] == '\0')
                   strcpy(sim_phone_num, "Not Provided");
                else
                   strcpy(sim_phone_num, mdn);
           } else {
              strcpy(sim_phone_num, "Unknown");

           }
       } else {
           printf("%s",szResponse);
           return false;
       }
  } else {
      printf("%s", szResponse);
      ret = false;

  }
  return ret;

}


// return NULL if no sim detected
/* us AT+CNUM
* get response +CME ERROR: SIM not inserted
* or +CNUM: "PROVIDER    ", "+12267914545", 145  or similar
*
* The AT^SOPS=? command may return provider name but it takes a long time ???
*/
void read_sim() {
char *resp;
char *p1, *p2;
string str, s;
bool bad_sim = false;
char sim_tag_buff[20];
char sim_cmd[20];
vector<string> items;

   current_carrier[0] = '\0';
   sim_phone_num[0] = '\0';
   sim_id[0] = '\0';
   for(;;){
     if ( RunAT( "AT^SIND?\r") ) {
           //hexdump(resp, strlen(resp));
           // check simstatus to see if SIM is installed if 0 = not installed, if 1 = installed, if 5 initialization complete
           p1 = strstr(szResponse,"simstatus");
           p2 = strchr(p1, '\n');
           *p2 = '\0';
           //hexdump(p1, strlen(p1));
           str = p1;
           items = split_str( str, ',');
           //printf("check items in vector of size %d, item 2  = %s\n", items.size(), items[2].c_str());
           if (items[2][0] == '0') {
                 strcpy(current_carrier, "No sim");
                 sim_phone_num[0] = '\0';
                 return;
           }
           if (items[2][0] == '1') {
              printf("Detected SIM inserted, initialization Not complete. Cannot get access!\n");
              bad_sim = true;
           }
           *p2++;
           p1 = strstr(p2,"eons");   // if in Normal mode the service provider name may be in this payload
           p2 = strchr(p1, '\n');
           *p2 = '\0';
           //hexdump(p1, strlen(p1));
           str = p1;
           items.clear();
           items = split_str( str, ',');
           s = items[3].substr(1, items[3].length()-2);
           if (s.length() > 0){
                  strcpy(current_carrier, s.c_str());
                  //printf("Length = %d, str = %s\n", s.length(), s.c_str());
          }
           if ( !bad_sim && RunAT( "AT+CNUM\r") ){
                 // we can get optional Carrier name/or and phone number from response
                 str =szResponse;
                 items.clear();
                 items = split_str( str, ',');
                 if (items.size() > 2) {
                     // printf("%d  %s, %s, %s\n", items.size(), items[0].c_str(), items[1].c_str(), items[2].c_str());
                     if (current_carrier[0] == '\0'){
                          // this may have carrier name , SIMs return  various data , could have blank fields           ;
                          size_t found = items[0].find("\"");
                          if (found != string::npos ){
                                found++;
                                s = items[0].substr(found, items[0].length()-(found+1));
                                strcpy(current_carrier, s.c_str());
                          }
                     }
                     s = items[1].substr(1, items[1].length()-2);
                     strcpy(sim_phone_num, s.c_str());
              }
          } else
              strcpy(sim_phone_num, "Not provided!") ;
          if (current_carrier[0] == '\0')
               strcpy(current_carrier, "Unknown");
          break;
     }
     break;
   }

   if( cell_type == CELL_PLS62_W){
     strcpy(sim_cmd,"AT+CCID?\r");
     strcpy(sim_tag_buff, "CCID: ");
   } else {
     strcpy(sim_cmd,"AT^SCID\r");
     strcpy(sim_tag_buff,"SCID: ");
   }

    if (!bad_sim &&  RunAT( sim_cmd) ) {
         p1 = strstr(szResponse, sim_tag_buff);
         p1 = p1+2;
         p2 = strchr(p1, '\r');
         *p2 = '\0';
         strcpy(sim_id,p1);
   }
}



/* a properly configure radio will restart into Airplane mode */
bool restart_module() {
bool ret = true;
int i;

     if ( !RunAT("AT+CFUN=1,1\r")) {
           ret = false;
     } else {
           // wait for the radio to reset to finish before we can send commands
         printf("Restarting Cinterion, please wait...\n");
          if ( WaitCTS(TRUE, 20000) ) {
               if( WaitCTS(FALSE, 20000) ) {
                   if ( WaitCTS(TRUE, 20000) ) {
                        // first command after reset may screwup response, send dummy command
                        ATCommand("AT\r", szResponse, ARRAYSIZE(szResponse));
                        ret = true;
                        normal_mode =  in_normal_mode();
                        if ( normal_mode )
                             printf("Restart to Normal mode complete.\n");
                        else
                             printf("Restart to Airplane mode complete.\n");
                   }
               }
          }
     }

    return ret;
}

/*
* return value (10 digit number) in string
* length() == 0 = AT command failed
* otherwise string = 10 digit number, could be 000000.....
*/
char *get_cdma_mdn() {
char *mdn = NULL;

  // assume we are in normal mode AT+cfun=1
  if ( RunAT( "AT$MDN?\r") ){
        mdn = parse_int_value((char *)"MDN: ", szResponse, 0);
  }
  return mdn;
}



/* verizon account must be activated with valid MDN  */
bool switch_to_verizon() {
char *resp;
bool ret = true;
size_t f;
/*
char *mdn = get_cdma_mdn();  <-- have to be in cdma2000 or we get error

       if (mdn != NULL){
            printf("Failed to get MDN!\n");
            return false;
       }
       if ( strstr(mdn, "000000") == 0 ){
              printf("Verizon profile has not been activated!\n");
              return false;
      }
*/
     if (strstr(current_account, "GSM/UMTS") != NULL) { // if currently in UMTS
           // select cdma2000 account
          if ( !RunAT( "AT^SCFG=\"MEopMode/AccT\",\"cdma2000\"\r"))
                return false;
          else
                strcpy(current_account, "cdma2000");
     }
     if (strstr(current_carrier,"Sprint") != NULL) {
           // switch to air plain mode to block any network access during the switch
           if ( set_to_airplane_mode() ){
               // save previous CDMA profile
               // save current profile
               RunAT( "AT^SCFG=\"CDMA/Operator/Store\",1,\"Sprint\"\r");
                // Load Verizon profile  from store
               RunAT("AT^SCFG=\"CDMA/Operator/Store\",0,\"Verizon\"\r");
               set_to_normal_mode();
          }
          else return false;
     }

     return true;

}

bool switch_to_sprint(){
char *resp;
bool ret = true;
size_t f;
/*
char *mdn = get_cdma_mdn();  <-- have to be in cdma2000 or we get error

       if (mdn != NULL){
            printf("Failed to get MDN!\n");
            return false;
       }
       if ( strstr(mdn."000000") == )0{
              printf("Sprint profile has not been activated!\n");
              return false;
      }
*/

     if (strstr(current_account, "GSM/UMTS") != NULL) { // if currently in UMTS
           // select cdma2000 account
          if ( !RunAT( "AT^SCFG=\"MEopMode/AccT\",\"cdma2000\"\r"))
                return false;
          else
                strcpy(current_account, "cdma2000");
     }
     if (strstr(current_carrier,"Verizon") != NULL) {
        // switch to air plain mode to block any network access during the switch
         if ( set_to_airplane_mode() ){
            // save previous CDMA profile
            // save current profile
            RunAT( "AT^SCFG=\"CDMA/Operator/Store\",1,\"Verizon\"\r");
           // Load Sprint profile
            RunAT("AT^SCFG=\"CDMA/Operator/Store\",0,\"Sprint\"\r");
            set_to_normal_mode();
         } else
            return false;
     }
     return true;
}


bool activate_verizon(){
char *resp;
char old_mdn[20];
char old_prl_version[20];
int ret = 0;
int i;

bool success = false;

   resp = get_prl_version();
   if (resp)
     strcpy(old_prl_version, resp);
   resp = get_cdma_mdn();
   if (resp)
      strcpy(old_mdn,resp);

   for(;;) {
         if ( !set_to_airplane_mode() )
            break;

//         if (strstr(current_account, "GSM/UMTS") != NULL)
//                resp = RunAT(appPort, "AT^SCFG=\"MEopMode/AccT\",\"cdma2000\"\r", 15);

         // attempt to load profile from storage
         if ( !RunAT( "AT^SCFG=\"CDMA/Operator/Store\",0,\"Verizon\"\r") ){
              printf("Failed to load profile from storage, attempting to reload factory settings");
              if ( !RunAT( "AT^SCFG=\"CDMA/Operator/Store\",2,\"Verizon\"\r") ) break;
         }
         // loading a profile may take some time, make sure it is completed by checking for OK
//         if (strstr(resp, "OK") == NULL){
//                resp = getResponse(appPort, 5);
//                hexdump((char *)"AT Command Response 2: ", resp, strlen(resp));
//        }
         if  (!restart_module( ) ) break;
        // radio comes up in normal mode

        display_signal_strength(get_signal_strength());
        display_frame_error_rate(frame_error_rate);

        printf("Starting Verizon Activation sequence...\n");
       if(verbose)
               hexdump((char *)"Send AT Command: ", (char *)"ATD*22899\r", 10);
          // wait for up to 2 mins
          ret = ATCommandUrc("ATD*22899\r", szResponse,  ARRAYSIZE(szResponse),  120000, (char *)"NO CARRIER");
          if(verbose && szResponse[0] != '\0') {
              hexdump((char *)"URCs:", szResponse, strlen(szResponse));
          }
          if (ret) {
              if ( strstr(szResponse,"ACTIVATION:  Success") != NULL ){
                      RunAT( "AT^SCFG=\"CDMA/Operator/Store\",1,\"Verizon\"\r");  // airplane mode
                     set_to_normal_mode();
                     success = true;
               }  else  if ( strstr(szResponse,"ACTIVATION:  Failed") != NULL ) {
                     printf("Failed Verizon activate URC, may be on raoming network.\n");
               }
          } else {
                printf("Failed to get dial command response!\n");
          }
        break;
   }

  if (success) {
      printf("Activation process successful, verify MDB...\n");

     // get and display MDN
     resp = get_cdma_mdn();
     if (resp == NULL){
          printf("Failed to get MDN!\n");
     } else {
         printf("MDN: %s\n", resp);
         if (strcmp(resp, old_mdn) != 0){
             printf("Previous MDN was %s\n", old_mdn);
         } else {
             printf("MDN did not change!\n");
         }
    }
    // get adn display PRL version
     resp = get_prl_version();
     if (resp == NULL){
          printf("Failed to get PRL Version!\n");
     } else {
         printf("PRLN: %s\n", resp);
         if (strcmp(resp, old_prl_version) != 0){
             printf("Previous PRL version was %s\n", old_prl_version);
         } else {
             printf("PRL version did not change!\n");
         }
    }
  }
  return success;
}

bool activate_sprint(){
char *resp;
int ret = 0;
int i,fd;
char old_mdn[20];
char old_prl_version[20];
bool success = false;

   resp = get_prl_version();
   if (resp)
     strcpy(old_prl_version, resp);
   resp = get_cdma_mdn();
   if (resp)
      strcpy(old_mdn,resp);
   for(;;) {
         if ( !set_to_airplane_mode() )   break;
         // attempt to load profile from storage
         if ( !RunAT( "AT^SCFG=\"CDMA/Operator/Store\",0,\"Sprint\"\r")) {
               printf("Failed to load profile from storage, attempting to reload factory settings");
               if( ! RunAT( "AT^SCFG=\"CDMA/Operator/Store\",2,\"Sprint\"\r") ) break;
         }
//         // loading a profile may take some time, make sure it is completed by checking for OK
//         if (strstr(resp, "OK") == NULL){
//                resp = getResponse(appPort, 5);
//                hexdump((char *)"AT Command Response 2: ", resp, strlen(resp));
//         }
         printf("Started Sprint Activation sequence (OMADM: HFA Required)...\n") ;
         if ( !restart_module( ) ) break;
        if ( !set_to_normal_mode() ) break;
        // check URCs to monitor activation and PRL upload status
          printf("Waiting for Sprint URCs...\n");
         szResponse[0] = 0;
         while (ReadComPort(szResponse, 800, 20000) ) {
                 if (strstr(szResponse, "Activation Process Complete") != NULL){
                           success = true;
                           break;
                   } else if (strstr(szResponse, "Connect Failed") != NULL){
                           success  = false;
                            break;
                   } else if (strstr(szResponse, "HFA Process Started") != NULL){
                            success = false;
                            break;
                   }
          }
          if ( szResponse[0] == '\0')
                 printf( "No URCs received within 20 seconds!\n");
          else
               hexdump((char *)"URCs:", szResponse, strlen(szResponse));
          break;
  }
  set_to_normal_mode();

  if (success){
     // get and display MDN
     resp = get_cdma_mdn();
     if (resp == NULL){
          printf("Failed to get MDN!\n");
     } else {
         printf("MDN: %s\n", resp);
         if (strcmp(resp, old_mdn) != 0){
             printf("Previous MDN was %s\n", old_mdn);
         } else {
             printf("MDN did not change!\n");
         }
    }
    // get adn display PRL version
     resp = get_prl_version();
     if (resp == NULL){
          printf("Failed to get PRL Version!\n");
     } else {
         printf("PRLN: %s\n", resp);
         if (strcmp(resp, old_prl_version) != 0){
             printf("Previous PRL version was %s\n", old_prl_version);
         } else {
             printf("PRL version did not change!\n");
         }
    }
  }
  return success;
}

void get_cdma_meid() {
char *p, *p1;
  meid[0] = 0;
  if (RunAT( "AT+GSN\r") ){
     p = strstr(szResponse,"0x");
     p1 = strchr(p,'\r');
     *p1 = 0;
     strcpy(meid,p);
  } else {
    printf("Failed to read MEID!\n");
  }

}
// get current account, current carrier, if UMTS check for SIM
// also get MEID
void  get_current_radio_config(){
char *resp;
size_t f;
char *mdn;

  current_mdn[0] = '\0';
  current_account[0] = '\0';
  current_carrier[0] = '\0';
  sim_phone_num[0] = '\0';
  sim_id[0] = '\0';

  if (is_4g){
      is_umts = true;
      strcpy(current_account, "\"GSM/LTE\"");
      sleep(1);  // ad some time in case Sim needs to be fully initialized

      if (cell_type == CELL_PLS62_W)
        read_PLS62_sim();
      else if (cell_type > 4)
        read_EC25_sim();
      else
         read_sim();

  } else {
     if (  RunAT( "AT^SCFG=\"MEopMode/AccT\"\r") ){
          if (strstr(szResponse,"cdma2000") != NULL) {
              is_umts = false;
              strcpy(current_account, "\"cdma2000\"");
              if ( RunAT( "AT^SCFG=\"CDMA/Operator/Store\"\r") ) {
                  if (strstr(szResponse,"Verizon") != NULL)
                          strcpy(current_carrier, "\"Verizon\"");
                  else
                          strcpy(current_carrier, "\"Sprint\"");
                   if( normal_mode) {
                       // get and display MDN
                      mdn = get_cdma_mdn();
                      if (mdn != NULL)
                          strcpy(current_mdn, mdn);
                 }
              }
              get_cdma_meid();
          } else {
               is_umts = true;
               strcpy(current_account, "\"GSM/UMTS\"");
               sleep(1);  // ad some time in case Sim needs to be fully initialized
               read_sim();
          }
      }
  }
  show_current_config();
}

char verizon_menu [] = "\n=========== Verizon Configuration =============\n\
1: Activate Verizon\n\
2: Restore Factory settings (clears out MDN!)\n\
3: Show Current Provider info\n\
q: Quit\n\n\
Enter Selection: ";

void configure_verizon(){
bool changed = false;
char *resp;

    show_current_config();
     while(1) {
              printf(verizon_menu);
              commkey = getchar();
              while (getchar() != '\n');
              if ( commkey == 'q' || commkey == 'Q')  break;
              changed = false;
              switch(commkey) {
                    case 'v':  // toggle verbose
                       verbose  =  ~verbose;
                       if(verbose)
                           printf("Verbose output enabled!\n");
                      else
                           printf("Verbose output disabled!\n");
                      continue;
                      break;
                    case '1':
                       changed = activate_verizon();
                       break;
                    case '2':
                       if ( set_to_airplane_mode() ) {
                         if (  RunAT( "AT^SCFG=\"CDMA/Operator/Store\",2,\"Verizon\"\r")) {
                              changed = true;
                          }
                       }
                       break;
                    case '3':
                      //show_current_config();
                      get_current_radio_config();
                     break;
             }
             set_to_normal_mode();
             if (changed)
                 //show_current_config();
              get_current_radio_config();
      }
}

char sprint_menu [] = "\n=========== Sprint Configuration =============\n\
1: Activate Sprint\n\
2: Restore Factory settings\n\
3: Show Current Provider info\n\
q: Quit\n\n\
Enter Selection: ";
bool configure_sprint() {
bool changed = false;
char *resp;

    show_current_config();
     while(1) {
              printf(sprint_menu);
              commkey = getchar();
              while (getchar() != '\n');
              if ( commkey == 'q' || commkey == 'Q')  break;
              changed = false;
              switch(commkey) {
                    case 'v':  // toggle verbose
                       verbose  =  ~verbose;
                       if(verbose)
                           printf("Verbose output enabled!\n");
                      else
                           printf("Verbose output disabled!\n");
                      continue;
                      break;
                    case '1':
                       changed = activate_sprint();
                       break;
                    case '2':
                      // put in airplain mode
                        if ( set_to_airplane_mode() ){
                            if ( RunAT( "AT^SCFG=\"CDMA/Operator/Store\",2,\"Sprint\"\r") ){
                               changed = true;
                            }
                        }
                       break;
                    case '3':
                      //show_current_config();
                      get_current_radio_config();
                     break;
             }
             set_to_normal_mode();
             if (changed)
                 //show_current_config();
              get_current_radio_config();
      }
}

void display_sim_info() {
   sleep(1); // add some time so SIM can be initialized and read
   read_sim();

   if (strstr(current_carrier, "No sim") != NULL)
       printf("No Sim detected!\n");
   else {
       if (sim_id[0] != 0)
           printf("SIM inserted %s\n", sim_id);
       printf("Current carrier is %s\n", current_carrier);
   }

   if (sim_phone_num[0] != '\0')
      printf("Subscriber phone number: %s\n", sim_phone_num);
   else
      printf("Subscriber phone number: Not readable!\n");
}

char unts_config_menu [] = "\n========  Umts Configuration ==========\n\
1: Change or insert a SIM\n\
q: Quit\n\n\
Enter Selection: ";

bool configure_umts(){
char *resp;


//TODO: may require a cell_power disable in order to detect a SIM switch
//      need to experiment with this !!1
     while(1) {
              printf(unts_config_menu);
              commkey = getchar();
              while (getchar() != '\n');
              if ( commkey == 'q' || commkey == 'Q')  break;
              switch(commkey) {
                 case 'v':  // toggle verbose
                    verbose  =  ~verbose;
                    if(verbose)
                        printf("Verbose output enabled!\n");
                 else
                        printf("Verbose output disabled!\n");
                   continue;
                   break;
                 case '1':
                   printf("Power off radio, please wait...\n");
                  power_off_radio();
                   for(;;) {
                      sleep(1);
                      if ( stat("/tmp/cell_power_on", &stFileInfo) != 0)  break;
                   }
                   printf("Insert or replace SIM then enter any key to power on and read SIM.\n");
                   while (!_kbhit() );
                   getchar();  // eat the entered char
                   printf("Powering on Radio, please wait...\n");
                    power_on_radio();
                   /* resp = get_urc(appPort, Sysstart_Urc, 25);
                   if ( resp == NULL){  //timedout
                      printf("Failed to get SYSSTART urc!\n");
                      continue;
                   } */
                   display_sim_info();
                   break;
              }
        }
}

char change_menu [] = "\n========  Switch from %s ==========\n\
1: Switch to %s\n\
2: Switch to %s\n\
3: Show Current Provider info\n\
q: Quit\n\n\
Enter Selection: ";

// See Multi Carrier for Cinterion App Note 67
/* switch from current carrier designated by title param to carrier1 or carrier2 */
void change_provider(const char *title,  const char *carrier1,  const char *carrier2){
char *resp;
bool op_status;
char menu[200];
const char *choice;

   sprintf(menu, change_menu, title, carrier1, carrier2);
   while(1) {
              printf(menu);
              commkey = getchar();
              while (getchar() != '\n');
              if ( commkey == 'q' || commkey == 'Q')  break;
              op_status = false;
              switch(commkey) {
                    case 'v':  // toggle verbose
                       verbose  =  ~verbose;
                       if(verbose)
                           printf("Verbose output enabled!\n");
                      else
                           printf("Verbose output disabled!\n");
                      continue;
                      break;
                    case '1':
                        if (strcmp(carrier1, "Verizon") == 0)
                               op_status = switch_to_verizon();
                        else if (strcmp(carrier1, "Sprint") == 0)
                               op_status = switch_to_sprint();
                        choice = carrier1;
                        break;
                    case '2':
                          if ( !set_to_normal_mode() ) {
                               printf("Failed to set normal mode");
                          } else {
                            if (strcmp(carrier2, "Umts") == 0){
                                 if(verbose) printf("Set account to GSM/UMTS...\n");
                                 if ( RunAT( "AT^SCFG=\"MEopMode/AccT\",\"GSM/UMTS\"\r") ) {
                                        op_status = true;
                                 } else {
                                       op_status = false;
                                 }
                            } else if (strcmp(carrier2, "Sprint") == 0)
                               op_status = switch_to_sprint();
                            choice = carrier2;
                        }
                        break;
                    case '3':
                       //show_current_config();
                       get_current_radio_config();
                       break;
                    default:
                        printf("Invalid menu selection, try again.\n");
                        continue;
            }
            if (op_status){
                  // switch was successful. do system restart, can monitor URCs
                 printf("Operation successful, Resetting Radio...\n");
                 restart_module();
                /* if ( strcmp(choice, "Sprint") == 0){
                        // TODo - check for URCs as a result of OMADM activity
                     printf("Get possible OMADM URCs...\n");
                      ;
                 }  */
                 get_current_radio_config();
                 break;
            } else
                printf("Operation failed!\n");
  }

}

// these commands may produce URCs
bool change_mode(char *cmd) {
bool at_ok = true;

        if ( strstr(cmd, "AT+CFUN=1") != NULL){
          if ( normal_mode )
               at_ok = RunAT(cmd);
          else
              set_to_normal_mode();
        } else if ( strstr(cmd, "AT+CFUN=4") != NULL) {
            if ( !normal_mode )
                at_ok = RunAT(cmd);
            else
               set_to_airplane_mode();
        }

        return at_ok;
}
/*
 * At the > prompt enter the AT command
 * Enter q to quit and go back to main menu
 * Pres Enter to rerun the last AT command executed
 * Enter . to read App port
 * Enter
 * e.g.: at+cmee=2   <- enables verbose error reporting on AT commands
 *
 */

void AT_console() {
int i;
int handle;
char line[120];
char cmd[120];
char *resp, *p;
bool cmd_ok = false;

 // send initial OK command
 //  resp = RunAT(appPort, "ATi\r", 15);
 //  if (resp == NULL) {
 //        printf("Failed to initialize AT command mode!\n");
 //        return;
 //  }
   printf("\nYour in the AT console using %s, \nPress Enter to rerun last command\nEnter . to read App port\nEnter q to quit.", CENTERION_APP_PORT);
  cmd[0] = '\0';
  for(;;) {
       printf(" \n> ");
       if ( fgets(line, 120, stdin) == NULL)
            continue;
       if ( line[0] == 'q') break;
       if( line[0] == '.') {
            //resp = read_from_serial(appPort,5);
            printf("Reading Application port...\n");
            szResponse[0] = 0;
            cmd_ok = ReadComPort(szResponse, 200, 5000);
       } else if( strlen(line) == 1 && cmd[0] != '\0'){
              cmd_ok = RunAT( cmd);
       } else {
            szResponse[0] = 0;
            line[strlen(line)-1] = '\r';
            p  = covertToUpper(line);
            strcpy(cmd, p);
           if ( strstr(cmd, "AT+CFUN=1,1") != NULL ){
                 restart_module( );
                 continue;
            } else  if ( strstr(cmd, "AT+CFUN=1") != NULL || strstr(cmd, "AT+CFUN=4") != NULL ){
                 cmd_ok = change_mode(cmd);
            } else
               cmd_ok =  RunAT( line);
      }
      if (cmd_ok) {
          if ( szResponse[strlen(szResponse)-1]  != '\n')
              strcat(szResponse,"\n");
          printf(szResponse);
      }
  }

}

void show_enumeration(int toSec){
FILE *fp;
char buff[200];
int i;

   for (i=0; i < (toSec*2); i++ ) {
         printf("\nRun lsusb...\n");
         fp = popen("lsusb", "r");
         if (fp != NULL) {
               while ( fgets(buff, sizeof(buff), fp) ) {
                   printf(buff);
                }
         }
         pclose(fp);
         WaitMs(500);
   }

}

void wait_for_enumeration(){
FILE *fp;
char buff[300];

     for(;;){
         fp = popen("tail -n 1 /tmp/messages", "r");
         if (fp != NULL) {
             fgets(buff, sizeof(buff), fp);
             //printf(buff);
             if (strstr(buff, "ttyACM0")){
                pclose(fp);
                printf(buff);
                return;
             }
         }
         pclose(fp);
     }
}

/* command SFDL is sent over MDM port ttyACM3 but after enumeration ttyACM3 does not exist, instead need to use ttyACM0 */

void Pls8_firmware_update_test(){
BYTE bData[100];
  // close APP port and open MDM port ttyACM3

  printf("closing PLS8 APP port...\n");
  CloseCOM();

   if ( !OpenCOM((char *)PLS8_MDM_PORT, BAUD_115200 ) ){
           printf("Failed to open PLS8 MDM Port %s!\n", PLS8_MDM_PORT );
           return;
   }
   printf("Opened PLS8 MDM port %s\n",  PLS8_MDM_PORT);
   if ( !wait_for_connection(10)) {
       printf("Failed to connect to modem!\n");
       return;
   }

   printf("Press Enter to start firmware update command test\n");
    while (!_kbhit() );
   getchar();  // eat the entered char

    if (!RunAT("AT^SFDL\r")) {
        printf("No OK response from AT^SFDL!\n");
        return;

    }
    printf("Close MDM port, wait for enumeration then open MDM port....\n");
    CloseCOM();
    wait_for_enumeration();
    sleep(5);
    if ( !OpenCOM((char *)"/dev/ttyACM0", BAUD_115200 ) ){
           printf("Failed to open PLS8 MDM Port %s!\n", "/dev/ttyACM0" );
           return;
    }

    printf("Opened PLS8 MDM port %s, waiting forever for input\n",  "/dev/ttyACM0");
    bData[0] = 0;
    printf("Reading port");
    for(;;) {

        if ( SerGetchTimeout(&bData[0], 10000)) {
            printf("\nReceived data from port = 0x%0x", bData[0] );
            return;
        }
        printf(".");
        WaitMs(200);
    }

}

char * get_creg_field(char *data){
char *p;
      data = strchr(data,',');
      data++;
      p = strchr(data,'\n');
      *p = '\0';
      return data;
}

char * get_sreg_field(char *data){
char *p, *p1;
      p = strchr(data, ',');
      p++;
      p1 = strchr(p,',');
      *p1 = '\0';
      return p;
}


/* deprecated
0     RSSI  -112 dBm
1 - 4 RSSI in 15 dB steps
   1 -97
   2 -82
   3 -67
   4 -52
5     RSSI  -51 dBm
99    RSSI not known or not detectable
*/

char * rssi_value(short rssi){
  if (rssi == 99)
    strcpy(buff, "Rssi not known or not detectable" );
  else {
      switch(rssi){
          case 0:
            strcpy(buff, "0 = -112dBm");
            break;
          case 1:
            strcpy(buff, "1 = -97dBm");
            break;
          case 2:
            strcpy(buff, "2 = -82dBm");
            break;
          case 3:
            strcpy(buff, "3 = -67dBm");
            break;
          case 4:
            strcpy(buff, "4 = -52dBm");
            break;
          case 5:
            strcpy(buff, "5 = -51dBm");
            break;
          default:
            sprintf(buff,"Invalid rssi value = %d", rssi);
            break;
      }
  }
  return buff;

}

char *reg_status(short reg, short roam, bool isUmts){
  if (isUmts){
    if(is_4g) {
        switch(reg){
          case 0: // 0 = other  is registered or is searching
            strcpy(buff, "0 = LTE is registered or is searching");
            break;
          case 1: // 1 = LTE registered to home network
            strcpy(buff, "1 = LTE registered to home network");
            if (roam)
               strcat(buff," ROAMING INDICATED!");
            break;
          case 2: // 2 = LTE not registered but searching
            strcpy(buff, "2 = LTE not registered but searching");
            if (roam)
               strcat(buff," ROAMING INDICATED!");
            break;
          case 3: // 3 = Registration denied
            strcpy(buff, "3 = Registration denied");
            if (roam)
               strcat(buff," ROAMING INDICATED!");
            break;
          case 5: // 5 = LTE registered, roaming with foreign network (domestic or international)
            strcpy(buff, "LTE registered, roaming");
             if (!roam)
               strcat(buff," ROAMING NOT INDICATED!");
            break;
          default:
            sprintf(buff, "Invalid CREG: response = %d\n", reg);
            break;
       }
    } else {
     switch(reg){
        case 0: // 0 = cdma is registered or is searching
          strcpy(buff, "0 = CDMA is registered or is searching");
          break;
        case 1: // 1 = UMTS registered to home network
          strcpy(buff, "1 = UMTS registered to home network");
          if (roam)
             strcat(buff," ROAMING INDICATED!");
          break;
        case 2: // 2 = UMTS not registered but searching
          strcpy(buff, "2 = UMTS not registered but searching");
          if (roam)
             strcat(buff," ROAMING INDICATED!");
          break;
        case 3: // 3 = Registration denied
          strcpy(buff, "3 = Registration denied");
          if (roam)
             strcat(buff," ROAMING INDICATED!");
          break;
        case 5: // 5 = UMTS registered, roaming with foreign network (domestic or international)
          strcpy(buff, "UMTS registered, roaming");
           if (!roam)
             strcat(buff," ROAMING NOT INDICATED!");
          break;
        default:
          sprintf(buff, "Invalid CREG: response = %d\n", reg);
          break;
     }
   }

  } else{
    switch(reg){

         case 0:  // 0 Not registered on cdma2000 1x/RTT network.
           strcpy(buff, "0 Not registered on cdma2000 1x/RTT network");
           break;
         case 1:  // 1 Registered on cdma2000 1x/RTT home network.
           strcpy(buff, "1 Registered on cdma2000 1x/RTT home network");
           break;
         case 2:  // 2 Not registered, but UE is currently searching for a new cdma2000 1x/RTT operator.
           strcpy(buff, "2 Not registered, searching for a new cdma2000 1x/RTT operator.");
           break;
         case 3:  // 3 Registration denied.
           strcpy(buff, "3 Registration denied.");
           break;
         case 4:  // 4 Unknown registration state.
           strcpy(buff, "4 Unknown registration state.");
           break;
         case 5:  // 5 Registered on roaming 1x/RTT network.
           strcpy(buff, "5 Registered on roaming 1x/RTT network");
           break;
         default:
            sprintf(buff, "Invalid CREG: response = %d\n", reg);
            break;
    }
  }
  return buff;
}

int twoG[] =   {0,3,4,5,6};
int threeG[] = {0,5,6,7,8};
int fourG[] =  {0,6,7,9,10};

void get_cell_tower_info(){
vector<string> fields;
vector<string> fields1;
int *index;
int i;
string data;

    if (RunAT("AT^SMONI\r") ) {
       data = &szResponse[10];
       fields = split_str( data, ',');
       if ( fields[0] == "2G"){
              index = &twoG[0];
       } else if (fields[0] == "3G"){
             index = &threeG[0];
       } else {
             index = &fourG[0];
       }
       for (i=0; i < 5; i++){
           fields1.push_back(fields[index[i]]);
       }
       data[data.find("\r")] = '\0';
       printf("\n%s Cell in service, Tower info: %s\n", fields1[0].c_str(), data.c_str());
       for (int i = 1; i < 5; i++){
          if( fields1[0] != "4G"){
              switch(i){
                case 1:
                   printf("\tMCC:  %s\n", fields1[i].c_str());
                   break;
                case 2:
                   printf("\tMNC:  %s\n", fields1[i].c_str());
                   break;
                case 3:
                   printf("\tLAC:  %s\n", fields1[i].c_str());
                   break;
                case 4:
                   printf("\tCell ID:  %s\n\n", fields1[i].c_str());
                   break;
              }
          } else {
              switch(i){
                case 1:
                   printf("\tMCC:  %s\n", fields1[i].c_str());
                   break;
                case 2:
                   printf("\tMNC:  %s\n", fields1[i].c_str());
                   break;
                case 3:
                   printf("\tGlobal Cell ID:  %s\n", fields1[i].c_str());
                   break;
                case 4:
                   printf("\tPhysical Cell ID:  %s\n\n", fields1[i].c_str());
                   break;
              }
          }
       }
    }
}

void show_current_pls62_status(){
char *p;
short reg;

  read_PLS62_sim();

  if (strstr(current_carrier, "No sim") != NULL)
         printf("No Sim detected!\n");
  else
       printf("Current carrier is %s\n", current_carrier);
  if (sim_id[0] != 0)
       printf("SIM inserted %s\n", sim_id);
  if (sim_phone_num[0] != 0)
       printf("Subscriber phone number: %s\n", sim_phone_num);
  else
       printf("Subscriber phone number could not be read!\n");

  if ( RunAT("AT+CREG?\r")){
      p = strstr( szResponse,"CREG");
      if (p != NULL) {
           p = parse_int_value((char *)"CREG: ", szResponse, 1);
           reg = atoi(p);
           if (reg == 1 || reg == 5){
                get_cell_tower_info();
            }
      }
   }

}

void show_current_ec25_status() {
char *p;
int reg ;
  if ( !read_EC25_sim() )
       printf("No Sim detected!\n");
  if (sim_id[0] != 0)
       printf("SIM inserted %s\n", sim_id);
  if (sim_phone_num[0] != 0)
       printf("Subscriber phone number: %s\n", sim_phone_num);
  else
       printf("Subscriber phone number could not be read!\n");

  if ( RunAT("AT+CREG?\r")){
      p = strstr( szResponse,"CREG");
      if (p != NULL) {
           p = parse_int_value((char *)"CREG: ", szResponse, 1);
           reg = atoi(p);
           if (reg == 1 || reg == 5){
                get_cell_tower_info();
            }
      }
   }

}

void show_current_status(){
bool isUmts = false;
char *resp, *p, *p1;

short rssi;
short roam;
short reg;
short simstatus;

   printf("\nRadio Type: %s ", current_account);

   if (cell_type > CELL_ELS31_V)
      return show_current_ec25_status();

   if (cell_type == CELL_PLS62_W)
      return   show_current_pls62_status();

   if (current_carrier[0] != '\0')
     printf("- %s", current_carrier);

   if (strstr(current_account,"UMTS") != NULL || strstr(current_account,"LTE") != NULL)
     isUmts = true;
   printf("\n");

   if ( ! RunAT("AT^SIND?\r") )  {
       printf("Failed AT^SIND?\n");
       return;
   }
   p = parse_int_value((char *)"rssi", szResponse, 1);
   rssi = atoi(p);

   p = parse_int_value((char *)"roam", szResponse, 1);
   roam = atoi(p);

   if (isUmts) {
        p = parse_int_value((char *)"simstatus", szResponse, 1);
        simstatus = atoi(p);
        if( normal_mode ) {
              // must be in Normal mode for CREG command
              if (  !RunAT( "AT+CREG?\r")  || strstr(szResponse, "CREG:") == NULL)  {
                   printf("Failed AT+CREG?\n");
                   return;
              }
              p = parse_int_value((char *)"CREG: ", szResponse, 1);
              //p = get_creg_field(resp);
              reg = atoi(p);
        }
   } else if( normal_mode ){
       if ( !RunAT("AT^SREG?\r"))  {
           printf("Failed AT^SREG?\n");
           return;
       }
  //     p = get_sreg_field(resp);
       p = parse_int_value((char *)"SREG: ", szResponse, 1);
       reg = atoi(p);
   }
   if( normal_mode) {
       display_signal_strength(get_signal_strength());
       display_frame_error_rate(frame_error_rate);
       printf("Registration status: %s\n", reg_status(reg,roam,isUmts));
   }
   if (isUmts) {
      if ( simstatus == 0)
          printf("No SIM detected!\n");
      else {
         printf("SIM detected \n");
         if (simstatus == 5){
             // can read sim to get provider name
             ; //TODO
         }
      }
      if (reg == 1 || reg == 5){
          get_cell_tower_info();
      }
   }
}

void test_kbhit(){
int i = 0;
    printf("Press any key to exit\n");
    while (! _kbhit()) {
        printf("This is test %d...\r",i++);
        fflush(stdout);
        usleep(1000000);  // wait 1 second
    }
    getchar();  // eat the entered char
    printf("\nDone.\n");

}

char * get_reg_state(){
static int buff_size = -1;
int i;
char *resp, *p, *p1;
bool isRegistered = false;

     i = get_signal_strength();

     switch(i) {
        case -1:
        case 99:
          sprintf(buff,"RSSI: Unknown, perhaps no network!");
          break;
        case 0:
          sprintf(buff, "RSSI -113 dBm or less");
          break;
        case 1:
          sprintf(buff,"RSSI: -111 dBm");
          break;
        default:
           i = 111 - (i-1) * 2;
           sprintf(buff, "RSSI -%d dBm", i);
           break;
     }
     display_frame_error_rate(frame_error_rate);

    if (strstr(current_account,  "cdma") == NULL){
        if( !RunAT( "AT+CREG?\r") ) return NULL;
        p = parse_int_value((char *)"CREG: ", resp, 1);
        i = atoi(p);
        switch(i){
           case 0:
             strcat(buff," Not searching for UMTS network - (no SIM?)");
             break;
           case 1:
              strcat(buff," Registered to UMTS home network");
              isRegistered = true;
              break;
           case 2:
              strcat(buff, " Searching for UMTS network...");
              break;
           case 3:
              strcat(buff, " Registration denied on UMTS network!");
              break;
           case 4:
               strcat(buff, " Unknown registration status!");
               break;
           case 5:
               strcat(buff, " Registered on roaming UMTS network");
               isRegistered = true;
               break;
        }
        if ( isRegistered ) {
            // get carrier name from SIM
            if ( RunAT( "AT^SIND?\r") ){
                  p = strstr(resp,"eons");
                  p1 = strchr(p, ',');
                  p = strchr(++p1, ',');
                  p1 = strchr(++p, ',');
                  p = strchr(++p1, ',');
                  p1 = strchr(++p, ',');
                  *p1 = '\0';
                  sprintf(mbuff, " - %s", p);
            }
            else
               strcpy(mbuff, " - Unknown");
            strcat(buff, mbuff);
        }
    } else {
        if (!RunAT( "AT^SREG?\r"))
            return NULL;

        p = parse_int_value((char *)"SREG: ", resp, 1);
        i = atoi(p);
        if ( i == 0){
           p = parse_int_value((char *)"SREG: ", resp, 1);
           i = atoi(p);
           switch(i){
               case 0:
                  strcat(buff, " Searching for CDMA network...");
                  break;
               case 1:
                  strcat(buff, " Camped on CDMA (EVDO) home network");
                  break;
               case 2:
                  strcat(buff, " Searching for CDMA (EVDO) network...");
                  break;
               case 3:
                  strcat(buff, " Camping on CDMA (EVDO) roaming network");
                  break;
               default:
                  sprintf(mbuff, "AT^SREG error returned %d for regStatusEVDO", i)  ;
                  strcat(buff, mbuff);
                  break;
           }
        } else {
            switch(i){
               case 1:
                  strcat(buff," Registered to CDMA (1x/RTT) home network");
                  isRegistered = true;
                  break;
               case 2:
                  strcat(buff, " Searching for CDMA (1x/RTT) network...");
                  break;
               case 3:
                  strcat(buff, " Registration denied on CDMA network!");
                  break;
               case 4:
                   strcat(buff, " Unknown registration status!");
                   break;
               case 5:
                   strcat(buff, " Registered on roaming CDMA (1x/RTT) network");
                   isRegistered = true;
                   break;
            }
        }
        if ( isRegistered ) {
            sprintf(mbuff, " - %s", current_carrier);
            strcat(buff, mbuff);
        }
    }

    i = strlen(buff);
    if (buff_size != -1 && i < buff_size)
        memset(&buff[i],' ', buff_size -i);

    buff_size = i;
    strcat(buff,"\n");
    return (char *)buff;
}

void monitor_reg_status(){
char *state_str;

    set_to_normal_mode();
    state_str = get_reg_state();
    printf("\nTo Exit, press any key and wait...\n");
    while (! _kbhit()) {
        if (state_str != NULL) {
             printf(state_str);
             fflush(stdout);
        }
        usleep(3000000);  // wait 3 second
        state_str = get_reg_state();
    }
    getchar();  // eat terminating char
}

bool show_PXS8_settings(){
char *resp;
char *p;
char c;

     printf( "\n%s", radio_description);
     if ( RunAT("AT^SCFG?\r") )
         printf( trim_response(szResponse ));

    if (  RunAT( "AT^SDPORT?\r") ) {
        if (strstr(szResponse,"ERROR")) {
             printf(szResponse);
        } else {
              resp = trim_response(szResponse);
              c = resp[9];
              printf("SDPORT = %c\n", c);
        }
     }


    if (  RunAT( "AT+CTZU?\r") ) {
        if (strstr(szResponse,"ERROR")) {
             printf(szResponse);
        } else {
              resp = trim_response(szResponse);
              c = resp[7];
              printf("CTZU (Automatic Time Zone URC) = %c\n", c);
        }
     }

     if (  RunAT( "AT+CMEE?\r") ) {
         if (strstr(szResponse,"ERROR")) {
              printf(szResponse);
         } else {
               resp = trim_response(szResponse);
               c = resp[7];
               printf("CMEE = %c\n", c);
         }
     }

     if( is_umts ){
        // Show BPRS related info
        RunAT( "AT^SGCONF?\r");
        resp = trim_response(szResponse);
        resp++;
        p = strchr(resp, '\r');
        *p = 0;
        printf("%s\n",resp);
     }
     RunAT( "AT^SAD=10\r");
     resp = trim_response(szResponse);
     resp++;
     p = strchr(resp, '\r');
     *p = 0;

     printf("Antenna Configuration: %s\n", resp);

     if( normal_mode ) {
           printf("Radio is in Normal mode!\n");
     } else
           printf("Radio is in Airplane mode!\n");
}

bool show_EC25_settings(){
char *p, *p1;
char *resp;

   if (RunAT("AT+QSIMDET?\r") ){
         p = &szResponse[2];
         p1 = strchr(p, '\r');
         *p1 = 0;
         printf("SIM hot-swap detection %s\n", p);
   } else {
       printf(szResponse);

   }

   // fails with ERROR if no SIM
   if (sim_id[0] == 0){
      printf("Sim not detected\n");
   } else {
       if (RunAT( "AT+CGDCONT?\r")){
            resp = trim_response(szResponse);
            resp++;
            p = strchr(resp, '\r');
            *p = 0;
            printf("%s\n\n",resp);
       } else {
           printf(szResponse);
       }
   }


    if ( RunAT("at+qindcfg=\"all\"\r")){
         p = &szResponse[2];
         p1 = strchr(p, '\r');
         *p1 = 0;
        printf("URC Configuration : %s\n", p);
   } else
      printf(szResponse);


}

bool show_4G_settings(){
char *resp;
char *p;

    printf(radio_description);
    if (RunAT( "AT^SCFG?\r"))
        printf(trim_response( (char *)szResponse));

    if(cell_type == CELL_PLS8)  {
        if ( RunAT( "AT^ssrvset=\"actsrvset\"\r")){
             if (strstr(szResponse,"ERROR")) {
                 printf(szResponse);
             } else {
                resp = trim_response((char *)szResponse);
                p = strstr(resp,"SET: ");
                p += 5;
                printf("\nThe Current Active Set = %s", p );
                if( RunAT( "AT^ssrvset=\"current\"\r") ){
                     printf("Active Set Values:\n");
                     printf( trim_response((char *)szResponse ));
                }
             }
        }
        if ( RunAT("AT+IPR=?\r") ){
             if (strstr(szResponse,"ERROR")) {
                  printf(szResponse);
             } else {
                 printf(trim_response(szResponse));
             }
        }
        if(  RunAT( "AT+IPR?\r") ){
             if (strstr(szResponse,"ERROR")) {
                  printf(szResponse);
             } else {
                 printf(trim_response(szResponse));
             }
        }

       RunAT( "AT+CGDCONT?\r");
       resp = trim_response(szResponse);
       resp++;
       p = strchr(resp, '\r');
       *p = 0;
       printf("%s\n\n",resp);
    }

    if( normal_mode ) {
           printf("Radio is in Normal mode!\n");
     } else
           printf("Radio is in Airplane mode!\n");
}

/* set radio to Normal mode */
void power_on_radio() {
   if ( !normal_mode)  {
          printf("\nTurning Cellular Power on, please wait...\n");
           normal_mode = set_to_normal_mode();
          if ( !normal_mode){
             printf("Failed to power on!\n");
             exit(1);
          }
    }
    printf("Radio powered on, in Normal mode, ready for use.\n");
}

/* ste radio to airplane mode */
void power_off_radio() {
    if ( normal_mode)  {
              printf("\nTurning Cellular Power off, please wait...\n");
               if( set_to_airplane_mode() ) normal_mode = false;
               if ( normal_mode){
                 printf("Failed to power off!\n");
                 exit(1);
              }
    }
    printf("Radio powered off, in Airplane mode.\n");
}



//TODO: add SIM info, monitor status Registation,  with sig strength, etc
char main_menu1 [] = "\n\n=========== 4G Configuration =============\n\
1: Turn Cellular Radio on (Normal mode)\n\
2: Turn Cellular Radio off (Airplane mode)\n\
3: Show current configuration\n\
=========== Utility Functions =============\n\
a: Send AT commands\n\
?: Display this menu\n\
q: Quit\n\n\
Enter Selection: ";
void main_4g() {
     is_4g = true;
     get_current_radio_config();
     if (cell_type > 4)
        show_EC25_settings();
     else
        show_4G_settings();
      while(1) {
                 printf(main_menu1);
                 commkey = getchar();
                 while (getchar() != '\n');
                 if ( commkey == 'q' || commkey == 'Q') {
                      // TODO - could power off or prompt user to power off
                      break;
                 }
              //   if ( commkey > '2' && !cell_power){
             //         printf("Selected feature requires the radio to be powered on!\n");
             //         continue;
             //    }
                 switch(commkey) {
                       case 'v':  // toggle verbose
                         if (verbose){
                            verbose = false;
                            printf("Verbose output disabled!\n");
                         } else {
                            verbose = true;
                            printf("Verbose output enabled!\n");
                         }
                         continue;
                         break;
                       case '1':  // Power on
                          power_on_radio();
                          break;
                       case '2':  // Power off
                           power_off_radio();
                           break;
                       case '3':
                            if (cell_type > 4)
                               show_EC25_settings();
                            else {
                               show_current_status();
                               show_4G_settings();
                            }
                           break;
                       case 'a':
                          AT_console();
                          break;
                }
      }
}


void check_status_monitor() {
FILE *fp;
char buff[200];

   fp = popen("ps | grep cell_status_monitor", "r");
   if (fp != NULL) {
         while ( fgets(buff, sizeof(buff), fp) ){
              if( strstr(buff, "grep ") != NULL) continue;
              if( strstr(buff, "cell_status_monitor") != NULL) {
                       printf("cell_status_monitor is running\ncalling config_start...\n") ;
                       system("config_start > /dev/null 2>&1");
                       sleep(1);
              }
         }
         pclose(fp);
   }

}


char main_menu [] = "\n=========== 3G (PXS8) Configuration =============\n\
1: Turn Cellular Radio on (Normal mode)\n\
2: Turn Cellular Radio off (Airplane mode)\n\
3: Show Current Status\n\
4: Configure provider\n\
5: Change Provider\n\
6: Monitor Network Registration\n\
=========== Utility Functions =============\n\
a: Send AT commands\n\
b: Read App port\n\
?: Display this menu\n\
q: Quit\n\n\
Enter Selection: ";

int main ( int argc, char *argv[] ){
int i;
FILE *fp;
char *resp;
struct stat stFileInfo;


   if ( stat("/tmp/cell_enabled", &stFileInfo) != 0)  {
       printf("Cellular power is not enabled, radio is inoperable!\n");
       exit(1);
   }
   if (argc > 1){
      if ( strcmp(argv[1], "-v") == 0 )
           verbose = true;
   }

   if ( !FileExists(CELL_TYPE_FILE) ){
       printf("Failed to find /temp/cell_type file!\n");
       exit(1);
   } else {
       fp = fopen(CELL_TYPE_FILE, "rb");
       if(fp != NULL) {
         cell_type = fgetc(fp);
         fclose(fp);
      }
  }

   // check if cint_status_monitor is running if so call config_start to kill it before
   // opening APP port
  check_status_monitor();

  // show contents of cell_id
  system("cat /tmp/cell_id");

  if (cell_type < 5 )
      strcpy(comm_port, CENTERION_APP_PORT);
  else
      strcpy(comm_port, QUECTEL_COMM_PORT);

   if ( !OpenCOM(comm_port, BAUD_115200 ) ){
           printf("Failed to open App port of Radio (/dev/ttyS2)!\n");
           exit(0);
   }

  if(  !wait_for_connection( 5 ) ) {
        printf("Failed to connect to Radio!");
        exit(0);
  }

   // check  for radio mode (Airplane or Normal)
  normal_mode =  in_normal_mode();
  if (normal_mode)
      printf("Radio is in Normal mode\n");
  else
      printf("Radio is in Airplane mode\n");

// TODO setup echo and cmee modes like in cell_power module

   if ( cell_type >  1) {
       // is PLS8 or PLS62 radio
       main_4g();
   } else {
       get_current_radio_config();
       show_PXS8_settings();
       while(1) {
                 printf(main_menu);
                 commkey = getchar();
                 while (getchar() != '\n');
                 if ( commkey == 'q' || commkey == 'Q') {
                     // TODO - could power off or prompt user to power off
                      break;
                 }
                 if (commkey =='a') {
                    AT_console();
                    continue;
                 }
                 if ( commkey > '2' && !normal_mode){
                      printf("Selected feature requires the radio to be powered on!\n");
                      continue;
                 }
                 switch(commkey) {
                       case 'v':  // toggle verbose
                         if (verbose){
                            verbose = false;
                            printf("Verbose output disabled!\n");
                         } else {
                            verbose = true;
                            printf("Verbose output enabled!\n");
                         }
                         continue;
                         break;
                     case '1':  // Power on
                          power_on_radio();
                          break;
                       case '2':  // Power off
                           power_off_radio();
                           break;
                       case '3':  // Show Current Status
                          //set_to_normal_mode();
                          get_current_radio_config();
                          show_current_status();
                          break;
                       case '4':  // Configure provider
                          if (strstr(current_account,"cdma2000") != NULL) {
                             if( strstr(current_carrier,"Verizon") != NULL)
                                configure_verizon();
                             else
                                configure_sprint();
                         }
                         else
                              configure_umts();
                         break;
                       case '5':  // Change Provider
                         if (strstr(current_account,"cdma2000") != NULL) {
                             if( strstr(current_carrier,"Verizon") != NULL)
                                 change_provider("Verizon", "Sprint","Umts");
                             else
                                 change_provider("Sprint", "Verizon", "Umts");
                         }
                         else
                              change_provider("Umts", "Verizon","Sprint");
                         break;
                       case '6':  // Monitor Network Registration
                          monitor_reg_status();
                          break;

                       case 'b':
                          printf("Reading Application port...\n");
                          szResponse[0] = 0;
                          if (ReadComPort(szResponse, 100, 5000) ){
                              if(verbose)
                                 hexdump((char *)"Response: ", szResponse, strlen(szResponse));
                              printf(resp);
                          }else
                             printf("No data returned!\n");
                          break;
                  }
     }
  }

  // exit with radio in its initial mode
  if ( item_exists( "/tmp/cell_power_on")) {
       if( !normal_mode ){
          printf("Setting to Normal mode before exit...\n");
          set_to_normal_mode();

       }
  } else if (normal_mode){
       printf("Setting to Airplane mode before exit...\n");
       set_to_airplane_mode() ;
  }

  CloseCOM();
  // system("/usr/sbin/http/cgi-bin/start_led_driver");

     return 0;
}