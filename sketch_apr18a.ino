#include <Arduino.h>
#include <WiFi.h>
#include <BluetoothSerial.h>
#include <Preferences.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_camera.h"
#include <ESP32Servo.h>
#include "SPI.h"
#include <ArduinoJson.h>
#include <UniversalTelegramBot.h>
#include <WiFiClientSecure.h>
#include <WiFiClient.h>

// PINS
#define TRIGGER_PIN 13
#define ECHO_PIN 15
#define SERVO_PIN 12

Servo gateServo;

Preferences preferences;

BluetoothSerial SerialBT;


// Camera GPIO
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

String ssid = "";
String password = "";
String cc = "";
String serverName = "192.168.43.144";//"192.168.43.144";     // IP del server
unsigned char configurato = 0;
int serverPort = 5000;
String serverPath = "/upload";
String adminKey = "";
String plate = "";
unsigned char secondi_cancello_aperto = 2;
float maxDistance = 200;
float minDistance = 100;
const char* botApiToken = "7415558691:AAFbq7oDwphdeNWGmEi5Ju5KFmjNJEJh-yA";
const unsigned long intervallo_bot = 1000;
unsigned long ultima_chiamata_telegram_bot = 0;
WiFiClient client;
WiFiClientSecure secureClient;
bool isPictureTaken = false;
bool gateClosed = true;
int count = 0;
unsigned char aperture_cancello = 0;
unsigned char tentativi_falliti = 0;
String last_plate_found = "N/A";
int durata_ciclo_cancello = 15000;


UniversalTelegramBot bot(botApiToken, secureClient);

void saveCC(String countrycode){
  preferences.begin("cc", false);  // false = modalità read/write
  preferences.putString("cc", countrycode);
  preferences.end();
}

String loadCC(){
  String cc;
  preferences.begin("cc", true);  // true = modalità sola lettura

  cc = preferences.getString("cc", "");
  preferences.end();
  return cc;
}

void saveCicloCancello(int ciclo_cancello){
  preferences.begin("cancello", false);  // false = modalità read/write
  preferences.putInt("ciclo_cancello", ciclo_cancello);
  preferences.end();
  delay(1000);
}

void loadCicloCancello(){
  preferences.begin("cancello", true);  // true = modalità sola lettura
  durata_ciclo_cancello = preferences.getInt("ciclo_cancello");
  preferences.end();
  delay(1000);
}

unsigned long distanzaStabileDa = 0;
long ultimaDistanza = -1;
unsigned long sogliaTempoFermo = 1500; // ms
int tolleranza = 5; // cm di tolleranza per dire "fermo"

void saveMetrics(unsigned char tentativi_falliti, unsigned char aperture_cancello, float maxDistance, float minDistance, String last_plate_found, unsigned long sogliaTempoFermo, int tolleranza){
  preferences.begin("metrics", false);  // false = modalità read/write
  preferences.putUChar("falliti", tentativi_falliti);
  preferences.putUChar("aperture", aperture_cancello);
  preferences.putFloat("max", maxDistance);
  preferences.putFloat("min", minDistance);
  preferences.putString("lastPlate", last_plate_found);
  preferences.putULong("sogliaTempoFermo", sogliaTempoFermo);
  preferences.putInt("tolleranza", tolleranza);
  preferences.end();
  delay(1000);
}

void loadMetrics(unsigned char &tentativi_falliti, unsigned char &aperture_cancello, float &maxDistance, float &minDistance, String &last_plate_found, unsigned long &sogliaTempoFermo, int &tolleranza){
  preferences.begin("metrics", true);  // true = modalità sola lettura

  tentativi_falliti = preferences.getUChar("falliti", 1);
  aperture_cancello = preferences.getUChar("aperture", 1);
  maxDistance = preferences.getFloat("max");
  minDistance = preferences.getFloat("min");
  last_plate_found = preferences.getString("lastPlate", "N/A");
  sogliaTempoFermo = preferences.getULong("sogliaTempoFermo");
  tolleranza = preferences.getInt("tolleranza");
  preferences.end();
  delay(1000);
}

void saveWiFiConfig(String ssid, String password, unsigned char configurato, String plate, String adminkey) {
  preferences.begin("wifi_config", false);  // false = modalità read/write

  preferences.putString("ssid", ssid);
  preferences.putString("password", password);
  preferences.putUChar("configurato", configurato);
  preferences.putString("plate", plate);
  preferences.putString("admin", adminkey);

  preferences.end();
  Serial.println("😎👍 Configurazione WiFi salvata su Preferences.");
}

void loadWiFiConfig(String &ssid, String &password, unsigned char &configurato, String &plate, String &admin) {
  preferences.begin("wifi_config", true);  // true = modalità sola lettura

  ssid = preferences.getString("ssid", "");
  password = preferences.getString("password", "");
  configurato = preferences.getUChar("configurato", 0);
  plate = preferences.getString("plate", "");
  admin = preferences.getString("admin", "");

  preferences.end();
  Serial.println("😎👍 Configurazione WiFi letta da Preferences.");
}

unsigned char ConfiguratoState() {
  preferences.begin("wifi_config", true);
  unsigned char stato = preferences.getUChar("configurato", false);
  preferences.end();
  return stato;
}

void resetWiFiConfig() {
  preferences.begin("wifi_config", false);
  preferences.clear();  // Cancella tutte le chiavi nel namespace
  preferences.end();
  Serial.println("⚠️ Configurazione WiFi resettata.");
}

void sendPlateToServer(String plate, String adminKey) {
  const char* server = "192.168.43.144";//"192.168.1.42";  // IP del server Flask
  const int port = 5000;

  if (!client.connect(server, port)) {
    Serial.println("⛔ Impossibile connettersi al server");
    return;
  }

  String payload = "{\"plate\":\"" + plate + "\",\"password\":\"" + adminKey + "\",\"admin\":true}";

  client.println("POST /addplate HTTP/1.1");
  client.println("Host: 192.168.43.144");
  client.println("Content-Type: application/json");
  client.print("Content-Length: ");
  client.println(payload.length());
  client.println();
  client.println(payload);

  while (client.connected() || client.available()) {
    if (client.available()) {
      String line = client.readStringUntil('\n');
      Serial.println(line);
    }
  }

  client.stop();
  Serial.println("😎👍 Dati inviati al server");
}

