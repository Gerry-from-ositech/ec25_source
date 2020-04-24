<script type="text/javascript" src="../js/AjaxRequest.js"></script>
<script type="text/javascript" src="../js/AnimateWaitBar.js"></script>
<script type="text/javascript" src="../js/htmlCoding.js"></script>

<style>
#select_opts {
   all: revert;
}
#setvalue {
   all: revert;
}
#DIFFSERV{
    white-space: nowrap;
    overflow-x: auto;
    overflow-y: hidden;
}
#errPublicSafety{
    all: revert;
      box-sizing: border-box;
      position: relative;
      /*height: 1.5em;*/
}
.editdropdown {
    all: revert;
    display: inline-block;
    position: relative;
    width: 90px;
}
.editdropdown select
{
  all: revert;
   width: 130px;
  display: inline-block;

}
.editdropdown > * {
  all: revert;
    box-sizing: border-box;
    height: 1.5em;
}
.editdropdown select {
    all: revert;
}
.editdropdown input {
    all: revert;
    position: absolute;
    width: 110px;
}

</style>

<script type="text/javascript">
 function trimTrailingSpaces(instr) {

     for(var i=instr.length-1; i >= 0; i--){
          if( instr.charAt(i)  != ' ') {
                instr = instr.substring(0,i+1);
                break;
          }
     }
     return  instr ;
}

 function textLimit(field, maxlen)  {
       if (field.value.length > maxlen)
           field.value = field.value.substring(0, maxlen);
}

function keyUp(el, evt) {
     if ((evt.keyCode||event.which) != 9){  // skip Tab key
        document.getElementById('Save').disabled=false;
        dirty = true;

        if(evt.type === 'paste') {
            if (evt.preventDefault)
                evt.preventDefault();
            evt.returnValue=false;

            // Get pasted data via clipboard API
            var clipboardData = evt.clipboardData || window.clipboardData;
            var pastedData = clipboardData.getData('Text');
            el.value = pastedData;
        }


        if (el.id == 'CellUser') {
              textLimit(el, 64);
              document.getElementById("auth").disabled=false;
        } else if (el.id == 'ConName')
           textLimit(el, 48);
        else
           textLimit(el, 64);
     }
     var user = document.getElementById("CellUser");
     var pass  =  document.getElementById("CellPass");
     if (user.value.length == 0) {
              document.getElementById("auth").disabled=true;
              pass .value = '';
     }

}

function enableSave() {
   document.getElementById('Save').disabled=false;
   dirty=true;

}

function CancelCon()  {
       dirty=false;
       location.href='CON_Con';
}

function APNautoToggle( autoCheck)  {

  if (autoCheck.checked) {
        document.getElementById('APN').value = '';
        document.getElementById('APN').disabled= true;
  } else
      document.getElementById('APN').disabled= false;
  dirty = true;
  document.getElementById('Save').disabled=false;

}

function OnChangeAuth( authSelect)  {
    if (authSelect.selectedIndex != -1 && authSelect.selectedIndex != orgAuthIndex) {
         dirty = true;
         document.getElementById('Save').disabled=false;
    }
}



// select an area in a text input field
function createSelection(start, end, field) {
   if ( field.createTextRange ) {
      var newend = end - start;
      var selRange = field.createTextRange();
      selRange.collapse(true);
      selRange.moveStart('character', start);
      selRange.moveEnd('character', newend);
      selRange.select();
   }
       /* For the other browsers */
   else if( field.setSelectionRange ) {
       field.setSelectionRange(start, end);
   }
}

var bInvalidChars, bInvalidPage;

function ExitPage(url) {
    if (checkNavEnable())
         return ValidatePage(url)
    else
         return false;
}

