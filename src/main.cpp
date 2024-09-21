
#include <Arduino.h>
#include <FS.h>									 //this needs to be first, or it all crashes and burns...
#include <SPIFFS.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>					//https://github.com/tzapu/WiFiManager
#include <ArduinoJson.h>					//https://github.com/bblanchon/ArduinoJson
#include <PubSubClient.h>					//https://github.com/knolleary/PubSubClient



/*
	This code is basically copied from:
	https://github.com/tzapu/WiFiManager/blob/master/examples/Parameters/SPIFFS/AutoConnectWithFSParametersAndCustomIP/AutoConnectWithFSParametersAndCustomIP.ino

*/

#define CLEARCREDENTIALS 0 //0 for WiFi data persistance, set 1 to force clear (for debug purposes)

struct MQTT_settings
{
	char mqtt_server[16] = 	"192.168.1.2";
	char mqtt_port[6] 	 = 	"1883";
	char api_token[14] 	 = 	"YOUR_APITOKEN";		//Increase api token size if necessary
	char mqtt_topic[20] = 	"Topic";
} mqtt_settings;

//default custom static IP
struct IP_settings
{
	char static_ip[16] = "192.168.1.3";
	char static_gw[16] = "192.168.1.1";
	char static_sn[16] = "255.255.255.0";
} ip_settings;

//Credentials for Captive portal
struct AP_default
{
	const char *ssID	= "ESP32";
	const char *passw	= "12345678";
} ap_default;

//flag for saving data
bool shouldSaveConfig = false;


WiFiClient espClient;
WiFiManager wifiManager;
PubSubClient client(espClient);

//initialize and set up GPIO and machine states

const int led1 = 16;
const int led2 = 17;
const int led3 = 18;
const int led4 = 19;
const int led5 = 25;
const int led6 = 26;
const int led7 = 32;
const int led8 = 33;

const int buttonPin = 22;

int buttonState = 0;
int previousButtonState = 0;


//callback notifying us of the need to save config
void saveConfigCallback () {
	shouldSaveConfig = true;
	Serial.println("Should save config");
}

void InitSPIFFS()
{
	//read configuration from FS json
	
	//clean FS, for testing
	//if new entries in captive portal are made
	//is recommended to clean FS
	if(CLEARCREDENTIALS)
		SPIFFS.format();

	Serial.println("mounting FS...");

	if (!SPIFFS.begin()) {
		Serial.println("failed to mount FS");		
		Serial.println("Please, reflash ESP32 again!!");
		
		Serial.println("rebooting ESP32...");
		ESP.restart();
	}
	
	Serial.println("Mounted filesystem");

	if (SPIFFS.exists("/config.json")) 
	{
		//file exists, reading and loading
		Serial.println("reading config file");
		File configFile = SPIFFS.open("/config.json", "r");
		if (configFile) 
		{
			Serial.println("opened config file");
			size_t size = configFile.size();

			// Allocate a buffer to store contents of the file.
			std::unique_ptr<char[]> buf(new char[size]);
			configFile.readBytes(buf.get(), size);

			JsonDocument json;
			auto deserializeError = deserializeJson(json, buf.get());
			serializeJson(json, Serial);
			if ( deserializeError ) 
				Serial.println("failed to load json config");
			else
			{
				if(json["mqtt_server"])	strcpy(mqtt_settings.mqtt_server, 	json["mqtt_server"]);
				if(json["mqtt_port"])	strcpy(mqtt_settings.mqtt_port, 	json["mqtt_port"]);
				if(json["api_token"])	strcpy(mqtt_settings.api_token, 	json["api_token"]);
				if(json["mqtt_topic"])	strcpy(mqtt_settings.mqtt_topic, 	json["mqtt_topic"]);


				if (!json["ip"]) 
					Serial.println("no custom ip in config");
				else
				{
					Serial.println("setting custom ip from config");
					strcpy(ip_settings.static_ip, json["ip"]);
					strcpy(ip_settings.static_gw, json["gateway"]);
					strcpy(ip_settings.static_sn, json["subnet"]);
					Serial.println(ip_settings.static_ip);
				}

				Serial.println("\nparsed json");
			} 
		}
	}
	//end read
	Serial.println(ip_settings.static_ip);
	Serial.println(mqtt_settings.api_token);
	Serial.println(mqtt_settings.mqtt_server);
	Serial.println(mqtt_settings.mqtt_topic);
}