void waitForBluetoothWiFiConfig() {
  loadMetrics(tentativi_falliti, aperture_cancello, maxDistance, minDistance, last_plate_found, sogliaTempoFermo, tolleranza);
  SerialBT.begin("ESP32CAM_Config");
  
  Serial.println("🔄 In attesa connessione Bluetooth...");

  while (!SerialBT.hasClient()) {
    delay(100);  // Attendi effettiva connessione
  }

  Serial.println("🔗 Dispositivo Bluetooth connesso");
  Serial.println("🔄 In attesa credenziali via Bluetooth (SSID=...;PASS=...;PLATE=...;ADMIN=...;MAX=...;MIN=...;CC=...;T=...)");
  SerialBT.println("🔄 In attesa credenziali via Bluetooth (SSID=...;PASS=...;PLATE=...;ADMIN=...;MAX=...;MIN=...;CC=...;T=...)");
  SerialBT.println("**ATTENZIONE: Inserire le distanze MAX e MIN in centimetri e T in millisecondi**"); 

  while (true) {
    if (SerialBT.available()) {
      String input = SerialBT.readStringUntil('\n');
      input.trim();

      if (
        input.indexOf("SSID=") != -1 &&
        input.indexOf(";PASS=") != -1 &&
        input.indexOf(";PLATE=") != -1 &&
        input.indexOf(";ADMIN=") != -1 &&
        input.indexOf(";MAX=") != -1 &&
        input.indexOf(";MIN=") != -1 &&
        input.indexOf(";CC=") != -1 &&
        input.indexOf(";T=") != -1
      ) {
        int passIndex  = input.indexOf(";PASS=");
        int plateIndex = input.indexOf(";PLATE=");
        int adminIndex = input.indexOf(";ADMIN=");
        int maxIndex = input.indexOf(";MAX=");
        int minIndex = input.indexOf(";MIN=");
        int ccIndex = input.indexOf(";CC=");
        int ciclocancelloIndex = input.indexOf(";T=");

        ssid     = input.substring(5, passIndex);
        password = input.substring(passIndex + 6, plateIndex);
        plate    = input.substring(plateIndex + 7, adminIndex);
        adminKey = input.substring(adminIndex + 7, maxIndex);
        maxDistance = input.substring(maxIndex + 5, minIndex).toFloat();
        minDistance = input.substring(minIndex + 5, ccIndex).toFloat();
        String cc = input.substring(ccIndex + 4, ciclocancelloIndex);
        durata_ciclo_cancello = input.substring(ciclocancelloIndex + 3).toInt();

        SerialBT.println("😎👍 Ricevuto:");
        SerialBT.println("SSID: " + ssid);
        SerialBT.println("PASS: " + password);
        SerialBT.println("PLATE: " + plate);
        SerialBT.println("ADMIN: " + adminKey);
        SerialBT.println("MAX: " + String(maxDistance) + " cm");
        SerialBT.println("MIN: " + String(minDistance) + " cm");
        SerialBT.println("CC: " + cc);

        // Salva tutto nelle Preferences
        preferences.begin("wifi_config", false);
        preferences.putString("ssid", ssid);
        preferences.putString("password", password);
        preferences.putString("plate", plate);
        preferences.putString("admin", adminKey);
        preferences.putUChar("configurato", 1);
        preferences.end();

        saveCC(cc);
        
        saveMetrics(tentativi_falliti, aperture_cancello, maxDistance, minDistance, last_plate_found, sogliaTempoFermo, tolleranza);
        
        saveCicloCancello(durata_ciclo_cancello);
        
        SerialBT.println("😎👍 Dati configurazione salvati. 🪛🤖 Per qualunque modifica contattare il Bot Telegram @LabMakingBot");
        Serial.println("😎👍 Dati configurazione salvati su Preferences. 🪛🤖 Per qualunque modifica contattare il Bot Telegram @LabMakingBot");
        break;
      } else {
        SerialBT.println("⛔ Formato non valido. Usa:");
        SerialBT.println("SSID=xxx;PASS=yyy;PLATE=zzz;ADMIN=kkk;MAX=ttt;MIN=www;CC=uu;T=ttt");
      }
    }
    delay(100);
  }
}


