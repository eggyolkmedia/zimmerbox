#include <ArduinoLog.h>
#include <Bounce2.h>
#include <DNSServer.h>
#include <EEPROM.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <WiFiManager.h>

#include "settings.h"

#define MODE_NORMAL 0
#define MODE_CONFIG 1

// Uncomment once deployed
// #define DISABLE_LOGGING
#define LOG_LEVEL LOG_LEVEL_VERBOSE

struct Config {
  char mqttHost[15];
  char mqttPort[6];

  char topicState[64];
  char topicSet[64];

  // Intentionally placed at the end, so adding config fields will be detected as a change
  uint8_t hashcode[16];
};

WiFiClient _espClient;
PubSubClient _mqttClient(_espClient);
Config _config;

unsigned long _lastState = 0;
unsigned long _lastButton = 0;
Bounce _bounce;

// TODO support more output types (dimmables, etc.)
bool _relayState = LOW;


void setup() {
  Serial.begin(9600);
  while(!Serial && !Serial.available()){}   

  Log.begin(LOG_LEVEL_VERBOSE, &Serial);
  
  EEPROM.begin(sizeof(Config));
  loadConfig();

  _bounce.attach(PIN_BUTTON);
  _bounce.interval(50);
  pinMode(PIN_RELAY, OUTPUT);
  pinMode(PIN_BUTTON, INPUT);

  setupWifi();

  _mqttClient.setServer(_config.mqttHost, atoi(_config.mqttPort));
  _mqttClient.setCallback(messageReceivedCallback);
}

bool isValidConfig() {
  uint8_t buff[16];
  getConfigHashcode(buff);
  return memcmp(_config.hashcode, buff, 16) == 0; 
}

// TODO change signature to return hash rather than populate the input buffer
void getConfigHashcode(uint8_t* buff16) {
  MD5Builder md5;
  md5.begin();
  md5.add((uint8_t*)&_config, sizeof(Config)-16); // Exclude previous hashcode
  md5.calculate();
  md5.getBytes(buff16);    
}

void saveConfig() {
  getConfigHashcode(_config.hashcode);
  EEPROM.put(0, _config);
  EEPROM.commit();
}

void loadConfig() {
  // Validate config (maybe store checksum? if missing, go into config mode; check that topicSet != topicState)
  EEPROM.get(0, _config);   
  Log.trace("Loaded config; valid? %T\n", isValidConfig());
}

void setupWifi() {
  WiFiManager wifiManager;

  // TODO Read default values from config if valid
  bool validConfig = isValidConfig();

  // TODO Consider adding labels
  WiFiManagerParameter label_separator("<hr><br><div align='center'>ZimmerBox Config</div><br>");
  WiFiManagerParameter label_mqtt_host("<label>MQTT Host</label>");
  WiFiManagerParameter label_mqtt_port("<label>MQTT Port</label>");
  WiFiManagerParameter label_topic_set("<label>SET Topic</label>");
  WiFiManagerParameter label_topic_state("<label>STATE Topic</label>");
  
  WiFiManagerParameter paramMqttHost("mqtt_host", "", validConfig ? _config.mqttHost : "192.168.0.12", 20);
  WiFiManagerParameter paramMqttPort("mqtt_port", "", validConfig ? _config.mqttPort : "1883", 10);
  
  // TODO Merge pubsub topics and move action to message
  // TODO Consider defaulting to hardcoded topics, change only device ID
  WiFiManagerParameter paramTopicSet("topic_set", "", validConfig ? _config.topicSet : "/zimmer/$name/set", 40);
  WiFiManagerParameter paramTopicState("topic_state", "", validConfig ? _config.topicState : "/zimmer/$name/state", 40);

  wifiManager.addParameter(&label_separator);
  wifiManager.addParameter(&label_mqtt_host);
  wifiManager.addParameter(&paramMqttHost);
  wifiManager.addParameter(&label_mqtt_port);
  wifiManager.addParameter(&paramMqttPort);
  wifiManager.addParameter(&label_topic_set);
  wifiManager.addParameter(&paramTopicSet);
  wifiManager.addParameter(&label_topic_state);
  wifiManager.addParameter(&paramTopicState);
  
  if (validConfig) {
    // TODO Set timeout, so if AC fails due to network outage reconnect without dialog
    wifiManager.autoConnect();
  } else {
    // TODO Necessary?
    wifiManager.setSaveConfigCallback(saveConfigCallback);
    // TODO Set specific / random AP name
    // TODO Handle failures
    wifiManager.startConfigPortal("ZimmerBox");

    // Save
    strcpy(_config.mqttHost, paramMqttHost.getValue());
    strcpy(_config.mqttPort, paramMqttPort.getValue());
    strcpy(_config.topicSet, paramTopicSet.getValue());
    strcpy(_config.topicState, paramTopicState.getValue());

    saveConfig();


    // TODO REMOVE
    Log.notice("Saved Config. Valid? %T\n", isValidConfig());
  }

  Log.trace("WiFi connected [%s]\n", WiFi.localIP().toString().c_str());

  // TODO Consider moving these somewhere else -- need to be done when wifi is connected but not related to wifi setup
  long now = millis();
  _lastState = now;
  _lastButton = now;
  updateRelay();  
}

