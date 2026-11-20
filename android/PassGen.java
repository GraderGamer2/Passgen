package app.passgen;

import android.app.Activity;
import android.content.*;
import android.content.ClipboardManager;
import android.graphics.*;
import android.graphics.drawable.GradientDrawable;
import android.os.*;
import android.text.*;
import android.text.style.ForegroundColorSpan;
import android.view.*;
import android.widget.*;
import java.io.*;
import java.net.*;
import java.security.*;
import java.util.*;

public class PassGen extends Activity {
  static final String[] SETS = {"abcdefghijklmnopqrstuvwxyz", "ABCDEFGHIJKLMNOPQRSTUVWXYZ", "0123456789", "!@#$%^&*()-_=+[]{};:,.<>?/~"};
  static final String[] NAMES = {"Lowercase", "Uppercase", "Digits", "Symbols"};
  final SecureRandom rng = new SecureRandom();
  final Handler h = new Handler(Looper.getMainLooper());
  final boolean[] on = {true, true, true, true};
  boolean dark = true, amb;
  int len = 20;
  double bits;
  String pw = "";
  Typeface tf;
  TextView out, lbl, bitsT, status;
  View bar;
  int bg, card, tx, mut, ac, dig, sym, bad;

  @Override protected void onCreate(Bundle b) {
    super.onCreate(b);
    getWindow().setFlags(WindowManager.LayoutParams.FLAG_SECURE, WindowManager.LayoutParams.FLAG_SECURE); // no screenshots
    dark = (getResources().getConfiguration().uiMode & 0x30) != 0x10;
    try { tf = Typeface.createFromAsset(getAssets(), "quicksand.ttf"); } catch (Exception e) { tf = Typeface.DEFAULT; }
    build();
  }

  int dp(int v) { return (int) (v * getResources().getDisplayMetrics().density); }

  TextView t(String s, int sp, int c) {
    TextView v = new TextView(this);
    v.setText(s); v.setTextSize(sp); v.setTextColor(c); v.setTypeface(tf);
    return v;
  }

  LinearLayout col() { LinearLayout l = new LinearLayout(this); l.setOrientation(LinearLayout.VERTICAL); return l; }

  LinearLayout card() {
    LinearLayout l = col();
    GradientDrawable g = new GradientDrawable();
    g.setColor(card); g.setCornerRadius(dp(18)); g.setStroke(dp(1), dark ? 0xFF262C36 : 0xFFDDE2EA);
    l.setBackground(g); l.setPadding(dp(18), dp(16), dp(18), dp(16));
    LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(-1, -2);
    p.bottomMargin = dp(14); l.setLayoutParams(p);
    return l;
  }

  GradientDrawable pill(int c, int r) { GradientDrawable g = new GradientDrawable(); g.setColor(c); g.setCornerRadius(dp(r)); return g; }

  Button btn(String s, int fill, int color) {
    Button b = new Button(this);
    b.setText(s); b.setAllCaps(false); b.setTextSize(16); b.setTypeface(tf, Typeface.BOLD);
    b.setTextColor(color); b.setBackground(pill(fill, 14)); b.setStateListAnimator(null);
    return b;
  }

  class Icon extends View {
    Icon(Context c) { super(c); }
    @Override protected void onDraw(Canvas c) {
      Paint p = new Paint(Paint.ANTI_ALIAS_FLAG); p.setColor(tx);
      float w = getWidth() / 2f, r = w * .36f;
      if (dark) { // moon
        Path a = new Path(), b = new Path();
        a.addCircle(w, w, r, Path.Direction.CW); b.addCircle(w + r * .55f, w - r * .45f, r * .85f, Path.Direction.CW);
        a.op(b, Path.Op.DIFFERENCE); c.drawPath(a, p);
      } else { // sun
        c.drawCircle(w, w, r * .62f, p); p.setStrokeWidth(w * .1f); p.setStrokeCap(Paint.Cap.ROUND);
        for (int i = 0; i < 8; i++) {
          double a = i * Math.PI / 4;
          c.drawLine(w + (float) Math.cos(a) * r * .95f, w + (float) Math.sin(a) * r * .95f, w + (float) Math.cos(a) * r * 1.35f, w + (float) Math.sin(a) * r * 1.35f, p);
        }
      }
    }
  }

