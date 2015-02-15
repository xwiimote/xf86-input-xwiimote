#ifndef KEY
#define KEY

#include <xwiimote.h>
#include <xf86Xinput.h>

#define MIN_KEYCODE 8
#define BUTTON_LEFT 1
#define BUTTON_MIDDLE 2
#define BUTTON_RIGHT 3
#define BUTTON_WHEELUP 4
#define BUTTON_WHEELDOWN 5

enum key_type {
	FUNC_IGNORE,
	FUNC_BTN,
	FUNC_KEY,
};

enum key_ir_mode {
  KEY_IR_MODE_IGNORE,
  KEY_IR_MODE_RELATIVE,
  KEY_IR_MODE_ABSOLUTE,
  KEY_IR_MODE_TOGGLE,
};

enum key_state {
  KEY_STATE_RELEASED = 0,
	KEY_STATE_PRESSED,
	KEY_STATE_PRESSED_WITH_IR,
	KEY_STATE_NUM
};

struct key_config {
	int type;
	union {
		int btn;
		unsigned int key;
	} u;
  unsigned int ir_mode;
};

struct key {
  unsigned int state;
};

void handle_key_event(struct key *key, struct key_config *config, unsigned int state, InputInfoPtr info);

void configure_key_by_value(struct key_config *config, char const *name, char const *value, InputInfoPtr info);
void configure_key(struct key_config *config, char const *option_key, InputInfoPtr info);

#endif
