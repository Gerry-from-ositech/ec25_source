/*
   Used as target URL for HTTP POST method


Note: in tmp.ini [TEMP] wifi_type which is set in FT_Im on configurator startup
 If not found then we are running on T2 hardware with old wifi chip /proc/0x9103
 If wifi_type == 0 - then one Access point supported and it is 2.4Ghz
 If wifi_type == 1  - then one Access point supported and it is 5 Ghz
 If wifi_type == 2  - the two Access points supported


*/

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <string.h>
#include <fstream>
#include <malloc.h>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
#include <linux/reboot.h>
#include <sys/reboot.h>
#include <debuglog.h>
#include "../../../util/inifile.h"
#include "../../../util/utils.h"
#include "../../nav_page/page.h"
#include "../../../access_point/apconfig.h"

#define DefaultIP      "192.168.171.2"

using namespace std;

#define CELLCONFIG_APP "/usr/sbin/http/cgi-bin/SvContent/cellconfig"
#define CELLCONFIG_RESP_FILE  "/tmp/configresp"
#define CELL_TYPE_FILE "/tmp/cell_type"

extern string SafteyMapOut(string);

Logger  applog("ApplyChanges");
CIniFile Tmp, Prev;
char buff[1200];
bool ureg_conf_deleted = false;

bool creds_changed() {
FILE *fp;
bool ret = false;

     if ((fp = fopen("/mnt/flash/config/conf/httpd.conf", "r")) != NULL) {
          while( fgets(buff, sizeof(buff), fp) != NULL) {
             if ( strncmp(buff, "/cgi-bin:", 9) == 0) {
                 if ( strstr(buff,"admin:admin" ) == NULL)
                         ret = true;
                 break;
             }
          }
          fclose(fp);
     }
     return ret;
}

/* resp is max three char string
         first char indicates type of update:
                   '0' - no update, nothing was changed
                   '1' - normal update of system config files
                   '2' - reset to factory defaults, no CDMA reset required
                   '3' - reset to factory defaults,  CDMA radio was reset
       second char indicates if reboot is required:
                   '0' no reboot required after changes applie
                   '1' reboot required  - see next byte for reason
      third char is only used when reboot is required and gives the reason for the reboot
                   '0' default staticip was restored - - when statcIP was  previously changes and user now does a restore)

                   // the following are depricated
                   '1' default logon creds where restored -  when the logon credentials were previously changed and the restore sets them back to defaults
                   '2' both staticip and creds where restore
*/
void ReturnResultPage( char *resp ) {
FILE *fp;
char buff[300];

 applog.log("In ReturnResultPage with resp:");
 applog.log(resp);
   printf("Content-Type: text/html\n\n");
//    if (resp[0] == '2' && strlen(resp) == 1) {
//        printf("<!DOCTYPE html>\n<html>\n<body onload='document.redir.submit()'>\n<form action='/cgi-bin/CON_Con' name='redir' method='post'> </form></body></html>");
//     } else {

        Header(PG_FT_EX);
        Sidebar(PG_FT_EX, false);
         printf("<script language='javascript'>\n");
         // if reboot required, set javascript varables with reboot flag and reason code
         if (ureg_conf_deleted)
           printf("var ureg_conf_deleted = true;\n");
         else
            printf("var ureg_conf_deleted = false;\n");
         if (strlen(resp) == 3 && resp[1] != '0'){
              printf(" var reboot = true;\n");
              if (resp[2] == '0')
                   printf(" var reboot_reason = 'staticip';\n");
              else if(resp[2] =='1')
                    printf(" var reboot_reason = 'creds';\n");
              else
                 printf(" var reboot_reason = 'both';\n");
        } else {
              printf(" var reboot = false;\n");
              printf(" var reboot_reason = 'no';\n");
        }
         printf("var result = %c;\n", resp[0] );
sprintf(buff, "var = %c", resp[0]);
applog.log(buff);
         printf("</script>\n");
         fp = popen( "cat ../../cgi/apply_changes.cgi", "r");
          if (fp != NULL) {
             while ( fgets(buff, sizeof(buff), fp) ){
                printf(buff);
             }
             pclose(fp);
          }
          else
              printf("Failed to access cgi file!\n");
          Footer();
//   }

}

