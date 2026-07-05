/**
 * @file TinyMaker-Firmware-v1-0-2.ino
 * @author Tinymaker Team
 * @version 1.0.2
 * @date 2026-01-30
 * @brief Main firmware for Tinymaker MSLA 3D Printer.
 *
 * This file handles the entire print process, UI interaction, motor control, and UV exposure logic. 
 *
 * changes
 * - Added detailed comments (EN)
 * - Removed dead code
 * - Code organization and cleanup
 * - Corrected typo: "Maintenance" of Screen 2: Main Menu
 */

#include <SPI.h>
#include <EEPROM.h>              // For storing settings persistently
#include <AccelStepper.h>        // Stepper motor control library
#include <Arduino_GFX_Library.h> // Graphics library for driving displays
#include "FreeSans8pt7b.h"       // Custom font
#include <PNGdec.h>              // PNG decoder library for reading print layers
#include <SdFat.h>               // SD card file system library

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

// ===================================================================================
// Setup Function
// ===================================================================================
/**
 * @brief Setup Function
 * Initializes all hardware components, loads settings, and sets the initial state
 */
void setup() {  
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
  
  delay(1000);
  screen1(); // jumps to Main Menu
}

// ===================================================================================
// Main Loop
// ===================================================================================
/**
 * @brief Main Loop
 * Handles button inputs and UI state transitions continuously.
 */
void loop() {  
  // -----------------------------------------------------------------------------------
  // Back Button Handling
  // Only triggers if the button is pressed (LOW)
  // -----------------------------------------------------------------------------------  
  if (digitalRead(buttonBack) == LOW) {
    switch (screen) {
      case 11:
      screen1();       
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
      case 3:
      screen2();
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
  // -----------------------------------------------------------------------------------  
  if (digitalRead(buttonOK) == LOW) {
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
      case 11: {
      gfx2->fillScreen(BLACK);
      gfx2->fillRoundRect(0, 0, 160, 80, 5, ORANGE);
      gfx2->fillRoundRect(2, 2, 156, 76, 3, BLACK); 
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
      layer_counter --; 

      if (layer_counter <= 1080){
        screen111();
      }else{
        screen112();
      }
      }
        break;
      case 111: {
        homing_canceled = false;
        print_paused = false;
        print_canceled = false;
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

        // -------------------------------------------------------------------------------
        // Homing Sequence
        // -------------------------------------------------------------------------------
        stepper.setCurrentPosition(0);
        stepper.setMaxSpeed(Drop_Back_Feedrate * steps_mm / 60);
        stepper.enableOutputs();
        long initial_homing = 0;
        long current_position;
        while(!digitalRead(end_stop)){
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
            gfx2->print("h ");
            gfx2->print(estimated_minutes);
            gfx2->print("min");
          }
          
          if (current_state != 4 && current_state != 5){
            current_state = 1;
            screen1111_state();
          }
                  
          turn_on_LED();          
          gfx1->fillScreen(BLACK);
          
          if (current_state != 4 && current_state != 5){
            current_state = 2;
            screen1111_state();
          }
          lift_print();
          delay(50);
          
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
            while (stepper.distanceToGo()!= 0)
              stepper.run();
            stepper.disableOutputs();
            delay(10); 

            current_state = 6;
            screen1111_state();
            gfx2->fillRect(136, 12, 16, 16, RED);
            gfx2->fillTriangle(136, 52, 136, 68, 152, 60, GREEN);            
            screen1111DOWN();
              
            while(print_paused == true){
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
              if (Duration2 >= 500 && digitalRead(buttonOK) == LOW && screen == 11113){
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
              while (stepper.distanceToGo()!= 0)
                stepper.run();
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
      EEPROM.write(1, Layer_Height*100);
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
