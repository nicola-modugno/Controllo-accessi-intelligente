#include <Arduino.h>
#include <WiFi.h>
#include <BluetoothSerial.h>
#include <EEPROM.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_camera.h"
#include <ESP32Servo.h>
#include "FS.h"
#include "SD_MMC.h"
#include "SPI.h"
#include <ArduinoJson.h>
#include <sdmmc_cmd.h>
#include <UniversalTelegramBot.h>
#include <WiFiClientSecure.h>
// 🔁 Ultrasuoni
#define TRIGGER_PIN 0
#define ECHO_PIN 16
//#define flashLight 4
#define SERVO_PIN 13
#define NUM_ADMIN_KEYS 1

Servo gateServo;

BluetoothSerial SerialBT;

#define EEPROM_SIZE 128
#define SSID_ADDR 0
#define PASS_ADDR 64

String plates[] = {"GJ52CPO" ,"LEZ2237", "NUO3NMF","R530CHP", "V9I7TNV","YN54LRE"};

String* platesfromSD;

String user_passwords[] = {"GJ52CPO" ,"LEZ2237", "NUO3NMF","R530CHP", "V9I7TNV","YN54LRE"};

String* user_passwordsfromSD;

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

char bluetoothCMD;
const uint8_t flagPassword = 0xFF;
const uint8_t flagWifiData = 0x00;
String ssid = "";
String password = "";
//String serverName = "www.circuitdigest.cloud";
//String serverPath = "/readnumberplate";
//const int serverPort = 443;
String serverName = "10.0.0.1";     // IP del tuo server
int serverPort = 5000;
String serverPath = "/upload";
String apiKey = "";
String passwords[] = {"Zy1Dhs8FW7Kx", "Ed5zTS8ocUfl", "jcht2rCgbMYB", "poldmeMpfrxR"};
String admin_keys[] = {"Ed5zT1S8ocUfl"};
char requestNumber = 0;
unsigned char NumOfPlates = 2;
unsigned long int starting_timestamp = 1747339200;
const char* botApiToken = "";
const unsigned long intervallo_bot = 1000;
unsigned long ultima_chiamata_telegram_bot = 0;
WiFiClientSecure client;
bool isPictureTaken = false;
bool gateClosed = true;
int count = 0;
unsigned char aperture_cancello = 0;
unsigned char tentativi_falliti = 0;
String last_plate_found;

UniversalTelegramBot bot(botApiToken, client);

void updateData(unsigned char numofplates, unsigned long int timeStamp, String* plateNumbers, String* passwords, char requestnumber, String ssid, String password) {
  // Apri file una sola volta
  File file = SD_MMC.open("/data", FILE_WRITE);
  if (!file) {
    Serial.println("Errore apertura file");
    return;
  }

  // Modifica il totale delle targhe memorizzate
  if (numofplates > 0){
    file.seek(0); // raggiunge l'offset in cui è allocato il totale delle targhe
    file.write((uint8_t*)&numofplates, sizeof(numofplates));
    Serial.printf("Written number of plates: %d\n", numofplates);
  }

  // Modifica il requestnumber, è utile per notificare il possessore o il manutentore di aggiornare la chiave dell'api quando raggiunge l'80% di utilizzo.
  // Può essere usato per calcolare le metriche.
  if (requestnumber != -1) {
    file.seek(1); // offset di requestnumber per sapere a quale richiesta mi trovo
    file.write((uint8_t*)&requestnumber, sizeof(requestNumber));
    Serial.printf("Written request number (%d)\n", requestnumber);
  }

  // Modifica il timestamp. Corrisponde alla data in cui è stata generata chiave. 
  // Può essere utile per notificare il manutentore quanto manca allo scadere della chiave.
  if (timeStamp > 0) {
    file.seek(2); // offset di requestnumber per sapere a quale richiesta mi trovo
    
    file.write((uint8_t*)&timeStamp, 4);
    Serial.printf("Written timestamp %lu, its size: %lu HEX: %02X %02X %02X\n", timeStamp);
    Serial.printf("starting_timestamp size: %lu HEX: %02X %02X %02X\n", starting_timestamp);
  }
  
  // Modifica con platesArray solo se viene passato
  // altrimenti di default è un array con solo la stringa "-1"
  if (plateNumbers != NULL && numofplates != 0 && passwords != NULL) {
    file.seek(5); // offset per le targhe
    for (int i = 0; i < numofplates; i++) {
      uint8_t len = plateNumbers[i].length();
      file.write(len); // salva la lunghezza della stringa
      file.write((const uint8_t*)plateNumbers[i].c_str(), len); // salva la stringa
      Serial.printf("Written plate #%d: %s\n", i, plateNumbers[i]);
      file.write(flagPassword);
      len = passwords[i].length();
      file.write(len); // salva la lunghezza della stringa
      file.write((const uint8_t*)passwords[i].c_str(), len); // salva la stringa
    }
  }
  if (ssid != NULL || ssid == "") {
      uint8_t len = ssid.length();
      file.write(flagWifiData);
      file.write(len); // salva la lunghezza della stringa
      file.write((const uint8_t*)ssid.c_str(), len); // salva la stringa
      Serial.printf("Written ssid #%d: %s\n", ssid);
      if (password != NULL || ssid == ""){
        file.write(flagPassword);
        len = password.length();
        file.write(len); // salva la lunghezza della stringa
        file.write((const uint8_t*)password.c_str(), len); // salva la stringa
        Serial.printf("Written password #%d: %s\n", password);
      }
    }
  file.close();
}

