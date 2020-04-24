
/* This CGI module is called when applying changes the user
 * has made (via the Browser based GUI). The changes will be
 * in the tmp.ini file (and the DIRTY flag will be set).
 * Changes are read from the tmp.ini and used to make the
 * local file titanlib.conf.
 *
 * If tmp.ini is not DIRTY then no changes are applied, otherwise
 * the above mentioned file is constructed and the local file is
 * copied to its working directories after the old file contents
 * have been backed up.   The file management sequence is:
 *
 * Backup /etc/titanlib/titanlib.conf to file /usr/sbin/http/cgi-bin/SvContent/titanlib.conf.prev
 * Apply changes to the local copy
 * Copy titanlib.conf  to  directory /etc/titanlib
 *
 * tmp.ini with its  "DIRTY" and "F=DEFAULT" keys set to  "NO" is backed up to tmp.ini.prev
 *
 *
 */
#include <fstream>
#include <iostream>
#include <string>
#include <string.h>
#include <sys/stat.h>
#include <sstream>
#include <unistd.h>
#include <debuglog.h>

#include "../../../util/inifile.h"
#include "../../../util/utils.h"
#include "../../nav_page/page.h"
using namespace std;

#define CELLCONFIG_RESP_FILE  "/tmp/configresp"
extern string SafteyMapOut(string);

CIniFile Tmp;
Logger  applog("ApplyChanges");
char debugbuff[200];
char buff[1200];
bool ureg_conf_deleted = false;
bool audio_choice_cleared = false;

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

int DeleteGWYSectionName() {
  string line;
  string csTemp;
  // open input file
  ifstream in("/tmp/gateway.CONF");
  // now open temp output file
  ofstream out("/tmp/gateway.tmp");
  // loop to read/write the file.  Note that you need to add code here to check
  // if you want to write the line
  // GWFlook
  while( getline(in,line) ) {
     csTemp = line.c_str();
     if( csTemp != "[GWY]")
         out << line << "\n";
  }
  in.close();
  out.close();
  // delete the original file
  remove("/tmp/gateway.CONF");
  // rename old to new
  rename("/tmp/gateway.tmp","/tmp/gateway.CONF");
  // all done!
  return 0;
}

/* resp is max three char string
         first char indicates type of update:
                   '0' - no update, nothing was changed
                   '1' - normal update of system config files
                   '2' - reset to factory defaults
       second char indicates if reboot is required:
                   '0' no reboot required after changes applied
                   '1' reboot required  - see next byte for reason
      third char is only used when reboot is required and gives the reason for the reboot
                   '1' default logon creds where restored -  when the logon credentials were previously changed and the restore sets them back to defaults

*/
void ReturnResultPage( char *resp ) {
FILE *fp;
char buff[300];

 applog.log("In ReturnResultPage with resp:");
 applog.log(resp);
    printf("Content-Type: text/html\n\n");

        Header(PG_FT_EX);
        Sidebar(PG_FT_EX, false);
         printf("<script language='javascript'>\n");
         // if reboot required, set javascript varables with reboot flag and reason code
         if (ureg_conf_deleted)
            printf("var ureg_conf_deleted = true;\n");
         else
            printf("var ureg_conf_deleted = false;\n");

         if (audio_choice_cleared)
            printf("var audio_choice_cleared = true;\n");
         else
            printf("var audio_choice_cleared = false;\n");


         if (strlen(resp) == 3 && resp[1] != '0'){
              // we only reboot Physio when creds have changed
              printf(" var reboot = true;\n");
              printf(" var reboot_reason = 'creds';\n");
        } else {
              printf(" var reboot = false;\n");
              printf(" var reboot_reason = 'no';\n");
        }
         printf("var result = %c;\n", resp[0] );  // '0' = no changes, '1'=changes applies, '2' = restore-to-factory
sprintf(buff, "result = %c", resp[0] );
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

}

