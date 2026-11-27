/* Host prototype included by the adapter in a temporary copy of the engine. */
enum { CURRENT, ENVELOPE, GATE, NO_HOLD, SQUARE_EDGES };
static int proto_mode, proto_onsets;
static float proto_envelope;

/* The original SFX Square comparator, with a period measured between rising edges. */
static px_bq edge_filter;
static float edge_env, edge_pitch, edge_attack, edge_release;
static int edge_sign, edge_age;

static void proto_square_edges(const px_engine *e, px_s2 *s, float input, px_mono_out *o)
{
    const float level = px_abs(input);
    edge_env += (level - edge_env) * (level > edge_env ? edge_attack : edge_release);
    const float filtered = bq_run(&edge_filter, input);
    const float threshold = 0.1f * edge_env;
    const int rising = edge_sign <= 0 && filtered > threshold;
    if (rising) edge_sign = 1;
    else if (edge_sign > 0 && filtered < -threshold) edge_sign = -1;

    /* The original output has no gate. Use recent crossings to release the
     * oscillator voices after a mute, without holding the 150 ms level tail. */
    const int timeout = (int)(0.025f * e->sr);
    if (edge_age <= timeout) ++edge_age;
    if (rising && edge_env > 0.001f) {
        if (edge_age <= timeout && edge_age >= (int)(e->sr / 1600.0f))
            edge_pitch = e->sr / (float)edge_age;
        edge_age = 0;
    }
    if (edge_age >= timeout) edge_pitch = 0.0f;

    const int gate = edge_pitch > 0.0f;
    const float expression = pxt_pick_velocity(1.6f * edge_env);
    *o = (px_mono_out){
        .gate = gate, .onset = gate && !s->mono.was, .release = !gate && s->mono.was,
        .f0 = edge_pitch, .vel = expression, .expr = expression,
    };
    s->mono.was = gate;
}

static void proto_track(const px_engine *e, px_s2 *s, float input, px_mono_out *o)
{
    px_mono *m = &s->mono;
    if (proto_mode == SQUARE_EDGES) {
        proto_square_edges(e, s, input, o);
    } else if (proto_mode == GATE) {
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
