// gcc main.c -o hello -lncursesw -lm && ./hello [cell aspect, e.g. 2.9]

#include <locale.h>
#include <math.h>
#include <ncurses.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

static void quit(int sig) {
	(void)sig;
	endwin();
	exit(0);
}

// character cell height / width; circles are drawn aspect times wider than tall
static double aspect = 2.0;

enum { OCEAN = 1, LAND, FIRE, WHITE, SKY };

// story timeline, seconds
#define ORBIT 0.5
#define DATE_IN 2.5
#define DATE_OUT 12.5
#define RETURN 13.0
#define LOOP 16.0

#define TILT 0.4

#define MAP_W 64
#define MAP_H 24

static const char *MAP[MAP_H] = {
	"................................................................",
	"................#############.....##.....##......##.............",
	"...#################..#######......#############################",
	"...#############..###..###..##...###############################",
	"....############..####.........#.##.#####################...#...",
	"..........#############........#########################.#......",
	"..........##########..........###.#######.#############..#......",
	"...........########...........####....################.##.......",
	"............###..#...........#########.###.###########..........",
	".............####.##.........#############..###..###.#..........",
	"................######.......#############...#...###..#.........",
	"..................######.......##########........##.##..........",
	"..................########........######..........#.##.####.....",
	"..................########........#######..........##.####......",
	"...................######.........#####.#............#####......",
	"...................#####..........####..............########....",
	"...................####............##...............##..###...#.",
	"...................###...................................#....#.",
	"...................##...........................................",
	"....................#...........................................",
	".....................#..........................................",
	"....................##.......################################...",
	"################################################################",
	"################################################################"
};

static const char *RAMP = ".:-=+*#%";

static const char *DATE = "12.04.1999";

static const char *glyph(char c, int row) {
	static const char *FONT[][3] = { ['0' - '.'] = { "┏━┓", "┃ ┃", "┗━┛" },
									 ['1' - '.'] = { "╺┓ ", " ┃ ", "╺┻╸" },
									 ['2' - '.'] = { "┏━┓", "┏━┛", "┗━╸" },
									 ['4' - '.'] = { "╻ ╻", "┗━┫", "  ╹" },
									 ['9' - '.'] = { "┏━┓", "┗━┫", "┗━┛" },
									 ['.' - '.'] = { " ", " ", "╹" } };
	return FONT[c - '.'][row];
}

static unsigned hash(int x, int y) {
	unsigned h = x * 374761393u + y * 668265263u;
	h = (h ^ h >> 13) * 1274126177u;
	return h ^ h >> 16;
}

static void put(int y, int x, const char *s, attr_t attr) {
	if (y < 0 || x < 0 || y >= LINES || x >= COLS)
		return;
	attrset(attr);
	mvaddstr(y, x, s);
}

static int is_land(int x, int y) {
	y = y < 0 ? 0 : y >= MAP_H ? MAP_H - 1 : y;
	return MAP[y][(x % MAP_W + MAP_W) % MAP_W] == '#';
}

// bilinear land coverage, so coastlines stay smooth at any globe size
static double land(double fx, double fy) {
	int x = floor(fx), y = floor(fy);
	double ax = fx - x, ay = fy - y;
	return (is_land(x, y) * (1 - ax) + is_land(x + 1, y) * ax) * (1 - ay) +
		   (is_land(x, y + 1) * (1 - ax) + is_land(x + 1, y + 1) * ax) * ay;
}

// phase: 0..2π over one loop, so every motion repeats seamlessly
static void draw_stars(double phase) {
	for (int y = 0; y < LINES; y++)
		for (int x = 0; x < COLS; x++) {
			unsigned h = hash(x, y);
			if (h % 70)
				continue;
			double b = sin(phase * (1 + h % 4) + h % 100);
			put(y, x, b > 0.9 ? "+" : "·",
				COLOR_PAIR(WHITE) | (b > 0 ? A_BOLD : A_DIM));
		}
}

static void draw_globe(int cy, int cx, int r, double spin) {
	for (int y = -r - 1; y <= r + 1; y++)
		for (int x = -aspect * r - 2; x <= aspect * r + 2; x++) {
			double u = x / (aspect * r), v = y / (double)r,
				   d = sqrt(u * u + v * v);
			double light = -0.8 * u - 0.3 * v;

			if (d > 1) {
				if (d < 1 + 1.0 / r)
					put(cy + y, cx + x, "·",
						COLOR_PAIR(SKY) | (light > 0 ? A_BOLD : A_DIM));
				continue;
			}

			double z = sqrt(1 - d * d);
			double lat = asin(z * sin(TILT) - v * cos(TILT));
			double lon = atan2(u, z * cos(TILT) + v * sin(TILT)) - spin;
			double fx = (lon / (2 * M_PI) + 0.5) * MAP_W,
				   fy = (0.5 - lat / M_PI) * MAP_H;
			light += 0.5 * z;

			if (land(fx - 0.5, fy - 0.5) > 0.5) {
				if (light > 0) {
					char c[2] = { RAMP[(int)fmin(light * 8, 7)] };
					put(cy + y, cx + x, c,
						COLOR_PAIR(LAND) | (light > 0.6 ? A_BOLD : 0));
				} else if (hash(floor(fx * 6), floor(fy * 6)) % 5 == 0)
					put(cy + y, cx + x, "·",
						COLOR_PAIR(FIRE) | A_BOLD); // city lights
				else
					put(cy + y, cx + x, light > -0.3 ? "." : " ",
						COLOR_PAIR(LAND) | A_DIM);
			} else {
				if (light > 0.995)
					put(cy + y, cx + x, "∙",
						COLOR_PAIR(WHITE) | A_BOLD); // sun glint
				else
					put(cy + y, cx + x,
						light > 0.45	? "≈"
						: light > 0.1	? "~"
						: light > -0.25 ? "."
										: " ",
						COLOR_PAIR(OCEAN) | (light > 0.45  ? A_BOLD
											 : light > 0.1 ? 0
														   : A_DIM));
			}
		}
}

