/* PassGen: native Wayland password generator. libc only: raw Wayland protocol, software rendering. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <ctype.h>
#include <math.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <sys/random.h>
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

__asm__(".section .rodata\n.balign 4\n.globl font_start\nfont_start:\n.incbin \"assets/quicksand.ttf\"\n.globl font_end\nfont_end:\n.previous\n");
extern const unsigned char font_start[], font_end[];

/* ---------- state ---------- */
static int sock, W = 616, H = 1000, nid = 4, buf, configured, dirty = 1, dark = 1, len = 20, amb, have_curl,
           src, dragging, cfd = -1, synced, has_kb, has_ptr, has_touch;
static int on[4] = {1, 1, 1, 1};
static uint32_t comp, shm, wm, seat, ddm, surf, xs, top, ddev, ptr, kb, tch;   /* ids must be allocated sequentially */
static uint32_t lastserial, *px, *pxbase, bufid[2];
static int busy[2], compact;
#define PA 60.f   /* fixed password-area height: the window never changes height while you drag the slider */
static size_t pxsz;
static double bits, mx, my;
static char pw[132], status[96], chk[132], suf[36];
static uint32_t statcol, BG, CARD, LINE, TX, MUT, AC, DIG, SYM, BAD;
static time_t clr;
static FILE *cp;
static char rb[131072];
static int rn;
static float sl_x, sl_w, S = 1.4f, xoff;
static int bw, bh, hcap;
#define LW 440.f   /* logical width; everything is laid out in these units and scaled by S */

/* ---------- wire protocol ---------- */
static uint32_t ibuf[16384], obuf[1024];
static uint8_t *ib = (uint8_t *)ibuf, *ob = (uint8_t *)obuf;
static int in_n, on_n, fdq[16], nfd;

static void msg(uint32_t o, uint32_t op, const char *f, ...) {
  va_list a; va_start(a, f);
  uint32_t *p = (uint32_t *)(ob + on_n), *q = p + 2;
  for (; *f; f++) {
    if (*f == 'u') *q++ = va_arg(a, uint32_t);
    else { const char *s = va_arg(a, const char *); uint32_t l = strlen(s) + 1;
      *q++ = l; memset(q, 0, (l + 3) & ~3u); memcpy(q, s, l); q += (l + 3) / 4; }
  }
  va_end(a);
  p[0] = o; p[1] = ((uint32_t)((q - p) * 4) << 16) | op; on_n += (q - p) * 4;
}
static void flush(int fd) {
  if (!on_n && fd < 0) return;
  struct iovec io = {ob, on_n}; char cb[CMSG_SPACE(4)]; struct msghdr m; memset(&m, 0, sizeof m); memset(cb, 0, sizeof cb);
  m.msg_iov = &io; m.msg_iovlen = 1;
  if (fd >= 0) { m.msg_control = cb; m.msg_controllen = sizeof cb; struct cmsghdr *c = CMSG_FIRSTHDR(&m);
    c->cmsg_level = SOL_SOCKET; c->cmsg_type = SCM_RIGHTS; c->cmsg_len = CMSG_LEN(4); memcpy(CMSG_DATA(c), &fd, 4); }
  if (sendmsg(sock, &m, 0) < 0) exit(1);
  on_n = 0;
}
static int recv_more(void) {
  struct iovec io = {ib + in_n, sizeof ibuf - in_n}; char cb[CMSG_SPACE(16 * 4)]; struct msghdr m; memset(&m, 0, sizeof m);
  m.msg_iov = &io; m.msg_iovlen = 1; m.msg_control = cb; m.msg_controllen = sizeof cb;
  ssize_t r = recvmsg(sock, &m, MSG_CMSG_CLOEXEC); if (r <= 0) return -1;
  for (struct cmsghdr *c = CMSG_FIRSTHDR(&m); c; c = CMSG_NXTHDR(&m, c))
    if (c->cmsg_type == SCM_RIGHTS) { int n = (c->cmsg_len - CMSG_LEN(0)) / 4; if (nfd + n <= 16) { memcpy(fdq + nfd, CMSG_DATA(c), n * 4); nfd += n; } }
  in_n += r; return 0;
}
static int popfd(void) { if (!nfd) return -1; int f = fdq[0]; memmove(fdq, fdq + 1, --nfd * sizeof(int)); return f; }

