#include <GPRS_Shield_Arduino.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>
#include <OneWire.h>

// The GSM Shield consumes A4, A5, D7, D8, D9
// 1 beep  = initilized successfully
// 3 beeps = sim900.init failed
// 6 beeps = sendSMS failed
// 9 beeps = checkSIMStatus failed


#define SERIAL_NUMBER  "1001"
#define DEBUG_MODE     false        // debug mode disables actually sending SMS and only outputs to serial.
#define MASTER_PASS    "Factory#1" // a factory master password
#define USER_TIMEOUT   600000      // ten minutes inactivity timeout while sms-programing

#define GSM_TX_PIN   7
#define GSM_RX_PIN   8
#define DS18S20_PIN  11
#define SPEAKER_PIN  A1

byte messageIndex = 0;  // sms message index on SIM
char msgIn[161];  // holds incoming sms and a null terminator
char msgOut[161]; // holds outgoing sms and a null terminator
char phoneIn[16];   // holds phone number contacting us
int  maxTemp;  // user configurable temprature range
int  minTemp;
char password[16];  // user configurable password
char phone1[16]; // these are the phone numbers where the alerts will be sent
char phone2[16];
char phone3[16];
char location[24]; // user configurable location
boolean currentProblem = false; // this keeps track of if the tempratures were outside of the threshold last it was checked

GPRS sim900(GSM_TX_PIN, GSM_RX_PIN, 9600);
OneWire DS18S20(DS18S20_PIN);


void setup() {
    // below needs to be run once per board then commented out
    // for (unsigned int i=0; i < 1024; i++) EEPROM.write(i, 0x00);
    Serial.begin(9600);
    loadSettings();
    sim900power();
    errorBeep(1);
}

void loop() {
    freeMem();
    delay(3000);
    if (sim900.checkSIMStatus() == 0) {  // check that the GSM board is healthy
        checkMessages();
        checkTemp();
    } else {
        delay(5000);                          // wait a bit and
        if (sim900.checkSIMStatus() != 0) {   // check again, just to be sure
            errorBeep(9);    // warn user of problem
            sim900power();   // reboot the board
        }
    }
}

void freeMem() {  // for diagnostic of memory leaks
    extern int __heap_start, *__brkval;
    int v;
    Serial.print((int)&v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval));
    Serial.println(F(" ==== Free Memory ===="));
}

void checkTemp() {
    int temp = getTemp();
    if (currentProblem) {
        if (temp < maxTemp && temp > minTemp) {
            memset(msgOut, 0x00, 161);  // blank the array
            appendMsg(F(" The temprature at "), msgOut); // construct a message
            appendMsg(String(location), msgOut);
            appendMsg(F(" has returned to within range. \n Currently: "), msgOut);
            appendMsg(String(temp), msgOut);
            appendMsg(F(" \n For more info send 'status'"), msgOut);
            if (sendAlert()) currentProblem = false; // send alert to the phones. if successful, don't again
        }
    } else {
        if (temp > maxTemp || temp < minTemp) {
            memset(msgOut, 0x00, 161);  // blank the array
            appendMsg(F(" A temprature alert has been triggered at "), msgOut); // construct a message
            appendMsg(String(location), msgOut);
            appendMsg(F(". \n Current temperature: "), msgOut);
            appendMsg(String(temp), msgOut);
            appendMsg(F(" \n For more info send 'status'"), msgOut);
            if (sendAlert()) currentProblem = true; // send alert to the phones. if successful, don't again
        }
    }
}

boolean sendAlert() {
    boolean returnVal = true;
    if (!msgSend(msgOut, phone1)) returnVal = false;  // send message to phone1
    if (!msgSend(msgOut, phone2)) returnVal = false;  // send message to phone2
    if (!msgSend(msgOut, phone3)) returnVal = false;  // send message to phone3
    return returnVal;
}

