// sampler.hpp — wall-clock hrtimer sampling of the worker thread, weighted by
// the thread CPU time elapsed since the previous sample (so descheduled or
// blocked time does not count). Enabled by SAMPLE_OUT=<file>, period by
// SAMPLE_US (default 250). Writes binary records + /proc/self/maps at stop.
#pragma once
#include <signal.h>
#include <time.h>
#include <ucontext.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace samp {
struct Rec { uint64_t rip, ret; uint32_t cpu_ns; uint32_t phase; };
static Rec *buf = nullptr;
static size_t n = 0, cap = 0;
static volatile uint32_t phase = 0;
static const volatile uint16_t *owner_ptr = nullptr;
static uint64_t last_cpu = 0;
static timer_t timer;
static const char *out = nullptr;

static uint64_t thread_cpu_ns() {
  timespec ts;
  clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
  return uint64_t(ts.tv_sec) * 1000000000ull + ts.tv_nsec;
}

static void handler(int, siginfo_t *, void *uc_) {
  const int saved = errno;
  ucontext_t *uc = static_cast<ucontext_t *>(uc_);
  const uint64_t now = thread_cpu_ns();
  if (n < cap) {
    const uint64_t rip = uc->uc_mcontext.gregs[REG_RIP];
    const uint64_t rsp = uc->uc_mcontext.gregs[REG_RSP];
    buf[n++] = {rip, *reinterpret_cast<uint64_t *>(rsp), uint32_t(now - last_cpu), uint32_t((phase & 0xffffu) | (uint32_t(owner_ptr ? *owner_ptr : 0) << 16))};
  }
  last_cpu = now;
  errno = saved;
}

static void start() {
  out = std::getenv("SAMPLE_OUT");
  if (!out || !*out) return;
  long us = 250;
  if (const char *s = std::getenv("SAMPLE_US")) us = std::atol(s);
  cap = size_t(64) << 20; // 64M records max (reserved, touched on use)
  buf = static_cast<Rec *>(mmap(nullptr, cap * sizeof(Rec), PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0));
  struct sigaction sa;
  std::memset(&sa, 0, sizeof sa);
  sa.sa_sigaction = handler;
  sa.sa_flags = SA_SIGINFO | SA_RESTART;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGPROF, &sa, nullptr);
  sigevent sev;
  std::memset(&sev, 0, sizeof sev);
  sev.sigev_notify = SIGEV_THREAD_ID;
  sev.sigev_signo = SIGPROF;
  sev._sigev_un._tid = (pid_t)syscall(SYS_gettid);
  if (timer_create(CLOCK_MONOTONIC, &sev, &timer)) { perror("timer_create"); return; }
  last_cpu = thread_cpu_ns();
  itimerspec its;
  its.it_interval.tv_sec = 0;
  its.it_interval.tv_nsec = us * 1000;
  its.it_value = its.it_interval;
  timer_settime(timer, 0, &its, nullptr);
}

static void stop() {
  if (!buf) return;
  itimerspec its{};
  timer_settime(timer, 0, &its, nullptr);
  FILE *f = std::fopen(out, "wb");
  if (!f) return;
  // header: "SAMP1\n<maps bytes>\n<maps text><records>"
  char maps[1 << 16];
  size_t ml = 0;
  if (FILE *m = std::fopen("/proc/self/maps", "r")) { ml = std::fread(maps, 1, sizeof maps, m); std::fclose(m); }
  std::fprintf(f, "SAMP1\n%zu\n", ml);
  std::fwrite(maps, 1, ml, f);
  std::fwrite(buf, sizeof(Rec), n, f);
  std::fclose(f);
}
} // namespace samp
