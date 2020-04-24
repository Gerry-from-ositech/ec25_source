#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <stdarg.h>
#include <algorithm>
#include <errno.h>
#include <ctype.h>
#include <iostream>
#include <stdio.h>
#include <string>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <sstream>
#include "utils.h"
#include "common.h"


using namespace std;

#define CELL_TYPE_FILE "/tmp/cell_type"

/**
 * Get the size of a file.
 * @return The filesize, or 0 if the file does not exist.
 */
size_t getFilesize(const char* filename) {
    struct stat st;
    if(stat(filename, &st) != 0) {
        return 0;
    }
    return st.st_size;
}

//Encode text parameter with HTML entity encodings for <>"&
string  HTMLencode(string text)  {

    int length = text.length();
    string out;
    for(int i = 0; i < length; i++) {
      char ch = text[i];
      switch(ch) {
      case '"':
        out.append("&quot;");
        break;
      case '<':
        out.append("&lt;");
        break;
      case '>':
        out.append("&gt;");
        break;
      case '&':
        out.append("&amp;");
        break;
      case '\x27':   // single quote
          out.append("&apos;");
          break;
      default:
        out.append(1,ch);
        break;
      }
    }
    return out;
}

//based on javascript encodeURIComponent()
//Replace all "'" characters with 5 char  "&#39;"
string& urlencode(string &c) {
    size_t lookHere = 0;
    size_t foundHere;
    // single quotes
    while((foundHere = c.find("'", lookHere)) != string::npos)  {
          c.replace(foundHere, 1, "&#39;");
          lookHere = foundHere + 5;
    }
    return c;
}
/*
It doesn't take the buffer size as an input because it is assumed that the buffer will be
large enough, this is safe because it is known that the length of the output will always
be <= that of the input, so either use the same buffer for the output or create one
that's at least the size of the input + 1 for the null terminator, e.g.:

char *output = malloc(strlen(input)+1);
urldecode(output, input);
printf("Decoded string: %s\n", output);
*/

void urldecode(char *dst, const char *src){
char a, b;
        while (*src) {
                if ((*src == '%') &&
                    ((a = src[1]) && (b = src[2])) &&
                    (isxdigit(a) && isxdigit(b))) {
                        if (a >= 'a')
                                a -= 'a'-'A';
                        if (a >= 'A')
                                a -= ('A' - 10);
                        else
                                a -= '0';
                        if (b >= 'a')
                                b -= 'a'-'A';
                        if (b >= 'A')
                                b -= ('A' - 10);
                        else
                                b -= '0';
                        *dst++ = 16*a+b;
                        src+=3;
                } else if (*src == '+') {
                        *dst++ = ' ';
                        src++;
                } else {
                        *dst++ = *src++;
                }
        }
        *dst++ = '\0';
}

/* looks for all '%' chars and assumes they are the start of URL entities like %26 and
   convert it to &. Each entity has %nn where nn are two hex chars.

   If a % is found without two hex xhars following then the incomming string is not considered
   to be URL encoded and the incomming string is returned unchanged.

   for example:

     Normally a URL encoded string has all blanks represented as %20 and all + represented as %2B
     input:   This is%20a%20test%20%20%20%20%20-%20a%26
     output:  This is a test     - a&

     input:   This is percent % and plus + %61%62%63%26
     output:  This is percent % and plus + %61%62%63%26
     No change because there is a % that does not have two hex digits following it

     Add the URL encoding after first %
     input:   This is percent %41 and plus + %61%62%63%26
     output:  This is percent A and plus   abc&

  also converts + to ' '
*/
string DecodeURL(string URL) {
    string buffer = "";
    int i;
    int len = URL.length();

    for (i = 0; i < len; i++) {
        int j = i ;
        char ch = URL.at(j);
        if (ch == '%'){
            char tmpstr[] = "0x0__";
            int chnum;
            tmpstr[3] = URL.at(j+1);
            tmpstr[4] = URL.at(j+2);
            if ( !isxdigit(tmpstr[3]) || !isxdigit(tmpstr[4])){
                 buffer = URL;
                 break;
            }
            chnum = strtol(tmpstr, NULL, 16);
            buffer += chnum;
            i += 2;
        }
        else
          if(ch == '+')
             buffer += ' ';
          else {
             buffer += ch;
          }
    }

    string bTemp = buffer.c_str();
    //bTemp.replace("+"," ");
    //replaceAll(bTemp, "+", " ");

  return bTemp;
}

