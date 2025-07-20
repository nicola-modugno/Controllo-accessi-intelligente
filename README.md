## 🚗 Controllo accessi intelligente con ESP32-CAM
![Splash](./splash.png)

Questo progetto realizza un sistema intelligente di controllo accessi veicolari basato sul **riconoscimento automatico delle targhe (ANPR)**, utilizzando una **ESP32-CAM**, un **sensore a ultrasuoni**, un **servomotore**, e un **telecomando RF**. Il sistema rileva automaticamente la presenza di un veicolo, cattura un'immagine della targa e la invia a un **server ANPR locale** (basato su OpenALPR). Se la targa è autorizzata, un attuatore meccanico simula la pressione del pulsante del telecomando per aprire il cancello.

Il sistema è **low-cost**, **facilmente configurabile via Bluetooth**, e include una modalità di **controllo manuale e diagnostica tramite bot Telegram**.

---

## 🔧 Funzionalità principali

- Rilevamento presenza veicolo con sensore a ultrasuoni
- Scatto automatico con ESP32-CAM
- Invio immagine a server ANPR (OpenALPR) via HTTP
- Verifica targa contro lista autorizzata
- Apertura cancello tramite servomotore che preme il pulsante del telecomando
- Configurazione iniziale via Bluetooth
- Apertura manuale e consultazione metriche via bot Telegram

---

## 🧰 Componenti utilizzati

| Componente            | Descrizione                                         |
|-----------------------|-----------------------------------------------------|
| ESP32-CAM             | Microcontrollore con fotocamera integrata          |
| Sensore a ultrasuoni  | Rileva presenza del veicolo                        |
| Servo SG90            | Premitura fisica del pulsante su telecomando RF   |
| Modulo FTDI           | Alimentazione e flashing firmware per ESP32-CAM   |
| Telecomando RF        | Interfaccia con il sistema di apertura esistente   |
| Case 3D               | Contenitore stampato in 3D per la protezione       |
| Pozzetto elettrico    | Alloggiamento esterno del sistema                  |

---

## 📸 Flusso di funzionamento

1. Il sensore a ultrasuoni rileva la presenza stabile di un veicolo.
2. L’ESP32-CAM scatta una foto e la invia via HTTP al server ANPR.
3. Il server (basato su Docker/OpenALPR) estrae il numero di targa.
4. Se la targa è autorizzata, il server invia un comando di apertura.
5. L’ESP32-CAM attiva il servomotore per premere il pulsante del telecomando.
6. Il cancello si apre; dopo un tempo configurato, il sistema si resetta.

---

## 📲 Comandi Telegram (admin only)

- `/opengate <admin_key>` — Apertura manuale cancello
- `/metrics <admin_key>` — Visualizzazione metriche
- `/cc <country_code> <admin_key>` — Imposta formato targa
- `/cancello <ms> <admin_key>` — Tempo ciclo apertura/chiusura
- `/d <max> <min> <admin_key>` — Imposta distanza operativa ultrasuoni
- `/tolleranza <cm> <admin_key>` — Imposta tolleranza movimento
- `/tempofermo <ms> <admin_key>` — Imposta tempo rilevamento veicolo fermo

---

## 🔌 Schema del circuito

![Schema del circuito](./schema_circuito.png)

---

## 💡 Estensioni possibili

- Integrazione con dashboard web o app mobile
- Gestione targhe via interfaccia grafica
- Logging avanzato degli accessi con timestamp
- Notifiche via Telegram o email
- Integrazione NFC o RFID come metodo di fallback

---

## 🎓 Progetto accademico

Questo progetto è stato realizzato come parte dell’esame del corso di **Laboratorio di Making**, Università di Bologna — A.A. 2024/2025.
