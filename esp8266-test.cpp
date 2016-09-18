#include <FS.h>                   //this needs to be first, or it all crashes and burns...

#include <Arduino.h>
#include <ESP8266WiFi.h>          //ESP8266 Core WiFi Library (you most likely already have this in your sketch)

#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     //Local WebServer used to serve the configuration portal
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic
#include <ESP8266HTTPClient.h>

#include <ArduinoJson.h> //https://github.com/bblanchon/ArduinoJson

const int LED = D1;

String JENKINS_URL = "http://192.168.178.3:8080";
String JOBS[2] = {"test","test2"};

char jenkinsUrl[40];
char* job;
bool failedBuild = false;
int blink = HIGH;

bool shouldSaveConfig = false;

void timer0_ISR (void) {
   if (failedBuild) {
	   digitalWrite(LED, blink);
	   blink = !blink;
   } else {
	   digitalWrite(LED, LOW);
	   blink = LOW;
   }
   timer0_write(ESP.getCycleCount() + 80000000L); // 80MHz == 1sec
   Serial.println("timer0_ISR");
}

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());

  Serial.println(myWiFiManager->getConfigPortalSSID());
}

void loadConfig() {
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

		          strcpy(jenkinsUrl, json["jenkins_Url"]);

		        } else {
		          Serial.println("failed to load json config");
		        }
		      }
		    }
		  } else {
		    Serial.println("failed to mount FS");
		}
}

void saveConfig() {
	  //save the custom parameters to FS
	  if (shouldSaveConfig) {
	    Serial.println("saving config");
	    DynamicJsonBuffer jsonBuffer;
	    JsonObject& json = jsonBuffer.createObject();
	    json["jenkins_Url"] = jenkinsUrl;

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

void setup() {
	Serial.begin(115200);

	pinMode(LED, OUTPUT);
	for(int i=0; i<5;i++) {
	   digitalWrite(LED, blink);
	   delay(500);
	   blink = !blink;
	}

	loadConfig();

	WiFiManager wifiManager;
	WiFiManagerParameter jenkins("Jenkins URL", "Jenkins URL", jenkinsUrl, 40);
	wifiManager.addParameter(&jenkins);

	//Event callbacks
	wifiManager.setAPCallback(configModeCallback);
	wifiManager.setSaveConfigCallback(saveConfigCallback);

	//wifiManager.resetSettings();
	//wifiManager.setConfigPortalTimeout(10);
	wifiManager.autoConnect("jenkins-duck");

	//read updated parameters
	strcpy(jenkinsUrl, jenkins.getValue());

	saveConfig();

	blink = LOW;
	digitalWrite(LED, LOW);

	noInterrupts();
	timer0_isr_init();
	timer0_attachInterrupt(timer0_ISR);
    timer0_write(ESP.getCycleCount() + 80000000L); // 80MHz == 1sec
    interrupts();
}

void getJenkinsJobs() {
	HTTPClient http;
    http.begin(JENKINS_URL + "/api/json?tree=jobs[name]");
	int httpCode = http.GET();
	if(httpCode > 0) {
		if(httpCode == HTTP_CODE_OK) {
		   String result = http.getString();
		   Serial.println(result);
		   DynamicJsonBuffer jsonBuffer;
		   JsonObject& root = jsonBuffer.parseObject(result);
		   JsonArray& jobs = root["jobs"];
		   //0job = jobs[0]["name"];
		   //for (JsonArray::iterator it=jobs.begin(); it!=jobs.end(); ++it)
		   //{
		     //Serial.println(it->key);
		     //Serial.println(it->value.asString());
		   //}
		   //Serial.println("Found job: ");
		   //Serial.println(job);
		   delay(1000);
		}
	}
	http.end();
}

void checkJenkinsJobs() {
	for(int i=0;i<sizeof(JOBS);i++) {
	HTTPClient http;
    http.begin(JENKINS_URL + "/job/" + JOBS[i] + "/lastBuild/api/json?tree=result");
	int httpCode = http.GET();
	if(httpCode > 0) {
		if(httpCode == HTTP_CODE_OK) {
		   String result = http.getString();
		   Serial.println(result);
		   failedBuild = result.indexOf("SUCCESS") == -1;
           if(failedBuild) {
        	   break;
           } else {
        	   continue;
           }
           failedBuild = false;
		}
	}
	http.end();
	}
}

void loop() {
	//getJenkinsJobs();
	checkJenkinsJobs();
	delay(10000);
}
