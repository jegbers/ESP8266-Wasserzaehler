// ****************************************************************
// Code for Wasserzaehler
// Jochen Egbers,   2019-11-01 - OTA added
// Jochen Egbers,   2019-10-23 - Changed to Wasserzaehler
// Code for GasMeter 
// Jochen Egbers,   2019-10-12 - WiFi-Manager added
// Jochen Egbers,   2019-10-09
// Alexander Kabza, 2018-04-08
// Alexander Kabza, 2017-03-15
// 
// ****************************************************************


#include <ESP8266WiFi.h>          //ESP8266 Core WiFi Library (you most likely already have this in your sketch)

#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     //Local WebServer used to serve the configuration portal
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic
#include <PubSubClient.h>         //https://https://pubsubclient.knolleary.net/ MQTT Client
#include <ArduinoOTA.h>
#include <RTCVars.h>              //for saving reset_counter, value and volt for unexpected reboot

ESP8266WebServer server(80);   //Web server object. Will be listening in port 80 (default for HTTP)

#define CODEVERSION "ESP8266_Wasserzaehler - 20191102"
#define LED_PIN 2
#define IN_PIN 4

// ##################### For RTCVars ####################
RTCVars state;                // create the state object
int reset_counter;            // we want to keep these values after reset
float volt;
long int value;


// ###################### For OTA ######################
bool ota_flag = true;
uint16_t time_elapsed = 0;

// ####################### For MQTT #######################
//"5.196.95.208"; // test.mosquitto.org
const char* mqttServer="5.196.95.208";
const int mqttPort=1883;
const char* mqttUser="";           // not used
const char* mqttPassword="";       // not used
#define TOKEN1 "joe_berlin/wasserzaehler/value"
#define TOKEN2 "joe_berlin/wasserzaehler/voltage"
//String SubscribeString="";
WiFiClient espClient;
PubSubClient client(espClient);

// ##################### For ADC-Measurement #####################
ADC_MODE(ADC_VCC);
unsigned int raw=0;
// float volt=0.0;
String Ubatt = "";

int actual = 1;
int prev = 1;
//long int value = 0;
//long int value;
//float value1=0.0;
float value1;
String MACStr;
String IPStr;

// ####################### ToHex ##############################

String ToHEX(unsigned char* bytearray, uint8_t arraysize){
  String str = "";
  for (uint8_t i = 0; i < arraysize; i++){
    if (i > 0) str += ":";
    if (bytearray[i] < 16) str += String(0, HEX);
    str += String(bytearray[i], HEX);
  }
  return str;
}

// ######################## Web-Server: handleAll ###################################

void handleAll() { //Handler
 
 String message = "";
 message += "\n";
 message += "ESP8266_Wasserzaehler\n";
 message += "=====================\n";
 message += "Version ";
 message += CODEVERSION;
 message += "\n";
 message += "SSID " + WiFi.SSID() + "\n";
 message += "IP " + WiFi.localIP().toString() + "\n";
 message += "MAC " + MACStr + "\n";
 message += "MQTT-Token ";
 message += TOKEN1;
 message += ", ";
 message += TOKEN2;
 message += "\n";
 message += String(int(reset_counter)) + " restarts after coldboot\n";
 message += "\n";
 message += "help: \n";
 message += "use /SetValue?Value=xx to set new value  with xx like 1234.56\n";
 message += "use /WiFiReset to reset WiFi-Settings \n";
 message += "use /Restart to restart the system \n";
 message += "\n";
 message += "aktuelle Messwerte: \n";
 message += "Wasserzaehler value=" + String(float(value)/1000) + " m^3\n";
 message += "Wasserzaehler voltage=" + String(float(volt)) + " V\n";
 message += "\n";

 server.send(200, "text/plain", message);       //Response to the HTTP request
}

// ######################## Web-Server: handleGenericArgs ###################################

void handleGenericArgs() { //Handler

  String message = "Number of args received:";
  message += server.args();            //Get number of parameters
  message += "\n";                            //Add a new line

  for (int i = 0; i < server.args(); i++) {
  
    message += "Arg no" + (String)i + " – ";   //Include the current iteration value
    message += server.argName(i) + ": ";     //Get the name of the parameter
    message += server.arg(i) + "\n";              //Get the value of the parameter
  }   
  server.send(200, "text/plain", message);       //Response to the HTTP request
}

