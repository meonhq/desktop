// stallprobe: 오디오 콜백 멈춤 측정 (Linux, JACK). LD_PRELOAD 로 앱에 붙인다. 앱 코드는 바꾸지 않는다.
// - JUCE 가 dlopen 한 libjack 의 jack_set_process_callback 을 가로채 오디오 콜백 시간을 잰다.
// - 콜백이 STALLPROBE_MS(기본 10) 넘게 안 끝나면 그 순간 프로세스의 모든 스레드 호출 스택을 찍는다
//   (같은 멈춤 동안 STALLPROBE_EVERY_MS(기본 20) 마다 다시). 오디오 스레드가 어디서 기다리는지 보인다.
// - 10초마다 콜백 수·최대 시간·구간별 개수를 한 줄로 남긴다 (summarize.py 가 읽는다).
// - 기록: STALLPROBE_LOG=파일 (없으면 표준 오류).
// E10(엔진 잠금) 뒤에도 AOO 내부 잠금(pthread_rwlock) 등에서 멈추면 스택에 그대로 나온다. README.md 참고.
// 빌드: gcc -O2 -g -fPIC -shared -o stallprobe.so stallprobe.c -ldl -lpthread
#define _GNU_SOURCE
#include <dlfcn.h>
#include <execinfo.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>

typedef int (*JackProcessCallback)(uint32_t nframes, void* arg);
typedef int (*set_cb_fn)(void* client, JackProcessCallback cb, void* arg);
typedef void* (*dlsym_fn)(void*, const char*);

static dlsym_fn real_dlsym;
static set_cb_fn real_set_cb;
static JackProcessCallback orig_cb;
static void* orig_arg;

static FILE* logf_;
static int stall_ms = 10, every_ms = 20;
static _Atomic int64_t cb_start;      // 0 = 콜백 밖
static _Atomic int audio_tid;
static _Atomic uint64_t n_calls, n_over[10];
static _Atomic int64_t max_ns;
static const int64_t edges_us[9] = { 1000, 2667, 5000, 10000, 20000, 50000, 100000, 500000, 1000000 };

#define MAXT 256
#define MAXF 48
struct slot { _Atomic int tid; _Atomic int n; void* f[MAXF]; };
static struct slot slots[MAXT];
static int sig_no;

static int64_t now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (int64_t)t.tv_sec * 1000000000LL + t.tv_nsec; }
static int my_tid(void) { return (int)syscall(SYS_gettid); }

static void on_sig(int s, siginfo_t* si, void* uc)
{
    (void)s; (void)si; (void)uc;
    int t = my_tid();
    for (int i = 0; i < MAXT; ++i)
        if (atomic_load(&slots[i].tid) == t) { int n = backtrace(slots[i].f, MAXF); atomic_store(&slots[i].n, n); return; }
}

static int trampoline(uint32_t nframes, void* arg)
{
    if (audio_tid == 0) audio_tid = my_tid();
    int64_t t0 = now_ns();
    atomic_store(&cb_start, t0);
    int r = orig_cb(nframes, arg);
    int64_t d = now_ns() - t0;
    atomic_store(&cb_start, 0);
    n_calls++;
    if (d > max_ns) max_ns = d;
    for (int i = 0; i < 9; ++i) if (d >= edges_us[i] * 1000) n_over[i]++;
    return r;
}

static int my_set_cb(void* client, JackProcessCallback cb, void* arg)
{
    orig_cb = cb; orig_arg = arg;
    fprintf(logf_, "[probe] jack_set_process_callback 가로챔\n"); fflush(logf_);
    return real_set_cb(client, trampoline, arg);
}

void* dlsym(void* h, const char* name)
{
    if (!real_dlsym) {
        real_dlsym = (dlsym_fn)dlvsym(RTLD_NEXT, "dlsym", "GLIBC_2.34");
        if (!real_dlsym) real_dlsym = (dlsym_fn)dlvsym(RTLD_NEXT, "dlsym", "GLIBC_2.2.5");
    }
    void* p = real_dlsym(h, name);
    if (p && strcmp(name, "jack_set_process_callback") == 0) { real_set_cb = (set_cb_fn)p; return (void*)my_set_cb; }
    return p;
}

static void thread_name(int tid, char* out, size_t n)
{
    char path[64]; snprintf(path, sizeof path, "/proc/self/task/%d/comm", tid);
    FILE* f = fopen(path, "r"); out[0] = 0;
    if (f) { if (fgets(out, (int)n, f)) out[strcspn(out, "\n")] = 0; fclose(f); }
}