void  applyNewConfig() {
string temp, sTemp, stmp, temp1,dirty, enabled, cellType;
string NumCon;
string csPriorityName, csPriorityIndex;
string backhaul, largeupload, broadcastrelay, multicastrelay;
string sPubSafety;
string SelType;
ofstream out;
CCHR  *pCChar;
CCHR *tmp;
FILE *fp, *fp1;
char *p;
struct stat filestatus;
CIniFile TmpLib;
CIniFile StaticIP;
std::stringstream ss;
bool default_settings = false;
ApConfig *ap;
char resp[4];
bool disabled_aps = false;

int i=0, x=0, wifi_type=0;
int mode, mirror, cur_mode, cur_mirror;
unsigned int hwopts = readHwOptions();

resp[0] = '0';     // initialize so no reboot will be performed
resp[1] = '\0';    // indicate if reboot is needed
resp[2] = '\0';    // indicate reason for reboot (staticip changed or creds changed or both)
resp[3] = '\0';

char debug_buff[200];
string debugStr = "Apply Pending Changes: ";

  Tmp.OpenIniFile(TEMP_INI);

  // check if we are appling defaults, rest to factory settings
  if( strcmp(Tmp.ReadString("TEMP", "DEFAULT", "NO"),  "RESTORE") == 0) {
     /* MTD6 is a locked partition where all config files are stored */
      if(!FileExists((char *)&"/tmp/nand_unlocked"))
          system("nand_unlock MTD6 MTD8 0 > /dev/null 2>&1"); // unlock MTD6
      applog.log((char *)"Set default settings........");
      remove(TITANLIB_TMP);  // Delete the current configuration file from /tmp
      remove("/mnt/flash/config/conf/titanlib.conf");
      resp[0] = '2';

      if (creds_changed())   {
         resp[1] = '1';  // reboot required
         resp[2] = '1';  // reboot reason - creds changed
      }
      system("cp ../../httpd.conf /mnt/flash/config/conf/httpd.conf");
      system("rm /mnt/flash/config/conf/uaputl?.conf");
      system("rm /mnt/flash/config/conf/ap.prev");

      // remove dm client related files
      if ( FileExists((char *)&"/mnt/flash/config/conf/ureg.conf")){
         system("rm /mnt/flash/config/conf/ureg.conf");
         ureg_conf_deleted = true;
         //system("touch /tmp/tboot");
      }
      system("rm /mnt/flash/titan-data/dm_reg.txt");
      system("rm /mnt/flash/titan-data/dm_state.txt");
      system("rm /mnt/flash/titan-data/u_check.txt");
      system("rm /mnt/flash/titan-data/update_date.txt");

      // reset logon credentials and default IP
//ls -l      system("cp /mnt/flash/config/conf/uaputl0.conf.def /mnt/flash/config/conf/uaputl0.conf");

      if ( FileExists((char *)&"/tmp/country_reset_enabled")){
          system("write_config countrycode~ > /dev/null 2>&1");
          system("touch /tmp/no_country > /dev/null 2>&1");
          system("rm /tmp/country_reset_enabled");
          Tmp. DeleteKey ("TEMP", "countrycode");
      } else {
          applog.log("calling create_uap_default module to build uaputl0.conf...");
          system("/usr/sbin/http/cgi-bin/create_uap_default");

          ap = new Ap8897();
          Tmp.WriteString("AccessPoint1", "DIRTY", "NO");
          applog.log("calling ap->Read()...");
          ap->Read(Tmp, "AccessPoint1", "/mnt/flash/config/conf/uaputl0.conf");
      }
      applog.log("Clear dirty flag");
      Tmp.WriteString("TEMP","DIRTY","NO");

     // from Tmp read values for mode and mode boot and set these values in the conf files
      temp = Tmp.ReadString("mode","br_mode","YES");
      if (temp == "NO")
         Tmp.WriteString("mode","br_mode","YES");
      SetModeFlags(1,1); // writes defaults to /mnt/flash/config/conf/mode.conf


      temp = Tmp.ReadString("modeboot","br_mode","YES");
      if (temp == "NO")
         Tmp.WriteString("modeboot","br_mode","YES");
      SetBootModeFlags(1,1);

      if (FileExists("/mnt/flash/config/conf/staticip.ini") ){
          applog.log("Reset staticip...");
           StaticIP.OpenIniFile("/mnt/flash/config/conf/staticip.ini");
           temp = StaticIP.ReadString("NETWORK", "IP", DefaultIP);
           StaticIP.CloseIniFile();
           system ("rm /mnt/flash/config/conf/staticip.ini");
           system("write_config staticIP~ mask~ > /dev/null");
           if (temp != DefaultIP) {
                resp[1] = '1';   // indicate reboot is required
                if (resp[2] == '1' )
                    resp[2] = '2';   // indicate both staticIP and creds changed
                else
                    resp[2] = '0';
                debugStr.append("Static IP reset, rebooting system.");
           }
      }

      // reset cellular CDMA
      if ( FileExists("/tmp/cdma_reset_enabled") ){
          applog.log("Reset 3G CDMA...");
          system("rm /tmp/cdma_reset_enabled");
          system("/usr/sbin/http/cgi-bin/SvContent/cellconfig reset 8 > /tmp/configresp");
          ProcessEndCheck("cellconfig", true);  // wait for it to terminate
          sleep(1);
          fp = fopen(CELLCONFIG_RESP_FILE, "r");
          if (fp != NULL)  {
                  fgets(buff, sizeof(buff), fp);
                  if( strncmp(buff, "OK", 2) == 0) {
                            resp[0] = '3';  // Radio was reset
                   }
                  fclose(fp);
           } else
                 debuglog(LOG_INFO, "Failed to reset CDMA");
      }

     if ( FileExists("/tmp/country_reset_enabled") ){
        applog.log("Clear regulatory domain country code");
             Tmp.DeleteKey ("TEMP", "countrycode");
             system("rm /tmp/country_reset_enabled");
             debuglog(LOG_INFO, "Reset regulatory domain to defaults.");
             system("write_config countrycode~ > /dev/null 2>&1");
             system("mlanutl wifi0 countrycode US > /dev/null 2>&1");
             resp[0] = '4';
      } else if (hwopts & OPT_ENABLE_REG_DOMAINS){
           // Regulatory domains enabled, check for country, if country found set country_code in tmp.ini
           pCChar = read_config(CFG_MEM_COUNTRY_CODE, "-n" );
           if (pCChar[0] == 0 || pCChar == NULL || strstr(pCChar ,"ERROR") != NULL){
               applog.log("Found Reglatory Domains flag enabled and no country_code defined" );
           } else {
               Tmp.WriteString("TEMP", "countrycode", pCChar);
           }
      }

      fp = fopen(CELL_TYPE_FILE, "rb");
      if(fp != NULL) {
   applog.log("Set cellular radio type");
          i = fgetc(fp);
          fclose(fp);
        if (i == 1){
           applog.log("Found a PXS8 Radio");
           Tmp.WriteString("TEMP", "cell_type", "PXS8");
        } else if (i == 2){
            applog.log("Found a PLS8 Radio");
            Tmp.WriteString("TEMP", "cell_type", "PLS8");
        } else if (i == 3){
            applog.log("Found a PLS62-W Radio");
            Tmp.WriteString("TEMP", "cell_type", "PLS62_W");
        }else if (i == 4){
            applog.log("Found a ELS31-V Radio");
            Tmp.WriteString("TEMP", "cell_type", "ELS31_V");
        } else if (i == 5){
            applog.log("Found a EC25-AF Radio");
            Tmp.WriteString("TEMP", "cell_type", "EC25_AF");
        } else if(i == 6){
            applog.log("Found a EC25-G Radio");
            Tmp.WriteString("TEMP", "cell_type", "EC25_G");            
        } else {  //TODO support other 4G modem types
            i = 2;
            applog.log("Found unsupported cellular module type %d, default to PLS8",i);
            Tmp.WriteString("TEMP", "cell_type", "PLS8");
        }
      } else
        applog.log("Failed to find /tmp/cell_type file!");

      Tmp.WriteString("TEMP", "DIRTY", "NO");
      Tmp.WriteString("TEMP", "DEFAULT", "NO");
      Tmp.WriteIniFile(TEMP_INI);
      Tmp.CloseIniFile();

      // reset trust store
      if ( FileExists("/usr/sbin/http/crestore/cert_map.jsn")){
           if( FileExists("/mnt/flash/config/conf/tsdirty")){
               debuglog(LOG_INFO, "Trust store has been modified, restore to factory settings...");
               system("rm /mnt/flash/config/conf/ssl/certs/*.*");
               system("cp /usr/sbin/http/crestore/*.pem /mnt/flash/config/conf/ssl/certs/");
               system("cp /usr/sbin/http/crestore/*.0 /mnt/flash/config/conf/ssl/certs/");
               system("cp /usr/sbin/http/crestore/cert_map.jsn /mnt/flash/config/conf/");
               system("rm /mnt/flash/config/conf/tsdirty");
           }
      }
         //reset dn_client.conf
      system("cp /mnt/flash/titan-appl/Templates/config/conf/dm_client.conf /mnt/flash/config/conf/dm_client.conf");

      system("sync");
      if(!FileExists((char *)&"/tmp/nand_unlocked"))
         system("nand_unlock MTD0 MTD6 1 > /dev/null 2>&1"); // lock MTD6
applog.log("Factory Defaults Reset");
      debuglog(LOG_INFO, "Factory Defaults Reset");

  } else  if( strcmp(Tmp.ReadString("TEMP", "DIRTY", "NO"),  "NO") == 0) {
          Tmp.CloseIniFile();
          resp[0] = '0';
          debugStr.append("No Changes made, config_stop called.");
          debuglog(LOG_INFO, debugStr.c_str());
          applog.log((char *)debugStr.c_str());
  } else {
         applog.log("TEMP:DIRTY=YES - we need to apply changes...");
          debuglog(LOG_INFO, debugStr.c_str());
          // Apply changes
         if ( FileExists("/tmp/config_on") == false)
              system("config_start");

         FILE *stream ;
         stream = freopen("/tmp/file.txt", "w", stderr); // We don't want to see errors
//         setenv("PATH", "/ositech:/mnt/flash/titan-appl/bin:/bin:/sbin:/usr/bin:/usr/sbin:/usr/bin/X11:/usr/local/bin", 1);

        if(!FileExists((char *)&"/tmp/nand_unlocked"))
            system("nand_unlock MTD6 MTD8 0 > /dev/null 2>&1"); // unlock MTD6

         // handle mode changes if any
         temp = Tmp.ReadString("modeboot", "DIRTY", "YES");
         if(temp == "YES") {
             temp1 = Tmp.ReadString("modeboot","br_mode","YES");
             if(temp1 == "YES") mode = 1;
             else mode = 0;
             GetBootModeFlags(&cur_mode, &cur_mirror);
             if (mode != cur_mode) {
                 applog.log("Mode is different, need to switch...");
                 SetBootModeFlags(mode, 1);
/*                 if (mode == 1){
                     system("/usr/sbin/http/cgi-bin/SvContent/flipmode BR");
                     applog.log("ApplyChanges: flip to BR mode, reboot not required?");
                     debuglog(LOG_INFO, "flip to BR mode");
                 } else {
                     system("/usr/sbin/http/cgi-bin/SvContent/flipmode AP");
                     applog.log("ApplyChanges: flip to AP mode, reboot not required?");
                     debuglog(LOG_INFO, "flip to AP mode");
                 } */
             } else
                applog.log("modeboot was dirty but current mode already set, no switch required!");
             Tmp.WriteString("modeboot","DIRTY","NO");
             // set mode.conf to be the same as mode_boot.conf
             GetBootModeFlags(&cur_mode, &cur_mirror);
             SetModeFlags(cur_mode, cur_mirror);
             if (mode)
                 Tmp.WriteString("mode","br_mode","YES");
             else
                 Tmp.WriteString("mode","br_mode","NO");
             Tmp.WriteString("mode","DIRTY","NO");
             //resp[1] = '1';  // indicate a reboot is required
     //applog.log("ApplyChanges: indicates reboot is required");
         }

         remove(TITANLIB_TMP);  // Delete the current temporary configuration file
         TmpLib.OpenIniFile(TITANLIB_TMP);

applog.log("Building new titanlib.conf...");
         // copy fields from tmp.ini to new titanlib.conf
        TmpLib.WriteString("General","cellular_good_threshold",Tmp.ReadString("TEMP","cellular_good_threshold", ""));
        TmpLib.WriteString("General","cellular_fair_threshold",Tmp.ReadString("TEMP","cellular_fair_threshold", ""));
        TmpLib.WriteString("General","cellular_poor_threshold",Tmp.ReadString("TEMP","cellular_poor_threshold", ""));
        TmpLib.WriteString("General","wifi_good_threshold",Tmp.ReadString("TEMP","wifi_good_threshold", ""));
        TmpLib.WriteString("General","wifi_fair_threshold",Tmp.ReadString("TEMP","wifi_fair_threshold", ""));
        TmpLib.WriteString("General","wifi_poor_threshold", Tmp.ReadString("TEMP","wifi_poor_threshold", ""));
        TmpLib.WriteString("General","wifi_scan_mode", Tmp.ReadString("TEMP","wifi_scan_mode", ""));
        sPubSafety = Tmp.ReadString("TEMP","public_safety_enabled", "NO");
        TmpLib.WriteString("General","public_safety_enabled", sPubSafety.c_str());
       // TmpLib.WriteString("General","ps_custom", Tmp.ReadString("TEMP","ps_custom", "00"));

//      TmpLib.WriteString("General","wifimtu", Tmp.ReadString("TEMP","wifimtu", "1500"));
        TmpLib.WriteString("General","cellmtu", Tmp.ReadString("TEMP","cellmtu", "1500"));
         TmpLib.WriteString("General","cell_reg_timeout", Tmp.ReadString("TEMP","cell_reg_timeout", "240"));
         TmpLib.WriteString("General","cell_act_timeout",Tmp.ReadString("TEMP","cell_act_timeout","240"));

        //Read the number of connections
        NumCon = Tmp.ReadString("TEMP","NumCons","");
        x = atoi(NumCon.c_str());

        TmpLib.WriteIniFile(TITANLIB_TMP);
        TmpLib.CloseIniFile();

        // The [Priority] section must precede the connections (Titan II limitation)
        // so we'll make it now

        // While we are doing this, create an array of all the connection names
        char connections[x][64];

        ofstream fout;
        fout.open(TITANLIB_TMP, ios_base::app);

        fout << "[Priority]"  << "\n";
        // Now we iterate through the sections of tmp.ini ignoring known section names
        // that are not saved connections, for each we save any available setting they
        // may have

        for(i = 1 ; i <= x; i++)  {
            ss.str(std::string());
            ss << i;
            csPriorityIndex = ss.str();
            csPriorityName = Tmp.ReadString("Priority", csPriorityIndex.c_str(), "");
            //fout << csPriorityIndex.c_str() << "=" << csPriorityName.c_str()  << "\n";
            fout << csPriorityName.c_str()  << "\n";
            strcpy(connections[i-1], csPriorityName.c_str());
        }
        fout.close();

  sprintf(debug_buff,"Constructing %d connections...", x);
   applog.log(debug_buff);
        TmpLib.OpenIniFile(TITANLIB_TMP);

        cellType = Tmp.ReadString("TEMP","cell_type","NONE");
      // loop through x number of connections
      for (i = 0; i < x; i++) {
         tmp = connections[i];        // Get a section name

         SelType = Tmp.ReadString(tmp,"SelType","");

         // Get all possible stored wireless values
          if ( (pCChar = Tmp.ReadString(tmp,"auth", "")) != "")
             TmpLib.WriteString(tmp,"auth", pCChar);
         if ( (pCChar = Tmp.ReadString(tmp,"AuthMeth", "")) != "")
              TmpLib.WriteString(tmp,"AuthMeth", pCChar);
         if ( (pCChar = Tmp.ReadString(tmp,"AccThresh", "")) != "")
             TmpLib.WriteString(tmp,"acceptable_threshold", pCChar);
         if ( (pCChar = Tmp.ReadString(tmp,"SSID", "NONE")) != "NONE")
               TmpLib.WriteString(tmp,"ssid_name", pCChar);
         if ( (pCChar = Tmp.ReadString(tmp,"SSID_Vis", "NONE")) != "NONE")
               TmpLib.WriteString(tmp,"ssid_visible", pCChar);
         if ( (pCChar = Tmp.ReadString(tmp,"AuthType", "NONE")) != "NONE")
                TmpLib.WriteString(tmp,"authentication", pCChar);
         if ( (pCChar = Tmp.ReadString(tmp,"Key", "NONE")) != "NONE")
                TmpLib.WriteString(tmp,"key", pCChar);
         if ( (pCChar = Tmp.ReadString(tmp,"APN", "NONE")) != "NONE"){

   applog.log("got APN string = '%s'", pCChar);
            if( cellType == "PXS8"){
               sprintf(buff,"+cgdcont=3,\"IP\",\"%s\"", pCChar);
               TmpLib.WriteString(tmp,"apn_string", buff);
            } else {  // for all 4G Radios
// TODO - Quectel EC25 may have different settings                
      applog.log("Write into string = '%s'", pCChar);
               sprintf(buff,"+cgdcont=3,\"IPV4V6\",\"%s\"", pCChar);
     applog.log(buff);
               TmpLib.WriteString(tmp,"apn_string", buff);
            }
         }

//--------------------------------------------------------------------------------------------


          sTemp = Tmp.ReadString(tmp,"DiffServ", "NONE");
          if(sTemp == "NONE"){
              if (SelType == "CELLULAR" && sPubSafety == "YES")
                  sTemp = "CS3/AF3x (60)";
              else
                  sTemp = "None (00)";
          }
          stmp =  SafteyMapOut(sTemp);
          if (stmp == "Custom"){
              stmp = sTemp;
              stmp.insert(0,"0x");
          }
          TmpLib.WriteString(tmp,"DiffServ", stmp.c_str());

          sTemp = Tmp.ReadString(tmp,"bandwidth_limit", "NONE");
          if(sTemp == "NONE"){
              if (SelType == "CELLULAR" && sPubSafety == "YES")
                  TmpLib.WriteString(tmp,"bandwidth_limit", "2000");
              else
                  TmpLib.WriteString(tmp,"bandwidth_limit", "0");
          } else if (sTemp == "None")
              TmpLib.WriteString(tmp,"bandwidth_limit", "0");
          else
              TmpLib.WriteString(tmp,"bandwidth_limit", "2000");
//----------------------------------------------------------------------------------------

         if ( (pCChar = Tmp.ReadString(tmp,"CellUser", "NONE")) != "NONE")
                  TmpLib.WriteString(tmp,"username", pCChar);
         if ( (pCChar = Tmp.ReadString(tmp,"CellPass", "NONE")) != "NONE")
                  TmpLib.WriteString(tmp,"password", pCChar);
         if ( (pCChar = Tmp.ReadString(tmp, "backhaul", "NONE")) != "NONE")
                 TmpLib.WriteString(tmp, "backhaul", pCChar);
         if ( (pCChar = Tmp.ReadString(tmp, "largeupload", "NONE")) != "NONE")
                  TmpLib.WriteString(tmp, "largeupload", pCChar);
          if ( (pCChar = Tmp.ReadString(tmp, "broadcastrelay", "NONE")) != "NONE")
              TmpLib.WriteString(tmp, "broadcastrelay", pCChar);
          if ( (pCChar = Tmp.ReadString(tmp, "multicastrelay", "NONE")) != "NONE")
             TmpLib.WriteString(tmp, "multicastrelay", pCChar);
          if ( (pCChar = Tmp.ReadString(tmp, "dial_string", "NONE")) != "NONE")
                   TmpLib.WriteString(tmp, "dial_string", pCChar);
          if ( (pCChar = Tmp.ReadString(tmp, "band", "NONE")) != "NONE")
                   TmpLib.WriteString(tmp, "band", pCChar);
          if ( (pCChar = Tmp.ReadString(tmp, "additional_init_string", "NONE")) != "NONE")
                   TmpLib.WriteString(tmp, "additional_init_string", pCChar);

          if ( (pCChar = Tmp.ReadString(tmp, "gobiindex", "NONE")) != "NONE")
                   TmpLib.WriteString(tmp, "gobiindex", pCChar);
          if ( (pCChar = Tmp.ReadString(tmp,"SelType", "")) != "") {
              TmpLib.WriteString(tmp,"type", pCChar);
 applog.log("All params for %s finished!", tmp);
         }
      }

      TmpLib.WriteIniFile(TITANLIB_TMP);
      TmpLib.CloseIniFile();
      Tmp.WriteString("TEMP", "DIRTY", "NO");
      Tmp.WriteString("TEMP", "DEFAULT", "NO");
      system("mv /tmp/titanlib.tmp /mnt/flash/config/conf/titanlib.conf");
 applog.log("Moved titanlib.conf");
    temp  = Tmp.ReadString("TEMP", "http_url", "NONE");
      if (temp != "NONE"){
 applog.log("Found db_client.conf changes.");
         temp += "\n\0";
         sTemp  = Tmp.ReadString("TEMP", "pass_days", "NONE");
         sTemp += "\n\0";
         //   sed -i "s/\(http_url=\).*/\1this is a test/g;s/\(pass_days=\).*/\1100/" /mnt/flash/config/conf/dm_client.conf
         // sprintf(buff, "sed -i \"s/\\(http_url=\\).*/\\1%s/g;s/\\(pass_days=\\).*/\\1%s/\" /mnt/flash/config/conf/dm_client.conf", temp.c_str(), sTemp.c_str());
         // sed has escaping problems because of special chars in the data
         system("cp /mnt/flash/config/conf/dm_client.conf /tmp/dm.tmp");
         fp = fopen("/tmp/dm.tmp", "rb");
         fp1 = fopen("/mnt/flash/config/conf/dm_client.conf", "wb");
         while (fgets(buff, sizeof(buff), fp)) {
            if ( strncmp(buff, "http_url",8) == 0){
                 p = strchr(buff,'=');
                 p++;
                 strcpy(p, temp.c_str());
            } else if ( strncmp(buff, "pass_days",9) == 0){
                 p = strchr(buff,'=');
                 p++;
                 strcpy(p, sTemp.c_str());
            }
            fwrite(buff, 1, strlen(buff), fp1);
         }
         fclose(fp);
         fclose(fp1);
         Tmp.DeleteKey ("TEMP", "http_url");
         Tmp.DeleteKey ("TEMP", "pass_days");

      }

      dirty = Tmp.ReadString("AccessPoint1", "DIRTY", "NONE");
if(dirty != "NONE")  {
   applog.log("AccessPoint1 dirty...........");
      ap = new Ap8897();

      /* check if new wifi APs are enabled:
       * If so, make sure that at least defaults are in place
       * If not, remove AP config file
       * TODO: is there a requirment for the enabled field since
       * it is assumed that if mode.conf indicates AP mode the at least one
       * AP will be enabled and if T3 and hwoption bit indicates two APs
       * then two APs will be enabled
       */
       /*

          enabled = Tmp.ReadString("AccessPoint1", "ENABLED", "NO");
          if( stat("/mnt/flash/config/conf/uaputl0.conf", &filestatus) != 0) {
              if (enabled == "YES"){
                   // target file uaputl0.conf does not exists, create it with defaults
                   system("cp /mnt/flash/config/conf/uaputl0.conf.def /mnt/flash/config/conf/uaputl0.conf");
                   applog.log("Created new default AccessPoint1 in uaputl0.conf");
              }
          } else if( enabled == "NO"){
              system("rm /mnt/flash/config/conf/uaputl0.conf");
          }
          enabled = Tmp.ReadString("AccessPoint2", "ENABLED", "NO");
          if( stat("/mnt/flash/config/conf/uaputl1.conf", &filestatus) != 0) {
              if (enabled == "YES"){
                   // target file uaputl1.conf does not exists, create it with defaults
                   system("cp /mnt/flash/config/conf/uaputl1.conf.def /mnt/flash/config/conf/uaputl1.conf");
                   applog.log("Created new default AccessPoint2 in uaputl1.conf");
              }
          } else if( enabled == "NO"){
              system("rm /mnt/flash/config/conf/uaputl1.conf");
          }
      */

          // update APs
//      system("rm /mnt/flash/config/conf/ap.prev");
//      Prev.OpenIniFile(AP_PREV_INI);

      if (dirty == "YES" ) {
           applog.log("AccessPoint1 is dirty");
           ap->Write(Tmp, "AccessPoint1", "/mnt/flash/config/conf/uaputl0.conf");
        /*
            temp = Tmp.ReadString("AccessPoint1", "ENABLED", "NO");
            if (temp == "NO"){
                applog.log("AccessPoint1 is not enabled");
               if(stat("/mnt/flash/config/conf/uaputl0.conf", &filestatus) == 0)
                  debuglog(LOG_INFO, "Disabled Access Point 1.");
               system("rm /mnt/flash/config/conf/uaputl0.conf");
               Tmp.WriteString("AccessPoint1", "DIRTY", "NO");
               copy_ini_section("AccessPoint1", Tmp, Prev);
               disabled_aps = true;
            } else {
                if(stat("/mnt/flash/config/conf/uaputl0.conf", &filestatus) != 0)  {
                    system("cp /mnt/flash/config/conf/uaputl0.conf.def /mnt/flash/config/conf/uaputl0.conf");
                    debuglog(LOG_INFO, "Enabled Access Point 1.");
                }
                Tmp.WriteString("AccessPoint1", "DIRTY", "NO");
                ap->Write(Tmp, "AccessPoint1", "/mnt/flash/config/conf/uaputl0.conf");
            }
          */
        }
/*
        dirty = Tmp.ReadString("AccessPoint2", "DIRTY", "NO");
        if (dirty == "YES" ) {  // if found a second AP that is DIRTY
            applog.log("AccessPoint1 is dirty");
           temp = Tmp.ReadString("AccessPoint2", "ENABLED", "NO");
           if (temp == "NO"){
             if(stat("/mnt/flash/config/conf/uaputl1.conf", &filestatus) == 0)
                     debuglog(LOG_INFO, "Disabled Access Point 2.");
              system("rm /mnt/flash/config/conf/uaputl1.conf");
              Tmp.WriteString("AccessPoint2", "DIRTY", "NO");
              copy_ini_section("AccessPoint2", Tmp, Prev);
              disabled_aps = true;
           } else {
               if(stat("/mnt/flash/config/conf/uaputl1.conf", &filestatus) != 0)  {
                   system("cp /mnt/flash/config/conf/uaputl1.conf.def /mnt/flash/config/conf/uaputl1.conf");
                   debuglog(LOG_INFO, "Enabled Access Point 2.");
               }
               Tmp.WriteString("AccessPoint2", "DIRTY", "NO");
               ap->Write(Tmp, "AccessPoint2", "/mnt/flash/config/conf/uaputl1.conf");
          }
        }
        if (disabled_aps) {
          Prev.WriteIniFile(AP_PREV_INI);
          Prev.CloseIniFile();
        }
*/
}

        temp = Tmp.ReadString("UREG", "user_name", "NONE");
        if (temp != "NONE"){
 applog.log("Ureg name = %s", temp.c_str());
            fp = fopen("/mnt/flash/config/conf/ureg.conf","wb");
            if (fp){
  applog.log("Creating ureg.conf from UREG section in tmp.ini, then remove [UREG] section in tmp.ini");
                temp = "user_name=";
                sTemp  = Tmp.ReadString("UREG", "user_name", "");
                temp.append(sTemp);
                temp.append(1,'\n');
                fwrite(temp.c_str(), 1, temp.length(), fp);

                temp = "user_company=";
                sTemp  = Tmp.ReadString("UREG", "user_company", "");
                temp.append(sTemp);
                temp.append(1,'\n');
                fwrite(temp.c_str(), 1, temp.length(), fp);
                temp = "user_phone=";
                sTemp  = Tmp.ReadString("UREG", "user_phone", "");
                temp.append(sTemp);
                temp.append(1,'\n');
                fwrite(temp.c_str(), 1, temp.length(), fp);
                temp = "user_email=";
                sTemp  = Tmp.ReadString("UREG", "user_email", "");
                temp.append(sTemp);
                temp.append(1,'\n');
                fwrite(temp.c_str(), 1, temp.length(), fp);
                temp = "user_tracking=";
                sTemp  = Tmp.ReadString("UREG", "user_tracking", "");
                temp.append(sTemp);
                temp.append(1,'\n');
                fwrite(temp.c_str(), 1, temp.length(), fp);
                temp = "description=";
                sTemp  = Tmp.ReadString("UREG", "description", "");
                temp.append(sTemp);
                temp.append(1,'\n');
                fwrite(temp.c_str(), 1, temp.length(), fp);
                fclose(fp);
           }
           Tmp.DeleteSection("UREG");
      }
 debuglog(LOG_INFO, "Update Config state tmp.ini");
        Tmp.WriteIniFile(TEMP_INI);
        Tmp.CloseIniFile();
        resp[0] = '1';   // normal update performed

        //debugStr.append("config_stop called.");
        debuglog(LOG_INFO, debugStr.c_str());
       system("cp /mnt/flash/titan-data/time_saved.txt /mnt/flash/config/conf/time_saved.txt");
       system("sync");
       if(!FileExists((char *)&"/tmp/nand_unlocked"))
            system("nand_unlock MTD0 MTD6 1 > /dev/null 2>&1"); // lock MTD6

   }
   sprintf(debug_buff,"Response is %s", resp);
   applog.log(debug_buff);

   ReturnResultPage( resp );

}