/* ---------- password logic ---------- */
static uint32_t rnd(uint32_t n) {
  uint32_t r, lim = (0xFFFFFFFFu / n) * n;
  do { if (getrandom(&r, 4, 0) != 4) exit(1); } while (r >= lim);
  return r % n;
}
static void gen(void) {
  static const char *S[4] = {"abcdefghijklmnopqrstuvwxyz", "ABCDEFGHIJKLMNOPQRSTUVWXYZ", "0123456789", "!@#$%^&*()-_=+[]{};:,.<>?/~"};
  char pool[4][64], all[256]; int np = 0, na = 0;
  for (int i = 0; i < 4; i++) if (on[i]) {
    int k = 0; for (const char *s = S[i]; *s; s++) if (!(amb && strchr("O0lI1|", *s))) pool[np][k++] = *s;
    pool[np][k] = 0; memcpy(all + na, pool[np], k); na += k; np++;
  }
  if (!np) { strcpy(pool[0], S[0]); np = 1; strcpy(all, S[0]); na = strlen(S[0]); }
  for (int i = np - 1; i > 0; i--) { int j = rnd(i + 1); char t[64]; memcpy(t, pool[i], 64); memcpy(pool[i], pool[j], 64); memcpy(pool[j], t, 64); }
  int k = 0;
  for (int i = 0; i < np && k < len; i++) pw[k++] = pool[i][rnd(strlen(pool[i]))];   /* one of each class when it fits */
  while (k < len) pw[k++] = all[rnd(na)];
  for (int i = len - 1; i > 0; i--) { int j = rnd(i + 1); char t = pw[i]; pw[i] = pw[j]; pw[j] = t; }
  pw[len] = 0; bits = len * log2((double)na); status[0] = 0; statcol = 0; dirty = 1;
}

static uint32_t rol(uint32_t v, int s) { return v << s | v >> (32 - s); }
static void sha1(const uint8_t *d, size_t n, uint8_t o[20]) {
  uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0}, w[80];
  uint8_t m[192]; memset(m, 0, sizeof m); memcpy(m, d, n); m[n] = 0x80;
  size_t nb = (n + 9 + 63) / 64; uint64_t bl = (uint64_t)n * 8; for (int i = 0; i < 8; i++) m[nb * 64 - 1 - i] = bl >> (8 * i);
  for (size_t b = 0; b < nb; b++) {
    for (int t = 0; t < 16; t++) w[t] = m[b * 64 + t * 4] << 24 | m[b * 64 + t * 4 + 1] << 16 | m[b * 64 + t * 4 + 2] << 8 | m[b * 64 + t * 4 + 3];
    for (int t = 16; t < 80; t++) w[t] = rol(w[t - 3] ^ w[t - 8] ^ w[t - 14] ^ w[t - 16], 1);
    uint32_t a = h[0], bb = h[1], c = h[2], dd = h[3], e = h[4];
    for (int t = 0; t < 80; t++) {
      uint32_t f, k;
      if (t < 20) { f = (bb & c) | (~bb & dd); k = 0x5A827999; } else if (t < 40) { f = bb ^ c ^ dd; k = 0x6ED9EBA1; }
      else if (t < 60) { f = (bb & c) | (bb & dd) | (c & dd); k = 0x8F1BBCDC; } else { f = bb ^ c ^ dd; k = 0xCA62C1D6; }
      uint32_t tmp = rol(a, 5) + f + e + k + w[t]; e = dd; dd = c; c = rol(bb, 30); bb = a; a = tmp;
    }
    h[0] += a; h[1] += bb; h[2] += c; h[3] += dd; h[4] += e;
  }
  for (int i = 0; i < 20; i++) o[i] = h[i / 4] >> (24 - 8 * (i % 4));
}

