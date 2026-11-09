/* Parameter table shared by the filter and the dock.
 *
 * The LFOs and the audio routings address parameters by index into this array,
 * so the order here is part of the saved settings format: appending is safe,
 * reordering is not.
 *
 * It lives in a header because the dock (a separate DLL) has to resolve the
 * same indices - it used to hardcode "21 = k_zoom, 12 = hue", which would
 * silently point at other parameters if anyone reordered the table.
 *
 * Keys are string literals rather than the S_* macros so this header can be
 * included on its own; spacebeam.c checks a few of them against the macros at
 * compile time (see SB_TARGET_KEY_CHECK).
 */

#ifndef SPACEBEAM_MOD_TARGETS_H
#define SPACEBEAM_MOD_TARGETS_H

#include <stddef.h>

struct sb_mod_target {
	const char *key;
	double lo, hi;
};

static const struct sb_mod_target sb_mod_targets[] = {
	{"ct_zoom", 0.05, 5.0},    {"ct_angle", -1.0, 1.0},
	{"ct_tx", -1.0, 1.0},      {"ct_ty", -1.0, 1.0},
	{"ct_tiltx", -1.0, 1.0},   {"ct_tilty", -1.0, 1.0},
	{"ct_bend", -3.0, 3.0},    {"ct_wobble", 0.0, 1.0},
	{"ct_warp", 0.0, 1.0},     {"ct_rgb", 0.0, 0.1},
	{"color_amt", 0.0, 1.0},   {"brightness", 0.0, 2.0},
	{"hue", 0.0, 1.0},         {"glow", 0.0, 2.0},
	{"contrast", 0.0, 2.0},    {"saturation", 0.0, 2.0},
	{"edge_amt", 0.0, 1.0},    {"edge_thresh", -0.05, 0.2},
	{"edge_hue", 0.0, 1.0},    {"edge_sat", 0.0, 1.0},
	{"k_amt", 0.0, 1.0},       {"k_zoom", 0.25, 5.0},
	{"d_mix", 0.0, 1.0},       {"d_scale", 1.5, 12.0},
	{"d_arms", 0.0, 8.0},      {"d_speed", -2.0, 2.0},
	{"d_ring", 0.1, 1.0},      {"d_zoom", 0.0, 1.0},
	{"mt_zoom", 0.05, 5.0},    {"mt_angle", -1.0, 1.0},
	{"mt_rgb", 0.0, 0.1},
	{"tn_mix", 0.0, 1.0},      {"tn_shape", 0.0, 1.0},
	{"tn_fov", 0.0, 1.0},      {"tn_speed", -2.0, 2.0},
	{"tn_curve", 0.0, 2.0},    {"tn_twist", -2.0, 2.0},
	{"tn_flux", 0.0, 1.0},     {"tn_roll", -1.0, 1.0},
	{"tn_hstr", 0.0, 1.0},     {"tn_hpos", 0.0, 1.0},
	{"tn_wstr", 0.0, 1.0},     {"tn_wpos", 0.0, 1.0},
	{"tn_fogd", 0.0, 1.0},     {"tn_fogh", 0.0, 1.0},
	{"sw_str", 0.0, 1.0},      {"sw_wide", 0.0, 2.0},
	{"sw_activity", 0.0, 4.0}, {"sw_speed", -2.0, 2.0},
	{"sw_roll", -1.0, 1.0},    {"sw_fogd", 0.0, 1.0},
	{"sw_fogh", 0.0, 1.0},
	{"fb_alpha", 0.0, 1.0},
};

#define SB_MOD_TARGET_COUNT \
	((int)(sizeof(sb_mod_targets) / sizeof(sb_mod_targets[0])))

/* Index of a parameter by key, or -1. Used by the dock to build default
 * routings without hardcoding positions. */
static inline int sb_target_index(const char *key)
{
	if (!key)
		return -1;
	for (int i = 0; i < SB_MOD_TARGET_COUNT; i++) {
		const char *k = sb_mod_targets[i].key;
		const char *a = k;
		const char *b = key;
		while (*a && *a == *b) {
			a++;
			b++;
		}
		if (*a == '\0' && *b == '\0')
			return i;
	}
	return -1;
}

#endif /* SPACEBEAM_MOD_TARGETS_H */
