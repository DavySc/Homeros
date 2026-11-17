/* The font tests run on a pinned font set, not on whatever the machine has:
   the directories of the font packages CI installs (Ubuntu's
   fonts-jetbrains-mono, fonts-noto-core, fonts-noto-cjk,
   fonts-noto-color-emoji, fonts-dejavu-core), or the colon-separated
   directories in $HOMEROS_FONTS.  fontconfig reads a config written to
   build/ that lists only those and keeps its cache there.  Tests run from
   the repository's root. */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void font_env(void)
{
	const char *d = getenv("HOMEROS_FONTS");
	if (!d) d = "/usr/share/fonts/truetype/jetbrains-mono:/usr/share/fonts/truetype/noto:"
	            "/usr/share/fonts/opentype/noto:/usr/share/fonts/truetype/dejavu";
	static char cwd[PATH_MAX], conf[PATH_MAX + 32];
	if (!getcwd(cwd, sizeof cwd)) { perror("font tests"); exit(1); }
	snprintf(conf, sizeof conf, "%s/build/fonts.conf", cwd);
	FILE *f = fopen(conf, "w");
	if (!f) { perror(conf); exit(1); }
	fprintf(f, "<?xml version=\"1.0\"?><fontconfig>");
	for (; *d; d += strcspn(d, ":"), d += *d == ':')
		fprintf(f, "<dir>%.*s</dir>", (int)strcspn(d, ":"), d);
	fprintf(f, "<cachedir>%s/build/fontconfig</cachedir></fontconfig>\n", cwd);
	fclose(f);
	setenv("FONTCONFIG_FILE", conf, 1);
}

#define FAMILY "JetBrains Mono"
