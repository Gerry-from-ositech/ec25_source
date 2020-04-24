/*
 * This file contains proprietary information and is subject to the terms and
 * conditions defined in file 'OSILICENSE.txt', which is part of this source
 * code package.
 */

/* This CGI script is used to configure New cellular connections and also to edit
 * existing cellular connections. The GUI component is located in /usr/sbin/http/cgi/CON_Cell.cgi
 * which is sent upline to the browser.
 *
 * This file is called from the CON_Con.cgi page when the user selects [Create New Cellular] button.
 * This is done via a HTTP POST request when user selects to create or edit a Cellular connection
 */
#include <cstdlib>
#include <stdio.h>
#include <string.h>
#include "../../../util/inifile.h"
#include "../../../util/utils.h"
 #include "../../nav_page/page.h"
#include <unistd.h>

using namespace std;

//char debugbuff[200];
//Logger  applog("CON_Cell");

char buff[200];

/***********************************************************************
*
* Description: Get the connection name from HTTP request data
*
* Calling Arguments:
* Name               Mode      Description
* name               IN        the name of the key to be used to get the connection name
*                              in this case it is "conn_name" value from CON_Con.cgi
*
* Return Value:
*    Type         Description
* char Pointer    to global buff holding the connection name
*
******************************************************************************/
char * GetConName(char *name) {
char* pszContentLength;
string  s;
int nameLen;
char *p;
int ContentLength;

//applog.log("Parse URL...");

nameLen = strlen(name);

pszContentLength = getenv("CONTENT_LENGTH");
if(pszContentLength == NULL)
  return NULL;
ContentLength = atoi(pszContentLength);
if(ContentLength > sizeof(buff))
  return NULL;

if(fread(buff, 1, ContentLength, stdin) == 0)
  return NULL;

if(ferror(stdin)) // die if there was an fread error
  return NULL;
buff[ContentLength] = '\0';

//sprintf(debugbuff, "Content length = %d, data = %s", ContentLength, buff);
//applog.log(debugbuff);
s = buff;
s = DecodeURL(s);

//applog.log((char *)s.c_str());

if (s.find(name) == 0 ) {
//applog.log("Found conn_name in data!");
       s = s.substr (nameLen+1);
       sprintf(buff, "%s", s.c_str());
}
else
      buff[0] = '\0';

//applog.log( buff);
return buff;
}

/*
   Escape certain chars by perpending a '\'
   this is after fetching the string from tmp.ini
   and before sending it to browser for display in
   edit box.

   Chars to be perpended include \ and '
*/
string EscapeStrChrs(string inStr) {
int i;
char ch;

string buffer = "";

  for (i = 0; i < inStr.length(); i++) {
     ch = inStr.at(i);
     if (ch == '\\')
         buffer.append("\\");
     else if( ch == '\'')
         buffer.append("\\");

     buffer += ch;

  }

  return buffer;
}


//Logger debug("ConCell");

