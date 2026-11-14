/*
 * nvtspi: talk to the downstream NVT driver through /proc/NVTSPI (freestanding, static, aarch64, no libc).
 *
 * The /proc/NVTSPI read handler takes its request from the user buffer:
 *   buf[0] = (bit7 << 7) | (len >> 8), buf[1] = len & 0xff, buf[2..] = SPI payload
 *   bit7 = 0 -> CTP_SPI_WRITE(payload, len) (NVTWRITE = 0)   bit7 = 1 -> CTP_SPI_READ(payload, len) (NVTREAD = 1)
 *   (payload[0] = cmd, the answer comes back in buf[2..])
 *
 * ops (one per argv):
 *   R:<addr hex>:<n>        select page of addr (write 0xff,page_hi,page_lo), then read n data bytes at cmd = addr & 0x7f
 *   r:<addr hex>:<n>        read n data bytes at cmd = addr & 0x7f WITHOUT selecting the page (uses whatever page is current)
 *   P:<addr hex>            only select the page of addr
 *   W:<addr hex>:<hexbytes> select page, then write the bytes at cmd = addr & 0x7f
 */
typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long u64;

static inline long sc4(long n, long a, long b, long c, long d)
{
	register long x8 __asm__("x8") = n;
	register long x0 __asm__("x0") = a;
	register long x1 __asm__("x1") = b;
	register long x2 __asm__("x2") = c;
	register long x3 __asm__("x3") = d;
	__asm__ volatile("svc 0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3) : "memory");
	return x0;
}

#define SYS_openat 56
#define SYS_read 63
#define SYS_write 64
#define SYS_exit 93

static long slen(const char *s) { long n = 0; while (s[n]) n++; return n; }
static void out(const char *s) { sc4(SYS_write, 1, (long)s, slen(s), 0); }

static void outhex(const u8 *p, int n)
{
	static const char h[] = "0123456789abcdef";
	char b[4];
	int i;
	for (i = 0; i < n; i++) {
		b[0] = h[p[i] >> 4]; b[1] = h[p[i] & 15]; b[2] = ' '; b[3] = 0;
		out(b);
	}
}

static void outdec(long v)
{
	char b[24]; int i = 22; int neg = v < 0;
	b[23] = 0;
	if (neg) v = -v;
	if (v == 0) b[i--] = '0';
	while (v) { b[i--] = '0' + (v % 10); v /= 10; }
	if (neg) b[i--] = '-';
	out(&b[i + 1]);
}

static int hexval(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

static u32 parse_hex(const char **s)
{
	u32 v = 0; int d;
	while ((d = hexval(**s)) >= 0) { v = (v << 4) | d; (*s)++; }
	return v;
}

static u32 parse_dec(const char **s)
{
	u32 v = 0;
	while (**s >= '0' && **s <= '9') { v = v * 10 + (**s - '0'); (*s)++; }
	return v;
}

static int fd;

/* rw: 1 = write, 0 = read. payload/len as the driver wants them. returns the syscall result, answer in req+2 */
static long proc_xfer(int rw, u8 *payload, int len, u8 *req)
{
	int i;
	/* driver enum: NVTWRITE = 0, NVTREAD = 1, and spi_wr = buf[0] >> 7, so bit 7 set = READ (rw here: 1 = write, 0 = read) */
	req[0] = (u8)(((rw ? 0 : 1) << 7) | ((len >> 8) & 0x7f));
	req[1] = (u8)(len & 0xff);
	for (i = 0; i < len; i++) req[2 + i] = payload[i];
	return sc4(SYS_read, fd, (long)req, len + 2, 0);
}

static long select_page(u32 addr, u8 *req)
{
	u8 p[3];
	p[0] = 0xff;
	p[1] = (addr >> 15) & 0xff;
	p[2] = (addr >> 7) & 0xff;
	return proc_xfer(1, p, 3, req);
}

static void run(const char *op)
{
	static u8 req[2048], pl[2048];
	const char *s = op + 2;
	u32 addr, n, i;
	long r;

	if (op[1] != ':') { out("bad op: "); out(op); out("\n"); return; }
	addr = parse_hex(&s);
	switch (op[0]) {
	case 'P':
		r = select_page(addr, req);
		out(op); out(" -> ret="); outdec(r); out("\n");
		break;
	case 'R':
	case 'r':
		if (*s == ':') s++;
		n = parse_dec(&s);
		if (n < 1 || n > 1000) { out("bad len\n"); return; }
		if (op[0] == 'R') {
			r = select_page(addr, req);
			if (r < 0) { out(op); out(" page select failed ret="); outdec(r); out("\n"); return; }
		}
		for (i = 0; i < n + 1; i++) pl[i] = 0;
		pl[0] = addr & 0x7f;
		r = proc_xfer(0, pl, n + 1, req);
		out(op); out(" -> ret="); outdec(r); out(" data="); outhex(&req[3], n);
		out(" | raw[0..2]="); outhex(req, 3); out("\n");
		break;
	case 'W':
		if (*s == ':') s++;
		n = 0;
		pl[0] = addr & 0x7f;
		while (hexval(*s) >= 0) { pl[1 + n++] = (u8)parse_hex(&s); if (*s == ',') s++; }
		r = select_page(addr, req);
		if (r < 0) { out(op); out(" page select failed ret="); outdec(r); out("\n"); return; }
		r = proc_xfer(1, pl, n + 1, req);
		out(op); out(" -> ret="); outdec(r); out("\n");
		break;
	default:
		out("unknown op: "); out(op); out("\n");
	}
}

void c_start(long *sp)
{
	int argc = (int)sp[0];
	char **argv = (char **)(sp + 1);
	int i;

	fd = (int)sc4(SYS_openat, -100, (long)"/proc/NVTSPI", 0, 0);
	if (fd < 0) { out("cannot open /proc/NVTSPI, ret="); outdec(fd); out("\n"); sc4(SYS_exit, 1, 0, 0, 0); }
	for (i = 1; i < argc; i++) run(argv[i]);
	sc4(SYS_exit, 0, 0, 0, 0);
}

__asm__(".global _start\n_start:\n\tmov x0, sp\n\tbl c_start\n");
