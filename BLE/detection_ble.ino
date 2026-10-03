/*
 * ============================================================
 *  DETECTION BLE 
 *  ============================================================
 */

#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

BLEScan* pBLEScan;

// ---- Table de deduplication ----
#define MAX_BLE 20
#define TIMEOUT_MS 30000  // Supprimer apres 30s sans signal

struct BLEDevice_t {
  char     mac[18];
  char     marque[30];
  char     type[30];
  int      rssi;
  int      count;
  unsigned long lastSeen;
};

BLEDevice_t bleDevices[MAX_BLE];
int numBLE = 0;

// ---- Formule distance corrigee ----
float estimateDistance(int rssi) {
  const float rssi_d0 = -59.0;  // RSSI mesure a 1 metre
  const float n       =  2.5;   // Coefficient interieur
  return pow(10.0, (rssi_d0 - rssi) / (10.0 * n));
}

// ---- Barre de signal ----
String signalBar(int rssi) {
  int bars = constrain(map(rssi, -90, -40, 0, 5), 0, 5);
  String b = "[";
  for (int i = 0; i < 5; i++) b += (i < bars) ? "|" : ".";
  return b + "]";
}

// ---- Retrouver fabricant ----
const char* getCompany(uint16_t id) {
  switch (id) {
    case 0x004C: return "Apple";
    case 0x0075: return "Samsung";
    case 0x0006: return "Microsoft";
    case 0x012D: return "Sony";
    case 0x038F: return "Xiaomi";
    case 0x0157: return "Huawei";
    case 0x00E0: return "Google";
    default:     return "Inconnu";
  }
}

// ---- Type appareil Apple ----
const char* getAppleType(uint8_t t) {
  switch (t) {
    case 0x07: return "AirPods";
    case 0x09: return "AirPods Pro";
    case 0x0F: return "AirPods Max";
    case 0x10: return "iPhone";
    case 0x0E: return "Apple Watch";
    case 0x06: return "iPad";
    default:   return "Apple device";
  }
}

// ---- Ajouter ou mettre a jour un appareil ----
// Retourne true si c'est un NOUVEL appareil
bool addOrUpdate(const char* mac, int rssi,
                 const char* marque, const char* type) {
  // Chercher si deja connu
  for (int i = 0; i < numBLE; i++) {
    if (strcmp(bleDevices[i].mac, mac) == 0) {
      bleDevices[i].rssi     = rssi;
      bleDevices[i].lastSeen = millis();
      bleDevices[i].count++;
      strncpy(bleDevices[i].marque, marque, 29);
      strncpy(bleDevices[i].type,   type,   29);
      return false;  // Connu
    }
  }
  // Nouvel appareil
  if (numBLE < MAX_BLE) {
    strncpy(bleDevices[numBLE].mac,    mac,    17);
    strncpy(bleDevices[numBLE].marque, marque, 29);
    strncpy(bleDevices[numBLE].type,   type,   29);
    bleDevices[numBLE].rssi     = rssi;
    bleDevices[numBLE].count    = 1;
    bleDevices[numBLE].lastSeen = millis();
    numBLE++;
    return true;  // Nouveau !
  }
  return false;
}

// ---- Callback BLE ----
class MyBLECallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice dev) {

    int rssi = dev.getRSSI();
    if (rssi < -80) return;  // Seuil

    const char* mac = dev.getAddress().toString().c_str();

    char marque[30] = "Inconnu";
    char type[30]   = "";

    // Identification fabricant
    if (dev.haveManufacturerData()) {
      String data = dev.getManufacturerData();
      if (data.length() >= 2) {
        uint16_t cid = (uint8_t)data[1] << 8 | (uint8_t)data[0];
        strncpy(marque, getCompany(cid), 29);

        // Type specifique Apple
        if (cid == 0x004C && data.length() >= 3) {
          strncpy(type, getAppleType((uint8_t)data[2]), 29);
        }
      }
    }

    // Nom si pas de fabricant
    if (strcmp(marque, "Inconnu") == 0 && dev.haveName()) {
      strncpy(marque, dev.getName().c_str(), 29);
    }

    // Ajouter ou mettre a jour
    bool isNew = addOrUpdate(mac, rssi, marque, type);

    // Afficher seulement les NOUVEAUX en temps reel
    if (isNew) {
      float dist = estimateDistance(rssi);
      Serial.println("\n========================================");
      Serial.println("  [BLE] NOUVEL APPAREIL DETECTE !");
      Serial.println("========================================");
      Serial.print  ("  MAC    : "); Serial.println(mac);
      Serial.print  ("  RSSI   : "); Serial.print(rssi); Serial.println(" dBm");
      Serial.print  ("  Signal : "); Serial.println(signalBar(rssi));
      Serial.print  ("  Dist ~ : "); Serial.print(dist, 1); Serial.println(" m");
      Serial.print  ("  Marque : "); Serial.println(marque);
      if (strlen(type) > 0) {
        Serial.print("  Type   : >>> "); Serial.println(type);
      }
    }
  }
};

// ---- Affichage tableau toutes les 5s ----
unsigned long lastPrint = 0;

void printDashboard() {
  unsigned long now = millis();
  int actifs = 0;

  Serial.println("\n---- APPAREILS BLE ACTIFS ----");
  for (int i = 0; i < numBLE; i++) {
    if (now - bleDevices[i].lastSeen > TIMEOUT_MS) continue;
    actifs++;
    float dist = estimateDistance(bleDevices[i].rssi);
    Serial.printf("  [%d] %s | %d dBm %s | ~%.1f m | %s",
      actifs,
      bleDevices[i].mac,
      bleDevices[i].rssi,
      signalBar(bleDevices[i].rssi).c_str(),
      dist,
      bleDevices[i].marque
    );
    if (strlen(bleDevices[i].type) > 0) {
      Serial.printf(" — %s", bleDevices[i].type);
    }
    Serial.printf(" | vu %dx\n", bleDevices[i].count);
  }
  if (actifs == 0) Serial.println("  Aucun appareil actif");
  Serial.println("------------------------------");
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("===========================================");
  Serial.println("  DETECTEUR BLE ");
  Serial.println("  Seminaire Detection Radiomobile");
  Serial.println("===========================================");
  Serial.println("[INFO] Scan ACTIF (detecte meme sans ouvrir parametres)");
  Serial.println("[INFO] Seuil : -80 dBm | Timeout : 30s");
  Serial.println("[INFO] Formule distance : rssi_d0 = -59 dBm, n = 2.5\n");

  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyBLECallbacks());

  // SCAN ACTIF — envoie des SCAN_REQ pour reveiller les appareils
  // Detecte meme les telephones qui n'ont pas ouvert les parametres BT
  pBLEScan->setActiveScan(true);

  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);

  Serial.println("[OK] Scanner BLE demarre...\n");
}

void loop() {
  // Scan continu de 3 secondes
  pBLEScan->start(3, false);
  pBLEScan->clearResults();

  // Tableau toutes les 5 secondes
  if (millis() - lastPrint > 5000) {
    printDashboard();
    lastPrint = millis();
  }

  delay(100);
}