/* Opt-in breach check: only the first 5 hex chars of the SHA-1 leave the machine (k-anonymity), via curl. */
static void check(void) {
  if (!have_curl || cp) return;
  uint8_t d[20]; char hx[41], cmd[220]; sha1((uint8_t *)pw, len, d);
  for (int i = 0; i < 20; i++) sprintf(hx + 2 * i, "%02X", d[i]);
  snprintf(cmd, sizeof cmd, "curl -fsS -m 8 -H 'Add-Padding: true' https://api.pwnedpasswords.com/range/%.5s 2>/dev/null", hx);
  if (!(cp = popen(cmd, "r"))) return;
  cfd = fileno(cp); rn = 0; strcpy(chk, pw); memcpy(suf, hx + 5, 36);
  strcpy(status, "Checking..."); statcol = 0; dirty = 1;
}
static void check_done(void) {
  int rc = pclose(cp); cp = NULL; cfd = -1; rb[rn] = 0;
  if (strcmp(chk, pw)) return;
  char *q = strstr(rb, suf); long n = q ? atol(q + 36) : 0;
  if (rc) { strcpy(status, "Couldn't reach the service"); statcol = 0; }
  else if (n > 0) { snprintf(status, sizeof status, "Found in %ld known breaches - don't use it", n); statcol = BAD; }
  else { strcpy(status, "Not found in known breaches"); statcol = DIG; }
  dirty = 1;
}

/* ---------- clipboard ---------- */
static void copy(void) {
  if (!ddev) { strcpy(status, "Clipboard unavailable"); statcol = BAD; dirty = 1; return; }
  if (src) msg(src, 1, "");
  src = nid++; msg(ddm, 0, "u", src);
  msg(src, 0, "s", "text/plain;charset=utf-8"); msg(src, 0, "s", "text/plain"); msg(src, 0, "s", "x-kde-passwordManagerHint");
  msg(ddev, 1, "uu", src, lastserial); flush(-1);
  clr = time(0) + 30; strcpy(status, "Copied - clears in 30s"); statcol = DIG; dirty = 1;
}

/* ---------- drawing ---------- */
static void alloc_buf(void);
static stbtt_fontinfo fi;
static void theme(void) {
  if (dark) { BG = 0x0E1116; CARD = 0x171B22; LINE = 0x262C36; TX = 0xE8ECF2; MUT = 0x8B95A5; AC = 0x7C9CFF; DIG = 0x6EE7B7; SYM = 0xFBBF24; BAD = 0xF87171; }
  else { BG = 0xF4F6FA; CARD = 0xFFFFFF; LINE = 0xDDE2EA; TX = 0x161A21; MUT = 0x667085; AC = 0x4361EE; DIG = 0x059669; SYM = 0xB45309; BAD = 0xDC2626; }
}
static void blendp(int x, int y, uint32_t c, float a) {
  if (a <= 0 || x < 0 || y < 0 || x >= W || y >= H) return;
  if (a > 1) a = 1;
  uint32_t d = px[y * W + x], r = 0;
  for (int s = 16; s >= 0; s -= 8) r |= (uint32_t)(((c >> s) & 255) * a + ((d >> s) & 255) * (1 - a) + .5f) << s;
  px[y * W + x] = 0xFF000000 | r;
}
static void rrect(float x, float y, float w, float h, float r, uint32_t c) {
  x = x * S + xoff; y *= S; w *= S; h *= S; r *= S;
  int x0 = fmaxf(0, floorf(x)), x1 = fminf(W, ceilf(x + w)), y0 = fmaxf(0, floorf(y)), y1 = fminf(H, ceilf(y + h));
  float cx = x + w / 2, cy = y + h / 2, hx = w / 2 - r, hy = h / 2 - r;
  for (int j = y0; j < y1; j++) for (int i = x0; i < x1; i++) {
    float dx = fabsf(i + .5f - cx) - hx, dy = fabsf(j + .5f - cy) - hy;
    float d = hypotf(fmaxf(dx, 0), fmaxf(dy, 0)) + fminf(fmaxf(dx, dy), 0) - r;
    blendp(i, j, c, .5f - d);
  }
}
static float glyph(float x, float y, int ch, float sz, uint32_t c, float cell) {
  x = x * S + xoff; y *= S; sz *= S; cell *= S;
  float sc = stbtt_ScaleForPixelHeight(&fi, sz); int adv, lsb, asc, desc, gap, w, h, xo, yo;
  stbtt_GetCodepointHMetrics(&fi, ch, &adv, &lsb); stbtt_GetFontVMetrics(&fi, &asc, &desc, &gap);
  if (cell > 0) x += (cell - adv * sc) / 2;
  unsigned char *bm = stbtt_GetCodepointBitmap(&fi, 0, sc, ch, &w, &h, &xo, &yo);
  if (bm) { for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) blendp((int)x + i + xo, (int)(y + asc * sc) + j + yo, c, bm[j * w + i] / 255.f); stbtt_FreeBitmap(bm, 0); }
  return adv * sc / S;
}
static float tw(const char *s, float sz, float sp) {
  float sc = stbtt_ScaleForPixelHeight(&fi, sz * S), w = 0; int a, l;
  for (; *s; s++) { stbtt_GetCodepointHMetrics(&fi, (unsigned char)*s, &a, &l); w += a * sc + sp * S; } return w / S;
}
static void text(float x, float y, const char *s, float sz, uint32_t c, float sp) {
  for (; *s; s++) x += glyph(x, y, (unsigned char)*s, sz, c, 0) + sp;
}
static void icon(float cx, float cy, float r) {   /* moon in dark mode, sun in light mode */
  cx = cx * S + xoff; cy *= S; r *= S; float R = 20 * S;
  for (int j = (int)(cy - R); j < cy + R; j++) for (int i = (int)(cx - R); i < cx + R; i++) {
    int hit = 0;
    for (int sj = 0; sj < 3; sj++) for (int si = 0; si < 3; si++) {
      float x = i + (si + .5f) / 3 - cx, y = j + (sj + .5f) / 3 - cy, d = hypotf(x, y);
      if (dark) hit += d < r && hypotf(x - r * .55f, y + r * .45f) >= r * .85f;
      else { float k = fmodf(atan2f(y, x) + 6.2831853f, .7853982f), dd = fminf(k, .7853982f - k);
        hit += d < r * .62f || (d > r * .95f && d < r * 1.35f && d * sinf(dd) < r * .09f); }
    }
    blendp(i, j, TX, hit / 9.f);
  }
}

