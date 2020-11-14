#include <SPI.h>              // include libraries
#include <LoRa.h>
#include "AESLib.h"
#include <Arduino.h>
#include <Hash.h>


AESLib aesLib;

char cleartext[256];
char ciphertext[512];

// AES Encryption Key
byte aes_key[] = "####################################";
// General initialization vector (you must use your own IV's in production for full security!!!)
byte aes_iv[N_BLOCK] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

String plaintext = "HELLO WORLD!";
String hash_key = "hai";

const int csPin = D8;          // LoRa radio chip select
const int resetPin = D3;       // LoRa radio reset
const int irqPin = D4;         // change for your board; must be a hardware interrupt pin

const int status_pin = D2;
const int relay_pin = D1;
// count of outgoing messages
int interval = 2000;          // interval between sends
long lastSendTime = 0;        // time of last packet send

long last_ack = 0;

void setup() {
  Serial.begin(9600);                   // initialize serial
  while (!Serial);

  Serial.println("Garage Node Started!");
  LoRa.setPins(csPin, resetPin, irqPin);// set CS, reset, IRQ pin

  if (!LoRa.begin(433E6)) {             // initialize ratio at 433 MHz
    Serial.println("LoRa init failed. Check your connections.");
    while (true);                       // if failed, do nothing
  }

  LoRa.setSyncWord(0xF3);
  Serial.println("LoRa init succeeded.");
  digitalWrite(relay_pin, LOW);
  pinMode(relay_pin, OUTPUT);
  pinMode(status_pin, INPUT_PULLUP);


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


byte enc_iv[N_BLOCK] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }; // iv_block gets written to, provide own fresh copy...
byte dec_iv[N_BLOCK] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };



void loop() {


  if ( millis() - lastSendTime > interval) {

    Serial.println("Command counter is: " + String(last_ack));
    String message = String( digitalRead(status_pin)  );
    sendMessage(message);
    Serial.println("Sending " + message);
    lastSendTime = millis();            // timestamp the message
    interval = random(2000) + 1000;    // 2-3 seconds

  }
  // parse for a packet, and call onReceive with the result:
  onReceive(LoRa.parsePacket());
}


void sendMessage(String outgoing) {

  LoRa.beginPacket();                   // start packet
  LoRa.print(outgoing);                 // add payload
  LoRa.endPacket();                     // finish packet and send it

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


void onReceive(int packetSize) {
  if (packetSize == 0) return;          // if there's no packet, return

  // read packet header bytes:
  String incoming = "";

  while (LoRa.available()) {
    incoming += (char)LoRa.read();
  }

  String decoded = decode(incoming);
  reset_iv();
  int len = decoded.length();
  long sync_value = atol(decoded.substring(6, len).c_str());

  
  if (decoded.substring(0, 6) == "toggle" ) {
    int len = decoded.length();
    String ostatak = decoded.substring(6, len);
    Serial.println(ostatak);

    //create challenge and wait for correct solution
    bool good_hit = hash_challenge();

    if (good_hit) {
      toggle();
      last_ack = sync_value;
    } else {
      Serial.println("Challenge failed!");
    }
  } else {
    Serial.println("KRIVA NAREDBA: " + incoming);
  }
}


bool hash_challenge() {

  long trial_period = 1600; //master node has 1.6s to provide the answer, otherwise it cancels shake
  bool authenticated = false;
  long resend_interval = 128;
  long lst = 0;
  long trial_start = millis();
  String response;

  String gc =  String(random(2147483647), HEX)  ;
  String hash = gc + hash_key;
  hash = sha1(hash);

  while (not authenticated and (millis() - trial_start < trial_period)) {
    //resend hash if node missed it
    if (millis() - lst > resend_interval) {
      sendMessage(gc);
      lst = millis();
    }

    //check for messages from Master node
    response = getMessage(LoRa.parsePacket());
    if (response != "-1") {
      //there is some kind of answer
      if (response == hash) {
        //correct response, accept handshake
        Serial.println("Master node je kreirao tocan hash!");
        authenticated = true;
      }
    }
  }

  return authenticated;

}


void toggle() {

  //toggle garage door for 350ms and sends ACK every 100ms for ~ 1.11s

  long ts = millis();
  digitalWrite(relay_pin, HIGH);

  while (millis() - ts < 350) {
    sendMessage("ACK");
    delay(100);
  }

  digitalWrite(relay_pin, LOW);

  //send ack for 1 more second
  ts = 0;
  while (millis() - ts < 350) {
    sendMessage("ACK");
    delay(100);
  }
}




String encode(String msg) {

  sprintf(cleartext, "%s", msg.c_str());
  Serial.println("Function cleartext version: " + String(cleartext));
  uint16_t clen = String(cleartext).length();
  Serial.println("Function clen: " + String(clen));
  String encrypted = encrypt(cleartext, clen, enc_iv);
  return encrypted;
}

String decode(String msg) {

  uint16_t dlen = msg.length();
  sprintf(ciphertext, "%s", msg.c_str());
  String decrypted = decrypt( ciphertext, dlen, dec_iv);
  return decrypted;

}

void reset_iv() {
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
  char encrypted[cipherlength];
  aesLib.encrypt64(msg, msgLen, encrypted, aes_key, sizeof(aes_key), iv);
  Serial.print("encrypted = "); Serial.println(encrypted);
  return String(encrypted);
}

String decrypt(char * msg, uint16_t msgLen, byte iv[]) {
  char decrypted[msgLen];
  aesLib.decrypt64(msg, msgLen, decrypted, aes_key, sizeof(aes_key), iv);
  return String(decrypted);
}
