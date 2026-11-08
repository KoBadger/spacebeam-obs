// SpaceBeam for OBS — filter source
// Ported from SpaceBeam (https://github.com/CalmyJane/SpaceBeam), GPL-3.0.
// Original app by Calmy Jane (Kotlin / OpenGL ES). This is a native OBS port of
// the effect chain:
//   Mixer(+feedback) -> Camera Transform -> Color -> Edge Detect
//   -> Kaleidoscope -> Droste -> Master Transform -> 3D Tunnel -> Swirl
//
// Every stage is a full-screen shader pass into ping-pong texrenders. Stages
// that are visually inert are skipped entirely, so unused effects cost nothing.

#include <obs-module.h>
#include <graphics/matrix4.h>
#include <graphics/vec2.h>
#include <graphics/vec4.h>
#include <util/platform.h>

#include <windows.h>  /* file dialogs for preset bank import/export */
#include <commdlg.h>  /* GetOpenFileNameW / GetSaveFileNameW */

#include <math.h>

#define SB_PI 3.14159265358979f
#define T_(k) obs_module_text(k)

/* --------------------------------------------------------------- settings */

#define S_CT_ZOOM   "ct_zoom"
#define S_CT_ANGLE  "ct_angle"
#define S_CT_TX     "ct_tx"
#define S_CT_TY     "ct_ty"
#define S_CT_TILTX  "ct_tiltx"
#define S_CT_TILTY  "ct_tilty"
#define S_CT_BEND   "ct_bend"
#define S_CT_WOBBLE "ct_wobble"
#define S_CT_WARP   "ct_warp"
#define S_CT_RGB    "ct_rgb"
#define S_CT_SPIN   "ct_spin"

#define S_C_AMT     "color_amt"
#define S_BRIT      "brightness"
#define S_HUE       "hue"
#define S_NEG       "negative"
#define S_GLOW      "glow"
#define S_CONTRAST  "contrast"
#define S_VIBRANCE  "saturation"

#define S_E_AMT     "edge_amt"
#define S_E_THRESH  "edge_thresh"
#define S_E_HUE     "edge_hue"
#define S_E_SAT     "edge_sat"
#define S_E_BRIT    "edge_brit"

#define S_K_AXIS    "k_axis"
#define S_K_AMT     "k_amt"
#define S_K_ZOOM    "k_zoom"

#define S_D_MIX     "d_mix"
#define S_D_SCALE   "d_scale"
#define S_D_ARMS    "d_arms"
#define S_D_SPEED   "d_speed"
#define S_D_RING    "d_ring"
#define S_D_ZOOM    "d_zoom"
#define S_D_LOOK_X  "d_look_x"
#define S_D_LOOK_Y  "d_look_y"

#define S_MT_ZOOM   "mt_zoom"
#define S_MT_ANGLE  "mt_angle"
#define S_MT_TX     "mt_tx"
#define S_MT_TY     "mt_ty"
#define S_MT_TILTX  "mt_tiltx"
#define S_MT_TILTY  "mt_tilty"
#define S_MT_BEND   "mt_bend"
#define S_MT_WOBBLE "mt_wobble"
#define S_MT_WARP   "mt_warp"
#define S_MT_RGB    "mt_rgb"
#define S_MT_SPIN   "mt_spin"

#define S_TN_MIX    "tn_mix"
#define S_TN_SHAPE  "tn_shape"
#define S_TN_FOV    "tn_fov"
#define S_TN_SPEED  "tn_speed"
#define S_TN_HSTR   "tn_hstr"
#define S_TN_HPOS   "tn_hpos"
#define S_TN_WSTR   "tn_wstr"
#define S_TN_WPOS   "tn_wpos"
#define S_TN_CURVE  "tn_curve"
#define S_TN_TWIST  "tn_twist"
#define S_TN_FLUX   "tn_flux"
#define S_TN_FOGD   "tn_fogd"
#define S_TN_FOGH   "tn_fogh"
#define S_TN_FOGS   "tn_fogs"
#define S_TN_FOGV   "tn_fogv"
#define S_TN_LOOK_X "tn_look_x"
#define S_TN_LOOK_Y "tn_look_y"
#define S_TN_ROLL   "tn_roll"

#define S_SW_STR      "sw_str"
#define S_SW_WIDE     "sw_wide"
#define S_SW_SPEED    "sw_speed"
#define S_SW_ACTIVITY "sw_activity"
#define S_SW_FOGD     "sw_fogd"
#define S_SW_FOGF     "sw_fogf"
#define S_SW_FOGH     "sw_fogh"
#define S_SW_FOGS     "sw_fogs"
#define S_SW_FOGV     "sw_fogv"
#define S_SW_LOOK_X   "sw_look_x"
#define S_SW_LOOK_Y   "sw_look_y"
#define S_SW_ROLL     "sw_roll"

#define S_FB_ON     "fb_on"
#define S_FB_TAP    "fb_tap"
#define S_FB_ALPHA  "fb_alpha"
#define S_FB_MODE   "fb_mode"
#define S_FB_DELAY  "fb_delay"

#define S_PRESET_SLOT "preset_slot"
#define S_PRESET_TIME "preset_time"
#define S_PRESET_CMD  "preset_cmd"
#define S_BANK_PATH   "preset_bank_path"

#define S_BPM      "bpm"
#define S_LFO_SYNC "lfo_sync"

/* ---- performance ---- */
#define S_RENDER_SCALE "render_scale"
#define S_SW_STEPS     "sw_steps"

/* ---- mixer ---- */
#define S_MIX_ON   "mix_on"
#define SB_MIX_LAYERS 4
#define SB_MIX_KEYBUF 32

#define SB_PRESETS 9
#define SB_PRESET_PREFIX "preset_"

#define SB_LFOS 4
#define SB_LFO_KEYBUF 32

/* preset_cmd values, so scripts (obs-websocket) can drive presets */
#define SB_CMD_NONE 0
#define SB_CMD_SAVE 1
#define SB_CMD_LOAD 2
#define SB_CMD_CLEAR 3
#define SB_CMD_EXPORT 4
#define SB_CMD_IMPORT 5

/* Tap points for the feedback loop. Order matches the effect chain. */
enum sb_tap {
	TAP_MIXER = 0,
	TAP_CAM_TRANSFORM,
	TAP_COLOR,
	TAP_EDGE,
	TAP_KALEIDO,
	TAP_DROSTE,
	TAP_MASTER,
	TAP_TUNNEL,
	TAP_COUNT,
};

/* ------------------------------------------------------------- structures */

struct sb_params {
	gs_eparam_t *u_tex, *prev_tex, *u_ratio;

	gs_eparam_t *t_zoom, *t_angle, *t_tx, *t_ty, *t_tiltx, *t_tilty,
		*t_warp, *t_rgb, *t_bend, *t_wobble;

	gs_eparam_t *brit, *hue, *neg, *glow, *contrast, *vibrance;

	gs_eparam_t *e_amt, *e_thresh, *e_hue, *e_sat, *e_brit, *e_pixel;

	gs_eparam_t *k_axis, *k_amt, *k_zoom;

	gs_eparam_t *d_mix, *d_logs, *d_c, *d_w, *d_phase, *d_ring, *d_look;

	gs_eparam_t *tn_mix, *tn_shape, *tn_fov, *tn_scroll, *tn_hstr, *tn_hpos,
		*tn_wstr, *tn_wpos, *tn_curve, *tn_twist, *tn_flux, *tn_fogd,
		*tn_fogh, *tn_fogs, *tn_fogv, *tn_look, *tn_roll;

	gs_eparam_t *sw_str, *sw_wide, *sw_scroll, *sw_sway, *sw_fogd, *sw_fogf,
		*sw_fogh, *sw_fogs, *sw_fogv, *sw_look, *sw_roll, *sw_steps;

	gs_eparam_t *fb_alpha, *fb_mode;

	/* mixer */
	gs_eparam_t *layer_opacity;
};

struct sb_filter {
	obs_source_t *context;
	gs_effect_t *effect;
	struct sb_params *p;

	gs_texrender_t *capture;
	gs_texrender_t *buf_a, *buf_b;
	gs_texrender_t *mixdown;
	/* mixer output must not share a texrender with the feedback mixdown,
	 * otherwise the feedback stage samples a buffer it just reset */
	gs_texrender_t *mixbuf;

	/* feedback history ring, one texrender per delayed frame */
	gs_texrender_t **fb_ring;
	uint32_t fb_ring_size;
	uint32_t fb_ring_pos;
	bool fb_ring_valid;

	uint32_t cx, cy;
	float ct_rot, mt_rot;
	float droste_phase;
	float tunnel_scroll;
	float swirl_scroll, swirl_sway;

	/* preset morph: interpolate every animated parameter toward last_preset */
	obs_data_t *last_preset; /* snapshot we are morphing toward */
	float morph_time;        /* seconds elapsed since load   */
	float morph_total;       /* configured duration          */
	bool  morphing;
	obs_data_t *morph_from;  /* values when the morph began   */
	obs_data_t *morph_to;    /* preset we are morphing toward */

	/* effective parameter values actually used for rendering */
	obs_data_t *eff;

	/* ---- multi-source mixer ---- */
	obs_source_t *layers[SB_MIX_LAYERS];
	char layer_name[SB_MIX_LAYERS][256];
	gs_texrender_t *layer_buf;

	obs_hotkey_id hk_load;
	obs_hotkey_id hk_save;

	bool in_action; /* guards against re-entrant action dispatch */

	double beat_accum; /* musical position, in beats */
	double time_accum; /* wall-clock seconds since creation */
};

/* ---------------------------------------------------------------- helpers */

static inline float sb_d(obs_data_t *s, const char *k)
{
	return (float)obs_data_get_double(s, k);
}

static inline int sb_i(obs_data_t *s, const char *k)
{
	return (int)obs_data_get_int(s, k);
}

