//
// Created by mabaiming on 16-9-1.
//

#ifndef TCP_TUNNEL_LOGGER_H
#define TCP_TUNNEL_LOGGER_H

#include "common.h"

#include <time.h>

#include <fstream>
#include <iostream>
#include <string>

#define log_error LoggerManager::getLogger(LoggerManager::ERROR) << "[ERROR " \
    << &LoggerManager::Logger::nowTime << "] " << __func__ << "(" << __FILE__ \
    << ":" << __LINE__ << ") "
#define log_warn LoggerManager::getLogger(LoggerManager::WARN) << "[WARN " \
    << &LoggerManager::Logger::nowTime << "] " << __func__ << "(" << __FILE__ \
    << ":" << __LINE__ << ") "
#define log_info LoggerManager::getLogger(LoggerManager::INFO) << "[INFO " \
    << &LoggerManager::Logger::nowTime << "] " << __func__ << "(" << __FILE__ \
    << ":" << __LINE__ << ") "
#define log_debug LoggerManager::getLogger(LoggerManager::DEBUG) << "[DEBUG " \
    << &LoggerManager::Logger::nowTime << "] " << __func__ << "(" << __FILE__ \
    << ":" << __LINE__ << ") "

using namespace std;
using namespace Common;

class LoggerManager {
public:
  static const int ERROR = 1;
  static const int WARN = 2;
  static const int INFO = 3;
  static const int DEBUG = 4;

  class Logger {
  public:
    LoggerManager* manage;
    int level;
    int skip;
  public:
    Logger(LoggerManager* manage_, int level_)
        : level(level_), manage(manage_), skip(0) {}
    ~Logger() {
      if (level <= manage->level) {
        *manage->out << endl;
      }
    }

    template<typename T> Logger&  operator << (T t) {
      if (!consumeSkip() && level <= manage->level) {
        *manage->out << t;
      }
      return *this;
    }

    Logger&  operator << (string (Logger::*fun)() const) {
      if (!consumeSkip() && level <= manage->level) {
        *manage->out << (this->*fun)();
      }
      return *this;
    }

    Logger&  operator << (ostream& endl(ostream&)) {
      return *this;
    }

    Logger& operator << (const FdToAddr& fdToAddr) {
      if (!consumeSkip() && level <= manage->level) {
        *manage->out << fdToAddr.toAddr().toString();
      }
      return *this;
    }

    string nowTime () const {
      struct tm* ptr;
      time_t lt;
      time_t ts = time(NULL);
      struct tm* localTimePtr = localtime(&ts);
      char timeStr[80];
      strftime(timeStr, 80, "%F %T", localTimePtr);
      return string(timeStr);
    }

  private:
    bool consumeSkip() {
      if (skip < manage->skip) {
        ++skip;
        if (skip > 3) {
          return true;
        }
      }
      return false;
    }
  };

  class Logger;

  static Logger getLogger(int level) {
    Logger logger(&instance, level);
    if (instance.out == NULL) {
      init(INFO, "stdout", true, false);
    }
    return logger;
  }

  static void init(const string& level, const string& file, bool append,
      bool debug) {
    int l = INFO;
    if (level == "ERROR") {
      l = ERROR;
    } else if (level == "WARN") {
      l = WARN;
    } else if (level == "INFO") {
      l = INFO;
    } else if (level == "DEBUG") {
      l = DEBUG;
    }
    init(l, file, append, debug);
  }

  static void init(int level, const string& file, bool append, bool debug) {
    instance.level = level;
    instance.skip = debug ? 0 : 9;
    if (file.empty() || file == "stdout") {
      instance.out = &cout;
    } else {
      ios_base::openmode mode = ios_base::out;
      if (append) {
        mode |= ios_base::app;
      } else {
        mode |= ios_base::trunc;
      }
      ofstream* fout = new ofstream;
      fout->open(file.c_str(), mode);
      if (!fout->is_open()) {
        instance.out = &cout;
        log_warn << "no output file provided for log, use stdout default";
        delete fout;
      } else {
        instance.out = fout;
      }
    }
  }

private:
  LoggerManager(): level(INFO), skip(0), out(NULL) {};
  LoggerManager(const Logger& logger);
  LoggerManager& operator = (const Logger& logger);
  ~LoggerManager() {
    if (out != NULL && out != &cout) {
      delete out;
      out = NULL;
    }
  }
  static LoggerManager instance;
  int level;
  int skip;
  ostream* out;
};

#endif //TCP_TUNNEL_LOGGER_H
