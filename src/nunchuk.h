#ifndef NUNCHUK
#define NUNCHUK

#include <xwiimote.h>

#include "analog-stick.h"

#define NUNCHUK_ANALOG_STICK_INDEX 0

enum nunchuk_key {
  NUNCHUK_KEY_C,
  NUNCHUK_KEY_Z,
  NUNCHUK_KEY_NUM,
};

struct nunchuk {
  struct analog_stick analog_stick;
  struct key keys[NUNCHUK_KEY_NUM];
};


struct nunchuk_config {
  struct analog_stick_config analog_stick;
  struct key_config keys[NUNCHUK_KEY_NUM];
};

void configure_nunchuk(struct nunchuk_config *config, char const * prefix, struct nunchuk_config *defaults, InputInfoPtr info);

void handle_nunchuk_analog_stick_event(struct nunchuk *nunchuk, struct nunchuk_config *config, struct xwii_event *ev, unsigned int state, InputInfoPtr info);
void handle_nunchuk_key_event(struct nunchuk *nunchuk, struct nunchuk_config *config, struct xwii_event *ev, unsigned int state, InputInfoPtr info);

unsigned int xwii_key_to_nunchuk_key(unsigned int keycode, InputInfoPtr info);

#endif
