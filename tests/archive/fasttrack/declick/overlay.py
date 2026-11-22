"""Generate a host adapter overlay. Production source is only read."""
from pathlib import Path
import shutil
import sys

root = Path(__file__).resolve().parents[4]
out = Path(sys.argv[1]).resolve()
out.mkdir(parents=True, exist_ok=True)
for name in ("engine", "third_party"):
    destination = out / name
    if not destination.exists():
        if name == "engine":
            shutil.copytree(root / name, destination)
        else:
            destination.symlink_to(root / name, target_is_directory=True)

path = out / "engine/px_synth_adapter.h"
source = (root / "engine/px_synth_adapter.h").read_text()


def replace(old, new):
    global source
    if source.count(old) != 1:
        raise ValueError(f"adapter changed: {old}")
    source = source.replace(old, new)


replace("static inline float px_s2_tms",
        '#include "voice_overlay.h"\n\nstatic inline float px_s2_tms')
replace("    s->type = type; s->arc = arc;",
        "    dc_clear();\n    s->type = type; s->arc = arc;")
replace("    if (!sounding) *env = 0.0f;",
        """    if (dc_coefficient && (sounding || (dc_steal && vi == 0)))
        declick_replace(&dc_state[vi]);
    if (!sounding) *env = 0.0f;""")
replace("    if (!v->active) { *env = 0.0f; *last_amp = 0.0f; return; }",
        "    if (!v->active) { declick_reset(&dc_state[vi]); *env = 0.0f; *last_amp = 0.0f; return; }")
replace("if (!v->active) { *env = 0.0f; *stage = 0; *last_amp = 0.0f; return; }",
        "if (!v->active) { declick_reset(&dc_state[vi]); *env = 0.0f; *stage = 0; *last_amp = 0.0f; return; }")
replace("    if (s->patch->noise) px_s2_noise(t, v, s->out, CTL, &m); else s->eng->render(t, v, s->out, CTL, &m);",
        """    int32_t *mix = s->out;
    if (dc_coefficient) {
        memset(dc_voice, 0, sizeof dc_voice);
        mix = dc_voice;
    }
    if (s->patch->noise) px_s2_noise(t, v, mix, CTL, &m); else s->eng->render(t, v, mix, CTL, &m);
    if (dc_coefficient) dc_mix(s, vi);
    if (!v->active) declick_reset(&dc_state[vi]);""")
replace("    memset(s->out, 0, sizeof s->out);",
        "    memset(s->out, 0, sizeof s->out);\n    memset(dc_tail, 0, sizeof dc_tail);\n    dc_applied = dc_replaced = 0;")
replace("s->ring[i] = (float)s->out[i] * g;",
        "s->ring[i] = (float)s->out[i] * g + dc_tail[i];")
path.write_text(source)
