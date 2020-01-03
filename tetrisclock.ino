// Author - Christopher Kong
// This is a modified version of Brian Lough's Tetris Clock project.
// You can find the original source at: https://github.com/witnessmenow/WiFi-Tetris-Clock
// This project doesn't use a wifi connection to set a time, instead
// hosting a web page to allow users to configure the clock settings through
// a web browser.
//
// The web page is available seperately in this repository. You can access
// it by connecting to the Wifi Access Point "ESP32" with the password "12345678"
// and going to 192.168.4.1 in your web browser.

// ----------------------------
// Standard Libraries - Already Installed if you have ESP32 set up
// ----------------------------

#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiAP.h>

// ----------------------------
// Additional Libraries - each one of these will need to be installed.
// ----------------------------

// Enabling this is meant to have a performance
// improvement but its worse for me.
// https://github.com/2dom/PxMatrix/pull/103
//#define double_buffer


// Display drivers.
// https://github.com/2dom/PxMatrix
#include <PxMatrix.h>

// Adafruit GFX library is a dependancy for the PxMatrix Library
// Can be installed from the library manager
// https://github.com/adafruit/Adafruit-GFX-Library

// I'm a tetris purist, so I had to edit this library to make it use
// traditional tetris characters. My version of the edited files will
// be available.
// https://github.com/toblum/TetrisAnimation
#include <TetrisMatrixDraw.h>

// Clock
// https://github.com/ropg/ezTime
#include <ezTime.h>

// ---- Stuff to configure ----

// ESP32 Configuration Access Point.
// The Default ID/PW is ESP32/12345678
const char *issid = "For Some Reason";
const char *ipassword = "This Doesn't Actually Work... But just in case it does, change it";

// This is a relic of Brian Lough's code, but it's still needed for configuring ezTime.
// If you want, you can change the timezone to your own using this list:
// https://en.wikipedia.org/wiki/List_of_tz_database_time_zones
#define MYTIMEZONE "America/Los_Angeles"

WiFiServer server(80);

// Sets whether the clock should be 12 hour format or not.
bool twelveHourFormat = true;

// Toggle between resetting one/all numbers
bool forceRefresh = false;
// -----------------------------

// ----- Wiring -------
#define P_LAT 22
#define P_A 19
#define P_B 23
#define P_C 18
#define P_D 5
#define P_E 15
#define P_OE 26 //TinyPICO
// ---------------------

portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
hw_timer_t * timer = NULL;
hw_timer_t * animationTimer = NULL;

PxMATRIX display(64, 32, P_LAT, P_OE, P_A, P_B, P_C, P_D, P_E);

TetrisMatrixDraw tetris(display);  // Main clock
TetrisMatrixDraw tetris2(display); // The "M" of AM/PM
TetrisMatrixDraw tetris3(display); // The "P" or "A" of AM/PM

Timezone myTZ;
unsigned long oneSecondLoopDue = 0;

bool showColon = true;
volatile bool finishedAnimating = false;
bool displayIntro = true;

String lastDisplayedTime = "";
String lastDisplayedAmPm = "";

int dispFrequency = 10000;

// This method is needed for driving the display
void IRAM_ATTR display_updater() {
  portENTER_CRITICAL_ISR(&timerMux);
  display.display(10);
  portEXIT_CRITICAL_ISR(&timerMux);
}