boolean msgSend(char msg[], char phone[]) {
    Serial.print(F("SENDING SMS to: "));
    Serial.println(phone);
    Serial.println(msg);
    Serial.println("");
    freeMem();
    if (!DEBUG_MODE) {
        if (String(strlwr(phone)) != "none" && String(strlwr(phone)) != "") {
            boolean result = sim900.sendSMS(phone,msg);  // send SMS
            if (result != 0) {  // if can't send SMS
                errorBeep(6);  // let us know
                return false;
            }
        }
    }
    return true;
}
void appendMsg(String string, char target[]) {
    size_t n = 0;
    char c = 1;
    while(c != 0) {  // find the null terminator position in the target
        c = target[n];
        n++;
    }
    size_t i = 0;
    c = 1;
    n--;  // don't include the null char
    while(c != 0) {  // copy the string over the target until hitting null terminator in string
        c = string[i];
        target[n] = string[i];
        n++;
        i++;
    }
}


void flash_into_arr(const __FlashStringHelper *flsh, char *str, size_t arrIndex){
    const char PROGMEM *p = (const char PROGMEM *)flsh;
    char c = 1;
    while (c != 0) {
        c = pgm_read_byte(p++);
        str[arrIndex] = c;
        arrIndex++;
    }
}

void appendMsg(const __FlashStringHelper *flsh, char target[]) {
    size_t n = 0;
    char c = 1;
    while(c != 0) {  // find the null terminator position in the target
        c = target[n];
        n++;
    }
    n--; // don't include the null char
    flash_into_arr(flsh, target, n);
}

void checkMessages() {
    boolean newMessage = messageArrived();
    if (newMessage && String(strlwr(msgIn)) == "program") { // if there is a new SMS: "program" 
        program(); // call program function
    }
    if (newMessage && String(strlwr(msgIn)) == "status") {  // if there is a new SMS: "status" 
        memset(msgOut, 0x00, 161);  // blank the array
        getStatus();  // getStatus() fills out the array with status info
        msgSend(msgOut, phoneIn);  // send the message
    }
}

boolean messageArrived() {  // returns true if you have a new message, or false otherwise (checks for SMS and Serial messages)
    messageIndex = sim900.isSMSunread();
    if (messageIndex > 0) { // there is one or more UNREAD SMS
        sim900.readSMS(messageIndex, msgIn, 160, phoneIn);
        sim900.deleteSMS(messageIndex);  // so as not to fill up the sim card inbox
        Serial.print(F("From number: "));
        Serial.println(phoneIn);
        Serial.print(F("Recieved SMS Message: "));
        Serial.println(msgIn);
        Serial.println("");
        freeMem();
        return true;
    }
    if (Serial.available()) {  // for deugging, this looks for serail messages and treats them like SMS also
        delay(250); // wait and make sure we have it all
        for(int i = 0;  i < 160; i++) {
            if (Serial.available()) {
                msgIn[i] = Serial.read();
            } else {
                msgIn[i] = 0x00; // null char for string
            }
        }
        memset(phoneIn, 0x00, 16);  // blank the array first
        memcpy(phoneIn, "none", 4); // so it doesn't reply with SMS
        Serial.print(F("Recieved Serial Message: "));
        Serial.println(msgIn);
        Serial.println("");
        freeMem();
        return true;
    }
    return false;
}