/* currently we are hard coded to only escape '\' chars in C strings but
 * other chars can be added as needed
 * all '\' chars are replaced by %5C.
 * Escaped string passup to client for insertion on ther page such as
 * inserting in input text field should be processed with javascript
 * unescape(str)
*/
string EscapeStr(string inStr) {
int i;
char ch;
char escStr[] = "%5C";
string buffer = "";

  for (i = 0; i < inStr.length(); i++) {

        ch = inStr.at(i);
        if (ch == '\\'){
           buffer.append(escStr);
        } else
           buffer += ch;
  }

  return buffer;
}

/* doubles up backslash in strings in order to escape them rather than %5C
*  Unlike  EscapeStr() the javascript unescape() does not have to be used
*/
string SlashEscapeStr(string inStr) {
int i;
char ch;
string buffer = "";

  for (i = 0; i < inStr.length(); i++) {
     ch = inStr.at(i);
     buffer += ch;
     if (ch == '\\')
         buffer.append("\\");
     else if (ch == 37)
         buffer.append("%");
  }

  return buffer;
}

string SlashUnescapeStr(string inStr) {
int i;
char chr1, chr2;
string buffer = "";
int size = inStr.length();

  for (i = 0; ; ) {
     chr1 = inStr.at(i++);
     buffer += chr1;
     if (i == size) break;
     chr2 = inStr.at(i++);
     if (chr1 == '\\' && chr2 == '\\')
          continue;
     buffer += chr2;
     if (i == size) break;
  }

  return buffer;
}


/* the c equivalent of SlashEscapeStr()
  str should be less than 300 bytes
*/
char * c_escape(char *str) {
char *p = str;
int i = 0;
static char c_escape_buff[300];

   while(*p) {
        if (*p == '\\') {
            c_escape_buff[i++] = '\\';
        }
        c_escape_buff[i++] = *p;
        p++;
   }
   c_escape_buff[i] = '\0';
   return c_escape_buff;

}

/* the c equivalent of SlashUnescapeStr()
  str should be less than 300 bytes
*/
char * c_unescape(char *str) {
char *p = str;
int i = 0;
static char c_unescape_buff[300];

   while(*p) {
        c_unescape_buff[i++] = *p++;
        if (*p == '\0') break;
        if (*p != '\\')
            c_unescape_buff[i++] = *p;
        p++;
   }

   c_unescape_buff[i] = '\0';
   return c_unescape_buff;

}

/* converts a string to uppercase
 * str - In - string befor modification
 *     - Out - string after modification
 *
 * Return: pointer to modified string
 */

char *convertToUpper(char *str){
    int i = 0;
    int len = 0;
    len = strlen(str);

    for(i = 0; str[i]; i++)  {
       str[i] = toupper(str[i]);
    }
    return  str;
}

void RestartHTTPD() {
    FILE *fp;
    char line[130];
   char cmd[50];
    bool found;

    found = false;

    // We don't want to see errors
    //FILE *stream ;
    //stream = freopen("file.txt", "w", stderr);
    fp = popen("ps", "r");

    // find the PID for httpd so we can kill it
    while ( fgets( line, sizeof line, fp))  {
        if(strstr(line, "httpd") != NULL)  {
            found = true;
            line[5] = '\0';
            break;
        }
    }
    pclose(fp);

    if(found) {
        sprintf(cmd, "kill %s", line);
        system(cmd);
    }

    // now restart the httpd daemon
    system("/usr/sbin/httpd -h /usr/sbin/http -c /mnt/flash/config/conf/httpd.conf");
}

string& replaceAll(string& context, const string& from, const string& to) {
    size_t lookHere = 0;
    size_t foundHere;
    while((foundHere = context.find(from, lookHere)) != string::npos) {
          context.replace(foundHere, from.size(), to);
          lookHere = foundHere + to.size();
    }
    return context;
}


char* itoa(int val, int base) {

    static char buf[32] = {0};
    int i = 30;
    for(; val && i ; --i, val /= base)
        buf[i] = "0123456789abcdef"[val % base];
    return &buf[i+1];
}

/* Warning if number of args passed in do not match format specifiers we get segfault */
char * format_str( const char * format, ... ) {
static char buff[400];
  va_list args;
  va_start (args, format);

  if ( vsprintf (buff, format, args) < 0) return NULL;
  va_end (args);
  return buff;
}

