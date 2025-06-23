void calculateTime() { // converts time in ms to hour minute and seconds
  myTime = millis() + offset;

  unsigned long totalSeconds = myTime / 1000;
  second = totalSeconds % 60;

  unsigned long totalMinutes = totalSeconds / 60;
  minute = totalMinutes % 60;

  unsigned long totalHours = totalMinutes / 60;
  hour = totalHours % 24;
}

void calculateTimeOffset(int init_hour, int init_minute, int init_second) { // calculates how much has to be added to millis to set time correctly
  unsigned long current_minute = init_hour*60 + init_minute;

  offset = ((current_minute * 60 + init_second) * 1000) - millis();

void getCompileTime() { //Gets compile time to set clock before phone connection is made
  
  const char* timeStr = __TIME__;

  // Udtræk timer (de første to tegn)
  char hourStr[3]; // Plads til to cifre + null-terminator
  hourStr[0] = timeStr[0];
  hourStr[1] = timeStr[1];
  hourStr[2] = '\0'; // Null-terminer strengen
  compileHour = atoi(hourStr); // Konverter streng til integer

  // Udtræk minutter (tegnene ved indeks 3 og 4)
  char minuteStr[3]; // Plads til to cifre + null-terminator
  minuteStr[0] = timeStr[3];
  minuteStr[1] = timeStr[4];
  minuteStr[2] = '\0'; // Null-terminer strengen
  compileMinute = atoi(minuteStr); // Konverter streng til integer
}