void saveConfigCallback() {
  // Do nothing?
}

void messageReceivedCallback(char* topic, byte* payload, unsigned int length) {
  Log.notice("Message received [%s] \n", topic, payload);
  // TODO Check if works....
//  char[length+1] msg;
//  for (int i = 0; i < length; i++) {
//    msg[i] = Serial.print((char)payload[i]);
//  }
//  Serial.println();

  // TODO More meaningful messages
  _relayState = (char)payload[0] == '1';
  updateRelay();
}

// TODO Replace with nonblocking reconnection
// TODO Allow switch operation even when not connected to MQTT server
void mqttReconnect() {
  // Loop until we're reconnected
  while (!_mqttClient.connected()) {
    Log.trace("Attempting MQTT connection...\n");
    // Attempt to connect
    if (_mqttClient.connect("ESP8266Client")) { // Todo ??
      Log.trace("connected\n");      
      _mqttClient.subscribe(_config.topicSet);
    } else {
      Log.trace("failed, rc = %d; will try again in 5 seconds\n", _mqttClient.state());      
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
  updateState();
}

void updateRelay() {
  // TODO Insert relay debouncing
  digitalWrite(PIN_RELAY, _relayState);
  updateState();
}

void updateState() {
    _lastState = millis();
    //snprintf (msg, 50, "hello world #%ld", value);
    char msg[2];
    msg[0] = _relayState ? '1' : '0';
    msg[1] = 0;
    Log.notice("[%s] published: %s\n", _config.topicState, msg);    
    _mqttClient.publish(_config.topicState, msg);
}

void checkButton() {
  _bounce.update();
  
  if (_bounce.fell()) {
    _lastButton = millis();
    _relayState = !_relayState;
    updateRelay();

    // TODO Start blinking after a while if button is pressed
  }

  if (_bounce.rose()) {
    unsigned long diff = millis() - _lastButton;
    if (diff > BUTTON_LONG_PRESS_MILLIS) {
      resetBox();
    }
  }
}

// Note: first reset after a serial flash will get stuck on 'boot mode:(1,7)'.
// This is a known ESP issue and affects only the first reset
// See also: https://github.com/esp8266/Arduino/issues/1017
void resetBox() {
  Log.notice("Resetting... the module will start in config mode\n");
  // TODO uncomment once config mode is ready
  // memset(config.hashcode, ?, 16); // Wipe hashcode, to go into config mode after reboot
  // TODO remove this once config mode is ready

  saveConfig();
  ESP.restart();
}

void loop() {
  if (!_mqttClient.connected()) {
    mqttReconnect();
  }
  _mqttClient.loop();

  checkButton();

  // TODO Handle millis() rollover  
  if (millis() - _lastState > STATE_FREQ_MILLIS) {
    updateState();
  }
}
