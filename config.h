static const char *font[] = { "Liberation Sans:size=12" };

/* DRW colors, so hex strings. */
static const char *color[2] = { "#000000", "#eaffff" };
static const char *color_f[2] = { "#000000", "#9eeeee" };

/* Hex numbers (RGB). */
/* #define BORDER_COLOR 0x52aaad */
#define BORDER_COLOR 0x000000
#define TITLE_COLOR 0xeaffff
#define TITLE_FOCUS_COLOR 0xffffea
#define CLOSE_BUTTON_COLOR 0xffaaaa
#define HIDE_BUTTON_COLOR 0x4488ee
/* Idk if it's really necessary, just put the same as the DRW. */
#define BAR_BACKGROUND 0xeaffff

/* Just comment out this def if you don't want background setting (so you can
 * set a background with other application). */
#define BACKGROUND 0xeaffff

#define TITLE_WIDTH 8
/* Pixels. You probably want just 1 as you have the title. */
#define BORDER_WIDTH 1

#define MODMASK (Mod4Mask)
#define FULLSCREEN_KEY XK_f

static const char *menu[] = { "dmenu_run", NULL };
static const char *volp[] = { "pamixer", "-i", "5", NULL };
static const char *volm[] = { "pamixer", "-d", "5", NULL };
static const char *brip[] = { "brightnessctl", "set", "+5%", NULL };
static const char *brim[] = { "brightnessctl", "set", "5%-", NULL };

static const char *terminal[] = { "alacritty", NULL };
static const char *acme[] = { "acme", NULL };
static const char *term9[] = { "9term", NULL };
static const char *browser[] = { "chromium", NULL };
static const char *mail[] = { "thunderbird", NULL };
static const char *fm[] = { "thunar", NULL };
static const char *nitrogen[] = { "nitrogen", NULL };
static const char *music[] = { "cantata", NULL };

/* The last does not matter. */
static MenuItem items[] = {
	{ "v+ ", volp, 3, 0 },
	{ "v- ", volm, 3, 0 },
	{ "b+ ", brip, 3, 0 },
	{ "b- ", brim, 3, 0 },
};
static MenuItem spawn_items[] = {
	{ "Terminal", terminal, 8, 0 },
	{ "acme", acme, 4, 0 },
	{ "9term", term9, 5, 0 },
	{ "Browser", browser, 7, 0 },
	{ "Mail", mail, 4, 0 },
	{ "FM", fm, 2, 0 },
	{ "Nitrogen", nitrogen, 8, 0 },
	{ "Music ", music, 6, 0 },
	{ "Menu ", menu, 5, 0 },
};
