/*
 * ============================================================
 *  DETECTION WIFI
 * ============================================================
 */

#include <WiFi.h>          
#include <esp_wifi.h>
#include <esp_event.h>

// Structure paquet WiFi brut
typedef struct {
  unsigned frame_ctrl : 16;
  unsigned duration   : 16;
  uint8_t  addr1[6];
  uint8_t  addr2[6];   // MAC source = le telephone
  uint8_t  addr3[6];
  unsigned sequence   : 16;
} wifi_ieee80211_mac_hdr_t;

typedef struct {
  wifi_ieee80211_mac_hdr_t hdr;
  uint8_t payload[0];
} wifi_ieee80211_packet_t;

// ---- Table de deduplication des MACs ----
#define MAX_MACS 20
struct KnownMAC {
  char mac[18];
  int  rssi;
  unsigned long lastSeen;
  int count;  // Combien de fois detecte
};
KnownMAC knownMacs[MAX_MACS];
int numKnown = 0;

// Chercher si une MAC est deja connue
// Retourne l'index ou -1 si nouvelle
int findMAC(const char* mac) {
  for (int i = 0; i < numKnown; i++) {
    if (strcmp(knownMacs[i].mac, mac) == 0) return i;
  }
  return -1;
}

// Ajouter ou mettre a jour une MAC
bool addOrUpdateMAC(const char* mac, int rssi) {
  int idx = findMAC(mac);
  if (idx >= 0) {
    // Deja connue — mise a jour
    knownMacs[idx].rssi     = rssi;
    knownMacs[idx].lastSeen = millis();
    knownMacs[idx].count++;
    return false;  // Pas nouveau
  }
  // Nouvelle MAC
  if (numKnown < MAX_MACS) {
    strncpy(knownMacs[numKnown].mac, mac, 17);
    knownMacs[numKnown].rssi     = rssi;
    knownMacs[numKnown].lastSeen = millis();
    knownMacs[numKnown].count    = 1;
    numKnown++;
    return true;  // Nouveau 
  }
  return false;
}

// ---- Formule de distance corrigee ----
// Modele log-distance : RSSI(d) = RSSI(d0) - 10*n*log10(d/d0)
// d0 = 1m (distance de reference)
// RSSI(d0) = -40 dBm (valeur mesure typique WiFi a 1 metre)
// n = 2.5 (coefficient de perte en interieur)
// => d = d0 * 10^((RSSI(d0) - RSSI) / (10*n))
float estimateDistance(int rssi) {
  const float rssi_d0 = -48.0;  // RSSI mesure a 1 metre
  const float n       =   2.5;  // Coefficient perte interieur
  const float d0      =   1.0;  // Distance de reference (m)

  float d = d0 * pow(10.0, (rssi_d0 - rssi) / (10.0 * n));
  return d;
}

// ---- Barre de signal visuelle ----
String signalBar(int rssi) {
  // Convertir RSSI en barres (0 a 5)
  // -30 dBm = 5 barres, -90 dBm = 0 barre
  int bars = constrain(map(rssi, -90, -30, 0, 5), 0, 5);
  String bar = "[";
  for (int i = 0; i < 5; i++) bar += (i < bars) ? "|" : ".";
  bar += "]";
  return bar;
}