// This fuction will check for invalid characters
// If any invalid character is detected, it will message the user and do nothing
// Otherwise, all other characters will be encoded in the form which SvTemp.exe
// will decode before saving and then continue to call ChgText()
function ValidatePage(url) {

   // Set global var to determine if navigation should be cancelled
   bInvalidPage = false;

  if(!dirty)  return true;

   // if user wants to move away to anther page
    if (url != 'null' && url != 'CON_Con') {
       alert('Unsaved change(s) have been made to this Page!\r\nUse the Save or Cancel button before attempting\r\nto move to another Page.');
       return false;
  }

  // Connection Name***************************************************************************
  bInvalidChars = false;
  var txt = document.form1.ConName.value;

  // first char of connection name cannot be ';'
  if (txt.charAt(0) === ';' ) {
         bInvalidChars = true;
  } else {

        // Check for general invalid characters
        if (!CheckForInvalidCharacters(txt))                           // general
            bInvalidChars = true;
        // The connection name cannot be blank either
        if (txt.length == 0  || txt.length > 48)   {
             bInvalidChars = true;
        }
        // Do not allow embedded spaces, changing priorities will break - depricated
        if(txt.indexOf(' ') != -1)                                     // spaces
        {
            bInvalidChars = true;
        }

  }

  if(bInvalidChars) {
        document.getElementById('errConName').style.display='inline';
        bInvalidPage = true;
  } else {
        document.getElementById('errConName').style.display='none';
  }

   // sanitze fields by removing trailing spaces if any?
    document.form1.CellUser.value = trimTrailingSpaces(document.form1.CellUser.value);
    document.form1.CellPass.value = trimTrailingSpaces(document.form1.CellPass.value);
    document.form1.APN.value = trimTrailingSpaces(document.form1.APN.value);

  var val;
  var i;
  // check username and passowrd fields for Digit prompt 'x-DigitNo'
  // if found flag field as error and show alert and exit with false
  // otherwise exit with true   id=  CellUser, CellPass
  val = document.getElementById('CellPass').value;
  if (val.length == 0) {
    // check that if celluser is blank then password should also be blank
    if (document.getElementById('CellPass').value.length != 0){
       bInvalidPage = true;
       document.getElementById('errPass').style.display='inline';
    } else
       document.getElementById('errPass').style.display='none';

 }

  // check if apn is required
  val = document.getElementById('APN').value;
  if ( val.length == 0 && document.getElementById('GobiSelect').value < 7 &&  document.getElementById('APNauto').checked == false  ) {
     bInvalidPage = true;
     document.getElementById('errApn').style.display='inline';
  } else {
        document.getElementById('errApn').style.display='none';
  }

  if ( val.indexOf("REQUIRES APN")  != -1) {
     bInvalidPage = true;
     document.getElementById('errApn').style.display='inline';
  } else {
     document.getElementById('errApn').style.display='none';
  }

  // check if Public Safety is activate for PLS8 or PXS8, if not validate DiffServ entry
  if (cell_type != 'PLS62_W' && document.getElementById('PublicSafety').style.display == 'none'){
    var re = /^[0-9a-fA-F]+$/;
    var val;
    var x = document.getElementById("select_opts");
    var elem = document.getElementById("selvalue");
    val = elem.value;
    var pick_found = false;

    // check for match in pick list
    for (var i =0; i < x.options.length; i++){
        if (val == x.options[i].text){
            pick_found = true;
            break;
        }
    }

    if (pick_found == false){
        // the value is not from the pick list, assume hex input
        bInvalidChars = false;
        if ( val.indexOf("0x") == 0 )
            val = val.substring(2);
        if (val.length != 2)
              bInvalidChars = true;
        else if (re.test(val) == false)
             bInvalidChars = true;
        else{
            // check that bottom two bits are zero
            var v = parseInt(val, 16);
            var x = v & 3;
            if (x != 0)
               bInvalidChars = true;
            else
               elem.value = val.toUpperCase();
        }if (bInvalidChars){
           //document.getElementById('title').style.display='none';
           //document.getElementById('errPublicSafety').style.display='block';
           bInvalidPage = true;
           alert("Invalid data entered for DiffServ. Must be two Hex chars. Least Significant two bits must be zeros. Valid range is from 00 to FC.");
            document.getElementById('selvalue').focus();
           return false;
        }
    }
    document.getElementById('DiffServ').value = elem.value;


  } else if (cell_type != 'PLS62_W') {
      var e = document.getElementById("pubsafety");
      document.getElementById('DiffServ').value = e.options[e.selectedIndex].value;
      document.getElementById('bandwidth_limit').value = '2 Mbps';  // hardcoded value
  } else {
      // hardcoded values for PLS62_W
      document.getElementById('DiffServ').value = 'None (00)';
      document.getElementById('bandwidth_limit').value = 'None';
  }

  if(document.form1.check_largeupload.checked == true)
        document.form1.largeupload.value='YES';
  else
       document.form1.largeupload.value='NO';

 if( document.form1.check_backhaul.checked == 1)
        document.form1.backhaul.value='YES';
 else
       document.form1.backhaul.value='NO';

 if (edit_mode == false) {
    val =  document.getElementById('GobiSelect').value;
    if (val.length == 0) {
           document.getElementById('title').style.display='none';
           document.getElementById('noSelecterror').style.display='block';
           bInvalidPage = true;
     } else {
              document.getElementById('title').style.display='block';
              document.getElementById('noSelecterror').style.display='none';
    }
}

// If any field was invalid, do not submit this form
  if(bInvalidPage)  {
        document.getElementById('title').style.display='none';
        document.getElementById('error').style.display='block';
        window.location='#';
        dirty = true;
        // cancel form submit
        alert('Some of your changes are invalid.')
        return false;
  } else {
        document.getElementById('title').style.display='block';
        document.getElementById('error').style.display='none';
  }
  // If any field was invalid, do not submit this form
  if(bInvalidPage)  {
      document.getElementById('title').style.display='none';
      document.getElementById('error').style.display='block';
      if (url != 'null' && url == 'CON_Con') return true;
      window.location='#';
      dirty = true;
      var answer = confirm('Are you sure you want to navigate away?')
      if(answer)
          return true;
      else
         return false;
  } else {
         document.getElementById('title').style.display='block';
         document.getElementById('error').style.display='none';
  }

 // if (gobi_index != -1) {
  if( edit_mode == false){
       SelectCellProvider(gobi_index, provname);
       return false;
  }


  // If success, call this
  document.form1.action="SvContent/SvCon";
  //document.form1.url = url;
  document.getElementById('url').value = url;
  document.form1.submit();

  dirty = false;

}

