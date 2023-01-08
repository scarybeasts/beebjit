#include <stdlib.h>
#include <assert.h>
#include <math.h>

#include "sync.h"
#include "track.h"
#include "base.h"

static double key_linear(const struct track_key k[2], double row)
{
	double t = (row - k[0].row) / ((double)k[1].row - k[0].row);
	return k[0].value.val + ((double)k[1].value.val - k[0].value.val) * t;
}

static double key_smooth(const struct track_key k[2], double row)
{
	double t = (row - k[0].row) / ((double)k[1].row - k[0].row);
	t = t * t * (3 - 2 * t);
	return k[0].value.val + ((double)k[1].value.val - k[0].value.val) * t;
}

static double key_ramp(const struct track_key k[2], double row)
{
	double t = (row - k[0].row) / ((double)k[1].row - k[0].row);
	t = pow(t, 2.0);
	return k[0].value.val + ((double)k[1].value.val - k[0].value.val) * t;
}

static int get_key_idx(const struct sync_track* t, double row)
{
	return key_idx_floor(t, (int)floor(row));
}

key_value sync_get_key_value(const struct sync_track* t, double row)
{
	int idx;
	static key_value default_value;
	default_value.val = 0.0f;

	if (!t->num_keys)
		return default_value;

	idx = get_key_idx(t, row);

	/* at the edges, return the first/last value.val */
	if (idx < 0)
		return t->keys[0].value;
	if (idx > (int)t->num_keys - 2)
		return t->keys[t->num_keys - 1].value;

	return t->keys[idx].value;
}

double sync_get_val(const struct sync_track *t, double row)
{
	int idx;

	assert(t->type == TRACK_FLOAT);

	/* If we have no keys at all, return a constant 0 */
	if (!t->num_keys)
		return 0.0f;

	idx = get_key_idx(t, row);

	/* at the edges, return the first/last value.val */
	if (idx < 0)
		return t->keys[0].value.val;
	if (idx > (int)t->num_keys - 2)
		return t->keys[t->num_keys - 1].value.val;

	/* interpolate according to key-type */
	switch (t->keys[idx].type) {
	case KEY_STEP:
		return t->keys[idx].value.val;
	case KEY_LINEAR:
		return key_linear(t->keys + idx, row);
	case KEY_SMOOTH:
		return key_smooth(t->keys + idx, row);
	case KEY_RAMP:
		return key_ramp(t->keys + idx, row);
	default:
		assert(0);
		return 0.0f;
	}
}

unsigned char sync_get_event(const struct sync_track* t, double row)
{
	int irow = (int)floor(row);

	/* Must be this exact row! */
	if (row != irow)
		return 0;

	assert(t->type == TRACK_EVENT);

	/* If we have no keys at all, return a constant 0 */
	if (!t->num_keys)
		return 0;

	/* Only return an event if the row is an exact hit. */
	for (int idx = 0; idx < t->num_keys; idx++) {
		if (t->keys[idx].row == irow) {
			return t->keys[idx].value.event;
		}
	}
	
	return 0;
}

static unsigned short colour_linear(const struct track_key k[2], double row)
{
	double t = (row - k[0].row) / ((double)k[1].row - k[0].row);
	unsigned short i = k[0].value.colour >> 12;

	int r0 = (k[0].value.colour >> 8) & 0xf;
	int g0 = (k[0].value.colour >> 4) & 0xf;
	int b0 = (k[0].value.colour >> 0) & 0xf;
	int r1 = (k[1].value.colour >> 8) & 0xf;
	int g1 = (k[1].value.colour >> 4) & 0xf;
	int b1 = (k[1].value.colour >> 0) & 0xf;

	// TODO: Make this interpolation accurate to precision of target platform.
	unsigned short new_r = (unsigned short)(r0 + ((double)r1 - r0) * t);
	unsigned short new_g = (unsigned short)(g0 + ((double)g1 - g0) * t);
	unsigned short new_b = (unsigned short)(b0 + ((double)b1 - b0) * t);

	return (i << 12) | (new_r << 8) | (new_g << 4) | new_b;
}

unsigned short sync_get_colour(const struct sync_track* t, double row)
{
	int idx;

	assert(t->type == TRACK_COLOUR);

	/* If we have no keys at all, return a constant 0 */
	if (!t->num_keys)
		return 0x0000;

	idx = get_key_idx(t, row);

	/* at the edges, return the first/last value.val */
	if (idx < 0)
		return t->keys[0].value.colour;
	if (idx > (int)t->num_keys - 2)
		return t->keys[t->num_keys - 1].value.colour;

	/* interpolate according to key-type */
	switch (t->keys[idx].type) {
	case KEY_STEP:
		return t->keys[idx].value.colour;
	case KEY_LINEAR:
	case KEY_SMOOTH:
	case KEY_RAMP:
		return colour_linear(t->keys + idx, row);
	default:
		assert(0);
		return 0x0000;
	}
}

int sync_find_key(const struct sync_track *t, int row)
{
	int lo = 0, hi = t->num_keys;

	/* binary search, t->keys is sorted by row */
	while (lo < hi) {
		int mi = (lo + hi) / 2;
		assert(mi != hi);

		if (t->keys[mi].row < row)
			lo = mi + 1;
		else if (t->keys[mi].row > row)
			hi = mi;
		else
			return mi; /* exact hit */
	}
	assert(lo == hi);

	/* return first key after row, negated and biased (to allow -0) */
	return -lo - 1;
}

#ifndef SYNC_PLAYER
int sync_set_key(struct sync_track *t, const struct track_key *k)
{
	int idx = sync_find_key(t, k->row);
	if (idx < 0) {
		/* no exact hit, we need to allocate a new key */
		void *tmp;
		idx = -idx - 1;
		tmp = realloc(t->keys, sizeof(struct track_key) *
		    (t->num_keys + 1));
		if (!tmp)
			return -1;
		t->num_keys++;
		t->keys = tmp;
		memmove(t->keys + idx + 1, t->keys + idx,
		    sizeof(struct track_key) * (t->num_keys - idx - 1));
	}
	t->keys[idx] = *k;
	return 0;
}

int sync_del_key(struct sync_track *t, int pos)
{
	void *tmp;
	int idx = sync_find_key(t, pos);
	assert(idx >= 0);
	memmove(t->keys + idx, t->keys + idx + 1,
	    sizeof(struct track_key) * (t->num_keys - idx - 1));
	assert(t->keys);
	tmp = realloc(t->keys, sizeof(struct track_key) *
	    (t->num_keys - 1));
	if (t->num_keys != 1 && !tmp)
		return -1;
	t->num_keys--;
	t->keys = tmp;
	return 0;
}
#endif
