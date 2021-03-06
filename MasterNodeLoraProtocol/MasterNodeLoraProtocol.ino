#include <SPI.h>
#include <LoRa.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <BlynkSimpleEsp8266.h>
#include <WiFiClientSecure.h>
#include "AESLib.h"
#include <Arduino.h>
#include <Hash.h>

AESLib aesLib;

char cleartext[256];
char ciphertext[512];

// AES Encryption Key
byte aes_key[] = "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@";

// General initialization vector (you must use your own IV's in production for full security!!!)
byte aes_iv[N_BLOCK] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

String plaintext = "HELLO WORLD!";

const int csPin = 15;          // LoRa radio chip select
const int resetPin = 16;       // LoRa radio reset
const int irqPin = 4;         // change for your board; must be a hardware interrupt pin

byte msgCount = 0;            // count of outgoing messages
int interval = 2000;          // interval between sends
long lastSendTime = 0;        // time of last packet send

//telegram auth token
char auth[] = "#################################################";

//wifi creds
char ssid[] = "###########";
char pass[] = "#############";

int command_counter = 0; //rolling counter for changing hash of commands
String hash_key = "hai";

BLYNK_WRITE(Vxxx)
{
  Serial.println("WebHook data:");
  Serial.println(param.asStr());
}

BLYNK_WRITE(Vxxxx)
{

  int pinData = param.asInt();

  if (pinData == 1)
  {
    toggle_garage();
  }
}



void setup() {
  Serial.begin(9600);                   // initialize serial
  while (!Serial);

  Serial.println("LoRa Duplex - Set sync word");

  // override the default CS, reset, and IRQ pins (optional)
  LoRa.setPins(csPin, resetPin, irqPin);// set CS, reset, IRQ pin

  if (!LoRa.begin(433E6)) {             // initialize ratio at 915 MHz
    Serial.println("LoRa init failed. Check your connections.");
    while (true);                       // if failed, do nothing
  }

  LoRa.setSyncWord(0xF3);           // ranges from 0-0xFF, default 0x34, see API docs
  Serial.println("LoRa init succeeded.");


  Blynk.begin(auth, ssid, pass);
  Blynk.virtualWrite(Vxxxx, "Blynk%20Has%20Started!");

  //-------------AES SETUP---------------------
  
  aes_init();
  aesLib.set_paddingmode(paddingMode::Array);

  char b64in[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

  char b64out[base64_enc_len(sizeof(aes_iv))];
  base64_encode(b64out, b64in, 16);

  char b64enc[base64_enc_len(10)];
  base64_encode(b64enc, (char*) "0123456789", 10);

  char b64dec[ base64_dec_len(b64enc, sizeof(b64enc))];
  base64_decode(b64dec, b64enc, sizeof(b64enc));
}


unsigned long loopcount = 0;
byte enc_iv[N_BLOCK] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }; // iv_block gets written to, provide own fresh copy...
byte dec_iv[N_BLOCK] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };


void loop() {

  Blynk.run();
  if (millis() - lastSendTime > interval) {
    Serial.println("Command counter is: " + String(command_counter));

    lastSendTime = millis();            // timestamp the message
    interval = 1000;    // 2-3 seconds
    msgCount++;
  }

  // parse for a packet, and call onReceive with the result:
  onReceive(LoRa.parsePacket());
}

void sendMessage(String outgoing) {
  LoRa.beginPacket();                   // start packet
  LoRa.print(outgoing);                 // add payload
  LoRa.endPacket();                     // finish packet and send it
  msgCount++;                           // increment message ID
}