// This method is for controlling the tetris library draw calls
void animationHandler()
{
#ifndef double_buffer
  portENTER_CRITICAL_ISR(&timerMux);
#endif

  // Not clearing the display and redrawing it when you
  // dont need to improves how the refresh rate appears
  if (!finishedAnimating) {
#ifdef double_buffer
    display.fillScreen(tetris.tetrisBLACK);
#else
    display.clearDisplay();
#endif
    //display.fillScreen(tetris.tetrisBLACK);
    if (displayIntro) {
      finishedAnimating = tetris.drawText(1, 21);
    } else {
      if (twelveHourFormat) {
        // Place holders for checking are any of the tetris objects
        // currently still animating.
        bool tetris1Done = false;
        bool tetris2Done = false;
        bool tetris3Done = false;

        tetris1Done = tetris.drawNumbers(-6, 26, showColon);
        tetris2Done = tetris2.drawText(56, 25);

        // Only draw the top letter once the bottom letter is finished.
        if (tetris2Done) {
          tetris3Done = tetris3.drawText(56, 15);
        }

        finishedAnimating = tetris1Done && tetris2Done && tetris3Done;

      } else {
        finishedAnimating = tetris.drawNumbers(2, 26, showColon);
      }
    }
#ifdef double_buffer
    display.showBuffer();
#endif
  }
#ifndef double_buffer
  portEXIT_CRITICAL_ISR(&timerMux);
#endif
}

TaskHandle_t ConfigServer;


// --------------------------------
//             SETUP
// --------------------------------
void setup() {
  Serial.begin(115200);

  // Start our wifi
  WiFi.mode(WIFI_AP_STA);
  Serial.println("Wifi Initialized");

  // Intialise display library
  display.begin(16, SPI_BUS_CLK, 27, SPI_BUS_MISO, SPI_BUS_SS); // TinyPICO
  display.flushDisplay();

  // This timer controlls the refresh rate of the display. I don't recommend
  // Touching it, but if you must...
  timer = timerBegin(0, 80, true);
  timerAttachInterrupt(timer, &display_updater, true);
  timerAlarmWrite(timer, 2000, true); 
  timerAlarmEnable(timer);
  yield();
  
#ifdef double_buffer
  display.fillScreen(tetris.tetrisBLACK);
#else
  display.clearDisplay();
#endif

#ifdef double_buffer
  display.showBuffer();
#endif

  // Setup EZ Time
  setDebug(INFO);

  // This is a relic of WiFi clock. waitForSync()
  // tells EZ Time to try to connect to an NTP server
  // to set the clock. However, we don't need it.
  // waitForSync();

  Serial.println();
  Serial.println("UTC:             " + UTC.dateTime());

  myTZ.setLocation(F(MYTIMEZONE));
  
  Serial.print(F("Time in your set timezone:         "));
  Serial.println(myTZ.dateTime());

  // (Probably) Prevent integer underflow during time adjust,
  // because the default time is 0 seconds from epoch (1 Jan 1970) and you can
  // subtract using the web interface.
  // Constant is one month (31 Days) worth of seconds.
  myTZ.setTime(myTZ.now() + 2678400);

  // This creates a web server running on the other core (ESP32 has two cores,
  // might as well)
  xTaskCreatePinnedToCore(
      RunAP, /* Function to implement the task */
      "WiFi Access Point & Server", /* Name of the task */
      10000,  /* Stack size in words */
      NULL,  /* Task input parameter */
      0,  /* Priority of the task */
      &ConfigServer,  /* Task handle. */
      0); /* Core where the task should run */
      

#ifdef double_buffer
  display.fillScreen(tetris.tetrisBLACK);
#else
  display.clearDisplay();
#endif

  // Vanity thing... call it "branding".
  // Change it if you want, but do note that this
  // is the max length of your message (9) on a 
  // 64 wide by 32 tall display.
  tetris.setText("CHICKALOO");
  
  // Start the Animation Timer
  animationTimer = timerBegin(1, 80, true);
  timerAttachInterrupt(animationTimer, &animationHandler, true);
  timerAlarmWrite(animationTimer, dispFrequency, true);
  timerAlarmEnable(animationTimer);

  // Wait for the animation to finish
  while (!finishedAnimating)
  {
    delay(10); //waiting for intro to finish
  }

  // Leave the message for 2 seconds
  delay(2000);
  
  finishedAnimating = false;
  displayIntro = false;
  tetris.scale = 2;
}