typedef struct { float x, y, w, h; int a; } Hit;
static Hit hits[32]; static int nh;
enum { A_DOTS = 10, A_CLOSE = 8, A_MIN = 9, A_THEME = 1, A_COPY, A_NEW, A_CHECK, A_SLIDE, A_PW, A_MOVE, A_T0 = 20 };
static void hit(float x, float y, float w, float h, int a) { hits[nh++] = (Hit){x * S + xoff, y * S, w * S, h * S, a}; }
static void line(float x0, float y0, float x1, float y1, float w, uint32_t c) {
  x0 = x0 * S + xoff; x1 = x1 * S + xoff; y0 *= S; y1 *= S; w *= S;
  float dx = x1 - x0, dy = y1 - y0;
  for (int j = (int)(fminf(y0, y1) - w - 1); j < fmaxf(y0, y1) + w + 1; j++) for (int i = (int)(fminf(x0, x1) - w - 1); i < fmaxf(x0, x1) + w + 1; i++) {
    float t = fminf(1, fmaxf(0, ((i + .5f - x0) * dx + (j + .5f - y0) * dy) / (dx * dx + dy * dy + 1e-6f)));
    blendp(i, j, c, w / 2 + .5f - hypotf(i + .5f - x0 - t * dx, j + .5f - y0 - t * dy));
  }
}
static void card(float x, float y, float w, float h) { rrect(x, y, w, h, 18, LINE); rrect(x + 1, y + 1, w - 2, h - 2, 17, CARD); }
static void btn(float x, float y, float w, const char *s, int primary, int a) {
  if (primary) rrect(x, y, w, 52, 14, AC); else { rrect(x, y, w, 52, 14, LINE); rrect(x + 1, y + 1, w - 2, 50, 13, CARD); }
  text(x + (w - tw(s, 16, 0)) / 2, y + 15, s, 16, primary ? 0xFFFFFF : TX, 0); hit(x, y, w, 52, a);
}

