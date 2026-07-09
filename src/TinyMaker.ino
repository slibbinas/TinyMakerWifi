/**
 * @file TinyMaker-Firmware-v1-0-2.ino
 * @author Tinymaker Team (Original), Viktoras Sidlauskas (Modified)
 * @version 1.0.2-vs-wifi-0.1
 * @date 2027-07-26
 * @brief Main firmware for Tinymaker MSLA 3D Printer.
 *
 * board ESP32-WROOM-32E-N4
 * Modifications by Viktoras Šidlauskas slibbinas@gmail.com
 *
 * This file handles the entire print process, UI interaction, motor control, and UV exposure logic. 
 *
 * changes
 * - Added detailed comments (EN)
 * - Removed dead code
 * - Code organization and cleanup
 * - Corrected typo: "Maintenance" of Screen 2: Main Menu
 */

 // ===================================================================================
// Build switches - set to 1/0 and recompile
// ===================================================================================
#define ENABLE_NETWORK       1   // 0 = firmware be WiFi/upload (kaip originalas)
#define ENABLE_SERIAL_DEBUG  0   // 0 = jokio Serial isvesties

#if ENABLE_SERIAL_DEBUG
  #define DBG    Serial.printf
  #define DBGLN(x)  Serial.println(x)
#else
  static inline void DBG(const char *, ...) {}
  #define DBGLN(x)
#endif

// Maksimalus modelio PNG failu skaicius: N x 0.05 mm auksciui.
// Originalas: 1080 (=54 mm). Pakelta iki 1200 (=60 mm) - realus sio
// spausdintuvo limitas; Z eigos atsarga kelimui lieka (max_height = 68 mm).
#define MAX_LAYER_FILES 1200

#include <SPI.h>
#include <EEPROM.h>              // For storing settings persistently
#include <AccelStepper.h>        // Stepper motor control library
#include <Arduino_GFX_Library.h> // Graphics library for driving displays
#include "FreeSans8pt7b.h"       // Custom font
#include <PNGdec.h>              // PNG decoder library for reading print layers
#include <SdFat.h>               // SD card file system library

#if ENABLE_NETWORK
#include <WiFi.h>
#include <WiFiManager.h>   // tzapu, v2.0.x
#include <ESPmDNS.h>
#include <WebServer.h>
#include <unzipLIB.h>      // bitbank2
#include <Update.h>        // web /update firmware flashing
#include <ArduinoOTA.h>    // PlatformIO espota uploads
#include <esp_wifi.h>      // reliable WiFi credential erase / config read
#endif
#include <Preferences.h>   // NVS: forcePortal flag, total print hours
                           // (outside the network guard - print hours are
                           // tracked in network-free builds too)

// Resin estimation globals live in PNG.ino - forward-declare for the
// files compiled before it (Interface.ino, TinyMaker.ino)
extern double resinUsedMl;
extern double resinEstimateMl;
bool estimateResin();               // returns true if user chose Start
bool startFromResin = false;        // set when Start pressed on resin screen
bool webStartPrint = false;         // set by the web SD manager after preview validation
bool webResumePrint = false;        // set by the web dashboard while paused

// Print-list selection kind: false = model folder (OK prints), true =
// .sl1/.zip archive in the SD root (OK imports/converts it). Maintained by
// listEntryValid() in Folder.ino.
bool selIsArchive = false;

// Factory settings reset - shared by setup() (bad/blank EEPROM) and the
// Settings -> "Back to Default" menu (Interface.ino).
void resetSettingsToDefault();

// Total print time, persisted in NVS (survives firmware re-flash, unlike the
// EEPROM settings area). Written rarely - only at print end/cancel - to spare
// flash wear. A power loss mid-print loses that session's time (accepted).
Preferences sysPrefs;
uint32_t totalPrintSecs = 0;        // lifetime printing seconds (loaded in setup)
unsigned long printStartMs = 0;     // millis() when the current print started
uint16_t uiTimeoutSecs = 0;         // 0 = never blank the UI screen
bool uvLedEnabled = true;           // false = dry-run motion/display only
bool mqttEnabled = false;           // Smart Home / MQTT integration scaffold
String mqttHost = "";
uint16_t mqttPort = 1883;
String mqttUser = "";
String mqttPass = "";
String mqttTopic = "TinyMaker";
unsigned long lastUiActivityMs = 0;
bool uiBlanked = false;

void savePrintTime() {
  totalPrintSecs += (millis() - printStartMs) / 1000UL;
  sysPrefs.begin("tinymaker", false);
  sysPrefs.putULong("printSecs", totalPrintSecs);
  sysPrefs.end();
}

