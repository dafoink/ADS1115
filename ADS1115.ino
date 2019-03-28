#include <FS.h>                   //this needs to be first, or it all crashes and burns...
#include <Adafruit_ADS1015.h>
#include <Wire.h>
#include <string.h>
#include <WiFiManager.h>
#include <ESP8266WiFi.h>
#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"
#include <ArduinoJson.h>
#include <DallasTemperature.h>
#include <OneWire.h>



#define Offset -0.03            //deviation compensate

#define OLED_RESET 4

#define LOGO16_GLCD_HEIGHT 16 
#define LOGO16_GLCD_WIDTH  16 

#define ONE_WIRE_BUS            D4      // DS18B20 pin
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature DS18B20(&oneWire);

Adafruit_ADS1115 ads(0x4A);

/************************* Adafruit.io Setup *********************************/
char aio_server[40] = "io.adafruit.com";
char aio_serverport[6] = "1883";
char aio_username[34] = "YOUR_AIO_USERNAME";
char aio_key[60] = "YOUR_AIO_KEY";
char deviceID[34] = "DeviceID";

// Create an ESP8266 WiFiClient class to connect to the MQTT server.
WiFiClient client;
// or... use WiFiFlientSecure for SSL
//WiFiClientSecure client;

// Setup the MQTT client class by passing in the WiFi client and MQTT server and login details.
//Adafruit_MQTT_Client mqtt(&client, AIO_SERVER, AIO_SERVERPORT, AIO_USERNAME, AIO_KEY);
Adafruit_MQTT_Client mqtt(&client, aio_server, 1883, aio_username, aio_key);

/****************************** Feeds ***************************************/
Adafruit_MQTT_Publish phPublish = Adafruit_MQTT_Publish(&mqtt, "");
Adafruit_MQTT_Publish tempPublish1 = Adafruit_MQTT_Publish(&mqtt, "");
Adafruit_MQTT_Publish tempPublish2 = Adafruit_MQTT_Publish(&mqtt, "");
/*************************** Sketch Code ************************************/

float lastPH = 0;
float lastTemp1 = 0;
float lastTemp2 = 0;

void MQTT_connect();

//flag for saving data
bool shouldSaveConfig = false;

void setup(void)
{
	ads.begin();
	Serial.begin(115200);
	delay(1000);

	//display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // initialize with the I2C addr 0x3C (for the 128x32)
	//display.display();

	setupDefaults();

	Serial.println("WiFi connected");
	Serial.println("IP address: "); Serial.println(WiFi.localIP());

	// setup onewire bus
	DS18B20.begin();
}

void loop(void)
{
	MQTT_connect();

	readPHValue();
	readTemperature();
	delay(5000);
}

void MQTT_connect() {
	int8_t ret;

	// Stop if already connected.
	if (mqtt.connected()) {
		return;
	}

	Serial.print("Connecting to MQTT... ");

	uint8_t retries = 3;
	while ((ret = mqtt.connect()) != 0) { // connect will return 0 for connected
		Serial.println(mqtt.connectErrorString(ret));
		Serial.println("Retrying MQTT connection in 5 seconds...");
		mqtt.disconnect();
		delay(5000);  // wait 5 seconds
		retries--;
		if (retries == 0) {
			// basically die and wait for WDT to reset me
			while (1);
		}
	}
	Serial.println("MQTT Connected!");
}

void readPHValue()
{
	static float pHValue, voltage;
	int16_t adc0;
	adc0 = ads.readADC_SingleEnded(0);
	voltage = (adc0 * 0.1875) / 1000;
	pHValue = 3.5*voltage + Offset;

	Serial.print("AIN0: ");
	Serial.print(adc0);
	Serial.print("\tVoltage: ");
	Serial.print(voltage, 7);
	Serial.print("\tPH: ");
	Serial.println(pHValue, 7);
	Serial.println();

	if (pHValue != lastPH)
	{
		/*char buf[100];
		dtostrf(pHValue, 4, 2, buf);
		writeText(buf, 1, 4, true);*/

		Serial.println("Going to publish PH Value");
		Serial.print("PH Value: ");
		Serial.println(pHValue);

		if (!phPublish.publish(pHValue))
		{
			Serial.println(F("phPublish publish Failed"));
		}
		else
		{
			Serial.println(F("phPublish publish OK!"));
		}
	}

	lastPH = pHValue;
}

void readSPFFS()
{
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

					strcpy(aio_server, json["aio_server"]);
					strcpy(aio_serverport, json["aio_serverport"]);
					strcpy(aio_username, json["aio_username"]);
					strcpy(aio_key, json["aio_key"]);
					strcpy(deviceID, json["deviceID"]);

				}
				else {
					Serial.println("failed to load json config");
				}
			}
		}
	}
	else {
		Serial.println("failed to mount FS");
	}
	Serial.println("Done retrieving setup");
	//end read
}

void saveSFFS()
{
	//save the custom parameters to FS
	if (SPIFFS.begin()) {
		if (shouldSaveConfig) {
			Serial.println("saving config");
			DynamicJsonBuffer jsonBuffer;
			JsonObject& json = jsonBuffer.createObject();
			json["aio_server"] = aio_server;
			json["aio_serverport"] = aio_serverport;
			json["aio_username"] = aio_username;
			json["aio_key"] = aio_key;
			json["deviceID"] = deviceID;

			File configFile = SPIFFS.open("/config.json", "w");
			if (!configFile) {
				Serial.println("failed to open config file for writing");
			}

			json.printTo(Serial);
			json.printTo(configFile);
			configFile.close();
			//end save
		}
	}
}

