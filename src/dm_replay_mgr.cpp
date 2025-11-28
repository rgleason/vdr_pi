/***************************************************************************
 *   Copyright (C) 2025 Alec Leamas                                        *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, see <https://www.gnu.org/licenses/>. *
 **************************************************************************/

/**
 * \file
 *
 * Implement dm_replay_mgr.h
 */

#include <algorithm>
#include <cassert>
#include <cctype>
#include <fstream>
#include <sstream>

#include "dm_replay_mgr.h"
#include "csv.h"
#include "std_filesystem.h"

using namespace std::chrono_literals;

static constexpr auto kEpoch = ReplayTimepoint{};   ///< 1/1 1970

static constexpr uint64_t kMaxUint64 = std::numeric_limits<uint64_t>::max();

static const char* const kNoDriverMessage =
    _(R"(I cannot find any loopback driver and is thus unable
to replay VDR data. The probable cause is that OpenCPN
is older than 5.14 -- such versions cannot be used to
replay VDR data.)");

/** Return true if dh refers to a loopback driver */
static bool IsLoopbackDriver(DriverHandle dh) {
  auto attr = GetAttributes(dh);
  auto found = attr.find("protocol");
  return found != attr.end() && attr["protocol"] == "loopback";
}

/* Return list of loopback drivers, either empty or one single element. */
static std::vector<DriverHandle> GetLoopbackDriver() {
  std::vector<DriverHandle> rv;
  auto handles = GetActiveDrivers();
  auto handle =
      std::find_if(handles.begin(), handles.end(),
                   [](DriverHandle dh) { return IsLoopbackDriver(dh); });
  if (handle != handles.end()) rv.push_back(*handle);
  return rv;
}

static ReplayTimepoint ParseTimeStamp(const std::string& stamp) {
  const char* const format = "%a %b %d %H:%M:%S %Y";
  std::istringstream is(stamp);
  is.imbue(std::locale("en_US.utf-8"));
  std::tm tm;
  is >> std::get_time(&tm, format);
  return ReplayClock::from_time_t(std::mktime(&tm));
}


/**
 * fast_csv_reader byte source reading from file filtering blank and comment
 * lines away. This should be done automagically by the reader, but I
 * don't get it to work.
 */
class DataMonitorReplayMgr::FilteredByteSource : public io::ByteSourceBase {
public:
  FilteredByteSource(const std::string& path) : m_stream(path) {}
  ~FilteredByteSource() override = default;

  int read(char* returned, int scount) override {
    assert(scount >= 0);
    auto count = static_cast<size_t>(scount);
    while (m_buff.size() < count && m_stream.good()) {
      std::string line;
      std::getline(m_stream, line);
      while (!line.empty() && std::isspace(line[0])) line = line.substr(1);
      if (line.empty()) continue;
      if (line[0] == '#') continue;
      m_buff.append(line + "\n");
    }
    std::streamsize length = std::min(count, m_buff.size());
    std::memcpy(returned, m_buff.c_str(), length);
    m_buff = m_buff.substr(length);
    return length;
  }

private:
  std::string m_buff;
  std::ifstream m_stream;
};

DataMonitorReplayMgr::Log::Log(const std::string& path) {
  std::ifstream stream(path);
  if (!stream.good()) {
    file_size = 0;
    return;
  }
  for (int i = 0; i < 10 && stream.good(); ++i) {
    std::string line;
    std::getline(stream, line);
    if (line.find("Created at:") == std::string::npos) continue;

    size_t colon_pos = line.find(':');
    std::string timestamp = line.substr(colon_pos + 1);
    curr_stamp = ParseTimeStamp(timestamp);
    break;
  }
  file_size = fs::file_size(path);
}

DataMonitorReplayMgr::DataMonitorReplayMgr(
    const std::string& path, std::function<void()> update_controls,
    VdrMsgCallback vdr_message)
    : m_state(State::kNotInited),
      m_log(path),
      m_csv_reader(path, std::make_unique<FilteredByteSource>(path)),
      m_update_controls(std::move(update_controls)),
      m_vdr_message(std::move(vdr_message)) {
  if (path == "") return;

  try {
    m_csv_reader.read_header(io::ignore_extra_column, "received_at", "protocol",
                             "msg_type", "source", "raw_data");
  } catch (io::error::base& e) {
    m_state = State::kError;
    std::string s(_("CSV header parse error: ").ToStdString() + e.what());
    m_vdr_message(VdrMsgType::kInfo, s);
    return;
  }
  m_loopback_drivers = GetLoopbackDriver();
  m_state = m_loopback_drivers.empty() ? State::kNoDriver : State::kIdle;
  if (m_state == State::kNoDriver)
    m_vdr_message(VdrMsgType::kInfo, kNoDriverMessage);
}

