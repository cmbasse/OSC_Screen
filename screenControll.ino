#include <FS.h>  
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h> 
#include <ArduinoJson.h>
/**OSC Requirments**/ 
#include <WiFiUdp.h>
#include <OSCMessage.h>
#include <OSCBundle.h>
#include <OSCData.h>
#include <SoftwareSerial.h>
/**OTA Requirments**/
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>

//default custom static IP
char static_ip[16] = "192.168.1.88";
char static_gw[16] = "192.168.1.1";
char static_sn[16] = "255.255.255.0";

//flag for saving data
bool shouldSaveConfig = false;

//OSC setup
WiFiUDP Udp;
IPAddress qLabIP(192,168,1,200); //qLab IP
unsigned int qLabPort = 53000; //qLab port
unsigned int localPort = 8888; // OSC port
OSCErrorCode error;
byte lock = 0; // only execute 1 OSC comand at a time

//projector setup
SoftwareSerial projSerial(14, 12, false, 256); //setup projector connection

//webserver setup
ESP8266WebServer server(80);

//Local intialization. Once its business is done, there is no need to keep it around
WiFiManager wifiManager;

char up = 5;
char down = 4;

char INDEX_HTML[640] =
"<!DOCTYPE HTML>"
"<html>"
"<head>"
"<meta name = \"viewport\" content = \"width = device-width, initial-scale = 1.0, maximum-scale = 1.0, user-scalable=0\">"
"<title>OSC Projector Config</title>"
"<style>"
"\"body { background-color: #808080; font-family: Arial, Helvetica, Sans-Serif; Color: #000000; }\""
"</style>"
"</head>"
"<body>"
"<h1>QLab Configuration</h1>"
"<FORM action=\"/\" method=\"post\">"
"<p>QLab IP:<INPUT type=\"text\" name=\"qLabIP\" value=\"%s\"></p>"
"<p>QLab Port:<INPUT type=\"text\" name=\"qLabPort\" value=\"%i\"></p>"
"<INPUT type=\"submit\" value=\"Send\"> <INPUT type=\"reset\">"
"</FORM>"
"</body>"
"</html>";


//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

void displayIndex(){
   byte oct1 = qLabIP[0];
   byte oct2 = qLabIP[1];
   byte oct3 = qLabIP[2];
   byte oct4 = qLabIP[3];

   char s[16];  
   sprintf(s, "%d.%d.%d.%d", oct1, oct2, oct3, oct4);
   char html[640];
   sprintf(html, INDEX_HTML, s, qLabPort);
   server.send(200, "text/html", html);
}

void handleRoot(){
  if (server.hasArg("qLabPort")) {
    handleSubmit();
  } else {
     displayIndex();
  }
}

void handleSubmit(){
  if (!server.hasArg("qLabPort")) return returnFail("BAD ARGS");
  qLabPort = server.arg("qLabPort").toInt();
  String ipTemp = server.arg("qLabIP");
  qLabIP.fromString(ipTemp);
  updateConfig();
  ESP.reset();
}

void returnFail(String msg){
  server.sendHeader("Connection", "close");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(500, "text/plain", msg + "\r\n");
}

