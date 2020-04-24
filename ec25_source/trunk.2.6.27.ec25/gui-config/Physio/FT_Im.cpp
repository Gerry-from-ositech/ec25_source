/* This module is called via a redirect from index.html as part of configurator startup.
 * Code added for Philips checks /etc/hostname  for the presense of "Philips" in the string
 * If not Philips then redeirect to updatex.html  otherwise ontinue with module execution
 *
 * Module deletes the current tmp.ini if it exists  and rebuilds it from the titanlib.conf if it exists
 * If titanlib.conf fors not exists the tmp.ini is created from Default.ini (DEFAULT=YES)
 *
 * After creating tmp.ini this module does a redirect to GWY_Welcome.cpp
 */
#include <string>
#include <string.h>
#include <errno.h>
#include <algorithm>
#include <fstream>
#include <netinet/in.h>
#include <cctype>
#include <sys/stat.h>
#include "../../../util/inifile.h"
#include "../../../util/utils.h"
#include <debuglog.h>

using namespace std;


#define FEATURE_PENDING_FLAG "/mnt/flash/titan-data/feature_pending"
extern string SafteyMapIn(string);
CIniFile Tmp;

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


char debug[500];  // for debugging
Logger  applog("FT_Im");
// Use this function to convert the variable names from the gateway format, to the the utilities.
string ConvF(string name)  {
   if(name=="DEBUGFLAG")           { name="DebugFlag"; return name; }
   if(name=="DEBUGLEVEL")          { name="DebugLvl"; return name; }
   if(name=="OVERLAPPEDOUTPUT")    { name="OverlaOutput"; return name;}
   if(name=="BAUDRATEUPSHIFT")     { name="BdRateShift"; return name; }
   if(name=="BAUDRATE")            { name="BdRate"; return name; }
   if(name=="WAITACK")             { name="WaitACK"; return name; }
   if(name=="STARTUPRESTART")      { name="StartupStart"; return name; }
   if(name=="VERIFICATIONPOLL")    { name="VerPoll"; return name; }
   if(name=="ECGPOLLTIMEOUT")      { name="EcgPollTO"; return name; }
   if(name=="AUDIOPOLLTIMEOUT")    { name="AudioPollTO"; return name; }
   if(name=="COMMANDRESPONSE")     { name="CmdResp"; return name; }
   if(name=="TIMESYNCDELAY")       { name="TimeSyncDel"; return name; }
   if(name=="TIMEZONEINDEX")       { name="TimeZoneIndex"; return name; }
   if(name=="DELAYACK")            { name="DelayACK"; return name; }
   if(name=="UPLOADPOSTDELAY")     { name="UploadPDelay"; return name; }
   if(name=="NETWORKDOWNTIME")     { name="NETWORKDOWNTIME"; return name; }
   if(name=="DEBUGURL")            { name="DebugURL"; return name; }
   if(name=="PROVIDERID")          { name="ProvID"; return name; }
   if(name=="USERNAME")            { name="Username"; return name; }
   if(name=="PASSWORD")            { name="Password"; return name; }
   if(name=="GATEWAYID")           { name="GwyID"; return name; }
   if(name=="TELENUMBER")          { name="TeleNum"; return name; }
   if(name=="CLIENTVERSION")       { name="CliVer"; return name; }
   if(name=="ROOT")                { name="Root"; return name; }
   if(name=="SERVERVERSION")       { name="ServVer"; return name; }
   if(name=="INIVERSION")          { name="IniVer"; return name; }
   if(name=="DESCRIPTION")         { name="DESCRIPTION"; return name; }
   if(name=="beeprate")            { name="beeprate"; return name; }
   if(name=="chunksize")           { name="chunksize"; return name; }
   if(name=="cellchunksize")       { name="cellchunksize"; return name; }
   if(name=="SSLBLOCKSIZE")        { name="sslblocksize"; return name;}
   if(name=="uploadretry")         { name="uploadretry"; return name; }
   if(name=="UploadAudioOnly")     { name="UploadAudioOnly"; return name; }
   if(name=="titanspk")     { name="titanspk"; return name; }
   return "";
}

////////////////////////////////////////////////////////////////////////////
void set_wifi_ap_options(unsigned int sys_options) {
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
        applog.log("Set option parameters in tmp.ini");
   }
}

bool get_org_model_number1(char *model_number) {
bool ret = false;
FILE *fp;

  fp = fopen("/mnt/flash/config/conf/org_model_number", "rb");
  if(fp != NULL) {
      fread(model_number, 1, 50, fp);
      fclose(fp);
      ret = true;
  }
  return ret;
}


/* the orgoption and option fields of wwprom are set to the same value as part of
 * testbead setup of devices before they are shipped to customer
 * the orgoption value will indicate a primary model type and will allow
 * us to determin the original model number
 *    orgoption     model
         0x04     - MPC15300-100-00    -100
         0x02     - MPC15300-200-00    -200
         0x06     - MPC15300-300-00    -300
         0x07     - MPC15300-400-00    -400

         replaces  get_org_model_number()
 */
unsigned int read_orgoptions() {
FILE *fp;
char *p;
char flags[100];
unsigned int opts;

   fp = popen("read_config -n orgoption", "r");
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
    opts = opts & 0x07;
    return opts;
}
unsigned int get_eeprom_hwoption() {
FILE *fp;
char *p;
char flags[100];
unsigned int opts;

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
    return opts;
}

