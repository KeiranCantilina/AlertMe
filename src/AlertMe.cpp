/*
	AlertMe.cpp - Library for sending emails and SMS from the ESP8266!
	Created by Connor Nishijima, October 14th 2017.
	Modified by Keiran Cantilina, July 26, 2021.
	Released under the GPLv3 license.
*/

#include "AlertMe.h"

char EMAIL_LOGIN[40];
char EMAIL_PASSWORD[40];
char RECIPIENT[40];
//char null_terminator[1];
//null_terminator[0] = '\0';
//char auth_char[100];

uint16_t smtp_port = 0;
char smtp_server[40];
bool needs_save = false;
bool alert_debug = false;
char* last_error = "";

bool stmp_connect_fail = false;
bool portal_timeout = false; // false means timed out, true means not timed out



WiFiManager wifiManager;

WiFiManagerParameter custom_text_port("<br>What is the SMTP port number? (eg. for Gmail: 465)");
WiFiManagerParameter our_smtp_port("port", "", "465", 5);

WiFiManagerParameter custom_text_server("<br><br>What is the SMTP server?<br>(eg. smtp.gmail.com)");
WiFiManagerParameter our_smtp_server("server", "", "smtp.gmail.com", 40);

WiFiManagerParameter custom_text_email("<br><br>What is the host email address?<br>(eg. johndoe@gmail.com)");
WiFiManagerParameter our_email("email", "", EMAIL_LOGIN, 40);

WiFiManagerParameter custom_text_password("<br><br>What is the host email password?<br>(eg. doepass123)");
WiFiManagerParameter our_password("password", "", EMAIL_PASSWORD, 40);

WiFiManagerParameter custom_text_recipient("<br><br>What is the desired recipient email address?<br>(eg. alarmrecipient@gmail.com)");
WiFiManagerParameter our_recipient("recipient", "", RECIPIENT, 40);

const char PROGMEM b64_alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                    "abcdefghijklmnopqrstuvwxyz"
                                    "0123456789+/";



template <typename Generic>
void DEBUG_AM(Generic text) {
  if (alert_debug) {
    Serial.print("*ALERTME: ");
    Serial.println(text);
  }
}
									
AlertMe::AlertMe(){}

// BASE64 -----------------------------------------------------------

#define encode64(arr) encode64_f(arr,sizeof(arr)-1)
									
inline void a3_to_a4(unsigned char * a4, unsigned char * a3) {
  a4[0] = (a3[0] & 0xfc) >> 2;
  a4[1] = ((a3[0] & 0x03) << 4) + ((a3[1] & 0xf0) >> 4);
  a4[2] = ((a3[1] & 0x0f) << 2) + ((a3[2] & 0xc0) >> 6);
  a4[3] = (a3[2] & 0x3f);
}

int base64_encode(char *output, const char *input, int inputLen) {
  int i = 0, j = 0;
  int encLen = 0;
  unsigned char a3[3];
  unsigned char a4[4];

  while (inputLen--) {
    a3[i++] = *(input++);
    if (i == 3) {
      a3_to_a4(a4, a3);

      for (i = 0; i < 4; i++) {
        output[encLen++] = pgm_read_byte(&b64_alphabet[a4[i]]);
      }

      i = 0;
    }
  }

  if (i) {
    for (j = i; j < 3; j++) {
      a3[j] = '\0';
    }

    a3_to_a4(a4, a3);

    for (j = 0; j < i + 1; j++) {
      output[encLen++] = pgm_read_byte(&b64_alphabet[a4[j]]);
    }

    while ((i++ < 3)) {
      output[encLen++] = '=';
    }
  }
  output[encLen] = '\0';
  return encLen;
}

int base64_enc_length(int plainLen) {
  int n = plainLen;
  return (n + 2 - ((n + 2) % 3)) / 3 * 4;
}

const char* encode64_f(char* input, uint8_t len) {
  // encoding

  int encodedLen = base64_enc_length(len);
  static char encoded[256];
  // note input is consumed in this step: it will be empty afterwards
  base64_encode(encoded, input, len);
  return encoded;
}

// END BASE64 ---------------------------------------------------------

// GSENDER ------------------------------------------------------------

class Gsender
{
  protected:
    Gsender();
  private:
    const char* _error = nullptr;
    char* _subject = nullptr;
    String _serverResponse;
    static Gsender* _instance;
    bool AwaitSMTPResponse(WiFiClientSecure &client, const String &resp = "", uint16_t timeOut = 10000);

  public:
    static Gsender* Instance();
    Gsender* Subject(const char* subject);
    Gsender* Subject(const String &subject);
    bool Send(const String &to, const String &message);
    bool TestConnection(char* server, uint16_t port);
    String getLastResponse();
    const char* getError();
};

