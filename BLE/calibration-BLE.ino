/*
 * ============================================================
 *  CALIBRATION rssi_d0 — BLE ESP32
 *  
 *  Instructions :
 *    1. Flasher ce code sur l'ESP32
 *    2. Ouvrir le moniteur serie a 115200 baud
 *    3. Placer le telephone a EXACTEMENT 1 metre de l'ESP32
 *    4. Activer le Bluetooth du telephone
 *    5. Attendre ~40 secondes
 *    6. Lire "==> RSSI_D0_BLE recommande : XX dBm"
 *    7. Copier cette valeur dans ton code principal
 * ============================================================
 */

#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

#define NB_MESURES_CIBLE  40
#define RSSI_MIN_SEUIL    -70   // Ignorer les signaux trop faibles

// ---- Variables globales ----
int   mesures[200];
int   nbMesures  = 0;
bool  calibDone  = false;
long  sommeRSSI  = 0;

// Meilleur appareil suivi
char  bestMAC[30]  = "";
int   bestRSSI     = -100;

// ---- Callback de calibration ----
class CalibCallback : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice dev) {
    if (calibDone) return;

    int rssi = dev.getRSSI();

    // Ignorer les valeurs aberrantes
    if (rssi < -95 || rssi > 0) return;

    const char* mac = dev.getAddress().toString().c_str();

    // Strategie : suivre l'appareil avec le signal le plus fort
    // = l'appareil le plus proche = notre telephone a 1m
    if (strlen(bestMAC) == 0) {
      // Premier appareil detecte
      strncpy(bestMAC, mac, 29);
      bestRSSI = rssi;
      Serial.printf("  [INFO] Premier appareil detecte : %s (%d dBm)\n",
        mac, rssi);

    } else if (strcmp(mac, bestMAC) == 0) {
      // Meme appareil — mettre a jour si RSSI plus fort
      if (rssi > bestRSSI) bestRSSI = rssi;

    } else if (rssi > bestRSSI + 8) {
      // Autre appareil BEAUCOUP plus fort → c'est lui qu'on cherche
      // On bascule et on recommence les mesures
      Serial.printf("\n  [SWITCH] Appareil plus fort : %s (%d dBm)\n", mac, rssi);
      Serial.printf("  [SWITCH] Abandon de %s (%d dBm)\n\n", bestMAC, bestRSSI);
      strncpy(bestMAC, mac, 29);
      bestRSSI  = rssi;
      nbMesures = 0;
      sommeRSSI = 0;
      return;

    } else {
      // Autre appareil moins fort → ignorer
      return;
    }

    // Ignorer les signaux trop faibles
    if (rssi < RSSI_MIN_SEUIL) return;

    // Enregistrer la mesure
    if (nbMesures < 200) {
      mesures[nbMesures] = rssi;
      sommeRSSI += rssi;
      nbMesures++;

      // Affichage progression toutes les 5 mesures
      if (nbMesures % 5 == 0) {
        float moy = (float)sommeRSSI / nbMesures;
        Serial.printf("  [%2d/%d] RSSI = %d dBm | Moy = %.1f dBm | %s\n",
          nbMesures, NB_MESURES_CIBLE, rssi, moy, bestMAC);
      }
    }
  }
};

BLEScan*       scanner;
CalibCallback* cb;
unsigned long  lastInfo = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("==========================================");
  Serial.println("   CALIBRATION RSSI_D0 — BLE");
  Serial.println("==========================================");
  Serial.println("  Instructions :");
  Serial.println("  1. Place le telephone a EXACTEMENT 1 metre");
  Serial.println("  2. Active le Bluetooth du telephone");
  Serial.println("  3. Attends ~40 secondes");
  Serial.printf ("  Seuil accepte : RSSI > %d dBm\n", RSSI_MIN_SEUIL);
  Serial.println("==========================================\n");

  BLEDevice::init("");
  scanner = BLEDevice::getScan();
  cb = new CalibCallback();
  scanner->setAdvertisedDeviceCallbacks(cb);

  // Scan ACTIF pour forcer les telephones a repondre
  scanner->setActiveScan(true);
  scanner->setInterval(100);
  scanner->setWindow(99);

  Serial.println("[OK] Scanner BLE demarre...\n");
}

void loop() {
  if (calibDone) return;

  // Lancer un scan de 3 secondes
  scanner->start(3, false);
  scanner->clearResults();

  // Message d'attente si rien detecte apres 10s
  if (millis() - lastInfo > 10000 && nbMesures == 0) {
    Serial.println("  [ATTENTE] Aucun signal BLE detecte...");
    Serial.println("  → Verifie que le Bluetooth du telephone est active");
    Serial.println("  → Essaie d'ouvrir les parametres Bluetooth");
    Serial.printf ("  → Seuil actuel : RSSI > %d dBm\n\n", RSSI_MIN_SEUIL);
    lastInfo = millis();
  }

  // Calcul du resultat quand assez de mesures
  if (!calibDone && nbMesures >= NB_MESURES_CIBLE) {
    calibDone = true;

    // Tri a bulles pour calculer la mediane
    for (int i = 0; i < nbMesures - 1; i++) {
      for (int j = 0; j < nbMesures - i - 1; j++) {
        if (mesures[j] > mesures[j + 1]) {
          int tmp    = mesures[j];
          mesures[j] = mesures[j + 1];
          mesures[j + 1] = tmp;
        }
      }
    }

    float moyenne = (float)sommeRSSI / nbMesures;
    int   mediane = mesures[nbMesures / 2];
    int   minVal  = mesures[0];
    int   maxVal  = mesures[nbMesures - 1];

    Serial.println("\n==========================================");
    Serial.println("   RESULTAT DE CALIBRATION BLE");
    Serial.println("==========================================");
    Serial.printf ("  Appareil suivi  : %s\n", bestMAC);
    Serial.printf ("  Nombre mesures  : %d\n", nbMesures);
    Serial.printf ("  Moyenne         : %.1f dBm\n", moyenne);
    Serial.printf ("  Mediane         : %d dBm\n", mediane);
    Serial.printf ("  Min / Max       : %d / %d dBm\n", minVal, maxVal);
    Serial.println();
    Serial.printf ("  ==> RSSI_D0_BLE recommande : %d dBm\n", mediane);
    Serial.println();
    Serial.println("  → Copie cette valeur dans ton code :");
    Serial.printf ("     #define RSSI_D0_BLE  %d.0f\n", mediane);
    Serial.println("==========================================");
    Serial.println("  Termine. Reset pour recommencer.");
  }

  delay(100);
}
