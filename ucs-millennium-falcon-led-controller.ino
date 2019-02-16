/* ucs-millennium-falcon-led-controller  
 * Arduino based LED light controller with WiFi (ESP-01 module) control
 *
 * Copyright (C) 2019 Timo Kokkonen <tjko@iki.fi> 
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#include <EEPROM.h>


#define PRO_MICRO 1

// Defaults for Arduino Pro Micro (3.3V/8Mhz)
#define BUTTON_PIN     14
#define LED_DRIVE_PIN  3 
#define LED2_DRIVE_PIN 5
#define LED3_DRIVE_PIN 6
#define LED4_DRIVE_PIN 9
#define ESP_RESET_PIN  4

#define LED_COUNT 4 // number of LED pins in use
#define LED_STATES 2 



// Automatic turn off timer (default 8 hours)
#define SLEEP_TIME  (1000UL * 60 * 60 * 8)


// Interval for periodic reset/reboot of ESP-01 module (default 48 hours)
#define ESP_RESET_TIME (1000UL * 60 * 60 * 48)
#define ESP_AUTO_REBOOT 1   // set to 1 if want to enable automatic reset of ESP module

// button "debounce" time adjust depending on switch being used...
#define DEBOUNCE_TIME 80

#define TURN_OFF_DELAY 1000   // turn of after "long" button press (1sec)

#define MODEM_TIMEOUT 5000
#define MODEM_TIMEOUT2 1000


#ifdef PRO_MICRO
 #define Console Serial
 #define Modem Serial1
 #define CONSOLE_SPEED 115200
 #define MODEM_SPEED 57600
#else
 // uncomment to use console via digital pins 10 & 11
 //#define ALTCONSOLE 1

 #define SOFTSERIAL_RX_PIN 10
 #define SOFTSERIAL_TX_PIN 11

 #include <SoftwareSerial.h>
 #ifdef ALTCONSOLE
  SoftwareSerial Console(SOFTSERIAL_RX_PIN,SOFTSERIAL_TX_PIN);
  #define Modem Serial
 #else
 #define Console Serial
  SoftwareSerial Modem(SOFTSERIAL_RX_PIN,SOFTSERIAL_TX_PIN);
 #endif
 #define CONSOLE_SPEED 9600
 #define MODEM_SPEED 9600
#endif


// EEPROM ID
#define ROM_ID_0 0x42
#define ROM_ID_1 0x10

// EEPROM Layout
#define ROM_ID_IDX  0 // 2 bytes  EEPROM ID string
#define ROM_LED_IDX 2 // 4 bytes  LED defaut power levels
 

#define HTTP_HEADERS   "Server: Arduino through ESP-01\r\n" \
                       "Connection: close\r\n" \
                       "Content-type: text/html; charset=iso-8859-15\r\n\r\n"

#define HTTP_PAGE_HEAD "<html><head><title>UCS Millennium Falcon Control Panel</title>\r\n" \
                       "<meta name=\"viewport\" content=\"width=device-width,initial-scale=0.93,user-scaleable=yes\">\r\n" \ 
                       "<style>\r\n" \
                       " #b1 { background: #016567; border-radius: 10px; padding: 10px; } " \
                       " #l1 { background: #03a3aa; border-radius: 5px; padding: 10px; } " \
                       " .sc { width:100%; } " \
                       " .s { -webkit-appearance: none; width: 82%; height: 15px; border-radius: 10px; background: #d3d3d3; outline: none; opacity: 0.7; -webkit-transition: .2s; transition: opacity .2s; }" \ 
                       " .s:hover { opacity: 1; } " \
                       " .s::-webkit-slider-thumb { -webkit-appearance: none; appearance: none; width: 30px; height: 30px; border-radius: 50%; background: #4CAF50; cursor: pointer;}" \
                       " .s::-moz-range-thumb { width: 30px; height: 30px; border-radius: 50%; background: #4caf50; cursor: pointer; }" \
                       " .button { border: none; border-radius: 16px; color: white; padding: 8px 14px; cursor: pointer; background-color: #4caf50; } " \
                       " .button:hover { background-color: #0b7dda; } " \
                       " .x1 { margin-left: 10px; } " \
                       "\r\n</style>\r\n</head><body>\r\n" \
                       "<table id=\"b1\">" \
                       "<tr><td><h2><font color=#03a3aa>UCS Millennium Falcon Control Panel</font></h2><p>" \
                       "<tr><td><a href=\"/on\"><button type=\"button\" class=\"button\">ON</button></a> " \
                       "<a href=\"/off\"><button type=\"button\" class=\"button\">OFF</button></a> &nbsp; " \
                       "<a href=\"/\"><button type=\"button\" class=\"button\">Refresh</button></a><br><br>"


#define HTTP_PAGE_FOOT "\r\n<script>\r\n" \
                       " var s1=document.getElementById(\"s1\");" \
                       " var o1=document.getElementById(\"v1\");" \
                       " o1.innerHTML=s1.value;" \
                       " s1.oninput=function() { o1.innerHTML=this.value; };" \
                       " var s2=document.getElementById(\"s2\");" \
                       " var o2=document.getElementById(\"v2\");" \
                       " o2.innerHTML=s2.value;" \
                       " s2.oninput=function() { o2.innerHTML=this.value; };" \
                       " var s3=document.getElementById(\"s3\");" \
                       " var o3=document.getElementById(\"v3\");" \
                       " o3.innerHTML=s3.value;" \
                       " s3.oninput=function() { o3.innerHTML=this.value; };" \                   
                       " var s4=document.getElementById(\"s4\");" \
                       " var o4=document.getElementById(\"v4\");" \
                       " o4.innerHTML=s4.value;" \
                       " s4.oninput=function() { o4.innerHTML=this.value; };" \                   
                       "\r\n</script>\r\n" \
                       "</body></html>\r\n"


const char http_headers[] PROGMEM = HTTP_HEADERS;
const char http_ok[] PROGMEM = "HTTP/1.0 200 OK\r\n";
const char http_err[] PROGMEM = "HTTP/1.0 403 Forbidden\r\n";
const char http_redirect[] PROGMEM = "HTTP/1.0 302 Found\r\n";
const char page_head[] PROGMEM = "HTTP/1.0 200 OK\r\n" HTTP_HEADERS HTTP_PAGE_HEAD;
const char page_foot[] PROGMEM = HTTP_PAGE_FOOT;



const char ModemInit1[] PROGMEM = "ATE0";
const char ModemInit2[] PROGMEM = "AT+CIPMUX=1";
const char ModemInit3[] PROGMEM = "AT+CIPSERVERMAXCONN=2";
const char ModemInit4[] PROGMEM = "AT+CIPDINFO=0";
const char ModemInit5[] PROGMEM = "AT+CIPSERVER=1,80";
const char ModemInit6[] PROGMEM = "AT+CIPSTO=30";
const char ModemInit7[] PROGMEM = "AT+GMR";
const char ModemInit8[] PROGMEM = "AT+CIPSTA_CUR?";

const char* const modem_init_cmds[] = {
    ModemInit1,
    ModemInit2,
    ModemInit3,
    ModemInit4,
    ModemInit5,
    ModemInit6,
    ModemInit7,
    ModemInit8,
    NULL
};

const byte led_pin[] = { LED_DRIVE_PIN, LED2_DRIVE_PIN, LED3_DRIVE_PIN, LED4_DRIVE_PIN };
byte led_state[][2] = { 
                            { 0, 100},
                            { 0, 100},
                            { 0, 100},
                            { 0, 100}
                          };


int last = 0;
int button = 0;
byte power = 0;
byte ledstate = 0;
byte lastledstate = 0;
unsigned long ticks;
unsigned long button_down = 0;
unsigned long change_t = 0;
unsigned long press_len = 0;
unsigned long sleep_t = 0;
unsigned long long_press_t = 0;
unsigned long esp_reset_t = 0;

char buf[256];
int bufptr = 0;
int readcount = 0;
int modem_state = 0;
int modem_init_state = 0;
unsigned long modem_send_t = 0;
char outbuf[1250];
byte outheaders = 0;
int connection = 0;
int inputlen = 0;
int inputread = 0;


void setup() {
  int i;

  pinMode(ESP_RESET_PIN, OUTPUT);
  digitalWrite(ESP_RESET_PIN, HIGH);
  pinMode(BUTTON_PIN, INPUT);
  pinMode(LED_BUILTIN, OUTPUT);  
  digitalWrite(LED_BUILTIN, HIGH);
  
  for (i=0; i < LED_COUNT; i++) {
    pinMode(led_pin[i],OUTPUT);
    analogWrite(led_pin[i],255);
  }

  // Initialize serial console...
  Console.begin(CONSOLE_SPEED);
  #ifndef PRO_MICRO
  while (!Console) {
    delay(1);
  }  
  #endif

  // Initialize serial port to WiFi module
  Modem.begin(MODEM_SPEED);
  while (!Modem) {
    delay(1);
  }

  delay(3000);

  Console.println(F("LED NightLight Controller (with WiFi) v2.0  " __DATE__));
  Console.println(F("Copyright (C) 2018-2019 Timo Kokkonen <tjko@iki.fi>"));
  Console.print(F("Sleep time: "));
  Console.println(SLEEP_TIME);


  // Check if EEPROM needs to be initialized...
  
  if (!(EEPROM.read(ROM_ID_IDX) == ROM_ID_0 && EEPROM.read(ROM_ID_IDX+1) == ROM_ID_1)) {
    Console.println(F("Initializing EEPROM..."));

    // clear entire EEPROM...
    for(i=0; i < EEPROM.length(); i++) EEPROM.write(i,0);

    // EEPROM id
    EEPROM.write(ROM_ID_IDX+0,ROM_ID_0);
    EEPROM.write(ROM_ID_IDX+1,ROM_ID_1);
    // LED default power levels
    EEPROM.write(ROM_LED_IDX+0, 100);
    EEPROM.write(ROM_LED_IDX+1, 100);
    EEPROM.write(ROM_LED_IDX+2, 100);
    EEPROM.write(ROM_LED_IDX+3, 0);
    
  }
  
  // Read configuration from EEPROM...

  for(i=0; i<LED_COUNT; i++) {
    led_state[i][1]=EEPROM.read(ROM_LED_IDX+i);
  }

    
  Console.println(F("Running..."));
  digitalWrite(LED_BUILTIN, LOW);
  for (int led=0; led < LED_COUNT; led++) {
    analogWrite(led_pin[led],0);
  }

}


// check fo available data from modem (ESP-01) and read it to buffer
// return >0  if full line waiting in buffer
int read_modem(char *buf, int bufsize, int *bufptr, int *readcount) {
  int r;
  while(Modem.available()) {
      char c = Modem.read();
      (*readcount)++;
      if (c == 10) { // LF
        buf[*bufptr]=0;
        *bufptr=0;
        r=*readcount;
        *readcount=0;
        return r;
      }
      if (c != 13) buf[(*bufptr)++]=c;
      if (*bufptr >= bufsize-1) {
        buf[*bufptr]=0;
        *bufptr=0;
        r=*readcount;
        *readcount=0;
        return r;      
      }
  }
  return 0;
}


// send a command to modem (ESP-01)
void modem_send(const char *cmd) {
  Console.print(F("S: "));
  Console.println(cmd);
  Modem.print(cmd);
  Modem.print(F("\r\n"));
  modem_send_t = millis();
}


// reset ESP-01 using the RST pin
void reset_modem()
{
  Console.println(F("Resetting modem..."));

  // toggle reset pin to LOW to cause ESP-01 module reset...
  digitalWrite(ESP_RESET_PIN, LOW);
  delay(500);
  digitalWrite(ESP_RESET_PIN, HIGH);

  Console.println(F("Waiting modem to boot up..."));
  // give module little bit time to boot up...
  delay(1500);

  modem_state=0;
}


// process HTTP requests...
void process_command(const char *cmd) {
  unsigned long ticks = millis();
  unsigned long secs = ticks/1000;
  unsigned long mins = secs/60;
  unsigned long hours = mins/60;
  unsigned long days = hours/24;
  unsigned long sleep_left = (sleep_t > 0 ? (SLEEP_TIME - (ticks - sleep_t))/60000: 0);

  outheaders=0;
  Console.print(F("process_command ("));
  Console.print(connection);
  Console.print(',');
  Console.print(inputlen);
  Console.print(F("): "));
  Console.println(cmd);

  if (!strncmp_P(cmd,PSTR("GET / "),6)) {
    snprintf_P(outbuf,sizeof(outbuf),
              PSTR(
                   "<tr><td id=\"l1\">Power: %s<br>Sleep timer: %lu min (left)<br>"
                   "<tr><td><br><hr><div class=\"sc\">\r\n"
                   "<p>Main Lights: <span id=\"v1\"></span>%%</p><div class=\"x1\"><form action=\"/set\" method=\"get\"><input type=\"range\" min=\"0\" max=\"100\" value=\"%d\" class=\"s\" id=\"s1\" name=\"s1\"> <button type=\"submit\" class=\"button\">Set</button></form></div>\r\n"
                   "<p>Engine Lights: <span id=\"v2\"></span>%%</p><div class=\"x1\"><form action=\"/set\" method=\"get\"><input type=\"range\" min=\"0\" max=\"100\" value=\"%d\" class=\"s\" id=\"s2\" name=\"s2\"> <button type=\"submit\" class=\"button\">Set</button></form></div>\r\n"
                   "<p>Interior Lights: <span id=\"v3\"></span>%%</p><div class=\"x1\"><form action=\"/set\" method=\"get\"><input type=\"range\" min=\"0\" max=\"100\" value=\"%d\" class=\"s\" id=\"s3\" name=\"s3\"> <button type=\"submit\" class=\"button\">Set</button></form></div>\r\n"
                   "<p>Weapons: <span id=\"v4\"></span>%%</p><div class=\"x1\"><form action=\"/set\" method=\"get\"><input type=\"range\" min=\"0\" max=\"100\" step=\"100\" value=\"%d\" class=\"s\" id=\"s4\" name=\"s4\"> <button type=\"submit\" class=\"button\">Set</button></form></div>\r\n"
                   "<br></div>\r\n"
                   "<tr><td><table width=\"100%%\"><tr><td><font size=-1 color=#03a3aa>Uptime: %lu days %02lu:%02lu:%02lu<br></font>"
                   "<td align=\"right\"><font size=-2>by tjko@iki.fi</font></table>"
                   "</table>\r\n"),
                   (ledstate>0?"ON":"OFF"),sleep_left,
                   led_state[0][1],led_state[1][1],led_state[2][1],led_state[3][1],
                   days,hours%24,mins%60,secs%60);
    outheaders=1;          
    return;
  } else if (!strncmp_P(cmd,PSTR("GET /on "),8)) {
    ledstate++;
    sleep_t = ticks;
    if (ledstate >= LED_STATES) ledstate=1;
  } else if (!strncmp_P(cmd,PSTR("GET /on?state="),14)) {
    char *s = cmd + 14;
    int maxstate = LED_STATES-1;
    int newstate = *s - '0';
    if (newstate < 0) newstate=0;
    if (newstate >= maxstate) newstate=maxstate;
    ledstate=newstate; 
    sleep_t = ticks;
  } else if (!strncmp_P(cmd,PSTR("GET /set?"),9)) {
    char *s = cmd+10;
    Console.print(F("SET :"));
    Console.println(s);
    char *sl = strtok(s,"=");
    if (sl) {
      int slider=atoi(sl);
      char *v = strtok(NULL,"=");
      if (slider > 0 && slider < 5 && v) {
        int val=atoi(v);
        if (val < 0) val=0;
        if (val > 100) val=100;
        Console.print("SLIDER:");
        Console.print(slider);
        Console.print(",");
        Console.println(val);
        led_state[slider-1][1]=val;
        lastledstate=255;
      }
    }
  } else if (!strncmp_P(cmd,PSTR("GET /off "),9)) {
    ledstate=0;
    sleep_t=0;
  } else if (!strncmp_P(cmd,PSTR("GET /save "),10)) {
    for(int i=0; i < LED_COUNT; i++) {
      // save current led power levels into EEPROM...
      EEPROM.write(ROM_LED_IDX+i,led_state[i][1]);
    }
  } else if (!strncmp_P(cmd,PSTR("GET /status "),12)) {
    byte led_default[4];
    for(int i=0; i < LED_COUNT; i++) {
      led_default[i]=EEPROM.read(ROM_LED_IDX+i);
    }
    snprintf_P(outbuf,sizeof(outbuf),PSTR("%S%SUCS Millennium Falcon,%d,%lu,%d,%d,%d,%d,%d,%d,%d,%d\r\n"),
               http_ok,http_headers,
               ledstate,sleep_left,
               led_state[0][1],led_state[1][1],led_state[2][1],led_state[3][1],
               led_default[0],led_default[1],led_default[2],led_default[3]
               );
    return;
  } else {
    snprintf_P(outbuf,sizeof(outbuf),PSTR("%S%SAccess Denied!\r\n"),http_err,http_headers);
    return;
  }

  snprintf_P(outbuf,sizeof(outbuf),PSTR("%SLocation: /\r\n"),http_redirect);      
}



///////////////////////////////////////////////////////////////////////////////////////////

void loop() {
  ticks = millis();
  char tmp[32];

  // check for button presses
  
  button = digitalRead(BUTTON_PIN);
  if (ticks - change_t > DEBOUNCE_TIME) {
    // do not read button value too often to filter out noise from the mechanical switch...

    if (button != last) {
      change_t = ticks;
      // button state changed...
      if (button == HIGH) { // button pressed down 
        button_down = ticks;
        sleep_t = ticks;
        digitalWrite(LED_BUILTIN, HIGH);
        ledstate++;
        if (ledstate > 1) ledstate=0;
        Console.print(ticks);
        Console.print(F(": Button Pressed"));
        Console.print(F(" LED="));
        Console.println(ledstate);
      } else { // button released
        press_len = ticks - button_down;
        digitalWrite(LED_BUILTIN, LOW);
        Console.print(ticks);
        Console.print(F(": Button Released (press length "));
        Console.print(press_len);
        Console.println(F("ms)"));
      }
      last=button;
    } else {
      // no change in button state...
      if ( button == HIGH &&  change_t > long_press_t &&
          (ticks - button_down) > TURN_OFF_DELAY ) {
        // check if button has been held down long enough ("long press")...
        long_press_t = ticks;
        sleep_t = 0;         
        ledstate = 0;
        Console.print(ticks);
        Console.println(F(": LED=OFF"));
        reset_modem();
      }
    }
  }
    

  // check if its time to turn off the light...

  if (sleep_t > 0 && (ticks - sleep_t) > SLEEP_TIME) {
    Console.print(ticks);
    Console.println(F(": Sleep timer LED=OFF"));
    Console.println(sleep_t);
    sleep_t = 0;
    ledstate = 0;
  }


  // control LED

  if (ledstate != lastledstate) {
    for(int led=0; led < LED_COUNT; led++) {
      byte pin = led_pin[led];
      byte val = led_state[led][ledstate] * (255.0/100.0);
      Console.print(F("set LED"));
      Console.print(led+1);
      Console.print(F(" val="));
      Console.println(val);
      analogWrite(pin,val);
    }
    lastledstate=ledstate;
  }


  // check if its time to reset modem (ESP module)...

#if ESP_AUTO_REBOOT
  if ( (ticks - esp_reset_t) > ESP_RESET_TIME) {
    reset_modem();
    esp_reset_t = ticks;
  }
#endif  

  // check if there there is line received from modem...

  int readlen;
  if ((readlen=read_modem(buf, sizeof(buf), &bufptr, &readcount)) > 0) {
    if (strlen(buf) > 0) {
      Console.print(F("R: ")); 
      Console.println(buf);
    }
    if (modem_state == 5) {
      inputread+=readlen;
      Console.print(F("read extra input: inputread="));
      Console.println(inputread);
    } else if (!strcmp_P(buf, PSTR("OK"))) {
      switch (modem_state) {
        case 1: 
          Console.println(F("modem ok"));
          modem_state=2;
          break;
        case 3:
          modem_init_state++;
          modem_state=2;
          break;
        case 6:
          Console.print(F("sending header: len="));
          Console.println(strlen_P(page_head));
          for(int x=0;x < strlen_P(page_head); x++) {
            char c=pgm_read_byte_near(page_head+x);
            Modem.print(c);
          }  
          modem_state=7;
          modem_send_t=millis();
          break;
        case 9:
          Console.print(F("send data: len="));
          Console.println(strlen(outbuf));
          Modem.print(outbuf);
          modem_state=10;
          modem_send_t=millis();
          break;
        case 12:
          Console.print(F("sending footer: len="));
          Console.println(strlen_P(page_foot));
          for(int x=0;x < strlen_P(page_foot); x++) {
            char c=pgm_read_byte_near(page_foot+x);
            Modem.print(c);
          }
          modem_state=13;
          modem_send_t=millis();
          break;  
      }  
    } else if(!strcmp_P(buf, PSTR("ERROR"))) {
      switch (modem_state) {
        case 3:
          modem_init_state++;
          modem_state=2;
          break;
        case 6:
        case 9:
        case 12:
          Console.println(F("CIPSEND failed"));
          modem_state=20;
          break;  
      }
    } else if (!strcmp_P(buf, PSTR("SEND OK"))) {
      switch (modem_state) {
        case 7:
          Console.println(F("send header ok"));
          modem_state=8;
          break;
        case 10:
          Console.println(F("send data ok"));
          if (outheaders) {
            modem_state=11;
          } else {
            modem_state=20;
          }
          break;
        case 13:
          Console.println(F("send footer ok"));
          modem_state=20;
          break;
      }
    } else if (!strcmp_P(buf, PSTR("SEND FAIL"))) {
      switch (modem_state) {
        case 7:
        case 10:
        case 13:
          Console.println(F("send fail"));
          modem_state=20;
          break;
      }
    } else if (!strncmp_P(buf, PSTR("+IPD,"),5)) {
      char *s = buf+5;
      char *con = strtok(s,",");
      if (con) {
        connection = atoi(con);
        char *len = strtok(NULL,":");
        if (len) {
          inputlen = atoi(len);  
          char *cmd = strtok(NULL,":");
          if (cmd) {
            inputread = (readlen-(cmd-buf));
            process_command(cmd);            
            modem_state=5;
            modem_send_t=millis();
          }     
        }
      }
    } else {
      //Console.println("no match");
    }
  }

  byte timeout = (millis() - modem_send_t > MODEM_TIMEOUT ? 1 : 0);
  switch (modem_state) {
    case 0:
      Console.println(F("modem init"));
      modem_send("AT");
      modem_state=1;
      break;

    case 1:
      if (timeout) {
        Console.println(F("modem timeout"));
        modem_state=0;
      }
      break;

    case 2:
      if (modem_init_cmds[modem_init_state]) {
        snprintf(tmp,sizeof(tmp),"%S",modem_init_cmds[modem_init_state]);
        modem_send(tmp);
        modem_state=3;
      } else {
        Console.println(F("init complete"));
        modem_init_state=0;
        modem_state=4;
      }
      break;

    case 3:
      if (timeout) {
        Console.println(F("init timeout"));
        modem_state=2; 
      }
      break;  

    case 4:
      break;

    case 5:
      timeout = (millis() - modem_send_t > MODEM_TIMEOUT2 ? 1 : 0);
      if (inputread >= inputlen || timeout) {
        if (timeout) {
          Console.print(F("input wait timeout: readcount="));
          Console.println(readcount);
          if (readcount > 0) {
            // flush input buffer
            readcount=0;
            bufptr=0;
          }
        }
        Console.println(F("input read"));
        if (outheaders) {
          Console.print(F("send header "));
          Console.println(strlen_P(page_head));
          snprintf_P(tmp,sizeof(tmp),PSTR("AT+CIPSEND=%d,%d"),connection,strlen_P(page_head));
          modem_send(tmp);
          modem_state=6;
        } else {
          modem_state=8;
        }
      }
      break;
  
      
    case 6:
    case 9:
    case 12:
      if (timeout) {
        Console.println(F("CIPSEND timeout"));
        modem_state=20;
      }
      break;

    case 7:
    case 10:
    case 13:
      if (timeout) {
        Console.println(F("data send timeout"));
        modem_state=4;  
      }
      break;


    case 8:
      Console.print(F("send data "));
      Console.println(strlen(outbuf));
      snprintf_P(tmp,sizeof(tmp),PSTR("AT+CIPSEND=%d,%d"),connection,strlen(outbuf));
      modem_send(tmp);
      modem_state=9;
      break;
 
    case 11:
      Console.print(F("send footer "));
      Console.println(strlen_P(page_foot));
      snprintf_P(tmp,sizeof(tmp),PSTR("AT+CIPSEND=%d,%d"),connection,strlen_P(page_foot));
      modem_send(tmp);
      modem_state=12;
      break;

    case 20:
      Console.println("close connection");
      snprintf_P(tmp,sizeof(tmp),PSTR("AT+CIPCLOSE=%d"),connection);
      modem_send(tmp);
      modem_state=4;
      break;

    default:
      Console.print(modem_state);
      Console.println(F(" unknown state"));
  }
  
}


// eof :-)