// ######################## Web-Server: handleSetValue ###################################

void handleSetValue() { 

  String message = "";
  String valuestr = "";

  if (server.arg("Value")== ""){     //Parameter not found

    message = "Value Argument not found";

  } else {     //Parameter found

    message = "New Value = ";
    message += server.arg("Value");        //Gets the value of the query parameter
    valuestr = server.arg("Value");
    value = valuestr.toFloat() * 1000;
    state.saveToRTC();                     // since we changed a state relevant variable, we store the new values
  }
  server.send(200, "text/plain", message); //Returns the HTTP response
}

// ######################## Web-Server: handleWiFiReset ###################################

void handleWiFiReset() {
    String message = "";
    message += "\n";
    message += "WiFiReset ok\n";
    server.send(200, "text/plain", message);          //Returns the HTTP response
    delay(2000);    
    Serial.println("Resetting WiFiManager Config");    
    WiFiManager wifiManager;
    wifiManager.resetSettings();
    Serial.println("restarting device... ");
    delay(1000);
    //ESP.restart(); // doesn`t work fully
    digitalWrite(D0, LOW);      
}

// ######################## Web-Server: handleRestart ###################################

void handleRestart() {
    String message = "";
    message += "\n";
    message += "Restart ok\n";
    server.send(200, "text/plain", message);          //Returns the HTTP response
    delay(2000);    
    Serial.println("Restarting ...");    
    delay(1000);
    ota_flag = true;
    //ESP.restart(); // doesn`t work fully
    digitalWrite(D0, LOW);      
}

void reconnect() {
  while (!client.connected()) {
     Serial.print("Reconnecting...");
     if (!client.connect("Wasserzaehler")) {
        Serial.print("failed, rc=");
        Serial.print(client.state());
        Serial.println(" retrying in 5 seconds");
        delay(5000);
     }
  }
}

//void callback(char* topic, byte* payload, unsigned int length) {
//  Serial.println("Receiving previous value");
//  for (int i=0; i<length; i++) {
//    SubscribeString += ((char)payload[i]);
//    value = SubscribeString.toFloat()*100;
//    Serial.print("value=");
//    Serial.println(value/100);
//  }
//}
// ************************ Setup ****************************************