Gsender* Gsender::_instance = 0;
Gsender::Gsender() {}
Gsender* Gsender::Instance()
{
  if (_instance == 0)
    _instance = new Gsender;
  return _instance;
}

Gsender* Gsender::Subject(const char* subject)
{
  delete [] _subject;
  _subject = new char[strlen(subject) + 1];
  strcpy(_subject, subject);
  return _instance;
}
Gsender* Gsender::Subject(const String &subject)
{
  return Subject(subject.c_str());
}

bool Gsender::AwaitSMTPResponse(WiFiClientSecure &client, const String &resp, uint16_t timeOut)
{
  uint32_t ts = millis();
  while (!client.available())
  {
    if (millis() > (ts + timeOut)) {
      _error = "SMTP Response TIMEOUT!";
      return false;
    }
  }
  _serverResponse = client.readStringUntil('\n');
  DEBUG_AM(_serverResponse);
  if (resp && _serverResponse.indexOf(resp) == -1) return false;
  return true;
}

String Gsender::getLastResponse()
{
  return _serverResponse;
}

const char* Gsender::getError()
{
  return _error;
}

bool Gsender::Send(const String &to, const String &message)
{
  WiFiClientSecure client;
  client.setInsecure();
  DEBUG_AM("Connecting to :");
  DEBUG_AM(smtp_server);
  if (!client.connect(smtp_server, smtp_port)) {
    _error = "Could not connect to mail server";
    return false;
  }
  if (!AwaitSMTPResponse(client, "220")) {
    _error = "Connection Error";
    return false;
  }

  DEBUG_AM("HELO friend:");
  client.println("HELO friend");
  if (!AwaitSMTPResponse(client, "250")) {
    _error = "identification error";
    return false;
  }

// ---------------------------------------------------------- AUTH LOGIN
  /*DEBUG_AM("AUTH LOGIN:");			// AUTH LOGIN
  client.println("AUTH LOGIN");
  AwaitSMTPResponse(client);
  
  DEBUG_AM("EMAIL_LOGIN:");
  DEBUG_AM(EMAIL_LOGIN);

  client.println(encode64(EMAIL_LOGIN));
  AwaitSMTPResponse(client);

  DEBUG_AM("EMAIL_PASSWORD:");
  DEBUG_AM(EMAIL_PASSWORD);

  client.println(encode64(EMAIL_PASSWORD));
  if (!AwaitSMTPResponse(client, "235")) {
    _error = "SMTP AUTH error";
    return false;
  }*/
// ---------------------------------------------------------------  AUTH PLAIN (added by Keiran Cantilina 28 July 2021)
  DEBUG_AM("AUTH PLAIN:");		
  client.println("AUTH PLAIN");
  AwaitSMTPResponse(client);
  
  DEBUG_AM("EMAIL_LOGIN:");
  DEBUG_AM(EMAIL_LOGIN);
  
  DEBUG_AM("EMAIL_PASSWORD:");
  DEBUG_AM(EMAIL_PASSWORD);  


  char* buf = (char*)malloc(100);
  String Username = EMAIL_LOGIN;
  String Password = EMAIL_PASSWORD;
  char plainAuth[Username.length()+Password.length()+3];
  plainAuth[0] = '\0';
  strcpy(&plainAuth[1], Username.c_str());
  strcpy(&plainAuth[2+Username.length()], Password.c_str());
  const char* pA = (const char*)&plainAuth;
  base64_encode(buf, pA, Username.length()+Password.length()+2);

  DEBUG_AM("ENCODE64: ");
  DEBUG_AM(buf);

   
   client.println(buf);
   DEBUG_AM("--------------------------TRYING REAL HARD");
   free(buf);

  if (!AwaitSMTPResponse(client, "235")) {
    _error = "SMTP AUTH error";
    return false;
  }
 // ------------------------------------------------------------------------------

  String mailFrom = "MAIL FROM: <" + String(EMAIL_LOGIN) + '>';
  DEBUG_AM(mailFrom);
  client.println(mailFrom);
  AwaitSMTPResponse(client);

  String rcpt = "RCPT TO: <" + to + '>';
  DEBUG_AM(rcpt);
  client.println(rcpt);
  AwaitSMTPResponse(client);

  DEBUG_AM("DATA:");
  client.println("DATA");
  if (!AwaitSMTPResponse(client, "354")) {
    _error = "SMTP DATA error";
    return false;
  }

  client.println("From: <" + String(EMAIL_LOGIN) + '>');
  client.println("To: <" + to + '>');

  client.print("Subject: ");
  client.println(_subject);

  client.println("Mime-Version: 1.0");
  client.println("Content-Type: text/html; charset=\"UTF-8\"");
  client.println("Content-Transfer-Encoding: 7bit");
  client.println();
  String body = "<!DOCTYPE html><html lang=\"en\">" + message + "</html>";
  client.println(body);
  client.println(".");
  if (!AwaitSMTPResponse(client, "250")) {
    _error = "Sending message error";
    return false;
  }
  client.println("QUIT");
  if (!AwaitSMTPResponse(client, "221")) {
    _error = "SMTP QUIT error";
    return false;
  }
  return true;
}

