# test_org.c with main() starting by two kinds of reference that do not run the code under test on the same amount of text:
# A (scale): the same plain text at a quarter of the size; B (outside): a byte loop in the test over the same text
# prints per round: A1 line64K line16K / A3 line64K line4K / A2 frame100k frame25k / B1 line64K ref / B2 frame100k ref ; then exits
import sys
src, out = sys.argv[1], sys.argv[2]
s = open(src).read()
old = 'int main(void)\n{\n'
assert s.count(old) == 1
new = r'''static double ref_ms(const char *s, size_t m)
{
	double t0 = cpu_ms();
	volatile size_t k = 0;
	for (int r = 0; r < 16; r++) for (size_t i = 0; i < m; i++) k += s[i] == '\n';
	return cpu_ms() - t0;
}
int main(int argc, char **argv)
{
	if (argc > 1) {
		static char ln[65536], pl[100000 * 12];
		static unsigned char fp[50 * 256];
		size_t m = 0;
		while (m + 5 < sizeof ln) memcpy(ln + m, "word ", 5), m += 5;
		ln[m++] = '\n';
		for (size_t i = 0; i < sizeof pl; i++) pl[i] = (i + 1) % 12 ? 'x' : '\n';
		for (int r = atoi(argv[1]); r > 0; r--) {
			double a = colour_line(ln, m, m, 0), b = colour_line(ln + m - m / 4, m / 4, m / 4, 0);
			fprintf(stderr, "A 1 0 %.4f %.4f\n", a, b);
			b = colour_line(ln + m - m / 16, m / 16, m / 16, 0);
			fprintf(stderr, "A 3 0 %.4f %.4f\n", a, b);
			a = frame(pl, sizeof pl, 100000 - 50, 12, 0, 0, fp); b = frame(pl, sizeof pl / 4, 25000 - 50, 12, 0, 0, fp);
			fprintf(stderr, "A 2 0 %.4f %.4f\n", a, b);
			a = colour_line(ln, m, m, 0); b = ref_ms(ln, m);
			fprintf(stderr, "B 1 0 %.4f %.4f\n", a, b);
			a = frame(pl, sizeof pl, 100000 - 50, 12, 0, 0, fp); b = ref_ms(pl, sizeof pl);
			fprintf(stderr, "B 2 0 %.4f %.4f\n", a, b);
		}
		return 0;
	}
'''
open(out, 'w').write(s.replace(old, new))
