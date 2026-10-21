// jamprobe: 합주 소리 끊김 측정 (Linux, JACK 클라이언트). 앱 코드는 바꾸지 않는다.
//   tone → 한 앱의 입력 (상대에게 보낼 내 소리, 사인파)
//   recv ← 다른 앱(또는 같은 서버의 상대 앱)의 출력 (상대에게서 받은 소리)
// 끊김 두 가지:
//   무음 = 소리가 오던 뒤 |x| < 0.01 이 2 ms 넘게 이어짐 (네트워크·지터 버퍼가 빔, 상대가 멈춤)
//   멈춤 = 소리가 있는 블록이 바로 앞 블록과 완전히 같음 (앱이 블록을 못 써서 JACK 이 지난 버퍼를 다시 냄)
// 시각은 CLOCK_MONOTONIC 초 (stallprobe 기록과 맞춰 보기 위해).
//
// 사용: jamprobe <JACK 서버 이름> <초> [표시 이름] [사인파 Hz] [클라이언트 이름]
//   한 서버에 둘 이상 띄울 때는 클라이언트 이름을 다르게 준다 (포트: <이름>:tone, <이름>:recv).
// 빌드: gcc -O2 -g -o jamprobe jamprobe.c -ljack -lm
#include <jack/jack.h>
#include <math.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define MAXB 4096
#define MAXE 8192
static double sr = 48000;
static volatile sig_atomic_t stop;
static _Atomic uint64_t xruns;
static double mono(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }

static jack_port_t *tone_port, *recv_port;
static const char* label = "받은 소리";
static double freq = 440, phase;
static int64_t quiet, frames, signal_at = -1;
static float prev[MAXB]; static int prevn; static int64_t stuck_run;
static _Atomic int ne; static char kind[MAXE]; static double at[MAXE], ms[MAXE];

static void add_event(char k, double t, double m)
{
    int n = ne; if (n >= MAXE) return;
    kind[n] = k; at[n] = t; ms[n] = m; ne = n + 1;
}

static int process(jack_nframes_t n, void* arg)
{
    (void)arg;
    double now = mono();
    float* o = jack_port_get_buffer(tone_port, n);
    const float* in = jack_port_get_buffer(recv_port, n);
    for (jack_nframes_t i = 0; i < n; ++i) { o[i] = (float)(0.3 * sin(phase)); phase += 2 * M_PI * freq / sr; if (phase > 2 * M_PI) phase -= 2 * M_PI; }
    // 멈춘 버퍼: 소리가 있는 블록이 앞 블록과 똑같다
    int loud = 0; for (jack_nframes_t i = 0; i < n; ++i) if (fabsf(in[i]) > 0.01f) { loud = 1; break; }
    int same = loud && prevn == (int)n && memcmp(prev, in, n * sizeof(float)) == 0;
    if (same) stuck_run++;
    else { if (stuck_run > 0) add_event('S', now - stuck_run * n / sr, stuck_run * n * 1000.0 / sr); stuck_run = 0; }
    if (n <= MAXB) { memcpy(prev, in, n * sizeof(float)); prevn = n; }
    // 무음 (소리가 3초 넘게 온 뒤부터 센다: 처음 연결 제외)
    for (jack_nframes_t i = 0; i < n; ++i) {
        if (fabsf(in[i]) < 0.01f) quiet++;
        else {
            if (signal_at < 0) signal_at = frames + i;
            if (quiet > (int64_t)(sr * 0.002) && frames + i - signal_at > sr * 3)
                add_event('Q', now + (double)i / sr - quiet / sr, quiet * 1000.0 / sr);
            quiet = 0;
        }
    }
    frames += n;
    return 0;
}

static int on_xrun(void* arg) { (void)arg; xruns++; return 0; }
static void on_sig(int s) { (void)s; stop = 1; }

int main(int argc, char** argv)
{
    const char* server = argc > 1 ? argv[1] : "default";
    double secs = argc > 2 ? atof(argv[2]) : 60;
    if (argc > 3) label = argv[3];
    if (argc > 4) freq = atof(argv[4]);
    const char* cname = argc > 5 ? argv[5] : "probe";
    jack_client_t* c = jack_client_open(cname, JackNoStartServer | JackServerName | JackUseExactName, NULL, server);
    if (!c) { fprintf(stderr, "jack 연결 실패 (%s, %s)\n", server, cname); return 1; }
    sr = jack_get_sample_rate(c);
    tone_port = jack_port_register(c, "tone", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    recv_port = jack_port_register(c, "recv", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
    jack_set_process_callback(c, process, NULL);
    jack_set_xrun_callback(c, on_xrun, NULL);
    signal(SIGINT, on_sig); signal(SIGTERM, on_sig);
    jack_activate(c);
    printf("PROBE start t=%.3f sr=%.0f bs=%u server=%s client=%s\n", mono(), sr, jack_get_buffer_size(c), server, cname); fflush(stdout);
    double t0 = mono(), last = t0; int printed = 0;
    while (!stop && mono() - t0 < secs) {
        usleep(100000);
        for (int n = ne; printed < n; ++printed)
            printf("EV %s %s t=%.3f %.1f ms\n", label, kind[printed] == 'S' ? "멈춤" : "무음", at[printed], ms[printed]);
        fflush(stdout);
        if (mono() - last > 10) { last = mono(); printf("PROBE t=%.0f xrun %llu, %s\n", last, (unsigned long long)xruns, signal_at >= 0 ? "소리 받음" : "아직"); fflush(stdout); }
    }
    double qs = 0, qm = 0, ss = 0, sm = 0; int qn = 0, sn = 0;
    for (int i = 0; i < ne; ++i) {
        if (kind[i] == 'Q') { qn++; qs += ms[i]; if (ms[i] > qm) qm = ms[i]; }
        else { sn++; ss += ms[i]; if (ms[i] > sm) sm = ms[i]; }
    }
    printf("PROBE END %s secs=%.0f 무음 %d번 합 %.1f ms 최대 %.1f ms | 멈춤 %d번 합 %.1f ms 최대 %.1f ms | signal=%d\n",
           label, mono() - t0, qn, qs, qm, sn, ss, sm, signal_at >= 0);
    printf("PROBE END xruns=%llu\n", (unsigned long long)xruns);
    jack_client_close(c);
    return 0;
}
