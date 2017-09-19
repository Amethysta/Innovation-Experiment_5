#include<ESP8266.h>
#include<AM2321.h>
#include<Servo.h>
#include<String.h>
#include <Adafruit_GPS.h>
#include "MPU6050_6Axis_Microduino.h"

#define SSID            "hahaha"
#define PASSWORD        "8888kkkk"
#define HOST_NAME       "api.heclouds.com"
#define DEVICEID        "9278381"
#define HOST_PORT       (80)
#define Shake_Port      A0
#define PIN_KEY         4
#define INTERVAL_SHORT  2000

String ApiKey = "=mPIlIHprvWbHBsnOBMLAow2jPs=";

char DataToSend[10];
String JsonToSend;
String PostString;

Servo Servo_8;
ESP8266 wifi(Serial);
MPU6050 mpu;
Adafruit_GPS GPS(&Serial1);

unsigned long sensorlastTime = millis();         //运行间隔计时器
float Temp, Humi;                                //温湿度
float latitude,longitude;                        //经纬度
float Ther_Temp, Ther_Humi, Ther_Shake,Ther_Yaw; //报警阈值
float ypr[3];                                    //欧拉角
int Shake_State;                                 //震动强度
String goods_id="13627722",dlv_id="13624133";                          //商品ID 快递员ID
bool lock_state;                                 //开关锁指令
bool locked = 0;                                 //locked初始化为0,代表程序刚启动时箱子是开着的

void setup(void)
{
  Serial.begin(115200);
  GPS.begin(38400);
  pinMode(Shake_Port, INPUT);
   mpu.begin(MODE_DMP); 
   Servo_8.attach(8);
  
  
  wifi.setUart(115200,DEFAULT_PATTERN);
  wifi.joinAP(SSID, PASSWORD);                    //连接wifi

  getThershold();                                 //获取阈值  
  getgoodsid();                                   //获取商品ID
  getdlvid();                                     //获取快递员ID
  //Serial.println(dlv_id);
  //Serial.println(goods_id);
  
  pinMode(PIN_KEY, INPUT);
  while(!locked)
  {
    if(!digitalRead(PIN_KEY))
    {sendlockdata();locked = 1;}                  //当摁下开关时,上传关锁指令,同时跳出循环进行正常周期工作
  }
}

void loop(void)
{  
   /*------------GPS定位并存储经纬度------------*/
  char c=GPS.read();
  if (GPS.newNMEAreceived()) 
  {
    if (GPS.parse(GPS.lastNMEA()))
    {
      latitude = GPS.latitude;
      longitude = GPS.longitude;
      lat_lon_transform();                          //将GPS原格式:dddmmmss转换成十进制:ddd.dd    
    }
  }
  /*------------工作循环------------*/
  if (sensorlastTime > millis())
  {sensorlastTime = millis();}   
  if (millis() - sensorlastTime > INTERVAL_SHORT) 
  {
    /*------------传感器读取各项信息------------*/
    AM2321 am2321;
    am2321.read();
    Temp = am2321.temperature / 10.0;
    Humi = am2321.humidity / 10.0;
    mpu.getYawPitchRoll(ypr);
   // Serial.print("mpu np");
    Shake_State = analogRead(Shake_Port);
    /*------------上传/读取数据------------*/    
    
      getlockstate();
      if (lock_state)
        {Servo_8.write(90);}
      else           
        {Servo_8.write(0);}
        wifi.createTCP(HOST_NAME, HOST_PORT);
      if(digitalRead(PIN_KEY)){sendgoodsalarm();}
      updateWheatherData();
      if(Shake_State>100) { sendshakedata(); }          //只上传那些震动较为剧烈的点,以减少数据量
      sendalarm();      
    wifi.releaseTCP(); 
    sensorlastTime = millis();
  }  
  
}

