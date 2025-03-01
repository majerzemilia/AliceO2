// Copyright 2019-2020 CERN and copyright holders of ALICE O2.
// See https://alice-o2.web.cern.ch/copyright for details of the copyright holders.
// All rights not expressly granted are reserved.
//
// This software is distributed under the terms of the GNU General Public
// License v3 (GPL Version 3), copied verbatim in the file "COPYING".
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

#include "AODJAlienReaderHelpers.h"
#include "Framework/TableTreeHelpers.h"
#include "Framework/AnalysisHelpers.h"
#include "Framework/RootTableBuilderHelpers.h"
#include "Framework/AlgorithmSpec.h"
#include "Framework/ConfigParamRegistry.h"
#include "Framework/ControlService.h"
#include "Framework/CallbackService.h"
#include "Framework/EndOfStreamContext.h"
#include "Framework/DeviceSpec.h"
#include "Framework/RawDeviceService.h"
#include "Framework/DataSpecUtils.h"
#include "DataInputDirector.h"
#include "Framework/SourceInfoHeader.h"
#include "Framework/ChannelInfo.h"
#include "Framework/Logger.h"

#if __has_include(<TJAlienFile.h>)
#include <TJAlienFile.h>
#endif
#include <TGrid.h>
#include <TFile.h>
#include <TTreeCache.h>
#include <TSystem.h>

#include <arrow/ipc/reader.h>
#include <arrow/ipc/writer.h>
#include <arrow/io/interfaces.h>
#include <arrow/table.h>
#include <arrow/util/key_value_metadata.h>

#include <thread>

using namespace o2;
using namespace o2::aod;

struct RuntimeWatchdog {
  int numberTimeFrames;
  uint64_t startTime;
  uint64_t lastTime;
  double runTime;
  uint64_t runTimeLimit;

  RuntimeWatchdog(Long64_t limit)
  {
    numberTimeFrames = -1;
    startTime = uv_hrtime();
    lastTime = startTime;
    runTime = 0.;
    runTimeLimit = limit;
  }

  bool update()
  {
    numberTimeFrames++;
    if (runTimeLimit <= 0) {
      return true;
    }

    auto nowTime = uv_hrtime();

    // time spent to process the time frame
    double time_spent = numberTimeFrames < 1 ? (double)(nowTime - lastTime) / 1.E9 : 0.;
    runTime += time_spent;
    lastTime = nowTime;

    return ((double)(lastTime - startTime) / 1.E9 + runTime / (numberTimeFrames + 1)) < runTimeLimit;
  }

  void printOut()
  {
    LOGP(info, "RuntimeWatchdog");
    LOGP(info, "  run time limit: {}", runTimeLimit);
    LOGP(info, "  number of time frames: {}", numberTimeFrames);
    LOGP(info, "  estimated run time per time frame: {}", (numberTimeFrames >= 0) ? runTime / (numberTimeFrames + 1) : 0.);
    LOGP(info, "  estimated total run time: {}", (double)(lastTime - startTime) / 1.E9 + ((numberTimeFrames >= 0) ? runTime / (numberTimeFrames + 1) : 0.));
  }
};

using o2::monitoring::Metric;
using o2::monitoring::Monitoring;
using o2::monitoring::tags::Key;
using o2::monitoring::tags::Value;

