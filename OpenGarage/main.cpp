/* OpenGarage Firmware
 *
 * Main loop
 * Mar 2016 @ OpenGarage.io
 *
 * This file is part of the OpenGarage library
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see
 * <http://www.gnu.org/licenses/>.
 */

#if defined(SERIAL_DEBUG)
  #define BLYNK_DEBUG
  #define BLYNK_PRINT Serial
#endif

/* #include <BlynkSimpleEsp8266.h> */
#include <DNSServer.h>
/* #include <PubSubClient.h> //https://github.com/Imroy/pubsubclient */

#include "pitches.h"
#include "OpenGarage.h"
#include "espconnect.h"
#include "driver/adc.h"
//OpenContacts
#include <MFRC522.h>
#include <SPI.h>
#include "driver/adc.h"

MFRC522::MIFARE_Key key;
MFRC522::StatusCode status;
MFRC522 mfrc522(RFSDA_PIN, RFRST_PIN);

OpenGarage og;
WebServer *server = NULL;
DNSServer *dns = NULL;

/* WidgetLED blynk_door(BLYNK_PIN_DOOR);
WidgetLED blynk_car(BLYNK_PIN_CAR); */

static Ticker led_ticker;
static Ticker aux_ticker;
static Ticker ip_ticker;
static Ticker restart_ticker;

static WiFiClient wificlient;
/* PubSubClient mqttclient(wificlient); */

static String scanned_ssids;
static byte read_cnt = 0;
static uint distance = 0;
static uint vdistance = 0;
static uint cardKey = 0;
static uint lastKey = 0;
static uint voltage = 0;
static float tempC = 0;
static float humid = 0;
static byte door_status = 0; //0 down, 1 up
static int vehicle_status = 0; //0 No, 1 Yes, 2 Unknown (door open), 3 Option Disabled
static bool curr_cloud_access_en = false;
static uint led_blink_ms = LED_FAST_BLINK;
static ulong justopen_timestamp = 0;
static byte curr_mode;
// this is one byte storing the door status histogram
// maximum 8 bits
static byte door_status_hist = 0;
static ulong curr_utc_time = 0;
static ulong curr_utc_hour= 0;
static HTTPClient http;

void do_setup();
void do_wake();

byte findKeyVal (const char *str, const char *key, char *strbuf=NULL, uint8_t maxlen=0) {
  uint8_t found=0;
  uint8_t i=0;
  const char *kp;
  kp=key;
  while(*str &&  *str!=' ' && *str!='\n' && found==0){
    if (*str == *kp){
      kp++;
      if (*kp == '\0'){
        str++;
        kp=key;
        if (*str == '='){
            found=1;
        }
      }
    } else {
      kp=key;
    }
    str++;
  }
  if(strbuf==NULL) return found; // if output buffer not provided, return right away

  if (found==1){
    // copy the value to a buffer and terminate it with '\0'
    while(*str &&  *str!=' ' && *str!='\n' && *str!='&' && i<maxlen-1){
      *strbuf=*str;
      i++;
      str++;
      strbuf++;
    }
    if (!(*str) ||  *str == ' ' || *str == '\n' || *str == '&') {
      *strbuf = '\0';
    } else {
      found = 0;  // Ignore partial values i.e. value length is larger than maxlen
      i = 0;
    }
  }
  return(i); // return the length of the value
}

void server_send_html_P(PGM_P content) {
  server->send_P(200, PSTR("text/html"), content);
  DEBUG_PRINT(strlen_P(content));
  DEBUG_PRINTLN(F(" bytes sent."));
}

void server_send_json(String json) {
  server->sendHeader("Access-Control-Allow-Origin", "*"); // from esp8266 2.4 this has to be sent explicitly
  server->send(200, "application/json", json);
}

void server_send_result(byte code, const char* item = NULL) {
  String json = F("{\"result\":");
  json += code;
  if (!item) item = "";
  json += F(",\"item\":\"");
  json += item;
  json += F("\"");
  json += F("}");
  server_send_json(json);
}

void server_send_result(const char* command, byte code, const char* item = NULL) {
  if(!command) server_send_result(code, item);
}

bool get_value_by_key(const char* key, uint& val) {
  if(server->hasArg(key)) {
    val = server->arg(key).toInt();   
    return true;
  } else {
    return false;
  }
}

bool get_value_by_key(const char* key, String& val) {
  if(server->hasArg(key)) {
    val = server->arg(key);   
    return true;
  } else {
    return false;
  }
}

bool findArg(const char *command, const char *name) {
  if(command) {
    return findKeyVal(command, name);
    // todo
  } else {
    return server->hasArg(name);
  }
}

