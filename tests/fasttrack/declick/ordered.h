#ifndef FT_DECLICK_ORDERED_H
#define FT_DECLICK_ORDERED_H

/* Host delivery prototype. Include after ft_ctrl.h. It consumes events
 * synchronously, in input order, independently of synthesis block flags.
 * No queue, capacity limit or asynchronous producer is involved. */
typedef struct {
    unsigned starts;
    unsigned releases;
    unsigned damps;
    int gate;
    float pitch;
} ordered_state;

static void ordered_deliver(ordered_state *s, unsigned events, float pitch)
{
    /* For a single source frame with both flags, release precedes attack. */
    if (events & (FTC_NOTE_OFF | FTC_DAMP)) {
        s->gate = 0;
        ++s->releases;
        s->damps += !!(events & FTC_DAMP);
    }
    if (events & (FTC_NOTE_ON | FTC_REPICK)) {
        s->gate = 1;
        ++s->starts;
    }
    if (events & (FTC_NOTE_ON | FTC_REPICK | FTC_LEGATO | FTC_PITCH_UPDATE))
        s->pitch = pitch;
}
#endif