function FinishRadioSetup()  {
   document.form1.action='SvContent/SvCon';
   document.getElementById('url').value = 'CON_Con';
   document.form1.submit();
   dirty = false;

}

function CheckForInvalidCharacters(txt) {
    //alert(txt);
    if (txt.indexOf('=')!=-1)          // =
    {
          bInvalidChars = true;
          return false;
    }
    if (txt.indexOf('[')!=-1)          // [
    {
          bInvalidChars = true;
          return false;
    }
    if (txt.indexOf(']')!=-1)          // ]
    {
          bInvalidChars = true;
          return false;
    }
    if (txt.indexOf("'")!=-1)    // single quote
    {
        bInvalidChars = true;
        return false;
    }
    if (txt.indexOf("\"")!=-1)    // double quote
    {
        bInvalidChars = true;
        return false;
    }
    if (txt.indexOf("\\")!=-1)    // backslash
    {
        bInvalidChars = true;
        return false;
    }
    if (txt.indexOf("%%")!=-1)
    {
       bInvalidChars = true;
       return false;
    }

    return true ;
}


function populate(presetList) {
    var x  = 1;
    if (document.form1.ConName.value.length == 0){
        // use the Provider name presetListObject[0] from the json list as the connection name
        // with all blanks removed
       document.form1.ConName.value = presetList[0].replace(/ /g,'');
    }else {
           // if con name is a default provider name replace it with the new one
          var current_name = document.form1.ConName.value;
          for (var i = 0; i < CellProviderPresets.length; i++) {
              if (current_name == CellProviderPresets[i][0].replace(/ /g,'') ){
                 document.form1.ConName.value = presetList[0].replace(/ /g,'');
                 break;
              }
          }
    }
    // populate the other fields
    document.form1.CellUser.value = presetList[1];
    document.form1.CellPass.value = presetList[2];
    document.form1.APN.value = presetList[3];
    document.getElementById('GobiSelect').value = presetList[4].toString();
    document.getElementById('APN').disabled = false;
//    document.getElementById('APNauto').checked = false;
//    if(presetList[3].indexOf("REQUIRES APN") != -1)
//      document.getElementById('APNauto').disabled = true;
//    else
//      document.getElementById('APNauto').disabled = false;
}