void gestisci_messaggi(int num_nuovi_messaggi) {
  String emptyString = "";
  for (int i = 0; i < num_nuovi_messaggi; i++) {
    String chat_id = bot.messages[i].chat_id;
    String text = bot.messages[i].text;
    String from_name = bot.messages[i].from_name;
    if (from_name == emptyString) from_name = "Sconosciuto";

    if (text == "/start") {
      bot.sendMessage(chat_id, "Benvenuto. Usa /addplate <TARGA> <PASSWORD> <ADMIN KEY> o /removeplate <TARGA> <ADMIN KEY> oppure /opengate <PASSWORD> o /opengate <ADMIN KEY>\n Per aggiornare il country code della targa usare /cc <CODICE> <ADMIN KEY>\nCodici disponibili:\n\t eu <-- European Union\n\t gb <-- United Kingdom\n\t us <-- USA\nUsa /d <DISTANZA MAX IN cm> <DISTANZA MIN IN cm> <ADMIN KEY> per aggiornare la massima e minima distanza operativa\n");
      bot.sendMessage(chat_id, "Usa /wifi <ssid> <password> <CHIAVE> per aggiornare le credenziali wifi.\nPer aggiornare la durata del ciclo apertura/chiusura del cancello code usare\n\n\n /cancello <DURATA ms> <ADMIN KEY>\nPer aggiornare la tolleranza usare\n\n\n /tolleranza <DISTANZA cm> <ADMIN KEY>");
    }

    if ( text == "/help"){
      bot.sendMessage(chat_id, "Usa /addplate <TARGA> <PASSWORD> <ADMIN KEY> o /removeplate <TARGA> <ADMIN KEY> oppure /opengate <PASSWORD> o /opengate <ADMIN KEY>\n Per aggiornare il country code della targa usare /cc <CODICE> <ADMIN KEY>\nCodici disponibili:\n\t eu <-- European Union\n\t gb <-- United Kingdom\n\t us <-- USA\nUsa /d <DISTANZA MAX IN cm> <DISTANZA MIN IN cm> <ADMIN KEY> per aggiornare la massima e minima distanza operativa\n");
      bot.sendMessage(chat_id, "Usa /wifi <ssid> <password> <CHIAVE> per aggiornare le credenziali wifi.Per aggiornare la durata del ciclo apertura/chiusura del cancello code usare\n\n\n /cancello <DURATA ms> <ADMIN KEY>");
    }

    // /addplate TARGA CHIAVE
    if (text.startsWith("/addplate ")) {
      int firstSpace = text.indexOf(' ');
      int secondSpace = text.indexOf(' ', firstSpace + 1);
      int thirdSpace = text.indexOf(' ', firstSpace + 2);

      if (firstSpace > 0 && secondSpace > firstSpace) {
        String plate = text.substring(firstSpace + 1, secondSpace);
        String password = text.substring(secondSpace + 1, thirdSpace);
        String key = text.substring(thirdSpace + 1);

        if (isAdminKeyValid(key)) {
          if (!plateEsiste(plate)) {
            aggiungiTarga(plate, password);
            bot.sendMessage(chat_id, "Targa aggiunta: " + plate);
          } else {
            bot.sendMessage(chat_id, "Targa o password già esistenti.");
          }
        } else {
          bot.sendMessage(chat_id, "Chiave non valida.");
        }
      } else {
        bot.sendMessage(chat_id, "Formato errato. Usa /addplate TARGA PASSWORD CHIAVE");
      }
    }

    // /cc countrycode chiave
    if (text.startsWith("/cc ")) {
      int firstSpace = text.indexOf(' ');
      int secondSpace = text.indexOf(' ', firstSpace + 1);
      int thirdSpace = text.indexOf(' ', firstSpace + 2);

      if (firstSpace > 0 && secondSpace > firstSpace) {
        String cc = text.substring(firstSpace + 1, secondSpace);
        String key = text.substring(secondSpace + 1);

        if (isAdminKeyValid(key)) {
          if (cc == "eu" || cc == "gb" || cc == "us") {
            saveCC(cc);
            bot.sendMessage(chat_id, "Country Code aggiornato");
          } else {
            bot.sendMessage(chat_id, "Country code inesistente\n\tCodici disponibili:\n\t\t eu <-- European Union\n\t\t gb <-- United Kingdom\n\t\t us <-- USA\n");
          }
        } else {
          bot.sendMessage(chat_id, "Chiave non valida.");
        }
      } else {
        bot.sendMessage(chat_id, "Formato errato.\nPer aggiornare il country code della targa usare\n\n\n /cc <CODICE> <ADMIN KEY> \n\n\nCodici disponibili:\n\n\t eu <-- European Union\n\t gb <-- United Kingdom\n\t us <-- USA\n");
      }
    }

    // /cancello t[ms] chiave
    if (text.startsWith("/cancello ")) {
      int firstSpace = text.indexOf(' ');
      int secondSpace = text.indexOf(' ', firstSpace + 1);
      int thirdSpace = text.indexOf(' ', firstSpace + 2);

      if (firstSpace > 0 && secondSpace > firstSpace) {
        String cancello = text.substring(firstSpace + 1, secondSpace);
        String key = text.substring(secondSpace + 1);

        if (isAdminKeyValid(key)) {
          durata_ciclo_cancello = cancello.toInt();
          saveCicloCancello(durata_ciclo_cancello);
          bot.sendMessage(chat_id, "Durata ciclo cancello aggiornata a " + cancello + " ms");
        } else {
          bot.sendMessage(chat_id, "Chiave non valida.");
        }
      } else {
        bot.sendMessage(chat_id, "Formato errato.\nPer aggiornare la durata del ciclo apertura/chiusura del cancello code usare\n\n\n /cancello <DURATA ms> <ADMIN KEY>");
      }
    }

    // /tolleranza d[cm] chiave
    if (text.startsWith("/tolleranza ")) {
      int firstSpace = text.indexOf(' ');
      int secondSpace = text.indexOf(' ', firstSpace + 1);
      int thirdSpace = text.indexOf(' ', firstSpace + 2);

      if (firstSpace > 0 && secondSpace > firstSpace) {
        String d = text.substring(firstSpace + 1, secondSpace);
        String key = text.substring(secondSpace + 1);

        if (isAdminKeyValid(key)) {
          loadMetrics(tentativi_falliti, aperture_cancello, maxDistance, minDistance, last_plate_found, sogliaTempoFermo, tolleranza);
          tolleranza = d.toInt();
          saveMetrics(tentativi_falliti, aperture_cancello, maxDistance, minDistance, last_plate_found, sogliaTempoFermo, tolleranza);
          bot.sendMessage(chat_id, "Tolleranza aggiornata a " + d + " cm");
        } else {
          bot.sendMessage(chat_id, "Chiave non valida.");
        }
      } else {
        bot.sendMessage(chat_id, "Formato errato.\nPer aggiornare la tolleranza usare\n\n\n /tolleranza <DISTANZA cm> <ADMIN KEY>");
      }
    }

    // /tempofermo t[ms] chiave
    if (text.startsWith("/tempofermo ")) {
      int firstSpace = text.indexOf(' ');
      int secondSpace = text.indexOf(' ', firstSpace + 1);
      int thirdSpace = text.indexOf(' ', firstSpace + 2);

      if (firstSpace > 0 && secondSpace > firstSpace) {
        String t = text.substring(firstSpace + 1, secondSpace);
        String key = text.substring(secondSpace + 1);

        if (isAdminKeyValid(key)) {
          loadMetrics(tentativi_falliti, aperture_cancello, maxDistance, minDistance, last_plate_found, sogliaTempoFermo, tolleranza);
          sogliaTempoFermo = strtoul(t.c_str(), NULL, 10);
          saveMetrics(tentativi_falliti, aperture_cancello, maxDistance, minDistance, last_plate_found, sogliaTempoFermo, tolleranza);
          bot.sendMessage(chat_id, "Lunghezza dell'intervallo aggiornata a " + t + " ms");
        } else {
          bot.sendMessage(chat_id, "Chiave non valida.");
        }
      } else {
        bot.sendMessage(chat_id, "Formato errato.\nPer aggiornare la lunghezza dell'intervallo in cui l'oggetto risulta essere fermo\n\n\n /tempofermo <TEMPO ms> <ADMIN KEY>");
      }
    }

    // /d distanza ultrasuoni 
    if (text.startsWith("/d ")) {
      int firstSpace = text.indexOf(' ');
      int secondSpace = text.indexOf(' ', firstSpace + 1);
      int thirdSpace = text.indexOf(' ', secondSpace + 1);

    if (firstSpace > 0 && secondSpace > firstSpace && thirdSpace > secondSpace) {
        String maxStr = text.substring(firstSpace + 1, secondSpace);
        String minStr = text.substring(secondSpace + 1, thirdSpace);
        String key = text.substring(thirdSpace + 1);

        float max = maxStr.toFloat();
        float min = minStr.toFloat();

        if (isAdminKeyValid(key)) {
          if (min >= 0 || max <= 1000) {
            loadMetrics(tentativi_falliti, aperture_cancello, maxDistance, minDistance, last_plate_found, sogliaTempoFermo, tolleranza);
            maxDistance = max;
            minDistance = min;
            saveMetrics(tentativi_falliti, aperture_cancello, maxDistance, minDistance, last_plate_found, tolleranza, sogliaTempoFermo);
            delay(2000);
            bot.sendMessage(chat_id, "Distanze aggiornate. max: "+maxStr+" cm min: "+min+" cm ");
          } else {
            bot.sendMessage(chat_id, "Distanza non valida. Inserire una distanza compresa tra 0 e 10 m");
          }
        } else {
          bot.sendMessage(chat_id, "Chiave non valida.");
        }
      } else {
        bot.sendMessage(chat_id, "Formato errato.\nPer aggiornare la distanza a cui rilevare l'auto usare\n\n\n /d <DISTANZA MAX IN cm> <DISTANZA MIN IN cm> <ADMIN KEY>.\nDistanza massima 10 m\nDistanza minima 0 m");
      }
    }

    // /wifi cambia credenziali wifi
    if (text.startsWith("/wifi ")) {
      int firstSpace = text.indexOf(' ');
      int secondSpace = text.indexOf(' ', firstSpace + 1);
      int thirdSpace = text.indexOf(' ', firstSpace + 2);

      if (firstSpace > 0 && secondSpace > firstSpace) {
        String new_ssid = text.substring(firstSpace + 1, secondSpace);
        String new_password = text.substring(secondSpace + 1, thirdSpace);
        String key = text.substring(thirdSpace + 1);

        if (isAdminKeyValid(key)) {
            ssid = new_ssid;
            password = new_password;
            bot.sendMessage(chat_id, "Wifi aggiornato.");
            saveWiFiConfig(ssid, password, configurato, plate, adminKey);
          } else {
            bot.sendMessage(chat_id, "Chiave non valida");
          }
        } else {
        bot.sendMessage(chat_id, "Formato errato. Usa /wifi <ssid> <password> <CHIAVE>");
      }
    }
    // /removeplate TARGA CHIAVE
    if (text.startsWith("/removeplate ")) {
      int firstSpace = text.indexOf(' ');
      int secondSpace = text.indexOf(' ', firstSpace + 1);

      if (firstSpace > 0 && secondSpace > firstSpace) {
        String plate = text.substring(firstSpace + 1, secondSpace);
        String key = text.substring(secondSpace + 1);

        if (isAdminKeyValid(key)) {
          if (plateEsiste(plate)) {
            rimuoviTarga(plate);
            bot.sendMessage(chat_id, "Targa rimossa: " + plate);
          } else {
            bot.sendMessage(chat_id, "Targa non trovata.");
          }
        } else {
          bot.sendMessage(chat_id, "Chiave non valida.");
        }
      } else {
        bot.sendMessage(chat_id, "Formato errato. Usa /removeplate TARGA CHIAVE");
      }
    }

    //apre il cancello se c'è un errore
    if (text.startsWith("/opengate ")) {
      String key = text.substring(text.indexOf(" ") + 1);
    if (isPasswordValid(key)) {
      bot.sendMessage(chat_id, "Cancello in apertura...");
      openGate();
      last_plate_found = "N/A -- Opened via BOT";
    } else {
      bot.sendMessage(chat_id, "Chiave non valida.");
    }
  }
    // Comando /metrics
    if (text.startsWith("/metrics ")) {
      String password_utente = text.substring(9); // estrae la password dopo "/metrics "
      
      if (!isAdminKeyValid(password_utente)) {
        bot.sendMessage(chat_id, "Password non valida.");
      }

      // Converte timestamp in giorni/ore/minuti
      unsigned long now_ms = millis();  // tempo totale in millisecondi (da accensione)

      unsigned long uptime_days    = now_ms / 86400000;                 // 1000 * 60 * 60 * 24
      unsigned long uptime_hours   = (now_ms % 86400000) / 3600000;    // 1000 * 60 * 60
      unsigned long uptime_minutes = (now_ms % 3600000) / 60000;       // 1000 * 60
      unsigned long uptime_seconds = (now_ms % 60000) / 1000;
      unsigned long uptime_ms      = now_ms % 1000;                      // millisecondi residui
      //Carico le metriche
      loadMetrics(tentativi_falliti, aperture_cancello, maxDistance, minDistance, last_plate_found, sogliaTempoFermo, tolleranza);
      delay(2000);
      // Prepara messaggio
      String report = "📊 *Stato sistema:*\n\n\n";
      report += "• Uptime: " + String(uptime_days) + "g " + String(uptime_hours) + ":" + String(uptime_minutes) + ":"+String(uptime_seconds) + "."+String(uptime_minutes) + "\n";
      report += "• Max distanza operativa: " + String(maxDistance / 100) + " m\n";
      report += "• Min distanza operativa: " + String(minDistance / 100) + " m\n";
      report += "• Durata ciclo cancello: " + String(durata_ciclo_cancello*0.001) + " s\n";
      report += "• Tolleranza: " + String(tolleranza / 100) + " m\n";
      report += "• Soglia tempo fermo: " + String(sogliaTempoFermo*0.001) + " s\n";
      report += "• Country Code (Tipo di targa): " + String(cc) + "\n";
      report += "• Aperture cancello: " + String(aperture_cancello) + "\n";
      report += "• Tentativi falliti: " + String(tentativi_falliti) + "\n";
      report += "• % Errore: " + String(100*tentativi_falliti/aperture_cancello) + "%\n";
      report += "• % Successo: " + String(100*(aperture_cancello-tentativi_falliti)/aperture_cancello) + "%\n";
      report += "• Ultima targa individuata: "+ last_plate_found + "\n";

      bot.sendMessage(chat_id, report, "Markdown");
    }

    Serial.println(from_name);
    Serial.println(chat_id);
    Serial.println(text); 
  }
}