void setupFeeds()
{
	const char* feedLocation = "/feeds/";

	char* feed1;
	const char* feed1Location = "-ph1";
	feed1 = (char*)malloc(strlen(aio_username) + strlen(feedLocation) + strlen(deviceID) + strlen(feed1Location) + 2);
	strcpy(feed1, aio_username);
	strcat(feed1, feedLocation);
	strcat(feed1, deviceID);
	strcat(feed1, feed1Location);

	char* feed2;
	const char* feed2Location = "-temp1";
	feed2 = (char*)malloc(strlen(aio_username) + strlen(feedLocation) + strlen(deviceID) + strlen(feed2Location) + 2);
	strcpy(feed2, aio_username);
	strcat(feed2, feedLocation);
	strcat(feed2, deviceID);
	strcat(feed2, feed2Location);

	char* feed3;
	const char* feed3Location = "-temp2";
	feed3 = (char*)malloc(strlen(aio_username) + strlen(feedLocation) + strlen(deviceID) + strlen(feed3Location) + 2);
	strcpy(feed3, aio_username);
	strcat(feed3, feedLocation);
	strcat(feed3, deviceID);
	strcat(feed3, feed3Location);

	Serial.print("Publish location ");
	Serial.println(feed1);
	phPublish = Adafruit_MQTT_Publish(&mqtt, (char*)feed1);
	tempPublish1 = Adafruit_MQTT_Publish(&mqtt, (char*)feed2);
	tempPublish2 = Adafruit_MQTT_Publish(&mqtt, (char*)feed3);
}

void resetSystem()
{
	WiFiManager wifiManager;
	if (!wifiManager.startConfigPortal("OnDemandAP")) {
		Serial.println("failed to connect and hit timeout");
		delay(3000);
		//reset and try again, or maybe put it to deep sleep
		ESP.reset();
		delay(5000);
	}
}

void saveConfigCallback() {
	Serial.println("Should save config");
	shouldSaveConfig = true;
}

void setupDefaults()
{
	Serial.println(F("CatalinaIoT Node"));

	//SPIFFS.format();  

	readSPFFS();

	WiFiManagerParameter custom_aio_server("server", "aio server", aio_server, 40);
	WiFiManagerParameter custom_aio_port("port", "aio port", aio_serverport, 6);
	WiFiManagerParameter custom_aio_username("username", "aio username", aio_username, 34);
	WiFiManagerParameter custom_aio_key("key", "aio key", aio_key, 60);
	WiFiManagerParameter custom_deviceID("deviceID", "device ID", deviceID, 34);


	//WiFiManager
	//Local intialization. Once its business is done, there is no need to keep it around
	WiFiManager wifiManager;
	//wifiManager.resetSettings();

	//set config save notify callback
	wifiManager.setSaveConfigCallback(saveConfigCallback);

	//add all your parameters here
	wifiManager.addParameter(&custom_aio_server);
	wifiManager.addParameter(&custom_aio_port);
	wifiManager.addParameter(&custom_aio_username);
	wifiManager.addParameter(&custom_aio_key);
	wifiManager.addParameter(&custom_deviceID);

	//exit after config instead of connecting

	wifiManager.setBreakAfterConfig(true);




	//tries to connect to last known settings
	//if it does not connect it starts an access point with the specified name
	//here  "AutoConnectAP" with password "password"
	//and goes into a blocking loop awaiting configuration
	if (!wifiManager.autoConnect("CatalinaIoT", "password")) {
		Serial.println("failed to connect, we should reset as see if it connects");
		delay(3000);
		ESP.reset();
		delay(5000);
	}
	Serial.println("Connected...AllRight!");
	Serial.println();


	//read updated parameters
	strcpy(aio_server, custom_aio_server.getValue());
	strcpy(aio_serverport, custom_aio_port.getValue());
	strcpy(aio_username, custom_aio_username.getValue());
	strcpy(aio_key, custom_aio_key.getValue());
	strcpy(deviceID, custom_deviceID.getValue());


	setupFeeds();

	saveSFFS();
}



void readTemperature()
{
	float temp1;
	float temp2;
	DS18B20.requestTemperatures();
	temp1 = DS18B20.getTempFByIndex(0);
	temp2 = DS18B20.getTempFByIndex(1);

	Serial.print("temp1: ");
	Serial.println(temp1);

	Serial.print("temp2: ");
	Serial.println(temp2);

	if (temp1 != lastTemp1)
	{
		Serial.println("Going to publish Temperature1 Value");

		if (!tempPublish1.publish(temp1))
		{
			Serial.println(F("tempPublish publish Failed"));
		}
		else
		{
			Serial.println(F("tempPublish publish OK!"));
		}
	}

	if (temp2 != lastTemp2)
	{
		Serial.println("Going to publish Temperature2 Value");

		if (!tempPublish2.publish(temp2))
		{
			Serial.println(F("tempPublish publish Failed"));
		}
		else
		{
			Serial.println(F("tempPublish publish OK!"));
		}
	}

	lastTemp1 = temp1;
	lastTemp2 = temp2;
}