char tmp_buffer[TMP_BUFFER_SIZE];

bool get_value_by_key(const char *command, const char *key, uint& val) {
  if(command) {
    byte ret = findKeyVal(command, key, tmp_buffer, TMP_BUFFER_SIZE);
    val = String(tmp_buffer).toInt();
    return ret;
  } else {
    return get_value_by_key(key, val);
  }
}

bool get_value_by_key(const char *command, const char *key, String& val) {
  if(command) {
    byte ret = findKeyVal(command, key, tmp_buffer, TMP_BUFFER_SIZE);
    val = String(tmp_buffer);
    return ret;
  } else {
    return get_value_by_key(key, val);
  }
}

String ipString;

void report_ip() {
  static uint notes[] = {NOTE_C4, NOTE_CS4, NOTE_D4, NOTE_DS4, NOTE_E4, NOTE_F4, NOTE_FS4, NOTE_G4, NOTE_GS4, NOTE_A4};
  static byte note = 0;
  static byte digit = 0;

  if(digit == ipString.length()) { // play ending note
    og.play_note(NOTE_C6); digit++; note=0;
    ip_ticker.once_ms(1000, report_ip);
    return;
  } else if(digit == ipString.length()+1) { // end
    og.play_note(0); note=0; digit=0;
    return;
  }
  char c = ipString.charAt(digit);
  if (c==' ') {
    og.play_note(0); digit++; note=0;
    ip_ticker.once_ms(1000, report_ip);
  } else if (c=='.') {
    og.play_note(NOTE_C5);
    digit++; note=0;
    ip_ticker.once_ms(500, report_ip);
  } else if (c>='0' && c<='9') {
    byte idx=9; // '0' maps to index 9;
    if(c>='1') idx=c-'1';
    if(note==idx+1) {
      og.play_note(0); note++;
      ip_ticker.once_ms(1000, report_ip);
    } else if(note==idx+2) {
      digit++; note=0;
      ip_ticker.once_ms(100, report_ip);
    } else {
      og.play_note(notes[note]);
      note++;
      ip_ticker.once_ms(500, report_ip);
    }
  }
}

void restart_in(uint32_t ms) {
  if(og.state != OG_STATE_WAIT_RESTART) {
    og.state = OG_STATE_WAIT_RESTART;
    DEBUG_PRINTLN(F("Prepare to restart..."));
    restart_ticker.once_ms(ms, og.restart);
  }
}

void on_home()
{
  if(curr_mode == OG_MOD_AP) {
    server_send_html_P(ap_home_html);
  } else {
    server_send_html_P(sta_home_html);
  }
}

void on_sta_view_options() {
  if(curr_mode == OG_MOD_AP) return;
  server_send_html_P(sta_options_html);
}

void on_sta_view_logs() {
  if(curr_mode == OG_MOD_AP) return;
  server_send_html_P(sta_logs_html);
}

char dec2hexchar(byte dec) {
  if(dec<10) return '0'+dec;
  else return 'A'+(dec-10);
}

String get_mac() {
  static String hex = "";
  if(!hex.length()) {
    byte mac[6];
    WiFi.macAddress(mac);

    for(byte i=0;i<6;i++) {
      hex += dec2hexchar((mac[i]>>4)&0x0F);
      hex += dec2hexchar(mac[i]&0x0F);
      if(i!=5) hex += ":";
    }
  }
  return hex;
}

String get_ap_ssid() {
  static String ap_ssid = "";
  if(!ap_ssid.length()) {
    byte mac[6];
    WiFi.macAddress(mac);
    ap_ssid = "OG_";
    for(byte i=3;i<6;i++) {
      ap_ssid += dec2hexchar((mac[i]>>4)&0x0F);
      ap_ssid += dec2hexchar(mac[i]&0x0F);
    }
  }
  return ap_ssid;
}

String get_ip() {
  String ip = "";
  IPAddress _ip = WiFi.localIP();
  ip = _ip[0];
  ip += ".";
  ip += _ip[1];
  ip += ".";
  ip += _ip[2];
  ip += ".";
  ip += _ip[3];
  return ip;
}

void sta_controller_fill_json(String& json) {
  json = "";
  json += F("{\"dist\":");
  json += distance;
  json += F(",\"door\":");
  json += door_status;
  json += F(",\"vehicle\":");
  json += vehicle_status;
  json += F(",\"rcnt\":");
  json += read_cnt;
  json += F(",\"fwv\":");
  json += og.options[OPTION_FWV].ival;
  json += F(",\"name\":\"");
  json += og.options[OPTION_NAME].sval;
  json += F("\",\"mac\":\"");
  json += get_mac();
  json += F("\",\"cid\":");
  json += ESP.getChipRevision();
  json += F(",\"rssi\":");
  json += (int16_t)WiFi.RSSI();
  if(og.options[OPTION_TSN].ival) {
  	json += F(",\"temp\":");
  	json += tempC;
  	json += F(",\"humid\":");
  	json += humid;
  }
  json += F("}");
}