bool isAdminKeyValid(String key) {
  if (!client.connect(serverName.c_str(), serverPort)) return false;

  String postData = "{\"password\":\"" + key + "\"}";

  client.print(String("POST /check_admin HTTP/1.1\r\n") +
               "Host: " + serverName + "\r\n" +
               "Content-Type: application/json\r\n" +
               "Content-Length: " + postData.length() + "\r\n" +
               "Connection: close\r\n\r\n" +
               postData);

  while (client.connected()) {
    String line = client.readStringUntil('\n');
    if (line == "\r") break; // Fine headers
  }

  String body = client.readString();
  client.stop();
  body.trim();

  return body == "true";
}



bool isPasswordValid(String key) {
  if (!client.connect(serverName.c_str(), serverPort)) {
    Serial.println("Errore connessione");
    return false;
  }

  String payload = "{\"password\":\"" + key + "\"}";

  client.print(String("POST /check_password HTTP/1.1\r\n") +
               "Host: " + serverName + "\r\n" +
               "Content-Type: application/json\r\n" +
               "Content-Length: " + payload.length() + "\r\n" +
               "Connection: close\r\n\r\n" +
               payload);

  // Salta header
  while (client.connected()) {
    String line = client.readStringUntil('\n');
    if (line == "\r") break;
  }

  String body = client.readString();
  client.stop();
  body.trim();

  Serial.println("Risposta check_password: " + body);
  return body == "true";
}