/* op may be 0 -  no changes detected or applied
*            1 - changes applied succesfully
*            2 - restore to factory defaults - no reboot
*            11 - changes applied include mode switch - reboot
*            21 - restor to factory defaults - reboot
*/
void  returnResultPage( string op ) {

  Header(PG_FT_EX);
  Sidebar(PG_FT_EX, false);
  printf("<script type='text/javascript' src='../../js/AjaxRequest.js'></script>\n");
  printf("<script language='javascript'>\n");
  printf(" var reboot = false;\n");
  printf("</script>\n");
  printf("<iframe id='exit' name='exit' height='300' width='100%%' marginheight='0' frameborder='0' style='display:none; overflow:hidden;' src='/wait.html'>Your browser does not support iframes</iframe>\n");
  printf("<div id='mainContent'>\n");

   if ( op.length() == 1) {
        if ( op == "0") {
            printf("<div id='title'><p><strong>Apply Changes</strong></p></div>\n");
            // ***** NOTE in case of any errors, we don't really want to say Changes Applied
            printf("Sorry, but there were no changes detected that need to be applied.\n");
            printf("</div>\n");  // end of mainContent
        }  else if (op == "1") {
           printf("<div id='title'><p><strong>Changes Applied</strong></p></div>\n");
           printf("Your Ositech Wireless Link has been configured for use with the settings you have applied.<p>\n");
           printf("</div>\n");  // end of mainContent
           printf("<script type='text/javascript' language='javascript'>\n\n");
           printf("var now = new Date();\n");
           printf("AjaxRequest.post({'url':'/cgi-bin/SvContent/SysCommand'\n");
                 printf(" ,'parameters': { 'cmd' : './logdebug \"'+'Applied changes at '+now+'\"' }\n");
           printf("});\n");
           printf("</script>\n");
        } else {

         // following 3 lines removed to mimic action of T2 which just locates to page CON_Con after reset to factory defaults
        //   printf("<div id='title'><p><strong>Changes Applied</strong></p></div>\n");
        //   printf("Your Ositech Wireless Link has been reset to Factory Values.\n");
        //   printf("</div>\n");
           printf("<script language='javascript'>\n");
            printf("window.location='/cgi-bin/CON_Con';\n");
           printf("</script>\n");
        }
  } else {  // system is rebooting
     if(op == "11") {
           printf("<script language='javascript'>\n");
           printf(" reboot = true;\n");
           printf("</script>\n");
           printf("<div id='title'><p><strong>Changes Applied</strong></p></div>\n");
           printf("The startup mode has been changed and a reboot is in progress.\n");
           printf("<p>To continue using the Configurator you need to log back in after reboot.</p>\n");
           printf("</div>\n");
     }
     else if(op == "21") {
           printf("<script language='javascript'>\n");
           printf(" window.onload = RebooTitan;\n");
           printf("</script>\n");
           printf("<div id='title'><p><strong>Changes Applied</strong></p></div>\n");
           printf("Your Ositech Wireless Link has been reset to Factory Values and a reboot is in progress.\n");
           printf("<p>To continue using the Configurator you need to log back in using the default IP (192.168.171.2).</p>\n");
           printf("</div>\n");
     }
  }

 // printf("<script type='text/javascript' language='javascript'>\n\n");
  printf("<script language='javascript'>\n");
  printf("var destUrl;\n");
  printf("var stage = 1;\n");

  printf ("function RebooTitan() { \n");
      printf("AjaxRequest.post({'url':'/cgi-bin/SvContent/SysCommand'\n");
                         printf(" ,'parameters': { 'cmd' : 'reboot' }\n");
                         printf(", 'onSuccess':function(req){}\n");
                         printf("});\n");
  printf("}\n");

   printf("function ConfigStarted(req) {\n");
       printf("stage = 2;\n");
       printf("checkConnection();\n");
   printf("}\n");

printf("function checkConnection() {\n");
    printf("if (window.navigator.onLine == false) {\n");
       printf("setTimeout(function(){checkConnection()}, 5000);\n");
    printf("} else {\n");
    // The window.navigator.onLine can return false positives so use the AjaxRequest to verify
    // we have a valid connection

    printf("AjaxRequest.post({'url':'/cgi-bin/SvContent/SysCommand'\n");
       printf(" ,'parameters': { 'cmd' : 'echo ok' }\n");
       printf(" ,'onSuccess': function(req){\n");
                       printf("if (stage == 1){\n");
                         printf("AjaxRequest.post({'url':'/cgi-bin/SvContent/SysCommand'\n");
                         printf(" ,'parameters': { 'cmd' : 'config_start' }\n");
                         printf(", 'onSuccess':function(req){stage=2; checkConnection(); }\n");
                         printf(", 'onError':function(req){ setTimeout(function(){checkConnection()}, 5000);} \n");
                         printf("});\n");

                       printf("} else {\n");

                        printf("AjaxRequest.post({'url':'/cgi-bin/SvContent/SysCommand'\n");
                              printf(" ,'parameters': { 'cmd' : './logdebug \"'+'Config_start called, resuming Configurator GUI...'+'\"' }\n");
                        printf("});\n");

                        printf("var submitForm = document.createElement('FORM');\n");
                        printf("document.body.appendChild(submitForm);\n");
                        //printf("if (destUrl.indexOf('debug') != -1)\n");
                       //    printf("submitForm.method = 'POST';\n");
                        //printf("else\n");
                           printf("submitForm.method = 'GET';\n");
                        printf("submitForm.action= destUrl;\n");
                        printf("submitForm.submit();\n");
                       printf("}\n");
                 printf("}\n");
       printf(" ,'onError': function(req){ setTimeout(function(){checkConnection()}, 5000);  }\n");
       printf(" ,'timeout': 5000  // timeout in millsecs\n");
       printf(" ,'onTimeout': function(req){ setTimeout(function(){checkConnection()}, 5000); }\n");
    printf("});\n");

  printf("}\n");
printf("}\n");

printf("function startConfig(url) {\n");

 printf("if (reboot)\n");
  printf("location.href=\"/\";\n");


  printf("destUrl = url;\n");  // save url of destination page for later use

  // disable showing page elemenst we no longer want to see
  printf("document.getElementById('header').style.display = 'none';\n");
  printf("document.getElementById('sidebar1').style.display = 'none';\n");
  printf("document.getElementById('mainContent').style.display = 'none';\n");

  // alter the outer container div
  printf("var e = document.getElementById('container');\n");
  printf("e.setAttribute('style','border: 0; text-align: left; margin: 20px 0 0 15px');\n");
  printf("document.getElementById('footer').style.display = 'none';\n");

  // show the iframe with the Please wait bar animation
  printf("document.getElementById('exit').style.display = 'block'\n");
  printf("stage = 1;\n");
  printf("checkConnection();\n");

printf("}\n");
printf("alert('hung2 ');\n");
  printf("if (reboot) { window.onload = RebooTitan; }\n");

printf("</script>\n");

   Footer();

}

