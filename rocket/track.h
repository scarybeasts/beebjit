#ifndef SYNC_TRACK_H
#define SYNC_TRACK_H

#include <string.h>
#include <stdlib.h>
#include "base.h"

enum key_type {
	KEY_STEP,   /* stay constant */
	KEY_LINEAR, /* lerp to the next value */
	KEY_SMOOTH, /* smooth curve to the next value */
	KEY_RAMP,
	KEY_TYPE_COUNT
};

typedef union {
	float val;
	unsigned char event;
	unsigned short colour;
} key_value;

struct track_key {
	int row;
	key_value value;
	enum key_type type;
};

enum track_type {
	TRACK_FLOAT,		/* standard Rocket track interoplating float values */
	TRACK_EVENT,		/* no interpolation supported, unsigned char value */
	TRACK_COLOUR,	/* represents a 12-bit colour value and palette index */
	TRACK_TYPE_COUNT
};

struct sync_track {
	char *name;
	struct track_key *keys;
	int num_keys;
	enum track_type type;
};

int sync_find_key(const struct sync_track *, int);
static inline int key_idx_floor(const struct sync_track *t, int row)
{
	int idx = sync_find_key(t, row);
	if (idx < 0)
		idx = -idx - 2;
	return idx;
}

#ifndef SYNC_PLAYER
int sync_set_key(struct sync_track *, const struct track_key *);
int sync_del_key(struct sync_track *, int);
static inline int is_key_frame(const struct sync_track *t, int row)
{
	return sync_find_key(t, row) >= 0;
}

key_value sync_get_key_value(const struct sync_track* t, double row);
#endif /* !defined(SYNC_PLAYER) */

#endif /* SYNC_TRACK_H */