// This is where the wifi server lives!
// I opted to use a super simple system, but
// you could probably do way more with a proper
// library.
void RunAP(void * param) {
  
  Serial.println("Configuring access point...");

  // This is supposed to set the name and password of the AP, but... it doesn't.
  WiFi.softAP(issid, ipassword);
  
  IPAddress myIP = WiFi.softAPIP();
  Serial.print("AP IP address: "); // Default is 192.168.4.1
  Serial.println(myIP);
  server.begin();

  Serial.print("Server started on core ");
  Serial.println(xPortGetCoreID());
  for(;;) {

    // If you don't have this delay, your TinyPico will crash because its "OS" has anxiety.
    vTaskDelay(10);
    
    WiFiClient client = server.available();   // listen for incoming clients
  
    if (client) {                             // if you get a client,
      Serial.println("New Client.");          // print a message out the serial port
      String currentLine = "";                // make a String to hold incoming data from the client
      while (client.connected()) {            // loop while the client's connected
        if (client.available()) {             // if there's bytes to read from the client,
          char c = client.read();             // read a byte, then
          Serial.write(c);                    // print it out the serial monitor
          if (c == '\n') {                    // if the byte is a newline character
  
            // if the current line is blank, you got two newline characters in a row.
            // that's the end of the client HTTP request, so send a response:
            if (currentLine.length() == 0) {
              // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
              // and a content-type so the client knows what's coming, then a blank line:
              client.println("HTTP/1.1 200 OK");
              client.println("Content-type:text/html");
              client.println();
  
              // the content of the HTTP response follows the header:
              client.print("<html>");
              client.print("<head>");

              // I would have used Bootstrap, but that relies on an internet connection and I wasn't sure if the minified version could fit on the TinyPico.
              // Instead, I got the "source" styling and just made my own.
              client.print("<style>");
              client.print("  .container {padding-right: 15px;padding-left: 15px;margin-right: auto;margin-left: auto; display:table-cell;vertical-align:middle;}");
              client.print("  .row{display:table; margin:25px;}");
              client.print("  .bg-dark{background-color:#2d132c;}");
              client.print("  .bg-highlight{background-color:#262626;}");
              client.print("  .bg-red{background-color:#ff6464;}");
              client.print("  .bg-grey{background-color:#555555;}");
              client.print("  .square-btn{width:100px;font-size:3em;margin:25px 25px;padding:23px 0;border-radius:5px;color:#eee;float:left;box-shadow: 0px 5px 10px #111;}");
              client.print("  .long-btn{width:250px;font-size:3em;margin:25px 25px;padding:23px 0;border-radius:5px;color:#eee;float:left;box-shadow: 0px 5px 10px #111;}");
              client.print("  .mid-text{width:100px;font-size:1.5em;padding-top:5px;margin:0px 25px;padding:0 auto;float:left;}");
              client.print("  .mid-text-long{width:250px;font-size:1.5em;padding-top:5px;margin:0px 25px;padding:0 auto;float:left;}");
              client.print("  .shadow{box-shadow: 0px 5px 15px #000;}");
              client.print("</style>");
              client.print("</head>");
              client.print("<body style=\"margin:0;padding:50px;width:100%;height:100%;background-color:#121212;color:#efefef;font-family:'Verdana', sans-serif;\">");
              client.print("  <div id=\"container\" class=\"container\" style=\"margin:auto;\">");
              client.print("    <div id=\"timeadjust\" class=\"row shadow\" style=\"width:600px;background-color:#1a1a1a;border-radius:5px;float:left;\">");
              client.print("      <div id=\"title\" style=\"width:575px;margin:25px 0px 0px 0px;font-size:2em;padding:15px 0px 15px 25px;background-color:#262626;\">");
              client.print("        <div>Time Adjustment</div>");
              client.print("      </div>");
              client.print("      <div id=\"toprow\" style=\"height:50px;display:table;text-align:center;\">");
              client.print("        <a href=\"/PTH\"><div id=\"pth\" class=\"square-btn bg-red\">+</div></a>");
              client.print("        <a href=\"/PH\"><div id=\"ph\" class=\"square-btn bg-red\">+</div></a>");
              client.print("        <a href=\"/PTM\"><div id=\"ptm\" class=\"square-btn bg-red\">+</div></a>");
              client.print("        <a href=\"/PM\"><div id=\"pm\" class=\"square-btn bg-red\">+</div></a>");
              client.print("      </div>");
              client.print("      <div id=\"midrow\" style=\"width:100%;height:50px;display:table;text-align:center;\">");
              client.print("        <div id=\"ph\" class=\"mid-text\">12 H</div>");
              client.print("        <div id=\"ph\" class=\"mid-text\">1 H</div>");
              client.print("        <div id=\"ph\" class=\"mid-text\">10 M</div>");
              client.print("        <div id=\"ph\" class=\"mid-text\">1 M</div>");
              client.print("      </div>");
              client.print("      <div id=\"botrow\" style=\"width:100%;height:50px;display:table;text-align:center;\">");
              client.print("        <a href=\"/MTH\"><div id=\"mth\" class=\"square-btn bg-red\">-</div></a>");
              client.print("        <a href=\"/MH\"><div id=\"mh\" class=\"square-btn bg-red\">-</div></a>");
              client.print("        <a href=\"/MTM\"><div id=\"mtm\" class=\"square-btn bg-red\">-</div></a>");
              client.print("        <a href=\"/MM\"><div id=\"mm\" class=\"square-btn bg-red\">-</div></a>");
              client.print("      </div>");
              client.print("    </div>");
              client.print("    <div id=\"clocksettings\" class=\"row shadow\" style=\"width:600px;background-color:#1a1a1a;border-radius:5px;float:left\">");
              client.print("      <div id=\"title\" style=\"width:575px;margin:25px 0px 0px 0px;font-size:2em;padding:15px 0px 15px 25px;background-color:#262626;\">");
              client.print("        <div>Clock Settings</div>");
              client.print("      </div>");
              client.print("      <div id=\"toprow\" style=\"height:50px;display:table;text-align:center;\">");

              // For some reason, it's possible to crash the clock by spamming the speed adjust buttons in particular, so we turn them off if they cannot be pressed.
              if (dispFrequency > 4000) {
                client.print("        <a href=\"/PA\"><div id=\"pth\" class=\"long-btn bg-red\">▲</div></a>");
              } else {
                client.print("        <div id=\"pa\" class=\"long-btn bg-grey\">▲</div>");
              }
              client.print("        <a href=\"/TF\"><div id=\"ph\" class=\"long-btn bg-red\">24H/12H</div></a>");
              client.print("      </div>");
              client.print("      <div id=\"midrow\" style=\"width:100%;height:50px;display:table;text-align:center;\">");
              client.print("        <div id=\"ph\" class=\"mid-text-long\">Animation Speed</div>");
              client.print("        <div id=\"ph\" class=\"mid-text-long\">Toggle</div>");
              client.print("      </div>");
              client.print("      <div id=\"botrow\" style=\"width:100%;height:50px;display:table;text-align:center;\">");
              if (dispFrequency < 16000) {
                client.print("        <a href=\"/MA\"><div id=\"ma\" class=\"long-btn bg-red\">▼</div></a>");
              } else {
                client.print("        <div id=\"ma\" class=\"long-btn bg-grey\">▼</div>");
              }
              client.print("        <a href=\"/TR\"><div id=\"ph\" class=\"long-btn bg-red\">Redraw</div></a>");
              client.print("      </div>");
              client.print("    </div>");
              client.print("  </div>");
              client.print("</body>");
              client.print("</html>");

              // The HTTP response ends with another blank line:
              client.println();
              // break out of the while loop:
              break;
            } else {    // if you got a newline, then clear currentLine:
              currentLine = "";
            }
          } else if (c != '\r') {  // if you got anything else but a carriage return character,
            currentLine += c;      // add it to the end of the currentLine
          }
  
          // Handle client requests
          // TIME ADJUSTMENTS
          
          if (currentLine.endsWith("GET /PTH")) {
            myTZ.setTime(myTZ.now() + 43200);
            setMatrixTime();
          }          
          if (currentLine.endsWith("GET /PH")) {
            myTZ.setTime(myTZ.now() + 3600);
            setMatrixTime();
          }
          if (currentLine.endsWith("GET /PTM")) {
            myTZ.setTime(myTZ.now() + 600);
            setMatrixTime();
          }
          if (currentLine.endsWith("GET /PM")) {
            myTZ.setTime(myTZ.now() + 60);
            setMatrixTime();
          }
          if (currentLine.endsWith("GET /MTH")) {
            myTZ.setTime(myTZ.now() - 43200);
            setMatrixTime();
          }
          if (currentLine.endsWith("GET /MH")) {
            myTZ.setTime(myTZ.now() - 3600);
            setMatrixTime();
          }
          if (currentLine.endsWith("GET /MTM")) {
            myTZ.setTime(myTZ.now() - 600);
            setMatrixTime();
          }
          if (currentLine.endsWith("GET /MM")) {
            myTZ.setTime(myTZ.now() - 60);
            setMatrixTime();
          }

          // Adjust animation speed. Crashes if it gets too fast or slow!
          if (currentLine.endsWith("GET /PA")) {
            if (dispFrequency > 4000) {
              dispFrequency -= 2000;
              timerAlarmWrite(animationTimer, dispFrequency, true);
            }
            setMatrixTime();
          }
          if (currentLine.endsWith("GET /MA")) {
            if (dispFrequency < 16000) {
              dispFrequency += 2000;
              timerAlarmWrite(animationTimer, dispFrequency, true);
            }
            setMatrixTime();
          }

          // Misc clock functions
          if (currentLine.endsWith("GET /TR")) {
            forceRefresh = !forceRefresh;
            setMatrixTime();
          }
          if (currentLine.endsWith("GET /TF")) {
            twelveHourFormat = !twelveHourFormat;
            setMatrixTime();
          }
        }
      }
      // close the connection:
      client.stop();
      Serial.println("Client Disconnected.");
    }
  }
}