bool FileExists(const char *filename) {
struct stat stFileInfo;
     if(stat(filename, &stFileInfo) == 0)
           return true;
     return( false);
}



unsigned int readHwOptions() {
FILE *fp;
unsigned int opts;

  fp = fopen(HWOPTION_FILE, "rb");
  if (fp == NULL) return 0;

  fread(&opts, sizeof(int), 1, fp);
  fclose(fp);

//printf("Got hw options = %u\n", opts);
  return opts;
}


/* returns -1 if could not read file, does not exist */
int ReadCurrentImageIndex() {
char buff[1000];
FILE *fp;
char *p;
int index = -1;

  fp = fopen("/tmp/tmp.ini", "rb");
  if(fp != NULL) {
    fread(buff, 1, sizeof(buff), fp);
    fclose(fp);
    p = strstr(buff, "gobiindex");
    if (p) {
        p += 10;
        *(p+1) = '\0';
        index = atoi(p);
    }
  }
  return index;
}


bool SupportsWiFi() {
bool ret = false;
unsigned int opts = readHwOptions();

   if (opts & OPT_WIFI)
        ret = true;
    return ret;
}

bool SupportsCellular()  {
// check for presnets of the Gobi driverin /dev/GobiQMI:qcqmi1
bool ret = false;
unsigned int opts = readHwOptions();

   if (opts & (OPT_CELLULAR | OPT_CELL_BOTTOM) )
       ret =  true;
   return ret;
}

bool CheckForAudio() {
bool ret = false;
unsigned int opts = readHwOptions();

   if (opts & OPT_AUDIO)
       ret = true;
   return ret;
}

bool CheckForAudio(char* szFileName){
  struct stat stFileInfo;
  bool blnReturn;
  int intStat;

  // Attempt to get the file attributes
  intStat = stat(szFileName,&stFileInfo);
  if(intStat == 0) {
    // We were able to get the file attributes
    // so the file obviously exists.
    blnReturn = true;
  } else {
    // We were not able to get the file attributes.
    // This may mean that we don't have permission to
    // access the folder which contains this file. If you
    // need to do that level of checking, lookup the
    // return values of stat which will give you
    // more details on why stat failed.
    blnReturn = false;
  }

  // if the file exists open it and read the first line
  // if it is NONE, return false, otherwise return true
    FILE *fp;
    char line[32];
    if(blnReturn)   {
        fp = fopen(szFileName, "r");
        fgets(line, sizeof line, fp);
        if(strstr(line, "NONE") != NULL)
            blnReturn = false;
        else
            blnReturn = true;
    }
    else
    blnReturn = false;

  return(blnReturn);
}

// -- Logger class ----


Logger::Logger(const char * moduleName){
    strcpy(m_name, moduleName);
    logging_enabled = FileExists(GUI_LOGGING_FILE);
};

Logger::~Logger() {}

void Logger::log(const char *data, ...) {
FILE *fp;
int i;
static char buff[400];
va_list args;

  if (logging_enabled){
      va_start (args, data);
      vsprintf (buff, data, args);
      va_end (args);
      fp = fopen("/tmp/log/app.log","ab");
      i = strlen(buff)-1;
      if (buff[i] == '\x0a')
          fprintf(fp,"%s:%s", m_name, buff);
         //fp.write(buff)
      else
          fprintf(fp,"%s:%s\r\n", m_name, buff);
      fclose(fp);
      system("sync");
  }
}

void Logger::clear() {
     remove("/tmp/log/app.log");
}

void Logger::enable( bool val){
    logging_enabled = val;
}

#define CONFIG_LOG_FILE "/tmp/log/util.log"
void LogMsg(const char *text) {
   FILE *fp;
   if ((fp = fopen(CONFIG_LOG_FILE,"ab")) == NULL)
        return;

   fprintf(fp, "%s\n", text);
   fclose(fp);
   system("sync");
}


void RemoveAppLog(void){
struct stat stFileInfo;
int intStat;
  // if file exists, remove it
  intStat = stat(CONFIG_LOG_FILE, &stFileInfo);
  if(intStat == 0)
     remove(CONFIG_LOG_FILE);
}



