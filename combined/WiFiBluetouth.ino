/*
 * ============================================================
 * DETECTION COMBINEE WiFi + BLE 
 * ============================================================
 */

#include <WiFi.h>
#include <esp_wifi.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <esp_bt.h>           
#include <freertos/semphr.h>

// ===========================================================
//  CONFIGURATION
// ===========================================================
#define MAX_DEV    20
#define TIMEOUT    30000
#define SEUIL      -80
#define N_IN        2.5f      // Indice d'atténuation (environnement classe)

// Valeurs de calibration à 1 mètre
#define RSSI_D0_WIFI -48.0f   
#define RSSI_D0_BLE  -59.0f   

// ===========================================================
//  TABLE PARTAGEE (STOCKAGE EN MEMOIRE)
// ===========================================================
struct Dev {
  char          mac[18];
  char          tech[5];
  char          info[30];
  int           rssi;
  int           cnt;
  unsigned long t;
};

Dev              devs[MAX_DEV];
int              nDev = 0;
SemaphoreHandle_t mtx; // Sécurité pour le partage entre les 2 cœurs

// Fonction d'estimation de distance adaptative
float estimateDist(int r, const char* tech) {
  // Sélection de la bonne référence selon la technologie
  float rssi_d0 = (strcmp(tech, "WiFi") == 0) ? RSSI_D0_WIFI : RSSI_D0_BLE;
  return pow(10.0f, (rssi_d0 - r) / (10.0f * N_IN));
}

// Générateur de barre de signal visuelle
String sigBar(int r) {
  int b = constrain(map(r, -90, -40, 0, 5), 0, 5);
  String s = "[";
  for (int i = 0; i < 5; i++) s += (i < b) ? "|" : ".";
  return s + "]";
}

// Ajout ou mise à jour sécurisée d'un appareil dans le tableau
bool addDev(const char* mac, const char* tech, const char* info, int rssi) {
  if (xSemaphoreTake(mtx, pdMS_TO_TICKS(15)) != pdTRUE)
    return false;

  for (int i = 0; i < nDev; i++) {
    if (strcmp(devs[i].mac, mac) == 0) {
      devs[i].rssi = rssi;
      devs[i].t    = millis();
      devs[i].cnt++;
      strncpy(devs[i].info, info, 29);
      xSemaphoreGive(mtx);
      return false;
    }
  }

  if (nDev < MAX_DEV) {
    strncpy(devs[nDev].mac,  mac,  17);
    strncpy(devs[nDev].tech, tech,  4);
    strncpy(devs[nDev].info, info, 29);
    devs[nDev].rssi = rssi;
    devs[nDev].cnt  = 1;
    devs[nDev].t    = millis();
    nDev++;
    xSemaphoreGive(mtx);
    return true;
  }

  xSemaphoreGive(mtx);
  return false;
}

// ===========================================================
//  WiFi — Structure des paquets de gestion (Management Frames)
// ===========================================================
typedef struct {
  unsigned fc : 16;
  unsigned dur: 16;
  uint8_t a1[6];
  uint8_t a2[6];   // Adresse MAC source de l'émetteur
  uint8_t a3[6];
  unsigned seq: 16;
} hdr_t;

typedef struct {
  hdr_t   h;
  uint8_t p[0];
} pkt_t;

// ===========================================================
//  WiFi — Callback d'interception (Mode Promiscuous)
// ===========================================================
void IRAM_ATTR wcb(void* b, wifi_promiscuous_pkt_type_t t) {
  if (t != WIFI_PKT_MGMT) return;

  const wifi_promiscuous_pkt_t* pk = (wifi_promiscuous_pkt_t*)b;
  const pkt_t* ip = (pkt_t*)pk->payload;
  const hdr_t* h  = &ip->h;

  // Filtrage : Écoute UNIQUEMENT des Probe Requests (0x0040)
  if ((h->fc & 0x00FC) != 0x0040) return;

  int r = pk->rx_ctrl.rssi;
  if (r < SEUIL) return;

  char mac[18];
  snprintf(mac, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
    h->a2[0], h->a2[1], h->a2[2],
    h->a2[3], h->a2[4], h->a2[5]);

  char ssid[30] = "broadcast";
  if (ip->p[0] == 0 && ip->p[1] > 0 && ip->p[1] <= 29) {
    memcpy(ssid, &ip->p[2], ip->p[1]);
    ssid[ip->p[1]] = '\0';
  }

  if (addDev(mac, "WiFi", ssid, r)) {
    Serial.printf("\n[WiFi] NOUVEAU : %s | %ddBm %s | %.1fm | %s\n",
      mac, r, sigBar(r).c_str(), estimateDist(r, "WiFi"), ssid);
  }
}

// ===========================================================
//  WiFi — Tâche dédiée sur le Core 0
// ===========================================================
const uint8_t wch[] = {1,6,11,2,3,4,5,7,8,9,10,12,13}; // Canaux à balayer

void wTask(void* p) {
  vTaskDelay(pdMS_TO_TICKS(2000)); // Laisse le temps au BLE de démarrer

  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(&wcb);

  Serial.println("[WiFi] Mode promiscuous (sniffer) activé sur Core 0");

  int c = 0;
  while (true) {
    esp_wifi_set_channel(wch[c], WIFI_SECOND_CHAN_NONE);
    c = (c + 1) % 13; // Saut de canal
    vTaskDelay(pdMS_TO_TICKS(200)); // Reste 200ms sur chaque canal
  }
}