bool Gsender::TestConnection(char* server, uint16_t port) {
  WiFiClientSecure client;
  client.setInsecure();
  DEBUG_AM("Connecting to :");
  DEBUG_AM(server);
  if (!client.connect(server, port)) {
    _error = "Could not connect to mail server";
    return false;
  }
  if (!AwaitSMTPResponse(client, "220")) {
    _error = "Connection Error";
    return false;
  }

  DEBUG_AM("HELO friend:");
  client.println("HELO friend");
  if (!AwaitSMTPResponse(client, "250")) {
    _error = "identification error";
    return false;
  }

// ------------------------------------------------------- AUTH LOGIN
/*
  DEBUG_AM("AUTH LOGIN:");
  client.println("AUTH LOGIN");
  AwaitSMTPResponse(client);


  DEBUG_AM("EMAIL_LOGIN:");
  DEBUG_AM(EMAIL_LOGIN);

  client.println(encode64(EMAIL_LOGIN));
  AwaitSMTPResponse(client);


  DEBUG_AM("EMAIL_PASSWORD:");
  DEBUG_AM(EMAIL_PASSWORD);

  client.println(encode64(EMAIL_PASSWORD));
  if (!AwaitSMTPResponse(client, "235")) {
    _error = "SMTP AUTH error";
    return false;
  }
*/
// ---------------------------------------------------------------  AUTH PLAIN (added by Keiran Cantilina 28 July 2021)
  DEBUG_AM("AUTH PLAIN:");		
  client.println("AUTH PLAIN");
  AwaitSMTPResponse(client);
  
  DEBUG_AM("EMAIL_LOGIN:");
  DEBUG_AM(EMAIL_LOGIN);
  
  DEBUG_AM("EMAIL_PASSWORD:");
  DEBUG_AM(EMAIL_PASSWORD);  

  char* buf = (char*)malloc(100);
  String Username = EMAIL_LOGIN;
  String Password = EMAIL_PASSWORD;
  char plainAuth[Username.length()+Password.length()+3];
  plainAuth[0] = '\0';
  strcpy(&plainAuth[1], Username.c_str());
  strcpy(&plainAuth[2+Username.length()], Password.c_str());
  const char* pA = (const char*)&plainAuth;
  base64_encode(buf, pA, Username.length()+Password.length()+2);

  DEBUG_AM("ENCODE64: ");
  DEBUG_AM(buf);

   
   client.println(buf);
   DEBUG_AM("--------------------------TRYING REAL HARD");
   free(buf);
 
  if (!AwaitSMTPResponse(client, "235")) {
    _error = "SMTP AUTH error";
    return false;
  }

 // ------------------------------------------------------------------------------

  return true;
}

// END OF GSENDER --------------------------------------------------

void load_settings() {
    if (SPIFFS.exists("/alertme_config.json")) {
      //file exists, reading and loading
      DEBUG_AM("Reading alertme_config.json...");

      File configFile = SPIFFS.open("/alertme_config.json", "r");
      if (configFile) {

        DEBUG_AM("Opened config file!");

        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        if (json.success()) {
          DEBUG_AM("Parsed config:");
		  if(alert_debug){
			json.printTo(Serial);
		  }
		  DEBUG_AM('\n');


          smtp_port = json["smtp_port"];
          strcpy(smtp_server, json["smtp_server"]);
          strcpy(EMAIL_LOGIN, json["smtp_email"]);
          strcpy(EMAIL_PASSWORD, json["smtp_password"]);
          strcpy(RECIPIENT,json["recipient_email"]);

        } else {
          DEBUG_AM("Failed to load json config!");
        }
      }
    }
	else{
		DEBUG_AM("/alertme_config.json doesn't exist!");
	}
}

void save_settings(){
	DEBUG_AM("Saving config file...");

    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();

    json["smtp_server"] = smtp_server;
    json["smtp_port"] = smtp_port;
    json["smtp_email"] = EMAIL_LOGIN;
    json["smtp_password"] = EMAIL_PASSWORD;
    json["recipient_email"] = RECIPIENT;

    File configFile = SPIFFS.open("/alertme_config.json", "w");
    if (!configFile) {
      DEBUG_AM("Failed to open config file for writing!");
    }

	json.printTo(configFile);
	if(alert_debug){
		json.printTo(Serial);
	}

    configFile.close();
}

