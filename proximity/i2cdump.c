/* i2cdump: freestanding static aarch64: i2cdump <bus> <addr hex>  -> hex dump of registers 0x00..0x7f (write reg, read 1 byte) */
typedef unsigned char u8;
static inline long sc(long n, long a, long b, long c, long d)
{
	register long x8 __asm__("x8") = n;
	register long x0 __asm__("x0") = a;
	register long x1 __asm__("x1") = b;
	register long x2 __asm__("x2") = c;
	register long x3 __asm__("x3") = d;
	__asm__ volatile("svc 0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3) : "memory");
	return x0;
}
static long slen(const char *s) { long n = 0; while (s[n]) n++; return n; }
static void out(const char *s) { sc(64, 1, (long)s, slen(s), 0); }
static void hex2(u8 v) { static const char h[] = "0123456789abcdef"; char b[4] = { h[v >> 4], h[v & 15], ' ', 0 }; out(b); }
static int hexval(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }

void c_start(long *sp)
{
	int argc = (int)sp[0];
	char **argv = (char **)(sp + 1);
	char path[] = "/dev/i2c-0";
	long fd, addr = 0;
	int i, d;
	u8 reg, val;

	if (argc < 3) { out("usage: i2cdump <bus digit> <addr hex>\n"); sc(93, 1, 0, 0, 0); }
	path[9] = argv[1][0];
	for (i = 0; (d = hexval(argv[2][i])) >= 0; i++) addr = addr * 16 + d;
	fd = sc(56, -100, (long)path, 2, 0);
	if (fd < 0) { out("cannot open "); out(path); out("\n"); sc(93, 1, 0, 0, 0); }
	if (sc(29, fd, 0x0706, addr, 0) < 0) { out("I2C_SLAVE_FORCE failed\n"); sc(93, 1, 0, 0, 0); }
	for (reg = 0; reg < 0x80; reg++) {
		if ((reg & 15) == 0) { hex2(reg); out(": "); }
		val = 0xee;
		if (sc(64, fd, (long)&reg, 1, 0) != 1 || sc(63, fd, (long)&val, 1, 0) != 1)
			out("?? ");
		else
			hex2(val);
		if ((reg & 15) == 15) out("\n");
	}
	sc(93, 0, 0, 0, 0);
}
__asm__(".global _start\n_start:\n\tmov x0, sp\n\tbl c_start\n");