static void logo(float x, float y, float s) {   /* padlock + dice pips: "random + secure" */
  float k = s / 100; const uint32_t WH = 0xFFFFFF;
  static const float P[5][2] = {{38, 53}, {62, 53}, {50, 62}, {38, 71}, {62, 71}};
  rrect(x, y, s, s, 22 * k, AC);
  rrect(x + 31.5f * k, y + 15.5f * k, 37 * k, 36 * k, 18.5f * k, WH);
  rrect(x + 40.5f * k, y + 24.5f * k, 19 * k, 30 * k, 9.5f * k, AC);
  rrect(x + 26 * k, y + 44 * k, 48 * k, 36 * k, 9 * k, WH);
  for (int i = 0; i < 5; i++) rrect(x + (P[i][0] - 3.8f) * k, y + (P[i][1] - 3.8f) * k, 7.6f * k, 7.6f * k, 3.8f * k, AC);
}

static void draw(void) {
  static const char *L[5] = {"Lowercase", "Uppercase", "Digits", "Symbols", "Avoid look-alikes (O 0 l 1 I |)"};
  float m = 18, cw = LW - 2 * m, y = 16;
  float total = 660 + PA;
  S = W / LW; if (hcap && hcap / total < S) S = hcap / total;
  int nhgt = (int)ceilf(total * S); if (W != bw || nhgt != bh) { H = nhgt; alloc_buf(); }
  int c = !busy[0] ? 0 : !busy[1] ? 1 : -1;
  if (c < 0) { dirty = 1; return; }   /* both buffers still in use by the compositor: retry on release */
  px = pxbase + (size_t)c * W * H;
  xoff = (W - LW * S) / 2;
  nh = 0; for (size_t i = 0; i < (size_t)W * H; i++) px[i] = 0xFF000000 | BG;
  logo(m, y + 6, 32);
  text(m + 42, y + 12, "PASSGEN", 13, MUT, 2.2f);
  for (int i = 0; i < 3; i++) { float cx = LW - m - 18 - 40 * i, cy = y + 22; rrect(cx - 18, cy - 17, 36, 34, 17, LINE);
    hit(cx - 18, cy - 18, 36, 36, i == 0 ? A_CLOSE : i == 1 ? A_MIN : A_THEME);
    if (i == 0) { line(cx - 5, cy - 5, cx + 5, cy + 5, 1.8f, TX); line(cx - 5, cy + 5, cx + 5, cy - 5, 1.8f, TX); }
    else if (i == 1) line(cx - 6, cy, cx + 6, cy, 1.8f, TX); else icon(cx, cy, 8); }
  hit(0, 0, LW, 60, A_MOVE);
  y += 56;
  float ch = 18 + PA + 14 + 6 + 10 + 16 + 8 + 16 + 16;
  card(m, y, cw, ch); hit(m, y, cw, 18 + PA, A_PW);
  { float aw = cw - 36, fs = 22, cwid = 14.5f, rh = 30; int pc, shown = len;
    if (compact) { pc = (int)(aw / cwid); if (len > pc * 2) shown = pc * 2; }   /* two lines, rest hidden */
    else for (;; fs -= .5f) { cwid = fs * .659f; rh = fs * 1.364f; pc = (int)(aw / cwid);   /* whole password, shrunk to fit */
      if (((len + pc - 1) / pc) * rh <= PA || fs <= 7) break; }
    float oy = y + 18 + (PA - ((shown + pc - 1) / pc) * rh) / 2;
    for (int i = 0; i < shown; i++) { char k = pw[i]; float gx = m + 18 + (i % pc) * cwid, gy = oy + (i / pc) * rh;
      if (shown < len && i == shown - 1) { for (int d = -1; d <= 1; d++) rrect(gx + cwid / 2 + d * 5 - 1.8f, gy + rh * .7f - 1.8f, 3.6f, 3.6f, 1.8f, MUT); }
      else glyph(gx, gy, k, fs, isdigit(k) ? DIG : isalpha(k) ? TX : SYM, cwid); } }
  float by = y + 18 + PA + 14, f = fminf(1, bits / 128);
  uint32_t sc = bits < 40 ? BAD : bits < 60 ? SYM : bits < 80 ? AC : DIG;
  rrect(m + 18, by, cw - 36, 6, 3, LINE); rrect(m + 18, by, fmaxf(6, (cw - 36) * f), 6, 3, sc);
  char b[40]; text(m + 18, by + 16, bits < 40 ? "Weak" : bits < 60 ? "Fair" : bits < 80 ? "Good" : bits < 100 ? "Strong" : "Excellent", 13, MUT, 0);
  snprintf(b, sizeof b, "~%d bits", (int)(bits + .5)); text(m + cw - 18 - tw(b, 13, 0), by + 16, b, 13, MUT, 0);
  text(m + 18, by + 40, status, 13, statcol ? statcol : MUT, 0);
  for (int d = 0; d < 3; d++) rrect(m + cw - 18 - 24 + d * 9.f, by + 46, 4.4f, 4.4f, 2.2f, compact ? AC : MUT);   /* "..." display toggle */
  hit(m + cw - 18 - 36, by + 34, 44, 30, A_DOTS);
  y += ch + 14;
  int nb = have_curl ? 3 : 2; float u = (cw - 8 * (nb - 1)) / (have_curl ? 4 : 3), x = m;
  btn(x, y, 2 * u, "Copy", 1, A_COPY); x += 2 * u + 8; btn(x, y, u, "New", 0, A_NEW); x += u + 8;
  if (have_curl) btn(x, y, u, "Check", 0, A_CHECK);
  y += 52 + 14;
  float lh = 16 + 28 + 12 + 24 + 16; card(m, y, cw, lh);
  text(m + 18, y + 20, "Length", 15, MUT, 0); snprintf(b, sizeof b, "%d", len); text(m + cw - 18 - tw(b, 26, 0), y + 12, b, 26, TX, 0);
  float sx = m + 29, sw = cw - 58; sl_x = sx * S + xoff; sl_w = sw * S; float ty = y + 16 + 28 + 12 + 12, t = (len - 1) / 127.f;
  rrect(sx, ty - 3, sw, 6, 3, LINE); rrect(sx, ty - 3, fmaxf(6, t * sw), 6, 3, AC);
  rrect(sx + t * sw - 11, ty - 11, 22, 22, 11, AC); rrect(sx + t * sw - 4, ty - 4, 8, 8, 4, 0xFFFFFF);
  hit(m, ty - 22, cw, 44, A_SLIDE);
  y += lh + 14;
  card(m, y, cw, 12 + 5 * 46);
  for (int i = 0; i < 5; i++) { float ry = y + 6 + i * 46; int v = i < 4 ? on[i] : amb;
    text(m + 18, ry + 12, L[i], 16, TX, 0);
    rrect(m + cw - 18 - 46, ry + 10, 46, 26, 13, v ? AC : LINE); rrect(m + cw - 18 - (v ? 26 : 46) + 3, ry + 13, 20, 20, 10, 0xFFFFFF);
    hit(m, ry, cw, 46, A_T0 + i); }
  y += 12 + 5 * 46 + 16;
  const char *f1 = "Generated on-device with the kernel's secure random.", *f2 = "Nothing is stored. The internet is used only if you tap Check.";
  text((LW - tw(f1, 12, 0)) / 2, y, f1, 12, MUT, 0); text((LW - tw(f2, 12, 0)) / 2, y + 18, f2, 12, MUT, 0);
  msg(surf, 1, "uuu", bufid[c], 0, 0); busy[c] = 1; msg(surf, 9, "uuuu", 0, 0, W, H); msg(surf, 6, ""); flush(-1);
}