/** /proc/self/task/T/stat 에서 상태·마지막 CPU·정책 */
static void thread_sched(int tid, char* out, size_t n)
{
    char path[64]; snprintf(path, sizeof path, "/proc/self/task/%d/stat", tid);
    FILE* f = fopen(path, "r"); out[0] = 0; if (!f) return;
    char buf[1024]; size_t r = fread(buf, 1, sizeof buf - 1, f); fclose(f); buf[r] = 0;
    char* p = strrchr(buf, ')'); if (!p) return; p += 2;
    char st = *p; int field = 3; long cpu = -1, rtprio = 0, policy = 0;
    for (char* q = p; *q; ++q) if (*q == ' ') { field++; long v = atol(q + 1); if (field == 39) cpu = v; else if (field == 40) rtprio = v; else if (field == 41) policy = v; }
    const char* pol = policy == 1 ? "FIFO" : policy == 2 ? "RR" : policy == 5 ? "IDLE" : policy == 3 ? "BATCH" : "OTHER";
    snprintf(out, n, "상태=%c cpu=%ld %s%s%ld", st, cpu, pol, rtprio ? " rt=" : "", rtprio ? rtprio : 0L);
}

static void dump_all(int64_t since_ns, int64_t stall_ns)
{
    // 슬롯에 스레드 번호를 적고 신호를 보낸다
    DIR* d = opendir("/proc/self/task"); if (!d) return;
    int k = 0; struct dirent* e;
    while ((e = readdir(d)) && k < MAXT) {
        int t = atoi(e->d_name); if (t <= 0) continue;
        atomic_store(&slots[k].n, 0); atomic_store(&slots[k].tid, t); k++;
    }
    closedir(d);
    static char pre[MAXT][96];
    for (int i = 0; i < k; ++i) thread_sched(slots[i].tid, pre[i], sizeof pre[i]);
    for (int i = 0; i < k; ++i) syscall(SYS_tgkill, getpid(), slots[i].tid, sig_no);
    for (int w = 0; w < 50; ++w) { int done = 1; for (int i = 0; i < k; ++i) if (!slots[i].n) done = 0; if (done) break; usleep(1000); }
    fprintf(logf_, "\n=== 멈춤 %.1f ms 째 (콜백 시작 t=%.3f s) 오디오 스레드 %d ===\n", stall_ns / 1e6, since_ns / 1e9, audio_tid);
    for (int pass = 0; pass < 2; ++pass)   // 오디오 스레드 먼저
        for (int i = 0; i < k; ++i) {
            int t = slots[i].tid; if ((pass == 0) != (t == audio_tid)) continue;
            int n = slots[i].n; char nm[64]; thread_name(t, nm, sizeof nm);
            fprintf(logf_, "--- 스레드 %d [%s]%s {%s}\n", t, nm, t == audio_tid ? " (오디오)" : "", pre[i]);
            if (n > 0) { char** s = backtrace_symbols(slots[i].f, n); for (int j = 2; j < n; ++j) fprintf(logf_, "  %s\n", s ? s[j] : "?"); free(s); }
        }
    for (int i = 0; i < k; ++i) atomic_store(&slots[i].tid, 0);
    fflush(logf_);
}

static void* watchdog(void* arg)
{
    (void)arg;
    int64_t last_dump_for = 0, last_dump_at = 0, last_sum = now_ns();
    for (;;) {
        usleep(1000);
        int64_t s = atomic_load(&cb_start), now = now_ns();
        if (s && now - s > (int64_t)stall_ms * 1000000) {
            if (s != last_dump_for || now - last_dump_at > (int64_t)every_ms * 1000000) {
                dump_all(s, now - s); last_dump_for = s; last_dump_at = now_ns();
            }
        }
        if (now - last_sum > 10000000000LL) {
            last_sum = now;
            fprintf(logf_, "[probe] t=%.0f s 콜백 %llu번, 최대 %.2f ms, ≥1ms %llu ≥2.67ms %llu ≥5ms %llu ≥10ms %llu ≥20ms %llu ≥50ms %llu ≥100ms %llu ≥500ms %llu\n",
                    now / 1e9, (unsigned long long)n_calls, max_ns / 1e6,
                    (unsigned long long)n_over[0], (unsigned long long)n_over[1], (unsigned long long)n_over[2], (unsigned long long)n_over[3],
                    (unsigned long long)n_over[4], (unsigned long long)n_over[5], (unsigned long long)n_over[6], (unsigned long long)n_over[7]);
            fflush(logf_);
        }
    }
    return NULL;
}

__attribute__((constructor)) static void init(void)
{
    const char* p = getenv("STALLPROBE_LOG");
    logf_ = p ? fopen(p, "a") : stderr; if (!logf_) logf_ = stderr;
    if (getenv("STALLPROBE_MS")) stall_ms = atoi(getenv("STALLPROBE_MS"));
    if (getenv("STALLPROBE_EVERY_MS")) every_ms = atoi(getenv("STALLPROBE_EVERY_MS"));
    void* warm[4]; backtrace(warm, 4);   // libgcc 를 미리 올려 둔다 (신호 처리기 안에서 처음 부르지 않게)
    sig_no = SIGRTMIN + 7;
    struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_sigaction = on_sig; sa.sa_flags = SA_SIGINFO | SA_RESTART; sigemptyset(&sa.sa_mask);
    sigaction(sig_no, &sa, NULL);
    pthread_t th; pthread_create(&th, NULL, watchdog, NULL); pthread_detach(th);
    fprintf(logf_, "[probe] 시작 pid %d, 멈춤 기준 %d ms\n", getpid(), stall_ms); fflush(logf_);
}