DataMonitorReplayMgr::~DataMonitorReplayMgr() = default;

void DataMonitorReplayMgr::HandleRow(const std::string& protocol,
                                     const std::string& msg_type,
                                     const std::string& source,
                                     const std::string& raw_data) {
  std::stringstream ss;
  if (protocol == "NMEA2000")
    ss << "nmea2000 " << source << " " << msg_type << " " << raw_data;
  else if (protocol == "NMEA0183")
    ss << "nmea0183 " << source << " " << msg_type << " " << raw_data;
  else if (protocol == "SignalK")
    ss << "signalk " << source << " " << msg_type << " " << raw_data;

  const auto s = ss.str();
  if (s.empty()) return;
  auto payload =
      std::make_shared<std::vector<uint8_t>>(s.begin(), s.end());
  WriteCommDriver(m_loopback_drivers[0], payload);
}

void DataMonitorReplayMgr::Start() {
  if (m_state == State::kIdle) m_log.read_bytes = 0;
  if (m_state == State::kPaused || m_state == State::kIdle)
    m_state = State::kPlaying;
  Notify();
}

int DataMonitorReplayMgr::Notify() {
  if (m_state != State::kPlaying && m_state != State::kIdle) return -1;
  std::string received_at;
  std::string protocol;
  std::string msg_type;
  std::string source;
  std::string raw_data;
  bool there_is_more = false;
  try {
    there_is_more = m_csv_reader.read_row(received_at, protocol, msg_type,
                                          source, raw_data);
  } catch (io::error::base& err) {
    m_vdr_message(VdrMsgType::kMessage, err.what());
    return 0;
  }
  m_log.read_bytes += received_at.size() + protocol.size() + msg_type.size() +
                      source.size() + raw_data.size() + 5;
  HandleRow(protocol, msg_type, source, raw_data);
  if (!there_is_more) {
    m_state = State::kEof;
    m_update_controls();
    return -1;
  }
  if (m_state == State::kIdle) m_state = State::kPlaying;
  std::chrono::milliseconds delay = ComputeDelay(received_at, m_log);
  return delay.count();
}

std::chrono::milliseconds DataMonitorReplayMgr::ComputeDelay(
    const std::string& received_at, Log& log) {
  using namespace std::chrono;
  constexpr auto kDefaultDelay = 100ms;

  const ReplayTimepoint now = ReplayClock::now();
  if (log.start_time == kEpoch) log.start_time = now;
  ReplayTimepoint timestamp = kEpoch;
  milliseconds duration_from_start;
  try {
    timestamp = kEpoch + milliseconds(std::stol(received_at));
    if (log.first_stamp == kEpoch) log.first_stamp = timestamp;
    duration_from_start =
        duration_cast<milliseconds>(timestamp - log.first_stamp);
  } catch (std::logic_error&) {
    m_vdr_message(VdrMsgType::kDebug,
                  std::string("Illegal timestamp: ") + received_at);
    duration_from_start =
        duration_cast<milliseconds>(now - log.start_time) + kDefaultDelay;
  }
  if (timestamp != kEpoch) log.curr_stamp = timestamp;

  const ReplayTimepoint replay_time = log.start_time + duration_from_start;
  if (replay_time <= now) return 0ms;  // catching up...
  return duration_cast<milliseconds>(replay_time - now);
}

uint64_t DataMonitorReplayMgr::GetCurrentTimestamp() const {
  using namespace std::chrono;
  return static_cast<uint64_t>(
      duration_cast<milliseconds>(m_log.curr_stamp - kEpoch).count());
}

double DataMonitorReplayMgr::GetProgressFraction() const {
  return static_cast<double>(m_log.read_bytes) / m_log.file_size;
}

bool DataMonitorReplayMgr::IsVdrFormat(const std::string& path) {
  std::ifstream stream(path);
  for (int i = 0; i < 5; ++i) {
    if (!stream.good()) return false;
    std::string line;
    std::getline(stream, line);
    if (line.find("timestamp_format") != std::string::npos &&
        line.find("EPOCH_MILLIS") != std::string::npos) {
      return true;
    }
  }
  return false;
}
