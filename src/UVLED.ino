#include <esp_timer.h>

// One-shot timer that cuts the UV LED at the exposure deadline no matter what
// the wait loop below is doing. Everything that loop calls blocks - a button
// press redraws the screen (~100-300 ms, since upstream 1.0.2), HTTP service
// sends a page - and a blocked loop used to stretch that layer's exposure by
// the blocking time. The timer makes the light's OFF edge exact; the loop's
// own digitalWrite(LOW) stays authoritative for cancel (the timer would let a
// canceled exposure burn to full time) and as the fallback.
static esp_timer_handle_t uvOffTimer = nullptr;
static void uvOffTimerCb(void *) { digitalWrite(LED, LOW); }

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
  // Countdown bookkeeping - the dashboard shows "Curing · Ns" from these.
  phaseStartMs = startTime;
  phaseTotalMs = ExposureMillis > 0 ? (unsigned long)ExposureMillis : 0;
  if (uvLedEnabled && ExposureMillis > 0) {
    if (!uvOffTimer) {
      esp_timer_create_args_t targs = {};
      targs.callback = uvOffTimerCb;
      targs.name = "uvoff";
      esp_timer_create(&targs, &uvOffTimer);
    }
    if (uvOffTimer) {
      esp_timer_stop(uvOffTimer);   // may not be armed; harmless
      esp_timer_start_once(uvOffTimer, (uint64_t)ExposureMillis * 1000ULL);
    }
  }

  while (Duration <= ExposureMillis && !print_canceled){
    Duration = millis()-startTime;
    Duration2 = millis()-startTime2;

    // Service HTTP while the LED burns. The exposure counts wall time, so
    // this costs it nothing - but without it the dashboard stalled until the
    // next between-phase window, up to a full base exposure (~35 s), which
    // read as "the page hung" (user finding). Same SD safety as the
    // between-phase windows: every SD-touching endpoint answers 409 while
    // the printer is busy.
    #if ENABLE_NETWORK
    network_service_http();   // HTTP only - no MQTT/Connect timeouts in here

    // 0-22 print saver: while dimmed (or on the waking press) the handlers
    // below must not see the buttons - a blind press may never pause/cancel.
    if (printSaverTick()) {
      Duration2 = 0;
      startTime2 = millis();
      continue;
    }
    #endif

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
  if (uvOffTimer) esp_timer_stop(uvOffTimer);  // don't let it fire into the next phase
  digitalWrite(LED, LOW);
  // A canceled exposure leaves its countdown mid-flight; zero it so the
  // "Canceling" state doesn't briefly show the dead exposure's number
  // before the final lift posts its own.
  if (print_canceled) phaseTotalMs = 0;
  if (uvLedEnabled) uvLedSessionMs += Duration;  // LED aging: count lit time only
}
