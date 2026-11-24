/* Box drawing (U+2500 to U+257F), block elements (U+2580 to U+259F) and
   Powerline's four separators (U+E0B0 to U+E0B3) are drawn here to fill the
   cell exactly, as Ghostty (font/sprite/) and WezTerm (customglyph.rs) draw
   them, instead of taken from a font: a font's line stops short of the
   cell's edge or overshoots it, so borders break and blocks show stripes
   between cells (Claude Code's banner on Windows, Davy's screenshot of
   2026-11-23).  Lines are the main font's underline thick (light), twice
   that (heavy), or two light lines a light line apart (double), centred in
   the cell, so they meet the next cell's. */
#include <math.h>
#include "find.h"

static unsigned char *o;
static int w, h;

int sprite(unsigned c)
{
	return (c >= 0x2500 && c <= 0x259F) || (c >= 0xE0B0 && c <= 0xE0B3);
}

/* Sets [x0, x1) by [y0, y1), clipped to the cell, to v. */
static void rect(int x0, int y0, int x1, int y1, int v)
{
	for (int y = y0 < 0 ? 0 : y0; y < y1 && y < h; y++)
		for (int x = x0 < 0 ? 0 : x0; x < x1 && x < w; x++) o[y * w + x] = (unsigned char)v;
}

/* Where a line k thick starts across a side n long: centred */
static int lo(int n, int k) { return (n - k) / 2; }

/* Coverage of a pixel whose centre is d from a line's middle, the line k
   wide: 1 inside, 0 outside, the half pixel either side blended */
static void cover(int x, int y, double d, double k)
{
	double a = k / 2 + 0.5 - d;
	int v = a >= 1 ? 255 : a <= 0 ? 0 : (int)(a * 255 + 0.5);
	if (v > o[y * w + x]) o[y * w + x] = (unsigned char)v;
}

/* A line k wide from (x0, y0) to (x1, y1), anti-aliased */
static void seg(double x0, double y0, double x1, double y1, double k)
{
	double dx = x1 - x0, dy = y1 - y0, l2 = dx * dx + dy * dy;
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) {
			double px = x + 0.5 - x0, py = y + 0.5 - y0, s = (px * dx + py * dy) / l2;
			s = s < 0 ? 0 : s > 1 ? 1 : s;
			cover(x, y, hypot(px - s * dx, py - s * dy), k);
		}
}

/* The arms up, right, down, left of U+2500 to U+257F: 1 light, 2 heavy,
   3 double, 0 none; "." for the dashes, arcs and diagonals. */
static const char arms[128][5] = {
	"0101", "0202", "1010", "2020", ".", ".", ".", ".", ".", ".", ".", ".", "0110", "0210", "0120", "0220",
	"0011", "0012", "0021", "0022", "1100", "1200", "2100", "2200", "1001", "1002", "2001", "2002", "1110", "1210", "2110", "1120",
	"2120", "2210", "1220", "2220", "1011", "1012", "2011", "1021", "2021", "2012", "1022", "2022", "0111", "0112", "0211", "0212",
	"0121", "0122", "0221", "0222", "1101", "1102", "1201", "1202", "2101", "2102", "2201", "2202", "1111", "1112", "1211", "1212",
	"2111", "1121", "2121", "2112", "2211", "1122", "1221", "2212", "1222", "2122", "2221", "2222", ".", ".", ".", ".",
	"0303", "3030", "0310", "0130", "0330", "0013", "0031", "0033", "1300", "3100", "3300", "1003", "3001", "3003", "1310", "3130",
	"3330", "1013", "3031", "3033", "0313", "0131", "0333", "1303", "3101", "3303", "1313", "3131", "3333", ".", ".", ".",
	".", ".", ".", ".", "0001", "1000", "0100", "0010", "0002", "2000", "0200", "0020", "0201", "1020", "0102", "2010",
};

/* Arm i (0 up, 1 right, 2 down, 3 left) from the cell's edge to s along
   it and k wide across it, centred, to v */
static void arm(int i, int s, int k, int v)
{
	int across = lo(i & 1 ? h : w, k);
	if (i & 1) rect(i == 1 ? s : 0, across, i == 1 ? w : s, across + k, v);
	else rect(across, i == 2 ? s : 0, across + k, i == 2 ? h : s, v);
}

/* Every arm reaches across the lines it meets, so junctions are filled.  A
   double arm is a band three lines wide with its middle line cut out
   (to the cell's centre, so the cuts of two double arms join and leave
   the inner corner of ╔); the others are drawn after the cuts.  A single
   arm that ends at a double line meets its nearer line (╟). */
static void lines(const char *a, int t)
{
	int k[4], K[4];
	for (int i = 0; i < 4; i++) { k[i] = a[i] - '0'; K[i] = k[i] == 3 ? 3 * t : k[i] * t; }
	for (int pass = 0; pass < 3; pass++)
		for (int i = 0; i < 4; i++) {
			if (!k[i] || (k[i] == 3) != (pass < 2)) continue;
			int n = i & 1 ? w : h, far = i == 1 || i == 2;   /* far: the arm goes right or down */
			int p = K[(i + 1) % 4] > K[(i + 3) % 4] ? K[(i + 1) % 4] : K[(i + 3) % 4];   /* the lines across it */
			int m = pass == 1 ? t : p ? p : K[i];
			int s = far ? lo(n, m) : lo(n, m) + m;
			if (pass == 2 && p == 3 * t && !k[(i + 2) % 4]) s = far ? lo(n, t) + t : lo(n, t);
			arm(i, s, pass == 1 ? t : K[i], pass == 1 ? 0 : 255);
		}
}

