/* Host prototype included by the adapter in a temporary copy of the engine. */
enum { CURRENT, ENVELOPE, GATE, NO_HOLD };
static int proto_mode, proto_onsets;
static float proto_envelope;

static void proto_track(const px_engine *e, px_s2 *s, float input, px_mono_out *o)
{
    px_mono *m = &s->mono;
    if (proto_mode == GATE) {
        if (m->lay.stabreq) {
            /* Square HQ uses the same estimator with the default note layer. */
            pxt_layer_init(&m->lay, e->sr);
            m->trk.sensitive_picks = 0;
        }
        pxt_layer_run(&m->lay, &m->trk, input, pxt_run(e, &m->trk, input));
        const int gate = m->lay.gate;
        *o = (px_mono_out){
            .gate = gate, .onset = gate && !m->was, .release = !gate && m->was,
            .f0 = m->lay.f0, .vel = m->lay.vel,
        };
        m->was = gate;
    } else {
        px_mono_run(e, m, input, o);
    }

    if (proto_mode == GATE || proto_mode == ENVELOPE) {
        const float level = px_abs(input);
        /* Calibrate the rectified envelope to the existing velocity scale. */
        const float ms = level > proto_envelope ? 0.002f : 0.020f;
        proto_envelope += (level - proto_envelope) / (ms * e->sr);
        o->expr = pxt_pick_velocity(1.6f * proto_envelope);
    }
    proto_onsets += o->onset;
}