void loadDeviceConfig() {
  sysPrefs.begin("tinymaker", true);
  totalPrintSecs = sysPrefs.getULong("printSecs", 0);
  uiTimeoutSecs = sysPrefs.getUShort("uiTimeout", 0);
  uvLedEnabled = sysPrefs.getBool("uvLed", true);
  mqttEnabled = sysPrefs.getBool("mqttEnabled", false);
  mqttHost = sysPrefs.getString("mqttHost", "");
  mqttPort = sysPrefs.getUShort("mqttPort", 1883);
  mqttUser = sysPrefs.getString("mqttUser", "");
  mqttPass = sysPrefs.getString("mqttPass", "");
  mqttTopic = sysPrefs.getString("mqttTopic", "TinyMaker");
  sysPrefs.end();
}

void saveDeviceConfig() {
  sysPrefs.begin("tinymaker", false);
  sysPrefs.putUShort("uiTimeout", uiTimeoutSecs);
  sysPrefs.putBool("uvLed", uvLedEnabled);
  sysPrefs.putBool("mqttEnabled", mqttEnabled);
  sysPrefs.putString("mqttHost", mqttHost);
  sysPrefs.putUShort("mqttPort", mqttPort);
  sysPrefs.putString("mqttUser", mqttUser);
  sysPrefs.putString("mqttPass", mqttPass);
  sysPrefs.putString("mqttTopic", mqttTopic);
  sysPrefs.end();
}

#if ENABLE_NETWORK
// Self-update (defined in Network.ino) - forward-declared so screen421()
// in Interface.ino and loop() can call them across the #if boundary
// (auto-prototypes are not generated for functions inside #if blocks).
void otaCheckLatest();
const char *otaLatestVerStr();
int otaVersionState();
bool otaHasUpdate();
void otaInstallLatest();
void network_service_window(uint16_t durationMs);
void screen422();   // "install from file" screen (Interface.ino, #if-guarded)
#endif

// ===================================================================================
// Pin Definitions
// ===================================================================================

// Button Pins
// Used for UI navigation and control
const int buttonBack = 33; // Back button
const int buttonUp = 32;   // Up button
const int buttonDown = 35; // Down button
const int buttonOK = 34;   // OK/Select button

// Sensor & Actuator Pins
const int end_stop = 26; // Z-axis endstop switch (limit switch)
const int mot_en = 13;   // Motor Enable pin
const int mot_step = 12; // Motor Step pin
const int mot_dir = 14;  // Motor Direction pin
const int LED = 21;      // UV LED control pin
const int FAN = 16;      // Cooling fan control pin
const int SDCS = 25;     // SD Card Chip Select pin

// ULN2003 Motor Driver Pins
// These pins drive the stepper motor coils via ULN2003
#define IN1 12
#define IN2 13
#define IN3 14
#define IN4 22

// initialize the stepper library
AccelStepper stepper(AccelStepper::HALF4WIRE, IN1, IN3, IN2, IN4);

// ===================================================================================
// Display Configuration
// ===================================================================================

// Display 1: Masking LCD (UV masking)
// This screen displays the layer image
Arduino_DataBus *bus = new Arduino_ESP32SPI(27 /* DC */, 5 /* CS */, 18 /* SCK */, 23 /* MOSI */, 19 /* MISO */, VSPI /* spi_num */);
Arduino_GFX *gfx1 = new Arduino_ST7789(bus, -1 /* RST */, 1 /* rotation */, true /* IPS */);

// Display 2: UI LCD (User Interface)
// This screen shows the menu and status to the user
Arduino_DataBus *bus2 = new Arduino_ESP32SPI(27 /* DC */, 4 /* CS */, 18 /* SCK */, 23 /* MOSI */, 19 /* MISO */, VSPI /* spi_num */);
Arduino_GFX *gfx2 = new Arduino_ST7735(bus2, -1 /* RST */, 3 /* rotation */, true /* IPS */,80 /* width */, 160 /* height */, 26 /* col offset 1 */, 1 /* row offset 1 */, 26 /* col offset 2 */, 1 /* row offset 2 */);

// ===================================================================================
// Global Variables
// ===================================================================================

// Timing Calculation Variables
// Used to track print time and button press duration
int startTime;
int Duration;
int startTime2;
int Duration2;

// State Variables
int screen = 1;             // Current screen ID
int counter = 0;            // General purpose counter (e.g., used for folder navigation)
long Position_before_pause; // Z-axis position stored when pausing

// Estimation Variables
// Used to calculate object height ang remaining print time
float total_height; // Total height of the object to print
long estimated_seconds;
byte estimated_hours;
byte estimated_minutes;
float motor_updown_time;       // Time taken for one up and down cycle
float motor_updown_time_total; // Total time spent on motor movements

// UI Navigation Variables
int setting_item;              // Current selected item in settings menu
bool setting_item_updown = 1;  // Direction indicator for settings (1=up, 0=down)
bool printing_item_updown = 1; //1=up,0=down.

// Printing Flags
bool homing_canceled = false; // Flag: Homing process canceled
bool print_paused = false;    // Flag: Print is currently paused
bool print_canceled = false;  // Flag: Print process canceled

// Motion Parameters
float steps_mm = 1463;     // Steps per millimeter for Z-axis
int homing_Feedrate = 300; // Feedrate for homing
float max_height = 68;     // Maximum build height (mm)