void loadData() {
  File file = SD_MMC.open("/data", FILE_READ);
  if (!file) {
    Serial.println("Errore apertura file");
    return;
  }

  unsigned char pippo;
  // 1. Leggi NumOfPlates
  file.seek(0);
  file.read((uint8_t*)&pippo, sizeof(pippo));
  Serial.print("Numero targhe (NumOfPlates): ");
  Serial.println(pippo);

  unsigned char paperino;
  // 2. Leggi requestNumber
  file.read((uint8_t*)&paperino, sizeof(paperino));
  Serial.print("Request number: ");
  Serial.println(paperino);

  // 3. Leggi starting_timestamp
  unsigned long int pluto;
  file.read((uint8_t*)&pluto, 4);

  Serial.printf("Timestamp (starting_timestamp): %d\n", pluto);

  // 4. Leggi platesfromSD
  if (platesfromSD != nullptr) {
    delete[] platesfromSD;  // libera memoria se già allocata
  }
  platesfromSD = new String[NumOfPlates];  // alloca nuova memoria

  if (user_passwordsfromSD != nullptr) {
    delete[] user_passwordsfromSD;  // libera memoria se già allocata
  }
  user_passwordsfromSD = new String[NumOfPlates];  // alloca nuova memoria

  file.seek(5); // mi posiziono dove iniziano le targhe
  for (int i = 0; i < NumOfPlates; i++) {
    uint8_t len;
    file.read(&len, 1); // legge il primo byte
    
    if (len == flagPassword){ // sta per leggere la lunghezza di una password
      
      Serial.printf("Symbol read: %d\n", len);
      file.read(&len, 1); // legge la lunghezza della password
      
      Serial.printf("Length read: %d\n", len);

      char buf[256] = {0}; // buffer temporaneo, sicuro fino a 255 caratteri
      file.read((uint8_t*)buf, len); // legge i `len` byte
      buf[len] = '\0'; // null-terminate

      user_passwords[i] = String(buf); // assegna alla variabile globale

      Serial.print("Password ");
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.println(user_passwords[i]);
    
    } else { // sta per leggere una targa

      Serial.printf("Length read: %d\n", len);

      char buf[256] = {0}; // buffer temporaneo, sicuro fino a 255 caratteri
      file.read((uint8_t*)buf, len); // legge i `len` byte
      buf[len] = '\0'; // null-terminate

      platesfromSD[i] = String(buf); // assegna alla variabile globale
    
      Serial.print("Targa ");
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.println(platesfromSD[i]);
    }
  }
  if (ssid != nullptr){
    uint8_t nextByte;
    if (file.read(&nextByte, 1) == flagWifiData){
      nextByte = file.read(&nextByte, 1); // legge la lunghezza dell'ssid
      
      Serial.printf("Length read: %d\n", nextByte);

      char buf[256] = {0}; // buffer temporaneo, sicuro fino a 255 caratteri
      file.read((uint8_t*)buf, nextByte); // legge i `len` byte
      buf[nextByte] = '\0'; // null-terminate

      ssid = String(buf); // assegna alla variabile globale
      Serial.printf("ssid read: %d\n", ssid);
      nextByte = file.read(&nextByte, 1); // leggo la password
      if (file.available()){
        nextByte = file.read(&nextByte, 1); // legge la lunghezza dell'ssid
      
        Serial.printf("Length read: %d\n", nextByte);

        char buf[256] = {0}; // buffer temporaneo, sicuro fino a 255 caratteri
        file.read((uint8_t*)buf, nextByte); // legge i `len` byte
        buf[nextByte] = '\0'; // null-terminate
        password = String(buf); // assegna alla variabile globale
        Serial.printf("password read: %d\n", password);
      }
    }  
  }
  file.close();
}