bool plateEsiste(String plate) {
  //client.setInsecure();  // Solo per test con HTTP o certificati self-signed
  if (!client.connect(serverName.c_str(), serverPort)) return false;

  client.print(String("GET /check/") + plate + " HTTP/1.1\r\n" +
               "Host: " + serverName + "\r\n" +
               "Connection: close\r\n\r\n");

  while (client.connected() && client.available()) {
    String line = client.readStringUntil('\n');
    if (line == "\r") break;
  }

  String body = client.readString();
  client.stop();
  body.trim();

  return body == "true";
}

void aggiungiTarga(String plate, String password) {

  String body = "{\"plate\":\"" + plate + "\",\"password\":\"" + password + "\",\"admin\":false}";

  //client.setInsecure();  // Solo per test con HTTP o certificati self-signed
  if (!client.connect(serverName.c_str(), serverPort)) return;

  client.print(String("POST /addplate HTTP/1.1\r\n") +
               "Host: " + serverName + "\r\n" +
               "Content-Type: application/json\r\n" +
               "Content-Length: " + body.length() + "\r\n" +
               "Connection: close\r\n\r\n" +
               body);

  while (client.connected() && client.available()) {
    String line = client.readStringUntil('\n');
    if (line == "\r") break;
  }

  String response = client.readString();
  client.stop();
  Serial.printf("aggiungiTarga: %s\n", response.c_str());
}


void rimuoviTarga(String plate) {
  //client.setInsecure();  // Solo per test con HTTP o certificati self-signed
  if (!client.connect(serverName.c_str(), serverPort)) return;

  client.print(String("DELETE /removeplate/") + plate + " HTTP/1.1\r\n" +
               "Host: " + serverName + "\r\n" +
               "Connection: close\r\n\r\n");

  while (client.connected() && client.available()) {
    String line = client.readStringUntil('\n');
    if (line == "\r") break;
  }

  String response = client.readString();
  client.stop();
  Serial.printf("rimuoviTarga: %s\n", response.c_str());
}

long readDistanceCM() {
  digitalWrite(TRIGGER_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIGGER_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIGGER_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 60000); // 60ms timeout corrisponde a circa 10 metri
  return duration * 0.034 / 2;
}



String extractJsonStringValue(const String& jsonString, const String& key) {
  int keyIndex = jsonString.indexOf(key);
  if (keyIndex == -1) return "";
  int startIndex = jsonString.indexOf(':', keyIndex) + 2;
  int endIndex = jsonString.indexOf('"', startIndex);
  if (startIndex == -1 || endIndex == -1) return "";
  return jsonString.substring(startIndex, endIndex);
}