/* return True if we have previous org_model number and if so check
 * for a model change caused by feature activation. In this case the
 * model number has changed and was set in tmp.ini field "GwyID2"
 *  If no org_model number return false so caller can create it
 *  This function could create flag files in /tmp
 *  /tmp/feature_error  - indicates invalid model change option bits screwed up
 *  /tmp/feature_activated - indicates pending feature should be activated
 *                           the model number should be updated in
 *                           gateway.conf field GwyID if it exists
 *                           and maybe reginfo.txt field GatewayID if it exists
 *                           alse in [TEMP]:GwyID
 *   the two flag files are used in GWY_Welcome.cpp to setup GUI prompts
 *   to indicate the confirmation of feature activation.
 */
bool check_for_feature_activation(unsigned int sys_options) {
char buff[50];
char buff1[80];
unsigned int  opt_byte = sys_options & 0x07;
bool reg_domains_supported = false;

   if ( sys_options & OPT_ENABLE_REG_DOMAINS )
       reg_domains_supported = true;

sprintf(debug, "in check_for_feature_activation() with options = 0x%08x", opt_byte);
applog.log(debug);
      /* determine the model number based on option bits and the original
         model number if it exists. If the  /mnt/flash/config/conf/org_model_number
         does not exist then create it from the option bits. These bits should match
         one of the four base units 0x04 0x02, 0x06, 0x07
      */
      unsigned org_ops = read_orgoptions();
      unsigned eeprom_hwopts = get_eeprom_hwoption();
      eeprom_hwopts  = eeprom_hwopts & 0x07;

sprintf(debug, "orgoption = 0x%08x", org_ops);
applog.log(debug);
sprintf(debug, "hwoption = 0x%08x", eeprom_hwopts);
applog.log(debug);


      if (org_ops == eeprom_hwopts && (org_ops == 0x04 )){
         applog.log("Detected a Wifi only - not upgradable!");
           // Wifi only device, only shipped model that cannot be feature upgraded
           if (reg_domains_supported )
                strcpy(buff1,"MPC15301-150-00");
            else
                strcpy(buff1, "MPC15301-100-00");
      } else {
            if (opt_byte == org_ops) {
                applog.log("No new features activated!");
                return false;   // no new features activated
            }
      //if ( get_org_model_number(buff) ) {
            // check for feature upgrade model number
            buff1[0] = '\0';
            if (org_ops == 0x04) { // MPC15300-100-00
                switch(opt_byte) {
                   case 4:
                       if (reg_domains_supported )
                             strcpy(buff1,"MPC15300-150-00");
                       else
                             strcpy(buff1,"MPC15300-100-00");
                       break;
                   case 6:
                        if (reg_domains_supported )
                             strcpy(buff1,"MPC15300-150-20");
                       else
                            strcpy(buff1,"MPC15300-100-20");
                       break;
                   case 5:
                       if (reg_domains_supported )
                             strcpy(buff1,"MPC15300-150-40");
                       else
                             strcpy(buff1,"MPC15300-100-40");
                       break;
                   case 7:
                       if (reg_domains_supported )
                             strcpy(buff1,"MPC15300-150-60");
                       else
                             strcpy(buff1,"MPC15300-100-60");
                   break;
                }
            }else if(org_ops == 0x02) {   //strstr(buff,"-200")){
                switch(opt_byte) {
                   case 2:
                       strcpy(buff1,"MPC15300-200-00");
                       break;
                   case 6:
                       strcpy(buff1,"MPC15300-200-10");
                       break;
                   case 3:
                       strcpy(buff1,"MPC15300-200-40");
                       break;
                   case 7:
                       strcpy(buff1,"MPC15300-200-50");
                   break;
                }
            }else if(org_ops == 0x06){
                if( opt_byte == 6){
                     if (reg_domains_supported )
                             strcpy(buff1,"MPC15300-350-00");
                       else
                            strcpy(buff1,"MPC15300-300-00");
                }else if (opt_byte == 7){
                      if (reg_domains_supported )
                          strcpy(buff1,"MPC15300-350-40");
                       else
                          strcpy(buff1,"MPC15300-300-40");
                }
            } else if(org_ops == 0x07){
                if(opt_byte == 7){
                      if (reg_domains_supported )
                             strcpy(buff1,"MPC15300-450-40");
                       else
                          strcpy(buff1,"MPC15300-400-00");
                }
            }
      }
            if (buff1[0] == '\0') {
  applog.log("No Model name change, something wrong ");
                  // this should not happend in production code
                  Tmp.WriteString("TEMP", "GwyID2","unknown");
                  debuglog(LOG_INFO, "WARNING: Upgrade Option flags do not match upgrade model");
                  if ( FileExists(FEATURE_PENDING_FLAG) )
                     system("touch /tmp/feature_error");
            } else {
  applog.log("Model number changes to");
  applog.log(buff1);
                 Tmp.WriteString("TEMP", "GwyID2", buff1);
                 if ( FileExists(FEATURE_PENDING_FLAG) ){
  applog.log("Feature was pending");
                       system("touch /tmp/feature_activated");
                       sprintf(debug,"Feature Activation: Gateway model changed to %s", buff1);
                       debuglog(LOG_INFO, debug);
                 }
            }
            remove(FEATURE_PENDING_FLAG);
            return true;
}

