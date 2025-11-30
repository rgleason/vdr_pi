/***************************************************************************
 *   Copyright (C) 2011  Jean-Eudes Onfray                                 *
 *   Copyright (C) 2025  Sebastian Rosser                                  *
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

#ifndef VDR_COMMONS_H_
#define VDR_COMMONS_H_

constexpr const char* const kControlWinName = "VdrControl";

enum class ReplayMode {
  kNetwork,      // Use network connection
  kInternalApi,  // Use PushNMEABuffer()
  kLoopback      // Use WriteCommDriver() on loopback driver
};

/**
 * Network settings for protocol output.
 */
struct ConnectionSettings {
  bool enabled;  //!< Enable network output
  bool use_tcp;  //!< Use TCP (true) or UDP (false)
  int port;      //!< Network port number

  ConnectionSettings() : enabled(false), use_tcp(true), port(10111) {}
};

/**
 * Data storage formats supported by the VDR plugin.
 *
 * Controls how data is structured and stored in VDR files. Each format offers
 * different capabilities for data organization and playback control.
 */
enum class VdrDataFormat {
  kRawNmea,  //!< Raw NMEA sentences stored unmodified
  kCsv,  //!< Structured CSV format with timestamps and message type metadata.
         // Future formats can be added here
};

/**
 * Protocol recording configuration settings.
 *
 * Controls which maritime data protocols are captured during recording.
 * Multiple protocols can be enabled simultaneously.
 */
struct VdrProtocolSettings {
  bool nmea0183;                   //!< Enable NMEA 0183 sentence recording
  bool nmea2000;                   //!< Enable NMEA 2000 PGN message recording
  bool signalK;                    //!< Enable Signal K data recording
  ConnectionSettings nmea0183Net;  //!< NMEA 0183 connection settings
  ConnectionSettings n2kNet;       //!< NMEA 2000 connection settings
  ConnectionSettings signalkNet;   //!< Signal K connection settings

  ReplayMode replay_mode;

  VdrProtocolSettings()
      : nmea0183(true),
        nmea2000(false),
        signalK(false),
        replay_mode(ReplayMode::kInternalApi)
  // nmea0183ReplayMode(NMEA0183ReplayMode::INTERNAL_API)
  {}
};

#endif  // VDR_COMMONS_H_