namespace o2::framework::readers
{
auto setEOSCallback(InitContext& ic)
{
  ic.services().get<CallbackService>().set(CallbackService::Id::EndOfStream,
                                           [](EndOfStreamContext& eosc) {
                                             auto& control = eosc.services().get<ControlService>();
                                             control.endOfStream();
                                             control.readyToQuit(QuitRequest::Me);
                                           });
}

template <typename O>
static inline auto extractTypedOriginal(ProcessingContext& pc)
{
  /// FIXME: this should be done in invokeProcess() as some of the originals may be compound tables
  return O{pc.inputs().get<TableConsumer>(aod::MetadataTrait<O>::metadata::tableLabel())->asArrowTable()};
}

template <typename... Os>
static inline auto extractOriginalsTuple(framework::pack<Os...>, ProcessingContext& pc)
{
  return std::make_tuple(extractTypedOriginal<Os>(pc)...);
}

AlgorithmSpec AODJAlienReaderHelpers::rootFileReaderCallback()
{
  auto callback = AlgorithmSpec{adaptStateful([](ConfigParamRegistry const& options,
                                                 DeviceSpec const& spec,
                                                 Monitoring& monitoring) {
    monitoring.send(Metric{(uint64_t)0, "arrow-bytes-created"}.addTag(Key::Subsystem, monitoring::tags::Value::DPL));
    monitoring.send(Metric{(uint64_t)0, "arrow-messages-created"}.addTag(Key::Subsystem, monitoring::tags::Value::DPL));
    monitoring.send(Metric{(uint64_t)0, "arrow-bytes-destroyed"}.addTag(Key::Subsystem, monitoring::tags::Value::DPL));
    monitoring.send(Metric{(uint64_t)0, "arrow-messages-destroyed"}.addTag(Key::Subsystem, monitoring::tags::Value::DPL));
    monitoring.send(Metric{(uint64_t)0, "arrow-bytes-expired"}.addTag(Key::Subsystem, monitoring::tags::Value::DPL));
    monitoring.flushBuffer();

    if (!options.isSet("aod-file")) {
      LOGP(fatal, "No input file defined!");
      throw std::runtime_error("Processing is stopped!");
    }

    auto filename = options.get<std::string>("aod-file");

    std::string parentFileReplacement;
    if (options.isSet("aod-parent-base-path-replacement")) {
      parentFileReplacement = options.get<std::string>("aod-parent-base-path-replacement");
    }

    int parentAccessLevel = 0;
    if (options.isSet("aod-parent-access-level")) {
      parentAccessLevel = options.get<int>("aod-parent-access-level");
    }

    // create a DataInputDirector
    auto didir = std::make_shared<DataInputDirector>(filename, &monitoring, parentAccessLevel, parentFileReplacement);
    if (options.isSet("aod-reader-json")) {
      auto jsonFile = options.get<std::string>("aod-reader-json");
      if (!didir->readJson(jsonFile)) {
        LOGP(error, "Check the JSON document! Can not be properly parsed!");
      }
    }

    // get the run time watchdog
    auto* watchdog = new RuntimeWatchdog(options.get<int64_t>("time-limit"));

    // selected the TFN input and
    // create list of requested tables
    header::DataHeader TFNumberHeader;
    header::DataHeader TFFileNameHeader;
    std::vector<OutputRoute> requestedTables;
    std::vector<OutputRoute> routes(spec.outputs);
    for (auto route : routes) {
      if (DataSpecUtils::partialMatch(route.matcher, header::DataOrigin("TFN"))) {
        auto concrete = DataSpecUtils::asConcreteDataMatcher(route.matcher);
        TFNumberHeader = header::DataHeader(concrete.description, concrete.origin, concrete.subSpec);
      } else if (DataSpecUtils::partialMatch(route.matcher, header::DataOrigin("TFF"))) {
        auto concrete = DataSpecUtils::asConcreteDataMatcher(route.matcher);
        TFFileNameHeader = header::DataHeader(concrete.description, concrete.origin, concrete.subSpec);
      } else {
        requestedTables.emplace_back(route);
      }
    }

    auto fileCounter = std::make_shared<int>(0);
    auto numTF = std::make_shared<int>(-1);
    return adaptStateless([TFNumberHeader,
                           TFFileNameHeader,
                           requestedTables,
                           fileCounter,
                           numTF,
                           watchdog,
                           didir](Monitoring& monitoring, DataAllocator& outputs, ControlService& control, DeviceSpec const& device) {
      // Each parallel reader device.inputTimesliceId reads the files fileCounter*device.maxInputTimeslices+device.inputTimesliceId
      // the TF to read is numTF
      assert(device.inputTimesliceId < device.maxInputTimeslices);
      int fcnt = (*fileCounter * device.maxInputTimeslices) + device.inputTimesliceId;
      int ntf = *numTF + 1;
      static int currentFileCounter = -1;
      static int filesProcessed = 0;
      if (currentFileCounter != *fileCounter) {
        currentFileCounter = *fileCounter;
        monitoring.send(Metric{(uint64_t)++filesProcessed, "files-opened"}.addTag(Key::Subsystem, monitoring::tags::Value::DPL));
      }

      // loop over requested tables
      bool first = true;
      static size_t totalSizeUncompressed = 0;
      static size_t totalSizeCompressed = 0;
      static uint64_t totalDFSent = 0;

      // check if RuntimeLimit is reached
      if (!watchdog->update()) {
        LOGP(info, "Run time exceeds run time limit of {} seconds. Exiting gracefully...", watchdog->runTimeLimit);
        LOGP(info, "Stopping reader {} after time frame {}.", device.inputTimesliceId, watchdog->numberTimeFrames - 1);
        didir->closeInputFiles();
        monitoring.flushBuffer();
        control.endOfStream();
        control.readyToQuit(QuitRequest::Me);
        return;
      }

      for (auto& route : requestedTables) {
        if ((device.inputTimesliceId % route.maxTimeslices) != route.timeslice) {
          continue;
        }

        // create header
        auto concrete = DataSpecUtils::asConcreteDataMatcher(route.matcher);
        auto dh = header::DataHeader(concrete.description, concrete.origin, concrete.subSpec);

        if (!didir->readTree(outputs, dh, fcnt, ntf, totalSizeCompressed, totalSizeUncompressed)) {
          if (first) {
            // check if there is a next file to read
            fcnt += device.maxInputTimeslices;
            if (didir->atEnd(fcnt)) {
              LOGP(info, "No input files left to read for reader {}!", device.inputTimesliceId);
              didir->closeInputFiles();
              monitoring.flushBuffer();
              control.endOfStream();
              control.readyToQuit(QuitRequest::Me);
              return;
            }
            // get first folder of next file
            ntf = 0;
            if (!didir->readTree(outputs, dh, fcnt, ntf, totalSizeCompressed, totalSizeUncompressed)) {
              LOGP(fatal, "Can not retrieve tree for table {}: fileCounter {}, timeFrame {}", concrete.origin, fcnt, ntf);
              throw std::runtime_error("Processing is stopped!");
            }
          } else {
            LOGP(fatal, "Can not retrieve tree for table {}: fileCounter {}, timeFrame {}", concrete.origin, fcnt, ntf);
            throw std::runtime_error("Processing is stopped!");
          }
        }

        if (first) {
          // TF number
          auto timeFrameNumber = didir->getTimeFrameNumber(dh, fcnt, ntf);
          auto o = Output(TFNumberHeader);
          outputs.make<uint64_t>(o) = timeFrameNumber;

          // Origin file name for derived output map
          auto o2 = Output(TFFileNameHeader);
          auto fileAndFolder = didir->getFileFolder(dh, fcnt, ntf);
          std::string currentFilename(fileAndFolder.file->GetName());
          if (strcmp(fileAndFolder.file->GetEndpointUrl()->GetProtocol(), "file") == 0 && fileAndFolder.file->GetEndpointUrl()->GetFile()[0] != '/') {
            // This is not an absolute local path. Make it absolute.
            static std::string pwd = gSystem->pwd() + std::string("/");
            currentFilename = pwd + std::string(fileAndFolder.file->GetName());
          }
          outputs.make<std::string>(o2) = currentFilename;
        }

        first = false;
      }
      totalDFSent++;
      monitoring.send(Metric{(uint64_t)totalDFSent, "df-sent"}.addTag(Key::Subsystem, monitoring::tags::Value::DPL));
      monitoring.send(Metric{(uint64_t)totalSizeUncompressed / 1000, "aod-bytes-read-uncompressed"}.addTag(Key::Subsystem, monitoring::tags::Value::DPL));
      monitoring.send(Metric{(uint64_t)totalSizeCompressed / 1000, "aod-bytes-read-compressed"}.addTag(Key::Subsystem, monitoring::tags::Value::DPL));

      // save file number and time frame
      *fileCounter = (fcnt - device.inputTimesliceId) / device.maxInputTimeslices;
      *numTF = ntf;
    });
  })};

  return callback;
}

} // namespace o2::framework::readers
