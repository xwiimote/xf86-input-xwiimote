#ifndef ANALOG_STICK_AXIS
#define ANALOG_STICK_AXIS

#define ANALOG_STICK_AXIS_AMPLIFY_DEFAULT 3
#define ANALOG_STICK_AXIS_DEADZONE_DEFAULT 40

#include "key.h"

enum analog_stick_axis_mode {
	ANALOG_STICK_AXIS_MODE_NONE,
	ANALOG_STICK_AXIS_MODE_ABSOLUTE,
	ANALOG_STICK_AXIS_MODE_RELATIVE,
};

struct analog_stick_axis {
	int32_t previous_value;
	double amplified;
	int32_t delta;
	int32_t previous_delta; /*for ANALOG_STICK_MOTION_TYPE_ABSOLUTE*/
	double subpixel; /*for ANALOG_STICK_MOTION_TYPE_RELATIVE*/
  struct key high;
  struct key low;
  unsigned int state;
};

struct analog_stick_axis_config {
  char name[100];
	int mode;
	struct key_config high;
	struct key_config low;
	int32_t deadzone;
	double amplify;
};

void configure_analog_stick_axis(struct analog_stick_axis_config *config, char const *option_key, InputInfoPtr info);
void handle_analog_stick_axis(struct analog_stick_axis *axis, struct analog_stick_axis_config *config, int32_t value, int state, InputInfoPtr info, int first_valuator);

#endif
