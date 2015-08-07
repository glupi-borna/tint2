/**************************************************************************
* Copyright (C) 2009-2015 Sebastian Reichel <sre@ring0.de>
*
* Battery with functional data (percentage, time to life) and drawing data
* (area, font, ...). Each panel use his own drawing data.
*
**************************************************************************/

#ifndef BATTERY_H
#define BATTERY_H

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "common.h"
#include "area.h"


// battery drawing parameter (per panel)
typedef struct Battery {
	// always start with area
	Area area;

	Color font;
	int bat1_posy;
	int bat2_posy;
} Battery;

enum chargestate {
	BATTERY_UNKNOWN,
	BATTERY_CHARGING,
	BATTERY_DISCHARGING,
	BATTERY_FULL
};

typedef struct battime {
	int16_t hours;
	int8_t minutes;
	int8_t seconds;
} battime;

typedef struct batstate {
	int percentage;
	struct battime time;
	enum chargestate state;
} batstate;

extern struct batstate battery_state;
extern PangoFontDescription *bat1_font_desc;
extern PangoFontDescription *bat2_font_desc;
extern int battery_enabled;
extern int battery_tooltip_enabled;
extern int percentage_hide;

extern int8_t battery_low_status;
extern char *battery_low_cmd;

extern char *battery_lclick_command;
extern char *battery_mclick_command;
extern char *battery_rclick_command;
extern char *battery_uwheel_command;
extern char *battery_dwheel_command;

static inline gchar* chargestate2str(enum chargestate state) {
	switch(state) {
		case BATTERY_CHARGING:
			return "Charging";
		case BATTERY_DISCHARGING:
			return "Discharging";
		case BATTERY_FULL:
			return "Full";
		case BATTERY_UNKNOWN:
		default:
			return "Unknown";
	};
}

// default global data
void default_battery();

// freed memory
void cleanup_battery();

int update_battery();

void init_battery();
void init_battery_panel(void *panel);

void draw_battery(void *obj, cairo_t *c);

int  resize_battery(void *obj);

void battery_action(int button);

#ifdef __linux
gboolean init_linux_batteries();
void free_linux_batteries();
void update_linux_batteries(enum chargestate *state, int8_t *percentage, int *seconds);
char* linux_batteries_get_tooltip();
#endif

#endif