void  applyNewConfig() {
  string temp, stmp;
  string sTemp;
  const char *pch;
  string GwyID;
  string SelType;
  string AccThresh;
  string SSID;
  string SSID_Vis;
  string AuthType;
  string Key;
  string Gobiindex;
  string InitString;
  string DialString;
  string CellUser;
  string CellPass;
  string Priority;
  string ApnString;
  string serialNum;
  string csPriorityName, csPriorityIndex;
  string backhaul, largeupload;
  string sPubSafety;
  //ofstream out;
  char line2[130];
  CCHR  *pCChar, *pCChar1;
  std::stringstream ss;
  CIniFile  TmpAudio;
  CIniFile  TmpGwy;
  CIniFile  TmpLib;
  CIniFile  Config;
  string sResult;
  ifstream inFile;
  string line;
  FILE *fp, *fp1;
  char *p;
  char resp[4];
  int i=0, x=0;
  bool hasCellularConnection = false;
  bool hasURegInfo = false;
  unsigned int hwopts = readHwOptions();
  unsigned int ui;
  resp[1] = '0';  // stay in BR mode
  resp[2] = '\0';
  resp[3] = '\0';

  Tmp.OpenIniFile(TEMP_INI);

  // We don't want to see errors
  FILE *stream ;
  stream = freopen("/tmp/file.txt", "w", stderr);

applog.log("In applyNewConfig()...");

  // Check and see if the user is restoring the defaults and if so
  // delete the reginfo.txt file,  and audio files
  temp.clear();
  temp = Tmp.ReadString("TEMP", "DEFAULT", "NO");
  if(temp == "RESTORE") {

    if(!FileExists((char *)&"/tmp/nand_unlocked"))
     system("nand_unlock MTD6 MTD8 0 > /dev/null 2>&1"); // unlock MTD6

       system("cp /usr/sbin/http/Default_configstate.ini /mnt/flash/config/conf/configstate.ini");

       system("rm /mnt/flash/config/conf/titanlib.conf");
       system("rm /mnt/flash/config/conf/gateway.conf");
applog.log("applyNewConfig(): detected restor DEFAULT = RESTORE");
       resp[0] = '2';
       system("rm /mnt/flash/titan-data/reginfo.txt");
       system("rm /mnt/flash/audios/*");
       Tmp.WriteString("TEMP", "registration_info", "NO");
       // reset the gateway event logs
       system("rm /mnt/flash/titan-data/eventlog*.txt");
       fp = fopen("/mnt/flash/titan-data/eventlog_no.txt", "w");
       fwrite("0", 1,1,fp);
       fclose(fp);
       fp = fopen("/mnt/flash/titan-data/serial_no.txt", "w");
       fwrite("0", 1,1,fp);
       fclose(fp);

       //sprintf(line2, "rm %s",  CURRENT_IMAGE_FILE);
      // system(line2);
       if( CheckForAudio() ) {
        applog.log("applyNewConfig(): Restore /mnt/flash/audio/titanaudio.conf to defaults");
          system("rm /mnt/flash/titan-data/tftpboot/*.wav");
          system("rm /mnt/flash/titan-data/tftpboot/*.tag");
          system("rm /mnt/flash/audio/*.wav");
          system("rm /mnt/flash/audio/*.tag");
          // put in audio config defaults
          TmpAudio.OpenIniFile("/mnt/flash/audio/titanaudio.conf");
          TmpAudio.WriteString("AUDIO", "samplerate", "11");
          Tmp.WriteString("TEMP", "samplerate", "11");
          TmpAudio.WriteString("AUDIO", "ID3v1size", "128");
          Tmp.WriteString("TEMP", "ID3v1size", "128");
          TmpAudio.WriteString("AUDIO", "incidenttime", "90");
          Tmp.WriteString("TEMP", "incidenttime", "90");
          TmpAudio.WriteString("AUDIO", "preferredTO", "1");
          Tmp.WriteString("TEMP", "preferredTO", "1");
          TmpAudio.WriteIniFile("/mnt/flash/audio/titanaudio.conf");
          TmpAudio.CloseIniFile();
          debuglog(LOG_INFO, "Restore /mnt/flash/audio/titanaudio.conf to defaults.");
       }

      // reset cellular CDMA
      if ( FileExists("/tmp/cdma_reset_enabled") ){
          system("rm /tmp/cdma_reset_enabled");
          system("/usr/sbin/http/cgi-bin/SvContent/cellconfig reset 8 > /tmp/configresp");
          ProcessEndCheck("cellconfig", true);  // wait for it to terminate
          sleep(1);
          fp = fopen(CELLCONFIG_RESP_FILE, "r");
          if (fp != NULL)  {
              fgets(buff, sizeof(buff), fp);
              if( strncmp(buff, "OK", 2) == 0) {
                  debuglog(LOG_INFO, "Radio CDMA carriers reset");
              }
              fclose(fp);
           } else
              debuglog(LOG_INFO, "Failed to reset CDMA");
      }

       debuglog(LOG_INFO, "Restore to factory settings.");

      if ( creds_changed())   {
           resp[1] = '1';  // reboot required
           resp[2] = '1';  // reboot reason - creds changed
       } else
          system("/etc/init.d/S95wifi_status_leds restart > /dev/null 2>&1");

       system("cp ../../httpd.conf /mnt/flash/config/conf/httpd.conf");

             // remove dm client related files
      if ( FileExists((char *)&"/mnt/flash/config/conf/ureg.conf")){
           system("rm /mnt/flash/config/conf/ureg.conf");
           ureg_conf_deleted = true;
      }
      system("rm /mnt/flash/titan-data/dm_reg.txt");
      system("rm /mnt/flash/titan-data/dm_state.txt");
      system("rm /mnt/flash/titan-data/u_check.txt");
      system("rm /mnt/flash/titan-data/update_date.txt");

      if (cellHardwareAvailable()){
          system("rm /mnt/flash/titan-data/audio_choice");
    applog.log("hwopts = 0x%x", hwopts);
          if( (hwopts & OPT_AUDIO) == 0){
               ui = hwopts | OPT_AUDIO;
      applog.log("ui = 0x%x", ui);
               sprintf(line2,"write_config option 0x%x > /dev/null 2>&1", ui);
   applog.log(line2);
               system(line2);
             //  fp = fopen(HWOPTION_FILE, "wb");
             //   if(fp != NULL) {
             //     fwrite(&ui, 1, 4, fp);
             //     fclose(fp);
             //  }

          }
          audio_choice_cleared = true;
      }

      if ( FileExists("/tmp/country_reset_enabled") ){
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
        }else {  //TODO support other 4G modem types
            i = 2;
            applog.log("Found unsupported cellular module type %d, default to PLS8",i);
            Tmp.WriteString("TEMP", "cell_type", "PLS8");
        }
      }

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

  } else   if( strcmp(Tmp.ReadString("TEMP", "DIRTY", "NO"),  "NO") == 0) {
       Tmp.CloseIniFile();
       resp[0] = '0';
//applog.log("applyNewConfig(): detected no changes to apply DIRTY = NO");
       //debugStr.append("No Changes made, config_stop called.");
       debuglog(LOG_INFO, "No Changes applied, config_stop called.");
  } else {

      if(!FileExists((char *)&"/tmp/nand_unlocked"))
          system("nand_unlock MTD6 MTD8 0 > /dev/null 2>&1"); // unlock MTD6

      temp = Tmp.ReadString("UREG", "user_name", "NONE");
      if (temp != "NONE") hasURegInfo = true;

      // Delete the current configuration files in ../SvContent, may not exist
      remove("/tmp/gateway.CONF");
      remove("/tmp/titanlib.conf");

      TmpGwy.OpenIniFile("/tmp/gateway.CONF");
      TmpLib.OpenIniFile("/tmp/titanlib.conf");

      // Apply changes
      if ( FileExists((char *)"/tmp/config_on") == false)
           system("config_start > /dev/null");

      if( temp.find("RESTORE") == string::npos) {
           resp[0] = '1';  // indicate normal changes applied (not restore)
      }

      if( CheckForAudio() ) {
           TmpAudio.OpenIniFile("/mnt/flash/audio/titanaudio.conf");
           TmpAudio.WriteString("AUDIO", "samplerate", Tmp.ReadString("TEMP", "samplerate", ""));
           TmpAudio.WriteString("AUDIO", "ID3v1size", Tmp.ReadString("TEMP", "ID3v1size", ""));
           TmpAudio.WriteString("AUDIO", "incidenttime", Tmp.ReadString("TEMP", "incidenttime", ""));
           TmpAudio.WriteString("AUDIO", "preferredTO", Tmp.ReadString("TEMP","preferredTO","2"));
           TmpAudio.WriteIniFile("/mnt/flash/audio/titanaudio.conf");
           TmpAudio.CloseIniFile();
      }

      // Get the gateway settings from tmp.ini
      sTemp = Tmp.ReadString("TEMP","titanspk","on");
      if (sTemp == "on")
          strcpy(line2,"echo titanspk=on > /mnt/flash/audio/titanspk.conf");
      else
          strcpy(line2, "echo titanspk=off > /mnt/flash/audio/titanspk.conf");
      system(line2);
      TmpGwy.WriteString("GWY", "titanspk", sTemp.c_str());

      temp = Tmp.ReadString("TEMP","GwyID1","");
      temp.append(".");
      temp.append(Tmp.ReadString("TEMP","GwyID2",""));
      temp.append(".");
      serialNum = Tmp.ReadString("TEMP","GwyID3","");
      temp.append(serialNum);

      // Write the settings to gateway.conf
      TmpGwy.WriteString("GWY", "DESCRIPTION", Tmp.ReadString("TEMP","DESCRIPTION",""));
      TmpGwy.WriteString("GWY", "INIVERSION", Tmp.ReadString("TEMP","IniVer",  ""));
      TmpGwy.WriteString("GWY", "CLIENTVERSION", Tmp.ReadString("TEMP","CliVer",  ""));
      TmpGwy.WriteString("GWY", "SERVERVERSION",  Tmp.ReadString("TEMP","ServVer",  ""));
      TmpGwy.WriteString("GWY", "TIMEZONEINDEX", Tmp.ReadString("TEMP","TimeZoneIndex",""));
      TmpGwy.WriteString("GWY", "ROOT", Tmp.ReadString("TEMP","Root",""));
      TmpGwy.WriteString("GWY", "TELENUMBER", Tmp.ReadString("TEMP","TeleNum","unknown"));
      TmpGwy.WriteString("GWY", "GATEWAYID", temp.c_str());
      TmpGwy.WriteString("GWY", "PASSWORD", Tmp.ReadString("TEMP","Password",""));
      TmpGwy.WriteString("GWY", "USERNAME", Tmp.ReadString("TEMP","Username",""));
      TmpGwy.WriteString("GWY", "PROVIDERID", Tmp.ReadString("TEMP","ProvID",""));
      TmpGwy.WriteString("GWY", "DEBUGURL", Tmp.ReadString("TEMP","DebugURL",""));
      TmpGwy.WriteString("GWY", "NETWORKDOWNTIME", Tmp.ReadString("TEMP","NETWORKDOWNTIME",  ""));
      TmpGwy.WriteString("GWY", "UPLOADPOSTDELAY", Tmp.ReadString("TEMP","UploadPDelay",""));
      TmpGwy.WriteString("GWY", "DELAYACK", Tmp.ReadString("TEMP","DelayACK",""));
      TmpGwy.WriteString("GWY", "TIMESYNCDELAY", Tmp.ReadString("TEMP","TimeSyncDel",""));
      TmpGwy.WriteString("GWY", "COMMANDRESPONSE", Tmp.ReadString("TEMP","CmdResp",""));
      TmpGwy.WriteString("GWY", "ECGPOLLTIMEOUT", Tmp.ReadString("TEMP","EcgPollTO",""));
      TmpGwy.WriteString("GWY", "AUDIOPOLLTIMEOUT",  Tmp.ReadString("TEMP","AudioPollTO",""));
      TmpGwy.WriteString("GWY", "VERIFICATIONPOLL", Tmp.ReadString("TEMP","VerPoll",""));
      TmpGwy.WriteString("GWY", "STARTUPRESTART", Tmp.ReadString("TEMP","StartupStart",""));
      TmpGwy.WriteString("GWY", "WAITACK", Tmp.ReadString("TEMP","WaitACK",""));
      TmpGwy.WriteString("GWY", "BAUDRATE", Tmp.ReadString("TEMP","BdRate",""));
      TmpGwy.WriteString("GWY", "BAUDRATEUPSHIFT", Tmp.ReadString("TEMP","BdRateShift",""));
      TmpGwy.WriteString("GWY", "OVERLAPPEDOUTPUT", Tmp.ReadString("TEMP","OverlaOutput",""));
      TmpGwy.WriteString("GWY", "DEBUGLEVEL", "FF");      // Hardcoded
      TmpGwy.WriteString("GWY", "DEBUGFLAG", Tmp.ReadString("TEMP","DebugFlag",""));
      TmpGwy.WriteString("GWY", "beeprate", Tmp.ReadString("TEMP", "beeprate", ""));
      TmpGwy.WriteString("GWY", "chunksize", Tmp.ReadString("TEMP", "chunksize", ""));
      TmpGwy.WriteString("GWY", "cellchunksize", Tmp.ReadString("TEMP", "cellchunksize", ""));
      TmpGwy.WriteString("GWY", "SSLBLOCKSIZE", Tmp.ReadString("TEMP", "sslblocksize", ""));
      TmpGwy.WriteString("GWY", "uploadretry", Tmp.ReadString("TEMP", "uploadretry", ""));
      TmpGwy.WriteString("GWY", "UploadAudioOnly", Tmp.ReadString("TEMP", "UploadAudioOnly", "YES"));

      TmpGwy.WriteIniFile("/tmp/gateway.CONF");
      TmpGwy.CloseIniFile();

//applog.log("applyNewConfig(): Write and close gateway.CONF");

      DeleteGWYSectionName();

//applog.log("applyNewConfig(): Removed GWYSection if there was one and remade gateway.CONF");

      // Write tmp.ini values  to titanlib.conf
      TmpLib.WriteString("General","cellular_good_threshold",Tmp.ReadString("TEMP","cellular_good_threshold",""));
      TmpLib.WriteString("General","cellular_fair_threshold",Tmp.ReadString("TEMP","cellular_fair_threshold",""));
      TmpLib.WriteString("General","cellular_poor_threshold",Tmp.ReadString("TEMP","cellular_poor_threshold",""));
      TmpLib.WriteString("General","wifi_good_threshold",Tmp.ReadString("TEMP","wifi_good_threshold",""));
      TmpLib.WriteString("General","wifi_fair_threshold",Tmp.ReadString("TEMP","wifi_fair_threshold",""));
      TmpLib.WriteString("General","wifi_poor_threshold",Tmp.ReadString("TEMP","wifi_poor_threshold",""));
      TmpLib.WriteString("General","wifi_scan_mode",Tmp.ReadString("TEMP","wifi_scan_mode",""));
      sPubSafety = Tmp.ReadString("TEMP","public_safety_enabled", "NO");
      TmpLib.WriteString("General","public_safety_enabled", sPubSafety.c_str());
      TmpLib.WriteString("General","cellmtu",Tmp.ReadString("TEMP","cellmtu",""));
      TmpLib.WriteString("General","cell_reg_timeout",Tmp.ReadString("TEMP","cell_reg_timeout","240"));
      TmpLib.WriteString("General","cell_act_timeout",Tmp.ReadString("TEMP","cell_act_timeout","240"));


       sPubSafety = Tmp.ReadString("TEMP","public_safety_enabled", "NONE");
          if (sPubSafety == "NONE"){
              sPubSafety = "NO";
          }
          //todo move above 3 lines to


      //Read the number of connections
      pch = Tmp.ReadString("TEMP","NumCons","");
      x = atoi(pch);

      TmpLib.WriteIniFile("/tmp/titanlib.conf");
      TmpLib.CloseIniFile();

      // The [Priority] section must precede the connections (Titan II limitation)
      // so we'll make it now

      // While we are doing this, create an array of all the connection names
      char connections[x][64];

//sprintf(debugbuff, "applyNewConfig(): got num of connections = %d", x);
//applog.log(debugbuff);
      ofstream fout;
      fout.open("/tmp/titanlib.conf", ios_base::app);

      fout << "[Priority]"  << "\n";
      // Now we iterate through the sections of tmp.ini ignoring known section names
      // that are not saved connections, for each we save any available setting they
      // may have
      for(i = 1 ; i <= x; i++) {
          ss.str(std::string());
          ss << i;
          csPriorityIndex = ss.str();
          csPriorityName = Tmp.ReadString("Priority", csPriorityIndex.c_str(), "");
          //fout << csPriorityIndex.c_str() << "=" << csPriorityName.c_str()  << "\n";
          fout << csPriorityName.c_str()  << "\n";
          strcpy(connections[i-1], csPriorityName.c_str());
      }
      fout.close();

      TmpLib.OpenIniFile("/tmp/titanlib.conf");
      for (i = 0; i < x; i++)  {
          // Get a section name
          temp = connections[i];

          //Read any available setting there may be
          SelType = Tmp.ReadString(temp.c_str(),"SelType","");
          if (SelType == "CELLULAR")
               hasCellularConnection = true;
          Gobiindex = Tmp.ReadString(temp.c_str(),"gobiindex","NONE");
          AccThresh = Tmp.ReadString(temp.c_str(),"AccThresh","NONE");
          SSID = Tmp.ReadString(temp.c_str(),"SSID","NONE");
          SSID_Vis = Tmp.ReadString(temp.c_str(),"SSID_Vis","NONE");
          AuthType = Tmp.ReadString(temp.c_str(),"AuthType","NONE");
          Key = Tmp.ReadString(temp.c_str(),"Key","NONE");
          InitString = Tmp.ReadString(temp.c_str(),"additional_init_string","NONE");
          DialString = Tmp.ReadString(temp.c_str(),"dial_string","NONE");
          CellUser = Tmp.ReadString(temp.c_str(),"CellUser","NONE");
          CellPass = Tmp.ReadString(temp.c_str(),"CellPass","NONE");
          ApnString = Tmp.ReadString(temp.c_str(),"APN","NONE");
          backhaul = Tmp.ReadString(temp.c_str(), "backhaul", "NONE");
          largeupload = Tmp.ReadString(temp.c_str(), "largeupload", "NONE");

          // Write settings for this connection to titanlib.conf
          if(AccThresh!= "NONE")
             TmpLib.WriteString(temp.c_str(),"acceptable_threshold",AccThresh.c_str());
          if(SSID!="NONE")
             TmpLib.WriteString(temp.c_str(),"ssid_name",SSID.c_str());
          if(SSID_Vis!="NONE")
             TmpLib.WriteString(temp.c_str(),"ssid_visible",SSID_Vis.c_str());
          if(AuthType!="NONE")
             TmpLib.WriteString(temp.c_str(),"authentication",AuthType.c_str());
          if(Key!="NONE")
             TmpLib.WriteString(temp.c_str(),"key",Key.c_str());
          if(backhaul!="NONE")
             TmpLib.WriteString(temp.c_str(), "backhaul", backhaul.c_str());
          if(largeupload!="NONE")
            TmpLib.WriteString(temp.c_str(), "largeupload", largeupload.c_str());

          if(Gobiindex != "NONE")
                TmpLib.WriteString(temp.c_str(), "gobiindex", Gobiindex.c_str());

          if(InitString!="NONE"){ TmpLib.WriteString(temp.c_str(),"additional_init_string",InitString.c_str()); }
          if(DialString!="NONE"){ TmpLib.WriteString(temp.c_str(),"dial_string",DialString.c_str()); }

          if ( (pCChar = Tmp.ReadString(temp.c_str(), "band", "NONE")) != "NONE")
                   TmpLib.WriteString(temp.c_str(), "band", pCChar);

          if(CellUser!="NONE"){ TmpLib.WriteString(temp.c_str(),"username",CellUser.c_str()); }
          if(CellPass!="NONE"){ TmpLib.WriteString(temp.c_str(),"password",CellPass.c_str()); }
          if(ApnString!="NONE"){
               /* prepend pdp context info to APN string, depending if 3G or 4G */
               sTemp = Tmp.ReadString("TEMP","cell_type","NONE");
               if( sTemp == "PXS8"){
                  sprintf(line2,"+cgdcont=3,\"IP\",\"%s\"", ApnString.c_str());
                  TmpLib.WriteString(temp.c_str(),"apn_string", line2);
               } else {  // for all 4G Radios
// TODO - Quectel EC25 may have different settings                
                  sprintf(line2,"+cgdcont=3,\"IPV4V6\",\"%s\"", ApnString.c_str());
                  TmpLib.WriteString(temp.c_str(),"apn_string", line2);
               }
          }
//----------------------------------------------------------------------


          sTemp = Tmp.ReadString(temp.c_str(),"DiffServ", "NONE");
          if (sTemp == "NONE"){
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
          TmpLib.WriteString(temp.c_str(),"DiffServ", stmp.c_str());

          sTemp = Tmp.ReadString(temp.c_str(),"bandwidth_limit", "NONE");
          if(sTemp == "NONE"){
                 if (SelType == "CELLULAR" && sPubSafety == "YES")
                     TmpLib.WriteString(temp.c_str(),"bandwidth_limit", "2000");
                  else
                    TmpLib.WriteString(temp.c_str(),"bandwidth_limit", "0");
          }else  if (sTemp == "None")
              TmpLib.WriteString(temp.c_str(),"bandwidth_limit", "0");
          else
              TmpLib.WriteString(temp.c_str(),"bandwidth_limit", "2000");

//----------------------------------------------------------

          sTemp = Tmp.ReadString(temp.c_str(),"auth", "NONE");
          if(sTemp != "NONE")
              TmpLib.WriteString(temp.c_str(),"auth", sTemp.c_str());

          sTemp = Tmp.ReadString(temp.c_str(), "AuthMeth", "NONE");
          if(sTemp != "NONE")
              TmpLib.WriteString(temp.c_str(),"AuthMeth", sTemp.c_str());
          TmpLib.WriteString(temp.c_str(),"type", SelType.c_str());
      }

      TmpLib.WriteIniFile("/tmp/titanlib.conf");
      TmpLib.CloseIniFile();
  //     if( !hasCellularConnection)
   //       system("rm /etc/titanlib/gobiindex");

      Tmp.WriteString("TEMP", "DIRTY", "NO");
      Tmp.WriteString("TEMP", "DEFAULT", "NO");

      // keep /usr/sbin/http/configstate.ini up to date
      Config.OpenIniFile("/mnt/flash/config/conf/configstate.ini");
      Config.WriteString("auto","autoCarrier", Tmp.ReadString("TEMP", "autoCarrier", "YES"));
      Config.WriteString("auto","autoCellTellnum", Tmp.ReadString("TEMP", "autoCellTellnum", "YES"));
      Config.WriteString("auto","carrierValue", Tmp.ReadString("TEMP", "autoCarrierValue", "carrier"));
      Config.WriteString("auto","tellnumValue", Tmp.ReadString("TEMP", "autoTellNumValue", "unknown"));
      Config.WriteIniFile("/mnt/flash/config/conf/configstate.ini");
      Config.CloseIniFile();

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


      if(hasURegInfo){
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
      Tmp.WriteIniFile(TEMP_INI);
      applog.log("Update and Close tmp.ini");
      Tmp.CloseIniFile();

      // Delete the real files
      // copy the new files
     // system("mv /mnt/flash/titan-data/conf/titanlib.conf /mnt/flash/titan-data/conf/titanlib.conf.prev");
      system("cp /tmp/titanlib.conf /mnt/flash/config/conf/titanlib.conf");
     // system("mv /mnt/flash/titan-data/gateway.conf /mnt/flash/titan-data/conf/gateway.conf.prev");
      system("cp /tmp/gateway.CONF /mnt/flash/config/conf/gateway.conf");
     // system("cp /tmp/tmp.ini /tmp/tmp.ini.prev");
//applog.log("applyNewConfig(): Deleted and copied files");

      // Delete the local files that should now be exported to the Titan II
      remove("/tmp/gateway.CONF");
      remove("/tmp/titanlib.conf");


      debuglog(LOG_INFO,"Configuration changes Applied.");
applog.log("Configuration changes Applied.");
      if ( serialNum.length() > 0 )    {
//applog.log("applyNewConfig(): Got SN from mtd_read");
         // This gateway has a serial number, we need to see if there is reginfo
         inFile.open(TEMP_INI);

         while( !inFile.eof() ) {
              getline(inFile, line);
              if( strstr(line.c_str(), "[REG]"))
                   break;
         }
         // If we didn't reach the end of the file, we are in the
         // [REG] section so there is reginfo.txt info but only write
         // it to the device if the file is not already present
//         fp = popen("ls /mnt/flash/titan-data/reginfo.txt", "r");

//         sResult.clear();
 //        while ( fgets( line2, sizeof line2, fp)) {
 //            sResult.append(line2);
//         }
 //        pclose(fp);
//applog.log("applyNewConfig(): did ls /mnt/flash/titan-data/reginfo.txt");
//applog.log(sResult.c_str())          ;

         // If the file exist and we're not at the end of the file then a {REG]
         // section was detected , copy from tmp.ini to "/mnt/flash/titan-data/reginfo.txt"
         if( !inFile.eof() )   {
            ofstream outFile;
            outFile.open("/mnt/flash/titan-data/reginfo.txt");
            while(!inFile.eof())  {
               getline(inFile, line);
               replaceAll(line, "\r", "");
               replaceAll(line, "\n", "");
               if( line.size() != 0 )
                   outFile << line << "\r\n";
               else
                 break;
            }
            outFile.close();
             inFile.close();
//applog.log("Delete [REG] section from tmp.ini...");
            // Delete the reginfo from the tmp.ini file so that it
            // will not be appended once exported again
            Tmp.OpenIniFile(TEMP_INI);
            Tmp.DeleteSection("REG");
            Tmp.WriteIniFile(TEMP_INI);
            Tmp.CloseIniFile();
         } else
              inFile.close();
     }

/*     // if we have Ureg info create ureg.conf
     if(hasURegInfo){
        inFile.open(TEMP_INI);
        outFile.open("/mnt/flash/config/conf/ureg.conf", ios::out | ios::app);
        outFile < "[UREG]" << endl;
        while(!inFile.eof())        {
            getline(inFile, line);
            if (line.find("[UREG]", 0) != string::npos){
                    while(!inFile.eof())  {
                        getline(inFile, line);
                        if (line.find('[',0,1) != string::npos) break;
                        outFile << line << endl;
                    }
            }
        }
        inFile.close();
        outFile.close();
     }
*/
      system("cp /mnt/flash/titan-data/time_saved.txt /mnt/flash/config/conf/time_saved.txt");
      system("sync");
      if(!FileExists((char *)&"/tmp/nand_unlocked"))
            system("nand_unlock MTD0 MTD6 1 > /dev/null 2>&1"); // lock MTD6
     // restart wifi_status_led_monitor in case changes where made to threholds
     system("/etc/init.d/S95wifi_status_leds restart > /dev/null 2>&1");
   }
   ReturnResultPage( resp );

}


/* This module is called serveral times from javascript in module ApplyChanges.html
 * which has been previosly loaded by the user pressing the Apply Changes navigation
 * tab.
 * ApplyChanges.html javascript issues Ajax POST calls:
 * cmd=run     - apply the changes to the system if there are any
 *               Respond with the type of response so the caller
 *               can finish the operation ie: call config_stop
 * cmd=respType - response with page that includes Header+Footer+navigation + prompt
 *                message appropriate for the type of action that was taken
 *
 */

int main()  {
char* lpszContentLength;
char* m_lpszRawData;
string  s;
int nContentLength;
char *p;

   // We don't want to see errors
   FILE *stream ;
   stream = freopen("/tmp/AjaxTestErrors", "w", stderr);

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
//applog.log("Start processing...");
//applog.log( m_lpszRawData);
  s = m_lpszRawData;

  free(m_lpszRawData);
//  printf("Content-Type: text/html\n\n");

  s = DecodeURL(s);

applog.log("ApplyChanges called from Browser");
applog.log((char *) s.c_str());

  /* input data must start with "cmd"
    * cmd=run  - apply config changes according to tmp.ini content
    * cmd=n    - n = 0,1,2 - output appropriate HTML with result message
   */
  if (s.find("cmd") == 0 )  {
       s = s.substr (4);
       if (s == "run")
           applyNewConfig();
     //  else
     //      returnResultPage(s[0]);
   }
//applog.log("End Processing!");
   exit(0);
}