char * determine_model_number(int cell_type_index, unsigned int opts, char model_num[]){
FILE *fp;
unsigned int m,i;
char line[100];
char key[20];

  applog.log("determine_model_number: hwoptions 0x%X, cell_type = %d", opts, cell_type_index);

    fp = fopen("/usr/sbin/http/model_map.txt","rb");
    if(fp != NULL) {
        fgets(line, sizeof line, fp);
//printf("First line: \"%s\"\n",line);
        if ( strncmp(line,"mask=0x",4) != 0){
            debuglog(LOG_INFO,"model_map.txt wrong format on line 1");
            applog.log("determine_model_number: model_map.txt wrong format on line 1");
        } else {
            // make key using mask of optsf
            strcpy(line,&line[5]);
            line[strlen(line)-1] = '\0';
            i = (unsigned int)strtol( line, NULL, 0);
            m = opts & i;
            sprintf(key,"%x-%d", m,cell_type_index);
  applog.log("determine_model_number: determine_model_number: map key = %s", key);
            while(fgets(line, sizeof line, fp)){
               if(line[0] == ';' || line[0] == '\n') continue;
//printf("key = %s - Next line : \"%s\"\n",key, line);
                if ( strncmp(line,key,strlen(key) ) == 0){
                     line[strlen(line)-1] = '\0';
                     strcpy(model_num, &line[strlen(key)+1]);
//printf("Returns: \"%s\"\n", model_num);
                }
            }
            fclose(fp);
            if (model_num[0] == '\0'){
                debuglog(LOG_INFO, "WARNING: No model number found for key \"%s\" in model_map.txt!", key);
                applog.log("determine_model_number: WARNING: No model number found for key \"%s\" in model_map.txt!", key);
            }
        }
    } else {
       debuglog(LOG_INFO, "Failed to find model_map.txt");
       applog.log("determine_model_number: Failed to find model_map.txt");
    }
    return model_num;
}


/* this function is called when Feature Activation takes place and the model number
 * is changed as a result. Some values in gateway.conf and reginfo.txt need to be updated
 *  GwyID=wifi+audio.TitanGateway.99AGA2004E95
 */
void modify_gateway_model_number(){
FILE *fp, *fp1;
char buff[200];
char buff1[200];
string temp;

    fp = fopen("/tmp/gateway.tmp", "wb");
    fp1 = fopen("/mnt/flash/config/conf/gateway.conf", "rb");
    while ((fgets(buff, sizeof(buff), fp1))) {
         // GATEWAYID=Wifi.MPC15000-001.WG01654321
         if (strncmp(buff, "GATEWAYID", 9) == 0)  {
                 temp = Tmp.ReadString("TEMP","GwyID1","");
                 temp.append(".");
                 temp.append(Tmp.ReadString("TEMP","GwyID2",""));
                 temp.append(".");
                 temp.append(Tmp.ReadString("TEMP","GwyID3",""));
                 temp.append("\n");
                 strcpy(&buff[10], temp.c_str());
         }
         fwrite(buff,1, strlen(buff), fp);
    }
    fclose(fp1);
    fclose(fp);
    system("mv /tmp/gateway.tmp /mnt/flash/config/conf/gateway.conf");
}

void modify_tmp_ini_def(){
CIniFile def;
    def.OpenIniFile("/mnt/flash/config/conf/tmp.ini.def");
    def.WriteString("TEMP", "GwyID1", Tmp.ReadString("TEMP","GwyID1", ""));
    def.WriteString("TEMP", "GwyID2", Tmp.ReadString("TEMP","GwyID2", ""));
    /*
    def.WriteString("TEMP", "TeleNum",Tmp.ReadString("TEMP", "TeleNum", "n/a"));
    def.WriteString("TEMP", "autoCarrierValue", Tmp.ReadString("TEMP", "autoCarrierValue", "Wifi"));
    def.WriteString("TEMP", "autoTellNumValue",Tmp.ReadString("TEMP", "autoTellNumValue","n/a"));
    def.WriteString("TEMP", "autoCarrier", Tmp.ReadString("TEMP", "autoCarrier", "NO"));
    */
    def.WriteIniFile("/mnt/flash/config/conf/tmp.ini.def");
    def.CloseIniFile();
}