void on_sta_controller() {
  if(curr_mode == OG_MOD_AP) return;
  String json;
  sta_controller_fill_json(json);
  server_send_json(json);
}

void on_sta_debug() {
  String json = "";
  json += F("{");
  json += F("\"rcnt\":");
  json += read_cnt;
  json += F(",\"fwv\":");
  json += og.options[OPTION_FWV].ival;
  json += F(",\"name\":\"");
  json += og.options[OPTION_NAME].sval;
  json += F("\",\"mac\":\"");
  json += get_mac();
  json += F("\",\"cid\":");
  json += ESP.getChipRevision();
  json += F(",\"rssi\":");
  json += (int16_t)WiFi.RSSI();
  json += F(",\"bssid\":\"");
  json += WiFi.BSSIDstr();
  json += F("\",\"build\":\"");
  json += (F(__DATE__));
  json += F("\",\"Freeheap\":");
  json += (uint16_t)ESP.getFreeHeap();
  json += F("}");
  server_send_json(json);
}

void sta_logs_fill_json(String& json) {
 /*  json = "";
  json += F("{\"name\":\"");
  json += og.options[OPTION_NAME].sval;
  json += F("\",\"time\":");
  json += curr_utc_time;
  json += F(",\"logs\":[");
  if(!og.read_log_start()) {
    json += F("]}");
    server_send_json(json);
    return;
  }
  LogStruct l;
  for(uint i=0;i<og.options[OPTION_LSZ].ival;i++) {
    if(!og.read_log_next(l)) break;
    if(!l.tstamp) continue;
    json += F("[");
    json += l.tstamp;
    json += F(",");
    json += l.status;
    json += F(",");
    json += l.dist;
    json += F("],");
  }
  og.read_log_end();
  json.remove(json.length()-1); // remove the extra ,
  json += F("]}"); */
}

void on_sta_logs() {
  if(curr_mode == OG_MOD_AP) return;
  String json;
  sta_logs_fill_json(json);
  server_send_json(json);
}

bool verify_device_key() {
  if(server->hasArg("dkey") && (server->arg("dkey") == og.options[OPTION_DKEY].sval))
    return true;
  return false;
}

bool verify_device_key(const char* command) {
  if(command) return true;
  else return verify_device_key();
}

void on_reset_all(){
  if(!verify_device_key()) {
    server_send_result(HTML_UNAUTHORIZED);
    return;
  }

  og.state = OG_STATE_RESET;
  server_send_result(HTML_SUCCESS);
}

void on_clear_log() {
  if(!verify_device_key()) {
    server_send_result(HTML_UNAUTHORIZED);
    return;
  }
  og.log_reset();
  server_send_result(HTML_SUCCESS);  
}

bool readCard() {
  if ( ! mfrc522.PICC_IsNewCardPresent()) {
    return false;
  }
  if ( ! mfrc522.PICC_ReadCardSerial()) {
    return false;
  }

  cardKey =  mfrc522.uid.uidByte[0] << 24;
  cardKey += mfrc522.uid.uidByte[1] << 16;
  cardKey += mfrc522.uid.uidByte[2] <<  8;
  cardKey += mfrc522.uid.uidByte[3];
  
  mfrc522.PICC_HaltA(); 
  mfrc522.PCD_StopCrypto1();

  if (cardKey == lastKey) {
    DEBUG_PRINTLN("Duplicate key read, do not sending");
    return false;
  }

  lastKey = cardKey;
  DEBUG_PRINTLN(cardKey);
  return true;
}

