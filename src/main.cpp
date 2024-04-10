// ===============================================================================
// LIBRARIES
// ===============================================================================
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <Arduino_JSON.h>

// #define ESP8266            // OPTIONAL: define type board family: [ESP8266, ESP32]
#if defined(ESP32)
  #include <WiFi.h>
  #include <AsyncTCP.h>
  #include "SPIFFS.h"         // OPTIONAL: available for SPIFFS in ESP32 only
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESPAsyncTCP.h>
  #include <LittleFS.h>       // OPTIONAL: Little file system for ESP8266
#endif

// ===============================================================================
// OPTIONS 
// ===============================================================================
#define useButton       // Use buttons
#define useBVAR         // Use Boolean control variables
#define useToggle       // Use toggle switches (ON - OFF)
#define usePWM          // Use analog output channels (PWM's)
#define useAVAR         // Use floating control variables
#define debug           // for debugging purpose only. Remove for final version.
#define Toledo          // OPTIONAL: Choose Wifi credentials [Cimanes, Toledo, apartment]
#define aFactor 10      // Factor for range of analog signals (10 -> one decimal; 100 -> 2 decimals). Match with JS!


// ===============================================================================
// MANAGE FILE SYSTEM AND COMMUNICATIONS 
// ===============================================================================
// Create AsyncWebServer object on port 80 and WebSocket object:
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
int cleanTimer = 0UL;

// Function to Initialize Wifi
void initWiFi() {
  #if defined(Cimanes)
    const char ssid[] = "Pepe_Cimanes";
    const char pass[] = "Cimanes7581" ;
  #elif defined(Toledo)
    const char ssid[] = "MIWIFI_HtR7" ;
    const char pass[] = "TdQTDf3H"    ;
  #elif defined(apartment)
    const char ssid[] = "HH71VM_309B_2.4G" ;
    const char pass[] = "Ey6DKct3"    ;
  #endif
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  Serial.print(F("Connecting to WiFi .."));
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(1000);
  }
  Serial.println(WiFi.localIP());
}

// Function to Initialize File System
void initFS() {
  if (!LittleFS.begin()) Serial.println(F("Error mounting File System"));
  // if (!SPIFFS.begin(true)) Serial.println("Error mounting File System");      // particular for SPIFFS in ESP32 only
  else Serial.println(F("File System mounted OK"));
}

// Function to notify all clients with a message containing the argument
void notifyClients(String msg) { ws.textAll(msg); }

// ===============================================================================
// Variables and functions defined to deal with buttons
// ===============================================================================
#ifdef useButton
  // Set output GPIO's used by the buttons (ON/OFF = GPIO2, AUTO/MAN = GPIO4 )
  #define statePin 2
  #define modePin 4
 
  // function "updateButton()": Replace placeholders found in HTML (%STATE%, %MODE%...) with their current value
  // Pass argument by reference "&var", so we can change its value inside the function:
  String updateButton(const String &var) {
   String feedback;
    if(var == "STATE") {
      if(digitalRead(statePin)) feedback = "ON";
      else feedback = "OFF";
    }
    else if (var == "MODE") {
      if(digitalRead(modePin)) feedback = "AUTO";
      else feedback = "MAN";
    }
    return feedback;
  }
#endif

// ===============================================================================
// TOGGLE SWITCHES (Digital outputs): Variables and functions
// ===============================================================================
#ifdef useToggle
  // Define the Digital Outputs to be controlled via toggle switches
    #define numDOs 2
    const byte arrDO[numDOs] = {12, 14};
  // Update toggle switch: return JSON object {"dfb":"12", "state":"1"}
  String updateDO(byte gpio){
    JSONVar jsonObj;
    jsonObj["dfb"] = gpio;                  // Number of the GPIO
    jsonObj["state"] = digitalRead(gpio);   // 0 or 1
    return JSON.stringify(jsonObj);         // JSON object converted into a String.
  }

#endif

