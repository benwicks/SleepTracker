#include <SdFat.h>
#include <SdFatUtil.h>
#include <Ethernet.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_ADXL345.h>

/************ ACCELEROMETER STUFF ************/
/* Assign a unique ID to this sensor at the same time */
Adafruit_ADXL345 accel = Adafruit_ADXL345(12345);

/************ ETHERNET STUFF ************/
byte mac[] = { 
  0x90, 0xA2, 0xDA, 0x0D, 0x9D, 0x7C };
byte ip[] = { 
  10, 3, 3, 50 };
EthernetServer server(80);

/************ SDCARD STUFF ************/
Sd2Card card;
SdVolume volume;
SdFile root;
SdFile file;

// store error strings in flash to save RAM
#define error(s) error_P(PSTR(s))

boolean doRecordData;
int lastButtonState;
const int greenLEDPin = 8;
const int redLEDPin = 7;
const int buttonPin = 3;
char cstr[11] =  {'l','o','g',' ',' ',' ','.','c','s','v'};
int fileNumber = 0;

void error_P(const char* str) {
  PgmPrint("error: ");
  SerialPrintln_P(str);
  if (card.errorCode()) {
    PgmPrint("SD error: ");
    Serial.print(card.errorCode(), HEX);
    Serial.print(',');
    Serial.println(card.errorData(), HEX);
  }
  while(1);
}

void setup()
{
  // Open serial communications and wait for port to open:
  Serial.begin(9600);

  doRecordData = false;
  lastButtonState = LOW;
  pinMode(greenLEDPin, OUTPUT);
  digitalWrite(greenLEDPin, LOW);
  pinMode(redLEDPin, OUTPUT);
  digitalWrite(redLEDPin, LOW);
  pinMode(buttonPin, INPUT);
  digitalWrite(buttonPin, LOW);
  
  // initialize the SD card at SPI_HALF_SPEED to avoid bus errors with
  // breadboards.  use SPI_FULL_SPEED for better performance.
  pinMode(10, OUTPUT);                       // set the SS pin as an output (necessary!)
  digitalWrite(10, HIGH);                    // but turn off the W5100 chip!

  if (!card.init(SPI_HALF_SPEED, 4)) {
    digitalWrite(redLEDPin, HIGH);
    error("card.init failed!");
  }

  // initialize a FAT volume
  if (!volume.init(&card)) error("vol.init failed!");

  PgmPrint("Volume is FAT");
  Serial.println(volume.fatType(),DEC);
  Serial.println();

  if (!root.openRoot(&volume)) {
    digitalWrite(redLEDPin, HIGH);
    error("openRoot failed");
  }

  /*
  // Remove datalog file
  if (!file.open(&root, "DATALOG.CSV", O_RDWR | O_TRUNC)) {
    Serial.println("Failed to remove datalog.txt");
    for (int i = 0; i < 3; i++) {
      digitalWrite(redLEDPin, HIGH);
      delay(100);
      digitalWrite(redLEDPin, LOW);
      delay(100);
    }
  }
  file.close();
  */
  
  // list file in root with date and size
  PgmPrintln("Files found in root:");
  root.ls(LS_DATE | LS_SIZE);
  Serial.println();

  // Recursive list of all directories
  PgmPrintln("Files found in all dirs:");
  root.ls(LS_R);
  Serial.println();
  PgmPrintln("Done");

  // Serial.println("Accelerometer Test");
  // TODO: Calibrate for at rest values

  /* Initialise the sensor */
  if(!accel.begin())
  {
    /* There was a problem detecting the ADXL345 ... check your connections */
    Serial.println("Ooops, no ADXL345 detected ... Check your wiring!");
    digitalWrite(redLEDPin, HIGH);
    while(1);
  }
  
  // Debugging complete, we start the server!
  Ethernet.begin(mac, ip);
  server.begin();
  Serial.println("Startup complete.");

  // Good alert
  for (int i = 0; i < 3; i++) {  
    digitalWrite(greenLEDPin, HIGH);
    delay(100);
    digitalWrite(greenLEDPin, LOW);
    delay(50);
  }    
}

void ListFiles(EthernetClient client, uint8_t flags) {
  // This code is just copied from SdFile.cpp in the SDFat library
  // and tweaked to print to the client output in html!
  dir_t p;

  root.rewind();
  client.println("<ul>");
  while (root.readDir(&p) > 0) {
    // done if past last used entry
    if (p.name[0] == DIR_NAME_FREE) break;

    // skip deleted entry and entries for . and  ..
    if (p.name[0] == DIR_NAME_DELETED || p.name[0] == '.') continue;

    // only list subdirectories and files
    if (!DIR_IS_FILE_OR_SUBDIR(&p)) continue;

    // print any indent spaces
    client.print("<li><a href=\"");
    for (uint8_t i = 0; i < 11; i++) {
      if (p.name[i] == ' ') continue;
      if (i == 8) {
        client.print('.');
      }
      client.print((char)p.name[i]);
    }
    client.print("\">");

    // print file name with possible blank fill
    for (uint8_t i = 0; i < 11; i++) {
      if (p.name[i] == ' ') continue;
      if (i == 8) {
        client.print('.');
      }
      client.print((char)p.name[i]);
    }

    client.print("</a>");

    if (DIR_IS_SUBDIR(&p)) {
      client.print('/');
    }

    // print modify date/time if requested
    if (flags & LS_DATE) {
      root.printFatDate(p.lastWriteDate);
      client.print(' ');
      root.printFatTime(p.lastWriteTime);
    }
    // print size if requested
    if (!DIR_IS_SUBDIR(&p) && (flags & LS_SIZE)) {
      client.print(' ');
      client.print(p.fileSize);
    }
    client.println("</li>");
  }
  client.println("</ul>");
}