void setup() {
  byte MAC[6];
  
  Serial.begin(115200);
  Serial.println("   ");
  Serial.print("Starting ");
  Serial.println(CODEVERSION);

  
  // ###################### RTCvars ####################
  state.registerVar( &reset_counter );  // we send a pointer to each of our variables
  state.registerVar( &value );
  state.registerVar( &volt );

  if (state.loadFromRTC()) {            // we load the values from rtc memory back into the registered variables
    reset_counter++;
    Serial.println("This is reset no. " + (String)reset_counter);
    state.saveToRTC();                  // since we changed a state relevant variable, we store the new values
  } else {
    reset_counter = 0;                  // cold boot part
    Serial.println("This seems to be a cold boot. We don't have a valid state on RTC memory");
    state.saveToRTC();
  }

  // #################### Defining Pins ###################
  pinMode(LED_PIN, OUTPUT);
  pinMode(IN_PIN, INPUT);
  pinMode(D0, OUTPUT);     //Vorbereitung für Reset per SW
  digitalWrite(D0, HIGH);  //Vorbereitung für Reset per SW
  

  // ################## WiFiManager #####################
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;

  //reset saved settings
  //wifiManager.resetSettings();
    
  //set custom ip for portal
  //wifiManager.setAPStaticIPConfig(IPAddress(10,0,1,1), IPAddress(10,0,1,1), IPAddress(255,255,255,0));

  //fetches ssid and pass from eeprom and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  //wifiManager.autoConnect("AutoConnectAP");
  //or use this for auto generated name ESP + ChipID
  wifiManager.autoConnect("Wasserzaehler");

  Serial.println("Start WiFi Station");
  WiFi.mode(WIFI_STA);
  //WiFi.begin(ssid, password); //Connect to the WiFi network
  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(400);
    Serial.print(".");
    digitalWrite(LED_PIN, LOW); // LED is ON
    delay(50);
    digitalWrite(LED_PIN, HIGH);  // LED is OFF
    delay(50);
  }
  Serial.println();
  Serial.print("Connected");
  //WiFi.config(ip, gateway, subnet);
  WiFi.macAddress(MAC);
  MACStr = ToHEX(MAC, sizeof(MAC));
  //Serial.println("");
  Serial.print("MAC = ");
  Serial.println(MACStr);

  while (WiFi.status() != WL_CONNECTED) { //Wait for connection
    digitalWrite(LED_PIN, LOW);  // LED is ON
    delay(50);
    digitalWrite(LED_PIN, HIGH); // LED is OFF
    delay(50);
  }

  digitalWrite(LED_PIN, LOW);  // LED is ON
  delay(900);
  digitalWrite(LED_PIN, HIGH); // LED is OFF

  Serial.print("WIFI connected to: ");
  Serial.println(WiFi.SSID());
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());  

  // ################ Web-Server ###################
  server.on("/genericArgs", handleGenericArgs); 
  server.on("/SetValue", handleSetValue);  
  server.on("/WiFiReset", handleWiFiReset); 
  server.on("/Restart", handleRestart);
  server.on("/", handleAll);   

  server.begin();                                       //Start the server
  Serial.println("Server listening");
  Serial.println("use /SetValue?Value=xx to set new value");
  actual = digitalRead(IN_PIN);
  
  client.setServer(mqttServer, mqttPort ); //default port for mqtt is 1883

  // ################# MQTT ##################### 
  //client.setCallback(callback); 
  
  while (!client.connected()) {
    Serial.println("Connecting to MQTT ...");
    if (client.connect("Wasserzaehler")) {
      Serial.println("MQTT connected");
    } else {
      Serial.print("failed with state ");
      Serial.print(client.state());
      delay(2000);
    }
  }

  //client.publish(TOKEN1);
  //client.subscribe(TOKEN1);
  
  // ####################### OTA #######################
  
  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname("esp8266_wasserzaehler");

  // No authentication by default
  ArduinoOTA.setPassword((const char *)"rad800");

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
    Serial.println("... Done");
} // end setup

// ************************** Loop **************************************

void loop() {

  // ################## waiting for OTA ##################
  if(ota_flag)
  {
     Serial.println("Waiting for OTA programming");
     while(time_elapsed < 15000)
     {       
        ArduinoOTA.handle();
        time_elapsed=millis();
        Serial.print(".");
        delay(20);
        //  Signalling with LED that is waiting for OTA programming
        digitalWrite(LED_PIN, HIGH);  // LED is OFF
        delay(20);
        digitalWrite(LED_PIN, LOW); // LED is ON
     }
     ota_flag = false;
     digitalWrite(LED_PIN, HIGH);  // LED is OFF
  }
  
  char buf[7];

  // #################### Web-Server ####################
  server.handleClient();         //Handling of incoming requests
  actual = digitalRead(IN_PIN);
  
  // #################### Looking fpr change in input signal ##################
  // LED schalten
  if (actual == 0) {
    digitalWrite(LED_PIN, LOW);  // LED is ON
    } else {
    digitalWrite(LED_PIN, LOW);  // LED is ON
    delay(10);
    digitalWrite(LED_PIN, HIGH); // LED is OFF
    }

  if (actual == 0 && actual != prev) {

    //value inkrementieren
    value++; 
    value1=float(value)/1000;
    //value1 seriell ausgeben
    Serial.print("Value=");
    Serial.print(value1);

    // Spannung messen
    int Vcc_Voltage = ESP.getVcc();
    volt = Vcc_Voltage/1000;
    Serial.print(" Spannung=");
    Serial.println(volt);
    
    state.saveToRTC();           
    
    // ########### Publish result to MQTT ################
    if (!client.connected()) {
        reconnect();
       }
    client.loop();
    //client.publish(TOKEN1, ftoa(value1, buf, 2));
    client.publish(TOKEN1,String(value1).c_str());
   
    //client.publish(TOKEN2, ftoa(volt, buf, 2));
    client.publish(TOKEN2,String(volt).c_str());
    Serial.println("MQTT gesendet");

  }
  prev = actual;
  delay(440); //Zykluszeit ca. 0,5s
  
} // end loop
// ****************************************************************