static void alloc_buf(void) {
  if (bufid[0]) { msg(bufid[0], 0, ""); msg(bufid[1], 0, ""); }
  if (pxbase) munmap(pxbase, 2 * pxsz);
  int st = W * 4, fd = memfd_create("pg", MFD_CLOEXEC); pxsz = (size_t)st * H;
  if (fd < 0 || ftruncate(fd, 2 * pxsz)) exit(1);
  pxbase = mmap(0, 2 * pxsz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);   /* two buffers: draw into the one the compositor released */
  int pool = nid++; msg(shm, 0, "uu", pool, (uint32_t)(2 * pxsz)); flush(fd); close(fd);
  for (int i = 0; i < 2; i++) { bufid[i] = nid++; busy[i] = 0; msg(pool, 0, "uuuuuu", bufid[i], (uint32_t)(i * pxsz), W, H, st, 0); }
  msg(pool, 1, ""); bw = W; bh = H;
}

/* ---------- input ---------- */
static void slide(double x) { double t = (x - sl_x) / sl_w; if (t < 0) t = 0; if (t > 1) t = 1; int n = 1 + (int)(t * 127 + .5); if (n != len) { len = n; gen(); } }
static void press(double x, double y, uint32_t serial) {
  lastserial = serial;
  for (int i = 0; i < nh; i++) { Hit *h = &hits[i];
    if (x < h->x || x >= h->x + h->w || y < h->y || y >= h->y + h->h) continue;
    switch (h->a) {
      case A_DOTS: compact ^= 1; dirty = 1; break;
      case A_CLOSE: exit(0);
      case A_MIN: msg(top, 13, ""); flush(-1); break;
      case A_THEME: dark = !dark; theme(); dirty = 1; break;
      case A_COPY: case A_PW: copy(); break;
      case A_NEW: gen(); break;
      case A_CHECK: check(); break;
      case A_SLIDE: dragging = 1; slide(x); break;
      case A_MOVE: if (seat) msg(top, 5, "uu", seat, serial); flush(-1); break;
      default: { int k = h->a - A_T0; if (k < 4) on[k] ^= 1; else amb ^= 1; gen(); }
    }
    return;
  }
}

