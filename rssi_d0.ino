/*
 * ============================================================
 *  CALIBRATION rssi_d0 — ESP32
 *  Version sans MAC fixe — prend le signal le plus fort
 *  
 *  Instructions :
 *    1. Flasher le code sur l'ESP32
 *    2. Ouvrir le moniteur série à 115200 baud
 *    3. Placer le téléphone à EXACTEMENT 1 mètre de l'ESP32
 *    4. Activer le WiFi 
 *    5. Eloigner tout autre appareil WiFi 
 *    6. Attendre ~30 secondes
 *    7. Lire la ligne "==> RSSI_D0 recommandé : XX dBm"
 * ============================================================
 */

#include <WiFi.h>
#include <esp_wifi.h>

// Nombre de mesures cible avant de calculer le résultat
#define NB_MESURES_CIBLE 40

// Seuil minimum : on ne garde que les signaux forts
// = uniquement l'appareil le plus proche (téléphone à 1m)
// Les signaux faibles (autres appareils lointains) sont ignorés
#define RSSI_MIN_SEUIL -65

// ---- Variables globales ----
int   mesures[200];
int   nbMesures  = 0;
bool  calibDone  = false;
long  sommeRSSI  = 0;

// MAC et RSSI du meilleur appareil detecte
char  bestMAC[18]    = "";
int   bestRSSI       = -100;
int   bestCount      = 0;

// ---- Structure paquet WiFi ----
typedef struct {
  unsigned frame_ctrl : 16;
  unsigned duration   : 16;
  uint8_t  addr1[6];
  uint8_t  addr2[6];
  uint8_t  addr3[6];
  unsigned sequence   : 16;
} wifi_ieee80211_mac_hdr_t;

typedef struct {
  wifi_ieee80211_mac_hdr_t hdr;
  uint8_t payload[0];
} wifi_ieee80211_packet_t;

// ---- Callback sniffer ----
void IRAM_ATTR sniffer_cb(void* buff, wifi_promiscuous_pkt_type_t type) {
  if (calibDone) return;
  if (type != WIFI_PKT_MGMT) return;

  const wifi_promiscuous_pkt_t*   pkt  = (wifi_promiscuous_pkt_t*)buff;
  const wifi_ieee80211_packet_t*  ipkt = (wifi_ieee80211_packet_t*)pkt->payload;
  const wifi_ieee80211_mac_hdr_t* hdr  = &ipkt->hdr;

  // Probe Request uniquement
  if ((hdr->frame_ctrl & 0x00FC) != 0x0040) return;

  int rssi = pkt->rx_ctrl.rssi;

  // Ignorer les valeurs aberrantes
  if (rssi < -95 || rssi > 0) return;

  // Construire la MAC source
  char mac[18];
  snprintf(mac, sizeof(mac),
    "%02X:%02X:%02X:%02X:%02X:%02X",
    hdr->addr2[0], hdr->addr2[1], hdr->addr2[2],
    hdr->addr2[3], hdr->addr2[4], hdr->addr2[5]);

  // Strategie : on suit la MAC qui a le RSSI le plus fort
  // Si c'est une nouvelle MAC plus forte → on bascule sur elle
  // Cela gere automatiquement les MACs aleatoires
  if (strlen(bestMAC) == 0) {
    // Premier appareil detecte → on le prend
    strncpy(bestMAC, mac, 17);
    bestRSSI = rssi;
  } else if (strcmp(mac, bestMAC) == 0) {
    // Meme MAC → mettre a jour le RSSI max observe
    if (rssi > bestRSSI) bestRSSI = rssi;
    bestCount++;
  } else if (rssi > bestRSSI + 10) {
    // Autre MAC BEAUCOUP plus forte (+10 dBm) → c'est un autre appareil
    // plus proche, on bascule dessus et on repart de zero
    Serial.printf("\n  [INFO] Nouvel appareil plus fort detecte : %s (%d dBm)\n",
      mac, rssi);
    Serial.printf("  [INFO] Basculement depuis %s (%d dBm)\n\n",
      bestMAC, bestRSSI);
    strncpy(bestMAC, mac, 17);
    bestRSSI  = rssi;
    bestCount = 0;
    // Remettre les mesures a zero
    nbMesures = 0;
    sommeRSSI = 0;
    return;
  } else {
    // Autre MAC moins forte → ignorer
    return;
  }

  // Ne garder que les signaux au-dessus du seuil
  // pour eviter les parasites lointains
  if (rssi < RSSI_MIN_SEUIL) return;

  // Enregistrer la mesure
  if (nbMesures < 200) {
    mesures[nbMesures] = rssi;
    sommeRSSI += rssi;
    nbMesures++;

    // Affichage progression toutes les 5 mesures
    if (nbMesures % 5 == 0) {
      float moy = (float)sommeRSSI / nbMesures;
      Serial.printf("  [%2d/%d] RSSI = %d dBm | Moy = %.1f dBm | MAC = %s\n",
        nbMesures, NB_MESURES_CIBLE, rssi, moy, bestMAC);
    }
  }
}