void sendData() {
  static String newdevice = "/php/newdevice.php";
  static String report = "/php/report.php";
  static String record = "/php/record.php";
  String html_url, html_content, request;
  static char html_chars[250]; //if we need to convert back to char array
  static HTTPClient http;

  if (og.get_mode() == OG_MOD_AP) {
    html_url = og.options[OPTION_URL].sval+newdevice;
    html_content = "bldg="+og.options[OPTION_BLDG].sval+
                   "&room="+og.options[OPTION_ROOM].sval+"&admin="+og.options[OPTION_ARD].ival+
                   "&api="+og.options[OPTION_AAPI].sval+"&name="+og.options[OPTION_ANAM].sval+
                   "&occup="+og.options[OPTION_OCCP].ival+"&mac="+get_mac();
    DEBUG_PRINTLN("Sending new device information...");
  
  }
  if (og.options[OPTION_ARD].ival == 1 && cardKey) {
    html_url = og.options[OPTION_URL].sval+report;
    html_content = "uid="+String(cardKey)+"&api="+og.options[OPTION_AAPI].sval+"&name="+og.options[OPTION_ANAM].sval+
                   "&mac="+get_mac();
    DEBUG_PRINTLN("Sending card information");
  }
  else if (cardKey) {
    html_url = og.options[OPTION_URL].sval+record;
    html_content = "uid="+String(cardKey)+"&api="+og.options[OPTION_AAPI].sval+"&voltage="+String(voltage)+"&mac="+get_mac();
    DEBUG_PRINTLN("Sending record data....");
  }
  else {
    html_url = og.options[OPTION_URL].sval+record;
    html_content = "api="+og.options[OPTION_AAPI].sval+"&voltage="+String(voltage)+"&mac="+get_mac();
    DEBUG_PRINTLN("Sending record data....");
  }

  DEBUG_PRINTLN(html_url);
  DEBUG_PRINTLN(html_content);
  
  http.begin(html_url);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  int httpCode = http.POST(html_content);
  //some callback for response code?
  http.end();

}

void sta_change_controller_main(const char *command) {
  if(curr_mode == OG_MOD_AP) return;

  if(!verify_device_key(command)) {
    server_send_result(command, HTML_UNAUTHORIZED);
    return;
  }

  if(findArg(command, "click") || findArg(command, "close") || findArg(command, "open")) {
    DEBUG_PRINTLN(F("Received button request (click, close, or open)"));
    server_send_result(command, HTML_SUCCESS);
    //1 is open
    if ((findArg(command, "close") && door_status) ||
        (findArg(command, "open") && !door_status) ||
        (findArg(command, "click"))) {
      DEBUG_PRINTLN(F("Valid command recieved based on door status"));
      if(!og.options[OPTION_ALM].ival) {
        // if alarm is not enabled, trigger relay right away
        og.click_relay();
      } else if(og.options[OPTION_AOO].ival && !door_status) {
      	// if 'Do not alarm on open' is on, and door is about to be open, no alarm needed
      	og.click_relay();
      } else {
        // else, set alarm
        og.set_alarm();
      }
    }else{
      DEBUG_PRINTLN(F("Command request not valid, door already in requested state"));
    }
  } else if(findArg(command, "reboot")) {
    server_send_result(command, HTML_SUCCESS);
    //restart_ticker.once_ms(1000, og.restart);
    restart_in(1000);
  } else if(findArg(command, "apmode")) {
    server_send_result(command, HTML_SUCCESS);
    og.reset_to_ap();
  } else {
    server_send_result(command, HTML_NOT_PERMITTED);
  }

}

void on_sta_change_controller() {
  sta_change_controller_main(NULL);  
}

