/* This module is called via a redirect from index.html as part of configurator startup.
 * Code added for Philips checks /etc/hostname  for the presense of "Philips" in the string
 * If not Philips then redeirect to updatex.html  otherwise ontinue with module execution
 *
 * Module deletes the current tmp.ini if it exists  and rebuilds it from the titanlib.conf if it exists
 * If titanlib.conf fors not exists the tmp.ini is created from Default.ini (DEFAULT=YES)
 *
 * After creating tmp.ini this module does a redirect to GWY_Welcome.cpp
 *
 * NOTE: The busybox httpd daemon expects it's CGI script files to be in the subdirectory cgi-bin under main web directory ../http/cgi-bin).
 * The CGI script files must also have permission to be executed (min mode 700).
 * SERVER_PROTOCOL=HTTP/1.0
 * SERVER_PORT=80
 */
#include <string>
#include <string.h>
#include <algorithm>
#include <netinet/in.h>
#include <fstream>
#include <cctype>
#include <sys/stat.h>
#include <debuglog.h>
#include "../../../util/inifile.h"
#include "../../../util/utils.h"
#include "../../../access_point/apconfig.h"

using namespace std;

#define CELL_TYPE_FILE "/tmp/cell_type"

extern string SafteyMapIn(string);

CIniFile Def, Tmp, Prev;
string debugStr;
unsigned int sys_options;

/* The Wifi bands and their frequency ranges depend on the Wifi chip and the
 * type of antenna in use.
 * 0  = 2.4 Ghz
 * 1  = 5Ghz
 * 2  = Both with combo antenna
 */
unsigned int band_options = 0;  // 0 = 2.4Ghz, 1 = 5Ghz, 2 = Both

/* number_of_aps supported
 * For T3 there could be none, 1 or 2 APs depending on hwoption settings
 * 0 = No APs supported, GUI will not provide Config Page
 * 1 = one AP is supported could be 2.4 or 5Ghz
 * 2 = two APs are supported, GUI insures both use same Band and channel! TBD
 *
 * This value is calculated in this module and preserved in tmp.ini for use by other modules
 */
unsigned int num_of_aps = 0;

/* simultaneous_client indicates if the T3 can have AP + client running at the same time
 * 0 - No
 * 1 = Yes
 * This value is calculated in this module and preserved in tmp.ini for use by other modules
 */
unsigned int simultaneous_client = 0;

char titan_sn[20];
char debugbuff[200];

Logger  applog("FT_Im");

/////////////////////////////////////////////////////////////////////////////
void set_wifi_ap_aptions(unsigned int sys_options) {
char buff[10];

   if ( sys_options &  OPT_WIFI){
          if ( sys_options &  OPT_ALLOW_TWO_APS){
              // assumes the 8898.def file is present
              num_of_aps = 2;
          } else
            num_of_aps = 1;  // Philips curently will have only 1 AP

           // byte 1 of hwoptions bits 0 and 1
           // 00 - 2.4Ghz           band_options = 0
           // 01 - Invalid (2.4Ghz disabled and 5 Ghz not enabled)
           // 10 - 2.4Ghz and 5Ghz  band_options = 2
           // 11 - 5Ghz only        band_options = 1

           if ( (sys_options & 0x00000300) == 0)
             band_options = 0;
           else if ((sys_options & OPT_DISABLE_24) == 0 && sys_options & OPT_ENABLE_5)
              band_options = 2;
           else
              band_options = 1;

          if (sys_options & OPT_CLIENT_AND_AP)
             simultaneous_client = 1;

         // add section options to tmp.ini
         sprintf(buff,"%d", band_options );
         Tmp.WriteString("TEMP","wifi_band_options", buff);
         sprintf(buff,"%d", num_of_aps );
         Tmp.WriteString("TEMP","num_of_aps", buff);
         if (simultaneous_client == 1)
              Tmp.WriteString("TEMP","simultaneous_client", "YES");
         else
             Tmp.WriteString("TEMP","simultaneous_client", "NO");
   }
}

bool ValidConfFile(const char *filename) {
bool ret = false;
  // check if the uaputl.conf file exists and if it is valid (at least 570 bytes long)
        struct stat stFileInfo;
        if( stat(filename, &stFileInfo) == 0) {
             // We were able to get the file attributes, so the file exists.
             if (stFileInfo.st_size > 570)
                 ret = true;   // file exists and has OK size
        }
        return ret;
}

