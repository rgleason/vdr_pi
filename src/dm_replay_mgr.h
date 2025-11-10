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

enum class VdrMsgType;  // forward

using VdrMsgCallback = std::function<void(VdrMsgType, const std::string&)>;

/** See libs/fast_csv_reader/fast_csv_reader/README.md */
using CsvReader =
    io::CSVReader<5, io::trim_chars<' '>, io::double_quote_escape<',', '\"'>,
                  io::single_line_comment<'#'>>;

using ReplayClock = std::chrono::system_clock;
using ReplayTimepoint = std::chrono::time_point<ReplayClock>;

static constexpr uint64_t kMaxUint64 = std::numeric_limits<uint64_t>::max();

/** Debug and Message assumed to be logged, Info presented as a GUI dialog. */
enum class VdrMsgType { kDebug, kMessage, kInfo };

/** Handle replaying of data recorded by Data Monitor */
class DataMonitorReplayMgr {
public:
  /**
   * Create instance in idle state playing from a log file.
   * @param path Log file created by Data Monitor.
   * @param update_controls Callback updating GUI based on current state.
   * @vdr_message Callback handling user info.
   */
  DataMonitorReplayMgr(const std::string& path,
                       std::function<void()> update_controls,
                       VdrMsgCallback vdr_message);

  /** Create instance in idle state doing nothing . */
  DataMonitorReplayMgr()
      : DataMonitorReplayMgr(
            "", [] {}, [](VdrMsgType, const std::string&) {}) {}

  ~DataMonitorReplayMgr();

  /** Start playing file */
  void Start();

  /** Pause playing */
  void Pause() {
    if (m_state == State::kPlaying) m_state = State::kPaused;
  }

  /**
   * Handle data monitor logfile replay timer tick, typically sending
   * one message.
   * @return Milliseconds to next message. Value < 0 means
   *    there is nothing more to send. Value == 0 indicates
   *    that we are catching up, next message should already have
   *    been sent.
   */
  int Notify();

  bool IsPlaying() const { return m_state == State::kPlaying; }

  bool IsAtEnd() const { return m_state == State::kEof; }

  bool IsError() const { return m_state == State::kError; }

  bool IsIdle() const { return m_state == State::kIdle; }

  bool IsPaused() const { return m_state == State::kPaused; }

  bool IsDriverMissing() const { return m_state == State::kNoDriver; }

  /** Return how much of current file is played, number between 0 and 1. */
  double GetProgressFraction() const;

  /**
   * Return currently played timestamp, milliseconds since 1/1 1970 or
   * kMaxUint64 if nothing played.
 . */
  uint64_t GetCurrentTimestamp() const;

private:
  /** CsvReader byte source handling comments and space. */
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

  CsvReader m_csv_reader;
  ReplayTimepoint m_replay_start;       ///< When the replay started
  ReplayTimepoint m_first_timestamp;    ///< First log line timestamp
  ReplayTimepoint m_current_timestamp;  ///< Currently played timestamp
  const unsigned m_file_size;
  unsigned m_read_bytes;
  std::function<void()> m_update_controls;
  VdrMsgCallback m_vdr_message;

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
   * @param ms Current processed logfile entry milliseconds timestamp.
   * @return Duration to next message.
   */
  std::chrono::milliseconds ComputeDelay(const std::string& ms);
};

#endif  //  Data_MonitoR_RePlaY_MgR_h