int main() {
char buff[255];
char buff1[100];
CCHR  *pCChar;
int nCount, i, j, NumCon;
int cell_type_index = 0;
string AccThresh, Type, SSID, SSID_Vis, Auth, Gobiindex,Cellmtu;
string Key, InitString, ApnString, DialString, CellUser, CellPass, Enc, EAPMethod;
string backhaul, largeupload, IniVer, DiffServ, Bandwidth, auth;
string tmp, stmp;
string keyname;
string sResult;
CIniFile TmpLib, Def, Config, tmpini;
char *  chrp;
char titan_sn[20];
bool defaults_enabled = false;
unsigned int sys_options, hw_opts,  opt_byte;
CIniFile TmpAudio;
string SamplingRate, TagSize, MaxIncidentTime;
FILE *fp;
size_t found;
char line2[255];
bool reg_domains_supported = false;


applog.log("Started....");


 /* MTD6 is a locked partition where all config files are stored */
// if(!FileExists((char *)&"/tmp/nand_unlocked"))
//     system("nand_unlock MTD6 MTD8 0"); // unlock MTD6

   // Don't allow stderr output to web browser
   FILE *stream ;
   stream = freopen("/tmp/file.txt", "w", stderr);

   // inform system that configurator is running
    if(FileExists("/tmp/config_on") == false)
         system("config_start > /dev/null");
   remove("/tmp/tboot");      // delete titan boot flag file
   //LoadAppName();
applog.log("detected system options...");
   sys_options = detect_system_options();
   sprintf(debug,"detected system options: %d", sys_options);
   applog.log(debug)  ;
   hw_opts = get_eeprom_hwoption();
   chrp = read_config(CFG_MEM_SERIAL, "-n");
   strcpy(titan_sn, chrp);

   applog.log("Titan serial num:");
   applog.log(titan_sn);

   // Ensure that the files exist
   bool bExist = true;
   if(FileExists("/mnt/flash/config/conf/gateway.conf") == false){
       bExist = false;
       applog.log("No gateway.conf file found!");
   }
   if(FileExists("/mnt/flash/config/conf/titanlib.conf") == false){
       bExist = false;
       applog.log("No titanlib.conf file found!");
   }
   remove("/tmp/tmp.ini");    // delete the current temp file so we start off clean

   if( SupportsCellular() ) {
         // check for existence of cell_type file, if it doesn't exist ,default to PXS8
         // however if the file does not exist the radio probably is not power enabled and will
         // be inoperable
         if ( !FileExists(CELL_TYPE_FILE) ){
             applog.log("cell_type file missing, default to PXS8. Radio is disabled!");
             Tmp.WriteString("TEMP", "cell_type", "PXS8");
             debuglog(LOG_INFO, "WARNING: cell_type file not found!");
         } else {
             applog.log("Found cell_type file, open and read it...");
             fp = fopen(CELL_TYPE_FILE, "rb");
             if(fp != NULL) {
               cell_type_index = fgetc(fp);
               fclose(fp);
            }
        }
  }

   // need to get some stuff  from Default.ini later
   Def.OpenIniFile("/usr/sbin/http/cgi-bin/SvContent/Default.ini");

   if(bExist == true)  {
        Tmp.OpenIniFile("/tmp/tmp.ini");
       //applog.log("At least one of gateway.conf, titanlib.conf or titanaudio.conf exists");
         applog.log("saw a conf file, assume we have a configuration to use");
       /* build new tmp.ini from from existing system conf files */

       if ( !FileExists("/mnt/flash/config/conf/configstate.ini") ){
           if(!FileExists((char *)&"/tmp/nand_unlocked"))
              system("nand_unlock MTD6 MTD8 0 > /dev/null 2>&1"); // unlock MTD6
           system("cp /usr/sbin/http/Default_configstate.ini /mnt/flash/config/conf/configstate.ini");
           system("sync");
           if(!FileExists((char *)&"/tmp/nand_unlocked"))
               system("nand_unlock MTD0 MTD6 1 > /dev/null 2>&1"); // lock MTD6
       }
       // copy auto flags state into tmp.ini for use in Basic Page, Export, etc
       Config.OpenIniFile("/mnt/flash/config/conf/configstate.ini");
       Tmp.WriteString("TEMP", "autoCarrier", Config.ReadString("auto","autoCarrier", "YES"));
       Tmp.WriteString("TEMP", "autoCellTellnum", Config.ReadString("auto","autoCellTellnum", "YES"));
       Tmp.WriteString("TEMP", "autoCarrierValue", Config.ReadString("auto","carrierValue", "carrier"));
       Tmp.WriteString("TEMP", "autoTellNumValue", Config.ReadString("auto","tellnumValue", "unknown"));
       Config.CloseIniFile();

       TmpLib.OpenIniFile("/mnt/flash/config/conf/titanlib.conf");

       // This portion reads all the settings from gateway.conf file and puts them into the tmp.ini file
       // and for some reason changes the key names
       string b;
       string name, val;
       string EqComp = "=";
       char EQ;
       char EQ_C=EqComp.at(0);

       i = NumCon = 0;

       //fstream inputFile("/usr/sbin/http/cgi-bin/SvContent/gateway.conf");
       fstream inputFile("/mnt/flash/config/conf/gateway.conf");

       while (!inputFile.eof()) {
           getline(inputFile, b);

           if(b != ""){
               name=b.c_str();
               for(i = 0; i <= name.length(); i++){
                   EQ=name.at(i);
                   if(EQ == EQ_C)   {                           //ITS FOUND THE SPLITTER
                       val = name.substr(i + 1, name.length());            // save key value
                       name = name.substr(0, i);                   // save key name
                       name = ConvF(name);                     // convert the key name for some reason?????

                       //Write the settings into the tmp.ini as  key / value pairs

                       /* if name = "GwyID" then split into three fields in tmp.ini GwyID1, GwyID2, GwyID3
                        * when changes are applied the three fields are combined with '.' as seperator
                        * befor writing back to gateway.conf
                        */
                       if (name == "GwyID"){
                            tmp = "";
                            j = 0;
                            for (std::string::iterator it=val.begin(); it != val.end(); it++) {
                                if (*it == '.') {
                                  if (j == 0)
                                    Tmp.WriteString("TEMP", "GwyID1", tmp.c_str());
                                  else if (j == 1)
                                    Tmp.WriteString("TEMP", "GwyID2", tmp.c_str());
                                  j++;
                                  tmp = "";
                                }
                                else
                                  tmp.push_back(*it);
                            }
                            Tmp.WriteString("TEMP", "GwyID3", tmp.c_str());  // serial number
                       } else {
                            Tmp.WriteString("TEMP", name.c_str(), val.c_str());
//applog.log("%s=%s", name.c_str(), val.c_str()) ;
                       }
                   }
               }
           }
       }
       inputFile.close();
       tmp = Tmp.ReadString("TEMP","UploadAudioOnly", "NONE");
       if (tmp == "NONE")
         Tmp.WriteString("TEMP", "UploadAudioOnly", "YES"); // add default if field missing from gateway.conf

       // Write changes to the ini file
       Tmp.WriteIniFile("/tmp/tmp.ini");

       stmp = TmpLib.ReadString("General","public_safety_enabled","NO");
       Tmp.WriteString("TEMP","public_safety_enabled", stmp.c_str());

       // Get the basic settings from titanlib.conf
       // and then write them to tmp.ini
       keyname = TmpLib.ReadString("General","cellular_good_threshold", "");
       Tmp.WriteString("TEMP","cellular_good_threshold",keyname.c_str());

       keyname = TmpLib.ReadString("General","cellular_fair_threshold", "");
       Tmp.WriteString("TEMP","cellular_fair_threshold",keyname.c_str());

       keyname = TmpLib.ReadString("General","cellular_poor_threshold", "");
       Tmp.WriteString("TEMP","cellular_poor_threshold",keyname.c_str());

       keyname = TmpLib.ReadString("General","wifi_good_threshold", "");
       Tmp.WriteString("TEMP","wifi_good_threshold",keyname.c_str());

       keyname = TmpLib.ReadString("General","wifi_fair_threshold", "");
       Tmp.WriteString("TEMP","wifi_fair_threshold",keyname.c_str());

       keyname = TmpLib.ReadString("General","wifi_poor_threshold", "");
       Tmp.WriteString("TEMP","wifi_poor_threshold",keyname.c_str());

       keyname = TmpLib.ReadString("General","wifi_scan_mode", "");
       Tmp.WriteString("TEMP","wifi_scan_mode",keyname.c_str());

       Cellmtu = TmpLib.ReadString("General","cellmtu", "");
       if( Cellmtu != "")
             Tmp.WriteString("TEMP","cellmtu",Cellmtu.c_str());

       Tmp.WriteString("TEMP","cell_reg_timeout",  TmpLib.ReadString("General","cell_reg_timeout", "60"));
       Tmp.WriteString("TEMP","cell_act_timeout",  TmpLib.ReadString("General","cell_act_timeout", "240"));

//       IniVer = TmpLib.ReadString("TEMP","IniVer", "4.0.0.0");
       if (Cellmtu == "")
             Tmp.WriteString("TEMP","cellmtu", Def.ReadString("TEMP","cellmtu", "1300"));
       Def.CloseIniFile();
//       Tmp.WriteString("TEMP","IniVer",IniVer.c_str());

       // Write current setting to the inifile
       Tmp.WriteIniFile(TEMP_INI);

       fp = popen( "cat /mnt/flash/config/conf/titanlib.conf", "r");
       if (fp != NULL) {
          while ( fgets(line2, sizeof(line2), fp) ) {
             if( line2[0] == '[' && strchr(line2,']')){
                if( !strstr(line2,"[General]") && !strstr(line2,"[Priority]")) {
                       NumCon++;
                       applog.log("Found connection name:");
                       applog.log(line2);
                 }
            }
          }
          pclose(fp);
       }
       else
           printf("Failed to access titanlib.conf!\n");


/*
       // Now we need to count the number of connections that are in the titanlib.conf file
       //inputFile.open("/usr/sbin/http/cgi-bin/SvContent/titanlib.conf");
       inputFile.open("/mnt/flash/config/conf/titanlib.conf");
       i = 0;

       while (!inputFile.eof()) {
           inputFile >> b;
           if(b.find("[") != -1 && b.find("]") != -1)  {
               if(b.find("[General]") == -1 && b.find("[Priority]") == -1)
                   NumCon++;
           }
       }
       inputFile.close();
*/

   if ( NumCon > 0 ) {
   // Now we have the number of connections, open the file again and make an array of
        // the connection names
      applog.log("parse connections...");
        char connections[NumCon][64];
        fp = popen( "cat /mnt/flash/config/conf/titanlib.conf", "r");
        if (fp != NULL) {
           i = 0;
           while ( fgets(line2, sizeof(line2), fp) ){
              if( line2[0] == '[' && strchr(line2,']')){
                 if( !strstr(line2,"[General]") && !strstr(line2,"[Priority]")) {
                      line2[strlen(line2)-2] = '\0';
             applog.log(line2);
                      strcpy(connections[i], &line2[1]);
  //sprintf(debug,"Final name for connetion %d = %s", i+1, connections[i]); applog.log(debug);
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
       // GWFlook
       while (!inputFile.eof())   {
           inputFile >> b;
           if(b.find("[") != -1 && b.find("]") != -1) {
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
       for (j = 0; j < NumCon; j++)  {
           tmp = connections[j];               // Get a section name

           // Get all possible stored wireless values
           AccThresh = TmpLib.ReadString(tmp.c_str(),"acceptable_threshold", "NONE-GFS");
           Type = TmpLib.ReadString(tmp.c_str(),"type", "NONE-GFS");
           SSID = TmpLib.ReadString(tmp.c_str(),"ssid_name", "NONE-GFS");
           SSID_Vis = TmpLib.ReadString(tmp.c_str(),"ssid_visible", "NONE-GFS");
           Auth = TmpLib.ReadString(tmp.c_str(),"authentication", "NONE-GFS");
           auth = TmpLib.ReadString(tmp.c_str(),"auth", "NONE-GFS");
           Key = TmpLib.ReadString(tmp.c_str(),"key", "NONE-GFS");
           InitString = TmpLib.ReadString(tmp.c_str(),"additional_init_string", "NONE-GFS");
           ApnString = TmpLib.ReadString(tmp.c_str(),"apn_string", "NONE-GFS");
           DiffServ = TmpLib.ReadString(tmp.c_str(),"DiffServ", "NONE-GFS");
           Bandwidth = TmpLib.ReadString(tmp.c_str(),"bandwidth_limit", "NONE-GFS");
           DialString = TmpLib.ReadString(tmp.c_str(),"dial_string", "NONE-GFS");
           CellUser = TmpLib.ReadString(tmp.c_str(),"username", "NONE-GFS");
           CellPass = TmpLib.ReadString(tmp.c_str(),"password", "NONE-GFS");
           EAPMethod = TmpLib.ReadString(tmp.c_str(),"eap_method", "NONE-GFS");
           Enc = TmpLib.ReadString(tmp.c_str(),"encryption", "NONE-GFS");
           backhaul = TmpLib.ReadString(tmp.c_str(), "backhaul", "NONE-GFS");
           largeupload = TmpLib.ReadString(tmp.c_str(), "largeupload", "NONE-GFS");
           Gobiindex = TmpLib.ReadString(tmp.c_str(), "gobiindex", "NONE-GFS");
 //          Cellmtu = TmpLib.ReadString(tmp.c_str(), "cellmtu", "NONE-GFS");

           // Now store them into the tmp.ini file we are creating
           if(Type!="NONE-GFS")        { Tmp.WriteString(tmp.c_str(),"SelType",Type.c_str()); }
           if(AccThresh!="NONE-GFS")   { Tmp.WriteString(tmp.c_str(),"AccThresh",AccThresh.c_str()); }
           if(SSID!="NONE-GFS")        { Tmp.WriteString(tmp.c_str(),"SSID",SSID.c_str()); }
           if(SSID_Vis!="NONE-GFS")    { Tmp.WriteString(tmp.c_str(),"SSID_Vis",SSID_Vis.c_str()); }
           if(Auth!="NONE-GFS")        { Tmp.WriteString(tmp.c_str(),"AuthType",Auth.c_str()); }
           if(auth!="NONE-GFS")        { Tmp.WriteString(tmp.c_str(),"auth",auth.c_str()); }
           if(Enc!="NONE-GFS")     { Tmp.WriteString(tmp.c_str(),"Enc",Enc.c_str()); }
           if(EAPMethod!="NONE-GFS")   { Tmp.WriteString(tmp.c_str(),"EAPMethod",EAPMethod.c_str()); }
           if(Key!="NONE-GFS")     { Tmp.WriteString(tmp.c_str(),"Key",Key.c_str()); }
           if(InitString!="NONE-GFS")  { Tmp.WriteString(tmp.c_str(),"additional_init_string",InitString.c_str()); }
           if(DialString!="NONE-GFS")  { Tmp.WriteString(tmp.c_str(),"dial_string",DialString.c_str()); }
           if(CellUser!="NONE-GFS")    { Tmp.WriteString(tmp.c_str(),"CellUser",CellUser.c_str()); }
           if(CellPass!="NONE-GFS")        { Tmp.WriteString(tmp.c_str(),"CellPass",CellPass.c_str()); }
           if(ApnString!="NONE-GFS")   {
               /* strip off etra data to get raw APN string */
               if( ApnString.compare(0,10,"+cgdcont=3") == 0){
                     found = ApnString.find("\",\"");
                     found += 3;
                     stmp = ApnString.substr(found);
                     stmp.erase(stmp.end()-1, stmp.end());
                     Tmp.WriteString(tmp.c_str(),"APN", stmp.c_str());
               } else
                   Tmp.WriteString(tmp.c_str(),"APN", ApnString.c_str());
          }
          if(DiffServ != "NONE-GFS"){
             stmp =  SafteyMapIn(DiffServ);
             if (stmp == "Custom"){
                stmp = DiffServ;
                stmp = DiffServ.substr(2, string::npos);
             }
             Tmp.WriteString(tmp.c_str(),"DiffServ", stmp.c_str());
          }
          if (Bandwidth != "NONE-GFS"){
              if (Bandwidth == "0")
                  Tmp.WriteString(tmp.c_str(),"bandwidth_limit", "None");
              else {
                   Tmp.WriteString(tmp.c_str(),"bandwidth_limit","2 Mbps");
              }
          }

           if ( (pCChar = TmpLib.ReadString(tmp.c_str(), "band", "NONE")) != "NONE")
               Tmp.WriteString(tmp.c_str(), "band", pCChar);

//           if( Cellmtu != "NONE-GFS")   {   Tmp.WriteString(tmp.c_str(),"cellmtu",Cellmtu.c_str());  }
           if(backhaul != "NONE-GFS")
               Tmp.WriteString(tmp.c_str(), "backhaul", backhaul.c_str());
           if(largeupload != "NONE-GFS")
               Tmp.WriteString(tmp.c_str(), "largeupload", largeupload.c_str());
           if(Gobiindex != "NONE-GFS") {
               Tmp.WriteString(tmp.c_str(), "gobiindex", Gobiindex.c_str());
               Tmp.WriteString("TEMP", "gobiindex", Gobiindex.c_str());
           }
          /* if ( (pCChar = TmpLib.ReadString(tmp.c_str(), "cell_reg_timeout", "")) != ""){
                     Tmp.WriteString("TEMP", "cell_reg_timeout", pCChar);
                  //               Tmp.WriteString(tmp.c_str(), "cell_reg_timeout", pCChar);
            }*/
       }
}
       Tmp.WriteInt("TEMP", "NumCons" ,NumCon);            // This value is saved so as to not need to count the
                                                   // number of wireless sections again (like above)
       TmpLib.CloseIniFile();

       // Now create the priority section for tmp.ini
       string line;
       string csTemp, csTempUpper;
       BOOL bPrioritySection;

       // open input file
       //ifstream in("/usr/sbin/http/cgi-bin/SvContent/titanlib.conf");
       ifstream in("/mnt/flash/config/conf/titanlib.conf");

       bPrioritySection = FALSE;
       // We're done with NumCon so use it
       NumCon = 1;

       while( getline(in,line) )    {
           csTemp = line.c_str();
           csTempUpper = csTemp;
           std::transform(csTempUpper.begin(), csTempUpper.end(), csTempUpper.begin(), ::toupper);

           if(!bPrioritySection)   {
               if(csTempUpper == "[PRIORITY]")
                   bPrioritySection = TRUE;
           }  else    {
               // If we entered a new section (i.e. not PRIORITY)
               if(csTemp.find("[", 0) != -1)
                   break;
               // If it was not a blank line
               if(csTemp.c_str() != "")
                   Tmp.WriteString("PRIORITY", itoa(NumCon++, 10), csTemp.c_str());
           }
       }
       in.close();

       opt_byte = sys_options & 0x07;
       // expect that an org_model_name file exists since it should have been created previously
       applog.log("Checking if feature activation is set...");
       if ( FileExists(FEATURE_PENDING_FLAG) && check_for_feature_activation(sys_options)) {
             /* TODO maybe: set GeyID1 and perhaps other fields based on its previous setting and the new options
             tmp = Tmp.ReadString("TEMP","GwyID1","");
             if (tmp == "Wifi" || tmp == "carrier") {
                 if ((opt_byte & 0x02) == 0) {   // no cellular
                    if (tmp == "carrier") {
                        tmp = "Wifi";
                    }
                 } else  { //if ((opt_byte & 0x04) == 0) { // no wifi
                      tmp = "carrier";
                 }
             }

             */
             if ( FileExists("/tmp/feature_activated") ){
                 applog.log("Detected feature activation with a current gateway config");
                 if ( FileExists("/mnt/flash/config/conf/gateway.conf") ){
                      modify_gateway_model_number();
                 }
                 modify_tmp_ini_def();
             }
       }
       Tmp.WriteString("TEMP", "DIRTY", "NO");

   } else  {
        applog.log("no config found, create defaults and copy to tmp.ini.def...");
        defaults_enabled = true;
      // The files don't exist so just start of using the Default.ini file
      system("cp /usr/sbin/http/cgi-bin/SvContent/Default.ini /tmp/tmp.ini");

      /* build new tmp.ini from scratch using Default.ini, etc   */
      Tmp.OpenIniFile("/tmp/tmp.ini");
      Tmp.WriteString("TEMP","DIRTY","NO");

      if(!FileExists((char *)&"/tmp/nand_unlocked"))
         system("nand_unlock MTD6 MTD8 0"); // unlock MTD6

      if (!FileExists("/mnt/flash/config/conf/configstate.ini"))
          system("cp /usr/sbin/http/Default_configstate.ini /mnt/flash/config/conf/configstate.ini");
      // copy auto flags state into tmp.ini for use in Basic Page, Export, etc
      Config.OpenIniFile("/mnt/flash/config/conf/configstate.ini");
      Tmp.WriteString("TEMP", "autoCarrier", Config.ReadString("auto","autoCarrier", "YES"));
      Tmp.WriteString("TEMP", "autoCellTellnum", Config.ReadString("auto","autoCellTellnum", "YES"));
      Tmp.WriteString("TEMP", "autoCarrierValue", Config.ReadString("auto","carrierValue", "carrier"));
      Tmp.WriteString("TEMP", "autoTellNumValue", Config.ReadString("auto","tellnumValue", "unknown"));
      Tmp.WriteString("TEMP", "public_safety_enabled", "NO");
      Config.CloseIniFile();

      sprintf(debug,"final options: %d", sys_options);
      applog.log(debug);

      opt_byte = sys_options & 0x07;
      // determine the model number of this device based on option flags and cell type, Note: cell_type_index = 0 = Wifi only
      buff[0] = '\0';
      determine_model_number(cell_type_index, hw_opts, &buff[0]);
      if( buff[0] == '\0') {
          Tmp.WriteString("TEMP", "GwyID2","unknown");
      } else
          Tmp.WriteString("TEMP", "GwyID2", buff);


      if ((sys_options & 0x02) == 0) {  // if not cellular, assume Wifi
           Tmp.WriteString("TEMP", "GwyID1", "Wifi");
           Tmp.WriteString("TEMP", "TeleNum", "n/a");
           Tmp.WriteString("TEMP", "autoCarrierValue", "Wifi");
           Tmp.WriteString("TEMP", "autoTellNumValue","n/a");
           Tmp.WriteString("TEMP", "autoCarrier", "NO");
      } else { // cellular supported
           Tmp.WriteString("TEMP", "GwyID1", "carrier");
           Tmp.WriteString("TEMP", "TeleNum", "unknown");
      }

      // get SN from mtd set in tmp.ini  GwyID3
      Tmp.WriteString("TEMP", "GwyID3", titan_sn);
   }

   // check if country code set in eeprom, if so read in current country coded
   chrp = read_config(CFG_MEM_COUNTRY_CODE, "-n" );
   if (chrp[0] == 0 || chrp == NULL || strstr(chrp ,"ERROR") != NULL)
      ;
   else {
       applog.log("Found countrycode in eeprom config memory");
       Tmp.WriteString("TEMP", "countrycode", chrp);
   }


   if( cell_type_index) {   // we have cellular support set cell_type in tmp.ini
        if (cell_type_index == 1){
           applog.log("Found a PXS8 Radio");
           Tmp.WriteString("TEMP", "cell_type", "PXS8");
        } else if (cell_type_index == 2){
            applog.log("Found a PLS8 Radio");
            Tmp.WriteString("TEMP", "cell_type", "PLS8");
        } else if (cell_type_index == 3){
            applog.log("Found a PLS62-W Radio");
            Tmp.WriteString("TEMP", "cell_type", "PLS62_W");
        }else if (cell_type_index == 4){
            applog.log("Found a ELS31-V Radio");
            Tmp.WriteString("TEMP", "cell_type", "ELS31_V");
        }else if (cell_type_index == 5){
            applog.log("Found a EC25-AF Radio");
            Tmp.WriteString("TEMP", "cell_type", "EC25_AF");
        }else if(cell_type_index == 6){
            applog.log("Found a EC25-G Radio");
            Tmp.WriteString("TEMP", "cell_type", "EC25_G");           
        }else {  //TODO support other 4G modem types
            cell_type_index = 2;
            applog.log("Found unsupported cellular module type %d, default to PLS8",i);
            Tmp.WriteString("TEMP", "cell_type", "PLS8");
        }
    }

   applog.log("Checking for Audio support...");
   if( CheckForAudio() ) {
      applog.log("Read in Audio parameters from /mnt/flash/audio/titanaudio.conf");
      if ( !TmpAudio.OpenIniFile("/mnt/flash/audio/titanaudio.conf") ){
          debuglog(LOG_INFO, "WARNING: Failed to open /mnt/flash/audio/titanaudio.conf!");
          applog.log("Failed to open /mnt/flash/audio/titanaudio.conf, use canned defaults");
      }
      keyname = TmpAudio.ReadString("AUDIO", "samplerate", "11");
      Tmp.WriteString("TEMP", "samplerate", keyname.c_str());
      keyname = TmpAudio.ReadString("AUDIO", "ID3v1size", "128");
      Tmp.WriteString("TEMP", "ID3v1size", keyname.c_str());
      keyname = TmpAudio.ReadString("AUDIO", "incidenttime", "90");
      Tmp.WriteString("TEMP", "incidenttime", keyname.c_str());
      keyname = TmpAudio.ReadString("AUDIO", "preferredTO","1");
      Tmp.WriteString("TEMP", "preferredTO", keyname.c_str());
      TmpAudio.CloseIniFile();
   } else
      applog.log("Audio feature is disabled!");

   set_wifi_ap_options(sys_options);

   if (FileExists(REGINFO_FILE) &&  getFilesize(REGINFO_FILE) > 0)
      Tmp.WriteString("TEMP", "registration_info", "YES");
   else
      Tmp.WriteString("TEMP", "registration_info", "NO");

   Tmp.WriteIniFile("/tmp/tmp.ini");

   if (defaults_enabled){
       applog.log("defaults enabled");
       Tmp.WriteString("TEMP", "public_safety_enabled", "NO");
       /* save copy of tmp.ini defaults for use in restore to factory settings*/
       Tmp.WriteString("TEMP", "DIRTY", "YES");
       Tmp.WriteString("TEMP", "DEFAULT", "RESTORE");
       Tmp.WriteIniFile("/mnt/flash/config/conf/tmp.ini.def");
       system("sync");
       if(!FileExists((char *)&"/tmp/nand_unlocked"))
          system("nand_unlock MTD0 MTD6 1 > /dev/null 2>&1"); // lock MTD6
   }

  Tmp.CloseIniFile();
  system("sync");
  printf("Content-Type: text/html\n\n");
  return 0;

}