// ===============================================================================
// ANALOG OUTPUTS (PWM): Variables and functions
// ===============================================================================
#ifdef usePWM
  // Define the PWM output channels and ranges
  #define numPWMs 2
  // const array with PWM config. in format "{channel, rangeMin, rangeMax}"
  // array to store and report PWM values
  const int arrPWM[numPWMs][4] = {{5, 0, 1000}, {15, 50, 350}};
  int PWMval[numPWMs] = {0, 0};

  // Update analog feedback of PWM: return JSON object {"afb":"5", "value":"15"}
  String updatePWM(byte index){
    JSONVar jsonObj;                      // Create JSON object for A.O. PWM's
    jsonObj["afb"] = arrPWM[index][0];    // Number of the PWM channel
    jsonObj["value"] = PWMval[index];     // converted value fo the A.O. in that channel
    return JSON.stringify(jsonObj);       // JSON object converted into a String.
  }
#endif

// ===============================================================================
// CONTROL ANALOG VARIABLES: Variables and functions
// ===============================================================================
#ifdef useAVAR
  // Define the PWM output channels and ranges
  #define numAVARS 2
  const char* AVAR[numAVARS] = {"tSET", "rhSET"};     // array with variable names 
  int AVARval[numAVARS] = {0, 0};                     // array to store variable values

  // Update analog feedback of control variable: return JSON object {"afb":"tSET", "value":"22"}
  String updateAVAR(byte index){
    JSONVar jsonObj;                      // Create JSON object for Floating Variables
    jsonObj["afb"] = AVAR[index];         // Variable name
    jsonObj["value"] = AVARval[index];    // Variable value
    return JSON.stringify(jsonObj);       // JSON object converted into a String.
  }
#endif

