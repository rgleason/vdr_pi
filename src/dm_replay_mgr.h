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

/**
 * \file
 *
 * Data Monitor log files replay state.
 */

#ifndef Data_MonitoR_RePlaY_MgR_h
#define Data_MonitoR_RePlaY_MgR_h

#include <chrono>
#include <cstdint>
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

/** Debug and Message assumed to be logged, Info presented as a GUI dialog. */
enum class VdrMsgType { kDebug, kMessage, kInfo };

/**
 * Handle replaying of data recorded by Data Monitor. A model object, GUI
 * interaction is handled by callbacks.
 */
class DataMonitorReplayMgr {
public:
  /**
   * Create instance  ready to play a log file.
   * @param path Log file created by Data Monitor in VDR mode.
   * @param update_controls Callback updating GUI based on current state.
   * @param vdr_message Callback handling user info.
   */
  DataMonitorReplayMgr(const std::string& path,
                       std::function<void()> update_controls,
                       VdrMsgCallback vdr_message);

  /** Create instance in idle state doing nothing. */
  DataMonitorReplayMgr()
      : DataMonitorReplayMgr(
            "", [] {}, [](VdrMsgType, const std::string&) {}) {}

  ~DataMonitorReplayMgr();

  /** Start or restart playing file */
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

  [[nodiscard]] bool IsPlaying() const { return m_state == State::kPlaying; }

  [[nodiscard]] bool IsAtEnd() const { return m_state == State::kEof; }

  [[nodiscard]] bool IsError() const { return m_state == State::kError; }

  [[nodiscard]] bool IsIdle() const { return m_state == State::kIdle; }

  [[nodiscard]] bool IsPaused() const { return m_state == State::kPaused; }

  [[nodiscard]] bool IsDriverMissing() const {
    return m_state == State::kNoDriver;
  }

  /** Return how much of current file is played, number between 0 and 1. */
  [[nodiscard]] double GetProgressFraction() const;

  /**
   * Return currently played timestamp, milliseconds since 1/1 1970.
   * Undefined if nothing played.
 . */
  [[nodiscard]] uint64_t GetCurrentTimestamp() const;

  /** Return true if file on path seems to be a Data Monitor VDR logfile */
  static bool IsVdrFormat(const std::string& path);

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

  /** Status with respect to the logfile. */
  struct Log {
    ReplayTimepoint start_time;   ///< When the replay started
    ReplayTimepoint first_stamp;  ///< First log line timestamp
    ReplayTimepoint curr_stamp;   ///< Currently played timestamp
    unsigned read_bytes;          ///< # read bytes so far
    unsigned file_size;

    explicit Log(unsigned _file_size) : read_bytes(0), file_size(_file_size) {}
    explicit Log(const std::string& path);
  } m_log;

  CsvReader m_csv_reader;
  std::function<void()> m_update_controls;
  VdrMsgCallback m_vdr_message;

  /** A single loopback driver or empty if none available. */
  std::vector<DriverHandle> m_loopback_drivers;

  void HandleRow(const std::string& protocol, const std::string& msg_type,
                 const std::string& source, const std::string& raw_data) const;

  /**
   * Compute duration to next message to be sent <br> and update
   * log timestamps.
   * @param ms Current processed logfile entry, milliseconds timestamp.
   * @param log Current used timestamps
   * @return Duration to next message.
   */
  std::chrono::milliseconds ComputeDelay(const std::string& ms, Log& log) const;
};

#endif  //  Data_MonitoR_RePlaY_MgR_h
