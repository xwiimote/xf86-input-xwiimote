#ifndef WIIMOTE
#define WIIMOTE

#include <xwiimote.h>

#include "key.h"
#include "ir.h"
#include "accelerometer.h"
#include "motionplus.h"

enum wiimote_key {
  WIIMOTE_KEY_LEFT,
  WIIMOTE_KEY_RIGHT,
  WIIMOTE_KEY_UP,
  WIIMOTE_KEY_DOWN,
  WIIMOTE_KEY_A,
  WIIMOTE_KEY_B,
  WIIMOTE_KEY_PLUS,
  WIIMOTE_KEY_MINUS,
  WIIMOTE_KEY_HOME,
  WIIMOTE_KEY_ONE,
  WIIMOTE_KEY_TWO,
  WIIMOTE_KEY_NUM
};

struct wiimote {
  struct key keys[WIIMOTE_KEY_NUM];
  struct ir ir;
  struct accelerometer accelerometer;
  struct motionplus motionplus;
};

struct wiimote_config {
  struct key_config keys[WIIMOTE_KEY_NUM];
  struct ir_config ir;
  struct accelerometer_config accelerometer;
  struct motionplus_config motionplus;
};

void preinit_wiimote(struct wiimote_config *config);
void configure_wiimote(struct wiimote_config *config, char const * prefix, struct wiimote_config *defaults, InputInfoPtr info);

BOOL wiimote_ir_is_active(struct wiimote *wiimote, struct wiimote_config *config, struct xwii_event *ev);

void handle_wiimote_key(struct wiimote *wiimote, struct wiimote_config *config, struct xwii_event *ev, unsigned int state, InputInfoPtr info);
void handle_wiimote_ir(struct wiimote *wiimote, struct wiimote_config *config, struct xwii_event *ev, unsigned int state, InputInfoPtr info);
void handle_wiimote_motionplus(struct wiimote *wiimote, struct wiimote_config *config, struct xwii_event *ev, unsigned int state, InputInfoPtr info);
void handle_wiimote_accelerometer(struct wiimote *wiimote, struct wiimote_config *config, struct xwii_event *ev, unsigned int state, InputInfoPtr info);


#endif