// How big our line buffer should be. 100 is plenty!
#define BUFSIZ 100

void loop()
{
  int buttonState = digitalRead(buttonPin);
  if (buttonState != lastButtonState) {
    digitalWrite(redLEDPin, HIGH);
    if (buttonState == HIGH) {
      doRecordData = !doRecordData;
      digitalWrite(greenLEDPin, doRecordData);
      delay(10);
      if (doRecordData) {
        // Create new file
        int p1 = 0;
        int p2 = 0;
        int p3 = 0;
        char buffer[3];
        // lowest fn = 000.csv
        // highest fn = 999.csv
        boolean alreadyExists = true;
        while (alreadyExists) {
          p1 = (int) (fileNumber / 100);
          p2 = (int) (fileNumber / 10);
          p3 = fileNumber % 10;
          cstr[3] = itoa(p1, buffer, 10)[0];
          cstr[4] = itoa(p2, buffer, 10)[0];
          cstr[5] = itoa(p3, buffer, 10)[0];
          Serial.println(cstr);
          if (root.exists(cstr)) {
            // check on next file
            fileNumber = fileNumber + 1;
          } else {
            Serial.println("Open failed");
            if (!file.open(&root, cstr, O_CREAT | O_WRITE)) {
              Serial.println("Error writing first time");
            } else {
              file.print("TIME,X,Y,Z\n");
              file.close();
            }
            alreadyExists = false;
          }
        }
        if (fileNumber > 999) {
          // Maxed out number of files!
          while (true) {
            digitalWrite(redLEDPin, HIGH);
            delay(50);
            digitalWrite(redLEDPin, LOW);
            delay(50);
          }
        }
      }
    } 
    digitalWrite(redLEDPin, LOW);
  }
  lastButtonState = buttonState;
  while (doRecordData) {
    digitalWrite(greenLEDPin, LOW);
    /* Get a new sensor event */
    sensors_event_t event; 
    accel.getEvent(&event);

    if (!file.open(&root, cstr, O_CREAT | O_APPEND | O_WRITE)) {
      Serial.println("Opening log for append failed");
      delay(25);
      digitalWrite(redLEDPin, HIGH);
    } else {
      file.print(millis(), DEC);
      file.print(',');
      file.print(event.acceleration.x, DEC);
      file.print(',');
      file.print(event.acceleration.y, DEC);
      file.print(',');
      file.print(event.acceleration.z, DEC);
      file.print('\n');
      file.close();
      digitalWrite(greenLEDPin, HIGH);
      digitalWrite(redLEDPin, LOW);
    }
    delay(1000);
    buttonState = digitalRead(buttonPin);
    if (buttonState != lastButtonState) {
      digitalWrite(redLEDPin, HIGH);
      if (buttonState == HIGH) {
        doRecordData = !doRecordData;
        digitalWrite(greenLEDPin, doRecordData);
        if (!doRecordData) {
          // increment file name for next time
          fileNumber = fileNumber + 1;
        }
        delay(10);
      } 
      digitalWrite(redLEDPin, LOW);
    }
    lastButtonState = buttonState;    
  }
  
  EthernetClient client = server.available();
  if (client) {
    char clientline[BUFSIZ];
    int index = 0;
    
    // an http request ends with a blank line
    boolean current_line_is_blank = true;

    // reset the input buffer
    index = 0;

    while (client.connected()) {
      if (client.available()) {
        char c = client.read();

        // If it isn't a new line, add the character to the buffer
        if (c != '\n' && c != '\r') {
          clientline[index] = c;
          index++;
          // are we too big for the buffer? start tossing out data
          if (index >= BUFSIZ) 
            index = BUFSIZ -1;

          // continue to read more data!
          continue;
        }

        // got a \n or \r new line, which means the string is done
        clientline[index] = 0;

        // Print it out for debugging
        Serial.println(clientline);

        // Look for substring such as a request to get the root file
        if (strstr(clientline, "GET / ") != 0) {
          // send a standard http response header
          client.println("HTTP/1.1 200 OK");
          client.println("Content-Type: text/html");
          client.println();

          // print all the files, use a helper to keep it clean
          client.println("<h2>Files:</h2>");
          ListFiles(client, LS_SIZE);
          delay(10);
          client.stop();
          break;
        } else if (strstr(clientline, "GET /") != 0) {
          // this time no space after the /, so a sub-file!
          char *filename;

          filename = clientline + 5; // look after the "GET /" (5 chars)
          // a little trick, look for the " HTTP/1.1" string and 
          // turn the first character of the substring into a 0 to clear it out.
          (strstr(clientline, " HTTP"))[0] = 0;

          // print the file we want
          Serial.println(filename);

          if (!file.open(&root, filename, O_READ)) {
            client.println("HTTP/1.1 404 Not Found");
            client.println("Content-Type: text/html");
            client.println();
            client.println("<h2>File Not Found!</h2>");
            break;
          }

          Serial.println("Opened!");

          client.println("HTTP/1.1 200 OK");
          client.println("Content-Type: text/plain");
          client.println();

          int16_t c;
          while ((c = file.read()) > 0) {
            // uncomment the serial to debug (slow!)
            //Serial.print((char)c);
            client.print((char)c);
          }
          file.close();
          delay(10);
          client.stop();
          break;
        } else {
          // everything else is a 404
          client.println("HTTP/1.1 404 Not Found");
          client.println("Content-Type: text/html");
          client.println();
          client.println("<h2>File Not Found!</h2>");
          delay(10);
          client.stop();
          break;
        }
      }
    }
  }
}