static inline float sb_clampf(float v, float lo, float hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

static gs_eparam_t *PE(gs_effect_t *e, const char *n)
{
	return gs_effect_get_param_by_name(e, n);
}

/* ------------------------------------------------------------ param cache */

static void sb_cache_params(struct sb_filter *s)
{
	gs_effect_t *e = s->effect;
	struct sb_params *p = bzalloc(sizeof(*p));
	s->p = p;

	p->u_tex = PE(e, "u_tex");
	p->prev_tex = PE(e, "prev_tex");
	p->u_ratio = PE(e, "u_ratio");

	p->t_zoom = PE(e, "u_T_ZOOM");
	p->t_angle = PE(e, "u_T_ANGLE");
	p->t_tx = PE(e, "u_T_TX");
	p->t_ty = PE(e, "u_T_TY");
	p->t_tiltx = PE(e, "u_T_TILTX");
	p->t_tilty = PE(e, "u_T_TILTY");
	p->t_warp = PE(e, "u_T_WARP");
	p->t_rgb = PE(e, "u_T_RGB");
	p->t_bend = PE(e, "u_T_BEND");
	p->t_wobble = PE(e, "u_T_WOBBLE");

	p->brit = PE(e, "u_BRIT");
	p->hue = PE(e, "u_HUE");
	p->neg = PE(e, "u_NEG");
	p->glow = PE(e, "u_GLOW");
	p->contrast = PE(e, "u_CONTRAST");
	p->vibrance = PE(e, "u_VIBRANCE");

	p->e_amt = PE(e, "u_E_AMT");
	p->e_thresh = PE(e, "u_E_THRESH");
	p->e_hue = PE(e, "u_E_HUE");
	p->e_sat = PE(e, "u_E_SAT");
	p->e_brit = PE(e, "u_E_BRIT");
	p->e_pixel = PE(e, "u_E_PIXEL");

	p->k_axis = PE(e, "u_K_AXIS");
	p->k_amt = PE(e, "u_K_AMT");
	p->k_zoom = PE(e, "u_K_ZOOM");

	p->d_mix = PE(e, "u_D_MIX");
	p->d_logs = PE(e, "u_D_LOGS");
	p->d_c = PE(e, "u_D_C");
	p->d_w = PE(e, "u_D_W");
	p->d_phase = PE(e, "u_D_PHASE");
	p->d_ring = PE(e, "u_D_RING");
	p->d_look = PE(e, "u_D_LOOK");

	p->tn_mix = PE(e, "u_TN_MIX");
	p->tn_shape = PE(e, "u_TN_SHAPE");
	p->tn_fov = PE(e, "u_TN_FOV");
	p->tn_scroll = PE(e, "u_TN_SCROLL");
	p->tn_hstr = PE(e, "u_TN_HSTR");
	p->tn_hpos = PE(e, "u_TN_HPOS");
	p->tn_wstr = PE(e, "u_TN_WSTR");
	p->tn_wpos = PE(e, "u_TN_WPOS");
	p->tn_curve = PE(e, "u_TN_CURVE");
	p->tn_twist = PE(e, "u_TN_TWIST");
	p->tn_flux = PE(e, "u_TN_FLUX");
	p->tn_fogd = PE(e, "u_TN_FOGD");
	p->tn_fogh = PE(e, "u_TN_FOGH");
	p->tn_fogs = PE(e, "u_TN_FOGS");
	p->tn_fogv = PE(e, "u_TN_FOGV");
	p->tn_look = PE(e, "u_TN_LOOK");
	p->tn_roll = PE(e, "u_TN_ROLL");

	p->sw_str = PE(e, "u_SW_STR");
	p->sw_wide = PE(e, "u_SW_WIDE");
	p->sw_scroll = PE(e, "u_SW_SCROLL");
	p->sw_sway = PE(e, "u_SW_SWAY");
	p->sw_fogd = PE(e, "u_SW_FOGD");
	p->sw_fogf = PE(e, "u_SW_FOGF");
	p->sw_fogh = PE(e, "u_SW_FOGH");
	p->sw_fogs = PE(e, "u_SW_FOGS");
	p->sw_fogv = PE(e, "u_SW_FOGV");
	p->sw_look = PE(e, "u_SW_LOOK");
	p->sw_roll = PE(e, "u_SW_ROLL");
	p->sw_steps = PE(e, "u_SW_STEPS");

	p->fb_alpha = PE(e, "u_FB_ALPHA");
	p->fb_mode = PE(e, "u_FB_MODE");
	p->layer_opacity = PE(e, "u_layer_opacity");
}

/* --------------------------------------------------------------- buffers */

static void sb_free_buffers(struct sb_filter *s)
{
	if (s->fb_ring) {
		for (uint32_t i = 0; i < s->fb_ring_size; i++)
			gs_texrender_destroy(s->fb_ring[i]);
		bfree(s->fb_ring);
		s->fb_ring = NULL;
	}
	s->fb_ring_size = 0;
	s->fb_ring_pos = 0;
	s->fb_ring_valid = false;

	if (s->capture) {
		gs_texrender_destroy(s->capture);
		s->capture = NULL;
	}
	if (s->buf_a) {
		gs_texrender_destroy(s->buf_a);
		s->buf_a = NULL;
	}
	if (s->buf_b) {
		gs_texrender_destroy(s->buf_b);
		s->buf_b = NULL;
	}
	if (s->mixdown) {
		gs_texrender_destroy(s->mixdown);
		s->mixdown = NULL;
	}
	if (s->mixbuf) {
		gs_texrender_destroy(s->mixbuf);
		s->mixbuf = NULL;
	}
	if (s->layer_buf) {
		gs_texrender_destroy(s->layer_buf);
		s->layer_buf = NULL;
	}
}

static void sb_ensure_buffers(struct sb_filter *s, uint32_t cx, uint32_t cy, uint32_t delay)
{
	bool size_changed = (s->cx != cx || s->cy != cy || !s->capture);
	bool ring_changed = (s->fb_ring_size != delay || !s->fb_ring);

	if (!size_changed && !ring_changed)
		return;

	obs_enter_graphics();

	if (size_changed) {
		if (s->capture) {
			gs_texrender_destroy(s->capture);
			s->capture = NULL;
		}
		if (s->buf_a) {
			gs_texrender_destroy(s->buf_a);
			s->buf_a = NULL;
		}
		if (s->buf_b) {
			gs_texrender_destroy(s->buf_b);
			s->buf_b = NULL;
		}
		if (s->mixdown) {
			gs_texrender_destroy(s->mixdown);
			s->mixdown = NULL;
		}
		if (s->mixbuf) {
			gs_texrender_destroy(s->mixbuf);
			s->mixbuf = NULL;
		}
		s->capture = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
		s->buf_a = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
		s->buf_b = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
		s->mixdown = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
		s->mixbuf = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
		if (!s->layer_buf)
			s->layer_buf = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
		s->cx = cx;
		s->cy = cy;
	}

	if (ring_changed) {
		if (s->fb_ring) {
			for (uint32_t i = 0; i < s->fb_ring_size; i++)
				gs_texrender_destroy(s->fb_ring[i]);
			bfree(s->fb_ring);
			s->fb_ring = NULL;
		}
		s->fb_ring = bzalloc(sizeof(gs_texrender_t *) * delay);
		for (uint32_t i = 0; i < delay; i++)
			s->fb_ring[i] = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
		s->fb_ring_size = delay;
		s->fb_ring_pos = 0;
		s->fb_ring_valid = false;
	}

	obs_leave_graphics();
}

/* ---------------------------------------------------------------- passes */

/* Run one technique into `out` (never NULL). Returns the output texture. */
static gs_texture_t *sb_pass(struct sb_filter *s, const char *tech, gs_texture_t *in,
			     gs_texrender_t *out, uint32_t w, uint32_t h)
{
	gs_effect_t *e = s->effect;
	gs_technique_t *t = gs_effect_get_technique(e, tech);
	if (!t || !in || !out)
		return in;

	gs_texrender_reset(out);
	if (!gs_texrender_begin(out, w, h))
		return in;

	struct vec4 clear;
	vec4_zero(&clear);
	gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
	gs_ortho(0.0f, (float)w, 0.0f, (float)h, -100.0f, 100.0f);

	gs_effect_set_texture(s->p->u_tex, in);
	gs_effect_set_float(s->p->u_ratio, h ? (float)w / (float)h : 1.0f);

	size_t passes = gs_technique_begin(t);
	for (size_t i = 0; i < passes; i++) {
		gs_technique_begin_pass(t, i);
		gs_draw_sprite(in, 0, w, h);
		gs_technique_end_pass(t);
	}
	gs_technique_end(t);

	gs_texrender_end(out);
	return gs_texrender_get_texture(out);
}

/* Copy a texture into a texrender slot (used for feedback history). */
static void sb_copy_into(struct sb_filter *s, gs_texture_t *src, gs_texrender_t *dst,
			 uint32_t w, uint32_t h)
{
	if (!src || !dst)
		return;

	gs_texrender_reset(dst);
	if (!gs_texrender_begin(dst, w, h))
		return;

	struct vec4 clear;
	vec4_zero(&clear);
	gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
	gs_ortho(0.0f, (float)w, 0.0f, (float)h, -100.0f, 100.0f);

	gs_effect_t *def = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	gs_eparam_t *img = gs_effect_get_param_by_name(def, "image");
	gs_effect_set_texture(img, src);
	while (gs_effect_loop(def, "Draw"))
		gs_draw_sprite(src, 0, w, h);

	gs_texrender_end(dst);
}

/* Pick a pass target that is not the texrender the input came from. */
static gs_texrender_t *sb_pick_target(struct sb_filter *s, gs_texture_t *in)
{
	gs_texture_t *ta = gs_texrender_get_texture(s->buf_a);
	gs_texture_t *tb = gs_texrender_get_texture(s->buf_b);

	if (in == ta)
		return s->buf_b;
	if (in == tb)
		return s->buf_a;
	return s->buf_a;
}

/* Run one technique into a texrender that is safe for `in`. */
static gs_texture_t *sb_pass_auto(struct sb_filter *s, const char *tech, gs_texture_t *in,
				  uint32_t w, uint32_t h)
{
	return sb_pass(s, tech, in, sb_pick_target(s, in), w, h);
}

/* ------------------------------------------------------------ stage fills */

static void sb_setup_transform(struct sb_filter *s, obs_data_t *st, bool master, float w, float h)
{
	struct sb_params *p = s->p;
	const char *pz = master ? S_MT_ZOOM : S_CT_ZOOM;
	const char *pa = master ? S_MT_ANGLE : S_CT_ANGLE;
	const char *ptx = master ? S_MT_TX : S_CT_TX;
	const char *pty = master ? S_MT_TY : S_CT_TY;
	const char *ptix = master ? S_MT_TILTX : S_CT_TILTX;
	const char *ptiy = master ? S_MT_TILTY : S_CT_TILTY;
	const char *pbd = master ? S_MT_BEND : S_CT_BEND;
	const char *pwb = master ? S_MT_WOBBLE : S_CT_WOBBLE;
	const char *pwp = master ? S_MT_WARP : S_CT_WARP;
	const char *prg = master ? S_MT_RGB : S_CT_RGB;

	float zoom = sb_d(st, pz);
	if (zoom < 0.05f)
		zoom = 0.05f;

	float angle = sb_d(st, pa) * 360.0f;
	angle += master ? (s->mt_rot * 180.0f / SB_PI) : (s->ct_rot * 180.0f / SB_PI);

	gs_effect_set_float(p->t_zoom, zoom);
	gs_effect_set_float(p->t_angle, angle * SB_PI / 180.0f);
	gs_effect_set_float(p->t_tx, sb_d(st, ptx));
	gs_effect_set_float(p->t_ty, sb_d(st, pty));
	gs_effect_set_float(p->t_tiltx, sb_d(st, ptix) * 1.5f);
	gs_effect_set_float(p->t_tilty, sb_d(st, ptiy) * 1.5f);
	gs_effect_set_float(p->t_warp, sb_d(st, pwp));
	gs_effect_set_float(p->t_rgb, sb_d(st, prg));
	gs_effect_set_float(p->t_bend, sb_d(st, pbd));
	gs_effect_set_float(p->t_wobble, sb_d(st, pwb));
	gs_effect_set_float(p->u_ratio, h ? w / h : 1.0f);
}

/* ----------------------------------------------------------- lifecycle ---- */

/* ------------------------------------------------------- lifecycle ---- */

/* Forward decls: preset actions are defined further down (properties area). */
static void sb_preset_save_action(struct sb_filter *s);
static void sb_preset_load_action(struct sb_filter *s);
static void sb_preset_clear_action(struct sb_filter *s);
static void sb_refresh_layers(struct sb_filter *s, obs_data_t *st);
static bool sb_bank_save_as(struct sb_filter *s);
static bool sb_bank_load(struct sb_filter *s);
static bool sb_bank_save_as(struct sb_filter *s);
static bool sb_bank_load(struct sb_filter *s);
static void sb_hotkey_load(void *data, obs_hotkey_id id, obs_hotkey_t *hk, bool pressed);
static void sb_hotkey_save(void *data, obs_hotkey_id id, obs_hotkey_t *hk, bool pressed);

static const char *sb_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return T_("SpaceBeamFilter");
}

static void sb_update(void *data, obs_data_t *settings)
{
	struct sb_filter *s = data;

	s->ct_rot = 0.0f;
	s->mt_rot = 0.0f;
	s->droste_phase = 0.0f;
	s->tunnel_scroll = 0.0f;
	s->swirl_scroll = 0.0f;
	s->swirl_sway = 0.0f;

	/* Keep the effective buffer in sync immediately, so the first frame
	 * after a settings change is correct even before video_tick runs. */
	if (s->eff && settings) {
		obs_data_item_t *item = obs_data_first(settings);
		while (item) {
			const char *name = obs_data_item_get_name(item);
			if (name && strncmp(name, SB_PRESET_PREFIX, 7) != 0) {
				switch (obs_data_item_gettype(item)) {
				case OBS_DATA_NUMBER:
					obs_data_set_double(s->eff, name,
							    obs_data_item_get_double(item));
					break;
				case OBS_DATA_BOOLEAN:
					obs_data_set_bool(s->eff, name,
							  obs_data_item_get_bool(item));
					break;
				default:
					break;
				}
			}
			obs_data_item_next(&item);
		}
	}

	/* Scriptable preset commands (obs-websocket / plugins can set preset_cmd).
	 * The command resets itself so it behaves like a button press. */
	if (!s->in_action) {		int cmd = (int)obs_data_get_int(settings, S_PRESET_CMD);
		if (cmd != SB_CMD_NONE) {
			s->in_action = true;
			obs_data_set_int(settings, S_PRESET_CMD, SB_CMD_NONE);

			if (cmd == SB_CMD_SAVE)
				sb_preset_save_action(s);
			else if (cmd == SB_CMD_LOAD)
				sb_preset_load_action(s);
			else if (cmd == SB_CMD_CLEAR)
				sb_preset_clear_action(s);
			else if (cmd == SB_CMD_EXPORT)
				sb_bank_save_as(s);
			else if (cmd == SB_CMD_IMPORT)
				sb_bank_load(s);

			s->in_action = false;
		}
	}

	/* Keep the mixer layer references in sync with the chosen names. */
}

static void *sb_create(obs_data_t *settings, obs_source_t *context)
{
	struct sb_filter *s = bzalloc(sizeof(*s));
	s->context = context;
	s->eff = obs_data_create();

	char *path = obs_module_file("shaders/spacebeam.effect");
	char *err = NULL;

	obs_enter_graphics();
	s->effect = gs_effect_create_from_file(path, &err);
	if (s->effect)
		sb_cache_params(s);
	else
		blog(LOG_ERROR, "[spacebeam] effect load failed: %s (%s)",
		     path ? path : "?", err ? err : "no error string");
	obs_leave_graphics();

	if (err)
		bfree(err);
	if (path)
		bfree(path);

	s->hk_load = obs_hotkey_register_source(context, "spacebeam.load_preset",
					      T_("HotkeyLoadPreset"), sb_hotkey_load, s);
	s->hk_save = obs_hotkey_register_source(context, "spacebeam.save_preset",
					      T_("HotkeySavePreset"), sb_hotkey_save, s);

	UNUSED_PARAMETER(settings);
	return s;
}

