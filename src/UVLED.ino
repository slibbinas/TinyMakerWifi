/**
 * @brief Turn On LED (Exposure)
 * Controls the UV LED exposure for the current layer.
 * Also handles button inputs (Pause, Cancel) during exposure.。
 */
void turn_on_LED(){
  long ExposureMillis;
  if(current_layer <= Base_Layer)
    ExposureMillis = Base_Exposure * 1000;
  if(current_layer > Base_Layer && current_layer <= Base_Layer + Transition_Layer){
    int a = Base_Exposure - Regular_Exposure;
    int b = Transition_Layer + 1;
    float c = (float)a / (float)b;
    Transition_Exposure -= c;
    ExposureMillis = Transition_Exposure * 1000; 
  }    
  if(current_layer > Base_Layer + Transition_Layer)
    ExposureMillis = Regular_Exposure * 1000;
  
  startTime = millis();
  Duration = 0;
  startTime2 = millis();
  digitalWrite(LED, uvLedEnabled ? HIGH : LOW);
  
  while (Duration <= ExposureMillis && !print_canceled){
    Duration = millis()-startTime;
    Duration2 = millis()-startTime2;
    
    // Check button inputs every 500ms
    if (Duration2 >= 500 && digitalRead(buttonUp) == LOW && screen == 1111 && print_canceled == false && print_paused == false){
      screen1111UP();
      Duration2 = 0;
      startTime2 = millis();
    }
    if (Duration2 >= 500 && digitalRead(buttonDown) == LOW && screen == 1111 && print_canceled == false && print_paused == false){
      screen1111DOWN();
      Duration2 = 0;
      startTime2 = millis();
    }
    if (Duration2 >= 500 && digitalRead(buttonOK) == LOW && printing_item_updown == 1 && screen != 11111 && print_canceled == false && print_paused == false){
      screen11111();
      Duration2 = 0;
      startTime2 = millis();
    }
    if (Duration2 >= 500 && digitalRead(buttonOK) == LOW && printing_item_updown == 0 && screen != 11112 && print_canceled == false && print_paused == false){
      screen11112();
      Duration2 = 0;
      startTime2 = millis();
    }
    if (Duration2 >= 500 && digitalRead(buttonBack) == LOW && screen == 11111){
      screen1111();
      screen1111_state();
      screen1111UP();
      Duration2 = 0;
      startTime2 = millis();
    }
    if (Duration2 >= 500 && digitalRead(buttonBack) == LOW && screen == 11112){
      screen1111();
      screen1111_state();
      screen1111DOWN();
      Duration2 = 0;
      startTime2 = millis();
    }
    if (Duration2 >= 500 && digitalRead(buttonOK) == LOW && screen == 11111){
      screen1111();
      current_state = 4;
      screen1111_state();
      gfx2->fillRect(136, 12, 16, 16, 0x8410);
      gfx2->fillRect(136, 52, 6, 16, 0x8410);
      gfx2->fillRect(146, 52, 6, 16, 0x8410);
      gfx2->drawRoundRect(128, 4, 32, 32, 3, 0x8410);
      print_canceled = true;
      Duration2 = 0;
      startTime2 = millis();
    }  
    if (Duration2 >= 500 && digitalRead(buttonOK) == LOW && screen == 11112){
      screen1111();
      current_state = 5;
      screen1111_state();
      screen1112();
      gfx2->fillRect(136, 12, 16, 16, 0x8410);
      gfx2->fillTriangle(136, 52, 136, 68, 152, 60, 0x8410);
      gfx2->drawRoundRect(128, 44, 32, 32, 3, 0x8410);
      print_paused = true;
      Duration2 = 0;
      startTime2 = millis();
    }   
  }
  digitalWrite(LED, LOW);
}