// ===========================================================
//  BLE — Décodage des marques / constructeurs
// ===========================================================
const char* cname(uint16_t id) {
  switch (id) {
    case 0x004C: return "Apple";
    case 0x0075: return "Samsung";
    case 0x012D: return "Sony";
    case 0x038F: return "Xiaomi";
    case 0x0157: return "Huawei";
    case 0x00E0: return "Google";
    default:     return "BLE";
  }
}

const char* atype(uint8_t t) {
  switch (t) {
    case 0x07: return "AirPods";
    case 0x09: return "AirPods Pro";
    case 0x10: return "iPhone";
    case 0x0E: return "AppleWatch";
    case 0x06: return "iPad";
    default:   return "Apple";
  }
}

class BCb : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice d) {
    int r = d.getRSSI();
    if (r < SEUIL) return;

    const char* mac = d.getAddress().toString().c_str();
    char info[30] = "BLE";

    if (d.haveManufacturerData()) {
      String dt = d.getManufacturerData();
      if (dt.length() >= 2) {
        uint16_t cid = (uint8_t)dt[1] << 8 | (uint8_t)dt[0];
        if (cid == 0x004C && dt.length() >= 3)
          snprintf(info, 30, "Apple/%s", atype((uint8_t)dt[2]));
        else
          snprintf(info, 30, "%s", cname(cid));
      }
    } else if (d.haveName()) {
      strncpy(info, d.getName().c_str(), 29);
    }

    if (addDev(mac, "BLE", info, r)) {
      Serial.printf("\n[BLE]  NOUVEAU : %s | %ddBm %s | %.1fm | %s\n",
        mac, r, sigBar(r).c_str(), estimateDist(r, "BLE"), info);
    }
  }
};

// ===========================================================
//  BLE — Tâche dédiée sur le Core 1
// ===========================================================
BLEScan* bsc;

void bTask(void* p) {
  BLEDevice::init("");

  bsc = BLEDevice::getScan();
  bsc->setAdvertisedDeviceCallbacks(new BCb());
  bsc->setActiveScan(true);
  bsc->setInterval(100);
  bsc->setWindow(50);   // Fenêtre réduite à 50% pour laisser la radio libre au Wi-Fi

  Serial.println("[BLE] Scanner actif démarré sur Core 1");

  while (true) {
    bsc->start(3, false); // Scan pendant 3 secondes
    bsc->clearResults();
    vTaskDelay(pdMS_TO_TICKS(500)); // Pause de 500ms pour laisser respirer le Wi-Fi
  }
}

// ===========================================================
//  AFFICHAGE DU TABLEAU DE BORD (MONITEUR SERIE)
// ===========================================================
void printTable() {
  unsigned long now = millis();
  int wc = 0, bc = 0;

  Serial.println("\n------- APPAREILS APPARENTS DANS LA CLASSE -------");

  if (xSemaphoreTake(mtx, pdMS_TO_TICKS(100)) == pdTRUE) {
    int n = 0;
    for (int i = 0; i < nDev; i++) {
      // Ignorer l'appareil 
      if (now - devs[i].t > TIMEOUT) continue;
      
      n++;
      if (strcmp(devs[i].tech, "WiFi") == 0) wc++;
      else bc++;

      Serial.printf("[%d] %s | %s | %ddBm %s | ~%.1fm | %s | x%d\n",
        n,
        devs[i].mac,
        devs[i].tech,
        devs[i].rssi,
        sigBar(devs[i].rssi).c_str(),
        estimateDist(devs[i].rssi, devs[i].tech), // Utilise la bonne calibration technique
        devs[i].info,
        devs[i].cnt
      );
    }
    if (n == 0) Serial.println("  Aucun signal détecté pour le moment...");
    xSemaphoreGive(mtx);
  }

  Serial.printf("SYNTHÈSE -> WiFi : %d | BLE : %d | Total : %d | Temps de run : %lus\n",
    wc, bc, wc + bc, now / 1000);
  Serial.println("--------------------------------------------------");
}

// ===========================================================
//  INITIALISATION DU SYSTEME
// ===========================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("==================================================");
  Serial.println("         SYSTEME DE DETECTION MIXTE RADIO         ");
  Serial.println("==================================================");

  mtx = xSemaphoreCreateMutex();

  // Étape 1 : Configurer la base matérielle Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(200);
  Serial.println("[OK] Couche matérielle Wi-Fi initialisée (STA)");

  // Étape 2 : Distribuer le travail sur les deux cœurs de l'ESP32
  // Tâche BLE sur le Core 1 (Priorité haute : 2)
  xTaskCreatePinnedToCore(bTask, "BLE_Task", 8192, NULL, 2, NULL, 1);

  // Tâche Wi-Fi Sniffer sur le Core 0 (Priorité normale : 1)
  xTaskCreatePinnedToCore(wTask, "WiFi_Task", 4096, NULL, 1, NULL, 0);

  delay(3000);
  Serial.println("[OK] Coexistence Dual-Core active et prête.\n");
}

// L'affichage s'actualise de manière autonome toutes les 5 secondes
void loop() {
  printTable();
  delay(5000);
}