void sta_change_options_main(const char *command) {
  if(curr_mode == OG_MOD_AP) return;

  if(!verify_device_key(command)) {
    server_send_result(command, HTML_UNAUTHORIZED);
    return;
  }

  uint ival = 0;
  String sval;
  byte i;
  OptionStruct *o = og.options;
  
  byte usi = 0;
  // FIRST ROUND: check option validity
  // do not save option values yet
  for(i=0;i<NUM_OPTIONS;i++,o++) {
    const char *key = o->name.c_str();
    // these options cannot be modified here
    if(i==OPTION_FWV || i==OPTION_MOD  || i==OPTION_SSID ||
      i==OPTION_PASS || i==OPTION_DKEY)
      continue;
    
    if(o->max) {  // integer options
      if(get_value_by_key(command, key, ival)) {
        if(ival>o->max) {	// check max limit
          server_send_result(command, HTML_DATA_OUTOFBOUND, key);
          return;
        }
        // check min limit
        if(i==OPTION_DRI && ival < 50) {
        	server_send_result(command,HTML_DATA_OUTOFBOUND, key);
        	return;
        }
        if(i==OPTION_LSZ && ival < 20) {
          // minimal log size is 20
          server_send_result(command, HTML_DATA_OUTOFBOUND, key);
          return;
        }
        if(i==OPTION_CDT && ival < 50) {
          // click delay time should be at least 50 ms
          server_send_result(command, HTML_DATA_OUTOFBOUND, key);
          return;
        }
        if(i==OPTION_USI && ival==1) {
          // mark device IP and gateway IP change
          usi = 1;
        }
      }
    }
  }
  
  // Check device IP and gateway IP changes
  String dvip, gwip, subn, dns1;
  const char* _dvip = "dvip";
  const char* _gwip = "gwip";
  const char* _subn = "subn";
  const char* _dns1 = "dns1";
  if(usi) {
    if(get_value_by_key(command, _dvip, dvip)) {
      if(get_value_by_key(command, _gwip, gwip)) {
        // check validity of IP address
        IPAddress ip;
        if(!ip.fromString(dvip)) {server_send_result(command, HTML_DATA_FORMATERROR, _dvip); return;}
        if(!ip.fromString(gwip)) {server_send_result(command, HTML_DATA_FORMATERROR, _gwip); return;}
        if(get_value_by_key(command, _subn, subn)) {
          if(!ip.fromString(subn)) {
            server_send_result(command, HTML_DATA_FORMATERROR, _subn);
            return;
          }
        }
        if(get_value_by_key(command, _dns1, dns1)) {
        	if(!ip.fromString(dns1)) {
            server_send_result(command, HTML_DATA_FORMATERROR, _dns1);
            return;
        	}
        }
      } else {
        server_send_result(command, HTML_DATA_MISSING, _gwip);
        return;
      }              
    } else {
      server_send_result(command, HTML_DATA_MISSING, _dvip);
      return;
    }
  }
  // Check device key change
  String nkey, ckey;
  const char* _nkey = "nkey";
  const char* _ckey = "ckey";
  
  if(get_value_by_key(command, _nkey, nkey)) {
    if(get_value_by_key(command, _ckey, ckey)) {
      if(!nkey.equals(ckey)) {
        server_send_result(command, HTML_MISMATCH, _ckey);
        return;
      }
    } else {
      server_send_result(command, HTML_DATA_MISSING, _ckey);
      return;
    }
  }
  
  // SECOND ROUND: change option values
  o = og.options;
  for(i=0;i<NUM_OPTIONS;i++,o++) {
    const char *key = o->name.c_str();
    // these options cannot be modified here
    if(i==OPTION_FWV || i==OPTION_MOD  || i==OPTION_SSID ||
      i==OPTION_PASS || i==OPTION_DKEY)
      continue;
    
    if(o->max) {  // integer options
      if(get_value_by_key(command, key, ival)) {
        o->ival = ival;
      }
    } else {
      if(get_value_by_key(command, key, sval)) {
        o->sval = sval;
      }
    }
  }

  if(usi) {
    get_value_by_key(command, _dvip, dvip);
    get_value_by_key(command, _gwip, gwip);
    og.options[OPTION_DVIP].sval = dvip;
    og.options[OPTION_GWIP].sval = gwip;
    if(get_value_by_key(command, _subn, subn)) {
      og.options[OPTION_SUBN].sval = subn;
    }
    if(get_value_by_key(command, _dns1, dns1)) {
    	og.options[OPTION_DNS1].sval = dns1;
    }
  }
  
  if(get_value_by_key(command, _nkey, nkey)) {
      og.options[OPTION_DKEY].sval = nkey;
  }

  og.options_save();
  server_send_result(command, HTML_SUCCESS);
}

void on_sta_change_options() {
  sta_change_options_main(NULL);
}

void sta_options_fill_json(String& json) {
  json = "{";
  OptionStruct *o = og.options;
  for(byte i=0;i<NUM_OPTIONS;i++,o++) {
    if(!o->max) {
      if(i==OPTION_PASS || i==OPTION_DKEY) { // do not output password or device key
        continue;
      } else {
        json += F("\"");
        json += o->name;
        json += F("\":");
        json += F("\"");
        json += o->sval;
        json += F("\"");
        json += ",";
      }
    } else {  // if this is a int option
      json += F("\"");
      json += o->name;
      json += F("\":");
      json += o->ival;
      json += ",";
    }
  }
  json.remove(json.length()-1); // remove the extra ,
  json += F("}");
}

void on_sta_options() {
  if(curr_mode == OG_MOD_AP) return;
  String json;
  sta_options_fill_json(json);
  server_send_json(json);
}

void on_ap_scan() {
  if(curr_mode == OG_MOD_STA) return;
  server_send_json(scanned_ssids);
}

void on_ap_change_config() {
  if(curr_mode == OG_MOD_STA) return;
  if(server->hasArg("ssid")&&server->arg("ssid").length()!=0) {
    og.options[OPTION_SSID].sval = server->arg("ssid");
    og.options[OPTION_PASS].sval = server->arg("pass");
    og.options[OPTION_URL].sval = server->arg("url");
    og.options[OPTION_BLDG].sval = server->arg("bldg");
    og.options[OPTION_ROOM].sval = server->arg("room");
    og.options[OPTION_ANAM].sval = server->arg("admin_name");
    og.options[OPTION_ARD].ival = server->arg("admin_read").toInt();
    og.options[OPTION_AAPI].sval = server->arg("admin_api");
    og.options[OPTION_OCCP].ival = server->arg("occup").toInt();

    og.options_save();
    server_send_result(HTML_SUCCESS);
    og.state = OG_STATE_TRY_CONNECT;

  } else {
    server_send_result(HTML_DATA_MISSING, "ssid");
  }
}