/* n dashes along a light (k = t) or heavy line, gaps between them a third
   of each dash's share, half a gap at each end so they tile */
static void dashes(int vertical, int n, int k)
{
	int len = vertical ? h : w, across = lo(vertical ? w : h, k);
	for (int i = 0; i < n; i++) {
		int a = i * len / n, b = (i + 1) * len / n, g = (b - a) / 3 > 0 ? (b - a) / 3 : 1;
		if (vertical) rect(across, a + g / 2, across + k, b - (g - g / 2), 255);
		else rect(a + g / 2, across, b - (g - g / 2), across + k, 255);
	}
}

/* ╭ ╮ ╯ ╰: a quarter circle between the light lines' middles, with the
   lines straight on from it to the edges; sx, sy: the arc opens right
   (+1) or left, down (+1) or up */
static void arc(int sx, int sy, int t)
{
	double mx = lo(w, t) + t / 2.0, my = lo(h, t) + t / 2.0;
	double rx = sx > 0 ? w - mx : mx, ry = sy > 0 ? h - my : my, r = rx < ry ? rx : ry;
	double cx = mx + sx * r, cy = my + sy * r;
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
			if ((x + 0.5 - cx) * sx <= 0 && (y + 0.5 - cy) * sy <= 0)
				cover(x, y, fabs(hypot(x + 0.5 - cx, y + 0.5 - cy) - r), t);
	int ex = (int)cx, ey = (int)cy;
	if (sx > 0) rect(ex, lo(h, t), w, lo(h, t) + t, 255); else rect(0, lo(h, t), ex, lo(h, t) + t, 255);
	if (sy > 0) rect(lo(w, t), ey, lo(w, t) + t, h, 255); else rect(lo(w, t), 0, lo(w, t) + t, ey, 255);
}

void sprite_draw(unsigned c, int cw, int ch, int t, unsigned char *out)
{
	o = out;
	w = cw;
	h = ch;
	rect(0, 0, w, h, 0);
	if (c >= 0xE0B0) {   /* Powerline: a solid or outline triangle pointing right (B0, B1) or left */
		int left = c >= 0xE0B2;
		double tip = left ? 0 : w, back = left ? w : 0;
		if (c & 1) {
			seg(back, 0, tip, h / 2.0, t);
			seg(tip, h / 2.0, back, h, t);
			return;
		}
		for (int y = 0; y < h; y++) {   /* the edge's x at the row's middle, partial pixels blended */
			double e = w * (1 - fabs(y + 0.5 - h / 2.0) / (h / 2.0));
			for (int x = 0; x < w; x++) {
				double a = e - x;
				o[y * w + (left ? w - 1 - x : x)] = (unsigned char)(a >= 1 ? 255 : a <= 0 ? 0 : (int)(a * 255 + 0.5));
			}
		}
		return;
	}
	if (c >= 0x2580) {   /* blocks: halves, eighths, shades, quadrants */
		int mx = (w + 1) / 2, my = h / 2;   /* as ▌'s and ▄'s eighths round, so halves and quadrants meet */
		if (c == 0x2580) rect(0, 0, w, my, 255);
		else if (c <= 0x2588) rect(0, h - (h * (int)(c - 0x2580) + 4) / 8, w, h, 255);
		else if (c <= 0x258F) rect(0, 0, (w * (int)(0x2590 - c) + 4) / 8, h, 255);
		else if (c == 0x2590) rect(mx, 0, w, h, 255);
		else if (c <= 0x2593) rect(0, 0, w, h, 64 * (int)(c - 0x2590));   /* ░ ▒ ▓: a quarter, half, three quarters */
		else if (c == 0x2594) rect(0, 0, w, (h + 4) / 8, 255);
		else if (c == 0x2595) rect(w - (w + 4) / 8, 0, w, h, 255);
		else {   /* quadrants: bits upper left, upper right, lower left, lower right */
			static const unsigned char q[] = { 4, 8, 1, 13, 9, 7, 11, 2, 6, 14 };
			unsigned b = q[c - 0x2596];
			if (b & 1) rect(0, 0, mx, my, 255);
			if (b & 2) rect(mx, 0, w, my, 255);
			if (b & 4) rect(0, my, mx, h, 255);
			if (b & 8) rect(mx, my, w, h, 255);
		}
		return;
	}
	const char *a = arms[c - 0x2500];
	if (a[0] != '.') { lines(a, t); return; }
	switch (c) {
	case 0x2504: case 0x2505: case 0x2506: case 0x2507: dashes(c >= 0x2506, 3, c & 1 ? 2 * t : t); return;
	case 0x2508: case 0x2509: case 0x250A: case 0x250B: dashes(c >= 0x250A, 4, c & 1 ? 2 * t : t); return;
	case 0x254C: case 0x254D: case 0x254E: case 0x254F: dashes(c >= 0x254E, 2, c & 1 ? 2 * t : t); return;
	case 0x256D: arc(1, 1, t); return;
	case 0x256E: arc(-1, 1, t); return;
	case 0x256F: arc(-1, -1, t); return;
	case 0x2570: arc(1, -1, t); return;
	}
	if (c != 0x2572) seg(w, 0, 0, h, t);   /* ╱ ╳ */
	if (c != 0x2571) seg(0, 0, w, h, t);   /* ╲ ╳ */
}