void SetApDefaults(char *section1, char *section2, char *band) {    // depricated
char line[100];
string default_ssid;
const char *pch;

    default_ssid = Def.ReadString(section2,"SSID", "Wireless Link");
    // copy defaults from Defaults.ini to tmp.ini
    Tmp.WriteString(section1,"SSID", default_ssid.c_str());
    Tmp.WriteString(section1,"BroadcastSSID", Def.ReadString(section2,"BroadcastSSID", "1"));
    Tmp.WriteString(section1,"Protocol", Def.ReadString(section2,"Protocol", "WPA2-PSK"));
    Tmp.WriteString(section1,"PairwiseCipher", Def.ReadString(section2,"PairwiseCipher", "TKIP"));

    if (band != NULL){
       Tmp.WriteString(section1,"Band", band);
       Tmp.WriteString(section1,"Channel", "auto");

       // get rest of params for T3 Acess Point
       if ( strcmp(band,"0") == 0)
          Tmp.WriteString(section1,"network_mode", Def.ReadString(section2,"network_mode", "802.11b"));
       else
          Tmp.WriteString(section1,"network_mode", "802.11a");

       if ( sys_options &  OPT_ENABLE_REG_DOMAINS){
           if ( FileExists((char *)&"/tmp/no_country"))
               Tmp.WriteString(section1,"11d_enable", "0");
           else {
               Tmp.WriteString(section1,"11d_enable", "1");
       //        pch = read_config(CFG_MEM_COUNTRY_CODE, "-n" );
       //        Tmp.WriteString(section1,"country", pch);
               Tmp.WriteString(section1,"Channel", Def.ReadString(section2,"Channel", "auto"));
           }
       } else
          Tmp.WriteString(section1,"11d_enable", Def.ReadString(section2,"11d_enable", "0"));
       Tmp.WriteString(section1,"domain", Def.ReadString(section2,"domain", "FCC1"));
    } else
       Tmp.WriteString(section1,"Channel", Def.ReadString(section2,"Channel", "auto"));


    // use serial number to set default hotspot setting
    if (titan_sn[0]  != '\0') {
         if (default_ssid[0] != '\0') {
          /* Backward compatability with T2, the SSID,PSK and Key_0 fields keep their quotes when in tmp.ini */
              // default_ssid[default_ssid.length()] =  ' ';
               default_ssid.append("-");
          default_ssid.append(titan_sn);
         // Tmp.WriteString(section1, "SSID", default_ssid.c_str());
          Tmp.WriteString(section1, "SSID", ""); // force user to snter SSID
          sprintf(line, "accessmrx");    // default PSK key is "accessmrx" .
          Tmp.WriteString(section1,"PSK", line);
       } else {
       //    default_ssid += ' ';
       //    default_ssid += titan_sn;
       //    Tmp.WriteString(section1, "SSID", default_ssid.c_str());
       //    sprintf(line, "accessmrx");    // default PSK key is "accessmrx" .
       //    Tmp.WriteString(section1,"PSK", line);
           Tmp.WriteString(section1,"PSK", "");
       }
    }  else {
       Tmp.WriteString(section1,"PSK", "");
    }

}

// load current values for AccessPoint(s)  or set defaults into tmp.ini
// assume that if the *.conf files do not exists in /etc/titanlib that the system
// is running for the first time after flashing
//
// mode = 1 = br_mode
//      = 0 = AP mode
void InitalizeAccessPoints(int mode) {    // depricated
int i,wifi_type;
char line[100];
string sTemp;
bool ap1_disabled = false;
bool ap2_disabled = true;  // Philips does not support second AP
applog.log( "Initialize Access Point(s)...");

     // new Titan 3 Wifi chip can have two AccessPoints but Philips is restricted to one
     if ( Prev.OpenIniFile(AP_PREV_INI) ){
applog.log("Found ap.prev file and opened it");
        sTemp = Prev.ReadString("AccessPoint1","SSID", "NONE");
        if (sTemp != "NONE")
           ap1_disabled = true;
        sTemp = Prev.ReadString("AccessPoint2","SSID", "NONE");
        if (sTemp != "NONE")
           ap2_disabled = true;
     }
     Ap8897 ap;
     Tmp.WriteString("AccessPoint1","DIRTY", "NO");
     if(ap1_disabled) {
        applog.log("Access point 1 is disabled, load from ap.prev");
        copy_ini_section("AccessPoint1", Prev, Tmp);
     } else {
        if (!ValidConfFile("/mnt/flash/config/conf/uaputl0.conf.def")){
            // first time (after flashing the system) set defaults
            system("cp /usr/sbin/http/uap8897.def /mnt/flash/config/conf/uaputl0.conf.def");
            if(band_options == 1)
               SetApDefaults("AccessPoint1", "uaputl0",  "1");
            else
               SetApDefaults("AccessPoint1", "uaputl0",  "0");
  applog.log("uaputl0.conf.def not found or invalid, create from uap8897.def");
            ap.Write(Tmp, "AccessPoint1", "/mnt/flash/config/conf/uaputl0.conf.def" );
//            Tmp.WriteString("TEMP","uap_defaults", "YES");
//            Tmp.WriteString("AccessPoint1","DIRTY", "YES");

        } else if ( FileExists((char *)&"/mnt/flash/config/conf/uaputl0.conf")) {
          applog.log("uaputl0.conf found, load into tmp.ini");
            ap.Read(Tmp, "AccessPoint1", "/mnt/flash/config/conf/uaputl0.conf");
        }else {
            applog.log("Previous uaputl0.conf.def found, load into tmp.ini");
            ap.Read(Tmp, "AccessPoint1", "/mnt/flash/config/conf/uaputl0.conf.def" );
//            Tmp.WriteString("TEMP","uap_defaults", "YES");
//            if (mode == 1)
//                Tmp.WriteString("AccessPoint1","DIRTY", "YES");
        }

     }

}



