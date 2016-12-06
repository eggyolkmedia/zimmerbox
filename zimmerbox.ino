#include <Bounce2.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#include "settings.h"

struct Config {
  char wifi_ssid[20];
  char wifi_pass[20];

  char mqtt_host[15];
  int mqtt_port;

  char topic_state[64];
  char topic_set[64];
};

bool relay_state = LOW; // TODO Get from memory

WiFiClient espClient;
PubSubClient client(espClient);
// TODO Verify that topic_state != topic_set (otherwise it causes infinite loops)
Config config = {WIFI_SSID, WIFI_PASS, MQTT_HOST, MQTT_PORT, TOPIC_STATE, TOPIC_SET};

unsigned long last_state = 0;
unsigned long last_button = 0;
char msg[50];
Bounce bounce;

void setup() {
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_BUTTON, INPUT);

  bounce.attach(PIN_BUTTON);
  bounce.interval(50);

  Serial.begin(115200);
  setup_wifi();
  client.setServer(config.mqtt_host, config.mqtt_port);
  client.setCallback(callback);
}

void setup_wifi() {
//  delay(10);
  delay(1000);
  
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
//    Serial.println("fell");
    relay_state = !relay_state;
    update_relay();
    

    // TODO Start blinking after a while...
  }

  if (bounce.rose()) {
    Serial.println("rose");
    unsigned long diff = millis() - last_button;
    if (diff > BUTTON_LONG_PRESS_MILLIS) {
      reset_box();
    }
  }
}

void reset_box() {
  Serial.println("Resetting...");
  // TODO Actual reset + toggle box relay (maybe after small delay?)
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