void lat_lon_transform()
{
  latitude = (int(latitude)/100)+((int(latitude)%100)/60.0)+((latitude-int(latitude))/60.0);
  longitude = (int(longitude)/100)+((int(longitude)%100)/60.0)+((longitude-int(longitude))/60.0);
}
void getdlvid()
{
  /*------------建立连接------------*/
   wifi.createTCP(HOST_NAME, HOST_PORT);
  /*------------构造上传字符串------------*/
  PostString = "GET /devices/";
  PostString += DEVICEID;
  PostString += "/datastreams/dlv_id HTTP/1.1";
  PostString += "\r\n";
  PostString += "api-key:";
  PostString += ApiKey;
  PostString += "\r\n";
  PostString += "Host:api.heclouds.com\r\n\r\n";
  /*------------上传与接收------------*/
  const char *PostArray = PostString.c_str();
  uint8_t buffer[350] = {0};
  wifi.send((const uint8_t*)PostArray, strlen(PostArray));   
  wifi.recv(buffer, sizeof(buffer));
  /*------------处理返回数据------------*/
  int i = searchforvalue(buffer);//模式匹配算法,寻找收到的json中的有效数值
  char id[8] = {(char)buffer[i+1],(char)buffer[i+2],(char)buffer[i+3],(char)buffer[i+4],(char)buffer[i+5],(char)buffer[i+6],(char)buffer[i+7],(char)buffer[i+8]};
  String temid=id;
  dlv_id=temid.substring(0,8);
  PostArray = NULL;
  wifi.releaseTCP();
}
void sendgoodsalarm()
{
  String JsonToSendGoods;
  String PostStringGoods;
  /*------------构造上传json包------------*/
  JsonToSendGoods ="{";
  
  if(Temp>=Ther_Temp)           {JsonToSendGoods += "\"ala_temp\":";   JsonToSendGoods += "1,";}  
  if(Humi>=Ther_Humi)           {JsonToSendGoods += "\"ala_humi\":";  JsonToSendGoods += "1,";}  
  if(Shake_State>=Ther_Shake)   {JsonToSendGoods += "\"ala_shk\":";   JsonToSendGoods += "1,";}
  if(abs(ypr[2])>=Ther_Yaw)     {JsonToSendGoods += "\"ala_flip\":";  JsonToSendGoods += "1,";}  
  if(digitalRead(PIN_KEY))      {JsonToSendGoods += "\"lock\":";      JsonToSendGoods += "1,";} 
  JsonToSendGoods += "\"latitude\":";
  dtostrf(latitude, 1, 2, DataToSend);
  JsonToSendGoods += String(DataToSend) ;  
  JsonToSendGoods += ",\"longitude\":";
  dtostrf(longitude, 1, 2, DataToSend);
  JsonToSendGoods += String(DataToSend);    
  JsonToSendGoods += ",\"deliver\":"; 
  JsonToSendGoods += dlv_id;  
  JsonToSendGoods += "}";
  /*------------构造上传字符串------------*/
  PostStringGoods = "POST /devices/";
  PostStringGoods += goods_id;
  PostStringGoods += "/datapoints?type=3 HTTP/1.1";
  PostStringGoods += "\r\n";
  PostStringGoods += "api-key:";
  PostStringGoods += ApiKey;
  PostStringGoods += "\r\n";
  PostStringGoods += "Host:api.heclouds.com\r\n";
  PostStringGoods += "Content-Length:";
  PostStringGoods += JsonToSendGoods.length();
  PostStringGoods += "\r\n";
  PostStringGoods += "\r\n";
  PostStringGoods += JsonToSendGoods;
  PostStringGoods += "\r\n";
  PostStringGoods += "\r\n";
  PostStringGoods += "\r\n";
  /*------------发送字符串------------*/
  const char *PostArray = PostStringGoods.c_str();
  wifi.send((const uint8_t*)PostArray, strlen(PostArray));  
  PostArray = NULL;
  JsonToSendGoods="";
}

int searchforvalue(uint8_t str[])
{
  char s_str[]="value\":";
  int j = 0;
  int i = 0;
  for(;(char)str[i] != '\0' && s_str[j] != '\0';i++)
  {
    if(str[i] == s_str[j]){j++;}
    else{i = i - j + 1;j = 0;}
  }
  return i;
}
void getgoodsid()
{
    
  wifi.createTCP(HOST_NAME, HOST_PORT);
  
  PostString = "GET /devices/";
  PostString += DEVICEID;
  PostString += "/datastreams/goods_id HTTP/1.1";
  PostString += "\r\n";
  PostString += "api-key:";
  PostString += ApiKey;
  PostString += "\r\n";
  PostString += "Host:api.heclouds.com\r\n\r\n";
  
  const char *PostArray = PostString.c_str();
  uint8_t buffer[320] = {0};
  wifi.send((const uint8_t*)PostArray, strlen(PostArray));  
  wifi.recv(buffer, sizeof(buffer));
  
  int i = searchforvalue(buffer);
  char id[8] = {(char)buffer[i+1],(char)buffer[i+2],(char)buffer[i+3],(char)buffer[i+4],(char)buffer[i+5],(char)buffer[i+6],(char)buffer[i+7],(char)buffer[i+8]};
  String temid = id;
  goods_id = temid.substring(0,8);
  PostArray = NULL;
  wifi.releaseTCP();
}