void  LoadBridgeDefaults(){

  Tmp.WriteString("TEMP","DIRTY","NO");
  Tmp.WriteString("TEMP","DEFAULT",  Def.ReadString("TEMP","DEFAULT", "YES"));
  Tmp.WriteString("TEMP","IniVer",  Def.ReadString("TEMP","IniVer", "5.0.0.0"));
  Tmp.WriteString("TEMP","NumCons",  Def.ReadString("TEMP","NumCons", "0"));
  Tmp.WriteString("TEMP","cellular_good_threshold",  Def.ReadString("TEMP","cellular_good_threshold", "70"));
  Tmp.WriteString("TEMP","cellular_fair_threshold",  Def.ReadString("TEMP","cellular_fair_threshold", "80"));
  Tmp.WriteString("TEMP","cellular_poor_threshold",  Def.ReadString("TEMP","cellular_poor_threshold", "90"));
  Tmp.WriteString("TEMP","wifi_good_threshold",  Def.ReadString("TEMP","wifi_good_threshold", "70"));
  Tmp.WriteString("TEMP","wifi_fair_threshold",  Def.ReadString("TEMP","wifi_fair_threshold", "80"));
  Tmp.WriteString("TEMP","wifi_poor_threshold",  Def.ReadString("TEMP","wifi_poor_threshold", "90"));
  Tmp.WriteString("TEMP","wifi_scan_mode",  Def.ReadString("TEMP","wifi_scan_mode", "AP_ONLY"));
//  Tmp.WriteString("TEMP","SOUND",  Def.ReadString("TEMP","SOUND", "OFF"));
  Tmp.WriteString("TEMP","cellmtu",  Def.ReadString("TEMP","cellmtu", "1428"));
  Tmp.WriteString("TEMP","cell_reg_timeout",  Def.ReadString("TEMP","cell_reg_timeout", "240"));
  Tmp.WriteString("TEMP","cell_act_timeout",  Def.ReadString("TEMP","cell_act_timeout", "240"));
  Tmp.WriteString("TEMP", "public_safety_enabled", "NO");

}

void  LoadApDefaults(){
string cc;
   // load AccessPoint1 section defaults -
  Tmp.WriteString("AccessPoint1", "SSID",  Def.ReadString("AccessPoint1","SSID",""));
  Tmp.WriteString("AccessPoint1", "Channel",  Def.ReadString("AccessPoint1","Channel","auto"));
  Tmp.WriteString("AccessPoint1", "Band",  Def.ReadString("AccessPoint1","Band","0"));
  Tmp.WriteString("AccessPoint1", "BroadcastSSID",  Def.ReadString("AccessPoint1","BroadcastSSID","1"));
  Tmp.WriteString("AccessPoint1", "Protocol",  Def.ReadString("AccessPoint1","Protocol","WPA2-PSK"));
  Tmp.WriteString("AccessPoint1", "PairwiseCipher",  Def.ReadString("AccessPoint1","PairwiseCipher","AES"));
  Tmp.WriteString("AccessPoint1", "network_mode",  Def.ReadString("AccessPoint1","network_mode","802.11bgn"));
  Tmp.WriteString("AccessPoint1", "11d_enable", Def.ReadString("AccessPoint1","11d_enable","0"));
  Tmp.WriteString("AccessPoint1", "DIRTY",  Def.ReadString("AccessPoint1","DIRTY","NO"));


  //Tmp.WriteString("AccessPoint1", "PSK",  "accessmrx");

  cc =  Tmp.ReadString("TEMP","countrycode","NONE");
  if ( cc == "NONE"){
     Tmp.WriteString("AccessPoint1", "country",  Def.ReadString("AccessPoint1","country","US"));
     Tmp.WriteString("AccessPoint1", "domain",  Def.ReadString("AccessPoint1","domain","FCC1"));
  }  else {
       Tmp.WriteString("AccessPoint1", "country", (char *)cc.c_str());
       cc = lookup_country_domain((char *)cc.c_str());
       Tmp.WriteString("AccessPoint1", "domain", (char *)cc.c_str());
  }

}

