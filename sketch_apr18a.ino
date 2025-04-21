#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_camera.h"
#include <ESP32Servo.h>
#include "FS.h"
#include "SD_MMC.h"


const char* ssid = "WuTangLAN";
const char* password = "2q5w1234";
String serverName = "www.circuitdigest.cloud";
String serverPath = "/readnumberplate";
const int serverPort = 443;
String apiKey = "bPZ9uTAMBBM6";

// 🔁 Ultrasuoni
#define TRIGGER_PIN 13
#define ECHO_PIN 15
#define flashLight 4
#define NumOfPlates 6
#define SERVO_PIN 14

Servo gateServo;

String plates[] = {"GJ52CPO" ,"LEZ2237", "NUO3NMF","R530CHP", "V9I7TNV","YN54LRE"};

WiFiClientSecure client;

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

int count = 0;
bool isPictureTaken = false;
bool gateClosed = true;

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
  //Inizializzazione salvataggio su microSD -------------------------------------------------------------------------
  if (!SD_MMC.begin()) {
    Serial.println("Errore inizializzazione SD card");
  } else {
    Serial.println("SD card inizializzata correttamente");
  }
  //-----------------------------------------------------------------------------------------------------------------
  pinMode(TRIGGER_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(flashLight, OUTPUT);
  digitalWrite(flashLight, LOW);

  WiFi.mode(WIFI_STA);
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("\nWiFi connected. IP: " + WiFi.localIP().toString());

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
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 5;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_CIF;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    delay(1000);
    ESP.restart();
  }
  gateServo.setPeriodHertz(50);               
  gateServo.attach(SERVO_PIN, 500, 2400);   
  Serial.println("Chiudo il cancello."); 
  gateServo.write(0); // cancello chiuso
  Serial.println("Setup completato. In attesa di un oggetto...");
}

void loop() {
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
      if (string.equals(plates[i])){
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


  if (!fb) {
    Serial.println("Errore cattura immagine");
    return -1;
  }

  // Salva immagine su SD ---------------------------------------------
  String path = "/foto" + String(millis()) + ".jpg";
  File file = SD_MMC.open(path.c_str(), FILE_WRITE);
  if (!file) {
    Serial.println("Errore apertura file per scrittura su SD");
  } else {
    file.write(fb->buf, fb->len);
    file.close();
    Serial.println("Foto salvata su SD in: " + path);
  }
  //-------------------------------------------------------------------

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
    } else if (!result){
      closeGate();      
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