  void build() {
    bg = dark ? 0xFF0E1116 : 0xFFF4F6FA; card = dark ? 0xFF171B22 : 0xFFFFFFFF;
    tx = dark ? 0xFFE8ECF2 : 0xFF161A21; mut = dark ? 0xFF8B95A5 : 0xFF667085;
    ac = dark ? 0xFF7C9CFF : 0xFF4361EE; dig = dark ? 0xFF6EE7B7 : 0xFF059669;
    sym = dark ? 0xFFFBBF24 : 0xFFB45309; bad = dark ? 0xFFF87171 : 0xFFDC2626;
    getWindow().setStatusBarColor(bg); getWindow().setNavigationBarColor(bg);
    ScrollView sv = new ScrollView(this); sv.setBackgroundColor(bg);
    sv.setSystemUiVisibility(dark ? 0 : 0x2010);
    LinearLayout root = col(); root.setPadding(dp(18), dp(20), dp(18), dp(28)); sv.addView(root);

    LinearLayout hd = new LinearLayout(this); hd.setGravity(Gravity.CENTER_VERTICAL);
    TextView title = t("PASSGEN", 13, mut); title.setLetterSpacing(.16f);
    hd.addView(title, new LinearLayout.LayoutParams(0, -2, 1));
    Icon ic = new Icon(this); ic.setContentDescription("Toggle theme");
    ic.setOnClickListener(v -> { dark = !dark; build(); });
    hd.addView(ic, new LinearLayout.LayoutParams(dp(44), dp(44)));
    root.addView(hd);

    LinearLayout c1 = card(); c1.setPadding(dp(18), dp(20), dp(18), dp(16));
    ((LinearLayout.LayoutParams) c1.getLayoutParams()).topMargin = dp(12);
    out = t("", 22, tx); out.setTypeface(Typeface.MONOSPACE); out.setMinHeight(dp(90)); out.setGravity(Gravity.CENTER_VERTICAL);
    out.setOnClickListener(v -> copy());
    c1.addView(out);
    FrameLayout track = new FrameLayout(this); track.setBackground(pill(dark ? 0xFF262C36 : 0xFFDDE2EA, 3));
    bar = new View(this); track.addView(bar, new FrameLayout.LayoutParams(0, dp(6)));
    LinearLayout.LayoutParams tp = new LinearLayout.LayoutParams(-1, dp(6)); tp.topMargin = dp(12);
    c1.addView(track, tp);
    LinearLayout meta = new LinearLayout(this); meta.setPadding(0, dp(8), 0, 0);
    lbl = t("", 13, mut); bitsT = t("", 13, mut);
    meta.addView(lbl, new LinearLayout.LayoutParams(0, -2, 1)); meta.addView(bitsT);
    c1.addView(meta);
    status = t("", 13, mut); status.setPadding(0, dp(6), 0, 0); c1.addView(status);
    root.addView(c1);

    LinearLayout row = new LinearLayout(this);
    Button cp = btn("Copy", ac, 0xFFFFFFFF), nw = btn("New", card, tx), ck = btn("Check", card, tx);
    cp.setOnClickListener(v -> copy()); nw.setOnClickListener(v -> gen()); ck.setOnClickListener(v -> check());
    row.addView(cp, new LinearLayout.LayoutParams(0, dp(52), 2)); 
    LinearLayout.LayoutParams m = new LinearLayout.LayoutParams(0, dp(52), 1); m.leftMargin = dp(8);
    row.addView(nw, m);
    LinearLayout.LayoutParams m2 = new LinearLayout.LayoutParams(0, dp(52), 1); m2.leftMargin = dp(8);
    row.addView(ck, m2);
    LinearLayout.LayoutParams rp = new LinearLayout.LayoutParams(-1, -2); rp.bottomMargin = dp(14);
    root.addView(row, rp);

    LinearLayout c2 = card();
    LinearLayout lh = new LinearLayout(this);
    TextView lt = t("Length", 15, mut), ln = t("" + len, 26, tx);
    lh.addView(lt, new LinearLayout.LayoutParams(0, -2, 1)); lh.addView(ln);
    c2.addView(lh);
    SeekBar sb = new SeekBar(this); sb.setMax(127); sb.setProgress(len - 1);
    sb.getProgressDrawable().setTint(ac); sb.getThumb().setTint(ac);
    sb.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
      public void onProgressChanged(SeekBar s, int p, boolean u) { len = p + 1; ln.setText("" + len); gen(); }
      public void onStartTrackingTouch(SeekBar s) {}
      public void onStopTrackingTouch(SeekBar s) {}
    });
    c2.addView(sb); root.addView(c2);

    LinearLayout c3 = card();
    for (int i = 0; i < 5; i++) {
      final int k = i;
      Switch sw = new Switch(this);
      sw.setText(i < 4 ? NAMES[i] : "Avoid look-alikes (O 0 l 1 I |)"); sw.setTextSize(16);
      sw.setTextColor(tx); sw.setTypeface(tf); sw.setPadding(0, dp(10), 0, dp(10));
      sw.setChecked(i < 4 ? on[i] : amb);
      sw.setOnCheckedChangeListener((v, ch) -> { if (k < 4) on[k] = ch; else amb = ch; gen(); });
      c3.addView(sw);
    }
    root.addView(c3);

    TextView f = t("Generated on-device with SecureRandom.\nNothing is stored. The internet is only used if you tap Check.", 12, mut);
    f.setGravity(Gravity.CENTER); root.addView(f);
    setContentView(sv);
    gen();
  }

  void gen() {
    ArrayList<String> pools = new ArrayList<>(); StringBuilder all = new StringBuilder();
    for (int i = 0; i < 4; i++) if (on[i]) { String s = amb ? SETS[i].replaceAll("[O0lI1|]", "") : SETS[i]; pools.add(s); all.append(s); }
    if (pools.isEmpty()) { pools.add(SETS[0]); all.append(SETS[0]); }
    char[] o = new char[len]; int k = 0;
    Collections.shuffle(pools, rng);
    for (String p : pools) if (k < len) o[k++] = p.charAt(rng.nextInt(p.length())); // one of each class when it fits
    while (k < len) o[k++] = all.charAt(rng.nextInt(all.length()));
    for (int i = len - 1; i > 0; i--) { int j = rng.nextInt(i + 1); char t = o[i]; o[i] = o[j]; o[j] = t; }
    pw = new String(o);
    bits = len * Math.log(all.length()) / Math.log(2);
    SpannableString sp = new SpannableString(pw);
    for (int i = 0; i < len; i++) {
      char c = o[i];
      sp.setSpan(new ForegroundColorSpan(Character.isDigit(c) ? dig : Character.isLetter(c) ? tx : sym), i, i + 1, 0);
    }
    out.setText(sp);
    String n; int col;
    if (bits < 40) { n = "Weak"; col = bad; } else if (bits < 60) { n = "Fair"; col = sym; }
    else if (bits < 80) { n = "Good"; col = ac; } else if (bits < 100) { n = "Strong"; col = dig; } else { n = "Excellent"; col = dig; }
    lbl.setText(n); bitsT.setText("~" + Math.round(bits) + " bits"); status.setText("");
    bar.setBackground(pill(col, 3));
    bar.post(() -> { FrameLayout.LayoutParams p = (FrameLayout.LayoutParams) bar.getLayoutParams(); p.width = (int) (((View) bar.getParent()).getWidth() * Math.min(1, bits / 128)); bar.setLayoutParams(p); });
  }

  void copy() {
    ClipboardManager cm = (ClipboardManager) getSystemService(CLIPBOARD_SERVICE);
    ClipData cd = ClipData.newPlainText("", pw);
    PersistableBundle pb = new PersistableBundle(); pb.putBoolean("android.content.extra.IS_SENSITIVE", true);
    cd.getDescription().setExtras(pb);
    cm.setPrimaryClip(cd);
    Toast.makeText(this, "Copied - clears in 30s", Toast.LENGTH_SHORT).show();
    h.removeCallbacksAndMessages(null);
    h.postDelayed(() -> { if (Build.VERSION.SDK_INT >= 28) cm.clearPrimaryClip(); }, 30000);
  }

  // Opt-in breach check: k-anonymity, only the first 5 chars of the SHA-1 ever leave the device.
  void check() {
    status.setTextColor(mut); status.setText("Checking...");
    final String p = pw;
    new Thread(() -> {
      String r; int col = mut;
      try {
        byte[] d = MessageDigest.getInstance("SHA-1").digest(p.getBytes("UTF-8"));
        StringBuilder x = new StringBuilder();
        for (byte b : d) x.append(String.format("%02X", b));
        String pre = x.substring(0, 5), suf = x.substring(5);
        HttpURLConnection c = (HttpURLConnection) new URL("https://api.pwnedpasswords.com/range/" + pre).openConnection();
        c.setConnectTimeout(8000); c.setReadTimeout(8000); c.setRequestProperty("Add-Padding", "true");
        BufferedReader br = new BufferedReader(new InputStreamReader(c.getInputStream()));
        String l; long n = 0;
        while ((l = br.readLine()) != null) if (l.startsWith(suf)) { n = Long.parseLong(l.substring(l.indexOf(':') + 1).trim()); break; }
        br.close();
        if (n > 0) { r = "Found in " + n + " known breaches - don't use it"; col = bad; }
        else { r = "Not found in known breaches"; col = dig; }
      } catch (Exception e) { r = "Couldn't reach the service"; }
      final String fr = r; final int fcol = col;
      runOnUiThread(() -> { if (p.equals(pw)) { status.setTextColor(fcol); status.setText(fr); } });
    }).start();
  }
}
