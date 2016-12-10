#include <Bounce2.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
// TODO Implement basic config validation using this as a CRC
#include "MD5Builder.h"
#include <PubSubClient.h>

#include "settings.h"

#define MODE_NORMAL 0
#define MODE_CONFIG 1

struct Config {
  char wifi_ssid[20];
  char wifi_pass[20];

  char mqtt_host[15];
  int mqtt_port;

  char topic_state[64];
  char topic_set[64];

  // Intentionally placed at the end, so adding config fields will be detected as a change
  uint8_t hashcode[16];
};

WiFiClient espClient;
PubSubClient client(espClient);
Config config;

unsigned long last_state = 0;
unsigned long last_button = 0;
char msg[50];
Bounce bounce;

bool relay_state = LOW;
byte mode;

void setup() { 
  Serial.begin(115200);
  delay(1000); // TODO Nonblocking wait (maybe blink led)
   
  EEPROM.begin(sizeof(Config));
  load_config();

  // Determine mode according to config validity
  mode = validate_config() ? MODE_NORMAL : MODE_CONFIG;
  
  bounce.attach(PIN_BUTTON);
  bounce.interval(50);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_BUTTON, INPUT);

  setup_wifi();
  client.setServer(config.mqtt_host, config.mqtt_port);
  client.setCallback(callback);
}

bool validate_config() {
  uint8_t buff[16];
  get_config_hashcode(buff);
  return memcmp(config.hashcode, buff, 16) == 0; 
}

void get_config_hashcode(uint8_t* buff16) {
  MD5Builder md5;
  md5.begin();
  md5.add((uint8_t*)&config, sizeof(Config)-16); // Exclude previous hashcode
  md5.calculate();
  md5.getBytes(buff16);    
}

void save_config() {  
  EEPROM.put(0, config);
  EEPROM.commit();
}

void load_config() {
  // Validate config (maybe store checksum? if missing, go into config mode; check that topic_set != topic_state)
  EEPROM.get(0, config);  
  Serial.print("Loaded config; ssid = ");
  Serial.println(config.wifi_ssid);
  Serial.print("Config valid? ");
  Serial.println(validate_config());
}
void setup_wifi() {
  // TODO Replace with optional logging
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(config.wifi_ssid);

  WiFi.begin(config.wifi_ssid, config.wifi_pass);

  // TODO Replace with nonblocking connection + button check
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  long now = millis();
  last_state = now;
  last_button = now;

  update_relay();
}

void callback(char* topic, byte* payload, unsigned int length) {
  // TODO Nicer logging
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  // TODO More meaningful messages
  relay_state = (char)payload[0] == '1';
  update_relay();
}

// TODO Replace with nonblocking reconnection
void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect("ESP8266Client")) { // Todo ??
      Serial.println("connected");      
      client.subscribe(config.topic_set);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
  update_state();
}

void update_relay() {
  // TODO Insert relay debouncing
  digitalWrite(PIN_LED, !relay_state);
  update_state();
}

void update_state() {
    last_state = millis();
    //snprintf (msg, 50, "hello world #%ld", value);
    msg[0] = relay_state ? '1' : '0';
    msg[1] = 0;
    Serial.print("[");
    Serial.print(config.topic_state);
    Serial.print("] published: ");
    Serial.println(msg);
    client.publish(config.topic_state, msg);
}

void check_button() {
  bounce.update();
  
  if (bounce.fell()) {
    last_button = millis();
    relay_state = !relay_state;
    update_relay();

    // TODO Start blinking after a while if button is pressed
  }

  if (bounce.rose()) {
    unsigned long diff = millis() - last_button;
    if (diff > BUTTON_LONG_PRESS_MILLIS) {
      reset_box();
    }
  }
}

// Note: first reset after a serial flash will get stuck on 'boot mode:(1,7)'.
// This is a known ESP issue and affects only the first reset
// See also: https://github.com/esp8266/Arduino/issues/1017
void reset_box() {
  Serial.println("Resetting... the module will start in config mode");
  memset(config.hashcode, 0, 16);
  save_config();
  ESP.restart();
}

Config initial_config() {
  Config cc = {WIFI_SSID, WIFI_PASS, MQTT_HOST, MQTT_PORT, TOPIC_STATE, TOPIC_SET};
  return cc;
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  check_button();

  // TODO Handle millis() rollover  
  if (millis() - last_state > STATE_FREQ_MILLIS) {
    update_state();
  }

  
}
