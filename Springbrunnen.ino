
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

extern "C" {
#include "user_interface.h"
}

#ifndef STASSID
#define STASSID "SSID"
#define STAPSK  "Wlan-Key"
#endif

//D1 -> Relay 1
//D2 -> Relay 2

//Loxone //////////////////////////////
const /*PROGMEM*/ IPAddress loxone(192, 168, 177, XXX);//Anpassen auf Loxone
const /*PROGMEM*/ int loxonePort = 7001; //Port der Loxone

// Wlan //////////////////////////
const char* ssid     = STASSID;
const char* password = STAPSK;

//ESP Daten ///////////////////////////
const /*PROGMEM*/ IPAddress ip (192, 168, 177, XXX); //Adresse des Arduino
const /*PROGMEM*/ int localPort = 8888;      // local port to listen on

// buffers for receiving and sending data
char packetBuffer[UDP_TX_PACKET_MAX_SIZE]; //buffer to hold incoming packet,

////Globals /////////////////////////
byte relaisStatus = B00000000;  //Erststatus der Relais
bool initUDPState = false;
bool relaisChange = false; //Zeigt an ob ein Relais geschaltet wurde
bool relaisSend = false; //Zeigt an ob Wert gesendet wurde
bool lockState = false; // Soll verhindern, dass mehrfaches senden die Relays "irritiert"
bool firstRunRelais = true;  //Für den ersten durchgang

//Timer //////////////////////
os_timer_t myTimer;
bool tickOccured;
int Counter = 0;           // Argument für die Callback-Funktion

// UDP /////////////////////
WiFiUDP Udp;

void setup() {
  //Serial Monitor
  Serial.begin(115200);
  
  // We start by connecting to a WiFi network

  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  /* Explicitly set the ESP8266 to be a WiFi-client, otherwise, it by default,
     would try to act as both a client and an access-point and could cause
     network-issues with your other WiFi-devices on your WiFi-network. */
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.printf("UDP server on port %d\n", localPort);
  Udp.begin(localPort); //Startet UDP-Service

  // Setzen der Digitalen Ausgänge
  pinMode(D1, OUTPUT);
  digitalWrite(D1, HIGH);
  pinMode(D2, OUTPUT);
  digitalWrite(D2, HIGH);

  // Timer aktivieren
  tickOccured = false;
  os_timer_setfn(&myTimer, timerCallback, &Counter);
  os_timer_arm(&myTimer, 60000, true); //millisekunden
}

// start of timerCallback
void timerCallback(void *pArg) {

      tickOccured = true;
      *((int *) pArg) += 1;

} // End of timerCallback

//Schalten der Relais anhand des Byte-Wertes (Byte-Stelle 0= arduino Port 1 Byte-Stelle 7 = Arduino Port 8)
void relaisSwitch()
{
  for (int i = 0; i < 8; i++)
  {
    bool theBit = bitRead(relaisStatus, i);
    //Serial.print("TheBit: ");
    //Serial.println(theBit);
    int relaisAusgang = i + 1; //Anpassung an Arduino Port
    //Serial.print("RelaisAusgang: ");
    //Serial.println(relaisAusgang);
    switch (relaisAusgang) {  //Da nur zwei Relais verwendet werden mittels case
      case 1:
        if (theBit)
        {
          digitalWrite(D1, LOW);
        } else {
          digitalWrite(D1, HIGH);
        }
        break;
      case 2:
        if (theBit)
        {
          digitalWrite(D2, LOW);
        } else {
          digitalWrite(D2, HIGH);
        }
        break;      
    }
  }
}

//Schreiben des Bytes der Relais
//Relais sind als Byte hinterlegt (8Bit)
void relaisByteWriter(int relais) //Werte von 0-7 entsprechen 1-8 der Relais
{
  //Serial.println(relais);
  bool bitChanged = false;  //Haben sich die Bits geändert
  //Ändert den Status des Relais (An = 11 oder Aus = 10)

    // Schaltwert identifizieren
    int schaltwert = relais% 10;
    //Serial.print("Schaltwert: ");
    //Serial.println(schaltwert); 
    
    // Relais identifizieren
    int relaiswert = (relais / 10) % 10;
    //Serial.print("Relaiswert: ");
    //Serial.println(relaiswert);
    
    if (schaltwert == 0)
    {
      bitWrite(relaisStatus, relaiswert, LOW);
    }
    else
    {
      bitWrite(relaisStatus, relaiswert, HIGH);
    }
      
    relaisChange = true;  // Relais haben sich geänder
    relaisSend = false;   // Status wurde noch ncith gesendet
    bitChanged = true;

  if (bitRead(relaisStatus, relais) == HIGH && relais != 0 && relais <= 8 && !bitChanged)
  {
    Serial.println("HIGH");
    bitWrite(relaisStatus, relais, LOW); //Relais aus
    relaisChange = true;
    relaisSend = false;
  }
}

//Sendet Relais Status
bool sendRelais()
{
  bool success;
  int laenge;
  int ergebniss;
  String udprelais = "Relais_";
  udprelais.concat(relaisStatus);
  laenge = udprelais.length() + 1;
  char buf2[laenge];
  udprelais.toCharArray(buf2, laenge);
  success = Udp.beginPacket(loxone, loxonePort);
  if (!success)
  {
    return false;
  }
  else
  {
    ergebniss = Udp.write(buf2);
    success = Udp.endPacket();
  }

  if (ergebniss > 0)
  {
    lockState = false;
    return true;
  }

  return false;
}

// Testet ob eine Zahl gesedet wird, für das Schalten der Relais
int verify(char * string)
{
  int x = 0;
  int len = strlen(string);
  while (x < len) {
    if (!isdigit(*(string + x)))
      return 1;
    ++x;
  }

  return 0;
}

//Lesen des UDP Streams
void readUDP()
{
  int packetSize = Udp.parsePacket();
  char packetBuffer[packetSize + 1];
  if (packetSize > 0)
  {
    if (Udp.remoteIP() == loxone)
    {
      Udp.read(packetBuffer, packetSize + 1);
      //Serial.print(("received: "));
      //Serial.println(packetBuffer);
  
      if (verify(packetBuffer) && !lockState)
      {
        int relais = atoi(packetBuffer);
        //passt den zähler an byte an
        //Serial.print("INT: ");
        //Serial.println(relais);
        relaisByteWriter(relais);
        relaisSwitch();
      } 
    } else {Serial.println("Not Loxone!"); }
  }
}


void loop() {
  //Senden des RelaisStatus
  if (((relaisChange && !relaisSend) || firstRunRelais) || tickOccured)
  {
    bool successRelais = sendRelais();
    //Serial.println("Senden Relais");
    if (successRelais)
    {
      relaisSend = true;
      tickOccured = false;
      if (firstRunRelais)
      {
        sendRelais(); //extra zweimal senden damit startwert vorhanden ist
        //Serial.println("Senden Relais twice");
        firstRunRelais = false;
      }
    }
    else
    {
      relaisSend = false;
    }
  }

  readUDP(); //Lesen der Daten von Loxone
}