void saveConfigCallback () {
	save_settings();
}

void configModeCallback (WiFiManager *myWiFiManager) {
	if(stmp_connect_fail){
		Serial.print("Failed to connect to SMTP server; config AP active with SSID: '");
	}
	else{
		Serial.print("\nFailed to connect to WiFi; config AP active with SSID: '");
	}
	Serial.print(myWiFiManager->getConfigPortalSSID());
	Serial.println("'");
	stmp_connect_fail = false;
}

bool AlertMe::conn_network(bool retry) {
  wifiManager.addParameter(&custom_text_port);
  wifiManager.addParameter(&our_smtp_port);

  wifiManager.addParameter(&custom_text_server);
  wifiManager.addParameter(&our_smtp_server);

  wifiManager.addParameter(&custom_text_email);
  wifiManager.addParameter(&our_email);

  wifiManager.addParameter(&custom_text_password);
  wifiManager.addParameter(&our_password);
	
  wifiManager.addParameter(&custom_text_recipient);
  wifiManager.addParameter(&our_recipient);

  wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setConfigPortalTimeout(180);
	
  if (!retry) {
	DEBUG_AM("Connecting to your WiFi network...");	
    if(!wifiManager.autoConnect("LNAlert Configuration")){ // if connection fails due to timeout, reboot
		return false;
	}
	if(needs_save){
		needs_save = false;
		save_settings();
	}
  }

  Gsender *gsender = Gsender::Instance();
  DEBUG_AM("Testing SMTP connection...");

  if (gsender->TestConnection(smtp_server, smtp_port) == false) {
	stmp_connect_fail = true;
    portal_timeout = wifiManager.startConfigPortal("LNAlert Configuration");
	DEBUG_AM("---------------------------------------------------------------PORTAL TIMEOUT STATUS: ");
    if (portal_timeout == true){
		DEBUG_AM("TRUE"); // portal did not time out
	}
	else{
		DEBUG_AM("FALSE"); // Portal timed out. Reset!
		return false;
	}
	
	DEBUG_AM("Retrying connections with new info...");
	
	smtp_port = atoi(our_smtp_port.getValue());
    strcpy(smtp_server, our_smtp_server.getValue());
    strcpy(EMAIL_LOGIN, our_email.getValue());
    strcpy(EMAIL_PASSWORD, our_password.getValue());
    strcpy(RECIPIENT, our_recipient.getValue());
	
	save_settings();
	
	DEBUG_AM("NEW PORT");
	DEBUG_AM(smtp_port);
	DEBUG_AM("NEW SERVER");
	DEBUG_AM(smtp_server);
	DEBUG_AM("NEW LOGIN");
	DEBUG_AM(EMAIL_LOGIN);
	DEBUG_AM("NEW PASSWORD");
	DEBUG_AM(EMAIL_PASSWORD);
	DEBUG_AM("NEW RECIPIENT");
	DEBUG_AM(RECIPIENT);
	
    return(conn_network(true));
  }
  return true;
}

const char* AlertMe::send(String subject, String message/*, String dest*/) {

  DEBUG_AM("Sending message to ");
  //DEBUG_AM(dest);
  DEBUG_AM(RECIPIENT);
  DEBUG_AM(':');
  DEBUG_AM("SUBJECT: ");
  DEBUG_AM(subject);
  DEBUG_AM("   BODY: ");
  DEBUG_AM(message);

  Gsender *gsender = Gsender::Instance();
  if (gsender->Subject(subject)->Send(RECIPIENT/*dest*/, message) == true) {

    DEBUG_AM("Message sent successfully.");

    return "SENT";
  }
  else {
    strcpy(last_error,gsender->getError());

    DEBUG_AM("Message sending failed. (");
	DEBUG_AM(last_error);
	DEBUG_AM(')');
 
   return last_error;
  }
}

void AlertMe::debug(bool enabled){
	alert_debug = enabled;
}

bool AlertMe::config(){
	wifiManager.setConfigPortalTimeout(180);
	if(!wifiManager.startConfigPortal("LNAlert Configuration")){
		return false;
	}
	return true;
}

void AlertMe::reset(bool format){
	if(format){
		SPIFFS.format();
	}
	wifiManager.resetSettings();
}

bool AlertMe::connect(bool debug_wifi) {
	DEBUG_AM("Mounting SPIFFS...");
	if (SPIFFS.begin()) {
		DEBUG_AM("Mounted file system.");
	}
	else {
		DEBUG_AM("Failed to mount FS!");
	}
	wifiManager.setDebugOutput(debug_wifi);
	load_settings();
	if(!conn_network()){
		return false;
	}
	return true;
}

const char* AlertMe::get_error() {
	return last_error;
}
