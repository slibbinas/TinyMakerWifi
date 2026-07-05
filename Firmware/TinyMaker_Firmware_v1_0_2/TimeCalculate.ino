/**
 * @brief Get Motor Up/Down Time
 * Calculates the time required for a full lift/retract cycle based on settings.
 *
 * Note: This function uses a large lookup table (switch-case) based on an
 * encoded integer `i`. The integer `i` encodes distances and feedrates.
 */
void get_motor_updown_time(){
  int i = 0;
  i += Slow_Lift_Distance * 10000;
  i += Fast_Lift_Distance * 1000;
  i += Slow_Lift_Feedrate * 10;
  i += Fast_Lift_Feedrate;
  i += Drop_Back_Feedrate / 10;

  if(i <= 33555){
    switch(i){
      case 11222:
      motor_updown_time = 10.93;
        break;
      case 11223:
      motor_updown_time = 10.96;
        break;
      case 11224:
      motor_updown_time = 10.11;
        break;
      case 11225:
      motor_updown_time = 9.65;
        break;
      case 11232:
      motor_updown_time = 11.86;
        break;
      case 11233:
      motor_updown_time = 10.01;
        break;
      case 11234:
      motor_updown_time = 9.16;
        break;
      case 11235:
      motor_updown_time = 8.69;
        break;
      case 11242:
      motor_updown_time = 11.39;
        break;
      case 11243:
      motor_updown_time = 9.59;
        break;
      case 11244:
      motor_updown_time = 8.74;
        break;
      case 11245:
      motor_updown_time = 8.27;
        break;
      case 11252:
      motor_updown_time = 11.19;
        break;
      case 11253:
      motor_updown_time = 9.39;
        break;
      case 11254:
      motor_updown_time = 8.53;
        break;
      case 11255:
      motor_updown_time = 8.06;
        break;
      case 11322:
      motor_updown_time = 11.81;
        break;
      case 11323:
      motor_updown_time = 10.01;
        break;
      case 11324:
      motor_updown_time = 9.15;
        break;
      case 11325:
      motor_updown_time = 8.68;
        break;
      case 11332:
      motor_updown_time = 10.86;
        break;
      case 11333:
      motor_updown_time = 9.06;
        break;
      case 11334:
      motor_updown_time = 8.21;
        break;
      case 11335:
      motor_updown_time = 7.74;
        break;
      case 11342:
      motor_updown_time = 10.43;
        break;
      case 11343:
      motor_updown_time = 8.63;
        break;
      case 11344:
      motor_updown_time = 7.77;
        break;
      case 11345:
      motor_updown_time = 7.30;
        break;
      case 11352:
      motor_updown_time = 10.21;
        break;
      case 11353:
      motor_updown_time = 8.41;
        break;
      case 11354:
      motor_updown_time = 7.55;
        break;
      case 11355:
      motor_updown_time = 7.08;
        break;
      case 11422:
      motor_updown_time = 11.38;
        break;
      case 11423:
      motor_updown_time = 9.57;
        break;
      case 11424:
      motor_updown_time = 8.72;
        break;
      case 11425:
      motor_updown_time = 8.26;
        break;
      case 11432:
      motor_updown_time = 10.43;
        break;
      case 11433:
      motor_updown_time = 8.63;
        break;
      case 11434:
      motor_updown_time = 7.78;
        break;
      case 11435:
      motor_updown_time = 7.31;
        break;
      case 11442:
      motor_updown_time = 9.98;
        break;
      case 11443:
      motor_updown_time = 8.18;
        break;
      case 11444:
      motor_updown_time = 7.33;
        break;
      case 11445:
      motor_updown_time = 6.86;
        break;
      case 11452:
      motor_updown_time = 9.75;
        break;
      case 11453:
      motor_updown_time = 7.95;
        break;
      case 11454:
      motor_updown_time = 7.09;
        break;
      case 11455:
      motor_updown_time = 6.62;
        break;
      case 11522:
      motor_updown_time = 11.14;
        break;
      case 11523:
      motor_updown_time = 9.34;
        break;
      case 11524:
      motor_updown_time = 8.44;
        break;
      case 11525:
      motor_updown_time = 7.97;
        break;
      case 11532:
      motor_updown_time = 10.14;
        break;
      case 11533:
      motor_updown_time = 8.34;
        break;
      case 11534:
      motor_updown_time = 7.49;
        break;
      case 11535:
      motor_updown_time = 7.02;
        break;
      case 11542:
      motor_updown_time = 9.69;
        break;
      case 11543:
      motor_updown_time = 7.89;
        break;
      case 11544:
      motor_updown_time = 7.04;
        break;
      case 11545:
      motor_updown_time = 6.57;
        break;
      case 11552:
      motor_updown_time = 9.44;
        break;
      case 11553:
      motor_updown_time = 7.64;
        break;
      case 11554:
      motor_updown_time = 6.79;
        break;
      case 11555:
      motor_updown_time = 6.32;
        break;
      case 12222:
      motor_updown_time = 18.75;
        break;
      case 12223:
      motor_updown_time = 15.95;
        break;
      case 12224:
      motor_updown_time = 14.59;
        break;
      case 12225:
      motor_updown_time = 13.82;
        break;
      case 12232:
      motor_updown_time = 16.82;
        break;
      case 12233:
      motor_updown_time = 14.02;
        break;
      case 12234:
      motor_updown_time = 12.66;
        break;
      case 12235:
      motor_updown_time = 11.89;
        break;
      case 12242:
      motor_updown_time = 15.90;
        break;
      case 12243:
      motor_updown_time = 13.10;
        break;
      case 12244:
      motor_updown_time = 11.74;
        break;
      case 12245:
      motor_updown_time = 10.97;
        break;
      case 12252:
      motor_updown_time = 15.39;
        break;
      case 12253:
      motor_updown_time = 12.59;
        break;
      case 12254:
      motor_updown_time = 11.23;
        break;
      case 12255:
      motor_updown_time = 10.46;
        break;
      case 12322:
      motor_updown_time = 17.80;
        break;
      case 12323:
      motor_updown_time = 15.00;
        break;
      case 12324:
      motor_updown_time = 13.64;
        break;
      case 12325:
      motor_updown_time = 12.87;
        break;
      case 12332:
      motor_updown_time = 15.85;
        break;
      case 12333:
      motor_updown_time = 13.05;
        break;
      case 12334:
      motor_updown_time = 11.70;
        break;
      case 12335:
      motor_updown_time = 10.93;
        break;
      case 12342:
      motor_updown_time = 14.92;
        break;
      case 12343:
      motor_updown_time = 12.11;
        break;
      case 12344:
      motor_updown_time = 10.76;
        break;
      case 12345:
      motor_updown_time = 9.99;
        break;
      case 12352:
      motor_updown_time = 14.39;
        break;
      case 12353:
      motor_updown_time = 11.59;
        break;
      case 12354:
      motor_updown_time = 10.23;
        break;
      case 12355:
      motor_updown_time = 9.47;
        break;
      case 12422:
      motor_updown_time = 17.35;
        break;
      case 12423:
      motor_updown_time = 14.55;
        break;
      case 12424:
      motor_updown_time = 13.19;
        break;
      case 12425:
      motor_updown_time = 12.43;
        break;
      case 12432:
      motor_updown_time = 15.41;
        break;
      case 12433:
      motor_updown_time = 12.60;
        break;
      case 12434:
      motor_updown_time = 11.25;
        break;
      case 12435:
      motor_updown_time = 10.48;
        break;
      case 12442:
      motor_updown_time = 14.46;
        break;
      case 12443:
      motor_updown_time = 11.65;
        break;
      case 12444:
      motor_updown_time = 10.30;
        break;
      case 12445:
      motor_updown_time = 9.53;
        break;
      case 12452:
      motor_updown_time = 13.92;
        break;
      case 12453:
      motor_updown_time = 11.11;
        break;
      case 12454:
      motor_updown_time = 9.76;
        break;
      case 12455:
      motor_updown_time = 8.99;
        break;
      case 12522:
      motor_updown_time = 17.10;
        break;
      case 12523:
      motor_updown_time = 14.30;
        break;
      case 12524:
      motor_updown_time = 12.95;
        break;
      case 12525:
      motor_updown_time = 12.18;
        break;
      case 12532:
      motor_updown_time = 15.16;
        break;
      case 12533:
      motor_updown_time = 12.35;
        break;
      case 12534:
      motor_updown_time = 11.00;
        break;
      case 12535:
      motor_updown_time = 10.23;
        break;
      case 12542:
      motor_updown_time = 14.21;
        break;
      case 12543:
      motor_updown_time = 11.41;
        break;
      case 12544:
      motor_updown_time = 10.05;
        break;
      case 12545:
      motor_updown_time = 9.28;
        break;
      case 12552:
      motor_updown_time = 13.66;
        break;
      case 12553:
      motor_updown_time = 10.85;
        break;
      case 12554:
      motor_updown_time = 9.50;
        break;
      case 12555:
      motor_updown_time = 8.73;
        break;
      case 13222:
      motor_updown_time = 24.76;
        break;
      case 13223:
      motor_updown_time = 20.96;
        break;
      case 13224:
      motor_updown_time = 19.10;
        break;
      case 13225:
      motor_updown_time = 18.03;
        break;
      case 13232:
      motor_updown_time = 21.83;
        break;
      case 13233:
      motor_updown_time = 18.03;
        break;
      case 13234:
      motor_updown_time = 16.17;
        break;
      case 13235:
      motor_updown_time = 15.10;
        break;
      case 13242:
      motor_updown_time = 20.41;
        break;
      case 13243:
      motor_updown_time = 16.61;
        break;
      case 13244:
      motor_updown_time = 14.75;
        break;
      case 13245:
      motor_updown_time = 13.69;
        break;
      case 13252:
      motor_updown_time = 19.60;
        break;
      case 13253:
      motor_updown_time = 15.80;
        break;
      case 13254:
      motor_updown_time = 13.94;
        break;
      case 13255:
      motor_updown_time = 12.87;
        break;
      case 13322:
      motor_updown_time = 23.81;
        break;
      case 13323:
      motor_updown_time = 20.01;
        break;
      case 13324:
      motor_updown_time = 18.15;
        break;
      case 13325:
      motor_updown_time = 17.08;
        break;
      case 13332:
      motor_updown_time = 20.87;
        break;
      case 13333:
      motor_updown_time = 17.06;
        break;
      case 13334:
      motor_updown_time = 15.20;
        break;
      case 13335:
      motor_updown_time = 14.14;
        break;
      case 13342:
      motor_updown_time = 19.43;
        break;
      case 13343:
      motor_updown_time = 15.63;
        break;
      case 13344:
      motor_updown_time = 13.77;
        break;
      case 13345:
      motor_updown_time = 12.70;
        break;
      case 13352:
      motor_updown_time = 18.60;
        break;
      case 13353:
      motor_updown_time = 14.80;
        break;
      case 13354:
      motor_updown_time = 12.94;
        break;
      case 13355:
      motor_updown_time = 11.88;
        break;
      case 13422:
      motor_updown_time = 23.36;
        break;
      case 13423:
      motor_updown_time = 19.56;
        break;
      case 13424:
      motor_updown_time = 17.70;
        break;
      case 13425:
      motor_updown_time = 16.64;
        break;
      case 13432:
      motor_updown_time = 20.42;
        break;
      case 13433:
      motor_updown_time = 16.61;
        break;
      case 13434:
      motor_updown_time = 14.75;
        break;
      case 13435:
      motor_updown_time = 13.69;
        break;
      case 13442:
      motor_updown_time = 18.97;
        break;
      case 13443:
      motor_updown_time = 15.17;
        break;
      case 13444:
      motor_updown_time = 13.31;
        break;
      case 13445:
      motor_updown_time = 12.24;
        break;
      case 13452:
      motor_updown_time = 18.13;
        break;
      case 13453:
      motor_updown_time = 14.32;
        break;
      case 13454:
      motor_updown_time = 12.47;
        break;
      case 13455:
      motor_updown_time = 11.40;
        break;
      case 13522:
      motor_updown_time = 23.11;
        break;
      case 13523:
      motor_updown_time = 19.31;
        break;
      case 13524:
      motor_updown_time = 17.45;
        break;
      case 13525:
      motor_updown_time = 16.39;
        break;
      case 13532:
      motor_updown_time = 20.17;
        break;
      case 13533:
      motor_updown_time = 16.36;
        break;
      case 13534:
      motor_updown_time = 14.50;
        break;
      case 13535:
      motor_updown_time = 13.44;
        break;
      case 13542:
      motor_updown_time = 18.72;
        break;
      case 13543:
      motor_updown_time = 14.92;
        break;
      case 13544:
      motor_updown_time = 13.06;
        break;
      case 13545:
      motor_updown_time = 11.99;
        break;
      case 13552:
      motor_updown_time = 17.87;
        break;
      case 13553:
      motor_updown_time = 14.06;
        break;
      case 13554:
      motor_updown_time = 12.20;
        break;
      case 13555:
      motor_updown_time = 11.14;
        break;
      case 21222:
      motor_updown_time = 18.76;
        break;
      case 21223:
      motor_updown_time = 15.96;
        break;
      case 21224:
      motor_updown_time = 14.60;
        break;
      case 21225:
      motor_updown_time = 13.83;
        break;
      case 21232:
      motor_updown_time = 17.83;
        break;
      case 21233:
      motor_updown_time = 15.02;
        break;
      case 21234:
      motor_updown_time = 13.67;
        break;
      case 21235:
      motor_updown_time = 12.90;
        break;
      case 21242:
      motor_updown_time = 17.42;
        break;
      case 21243:
      motor_updown_time = 14.61;
        break;
      case 21244:
      motor_updown_time = 13.25;
        break;
      case 21245:
      motor_updown_time = 12.48;
        break;
      case 21252:
      motor_updown_time = 17.20;
        break;
      case 21253:
      motor_updown_time = 14.39;
        break;
      case 21254:
      motor_updown_time = 13.04;
        break;
      case 21255:
      motor_updown_time = 12.27;
        break;
      case 21322:
      motor_updown_time = 16.81;
        break;
      case 21323:
      motor_updown_time = 14.01;
        break;
      case 21324:
      motor_updown_time = 12.65;
        break;
      case 21325:
      motor_updown_time = 11.89;
        break;
      case 21332:
      motor_updown_time = 15.86;
        break;
      case 21333:
      motor_updown_time = 13.06;
        break;
      case 21334:
      motor_updown_time = 11.70;
        break;
      case 21335:
      motor_updown_time = 10.94;
        break;
      case 21342:
      motor_updown_time = 15.43;
        break;
      case 21343:
      motor_updown_time = 12.62;
        break;
      case 21344:
      motor_updown_time = 11.27;
        break;
      case 21345:
      motor_updown_time = 10.50;
        break;
      case 21352:
      motor_updown_time = 15.20;
        break;
      case 21353:
      motor_updown_time = 12.40;
        break;
      case 21354:
      motor_updown_time = 11.04;
        break;
      case 21355:
      motor_updown_time = 10.27;
        break;
      case 21422:
      motor_updown_time = 15.87;
        break;
      case 21423:
      motor_updown_time = 13.06;
        break;
      case 21424:
      motor_updown_time = 11.71;
        break;
      case 21425:
      motor_updown_time = 10.94;
        break;
      case 21432:
      motor_updown_time = 14.92;
        break;
      case 21433:
      motor_updown_time = 12.11;
        break;
      case 21434:
      motor_updown_time = 10.76;
        break;
      case 21435:
      motor_updown_time = 9.99;
        break;
      case 21442:
      motor_updown_time = 14.47;
        break;
      case 21443:
      motor_updown_time = 11.66;
        break;
      case 21444:
      motor_updown_time = 10.31;
        break;
      case 21445:
      motor_updown_time = 9.54;
        break;
      case 21452:
      motor_updown_time = 14.22;
        break;
      case 21453:
      motor_updown_time = 11.42;
        break;
      case 21454:
      motor_updown_time = 10.06;
        break;
      case 21455:
      motor_updown_time = 9.30;
        break;
      case 21522:
      motor_updown_time = 15.31;
        break;
      case 21523:
      motor_updown_time = 12.51;
        break;
      case 21524:
      motor_updown_time = 11.15;
        break;
      case 21525:
      motor_updown_time = 10.39;
        break;
      case 21532:
      motor_updown_time = 14.37;
        break;
      case 21533:
      motor_updown_time = 11.56;
        break;
      case 21534:
      motor_updown_time = 10.21;
        break;
      case 21535:
      motor_updown_time = 9.44;
        break;
      case 21542:
      motor_updown_time = 13.92;
        break;
      case 21543:
      motor_updown_time = 11.11;
        break;
      case 21544:
      motor_updown_time = 9.76;
        break;
      case 21545:
      motor_updown_time = 8.99;
        break;
      case 21552:
      motor_updown_time = 13.67;
        break;
      case 21553:
      motor_updown_time = 10.86;
        break;
      case 21554:
      motor_updown_time = 9.51;
        break;
      case 21555:
      motor_updown_time = 8.74;
        break;
      case 22222:
      motor_updown_time = 24.77;
        break;
      case 22223:
      motor_updown_time = 20.97;
        break;
      case 22224:
      motor_updown_time = 19.11;
        break;
      case 22225:
      motor_updown_time = 18.04;
        break;
      case 22232:
      motor_updown_time = 22.84;
        break;
      case 22233:
      motor_updown_time = 19.04;
        break;
      case 22234:
      motor_updown_time = 17.17;
        break;
      case 22235:
      motor_updown_time = 16.11;
        break;
      case 22242:
      motor_updown_time = 21.92;
        break;
      case 22243:
      motor_updown_time = 18.12;
        break;
      case 22244:
      motor_updown_time = 16.26;
        break;
      case 22245:
      motor_updown_time = 15.19;
        break;
      case 22252:
      motor_updown_time = 21.41;
        break;
      case 22253:
      motor_updown_time = 17.60;
        break;
      case 22254:
      motor_updown_time = 15.74;
        break;
      case 22255:
      motor_updown_time = 14.68;
        break;
      case 22322:
      motor_updown_time = 22.82;
        break;
      case 22323:
      motor_updown_time = 19.02;
        break;
      case 22324:
      motor_updown_time = 17.16;
        break;
      case 22325:
      motor_updown_time = 16.09;
        break;
      case 22332:
      motor_updown_time = 20.88;
        break;
      case 22333:
      motor_updown_time = 17.08;
        break;
      case 22334:
      motor_updown_time = 15.21;
        break;
      case 22335:
      motor_updown_time = 14.15;
        break;
      case 22342:
      motor_updown_time = 19.94;
        break;
      case 22343:
      motor_updown_time = 16.14;
        break;
      case 22344:
      motor_updown_time = 14.28;
        break;
      case 22345:
      motor_updown_time = 13.21;
        break;
      case 22352:
      motor_updown_time = 19.41;
        break;
      case 22353:
      motor_updown_time = 15.61;
        break;
      case 22354:
      motor_updown_time = 13.75;
        break;
      case 22355:
      motor_updown_time = 12.69;
        break;
      case 22422:
      motor_updown_time = 21.88;
        break;
      case 22423:
      motor_updown_time = 18.07;
        break;
      case 22424:
      motor_updown_time = 16.21;
        break;
      case 22425:
      motor_updown_time = 15.15;
        break;
      case 22432:
      motor_updown_time = 19.93;
        break;
      case 22433:
      motor_updown_time = 16.13;
        break;
      case 22434:
      motor_updown_time = 14.27;
        break;
      case 22435:
      motor_updown_time = 13.22;
        break;
      case 22442:
      motor_updown_time = 18.98;
        break;
      case 22443:
      motor_updown_time = 15.18;
        break;
      case 22444:
      motor_updown_time = 13.32;
        break;
      case 22445:
      motor_updown_time = 12.25;
        break;
      case 22452:
      motor_updown_time = 18.44;
        break;
      case 22453:
      motor_updown_time = 14.64;
        break;
      case 22454:
      motor_updown_time = 12.78;
        break;
      case 22455:
      motor_updown_time = 11.71;
        break;
      case 22522:
      motor_updown_time = 21.33;
        break;
      case 22523:
      motor_updown_time = 17.52;
        break;
      case 22524:
      motor_updown_time = 15.66;
        break;
      case 22525:
      motor_updown_time = 14.60;
        break;
      case 22532:
      motor_updown_time = 19.38;
        break;
      case 22533:
      motor_updown_time = 15.58;
        break;
      case 22534:
      motor_updown_time = 13.72;
        break;
      case 22535:
      motor_updown_time = 12.65;
        break;
      case 22542:
      motor_updown_time = 18.43;
        break;
      case 22543:
      motor_updown_time = 14.63;
        break;
      case 22544:
      motor_updown_time = 12.77;
        break;
      case 22545:
      motor_updown_time = 11.70;
        break;
      case 22552:
      motor_updown_time = 17.88;
        break;
      case 22553:
      motor_updown_time = 14.07;
        break;
      case 22554:
      motor_updown_time = 12.22;
        break;
      case 22555:
      motor_updown_time = 11.15;
        break;
      case 23222:
      motor_updown_time = 30.78;
        break;
      case 23223:
      motor_updown_time = 25.98;
        break;
      case 23224:
      motor_updown_time = 23.62;
        break;
      case 23225:
      motor_updown_time = 22.25;
        break;
      case 23232:
      motor_updown_time = 27.85;
        break;
      case 23233:
      motor_updown_time = 23.05;
        break;
      case 23234:
      motor_updown_time = 20.68;
        break;
      case 23235:
      motor_updown_time = 19.32;
        break;
      case 23242:
      motor_updown_time = 26.43;
        break;
      case 23243:
      motor_updown_time = 21.63;
        break;
      case 23244:
      motor_updown_time = 19.27;
        break;
      case 23245:
      motor_updown_time = 17.91;
        break;
      case 23252:
      motor_updown_time = 25.62;
        break;
      case 23253:
      motor_updown_time = 20.82;
        break;
      case 23254:
      motor_updown_time = 18.45;
        break;
      case 23255:
      motor_updown_time = 17.09;
        break;
      case 23322:
      motor_updown_time = 28.83;
        break;
      case 23323:
      motor_updown_time = 24.03;
        break;
      case 23324:
      motor_updown_time = 21.67;
        break;
      case 23325:
      motor_updown_time = 20.31;
        break;
      case 23332:
      motor_updown_time = 25.89;
        break;
      case 23333:
      motor_updown_time = 21.08;
        break;
      case 23334:
      motor_updown_time = 18.72;
        break;
      case 23335:
      motor_updown_time = 17.36;
        break;
      case 23342:
      motor_updown_time = 24.45;
        break;
      case 23343:
      motor_updown_time = 19.65;
        break;
      case 23344:
      motor_updown_time = 17.29;
        break;
      case 23345:
      motor_updown_time = 15.92;
        break;
      case 23352:
      motor_updown_time = 23.62;
        break;
      case 23353:
      motor_updown_time = 18.82;
        break;
      case 23354:
      motor_updown_time = 16.46;
        break;
      case 23355:
      motor_updown_time = 15.10;
        break;
      case 23422:
      motor_updown_time = 27.89;
        break;
      case 23423:
      motor_updown_time = 23.08;
        break;
      case 23424:
      motor_updown_time = 20.72;
        break;
      case 23425:
      motor_updown_time = 19.36;
        break;
      case 23432:
      motor_updown_time = 24.94;
        break;
      case 23433:
      motor_updown_time = 20.14;
        break;
      case 23434:
      motor_updown_time = 17.77;
        break;
      case 23435:
      motor_updown_time = 16.41;
        break;
      case 23442:
      motor_updown_time = 23.50;
        break;
      case 23443:
      motor_updown_time = 18.69;
        break;
      case 23444:
      motor_updown_time = 16.33;
        break;
      case 23445:
      motor_updown_time = 14.96;
        break;
      case 23452:
      motor_updown_time = 22.65;
        break;
      case 23453:
      motor_updown_time = 17.85;
        break;
      case 23454:
      motor_updown_time = 15.49;
        break;
      case 23455:
      motor_updown_time = 14.12;
        break;
      case 23522:
      motor_updown_time = 27.34;
        break;
      case 23523:
      motor_updown_time = 22.53;
        break;
      case 23524:
      motor_updown_time = 20.17;
        break;
      case 23525:
      motor_updown_time = 18.81;
        break;
      case 23532:
      motor_updown_time = 24.39;
        break;
      case 23533:
      motor_updown_time = 19.59;
        break;
      case 23534:
      motor_updown_time = 17.23;
        break;
      case 23535:
      motor_updown_time = 15.86;
        break;
      case 23542:
      motor_updown_time = 22.94;
        break;
      case 23543:
      motor_updown_time = 18.14;
        break;
      case 23544:
      motor_updown_time = 15.78;
        break;
      case 23545:
      motor_updown_time = 14.41;
        break;
      case 23552:
      motor_updown_time = 22.09;
        break;
      case 23553:
      motor_updown_time = 17.29;
        break;
      case 23554:
      motor_updown_time = 14.93;
        break;
      case 23555:
      motor_updown_time = 13.56;
        break;
      case 31222:
      motor_updown_time = 24.78;
        break;
      case 31223:
      motor_updown_time = 20.98;
        break;
      case 31224:
      motor_updown_time = 19.12;
        break;
      case 31225:
      motor_updown_time = 18.06;
        break;
      case 31232:
      motor_updown_time = 23.85;
        break;
      case 31233:
      motor_updown_time = 20.05;
        break;
      case 31234:
      motor_updown_time = 18.19;
        break;
      case 31235:
      motor_updown_time = 17.12;
        break;
      case 31242:
      motor_updown_time = 23.43;
        break;
      case 31243:
      motor_updown_time = 19.63;
        break;
      case 31244:
      motor_updown_time = 17.77;
        break;
      case 31245:
      motor_updown_time = 16.71;
        break;
      case 31252:
      motor_updown_time = 23.22;
        break;
      case 31253:
      motor_updown_time = 19.42;
        break;
      case 31254:
      motor_updown_time = 17.56;
        break;
      case 31255:
      motor_updown_time = 16.49;
        break;
      case 31322:
      motor_updown_time = 21.84;
        break;
      case 31323:
      motor_updown_time = 18.04;
        break;
      case 31324:
      motor_updown_time = 16.18;
        break;
      case 31325:
      motor_updown_time = 15.11;
        break;
      case 31332:
      motor_updown_time = 20.89;
        break;
      case 31333:
      motor_updown_time = 17.09;
        break;
      case 31334:
      motor_updown_time = 15.23;
        break;
      case 31335:
      motor_updown_time = 14.16;
        break;
      case 31342:
      motor_updown_time = 20.45;
        break;
      case 31343:
      motor_updown_time = 16.67;
        break;
      case 31344:
      motor_updown_time = 14.79;
        break;
      case 31345:
      motor_updown_time = 13.73;
        break;
      case 31352:
      motor_updown_time = 20.23;
        break;
      case 31353:
      motor_updown_time = 16.43;
        break;
      case 31354:
      motor_updown_time = 14.57;
        break;
      case 31355:
      motor_updown_time = 13.50;
        break;
      case 31422:
      motor_updown_time = 20.41;
        break;
      case 31423:
      motor_updown_time = 16.59;
        break;
      case 31424:
      motor_updown_time = 14.73;
        break;
      case 31425:
      motor_updown_time = 13.66;
        break;
      case 31432:
      motor_updown_time = 19.44;
        break;
      case 31433:
      motor_updown_time = 15.64;
        break;
      case 31434:
      motor_updown_time = 13.78;
        break;
      case 31435:
      motor_updown_time = 12.72;
        break;
      case 31442:
      motor_updown_time = 19.00;
        break;
      case 31443:
      motor_updown_time = 15.19;
        break;
      case 31444:
      motor_updown_time = 13.34;
        break;
      case 31445:
      motor_updown_time = 12.27;
        break;
      case 31452:
      motor_updown_time = 18.75;
        break;
      case 31453:
      motor_updown_time = 14.95;
        break;
      case 31454:
      motor_updown_time = 13.09;
        break;
      case 31455:
      motor_updown_time = 12.02;
        break;
      case 31522:
      motor_updown_time = 19.54;
        break;
      case 31523:
      motor_updown_time = 15.74;
        break;
      case 31524:
      motor_updown_time = 13.88;
        break;
      case 31525:
      motor_updown_time = 12.82;
        break;
      case 31532:
      motor_updown_time = 18.59;
        break;
      case 31533:
      motor_updown_time = 14.79;
        break;
      case 31534:
      motor_updown_time = 12.93;
        break;
      case 31535:
      motor_updown_time = 11.87;
        break;
      case 31542:
      motor_updown_time = 18.15;
        break;
      case 31543:
      motor_updown_time = 14.34;
        break;
      case 31544:
      motor_updown_time = 12.48;
        break;
      case 31545:
      motor_updown_time = 11.42;
        break;
      case 31552:
      motor_updown_time = 17.90;
        break;
      case 31553:
      motor_updown_time = 14.09;
        break;
      case 31554:
      motor_updown_time = 12.23;
        break;
      case 31555:
      motor_updown_time = 11.17;
        break;
      case 32222:
      motor_updown_time = 30.80;
        break;
      case 32223:
      motor_updown_time = 25.99;
        break;
      case 32224:
      motor_updown_time = 23.63;
        break;
      case 32225:
      motor_updown_time = 22.27;
        break;
      case 32232:
      motor_updown_time = 28.86;
        break;
      case 32233:
      motor_updown_time = 24.06;
        break;
      case 32234:
      motor_updown_time = 21.70;
        break;
      case 32235:
      motor_updown_time = 20.34;
        break;
      case 32242:
      motor_updown_time = 27.95;
        break;
      case 32243:
      motor_updown_time = 23.15;
        break;
      case 32244:
      motor_updown_time = 20.78;
        break;
      case 32245:
      motor_updown_time = 19.42;
        break;
      case 32252:
      motor_updown_time = 27.43;
        break;
      case 32253:
      motor_updown_time = 22.63;
        break;
      case 32254:
      motor_updown_time = 20.27;
        break;
      case 32255:
      motor_updown_time = 18.91;
        break;
      case 32322:
      motor_updown_time = 27.85;
        break;
      case 32323:
      motor_updown_time = 23.05;
        break;
      case 32324:
      motor_updown_time = 20.69;
        break;
      case 32325:
      motor_updown_time = 19.33;
        break;
      case 32332:
      motor_updown_time = 25.90;
        break;
      case 32333:
      motor_updown_time = 21.10;
        break;
      case 32334:
      motor_updown_time = 18.74;
        break;
      case 32335:
      motor_updown_time = 17.38;
        break;
      case 32342:
      motor_updown_time = 24.97;
        break;
      case 32343:
      motor_updown_time = 20.17;
        break;
      case 32344:
      motor_updown_time = 17.80;
        break;
      case 32345:
      motor_updown_time = 16.44;
        break;
      case 32352:
      motor_updown_time = 24.44;
        break;
      case 32353:
      motor_updown_time = 19.64;
        break;
      case 32354:
      motor_updown_time = 17.28;
        break;
      case 32355:
      motor_updown_time = 15.91;
        break;
      case 32422:
      motor_updown_time = 26.41;
        break;
      case 32423:
      motor_updown_time = 21.61;
        break;
      case 32424:
      motor_updown_time = 19.24;
        break;
      case 32425:
      motor_updown_time = 17.88;
        break;
      case 32432:
      motor_updown_time = 24.46;
        break;
      case 32433:
      motor_updown_time = 19.66;
        break;
      case 32434:
      motor_updown_time = 17.30;
        break;
      case 32435:
      motor_updown_time = 15.94;
        break;
      case 32442:
      motor_updown_time = 23.52;
        break;
      case 32443:
      motor_updown_time = 18.71;
        break;
      case 32444:
      motor_updown_time = 16.35;
        break;
      case 32445:
      motor_updown_time = 14.98;
        break;
      case 32452:
      motor_updown_time = 22.97;
        break;
      case 32453:
      motor_updown_time = 18.16;
        break;
      case 32454:
      motor_updown_time = 15.80;
        break;
      case 32455:
      motor_updown_time = 14.44;
        break;
      case 32522:
      motor_updown_time = 25.56;
        break;
      case 32523:
      motor_updown_time = 20.76;
        break;
      case 32524:
      motor_updown_time = 18.39;
        break;
      case 32525:
      motor_updown_time = 17.03;
        break;
      case 32532:
      motor_updown_time = 23.55;
        break;
      case 32533:
      motor_updown_time = 18.75;
        break;
      case 32534:
      motor_updown_time = 16.39;
        break;
      case 32535:
      motor_updown_time = 15.02;
        break;
      case 32542:
      motor_updown_time = 22.60;
        break;
      case 32543:
      motor_updown_time = 17.80;
        break;
      case 32544:
      motor_updown_time = 15.44;
        break;
      case 32545:
      motor_updown_time = 14.07;
        break;
      case 32552:
      motor_updown_time = 22.05;
        break;
      case 32553:
      motor_updown_time = 17.25;
        break;
      case 32554:
      motor_updown_time = 14.89;
        break;
      case 32555:
      motor_updown_time = 13.52;
        break;
      case 33222:
      motor_updown_time = 36.76;
        break;
      case 33223:
      motor_updown_time = 30.95;
        break;
      case 33224:
      motor_updown_time = 28.09;
        break;
      case 33225:
      motor_updown_time = 26.42;
        break;
      case 33232:
      motor_updown_time = 33.82;
        break;
      case 33233:
      motor_updown_time = 28.02;
        break;
      case 33234:
      motor_updown_time = 25.15;
        break;
      case 33235:
      motor_updown_time = 23.49;
        break;
      case 33242:
      motor_updown_time = 32.40;
        break;
      case 33243:
      motor_updown_time = 26.60;
        break;
      case 33244:
      motor_updown_time = 23.74;
        break;
      case 33245:
      motor_updown_time = 22.08;
        break;
      case 33252:
      motor_updown_time = 31.59;
        break;
      case 33253:
      motor_updown_time = 25.79;
        break;
      case 33254:
      motor_updown_time = 22.92;
        break;
      case 33255:
      motor_updown_time = 21.26;
        break;
      case 33322:
      motor_updown_time = 33.81;
        break;
      case 33323:
      motor_updown_time = 28.00;
        break;
      case 33324:
      motor_updown_time = 25.14;
        break;
      case 33325:
      motor_updown_time = 23.48;
        break;
      case 33332:
      motor_updown_time = 30.86;
        break;
      case 33333:
      motor_updown_time = 25.06;
        break;
      case 33334:
      motor_updown_time = 22.19;
        break;
      case 33335:
      motor_updown_time = 20.53;
        break;
      case 33342:
      motor_updown_time = 29.42;
        break;
      case 33343:
      motor_updown_time = 23.62;
        break;
      case 33344:
      motor_updown_time = 20.76;
        break;
      case 33345:
      motor_updown_time = 19.12;
        break;
      case 33352:
      motor_updown_time = 28.62;
        break;
      case 33353:
      motor_updown_time = 22.82;
        break;
      case 33354:
      motor_updown_time = 19.96;
        break;
      case 33355:
      motor_updown_time = 18.30;
        break;
      case 33422:
      motor_updown_time = 32.39;
        break;
      case 33423:
      motor_updown_time = 26.58;
        break;
      case 33424:
      motor_updown_time = 23.72;
        break;
      case 33425:
      motor_updown_time = 22.07;
        break;
      case 33432:
      motor_updown_time = 29.46;
        break;
      case 33433:
      motor_updown_time = 23.64;
        break;
      case 33434:
      motor_updown_time = 20.78;
        break;
      case 33435:
      motor_updown_time = 19.12;
        break;
      case 33442:
      motor_updown_time = 28.00;
        break;
      case 33443:
      motor_updown_time = 22.20;
        break;
      case 33444:
      motor_updown_time = 19.34;
        break;
      case 33445:
      motor_updown_time = 17.67;
        break;
      case 33452:
      motor_updown_time = 27.16;
        break;
      case 33453:
      motor_updown_time = 21.35;
        break;
      case 33454:
      motor_updown_time = 18.49;
        break;
      case 33455:
      motor_updown_time = 16.83;
        break;
      case 33522:
      motor_updown_time = 31.54;
        break;
      case 33523:
      motor_updown_time = 25.74;
        break;
      case 33524:
      motor_updown_time = 22.84;
        break;
      case 33525:
      motor_updown_time = 21.18;
        break;
      case 33532:
      motor_updown_time = 28.56;
        break;
      case 33533:
      motor_updown_time = 22.75;
        break;
      case 33534:
      motor_updown_time = 19.89;
        break;
      case 33535:
      motor_updown_time = 18.23;
        break;
      case 33542:
      motor_updown_time = 27.11;
        break;
      case 33543:
      motor_updown_time = 21.31;
        break;
      case 33544:
      motor_updown_time = 18.44;
        break;
      case 33545:
      motor_updown_time = 16.78;
        break;
      case 33552:
      motor_updown_time = 26.26;
        break;
      case 33553:
      motor_updown_time = 20.45;
        break;
      case 33554:
      motor_updown_time = 17.59;
        break;
      case 33555:
      motor_updown_time = 15.93;
        break;    
    }
  }
}