// ===============================================================================
// MANAGE MESSAGES FROM CLIENTS (via WebSocket)
// ===============================================================================
  // Callback function to run when we receive new data from the clients via WebSocket protocol:
  // AwsFrameInfo provides information about the WebSocket frame being processed:
  // typedef struct { bool final; AwsFrameType opcode; bool isMasked; uint64_t payloadLength; uint8_t mask[4]; } AwsFrameInfo;
  // (AwsFrameInfo*)arg: It's converting the void* pointer arg to a pointer of type AwsFrameInfo*. This allows the function to access the WebSocket frame information stored in the arg parameter.
  void handleWSMessage(void *arg, uint8_t *data, size_t len) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) { 
      data[len] = 0;
      const char* msg = (char*)data;
      JSONVar jsonObj = JSON.parse(msg);
      #ifdef debug 
        Serial.print(F("key - "));      
        Serial.print(jsonObj.keys()[0]); 
      #endif

      //------------------------------------------------------
      // Refresh feedback for ALL BUTTONS & TOGGLE SWITCHES (requested by JS when the page is loaded):
      //------------------------------------------------------
      // JS function onOpen(event)  --> msg = `{"all": "update"}`
      if (jsonObj.hasOwnProperty("all")) {        // Update all feedbacks when page loads.
        #ifdef useButton
          notifyClients(updateButton("STATE"));   // update Button field "state".
          notifyClients(updateButton("MODE"));    // update Button field "mode".
        #endif        
        #ifdef useToggle
          for (byte i:arrDO) { notifyClients(updateDO(i)); }
        #endif
        #ifdef usePWM
          for (byte i = 0; i < numPWMs; i++) { notifyClients(updatePWM(i)); }
        #endif
        #ifdef useAVAR
          for (byte i = 0; i < numAVARS; i++) { notifyClients(updateAVAR(i)); }
        #endif
      }

      //------------------------------------------------------
      // Update BUTTON (operate D.O. in ESP and feedback to JS):
      //------------------------------------------------------
      // JS function press(element) --> msg {"but":"XXX"} (XXX is the button ID)
      #ifdef useButton
        else if (jsonObj.hasOwnProperty("but")) {
          const char* butName =  jsonObj["but"];
          Serial.println(butName);
          if (strcmp(butName, "bON") == 0)  digitalWrite(statePin, 1);
          else if (strcmp(butName, "bOFF") == 0)  digitalWrite(statePin, 0);
          else if (strcmp(butName, "bAUTO") == 0)  digitalWrite(modePin, 1); 
          else if (strcmp(butName, "bMAN") == 0)   digitalWrite(modePin, 0);      
          notifyClients(butName+1);
        }
      #endif

      //------------------------------------------------------
      // Operate TOGGLE SWITCH (output in ESP and feedback to JS):
      //------------------------------------------------------
      // JS function toggle(element) --> msg {"d_o":"x"}
      #ifdef useToggle
        else if (jsonObj.hasOwnProperty("d_o")) {
          const byte DOchannel = byte(atoi(jsonObj["d_o"]));
          digitalWrite(DOchannel, !digitalRead(DOchannel));
          notifyClients(updateDO(DOchannel));
        }
      #endif  

      //------------------------------------------------------
      // Tune PWM A.O. (tune output in ESP and feedback to JS):
      //------------------------------------------------------
      // JS function tune(element, value) --> msg {"a_o":"x", "value":"xx"}
      #ifdef usePWM
        else if (jsonObj.hasOwnProperty("a_o")) {
          byte pwmIndex = 255;
          const byte pwmOutput = byte(atoi(jsonObj["a_o"]));
          for (byte i=0; i<numPWMs; i++) {
            if (pwmOutput == arrPWM[i][0]) { pwmIndex = i; break; }    // identify the output channel
          }
          if (pwmIndex == 255) return;
          PWMval[pwmIndex] = atoi(jsonObj["value"]);  // update array PWMval with new value (keep 1 decimal place only)
          analogWrite(pwmOutput, map(PWMval[pwmIndex], arrPWM[pwmIndex][1], arrPWM[pwmIndex][2], 0, 255));  // Change (mapped) output signal.
          notifyClients(updatePWM(pwmIndex));         // Send feedback to JS.
        }
      #endif

      //------------------------------------------------------
      // Set CONTROL ANALOG VARIABLE
      //------------------------------------------------------
      // JS function set(element, value) --> msg {"set":"x", "value":"xx"}
      #ifdef useAVAR
        else if (jsonObj.hasOwnProperty("set")) {
          byte varIndex = 255;
          const char* varName = jsonObj["set"];
          for (byte i=0; i<numPWMs; i++) {
            if (strcmp(varName, AVAR[i]) == 0) { varIndex = i; break; }
          }
          if (varIndex == 255) return;
          AVARval[varIndex] = atoi(jsonObj["value"]);
          notifyClients(updateAVAR(varIndex));
        }
      #endif
    }
  }

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, 
             void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("WebSocket client #%u connected from %s\n", client->id(),
      client->remoteIP().toString().c_str());
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("WebSocket client #%u disconnected\n", client->id());
      break;
    case WS_EVT_DATA:
      handleWSMessage(arg, data, len);
      break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
  }
}

void initWebSocket() {
  ws.onEvent(onEvent);
  server.addHandler(&ws);
}

void setup() {
  Serial.begin(115200);

  // Set GPIOs as outputs (buttons and switches)
  #ifdef useButton
    pinMode(statePin, OUTPUT);
    pinMode(modePin, OUTPUT);
    digitalWrite(statePin, 0);
    digitalWrite(modePin, 0);
  #endif
  #ifdef useToggle
    for (byte i =0; i<numDOs; i++) { pinMode(arrDO[i], OUTPUT); }
  #endif
  #ifdef usePWM
    for (byte i =0; i<numPWMs; i++) { 
      pinMode(arrPWM[i][0], OUTPUT);
      analogWrite(arrPWM[i][0], 0);
    }
  #endif


  // Initialize wifi, file system and WebSocket: 
  initWiFi();
  initFS();
  initWebSocket();

  // Load index page when the server is called (on root "/")
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/index.html", "text/html",false);
  });

  // Serve files (JS, CSS and favicon) in a directory when requested by the root URL. 
  server.serveStatic("/", LittleFS, "/");
  server.begin();     // Start the server.
}

void loop() {
  if (millis() - cleanTimer > 2000) {
    cleanTimer = millis();
    ws.cleanupClients();
  }
}
