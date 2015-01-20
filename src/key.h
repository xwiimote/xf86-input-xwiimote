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
void press_key(struct key *key, struct key_config *config, unsigned int state, InputInfoPtr info);
void depress_key(struct key key, struct key_config *config, InputInfoPtr info);
void configure_key(struct key_config *config, char const *name, char const *value, InputInfoPtr info);

#endif