void testConnection() {
  const char* serverIP = "192.168.43.144";  // <-- metti l'IP giusto qui
  const int serverPort = 5000;

  if (!client.connect(serverIP, serverPort)) {
    Serial.println("⛔ Connessione al server fallita");
    waitForBluetoothWiFiConfig();
    SerialBT.end();
    return;
  }

  Serial.println("😎👍 Connesso al server, invio GET /");

  client.println("GET / HTTP/1.1");
  client.print("Host: ");
  client.println(serverIP);
  client.println("Connection: close");
  client.println();

  unsigned long timeout = millis() + 5000; // timeout di 5 secondi
  while (client.connected() && millis() < timeout) {
    while (client.available()) {
      String line = client.readStringUntil('\n');
      Serial.println(line);
    }
  }

  client.stop();
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);
  Serial.println("📦 Avvio setup");
  //pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);  // bottone connesso a GND

  pinMode(TRIGGER_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  
  loadWiFiConfig(ssid, password, configurato, plate, adminKey);

  if (configurato == 0 || ssid == "") {
    Serial.println("⚠️ Nessuna configurazione WiFi salvata, attivo BT");
    waitForBluetoothWiFiConfig(); 
    SerialBT.end();
  }

  Serial.println("📶 Connessione a WiFi salvato...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());

  int retry = 0;
  Serial.print("Wifi\t");
  while (WiFi.status() != WL_CONNECTED && retry < 20) {
    delay(500);
    Serial.print(".");
    retry++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("\n😎👍 Connesso. IP: ");
    Serial.println(WiFi.localIP());
    if (configurato == 1){
      configurato = 2;
      saveWiFiConfig(ssid, password, configurato, plate, adminKey);
      sendPlateToServer(plate, adminKey);
    }
  } else {
    Serial.println("\n⛔ Connessione WiFi fallita, attivo BT");
    waitForBluetoothWiFiConfig();
    SerialBT.end();
  }

  // Delay extra opzionale per assicurare memoria libera prima della camera
  delay(1000);
  Serial.print("psram\t");
  Serial.printf("Heap: %u | PSRAM: %u\n", ESP.getFreeHeap(), ESP.getFreePsram());

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    Serial.println("OK");
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 5;
    config.fb_count = 1;
  } else {
    Serial.println("FAIL... switching to CIF");
    config.frame_size = FRAMESIZE_CIF;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  Serial.print("CAMERA\t");
  delay(500);  // Breve delay di sicurezza
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("FAIL 0x%x\n", err);
    ESP.restart();
  } else {
    Serial.println("OK");
  }
  Serial.println("Carico il cc");
  cc = loadCC();  // Carica il country code personalizzato
  Serial.printf("cc caricato: %s\n",cc);
  secureClient.setCACert(TELEGRAM_CERTIFICATE_ROOT);
  gateServo.attach(SERVO_PIN);
  gateServo.write(0);
  loadMetrics(tentativi_falliti, aperture_cancello, maxDistance, minDistance, last_plate_found, sogliaTempoFermo, tolleranza);
  Serial.printf("tentativi_falliti: %d\naperture_cancello: %d\nmaxDistance: %f\nminDistance: %f\nlast_plate_found: %s\n",tentativi_falliti, aperture_cancello, maxDistance, minDistance, last_plate_found);
  Serial.printf("Carico ciclo cancello\n");
  loadCicloCancello();
  delay(2000);
  Serial.println("🔧 Inizio test connessione server...");
  testConnection();
  Serial.println("😎👍 Setup completato.");
  delay(5000);
}

void loop() {

  // Gestione bot Telegram
  if (millis() - ultima_chiamata_telegram_bot > intervallo_bot) {
    int num_nuovi_messaggi = bot.getUpdates(bot.last_message_received + 1);
    if (num_nuovi_messaggi > 0){
      gestisci_messaggi(num_nuovi_messaggi);
    }
    ultima_chiamata_telegram_bot = millis();
  }

  // Lettura distanza
  long distance = readDistanceCM();
  Serial.print("Distanza rilevata: ");
  Serial.print(distance);
  Serial.println(" cm");

  // Verifica se è entro range valido
  if (distance >= minDistance && distance <= maxDistance) {
    // Verifica se è stabile (cioè varia poco)
    if (ultimaDistanza != -1 && abs(distance - ultimaDistanza) <= tolleranza) {
      if (distanzaStabileDa == 0) {
        distanzaStabileDa = millis(); // Inizia conteggio tempo stabile
      }
      // Se l'oggetto è stabile da abbastanza tempo
      if (millis() - distanzaStabileDa >= sogliaTempoFermo && !isPictureTaken) {
        Serial.println("✅ Oggetto fermo rilevato, scatto in corso...");
        int status = sendPhoto2();
      }
    } else {
      // Reset: oggetto in movimento
      distanzaStabileDa = 0;
    }
  } else {
    // Fuori range
    distanzaStabileDa = 0;
  }

  ultimaDistanza = distance;
  delay(300);
}

void openGate() {
  gateClosed = false;
  Serial.println("Apro il cancello.");
  aperture_cancello++;
  gateServo.write(90); // Apri il cancello (regolare l'angolo)
  delay(2000); // Tiene premuto per un po'
  closeGate();
  saveMetrics(tentativi_falliti, aperture_cancello, maxDistance, minDistance, last_plate_found, sogliaTempoFermo, tolleranza);
}

void closeGate() {
  isPictureTaken = false;
  if (!gateClosed) {
    Serial.println("Attendo apertura cancello.");
    gateServo.write(0); // Chiudi il cancello
    gateClosed = true;
  } else {
    Serial.println("Cancello chiuso.");
  }
  delay(durata_ciclo_cancello);
}

