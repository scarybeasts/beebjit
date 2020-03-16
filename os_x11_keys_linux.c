#include "os_x11_keys.h"

#include "keyboard.h"

static int g_keys_mapping_built;
static uint8_t g_keys_mapping[256];

static void
os_x11_keys_build_mapping() {
  uint32_t i;

  for (i = 0; i < 256; ++i) {
    uint8_t val = 0;
    switch (i) {
    case 9:
      val = k_keyboard_key_escape;
      break;
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
    case 18:
      val = (i + '1' - 10);
      break;
    case 19:
      val = '0';
      break;
    case 20:
      val = '-';
      break;
    case 21:
      val = '=';
      break;
    case 22:
      val = k_keyboard_key_backspace;
      break;
    case 23:
      val = k_keyboard_key_tab;
      break;
    case 24:
      val = 'Q';
      break;
    case 25:
      val = 'W';
      break;
    case 26:
      val = 'E';
      break;
    case 27:
      val = 'R';
      break;
    case 28:
      val = 'T';
      break;
    case 29:
      val = 'Y';
      break;
    case 30:
      val = 'U';
      break;
    case 31:
      val = 'I';
      break;
    case 32:
      val = 'O';
      break;
    case 33:
      val = 'P';
      break;
    case 34:
      val = '[';
      break;
    case 35:
      val = ']';
      break;
    case 36:
      val = k_keyboard_key_enter;
      break;
    case 37:
      val = k_keyboard_key_ctrl_left;
      break;
    case 38:
      val = 'A';
      break;
    case 39:
      val = 'S';
      break;
    case 40:
      val = 'D';
      break;
    case 41:
      val = 'F';
      break;
    case 42:
      val = 'G';
      break;
    case 43:
      val = 'H';
      break;
    case 44:
      val = 'J';
      break;
    case 45:
      val = 'K';
      break;
    case 46:
      val = 'L';
      break;
    case 47:
      val = ';';
      break;
    case 48:
      val = '\'';
      break;
    case 50:
      val = k_keyboard_key_shift_left;
      break;
    case 51:
      val = '\\';
      break;
    case 52:
      val = 'Z';
      break;
    case 53:
      val = 'X';
      break;
    case 54:
      val = 'C';
      break;
    case 55:
      val = 'V';
      break;
    case 56:
      val = 'B';
      break;
    case 57:
      val = 'N';
      break;
    case 58:
      val = 'M';
      break;
    case 59:
      val = ',';
      break;
    case 60:
      val = '.';
      break;
    case 61:
      val = '/';
      break;
    case 62:
      val = k_keyboard_key_shift_right;
      break;
    case 64:
      val = k_keyboard_key_alt_left;
      break;
    case 65:
      val = ' ';
      break;
    case 66:
      val = k_keyboard_key_caps_lock;
      break;
    case 67:
      val = k_keyboard_key_f1;
      break;
    case 68:
      val = k_keyboard_key_f2;
      break;
    case 69:
      val = k_keyboard_key_f3;
      break;
    case 70:
      val = k_keyboard_key_f4;
      break;
    case 71:
      val = k_keyboard_key_f5;
      break;
    case 72:
      val = k_keyboard_key_f6;
      break;
    case 73:
      val = k_keyboard_key_f7;
      break;
    case 74:
      val = k_keyboard_key_f8;
      break;
    case 75:
      val = k_keyboard_key_f9;
      break;
    case 76:
      val = k_keyboard_key_f0;
      break;
    case 95:
      val = k_keyboard_key_f11;
      break;
    case 96:
      val = k_keyboard_key_f12;
      break;
    case 111:
      val = k_keyboard_key_arrow_up;
      break;
    case 113:
      val = k_keyboard_key_arrow_left;
      break;
    case 114:
      val = k_keyboard_key_arrow_right;
      break;
    case 115:
      val = k_keyboard_key_end;
      break;
    case 116:
      val = k_keyboard_key_arrow_down;
      break;
    default:
      break;
    }
    g_keys_mapping[i] = val;
  }
}

uint8_t*
os_x11_keys_get_mapping(void* p_display) {
  (void) p_display;
  
  if (!g_keys_mapping_built) {
    os_x11_keys_build_mapping();
    g_keys_mapping_built = 1;
  }

  return &g_keys_mapping[0];
}