// ---- Callback principal ----
void IRAM_ATTR wifi_sniffer_packet_handler(
    void* buff,
    wifi_promiscuous_pkt_type_t type)
{
  if (type != WIFI_PKT_MGMT) return;

  const wifi_promiscuous_pkt_t* pkt =
      (wifi_promiscuous_pkt_t*)buff;
  const wifi_ieee80211_packet_t* ipkt =
      (wifi_ieee80211_packet_t*)pkt->payload;
  const wifi_ieee80211_mac_hdr_t* hdr = &ipkt->hdr;

  // 0x0040 = Probe Request uniquement
  if ((hdr->frame_ctrl & 0x00FC) != 0x0040) return;

  int rssi = pkt->rx_ctrl.rssi;

  // Seuil -80 dBm 
  if (rssi < -80) return;

  // Construire la chaine MAC
  char mac[18];
  snprintf(mac, sizeof(mac),
    "%02X:%02X:%02X:%02X:%02X:%02X",
    hdr->addr2[0], hdr->addr2[1], hdr->addr2[2],
    hdr->addr2[3], hdr->addr2[4], hdr->addr2[5]);

  // Extraire le SSID demande
  char ssid[33] = "[broadcast]";
  if (ipkt->payload[0] == 0x00 && ipkt->payload[1] > 0
      && ipkt->payload[1] <= 32) {
    memcpy(ssid, &ipkt->payload[2], ipkt->payload[1]);
    ssid[ipkt->payload[1]] = '\0';
  }

  // Distance 
  float dist = estimateDistance(rssi);

  // Ajouter ou mettre a jour
  bool isNew = addOrUpdateMAC(mac, rssi);

  // Les connus sont affiches dans le statut toutes les 5s
  if (isNew) {
    Serial.println("\n========================================");
    Serial.println("  [WiFi] NOUVEL APPAREIL DETECTE !");
    Serial.println("========================================");
    Serial.print  ("  MAC        : "); Serial.println(mac);
    Serial.print  ("  RSSI       : "); Serial.print(rssi); Serial.println(" dBm");
    Serial.print  ("  Signal     : "); Serial.println(signalBar(rssi));
    Serial.print  ("  Canal      : "); Serial.println(pkt->rx_ctrl.channel);
    Serial.print  ("  SSID voulu : "); Serial.println(ssid);
    Serial.print  ("  Distance ~ : "); Serial.print(dist, 1); Serial.println(" m");
    Serial.print  ("  Appareils  : "); Serial.println(numKnown);
  }
}

// ---- Channel hopping ----
const uint8_t channels[] = {1, 6, 11, 2, 3, 4, 5, 7, 8, 9, 10, 12, 13};
const int numChannels = 13;
int currentChannel = 0;
unsigned long lastChannelSwitch = 0;
unsigned long lastStatus = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("==========================================");
  Serial.println("  DETECTEUR WiFi ");
  Serial.println("  Seminaire Detection des signaux Radiomobile");
  Serial.println("==========================================");
  Serial.println("  Formule distance : d = 10^((RSSI_ref - RSSI) / (10*n))");
  Serial.println("  RSSI_ref = -48 dBm (a 1m) | n = 2.5");
  Serial.println("==========================================\n");

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(&wifi_sniffer_packet_handler);
  esp_wifi_set_channel(channels[0], WIFI_SECOND_CHAN_NONE);

  Serial.println("[OK] Scan demarre\n");
}

void loop() {
  unsigned long now = millis();

  // Channel hopping toutes les 200ms
  if (now - lastChannelSwitch > 200) {
    currentChannel = (currentChannel + 1) % numChannels;
    esp_wifi_set_channel(channels[currentChannel], WIFI_SECOND_CHAN_NONE);
    lastChannelSwitch = now;
  }

  // Tableau de bord toutes les 5 secondes
  if (now - lastStatus > 5000) {
    Serial.println("\n---- APPAREILS ACTIFS ----");
    int actifs = 0;
    for (int i = 0; i < numKnown; i++) {
      // Ignorer ceux non vus depuis 60s
      if (now - knownMacs[i].lastSeen > 60000) continue;
      actifs++;
      float dist = estimateDistance(knownMacs[i].rssi);
      Serial.printf("  [%d] %s | %d dBm %s | ~%.1f m | vu %dx\n",
        actifs,
        knownMacs[i].mac,
        knownMacs[i].rssi,
        signalBar(knownMacs[i].rssi).c_str(),
        dist,
        knownMacs[i].count
      );
    }
    if (actifs == 0) Serial.println("  Aucun appareil actif");
    Serial.printf("  Canal actuel : CH%d\n", channels[currentChannel]);
    Serial.println("--------------------------");
    lastStatus = now;
  }
}