/*int sendPhoto() {
  isPictureTaken = true;
  camera_fb_t* fb = NULL;
  delay(100);
  fb = esp_camera_fb_get();
  delay(100);

  //client.setInsecure();
  if (client.connect(serverName.c_str(), serverPort)) {
    Serial.println("Connessione al server riuscita");

    count++;
    String filename = apiKey + ".jpeg";
    String head = "--CircuitDigest\r\nContent-Disposition: form-data; name=\"imageFile\"; filename=\"" + filename + "\"\r\nContent-Type: image/jpeg\r\n\r\n";
    String tail = "\r\n--CircuitDigest--\r\n";
    uint32_t imageLen = fb->len;
    uint32_t extraLen = head.length() + tail.length();
    uint32_t totalLen = imageLen + extraLen;

    client.println("POST " + serverPath + " HTTP/1.1");
    client.println("Host: " + serverName);
    client.println("Content-Length: " + String(totalLen));
    client.println("Content-Type: multipart/form-data; boundary=CircuitDigest");
    client.println("Authorization:" + apiKey);
    client.println();
    client.print(head);

    uint8_t* fbBuf = fb->buf;
    size_t fbLen = fb->len;
    for (size_t n = 0; n < fbLen; n += 1024) {
      if (n + 1024 < fbLen) {
        client.write(fbBuf, 1024);
        fbBuf += 1024;
      } else {
        size_t remainder = fbLen % 1024;
        client.write(fbBuf, remainder);
      }
    }
    client.print(tail);
    esp_camera_fb_return(fb);

    Serial.println("Foto inviata, in attesa risposta...");

    String response;
    long startTime = millis();
    while (client.connected() && millis() - startTime < 5000) {
      if (client.available()) {
        char c = client.read();
        response += c;
      }
    }

    Serial.println("Risposta server:");
    Serial.println(response);
    String NPRData = extractJsonStringValue(response, "\"access\"");
    NPRData.trim();
    Serial.println("Numero targa: " + NPRData);
    if (NPRData == "1"){
      openGate();
    } else if (NPRData != "1"){
      closeGate();
      tentativi_falliti++;      
    }
    client.stop();
    return 0;
  } 
  else {
    Serial.println("Connessione al server fallita");
    esp_camera_fb_return(fb);
    return -2;
  }
}*/

/*
int sendPhoto2() {
  isPictureTaken = true;
  last_plate_found = "N/A";

  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Errore nella cattura immagine");
    isPictureTaken = false;
    return -1;
  }

  delay(100);
  Serial.println("📸 Immagine catturata");

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnesso, tentativo riconnessione...");
    WiFi.reconnect();
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 10) {
      delay(500);
      retries++;
    }
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Impossibile riconnettersi al WiFi");
      esp_camera_fb_return(fb);
      isPictureTaken = false;
      client.stop();
      return -3;
    }
  }

  String boundary = "----WebKitFormBoundary7MA4YWxkTrZu0gW";

  String head = "--" + boundary + "\r\n" +
                "Content-Disposition: form-data; name=\"countrycode\"\r\n\r\n" +
                cc + "\r\n" +

                "--" + boundary + "\r\n" +
                "Content-Disposition: form-data; name=\"imageFile\"; filename=\"esp32.jpg\"\r\n" +
                "Content-Type: image/jpeg\r\n\r\n";

  String tail = "\r\n--" + boundary + "--\r\n";
  int contentLength = head.length() + fb->len + tail.length();

  Serial.printf("Tentativo connessione a %s:%d\n", serverName.c_str(), serverPort);

  if (!client.connect(serverName.c_str(), serverPort)) {
    Serial.println("⛔ Connessione fallita al server");
    esp_camera_fb_return(fb);
    isPictureTaken = false;
    client.stop();
    return -2;
  }

  Serial.println("😎👍 Connessione al server riuscita");

  client.println("POST " + serverPath + " HTTP/1.1");
  client.println("Host: " + serverName);
  client.println("Content-Type: multipart/form-data; boundary=" + boundary);
  client.println("Content-Length: " + String(contentLength));
  client.println("Connection: close");
  client.println();

  client.print(head);

  uint8_t* fbBuf = fb->buf;
  size_t fbLen = fb->len;
  size_t chunkSize = 1024;
  for (size_t sent = 0; sent < fbLen; sent += chunkSize) {
    size_t toSend = min(chunkSize, fbLen - sent);
    size_t written = client.write(fbBuf + sent, toSend);
    if (written != toSend) {
      Serial.printf("⚠️ Errore invio chunk: scritti %d su %d bytes\n", written, toSend);
      break;
    }
    delay(1);
  }

  client.print(tail);
  client.flush();

  esp_camera_fb_return(fb);
  Serial.println("📤 Immagine inviata, attendo risposta...");

  String response = "";
  bool headerEnded = false;
  long timeout = millis();
  const long TIMEOUT_MS = 10000;

  while (client.connected() && (millis() - timeout < TIMEOUT_MS)) {
    if (client.available()) {
      String line = client.readStringUntil('\n');
      if (!headerEnded) {
        if (line == "\r") headerEnded = true;
      } else {
        response += line + "\n";
      }
      timeout = millis();
    } else {
      delay(10);
    }
  }
  Serial.println("response: "+response);
  if (response.length() == 0) {
    Serial.println("⛔ Nessuna risposta ricevuta dal server");
    isPictureTaken = false;
    client.stop();
    return -4;
  }

  Serial.println("📨 Risposta server:");
  Serial.println(response);

  String accessValue = extractJsonStringValue(response, "\"access\"");
  accessValue.trim();

  Serial.println("Valore access: " + accessValue);

  if (accessValue == "0") {
    Serial.println("😎👍 Targa autorizzata - Apertura cancello");
    last_plate_found = extractJsonStringValue(response, "\"plate\"");
    openGate();
    saveMetrics(tentativi_falliti, aperture_cancello, maxDistance, minDistance, last_plate_found, sogliaTempoFermo, tolleranza);
    delay(2000);
    client.stop();
    return 0;
  } else if (accessValue == "1") {
    Serial.println("⛔ Targa non autorizzata");
    closeGate();
    tentativi_falliti++;
    saveMetrics(tentativi_falliti, aperture_cancello, maxDistance, minDistance, last_plate_found, sogliaTempoFermo, tolleranza);
    delay(2000);
    client.stop();
    return 1;
  } else {
    Serial.println("⚠️ Risposta server non valida");
    closeGate();
    tentativi_falliti++;
    saveMetrics(tentativi_falliti, aperture_cancello, maxDistance, minDistance, last_plate_found, sogliaTempoFermo, tolleranza);
    delay(2000);
    client.stop();
    return -5;
  } 
}*/