void RightTrimStr(char* strPtr){
    unsigned int i,len;

    if (strPtr == NULL) {
        return;
    }

    len = strlen(strPtr);
    //for(i=len-1; i>=0; i--) {
    for(i=len-1; i!=0; i--) {
        if (strPtr[i] != ' ') {
            break;
        }
    }

    strPtr[i+1] = '\0';
}

void LeftTrimStr(char * strPtr){
    unsigned int i,j,len;

    if (strPtr == NULL || strPtr == "") {
        return;
    }

    len = strlen(strPtr);
    for(i=0; i<len; i++) {
        if (strPtr[i] != ' ') {
            break;
        }
    }
    if (i == 0) {
        return;
    }

    for(j=0; i<len; i++,j++) {
       strPtr[j] = strPtr[i];
    }
    strPtr[j] = '\0';
}

void Strip(char *strPtr){
    RightTrimStr(strPtr);
    if (strlen(strPtr) > 0)
      LeftTrimStr(strPtr);
}

void Pause(char *msg) {
   printf("Pause: ");
   if (msg != NULL)
      printf("%s ", msg);
   printf("\n==== press Enter to continue >");
   getchar();
   printf("\n");
}

/* test if the specified process is running for up to a specified number of seconds
   return true if process is running
          false if not running
*/
bool ProcessRunningCheck(char *processName, int toSecs) {
FILE *fp;
int i;
char line[350];    // needs to be big enough for the longest line returned by ps
char cmd[100];

    sprintf(cmd, "ps | grep -v grep | grep %s", processName);
    for(i=0;i < toSecs;i++)  {
        fp = popen(cmd, "r");
        while(fgets( line, sizeof line, fp))  {
            if(strstr(line, processName) != NULL){
              pclose(fp);
              return true;
           }
        }
        pclose(fp);
        sleep(1);
    }
    return false;
}

/* Check if a process is running
 * Return true to indicate process is still running
 *        false to indicate process is not running
 */
//void WaitForProcessEnd(char *processName) {
bool ProcessEndCheck(char *processName, bool waitForCompletion) {

FILE *fp;
char line[350];    // needs to be big enough for the longest line returned by ps
bool found = false;

    for(;;found=false)  {
        fp = popen("ps", "r");
        while(fgets( line, sizeof line, fp))  {
            if(strstr(processName, line) != NULL){
             found = true;
             break;
           }
        }
        pclose(fp);
       if (waitForCompletion == false)
           return found;
       if(found == false) break;
       sleep(2);
    }
   return found;
}



//static char * configImageProvNames[] = {"Generic", "Verizon", "AT & T", "Sprint", "T-Mobile", "Vodafone", "Telefonica", "Telecom Italia", "Orange", "Unknown" };
static const char * configImageProvNames[] = {"GenericUMTS", "AT & T", "Telefonica", "Telecom Italia",  "T-Mobile", "Orange", "Vodafone", "Sprint", "Verizon", "Unknown" };

char * getCellProvName(unsigned int index) {
   return (char *)configImageProvNames[index];
}

char appName[100];
char *LoadAppName(void) {
FILE *fp;
char line[100];

  memset(line,0, sizeof(line));
  fp = popen("cat /etc/hostname", "r");
  fgets( line, sizeof line, fp);
  pclose(fp);
  strcpy(appName, line);
  return &appName[0];
}

char *get_project_name(){
   if (FileExists("/mnt/flash/audio/titanaudio.conf")){
      strcpy(appName, "Physio");
   }else if(FileExists("/usr/sbin/status_listener")){
      strcpy(appName,"Philips");
   }else {
      strcpy(appName, "BlueRover");
   }
   return appName;

}


/* The  br_mode field in the file /mnt/flash/config/conf/mode.conf is used as the flag
   to show the connection modes. If the value of br_mode field is YES, it is bridge/router mode.
   If the value of br_mode field is NO, it is access point mode.

   The file /tmp/mode.conf has the following similar format:
   br_mode=YES
   mirror=YES
*/

bool GetModeFlags(int *mode, int *mirror) {
FILE *fp;
char buff[50];
bool ret = false;
  *mode=1;
  *mirror=1;

  fp = fopen("/tmp/mode.conf", "r");
  if (fp != NULL) {
       while(fgets(buff, sizeof(buff), fp) != NULL){
          if(strstr(buff,"br_mode") != NULL){
            if(strstr(buff,"YES") != NULL)
              *mode=1;
            else
              *mode=0;
          }
          else if(strstr(buff,"mirror") != NULL){
            if(strstr(buff,"YES") != NULL)
              *mirror=1;
            else
              *mirror=0;
          }
       }
      fclose(fp);
      ret = true;
  }
  return ret;
}