var gobi_index = -1;
function presetSelection() {
var selectObj = document.getElementById('PresetList');
var idx = selectObj.selectedIndex;
var i;
   for(i=0;i<inputs.length;i++){
          if( edit_mode == false && inputs[i].id === "ConNameOrg") continue;
          inputs[i].disabled=false;
   }

   dirty = true;
//   document.getElementById('Save').disabled=false;
   gobi_index = CellProviderPresets[idx][4];
   provname = CellProviderPresets[idx][0];

    if ( provname.indexOf("Safety") != -1){
       document.getElementById('public_safety_enabled').value = 'YES';
       document.getElementById("DIFFSERV").style.display = "none";
       document.getElementById("PublicSafety").style.display = "block";
       document.getElementById("BandwidthLimit").style.display = "none";
       document.getElementById("bandwidth_limit").value = "2 Mbps";
   }else {
       document.getElementById('public_safety_enabled').value = 'NO';
       document.getElementById("selvalue").disabled = false;
       document.getElementById("PublicSafety").style.display = "none";
       document.getElementById("DIFFSERV").style.display = "block";
       document.getElementById("bandwidth_limit").value = "None";
       var b = document.getElementById("select_opts");
       var elem = document.getElementById("selvalue");
       b.selectedIndex = 0;
       elem.value = "None (00)";

       document.getElementById("BandwidthLimit").style.display = "block";
   }

   var presetListObject = CellProviderPresets[idx];
   populate(presetListObject);
}

/* HTMLEncode  by using browsers own automatic HTML converter
* required for IE8
*/
function HTMLencode(text) {
   var div = document.createElement('div');
   var text = document.createTextNode(text);
   div.appendChild(text);
   return div.innerHTML;
}

var  providerName='';
    function DoCancel() {
          document.getElementById('asubtitle').innerHTML = "";
          document.getElementById('progress').style.display = 'none';
          document.getElementById('ajaxwin').style.display = 'none';
          document.getElementById('mainContent').style.display = 'block';
          disableNav = false;
    }

function onRequestError(req) {
   alert('Error!\n' +'StatusCode='+req.status +'\nStatusText='+req.statusText+'\nAccessed Url='+req.url+'with Method = ' + req.method + ' and QueryString = ' + req.queryString +'\nContents='+req.responseText);
   DoCancel();
}

function large_upload_click(){

  var cb = document.getElementById('check_largeupload');
  if (cb.checked) {
        // user wants to select checkbox
        var a = confirm("WARNING: Enabling Large Upload over a Cellular connection can greatly increase the data usage \
over cellular. It can also result in undesirable operational results as cellular data throughput \
variations could result in repeated unsuccessful data transfers to the LIFENET server.\n\nTo proceed press OK."

          );
        if (a == false){
           cb.checked = false;
        } else
           enableSave();
  } else
     enableSave();

}

