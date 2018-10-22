/*
 The MIT License (MIT)

 Copyright (c) [2016] [BTC.COM]

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
 */
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <err.h>
#include <errno.h>
#include <unistd.h>

#include <iostream>

#include <boost/interprocess/sync/file_lock.hpp>
#include <glog/logging.h>
#include <libconfig.h++>

#include "ShareConvertor.hpp"

using namespace std;
using namespace libconfig;

// kafka repeater
KafkaRepeater *gKafkaRepeater = nullptr;

void handler(int sig) {
  if (gKafkaRepeater) {
    gKafkaRepeater->stop();
  }
}

void usage() {
  fprintf(stderr, "Usage:\n\tkafka_repeater -c \"kafka_repeater.cfg\" -l \"log_kafka_repeater\"\n");
}

template<typename S, typename V>
void readFromSetting(const S &setting,
                     const string &key,
                     V &value,
                     bool optional = false)
{
  if (!setting.lookupValue(key, value) && !optional) {
    LOG(FATAL) << "config section missing key: " << key;
  }
}

int main(int argc, char **argv) {
  char *optLogDir = NULL;
  char *optConf   = NULL;
  int c;

  if (argc <= 1) {
    usage();
    return 1;
  }
  while ((c = getopt(argc, argv, "c:l:h")) != -1) {
    switch (c) {
      case 'c':
        optConf = optarg;
        break;
      case 'l':
        optLogDir = optarg;
        break;
      case 'h': default:
        usage();
        exit(0);
    }
  }

  // Initialize Google's logging library.
  google::InitGoogleLogging(argv[0]);
  if (optLogDir == NULL || strcmp(optLogDir, "stderr") == 0) {
    FLAGS_logtostderr = 1;
  } else {
    FLAGS_log_dir = string(optLogDir);
  }
  // Log messages at a level >= this flag are automatically sent to
  // stderr in addition to log files.
  FLAGS_stderrthreshold = 3;    // 3: FATAL
  FLAGS_max_log_size    = 100;  // max log file size 100 MB
  FLAGS_logbuflevel     = -1;   // don't buffer logs
  FLAGS_stop_logging_if_full_disk = true;

  // Read the file. If there is an error, report it and exit.
  libconfig::Config cfg;
  try
  {
    cfg.readFile(optConf);
  } catch(const FileIOException &fioex) {
    std::cerr << "I/O error while reading file." << std::endl;
    return(EXIT_FAILURE);
  } catch(const ParseException &pex) {
    std::cerr << "Parse error at " << pex.getFile() << ":" << pex.getLine()
    << " - " << pex.getError() << std::endl;
    return(EXIT_FAILURE);
  }

  // lock cfg file:
  //    you can't run more than one process with the same config file
  /*boost::interprocess::file_lock pidFileLock(optConf);
  if (pidFileLock.try_lock() == false) {
    LOG(FATAL) << "lock cfg file fail";
    return(EXIT_FAILURE);
  }*/

  signal(SIGTERM, handler);
  signal(SIGINT,  handler);

  try {
    bool enableSConvBtcV2ToV1 = false;
    int repeatedNumberDisplayInterval = 10;
    readFromSetting(cfg, "share_convertor.bitcoin_v2_to_v1", enableSConvBtcV2ToV1, true);
    readFromSetting(cfg, "log.repeated_number_display_interval", repeatedNumberDisplayInterval, true);


    if (enableSConvBtcV2ToV1) {
      gKafkaRepeater = new ShareConvertorBitcoinV2ToV1(
        cfg.lookup("kafka.in_brokers"), cfg.lookup("kafka.in_topic"), cfg.lookup("kafka.in_group_id"),
        cfg.lookup("kafka.out_brokers"), cfg.lookup("kafka.out_topic")
      );
    }
    else {
      LOG(INFO) << "no repeater enabled";
      return 0;
    }

    
    if (!gKafkaRepeater->init()) {
      LOG(FATAL) << "kafka repeater init failed";
      return 1;
    }

    gKafkaRepeater->runMessageNumberDisplayThread(repeatedNumberDisplayInterval);
    gKafkaRepeater->run();
  }
  catch (std::exception & e) {
    LOG(FATAL) << "exception: " << e.what();
    return 1;
  }

  google::ShutdownGoogleLogging();
  return 0;
}
