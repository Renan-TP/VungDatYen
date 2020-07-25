#include <ESP8266WiFi.h>
#include <strings_en.h>
#include <WiFiManager.h>
#include <ESP8266HTTPClient.h>
#include <Arduino_JSON.h>
#include <EEPROM.h>
#include "string.h"
#include "stdio.h"
//===============Define===============
//-------------Wifi/URL/JSON object--------------------------
#define AP_SEVER "IoTVision_O1V_1.0"         //todo: CHANGE!
#define AP_PASSWORD "shl12345"
#define POST_URL "http://JSON.IoTVision.vn/api/BoGaNhaYen_DuLieu"
#define GET_URL "http://JSON.IoTVision.vn/api/BoGaNhaYenHienThiOnline?MatKhau=nhayen"
#define Object1 "MaBoGa"
#define Object2 "DongDien"
#define Object3 "SoLanQuaTai"
#define Object4 "DongNgat"
#define Object5 "MucNuoc"
#define Object6 "MucNuocBoGa2"
#define Object7 "MucNuocBoGa3"
#define Object8 "MucNuocBoGa4"
//------------Cai dat bo ga---------------
#define MaBoGa "nhayen_G001"                //todo: CHANGE!
#define SoLuongBoGa 1                       //todo: CHANGE!
//------------Cai dat mach----------------
#define relayPin 14
#define buttonPin 16
#define waterPin1 5
#define waterPin2 4
#define waterPin3 12
#define waterPin4 13
#define analogInPin A0
//---------Cai dat Offset/Debug-----------
#define offset -3                         //todo: CHANGE!
#define DB 1                              //todo: CHANGE!
#define WFDBTime 300
#define timeprocess 100
//==============Variable=============
//-------------Relay----------------
boolean relayOn = 1;
byte fault = 0;
byte recloseIndex = 0;
//-----------------Wifi Debug--------------------------
bool WFDB = 0;
byte WifiDBIndex = 0;
//boolean Flag.fault = 0
//------------Sensor (Shall not change)----------------
int maxValue = 0;
int minValue = 0;
double Vpp = 0;
double Peak = 0;
byte pass = 0;
//------------------Water Level------------------------
byte WaterLevel = 1;
typedef struct{
  boolean t100ms = 0;
  boolean t250ms = 0;
  boolean t500ms = 0;
  boolean t1s = 0;
  boolean t5s = 0;
  boolean t10s = 0;
  boolean fault = 0;
  boolean doProcess = 0;
}flagType;
flagType Flag;
typedef struct{
  unsigned long t100ms = 0;
  unsigned long t250ms = 0;
  unsigned long t500ms = 0;
  unsigned long t1s = 0;
  unsigned long t5s = 0;
  unsigned long t10s = 0;
  unsigned long tprocess = 0;
}startType;
startType StartTimer;
typedef struct{
  int overload;
  int normal;
  int noload;
}ThongSoType;
ThongSoType ThongSo[4];
void ThongSoInit(){
  //--------1--------
  ThongSo[0].overload = 100 + offset; //256 absolutely
  ThongSo[0].normal = 50 + offset; //57 absolutely
  ThongSo[0].noload = 40 + offset;
  //--------2--------
  ThongSo[1].overload = 150 + offset; //295 with 1, 490 with 2 absolutely
  ThongSo[1].normal = 117 + offset; //124
  ThongSo[1].noload = 70 + offset; // 114 with 1, 98 with 2 absolutely
  //--------3--------
  ThongSo[2].overload = 200 + offset; //342 with 1, 
  ThongSo[2].normal = 175 + offset; //186 absolutely
  ThongSo[2].noload = 90 + offset; //166 with 1, 154 with 2, 146 with 3 absolutely
  //--------4--------
  ThongSo[3].overload = 280 + offset; //390 with 1
  ThongSo[3].normal = 240 + offset; //250
  ThongSo[3].noload = 100 + offset; //231 with 1
}
HTTPClient http;
WiFiManager wm;
void setup() {
  //WiFi.mode(WIFI_NONE_SLEEP);
  Serial.begin(115200);
  if(DB) Serial.setDebugOutput(true);
  Serial.println();
  WifiInit();
  delay(5000);
  pinMode(relayPin, OUTPUT);
  pinMode(buttonPin, INPUT);
  pinMode(waterPin1, INPUT_PULLUP);
  pinMode(waterPin2, INPUT_PULLUP);
  pinMode(waterPin3, INPUT_PULLUP);
  pinMode(waterPin4, INPUT_PULLUP);
  ThongSoInit();
}
void loop() {
//  wm.process();
  debug();
  checkTimer();
  if(Flag.doProcess){
    Peak = ProcessIrms() + offset;
  }
  if(Peak < 0) Peak = 0;
  if(Flag.t250ms){
    Flag.doProcess = 1;
    StartTimer.tprocess = millis();
  }
  if(Flag.t500ms){
    if(CheckTheoSoLuong(SoLuongBoGa) == 0){ //Kiem tra theo so luong bo
      Flag.fault = true;
      relayOn = false;
      fault++;
      if(DB) Serial.println("Fault!!");
    }
  }
  if(Flag.t1s){
    if((fault<3)&&Flag.fault){
      recloseIndex++;
    }
    relayOn = (recloseIndex >= 5)? true:relayOn;
    WiFiButton(WifiDBIndex);
  }
  if(Flag.t5s){
    if (WiFi.status() == WL_CONNECTED){
      PostTDuLieu();
    }else Serial.println("Wifi Conection Loss");
  }
  if(Flag.t10s){
    if (WiFi.status() != WL_CONNECTED){
      WifiDB();
    }
    if(digitalRead(waterPin1) == LOW){
      WaterLevel = 3;
    }else if(digitalRead(waterPin2) == LOW){
      WaterLevel = 0;
    }else WaterLevel = 1;
  }
  if(relayOn)
  {
    digitalWrite(relayPin, HIGH); // set the LED on
    recloseIndex = 0;
  } else {
    digitalWrite(relayPin, LOW); // set the LED off
  }
  FalseTheFlag();
}

