#!/usr/bin/env python3
"""Make build/fasttrack/eng_root/engine/ = a copy of engine/ with the FastTrack experiments patched into px_track.h (engine/ itself is never edited).
Each experiment is guarded by a macro so a variant is just -D flags:
  FT_LEAKY=n    A1  leaky stability vote\n  FT_MUTE_K=k   A2  mute-thump threshold (production 0.3)\n  FT_YIN        H1  a short-window YIN-style period (pxt_period over the post-pick samples, first minimum below FT_YIN_THR) replaces the ERB candidate
                    while a pick is pending when the ERB candidate is missing or is an upper partial (ratio 2..5) of it.  Published idea: de Cheveigne &
                    Kawahara 2002 (first dip under a threshold, not the global minimum); here on the tracker's existing 2.76 kHz history."""
import shutil, os
root = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..')); dst = root + '/build/fasttrack/eng_root/engine'
shutil.rmtree(root + '/build/fasttrack/eng_root', ignore_errors=True); os.makedirs(root + '/build/fasttrack/eng_root'); shutil.copytree(root + '/engine', dst)
if not os.path.exists(root + '/build/fasttrack/eng_root/third_party'): os.symlink(root + '/third_party', root + '/build/fasttrack/eng_root/third_party')
p = dst + '/px_track.h'; s = open(p).read()
helper = r'''
#ifdef FT_YIN
#ifndef FT_YIN_THR
#define FT_YIN_THR 0.20f
#endif
#ifndef FT_YIN_W
#define FT_YIN_W 20
#endif
#ifndef FT_YIN_MINF
#define FT_YIN_MINF 90.0f
#endif
/* FastTrack H1: period of the post-pick waveform (dec history, 2.76 kHz). frames = wait_n. Returns Hz or 0. */
static float pxt_yin_pick(const px_track *t, int frames)
{
    const float rate = pxt_rate / 4.0f;
    const int recent = (frames - 1) * (PXT_HOP / 4), w = FT_YIN_W;
    int lmax = recent - w - 1; const int lcap = (int)(rate / FT_YIN_MINF); if (lmax > lcap) lmax = lcap;
    const int lmin = 5;
    if (lmax <= lmin + 1) return 0.0f;
    float prev = pxt_period(t, (float)lmin, w), cur = pxt_period(t, (float)(lmin + 1), w);
    for (int L = lmin + 1; L < lmax; L++) {
        const float next = pxt_period(t, (float)(L + 1), w);
        if (cur < FT_YIN_THR && cur <= prev && cur <= next) {                /* first dip under the threshold */
            const float den = prev - 2.0f * cur + next, off = den > 1e-9f ? 0.5f * (prev - next) / den : 0.0f;
            return rate / ((float)L + (off > 0.5f ? 0.5f : off < -0.5f ? -0.5f : off));
        }
        prev = cur; cur = next;
    }
    return 0.0f;
}
#endif
'''
s = s.replace("/* x: the guitar sample; framed: pxt_run returned 1 for it (t holds a new frame) */", helper + "/* x: the guitar sample; framed: pxt_run returned 1 for it (t holds a new frame) */", 1)
hook = "            L->wait_n++;\n"
assert hook in s
s = s.replace(hook, hook + r'''#ifdef FT_YIN
            { const float ty = pxt_yin_pick(t, L->wait_n);
              if (ty > 0.0f) {
                  const float q = m > 0.0f ? m / ty : 0.0f;
                  const int partial = q > 1.9f && q < 5.15f && (q - (float)(int)(q + 0.5f) < 0.08f * q) && ((float)(int)(q + 0.5f) - q < 0.08f * q);
                  if (m <= 0.0f || partial) m = ty;
              } }
#endif
''', 1)
old = "            if (m > 0.0f && pxt_near(m, L->stab_f, 30.0f)) L->stab_n++; else { L->stab_f = m; L->stab_n = m > 0.0f; }"
assert old in s
new = '''#ifdef FT_LEAKY
            /* FastTrack A1: a leaky stability vote (one odd frame costs FT_LEAKY votes, a reset only below 3 votes), as the slur vote already is */
            if (m > 0.0f && pxt_near(m, L->stab_f, 30.0f)) L->stab_n++;
            else if (L->stab_n >= 3) { L->stab_n -= FT_LEAKY; if (L->stab_n < 0) L->stab_n = 0; }
            else { L->stab_f = m; L->stab_n = m > 0.0f; }
#else
''' + old + '''
#endif'''
s = s.replace(old, new, 1)
open(p, 'w').write(s)
# FT_MUTE_K: the mute-thump rule of px_mono.h (a pick whose attack peak is under K x the peak before the mute is a thump, not a note); production K = 0.3 (-10.5 dB)
q = dst + '/px_mono.h'; m = open(q).read()
assert m.count("0.3f * m->mute_pk") == 2
m = m.replace('0.3f * m->mute_pk', 'FT_MUTE_K * m->mute_pk')
m = m.replace('#define PX_MONO_H\n', '#define PX_MONO_H\n#ifndef FT_MUTE_K\n#define FT_MUTE_K 0.3f\n#endif\n', 1)
open(q, 'w').write(m)