// lit by the same sun as the Earth, so its phase matches the terminator
static void draw_moon(int cy, int cx, int r) {
	for (int y = -r; y <= r; y++)
		for (int x = -aspect * r; x <= aspect * r; x++) {
			double u = x / (aspect * r), v = y / (double)r, d = u * u + v * v;
			if (d > 1)
				continue;
			double light = -0.8 * u - 0.3 * v + 0.5 * sqrt(1 - d);
			if (light <= 0) {
				put(cy + y, cx + x, " ", 0);
				continue;
			}
			int shade = fmin(light * 8, 7) - (hash(x, y) % 4 == 0); // craters
			char c[2] = { RAMP[shade < 0 ? 0 : shade] };
			put(cy + y, cx + x, c,
				COLOR_PAIR(WHITE) | (light > 0.6 ? A_BOLD : 0));
		}
}

// one orbit point per moment of flight; hidden while behind the globe
static void draw_orbit(int cy, int cx, int r, double t) {
	for (int k = 150; k >= 0; k--) {
		double s = t - k * 0.02;
		if (s < ORBIT || s > RETURN)
			continue;

		double th = (s - ORBIT) * 4 * M_PI / (RETURN - ORBIT);
		int x = cx + lround(1.45 * aspect * r * cos(th));
		int y = cy + lround(r * (0.35 * sin(th) - 0.3 * cos(th)));
		double u = (x - cx) / (aspect * r), v = (y - cy) / (double)r;
		if (sin(th) < 0 && u * u + v * v <= 1)
			continue;

		if (k == 0)
			put(y, x, "●", COLOR_PAIR(FIRE) | A_BOLD);
		else
			put(y, x, "∙", COLOR_PAIR(WHITE) | (k < 80 ? A_BOLD : 0));
	}
}

static void draw_date(int y, int cx, double t) {
	int x = cx - 35 / 2;
	for (int i = 0; DATE[i]; i++) {
		double shown = t - (DATE_IN + i * 0.2),
			   hidden = t - (DATE_OUT + i * 0.1);
		int w = DATE[i] == '.' ? 1 : 3;
		if (shown >= 0 && hidden < 0)
			for (int row = 0; row < 3; row++)
				put(y + row, x - 1, " ", 0),
					put(y + row, x, glyph(DATE[i], row),
						COLOR_PAIR(shown < 0.3 ? FIRE : SKY) | A_BOLD);
		x += w + 1;
	}
}

// GitHub dark theme, as nearest xterm-256 colors; the terminal's own palette
// otherwise
static void init_colors(void) {
	start_color();
	int rich = COLORS >= 256, bg = rich ? 234 : -1;
	if (!rich)
		use_default_colors();
	init_pair(OCEAN, rich ? 111 : COLOR_BLUE, bg);
	init_pair(LAND, rich ? 157 : COLOR_GREEN, bg);
	init_pair(FIRE, rich ? 215 : COLOR_YELLOW, bg);
	init_pair(WHITE, rich ? 255 : COLOR_WHITE, bg);
	init_pair(SKY, rich ? 26 : COLOR_CYAN, bg);
	bkgd(COLOR_PAIR(WHITE));
}

// ask the terminal for its cell size in pixels; not every terminal reports it
static void measure_aspect(void) {
	struct winsize ws;
	if (!ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) && ws.ws_xpixel && ws.ws_ypixel)
		aspect = (double)ws.ws_ypixel * ws.ws_col / (ws.ws_xpixel * ws.ws_row);
}

int main(int argc, char **argv) {
	setlocale(LC_ALL, "");
	signal(SIGINT, quit);
	initscr();
	cbreak();
	noecho();
	curs_set(0);
	timeout(20);
	init_colors();

	if (argc < 2)
		measure_aspect();
	else
		aspect = atof(argv[1]);
	if (!(aspect >= 1 && aspect <= 4))
		aspect = 2.0;

	struct timespec start, ts;
	clock_gettime(CLOCK_MONOTONIC, &start);

	for (int ch; (ch = getch()) == ERR || ch == KEY_RESIZE;) {
		if (ch == KEY_RESIZE && argc < 2)
			measure_aspect();
		clock_gettime(CLOCK_MONOTONIC, &ts);
		double now =
			ts.tv_sec - start.tv_sec + (ts.tv_nsec - start.tv_nsec) / 1e9;
		double t = fmod(now, LOOP), phase = 2 * M_PI * t / LOOP;

		int r = (LINES - 8) / 2;
		if (r * 3 * aspect > COLS - 4)
			r = (COLS - 4) / (3 * aspect);
		if (r < 4)
			r = 4;
		int top = (LINES - (2 * r + 6)) / 2;
		int cy = top + r + 1, cx = COLS / 2;

		erase();
		draw_stars(phase);
		draw_globe(cy, cx, r, phase);
		int m = r < 8 ? 2 : r / 4;
		draw_moon(cy - r + m + lround(sin(phase * 2)),
				  fmin(cx + aspect * (r + m) + 6, COLS - aspect * m - 3), m);
		draw_orbit(cy, cx, r, t);
		draw_date(cy + r + 2, cx, t);
		refresh();
	}
	endwin();
}