void InitWiFi()
{
	/*
	*	Reset saved settings USE ONLY FOR DEBUGGING
	*	TO-DO
	*	- create a button pin function to do it by hardware 
	*	or let 2 contact pins to do ir with a removable wire
	*/

	if(CLEARCREDENTIALS)
		wifiManager.resetSettings();  

	// The extra parameters to be configured (can be either global or just in the setup)
	// After connecting, parameter.getValue() will get you the configured value
	// id/name placeholder/prompt default length
	WiFiManagerParameter custom_mqtt_server(	"server", 	"mqtt server", 			mqtt_settings.mqtt_server, 	40);
	WiFiManagerParameter custom_mqtt_port(		"port", 	"mqtt port", 			mqtt_settings.mqtt_port, 	5);
	WiFiManagerParameter custom_api_token(		"apikey", 	"API token", 			mqtt_settings.api_token, 	34);
	WiFiManagerParameter custom_mqtt_topic(		"topic", 	"Subscribe to topic", 	mqtt_settings.mqtt_topic, 	34);

	//set config save notify callback
	wifiManager.setSaveConfigCallback(saveConfigCallback);

	//set static ip
	IPAddress _ip, _gw, _sn;
	_ip.fromString(ip_settings.static_ip);
	_gw.fromString(ip_settings.static_gw);
	_sn.fromString(ip_settings.static_sn);

	wifiManager.setSTAStaticIPConfig(_ip, _gw, _sn);

	//add all your parameters here
	wifiManager.addParameter(&custom_mqtt_server);
	wifiManager.addParameter(&custom_mqtt_port);
	wifiManager.addParameter(&custom_api_token);
	wifiManager.addParameter(&custom_mqtt_topic);
	

	//set minimum quality of signal so it ignores AP's under that quality
	//defaults to 8%
	wifiManager.setMinimumSignalQuality();

	//fetches ssid and pass and tries to connect
	//if it does not connect it starts an access point with the specified default name 
	//and goes into a blocking loop awaiting configuration
	if (!wifiManager.autoConnect(ap_default.ssID, ap_default.passw)) 
	{
		Serial.println("failed to connect and hit timeout");
		delay(3000);
		//reset and try again, or maybe put it to deep sleep
		//ESP.restart();
	}

	//read updated parameters
	strcpy(mqtt_settings.mqtt_server, 	custom_mqtt_server.getValue());
	strcpy(mqtt_settings.mqtt_port, 	custom_mqtt_port.getValue());
	strcpy(mqtt_settings.api_token, 	custom_api_token.getValue());
	strcpy(mqtt_settings.mqtt_topic, 	custom_mqtt_topic.getValue());

	//if you get here you have connected to the WiFi
	Serial.println("connected...yeey :)");
}

void SaveParamsToFS()
{
	//save the custom parameters to FS
	if (shouldSaveConfig) {
		Serial.println("saving config");
		JsonDocument json;

		json["mqtt_server"] = 	mqtt_settings.mqtt_server;
		json["mqtt_port"] 	= 	mqtt_settings.mqtt_port;
		json["api_token"] 	= 	mqtt_settings.api_token;
		json["mqtt_topic"]	= 	mqtt_settings.mqtt_topic;

		json["ip"] 		= 	WiFi.localIP().toString();
		json["gateway"] = 	WiFi.gatewayIP().toString();
		json["subnet"] 	= 	WiFi.subnetMask().toString();

		File configFile = SPIFFS.open("/config.json", "w");
		if (!configFile)
			Serial.println("failed to open config file for writing");

		serializeJson(json, Serial);
		serializeJson(json, configFile);

		configFile.close();
	}
}


////////////////////////////////////////////////////////////////////////////////
/*
Behavioral functions for your ESP 
*/
////////////////////////////////////////////////////////////////////////////////