static void sb_destroy(void *data)
{
	struct sb_filter *s = data;

	if (s->hk_load)
		obs_hotkey_unregister(s->hk_load);
	if (s->hk_save)
		obs_hotkey_unregister(s->hk_save);

	for (int i = 0; i < SB_MIX_LAYERS; i++) {
		if (s->layers[i])
			obs_source_release(s->layers[i]);
	}

	obs_enter_graphics();
	sb_free_buffers(s);
	if (s->effect)
		gs_effect_destroy(s->effect);
	obs_leave_graphics();

	if (s->p)
		bfree(s->p);
	if (s->eff)
		obs_data_release(s->eff);
	if (s->morph_from)
		obs_data_release(s->morph_from);
	if (s->morph_to)
		obs_data_release(s->morph_to);
	bfree(s);
}

/* -------------------------------------------------------------------- LFO */

/* Every parameter the LFOs are allowed to drive, with the clamp range used
 * for normalisation (so depth 1.0 sweeps the whole slider). */
struct sb_mod_target {
	const char *key;
	double lo, hi;
};

static const struct sb_mod_target sb_mod_targets[] = {
	{S_CT_ZOOM, 0.05, 5.0},   {S_CT_ANGLE, -1.0, 1.0},
	{S_CT_TX, -1.0, 1.0},     {S_CT_TY, -1.0, 1.0},
	{S_CT_TILTX, -1.0, 1.0},  {S_CT_TILTY, -1.0, 1.0},
	{S_CT_BEND, -3.0, 3.0},   {S_CT_WOBBLE, 0.0, 1.0},
	{S_CT_WARP, 0.0, 1.0},    {S_CT_RGB, 0.0, 0.1},
	{S_C_AMT, 0.0, 1.0},      {S_BRIT, 0.0, 2.0},
	{S_HUE, 0.0, 1.0},        {S_GLOW, 0.0, 2.0},
	{S_CONTRAST, 0.0, 2.0},   {S_VIBRANCE, 0.0, 2.0},
	{S_E_AMT, 0.0, 1.0},      {S_E_THRESH, -0.05, 0.2},
	{S_E_HUE, 0.0, 1.0},      {S_E_SAT, 0.0, 1.0},
	{S_K_AMT, 0.0, 1.0},      {S_K_ZOOM, 0.25, 5.0},
	{S_D_MIX, 0.0, 1.0},      {S_D_SCALE, 1.5, 12.0},
	{S_D_ARMS, 0.0, 8.0},     {S_D_SPEED, -2.0, 2.0},
	{S_D_RING, 0.1, 1.0},     {S_D_ZOOM, 0.0, 1.0},
	{S_MT_ZOOM, 0.05, 5.0},   {S_MT_ANGLE, -1.0, 1.0},
	{S_MT_RGB, 0.0, 0.1},
	{S_TN_MIX, 0.0, 1.0},     {S_TN_SHAPE, 0.0, 1.0},
	{S_TN_FOV, 0.0, 1.0},     {S_TN_SPEED, -2.0, 2.0},
	{S_TN_CURVE, 0.0, 2.0},   {S_TN_TWIST, -2.0, 2.0},
	{S_TN_FLUX, 0.0, 1.0},    {S_TN_ROLL, -1.0, 1.0},
	{S_TN_HSTR, 0.0, 1.0},    {S_TN_HPOS, 0.0, 1.0},
	{S_TN_WSTR, 0.0, 1.0},    {S_TN_WPOS, 0.0, 1.0},
	{S_TN_FOGD, 0.0, 1.0},    {S_TN_FOGH, 0.0, 1.0},
	{S_SW_STR, 0.0, 1.0},     {S_SW_WIDE, 0.0, 2.0},
	{S_SW_ACTIVITY, 0.0, 4.0},{S_SW_SPEED, -2.0, 2.0},
	{S_SW_ROLL, -1.0, 1.0},   {S_SW_FOGD, 0.0, 1.0},
	{S_SW_FOGH, 0.0, 1.0},
	{S_FB_ALPHA, 0.0, 1.0},
};

#define SB_MOD_TARGET_COUNT ((int)(sizeof(sb_mod_targets) / sizeof(sb_mod_targets[0])))

/* LFO wave shapes */
enum { LFO_SINE, LFO_TRI, LFO_SAW, LFO_SQUARE, LFO_RANDOM };
#define SB_LFO_WAVE_COUNT 5

static double lfo_wave(int wave, double phase, unsigned seed)
{
	/* phase is 0..1; result is -1..1 */
	switch (wave) {
	case LFO_SINE:
		return sin(phase * 6.283185307179586);
	case LFO_TRI:
		return 1.0 - 4.0 * fabs(phase - 0.5);
	case LFO_SAW:
		return phase * 2.0 - 1.0;
	case LFO_SQUARE:
		return phase < 0.5 ? 1.0 : -1.0;
	case LFO_RANDOM: {
		/* sample-and-hold: one random value per cycle */
		unsigned h = seed * 2654435761u;
		unsigned idx = (unsigned)(phase * 1000000.0);
		h ^= idx * 2246822519u;
		h ^= h >> 13;
		h *= 3266489917u;
		h ^= h >> 16;
		return ((double)(h % 20000) / 10000.0) - 1.0;
	}
	default:
		return 0.0;
	}
}

static void sb_lfo_key(int i, const char *field, char *buf, size_t n)
{
	snprintf(buf, n, "lfo%d_%s", i + 1, field);
}

/* Apply every LFO on top of the effective values. */
static void sb_apply_lfos(struct sb_filter *s, obs_data_t *st)
{
	if (!s->eff || !st)
		return;

	bool sync = obs_data_get_bool(st, S_LFO_SYNC);

	for (int i = 0; i < SB_LFOS; i++) {
		char kt[SB_LFO_KEYBUF], kw[SB_LFO_KEYBUF], kr[SB_LFO_KEYBUF];
		char kd[SB_LFO_KEYBUF], kp[SB_LFO_KEYBUF], ken[SB_LFO_KEYBUF];
		sb_lfo_key(i, "target", kt, sizeof(kt));
		sb_lfo_key(i, "wave", kw, sizeof(kw));
		sb_lfo_key(i, "rate", kr, sizeof(kr));
		sb_lfo_key(i, "depth", kd, sizeof(kd));
		sb_lfo_key(i, "phase", kp, sizeof(kp));
		sb_lfo_key(i, "on", ken, sizeof(ken));

		if (!obs_data_get_bool(st, ken))
			continue;

		int target = (int)obs_data_get_int(st, kt);
		if (target < 0 || target >= SB_MOD_TARGET_COUNT)
			continue;

		const struct sb_mod_target *tgt = &sb_mod_targets[target];
		double depth = obs_data_get_double(st, kd);
		if (depth <= 0.0001)
			continue;

		double rate = obs_data_get_double(st, kr); /* cycles per beat, or Hz */
		double phase_off = obs_data_get_double(st, kp);
		int wave = (int)obs_data_get_int(st, kw);

		double cycles;
		if (sync) {
			/* rate = cycles per beat, driven by the BPM clock */
			cycles = s->beat_accum * rate;
		} else {
			/* rate = cycles per second (free-running Hz) */
			cycles = s->time_accum * rate;
		}

		double phase = cycles + phase_off;
		phase = phase - floor(phase);

		double v = lfo_wave(wave, phase, (unsigned)(i * 7919 + 17));

		double base = obs_data_get_double(s->eff, tgt->key);
		double range = tgt->hi - tgt->lo;
		double out = base + v * depth * range;

		if (out < tgt->lo)
			out = tgt->lo;
		if (out > tgt->hi)
			out = tgt->hi;

		obs_data_set_double(s->eff, tgt->key, out);
	}
}

static void sb_tick(void *data, float seconds)
{
	struct sb_filter *s = data;
	obs_data_t *st = obs_source_get_settings(s->context);

	/* ---- clocks ------------------------------------------------------- */
	double bpm = obs_data_get_double(st, S_BPM);
	if (bpm < 1.0)
		bpm = 120.0;
	s->beat_accum += (double)seconds * bpm / 60.0;
	s->time_accum += (double)seconds;

	/* ---- preset morph: advance every parameter toward the target ------- */
	if (s->morphing && s->morph_to && s->morph_from) {
		s->morph_time += seconds;
		float t = (s->morph_total > 0.0001f) ? (s->morph_time / s->morph_total) : 1.0f;
		if (t > 1.0f)
			t = 1.0f;
		/* smoothstep for a soft start/end */
		float e = t * t * (3.0f - 2.0f * t);

		obs_data_item_t *item = obs_data_first(s->morph_to);
		while (item) {
			const char *name = obs_data_item_get_name(item);
			if (name && obs_data_item_gettype(item) == OBS_DATA_NUMBER) {
				double to = obs_data_item_get_double(item);
				double from = obs_data_get_double(s->morph_from, name);
				obs_data_set_double(s->eff, name, from + (to - from) * e);
			}
			obs_data_item_next(&item);
		}

		if (t >= 1.0f) {
			/* Commit the finished preset to the live settings, otherwise
			 * the next tick would mirror the old settings back over it. */
			s->morphing = false;

			obs_data_t *settings = obs_source_get_settings(s->context);
			obs_data_item_t *it2 = obs_data_first(s->morph_to);
			while (it2) {
				const char *n2 = obs_data_item_get_name(it2);
				if (n2 && strncmp(n2, SB_PRESET_PREFIX, 7) != 0) {
					switch (obs_data_item_gettype(it2)) {
					case OBS_DATA_NUMBER:
						obs_data_set_double(settings, n2,
								    obs_data_item_get_double(it2));
						break;
					case OBS_DATA_BOOLEAN:
						obs_data_set_bool(settings, n2,
								  obs_data_item_get_bool(it2));
						break;
					default:
						break;
					}
				}
				obs_data_item_next(&it2);
			}
			obs_source_update(s->context, settings);
			obs_data_release(settings);
		}
	} else {
		/* no morph: mirror live settings into the effective buffer */
		obs_data_item_t *item = obs_data_first(st);
		while (item) {
			const char *name = obs_data_item_get_name(item);
			if (name && strncmp(name, SB_PRESET_PREFIX, 7) != 0) {
				switch (obs_data_item_gettype(item)) {
				case OBS_DATA_NUMBER:
					obs_data_set_double(s->eff, name,
							    obs_data_item_get_double(item));
					break;
				case OBS_DATA_BOOLEAN:
					obs_data_set_bool(s->eff, name,
							  obs_data_item_get_bool(item));
					break;
				default:
					break;
				}
			}
			obs_data_item_next(&item);
		}
	}

	/* LFOs run on top of whatever the settings/morph produced. */
	sb_apply_lfos(s, st);

	/* Mixer layer refs are resolved here (video thread) so render never
	 * dereferences a pointer the main thread is replacing. */
	sb_refresh_layers(s, st);
	s->ct_rot += sb_d(st, S_CT_SPIN) * seconds * 6.2831853f;
	s->mt_rot += sb_d(st, S_MT_SPIN) * seconds * 6.2831853f;
	s->ct_rot = fmodf(s->ct_rot, 6.2831853f);
	s->mt_rot = fmodf(s->mt_rot, 6.2831853f);

	float dspd = sb_d(st, S_D_SPEED);
	dspd = (dspd < 0.0f ? -1.0f : 1.0f) * dspd * dspd;
	s->droste_phase -= dspd * seconds;
	s->droste_phase = fmodf(s->droste_phase, 2.0f);
	if (s->droste_phase < 0.0f)
		s->droste_phase += 2.0f;

	float tspd = sb_d(st, S_TN_SPEED);
	tspd = (tspd < 0.0f ? -1.0f : 1.0f) * powf(fabsf(tspd), 2.2f);
	s->tunnel_scroll += tspd * seconds * 0.25f;

	float sspd = sb_d(st, S_SW_SPEED);
	sspd = (sspd < 0.0f ? -1.0f : 1.0f) * sspd * sspd;
	s->swirl_scroll += sspd * seconds;

	s->swirl_sway += sb_d(st, S_SW_ACTIVITY) * seconds;

	obs_data_release(st);
}

/* ------------------------------------------------------------------ render */

static void sb_mix_key(int i, const char *field, char *buf, size_t n)
{
	snprintf(buf, n, "mix%d_%s", i + 1, field);
}

/* Resolve the configured layer sources from their names.
 * Runs on the video thread (from sb_tick) so it cannot race the renderer. */