void handleNotFound(){
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i=0; i<server.args(); i++){
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

void updateConfig(){
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();

    json["ip"] = WiFi.localIP().toString();
    json["gateway"] = WiFi.gatewayIP().toString();
    json["subnet"] = WiFi.subnetMask().toString();
    json["qlabport"] = String(qLabPort);

    byte oct1 = qLabIP[0];
    byte oct2 = qLabIP[1];
    byte oct3 = qLabIP[2];
    byte oct4 = qLabIP[3];

    char s[16];  
    sprintf(s, "%d.%d.%d.%d", oct1, oct2, oct3, oct4);     
    json["qlabip"] = String(s);

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.prettyPrintTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
}

void setup() {
  pinMode(up, OUTPUT);
  pinMode(down, OUTPUT);
  
  digitalWrite(up, HIGH); 
  digitalWrite(down, HIGH); 

  Serial.begin(115200);
  //clean FS, for testing
  //SPIFFS.format();

  //read configuration from FS json
  Serial.println("mounting FS...");

  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);
        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nparsed json");
          if(json["ip"]) {
            char tmp_port[5];
            char tmp_ip[19];
            Serial.println("setting custom ip from config");
            strcpy(static_ip, json["ip"]);
            strcpy(static_gw, json["gateway"]);
            strcpy(static_sn, json["subnet"]);
            strcpy(tmp_port, json["qlabport"]);
            strcpy(tmp_ip, json["qlabip"]);
            qLabPort = atoi(tmp_port);
            qLabIP.fromString(tmp_ip);
          } else {
            Serial.println("no custom ip in config");
          }
        } else {
          Serial.println("failed to load json config");
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  //end read

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //set static ip 
  IPAddress _ip,_gw,_sn;
  _ip.fromString(static_ip);
  _gw.fromString(static_gw);
  _sn.fromString(static_sn);
  
  wifiManager.setSTAStaticIPConfig(_ip, _gw, _sn);

  //tries to connect to last known settings
  //if it does not connect it starts an access point with the specified name
  //here  "OSC_Projector_Setup" with password "projector"
  //and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect("OSC_Screen_Setup", "screen1976")) {
    Serial.println("failed to connect, we should reset as see if it connects");
    delay(3000);
    ESP.reset();
    delay(5000);
  }

  //if you get here you have connected to the WiFi
  Serial.println("connected...yeey :)");

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    updateConfig();
  }

  Serial.println("Starting UDP");
  Udp.begin(localPort);
  Serial.print("Local port: ");
  Serial.println(Udp.localPort());

  Serial.println("local ip");
  Serial.println(WiFi.localIP());
  Serial.println(WiFi.gatewayIP());
  Serial.println(WiFi.subnetMask());

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname("screenOSC");

  // No authentication by default
   ArduinoOTA.setPassword((const char *)"screen1223");

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();

  server.on("/", handleRoot);  
  server.onNotFound(handleNotFound);
  server.begin(); 
}

/* Raise the screen
 */
void screenUp(OSCMessage &msg){  
  Serial.print("Screen: ");
  Serial.println(" Up"); 
  if(msg.getInt(0)){    
    digitalWrite(up, LOW); 
    delay(1000);
    digitalWrite(up, HIGH);
  }
}

/* Raise the screen for a specific time
 */
void upTime(OSCMessage &msg){  
  Serial.print("Screen: ");
  Serial.println(" Up"); 
  if(msg.getInt(0)){    
    digitalWrite(up, LOW); 
    delay(1000);
    digitalWrite(up, HIGH);
    
    int wait = (abs(msg.getInt(0)) > 50000)? 50000 : abs(msg.getInt(0));
    delay(wait);

    digitalWrite(up, LOW);
    digitalWrite(down, LOW); 
    delay(1000);
    digitalWrite(up, HIGH);
    digitalWrite(down, HIGH); 
  }
}

/* Lower the screen
 */
void screenDown(OSCMessage &msg){  
  Serial.print("Screen: ");
  Serial.println(" Down"); 
  if(msg.getInt(0)){    
    digitalWrite(down, LOW); 
    delay(1000);
    digitalWrite(down, HIGH);
  }
}

/* Lower the screen for a specific time
 */
void downTime(OSCMessage &msg){  
  Serial.print("Screen: ");
  Serial.println(" Up"); 
  if(msg.getInt(0)){    
    digitalWrite(down, LOW); 
    delay(1000);
    digitalWrite(down, HIGH);
    
    int wait = (abs(msg.getInt(0)) > 50000)? 50000 : abs(msg.getInt(0));
    delay(wait);

    digitalWrite(up, LOW);
    digitalWrite(down, LOW); 
    delay(1000);
    digitalWrite(up, HIGH);
    digitalWrite(down, HIGH); 
  }
}

/* Stop the screen
 */
void screenStop(OSCMessage &msg){  
  Serial.print("Screen: ");
  Serial.println(" Stop"); 
  if(msg.getInt(0)){    
    digitalWrite(up, LOW);
    digitalWrite(down, LOW); 
    delay(1000);
    digitalWrite(up, HIGH);
    digitalWrite(down, HIGH); 
  }
}

void loop() {
  ArduinoOTA.handle();
  server.handleClient();
  
  OSCMessage msg;
  int size = Udp.parsePacket();

  if (size > 0) {
    while (size--) {
      msg.fill(Udp.read());
    }
    if (!msg.hasError()) {
      msg.dispatch("/screen/up", screenUp);
      msg.dispatch("/screen/down", screenDown);
      msg.dispatch("/screen/stop", screenStop);
      msg.dispatch("/screen/up/time", upTime);
      msg.dispatch("/screen/down/time", downTime);
    } else {
      error = msg.getError();
      Serial.print("error: ");
      Serial.println(error);
    }
  }

 
}