void on_ap_try_connect() {
  if(curr_mode == OG_MOD_STA) return;
  String json = "{";
  json += F("\"ip\":");
  json += (WiFi.status() == WL_CONNECTED) ? (uint32_t)WiFi.localIP() : 0;
  json += F("}");
  server_send_json(json);
  if(WiFi.status() == WL_CONNECTED && WiFi.localIP()) {
    /*DEBUG_PRINTLN(F("STA connected, updating option file"));
    og.options[OPTION_MOD].ival = OG_MOD_STA;
    if(og.options[OPTION_AUTH].sval.length() == 32) {
      og.options[OPTION_ACC].ival = OG_ACC_BOTH;
    }
    og.options_save();*/
    DEBUG_PRINTLN(F("IP received by client, restart."));
    //restart_ticker.once_ms(1000, og.restart); // restart once client receives IP address
    restart_in(1000);
  }
}

void on_ap_debug() {
  String json = "";
  json += F("{");
  /* json += F("\"dist\":");
  json += og.read_distance(); */
  json += F(",\"fwv\":");
  json += og.options[OPTION_FWV].ival;
  json += F("}");
  server_send_json(json);
}

/* Minimal call after deepsleep */
void do_wake() {
  DEBUG_BEGIN(115200);
  SPI.begin();
  og.begin();
  voltage = analogRead(PIN_ADC); //adc is on channel 2, must do read before WiFi connects

  if(server) {
    delete server;
    server = NULL;
  }

  mfrc522.PCD_Init();
  esp_sleep_enable_ext0_wakeup(PIN_PIR, 1);

}

void do_setup()
{
  DEBUG_BEGIN(115200);
  do_wake();
  WiFi.persistent(false); // turn off persistent, fixing flash crashing issue
  og.options_setup();

  DEBUG_PRINT(F("Complile Info: "));
  DEBUG_PRINT(F(__DATE__));
  DEBUG_PRINT(F(" "));
  DEBUG_PRINTLN(F(__TIME__));
  curr_mode = og.get_mode();
  //only start a server in AP mode
  if(!server && curr_mode == OG_MOD_AP) {
    server = new WebServer(og.options[OPTION_HTP].ival);
    if(curr_mode == OG_MOD_AP) dns = new DNSServer();
    DEBUG_PRINT(F("server started @ "));
    DEBUG_PRINTLN(og.options[OPTION_HTP].ival);
  }
  led_blink_ms = LED_FAST_BLINK;
  
}

void process_ui()
{
  // process button
  static ulong button_down_time = 0;
  if(og.get_button() == LOW) {
    if(!button_down_time) {
      button_down_time = millis();
    } else {
      ulong curr = millis();
      if(curr > button_down_time + BUTTON_FACRESET_TIMEOUT) {
        led_blink_ms = 0;
        og.set_led(LOW);
      } else if(curr > button_down_time + BUTTON_APRESET_TIMEOUT) {
        led_blink_ms = 0;
        og.set_led(HIGH);
      }
    }
  }
  else {
    if (button_down_time > 0) {
      ulong curr = millis();
      if(curr > button_down_time + BUTTON_FACRESET_TIMEOUT) {
        og.state = OG_STATE_RESET;
      } else if(curr > button_down_time + BUTTON_APRESET_TIMEOUT) {
        og.reset_to_ap();
      } else if(curr > button_down_time + BUTTON_REPORTIP_TIMEOUT) {        
        // report IP
        ipString = get_ip();
        ipString.replace(".", ". ");
        report_ip();
      } else if(curr > button_down_time + 50) {
        og.click_relay();
      }
      button_down_time = 0;
    }
  }
  // process led
  static ulong led_toggle_timeout = 0;
  if(led_blink_ms) {
    if(millis() > led_toggle_timeout) {
      // toggle led
      og.set_led(1-og.get_led());
      led_toggle_timeout = millis() + led_blink_ms;
    }
  }  
}

void on_ap_update() {
  server_send_html_P(ap_update_html);
}

void on_ap_upload_fin() { }