void LoadAccessPoint() {
   Ap8897 ap;
   if ( FileExists((char *)&"/mnt/flash/config/conf/uaputl0.conf") ) {
        applog.log("uaputl0.conf found, load into tmp.ini");
        ap.Read(Tmp, "AccessPoint1", "/mnt/flash/config/conf/uaputl0.conf");
        Tmp.WriteString("AccessPoint1","DIRTY", "NO");
   }
//    else {
//        LoadApDefaults();
//   }
}

char debug_buff[200];

int main(int argc, char *argv[] ) {

int i, j, NumCon;
string temp, stmp;
string Val;
string  sResult;
CCHR  *pCChar;
CCHR *tmp;
CIniFile TmpLib;
FILE *fp;
size_t found;
char line[255];
int mode, mirror;

bool defaults_enabled = false;
//working files that are created by the configurator and need to persist accros power-cycles
//should be placed in mtd10 /mnt/flash/config/conf


  // Don't allow stderr output to web browser
  FILE *stream ;
  stream = freopen("/tmp/file.txt", "w", stderr);

//Logger  applog("FT_Im");

//   load_config();
   // rebuild file hwoption each time in case write_config was used to change status memory
   sys_options = detect_system_options();

  // tmp  =  get_configmem_item("serial");
   tmp = read_config(CFG_MEM_SERIAL, "-n");
   strcpy(titan_sn, tmp);

   sprintf(debugbuff, "detected system options = %X", sys_options );
   applog.log(debugbuff);

  // Delete the existing files
  remove(TEMP_INI);          // delete the current temp file so we start off clean
  remove("/tmp/tboot");      // delete titan boot flag file
  setenv("PATH", "/ositech:/mnt/flash/titan-appl/bin:/bin:/sbin:/usr/bin:/usr/sbin:/usr/bin/X11:/usr/local/bin", 1);

  Def.OpenIniFile("./Default.ini");
  Tmp.OpenIniFile(TEMP_INI);

  set_wifi_ap_aptions(sys_options);

   if ( sys_options & OPT_ENABLE_REG_DOMAINS){
      applog.log("Found Reglatory Domains flag enabled" );
      tmp = read_config(CFG_MEM_COUNTRY_CODE, "-n" );
      if (tmp[0] == 0 || tmp == NULL || strstr(tmp ,"ERROR") != NULL)
        system("touch /tmp/no_country");
      else{
         system("rm /tmp/no_country > /dev/null 2>&1");
         applog.log("Found countrycode in eeprom config memory");
         Tmp.WriteString("TEMP", "countrycode", tmp);
      }
  }


  // if current configuration file titanlib.conf exist
  // Use its contents to rebuild tmp.ini
  if(FileExists((char *)&"/mnt/flash/config/conf/titanlib.conf"))  {
applog.log("Found existing titanlib.conf file" );
    TmpLib.OpenIniFile("/mnt/flash/config/conf/titanlib.conf");
    // This portion reads all the settings from titanlib.conf file and puts them into the tmp.ini file
    string b;
    //string name, val;
    i = NumCon = 0;


    Tmp.WriteString("TEMP","public_safety_enabled",TmpLib.ReadString("General","public_safety_enabled", "NO"));

    // copy fields from titanlib.conf to tmp.ini
    Tmp.WriteString("TEMP","cellular_good_threshold",TmpLib.ReadString("General","cellular_good_threshold", "70"));
    Tmp.WriteString("TEMP","cellular_fair_threshold",TmpLib.ReadString("General","cellular_fair_threshold", "80"));
    Tmp.WriteString("TEMP","cellular_poor_threshold",TmpLib.ReadString("General","cellular_poor_threshold", "90"));
    Tmp.WriteString("TEMP","wifi_good_threshold",TmpLib.ReadString("General","wifi_good_threshold", "70"));
    Tmp.WriteString("TEMP","wifi_fair_threshold",TmpLib.ReadString("General","wifi_fair_threshold", "80"));
    Tmp.WriteString("TEMP","wifi_poor_threshold", TmpLib.ReadString("General","wifi_poor_threshold", "90"));
    Tmp.WriteString("TEMP","wifi_scan_mode", TmpLib.ReadString("General","wifi_scan_mode", "AP_ONLY"));
//            Tmp.WriteString("TEMP","wifimtu",  TmpLib.ReadString("TEMP","wifimtu", "1500"));
    Tmp.WriteString("TEMP","cellmtu",  TmpLib.ReadString("General","cellmtu", "1430"));
    Tmp.WriteString("TEMP","cell_reg_timeout",  TmpLib.ReadString("General","cell_reg_timeout", "240"));
    Tmp.WriteString("TEMP","cell_act_timeout",  TmpLib.ReadString("General","cell_act_timeout", "240"));
    // need to get the ini version value from Default.ini
    Tmp.WriteString("TEMP","IniVer", Def.ReadString("TEMP","IniVer", "5.0.0.0"));

    // Write current setting to the inifile
    Tmp.WriteIniFile(TEMP_INI);

 applog.log("\nStart counting connections...");

   fp = popen( "cat /mnt/flash/config/conf/titanlib.conf", "r");
   if (fp != NULL) {
      while ( fgets(line, sizeof(line), fp) ){
         if( line[0] == '[' && strchr(line,']')){
            if( !strstr(line,"[General]") && !strstr(line,"[Priority]")) {
                   NumCon++;
                   applog.log("Found connection name:");
                   applog.log(line);
             }
        }
      }
      pclose(fp);
   }
   else
       printf("Failed to access titanlib.conf!\n");


/*
    // Now we need to count the number of connections that are in the titanlib.conf file
    fstream inputFile("/mnt/flash/config/conf/titanlib.conf");
    i = 0;
    while (!inputFile.eof())  {
          inputFile >> b;
          applog.log((char *)b.c_str());
  //        if( b.at(0) == '[' && b.find("]") != -1 )  {
         if( b.find("[") == 0 && b.find("]") != -1 )  {
                if( b.find("[General]") == -1 && b.find("[Priority]") == -1 ) {
                     NumCon++;
          applog.log("Found connection name:");
          applog.log((char *)b.c_str());
                }
         }
    }
    inputFile.close();

*/
applog.log("End countining connections\n");
    if ( NumCon > 0 ) {
        // Now we have the number of connections, open the file again and make an array of
        // the connection names
        char connections[NumCon][64];
        fp = popen( "cat /mnt/flash/config/conf/titanlib.conf", "r");
        if (fp != NULL) {
           while ( fgets(line, sizeof(line), fp) ){
              if( line[0] == '[' && strchr(line,']')){
                 if( !strstr(line,"[General]") && !strstr(line,"[Priority]")) {
 sprintf(debugbuff,"Connection section line: %s", line); applog.log(debugbuff);

                      line[strlen(line)-2] = '\0';
 sprintf(debugbuff,"After truncating off last ]: %s", line); applog.log(debugbuff);
                      strcpy(connections[i], &line[1]);
  sprintf(debugbuff,"Final name for connetion %d = %s", i+1, connections[i]); applog.log(debugbuff);
                      i++;
                  }
             }
           }
           pclose(fp);
        }
        else
            printf("Failed to access titanlib.conf!\n");

/*
        inputFile.open("/mnt/flash/config/conf/titanlib.conf");
        while (!inputFile.eof())    {
             inputFile >> b;
             if(b.find("[") != -1 && b.find("]") != -1)  {
                 if(b.find("[General]") == -1 && b.find("[Priority]") == -1)  {
                      b = b.substr(1, b.length() - 2);
                      strcpy(connections[i], b.c_str());
                      i++;
                 }
             }
        }
        inputFile.close();
*/
        // Now that we have the connection names stored,
        // go through each one writing to the tmp.ini file
        for (j = 0; j < NumCon; j++)    {
              tmp = connections[j];       // Get a section name
               // Get all possible stored wireless values
              if ( (pCChar = TmpLib.ReadString(tmp,"acceptable_threshold", "")) != "")
                    Tmp.WriteString(tmp,"AccThresh", pCChar);
              if ( (pCChar = TmpLib.ReadString(tmp,"type", "")) != "") {
                    Tmp.WriteString(tmp,"SelType", pCChar);
                    if (strcmp(pCChar, "CELLULAR") == 0) {
                         Tmp.WriteString("TEMP", "gobiindex", TmpLib.ReadString(tmp,"gobiindex","-1"));
                   }
             }
            if ( (pCChar = TmpLib.ReadString(tmp,"auth", "")) != "")
                   Tmp.WriteString(tmp,"auth", pCChar);
            if ( (pCChar = TmpLib.ReadString(tmp,"ssid_name", "")) != ""){
                  Tmp.WriteString(tmp,"SSID", pCChar);
                  sprintf(debug_buff,"In FT_Im ssid = %s", pCChar);
                  applog.log(debug_buff);
            }
            if ( (pCChar = TmpLib.ReadString(tmp,"ssid_visible", "")) != "")
                      Tmp.WriteString(tmp,"SSID_Vis", pCChar);
            if ( (pCChar = TmpLib.ReadString(tmp,"authentication", "")) != "")
                        Tmp.WriteString(tmp,"AuthType", pCChar);
            if ( (pCChar = TmpLib.ReadString(tmp,"key", "")) != "")
                       Tmp.WriteString(tmp,"Key", pCChar);
            temp =  TmpLib.ReadString(tmp,"apn_string", "NONE");
            if ( temp != "NONE") {
               /* strip off etra data to get raw APN string */
               if( temp.compare(0,10,"+cgdcont=3") == 0){
                     found = temp.find("\",\"");
                     found += 3;
                     stmp = temp.substr(found);
                     stmp.erase(stmp.end()-1, stmp.end());
                     Tmp.WriteString(tmp,"APN", stmp.c_str());
               } else
                   Tmp.WriteString(tmp, "APN", temp.c_str());
            }
            // process public Safety
            temp = TmpLib.ReadString(tmp,"DiffServ", "NONE");

            if(temp != "NONE"){
               stmp =  SafteyMapIn(temp);
               if (stmp == "Custom"){
                  stmp = temp.substr(2, string::npos);
               }
               Tmp.WriteString(tmp,"DiffServ", stmp.c_str());
            }

             temp = TmpLib.ReadString(tmp,"bandwidth_limit", "0");
             if (temp == "0")
                  Tmp.WriteString(tmp,"bandwidth_limit", "None");
              else {
                   Tmp.WriteString(tmp,"bandwidth_limit","2 Mbps");
              }

            if ( (pCChar = TmpLib.ReadString(tmp,"username", "")) != "")
                     Tmp.WriteString(tmp,"CellUser", pCChar);
            if ( (pCChar = TmpLib.ReadString(tmp,"password", "")) != "")
                     Tmp.WriteString(tmp,"CellPass", pCChar);
            if ( (pCChar = TmpLib.ReadString(tmp,"eap_method", "")) != "")
                       Tmp.WriteString(tmp,"EAPMethod", pCChar);
            if ( (pCChar = TmpLib.ReadString(tmp,"encryption", "")) != "")
                       Tmp.WriteString(tmp,"Enc", pCChar);
            if ( (pCChar = TmpLib.ReadString(tmp, "backhaul", "")) != "")
                    Tmp.WriteString(tmp, "backhaul", pCChar);
            if ( (pCChar = TmpLib.ReadString(tmp, "largeupload", "")) != "")
                     Tmp.WriteString(tmp, "largeupload", pCChar);
            if ( (pCChar = TmpLib.ReadString(tmp, "broadcastrelay", "")) != "")
                Tmp.WriteString(tmp, "broadcastrelay", pCChar);
            if ( (pCChar = TmpLib.ReadString(tmp, "multicastrelay", "")) != "")
               Tmp.WriteString(tmp, "multicastrelay", pCChar);
            if ( (pCChar = TmpLib.ReadString(tmp, "dial_string", "")) != "")
                     Tmp.WriteString(tmp, "dial_string", pCChar);
            if ( (pCChar = TmpLib.ReadString(tmp, "additional_init_string", "")) != "")
                     Tmp.WriteString(tmp, "additional_init_string", pCChar);
            if ( (pCChar = TmpLib.ReadString(tmp, "band", "")) != "")
                     Tmp.WriteString(tmp, "band", pCChar);

            if ( (pCChar = TmpLib.ReadString(tmp, "gobiindex", "NONE")) != "NONE")
                   Tmp.WriteString(tmp, "gobiindex", pCChar);
     }
 } else {
       // no connections defined in titanlib.conf, check for AP configurations
       if( !FileExists((char *)&"/mnt/flash/config/conf/uaputl0.conf") && !FileExists((char *)&"/mnt/flash/config/conf/uaputl1.conf")){
        applog.log("No config files found, set DEFAULT = YES" );
            Tmp.WriteString("TEMP", "DEFAULT", "YES");
       } else {
          applog.log("Found uaputl?.conf file(s)" );
           Tmp.WriteString("TEMP", "DEFAULT", "NO");
       }
 }


    Tmp.WriteInt("TEMP", "NumCons" ,NumCon); // This value is saved so as to not need to count the
                                             // number of wireless sections again (like above)
    TmpLib.CloseIniFile();
    // Now create the priority section for tmp.ini
    string line;
    string csTemp, csTempUpper;
    BOOL bPrioritySection;

    // open input file
    ifstream in("/mnt/flash/config/conf/titanlib.conf");

    bPrioritySection = FALSE;
    // We're done with NumCon so use it
    NumCon = 1;
    while( getline(in,line) )   {// get a line up to \n or EOF, the \n is discarded
           if (line.length() == 0) continue;  // skip blank lines
           csTemp = line.c_str();
           csTempUpper = csTemp;
           std::transform(csTempUpper.begin(), csTempUpper.end(), csTempUpper.begin(), ::toupper);
           if(!bPrioritySection)   {
                if(csTempUpper == "[PRIORITY]")
                    bPrioritySection = TRUE;
           }   else   {
                // If we entered a new section (i.e. not PRIORITY)
                if(csTemp.find("[", 0) != -1)
                  break;
                // If it was not a blank line
                if(csTemp.c_str() != "") {
//sprintf(debug_buff,"%s - %d", csTemp.c_str(),  NumCon);
//applog.log( debug_buff);
                         Tmp.WriteString("PRIORITY", itoa(NumCon++, 10), csTemp.c_str());
                  }
         }
    }
    in.close();

    // Write changes to tmp.ini
    Tmp.WriteString("TEMP", "DIRTY", "NO");
   }  else  {// load defaults
        defaults_enabled = true;
        LoadBridgeDefaults();
   //     LoadApDefaults();
        applog.log("Loaded Bridge defaults from Default.ini" );

   }



// check for existence of cell_type file, if it doesn't exist ,default to PXS8
// however if the file does not exist the radio probably is not power enabled and will
// be inoperable
   if ( !FileExists(CELL_TYPE_FILE) ){
       applog.log("cell_type file missing, default to PSX8. Radio is disabled!");
       Tmp.WriteString("TEMP", "cell_type", "PXS8");
       debuglog(LOG_INFO, "WARNING: cell_type file not found!");
   } else {
    applog.log("Found cell_type file, open and read it...");
       fp = fopen(CELL_TYPE_FILE, "rb");
       if(fp != NULL) {
         i = fgetc(fp);
         fclose(fp);
         if (i == 1){
            applog.log("Found a PXS8 Radio");
            Tmp.WriteString("TEMP", "cell_type", "PXS8");
         } else if (i == 2){
            applog.log("Found a PLS8 Radio");
             Tmp.WriteString("TEMP", "cell_type", "PLS8");
         } else if (i == 3){
             applog.log("Found a PLS62 Radio");
             Tmp.WriteString("TEMP", "cell_type", "PLS62_W");
         } else if(i == 4){
            applog.log("Found a ELS31-V Radio");
            Tmp.WriteString("TEMP", "cell_type", "ELS31_V");
         } else if (i == 5){
            applog.log("Found a EC25-AF Radio");
            Tmp.WriteString("TEMP", "cell_type", "EC25_AF");
         } else if(i == 6){
            applog.log("Found a EC25-G Radio");
            Tmp.WriteString("TEMP", "cell_type", "EC25_G");                   
         } else {
              applog.log("Unsupported cell_type index!");
         }
       }
   }

//   if (haveCellular == false && FileExists("/mnt/flash/config/conf/gobiindex") )
//      system("rm /mnt/flash/config/conf/gobiindex");

  // Now determine if the sound is on or off, ! is this relavent for Philips
//  sResult = "";
//  fp = popen("cmp /mnt/flash/titan-appl/etc/titanspkr_notes.conf ./titanspkr_notes.conf.on", "r");
// while(fgets(line, sizeof line, fp))  {
//      sResult.append(line);
//  }
//  pclose(fp);

//  if(sResult == "differ")
//      Tmp.WriteString("TEMP", "Sound", "ON");
//  else
//      Tmp.WriteString("TEMP", "Sound", "OFF");


  if (FileExists("/tmp/mode.conf")  ) {
      GetModeFlags(&mode, &mirror);
      if (mode == 1) {
           Tmp.WriteString("mode", "br_mode" , "YES");
           debugStr = "Active Mode = BR";
      }  else {
           Tmp.WriteString("mode", "br_mode" , "NO");
            debugStr = "Active Mode = AP";
      }
      if (mirror == 1)
           Tmp.WriteString("mode", "mirror" , "YES");
      else
           Tmp.WriteString("mode", "mirror" , "NO");
  } else {
        applog.log("Failed to find /tmp/mode.conf, creating it with default BR mode !");

         Tmp.WriteString("mode","br_mode", "YES");
         Tmp.WriteString("mode","mirror", "YES");
         SetModeFlags(1,1);
         mode = 1;
          debugStr = "Active Mode = BR";
  }

  Tmp.WriteString("mode","DIRTY","NO");
  if(!FileExists((char *)&"/tmp/nand_unlocked"))
         system("nand_unlock MTD6 MTD8 0 > /dev/null 2>&1"); // unlock MTD6

  // load current values for AccessPoint(s)  or set defaults
  //if (!defaults_enabled) //InitalizeAccessPoints(mode);
     LoadAccessPoint();
  applog.log("Finished InitalizeAccessPoints()");

  if (FileExists("/mnt/flash/config/conf/mode_boot.conf") ){
           GetBootModeFlags(&mode, &mirror);
           if (mode == 1) {
                Tmp.WriteString("modeboot", "br_mode" , "YES");
                debugStr.append(", Boot Mode = BR");
           } else {
                Tmp.WriteString("modeboot", "br_mode" , "NO");
                 debugStr.append(", Boot Mode = AP");
           }
           if (mirror == 1)
                Tmp.WriteString("modeboot", "mirror" , "YES");
           else
                Tmp.WriteString("modeboot", "mirror" , "NO");
   } else {
      applog.log("Failed to find /mnt/flash/config/conf/mode_boot.conf, creating it with default BR mode !");
       Tmp.WriteString("modeboot","DIRTY","NO");
       Tmp.WriteString("modeboot","br_mode", "YES");
       Tmp.WriteString("modeboot","mirror", "YES");
       SetBootModeFlags(1,1);
   }
   Tmp.WriteString("modeboot","DIRTY","NO");

  if (!defaults_enabled){
       /* check current operation mode by lookingat mode.conf
          if we are in AP mode then set to Wifi mode
       */
       temp = Tmp.ReadString("mode","br_mode","YES");
       if (temp == "NO"){
           system("echo drv_mode=1 >/proc/mwlan/config"); // disable AP mode, assumes config_start did bss_stop
           system("ifconfig wifi0 up");
           system("touch /tmp/configmodeswitch");
       }
       else
          system("remove /tmp/configmodeswitch");
   }
   //if ( HasSysModeChanged() )
  //     Tmp.WriteString("mode","DIRTY","YES");

  Tmp.WriteIniFile(TEMP_INI);
  if (defaults_enabled){
  applog.log("defaults enabled");
    Tmp.WriteString("TEMP", "public_safety_enabled", "NO");
      /* save copy of tmp.ini defaults for use in restore to factory settings*/
      Tmp.WriteString("TEMP", "DIRTY", "YES");
      Tmp.WriteString("TEMP", "DEFAULT", "RESTORE");
      Tmp.WriteIniFile("/mnt/flash/config/conf/tmp.ini.def");

      if (!FileExists("/mnt/flash/config/conf/cert_map.jsn") )
         system("cp /usr/sbin/http/crestore/cert_map.jsn /mnt/flash/config/conf/cert_map.jsn");
  }
  Tmp.CloseIniFile();
  Def.CloseIniFile();

  system("sync");
  if(!FileExists((char *)&"/tmp/nand_unlocked"))
      system("nand_unlock MTD0 MTD6 1 > /dev/null 2>&1"); // lock MTD6

  if (argc > 1) return 0;
  RemoveAppLog();
//  system("../initstat &");

  printf("Content-Type: text/html\n\n");

  return 0;
}