// ---- Channel hopping ----
const uint8_t channels[] = {1,6,11,2,3,4,5,7,8,9,10,12,13};
const int numChannels = 13;
int currentChannel = 0;
unsigned long lastSwitch = 0;
unsigned long lastInfo   = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("==========================================");
  Serial.println("   CALIBRATION rssi_d0 — Sans MAC fixe");
  Serial.println("==========================================");
  Serial.println("  Methode : signal le plus fort = ton telephone");
  Serial.println("  Seuil accepte : RSSI > " + String(RSSI_MIN_SEUIL) + " dBm");
  Serial.println();
  Serial.println("  Instructions :");
  Serial.println("  1. Place le telephone a EXACTEMENT 1 metre");
  Serial.println("  2. Active le WiFi (ne pas connecter)");
  Serial.println("  3. Eloigne les autres appareils WiFi si possible");
  Serial.println("  4. Attends ~30 secondes");
  Serial.println("==========================================\n");

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(&sniffer_cb);
  esp_wifi_set_channel(channels[0], WIFI_SECOND_CHAN_NONE);
}

void loop() {
  unsigned long now = millis();

  // Channel hopping toutes les 150ms
  if (now - lastSwitch > 150) {
    currentChannel = (currentChannel + 1) % numChannels;
    esp_wifi_set_channel(channels[currentChannel], WIFI_SECOND_CHAN_NONE);
    lastSwitch = now;
  }

  // Message d'attente si aucune mesure apres 10 secondes
  if (now - lastInfo > 10000 && nbMesures == 0) {
    Serial.println("  [ATTENTE] Aucun signal detecte...");
    Serial.println("  → Verifie que le WiFi du telephone est bien active");
    Serial.println("  → Verifie que le telephone est a 1 metre de l'ESP32");
    Serial.printf ("  → Seuil actuel : RSSI > %d dBm\n\n", RSSI_MIN_SEUIL);
    lastInfo = now;
  }

  // Résultat final quand on a assez de mesures
  if (!calibDone && nbMesures >= NB_MESURES_CIBLE) {
    calibDone = true;
    esp_wifi_set_promiscuous(false);

    // Tri a bulles pour calculer la mediane
    for (int i = 0; i < nbMesures - 1; i++) {
      for (int j = 0; j < nbMesures - i - 1; j++) {
        if (mesures[j] > mesures[j+1]) {
          int tmp    = mesures[j];
          mesures[j] = mesures[j+1];
          mesures[j+1] = tmp;
        }
      }
    }

    float moyenne = (float)sommeRSSI / nbMesures;
    int   mediane = mesures[nbMesures / 2];
    int   minVal  = mesures[0];
    int   maxVal  = mesures[nbMesures - 1];

    Serial.println("\n==========================================");
    Serial.println("   RESULTAT DE CALIBRATION");
    Serial.println("==========================================");
    Serial.printf ("  Appareil suivi  : %s\n",  bestMAC);
    Serial.printf ("  Nombre mesures  : %d\n",  nbMesures);
    Serial.printf ("  Moyenne         : %.1f dBm\n", moyenne);
    Serial.printf ("  Mediane         : %d dBm\n",   mediane);
    Serial.printf ("  Min / Max       : %d / %d dBm\n", minVal, maxVal);
    Serial.println();
    Serial.printf ("  ==> RSSI_D0 recommande : %d dBm\n", mediane);
    Serial.println();
    Serial.println("==========================================");
    Serial.println("  Calibration terminee. Reset pour recommencer.");
    Serial.println("==========================================");
  }
}