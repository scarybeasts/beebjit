#include "os_x11_keys.h"

#include <X11/Xlib.h>

struct mapping_struct {
  const char *x_key_name;
  uint8_t key;
};

static const struct mapping_struct s_x_keys_mapping[] = {
  {"Escape", k_keyboard_key_escape},
  {"0", '0'},
  {"1", '1'},
  {"2", '2'},
  {"3", '3'},
  {"4", '4'},
  {"5", '5'},
  {"6", '6'},
  {"7", '7'},
  {"8", '8'},
  {"9", '9'},
  {"minus", '-'},
  {"equal", '='},
  {"BackSpace", k_keyboard_key_backspace},

  {"Tab", k_keyboard_key_tab},
  {"Q", 'Q'},
  {"W", 'W'},
  {"E", 'E'},
  {"R", 'R'},
  {"T", 'T'},
  {"Y", 'Y'},
  {"U", 'U'},
  {"I", 'I'},
  {"O", 'O'},
  {"P", 'P'},
  {"bracketleft", '['},
  {"bracketright", ']'},
  {"backslash", '\\'},

  {"Caps_Lock", k_keyboard_key_caps_lock},
  {"A", 'A'},
  {"S", 'S'},
  {"D", 'D'},
  {"F", 'F'},
  {"G", 'G'},
  {"H", 'H'},
  {"J", 'J'},
  {"K", 'K'},
  {"L", 'L'},
  {"semicolon", ';'},
  {"apostrophe", '\''},
  {"Return", k_keyboard_key_enter},

  {"Shift_L", k_keyboard_key_shift_left},
  {"Z", 'Z'},
  {"X", 'X'},
  {"C", 'C'},
  {"V", 'V'},
  {"B", 'B'},
  {"N", 'N'},
  {"M", 'M'},
  {"comma", ','},
  {"period", '.'},
  {"slash", '/'},
  {"Shift_R", k_keyboard_key_shift_right},
  
  {"Control_L", k_keyboard_key_ctrl_left},
  {"Meta_L", k_keyboard_key_alt_left},
  {"space", ' '},

  {"F1", k_keyboard_key_f1},
  {"F2", k_keyboard_key_f2},
  {"F3", k_keyboard_key_f3},
  {"F4", k_keyboard_key_f4},
  {"F5", k_keyboard_key_f5},
  {"F6", k_keyboard_key_f6},
  {"F7", k_keyboard_key_f7},
  {"F8", k_keyboard_key_f8},
  {"F9", k_keyboard_key_f9},
  {"F10", k_keyboard_key_f0},
  {"F11", k_keyboard_key_f11},
  {"F12", k_keyboard_key_f12},

  {"Up",  k_keyboard_key_arrow_up},
  {"Down", k_keyboard_key_arrow_down},
  {"Left", k_keyboard_key_arrow_left},
  {"Right", k_keyboard_key_arrow_right},

  {"End", k_keyboard_key_end},
  
  {NULL},
};

static int s_keys_mapping_built;
static uint8_t s_keys_mapping[256];

/* XKeysymToKeycode only returns one keycode - but a keysym can map to multiple
 * keycodes. So this code does an exhaustive search to find everything.
 *
 * There's probably a better way of doing this.
 */
static void
init_mapping(Display* display) {
  const struct mapping_struct* p_mapping;
  int keycode;
  int min_keycode, max_keycode;
  /* X keycodes are byte values, so 256 entries is sufficient. */
  KeySym *keysyms[256];
  int num_keysyms[256];

  XDisplayKeycodes(display,&min_keycode,&max_keycode);

  /* XGetKeyboardMapping took long enough on my Mac that it seems to be worth
   * calling it for each keycode outside the loop. (The endless calls to
   * XKeysymToString in the inner loop below, on the other hand, aren't
   * obviously causing a problem.)
   */
  for (keycode = min_keycode; keycode <= max_keycode; ++keycode) {
    keysyms[keycode] = XGetKeyboardMapping(display,
                                           keycode,
                                           1,
                                           &num_keysyms[keycode]);                    
  }

  for (p_mapping = s_x_keys_mapping; p_mapping->x_key_name != NULL; ++p_mapping) {
    int found_x_key = 0;
    
    for (keycode = min_keycode; keycode <= max_keycode; ++keycode) {
      for (int i = 0; i < num_keysyms[keycode]; ++i) {
        char *keysym_name = XKeysymToString(keysyms[keycode][i]);
        if (keysym_name != NULL) {
          if (strcmp(keysym_name, p_mapping->x_key_name) == 0) {
            /* Can multiple keysyms map to the same keycode? Should probably
             * print a warning if that happens. */
            s_keys_mapping[keycode] = p_mapping->key;

            found_x_key = 1;

            /* Continue checking key codes. The same X key name might map to
             * multiple physical keys. */
            break;
          }
        }
      }
    }
    
    if (!found_x_key) {
      /* Not an error... right? This key presumably just isn't on the
       * keyboard. */
      log_do_log(k_log_misc, k_log_info, "X key not available: %s", p_mapping->x_key_name);
    }
  }
  
  for (keycode = min_keycode; keycode <= max_keycode; ++keycode) {
    XFree(keysyms[keycode]);
    keysyms[keycode] = NULL;
  }
}

uint8_t*
os_x11_keys_get_mapping(void* display) {
  if(!s_keys_mapping_built) {
    init_mapping(display);
   
    s_keys_mapping_built=1;
  }

  return s_keys_mapping;
}