void waitForBluetoothWiFiConfig() {
  SerialBT.begin("ESP32CAM_Config"); // Nome visibile sul telefono
  Serial.println("🔄 In attesa credenziali WiFi via Bluetooth (formato: SSID=xxx;PASS=yyy)");

  while (true) {
    if (SerialBT.available()) {
      String input = SerialBT.readStringUntil('\n');
      input.trim();
      if (input.startsWith("SSID=") && input.indexOf(";PASS=") != -1) {
        int sep = input.indexOf(";PASS=");
        ssid = input.substring(5, sep);
        password = input.substring(sep + 6);
        SerialBT.println("Ricevuto. SSID: " + ssid + " | PASS: " + password);

        // Salva su microSD se già supportato
        updateData(NumOfPlates, starting_timestamp, plates, user_passwords, requestNumber, ssid, password);

        SerialBT.println("🔁 Riavvio in corso...");
        delay(1500);
        ESP.restart();
      } else {
        SerialBT.println("Formato non valido. Usa: SSID=xxx;PASS=yyy");
      }
    }
    delay(100);
  }
}


void gestisci_messaggi(int num_nuovi_messaggi) {
  for (int i = 0; i < num_nuovi_messaggi; i++) {
    String chat_id = bot.messages[i].chat_id;
    String text = bot.messages[i].text;
    String from_name = bot.messages[i].from_name;
    if (from_name == "") from_name = "Sconosciuto";

    if (text == "/start") {
      bot.sendMessage(chat_id, "Benvenuto. Usa /addplate o /removeplate oppure /opengate.");
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
          if (!plateEsiste(plate) || !passwordEsiste(password)) {
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

    // /removeplate TARGA CHIAVE
    if (text.startsWith("/removeplate ")) {
      int firstSpace = text.indexOf(' ');
      int secondSpace = text.indexOf(' ', firstSpace + 1);
      int thirdSpace = text.indexOf(' ', firstSpace + 2);

      if (firstSpace > 0 && secondSpace > firstSpace) {
        String plate = text.substring(firstSpace + 1, secondSpace);
        String password = text.substring(secondSpace + 1, thirdSpace);
        String key = text.substring(thirdSpace + 1);

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

    if (text.startsWith("/opengate ")) {
      String key = text.substring(text.indexOf(" ") + 1);
    if (isPasswordValid(key)) {
      bot.sendMessage(chat_id, "Cancello in apertura...");
      openGate();
    } else {
      bot.sendMessage(chat_id, "Chiave non valida.");
    }
  }
      // Comando /metrics
    if (text.startsWith("/metrics ")) {
      String password_utente = text.substring(9); // estrae la password dopo "/metrics "
      bool isAdmin = false;
      for (int j = 0; j < sizeof(passwords)/sizeof(passwords[0]); j++) {
        if (password_utente == passwords[j]) {
          isAdmin = true;
          break;
        }
      }

      if (!isAdmin) {
        bot.sendMessage(chat_id, "Password non valida.");
        continue;
      }

      // Leggi spazio SD
      uint64_t totalBytes = SD_MMC.totalBytes();
      uint64_t usedBytes  = SD_MMC.usedBytes();
      uint64_t freeBytes  = totalBytes - usedBytes;

      // Converte timestamp in giorni/ore/minuti
      unsigned long now = millis() / 1000;
      unsigned long uptime_days = now / 86400;
      unsigned long uptime_hours = (now % 86400) / 3600;
      unsigned long uptime_minutes = (now % 3600) / 60;

      // Prepara messaggio
      String report = "📊 *Stato sistema:*\n";
      report += "• Targhe registrate: " + String(NumOfPlates) + "\n";
      report += "• Requests count: " + String((int)requestNumber) + "\n";
      report += "• Requests rimanenti: " +  String(100-(int)requestNumber) + "\n";
      report += "• Spazio SD: " + String(freeBytes / (1024 * 1024)) + " MB liberi\n";
      report += "• Uptime: " + String(uptime_days) + "g " + String(uptime_hours) + "h " + String(uptime_minutes) + "m\n";
      report += "• Aperture cancello: " + String(aperture_cancello) + "\n";
      report += "• Tentativi falliti: " + String(tentativi_falliti) + "\n";

      bot.sendMessage(chat_id, report, "Markdown");
    }

    Serial.println(from_name);
    Serial.println(chat_id);
    Serial.println(text); 
  }
}

bool isPasswordValid(String key) {
  for (int i = 0; i < NumOfPlates; i++) {
    if (key == admin_keys[i]) {
      return true;
    }
  }
  return false;
}

bool isAdminKeyValid(String key) {
  for (int i = 0; i < NUM_ADMIN_KEYS; i++) {
    if (key == admin_keys[i]) return true;
  }
  return false;
}

bool plateEsiste(String plate) {
  for (int i = 0; i < NumOfPlates; i++) {
    if (platesfromSD[i] == plate) return true;
  }
  return false;
}

bool passwordEsiste(String password) {
  for (int i = 0; i < NumOfPlates; i++) {
    if (user_passwordsfromSD[i] == password) return true;
  }
  return false;
}

void aggiungiTarga(String plate, String password) {
  // Crea nuovo array, copia vecchi, aggiungi nuova, aggiorna SD
  String* nuoveTarghe = new String[NumOfPlates + 1];
  String* nuovePassword = new String[NumOfPlates + 1];
  for (int i = 0; i < NumOfPlates; i++) {
    nuoveTarghe[i] = platesfromSD[i];
    nuovePassword[i] = user_passwords[i];
  }
  nuoveTarghe[NumOfPlates] = plate;
  nuovePassword[NumOfPlates] = password;
  NumOfPlates++;
  updateData(NumOfPlates, 0, nuoveTarghe, nuovePassword, requestNumber, ssid, password); // Aggiorna file SD
  delete[] platesfromSD;
  platesfromSD = nuoveTarghe;
  delete[] user_passwordsfromSD;
  user_passwordsfromSD = nuovePassword;
}



void rimuoviTarga(String plate) {
  if (NumOfPlates == 0) return;
  String* nuoveTarghe = new String[NumOfPlates - 1];
  String* nuovePassword = new String[NumOfPlates - 1];
  int j = 0;
  for (int i = 0; i < NumOfPlates; i++) {
    if (platesfromSD[i] != plate) {
      nuoveTarghe[j++] = platesfromSD[i];
      nuovePassword[j++] = user_passwords[i];
    }
  }
  NumOfPlates--;
  updateData(NumOfPlates, 0, nuoveTarghe, nuovePassword, requestNumber, ssid, password);
  delete[] platesfromSD;
  platesfromSD = nuoveTarghe;
  delete[] user_passwordsfromSD;
  user_passwordsfromSD = nuovePassword;
}

// Funzione per leggere la distanza con ultrasuoni
long readDistanceCM() {
  digitalWrite(TRIGGER_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIGGER_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIGGER_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH, 30000); // max 30ms = ~5m
  return duration * 0.034 / 2;
}

// Aggiunge una targa tra quelle esistenti
void addPlate(char plate[]){
  String newPlates[NumOfPlates+1];

  for (int i = 0; i < NumOfPlates+1; i++){

    if (i == NumOfPlates){
      newPlates[i] = plate;
      NumOfPlates++;
      return;
    }
    else {
      newPlates[i] = platesfromSD[i];
    }
  } 
}

String extractJsonStringValue(const String& jsonString, const String& key) {
  int keyIndex = jsonString.indexOf(key);
  if (keyIndex == -1) return "";
  int startIndex = jsonString.indexOf(':', keyIndex) + 2;
  int endIndex = jsonString.indexOf('"', startIndex);
  if (startIndex == -1 || endIndex == -1) return "";
  return jsonString.substring(startIndex, endIndex);
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);
  delay(1000);  // attende 1 secondo per sicurezza
  pinMode(2, OUTPUT);
  //Inizializzazione salvataggio su microSD -------------------------------------------------------------------------
  if(!SD_MMC.begin("/sdcard", true)) {
    Serial.println("Err. microSD Init\n Check the peripheral.");
    return;
  }

  uint8_t cardType = SD_MMC.cardType();
  if(cardType == CARD_NONE){
    Serial.println("No SD card attached\n Please connect a microSD card of at least 4GB.");
    return;
  }

  Serial.print("SD Card Type: ");
  if(cardType == CARD_MMC){
    Serial.println("MMC");
  } else if(cardType == CARD_SD){
    Serial.println("SDSC");
  } else if(cardType == CARD_SDHC){
    Serial.println("SDHC");
  } else {
    Serial.println("UNKNOWN");
  }

  Serial.println("microSD\tOK");

  updateData(NumOfPlates, starting_timestamp, plates, user_passwords, requestNumber, ssid, password);
  loadData();

  
  //-----------------------------------------------------------------------------------------------------------------
  pinMode(TRIGGER_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  

  WiFi.mode(WIFI_STA);
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid.c_str(), password.c_str());
  client.setCACert(TELEGRAM_CERTIFICATE_ROOT);

  WiFi.begin(ssid.c_str(), password.c_str());
  client.setCACert(TELEGRAM_CERTIFICATE_ROOT);

  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 20) {
    Serial.print("Wifi\t");
    delay(500);
    retry++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connesso. IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nConnessione fallita. Avvio modalità configurazione Bluetooth.");
    waitForBluetoothWiFiConfig(); // entra in loop finché non riceve nuove credenziali
  }

  // Camera init
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
    Serial.printf("psram\tfound");
    config.frame_size = FRAMESIZE_HD;
    config.jpeg_quality = 5;
    config.fb_count = 2;
  } else {
    Serial.printf("psram\tfound... switching to SVGA");
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {    
    Serial.printf("Camera init failed with error 0x%x", err);
    delay(1000);
    ESP.restart();
  }
  Serial.println("Camera\tOK");
  gateServo.setPeriodHertz(50);               
  gateServo.attach(SERVO_PIN, 500, 2400);
  //serialBT.begin("Esp32-BT");
  Serial.println("Setup completed.");
  
}

void loop() {
    
  if (millis() - ultima_chiamata_telegram_bot > intervallo_bot) {
    int num_nuovi_messaggi = bot.getUpdates(bot.last_message_received + 1);
    int precedenti_messaggi = 0;
    
    for (int i = 0; i < num_nuovi_messaggi; i++) {
      Serial.println("Messaggio ricevuto:");
      Serial.println(bot.messages[i].text);

      gestisci_messaggi(num_nuovi_messaggi);
    }

    ultima_chiamata_telegram_bot = millis();
  }

  long distance = readDistanceCM();
  Serial.print("Distanza rilevata: ");
  Serial.print(distance);
  Serial.println(" cm");

  if (distance > 0 && distance < 5 && !isPictureTaken) {  // Se oggetto entro 5 cm
    delay(100); // breve attesa per evitare doppie rilevazioni
    int status = sendPhoto();
    if (status == 0) {
      Serial.println("Foto inviata con successo.");
    } else {
      Serial.println("Errore durante l'invio.");
    }
    delay(3000);  // Ritardo per evitare ripetizione continua
  }

  delay(300);
}

bool retrieveFromDB(String string){
  //Invia la query al DB
  //Se string è presente nel DB apre il gate
  //altrimenti closeGate()
  bool result = false;
  if (string.equals("") || string.equals("NULL")){
    result = false;
    isPictureTaken = false;
  }
  else {
    for (int  i = 0; i <= NumOfPlates-1; i++){
      if (string == platesfromSD[i]){
        result = true;
        Serial.print("Targa approvata. ");
        break;
      }
    }
    if (result == false){
      Serial.println("Targa non approvata.");
    }
  }
  return result;
}

void openGate() {
  gateClosed = false;
  Serial.println("Apro il cancello.");
  aperture_cancello++;
  gateServo.write(90); // Apri il cancello (regolare l'angolo)

  long distance = readDistanceCM();
  while (distance >= 0 && distance <= 10) {
    distance = readDistanceCM();
    Serial.println("Attendo che l'auto si sposti.");
    delay(100);  
  }

  delay(8000); // Attendi un po'
  closeGate();
}

void closeGate() {
  isPictureTaken = false;
  if (!gateClosed) {
    Serial.println("Chiudo il cancello.");
    gateServo.write(0); // Chiudi il cancello
    gateClosed = true;
  } else {
    Serial.println("Cancello già chiuso.");
  }
}

int sendPhoto() {
  isPictureTaken = true;
  camera_fb_t* fb = NULL;
  delay(100);
  fb = esp_camera_fb_get();
  delay(100);

  client.setInsecure();
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
    String NPRData = extractJsonStringValue(response, "\"number_plate\"");
    NPRData.trim();
    Serial.println("Numero targa: " + NPRData);
    bool result = retrieveFromDB(NPRData);
    if (result){
      openGate();
      last_plate_found = NPRData;
    } else if (!result){
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
}

int sendPhoto2() {
  camera_fb_t* fb = NULL;
  delay(100);
  fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Errore nella cattura immagine");
    return -1;
  }

  Serial.println("📸 Immagine catturata");

  String boundary = "----WebKitFormBoundary7MA4YWxkTrZu0gW";
  String head = "--" + boundary + "\r\n" +
                "Content-Disposition: form-data; name=\"imageFile\"; filename=\"esp32.jpg\"\r\n" +
                "Content-Type: image/jpeg\r\n\r\n";
  String tail = "\r\n--" + boundary + "--\r\n";

  int contentLength = head.length() + fb->len + tail.length();

  client.setInsecure();  // Solo per test con HTTP o certificati self-signed
  if (!client.connect(serverName.c_str(), serverPort)) {
    Serial.println("Connessione fallita");
    esp_camera_fb_return(fb);
    return -2;
  }

  Serial.println("Connessione al server riuscita");

  // Header HTTP
  client.println("POST " + serverPath + " HTTP/1.1");
  client.println("Host: " + serverName);
  client.println("Content-Type: multipart/form-data; boundary=" + boundary);
  client.println("Content-Length: " + String(contentLength));
  client.println(); // Fine header

  // Corpo HTTP
  client.print(head);
  client.write(fb->buf, fb->len);
  client.print(tail);

  esp_camera_fb_return(fb);
  Serial.println("📤 Immagine inviata");

  // Leggi risposta
  String response;
  long timeout = millis();
  while (client.connected() && millis() - timeout < 5000) {
    while (client.available()) {
      char c = client.read();
      response += c;
      timeout = millis(); // Reset timeout se riceviamo dati
    }
  }
  Serial.println("Risposta server:");
  Serial.println(response);
  String NPRData = extractJsonStringValue(response, "\"plate\"");
  NPRData.trim();
  Serial.println("Numero targa: " + NPRData);
  bool result = retrieveFromDB(NPRData);
  if (result){
    openGate();
    last_plate_found = NPRData;
  } else if (!result){
    closeGate();
    tentativi_falliti++;      
  }

  return 0;
}