void onReceive(int packetSize) {
  if (packetSize == 0) return;          // if there's no packet, return

  // read packet header bytes:
  String incoming = "";

  while (LoRa.available()) {
    incoming += (char)LoRa.read();
  }

  Serial.println("Message: " + incoming);
  if(incoming == "0"){
      
    incoming = "Garaža je otvorena";
    
    }else if(incoming = "1"){

      incoming = "Garaža je zatvorena";
      
      }else if (incoming = "ACK"){

      incoming = "Otvaram garažu!";        
        
        }
  Blynk.virtualWrite(Vxxxx, incoming);
  Serial.println("RSSI: " + String(LoRa.packetRssi()));
  Serial.println();
}


String getMessage(int packetSize) {
  if (packetSize == 0) return "-1";          // if there's no packet, return

  // read packet header bytes:
  String incoming = "";

  while (LoRa.available()) {
    incoming += (char)LoRa.read();
  }

  return incoming;

}



void toggle_garage() {

  Serial.println("\n\n*********************************\n__________________________");
  Blynk.virtualWrite(Vxxxxx, "Pokušavam otvoriti garažu");

  bool ack = false;
  String response;
  String message = "toggle" + String(command_counter);
  String encrypted_message = encode(message);
  reset_iv();

  long resend_interval = 100;    // 2-3 seconds
  long lst = 0;
  long timeout_start = millis();
  long timeout = 2000;
  
  while (not ack and millis()-timeout_start < timeout ) {
    if (millis() - lst > resend_interval) {
      sendMessage(encrypted_message);
      lst = millis();            // timestamp the message
    }
    
    response = getMessage(LoRa.parsePacket());

    if (response != "-1") {

        String hash = response + hash_key;
        hash = sha1(hash);
        Serial.println("hashyyy");
        Serial.println(hash);
        sendMessage(hash);

        delay(10);
        //sad bi trebalo cekat hash ack
        
        lst = millis();
        while(not ack and (millis()-lst)< 1001){
          //we wait for 1s for ack
          response = getMessage(LoRa.parsePacket());
          if(response == "ACK"){
            Blynk.virtualWrite(Vxxxxx, "Otvaram/zatvaram garažu!");
            ack = true;
            Serial.println("We got confirmation");
            command_counter++;
            delay(300);
            
            }
          }
    }
  }

  if (not ack){
    
     Blynk.virtualWrite(Vxxxxx, "Neuspjelo otvaranje, stisni opet");
    
    }

  
}


String encode(String msg){
  
  sprintf(cleartext, "%s", msg.c_str());
  Serial.println("Function cleartext version: "+ String(cleartext));
  uint16_t clen = String(cleartext).length();
  Serial.println("Function clen: " + String(clen));
  String encrypted = encrypt(cleartext, clen, enc_iv);
  return encrypted;

  
  }

String decode(String msg){

    uint16_t dlen = msg.length();
    sprintf(ciphertext, "%s", msg.c_str());    
    String decrypted = decrypt( ciphertext, dlen, dec_iv);
    return decrypted;
  
  }


void reset_iv(){
  
  
    for (int i = 0; i < 16; i++) {
      enc_iv[i] = 0;
      dec_iv[i] = 0;
    }
  
  }


//encryption functions
// Generate IV (once)
void aes_init() {
  Serial.println("gen_iv()");
  aesLib.gen_iv(aes_iv);
  Serial.println("encrypt()");
  Serial.println(encrypt(strdup(plaintext.c_str()), plaintext.length(), aes_iv));
}

String encrypt(char * msg, uint16_t msgLen, byte iv[]) {
  int cipherlength = aesLib.get_cipher64_length(msgLen);
  char encrypted[cipherlength]; // AHA! needs to be large, 2x is not enough
  aesLib.encrypt64(msg, msgLen, encrypted, aes_key, sizeof(aes_key), iv);
  Serial.print("encrypted = "); Serial.println(encrypted);
  return String(encrypted);
}

String decrypt(char * msg, uint16_t msgLen, byte iv[]) {
  char decrypted[msgLen];
  aesLib.decrypt64(msg, msgLen, decrypted, aes_key, sizeof(aes_key), iv);
  return String(decrypted);
}