static void sb_refresh_layers(struct sb_filter *s, obs_data_t *st)
{
	for (int i = 0; i < SB_MIX_LAYERS; i++) {
		char ks[SB_MIX_KEYBUF];
		sb_mix_key(i, "src", ks, sizeof(ks));

		const char *name = obs_data_get_string(st, ks);
		if (!name)
			name = "";
		if (strcmp(name, s->layer_name[i]) == 0)
			continue; /* unchanged */

		snprintf(s->layer_name[i], sizeof(s->layer_name[i]), "%s", name);

		obs_source_t *want = NULL;
		if (*name && strcmp(name, "(none)") != 0)
			want = obs_get_source_by_name(name);

		if (s->layers[i])
			obs_source_release(s->layers[i]);
		s->layers[i] = want;
	}
}

/* Draw one layer into the mixdown, with its transform. */
static void sb_draw_layer(struct sb_filter *s, obs_source_t *src, obs_data_t *st,
			  int i, uint32_t w, uint32_t h)
{
	char ko[SB_MIX_KEYBUF], km[SB_MIX_KEYBUF], kx[SB_MIX_KEYBUF];
	sb_mix_key(i, "opacity", ko, sizeof(ko));
	sb_mix_key(i, "mode", km, sizeof(km));
	sb_mix_key(i, "x", kx, sizeof(kx));

	double opacity = sb_d(st, ko);
	if (opacity <= 0.001)
		return;

	char ky[SB_MIX_KEYBUF], ks[SB_MIX_KEYBUF], kr[SB_MIX_KEYBUF];
	sb_mix_key(i, "y", ky, sizeof(ky));
	sb_mix_key(i, "scale", ks, sizeof(ks));
	sb_mix_key(i, "rot", kr, sizeof(kr));

	float sx = (float)sb_d(st, ks);
	float px = (float)sb_d(st, kx) * (float)w;
	float py = (float)sb_d(st, ky) * (float)h;
	float rot = (float)sb_d(st, kr) * 6.2831853f;

	uint32_t lw = obs_source_get_base_width(src);
	uint32_t lh = obs_source_get_base_height(src);
	if (!lw || !lh)
		return;

	gs_texrender_t *lb = s->layer_buf;
	gs_texrender_reset(lb);
	if (!gs_texrender_begin(lb, lw, lh))
		return;

	struct vec4 clear;
	vec4_zero(&clear);
	gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
	gs_ortho(0.0f, (float)lw, 0.0f, (float)lh, -100.0f, 100.0f);

	uint32_t flags = obs_source_get_output_flags(src);
	bool custom = (flags & OBS_SOURCE_CUSTOM_DRAW) != 0;
	bool async = (flags & OBS_SOURCE_ASYNC) != 0;
	if (!custom && !async)
		obs_source_default_render(src);
	else
		obs_source_video_render(src);

	gs_texrender_end(lb);

	gs_texture_t *tex = gs_texrender_get_texture(lb);
	if (!tex)
		return;

	/* place it: centre + scale + rotate, over the output canvas */
	gs_matrix_push();
	gs_matrix_translate3f((float)w * 0.5f + px, (float)h * 0.5f + py, 0.0f);
	gs_matrix_rotaa4f(0.0f, 0.0f, 1.0f, rot);
	gs_matrix_scale3f(sx, sx, 1.0f);
	gs_matrix_translate3f(-(float)lw * 0.5f, -(float)lh * 0.5f, 0.0f);

	int mode = (int)sb_i(st, km);

	gs_blend_state_push();
	switch (mode) {
	case 0: /* normal */
	default:
		gs_blend_function_separate(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA,
					   GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
		break;
	case 1: /* add */
		gs_blend_function_separate(GS_BLEND_SRCALPHA, GS_BLEND_ONE,
					   GS_BLEND_ONE, GS_BLEND_ONE);
		break;
	case 2: /* screen */
		gs_blend_function_separate(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCCOLOR,
					   GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
		break;
	case 3: /* multiply */
		gs_blend_function_separate(GS_BLEND_DSTCOLOR, GS_BLEND_ZERO,
					   GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
		break;
	case 4: /* max / lighten */
		gs_blend_function_separate(GS_BLEND_ONE, GS_BLEND_ONE,
					   GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
		break;
	}

	gs_effect_set_float(s->p->layer_opacity, (float)opacity);
	gs_effect_set_texture(s->p->u_tex, tex);

	gs_technique_t *t = gs_effect_get_technique(s->effect, "Layer");
	size_t np = gs_technique_begin(t);
	for (size_t pi = 0; pi < np; pi++) {
		gs_technique_begin_pass(t, pi);
		gs_draw_sprite(tex, 0, lw, lh);
		gs_technique_end_pass(t);
	}
	gs_technique_end(t);

	gs_blend_state_pop();

	gs_matrix_pop();
}

static void sb_render(void *data, gs_effect_t *effect)
{
	struct sb_filter *s = data;
	UNUSED_PARAMETER(effect);

	obs_source_t *target = obs_filter_get_target(s->context);
	obs_source_t *parent = obs_filter_get_parent(s->context);

	if (!target || !parent || !s->effect || !s->p) {
		obs_source_skip_video_filter(s->context);
		return;
	}

	uint32_t w = obs_source_get_base_width(target);
	uint32_t h = obs_source_get_base_height(target);
	if (!w || !h) {
		obs_source_skip_video_filter(s->context);
		return;
	}

	obs_data_t *st = s->eff ? s->eff : obs_source_get_settings(s->context);
	bool st_owned = (st != s->eff);

	/* Internal render resolution. Lowering it cuts fill cost quadratically,
	 * which is the main lever for the raymarched stages. The final blit
	 * still happens at full output size. */
	double rscale = sb_d(st, S_RENDER_SCALE);
	if (rscale < 0.1 || rscale > 1.0)
		rscale = 1.0;

	uint32_t out_w = w, out_h = h;
	if (rscale < 0.999) {
		uint32_t iw = (uint32_t)((double)w * rscale + 0.5);
		uint32_t ih = (uint32_t)((double)h * rscale + 0.5);
		if (iw < 8)
			iw = 8;
		if (ih < 8)
			ih = 8;
		w = iw;
		h = ih;
	}

	int delay = sb_i(st, S_FB_DELAY);
	if (delay < 1)
		delay = 1;
	if (delay > 32)
		delay = 32;

	bool fb_on = obs_data_get_bool(st, S_FB_ON);

	sb_ensure_buffers(s, w, h, (uint32_t)delay);
	if (!s->capture || !s->buf_a || !s->buf_b || !s->mixdown) {
		if (st_owned)
			obs_data_release(st);
		obs_source_skip_video_filter(s->context);
		return;
	}

	/* ---- 1. capture the incoming frame --------------------------------- */

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);

	gs_texrender_reset(s->capture);
	bool captured = false;
	if (gs_texrender_begin(s->capture, w, h)) {
		struct vec4 clear;
		vec4_zero(&clear);
		gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
		gs_ortho(0.0f, (float)w, 0.0f, (float)h, -100.0f, 100.0f);

		uint32_t flags = obs_source_get_output_flags(target);
		bool custom = (flags & OBS_SOURCE_CUSTOM_DRAW) != 0;
		bool async = (flags & OBS_SOURCE_ASYNC) != 0;

		if (target == parent && !custom && !async)
			obs_source_default_render(target);
		else
			obs_source_video_render(target);

		gs_texrender_end(s->capture);
		captured = true;
	}

	gs_blend_state_pop();

	if (!captured) {
		if (st_owned)
			obs_data_release(st);
		obs_source_skip_video_filter(s->context);
		return;
	}

	gs_texture_t *cur = gs_texrender_get_texture(s->capture);

	/* ---- 1b. multi-source mixer: composite extra layers ---------------- */

	if (obs_data_get_bool(st, S_MIX_ON) && s->layer_buf && s->mixbuf) {
		gs_texrender_reset(s->mixbuf);
		if (gs_texrender_begin(s->mixbuf, w, h)) {
			struct vec4 clear;
			vec4_zero(&clear);
			gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
			gs_ortho(0.0f, (float)w, 0.0f, (float)h, -100.0f, 100.0f);

			/* the filter's own input is layer 0, drawn opaque */
			gs_blend_state_push();
			gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
			gs_effect_set_float(s->p->layer_opacity, 1.0f);
			gs_effect_set_texture(s->p->u_tex, cur);
			gs_technique_t *t0 = gs_effect_get_technique(s->effect, "Layer");
			size_t n0 = gs_technique_begin(t0);
			for (size_t pi = 0; pi < n0; pi++) {
				gs_technique_begin_pass(t0, pi);
				gs_draw_sprite(cur, 0, w, h);
				gs_technique_end_pass(t0);
			}
			gs_technique_end(t0);
			gs_blend_state_pop();

			for (int i = 0; i < SB_MIX_LAYERS; i++) {
				if (s->layers[i])
					sb_draw_layer(s, s->layers[i], st, i, w, h);
			}

			gs_texrender_end(s->mixbuf);
			gs_texture_t *mixed = gs_texrender_get_texture(s->mixbuf);
			if (mixed)
				cur = mixed;
		}
	}

	/* ---- 2. mixer: source + delayed feedback --------------------------- */

	if (fb_on && s->fb_ring && s->fb_ring_valid) {
		uint32_t read_idx = (s->fb_ring_pos + 1u) % s->fb_ring_size;
		gs_texture_t *prev = gs_texrender_get_texture(s->fb_ring[read_idx]);
		if (prev) {
			gs_effect_set_texture(s->p->prev_tex, prev);
			gs_effect_set_float(s->p->fb_alpha, sb_d(st, S_FB_ALPHA));
			gs_effect_set_int(s->p->fb_mode, sb_i(st, S_FB_MODE));
			gs_texrender_reset(s->mixdown);
			if (gs_texrender_begin(s->mixdown, w, h)) {
				struct vec4 clear;
				vec4_zero(&clear);
				gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
				gs_ortho(0.0f, (float)w, 0.0f, (float)h, -100.0f, 100.0f);

				gs_effect_set_texture(s->p->u_tex, cur);
				gs_effect_set_float(s->p->u_ratio, (float)w / (float)h);

				gs_technique_t *t = gs_effect_get_technique(s->effect, "Feedback");
				size_t np = gs_technique_begin(t);
				for (size_t i = 0; i < np; i++) {
					gs_technique_begin_pass(t, i);
					gs_draw_sprite(cur, 0, w, h);
					gs_technique_end_pass(t);
				}
				gs_technique_end(t);
				gs_texrender_end(s->mixdown);
				cur = gs_texrender_get_texture(s->mixdown);
			}
		}
	}

	int tap = sb_i(st, S_FB_TAP);
	gs_texture_t *tap_tex = (tap == TAP_MIXER) ? cur : NULL;

	/* ---- 3. Camera Transform ------------------------------------------- */

	float ct_zoom = sb_d(st, S_CT_ZOOM);
	float ct_tx = sb_d(st, S_CT_TX), ct_ty = sb_d(st, S_CT_TY);
	float ct_tix = sb_d(st, S_CT_TILTX), ct_tiy = sb_d(st, S_CT_TILTY);
	float ct_bd = sb_d(st, S_CT_BEND), ct_wb = sb_d(st, S_CT_WOBBLE);
	float ct_wp = sb_d(st, S_CT_WARP), ct_rgb = sb_d(st, S_CT_RGB);

	bool ct_active = fabsf(ct_zoom - 1.0f) > 0.002f || fabsf(ct_tx) > 0.002f ||
			 fabsf(ct_ty) > 0.002f || fabsf(ct_tix) > 0.002f ||
			 fabsf(ct_tiy) > 0.002f || fabsf(ct_bd) > 0.002f ||
			 fabsf(ct_wb) > 0.002f || fabsf(ct_rgb) > 0.002f ||
			 fabsf(sb_d(st, S_CT_ANGLE)) > 0.002f ||
			 fabsf(sb_d(st, S_CT_SPIN)) > 0.0001f;

	if (ct_active) {
		sb_setup_transform(s, st, false, (float)w, (float)h);
		cur = sb_pass_auto(s, "Transform", cur, w, h);
	}
	if (tap == TAP_CAM_TRANSFORM)
		tap_tex = cur;

	/* ---- 4. Color ------------------------------------------------------- */

	float brit = sb_d(st, S_BRIT), hue = sb_d(st, S_HUE), neg = sb_d(st, S_NEG);
	float glow = sb_d(st, S_GLOW), con = sb_d(st, S_CONTRAST), vib = sb_d(st, S_VIBRANCE);
	float color_amt = sb_d(st, S_C_AMT);

	bool color_active = color_amt > 0.001f &&
			    (fabsf(brit - 1.0f) > 0.002f || hue > 0.01f || neg > 0.001f ||
			     glow > 0.01f || fabsf(con - 1.0f) > 0.002f ||
			     fabsf(vib - 1.0f) > 0.002f);

	if (color_active) {
		gs_effect_set_float(s->p->brit, brit);
		gs_effect_set_float(s->p->hue, hue);
		gs_effect_set_float(s->p->neg, neg);
		gs_effect_set_float(s->p->glow, glow);
		gs_effect_set_float(s->p->contrast, con);
		gs_effect_set_float(s->p->vibrance, vib);
		cur = sb_pass_auto(s, "Color", cur, w, h);
	}
	if (tap == TAP_COLOR)
		tap_tex = cur;

	/* ---- 5. Edge Detect ------------------------------------------------- */

	float e_amt = sb_d(st, S_E_AMT);
	if (e_amt > 0.001f) {
		gs_effect_set_float(s->p->e_amt, e_amt);
		gs_effect_set_float(s->p->e_thresh, sb_d(st, S_E_THRESH));
		gs_effect_set_float(s->p->e_hue, sb_d(st, S_E_HUE));
		gs_effect_set_float(s->p->e_sat, sb_d(st, S_E_SAT));
		gs_effect_set_float(s->p->e_brit, sb_d(st, S_E_BRIT));
		struct vec2 px;
		vec2_set(&px, 1.0f / (float)w, 1.0f / (float)h);
		gs_effect_set_vec2(s->p->e_pixel, &px);
		cur = sb_pass_auto(s, "Edge", cur, w, h);
	}
	if (tap == TAP_EDGE)
		tap_tex = cur;

	/* ---- 6. Kaleidoscope ------------------------------------------------ */

	float k_amt = sb_d(st, S_K_AMT);
	int k_axis = sb_i(st, S_K_AXIS);
	if (k_axis < 1)
		k_axis = 1;
	if (k_axis > 25)
		k_axis = 25;

	if (k_amt > 0.001f) {
		gs_effect_set_float(s->p->k_axis, (float)k_axis);
		gs_effect_set_float(s->p->k_amt, sb_clampf(k_amt, 0.0f, 1.0f));
		gs_effect_set_float(s->p->k_zoom, sb_d(st, S_K_ZOOM));
		cur = sb_pass_auto(s, "Kaleido", cur, w, h);
	}
	if (tap == TAP_KALEIDO)
		tap_tex = cur;

	/* ---- 7. Droste ------------------------------------------------------ */

	float d_mix = sb_d(st, S_D_MIX);
	if (d_mix > 0.001f) {
		float scale = sb_d(st, S_D_SCALE);
		if (scale < 1.05f)
			scale = 1.05f;
		float logs = logf(scale);

		float arms = sb_d(st, S_D_ARMS);
		if (arms < 0.0f)
			arms = 0.0f;
		float armsw = floorf(arms);

		gs_effect_set_float(s->p->d_mix, sb_clampf(d_mix, 0.0f, 1.0f));
		gs_effect_set_float(s->p->d_logs, logs);
		gs_effect_set_float(s->p->d_c, armsw * logs / SB_PI);
		gs_effect_set_float(s->p->d_w, arms - armsw);
		gs_effect_set_float(s->p->d_phase, s->droste_phase + sb_d(st, S_D_ZOOM) * 2.0f);
		gs_effect_set_float(s->p->d_ring, sb_d(st, S_D_RING));
		struct vec2 look;
		vec2_set(&look, sb_d(st, S_D_LOOK_X), sb_d(st, S_D_LOOK_Y));
		gs_effect_set_vec2(s->p->d_look, &look);

		cur = sb_pass_auto(s, "Droste", cur, w, h);
	}
	if (tap == TAP_DROSTE)
		tap_tex = cur;

	/* ---- 8. Master Transform ------------------------------------------- */

	float mt_zoom = sb_d(st, S_MT_ZOOM);
	bool mt_active = fabsf(mt_zoom - 1.0f) > 0.002f ||
			 fabsf(sb_d(st, S_MT_TX)) > 0.002f ||
			 fabsf(sb_d(st, S_MT_TY)) > 0.002f ||
			 fabsf(sb_d(st, S_MT_TILTX)) > 0.002f ||
			 fabsf(sb_d(st, S_MT_TILTY)) > 0.002f ||
			 fabsf(sb_d(st, S_MT_BEND)) > 0.002f ||
			 fabsf(sb_d(st, S_MT_WOBBLE)) > 0.002f ||
			 fabsf(sb_d(st, S_MT_RGB)) > 0.002f ||
			 fabsf(sb_d(st, S_MT_ANGLE)) > 0.002f ||
			 fabsf(sb_d(st, S_MT_SPIN)) > 0.0001f;

	if (mt_active) {
		sb_setup_transform(s, st, true, (float)w, (float)h);
		cur = sb_pass_auto(s, "Transform", cur, w, h);
	}
	if (tap == TAP_MASTER)
		tap_tex = cur;

	/* ---- 9. 3D Tunnel --------------------------------------------------- */

	float tn_mix = sb_d(st, S_TN_MIX);
	if (tn_mix > 0.001f) {
		gs_effect_set_float(s->p->tn_mix, sb_clampf(tn_mix, 0.0f, 1.0f));
		gs_effect_set_float(s->p->tn_shape, sb_d(st, S_TN_SHAPE));
		gs_effect_set_float(s->p->tn_fov, sb_d(st, S_TN_FOV));
		gs_effect_set_float(s->p->tn_scroll, s->tunnel_scroll);
		gs_effect_set_float(s->p->tn_hstr, sb_d(st, S_TN_HSTR));
		gs_effect_set_float(s->p->tn_hpos, sb_d(st, S_TN_HPOS));
		gs_effect_set_float(s->p->tn_wstr, sb_d(st, S_TN_WSTR));
		gs_effect_set_float(s->p->tn_wpos, sb_d(st, S_TN_WPOS));
		gs_effect_set_float(s->p->tn_curve, sb_d(st, S_TN_CURVE));
		gs_effect_set_float(s->p->tn_twist, sb_d(st, S_TN_TWIST));
		gs_effect_set_float(s->p->tn_flux, sb_d(st, S_TN_FLUX));
		gs_effect_set_float(s->p->tn_fogd, sb_d(st, S_TN_FOGD));
		gs_effect_set_float(s->p->tn_fogh, sb_d(st, S_TN_FOGH));
		gs_effect_set_float(s->p->tn_fogs, sb_d(st, S_TN_FOGS));
		gs_effect_set_float(s->p->tn_fogv, sb_d(st, S_TN_FOGV));
		struct vec2 look;
		vec2_set(&look, sb_d(st, S_TN_LOOK_X), sb_d(st, S_TN_LOOK_Y));
		gs_effect_set_vec2(s->p->tn_look, &look);
		gs_effect_set_float(s->p->tn_roll, sb_d(st, S_TN_ROLL) * 6.2831853f);

		cur = sb_pass_auto(s, "Tunnel", cur, w, h);
	}
	if (tap == TAP_TUNNEL)
		tap_tex = cur;

	/* ---- 10. Swirl ------------------------------------------------------ */

	float sw_str = sb_d(st, S_SW_STR);
	if (sw_str > 0.001f) {
		gs_effect_set_float(s->p->sw_str, sb_clampf(sw_str, 0.0f, 1.0f));
		gs_effect_set_float(s->p->sw_wide, sb_d(st, S_SW_WIDE));
		gs_effect_set_float(s->p->sw_scroll, s->swirl_scroll);
		gs_effect_set_float(s->p->sw_sway, s->swirl_sway);
		gs_effect_set_float(s->p->sw_fogd, sb_d(st, S_SW_FOGD));
		gs_effect_set_float(s->p->sw_fogf, sb_d(st, S_SW_FOGF));
		gs_effect_set_float(s->p->sw_fogh, sb_d(st, S_SW_FOGH));
		gs_effect_set_float(s->p->sw_fogs, sb_d(st, S_SW_FOGS));
		gs_effect_set_float(s->p->sw_fogv, sb_d(st, S_SW_FOGV));
		struct vec2 look;
		vec2_set(&look, sb_d(st, S_SW_LOOK_X), sb_d(st, S_SW_LOOK_Y));
		gs_effect_set_vec2(s->p->sw_look, &look);
		gs_effect_set_float(s->p->sw_roll, sb_d(st, S_SW_ROLL) * 6.2831853f);

	int sw_steps = sb_i(st, S_SW_STEPS);
	if (sw_steps < 8)
		sw_steps = 8;
	if (sw_steps > 60)
		sw_steps = 60;
	gs_effect_set_float(s->p->sw_steps, (float)sw_steps);

		cur = sb_pass_auto(s, "Swirl", cur, w, h);
	}

	/* ---- 11. store this frame for the feedback ring --------------------- */

	if (fb_on && s->fb_ring && s->fb_ring_size > 0) {
		gs_texture_t *src = tap_tex ? tap_tex : cur;
		sb_copy_into(s, src, s->fb_ring[s->fb_ring_pos], w, h);
		s->fb_ring_pos = (s->fb_ring_pos + 1u) % s->fb_ring_size;
		s->fb_ring_valid = true;
	}

	/* ---- 12. draw the result ------------------------------------------- */

	if (cur) {
		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
		gs_ortho(0.0f, (float)out_w, 0.0f, (float)out_h, -100.0f, 100.0f);

		gs_effect_t *def = obs_get_base_effect(OBS_EFFECT_DEFAULT);
		gs_eparam_t *img = gs_effect_get_param_by_name(def, "image");
		gs_effect_set_texture(img, cur);
		while (gs_effect_loop(def, "Draw"))
			gs_draw_sprite(cur, 0, out_w, out_h);

		gs_blend_state_pop();
	}

	if (st_owned)
		obs_data_release(st);
}

/* -------------------------------------------------------------- defaults */

static void sb_defaults(obs_data_t *s)
{
	obs_data_set_default_double(s, S_CT_ZOOM, 1.0);
	obs_data_set_default_double(s, S_CT_ANGLE, 0.0);
	obs_data_set_default_double(s, S_CT_TX, 0.0);
	obs_data_set_default_double(s, S_CT_TY, 0.0);
	obs_data_set_default_double(s, S_CT_TILTX, 0.0);
	obs_data_set_default_double(s, S_CT_TILTY, 0.0);
	obs_data_set_default_double(s, S_CT_BEND, 0.0);
	obs_data_set_default_double(s, S_CT_WOBBLE, 0.0);
	obs_data_set_default_double(s, S_CT_WARP, 0.0);
	obs_data_set_default_double(s, S_CT_RGB, 0.0);
	obs_data_set_default_double(s, S_CT_SPIN, 0.0);

	obs_data_set_default_double(s, S_C_AMT, 1.0);
	obs_data_set_default_double(s, S_BRIT, 1.0);
	obs_data_set_default_double(s, S_HUE, 0.0);
	obs_data_set_default_double(s, S_NEG, 0.0);
	obs_data_set_default_double(s, S_GLOW, 0.0);
	obs_data_set_default_double(s, S_CONTRAST, 1.0);
	obs_data_set_default_double(s, S_VIBRANCE, 1.0);

	obs_data_set_default_double(s, S_E_AMT, 0.0);
	obs_data_set_default_double(s, S_E_THRESH, 0.06);
	obs_data_set_default_double(s, S_E_HUE, 0.0);
	obs_data_set_default_double(s, S_E_SAT, 1.0);
	obs_data_set_default_double(s, S_E_BRIT, 1.0);

	obs_data_set_default_int(s, S_K_AXIS, 6);
	obs_data_set_default_double(s, S_K_AMT, 1.0);
	obs_data_set_default_double(s, S_K_ZOOM, 1.0);

	obs_data_set_default_double(s, S_D_MIX, 0.0);
	obs_data_set_default_double(s, S_D_SCALE, 3.0);
	obs_data_set_default_double(s, S_D_ARMS, 1.0);
	obs_data_set_default_double(s, S_D_SPEED, 0.0);
	obs_data_set_default_double(s, S_D_RING, 0.5);
	obs_data_set_default_double(s, S_D_ZOOM, 0.0);
	obs_data_set_default_double(s, S_D_LOOK_X, 0.0);
	obs_data_set_default_double(s, S_D_LOOK_Y, 0.0);

	obs_data_set_default_double(s, S_MT_ZOOM, 1.0);
	obs_data_set_default_double(s, S_MT_ANGLE, 0.0);
	obs_data_set_default_double(s, S_MT_TX, 0.0);
	obs_data_set_default_double(s, S_MT_TY, 0.0);
	obs_data_set_default_double(s, S_MT_TILTX, 0.0);
	obs_data_set_default_double(s, S_MT_TILTY, 0.0);
	obs_data_set_default_double(s, S_MT_BEND, 0.0);
	obs_data_set_default_double(s, S_MT_WOBBLE, 0.0);
	obs_data_set_default_double(s, S_MT_WARP, 0.0);
	obs_data_set_default_double(s, S_MT_RGB, 0.0);
	obs_data_set_default_double(s, S_MT_SPIN, 0.0);

	obs_data_set_default_double(s, S_TN_MIX, 0.0);
	obs_data_set_default_double(s, S_TN_SHAPE, 0.0);
	obs_data_set_default_double(s, S_TN_FOV, 0.5);
	obs_data_set_default_double(s, S_TN_SPEED, 0.0);
	obs_data_set_default_double(s, S_TN_HSTR, 0.0);
	obs_data_set_default_double(s, S_TN_HPOS, 0.0);
	obs_data_set_default_double(s, S_TN_WSTR, 0.0);
	obs_data_set_default_double(s, S_TN_WPOS, 0.0);
	obs_data_set_default_double(s, S_TN_CURVE, 1.0);
	obs_data_set_default_double(s, S_TN_TWIST, 0.0);
	obs_data_set_default_double(s, S_TN_FLUX, 0.0);
	obs_data_set_default_double(s, S_TN_FOGD, 0.0);
	obs_data_set_default_double(s, S_TN_FOGH, 0.0);
	obs_data_set_default_double(s, S_TN_FOGS, 0.0);
	obs_data_set_default_double(s, S_TN_FOGV, 1.0);
	obs_data_set_default_double(s, S_TN_LOOK_X, 0.0);
	obs_data_set_default_double(s, S_TN_LOOK_Y, 0.0);
	obs_data_set_default_double(s, S_TN_ROLL, 0.0);

	obs_data_set_default_double(s, S_SW_STR, 0.0);
	obs_data_set_default_double(s, S_SW_WIDE, 1.0);
	obs_data_set_default_double(s, S_SW_SPEED, 0.0);
	obs_data_set_default_double(s, S_SW_ACTIVITY, 1.0);
	obs_data_set_default_double(s, S_SW_FOGD, 0.0);
	obs_data_set_default_double(s, S_SW_FOGF, 0.0);
	obs_data_set_default_double(s, S_SW_FOGH, 0.0);
	obs_data_set_default_double(s, S_SW_FOGS, 0.0);
	obs_data_set_default_double(s, S_SW_FOGV, 0.0);
	obs_data_set_default_double(s, S_SW_LOOK_X, 0.0);
	obs_data_set_default_double(s, S_SW_LOOK_Y, 0.0);
	obs_data_set_default_double(s, S_SW_ROLL, 0.0);

	obs_data_set_default_double(s, S_RENDER_SCALE, 1.0);
	obs_data_set_default_int(s, S_SW_STEPS, 60);

	obs_data_set_default_bool(s, S_FB_ON, false);
	obs_data_set_default_int(s, S_FB_TAP, TAP_EDGE);
	obs_data_set_default_double(s, S_FB_ALPHA, 0.85);
	obs_data_set_default_int(s, S_FB_MODE, 1);
	obs_data_set_default_int(s, S_FB_DELAY, 1);

	obs_data_set_default_int(s, S_PRESET_SLOT, 1);
	obs_data_set_default_double(s, S_PRESET_TIME, 1.0);
	obs_data_set_default_int(s, S_PRESET_CMD, SB_CMD_NONE);
	obs_data_set_default_string(s, S_BANK_PATH, "");

	obs_data_set_default_double(s, S_BPM, 120.0);
	obs_data_set_default_bool(s, S_LFO_SYNC, true);
	for (int i = 0; i < SB_LFOS; i++) {
		char k[SB_LFO_KEYBUF];
		sb_lfo_key(i, "on", k, sizeof(k));
		obs_data_set_default_bool(s, k, false);
		sb_lfo_key(i, "target", k, sizeof(k));
		obs_data_set_default_int(s, k, -1);
		sb_lfo_key(i, "wave", k, sizeof(k));
		obs_data_set_default_int(s, k, LFO_SINE);
		sb_lfo_key(i, "rate", k, sizeof(k));
		obs_data_set_default_double(s, k, 1.0);
		sb_lfo_key(i, "depth", k, sizeof(k));
		obs_data_set_default_double(s, k, 0.25);
		sb_lfo_key(i, "phase", k, sizeof(k));
		obs_data_set_default_double(s, k, 0.0);
	}

	obs_data_set_default_bool(s, S_MIX_ON, false);
	for (int i = 0; i < SB_MIX_LAYERS; i++) {
		char k[SB_MIX_KEYBUF];
		sb_mix_key(i, "src", k, sizeof(k));
		obs_data_set_default_string(s, k, "");
		sb_mix_key(i, "opacity", k, sizeof(k));
		obs_data_set_default_double(s, k, 1.0);
		sb_mix_key(i, "mode", k, sizeof(k));
		obs_data_set_default_int(s, k, 0);
		sb_mix_key(i, "x", k, sizeof(k));
		obs_data_set_default_double(s, k, 0.0);
		sb_mix_key(i, "y", k, sizeof(k));
		obs_data_set_default_double(s, k, 0.0);
		sb_mix_key(i, "scale", k, sizeof(k));
		obs_data_set_default_double(s, k, 1.0);
		sb_mix_key(i, "rot", k, sizeof(k));
		obs_data_set_default_double(s, k, 0.0);
	}
}

/* ------------------------------------------------------------- presets --- */

/* Copy every animatable (non preset_*) numeric/bool key out of `src`. */
static obs_data_t *sb_snapshot_params(obs_data_t *src)
{
	obs_data_t *out = obs_data_create();
	obs_data_item_t *item = obs_data_first(src);

	while (item) {
		const char *name = obs_data_item_get_name(item);
		if (name && strncmp(name, SB_PRESET_PREFIX, 7) != 0) {
			switch (obs_data_item_gettype(item)) {
			case OBS_DATA_NUMBER:
				obs_data_set_double(out, name, obs_data_item_get_double(item));
				break;
			case OBS_DATA_BOOLEAN:
				obs_data_set_bool(out, name, obs_data_item_get_bool(item));
				break;
			default:
				break;
			}
		}
		obs_data_item_next(&item);
	}
	return out;
}

static void sb_preset_key(int slot, char *buf, size_t n)
{
	snprintf(buf, n, SB_PRESET_PREFIX "%d", slot);
}

/* Core actions, shared by the property buttons and the hotkeys. */

static void sb_preset_save_action(struct sb_filter *s)
{
	obs_data_t *settings = obs_source_get_settings(s->context);
	int slot = (int)obs_data_get_int(settings, S_PRESET_SLOT);
	if (slot < 1 || slot > SB_PRESETS)
		slot = 1;

	obs_data_t *snap = sb_snapshot_params(settings);
	char key[32];
	sb_preset_key(slot, key, sizeof(key));
	obs_data_set_string(settings, key, obs_data_get_json(snap));

	obs_data_release(snap);
	obs_source_update(s->context, settings);
	obs_data_release(settings);
	blog(LOG_INFO, "[spacebeam] saved preset %d", slot);
}

static void sb_preset_load_action(struct sb_filter *s)
{
	obs_data_t *settings = obs_source_get_settings(s->context);
	int slot = (int)obs_data_get_int(settings, S_PRESET_SLOT);
	if (slot < 1 || slot > SB_PRESETS)
		slot = 1;

	char key[32];
	sb_preset_key(slot, key, sizeof(key));
	const char *json = obs_data_get_string(settings, key);

	if (json && *json) {
		obs_data_t *target = obs_data_create_from_json(json);
		if (target) {
			if (s->morph_from)
				obs_data_release(s->morph_from);
			if (s->morph_to)
				obs_data_release(s->morph_to);

			s->morph_from = sb_snapshot_params(settings);
			s->morph_to = target;
			s->morph_time = 0.0f;
			s->morph_total = (float)obs_data_get_double(settings, S_PRESET_TIME);
			s->morphing = true;
			blog(LOG_INFO, "[spacebeam] load preset %d (morph %.2fs)", slot,
			     s->morph_total);
		}
	} else {
		blog(LOG_INFO, "[spacebeam] preset %d is empty", slot);
	}

	obs_data_release(settings);
}

static bool sb_preset_save(obs_properties_t *props, obs_property_t *p, void *data)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(p);
	sb_preset_save_action(data);
	return false;
}

static bool sb_preset_load(obs_properties_t *props, obs_property_t *p, void *data)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(p);
	sb_preset_load_action(data);
	return false;
}

static void sb_preset_clear_action(struct sb_filter *s)
{
	obs_data_t *settings = obs_source_get_settings(s->context);
	int slot = (int)obs_data_get_int(settings, S_PRESET_SLOT);
	char key[32];
	sb_preset_key(slot, key, sizeof(key));
	obs_data_unset_user_value(settings, key);
	obs_source_update(s->context, settings);
	obs_data_release(settings);
	blog(LOG_INFO, "[spacebeam] cleared preset %d", slot);
}

static bool sb_preset_clear(obs_properties_t *props, obs_property_t *p, void *data)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(p);
	sb_preset_clear_action(data);
	return false;
}

