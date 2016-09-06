//
// Created by mabaiming on 16-9-1.
//

#ifndef TCP_TUNNEL_LOGGER_H
#define TCP_TUNNEL_LOGGER_H

#include <string>
#include <fstream>
#include <iostream>

#define log_error LoggerManager::getLogger(LoggerManager::ERROR) << "[ERROR] " << __func__ << "(" << __FILE__ << ":" << __LINE__ << ") "
#define log_warn LoggerManager::getLogger(LoggerManager::WARN) << "[WARN] " << __func__ << "(" << __FILE__ << ":" << __LINE__ << ") "
#define log_info LoggerManager::getLogger(LoggerManager::INFO) << "[INFO] " << __func__ << "(" << __FILE__ << ":" << __LINE__ << ") "
#define log_debug LoggerManager::getLogger(LoggerManager::DEBUG) << "[DEBUG] " << __func__ << "(" << __FILE__ << ":" << __LINE__ << ") "

using namespace std;

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
    Logger(LoggerManager* inputManager, int inputLevel): level(inputLevel), manage(inputManager), skip(0) {}
    ~Logger() {
      if (level <= manage->level) {
        *manage->out << endl;
      }
    }
    template<typename T> Logger&  operator << (T t) {
      if (skip < manage->skip) {
        ++skip;
        if (skip > 1) {
          return *this;
        }
      }
      if (level <= manage->level) {
        *manage->out << t;
      }
      return *this;
    }
    Logger&  operator << (ostream& endl(ostream&)) {
      return *this;
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

  static void init(const string& level, const string& outputFile, bool append, bool debug) {
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
    init(l, outputFile, append, debug);
  }

  static void init(int level, const string& outputFile, bool append, bool debug) {
    instance.level = level;
    instance.skip = debug ? 0 : 7;
    if (outputFile.empty() || outputFile == "stdout") {
      instance.out = &cout;
    } else {
      ios_base::openmode mode = ios_base::out;
      if (append) {
        mode |= ios_base::app;
      } else {
        mode |= ios_base::trunc;
      }
      ofstream* fout = new ofstream;
      fout->open(outputFile.c_str(), mode);
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