// System State
int current_layer = 0; // Current layer being printed
int current_state = 0; // Current printing state
                       // (0=Homing, 1=Curing, 2=Lifting, 3=Dropping, 4=Canceling)
                       // (5=Pausing, 6=Paused, 7=Resuming, 8=Finish)

// Print Parameters (Loaded from EEPROM) 
float Layer_Height ;        // Layer thickness (mm)
long Base_Exposure ;        // Exposure time for base layers (s)
long Regular_Exposure ;     // Exposure time for normal layers (s)
byte Base_Layer ;           // Number of base layers
byte Transition_Layer ;     // Number of transition layers
byte Slow_Lift_Distance ;   // Distance for slow lift (mm)
byte Fast_Lift_Distance ;   // Distance for fast lift (mm)
int Slow_Lift_Feedrate ;    // Speed for slow lift (mm/min)
int Fast_Lift_Feedrate ;    // Speed for fast lift (mm/min)
int Drop_Back_Feedrate ;    // Speed for retract (mm/min)

// Default Manual Exposure Time
int manual_exposure = 35;

// Exposure calculation helper for transition layers
float Transition_Exposure ; 

// SD card instance
SdFat SD;

// File System Variables
char foldersel_long[101]; // Buffer for long folder names
String foldersel;         // Selected folder name (display version)
int layer_counter;        // Total number of layers
File root;                // Root directory object
String DirAndFile;        // Full path helper
String FileName;          // Current file name

// PNG Decoding
File myfile;
PNG png; // PNG decoder instance

void savePrintSettings() {
  EEPROM.write(1, Layer_Height * 100);
  EEPROM.write(2, Base_Exposure);
  EEPROM.write(3, Regular_Exposure);
  EEPROM.write(4, Base_Layer);
  EEPROM.write(5, Transition_Layer);
  EEPROM.write(6, Slow_Lift_Distance);
  EEPROM.write(7, Fast_Lift_Distance);
  EEPROM.write(8, Slow_Lift_Feedrate);
  EEPROM.write(9, Fast_Lift_Feedrate);
  EEPROM.write(10, Drop_Back_Feedrate);
  EEPROM.commit();
}

bool printerBusy() {
  return screen == 1111 || screen == 1112 || screen == 11111 ||
         screen == 11112 || screen == 11113;
}

bool handleUiTimeout() {
  bool buttonPressed = digitalRead(buttonBack) == LOW ||
                       digitalRead(buttonUp) == LOW ||
                       digitalRead(buttonDown) == LOW ||
                       digitalRead(buttonOK) == LOW;
  if (buttonPressed) {
    lastUiActivityMs = millis();
    if (uiBlanked) {
      uiBlanked = false;
      ((Arduino_TFT *)gfx2)->displayOn();
      screen1();
      delay(200);
      return true;                  // consume wake press
    }
  }

  if (uiTimeoutSecs == 0 || uiBlanked || printerBusy()) return false;
  if (!(screen == 1 || screen == 2 || screen == 3 || screen == 4)) return false;
  if (millis() - lastUiActivityMs < (unsigned long)uiTimeoutSecs * 1000UL) return false;

  gfx2->fillScreen(BLACK);
  ((Arduino_TFT *)gfx2)->displayOff(); // may not cut backlight if it is hard-wired
  uiBlanked = true;
  return false;
}

// ===================================================================================
// Settings
// ===================================================================================
/**
 * @brief Write factory-default print settings to EEPROM and reload them into
 * the live globals. Single source of truth for the defaults, used both on a
 * blank/corrupt EEPROM at boot and by Settings -> "Back to Default".
 * Layer_Height is stored x100 (10 -> 0.10 mm).
 */
void resetSettingsToDefault() {
  EEPROM.write(1, 10);   // Layer_Height     -> 0.10 mm
  EEPROM.write(2, 35);   // Base_Exposure
  EEPROM.write(3, 14);   // Regular_Exposure
  EEPROM.write(4, 2);    // Base_Layer
  EEPROM.write(5, 5);    // Transition_Layer
  EEPROM.write(6, 1);    // Slow_Lift_Distance
  EEPROM.write(7, 2);    // Fast_Lift_Distance
  EEPROM.write(8, 40);   // Slow_Lift_Feedrate
  EEPROM.write(9, 50);   // Fast_Lift_Feedrate
  EEPROM.write(10, 50);  // Drop_Back_Feedrate
  EEPROM.commit();

  Layer_Height = EEPROM.read(1) / 100.00;
  Base_Exposure = EEPROM.read(2);
  Regular_Exposure = EEPROM.read(3);
  Base_Layer = EEPROM.read(4);
  Transition_Layer = EEPROM.read(5);
  Slow_Lift_Distance = EEPROM.read(6);
  Fast_Lift_Distance = EEPROM.read(7);
  Slow_Lift_Feedrate = EEPROM.read(8);
  Fast_Lift_Feedrate = EEPROM.read(9);
  Drop_Back_Feedrate = EEPROM.read(10);
}

