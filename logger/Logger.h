/**
 * C++11 rewrite of rxi/log.c
 * Original: Copyright (c) 2020 rxi (MIT License)
 */

#ifndef LOGGER_H
#define LOGGER_H

#include <cstdio>
#include <cstdarg>
#include <ctime>
#include <vector>
#include <mutex>

#define LOG_VERSION "0.4.0"

class Logger {
public:
  enum Level { TRACE, DEBUG, INFO, WARN, ERROR, FATAL };

  struct Event {
    va_list ap;
    const char *fmt;
    const char *file;
    struct tm *time;
    void *udata;
    int line;
    Level level;
  };

  using LogFn = void (*)(Event *);

  static Logger& instance();

  void set_level(Level lv);
  void set_quiet(bool enable);
  void set_thread_safe(bool enable);
  void set_color(bool enable);
  void set_output(Level lv, FILE *fp);
  int  add_fp(FILE *fp, Level lv);
  int  add_callback(LogFn fn, void *udata, Level lv);

  const char* level_string(Level lv) const;

  void log(Level lv, const char *file, int line, const char *fmt, ...);

private:
  Logger() = default;
  Logger(const Logger&) = delete;
  Logger& operator=(const Logger&) = delete;

  struct Callback {
    LogFn fn;
    void *udata;
    Level level;
  };

  static void default_callback(Event *ev);
  static void file_callback(Event *ev);
  void init_event(Event *ev, void *udata);

  Level level = TRACE;
  bool quiet = false;
  bool thread_safe = true;
  bool use_color = true;
  FILE *outputs[6] = {
    stdout,   // TRACE
    stdout,   // DEBUG
    stdout,   // INFO
    stderr,   // WARN
    stderr,   // ERROR
    stderr    // FATAL
  };
  std::mutex mutex;
  std::vector<Callback> callbacks;
};

// 仅用于捕获 __FILE__ 和 __LINE__
#define log_trace(...) Logger::instance().log(Logger::TRACE, __FILE__, __LINE__, __VA_ARGS__)
#define log_debug(...) Logger::instance().log(Logger::DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define log_info(...)  Logger::instance().log(Logger::INFO,  __FILE__, __LINE__, __VA_ARGS__)
#define log_warn(...)  Logger::instance().log(Logger::WARN,  __FILE__, __LINE__, __VA_ARGS__)
#define log_error(...) Logger::instance().log(Logger::ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define log_fatal(...) Logger::instance().log(Logger::FATAL, __FILE__, __LINE__, __VA_ARGS__)

#endif
