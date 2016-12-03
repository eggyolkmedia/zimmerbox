#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include "settings.h"

const char* ssid = WIFI_SSID;
const char* password = WIFI_PASS;
const char* mqtt_server = MQTT_HOST;

bool relay_state = LOW; // TODO Get from memory

WiFiClient espClient;
PubSubClient client(espClient);

long lastMsg = 0;
char msg[50];

void setup() {
  pinMode(PIN_LED, OUTPUT);
  Serial.begin(115200);
  setup_wifi();
  client.setServer(mqtt_server, MQTT_PORT);
  client.setCallback(callback);
}

void setup_wifi() {
  delay(10);
  
  // TODO Replace with optional logging
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  // TODO Replace with nonblocking connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  update_relay();
  update_state();
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
  update_state();
}


// TODO Replace with nonblocking reconnection
void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect("ESP8266Client")) { // Todo ??
      Serial.println("connected");
      // Once connected, publish an announcement...
      client.publish(TOPIC_STATE, "Connected");
      // ... and resubscribe
      client.subscribe(TOPIC_SET);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void update_relay() {
  // TODO Insert relay debouncing
  digitalWrite(PIN_LED, !relay_state);
}

void update_state() {
    lastMsg = millis();
    //snprintf (msg, 50, "hello world #%ld", value);
    msg[0] = relay_state ? '1' : '0';
    msg[1] = 0;
    Serial.print("[");
    Serial.print(TOPIC_STATE);
    Serial.print("] published : ");
    Serial.println(msg);
    client.publish(TOPIC_STATE, msg);
}


void loop() {

  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  // TODO Handle millis() rollover  
  if (millis() - lastMsg > STATE_FREQ_MILLIS) {
    update_state();
  }
}