// ===================================================================================
// Setup Function
// ===================================================================================
/**
 * @brief Setup Function
 * Initializes all hardware components, loads settings, and sets the initial state
 */
void setup() {

  #if ENABLE_SERIAL_DEBUG
  Serial.begin(115200);
  #endif

  // -----------------------------------------------------------------------------------
  // Pin Configuration
  // -----------------------------------------------------------------------------------
  pinMode(buttonBack, INPUT);
  pinMode(buttonUp, INPUT);
  pinMode(buttonDown, INPUT);
  pinMode(buttonOK, INPUT);
  pinMode(end_stop, INPUT);
  pinMode(LED, OUTPUT);
  pinMode(FAN, OUTPUT);
  digitalWrite(LED, LOW); 
  digitalWrite(FAN, LOW);

  // -----------------------------------------------------------------------------------
  // Stepper Motor Configuration / 步进电机配置
  // -----------------------------------------------------------------------------------
  // Set maximum speed in steps per second
  // Higher values mean faster movement but less torque  
  stepper.setMaxSpeed(1200.0);
  
  // Set acceleration in steps per second^2. Controls how fast the motor ramps up to speed  
  stepper.setAcceleration(2500.0);
  
  // Disable motor outputs initially to save power and reduce heat when idle  
  stepper.disableOutputs();

  // -----------------------------------------------------------------------------------
  // SD Card Initialization / SD 卡初始化
  // -----------------------------------------------------------------------------------
  // Initialize SD card using the dedicated Chip Select pin and a safe SPI frequency (16MHz) 
  SD.begin(SDCS, SD_SCK_MHZ(16));
    
  // -----------------------------------------------------------------------------------
  // Displays Initialization / 显示屏初始化
  // -----------------------------------------------------------------------------------
  // Display 1: Masking LCD (UV masking)  
  gfx1->begin();
  gfx1->fillScreen(BLACK);  
  
  // Display 2: UI LCD (User Interface)
  gfx2->begin();
  gfx2->fillScreen(BLACK);
  
  // Display Welcome Screen
  screen0();

  // -----------------------------------------------------------------------------------
  // Settings Loading
  // -----------------------------------------------------------------------------------
  // Initialize EEPROM with 24 bytes of space to read stored parameters.  
  EEPROM.begin(24);

  // Read stored values from specific addresses.
  // Layer Height is stored multiplied by 100 to save as integer, so divide by 100.00 to restore float  
  Layer_Height = EEPROM.read(1) / 100.00;
  Base_Exposure = EEPROM.read(2);
  Regular_Exposure = EEPROM.read(3);
  Base_Layer = EEPROM.read(4);
  Transition_Layer = EEPROM.read(5);
  Slow_Lift_Distance = EEPROM.read(6);
  Fast_Lift_Distance = EEPROM.read(7);
  Slow_Lift_Feedrate = EEPROM.read(8);
  Fast_Lift_Feedrate = EEPROM.read(9);
  Drop_Back_Feedrate = EEPROM.read(10);

  // First boot after a full flash leaves EEPROM uninitialized (every byte
  // reads 0xFF = 255), which produced Layer_Height 2.55 mm and absurd
  // exposures/print-time estimates. Seed the same factory defaults the
  // Settings -> "Back to Default" menu uses when values are out of range.
  if (EEPROM.read(1) == 255 || Layer_Height < 0.01 || Layer_Height > 0.2) {
    resetSettingsToDefault();
  }

  // NVS-backed system values: lifetime print time + web/device settings.
  loadDeviceConfig();
  lastUiActivityMs = millis();

  delay(1000);
  #if ENABLE_NETWORK
  network_setup(); // SLIBBINAS WiFi + upload server (Network.ino)
  #endif
  screen1(); // jumps to Main Menu
}

bool prepareSelectedPrintPreview() {
  uiFrame(ORANGE);
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  gfx2->setCursor(22, 34);
  gfx2->print("Processing files");
  gfx2->setCursor(34, 52);
  gfx2->print("Please wait...");
  delay(500);

  layer_counter = 0;
  File entry;
  do {
    layer_counter += 100;
    FileName = foldersel_long;
    FileName += "/";
    FileName += layer_counter;
    FileName += ".png";
    entry = SD.open(FileName);
  } while(entry);
  layer_counter -= 100;

  do {
    layer_counter++;
    FileName = foldersel_long;
    FileName += "/";
    FileName += layer_counter;
    FileName += ".png";
    entry = SD.open(FileName);
  } while(entry);
  layer_counter--;

  if (layer_counter <= 0 || layer_counter > MAX_LAYER_FILES) {
    screen112();
    return false;
  }

  screen111();
  return true;
}

// ===================================================================================
// Main Loop
// ===================================================================================
/**
 * @brief Main Loop
 * Handles button inputs and UI state transitions continuously.
 */
