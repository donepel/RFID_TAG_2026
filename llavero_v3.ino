// Emulador EM4100 - ATtiny85 @ 16MHz PLL
// PB0 (pin 5) -> resistencia 10k -> base 2N3904 -> bobina
// Pulsadores: PB1, PB2, PB3, PB4 -> GND (INPUT_PULLUP)
//
// BTN 1-3: emula tarjeta fija (igual que v2)
// BTN 4:   brute force sobre lista de credenciales conocidas
//          itera una por una mientras el boton este presionado
//          cadaframe se transmite N veces antes de pasar a la siguiente

#define COIL_PIN    0   // PB0, pin fisico 5
#define BTN_1       3   // PB3, pin fisico 2
#define BTN_2       4   // PB4, pin fisico 3
#define BTN_3       1   // PB1, pin fisico 6
#define BTN_4       2   // PB2, pin fisico 7

#define HALF_BIT_US 256 // 16MHz PLL interno

// cuantas veces se repite cada credencial antes de pasar a la siguiente
#define REPS_PER_CARD 10

// *** CONFIGURAR ACA LOS 3 IDs FIJOS ***
#define FAC_1  184
#define ID_1   9706

#define FAC_2  200
#define ID_2   1234

#define FAC_3  100
#define ID_3   5678
// ***************************************

struct Card {
  uint16_t facility;
  uint32_t id;
};

const Card CARDS[3] = {
  { FAC_1, ID_1 },
  { FAC_2, ID_2 },
  { FAC_3, ID_3 }
};

const uint8_t BTNS[4] = { BTN_1, BTN_2, BTN_3, BTN_4 };

// *** LISTA DE CREDENCIALES PARA BRUTE FORCE ***
// Completar con los valores relevados de tus tarjetas de prueba
// Formato: { facility, id }
const Card BF_LIST[] = {
  {   0,     0 },
  {   0,     1 },
  { 255, 65535 },
  // --- agregar aca los valores reales ---
};
const uint16_t BF_COUNT = sizeof(BF_LIST) / sizeof(BF_LIST[0]);
// **********************************************

int tag[64];

uint8_t parity_nibble(uint8_t nibble) {
  uint8_t p = 0;
  for (int i = 0; i < 4; i++) p ^= (nibble >> i) & 1;
  return p;
}

void build_em4100_frame(uint16_t facility, uint32_t id) {
  uint8_t data[4] = {
    (uint8_t)((facility >> 8) & 0xFF),
    (uint8_t)( facility       & 0xFF),
    (uint8_t)((id       >> 8) & 0xFF),
    (uint8_t)( id             & 0xFF)
  };

  uint8_t bytes[5] = { 0x06, data[0], data[1], data[2], data[3] };
  uint8_t col_par[4] = {0, 0, 0, 0};
  int idx = 0;

  for (int i = 0; i < 9; i++) tag[idx++] = 1;

  for (int b = 0; b < 5; b++) {
    uint8_t hi = (bytes[b] >> 4) & 0x0F;
    for (int i = 3; i >= 0; i--) {
      uint8_t bit = (hi >> i) & 1;
      tag[idx++] = bit;
      col_par[3 - i] ^= bit;
    }
    tag[idx++] = parity_nibble(hi);

    uint8_t lo = bytes[b] & 0x0F;
    for (int i = 3; i >= 0; i--) {
      uint8_t bit = (lo >> i) & 1;
      tag[idx++] = bit;
      col_par[3 - i] ^= bit;
    }
    tag[idx++] = parity_nibble(lo);
  }

  for (int i = 0; i < 4; i++) tag[idx++] = col_par[i];
  tag[idx++] = 0;
}

void send_manchester_bit(int bit) {
  int m = 1 ^ bit;
  digitalWrite(COIL_PIN, m ? LOW : HIGH);
  delayMicroseconds(HALF_BIT_US);
  digitalWrite(COIL_PIN, m ? HIGH : LOW);
  delayMicroseconds(HALF_BIT_US);
}

void send_frame() {
  for (int rep = 0; rep < 5; rep++) {
    for (int i = 0; i < 64; i++) {
      send_manchester_bit(tag[i]);
    }
  }
}

void do_brute_force() {
  for (uint16_t i = 0; i < BF_COUNT; i++) {
    // Si sueltan el boton, cortar
    if (digitalRead(BTN_4) == HIGH) break;

    build_em4100_frame(BF_LIST[i].facility, BF_LIST[i].id);

    for (int r = 0; r < REPS_PER_CARD; r++) {
      if (digitalRead(BTN_4) == HIGH) break;
      send_frame();
      delay(10);
    }
  }
}

void setup() {
  pinMode(COIL_PIN, OUTPUT);
  digitalWrite(COIL_PIN, LOW);
  for (int i = 0; i < 4; i++) {
    pinMode(BTNS[i], INPUT_PULLUP);
  }
}

void loop() {
  // BTN 1-3: tarjetas fijas
  for (int i = 0; i < 3; i++) {
    if (digitalRead(BTNS[i]) == LOW) {
      build_em4100_frame(CARDS[i].facility, CARDS[i].id);
      while (digitalRead(BTNS[i]) == LOW) {
        send_frame();
        delay(10);
      }
      digitalWrite(COIL_PIN, LOW);
      break;
    }
  }

  // BTN 4: brute force
  if (digitalRead(BTN_4) == LOW) {
    do_brute_force();
    digitalWrite(COIL_PIN, LOW);
  }
}