// setMatrixTime updates the clock face IFF it needs to be updated
void setMatrixTime() {
  String timeString = "";
  String AmPmString = "";
  
  if (twelveHourFormat) {

    timeString = myTZ.dateTime("g:i");

    // Pad with a space if the time is only 4 characters 
    if (timeString.length() == 4) {
      timeString = " " + timeString;
    }

    //Get if its "AM" or "PM"
    AmPmString = myTZ.dateTime("A");
    if (lastDisplayedAmPm != AmPmString) {
      Serial.println(AmPmString);
      lastDisplayedAmPm = AmPmString;
      tetris2.setText("M", forceRefresh);

      // Parse out first letter of String
      tetris3.setText(AmPmString.substring(0, 1), forceRefresh);
    }
  } else {
    // Get time in format "01:15" or "22:15"(24 hour with leading 0)
    timeString = myTZ.dateTime("H:i");
  }

  // Only update Time if its different
  if (lastDisplayedTime != timeString) {
    Serial.println(timeString);
    lastDisplayedTime = timeString;
    tetris.setTime(timeString, forceRefresh);

    // Must set this to false so animation knows
    // to start again
    finishedAnimating = false;
  }
}

void handleColonAfterAnimation() {

  // It will draw the colon every time, but when the colour is black it
  // should look like its clearing it.
  uint16_t colour =  showColon ? tetris.tetrisYELLOW : tetris.tetrisBLACK;
  // The x position that you draw the tetris animation object
  int x = twelveHourFormat ? -6 : 2;
  // The y position adjusted for where the blocks will fall from
  // (this could be better!)
  int y = 26 - (TETRIS_Y_DROP_DEFAULT * tetris.scale);
  tetris.drawColon(x, y, colour);
}


void loop() {
  unsigned long now = millis();
  if (now > oneSecondLoopDue) {

    // Each second, check time and update colon
    setMatrixTime();
    
    showColon = !showColon;

    // Handle blinking colon after animation
    if (finishedAnimating) {
      handleColonAfterAnimation();
    }
    
    oneSecondLoopDue = now + 1000;
  }
}
