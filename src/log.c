/*
 * Copyright (c) 2025 Reza Jelveh
 * Copyright (c) 2020 rxi
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "log.h"

#define MAX_CALLBACKS 32
#define SYS_WRITE 0x05

typedef struct {
  log_LogFn fn;
  void *udata;
  int level;
} Callback;

static struct {
  void *udata;
  log_LockFn lock;
  int level;
  bool quiet;
  Callback callbacks[MAX_CALLBACKS];
} L;


static const char *level_strings[] = {
  "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"
};

static int __semihost(int op, void* arg) {
  int result;
  #ifdef __thumb__
  __asm volatile (
    "mov r0, %1\n"
    "mov r1, %2\n"
    "bkpt 0xAB\n"
    "mov %0, r0"
    : "=r" (result)
    : "r" (op), "r" (arg)
    : "r0", "r1", "memory"
  );
  #else
  __asm volatile (
    "mov r0, %1\n"
    "mov r1, %2\n"
    "svc 0x123456\n"
    "mov %0, r0"
    : "=r" (result)
    : "r" (op), "r" (arg)
    : "r0", "r1", "memory"
  );
  #endif
  return result;
}

static void semihosting_write(const char* buf, int length) {
  struct {
    int fd;
    const void* buf;
    int length;
  } args;

  args.fd = 1;
  args.buf = buf;
  args.length = length;
  __semihost(SYS_WRITE, &args);
}

static void stdout_callback(log_Event *ev) {
  static char buf[LOG_BUFFER_SIZE];
  int pos = 0;

  pos += strftime(buf + pos, sizeof(buf) - pos, "%H:%M:%S", ev->time);
  if (pos < sizeof(buf) - 1) buf[pos++] = ' ';

  const char* level_str = level_strings[ev->level];
  while (*level_str && pos < sizeof(buf) - 1) {
    buf[pos++] = *level_str++;
  }
  if (pos < sizeof(buf) - 1) buf[pos++] = ' ';

  const char* file = ev->file;
  while (*file && pos < sizeof(buf) - 1) {
    buf[pos++] = *file++;
  }
  if (pos < sizeof(buf) - 1) buf[pos++] = ':';

  pos += snprintf(buf + pos, sizeof(buf) - pos, "%d", ev->line);
  if (pos < sizeof(buf) - 1) buf[pos++] = ':';
  if (pos < sizeof(buf) - 1) buf[pos++] = ' ';

  if (pos < sizeof(buf)) {
    int remaining = sizeof(buf) - pos;
    int msg_len = vsnprintf(buf + pos, remaining, ev->fmt, ev->ap);
    if (msg_len >= 0) {
      pos += (msg_len < remaining) ? msg_len : (remaining - 1);
    }
  }

  if (pos >= sizeof(buf) - 1) {
    pos = sizeof(buf) - 2;
  }
  buf[pos++] = '\n';

  semihosting_write(buf, pos);
}


static void lock(void)   {
  if (L.lock) { L.lock(true, L.udata); }
}


static void unlock(void) {
  if (L.lock) { L.lock(false, L.udata); }
}


const char* log_level_string(int level) {
  return level_strings[level];
}


void log_set_lock(log_LockFn fn, void *udata) {
  L.lock = fn;
  L.udata = udata;
}


void log_set_level(int level) {
  L.level = level;
}


void log_set_quiet(bool enable) {
  L.quiet = enable;
}


int log_add_callback(log_LogFn fn, void *udata, int level) {
  for (int i = 0; i < MAX_CALLBACKS; i++) {
    if (!L.callbacks[i].fn) {
      L.callbacks[i] = (Callback) { fn, udata, level };
      return 0;
    }
  }
  return -1;
}


static void init_event(log_Event *ev, void *udata) {
  if (!ev->time) {
    time_t t = time(NULL);
    ev->time = localtime(&t);
  }
  ev->udata = udata;
}


void log_log(int level, const char *file, int line, const char *fmt, ...) {
  log_Event ev = {
    .fmt   = fmt,
    .file  = file,
    .line  = line,
    .level = level,
  };

  lock();

  if (!L.quiet && level >= L.level) {
    init_event(&ev, NULL);
    va_start(ev.ap, fmt);
    stdout_callback(&ev);
    va_end(ev.ap);
  }

  for (int i = 0; i < MAX_CALLBACKS && L.callbacks[i].fn; i++) {
    Callback *cb = &L.callbacks[i];
    if (level >= cb->level) {
      init_event(&ev, cb->udata);
      va_start(ev.ap, fmt);
      cb->fn(&ev);
      va_end(ev.ap);
    }
  }

  unlock();
}