/* --------------------------------------------------------------- hotkeys */

static void sb_hotkey_load(void *data, obs_hotkey_id id, obs_hotkey_t *hk, bool pressed)
{
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hk);
	if (!pressed)
		return;
	sb_preset_load_action(data);
}

static void sb_hotkey_save(void *data, obs_hotkey_id id, obs_hotkey_t *hk, bool pressed)
{
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hk);
	if (!pressed)
		return;
	sb_preset_save_action(data);
}

/* ------------------------------------------------- preset bank i/o ------ */

#define SB_BANK_MAGIC "spacebeam-preset-bank"
#define SB_BANK_VERSION 1

static bool sb_bank_save_as(struct sb_filter *s)
{
	char path[MAX_PATH] = {0};

	/* A configured path skips the dialog, which makes this scriptable. */
	obs_data_t *cur = obs_source_get_settings(s->context);
	const char *pre = obs_data_get_string(cur, S_BANK_PATH);
	if (pre && *pre) {
		snprintf(path, sizeof(path), "%s", pre);
		obs_data_release(cur);
		goto write_bank;
	}
	obs_data_release(cur);

	OPENFILENAMEW ofn = {0};
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = NULL;
	ofn.lpstrFilter = L"SpaceBeam preset bank (*.json)\0*.json\0All files (*.*)\0*.*\0";
	ofn.lpstrFile = NULL; /* set below */
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrDefExt = L"json";
	ofn.lpstrTitle = L"Export SpaceBeam preset bank";
	ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

	wchar_t wbuf[MAX_PATH] = L"spacebeam-bank.json";
	ofn.lpstrFile = wbuf;

	if (!GetSaveFileNameW(&ofn))
		return false;

	/* convert the chosen path back to UTF-8 */
	int need = WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, NULL, 0, NULL, NULL);
	if (need <= 0)
		return false;
	if ((size_t)need >= sizeof(path))
		return false;
	WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, path, (int)sizeof(path), NULL, NULL);

