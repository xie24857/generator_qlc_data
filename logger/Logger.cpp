/**
 * C++11 rewrite of rxi/log.c
 * Original: Copyright (c) 2020 rxi (MIT License)
 */

#include "logger/Logger.h"

static const char *level_strings[] = {
  "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"
};

static const char *level_colors[] = {
  "\x1b[94m", "\x1b[36m", "\x1b[32m", "\x1b[33m", "\x1b[31m", "\x1b[35m"
};


Logger& Logger::instance() {
  static Logger logger;
  return logger;
}


void Logger::set_level(Level lv) {
  level = lv;
}


void Logger::set_quiet(bool enable) {
  quiet = enable;
}


void Logger::set_thread_safe(bool enable) {
  thread_safe = enable;
}


void Logger::set_color(bool enable) {
  use_color = enable;
}


void Logger::set_output(Level lv, FILE *fp) {
  outputs[lv] = fp;
}


const char* Logger::level_string(Level lv) const {
  return level_strings[lv];
}


int Logger::add_callback(LogFn fn, void *udata, Level lv) {
  try {
    callbacks.push_back({fn, udata, lv});
    return 0;
  } catch (...) {
    return -1;
  }
}


int Logger::add_fp(FILE *fp, Level lv) {
  return add_callback(file_callback, fp, lv);
}


void Logger::init_event(Event *ev, void *udata) {
  if (!ev->time) {
    time_t t = time(nullptr);
    ev->time = localtime(&t);
  }
  ev->udata = udata;
}


void Logger::default_callback(Event *ev) {
  char buf[16];
  buf[strftime(buf, sizeof(buf), "%H:%M:%S", ev->time)] = '\0';
  FILE *fp = static_cast<FILE*>(ev->udata);

  if (instance().use_color && (fp == stdout || fp == stderr)) {
    fprintf(fp, "%s %s%-5s\x1b[0m \x1b[90m%s:%d:\x1b[0m ",
      buf, level_colors[ev->level], level_strings[ev->level],
      ev->file, ev->line);
  } else {
    fprintf(fp, "%s %-5s %s:%d: ",
      buf, level_strings[ev->level], ev->file, ev->line);
  }

  vfprintf(fp, ev->fmt, ev->ap);
  fprintf(fp, "\n");
  fflush(fp);
}


void Logger::file_callback(Event *ev) {
  char buf[64];
  buf[strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", ev->time)] = '\0';
  FILE *fp = static_cast<FILE*>(ev->udata);
  fprintf(fp, "%s %-5s %s:%d: ",
    buf, level_strings[ev->level], ev->file, ev->line);
  vfprintf(fp, ev->fmt, ev->ap);
  fprintf(fp, "\n");
  fflush(fp);
}


void Logger::log(Level lv, const char *file, int line, const char *fmt, ...) {
  Event ev;
  ev.fmt   = fmt;
  ev.file  = file;
  ev.line  = line;
  ev.level = lv;
  ev.time  = nullptr;

  std::unique_lock<std::mutex> lock(mutex, std::defer_lock);
  if (thread_safe) lock.lock();

  if (!quiet && lv >= level) {
    init_event(&ev, outputs[lv]);
    va_start(ev.ap, fmt);
    default_callback(&ev);
    va_end(ev.ap);
  }

  for (auto &cb : callbacks) {
    if (lv >= cb.level) {
      init_event(&ev, cb.udata);
      va_start(ev.ap, fmt);
      cb.fn(&ev);
      va_end(ev.ap);
    }
  }
}
