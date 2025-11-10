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

#include <algorithm>
#include <cassert>
#include <cctype>
#include <fstream>
#include <sstream>

#include "dm_replay_mgr.h"
#include "csv.h"
#include "std_filesystem.h"

using namespace std::chrono_literals;

static constexpr auto kEpoch = ReplayTimepoint{};

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

/** Send string to WriteCommDriver using given driver handle. */
static void SendString(DriverHandle dh, const std::string& s) {
  auto payload = std::make_shared<std::vector<uint8_t>>(s.begin(), s.end());
  WriteCommDriver(dh, payload);
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

DataMonitorReplayMgr::DataMonitorReplayMgr(
    const std::string& path, std::function<void()> update_controls,
    VdrMsgCallback vdr_message)
    : m_state(State::kNotInited),
      m_csv_reader(path, std::make_unique<FilteredByteSource>(path)),
      m_file_size(path.empty() ? 0 : fs::file_size(path)),
      m_read_bytes(0),
      m_update_controls(std::move(update_controls)),
      m_vdr_message(std::move(vdr_message)) {

  if (path == "") return;

  m_state = State::kError;
  try {
    m_csv_reader.read_header(io::ignore_extra_column, "received_at", "protocol",
                             "msg_type", "source", "raw_data");
  } catch (io::error::base& e) {
    std::string s(_("CSV header parse error: ").ToStdString() + e.what());
    m_vdr_message(VdrMsgType::kInfo, s);
    m_update_controls();
    return;
  }
  m_loopback_drivers = GetLoopbackDriver();
  m_state = m_loopback_drivers.empty() ? State::kNoDriver : State::kIdle;
  if (m_state == State::kNoDriver)
    m_vdr_message(VdrMsgType::kInfo, kNoDriverMessage);
}

DataMonitorReplayMgr::~DataMonitorReplayMgr() = default;

void DataMonitorReplayMgr::HandleSignalK(const std::string& context_self,
                                         const std::string& source,
                                         const std::string& json) {
  std::stringstream ss;
  ss << "signalk " << source << " " << context_self << " " << json;
  SendString(m_loopback_drivers[0], ss.str());
}

void DataMonitorReplayMgr::Handle2000(const std::string& pgn,
                                      const std::string& source,
                                      const std::string& raw_data) {
  std::stringstream ss;
  ss << "nmea2000 " << source << " " << pgn << " " << raw_data;
  SendString(m_loopback_drivers[0], ss.str());
}

void DataMonitorReplayMgr::Handle0183(const std::string& id,
                                      const std::string& source,
                                      const std::string& sentence) {
  std::stringstream ss;
  ss << "nmea0183 " << source << " " << id << " " << sentence;
  SendString(m_loopback_drivers[0], ss.str());
}

void DataMonitorReplayMgr::Start() {
  if (m_state == State::kIdle) m_read_bytes = 0;
  if (m_state == State::kPaused || m_state == State::kIdle)
    m_state = State::kPlaying;
  Notify();
}

void DataMonitorReplayMgr::HandleRow(const std::string& protocol,
                                     const std::string& msg_type,
                                     const std::string& source,
                                     const std::string& raw_data) {
  if (protocol == "NMEA2000")
    Handle2000(msg_type, source, raw_data);
  else if (protocol == "NMEA0183")
    Handle0183(msg_type, source, raw_data);
  else if (protocol == "SignalK")
    HandleSignalK(msg_type, source, raw_data);
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
  m_read_bytes += received_at.size() + protocol.size() + msg_type.size() +
                  source.size() + raw_data.size() + 5;
  HandleRow(protocol, msg_type, source, raw_data);
  if (!there_is_more) {
    m_state = State::kEof;
    m_update_controls();
    return -1;
  }
  std::chrono::milliseconds delay = ComputeDelay(received_at);
  return delay.count();
}

std::chrono::milliseconds DataMonitorReplayMgr::ComputeDelay(
    const std::string& received_at) {
  using namespace std::chrono;
  constexpr auto kDefaultDelay = 100ms;

  const ReplayTimepoint now = ReplayClock::now();
  if (m_replay_start == kEpoch) m_replay_start = now;
  auto duration_from_start =
      duration_cast<milliseconds>(now - m_replay_start) + kDefaultDelay;
  ReplayTimepoint timestamp = kEpoch;
  try {
    timestamp = kEpoch + milliseconds(std::stol(received_at));
    if (m_first_timestamp == kEpoch) m_first_timestamp = timestamp;
    duration_from_start =
        duration_cast<milliseconds>(timestamp - m_first_timestamp);
  } catch (std::invalid_argument&) {
  } catch (std::out_of_range&) {
  }
  if (timestamp != kEpoch) m_current_timestamp = timestamp;
  if (m_state == State::kIdle) m_state = State::kPlaying;

  const ReplayTimepoint replay_time = m_replay_start + duration_from_start;
  if (replay_time <= now) return 0ms;  // catching up...
  return duration_cast<milliseconds>(replay_time - now);
}

uint64_t DataMonitorReplayMgr::GetCurrentTimestamp() const {
  using namespace std::chrono;
  return static_cast<uint64_t>(
      duration_cast<milliseconds>(m_current_timestamp - kEpoch).count());
}

double DataMonitorReplayMgr::GetProgressFraction() const {
  return static_cast<double>(m_read_bytes) / m_file_size;
}