//------------------------------Sensor Reading-------------------------------
double ProcessIrms()
{
  if (millis() - StartTimer.tprocess < timeprocess)
  {
    int readValue = analogRead(analogInPin);
    maxValue = (readValue > maxValue) ? readValue : maxValue;
    minValue = (readValue < minValue) ? readValue : minValue;
  }
  else
  {
    Vpp = (maxValue - minValue);
    maxValue = 0;
    minValue = 530;
    //StartTimer.tprocess = millis();
    Flag.doProcess = 0;
  } 
  return Vpp;   
}

int CheckTrangthai(int _start, int _normal, int _noload, byte &_pass){
  if(checkValue(Peak, _start)){
    if(_pass == 0) if(DB) Serial.println("Dang khoi dong");
    if(DB) Serial.println(Peak);
    _pass++;
    if(_pass >= 10){_pass = 0; Serial.print("Overload!"); return 0;}else{return 5;}
  }else if(checkValue(Peak, _normal)){
    if(DB) Serial.print("Hoat dong binh thuong: ");
    if(DB) Serial.println(Peak);
    _pass = 0;
    return 1;
  }else if(checkValue(Peak, _noload)){
    if(DB) Serial.print("Hoat dong khong tai: ");
    if(DB) Serial.println(Peak);
    _pass = 0;
    return 2;        
  }else{
    if(DB) Serial.print("Khong co ket noi: ");
    if(DB) Serial.println(Peak);
    _pass = 0;
    return 3;
  }
}
int CheckTheoSoLuong(int _s){
  return CheckTrangthai(ThongSo[_s-1].overload, ThongSo[_s-1].normal, ThongSo[_s-1].noload, pass);
}
bool checkValue(int _i, int _h){
  if(_i >= _h){
    return 1;
  }
  return 0;
}

