#include <SuplaDevice.h>
#include <supla/network/esp32_wifi.h>

#define MY_DEVICE_NAME     "TEST ESP"
#define MY_WIFI_NAME "SUPLA-TEST-ESP"
// nazwa sieci WIFI
#define WIFI_SSID "____fill_in____"
// hasło do wifi
#define WIFI_PASS "____fill_in____"
// Adres Twojego serwera Supla (np. svr24.supla.org lub IP własnej instancji)
#define SUPLA_SRV  "svrXX.supla.org"
// Adres e-mail przypisany do Twojego konta Supla Cloud
#define SUPLA_EML "____fill_in____"

const char GUID[SUPLA_GUID_SIZE] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};
const char AUTHKEY[SUPLA_AUTHKEY_SIZE] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};
// Definiujemy maksymalny rozmiar bufora startowego w bajtach (RAM)

class TelnetLog {
  #define LOG_BUFFER_SIZE 32768   // ustaw tak duży jak możliwe

  private:
    inline static NetworkServer telnetServer = NetworkServer(23);
    inline static NetworkClient telnetClient;
    inline static bool telnetInitialized = false;
    inline static bool pause = false;

    inline static uint8_t myLog[LOG_BUFFER_SIZE];
    inline static uint32_t myLogHead = 0, myLogTail = 0;
    inline static uint32_t totalRead = 0, totalWrite = 0;
  public:
    static void getLogStatusDescription(char* result, int maxLen) {
      snprintf(result, maxLen, ">>> myLogTail=%d myLogHead=%d totalRead=%d totalWrite=%d", myLogTail, myLogHead, totalRead, totalWrite);
    }

    static void onSerialReceive() {
      uint8_t buffer[256];
      for(int bytesAvailable = Serial.available(); bytesAvailable>0;) {
        if (LOG_BUFFER_SIZE-myLogHead==0) {
          myLogHead = 0;
          if (myLogTail==0) myLogTail=1;
        }
        int bytesRead = Serial.read(buffer, sizeof(buffer));
        if (memcmp(buffer, "SRPC", 4)) { 
          if (bytesRead > LOG_BUFFER_SIZE-myLogHead) {
            int previousHead = myLogHead;
            memcpy(myLog+myLogHead, buffer, LOG_BUFFER_SIZE-myLogHead);
            memcpy(myLog, buffer+LOG_BUFFER_SIZE-myLogHead, bytesRead-LOG_BUFFER_SIZE+myLogHead);
            myLogHead = (myLogHead+bytesRead) % LOG_BUFFER_SIZE;
            if (myLogTail>previousHead || myLogTail<=myLogHead)
              myLogTail = myLogHead+1;
          }
          else {
            memcpy(myLog+myLogHead, buffer, bytesRead);
            myLogHead += bytesRead;
            if (myLogTail<=myLogHead && myLogTail>myLogHead-bytesRead)
              myLogTail = myLogHead+1;
            if (myLogTail>=LOG_BUFFER_SIZE)
              myLogTail = 0;
          }
          totalRead += bytesRead;  
        }
        bytesAvailable -= bytesRead; 
      }
    }

    static void iterate() {   
      if (WiFi.status() == WL_CONNECTED && !telnetInitialized) {
        telnetServer.begin();
        telnetServer.setNoDelay(true);
        Serial.println("SERWER TELNET GOTOWY");
        telnetInitialized = true;
      }
      if (telnetInitialized) {
        if (telnetServer.hasClient()) { // czy klient się podłączył
          if (!telnetClient || !telnetClient.connected()) {
            if (telnetClient) telnetClient.stop(); // Rozłącz starego klienta, jeśli wisiał
            telnetClient = telnetServer.available();
            telnetClient.println("--- Połączono z konsolą logów ESP32 przez Telnet ---");
            telnetClient.println("--- ctrl+p, enter = pauza/wznowienie ---");
            telnetClient.println("--- ctrl+i, enter = statystyki loga ---");         
          } else {
            // Odrzuć kolejne połączenie, jeśli jedno jest już aktywne
            telnetServer.available().stop();
          }    
        }
          // Wypychanie danych z bufora RAM do telnet
        if (!pause && myLogHead!=myLogTail && telnetClient && telnetClient.connected()) {
          if (myLogTail>=LOG_BUFFER_SIZE)
            myLogTail=0;
          uint32_t dataToSend = (myLogTail<myLogHead)? myLogHead - myLogTail : LOG_BUFFER_SIZE - myLogTail ;
          telnetClient.write(myLog+myLogTail, dataToSend);
          myLogTail += dataToSend;
          totalWrite += dataToSend;
          if (myLogTail == LOG_BUFFER_SIZE)
            myLogTail = 0;
        }
        // obsługa wejścia
        while (telnetClient && telnetClient.available()) {
          int8_t ch = telnetClient.read();
          if (ch==16) {     // ctrl + p
            pause = !pause;
            telnetClient.println(pause?"PAUZA":"WZNOWIENIE");
          }
          if (ch==9) {     // ctrl + i
            char buf[128];
            getLogStatusDescription(buf, sizeof (buf));
            telnetClient.println(buf);
          }
        }
      }
    }
};

void setup() { 
  Serial.begin(115200);
  uart_internal_loopback(0, 3);   // kopiuj wszystkie logi z UART TX to RX (GPIO 3)
  Serial.onReceive(TelnetLog::onSerialReceive);  // ustaw callback for wszystkich notyfikacji RX
  delay(500);
  SUPLA_LOG_DEBUG("START");
  SuplaDevice.setName(MY_DEVICE_NAME);
  new Supla::ESPWifi(WIFI_SSID, WIFI_PASS);

  SuplaDevice.begin(
    GUID,               // Globalny identyfikator urządzenia
    SUPLA_SRV,  
    SUPLA_EML, 
    AUTHKEY             // Klucz autoryzacji urządzenia
  );
  SUPLA_LOG_DEBUG("INICJALIZACJA ZAKONCZONA");
}

auto last = millis();
void loop() {
  SuplaDevice.iterate();
  TelnetLog::iterate();
  if (last+2000<millis()) {
    char descr[128];
    TelnetLog::getLogStatusDescription(descr, sizeof(descr));
    SUPLA_LOG_DEBUG(descr);
    last = millis();
  }
}