function ReactivateCdma() {
         document.getElementById('atitle').innerHTML = "Activating CDMA Device on " + this.providerName+" Network.";
         document.getElementById('asubtitle').innerHTML = "";
         document.getElementById('runningMsg').innerHTML = "May take up to 5 Minutes.  ";
         document.getElementById('mainContent').style.display = 'none';
         document.getElementById('cdmabuttons').style.visibility = 'hidden';
         document.getElementById('progress').style.display = 'block';
         document.getElementById('ajaxwin').style.display = 'block';
         disableNav = true;
          AjaxRequest.get({'url':'SvContent/cellconfig'
                      ,'parameters': { 'cmd' : 'reactivate,'+gobiindex }
                      ,'onSuccess': function(req) {
                           disableNav = false;
                            var n = req.responseText.indexOf("OK");
                            if ( n != -1 ) {
                                window.location='CON_Con';
                            //    document.form1.action='SvContent/SvCon';
                            //    document.getElementById('url').value = 'CON_Con';
                            //    document.form1.submit();
                            }  else {
                                document.getElementById('progress').style.display = 'none';
                                document.getElementById('atitle').value = "Radio Activation failed!";
                                var str = req.responseText + "\nEnter OK to retry.";
                                var answer = confirm (str);
                                if (answer) {
                                     document.getElementById('atitle').innerHTML = '';
                                     ReactivateCdma();
                                } else {
                                      DoCancel();
                                }
                            }
                       }
                      ,'onError': function(req){ onRequestError(req);  }
          });
   }

    /* Select Cellular Provider based on provIndex, could be UTMS (0...6) or CDMA (7,8)
     * Prompt for and do retry if failed
     * Show appropriate message according to response from UTMS or CDMA flip
     */
    function SelectCellProvider(provIndex, provname){
      imageIndexStr = provIndex.toString();
      this.providerName =  HTMLencode(provname);
      document.getElementById('atitle').innerHTML  =  "Setup Cellular Radio for Provider "+this.providerName;
      var select_to = reg_to + act_to;
      document.getElementById('runningMsg').innerHTML = 'May take up to '+select_to+' seconds for network registration and SIM provisioning if required.';
   //   var to = select_to/60;
   //   if (to == 0)
   //      document.getElementById('runningMsg').innerHTML = 'May take up to '+select_to+' Seconds.';
   //   else {
   //       select_to = select_to - (to * 60)
   //       if( select_to == 0)
   //          document.getElementById('runningMsg').innerHTML = 'May take up to '+to+' Minutes.';
   //       else
   //         document.getElementById('runningMsg').innerHTML = 'May take up to '+to+' Minutes and '+select_to+' seconds.';
   //   }


      document.getElementById('mainContent').style.display = 'none';
      document.getElementById('asubtitle').innerHTML = "";
      document.getElementById('cdmabuttons').style.visibility = 'hidden';
      document.getElementById('progress').style.display = 'block';
      document.getElementById('ajaxwin').style.display = 'block';

      disableNav = true;

      // Response:     // OK - UTMS flip was successful
      // Active - CDMA flip was successful and Radio is activated
      // NotActive - CDMA image flipped OK but Radio not activated, show option buttons to activate or not
      // Fatal Error: - flip failed with reason message show alert to allow cancel only
      // other indicates an Error: - e.g: flip failed with reason message show prompt to allow retry

      if (cell_type === 'PXS8')
           var cgi_cmd = 'select,'+imageIndexStr;
      else
           var cgi_cmd = 'select4g,'+imageIndexStr;

      AjaxRequest.get({'url':'SvContent/cellconfig'
                      ,'parameters': { 'cmd' : cgi_cmd }
                      ,'onSuccess': function(req){
                           document.getElementById('progress').style.display = 'none';
                           if (req.responseText == 'OK' ){
                                document.form1.action='SvContent/SvCon';
                                document.getElementById('url').value = 'CON_Con';
                                document.form1.submit();
                           } else if(req.responseText.substring(0,5) == 'Fatal'){
                                document.getElementById('asubtitle').value = "Cellular Radio Configuration failed!";
                                alert(req.responseText);
                                DoCancel();
                           } else {
                                document.getElementById('asubtitle').value = "Cellular Radio Configuration failed!";
                                var str = req.responseText + "\nEnter OK to retry.";
                                var answer = confirm (str);
                                if (answer) {
                                    // document.getElementById('asubtitle').innerHTML = '';
                                    // SelectCellProvider(provIndex, provname);

               document.form1.action='SvContent/SvCon';
               document.getElementById('url').value = 'CON_Con';
               document.form1.submit();

                                } else {
                                      DoCancel();
                                }
                        }
                  }
                  ,'onError': function(req){ onRequestError(req);  }
      });

   }