void on_ap_upload() {
 /*  HTTPUpload& upload = server->upload();
  if(upload.status == UPLOAD_FILE_START){
    Serial.println(F("prepare to upload: "));
    Serial.println(upload.filename);
    uint32_t maxSketchSpace = (ESP.getFreeSketchSpace()-0x1000)&0xFFFFF000;
    if(!Update.begin(maxSketchSpace)) {
      Serial.println(F("not enough space"));
    }
  } else if(upload.status == UPLOAD_FILE_WRITE) {
    Serial.print(".");
    if(Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Serial.println(F("size mismatch"));
    }
      
  } else if(upload.status == UPLOAD_FILE_END) {
    
    Serial.println(F("upload completed"));
       
  } else if(upload.status == UPLOAD_FILE_ABORTED){
    Update.end();
    Serial.println(F("upload aborted"));
  }
  delay(0);   */  
}

void check_status_ap() {
  static ulong cs_timeout = 0;
  if(millis() > cs_timeout) {
    Serial.println(OG_FWV);
    cs_timeout = millis() + 2000;
  }
}

void check_status() {

}

void time_keeping() {
  static bool configured = false;
  static ulong prev_millis = 0;
  static ulong time_keeping_timeout = 0;

  if(!configured) {
    DEBUG_PRINTLN(F("Set time server"));
    configTime(0, 0, "time.google.com", "pool.ntp.org", NULL);
    configured = true;
  }

  if(!curr_utc_time || (curr_utc_time > time_keeping_timeout)) {
    ulong gt = time(nullptr);
    if(gt<978307200L) {
      // if we didn't get response, re-try after 2 seconds
      time_keeping_timeout = curr_utc_time + 2;
    } else {
      curr_utc_time = gt;
      curr_utc_hour = (curr_utc_time % 86400)/3600;
      DEBUG_PRINT(F("Updated time from NTP: "));
      DEBUG_PRINT(curr_utc_time);
      DEBUG_PRINT(" Hour: ");
      DEBUG_PRINTLN(curr_utc_hour);
      // if we got a response, re-try after TIME_SYNC_TIMEOUT seconds
      time_keeping_timeout = curr_utc_time + TIME_SYNC_TIMEOUT;
      prev_millis = millis();
    }
  }
  while(millis() - prev_millis >= 1000) {
    curr_utc_time ++;
    curr_utc_hour = (curr_utc_time % 86400)/3600;
    prev_millis += 1000;
  }
}

/*void process_alarm() {
   if(!og.alarm) return;
  static ulong prev_half_sec = 0;
  ulong curr_half_sec = millis()/500;
  if(curr_half_sec != prev_half_sec) {  
    prev_half_sec = curr_half_sec;
    if(prev_half_sec % 2 == 0) {
      og.play_note(ALARM_FREQ);
    } else {
      og.play_note(0);
    }
    og.alarm--;
    if(og.alarm==0) {
      og.play_note(0);
      og.click_relay();
    }
  } 
}*/

void do_sleep() {
  DEBUG_PRINTLN("Entering deepsleep");
  adc_power_off();
  esp_deep_sleep_start();
}