bool GetBootModeFlags(int *mode, int *mirror) {
FILE *fp;
char buff[50];
bool ret = false;
  *mode=1;
  *mirror=1;

  fp = fopen("/mnt/flash/config/conf/mode_boot.conf", "r");
  if (fp != NULL) {
       while(fgets(buff, sizeof(buff), fp) != NULL){
          if(strstr(buff,"br_mode") != NULL){
            if(strstr(buff,"YES") != NULL)
              *mode=1;
            else
              *mode=0;
          }
          else if(strstr(buff,"mirror") != NULL){
            if(strstr(buff,"YES") != NULL)
              *mirror=1;
            else
              *mirror=0;
          }
       }
      fclose(fp);
      ret = true;
  }
  return ret;
}

void SetBootModeFlags(int mode, int mirror) {
FILE *fp;

  fp = fopen("/mnt/flash/config/conf/mode_boot.conf", "w");
  if ( mode > -1) {
     if (mode == 1)
       fwrite("br_mode=YES\n", 1, 12, fp);
     else
       fwrite("br_mode=NO\n", 1, 11, fp);
  }
  if (mirror > -1) {
     if (mirror == 1)
        fwrite("mirror=YES\n", 1, 11, fp);
     else
       fwrite("mirror=NO\n", 1, 10, fp);
  }
  fclose(fp);

}


void SetModeFlags(int mode, int mirror) {
FILE *fp;

  fp = fopen("/tmp/mode.conf", "w");
  if ( mode > -1) {
     if (mode == 1)
       fwrite("br_mode=YES\n", 1, 12, fp);
     else
       fwrite("br_mode=NO\n", 1, 11, fp);
  }
  if (mirror > -1) {
     if (mirror == 1)
        fwrite("mirror=YES\n", 1, 11, fp);
     else
       fwrite("mirror=NO\n", 1, 10, fp);
  }
  fclose(fp);

}

/* check if br_mode is the same in mode.conf and mode_boot.conf */
bool HasSysModeChanged() {
char buff[50];
int mode, mirror, currmode;
FILE *fp;

  GetModeFlags(&mode, &mirror);

  fp = fopen("/mnt/flash/config/conf/mode_boot.conf", "r");
  if (fp != NULL) {
    if( fgets(buff, sizeof(buff), fp) != NULL) {
      fclose(fp);
      if(strstr(buff,"YES") != NULL)
        currmode = 1;
      else
        currmode = 0;
      if (mode == currmode)
             return false;
    }
  }
  return true;

}


/* Execute a child process without waiting for it to finish ie: config_start */
void ExecShell(char *command) {
int child_pid;

   child_pid = fork();
   if (child_pid == 0) {
        //execl (command, command >/dev/null", NULL);
       execl("/usr/bin/sh", "sh", "-c", command,0);
       //printf("Failed calling %s with error %s\n", SYS_MON_PATH, strerror(errno));
        _exit (EXIT_FAILURE);
   }
//   else if(child_pid == -1)  //fork error\
//       ;

}

void debuglog1(char *msg) {

   FILE *fp;
   if ((fp = fopen("/mnt/flash/config/debuglog.txt","ab")) == NULL)
        return;

   fprintf(fp, "%s\n", msg);
   fclose(fp);

}

#define SWAP_CHARS(a, b) { u8 t = S[a];  S[a] = S[b];  S[b] = t; }

char ebuf[300];

void osi_rc4(u8 *data, size_t data_len, const u8 *key, size_t keylen) {
u32 i, j, k;
u8 S[256], *pos;
size_t kpos;

     for (i = 0; i < 256; i++)
             S[i] = i;
     j = kpos = 0;
     for (i = 0; i < 256; i++) {
         j = (j + S[i] + key[kpos]) & 0xff;
         kpos++;
         if (kpos >= keylen)
                 kpos = 0;
         SWAP_CHARS(i, j);
     }

     i = j = 0;
     for (k = 0; k < 0; k++) {
         i = (i + 1) & 0xff;
         j = (j + S[i]) & 0xff;
         SWAP_CHARS(i, j);
     }

     pos = data;
     for (k = 0; k < data_len; k++) {
         i = (i + 1) & 0xff;
         j = (j + S[i]) & 0xff;
         SWAP_CHARS(i, j);
         *pos++ ^= S[(S[i] + S[j]) & 0xff];
     }
}