function OnDIFFSERVChange(obj){
  if (obj.value == 'Custom') return;

  if (obj.length == 9){
      obj.remove(8)
  }

  obj.previousElementSibling.value=obj.value;
  obj.previousElementSibling.focus();
  enableSave();
}

function OnBandwidthLimitChange(obj){
    document.getElementById('bandwidth_limit').value = obj.value;
    enableSave();
}

</script>

<div id='ajaxwin' style='display:none;margin: 0 20px 0 13em'>
     <h3 id='atitle'></h3>
     <h4 id='asubtitle'></h4>
     <div id='progress' style='display:none;'>
          <span id='runningMsg'>Please Wait &nbsp;&nbsp;&nbsp;</span><br />
          <!-- <img id='waitbar' src='/images/barwait.gif'> -->
           <script type='text/javascript'>
              bar = createBar(250,15,'white',1,'black','blue',75,8,7,'redir()');
           </script>
     </div>
     <div id='cdmabuttons'  style='visibility:hidden'><input type='button' id='btnActivate' value='Activate' onclick='DoActivate()' />
        &nbsp;&nbsp;&nbsp;&nbsp;<input type='button' id='btnCamcel' value='Cancel' onclick='DoCancel()' /></div>
</div>

<div id='mainContent'>
   <div><p><strong id='title'>New Cellular Connection</strong></p></div>
   <div style='display:none' id='error'><p><strong><font color=red>One or more settings you have entered are invalid.  Invalid fields have been marked.  Please review these settings and make changes as necessary and try again.</font></strong></p></div>
   <div style='display:none' id='noSelecterror'><p><strong><font color=red>One Cellular Provider must be selected from the list on the right!</font></strong></p></div>

   <table width='100%%'><tr><td width='45%%' valign='top'>
   <form AUTOCOMPLETE='false' id='form1' name='form1' method='POST' onSubmit='return ValidatePage();'>
   <div align='left'><label for='ConName'>Connection Name: </label>
   <input onkeyUp='keyUp(this, event)' onpaste='keyUp(this, event)' oncut='keyUp(this, event)'  type='text' onChange='dirty=true' name='ConName' id='ConName' value=''><span style='display:none' id='errConName'><font color=red>*</font></span>
   </div><br>
   <input type='hidden' name='ConNameOrg' id='ConNameOrg'>
   <input type='hidden' name='AccThresh' id='AccThresh' value='Poor'>

   <div id='largupload_gui' style='display:none;' >
      <label for='largeupload'>Large Upload: </label><input onClick= 'large_upload_click();' type='checkbox' name='check_largeupload' id='check_largeupload' >
      <input type='hidden' name='largeupload'>
      <span id='backhaul_box'><label for='backhaul'>&nbsp; &nbsp; Backhaul: </label> <input onClick= 'enableSave();' type='checkbox'  name='check_backhaul' id='check_backhaul' checked='checked' ></span>
   </div><br>
   <input type='hidden' name='backhaul' id='backhaul' value='YES'>

   <div align='left' id='DAPN' class='text'><label for='APN'>APN String: </label>
   <input type='text' style='width:180px;' onkeyUp='keyUp(this, event)' onpaste='keyUp(this, event)' oncut='keyUp(this, event)'  onChange='dirty=true' name='APN' id='APN' value=''><span style='display:none' id='errApn'><font color=red>*</font></span>
  <!--  <input type='checkbox' onclick= 'APNautoToggle(this);' name='apn_auto' id='APNauto' ><label>Auto</label>  -->
   </div>


  <div class='text' id='DIFFSERV' style='display:none'  >
     <br />
    <label>DiffServ: </label>
    <div  class="editdropdown" id='editdropdown'>
        <input type="text" id='selvalue' onkeyUp='keyUp(this, event)' />
        <select  id="select_opts" onchange="OnDIFFSERVChange(this);"  >
            <option>None (00)</option>
            <option>CS7 (E0)</option>
            <option>CS6 (C0)</option>
            <option>CS5/EF (A0)</option>
            <option>CS4/AF4x (80)</option>
            <option>CS3/AF3x (60)</option>
            <option>CS2/AF2x (40)</option>
            <option>CS1/AF1x (20)</option>
        </select>
    </div><span style='display:none' id='errPublicSafety'><font color=red>&nbsp;&nbsp;*</font></span>
    </div>
  </div>

  <div class='text' id='PublicSafety' style='display:none' >
   <br />
    <label for='pubsafety'>DiffServ: </label>
        <select  id="pubsafety" onchange="enableSave();" >
           <option>CS3/AF3x (60)</option>
           <option>CS4/AF4x (80)</option>
           <option>CS5/EF (A0)</option>
        </select>
    </div>
    <br />
  </div>


  <div class='text' id='BandwidthLimit' style='display:none' >
    <label for='bandwidth'>Bandwidth Limit: </label>
        <select  id="bandwidth" onchange="OnBandwidthLimitChange(this);" >
           <option value="None">None</option>
           <option value="2 Mbps">2 Mbps</option>
        </select>
    <br />
    <br />
  </div>
  <input type='hidden' name='bandwidth_limit' id='bandwidth_limit' value='None' >



  <input type='hidden' name='public_safety_enabled' id='public_safety_enabled'>
  <input type='hidden' name='DiffServ' id='DiffServ'>

   <div align='left' id='DCellUser' class='text'><label for='CellUser'>Login Username: </label>
   <input type='text' onkeyUp='keyUp(this, event)' onpaste='keyUp(this, event)' oncut='keyUp(this, event)'  onChange='dirty=true' name='CellUser' id='CellUser' value='' />
   </div><br>

   <div align='left' id='DCellPass' class='text'><label for='CellPass'>Login Password: </label>
   <input type='password'  onkeyUp='keyUp(this, event)' onpaste='keyUp(this, event)' oncut='keyUp(this, event)'  onChange='dirty=true' name='CellPass' id='CellPass' value='' /><span style='display:none' id='errPass'><font color=red>*</font></span>
   </div><br>

   <div  class='text'><label for='auth'>Authorization: </label>
        <select name='auth' id='auth' onChange='OnChangeAuth(this)' disabled>
           <option name='optPAP' value='PAP'>PAP</option>
           <option name='optCHAP' value='CHAP'>CHAP</option>
      <!--    <option name='optPAP+CHAP' value='PAP+CHAP' SELECTED>PAP+CHAP</option> -->
        </select>
   </div><br>

   <input type='button' name='Save' id='Save'  disabled='disable' value='Save' onClick="ValidatePage('CON_Con')">
   <input type='button' name='Cancel' id='Cancel' onclick="dirty=false;window.location='CON_Con'" Value='Cancel'>
   <input type='button'   style='display:none;'   name='Reactivate' id='Reactivate' onclick='ReactivateCdma();' Value='Reactivate'>
   <input type='hidden' name='url' id='url'>
   <input type='hidden' name='SelType' id='SelType' value='Cellular'>
   <input type='hidden' name='GobiSelect' id='GobiSelect' value=''>
   </form>
   </td><td>&nbsp;&nbsp;</td><td id='viewCellProvs'  style='padding-left: 50px;padding-right: 50px;vertical-align:text-top;' >
      <div> Select Cellular Provider:<br>
        <select style='width:190px' name='PresetList' id='PresetList' size='8' onchange='presetSelection();'>
        </select>
      </div></td></tr>

   </table>