write_bank:
	{
	obs_data_t *settings = obs_source_get_settings(s->context);
	obs_data_t *bank = obs_data_create();

	obs_data_set_string(bank, "magic", SB_BANK_MAGIC);
	obs_data_set_int(bank, "version", SB_BANK_VERSION);
	obs_data_set_int(bank, "slots", SB_PRESETS);

	for (int i = 1; i <= SB_PRESETS; i++) {
		char k[32];
		sb_preset_key(i, k, sizeof(k));
		const char *json = obs_data_get_string(settings, k);
		if (json && *json)
			obs_data_set_string(bank, k, json);
	}

	/* also store the live look so an empty bank still round-trips */
	obs_data_t *snap = sb_snapshot_params(settings);
	obs_data_set_string(bank, "current", obs_data_get_json(snap));
	obs_data_release(snap);

	bool ok = obs_data_save_json(bank, path);
	obs_data_release(bank);
	obs_data_release(settings);

	blog(LOG_INFO, "[spacebeam] export bank -> %s (%s)", path, ok ? "ok" : "FAILED");
	return ok;
	}
}

static bool sb_bank_load(struct sb_filter *s)
{
	char path[MAX_PATH] = {0};

	obs_data_t *cur = obs_source_get_settings(s->context);
	const char *pre = obs_data_get_string(cur, S_BANK_PATH);
	if (pre && *pre) {
		snprintf(path, sizeof(path), "%s", pre);
		obs_data_release(cur);
		goto read_bank;
	}
	obs_data_release(cur);

	wchar_t wbuf[MAX_PATH] = {0};

	OPENFILENAMEW ofn = {0};
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = NULL;
	ofn.lpstrFilter = L"SpaceBeam preset bank (*.json)\0*.json\0All files (*.*)\0*.*\0";
	ofn.lpstrFile = wbuf;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrDefExt = L"json";
	ofn.lpstrTitle = L"Import SpaceBeam preset bank";
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

	if (!GetOpenFileNameW(&ofn))
		return false;

	int need = WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, NULL, 0, NULL, NULL);
	if (need <= 0 || (size_t)need >= sizeof(path))
		return false;
	WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, path, (int)sizeof(path), NULL, NULL);