static const struct { const char *n; uint32_t *id; int v; } G[] = {{"wl_compositor", &comp, 4}, {"wl_shm", &shm, 1}, {"xdg_wm_base", &wm, 1}, {"wl_seat", &seat, 1}, {"wl_data_device_manager", &ddm, 1}};

static void handle(uint32_t o, uint32_t op, uint32_t *p) {
  if (o == 1 && op == 0) { fprintf(stderr, "wayland error: %s\n", (char *)(p + 3)); exit(1); }
  else if (o == 2 && op == 0) { uint32_t l = p[1], v = p[2 + (l + 3) / 4]; char *s = (char *)(p + 2);
    for (unsigned i = 0; i < sizeof G / sizeof *G; i++) if (!strcmp(s, G[i].n)) { *G[i].id = nid++; msg(2, 0, "usuu", p[0], s, v < (uint32_t)G[i].v ? v : (uint32_t)G[i].v, *G[i].id); } }
  else if (o == 3) synced = 1;
  else if (o == wm && op == 0) msg(wm, 3, "u", p[0]);
  else if (o == xs && op == 0) { msg(xs, 4, "u", p[0]); configured = 1; dirty = 1; }
  else if (o == top && op == 0) { int w = p[0], h = p[1]; if (w > 0 && h > 0 && (w != bw || h != bh)) { W = w; hcap = h; dirty = 1; } }
  else if (o == top && op == 1) exit(0);
  else if (o == seat && op == 0) {
    if ((p[0] & 1) && !has_ptr) { has_ptr = 1; ptr = nid++; msg(seat, 0, "u", ptr); }
    if ((p[0] & 2) && !has_kb) { has_kb = 1; kb = nid++; msg(seat, 1, "u", kb); }
    if ((p[0] & 4) && !has_touch) { has_touch = 1; tch = nid++; msg(seat, 2, "u", tch); } }
  else if (o == ptr) {
    if (op == 0) { mx = (int)p[2] / 256.0; my = (int)p[3] / 256.0; }
    else if (op == 2) { mx = (int)p[1] / 256.0; my = (int)p[2] / 256.0; if (dragging) slide(mx); }
    else if (op == 3) { if (p[3] == 1) press(mx, my, p[0]); else dragging = 0; } }
  else if (o == tch) {
    if (op == 0) press((int)p[4] / 256.0, (int)p[5] / 256.0, p[0]);
    else if (op == 1) dragging = 0;
    else if (op == 2 && dragging) slide((int)p[2] / 256.0); }
  else if (o == kb) { if (op == 0) { int f = popfd(); if (f >= 0) close(f); } else if (op == 3 && p[2] == 1 && p[3] == 1) exit(0); } /* Esc quits */
  else if (bufid[0] && (o == bufid[0] || o == bufid[1])) { if (op == 0) busy[o == bufid[1]] = 0; }
  else if (src && o == (uint32_t)src) {
    if (op == 1) { int f = popfd(); if (f >= 0) { const char *mime = (char *)(p + 1);
      const char *t = strcmp(mime, "x-kde-passwordManagerHint") ? pw : "secret"; if (write(f, t, strlen(t)) < 0) {} close(f); } }
    else if (op == 2) { msg(src, 1, ""); src = 0; strcpy(status, ""); dirty = 1; } }
}