int main() {
char* lpszContentLength;
char* m_lpszRawData;
string  s;
int nContentLength;
char *p;


// We don't want to see errors
   FILE *stream ;
   stream = freopen("/tmp/file.txt", "w", stderr);

  lpszContentLength = getenv("CONTENT_LENGTH"); // retrieve CONTENT_LENGTH environment variable

  if(lpszContentLength == NULL) // die if we couldn't get it
    return 0;         // this probably means the program is NOT being run under CGI

  nContentLength = atoi(lpszContentLength); // atoi it

  if(nContentLength == 0) // die if it's 0
    return 0;

  p = m_lpszRawData = (char*) malloc(nContentLength+1); // allocate a buffer

  if(m_lpszRawData == NULL) // die if allocation failed
    return 0;

  memset(m_lpszRawData, 0, nContentLength+1); // zero out the buffer

  if(fread(m_lpszRawData, 1, nContentLength, stdin) == 0) // get the data
    return 0;

  if(ferror(stdin)) // die if there was an fread error
    return 0;
applog.log("Start processing...");
applog.log( m_lpszRawData);
  s = m_lpszRawData;
    free(m_lpszRawData);
   s = DecodeURL(s);
applog.log((char *) s.c_str());

   /* input data must start with "cmd"
    * cmd=run  - apply config changes according to tmp.ini content
    * cmd=n    - n = 0,1,2 - output appropriate HTML with result message
    */
   if (s.find("cmd") == 0 ) {
       s = s.substr (4);
       if (s == "run")
         applyNewConfig();
       else
         returnResultPage(s);
   }
applog.log("End Processing!");
   exit(0);
}