//-----------------Wifi config------------------------------------------
void WifiInit(){
  wm.setConfigPortalTimeout(60);//Timeout 60s
  //wm.setAPClientCheck(true);
  //std::vector<const char *> menu = {"wifi","info","param","sep","restart","exit"};
  std::vector<const char *> menu = {"wifi","restart"};
  wm.setMenu(menu);
  // set dark theme
  wm.setClass("invert");
}
void setupWifiDB(){
  WiFiManagerParameter custom_text("<body style = \"font-family: Roboto; line-height: 20px; font-weight:400; font-style: normal; color: #888; font-size: 13px; visibility: visible;\">" \
   "<div class=\"box-content\">"\
   "<h2 >IoTVision © 7 - 2020</h2>"\
   "<p>Thank you for using my product, love you!</p></div>");
  wm.addParameter(&custom_text);
//  wm.setConfigPortalBlocking(false);
  wm.setConfigPortalTimeout(120);
  if (!wm.startConfigPortal(AP_SEVER, AP_PASSWORD)) {
    Serial.println("failed to connect and hit timeout");
//    delay(3000);
//    //reset and try again, or maybe put it to deep sleep
//    ESP.restart();
//    delay(5000);
  }
}
void WiFiButton(byte &_WifiDBIndex){
  if(DB) Serial.printf("Buton: %i \r\n", digitalRead(buttonPin));
  if(digitalRead(buttonPin) == HIGH){
    _WifiDBIndex++;
    if(_WifiDBIndex >= 5) {_WifiDBIndex = 0; setupWifiDB();}
  }else _WifiDBIndex = 0;
}
void WifiDB(){
  for(int i=0;i<10;i++){
    delay(WFDBTime);
  }
  if (WiFi.status() != WL_CONNECTED){
    if(WFDB){
      for(int i=0;i<5;i++){
        delay(WFDBTime);
      }  
    }
  }else WFDB = 1;
}

//------------------------------POST/GET data--------------------------------
void PostTDuLieu(){
  if(SoLuongBoGa == 1){
    postJSON(MaBoGa, round((double)Peak)/100, relayOn, fault, WaterLevel);
  }else if(SoLuongBoGa == 2){
    //postJSON(MaBoGa, round((double)Peak)/100, relayOn, fault, digitalRead(waterPin1), digitalRead(waterPin2));
    postJSON(MaBoGa, round((double)Peak)/100, relayOn, fault, WaterLevel);
  }else if(SoLuongBoGa == 3){    
    //postJSON(MaBoGa, round((double)Peak)/100, relayOn, fault, digitalRead(waterPin1), digitalRead(waterPin2),digitalRead(waterPin3));
    postJSON(MaBoGa, round((double)Peak)/100, relayOn, fault, WaterLevel);
  }else{
    //postJSON(MaBoGa, round((double)Peak)/100, relayOn, fault, digitalRead(waterPin1), digitalRead(waterPin2),digitalRead(waterPin3), digitalRead(waterPin4));
    postJSON(MaBoGa, round((double)Peak)/100, relayOn, fault, WaterLevel);
  }
}
//bool postJSON(String _maboga, double _giatricambien, byte _trangthai, byte _f, int _n1, int _n2, int _n3, int _n4){
//  http.begin(POST_URL);
//  http.addHeader("Content-Type", "application/json");
//  JSONVar _json;
//  _json[Object1]= _maboga;
//  _json[Object2]= _giatricambien;
//  _json[Object3]= _f;
//  _json[Object4]= _trangthai;
//  _json[Object5]= _n1;
//  _json[Object6]= _n2;
//  _json[Object7]= _n3;
//  _json[Object8]= _n4;
//  int httpCodePost = http.POST(JSON.stringify(_json));   //Send the request
//  if(DB) Serial.printf("httpCodePost: %i\r\n", httpCodePost);
//  http.end();
//  if(httpCodePost >= 200 && httpCodePost < 300){
//    return true;
//  }else return false;
//}
//bool postJSON(String _maboga, double _giatricambien, byte _trangthai, byte _f, int _n1, int _n2, int _n3){
//  http.begin(POST_URL);
//  http.addHeader("Content-Type", "application/json");
//  JSONVar _json;
//  _json[Object1]= _maboga;
//  _json[Object2]= _giatricambien;
//  _json[Object3]= _f;
//  _json[Object4]= _trangthai;
//  _json[Object5]= _n1;
//  _json[Object6]= _n2;
//  _json[Object7]= _n3;
//  _json[Object8]= -1;
//  int httpCodePost = http.POST(JSON.stringify(_json));   //Send the request
//  if(DB) Serial.printf("httpCodePost: %i\r\n", httpCodePost);
//  http.end();
//  if(httpCodePost >= 200 && httpCodePost < 300){
//    return true;
//  }else return false;
//}
//bool postJSON(String _maboga, double _giatricambien, byte _trangthai, byte _f, int _n1, int _n2){
//  http.begin(POST_URL);
//  http.addHeader("Content-Type", "application/json");
//  JSONVar _json;
//  _json[Object1]= _maboga;
//  _json[Object2]= _giatricambien;
//  _json[Object3]= _f;
//  _json[Object4]= _trangthai;
//  _json[Object5]= _n1;
//  _json[Object6]= _n2;
//  _json[Object7]= -1;
//  _json[Object8]= -1;
//  int httpCodePost = http.POST(JSON.stringify(_json));   //Send the request
//  if(DB) Serial.printf("httpCodePost: %i\r\n", httpCodePost);
//  http.end();
//  if(httpCodePost >= 200 && httpCodePost < 300){
//    return true;
//  }else return false;
//}

