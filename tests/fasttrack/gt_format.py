"""FastTrack ground-truth annotation format (v1).  One `recordings.csv` and one `events.csv` per recording set.

events.csv   recording,t_ms,action,pitch_midi,string,fret,expected_event,confidence,timing_basis,end_pitch_midi,duration_ms,notes
  t_ms           the ACTUAL ACOUSTIC onset (first waveform rise of the event) in ms from file start, as measured/verified on the recording.  NEVER the metronome/intended time.
  action         what the guitarist physically did (PHYSICAL, not what a detector reports):
                   PICK            plucked/picked a new note (open or fretted)
                   REPICK          plucked the same pitch again while it still rings
                   HAMMER_ON       a fretting-hand strike raises the pitch of a ringing string, no pluck
                   PULL_OFF        a fretting-hand pull lowers the pitch of a ringing string, no pluck
                   SLIDE           the fretting finger moves along the string, pitch moves continuously (end_pitch_midi)
                   BEND            string pushed/pulled: continuous pitch change (end_pitch_midi, duration_ms)
                   VIBRATO         periodic pitch modulation of a ringing note (duration_ms)
                   PALM_MUTE_PICK  a pluck while the picking-hand palm mutes the string
                   HARD_MUTE       deliberate rapid damping of the ringing strings (hand/finger), no new note intended
                   NOISE           an acoustic event that is NOT a note: fret noise, string contact, pick scrape, mute thump with no note intended
                   LIFT            the note is released naturally (finger lifted / note ends)
  expected_event the synth response a correct system should give (EVALUATION target, not a detector rule):  NOTE_ON, REPICK, LEGATO_CHANGE, PITCH_UPDATE, DAMP, NOTE_OFF, NONE
  confidence     0..1 of THIS annotation (timing and class together).  <0.5 events are reported separately and excluded from the headline score.
  timing_basis   waveform | spectrogram | listening | auto_refined | source_annotation | intended   ('intended' is only allowed with confidence <= 0.3)
recordings.csv  recording,path,sample_rate,bits,channels,signal,guitar,pickup,player,exercise_id,intended_tempo_bpm,split,processing,complete_annotation,licence,notes
  signal         DI (required for the development set) | mic | pickup_acoustic
  processing     must be 'none' for DI claims (no compression, gate, distortion, pitch processing)
  split          dev | holdout | calibration.  A detector must never be trained and evaluated on the same performances; 'holdout' recordings are not to be looked at during development.
  complete_annotation  1 if every acoustic event on the recording is annotated (needed to count false events per minute)"""
ACTIONS = ['PICK', 'REPICK', 'HAMMER_ON', 'PULL_OFF', 'SLIDE', 'BEND', 'VIBRATO', 'PALM_MUTE_PICK', 'HARD_MUTE', 'NOISE', 'LIFT']
EXPECTED = ['NOTE_ON', 'REPICK', 'LEGATO_CHANGE', 'PITCH_UPDATE', 'DAMP', 'NOTE_OFF', 'NONE']
DEFAULT_EXPECTED = dict(PICK='NOTE_ON', REPICK='REPICK', HAMMER_ON='LEGATO_CHANGE', PULL_OFF='LEGATO_CHANGE', SLIDE='PITCH_UPDATE', BEND='PITCH_UPDATE', VIBRATO='PITCH_UPDATE',
                        PALM_MUTE_PICK='NOTE_ON', HARD_MUTE='DAMP', NOISE='NONE', LIFT='NOTE_OFF')
PICK_LIKE = ('PICK', 'REPICK', 'PALM_MUTE_PICK')
TIMING = ['waveform', 'spectrogram', 'listening', 'auto_refined', 'source_annotation', 'intended']
SIGNALS = ['DI', 'mic', 'pickup_acoustic']; SPLITS = ['dev', 'holdout', 'calibration']
EV_COLS = 'recording,t_ms,action,pitch_midi,string,fret,expected_event,confidence,timing_basis,end_pitch_midi,duration_ms,notes'.split(',')
REC_COLS = 'recording,path,sample_rate,bits,channels,signal,guitar,pickup,player,exercise_id,intended_tempo_bpm,split,processing,complete_annotation,licence,notes'.split(',')
IOI_EDGES = [0, 40, 60, 80, 125, 200, 1e9]; IOI_LABELS = ['<40 ms', '40-60 ms', '60-80 ms', '80-125 ms (8-12.5/s)', '125-200 ms (5-8/s)', '>200 ms']
def ioi_bin(x):
    for k in range(6):
        if IOI_EDGES[k] <= x < IOI_EDGES[k + 1]: return IOI_LABELS[k]
