// A stand-in for the C3 controller on an ALSA sequencer port (Linux), for
// testing SignalPatch's side of docs/CONTROLLER.md without the hardware:
// says hello as model 0x02, answers the settings SysEx, prints what
// SignalPatch sends, and plays the commands given on stdin once SignalPatch
// has answered: "cc <n> <v>", "note <n>" (press and release),
// "wait <seconds>", "quit".
//
//   gcc -O1 -Wall -o fakec3 fakec3.c -lasound
//   printf 'wait 2\ncc 20 66\nnote 63\n' | ./fakec3
#include <alsa/asoundlib.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

static snd_seq_t* seq; static int port;
static int settings[24] = { 10,16,17,18,11,12,13,20,60,61,62,63,1,0,3,30,4,1,0,1,100,0,0,0 };
static const int defaults[24] = { 10,16,17,18,11,12,13,20,60,61,62,63,1,0,3,30,4,1,0,1,100,0,0,0 };
static int answered = 0;

static double now (void) { struct timespec t; clock_gettime (CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

static void sendEvent (snd_seq_event_t* ev)
{
    snd_seq_ev_set_source (ev, port); snd_seq_ev_set_subs (ev); snd_seq_ev_set_direct (ev);
    snd_seq_event_output_direct (seq, ev);
}
static void sysex (const unsigned char* data, int length)
{
    snd_seq_event_t ev; snd_seq_ev_clear (&ev); snd_seq_ev_set_sysex (&ev, length, (void*) data); sendEvent (&ev);
}
static void hello (void) { const unsigned char m[] = { 0xF0,0x7D,0x53,0x7F,0x00,0x02,0x01,0x00,0xF7 }; sysex (m, sizeof m); }
static void dump (void)
{
    unsigned char m[4 + 48 + 1]; int n = 0;
    m[n++] = 0xF0; m[n++] = 0x7D; m[n++] = 0x53; m[n++] = 0x11;
    for (int k = 0; k < 24; ++k) { m[n++] = (unsigned char) k; m[n++] = (unsigned char) settings[k]; }
    m[n++] = 0xF7; sysex (m, n);
}
static void received (const unsigned char* d, int len)
{
    printf ("<-"); for (int i = 0; i < len; ++i) printf (" %02X", d[i]);
    if (len > 4 && d[3] >= 0x01 && d[3] <= 0x08 && d[3] != 0x03) { printf ("   \""); for (int i = 5; i < len - 1; ++i) if (d[i] >= 32 && d[i] < 127) putchar (d[i]); printf ("\""); }
    printf ("\n"); fflush (stdout);
    if (len < 5 || d[0] != 0xF0 || d[1] != 0x7D || d[2] != 0x53) return;
    if (d[3] == 0x7F && d[4] == 0x01) answered = 1;
    if (d[3] == 0x10) dump();
    if (d[3] == 0x12) { for (int i = 4; i + 1 < len - 1; i += 2) if (d[i] < 24) settings[d[i]] = d[i + 1]; dump(); }
    if (d[3] == 0x13) { memcpy (settings, defaults, sizeof settings); dump(); }
}

int main (void)
{
    if (snd_seq_open (&seq, "default", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK) < 0) return 1;
    snd_seq_set_client_name (seq, "SignalPatch C3");
    port = snd_seq_create_simple_port (seq, "SignalPatch C3",
        SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ | SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
        SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    printf ("fake C3 on %d:%d\n", snd_seq_client_id (seq), port); fflush (stdout);
    double nextHello = 0, resume = 0; char line[256];
    int stdinOpen = 1;
    for (;;)
    {
        snd_seq_event_t* ev = NULL;
        while (snd_seq_event_input (seq, &ev) >= 0 && ev != NULL)
        {
            if (ev->type == SND_SEQ_EVENT_SYSEX) received ((const unsigned char*) ev->data.ext.ptr, (int) ev->data.ext.len);
            ev = NULL;
        }
        if (! answered && now() > nextHello) { hello(); nextHello = now() + 2.0; }
        if (stdinOpen && answered && now() > resume)
        {
            struct pollfd p = { 0, POLLIN, 0 };
            if (poll (&p, 1, 0) > 0)
            {
                if (! fgets (line, sizeof line, stdin)) { stdinOpen = 0; continue; }
                int a = 0, b = 0; double w = 0;
                snd_seq_event_t out; snd_seq_ev_clear (&out);
                if (sscanf (line, "cc %d %d", &a, &b) == 2) { snd_seq_ev_set_controller (&out, settings[0] - 1, a, b); sendEvent (&out); }
                else if (sscanf (line, "note %d", &a) == 1)
                { snd_seq_ev_set_noteon (&out, settings[0] - 1, a, 100); sendEvent (&out); usleep (30000);
                  snd_seq_ev_clear (&out); snd_seq_ev_set_noteoff (&out, settings[0] - 1, a, 0); sendEvent (&out); }
                else if (sscanf (line, "wait %lf", &w) == 1) resume = now() + w;
                else if (strncmp (line, "quit", 4) == 0) break;
            }
        }
        usleep (5000);
    }
    snd_seq_close (seq);
    return 0;
}
