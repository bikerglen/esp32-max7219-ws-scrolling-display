// this code requires the following libraries:
//
// https://github.com/gilmaimon/ArduinoWebsockets
// https://arduinojson.org/
// https://github.com/MajicDesigns/MD_MAX72XX
//
// All these libraries can be installed using the Arduino library manager.
//
// Tested using an Adafruit Feather Huzzah ESP32 and cheap generic MAX7219 displays from Amazon.
//
// Connect display to SPI_SCK (5), SPI_SDO (18), and SPI_CS (A5) on the ESP32 Feather.
//
// Set ssid, password below to the credentials for your network
//
// Set host, port to the IP and port of a websocket server (see websocat command immediately below).
//
// Use websocat to set up a websockets echo server on IP a.b.c.d and port port:
//   websocat -E -t ws-l:a.b.c.d:port broadcast:mirror:
//
// Compile code and run it on the ESP32. It should connect to the websockets server created above.
// Use the serial monitor to view useful debug info. Baud is 115200.
//
// Use websocat to send data to the display via the websockets server:
//   echo '{"message":"Hello, world!","speed":"50"}' | websocat ws://a.b.c.d:port
//

#include <SPI.h>
#include <WiFi.h>
#include <ArduinoWebsockets.h>
#include <ArduinoJson.h>
#include <MD_MAX72xx.h>

const char* ssid = ""; //Enter SSID
const char* password = ""; //Enter Password
const char* websockets_server_host = "a.b.c.d"; //Enter server adress
const uint16_t websockets_server_port = PORT; // Enter server port

#define IMMEDIATE_NEW   1     // if 1 will immediately display a new message
#define HARDWARE_TYPE MD_MAX72XX::DR1CR0RR0_HW
#define MAX_DEVICES 8
#define CLK_PIN    5  // or SCK
#define DATA_PIN  18  // or MOSI
#define CS_PIN    A5  // or SS
#define SCROLL_DELAY  100  // in milliseconds
#define CHAR_SPACING  1 // pixels between characters
#define BUF_SIZE  75

MD_MAX72XX mx = MD_MAX72XX(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);

uint8_t curMessage[BUF_SIZE] = { " " };
uint8_t newMessage[BUF_SIZE] = { " " };
bool newMessageAvailable = false;
uint16_t scrollDelay = SCROLL_DELAY;

using namespace websockets;
WebsocketsClient client;
bool connected = false;
void onEvent (WebsocketsClient &client, WebsocketsEvent event, String data);
void onMessage (WebsocketsMessage message);

StaticJsonDocument<512> doc;

void setup()
{
    // open serial port
    Serial.begin (115200);
    
    // connect to wifi
    WiFi.begin (ssid, password);

    // wait some time to connect to wifi
    for(int i = 0; i < 10 && WiFi.status () != WL_CONNECTED; i++) {
        Serial.print (".");
        delay (1000);
    }

    // check if connected to wifi
    if (WiFi.status () != WL_CONNECTED) {
        Serial.println ("No Wi-Fi!");
        return;
    } else {
        Serial.println ("Connected to Wi-Fi!");
    }

    // run callbacks when events and messages are received
    connected = false;
    client.onEvent(onEvent);
    client.onMessage(onMessage);

    mx.begin();
    mx.setShiftDataInCallback (scrollDataSource);
    mx.setShiftDataOutCallback (scrollDataSink);
}


void loop()
{
    if (!connected) {
        // try to connect to Websockets server
        Serial.println ("connecting to websockets server.");
        connected = client.connect(websockets_server_host, websockets_server_port, "/mopidy/ws");
        if (connected) {
            Serial.println ("Connected!");
        }
    } else {
        // let the websockets client check for incoming messages
        if (client.available ()) {
            client.poll ();
        }
    }
    scrollText ();
}


void onEvent (WebsocketsClient &client, WebsocketsEvent event, String data)
{
    if (event == WebsocketsEvent::ConnectionClosed) {
        Serial.println ("Disconnected!");
        connected = false;
    }
}


void onMessage (WebsocketsMessage message)
{
    Serial.print("Got Message: ");
    Serial.println(message.data());

    DeserializationError error = deserializeJson (doc, message.c_str());

    if (error) {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.f_str());
        return;
    }

    const char *msgstr = doc["message"];
    const char *speedstr = doc["speed"];
    const int speed = doc["speed"];

    if (msgstr) {
        Serial.print ("message = '");
        Serial.print (msgstr);
        Serial.println ("'");

        strncpy ((char *)newMessage, msgstr, BUF_SIZE);
        int a = strlen (msgstr);
        if (a < (BUF_SIZE-2)) {
            newMessage[a] = ' ';
            newMessage[a+1] = 0;
        }
        newMessage[BUF_SIZE-2] = ' ';
        newMessage[BUF_SIZE-1] = 0;
        newMessageAvailable = true;
    }
    if (speedstr) {
        if (strlen (speedstr) != 0) {
            Serial.print ("speed = ");
            Serial.println (speed);
            scrollDelay = speed;
        }
    }
}


void scrollDataSink(uint8_t dev, MD_MAX72XX::transformType_t t, uint8_t col)
{
}


uint8_t scrollDataSource(uint8_t dev, MD_MAX72XX::transformType_t t)
// Callback function for data that is required for scrolling into the display
{
  static uint8_t* p = curMessage;
  static enum { NEW_MESSAGE, LOAD_CHAR, SHOW_CHAR, BETWEEN_CHAR } state = LOAD_CHAR;
  static uint8_t  curLen, showLen;
  static uint8_t  cBuf[15];
  uint8_t colData = 0;    // blank column is the default

#if IMMEDIATE_NEW
  if (newMessageAvailable)  // there is a new message waiting
  {
    state = NEW_MESSAGE;
    mx.clear(); // clear the display
  }
#endif

  // finite state machine to control what we do on the callback
  switch(state)
  {
    case NEW_MESSAGE:   // Load the new message
      memcpy(curMessage, newMessage, BUF_SIZE);  // copy it in
      newMessageAvailable = false;    // used it!
      p = curMessage;
      state = LOAD_CHAR;
      break;

    case LOAD_CHAR: // Load the next character from the font table
      showLen = mx.getChar(*p++, sizeof(cBuf)/sizeof(cBuf[0]), cBuf);
      curLen = 0;
      state = SHOW_CHAR;

      // if we reached end of message, opportunity to load the next
      if (*p == '\0')
      {
        p = curMessage;     // reset the pointer to start of message
#if !IMMEDIATE_NEW
        if (newMessageAvailable)  // there is a new message waiting
        {
          state = NEW_MESSAGE;    // we will load it here
          break;
        }
#endif
      }
      // !! deliberately fall through to next state to start displaying

    case SHOW_CHAR: // display the next part of the character
      colData = cBuf[curLen++];
      if (curLen == showLen)
      {
        showLen = CHAR_SPACING;
        curLen = 0;
        state = BETWEEN_CHAR;
      }
      break;

    case BETWEEN_CHAR: // display inter-character spacing (blank columns)
      colData = 0;
      curLen++;
      if (curLen == showLen)
        state = LOAD_CHAR;
      break;

    default:
      state = LOAD_CHAR;
  }

  return(colData);
}


void scrollText(void)
{
    static uint32_t prevTime = 0;
    
    // Is it time to scroll the text?
    if (millis()-prevTime >= scrollDelay) {
        mx.transform(MD_MAX72XX::TSL);  // scroll along - the callback will load all the data
        prevTime = millis();      // starting point for next time
    }
}