char * encrypt_decrypt(char *buf, int buf_len, int op) {
        int i;
        osi_rc4((u8*)buf, buf_len, (u8*)"password", 8);
        for ( i = 0; i < buf_len; i ++) {
             ebuf[i] = buf[i];
         }
        ebuf[buf_len]='\0';
        return ebuf;
}

unsigned char * encrypt_decrypt_pw(char *buf, int buf_len, const char *pw) {
        int i;
        osi_rc4((u8*)buf, buf_len, (u8*)pw, strlen(pw));
        for ( i = 0; i < buf_len; i ++) {
             ebuf[i] = buf[i];
         }
        ebuf[buf_len]='\0';
        return (unsigned char *)ebuf;
}


/* Use item_name to retrive item data from config memory and return the value
 * as a string pointer
 read_config|grep option|awk -F'=' '{print $2}'
   read_config|grep option|awk -F'=0x' '{print $2}'

char config_item_buff[100];
char * get_configmem_item(char *item_name) {
FILE *fp;

char *p;
char cmd[100];

sprintf(cmd, "read_config | grep %s|awk -F'=' '{print $2}'", item_name);
//printf("popen %s\n", cmd);

fp = popen(cmd, "r");
if (fp != NULL){
   fgets(config_item_buff, sizeof(config_item_buff), fp);
   pclose(fp);
   p = strchr(config_item_buff, '\n');
   if (p == NULL){
      config_item_buff[0] = '\0';
      return config_item_buff;
   }
   *p = '\0';
   return config_item_buff;
} else
  printf("popen failed!\n");

}
*/

char config_item_buff[100];
char *read_config(const char *item, char *opt ){
FILE *fp;
char *p;
char cmd[100];

   config_item_buff[0] = '\0';
   sprintf(cmd, "read_config %s %s", opt, item);
   fp = popen(cmd, "r");
   if (fp != NULL){
      fgets(config_item_buff, sizeof(config_item_buff), fp);
      pclose(fp);
      p = strchr(config_item_buff, '\n');
      if (p == NULL){
         config_item_buff[0] = '\0';
         return config_item_buff;
      }
      *p = '\0';
      return config_item_buff;
   } else
     return NULL;    //printf("popen failed!\n");
}

bool update_uap_countrycode(char *code){
FILE *fp, *fp1;
char buff[300];

  if( item_exists("/mnt/flash/config/conf/uaputl0.conf")){
      system("mv /mnt/flash/config/conf/uaputl0.conf /tmp/tmpfile");
      system("sync");
      fp = fopen("/mnt/flash/config/conf/uaputl0.conf", "w");
      fp1 = fopen("/tmp/tmpfile", "r");
if(fp1 == NULL){
   system("sync");
   fp1 = fopen("/tmp/tmpfile", "r");
}
if(fp1 == NULL) return false;
      while ((fgets(buff, sizeof(buff), fp1))) {
           if ( strncmp(buff, "Channel=", 8) == 0){
              fwrite("Channel=0,1\n",1, 13, fp);
              continue;
           }else if (strncmp(buff, "11d_enable=", 11) == 0)  {
              fwrite("11d_enable=1\n",1, 13, fp);
              continue;
           } else if (strncmp(buff,"country=",8) == 0){
              sprintf(buff,"country=%s\n",code);
              fwrite(buff,1, strlen(buff), fp);
              continue;
          }
          fwrite(buff,1, strlen(buff), fp);
      }
      fclose(fp);
      fclose(fp1);
      system("sync");
      return true;
  }
  return false;
}


/* system configuration options supported by the Titan are stored as a bitmask
 * in  /dev/mtd0 as part of the system package
 * and a record as a bitmask (unsigned 32 bit integer) in binary file
 * /usr/sbin/http/hwoptions.
 *
 * Bit 0 - Audio supported
 * Bit 1 - Cellulur supported  1 = supported, 0 = not supported
 * Bit 2 - WiFi 1 = supported, 0 = not supported
 * Bit 3 - ----+
 * Bit 4 -     \
 * Bit 5 -     +-- Reserved for other options (Bluetooth, etc)
 * Bit 6 -     \
 * Bit 7 - ----+
 * Bit 9 ..31 - Software options,  Currently Unused
 *
 *  See util.h for latest hwoption bit flags and see
 * TitanII_ConfigurationMemory.doc Section 4 for full details
 */