int main() {
 string s;
 string sTemp;
 CIniFile iniFile;
 char con_name[100];
 char *pch;
 CCHR  *pCChar;
 int i, length;
 bool isGobi = false;
 FILE *fp;
 size_t size;

   con_name[0] = '\0';
   pch = GetConName("conn_name");
   if (pch != NULL)
      strcpy(con_name, pch);

   iniFile.OpenIniFile(TEMP_INI);
   s = iniFile.ReadString("TEMP", "DIRTY", "NO");

   printf("Content-Type: text/html\n\n");
   Header(PG_CON_CELL);
   if(s.find("YES") != -1)
       Sidebar(PG_CON_CELL, true);
   else
       Sidebar(PG_CON_CELL, false);

   printf("<script type='text/javascript' language='javascript'>\n");

//   printf("var CellProviderPresets = [['Verizon','','','',8],['AT & T','isp@cingular.com','cingular1','isp.cingular',1],['Rogers','wapuser1','wap','internet.com',0]];\n");
//   printf("var CellProviderPresets = [['Generic','','','',0],['Verizon','','','',8],['AT & T','','','m2m.com.attz',1],['Sprint','','','internet.com',7],['T-Mobile','','','epo.tmobile.com',4],['Vodafone','','','',6],['Telefonica','','','',2],['Telecom Italia','','','',3],['Orange','','','',5],['Bell Mobility','','','inet.bell.ca',0],['Telus','','','sp.telus.com',0],['Rogers','wapuser1','wap','internet.com',0],['Fido','fido','fido','internet.fido.ca',0]];\n");
//  printf("var CellProviderPresets = [['AT&T','isp@cingular.com','cingular1','isp.cingular',1],['AT&T(Kore)','','','c2.korem2m.com',1],['Generic UMTS','','','required.apn.string',0], ['Verizon','','','',8],['Sprint','','','',7]];\n");
//TODO: add the APN strings for LTE PLS8 for Verizon and Sprint
   s = iniFile.ReadString("TEMP", "cell_type", "NO");

  if (s== "NO" || s == "PXS8"){
     /* for 3G which may be sold outside North America:
        If eeprom counytrycode = US or CA or Blank - show full list of provideres
        else show only one provider
     */

     if ( FileExists("/usr/sbin/http/cgi-bin/enable_sprint") )
        printf("var CellProviderPresets = [  ['Verizon','','','',8], ['Verizon (KORE)','','','',8] , ['Sprint','','','',7], ['AT&T','','','isp.cingular',1], ['AT&T (KORE)','','','c2.korem2m.com',1], ['Other (GSM/UMTS)','','','REQUIRES APN STRING',0] ];\n");
     else {
      sTemp = read_config(CFG_MEM_COUNTRY_CODE, "-n" );
      if ( strstr(sTemp.c_str() ,"ERROR") != NULL || (sTemp.length() != 0 && sTemp != "US" &&  sTemp != "CA"))
         printf("var CellProviderPresets = [  ['Generic (GSM/UMTS)','','','REQUIRES APN STRING',0] ];\n");
      else
         printf("var CellProviderPresets = [  ['Verizon','','','',8], ['AT&T','','','isp.cingular',1],  ['Rogers','','','internet.com',0], ['Other (GSM/UMTS)','','','REQUIRES APN STRING',0] ];\n");
     }
  } else if(s == "ELS31_V"){
       printf("var CellProviderPresets = [  ['Verizon','','','',8] ];\n");

  } else if(s == "PLS62_W" || s == "EC25_G"){
       //printf("var CellProviderPresets = [  ['AT&T','','','broadband',1],  ['Bell','','','inet.bell.ca',0], ['Other (LTE)','','','REQUIRES APN STRING',0] ];\n");
    printf("var CellProviderPresets = [  ['LTE','','','REQUIRES APN STRING',0] ];\n");
  } else  if ( s == "EC25_AF") {
     if ( FileExists("/usr/sbin/http/cgi-bin/enable_sprint") )
        printf("var CellProviderPresets = [  ['Verizon','','','vzwinternet',8],   ['Verizon (KORE)','','','vzwinternet',8] , ['Sprint','','','',7], ['AT&T','','','i2gold',1], ['AT&T (   KORE)','','','i2gold',1], ['Other (LTE)','','','REQUIRES APN STRING',0] ];\n");
     else
        //  printf("var CellProviderPresets = [  ['Verizon','','','vzwinternet',8], ['Verizon (KORE)','','','wyleslte.gw7.vzwentp',8], ['AT&T','','','broadband',1], ['AT&T (KORE)','','','10569.mcs',1],  ['Bell','','','inet.bell.ca',0], ['Rogers','','','ltemobile.apn',0],['Other (LTE)','','','REQUIRES APN STRING',0] ];\n");
        printf("var CellProviderPresets = [  ['Verizon','','','vzwinternet',8], ['Verizon (Public Safety)','','','REQUIRES APN STRING',8], ['AT&T','','','broadband',1], ['Bell','','','inet.bell.ca',0],  ['Rogers','','','ltemobile.apn',0], ['Telus','','','isp.telus.com',0], ['Other (LTE)','','','REQUIRES APN STRING',0] ];\n");

  } else  {
     if ( FileExists("/usr/sbin/http/cgi-bin/enable_sprint") )
        printf("var CellProviderPresets = [  ['Verizon','','','vzwinternet',8],   ['Verizon (KORE)','','','vzwinternet',8] , ['Sprint','','','',7], ['AT&T','','','i2gold',1], ['AT&T (   KORE)','','','i2gold',1], ['Other (LTE)','','','REQUIRES APN STRING',0] ];\n");
     else
      //  printf("var CellProviderPresets = [  ['Verizon','','','vzwinternet',8], ['Verizon (KORE)','','','wyleslte.gw7.vzwentp',8], ['AT&T','','','broadband',1], ['AT&T (KORE)','','','10569.mcs',1],  ['Bell','','','inet.bell.ca',0], ['Rogers','','','ltemobile.apn',0],['Other (LTE)','','','REQUIRES APN STRING',0] ];\n");
      printf("var CellProviderPresets = [  ['Verizon','','','vzwinternet',8], ['Verizon (Public Safety)','','','REQUIRES APN STRING',8], ['AT&T','','','broadband',1], ['Bell','','','inet.bell.ca',0],  ['Rogers','','','ltemobile.apn',0], ['Telus','','','isp.telus.com',0], ['Other (LTE)','','','REQUIRES APN STRING',0] ];\n");
  }

   printf("var cell_type = '%s';\n", s.c_str());

   if (con_name[0]  != '\0' ) {
       printf("var title = 'Edit Cellular Connection:';\n");
   } else
       printf("var title = 'Create New Cellular Connection';");

    sTemp  = iniFile.ReadString(con_name,"backhaul","YES");
    printf("var backhaul = '%s';\n", sTemp.c_str());



//  if (s == "PLS8")
//       printf("var cell_type = 'PLS8';\n");
//  else if (s == "PXS8")
//      printf("var cell_type = 'PXS8';\n");
//  else if( s == "ELS31_V")
//       printf("var cell_type = 'ELS31_V';\n")


  if (con_name[0]  != '\0' ) {  // this is an edit operation of existing connection
       printf("var edit_mode = true;\n");
       s = EscapeStr(con_name);
       printf("var con_name = '%s';\n", s.c_str());

      sTemp = iniFile.ReadString(con_name,"CellUser","");
      sTemp = EscapeStrChrs(sTemp);
      printf("var cell_user = '%s';\n", sTemp.c_str());

      sTemp = iniFile.ReadString(con_name,"CellPass","");
      // printf("var cell_pass = '%s';\n", EscapeStr(sTemp).c_str());
      if (sTemp != "") {
         /* convert from hex ascii to binary
          * then decrupt and use cleartext in Key input
          */
         length = sTemp.length()/2;
         memset(buff, 0, sizeof (buff));
         for(i = 0; i < length * 2; i += 2) {
             buff[i/2] = strtol(sTemp.substr(i, 2).c_str(), NULL, 16);
         }
         sTemp = encrypt_decrypt(buff, length, 0);
         replaceAll(sTemp, "\"", "&#34;");
         replaceAll(sTemp, "'", "&#39;");
         replaceAll(sTemp, "\\", "&#92;");

         printf("var cell_pass = '%s';\n", sTemp.c_str());
      } else
         printf("var cell_pass = '';\n");

      sTemp = iniFile.ReadString(con_name,"APN","");
      printf("var APN = '%s';\n", EscapeStr(sTemp).c_str());


      pCChar = iniFile.ReadString(con_name,"auth","3");
      if (strcmp(pCChar,"1") == 0)
         printf("var auth_type = 'PAP';\n");
      else if (strcmp(pCChar,"2") == 0)
         printf("var auth_type = 'CHAP';\n");
      else
         printf("var auth_type = 'PAP+CHAP';\n");
      //  i = ReadCurrentImageIndex(); for Phsio the connection def has a gobiindex field
      sTemp = iniFile.ReadString(con_name,"gobiindex","0");
      printf("var gobiindex= '%s';\n",  sTemp.c_str());


   } else {   // this is a create new connection operation
        printf("var edit_mode = false;\n");
        //system("rm /etc/titanlib/gobiindex");
   }

      sTemp = iniFile.ReadString(con_name,"DiffServ","NO");
      if (sTemp != "NO"){
            printf("var safety_type = '%s';\n", sTemp.c_str());
      } else {
            printf("var safety_type = 'None (00)';\n");
            iniFile.WriteString(con_name,"DiffServ","None (00)");
      }

      sTemp = iniFile.ReadString(con_name,"bandwidth_limit","NO");
      if (sTemp != "NO")
          printf("var bandwidth_limit = '%s';\n", sTemp.c_str());
      else {
          printf("var bandwidth_limit = 'None';\n");
          iniFile.WriteString(con_name,"bandwidth_limit","None");
      }

   //sTemp = iniFile.ReadString("TEMP","ps_custom","00");
   //printf("var ps_custom = '%s';\n", sTemp.c_str());


   sTemp = iniFile.ReadString("TEMP","cell_reg_timeout","60");
   printf("var reg_to = %s;\n", sTemp.c_str());
   sTemp = iniFile.ReadString("TEMP","cell_act_timeout","60");
   printf("var act_to = %s;\n", sTemp.c_str());
   sTemp = iniFile.ReadString("TEMP","public_safety_enabled","NO");
   printf("var public_safety_enabled = '%s';\n", sTemp.c_str());


   printf("</script>");

   fp = popen( "cat ../cgi/CON_Cell.cgi", "r");
   if (fp != NULL) {
      while ( fgets(buff, sizeof(buff), fp) )
         printf(buff);
      pclose(fp);
   }
   else
       printf("Failed to access cgi file!\n");

   Footer();
   return 0;
}