void do_loop() {

  static ulong connecting_timeout;
  static ulong sleep_timeout;

  switch(og.state) {
  case OG_STATE_INITIAL:
    if(curr_mode == OG_MOD_AP) {
      scanned_ssids = scan_network();
      String ap_ssid = get_ap_ssid();
      start_network_ap(ap_ssid.c_str(), NULL);
      delay(500);
      dns->setErrorReplyCode(DNSReplyCode::NoError);
      dns->start(53, "*", WiFi.softAPIP());
      server->on("/",   on_home);    
      server->on("/js", on_ap_scan);
      server->on("/cc", on_ap_change_config);
      server->on("/jt", on_ap_try_connect);
      server->on("/db", on_ap_debug);      
/*       server->on("/update", HTTP_GET, on_ap_update);
      server->on("/update", HTTP_POST, on_ap_upload_fin, on_ap_upload); */      
      server->on("/resetall",on_reset_all);
      server->onNotFound(on_home);
      server->begin();
      DEBUG_PRINTLN(F("Web Server endpoints (AP mode) registered"));
      og.state = OG_STATE_CONNECTED;
      DEBUG_PRINTLN(WiFi.softAPIP());
      connecting_timeout = 0;
    } else {
      led_blink_ms = LED_SLOW_BLINK;
      DEBUG_PRINT(F("Attempting to connect to SSID: "));
      DEBUG_PRINTLN(og.options[OPTION_SSID].sval.c_str());
      WiFi.mode(WIFI_STA);
      start_network_sta(og.options[OPTION_SSID].sval.c_str(), og.options[OPTION_PASS].sval.c_str());
      og.config_ip();
      og.state = OG_STATE_CONNECTING;
      connecting_timeout = millis() + 60000;
    }
    break;

  case OG_STATE_TRY_CONNECT:
    led_blink_ms = LED_SLOW_BLINK;
    DEBUG_PRINT(F("Attempting to connect to SSID: "));
    DEBUG_PRINTLN(og.options[OPTION_SSID].sval.c_str());
    start_network_sta_with_ap(og.options[OPTION_SSID].sval.c_str(), og.options[OPTION_PASS].sval.c_str());
    og.config_ip();
    og.state = OG_STATE_CONNECTED;
    break;
    
  case OG_STATE_CONNECTING:
    led_blink_ms = LED_SLOW_BLINK;
    if(WiFi.status() == WL_CONNECTED) {
      DEBUG_PRINT(F("Wireless connected, IP: "));
      DEBUG_PRINTLN(WiFi.localIP());
      sendData(); //loggin the activation

      /* server->on("/", on_home);
      server->on("/jc", on_sta_controller);
      server->on("/jo", on_sta_options);
      server->on("/jl", on_sta_logs);
      server->on("/vo", on_sta_view_options);
      server->on("/vl", on_sta_view_logs);
      server->on("/cc", on_sta_change_controller);
      server->on("/co", on_sta_change_options);
      server->on("/db", on_sta_debug);
      server->on("/update", HTTP_GET, on_sta_update);
      server->on("/update", HTTP_POST, on_sta_upload_fin, on_sta_upload);
      server->on("/clearlog", on_clear_log);
      server->on("/resetall",on_reset_all);
      server->begin(); 
      DEBUG_PRINTLN(F("Web Server endpoints (STA mode) registered"));

      // use ap ssid as mdns name
      if(MDNS.begin(get_ap_ssid().c_str())) {
        DEBUG_PRINTLN(F("MDNS registered"));
        DEBUG_PRINTLN(get_ap_ssid().c_str());
        //MDNS.addService("http", "tcp", og.options[OPTION_HTP].ival);
				//DEBUG_PRINTLN(og.options[OPTION_HTP].ival);
      }*/

      led_blink_ms = 0;
      og.set_led(HIGH);
      og.play_startup_tune();
      og.state = OG_STATE_CONNECTED;
      connecting_timeout = 0;
      sleep_timeout = millis() + 10000;
    } else {
      if(millis() > connecting_timeout) {
        DEBUG_PRINTLN(F("Wifi Connecting timeout, restart"));
        og.restart();
      }
    }
    break;

  case OG_STATE_RESET:
    og.state = OG_STATE_INITIAL;
    og.options_reset();
    og.log_reset();
    og.restart();
    break;
    
  case OG_STATE_WAIT_RESTART:
    if(dns) dns->processNextRequest();  
    if(server) server->handleClient();    
    break;
    
  case OG_STATE_CONNECTED: //THIS IS THE MAIN LOOP
    if(curr_mode == OG_MOD_AP) {
      dns->processNextRequest();
      server->handleClient();
      check_status_ap();
      connecting_timeout = 0;
      if(og.options[OPTION_MOD].ival == OG_MOD_STA) {
        // already in STA mode, waiting to reboot
        break;
      }
      if(WiFi.status() == WL_CONNECTED && WiFi.localIP()) {
        DEBUG_PRINTLN(F("STA connected, updating option file"));
        sendData(); //newdevice data sent
        og.options[OPTION_MOD].ival = OG_MOD_STA;
        og.options_save();
        og.play_startup_tune();
        //restart_ticker.once_ms(10000, og.restart);
        restart_in(10000);
      }
      
    } else {
      if(WiFi.status() == WL_CONNECTED) {
      	//MDNS.update();
        //time_keeping();
        //Don't sleep if we are an admin device, for now
        if (millis() > sleep_timeout && og.options[OPTION_ARD].ival == 0) { do_sleep(); }

        //check_status(); //This checks the door, sends info to services and processes the automation rules
        if (readCard()) { sendData(); sleep_timeout = millis()+5000;}

        //server->handleClient();
        connecting_timeout = 0;
      } else {
        //og.state = OG_STATE_INITIAL;
        if(!connecting_timeout) {
          DEBUG_PRINTLN(F("State is CONNECTED but WiFi is disconnected, start timeout counter."));
          connecting_timeout = millis()+60000;
        }
        else if(millis() > connecting_timeout) {
          DEBUG_PRINTLN(F("timeout reached, reboot"));
          og.restart();
        }
      }
    }
    break;
  }

  //Nework independent functions, handle events like reset even when not connected
  process_ui();
/*   if(og.alarm)
    process_alarm(); */
}