unsigned int detect_system_options() {
FILE *fp;
char *opts;
unsigned int out, old, tmp;

      opts =  read_config(CFG_MEM_OPTIONS, "-n" ); //get_configmem_item("option");

      /* convert the byte array into a unsigned int that gets
       * written to binary file HWOPTION_FILE
       * This is used as a bitmask with the OPT_nnnn flags
       */
      out = (unsigned int)strtol(opts, NULL, 0);

      old = readHwOptions();
      if ((fp = fopen(HWOPTION_FILE, "wb")) == NULL)   {
           perror("fopen");
      }   else   {
            /*if ( (out & OPT_WIFI) == 0) {
                // no wifi so no AP
                out = out & 0xFFFFF0FF;
            }*/
            fwrite(&out, 1, 4, fp);
            fclose(fp);

     }
     return out;
}

void wait_millsec(unsigned long sleepMilsec) {
struct timespec t;

  t.tv_sec   = sleepMilsec / 1000;
  t.tv_nsec  = (sleepMilsec % 1000) * 1000000L;
  nanosleep(&t, NULL);
}

int item_exists( const char *item){
struct stat stFileInfo;
     return (stat(item, &stFileInfo) == 0);
}

std::vector<std::string> &split(const std::string &s, char delim, std::vector<std::string> &elems) {
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        elems.push_back(item);
    }
    return elems;
}

std::vector<std::string> split(const std::string &s, char delim) {
    std::vector<std::string> elems;
    split(s, delim, elems);
    return elems;
}

void hexdump(char *msg, void *ptr, int buflen, bool log) {
unsigned char *buf = (unsigned char*)ptr;
int i, j;
int width = 20;
FILE *fp;

    if (log) {
         if ((fp = fopen(CONFIG_LOG_FILE,"ab")) == NULL)
              log = false;
        else {
             fprintf(fp,"%s\n",msg);
             fprintf(fp,"------------ HexDump (%d bytes)-------------\n", buflen);
         }
    }

    if( !log) {
        printf("%s\n", msg);
        printf("------------ HexDump (%d bytes)-------------\n", buflen);
    }

    for (i=0; i<buflen; i+=width) {
        if(log)
            fprintf(fp,"%06d: ", i);
         else
            printf("%06d: ", i);
         for (j=0; j<width; j++)
            if (i+j < buflen)  {
                  if(log)
                       fprintf(fp,"%02x ", buf[i+j]);
                  else
                       printf("%02x ", buf[i+j]);
            } else   {
                 if(log)
                      fprintf(fp,"   ");
                 else
                  printf("   ");
             }
         if(log)
              fprintf(fp," ");
         else
             printf(" ");
         for (j=0; j<width; j++)
            if (i+j < buflen){
                 if(log)
                     fprintf(fp, "%c", isprint(buf[i+j]) ? buf[i+j] : '.');
                 else
                      printf("%c", isprint(buf[i+j]) ? buf[i+j] : '.');
            }
         if(log)
              fprintf(fp,"\n");
         else
              printf("\n");
    }
    if(log){
          fprintf(fp,"----------------------------------\n");
          fclose(fp);
    }else
         printf("----------------------------------\n");

}

int isAllZeros(char *pstr) {
    for(; *pstr != '\0'; pstr++)
         if (*pstr != '0') return 0;
    return 1;
}

int isOfflineUpdateEnabled(){
FILE *fp;
char buff[300];

   buff[0] = 0;
   fp = popen("ls -l /mnt/flash/titan-data/offline/*.usf", "r");
   fgets(buff, sizeof buff, fp);
   pclose(fp);
   if (strstr(buff,"No such"))
       return 0;
   return 1;

}

/* check if audio is to be supported by checking eeprom hwoption
 * if audio bit is set in hwoption then return TRue else return false
 * TODO: rename this function to express its new purpose
 */
bool cellHardwareAvailable(){
FILE *fp;
char *p;
char flags[100];
unsigned int opts;
int ret = 0;

   fp = popen("read_config -n hwoption", "r");
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

    if (opts & 0x01) return true;
    return false;

}