bool postJSON(String _maboga, double _giatricambien, byte _trangthai, byte _f, int _n1){
  http.begin(POST_URL);
  http.addHeader("Content-Type", "application/json");
  JSONVar _json;
  _json[Object1]= _maboga;
  _json[Object2]= _giatricambien;
  _json[Object3]= _f;
  _json[Object4]= _trangthai;
  _json[Object5]= _n1;
//  _json[Object6]= -1;
//  _json[Object7]= -1;
//  _json[Object8]= -1;
  int httpCodePost = http.POST(JSON.stringify(_json));   //Send the request
  if(DB){ 
    Serial.printf("httpCodePost: %i\r\n", httpCodePost);
    Serial.println(JSON.stringify(_json));
  }
  //http.end();
  if(httpCodePost >= 200 && httpCodePost < 300){
    return true;
  }else return false;
}

//-----------------No change below will fine!---------------------------
void debug(){
  while(Serial.available()){
      char inChar = (char)Serial.read();
      if(inChar == '1'){
        relayOn = 1;
        Serial.println("ON");
      } else if(inChar == '2'){
        relayOn = 0;
        Serial.println("OFF");
      } else if(inChar == '3'){
        setupWifiDB();
      }
  }
}
void checkTimer(){
  if((millis() - StartTimer.t100ms) >= 100){
    Flag.t100ms = true;
    StartTimer.t100ms = millis();
  }
  if((millis() - StartTimer.t250ms) >= 250){
    Flag.t250ms = true;
    StartTimer.t250ms = millis();
  }
  if((millis() - StartTimer.t500ms) >= 500){
    Flag.t500ms = true;
    StartTimer.t500ms = millis();
  }
  if((millis() - StartTimer.t1s) >= 1000){
    Flag.t1s = true;
    StartTimer.t1s = millis();
  }
  if((millis() - StartTimer.t5s) >= 5000){
    Flag.t5s = true;
    StartTimer.t5s = millis();
  }
  if((millis() - StartTimer.t10s) >= 10000){
    Flag.t10s = true;
    StartTimer.t10s = millis();
  }
}
void FalseTheFlag(){
  Flag.t100ms = false;
  Flag.t250ms = false;
  Flag.t500ms = false;
  Flag.t1s = false;
  Flag.t5s = false;
  Flag.t10s = false;
}