static void dispatch(void) {
  int off = 0;
  while (in_n - off >= 8) { uint32_t *h = (uint32_t *)(ib + off), sz = h[1] >> 16;
    if (sz < 8 || in_n - off < (int)sz) break; handle(h[0], h[1] & 0xffff, h + 2); off += sz; }
  memmove(ib, ib + off, in_n - off); in_n -= off;
}

int main(void) {
  const char *rd = getenv("XDG_RUNTIME_DIR"), *dn = getenv("WAYLAND_DISPLAY");
  if (!rd) { fputs("no XDG_RUNTIME_DIR\n", stderr); return 1; }
  struct sockaddr_un a; memset(&a, 0, sizeof a); a.sun_family = AF_UNIX;
  snprintf(a.sun_path, sizeof a.sun_path, dn && dn[0] == '/' ? "%s%s" : "%s/%s", dn && dn[0] == '/' ? "" : rd, dn ? dn : "wayland-0");
  sock = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (connect(sock, (struct sockaddr *)&a, sizeof a)) { fputs("can't connect to Wayland\n", stderr); return 1; }
  stbtt_InitFont(&fi, font_start, 0);
  have_curl = system("command -v curl >/dev/null 2>&1") == 0;
  { const char *e = getenv("PASSGEN_SCALE"); if (e && atof(e) >= .6) W = (int)(LW * atof(e)); }
#ifdef PG_DUMP
  if (getenv("PL")) len = atoi(getenv("PL"));
  if (getenv("PC")) compact = 1;
#endif
  theme(); gen();
  msg(1, 1, "u", 2); msg(1, 0, "u", 3); flush(-1);
  while (!synced) { if (recv_more() < 0) return 1; dispatch(); flush(-1); }
  if (!comp || !shm || !wm) { fputs("compositor lacks a required protocol\n", stderr); return 1; }
  surf = nid++; msg(comp, 0, "u", surf);
  xs = nid++; msg(wm, 2, "uu", xs, surf);
  top = nid++; msg(xs, 1, "u", top);
  msg(top, 2, "s", "PassGen"); msg(top, 3, "s", "passgen"); msg(surf, 6, "");
  if (ddm && seat) { ddev = nid++; msg(ddm, 1, "uu", ddev, seat); }
  flush(-1);
  for (;;) {
    struct pollfd f[2] = {{sock, POLLIN, 0}, {cfd, POLLIN, 0}};
    int to = clr ? (int)((clr - time(0)) * 1000) : -1; if (clr && to < 0) to = 0;
    poll(f, cfd >= 0 ? 2 : 1, to);
    if (f[0].revents & POLLIN) { if (recv_more() < 0) break; dispatch(); }
    if (cfd >= 0 && (f[1].revents & (POLLIN | POLLHUP))) {
      ssize_t r = read(cfd, rb + rn, sizeof rb - 1 - rn);
      if (r > 0) rn += r; else check_done(); }
    if (clr && time(0) >= clr) { clr = 0; if (src) { msg(ddev, 1, "uu", 0, lastserial); strcpy(status, "Clipboard cleared"); statcol = 0; dirty = 1; } }
    if (dirty && configured) { dirty = 0; draw();
#ifdef PG_DUMP
      FILE *df = fopen("/tmp/pg.ppm", "wb"); fprintf(df, "P6\n%d %d\n255\n", W, H);
      for (int i = 0; i < W * H; i++) { fputc(px[i] >> 16, df); fputc(px[i] >> 8, df); fputc(px[i], df); } fclose(df); exit(0);
#endif
    } else flush(-1);
  }
  return 0;
}
