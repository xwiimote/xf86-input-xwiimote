#ifndef ANALOG_STICK
#define ANALOG_STICK

#define ANALOG_STICK_SLICE_CENTER_ANGLE 360 / 8
#define ANALOG_STICK_SLICE_OUTER_ANGLE (180 - ANALOG_STICK_SLICE_CENTER_ANGLE) / 2

#include "key.h"
#include "analog-stick-axis.h"

enum analog_stick_shape {
  ANALOG_STICK_SHAPE_CIRCLE,
  ANALOG_STICK_SHAPE_OCTEGON,
  ANALOG_STICK_SHAPE_NUM,
};

struct analog_stick {
	struct analog_stick_axis x;
	struct analog_stick_axis y;
  unsigned int state;
};

struct analog_stick_config {
  enum analog_stick_shape shape;
	struct analog_stick_axis_config x;
	struct analog_stick_axis_config y;
};

void configure_analog_stick(struct analog_stick_config *config,	char const *name, InputInfoPtr info);
void handle_analog_stick(struct analog_stick *stick, struct analog_stick_config *config, struct xwii_event *ev, int stick_index, unsigned int state, InputInfoPtr info);

#endif