read_bank:
	{
	obs_data_t *bank = obs_data_create_from_json_file(path);
	if (!bank) {
		blog(LOG_WARNING, "[spacebeam] import: cannot read %s", path);
		return false;
	}

	const char *magic = obs_data_get_string(bank, "magic");
	if (!magic || strcmp(magic, SB_BANK_MAGIC) != 0) {
		blog(LOG_WARNING, "[spacebeam] import: %s is not a SpaceBeam bank", path);
		obs_data_release(bank);
		return false;
	}

	obs_data_t *settings = obs_source_get_settings(s->context);
	int loaded = 0;

	for (int i = 1; i <= SB_PRESETS; i++) {
		char k[32];
		sb_preset_key(i, k, sizeof(k));
		const char *json = obs_data_get_string(bank, k);
		if (json && *json) {
			obs_data_set_string(settings, k, json);
			loaded++;
		}
	}

	obs_source_update(s->context, settings);
	obs_data_release(settings);
	obs_data_release(bank);

	blog(LOG_INFO, "[spacebeam] import bank <- %s (%d slots)", path, loaded);
	return true;
	}
}

static bool sb_preset_export(obs_properties_t *props, obs_property_t *p, void *data)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(p);
	return sb_bank_save_as(data) ? false : false;
}

static bool sb_preset_import(obs_properties_t *props, obs_property_t *p, void *data)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(p);
	sb_bank_load(data);
	return true; /* refresh properties, slot contents changed */
}

/* ------------------------------------------------------------ properties */

static bool sb_mixer_modified(obs_properties_t *props, obs_property_t *prop, obs_data_t *settings);

struct sb_src_list_ctx {
	obs_property_t *list;
	const char *cur;
};

static bool sb_src_list_cb(void *data, obs_source_t *src)
{
	struct sb_src_list_ctx *ctx = data;
	const char *name = obs_source_get_name(src);
	if (name && *name)
		obs_property_list_add_string(ctx->list, name, name);
	return true;
}

static void sb_fill_source_list(obs_property_t *list, obs_data_t *settings, const char *key)
{
	obs_property_list_clear(list);
	obs_property_list_add_string(list, T_("None"), "");

	const char *cur = obs_data_get_string(settings, key);

	struct sb_src_list_ctx ctx = {.list = list, .cur = cur};
	obs_enum_sources(sb_src_list_cb, &ctx);

	if (cur && *cur) {
		/* keep a stale choice visible so it can be cleared */
		bool found = false;
		size_t n = obs_property_list_item_count(list);
		for (size_t i = 0; i < n; i++) {
			const char *v = obs_property_list_item_string(list, i);
			if (v && strcmp(v, cur) == 0) {
				found = true;
				break;
			}
		}
		if (!found)
			obs_property_list_add_string(list, cur, cur);
	}
}