# ---- FT_HOOKS: diagnostics only (no behaviour change). Every place the legacy wrapper/adapter suppresses, delays or merges a note event reports a reason code:
#   H hold-undo of a weak repick   M mute-thump undo   S slur (onset turned into a pitch glide)   C 35 ms harmonic confirm hold   D dead-pick veto
#   P adapter: second note-on in one 32-sample block overwrote a pending one   Q adapter: release in the same block as the note-on (note-on then instant note-off)
#   F adapter: FOLLOW consumed the note-on (no envelope attack)   N/L/X: what the tracker told the synth (note-on / slur / release)
q = dst + '/px_mono.h'; m = open(q).read()
def rep(txt, old, new):
    assert txt.count(old) == 1, old
    return txt.replace(old, new)
m = rep(m, '#define PX_MONO_H\n', '#define PX_MONO_H\n#ifdef FT_HOOKS\nstatic long ft_now = 0; static void ft_why(char c, float f) { printf("W %.2f %c %.2f\\n", ft_now * 1000.0 / 44100.0, c, f); }\n#define FT_WHY(c, f) ft_why(c, f)\n#else\n#define FT_WHY(c, f) ((void)0)\n#endif\n')
m = rep(m, "            m->lay.onset = 0;\n            m->lay.f0 = f0_before;", "            FT_WHY('H', m->lay.f0); m->lay.onset = 0;\n            m->lay.f0 = f0_before;")
m = rep(m, "a thump, not a pick: undo the retrigger */\n            const pxt_layer n = m->lay;", "a thump, not a pick: undo the retrigger */\n            FT_WHY('M', m->lay.f0); const pxt_layer n = m->lay;")
m = rep(m, "{ slur_now = 1; m->lay.vel = vel_b;", "{ FT_WHY('S', m->lay.f0); slur_now = 1; m->lay.vel = vel_b;")
m = rep(m, "if (sus) { m->pend =", "if (sus) { FT_WHY('C', m->lay.f0); m->pend =")
m = rep(m, "        onset = 0; m->pend = 0; m->lay.f0 = m->f_ref > 0.0f ? m->f_ref : f0_before;", "        FT_WHY('D', m->lay.f0); onset = 0; m->pend = 0; m->lay.f0 = m->f_ref > 0.0f ? m->f_ref : f0_before;")
open(q, 'w').write(m)
a = dst + '/px_synth_adapter.h'; t = open(a).read()
t = rep(t, "        px_mono_run(e, &s->mono, in[k], &o);\n", "        px_mono_run(e, &s->mono, in[k], &o);\n        if (o.onset) FT_WHY('N', o.f0); if (o.slur) FT_WHY('L', o.f0); if (o.release) FT_WHY('X', (float)o.damp);\n        if (o.onset && s->pend_on) FT_WHY('P', o.f0);\n        if (o.release && s->pend_on) FT_WHY('Q', o.f0);\n")
t = rep(t, "        s->pick_accent = 0.25f * s->vel;\n        s->pend_on = 0;", "        s->pick_accent = 0.25f * s->vel; FT_WHY('F', s->f0);\n        s->pend_on = 0;")
open(a, 'w').write(t)

# FT_FOLLOW=0/1: force the adapter's FOLLOW articulation on/off for any patch (tracker decisions untouched: only what the synth does with a note-on changes)
t = open(a).read()
t = rep(t, "    s->type = type; s->arc = arc; s->patch = pa;", "#ifdef FT_FOLLOW\n    s->follow = FT_FOLLOW;\n#endif\n    s->type = type; s->arc = arc; s->patch = pa;")
open(a, 'w').write(t)
