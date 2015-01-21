#ifndef KEY
#define KEY

#include <xwiimote.h>
#include <xf86Xinput.h>

#define MIN_KEYCODE 8

enum key_type {
	FUNC_IGNORE,
	FUNC_BTN,
	FUNC_KEY,
};

enum key_state {
  KEY_STATE_RELEASED,
	KEY_STATE_PRESSED,
	KEY_STATE_PRESSED_WITH_IR,
	KEY_STATE_NUM
};

struct key_config {
  char name[100];
	int type;
	union {
		int btn;
		unsigned int key;
	} u;
};

struct key {
  unsigned int state;
};

void handle_key(struct key *key, struct key_config *config, unsigned int state, InputInfoPtr info);

void configure_key_by_value(struct key_config *config, char const *name, char const *value, InputInfoPtr info);
void configure_key(struct key_config *config, char const *option_key, InputInfoPtr info);

#endif