void program() {
    unsigned long timeout = millis() + USER_TIMEOUT; // user inactivity timeout
    char authorizedPhone[16];

    if (millis() < timeout) {
        memset(msgOut, 0x00, 161);  // blank the array
        appendMsg(F(" Program Mode: \n What is the password? \n"), msgOut); // construct a message
        msgSend(msgOut, phoneIn);  // send the message
        while(millis() < timeout){ // loop until manual break
            delay(1000);
            if (messageArrived()) {
                timeout = millis() + USER_TIMEOUT; // user action, reset timeout
                if (String(msgIn) == password || String(msgIn) == MASTER_PASS) {
                    memset(authorizedPhone, 0x00, 16);  // blank the array first
                    memcpy(authorizedPhone, phoneIn, 15); // phone number is now authenticated
                    break;
                } 
                else {
                    memset(msgOut, 0x00, 161);  // blank the array
                    appendMsg(F(" Incorrect password. \n Try again. \n"), msgOut); // construct a message
                    msgSend(msgOut, phoneIn);  // send the message
                }
            }
        }
    }

    if (millis() < timeout) {
        memset(msgOut, 0x00, 161);  // blank the array
        appendMsg(F(" You have been authenicated. \n Send the new password. \n (Currently "), msgOut); // construct a message
        appendMsg(String(password), msgOut);
        appendMsg(")", msgOut);
        msgSend(msgOut, authorizedPhone);  // send the message
        while(millis() < timeout){ // loop until manual break
            delay(1000);
            if (messageArrived()) {
                timeout = millis() + USER_TIMEOUT; // user action, reset timeout
                if (String(authorizedPhone) == String(phoneIn)) { // security check
                    memset(password, 0x00, 16);  // blank the array first
                    memcpy(password, msgIn, 15);
                    break;
                }
            }
        }
    }

    if (millis() < timeout) {
        memset(msgOut, 0x00, 161);  // blank the array
        appendMsg(F(" Send the new maximum temp. \n (Currently "), msgOut); // construct a message
        appendMsg(String(maxTemp), msgOut);
        appendMsg(")", msgOut);
        msgSend(msgOut, authorizedPhone);  // send the message
        while(millis() < timeout){ // loop until manual break
            delay(1000);
            if (messageArrived()) {
                timeout = millis() + USER_TIMEOUT; // user action, reset timeout
                if (String(authorizedPhone) == String(phoneIn)) { // security check
                    maxTemp = String(msgIn).toInt();
                    break;
                }
            }
        }
    }

    if (millis() < timeout) {
        memset(msgOut, 0x00, 161);  // blank the array
        appendMsg(F(" Send the new minimum temp. \n (Currently "), msgOut); // construct a message
        appendMsg(String(minTemp), msgOut);
        appendMsg(")", msgOut);
        msgSend(msgOut, authorizedPhone);  // send the message
        while(millis() < timeout){ // loop until manual break
            delay(1000);
            if (messageArrived()) {
                timeout = millis() + USER_TIMEOUT; // user action, reset timeout
                if (String(authorizedPhone) == String(phoneIn)) { // security check
                    minTemp = String(msgIn).toInt();
                    break;
                }
            }
        }
    }

    if (millis() < timeout) {
        memset(msgOut, 0x00, 161);  // blank the array
        appendMsg(F(" Send a phone number which you wish to receive alerts. \n (Currently "), msgOut); // construct a message
        appendMsg(String(phone1), msgOut);
        appendMsg(")", msgOut);
        appendMsg(F("\n Send only digits! Include area code. (10 digits)"), msgOut); // construct a message
        msgSend(msgOut, authorizedPhone);  // send the message
        while(millis() < timeout){ // loop until manual break
            delay(1000);
            if (messageArrived()) {
                timeout = millis() + USER_TIMEOUT; // user action, reset timeout
                if (String(authorizedPhone) == String(phoneIn)) { // security check
                    memset(phone1, 0x00, 16);  // blank the array first
                    memcpy(phone1, msgIn, 10);
                    break;
                }
            }
        }
    }

    if (millis() < timeout) {
        memset(msgOut, 0x00, 161);  // blank the array
        appendMsg(F(" Send second phone number which you wish to receive alerts. \n (Currently "), msgOut); // construct a message
        appendMsg(String(phone2), msgOut);
        appendMsg(")", msgOut);
        appendMsg(F("\n If you do not wish to add another number send 'none'."), msgOut); // construct a message
        msgSend(msgOut, authorizedPhone);  // send the message
        while(millis() < timeout){ // loop until manual break
            delay(1000);
            if (messageArrived()) {
                timeout = millis() + USER_TIMEOUT; // user action, reset timeout
                if (String(authorizedPhone) == String(phoneIn)) { // security check
                    memset(phone2, 0x00, 16);  // blank the array first
                    memcpy(phone2, msgIn, 10);
                    break;
                }
            }
        }
    }

    if (millis() < timeout) {
        memset(msgOut, 0x00, 161);  // blank the array
        appendMsg(F(" Send third phone number which you wish to receive alerts. \n (Currently "), msgOut); // construct a message
        appendMsg(String(phone3), msgOut);
        appendMsg(")", msgOut);
        appendMsg(F("\n If you do not wish to add another number send 'none'."), msgOut); // construct a message
        msgSend(msgOut, authorizedPhone);  // send the message
        while(millis() < timeout){ // loop until manual break
            delay(1000);
            if (messageArrived()) {
                timeout = millis() + USER_TIMEOUT; // user action, reset timeout
                if (String(authorizedPhone) == String(phoneIn)) { // security check
                    memset(phone3, 0x00, 16);  // blank the array first
                    memcpy(phone3, msgIn, 10);
                    break;
                }
            }
        }
    }

    if (millis() < timeout) {
        memset(msgOut, 0x00, 161);  // blank the array
        appendMsg(F(" Send the location of this device. \n (Currently "), msgOut); // construct a message
        appendMsg(String(location), msgOut);
        appendMsg(")", msgOut);
        msgSend(msgOut, authorizedPhone);  // send the message
        while(millis() < timeout){ // loop until manual break
            delay(1000);
            if (messageArrived()) {
                timeout = millis() + USER_TIMEOUT; // user action, reset timeout
                if (String(authorizedPhone) == String(phoneIn)) { // security check
                    memset(location, 0x00, 24);  // blank the array first
                    memcpy(location, msgIn, 23);
                    break;
                }
            }
        }
    }

    if (millis() < timeout) {
        memset(msgOut, 0x00, 161);  // blank the array
        appendMsg(F(" pwd: "), msgOut); // construct a message
        appendMsg(String(password), msgOut);
        appendMsg(F("\n max temp: "), msgOut);
        appendMsg(String(maxTemp), msgOut);
        appendMsg(F("\n min temp: "), msgOut);
        appendMsg(String(minTemp), msgOut);
        appendMsg(F("\n phone1: "), msgOut);
        appendMsg(String(phone1), msgOut);
        appendMsg(F("\n phone2: "), msgOut);
        appendMsg(String(phone2), msgOut);
        appendMsg(F("\n phone3: "), msgOut);
        appendMsg(String(phone3), msgOut);
        appendMsg(F("\n location: "), msgOut);
        appendMsg(String(location), msgOut);
        appendMsg(F("\n save?"), msgOut);
        msgSend(msgOut, authorizedPhone);  // send the message
        while(millis() < timeout){ // loop until manual break
            delay(1000);
            if (messageArrived()) {
                timeout = millis() + USER_TIMEOUT; // user action, reset timeout
                if (String(authorizedPhone) == String(phoneIn)) { // security check
                    if (String(strlwr(msgIn)) == "yes") {
                        saveSettings(); // write settings to eeprom
                        memset(msgOut, 0x00, 161);  // blank the array
                        appendMsg(F(" Settings saved."), msgOut); // construct a message
                        msgSend(msgOut, authorizedPhone);  // send the message
                        break;
                    }
                    if (String(strlwr(msgIn)) == "no") {
                        loadSettings();  // restore settings from eeprom
                        memset(msgOut, 0x00, 161);  // blank the array
                        appendMsg(F("Settings discarded."), msgOut); // construct a message
                        msgSend(msgOut, authorizedPhone);  // send the message
                        break;
                    }
                }
            }
        }
    }

    if (millis() > timeout) {
        loadSettings();  // restore settings from eeprom
        memset(msgOut, 0x00, 161);  // blank the array
        appendMsg(F(" PROGRAMING TIMEOUT \n Settings discarded. \n Send 'program' to try again."), msgOut); // construct a message
        msgSend(msgOut, phoneIn);  // send the message
    }

}

