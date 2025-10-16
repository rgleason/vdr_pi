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
#include <fstream>
#include <string>
#include <vector>

#include "csv.h"
#include "ocpn_plugin.h"

using CsvReader =
    io::CSVReader<5, io::trim_chars<' '>, io::double_quote_escape<',', '\"'>,
                  io::single_line_comment<'#'>>;

using ReplayClock = std::chrono::system_clock;
using ReplayTimepoint = std::chrono::time_point<ReplayClock>;

class DataMonitorReplayMgr {
public:
  /**
   * Initiate playing from given log file path
   */
  DataMonitorReplayMgr(const std::string& path);

  /**
   * Create instance in idle state doing nothing
 . */
  DataMonitorReplayMgr() : DataMonitorReplayMgr("") {}

  /*
   * Handle data monitor logfile replay timer event
   * @return Milliseconds to next tick. Values <= 0 means
   *    that there should be no delay.
   */
  int Notify();

private:
  enum class State {
    kNotInited,
    kAwaitLine1,
    kPlaying,
    kEof,
    kError,
    kNoDriver
  } m_state;
  std::ifstream m_stream;
  CsvReader m_csv_reader;
  ReplayTimepoint m_replay_start;     ///< When the replay started
  ReplayTimepoint m_first_timestamp;  ///< First log line timestamp

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

  std::chrono::milliseconds ComputeDelay(const std::string& ms);
};

#endif  //  Data_MonitoR_RePlaY_MgR_h