</div>


<script type='text/javascript' language='javascript'>
document.getElementById('title').innerHTML = title;

var id = document.getElementById('largupload_gui');
id.style.display = 'block';
//document.getElementById('largupload_gui').style.display = 'block';  does not work but above does????

// Populate the provider pick list
var list = document.getElementById('PresetList');
var i = CellProviderPresets.length;
if (i > 12) i = 12;
list.size = i.toString();
if (i == 1){
    document.getElementById('viewCellProvs').style.display='none';
    dirty = true;
    document.getElementById('Save').disabled=false;
    gobi_index = CellProviderPresets[0][4];
    provname = CellProviderPresets[0][0];
    populate(CellProviderPresets[0]);
    document.getElementById('ConName').focus();

} else {
    if( edit_mode == false){
        var inputs=document.getElementsByTagName('input');
        for(i=0;i<inputs.length;i++){
            if (inputs[i].id !== "Cancel")
               inputs[i].disabled=true;
        }
    }
    for(var i = 0; i < CellProviderPresets.length; i++) {
        list.options[i] = new Option(CellProviderPresets[i][0]);
    }
    list.options[0].selected = false;
}

var orgAuthIndex = 2;

document.getElementById('public_safety_enabled').value = public_safety_enabled;


if (edit_mode) {
   document.getElementById('viewCellProvs').style.display='none';
   document.getElementById('ConName').value = unescape(con_name);
   document.getElementById('APN').value = unescape(APN);
   if ( gobiindex < 7 &&  document.getElementById('APN').value == '' ) {
        document.getElementById('APNauto').checked = true;
        document.getElementById('APN').disabled= true;
   }
   if (gobiindex > 6 && cell_type === 'PXS8'){
        document.getElementById('Reactivate').style.display='inline';
   }

   if (cell_user.length != 0) {
      document.getElementById('CellUser').value = htmlCoding.htmlDecode(cell_user);
      document.getElementById('CellPass').value = htmlCoding.htmlDecode(cell_pass);
      document.getElementById("auth").disabled=false;
      document.getElementById('auth').value = auth_type;
      var sel = document.getElementById('auth');
      for(var i, j = 0; i = sel.options[j]; j++) {
        if(i.value == auth_type) {
            sel.selectedIndex = j;
            orgAuthIndex = j;
            break;
        }
      }
   }

   if (public_safety_enabled == "YES"){
        document.getElementById("PublicSafety").style.display = "block";
        // document.getElementById('pubsafety').value = safety_type; Fails in IE but not other browseres !!!!!!!!

        var elps = document.getElementById('pubsafety');
        var options = elps.options;
        for (var i = 0; i < options.length; i++) {
            if (options[i].value == safety_type) {
                elps.selectedIndex = i;
            }
        }
   } else if ( cell_type != 'PLS62_W'){
        document.getElementById("selvalue").disabled = false;
        document.getElementById("DIFFSERV").style.display = "block";
        document.getElementById("selvalue").value = safety_type;
        document.getElementById("BandwidthLimit").style.display = "block";
        document.getElementById("bandwidth_limit").value = bandwidth_limit;

        var elps = document.getElementById("bandwidth");
        elps.value = bandwidth_limit;

        elps = document.getElementById('select_opts');
        var options = elps.options;
        var found = false;
        for (var i = 0; i < options.length; i++) {
            if (options[i].value == safety_type) {
                elps.selectedIndex = i;
                found = true;
                break;
            }
        }
        if(!found){
           var new_option = document.createElement("option");
           new_option.text = "Custom";
           elps.add(new_option);
           elps.selectedIndex = options.length;
        }
   }

   if (backhaul == 'NO')
         document.getElementById("check_backhaul").checked = false;
   if(largeupload == 'YES')
        document.getElementById('check_largeupload').checked = true;
   document.getElementById('ConNameOrg').value = document.getElementById('ConName').value;
   document.getElementById('ConName').focus();
   document.getElementById('GobiSelect').value = gobiindex;

} else {

    document.getElementById('ConNameOrg').disabled=true;
    document.getElementById('PresetList').focus();
}

</script>