int sendPhoto2() {
  last_plate_found = "N/A";

  // **FIX PRINCIPALE: Pulisci il buffer della camera prima di catturare**
  // Scarta eventuali frame vecchi nel buffer
  camera_fb_t* temp_fb = esp_camera_fb_get();
  if (temp_fb) {
    esp_camera_fb_return(temp_fb);
    delay(100); // Piccola pausa per permettere alla camera di stabilizzarsi
  }

  // Ora cattura il frame fresco
  camera_fb_t* fb = NULL;
  fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Errore nella cattura immagine");
    isPictureTaken = false;
    return -1;
  }
  
  isPictureTaken = true;
  Serial.println("📸 Immagine catturata (frame fresco)");

  // Check WiFi connection before proceeding
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnesso, tentativo riconnessione...");
    WiFi.reconnect();
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries <= 10) {
      delay(500);
      retries++;
    }
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Impossibile riconnettersi al WiFi");
      esp_camera_fb_return(fb);
      isPictureTaken = false;
      return -3;
    }
  }

  String boundary = "----WebKitFormBoundary7MA4YWxkTrZu0gW";
  String head = "--" + boundary + "\r\n" +
                "Content-Disposition: form-data; name=\"imageFile\"; filename=\"esp32.jpg\"\r\n" +
                "Content-Type: image/jpeg\r\n\r\n";
  String tail = "\r\n--" + boundary + "--\r\n";

  int contentLength = head.length() + fb->len + tail.length();
  
  Serial.printf("Tentativo connessione a %s:%d\n", serverName.c_str(), serverPort);
  
  if (!client.connect(serverName.c_str(), serverPort)) {
    Serial.println("⛔ Connessione fallita al server");
    Serial.printf("Server: %s, Porta: %d\n", serverName.c_str(), serverPort);
    esp_camera_fb_return(fb);
    isPictureTaken = false;
    return -2;
  }

  Serial.println("😎👍 Connessione al server riuscita");

  // Send HTTP headers
  client.println("POST " + serverPath + " HTTP/1.1");
  client.println("Host: " + serverName);
  client.println("Content-Type: multipart/form-data; boundary=" + boundary);
  client.println("Content-Length: " + String(contentLength));
  client.println("Connection: close");
  client.println();

  // Send HTTP body
  client.print(head);
  
  // Send image data in chunks
  uint8_t* fbBuf = fb->buf;
  size_t fbLen = fb->len;
  Serial.printf("FB len: %d\n", fbLen);
  size_t chunkSize = 1024;
  
  for (size_t sent = 0; sent < fbLen; sent += chunkSize) {
    size_t toSend = min(chunkSize, fbLen - sent);
    size_t written = client.write(fbBuf + sent, toSend);
    if (written != toSend) {
      Serial.printf("⚠️ Errore invio chunk: scritti %d su %d bytes\n", written, toSend);
      break;
    }
    delay(1);
  }
  
  client.print(tail);
  client.flush();

  // **IMPORTANTE: Rilascia il buffer immediatamente dopo l'invio**
  esp_camera_fb_return(fb);
  fb = NULL;
  
  Serial.println("📤 Immagine inviata, attendo risposta...");

  // Read server response
  String response = "";
  bool headerEnded = false;
  long timeout = millis();
  const long TIMEOUT_MS = 50000;
  
  while ((millis() - timeout) < TIMEOUT_MS && (client.connected() || client.available())) {
    while (client.available()) {
      String line = client.readStringUntil('\n');
      line.trim();
      Serial.print("> "); Serial.println(line);

      if (!headerEnded) {
        if (line.length() == 0) {
          headerEnded = true;
          Serial.println("🔽 Fine header HTTP");
        }
      } else {
        response += line + "\n";
      }
      timeout = millis();
    }
    delay(10);
  }
  
  client.stop();
  
  if (response.length() == 0) {
    Serial.println("⛔ Nessuna risposta ricevuta dal server");
    isPictureTaken = false;
    return -4;
  }

  Serial.println("📨 Risposta server:");
  Serial.println(response);
  
  // Parse JSON response
  String accessValue = extractJsonStringValue(response, "\"access\"");
  accessValue.trim();
  accessValue.replace(",", "");
  
  Serial.println("Valore access: " + accessValue);
  
  if (accessValue == "0") {
    Serial.println("😎👍 Targa autorizzata - Apertura cancello");
    last_plate_found = extractJsonStringValue(response, "\"plate\"");
    openGate();
    saveMetrics(tentativi_falliti, aperture_cancello, maxDistance, minDistance, last_plate_found, sogliaTempoFermo, tolleranza);
    isPictureTaken = false; // Reset flag
    return 0;
  } else if (accessValue == "1") {
    Serial.println("⛔ Targa non autorizzata");
    closeGate();
    tentativi_falliti++;
    saveMetrics(tentativi_falliti, aperture_cancello, maxDistance, minDistance, last_plate_found, sogliaTempoFermo, tolleranza);
    isPictureTaken = false; // Reset flag
    return 1;
  } else {
    Serial.println("⚠️ Risposta server non valida");
    closeGate();
    tentativi_falliti++;
    saveMetrics(tentativi_falliti, aperture_cancello, maxDistance, minDistance, last_plate_found, sogliaTempoFermo, tolleranza);
    isPictureTaken = false; // Reset flag
    return -5;
  }
}