void updateWheatherData()
{  
  JsonToSend = "{\"Temperature\":";
  dtostrf(Temp, 1, 1, DataToSend);
  JsonToSend += String(DataToSend);
  JsonToSend += ",\"Humidity\":";
  dtostrf(Humi, 1, 1, DataToSend);
  JsonToSend += String(DataToSend);

  JsonToSend += ",\"latitude\":";
  dtostrf(latitude, 1, 2, DataToSend);
  JsonToSend += String(DataToSend);
  JsonToSend += ",\"longitude\":";
  dtostrf(longitude, 1, 2, DataToSend);
  JsonToSend += String(DataToSend);
  JsonToSend += "}";

  PostString = "POST /devices/";
  PostString += DEVICEID;
  PostString += "/datapoints?type=3 HTTP/1.1";
  PostString += "\r\n";
  PostString += "api-key:";
  PostString += ApiKey;
  PostString += "\r\n";
  PostString += "Host:api.heclouds.com\r\n";
  PostString += "Content-Length:";
  PostString += JsonToSend.length();
  PostString += "\r\n";
  PostString += "\r\n";
  PostString += JsonToSend;
  PostString += "\r\n";
  PostString += "\r\n";
  PostString += "\r\n";

  const char *PostArray = PostString.c_str();
  wifi.send((const uint8_t*)PostArray, strlen(PostArray));

  PostArray = NULL; 
}
void sendshakedata()
{
  JsonToSend = "{\"Shake\":";
  dtostrf(Shake_State, 1, 0, DataToSend);
  JsonToSend += String(DataToSend) + "}";
  
  PostString = "POST /devices/";
  PostString += DEVICEID;
  PostString += "/datapoints?type=3 HTTP/1.1";
  PostString += "\r\n";
  PostString += "api-key:";
  PostString += ApiKey;
  PostString += "\r\n";
  PostString += "Host:api.heclouds.com\r\n";
  PostString += "Content-Length:";
  PostString += JsonToSend.length();
  PostString += "\r\n";
  PostString += "\r\n";
  PostString += JsonToSend;
  PostString += "\r\n";
  PostString += "\r\n";
  PostString += "\r\n";

  const char *PostArray = PostString.c_str();
  wifi.send((const uint8_t*)PostArray, strlen(PostArray));

  PostArray = NULL; 
}
void sendlockdata()
{
  wifi.createTCP(HOST_NAME,HOST_PORT);
  
  JsonToSend="{\"lock\":0}";
  
  PostString = "POST /devices/";
  PostString += DEVICEID;
  PostString += "/datapoints?type=3 HTTP/1.1";
  PostString += "\r\n";
  PostString += "api-key:";
  PostString += ApiKey;
  PostString += "\r\n";
  PostString += "Host:api.heclouds.com\r\n";
  PostString += "Content-Length:";
  PostString += JsonToSend.length();
  PostString += "\r\n";
  PostString += "\r\n";
  PostString +=JsonToSend;
  PostString += "\r\n";
  PostString += "\r\n";
  PostString += "\r\n";

  const char *PostArray = PostString.c_str();
  wifi.send((const uint8_t*)PostArray, strlen(PostArray));

  PostArray = NULL; 
  wifi.releaseTCP();
}
void getThershold()
{  
  wifi.createTCP(HOST_NAME, HOST_PORT);
  
  PostString = "GET /devices/";
  PostString += DEVICEID;
  PostString += "/datastreams?datastream_ids=thretemp HTTP/1.1";
  PostString += "\r\n";
  PostString += "api-key:";
  PostString += ApiKey;
  PostString += "\r\n";
  PostString += "Host:api.heclouds.com\r\n";
  PostString += "\r\n";
  
  const char *PostArray = PostString.c_str();
  uint8_t buffer[350];
  wifi.send((const uint8_t*)PostArray, strlen(PostArray));
  wifi.recv(buffer, sizeof(buffer));
  
  int i=searchforvalue(buffer);
  char t[4]={(char)buffer[300],(char)buffer[301],(char)buffer[302],(char)buffer[303]};
  Ther_Temp=atoi(t);
  
  PostString = "GET /devices/";
  PostString += DEVICEID;
  PostString += "/datastreams?datastream_ids=threhumi HTTP/1.1";
  PostString += "\r\n";
  PostString += "api-key:";
  PostString += ApiKey;
  PostString += "\r\n";
  PostString += "Host:api.heclouds.com\r\n";
  
  PostString += "\r\n";
  PostArray = PostString.c_str();
  wifi.send((const uint8_t*)PostArray, strlen(PostArray));
  wifi.recv(buffer, sizeof(buffer) );
  
  i=searchforvalue(buffer);
  char h[4]={(char)buffer[300],(char)buffer[301],(char)buffer[302],(char)buffer[303]};
  Ther_Humi=atoi(h);
  
  PostString = "GET /devices/";
  PostString += DEVICEID;
  PostString += "/datastreams?datastream_ids=threshck HTTP/1.1";
  PostString += "\r\n";
  PostString += "api-key:";
  PostString += ApiKey;
  PostString += "\r\n";
  PostString += "Host:api.heclouds.com\r\n";
  PostString += "\r\n";
  
  PostArray = PostString.c_str();
  wifi.send((const uint8_t*)PostArray, strlen(PostArray));
  wifi.recv(buffer, sizeof(buffer));
  
  i=searchforvalue(buffer);
  char s[4]={(char)buffer[300],(char)buffer[301],(char)buffer[302],(char)buffer[303]};
  Ther_Shake=atoi(s);
  
  PostString = "GET /devices/";
  PostString += DEVICEID;
  PostString += "/datastreams?datastream_ids=threyaaw HTTP/1.1";
  PostString += "\r\n";
  PostString += "api-key:";
  PostString += ApiKey;
  PostString += "\r\n";
  PostString += "Host:api.heclouds.com\r\n";
  PostString += "\r\n";
  
  PostArray = PostString.c_str();
  wifi.send((const uint8_t*)PostArray, strlen(PostArray));
  wifi.recv(buffer, sizeof(buffer) );
  
  i=searchforvalue(buffer);
  char y[4]={(char)buffer[300],(char)buffer[301],(char)buffer[302],(char)buffer[303]};
  Ther_Yaw=atoi(y);

//Serial.println(Ther_Temp);
  wifi.releaseTCP();
  PostArray = NULL;
}