void loop() {  
  #if ENABLE_NETWORK
  network_loop(); // network uploads - only serviced while printer is idle
  #endif
  if (handleUiTimeout()) return;
  // -----------------------------------------------------------------------------------
  // Back Button Handling
  // Only triggers if the button is pressed (LOW)
  // -----------------------------------------------------------------------------------  
  if (digitalRead(buttonBack) == LOW) {
    switch (screen) {
      case 11:
      screen1();       
        break;
      case 113:                 // cancel delete -> back to model list
      screen11();
      counter --;
      folderDown(root);
        break;
      case 111:
      screen11();
      counter --;
      folderDown(root);
        break;
      case 112:
      screen11();
      counter --;
      folderDown(root);
        break;      
      case 12:
      screen1(); 
        break;
      case 21:
      screen1(); 
      screen2();
        break;
      case 22:
      screen1(); 
      screen2();
        break;
      case 23:
      screen1(); 
      screen2();
        break;
      case 211:
      screen21(); 
        break;
      case 212:
      screen21(); 
        break;
      case 213:
      gfx2->fillRoundRect(2, 20, 156, 56, 3, BLACK); 
      gfx2->setTextColor(WHITE);
      gfx2->setCursor(46, 43);
      gfx2->print("Canceled");
      delay(600);
      gfx2->setTextColor(BLACK);
      gfx2->setCursor(46, 43);
      gfx2->print("Canceled");
      delay(600);
      gfx2->setTextColor(WHITE);
      gfx2->setCursor(46, 43);
      gfx2->print("Canceled");
      delay(600);
      gfx2->setTextColor(BLACK);
      gfx2->setCursor(46, 43);
      gfx2->print("Canceled");
      delay(600);
      gfx2->setTextColor(WHITE);
      gfx2->setCursor(46, 43);
      gfx2->print("Canceled");
      delay(600);
      screen21(); 
        break;
      case 221:
      screen21();
      screen22();
        break;
      case 222:
      screen21();
      screen22();      
        break;        
      case 223:
      screen21();
      screen22();      
        break; 
      case 2211:
      screen221();
        break;
      case 2221:
      screen221();
      screen222();      
        break;        
      case 2231:
      screen221();
      screen223();      
        break; 
      case 231:
      screen21();
      screen23();
        break;
      case 2311:
      screen21();
      screen23();
        break;
      case 31:
      screen1(); 
      screen3();
        break; 
      case 41:
      case 42:
      case 43:
      screen1(); 
      screen4();
        break;
      case 411:
      screen41();
        break;
      case 421:
      screen41();
      screen42();
        break;
      #if ENABLE_NETWORK
      case 422:                 // install-from-file screen -> back to Update
      screen421();
        break;
      #endif
      case 431:
      screen41();
      screen43();
        break;
      case 311:
      if(setting_item_updown == 1){
        setting_item ++;
        screen31UP();
      }
      if(setting_item_updown == 0){
        setting_item --;
        screen31DOWN();
      }
      delay(300);
        break; 
      #if ENABLE_NETWORK
      case 312:                 // WiFi Info -> back to System menu
        screen41();
        break;
      case 3121:                // Reset confirm -> cancel, back to WiFi Info
        screenWifiInfo();
        break;
      #endif
      case 3111:
      Layer_Height = EEPROM.read(1) / 100.00;
      Base_Exposure = EEPROM.read(2);
      Regular_Exposure = EEPROM.read(3);
      Base_Layer = EEPROM.read(4);
      Transition_Layer = EEPROM.read(5);
      Slow_Lift_Distance = EEPROM.read(6);
      Fast_Lift_Distance = EEPROM.read(7);
      Slow_Lift_Feedrate = EEPROM.read(8);
      Fast_Lift_Feedrate = EEPROM.read(9);
      Drop_Back_Feedrate = EEPROM.read(10);
      if(setting_item_updown == 1){
        setting_item ++;
        screen31UP();
      }
      if(setting_item_updown == 0){
        setting_item --;
        screen31DOWN();
      } 
        break;
    }
    delay(200);
  }

  // -----------------------------------------------------------------------------------
  // Up Button Handling
  // -----------------------------------------------------------------------------------  
  if (digitalRead(buttonUp) == LOW) {
    switch (screen) {
      case 2:
      screen1();    
        break;
      case 111:                 // UP on preview screen -> estimate resin
      if (estimateResin())
        startFromResin = true;  // Start pressed -> print starts in OK handler
      else
        screen111();            // Back pressed -> redraw preview (Height/Time)
        break;
      case 3:
      screen2();
        break;
      case 4:
      screen3();
        break;
      case 42:
      screen41();
        break;
      #if ENABLE_NETWORK
      case 421:                 // UP on Update screen -> install from file
      screen422();
        break;
      #endif
      case 43:
      screen41(); 
      screen42();
        break;
      case 11:
      folderUp(root);
        break;
      case 22:
      screen21();
        break;
      case 23:
      screen22();
        break;
      case 222:
      screen221();
        break;
      case 223:
      screen222();
        break; 
      case 2211:
      manual_lift();
        break;
      case 2221:
      manual_lift();
        break;
      case 2231:
      manual_lift();
        break;      
      case 2311:
      screen2311increase();
        break;            
      case 31:
      screen31UP();
        break;
      case 311:
      screen3111increase();
        break;
      case 3111:
      screen3111increase();
        break;
    }
    delay(200);
  }  

  // -----------------------------------------------------------------------------------
  // Down Button Handling
  // -----------------------------------------------------------------------------------  
  if (digitalRead(buttonDown) == LOW) {
    switch (screen) {
      case 1:
      screen2();      
        break;
      case 2:
      screen3();
        break;
      case 3:
      screen4();
        break;
      case 41:
      screen42();
        break;
      case 42:
      screen43();
        break;
      case 11:
      folderDown(root);
        break;
      case 21:
      screen22();
        break;
      case 22:
      screen23();
        break;
      case 221:
      screen222();
        break;
      case 222:
      screen223();
        break;
      case 2211:
      manual_down();
        break;
      case 2221:
      manual_down();
        break;
      case 2231:
      manual_down();
        break;
      case 2311:
      screen2311decrease();
        break;                    
      case 31:
      screen31DOWN();
        break;
      case 311:
      screen3111decrease();
        break;
      case 3111:
      screen3111decrease();
        break;
    }
    delay(200);
  }  

  // -----------------------------------------------------------------------------------
  // OK Button Handling
  // (startFromResin/webStartPrint let non-OK flows start the existing print path)
  // -----------------------------------------------------------------------------------
  if (digitalRead(buttonOK) == LOW || startFromResin || webStartPrint) {
    switch (screen) {
      case 1:
      if (SD.begin(SDCS, SD_SCK_MHZ(16))){
        root = SD.open("/");
        screen11();
        counter = 0;
        folderDown(root);                        
      }
      else{
        screen12();
      }
        break;
      case 113:                 // delete confirmed
        deleteSelectedModel();
        break;
      case 11: {
      // Long-press OK (>= 1.5 s) on a model -> delete confirmation
      {
        unsigned long okHold = millis();
        while (digitalRead(buttonOK) == LOW && millis() - okHold < 1500) delay(10);
        if (millis() - okHold >= 1500 && strlen(foldersel_long) > 0) {
          screenDeleteConfirm();
          // Wait for button release - otherwise the still-held OK would be
          // read again on the next loop() pass and instantly confirm delete
          while (digitalRead(buttonOK) == LOW) delay(10);
          break;
        }
      }
      if (selIsArchive) {
        // OK on a .sl1/.zip -> convert it to a model, then refresh the list
        // (the archive is gone, the new model folder appears)
        importSelectedArchive();
        screen11();
        counter = 0;
        folderDown(root);
        break;
      }
      prepareSelectedPrintPreview();
      }
        break;
      case 111: {
        startFromResin = false;   // consume the resin-screen Start request
        webStartPrint = false;    // consume the web SD-manager Start request
        printStartMs = millis();  // print-hours accounting (incl. pauses)
        homing_canceled = false;
        print_paused = false;
        print_canceled = false;
        webResumePrint = false;
        resinUsedMl = 0.0;        // reset cured-resin counter for this print
        current_state = 0;
        current_layer = 0;
        Position_before_pause = 0;
        Transition_Exposure = Base_Exposure;
        screen1111();
        gfx2->fillRect(136, 52, 6, 16, 0x8410);
        gfx2->fillRect(146, 52, 6, 16, 0x8410);        
        screen1111_state();
        screen1111UP();
        delay(500);
        #if ENABLE_NETWORK
        network_service_window(500);
        #endif

        // -------------------------------------------------------------------------------
        // Homing Sequence
        // -------------------------------------------------------------------------------
        stepper.setCurrentPosition(0);
        stepper.setMaxSpeed(Drop_Back_Feedrate * steps_mm / 60);
        stepper.enableOutputs();
        long initial_homing = 0;
        long current_position;
        while(!digitalRead(end_stop) && !homing_canceled && !print_canceled){
          stepper.moveTo(initial_homing);  // Set the position to move to
          initial_homing--;  // Decrease by 1 for next move if needed
          stepper.run();  // Start moving the stepper          
          current_position = stepper.currentPosition();
          if (current_position < -106799){
            stepper.disableOutputs();
            homing_canceled = true;
            gfx2->fillRoundRect(5, 5, 150, 70, 7, BLACK);
            gfx2->fillRoundRect(7, 7, 146, 66, 5, RED);
            gfx2->fillRoundRect(9, 9, 142, 62, 3, BLACK);
            gfx2->fillRoundRect(16, 11, 5, 10, 1, RED); 
            gfx2->fillCircle(18, 25, 2, RED); 
            gfx2->setTextColor(WHITE);
            gfx2->setTextSize(1);
            gfx2->setCursor(27, 23);
            gfx2->println("Homing error,");
            gfx2->setCursor(13, 41);
            gfx2->println("print canceled."); 
            gfx2->fillRoundRect(82, 51, 67, 18, 2,  0x879F);
            gfx2->setCursor(100, 64);
            gfx2->println("OK :(");
            while(digitalRead(buttonOK) == HIGH);
            break;  
          }
          if (Duration >= 500 && screen == 1111 && digitalRead(buttonOK) == LOW) {
            screen11111();
            startTime = millis();
          }
          Duration = millis()-startTime;
          if (Duration >= 500 && screen == 11111 && digitalRead(buttonOK) == LOW){
            stepper.disableOutputs();
            homing_canceled = true;
            break;
          }
          if (Duration >= 500 && screen == 11111 && digitalRead(buttonBack) == LOW){
            screen1111();
            gfx2->fillRect(136, 52, 6, 16, 0x8410);
            gfx2->fillRect(146, 52, 6, 16, 0x8410);            
            screen1111_state();
            screen1111UP();
          }
        }
        delay(50);
          
        if (homing_canceled != true){
          stepper.disableOutputs();
          stepper.setCurrentPosition(0);
          digitalWrite(FAN, HIGH);
          if (screen != 11111){
            gfx2->fillRect(136, 52, 6, 16, YELLOW);
            gfx2->fillRect(146, 52, 6, 16, YELLOW);
          }
        }
          
        // -------------------------------------------------------------------------------
        // Printing Loop
        // -------------------------------------------------------------------------------       
        while(!homing_canceled && !print_canceled){            
          estimated_seconds = 0;
          estimated_hours = 0;
          estimated_minutes = 0;
          motor_updown_time_total = 0;
          if (current_layer < Base_Layer)
            estimated_seconds += (Base_Layer - current_layer) * Base_Exposure;                
          estimated_seconds += (layer_counter - current_layer) * Regular_Exposure;            
          motor_updown_time_total += (layer_counter - current_layer - 1) * motor_updown_time;            
          estimated_seconds += motor_updown_time_total;             
          estimated_hours = estimated_seconds / 3600;
          estimated_minutes = (estimated_seconds % 3600) / 60;
                        
          print_next_png();
          #if ENABLE_NETWORK
          network_service_window(160);
          #endif
            
          if (screen != 11111 && screen != 11112){                
            gfx2->fillRoundRect(2, 38, 116, 40, 3, BLACK);
            gfx2->setFont(&FreeSans8pt7b);
            gfx2->setTextColor(WHITE);
            gfx2->setTextSize(1);    
            gfx2->setCursor(6, 54);
            gfx2->print(current_layer);      
            gfx2->print(" / ");
            gfx2->print(layer_counter);
            gfx2->setCursor(6, 74);
            gfx2->print(estimated_hours);
            gfx2->print("h");
            gfx2->print(estimated_minutes);
            gfx2->print("m ");
            gfx2->setTextColor(0x879F);   // live cured-resin counter (matches screen1111)
            gfx2->print(resinUsedMl, 1);
            gfx2->print("ml");
            gfx2->setTextColor(WHITE);
          }
          
          if (current_state != 4 && current_state != 5){
            current_state = 1;
            screen1111_state();
          }
                  
          turn_on_LED();          
          gfx1->fillScreen(BLACK);
          #if ENABLE_NETWORK
          network_service_window(160);
          #endif
          
          if (current_state != 4 && current_state != 5){
            current_state = 2;
            screen1111_state();
          }
          lift_print();
          delay(50);
          #if ENABLE_NETWORK
          network_service_window(160);
          #endif
          
          if(current_layer == layer_counter)
            break;
            
          // -----------------------------------------------------------------------------
          // Pause Handling
          // -----------------------------------------------------------------------------          
          if(print_paused == true){
            Position_before_pause = stepper.currentPosition();
            stepper.setMaxSpeed(Fast_Lift_Feedrate * steps_mm / 60);
            stepper.enableOutputs();
            if (Position_before_pause + (20 * steps_mm) <= max_height * steps_mm)
              stepper.move(20 * steps_mm);
            else
              stepper.moveTo(max_height * steps_mm);  
            while (stepper.distanceToGo()!= 0) {
              stepper.run();
            }
            stepper.disableOutputs();
            delay(10); 

            current_state = 6;
            screen1111_state();
            gfx2->fillRect(136, 12, 16, 16, RED);
            gfx2->fillTriangle(136, 52, 136, 68, 152, 60, GREEN);            
            screen1111DOWN();
              
            while(print_paused == true){
              #if ENABLE_NETWORK
              network_loop();
              #endif
              Duration2 = millis()-startTime2;
              if (Duration2 >= 500 && digitalRead(buttonUp) == LOW && screen == 1112){
              screen1111UP();
              Duration2 = 0;
              startTime2 = millis();
              }
              if (Duration2 >= 500 && digitalRead(buttonDown) == LOW && screen == 1112){
              screen1111DOWN();
              Duration2 = 0;
              startTime2 = millis();
              }
              if (Duration2 >= 500 && digitalRead(buttonOK) == LOW && printing_item_updown == 1 && screen != 11111){
              screen11111();
              Duration2 = 0;
              startTime2 = millis();
              }
              if (Duration2 >= 500 && digitalRead(buttonOK) == LOW && printing_item_updown == 0 && screen != 11113){
              screen11113();
              Duration2 = 0;
              startTime2 = millis();
              }
              if (Duration2 >= 500 && digitalRead(buttonBack) == LOW && screen == 11111){
              screen1111();
              screen1111_state();
              screen1112();
              screen1111UP();
              Duration2 = 0;
              startTime2 = millis();
              }
              if (Duration2 >= 500 && digitalRead(buttonBack) == LOW && screen == 11113){
              screen1111();
              screen1111_state();
              screen1112();
              screen1111DOWN();
              Duration2 = 0;
              startTime2 = millis();
              }
              if (Duration2 >= 500 && digitalRead(buttonOK) == LOW && screen == 11111){
              screen1111();
              current_state = 4;
              screen1111_state();
              screen1111UP();
              print_canceled = true;
              print_paused = false;
              }  
              if ((Duration2 >= 500 && digitalRead(buttonOK) == LOW && screen == 11113) || webResumePrint){
              webResumePrint = false;
              screen1111();
              current_state = 7;
              screen1111_state();           
              gfx2->fillRect(136, 12, 16, 16, 0x8410);
              gfx2->fillRect(136, 52, 6, 16, 0x8410);
              gfx2->fillRect(146, 52, 6, 16, 0x8410);
              gfx2->drawRoundRect(128, 44, 32, 32, 3, 0x8410);
              stepper.setMaxSpeed(Fast_Lift_Feedrate * steps_mm / 60);
              stepper.enableOutputs();
              stepper.moveTo(Position_before_pause);  
              while (stepper.distanceToGo()!= 0) {
                stepper.run();
              }
              stepper.disableOutputs();
              delay(10);
              gfx2->fillRect(136, 12, 16, 16, RED);
              gfx2->fillRect(136, 52, 6, 16, YELLOW);
              gfx2->fillRect(146, 52, 6, 16, YELLOW); 
              gfx2->drawRoundRect(128, 44, 32, 32, 3, WHITE);
              print_paused = false;    
              }       
            }                     
          }
          
          if (!print_canceled){
            current_state = 3;
            screen1111_state();
            lower_print();
            #if ENABLE_NETWORK
            network_service_window(160);
            #endif
          }           
        } 
        if (!homing_canceled){
          if (!print_canceled){
            current_state = 8;
            screen1111_state();
            gfx2->fillRect(136, 12, 16, 16, 0x8410);
            gfx2->fillRect(136, 52, 6, 16, 0x8410);
            gfx2->fillRect(146, 52, 6, 16, 0x8410);
            if(printing_item_updown == 1)
              gfx2->drawRoundRect(128, 4, 32, 32, 3, 0x8410);
            if(printing_item_updown == 0)
              gfx2->drawRoundRect(128, 44, 32, 32, 3, 0x8410);            
          } 
          lift_finished_print();
        }
        digitalWrite(FAN, LOW);
        savePrintTime();   // single exit point: finish, cancel and homing-abort
        screen1();
      }
        break;
      
      case 12:
      if (SD.begin(SDCS, SD_SCK_MHZ(16))){
        root = SD.open("/");
        screen11();
        counter = 0;
        folderDown(root);                        
      }
      else{
        screen12();
      }
        break;
      case 2:
      screen21();
        break;
      case 21:
      screen211();
        break;
      case 211:
      screen212();
        break;
      case 212:
      screen213();
        break;
      case 213:
      screen214();
        break;
      case 22:
      screen221();
        break;
      case 221:
      screen2211();
        break;
      case 222:
      screen2221();
        break;
      case 223:
      screen2231();
        break; 
      case 23:
      screen231();
        break;
      case 231:
      screen2311();
        break; 
      case 2311:
      screen23111();
        break;        
      case 3:
      setting_item = 1;
      screen31UP();
        break;
      case 31:
        screen311();
        break;
      case 4:
        screen41();
        break;
      case 41:
        #if ENABLE_NETWORK
        screenWifiInfo();
        #else
        screen411();
        #endif
        break;
      case 42:
        screen421();
        break;
      case 43:
        screen431();
        break;
      #if ENABLE_NETWORK
      case 312:                 // WiFi Info -> open Reset WiFi confirmation
        screenWifiResetConfirm();
        break;
      case 3121:                // Confirmed -> erase credentials + reboot
        wifiDoReset();
        break;
      case 421:                 // Update screen -> install latest (self-update)
        if (otaHasUpdate()) otaInstallLatest();
        break;
      #endif
      case 311:
      if(setting_item_updown == 1){
        setting_item ++;
        screen31UP();
      }
      if(setting_item_updown == 0){
        setting_item --;
        screen31DOWN();
      }
        break; 
      case 3111:
      savePrintSettings();
      if(setting_item_updown == 1){
        setting_item ++;
        screen31UP();
      }
      if(setting_item_updown == 0){
        setting_item --;
        screen31DOWN();
      } 
        break;
    }
    delay(200);
  } 
}
