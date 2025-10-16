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
#include <fstream>
#include <sstream>

#include "dm_replay_mgr.h"
#include "csv.h"
#include "std_filesystem.h"

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

DataMonitorReplayMgr::DataMonitorReplayMgr(const std::string& path_str)
    : m_stream(path_str), m_csv_reader(path_str, m_stream) {
  if (path_str == "") {
    m_state = State::kNotInited;
    return;
  }
  m_state = State::kError;
  if (!m_stream.is_open()) return;
  try {
    m_csv_reader.read_header(io::ignore_extra_column, "received_at", "protocol",
                             "msg_type", "source", "raw_data");
  } catch (std::exception&) {
    return;
  }
  m_loopback_drivers = GetLoopbackDriver();
  m_state = m_loopback_drivers.empty() ? State::kNoDriver : State::kAwaitLine1;
}

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
  if (m_state != State::kPlaying && m_state != State::kAwaitLine1) return 0;
  std::string received_at;
  std::string protocol;
  std::string msg_type;
  std::string source;
  std::string raw_data;
  bool there_is_more =
      m_csv_reader.read_row(received_at, protocol, msg_type, source, raw_data);
  HandleRow(protocol, msg_type, source, raw_data);
  if (!there_is_more) {
    m_state = State::kEof;
    return 0;
  }
  std::chrono::milliseconds delay = ComputeDelay(received_at);
  return delay.count();
}

std::chrono::milliseconds DataMonitorReplayMgr::ComputeDelay(
    const std::string& received_at) {

  using namespace std::chrono;
  constexpr int kDefaultDelay = 100;
  constexpr auto kEpoch = ReplayTimepoint{};

  const ReplayTimepoint now = ReplayClock::now();
  auto duration_from_start = duration_cast<milliseconds>(now - m_replay_start) +
                             milliseconds(kDefaultDelay);
  ReplayTimepoint timestamp = kEpoch;
  try {
    timestamp = kEpoch + milliseconds(std::stoi(received_at));
    duration_from_start =
        duration_cast<milliseconds>(timestamp - m_first_timestamp);
  } catch (std::invalid_argument&) {
  } catch (std::out_of_range&) {
  }
  if (m_state == State::kAwaitLine1 && timestamp != kEpoch) {
    m_first_timestamp = timestamp;
    m_replay_start = now;
    m_state = State::kPlaying;
  }
  const ReplayTimepoint replay_time = m_replay_start + duration_from_start;
  return duration_cast<milliseconds>(replay_time - now);
}