void getlockstate()
{ 
  wifi.createTCP(HOST_NAME, HOST_PORT);
  PostString = "GET /devices/";
  PostString += DEVICEID;
  PostString += "/datastreams/lock HTTP/1.1";
  PostString += "\r\n";
  PostString += "api-key:";
  PostString += ApiKey;
  PostString += "\r\n";
  PostString += "Host:api.heclouds.com\r\n\r\n";
  
  const char *PostArray = PostString.c_str();
  uint8_t buffer[350] = {0};
  wifi.send((const uint8_t*)PostArray, strlen(PostArray));  
  uint64_t len = wifi.recv(buffer, sizeof(buffer));

  PostArray = NULL;
  
  if ((char)buffer[295] == '1')lock_state=1;
  else lock_state=0;
  wifi.releaseTCP();
}
void sendalarm()
{  
  JsonToSend = "{\"ala_temp\":";  
  if(Temp>=Ther_Temp)  {JsonToSend += "1";sendgoodsalarm();}
  else{JsonToSend += "0";}
  
  JsonToSend += ",\"ala_humi\":";  
  if(Humi>=Ther_Humi)  {JsonToSend += "1";sendgoodsalarm();}
  else{JsonToSend += "0";}
  
  JsonToSend += ",\"ala_shk\":";  
  if(Shake_State>=Ther_Shake)  {JsonToSend += "1";sendgoodsalarm();}
  else{JsonToSend += "0";}

  JsonToSend += ",\"ala_flip\":";  
  if(abs(ypr[2])>=Ther_Yaw)  {JsonToSend += "1";sendgoodsalarm();}
  else{JsonToSend += "0";}
  JsonToSend += "}";

  PostString = "POST /devices/";
  PostString += DEVICEID;
  PostString += "/datapoints?type=3 HTTP/1.1";
  PostString += "\r\n";
  PostString += "api-key:";
  PostString += ApiKey;
  PostString += "\r\n";
  PostString += "Host:api.heclouds.com\r\n";
  PostString += "Content-Length:";
  PostString += JsonToSend.length();
  PostString += "\r\n";
  PostString += "\r\n";
  PostString += JsonToSend;
  PostString += "\r\n";
  PostString += "\r\n";
  PostString += "\r\n";

  const char *PostArray = PostString.c_str();
  wifi.send((const uint8_t*)PostArray, strlen(PostArray));
  
  PostArray=NULL;  
}