void getStatus() {
    int temp = getTemp();
    appendMsg(F(" temp now: "), msgOut);
    appendMsg(String(temp), msgOut);
    appendMsg(F("\n max temp: "), msgOut);
    appendMsg(String(maxTemp), msgOut);
    appendMsg(F("\n min temp: "), msgOut);
    appendMsg(String(minTemp), msgOut);
    appendMsg(F("\n phone1: "), msgOut);
    appendMsg(String(phone1), msgOut);
    appendMsg(F("\n phone2: "), msgOut);
    appendMsg(String(phone2), msgOut);
    appendMsg(F("\n phone3: "), msgOut);
    appendMsg(String(phone3), msgOut);
    appendMsg(F("\n location: "), msgOut);
    appendMsg(String(location), msgOut);
    appendMsg(F("\n SN: "), msgOut);
    appendMsg(F(SERIAL_NUMBER), msgOut);
    appendMsg(F("\n SIG: "), msgOut);
    appendMsg(String(sim900.checkReception()), msgOut);
    appendMsg(F("/30"), msgOut); 
}


int getTemp() {  //returns the temperature from one DS18S20 in fahrenheit
    byte data[12];
    byte addr[8];
    if ( !DS18S20.search(addr)) {
        //no more sensors on chain, reset search
        DS18S20.reset_search();
        return -1000;
    }
    if ( OneWire::crc8( addr, 7) != addr[7]) {
        // crc is not valid
        return -1000;
    }
    if ( addr[0] != 0x10 && addr[0] != 0x28) {
        // Device is not recognized
        return -1000;
    }
    DS18S20.reset();
    DS18S20.select(addr);
    DS18S20.write(0x44,1); // start conversion, with parasite power on at the end
    delay(800);
    byte present = DS18S20.reset();
    DS18S20.select(addr);  
    DS18S20.write(0xBE); // Read Scratchpad
    for (int i = 0; i < 9; i++) // we need 9 bytes
        data[i] = DS18S20.read();
    DS18S20.reset_search();
    byte MSB = data[1];
    byte LSB = data[0];
    float tempRead = ((MSB << 8) | LSB); //using two's compliment
    float TemperatureC = tempRead / 16;  // in celsius
    return (int)(TemperatureC * 9/5 + 32.5); // convert to fahrenheit
}