static obs_properties_t *sb_props_mix_layer(int i)
{
	obs_properties_t *p = obs_properties_create();
	char k[SB_MIX_KEYBUF];

	sb_mix_key(i, "src", k, sizeof(k));
	obs_property_t *src = obs_properties_add_list(p, k, T_("MixSource"),
						      OBS_COMBO_TYPE_LIST,
						      OBS_COMBO_FORMAT_STRING);
	obs_property_set_modified_callback(src, sb_mixer_modified);

	sb_mix_key(i, "opacity", k, sizeof(k));
	obs_properties_add_float_slider(p, k, T_("MixOpacity"), 0.0, 1.0, 0.001);

	sb_mix_key(i, "mode", k, sizeof(k));
	obs_property_t *mode = obs_properties_add_list(p, k, T_("MixBlend"),
						       OBS_COMBO_TYPE_LIST,
						       OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(mode, T_("BlendNormal"), 0);
	obs_property_list_add_int(mode, T_("BlendAdd"), 1);
	obs_property_list_add_int(mode, T_("BlendScreenL"), 2);
	obs_property_list_add_int(mode, T_("BlendMultiply"), 3);
	obs_property_list_add_int(mode, T_("BlendLighten"), 4);

	sb_mix_key(i, "x", k, sizeof(k));
	obs_properties_add_float_slider(p, k, T_("MixX"), -1.0, 1.0, 0.001);

	sb_mix_key(i, "y", k, sizeof(k));
	obs_properties_add_float_slider(p, k, T_("MixY"), -1.0, 1.0, 0.001);

	sb_mix_key(i, "scale", k, sizeof(k));
	obs_properties_add_float_slider(p, k, T_("MixScale"), 0.05, 4.0, 0.001);

	sb_mix_key(i, "rot", k, sizeof(k));
	obs_properties_add_float_slider(p, k, T_("MixRot"), -1.0, 1.0, 0.001);

	return p;
}

static obs_properties_t *sb_props_mixer(void)
{
	obs_properties_t *p = obs_properties_create();
	obs_properties_add_bool(p, S_MIX_ON, T_("MixEnable"));

	for (int i = 0; i < SB_MIX_LAYERS; i++) {
		char id[32], label[64];
		snprintf(id, sizeof(id), "mixlayer%d", i + 1);
		snprintf(label, sizeof(label), T_("MixLayerN"), i + 1);
		obs_properties_add_group(p, id, label, OBS_GROUP_NORMAL, sb_props_mix_layer(i));
	}

	return p;
}

/* Refresh the source lists when the properties are opened. */
static bool sb_mixer_modified(obs_properties_t *props, obs_property_t *prop,
			      obs_data_t *settings)
{
	UNUSED_PARAMETER(prop);
	for (int i = 0; i < SB_MIX_LAYERS; i++) {
		char k[SB_MIX_KEYBUF], id[32];
		sb_mix_key(i, "src", k, sizeof(k));
		snprintf(id, sizeof(id), "mixlayer%d", i + 1);

		obs_property_t *grp = obs_properties_get(props, id);
		obs_properties_t *g = grp ? obs_property_group_content(grp) : NULL;
		if (g) {
			obs_property_t *list = obs_properties_get(g, k);
			if (list)
				sb_fill_source_list(list, settings, k);
		}
	}
	return true;
}

static void sb_tap_list(obs_property_t *p)
{
	obs_property_list_add_int(p, T_("TapMixer"), TAP_MIXER);
	obs_property_list_add_int(p, T_("TapCameraTransform"), TAP_CAM_TRANSFORM);
	obs_property_list_add_int(p, T_("TapColor"), TAP_COLOR);
	obs_property_list_add_int(p, T_("TapEdge"), TAP_EDGE);
	obs_property_list_add_int(p, T_("TapKaleido"), TAP_KALEIDO);
	obs_property_list_add_int(p, T_("TapDroste"), TAP_DROSTE);
	obs_property_list_add_int(p, T_("TapMaster"), TAP_MASTER);
	obs_property_list_add_int(p, T_("TapTunnel"), TAP_TUNNEL);
}

static void sb_mode_list(obs_property_t *p)
{
	obs_property_list_add_int(p, T_("BlendAdd"), 0);
	obs_property_list_add_int(p, T_("BlendScreen"), 1);
	obs_property_list_add_int(p, T_("BlendMultiply"), 2);
	obs_property_list_add_int(p, T_("BlendDifference"), 3);
	obs_property_list_add_int(p, T_("BlendOverlay"), 4);
	obs_property_list_add_int(p, T_("BlendMax"), 5);
	obs_property_list_add_int(p, T_("BlendMin"), 6);
	obs_property_list_add_int(p, T_("BlendSubtract"), 7);
}

static obs_properties_t *sb_props_transform(bool master);
static obs_properties_t *sb_props_color(void);
static obs_properties_t *sb_props_edge(void);
static obs_properties_t *sb_props_kaleido(void);
static obs_properties_t *sb_props_droste(void);
static obs_properties_t *sb_props_tunnel(void);
static obs_properties_t *sb_props_swirl(void);
static obs_properties_t *sb_props_feedback(void);
static obs_properties_t *sb_props_presets(void);
static obs_properties_t *sb_props_modulation(void);
static obs_properties_t *sb_props_performance(void);

static obs_properties_t *sb_properties(void *data)
{
	obs_properties_t *props = obs_properties_create();

	obs_properties_add_group(props, "grp_mix", T_("Mixer"), OBS_GROUP_NORMAL,
				 sb_props_mixer());

	obs_properties_add_group(props, "grp_ct", T_("CameraTransform"), OBS_GROUP_NORMAL,
				 sb_props_transform(false));
	obs_properties_add_group(props, "grp_color", T_("Color"), OBS_GROUP_NORMAL,
				 sb_props_color());
	obs_properties_add_group(props, "grp_edge", T_("EdgeDetect"), OBS_GROUP_NORMAL,
				 sb_props_edge());
	obs_properties_add_group(props, "grp_k", T_("Kaleidoscope"), OBS_GROUP_NORMAL,
				 sb_props_kaleido());
	obs_properties_add_group(props, "grp_d", T_("Droste"), OBS_GROUP_NORMAL,
				 sb_props_droste());
	obs_properties_add_group(props, "grp_mt", T_("MasterTransform"), OBS_GROUP_NORMAL,
				 sb_props_transform(true));
	obs_properties_add_group(props, "grp_tn", T_("Tunnel"), OBS_GROUP_NORMAL,
				 sb_props_tunnel());
	obs_properties_add_group(props, "grp_sw", T_("Swirl"), OBS_GROUP_NORMAL,
				 sb_props_swirl());
	obs_properties_add_group(props, "grp_fb", T_("Feedback"), OBS_GROUP_NORMAL,
				 sb_props_feedback());
	obs_properties_add_group(props, "grp_presets", T_("Presets"), OBS_GROUP_NORMAL,
				 sb_props_presets());
	obs_properties_add_group(props, "grp_mod", T_("Modulation"), OBS_GROUP_NORMAL,
				 sb_props_modulation());
	obs_properties_add_group(props, "grp_perf", T_("Performance"), OBS_GROUP_NORMAL,
				 sb_props_performance());

	/* populate the layer source lists now that the source tree exists */
	for (int i = 0; i < SB_MIX_LAYERS; i++) {
		char k[SB_MIX_KEYBUF], id[32];
		sb_mix_key(i, "src", k, sizeof(k));
		snprintf(id, sizeof(id), "mixlayer%d", i + 1);
		obs_property_t *grp = obs_properties_get(props, id);
		obs_properties_t *g = grp ? obs_property_group_content(grp) : NULL;
		if (g) {
			obs_property_t *list = obs_properties_get(g, k);
			if (list) {
				obs_data_t *st = data ? obs_source_get_settings(data) : NULL;
				if (st) {
					sb_fill_source_list(list, st, k);
					obs_data_release(st);
				}
			}
		}
	}

	return props;
}

static obs_properties_t *sb_props_transform(bool master)
{
	obs_properties_t *p = obs_properties_create();
	const char *z = master ? S_MT_ZOOM : S_CT_ZOOM;
	const char *a = master ? S_MT_ANGLE : S_CT_ANGLE;
	const char *tx = master ? S_MT_TX : S_CT_TX;
	const char *ty = master ? S_MT_TY : S_CT_TY;
	const char *tix = master ? S_MT_TILTX : S_CT_TILTX;
	const char *tiy = master ? S_MT_TILTY : S_CT_TILTY;
	const char *bd = master ? S_MT_BEND : S_CT_BEND;
	const char *wb = master ? S_MT_WOBBLE : S_CT_WOBBLE;
	const char *wp = master ? S_MT_WARP : S_CT_WARP;
	const char *rg = master ? S_MT_RGB : S_CT_RGB;
	const char *sp = master ? S_MT_SPIN : S_CT_SPIN;

	obs_properties_add_float_slider(p, z, T_("Zoom"), 0.05, 5.0, 0.001);
	obs_properties_add_float_slider(p, a, T_("Angle"), -1.0, 1.0, 0.001);
	obs_properties_add_float_slider(p, tx, T_("MoveX"), -1.0, 1.0, 0.001);
	obs_properties_add_float_slider(p, ty, T_("MoveY"), -1.0, 1.0, 0.001);
	obs_properties_add_float_slider(p, tix, T_("TiltX"), -1.0, 1.0, 0.001);
	obs_properties_add_float_slider(p, tiy, T_("TiltY"), -1.0, 1.0, 0.001);
	obs_properties_add_float_slider(p, bd, T_("Bend"), -3.0, 3.0, 0.001);
	obs_properties_add_float_slider(p, wb, T_("Wobble"), 0.0, 1.0, 0.001);
	obs_properties_add_float_slider(p, wp, T_("Distort"), 0.0, 1.0, 0.001);
	obs_properties_add_float_slider(p, rg, T_("RGBShift"), 0.0, 0.1, 0.001);
	obs_properties_add_float_slider(p, sp, T_("Spin"), -1.0, 1.0, 0.001);
	return p;
}

static obs_properties_t *sb_props_color(void)
{
	obs_properties_t *p = obs_properties_create();
	obs_properties_add_float_slider(p, S_C_AMT, T_("Amount"), 0.0, 1.0, 0.001);
	obs_properties_add_float_slider(p, S_BRIT, T_("Brightness"), 0.0, 2.0, 0.001);
	obs_properties_add_float_slider(p, S_HUE, T_("Hue"), 0.0, 1.0, 0.001);
	obs_properties_add_float_slider(p, S_NEG, T_("Negative"), 0.0, 1.0, 0.001);
	obs_properties_add_float_slider(p, S_GLOW, T_("Glow"), 0.0, 2.0, 0.001);
	obs_properties_add_float_slider(p, S_CONTRAST, T_("Contrast"), 0.0, 2.0, 0.001);
	obs_properties_add_float_slider(p, S_VIBRANCE, T_("Saturation"), 0.0, 2.0, 0.001);
	return p;
}

static obs_properties_t *sb_props_edge(void)
{
	obs_properties_t *p = obs_properties_create();
	obs_properties_add_float_slider(p, S_E_AMT, T_("Amount"), 0.0, 1.0, 0.001);
	obs_properties_add_float_slider(p, S_E_THRESH, T_("Threshold"), -0.05, 0.2, 0.001);
	obs_properties_add_float_slider(p, S_E_HUE, T_("Hue"), 0.0, 1.0, 0.001);
	obs_properties_add_float_slider(p, S_E_SAT, T_("Saturation"), 0.0, 1.0, 0.001);
	obs_properties_add_float_slider(p, S_E_BRIT, T_("Brightness"), 0.0, 2.0, 0.001);
	return p;
}

static obs_properties_t *sb_props_kaleido(void)
{
	obs_properties_t *p = obs_properties_create();
	obs_properties_add_int_slider(p, S_K_AXIS, T_("Axis"), 1, 25, 1);
	obs_properties_add_float_slider(p, S_K_AMT, T_("Amount"), 0.0, 1.0, 0.001);
	obs_properties_add_float_slider(p, S_K_ZOOM, T_("KZoom"), 0.25, 5.0, 0.001);
	return p;
}

static obs_properties_t *sb_props_droste(void)
{
	obs_properties_t *p = obs_properties_create();
	obs_properties_add_float_slider(p, S_D_MIX, T_("Strength"), 0.0, 1.0, 0.001);
	obs_properties_add_float_slider(p, S_D_SCALE, T_("Scale"), 1.5, 12.0, 0.001);
	obs_properties_add_float_slider(p, S_D_ARMS, T_("Spiral"), 0.0, 8.0, 0.001);
	obs_properties_add_float_slider(p, S_D_SPEED, T_("Speed"), -2.0, 2.0, 0.001);
	obs_properties_add_float_slider(p, S_D_RING, T_("RingSize"), 0.1, 1.0, 0.001);
	obs_properties_add_float_slider(p, S_D_ZOOM, T_("Zoom"), 0.0, 1.0, 0.001);
	obs_properties_add_float_slider(p, S_D_LOOK_X, T_("LookX"), -0.6, 0.6, 0.001);
	obs_properties_add_float_slider(p, S_D_LOOK_Y, T_("LookY"), -0.6, 0.6, 0.001);
	return p;
}

static obs_properties_t *sb_props_tunnel(void)
{
	obs_properties_t *p = obs_properties_create();
	obs_properties_add_float_slider(p, S_TN_MIX, T_("Strength"), 0.0, 1.0, 0.001);
	obs_properties_add_float_slider(p, S_TN_SHAPE, T_("Shape"), 0.0, 1.0, 0.001);
	obs_properties_add_float_slider(p, S_TN_FOV, T_("Fisheye"), 0.0, 1.0, 0.001);
	obs_properties_add_float_slider(p, S_TN_SPEED, T_("Speed"), -2.0, 2.0, 0.001);
	obs_properties_add_float_slider(p, S_TN_CURVE, T_("Curve"), 0.0, 2.0, 0.001);
	obs_properties_add_float_slider(p, S_TN_TWIST, T_("Vortex"), -2.0, 2.0, 0.001);
	obs_properties_add_float_slider(p, S_TN_FLUX, T_("Flux"), 0.0, 1.0, 0.001);
	obs_properties_add_float_slider(p, S_TN_ROLL, T_("Roll"), -1.0, 1.0, 0.001);
	obs_properties_add_float_slider(p, S_TN_LOOK_X, T_("LookX"), -1.0, 1.0, 0.001);
	obs_properties_add_float_slider(p, S_TN_LOOK_Y, T_("LookY"), -1.0, 1.0, 0.001);
	obs_properties_add_float_slider(p, S_TN_HSTR, T_("RainbowStrength"), 0.0, 1.0, 0.001);
	obs_properties_add_float_slider(p, S_TN_HPOS, T_("RainbowPosition"), 0.0, 1.0, 0.001);
	obs_properties_add_float_slider(p, S_TN_WSTR, T_("WaveStrength"), 0.0, 1.0, 0.001);
	obs_properties_add_float_slider(p, S_TN_WPOS, T_("WavePosition"), 0.0, 1.0, 0.001);
	obs_properties_add_float_slider(p, S_TN_FOGD, T_("FogDistance"), 0.0, 1.0, 0.001);
	obs_properties_add_float_slider(p, S_TN_FOGH, T_("FogHue"), 0.0, 1.0, 0.001);
	obs_properties_add_float_slider(p, S_TN_FOGS, T_("FogSaturation"), 0.0, 1.0, 0.001);
	obs_properties_add_float_slider(p, S_TN_FOGV, T_("FogBrightness"), 0.0, 1.0, 0.001);
	return p;
}

static obs_properties_t *sb_props_swirl(void)
{
	obs_properties_t *p = obs_properties_create();
	obs_properties_add_float_slider(p, S_SW_STR, T_("Strength"), 0.0, 1.0, 0.001);
	obs_properties_add_float_slider(p, S_SW_WIDE, T_("Wideness"), 0.0, 2.0, 0.001);
	obs_properties_add_float_slider(p, S_SW_ACTIVITY, T_("Activity"), 0.0, 4.0, 0.001);
	obs_properties_add_float_slider(p, S_SW_SPEED, T_("Speed"), -2.0, 2.0, 0.001);
	obs_properties_add_float_slider(p, S_SW_ROLL, T_("Roll"), -1.0, 1.0, 0.001);
	obs_properties_add_float_slider(p, S_SW_LOOK_X, T_("LookX"), -1.0, 1.0, 0.001);
	obs_properties_add_float_slider(p, S_SW_LOOK_Y, T_("LookY"), -1.0, 1.0, 0.001);
	obs_properties_add_float_slider(p, S_SW_FOGD, T_("FogDistance"), 0.0, 1.0, 0.001);
	obs_properties_add_float_slider(p, S_SW_FOGF, T_("FogSoftness"), 0.0, 1.0, 0.001);
	obs_properties_add_float_slider(p, S_SW_FOGH, T_("FogHue"), 0.0, 1.0, 0.001);
	obs_properties_add_float_slider(p, S_SW_FOGS, T_("FogSaturation"), 0.0, 1.0, 0.001);
	obs_properties_add_float_slider(p, S_SW_FOGV, T_("FogBrightness"), 0.0, 1.0, 0.001);
	return p;
}

static obs_properties_t *sb_props_feedback(void)
{
	obs_properties_t *p = obs_properties_create();
	obs_properties_add_bool(p, S_FB_ON, T_("EnableFeedback"));

	obs_property_t *tap = obs_properties_add_list(p, S_FB_TAP, T_("TapPoint"),
						     OBS_COMBO_TYPE_LIST,
						     OBS_COMBO_FORMAT_INT);
	sb_tap_list(tap);

	obs_property_t *mode = obs_properties_add_list(p, S_FB_MODE, T_("BlendMode"),
						      OBS_COMBO_TYPE_LIST,
						      OBS_COMBO_FORMAT_INT);
	sb_mode_list(mode);

	obs_properties_add_float_slider(p, S_FB_ALPHA, T_("FeedbackAmount"), 0.0, 1.0, 0.001);
	obs_properties_add_int_slider(p, S_FB_DELAY, T_("FrameDelay"), 1, 32, 1);
	return p;
}

static obs_properties_t *sb_props_lfo(int i)
{
	obs_properties_t *p = obs_properties_create();

	char k[SB_LFO_KEYBUF];
	char label[64];

	sb_lfo_key(i, "on", k, sizeof(k));
	snprintf(label, sizeof(label), T_("LfoNEnable"), i + 1);
	obs_properties_add_bool(p, k, label);

	sb_lfo_key(i, "target", k, sizeof(k));
	obs_property_t *tgt = obs_properties_add_list(p, k, T_("LfoTarget"),
						      OBS_COMBO_TYPE_LIST,
						      OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(tgt, T_("LfoTargetNone"), -1);
	for (int t = 0; t < SB_MOD_TARGET_COUNT; t++)
		obs_property_list_add_int(tgt, sb_mod_targets[t].key, t);

	sb_lfo_key(i, "wave", k, sizeof(k));
	obs_property_t *wv = obs_properties_add_list(p, k, T_("LfoWaveform"),
						     OBS_COMBO_TYPE_LIST,
						     OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(wv, T_("WaveSine"), LFO_SINE);
	obs_property_list_add_int(wv, T_("WaveTriangle"), LFO_TRI);
	obs_property_list_add_int(wv, T_("WaveSaw"), LFO_SAW);
	obs_property_list_add_int(wv, T_("WaveSquare"), LFO_SQUARE);
	obs_property_list_add_int(wv, T_("WaveRandom"), LFO_RANDOM);

	sb_lfo_key(i, "rate", k, sizeof(k));
	obs_properties_add_float_slider(p, k, T_("LfoRate"), 0.01, 16.0, 0.01);

	sb_lfo_key(i, "depth", k, sizeof(k));
	obs_properties_add_float_slider(p, k, T_("LfoDepth"), 0.0, 1.0, 0.001);

	sb_lfo_key(i, "phase", k, sizeof(k));
	obs_properties_add_float_slider(p, k, T_("LfoPhase"), 0.0, 1.0, 0.001);

	return p;
}

static obs_properties_t *sb_props_modulation(void)
{
	obs_properties_t *p = obs_properties_create();

	obs_properties_add_float_slider(p, S_BPM, T_("Bpm"), 20.0, 300.0, 0.1);
	obs_properties_add_bool(p, S_LFO_SYNC, T_("SyncToTempo"));

	for (int i = 0; i < SB_LFOS; i++) {
		char id[32], label[64];
		snprintf(id, sizeof(id), "lfo%d", i + 1);
		snprintf(label, sizeof(label), T_("LfoN"), i + 1);
		obs_properties_add_group(p, id, label, OBS_GROUP_NORMAL, sb_props_lfo(i));
	}

	return p;
}

static obs_properties_t *sb_props_performance(void)
{
	obs_properties_t *p = obs_properties_create();
	obs_properties_add_float_slider(p, S_RENDER_SCALE, T_("RenderScale"), 0.25, 1.0, 0.05);
	obs_properties_add_int_slider(p, S_SW_STEPS, T_("SwirlSteps"), 8, 60, 1);
	return p;
}

static obs_properties_t *sb_props_presets(void)
{
	obs_properties_t *p = obs_properties_create();

	obs_property_t *slot = obs_properties_add_list(p, S_PRESET_SLOT, T_("PresetSlot"),
						      OBS_COMBO_TYPE_LIST,
						      OBS_COMBO_FORMAT_INT);
	for (int i = 1; i <= SB_PRESETS; i++) {
		char label[32];
		snprintf(label, sizeof(label), T_("PresetN"), i);
		obs_property_list_add_int(slot, label, i);
	}

	obs_properties_add_float_slider(p, S_PRESET_TIME, T_("TransitionTime"), 0.0, 20.0, 0.05);

	obs_properties_add_button2(p, "preset_save", T_("SavePreset"), sb_preset_save, NULL);
	obs_properties_add_button2(p, "preset_load", T_("LoadPreset"), sb_preset_load, NULL);
	obs_properties_add_button2(p, "preset_clear", T_("ClearPreset"), sb_preset_clear, NULL);
	obs_properties_add_text(p, S_BANK_PATH, T_("BankPath"), OBS_TEXT_DEFAULT);
	obs_properties_add_button2(p, "preset_export", T_("ExportBank"), sb_preset_export, NULL);
	obs_properties_add_button2(p, "preset_import", T_("ImportBank"), sb_preset_import, NULL);

	return p;
}

/* ---------------------------------------------------------------- module */

struct obs_source_info spacebeam_filter = {
	.id = "spacebeam_filter",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO,
	.get_name = sb_get_name,
	.create = sb_create,
	.destroy = sb_destroy,
	.update = sb_update,
	.get_defaults = sb_defaults,
	.get_properties = sb_properties,
	.video_tick = sb_tick,
	.video_render = sb_render,
};

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("spacebeam", "en-US")

bool obs_module_load(void)
{
	obs_register_source(&spacebeam_filter);
	blog(LOG_INFO, "[spacebeam] plugin loaded v0.4.0");
	return true;
}
