/***************************************************************************
 *   Copyright (C) 2025  Alec Leamas                                       *
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

#ifndef Data_MonitoR_RePlaY_MgR_h
#define Data_MonitoR_RePlaY_MgR_h

#include <chrono>
#include <cstdint>
#include <fstream>
#include <functional>
#include <limits>
#include <string>
#include <vector>

#include "csv.h"
#include "ocpn_plugin.h"

using CsvReader =
    io::CSVReader<5, io::trim_chars<' '>, io::double_quote_escape<',', '\"'>,
                  io::single_line_comment<'#'>>;

using ReplayClock = std::chrono::system_clock;
using ReplayTimepoint = std::chrono::time_point<ReplayClock>;

constexpr uint64_t kMaxUint64 = std::numeric_limits<uint64_t>::max();

class DataMonitorReplayMgr {
public:
  /** Create instance in idle state playing from given log file path.  */
  DataMonitorReplayMgr(const std::string& path,
                       std::function<void()> update_controls);

  /** Create instance in idle state doing nothing . */
  DataMonitorReplayMgr() : DataMonitorReplayMgr("", [] {}) {}

  ~DataMonitorReplayMgr();

  /** Start playing file */
  void Start() {
    if (m_state == State::kIdle) {
      m_read_bytes = 0;
      Notify();
    }
  }

  /** Pause playing... */
  void Pause() {
    if (m_state == State::kPlaying) m_state = State::kIdle;
  }

  /*
   * Handle data monitor logfile replay timer tick, typically sending
   * one message.
   * @return Milliseconds to next message. Values < 0 means
   *    there is nothing more to send. Value == 0 indicates
   *    that we are catching up, next message shuld already have
   *    been sent.
   */
  int Notify();

  bool HasFile() const;

  bool IsPlaying() const { return m_state == State::kPlaying; }

  bool IsAtEnd() const { return m_state == State::kEof; }

  double GetProgressFraction() const;

  bool IsError() const { return m_state == State::kError; }

  bool IsPaused() const { return m_state == State::kPaused; }

  /**
   * Return currently played timestamp, milliseconds since 1/1 1970 or
   * kMaxUint64 if nothing played.
 . */
  uint64_t GetCurrentTimestamp() const;

private:
  class FilteredByteSource;

  enum class State {
    kNotInited,
    kIdle,
    kPlaying,
    kPaused,
    kEof,
    kError,
    kNoDriver
  } m_state;
  std::ifstream m_stream;
  FilteredByteSource* m_byte_source;
  CsvReader m_csv_reader;
  ReplayTimepoint m_replay_start;       ///< When the replay started
  ReplayTimepoint m_first_timestamp;    ///< First log line timestamp
  ReplayTimepoint m_current_timestamp;  ///< Currently played timestamp
  std::function<void()> m_update_controls;
  const unsigned m_file_size;
  unsigned m_read_bytes;

  /** A single loopback driver or empty if none available. */
  std::vector<DriverHandle> m_loopback_drivers;

  void HandleSignalK(const std::string& context_self, const std::string& source,
                     const std::string& json);

  void Handle2000(const std::string& pgn_str, const std::string& source,
                  const std::string& raw_data);

  void Handle0183(const std::string& id, const std::string& source,
                  const std::string& sentence);

  void HandleRow(const std::string& protocol, const std::string& msg_type,
                 const std::string& source, const std::string& raw_data);

  /**
   * Compute duration to next message to be sent <br>
   * Side effects: Updates m_replay_start, m_first_timestamp,
   * m_current_timestamp and m_state.
   * @return Duration to next message.
   */
  std::chrono::milliseconds ComputeDelay(const std::string& ms);
};

#endif  //  Data_MonitoR_RePlaY_MgR_h
