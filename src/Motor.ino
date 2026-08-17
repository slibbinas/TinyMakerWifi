/**
 * @brief Manual Lift
 * Moves the build plate up based on selected distance.
 */
void manual_lift(){
  stepper.setMaxSpeed(Drop_Back_Feedrate * steps_mm / 60);
  stepper.enableOutputs();
  switch (screen){
    case 2211:
    stepper.move(0.1 * steps_mm);
      break;
    case 2221:
    stepper.move(1 * steps_mm);
      break;
    case 2231:
    stepper.move(10 * steps_mm);
      break;    
  }
  byte cancel = 0;
  while (cancel == 0 && stepper.distanceToGo()!= 0){
    stepper.run(); 
    if (digitalRead(buttonBack) == LOW)
      cancel = 1;       
  }
  stepper.disableOutputs();  
  if (cancel == 1){
    switch (screen){
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
    }  
  }
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



/**
 * @brief Manual Down
 * Moves the build plate down based on selected distance.
 */
void manual_down(){
  stepper.setMaxSpeed(Drop_Back_Feedrate * steps_mm / 60);
  stepper.enableOutputs();
  switch (screen){
    case 2211:
    stepper.move(-0.1 * steps_mm);
      break;
    case 2221:
    stepper.move(-1 * steps_mm);
      break;
    case 2231:
    stepper.move(-10 * steps_mm);
      break;    
  }
  byte cancel = 0;
  while (cancel == 0 && stepper.distanceToGo()!= 0 && !digitalRead(end_stop)){
    stepper.run(); 
    if (digitalRead(buttonBack) == LOW)
      cancel = 1;       
  }
  stepper.disableOutputs();  
  if (cancel == 1){
    switch (screen){
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
    }  
  }
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



/**
 * @brief Home Machine
 * Moves the Z-axis down until the endstop is triggered to establish the zero position.
 *
 * REMOVED (audit finding): nothing called this - the real print-start homing
 * lives inline in TinyMaker.ino's print flow and services HTTP itself. The
 * dead copy even received tonight's HTTP-servicing fix, which is exactly the
 * trap dead code sets.
 */
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



/**
 * @brief Lift Print (Peel)
 * Performs the lift sequence to peel the cured layer.
 * Includes Slow Lift followed by Fast Lift.
 * Checks for user inputs.
 */
void lift_print(){
  int lift_print_steps_slow = (Slow_Lift_Distance * steps_mm);
  int lift_print_steps_fast = (Fast_Lift_Distance * steps_mm);
  int lift_print_steps_total = (lift_print_steps_slow + lift_print_steps_fast);
  stepper.setMaxSpeed(Slow_Lift_Feedrate * steps_mm / 60);
  stepper.enableOutputs();
  stepper.move(lift_print_steps_total);
  unsigned long liftNetTs = 0;
  // 0.17 1-38b: granular power-loss checkpoints during the lift. The plate is
  // RISING, so its actual height is always >= any earlier sample - recording it
  // verbatim keeps the safety invariant (live <= true height) AND gives sub-mm
  // resume instead of the pessimistic pre-lift low. Cadence: Balanced vs Precise.
  unsigned long liftCkptTs = 0;
  unsigned long resumeCkptMs = resumePrecise ? RESUME_CKPT_MS_PRECISE : RESUME_CKPT_MS_BALANCED;
  while (stepper.distanceToGo()!= 0){
    #if ENABLE_NETWORK
    // Lift takes seconds and used to serve no network at all - browser
    // requests (status, cached-page 304s, stop) stalled until it finished.
    // The layer is already cured; a brief motor pause is harmless.
    if (millis() - liftNetTs > 300) {
      liftNetTs = millis();
      network_loop();
    }
    #endif
    // Fires on the first iteration before the first run() (plate still at the
    // base, so live == base == the old pre-lift 'M'); later ones track the rise.
    if (resumeEnabled && !print_canceled && millis() - liftCkptTs >= resumeCkptMs) {
      liftCkptTs = millis();
      resumeCheckpointLive('M', resumeCycleBaseSteps, stepper.currentPosition());
    }
    if (stepper.distanceToGo()==lift_print_steps_fast){
      stepper.setMaxSpeed(Fast_Lift_Feedrate * steps_mm / 60);
    }
    stepper.run();
    Duration2 = millis()-startTime2;
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
      publishStopEstimate();   // kada sustos - nuo pirmos sekundes
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
  stepper.disableOutputs();
  delay(10);
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



/**
 * @brief Lower Print
 * Moves the platform down for the next layer (Retract).
 * Checks for button inputs during movement.
 */
void lower_print(){
  int lower_print_steps = ((0 - Slow_Lift_Distance - Fast_Lift_Distance + Layer_Height) * steps_mm);
  stepper.setMaxSpeed(Drop_Back_Feedrate * steps_mm / 60);
  stepper.enableOutputs();
  stepper.move(lower_print_steps);
  unsigned long lowerNetTs = 0;
  // 0.17 1-38b: granular checkpoints during the DROP. The plate DESCENDS, so a
  // verbatim reading could sit ABOVE the next real position -> recovery could
  // drive DOWN into the FEP. Bias `live` DOWN by the furthest one cadence window
  // can travel (x2 safety) so live <= true height always holds (the plate is
  // frozen whenever stepper.run() is not called, so descent per window is
  // bounded by v_max x cadence). Worst-case resume overshoot ~= dropMargin.
  unsigned long lowerCkptTs = 0;
  unsigned long resumeCkptMs = resumePrecise ? RESUME_CKPT_MS_PRECISE : RESUME_CKPT_MS_BALANCED;
  long dropMargin = (long)ceilf((Drop_Back_Feedrate * steps_mm / 60.0f) * (resumeCkptMs / 1000.0f) * 2.0f);
  if (dropMargin < 8) dropMargin = 8;
  while (stepper.distanceToGo()!= 0){
    #if ENABLE_NETWORK
    if (millis() - lowerNetTs > 300) {   // same as lift_print - see above
      lowerNetTs = millis();
      network_loop();
    }
    #endif
    // NB: NOT gated on print_canceled. The drop loop runs to completion even
    // after a mid-drop cancel, so `live` MUST keep refreshing through that
    // descent - otherwise a stale (higher) live + a power loss before the 'E'
    // write would let resume drive DOWN into the FEP (audit HIGH). A cancelled
    // print's record is cleared at the print's exit anyway (resumeClear).
    if (resumeEnabled && millis() - lowerCkptTs >= resumeCkptMs) {
      lowerCkptTs = millis();
      long live = stepper.currentPosition() - dropMargin;   // down-biased, safe while descending
      if (live < 0) live = 0;
      resumeCheckpointLive('M', resumeCycleBaseSteps, live);
    }
    stepper.run();
    Duration2 = millis()-startTime2;
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
      publishStopEstimate();   // kada sustos - nuo pirmos sekundes
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
  stepper.disableOutputs();
  delay(500);
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



/**
 * @brief Lift Finished Print
 * Lifts the platform to the maximum height after printing is complete.
 */
/* „Kada sustos" - vienas skaicius visam stabdymo laukimui (V 08-17/18).
   Nutraukimas priimamas penkiose vietose (ekrano mygtukas per ekspozicija ir per
   lifta, web komanda, spausdinimo ciklas), o iki siol nė viena ju nieko neskelbdavo:
   ekspozicijos skaitiklis buvo nunulinamas, o savo trukme paskelbdavo tik galutinis
   pakelimas - tad pirmoji laukimo dalis eidavo be jokio skaiciaus.
   Cia skaiciuojam ta pati, ka zmogus ir klausia: kiek liko iki plokstes virsuje.
   Likusi einamosios fazes dalis (ekspozicija ar judesys) plius pakelimo trukme is
   atstumo ir greicio. Galutinis pakelimas veliau persiskaiciuoja tiksliai, tad
   ivertis tik pagereja. */
void publishStopEstimate() {
  // Homing'as baigiasi BE galutinio pakelimo (TinyMaker: lift_finished_print
  // kvieciamas tik tada, kai homing'as nebuvo nutrauktas), tad zadeti sekundziu
  // ten negalima - V 08-18 stabdant pries spaudinio pradzia snackas rode ~23 s,
  // o printeris sustodavo tuoj pat.
  if (homing_canceled || current_state == 0) { phaseTotalMs = 0; return; }
  unsigned long remain = 0;
  if (phaseTotalMs > 0) {
    unsigned long el = millis() - phaseStartMs;
    if (el < phaseTotalMs) remain = phaseTotalMs - el;
  }
  long target = (long)(max_height * steps_mm);
  // Dry run parkuojasi ties pauzes aukstumu, ne virsuje - ta pati taisykle, kaip lifte.
  if (!uvLedEnabled) {
    long dryTarget = stepper.currentPosition() + (long)pauseLiftMm * steps_mm;
    if (dryTarget < target) target = dryTarget;
  }
  float stepsPerSec = Fast_Lift_Feedrate * steps_mm / 60.0f;
  long stepsToGo = target - stepper.currentPosition();
  unsigned long lift = (stepsToGo > 0 && stepsPerSec > 1.0f)
                     ? (unsigned long)((float)stepsToGo / stepsPerSec * 1000.0f) : 0;
  phaseStartMs = millis();
  phaseTotalMs = remain + lift;
}

void lift_finished_print(){
  /* Dry run pabaigoje (ir ji atsaukus) kelti iki pat virsaus nera ko: nieko
     neatspausdinta, nuimti nera ko, o kelias uztrunka desimtis sekundziu ir testuojant
     kartojasi be galo (V 08-13). Keliam iki „Pause lift height" - tiek, kad plokste
     butu patogu nuimti ar uzdeti. Tikram spaudiniui elgsena NEKINTA. */
  long lift_finished_print_steps = (max_height * steps_mm);
  if (!uvLedEnabled) {
    long dryTarget = stepper.currentPosition() + (long)pauseLiftMm * steps_mm;
    if (dryTarget < lift_finished_print_steps) lift_finished_print_steps = dryTarget;
  }
  stepper.setMaxSpeed(Fast_Lift_Feedrate * steps_mm / 60);
  stepper.enableOutputs();
  stepper.moveTo(lift_finished_print_steps);
  // This lift ends BOTH a finished and a canceled print and takes tens of
  // seconds - the dashboard's "Canceling"/"Finished" sat frozen through it
  // (user finding). Serve HTTP like homing does, but keep the first couple
  // of millimetres silent: that stretch is the last layer's peel, and a
  // service pause mid-peel is the one thing kept away from parts.
  long liftStartPos = stepper.currentPosition();
  // Feed the dashboard countdown ("Canceling · 25s"): distance and top speed
  // are known, so the duration is arithmetic. The accel ramp adds a moment -
  // the browser drops the number if the estimate overruns.
  {
    float stepsPerSec = Fast_Lift_Feedrate * steps_mm / 60.0f;
    long stepsToGo = lift_finished_print_steps - liftStartPos;
    if (stepsToGo > 0 && stepsPerSec > 1.0f) {
      phaseStartMs = millis();
      phaseTotalMs = (unsigned long)((float)stepsToGo / stepsPerSec * 1000.0f);
    }
  }
  unsigned long lastHttpSvc = millis();
  while (stepper.distanceToGo()!= 0){
    stepper.run();
    if (stepper.currentPosition() - liftStartPos > (long)(2 * steps_mm) &&
        millis() - lastHttpSvc >= 200) {
      lastHttpSvc = millis();
      #if ENABLE_NETWORK
      network_service_http();
      #endif
    }
  }
  stepper.disableOutputs();
  delay(10);
}