void saveSettings() {
    for (int i = 0;  i < 16; i++)
        EEPROM.write(i, password[i]);  // password occupies eeprom 0-15
    for (int i = 0; i < 16; i++)
        EEPROM.write(i+16, phone1[i]); // phone1 occupies eeprom 16-31
    for (int i = 0; i < 16; i++)
        EEPROM.write(i+32, phone2[i]); // phone2 occupies eeprom 32-47
    for (int i = 0; i < 16; i++)
        EEPROM.write(i+48, phone3[i]); // phone3 occupies eeprom 48-63
    for (int i = 0; i < 24; i++)
        EEPROM.write(i+64, location[i]); // location occupies eeprom 64-87
    EEPROM.write(88, highByte(minTemp));
    EEPROM.write(89, lowByte(minTemp));
    EEPROM.write(90, highByte(maxTemp));
    EEPROM.write(91, lowByte(maxTemp));
}

void loadSettings() {
    for (int i = 0;  i < 16; i++)
        password[i] = EEPROM.read(i);  // password occupies eeprom 0-15
    for (int i = 0; i < 16; i++)
        phone1[i] = EEPROM.read(i+16); // phone1 occupies eeprom 16-31
    for (int i = 0; i < 16; i++)
        phone2[i] = EEPROM.read(i+32); // phone2 occupies eeprom 32-47
    for (int i = 0; i < 16; i++)
        phone3[i] = EEPROM.read(i+48); // phone3 occupies eeprom 48-63
    for (int i = 0; i < 24; i++)
        location[i] = EEPROM.read(i+64); // location occupies eeprom 64-87
    minTemp = (EEPROM.read(88) << 8) | EEPROM.read(89);
    maxTemp = (EEPROM.read(90) << 8) | EEPROM.read(91);
}


void errorBeep(byte beepCount) {
    freeMem();
    pinMode(SPEAKER_PIN, OUTPUT);
    for (byte i = 0; i < beepCount; i++) {
        digitalWrite(SPEAKER_PIN, HIGH);  // beep
        delay(500);
        digitalWrite(SPEAKER_PIN, LOW);  // stop
        delay(250);
    }
    Serial.print(F("ERROR NUMBER: "));
    Serial.println(beepCount);
}

void sim900power() {
    powerCycle900();  // we expect it to be off now, so we are turning it on hopefully
    Serial.println(F("Initializing SIM900..."));
    while(sim900.init() != 0) {  // sim900.init() returns 0 on success
        Serial.println(F("Problem initializing. Retrying"));
        powerCycle900(); // turn on or off the sim 900 board
        errorBeep(3);    // let people know something didn't go right.
        delay(1000);
    }
    Serial.println(F("SIM900 Initialized"));
}


void powerCycle900() { // virtually presses the power button on the sim900 board
    Serial.print(F("Pressing SIM900 power button... "));
    pinMode(9, OUTPUT); // the sim900 power putton is pressed by pin 9
    digitalWrite(9,LOW);
    delay(2000);
    digitalWrite(9,HIGH);
    delay(2000);
    digitalWrite(9,LOW);
    Serial.println(F("Done!"));
    delay(2000);
}
