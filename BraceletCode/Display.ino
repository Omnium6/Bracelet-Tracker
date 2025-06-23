
void setupEPD() {
  int compileHour;
  int compileMinute;
  pinMode(buzzerPin, OUTPUT);
  Config_Init();
  //if(EPD_Init() != 0) {
    //Serial.print("Display init failed");
  //}
  EPD_Init();
  EPD_Clear();
  DEV_Delay_ms(500);
  EPD_Sleep();
}

void updateDisplayTime() {
  EPD_TurnOnDisplay();
  EPD_Init();
  paith.Clear(EPD_WHITE); //Clear imagebuffer so new data is not just written on top of old
  sprintf(hour_str, "%02u", hour);      //Converts time int to str
  sprintf(minute_str, "%02u", minute);  //Converts time int to str
  paith.DrawStringAt(25, 35, hour_str, &Font24, EPD_BLACK);   //Draws string to image buffer
  paith.DrawStringAt(25, 65, minute_str, &Font24, EPD_BLACK); //Draws string to image buffer
  EPD_Display(image_temp);
  EPD_Sleep(); //Puts display in sleepmode for power savings
}
void EPDAlarm() {
  tone(buzzerPin, 4000, 1500); //makes buzzer play 4000 hz tone for 1500ms
  EPD_TurnOnDisplay();
  EPD_Init();
  paith.Clear(EPD_BLACK);
  paith.DrawStringAt(5,5, "FALL", &Font24, EPD_WHITE);  //Draws string to image buffer
  paith.DrawStringAt(5,35, "Dete", &Font24, EPD_WHITE);
  paith.DrawStringAt(5,65, "cted", &Font24, EPD_WHITE);
  EPD_Display(image_temp);
  EPD_Sleep();
}