//set GPIO modes of your board
void initializePins()
{
	// Initialize the Button pin as an input
	pinMode(buttonPin, INPUT);

	// Initialize the pins as an output
	pinMode(led1, OUTPUT);
	pinMode(led2, OUTPUT);     
	pinMode(led3, OUTPUT);     
	pinMode(led4, OUTPUT);     
	pinMode(led5, OUTPUT);     
	pinMode(led6, OUTPUT);     
	pinMode(led7, OUTPUT);     
	pinMode(led8, OUTPUT);
}

void turnLeds(int isOn) 
{
	//testing function
	digitalWrite(led1, isOn);
	digitalWrite(led2, isOn);
	digitalWrite(led3, isOn);
	digitalWrite(led4, isOn);
	digitalWrite(led5, isOn);
	digitalWrite(led6, isOn);
	digitalWrite(led7, isOn);
	digitalWrite(led8, isOn);

	Serial.print("Button ");
	Serial.println(isOn == HIGH ? "pressed" : "released");
}

//MQTT subscriptions
void mqttCallback(char* topic, byte* payload, unsigned int length) 
{
	Serial.print("Message arrived [");
	Serial.print(topic);
	Serial.print("]: ");
	for (int i = 0; i < length; i++) 
		Serial.print((char)payload[i]);
	Serial.println();

	if ((char)payload[0] == '1')	// Switch on the LED if an 1 was received as first character
		digitalWrite(led5, LOW);	// Turn the LED on (Note that LOW is the voltage level
									// but actually the LED is on; this is because
									// it is active low on the ESP-01)
	else 
		digitalWrite(led5, HIGH);	// Turn the LED off by making the voltage HIGH
	}

void mqttReconnect() 
{
	// Loop until we're reconnected
	while (!client.connected())
	{
		Serial.print("Attempting MQTT connection...");
		String clientId = "ESP32Client-" + String(random(0xffff), HEX);	// Create a random client ID
		if (client.connect(clientId.c_str()))							// Attempt to connect
		{
			Serial.println("connected");

			client.publish("outTopic", "hello world");		// Once connected, publish an announcement...
			client.subscribe(mqtt_settings.mqtt_topic);		// ... and resubscribe to topic from setup
		} 
		else 
		{
			Serial.print("failed, rc=");
			Serial.print(client.state());
			Serial.println(" try again in 5 seconds");		// Wait 5 seconds before retrying

			delay(5000);
		}
	}
}


void setup() 
{
	// put your setup code here, to run once:
	Serial.begin(115200);
	Serial.println();

	initializePins();

	InitSPIFFS();
	InitWiFi();
	SaveParamsToFS();

	client.setServer(mqtt_settings.mqtt_server, atoi(mqtt_settings.mqtt_port));
	client.setCallback(mqttCallback);

	Serial.println("local ip");
	Serial.println(WiFi.localIP());
	Serial.println(WiFi.gatewayIP());
	Serial.println(WiFi.subnetMask());
}

void loop() 
{

	unsigned long lastMsg = 0;
	const int msgBufferSize	= 50;
	char msg[msgBufferSize];
	int value = 0;

	for(;;)
	{
		client.setSocketTimeout(1);
	
		buttonState = digitalRead(buttonPin);
		if (buttonState == HIGH && previousButtonState == LOW) //Raising edge detection
			turnLeds(HIGH);
	
		if(buttonState == LOW && previousButtonState == HIGH) //Falling edge detection
			turnLeds(LOW);
	
		previousButtonState = buttonState;

		//mqtt
		if (!client.connected()) 
			mqttReconnect();
		
		client.loop();

		//Test publishing msg to mqtt
		unsigned long now = millis();
		if (now - lastMsg > 2000) 
		{
			lastMsg = now;
			++value;
			snprintf (msg, msgBufferSize, "hello world #%ld", value);
			Serial.print("Publish message: ");
			Serial.println(msg);
			client.publish("outTopic", msg);
		}
	}

	//This code should never be executed, but if infinite loop raises end... xD
	ESP.restart();
}