#define REG_COUNTRIES_FILE  "/usr/sbin/http/WifiCountries.jsn"

string lookup_country_domain(char *cc){
FILE *fp;
string sTemp;
char pat[10];
struct stat stFileInfo;
char *buffp;
char *p, *p1;

  sTemp = "FCC1";
  if (strcmp(cc,"US") == 0 || strcmp(cc, "CA") == 0){
        sTemp = "FCC1";
  } else if(stat(REG_COUNTRIES_FILE, &stFileInfo) == 0){
      // file exists, so read it all into memory
      // get file size, allocate memory, open file and read all of its data
      buffp = (char *) malloc(stFileInfo.st_size+1);
      memset(buffp, 0, stFileInfo.st_size+1);
      fp = fopen(REG_COUNTRIES_FILE,"rb");
      fread(buffp, 1, stFileInfo.st_size, fp );
      fclose(fp);
      sprintf(pat,"\"%s\",",cc);
      p = strstr(buffp, pat);
      p += 6;
      p1 = strchr(p, '\"');
      *p1 = 0;
      sTemp = p;
  }

  return sTemp;
}

/*
 * Case Sensitive Implementation of startsWith()
 * It checks if the string 'mainStr' starts with given string 'toMatch'
 */
bool startsWith(std::string mainStr, std::string toMatch) {
  // std::string::find returns 0 if toMatch is found at starting
  if(mainStr.find(toMatch) == 0)
    return true;
  else
    return false;
}

/*
 * Case Insensitive Implementation of startsWith()
 * It checks if the string 'mainStr' starts with given string 'toMatch'
 */
bool startsWithCaseInsensitive(std::string mainStr, std::string toMatch){
  // Convert mainStr to lower case
  std::transform(mainStr.begin(), mainStr.end(), mainStr.begin(), ::tolower);
  // Convert toMatch to lower case
  std::transform(toMatch.begin(), toMatch.end(), toMatch.begin(), ::tolower);

  if(mainStr.find(toMatch) == 0)
    return true;
  else
    return false;
}


enum sys_state cellular_available(){
FILE *fp;
char buff[200];
enum sys_state ret = SYS_READY;
string str;
bool found = false;

     fp = popen("lsusb", "r");
     if (fp != NULL) {
            while ( fgets(buff, sizeof(buff), fp) ) {
               if ( strstr(buff, ":0053") || strstr(buff,":0061") || strstr(buff, "05C6:0125") || strstr(buff, ":005b") || strstr(buff, ":00a0"))
                    found =  true;
            }
     }
     pclose(fp);

     if (found) {
        fp = popen("ls -1 /tmp/cell_*", "r");
        if (fp != NULL) {
            while ( fgets(buff, sizeof(buff), fp) )
              str.append(buff, strlen(buff)-1);
            pclose(fp);
            if ( str.find("cell_enabled")!=string::npos && str.find("cell_id")!=string::npos && str.find("cell_type")!=string::npos)
                ret = SYS_READY;
            else
                ret = CELL_MISSING_FILES;
        }
    } else
      ret = CELL_NOT_ENUMERATED;

    return ret;
}

enum sys_state  wait_for_cell_enabled(int toSec){
int x,s;
enum sys_state ret = SYS_READY;
  for (x=0; x < toSec; x++){
        ret = cellular_available();
        if (ret == CELL_NOT_ENUMERATED || ret == SYS_READY) break;
        else
           ret = CELL_MISSING_FILES;
   }
   return ret;
}

enum sys_state wait_for_end_of_bootup(int toSec){
int i,x, s;
unsigned int opts;
enum sys_state ret = NO_BOOTEND;
  for(i=0; i < toSec; i++){
     if( FileExists("/tmp/scriptend")){
         opts = (unsigned int)strtol(read_config(CFG_MEM_OPTIONS, "-n" ), NULL, 0);
         if (opts & (OPT_CELLULAR | OPT_CELL_BOTTOM) ){
             if ( FileExists("/tmp/cell_not_mounted")){
                 ret = CELL_NOT_MOUNTED;
                 break;
             }
             ret = wait_for_cell_enabled(toSec);
             break;
         } else {
             ret = SYS_READY; // OK status, cellular not supported (Wifi only)
             break;
         }
     }
  }
  return ret;

}

