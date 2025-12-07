/***************************************************************************
 *   Copyright (C) 2011 by Jean-Eudes Onfray                               *
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
#include <cstring>
#include <typeinfo>

#include "wx/wxprec.h"

#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif

#include "wx/app.h"
#include "wx/display.h"
#include "wx/filefn.h"
#include "wx/filename.h"
#include "wx/image.h"
#include "wx/log.h"
#include "wx/tokenzr.h"

#include "dm_replay_mgr.h"

#include "commons.h"
#include "icons.h"
#include "ocpn_plugin.h"
#include "record_play_mgr.h"
#include "vdr_pi_control.h"
#include "vdr_pi.h"
#include "vdr_pi_prefs.h"

wxDEFINE_EVENT(EVT_N2K, ObservedEvt);
wxDEFINE_EVENT(EVT_SIGNALK, ObservedEvt);

/**
 * Converts 2 bytes of NMEA 2000 data to an unsigned 16-bit integer
 *
 * @param data Pointer to bytes in the NMEA 2000 message (2 bytes,
 * little-endian)
 * @return Raw unsigned 16-bit integer value
 *
 * Data is stored in little-endian order (LSB first).
 * Example: bytes 0x02 0x02 -> uint16_t 0x0202 = 514
 *
 * Invalid/unavailable values are typically indicated by 0xFFFF.
 */
static inline uint16_t N2KToInt16(const uint8_t* data) {
  return data[0] | (data[1] << 8);  // little-endian uint16
}

void RecordPlayMgr::Init() {
  m_event_handler = new wxEvtHandler();
  m_timer = new VdrTimer(this);

  //  Get a pointer to the opencpn configuration object
  m_config = GetOCPNConfigObject();

  //  Load the configuration items
  LoadConfig();

  //  Set up NMEA 2000 listeners based on preferences
  UpdateNMEA2000Listeners();

  //  If auto-start is enabled, and we're not playing back and not using speed
  //  threshold, start recording after initialization.
  m_recording_manually_disabled = false;
  if (m_auto_start_recording && !m_use_speed_threshold && !IsPlaying()) {
    wxLogMessage("Auto-starting recording on plugin initialization");
    StartRecording();
  }

  m_tb_item_id_record =
      InsertPlugInToolSVG("VDR", g_svg_vdr_record, g_svg_record_toggled,
                          g_svg_record_toggled, wxITEM_CHECK, _("VDR Record"),
                          "", nullptr, kVdrToolPosition, 0, m_parent);
  m_tb_item_id_play = InsertPlugInToolSVG(
      "VDR", g_svg_vdr_play, g_svg_play_toggled, g_svg_play_toggled,
      wxITEM_CHECK, _("VDR Play"), "", nullptr, kVdrToolPosition, 0, m_parent);
  m_recording = false;
  SetToolbarToolStatus();
}

void RecordPlayMgr::DeInit() {
  SaveConfig();
  if (m_timer) {
    if (m_timer->IsRunning()) {
      m_timer->Stop();
      m_istream.Close();
    }
    delete m_timer;
    m_timer = nullptr;
  }

  if (m_recording) {
    m_ostream.Close();
    m_recording = false;
#ifdef __ANDROID__
    bool AndroidSecureCopyFile(wxString in, wxString out);
    AndroidSecureCopyFile(m_temp_outfile, m_final_outfile);
    ::wxRemoveFile(m_temp_outfile);
#endif
  }

  // Stop and cleanup all network servers.
  StopNetworkServers();
  m_network_servers.clear();

  RemovePlugInTool(m_tb_item_id_record);
  RemovePlugInTool(m_tb_item_id_play);

  if (m_event_handler) {
    m_event_handler->Unbind(EVT_N2K, &RecordPlayMgr::OnN2KEvent, this);
    m_event_handler->Unbind(EVT_SIGNALK, &RecordPlayMgr::OnSignalKEvent, this);
    delete m_event_handler;
    m_event_handler = nullptr;
  }
  m_n2k_listeners.clear();
  m_signalk_listeners.clear();
}

// Format timestamp: YYYY-MM-DDTHH:MM:SS.mmmZ
// The format combines ISO format with milliseconds in UTC.
// ts is assumed to be in UTC already.
wxString FormatIsoDateTime(const wxDateTime& ts) {
  return ts.Format("%Y-%m-%dT%H:%M:%S.%lZ");
}

RecordPlayMgr::RecordPlayMgr(opencpn_plugin* parent, VdrControlGui* control_gui)
    : m_dm_replay_mgr(std::make_unique<DataMonitorReplayMgr>()),
      m_parent(parent),
      m_control_gui(control_gui) {
  // Create the PlugIn icons
  InitializeImages();

  wxFileName fn;

  auto path = GetPluginDataDir("vdr_pi");
  fn.SetPath(path);
  fn.AppendDir("data");
  fn.SetFullName("vdr_panel_icon.png");

  path = fn.GetFullPath();

  wxInitAllImageHandlers();

  wxLogDebug(wxString("Using icon path: ") + path);
  if (!wxImage::CanRead(path)) {
    wxLogDebug("Initiating image handlers.");
    wxInitAllImageHandlers();
  }
  wxImage panelIcon(path);
  if (panelIcon.IsOk())
    m_panelBitmap = wxBitmap(panelIcon);
  else
    wxLogWarning("VDR panel icon has NOT been loaded");

  // Runtime variables
  m_recording = false;
  m_recording_paused = false;
  m_playing = false;
  m_is_csv_file = false;
  m_last_speed = 0.0;
  m_sentence_buffer.clear();
  m_messages_dropped = false;
}

void RecordPlayMgr::UpdateSignalKListeners() {
  m_event_handler->Unbind(EVT_SIGNALK, &RecordPlayMgr::OnSignalKEvent, this);
  m_signalk_listeners.clear();
  wxLogMessage("Configuring SignalK listeners. SignalK enabled: %d",
               m_protocols.signalK);
  if (m_protocols.signalK) {
    // TODO: Implement SignalK configuration.
  }
}

void RecordPlayMgr::OnSignalKEvent(wxCommandEvent& event) {
  if (!m_protocols.signalK) {
    // SignalK recording is disabled.
    return;
  }
  // TODO: Implement SignalK recording.
}

void RecordPlayMgr::OnN2KEvent(wxCommandEvent& event) {
  if (!m_protocols.nmea2000) {
    // NMEA 2000 recording is disabled.
    return;
  }

  auto& ev = dynamic_cast<ObservedEvt&>(event);
  // Get payload and source
  std::vector<uint8_t> payload = GetN2000Payload(0, ev);  // ID does not matter.
  // Extract PGN from payload (bytes 3-5, little endian)
  if (payload.size() < 6) {
    return;  // Not enough bytes for valid message
  }
  uint32_t pgn = payload[3] | (payload[4] << 8) | (payload[5] << 16);

  // Check for COG & SOG, Rapid Update PGN (129026)
  if (pgn == 129026) {
    // COG & SOG message format:
    // Byte 0: SID
    // Byte 1: COG Reference (0=True, 1=Magnetic)
    // Byte 2-5: COG (float, radians)
    // Byte 6-9: SOG (float, meters per second)
    if (payload.size() >= 19) {  // 11 header bytes + 8 data bytes
      // Extract SOG value (uint16, 2 bytes, little-endian)
      uint16_t raw_sog = N2KToInt16(&payload[17]);

      // Convert to m/s using NMEA 2000 resolution, then to knots.
      float speed_knots = (raw_sog * 0.01f) * 1.94384f;

      // Update last known speed.
      m_last_speed = speed_knots;

      // Check if we should start/stop recording based on speed.
      CheckAutoRecording(speed_knots);
    }
  }

  if (!m_recording) {
    return;
  }

  // Convert payload for logging
  wxString log_payload;
  for (size_t i = 0; i < payload.size(); i++) {
    log_payload += wxString::Format("%02X", payload[i]);
  }

  // Format N2K message for recording.
  wxString formatted_message;
  switch (m_data_format) {
    case VdrDataFormat::kCsv: {
      // CSV format: timestamp,type,id,payload
      // where "id" is the PGN number.
      wxString timestamp = FormatIsoDateTime(wxDateTime::UNow());
      formatted_message =
          wxString::Format("%s,NMEA2000,%d,%s\n", timestamp, pgn, log_payload);
      break;
    }
    case VdrDataFormat::kRawNmea:
      // PCDIN format: $PCDIN,<pgn>,<payload>
      formatted_message =
          wxString::Format("$PCDIN,%d,%s\r\n", pgn, log_payload);
      break;
  }

  // Check if we need to rotate the VDR file.
  CheckLogRotation();

  m_ostream.Write(formatted_message.ToStdString());
}

void RecordPlayMgr::UpdateNMEA2000Listeners() {
  m_event_handler->Unbind(EVT_N2K, &RecordPlayMgr::OnN2KEvent, this);
  m_n2k_listeners.clear();
  wxLogMessage("Configuring NMEA 2000 listeners. NMEA 2000 enabled: %d",
               m_protocols.nmea2000);
  if (m_protocols.nmea2000) {
    std::map<unsigned int, wxString> parameterGroupNumbers = {
        // System & ISO messages
        {59392, "ISO Acknowledgement"},
        {59904, "ISO Request"},
        {60160, "ISO Transport Protocol, Data Transfer"},
        {60416, "ISO Transport Protocol, Connection Management"},
        {60928, "ISO Address Claim"},
        {61184, "Manufacturer Proprietary Single Frame"},
        {65280, "Manufacturer Proprietary Single Frame"},

        // B&G Proprietary
        {65305, "B&G AC12 Autopilot Status"},
        {65309, "B&G WS320 Wind Sensor Battery Status"},
        {65312, "B&G WS320 Wind Sensor Wireless Status"},
        {65340, "B&G AC12 Autopilot Mode"},
        {65341, "B&G AC12 Wind Angle"},

        // Time & Navigation
        {126992, "System Time"},
        {127233, "MOB (Man Overboard) Data"},
        {127237, "Heading/Track Control"},
        {127245, "Rudder Angle"},
        {127250, "Vessel Heading"},
        {127251, "Rate of Turn"},
        {127252, "Heave"},
        {127257, "Vessel Attitude (Roll/Pitch)"},
        {127258, "Magnetic Variation"},
        {128259, "Speed Through Water"},
        {128267, "Water Depth Below Transducer"},
        {128275, "Distance Log (Total/Trip)"},
        {128777, "Anchor Windlass Status"},
        {129025, "Position Rapid Update (Lat/Lon)"},
        {129026, "Course/Speed Over Ground (COG/SOG)"},
        {129029, "GNSS Position Data"},
        {129283, "Cross Track Error"},
        {129284, "Navigation Data (WP Info)"},
        {129285, "Navigation Route/WP Info"},
        {129540, "GNSS Satellites in View"},
        {130577, "Direction Data (Set/Drift)"},

        // AIS
        {129038, "AIS Class A Position Report"},
        {129039, "AIS Class B Position Report"},
        {129793, "AIS UTC and Date Report"},
        {129794, "AIS Class A Static Data"},
        {129798, "AIS SAR Aircraft Position"},
        {129802, "AIS Safety Broadcast"},

        // Environmental & Systems
        {127488, "Engine Parameters, Rapid"},
        {127489, "Engine Parameters, Dynamic"},
        {127505, "Fluid Level"},
        {127508, "Battery Status"},
        {130306, "Wind Speed/Angle"},
        {130310, "Environmental Parameters (Air/Water)"},
        {130311, "Environmental Parameters (Alt Format)"},
        {130313, "Humidity"},
        {130314, "Actual Pressure"},
        {130316, "Temperature Extended Range"}};

    for (const auto& it : parameterGroupNumbers) {
      m_n2k_listeners.push_back(
          GetListener(NMEA2000Id(it.first), EVT_N2K, m_event_handler));
    }

    m_event_handler->Bind(EVT_N2K, &RecordPlayMgr::OnN2KEvent, this);
  }
}

/**
 * Converts 4 bytes of NMEA 2000 data to a 32-bit IEEE 754 floating point number
 *
 * @param data Pointer to bytes in the NMEA 2000 message (4 bytes,
 * little-endian)
 * @return The float value represented by the bytes
 *
 * Data is stored in little-endian IEEE 754 single-precision format.
 * Bytes are combined into a uint32_t and then reinterpreted as a float using
 * memcpy to avoid strict aliasing violations.
 *
 * Example: bytes 0x00 0x00 0x80 0x3F -> float 1.0
 *
 * Note: Some NMEA 2000 fields use scaled integers instead of floats.
 * Verify the PGN specification before using this function.
 */
inline float N2KToFloat(const uint8_t* data) {
  float result;
  uint32_t temp = (static_cast<uint32_t>(data[0])) |
                  (static_cast<uint32_t>(data[1]) << 8) |
                  (static_cast<uint32_t>(data[2]) << 16) |
                  (static_cast<uint32_t>(data[3]) << 24);
  std::memcpy(&result, &temp, sizeof(float));
  return result;
}

wxString RecordPlayMgr::FormatNmea0183AsCsv(const wxString& nmea) {
  // Get current time with millisecond precision
  wxString timestamp = FormatIsoDateTime(wxDateTime::UNow());

  wxString type = "NMEA0183";
  if (nmea.StartsWith("!")) {
    type = "AIS";
  }

  // Escape any commas in the NMEA message
  wxString escaped = nmea.Strip(wxString::both);
  escaped.Replace("\"", "\"\"");
  escaped = wxString::Format("\"%s\"", escaped);

  // Format CSV line: timestamp,type,id,message
  return wxString::Format("%s,%s,,%s\n", timestamp, type, escaped);
}

void RecordPlayMgr::SetNMEASentence(wxString& sentence) {
  if (!m_protocols.nmea0183) {
    // Recording of NMEA 0183 is disabled.
    return;
  }
  // Check for RMC sentence to get speed and check for auto-recording.
  // There can be different talkers on the stream so look at the message type
  // irrespective of the talker.
  if (sentence.size() >= 6 && sentence.substr(3, 3) == "RMC") {
    wxStringTokenizer tkz(sentence, ",");
    wxString token;

    // Skip to speed field (field 7), which is the speed over ground in knots.
    for (int i = 0; i < 7 && tkz.HasMoreTokens(); i++) {
      token = tkz.GetNextToken();
    }

    if (tkz.HasMoreTokens()) {
      token = tkz.GetNextToken();
      if (!token.IsEmpty()) {
        double speed;
        if (token.ToDouble(&speed)) {
          m_last_speed = speed;
          CheckAutoRecording(speed);
        }
      }
    }
  }

  // Only record if recording is active (whether manual or automatic)
  if (!m_recording || m_recording_paused) return;

  // Check if we need to rotate the VDR file.
  CheckLogRotation();

  wxString normalized_sentence = sentence;
  normalized_sentence.Trim(true);

  switch (m_data_format) {
    case VdrDataFormat::kCsv:
      m_ostream.Write(FormatNmea0183AsCsv(normalized_sentence));
      break;
    case VdrDataFormat::kRawNmea:
    default:
      if (!normalized_sentence.EndsWith("\r\n")) {
        normalized_sentence += "\r\n";
      }
      m_ostream.Write(normalized_sentence);
      break;
  }
}

void RecordPlayMgr::SetAISSentence(wxString& sentence) {
  SetNMEASentence(sentence);  // Handle the same way as NMEA
}

const ConnectionSettings& RecordPlayMgr::GetNetworkSettings(
    const wxString& protocol) const {
  if (protocol == "N2K")
    return m_protocols.n2kNet;
  else if (protocol == "NMEA0183")
    return m_protocols.nmea0183Net;
  else if (protocol == "SignalK")
    return m_protocols.signalkNet;

  // Default to NMEA0183 if unknown protocol
  return m_protocols.nmea0183Net;
}

void RecordPlayMgr::CheckAutoRecording(double speed) {
  if (!m_auto_start_recording) {
    // If auto-recording is disabled in settings, do nothing.
    return;
  }

  if (IsPlaying()) {
    // If playback is active, no recording allowed.
    return;
  }

  if (!m_use_speed_threshold) {
    // If we're not using speed threshold, nothing to check.
    return;
  }

  // If speed drops below threshold, clear the manual disable flag.
  if (speed < m_speed_threshold) {
    if (m_recording_manually_disabled) {
      m_recording_manually_disabled = false;
      wxLogMessage("Re-enabling auto-recording capability");
    }
  }

  if (m_recording_manually_disabled) {
    // Don't auto-record if manually disabled.
    return;
  }

  if (speed >= m_speed_threshold) {
    // Reset the below-threshold timer when speed goes above threshold.
    m_below_threshold_since = wxDateTime();
    if (!m_recording) {
      wxLogMessage("Start recording, speed %.2f exceeds threshold %.2f", speed,
                   m_speed_threshold);
      StartRecording();
    } else if (m_recording_paused) {
      wxLogMessage("Resume recording, speed %.2f exceeds threshold %.2f", speed,
                   m_speed_threshold);
      ResumeRecording();
    }
  } else if (m_recording) {
    // Add hysteresis to prevent rapid starting/stopping
    static constexpr double kHysteresis = 0.2;  // 0.2 knots below threshold
    if (speed < (m_speed_threshold - kHysteresis)) {
      // If we're recording, and it was auto-started, handle stop delay
      if (!m_below_threshold_since.IsValid()) {
        m_below_threshold_since = wxDateTime::Now().ToUTC();
        wxLogMessage(
            "Speed dropped below threshold, starting pause delay timer");
      } else {
        // Check if enough time has passed
        wxTimeSpan time_below =
            wxDateTime::Now().ToUTC() - m_below_threshold_since;
        if (time_below.GetMinutes() >= m_stop_delay) {
          wxLogMessage(
              "Pause recording, speed %.2f below threshold %.2f for %d minutes",
              speed, m_speed_threshold, m_stop_delay);
          PauseRecording("Speed dropped below threshold");
          m_below_threshold_since = wxDateTime();  // Reset timer
        }
      }
    }
  }
  SetToolbarToolStatus();
}

bool RecordPlayMgr::IsNmea0183OrAis(const wxString& line) {
  // NMEA sentences start with $ or !
  return line.StartsWith("$") || line.StartsWith("!");
}

bool RecordPlayMgr::ParseCSVHeader(const wxString& header) {
  // Reset indices
  constexpr unsigned kInvalidIndex = std::numeric_limits<unsigned int>::max();
  m_timestamp_idx = kInvalidIndex;
  m_message_idx = kInvalidIndex;
  m_header_fields.Clear();

  // If it looks like NMEA/AIS, it's not a header
  if (IsNmea0183OrAis(header)) {
    return false;
  }

  // Split the header line
  wxStringTokenizer tokens(header, ",");
  unsigned int idx = 0;

  while (tokens.HasMoreTokens()) {
    wxString field = tokens.GetNextToken().Trim(true).Trim(false).Lower();
    m_header_fields.Add(field);

    // Look for key fields
    if (field.Contains("timestamp")) {
      m_timestamp_idx = idx;
    } else if (field.Contains("message")) {
      m_message_idx = idx;
    }
    idx++;
  }
  return (m_timestamp_idx != kInvalidIndex && m_message_idx != kInvalidIndex);
}

bool RecordPlayMgr::ParseCSVLineTimestamp(const wxString& line,
                                          wxString* message,
                                          wxDateTime* timestamp) {
  assert(m_is_csv_file);
  return m_timestamp_parser.ParseCsvLineTimestamp(
      line, m_timestamp_idx, m_message_idx, message, timestamp);
}

void RecordPlayMgr::FlushSentenceBuffer() {
  for (const auto& sentence : m_sentence_buffer) {
    PushNMEABuffer(sentence + "\r\n");
  }
  m_sentence_buffer.clear();
}

double RecordPlayMgr::GetSpeedMultiplier() const {
  return m_control_gui ? m_control_gui->GetSpeedMultiplier() : 1.0;
}

void RecordPlayMgr::Notify() {
  if (m_protocols.replay_mode == ReplayMode::kLoopback) {
    if (m_control_gui) m_control_gui->SetProgress(GetProgressFraction());
    int delay = m_dm_replay_mgr->Notify();
    if (delay >= 0) m_timer->Start(delay, wxTIMER_ONE_SHOT);
    return;
  }
  if (!m_istream.IsOpened()) return;

  wxDateTime now = wxDateTime::UNow();
  wxDateTime target_time;
  bool behind_schedule = true;
  int precision;

  // For non-timestamped files, base rate of 10 messages/second
  constexpr int kBaseMessagesPerBatch = 10;
  constexpr int kBaseIntervalMs = 1000;  // 1 second

  // Keep processing messages until we catch up with scheduled time.
  while (behind_schedule && !m_istream.Eof()) {
    wxString line;
    int pos = static_cast<int>(m_istream.GetCurrentLine());

    if (pos == -1) {
      // First line - check if it's CSV.
      line = GetNextNonEmptyLine(true);
      m_is_csv_file = ParseCSVHeader(line);
      if (m_is_csv_file) {
        // Get first data line.
        line = GetNextNonEmptyLine();
      } else {
        // For non-CSV, process the first line as NMEA.
        // Reset to start of file.
        line = GetNextNonEmptyLine(true /* fromStart */);
      }
    } else {
      line = GetNextNonEmptyLine();
    }

    if (m_istream.Eof() && line.IsEmpty()) {
      m_at_file_end = true;
      PausePlayback();
      if (m_control_gui) {
        m_control_gui->UpdateControls();
      }
      return;
    }

    // Parse the line according to detected format (CSV or raw NMEA/AIS).
    wxDateTime timestamp;
    wxString nmea;
    bool msg_has_timestamp = false;

    if (m_is_csv_file) {
      bool success = ParseCSVLineTimestamp(line, &nmea, &timestamp);
      if (success) {
        nmea += "\r\n";
        msg_has_timestamp = true;
      }
    } else {
      nmea = line + "\r\n";
      msg_has_timestamp =
          m_timestamp_parser.ParseTimestamp(line, timestamp, precision);
    }

    if (!nmea.IsEmpty()) {
      if (m_protocols.replay_mode == ReplayMode::kInternalApi) {
        // Add sentence to buffer, maintaining max size.
        m_sentence_buffer.push_back(nmea);
      }

      // Send through network if enabled.
      HandleNetworkPlayback(nmea);

      if (msg_has_timestamp) {
        // The current sentence has a timestamp from the primary time source.
        m_current_timestamp = timestamp;
        target_time = GetNextPlaybackTime();
        // Check if we've caught up to schedule.
        if (target_time.IsValid() && target_time > now) {
          behind_schedule = false;  // This will break the loop.
          // Before scheduling next update, flush our sentence buffer.
          FlushSentenceBuffer();
          // Schedule next notification.
          wxTimeSpan wait_time = target_time - now;
          m_timer->Start(
              static_cast<int>(wait_time.GetMilliseconds().ToDouble()),
              wxTIMER_ONE_SHOT);
        }
      } else if (!HasValidTimestamps() &&
                 m_sentence_buffer.size() >= kBaseMessagesPerBatch) {
        // For files that do not have timestamped records (or timestamps are not
        // in chronological order), use batch processing.
        behind_schedule = false;  // This will break the loop.
        FlushSentenceBuffer();

        // Calculate interval based on speed multiplier
        int interval = static_cast<int>(kBaseIntervalMs / GetSpeedMultiplier());

        // Schedule next batch.
        m_timer->Start(interval, wxTIMER_ONE_SHOT);
      }

      if (m_sentence_buffer.size() > kMaxBufferSize) {
        if (!m_messages_dropped) {
          wxLogMessage(
              "Playback dropping messages to maintain timing at %.0fx speed",
              GetSpeedMultiplier());
          m_messages_dropped = true;
        }
        m_sentence_buffer.pop_front();
      }
    }
  }

  // Update progress regardless of file type.
  if (m_control_gui) {
    m_control_gui->SetProgress(GetProgressFraction());
  }
}

void RecordPlayMgr::OnVdrMsg(VdrMsgType type, const std::string msg) {
  switch (type) {
    case VdrMsgType::kDebug:
      wxLogDebug(wxString(msg));
      break;
    case VdrMsgType::kMessage:
      wxLogMessage(wxString(msg));
      break;
    case VdrMsgType::kInfo:
      OCPNMessageBox_PlugIn(wxTheApp->GetTopWindow(), msg);
      break;
  }
}

wxDateTime RecordPlayMgr::GetNextPlaybackTime() const {
  if (!m_current_timestamp.IsValid() || !m_first_timestamp.IsValid() ||
      !m_playback_base_time.IsValid()) {
    return {};  // Return invalid time if we don't have valid timestamps
  }
  // Calculate when this message should be played relative to playback start.
  wxTimeSpan elapsed_time = m_current_timestamp - m_first_timestamp;
  wxLongLong ms = elapsed_time.GetMilliseconds();
  double scaled_ms = ms.ToDouble() / GetSpeedMultiplier();
  wxTimeSpan scaled_elapsed =
      wxTimeSpan::Milliseconds(static_cast<long>(scaled_ms));
  return m_playback_base_time + scaled_elapsed;
}

void RecordPlayMgr::OnToolbarToolCallback(int id) {
  auto& control_pane = GetFrameAuiManager()->GetPane(kControlWinName);

  if (id == m_tb_item_id_play) {
    // Don't allow playback while recording
    if (m_recording) {
      wxMessageBox(_("Stop recording before starting playback."),
                   _("VDR Plugin"), wxOK | wxICON_INFORMATION);
      SetToolbarItemState(id, false);
      return;
    }
    // Check if the toolbar button is being toggled off
    // if (m_callbacks.get_control()) {
    if (control_pane.IsShown()) {
      // Stop any active playback
      if (m_timer->IsRunning()) {
        m_timer->Stop();
        m_istream.Close();
      }

      // Hide control window
      control_pane.Hide();
      GetFrameAuiManager()->Update();

      // Update toolbar state
      SetToolbarToolStatus();
      return;
    }

    // if (!m_callbacks.get_control()) {
    if (!control_pane.IsShown()) {
      control_pane.Show();
      if (m_protocols.replay_mode == ReplayMode::kLoopback) {
        m_control_gui->EnableSpeedSlider(false);
      }
    } else {  // collapse
      control_pane.Show(!control_pane.IsShown());
    }
    GetFrameAuiManager()->Update();
  } else if (id == m_tb_item_id_record) {
    // Don't allow recording while playing
    if (m_timer->IsRunning()) {
      wxMessageBox(_("Stop playback before starting recording."),
                   _("VDR Plugin"), wxOK | wxICON_INFORMATION);
      SetToolbarToolStatus();
      return;
    }
    if (m_recording) {
      StopRecording("Recording stopped manually");
      // Recording was stopped manually, so disable auto-recording
      m_recording_manually_disabled = true;
    } else {
      StartRecording();
      if (m_recording) {
        // Cannot really happen, remove FIXME (leamas)
        // Only set button state if recording started
        // successfully
        m_recording_manually_disabled = false;
      }
    }
  }
  SetToolbarToolStatus();
}

bool RecordPlayMgr::IsPlaying() const {
  if (m_protocols.replay_mode == ReplayMode::kLoopback)
    return m_dm_replay_mgr->IsPlaying();
  return m_playing;
}

bool RecordPlayMgr::IsError() const {
  if (m_protocols.replay_mode == ReplayMode::kLoopback)
    return m_dm_replay_mgr->IsError();
  return false;
}

bool RecordPlayMgr::IsAtFileEnd() const {
  if (m_protocols.replay_mode == ReplayMode::kLoopback)
    return m_dm_replay_mgr->IsAtEnd();
  return m_at_file_end;
}

wxDateTime RecordPlayMgr::GetCurrentTimestamp() const {
  if (m_protocols.replay_mode != ReplayMode::kLoopback)
    return m_current_timestamp;

  uint64_t stamp = m_dm_replay_mgr->GetCurrentTimestamp();
  wxDateTime date_time(time_t(stamp / 1000));
  date_time.SetMillisecond(stamp % 1000);
  return date_time;
}

void RecordPlayMgr::SetColorScheme(PI_ColorScheme cs) {
  if (m_control_gui) {
    m_control_gui->SetColorScheme(cs);
  }
}

wxString RecordPlayMgr::GenerateFilename() const {
  wxDateTime now = wxDateTime::Now().ToUTC();
  wxString timestamp = now.Format("%Y%m%dT%H%M%SZ");
  wxString extension = (m_data_format == VdrDataFormat::kCsv) ? ".csv" : ".txt";
  return "vdr_" + timestamp + extension;
}

bool RecordPlayMgr::LoadConfig() {
  auto* config = (wxFileConfig*)m_config;

  if (!config) return false;

  config->SetPath("/PlugIns/VDR");
  config->Read("InputFilename", &m_input_file, "");
  config->Read("OutputFilename", &m_ofilename, "");

  // Default directory handling based on platform
#ifdef __ANDROID__
  wxString default_dir =
      "/storage/emulated/0/Android/data/org.opencpn.opencpn/files";
#else
  wxString default_dir = *GetpPrivateApplicationDataLocation();
#endif

  // Recording preferences.
  config->Read("RecordingDirectory", &m_recording_dir, default_dir);
  config->Read("Interval", &m_interval, 1000);
  config->Read("LogRotate", &m_log_rotate, false);
  config->Read("LogRotateInterval", &m_log_rotate_interval, 24);
  config->Read("AutoStartRecording", &m_auto_start_recording, false);
  config->Read("UseSpeedThreshold", &m_use_speed_threshold, false);
  config->Read("SpeedThreshold", &m_speed_threshold, 0.5);
  config->Read("StopDelay", &m_stop_delay, 10);  // Default 10 minutes

  config->Read("EnableNMEA0183", &m_protocols.nmea0183, true);
  config->Read("EnableNMEA2000", &m_protocols.nmea2000, false);
  config->Read("EnableSignalK", &m_protocols.signalK, false);

  int format;
  config->Read("DataFormat", &format,
               static_cast<int>(VdrDataFormat::kRawNmea));
  m_data_format = static_cast<VdrDataFormat>(format);

  // Replay preferences.
  int replay_mode;
  config->Read("NMEA0183ReplayMode", &replay_mode,
               static_cast<int>(ReplayMode::kInternalApi));
  m_protocols.replay_mode = static_cast<ReplayMode>(replay_mode);

  // NMEA 0183 network settings
  config->Read("NMEA0183_UseTCP", &m_protocols.nmea0183Net.use_tcp, false);
  config->Read("NMEA0183_Port", &m_protocols.nmea0183Net.port, 10111);
  config->Read("NMEA0183_Enabled", &m_protocols.nmea0183Net.enabled, false);

  // NMEA 2000 network settings
  config->Read("NMEA2000_UseTCP", &m_protocols.n2kNet.use_tcp, false);
  config->Read("NMEA2000_Port", &m_protocols.n2kNet.port, 10112);
  config->Read("NMEA2000_Enabled", &m_protocols.n2kNet.enabled, false);

#if 0
  // Signal K network settings
  config->Read("SignalK_UseTCP", &m_protocols.signalkNet.use_tcp, true);
  config->Read("SignalK_Port", &m_protocols.signalkNet.port, 8375);
  config->Read("SignalK_Enabled", &m_protocols.signalkNet.enabled, false);
#endif

  return true;
}

bool RecordPlayMgr::SaveConfig() {
  auto* config = (wxFileConfig*)m_config;

  if (!config) return false;

  config->SetPath("/PlugIns/VDR");

  // Recording preferences.
  config->Write("InputFilename", m_input_file);
  config->Write("OutputFilename", m_ofilename);
  config->Write("RecordingDirectory", m_recording_dir);
  config->Write("Interval", m_interval);
  config->Write("LogRotate", m_log_rotate);
  config->Write("LogRotateInterval", m_log_rotate_interval);
  config->Write("AutoStartRecording", m_auto_start_recording);
  config->Write("UseSpeedThreshold", m_use_speed_threshold);
  config->Write("SpeedThreshold", m_speed_threshold);
  config->Write("StopDelay", m_stop_delay);
  config->Write("DataFormat", static_cast<int>(m_data_format));

  config->Write("EnableNMEA0183", m_protocols.nmea0183);
  config->Write("EnableNMEA2000", m_protocols.nmea2000);
  config->Write("EnableSignalK", m_protocols.signalK);

  // Replay preferences.
  config->Write("NMEA0183ReplayMode",
                static_cast<int>(m_protocols.replay_mode));

  // NMEA 0183 network settings
  config->Write("NMEA0183_UseTCP", m_protocols.nmea0183Net.use_tcp);
  config->Write("NMEA0183_Port", m_protocols.nmea0183Net.port);
  config->Write("NMEA0183_Enabled", m_protocols.nmea0183Net.enabled);

  // NMEA 2000 network settings
  config->Write("NMEA2000_UseTCP", m_protocols.n2kNet.use_tcp);
  config->Write("NMEA2000_Port", m_protocols.n2kNet.port);
  config->Write("NMEA2000_Enabled", m_protocols.n2kNet.enabled);

#if 0
  // Signal K network settings
  config->Write("SignalK_UseTCP", m_protocols.signalkNet.use_tcp);
  config->Write("SignalK_Port", m_protocols.signalkNet.port);
  config->Write("SignalK_Enabled", m_protocols.signalkNet.enabled);
#endif

  return true;
}

void RecordPlayMgr::StartRecording() {
  if (m_recording && !m_recording_paused) return;

  // Don't start recording if playback is active
  if (IsPlaying()) {
    wxLogMessage("Cannot start recording while playback is active");
    return;
  }

  // If we're just resuming a paused recording, don't create a new file.
  if (m_recording_paused) {
    wxLogMessage("Resume paused recording");
    m_recording_paused = false;
    m_recording = true;
    return;
  }

  // Generate filename based on current date/time
  wxString filename = GenerateFilename();
  wxString fullpath = wxFileName(m_recording_dir, filename).GetFullPath();

#ifdef __ANDROID__
  // For Android, we need to use the temp file for writing, but keep track of
  // the final location
  m_temp_outfile = *GetpPrivateApplicationDataLocation();
  m_temp_outfile += wxString("/vdr_temp") +
                    (m_data_format == VdrDataFormat::kCsv ? ".csv" : ".txt");
  m_final_outfile = "/storage/emulated/0/Android/Documents/" + filename;
  fullpath = m_temp_outfile;
#endif

  // Ensure directory exists
  if (!wxDirExists(m_recording_dir)) {
    if (!wxMkdir(m_recording_dir)) {
      wxLogError("Failed to create recording directory: %s", m_recording_dir);
      return;
    }
  }

  if (!m_ostream.Open(fullpath, wxFile::write)) {
    wxLogError("Failed to create recording file: %s", fullpath);
    return;
  }
  wxLogMessage("Start recording to file: %s", fullpath);

  // Write CSV header if needed
  if (m_data_format == VdrDataFormat::kCsv) {
    m_ostream.Write("timestamp,type,id,message\n");
  }

  m_recording = true;
  m_recording_paused = false;
  m_recording_start = wxDateTime::Now().ToUTC();
  m_current_recording_start = m_recording_start;
}

void RecordPlayMgr::PauseRecording(const wxString& reason) {
  if (!m_recording || m_recording_paused) return;

  wxLogMessage("Pause recording. Reason: %s", reason);
  m_recording_paused = true;
  m_recording_pause_time = wxDateTime::Now().ToUTC();
}

void RecordPlayMgr::ResumeRecording() {
  if (!m_recording_paused) return;
  m_recording_paused = false;
}

void RecordPlayMgr::StopRecording(const wxString& reason) {
  if (!m_recording) return;
  wxLogMessage("Stop recording. Reason: %s", reason);
  m_ostream.Close();
  m_recording = false;

#ifdef __ANDROID__
  bool AndroidSecureCopyFile(wxString in, wxString out);
  AndroidSecureCopyFile(m_temp_outfile, m_final_outfile);
  ::wxRemoveFile(m_temp_outfile);
#endif
}

void RecordPlayMgr::AdjustPlaybackBaseTime() {
  if (!m_first_timestamp.IsValid() || !m_current_timestamp.IsValid()) {
    return;
  }

  // Calculate how much time has "elapsed" in the recording up to our current
  // position.
  wxTimeSpan elapsed = m_current_timestamp - m_first_timestamp;

  // Set base time so that current playback position corresponds to current wall
  // clock.
  m_playback_base_time =
      wxDateTime::UNow() -
      wxTimeSpan::Milliseconds(static_cast<long>(
          (elapsed.GetMilliseconds().ToDouble() / GetSpeedMultiplier())));
}

void RecordPlayMgr::StartPlayback(wxString& file_status) {
  if (m_input_file.IsEmpty()) {
    file_status = _("No file selected.");
    // m_control_gui->UpdateFileStatus(_("No file selected."));
    return;
  }
  if (!wxFileExists(m_input_file)) {
    file_status = _("File does not exist.");
    // m_control_gui->UpdateFileStatus(_("File does not exist."));
    return;
  }
  if (m_protocols.replay_mode == ReplayMode::kLoopback) {
    if (!m_dm_replay_mgr->IsPaused()) m_dm_replay_mgr = DmReplayMgrFactory();
    m_dm_replay_mgr->Start();
    if (m_dm_replay_mgr->IsPlaying())
      file_status = _("File successfully loaded");
    // m_control_gui->UpdateFileStatus(_("File successfully loaded"));
    Notify();
    return;
  }

  // Reset end-of-file state when starting playback
  m_at_file_end = false;

  // Always adjust base time when starting playback, whether from pause or seek
  AdjustPlaybackBaseTime();

  if (!m_istream.IsOpened()) {
    if (!m_istream.Open(m_input_file)) {
      file_status = _("Failed to open file.");
      // m_control_gui->UpdateFileStatus(_("Failed to open file."));
      return;
    }
  }
  m_messages_dropped = false;
  m_playing = true;

  // Initialize network servers if needed
  if (!InitializeNetworkServers()) {
    // Continue playback even if network server initialization fails
    // The user has been notified via error messages in InitializeNetworkServers
    wxLogWarning("Continuing playback with failed network servers");
  }

  if (m_control_gui) {
    m_control_gui->SetProgress(GetProgressFraction());
    m_control_gui->UpdateControls();
    m_control_gui->UpdateFileLabel(m_input_file);
  }
  wxLogMessage(
      "Start playback from file: %s. Progress: %.2f. Has timestamps: %d",
      m_input_file, GetProgressFraction(), m_has_timestamps);
  // Process first line immediately.
  m_istream.GoToLine(-1);

  Notify();
}

void RecordPlayMgr::PausePlayback() {
  if (m_protocols.replay_mode == ReplayMode::kLoopback) {
    m_dm_replay_mgr->Pause();
    if (m_control_gui) m_control_gui->UpdateControls();
    return;
  }

  if (!m_playing) return;

  m_timer->Stop();
  m_playing = false;
  if (m_control_gui) m_control_gui->UpdateControls();
}

void RecordPlayMgr::StopPlayback() {
  if (!m_playing) return;

  m_timer->Stop();
  m_playing = false;
  m_istream.Close();

  // Stop all network servers
  StopNetworkServers();

  if (m_control_gui) {
    m_control_gui->SetProgress(0);
    m_control_gui->UpdateControls();
    m_control_gui->UpdateFileLabel("");
  }
}

VdrNetworkServer* RecordPlayMgr::GetServer(const wxString& protocol) {
  auto it = m_network_servers.find(protocol);
  if (it == m_network_servers.end()) {
    // Create new server instance if it doesn't exist.
    auto server = std::make_unique<VdrNetworkServer>();
    VdrNetworkServer* serverPtr = server.get();
    m_network_servers[protocol] = std::move(server);
    return serverPtr;  // FIXME (leamas) giving away raw pointer
  }
  return it->second.get();
}

bool RecordPlayMgr::InitializeNetworkServers() {
  bool success = true;
  wxString errors;

  // Initialize NMEA0183 network server if needed
  if (m_protocols.nmea0183Net.enabled) {
    VdrNetworkServer* server = GetServer("NMEA0183");
    if (!server->IsRunning() ||
        server->IsTCP() != m_protocols.nmea0183Net.use_tcp ||
        server->GetPort() != m_protocols.nmea0183Net.port) {
      server->Stop();  // Stop existing server if running
      wxString error;
      if (!server->Start(m_protocols.nmea0183Net.use_tcp,
                         m_protocols.nmea0183Net.port, error)) {
        success = false;
        errors += error;
      } else {
        wxLogMessage("Started NMEA0183 server: %s on port %d",
                     m_protocols.nmea0183Net.use_tcp ? "TCP" : "UDP",
                     m_protocols.nmea0183Net.port);
      }
    }
  } else {
    VdrNetworkServer* server = GetServer("NMEA0183");
    if (server->IsRunning()) {
      server->Stop();
      wxLogMessage("Stopped NMEA0183 network server (disabled in preferences)");
    }
  }

  // Initialize NMEA2000 network server if needed
  if (m_protocols.n2kNet.enabled) {
    VdrNetworkServer* server = GetServer("N2K");
    if (!server->IsRunning() || server->IsTCP() != m_protocols.n2kNet.use_tcp ||
        server->GetPort() != m_protocols.n2kNet.port) {
      server->Stop();  // Stop existing server if running
      wxString error;
      if (!server->Start(m_protocols.n2kNet.use_tcp, m_protocols.n2kNet.port,
                         error)) {
        success = false;
        errors += error;
      } else {
        wxLogMessage("Started NMEA2000 server: %s on port %d",
                     m_protocols.n2kNet.use_tcp ? "TCP" : "UDP",
                     m_protocols.n2kNet.port);
      }
    }
  } else {
    VdrNetworkServer* server = GetServer("N2K");
    if (server->IsRunning()) {
      server->Stop();
      wxLogMessage("Stopped NMEA2000 network server (disabled in preferences)");
    }
  }

  if (m_control_gui) {
    if (!success) {
      m_control_gui->UpdateNetworkStatus(errors);
    } else {
      m_control_gui->UpdateNetworkStatus("");
    }
  }

  return success;
}

void RecordPlayMgr::StopNetworkServers() {
  // Stop NMEA0183 server if running
  if (VdrNetworkServer* server = GetServer("NMEA0183")) {
    if (server->IsRunning()) {
      server->Stop();
      wxLogMessage("Stopped NMEA0183 network server");
    }
  }

  // Stop NMEA2000 server if running
  if (VdrNetworkServer* server = GetServer("N2K")) {
    if (server->IsRunning()) {
      server->Stop();
      wxLogMessage("Stopped NMEA2000 network server");
    }
  }
}

void RecordPlayMgr::HandleNetworkPlayback(const wxString& data) {
  // For NMEA 0183 data
  if (m_protocols.nmea0183Net.enabled &&
      (data.StartsWith("$") || data.StartsWith("!"))) {
    VdrNetworkServer* server = GetServer("NMEA0183");
    if (server && server->IsRunning()) {
      server->SendText(data);  // Use SendText() for NMEA messages
    }
  }
  // For NMEA 2000 data in various text formats
  else if (m_protocols.n2kNet.enabled &&
           (data.StartsWith("$PCDIN") ||   // SeaSmart
            data.StartsWith("!AIVDM") ||   // Actisense ASCII
            data.StartsWith("$MXPGN") ||   // MiniPlex
            data.StartsWith("$YDRAW"))) {  // YD RAW
    VdrNetworkServer* server = GetServer("N2K");
    if (server && server->IsRunning()) {
      server->SendText(data);  // Use SendText() for text-based formats
    }
  }
}

void RecordPlayMgr::SetDataFormat(VdrDataFormat format) {
  // If format hasn't changed, do nothing.
  if (format == m_data_format) {
    return;
  }

  if (m_recording) {
    // If recording is active, we need to handle the transition,
    // e.g., from CSV to raw NMEA. A new file will be created.
    wxDateTime recording_start = m_recording_start;
    wxString current_dir = m_recording_dir;
    StopRecording("Changing output data format");
    m_data_format = format;
    // Start new recording
    m_recording_start = recording_start;  // Preserve original start time
    m_recording_dir = current_dir;
    StartRecording();
  } else {
    // Simply update the format if not recording.
    m_data_format = format;
  }
}

void RecordPlayMgr::ShowPreferencesDialog(wxWindow* parent) {
  VdrPrefsDialog dlg(parent, wxID_ANY, m_data_format, m_recording_dir,
                     m_log_rotate, m_log_rotate_interval,
                     m_auto_start_recording, m_use_speed_threshold,
                     m_speed_threshold, m_stop_delay, m_protocols);
#ifdef __WXQT__  // Android
  if (parent) {
    int xmax = parent->GetSize().GetWidth();
    int ymax = parent->GetParent()
                   ->GetSize()
                   .GetHeight();  // This would be the Options dialog itself
    dlg.SetSize(xmax, ymax);
    dlg.Layout();
    dlg.Move(0, 0);
  }
#endif

  if (dlg.ShowModal() == wxID_OK) {
    bool previous_nmea2000_state = m_protocols.nmea2000;
    bool previous_signal_k_state = m_protocols.signalK;
    SetDataFormat(dlg.GetDataFormat());
    SetRecordingDir(dlg.GetRecordingDir());
    SetLogRotate(dlg.GetLogRotate());
    SetLogRotateInterval(dlg.GetLogRotateInterval());
    SetAutoStartRecording(dlg.GetAutoStartRecording());
    SetUseSpeedThreshold(dlg.GetUseSpeedThreshold());
    SetSpeedThreshold(dlg.GetSpeedThreshold());
    SetStopDelay(dlg.GetStopDelay());
    m_protocols = dlg.GetProtocolSettings();
    SaveConfig();

    // Update NMEA 2000 listeners if the setting changed
    if (previous_nmea2000_state != m_protocols.nmea2000) {
      UpdateNMEA2000Listeners();
    }
    if (previous_signal_k_state != m_protocols.signalK) {
      UpdateSignalKListeners();
    }

    // Update UI if needed
    if (m_control_gui) {
      m_control_gui->UpdateControls();
    }
  }
}

void RecordPlayMgr::ShowPreferencesDialogNative(wxWindow* parent) {
  VdrPrefsDialog dlg(parent, wxID_ANY, m_data_format, m_recording_dir,
                     m_log_rotate, m_log_rotate_interval,
                     m_auto_start_recording, m_use_speed_threshold,
                     m_speed_threshold, m_stop_delay, m_protocols);

  if (dlg.ShowModal() == wxID_OK) {
    bool previous_nmea2000_state = m_protocols.nmea2000;
    bool previous_signal_k_state = m_protocols.signalK;
    SetDataFormat(dlg.GetDataFormat());
    SetRecordingDir(dlg.GetRecordingDir());
    SetLogRotate(dlg.GetLogRotate());
    SetLogRotateInterval(dlg.GetLogRotateInterval());
    SetAutoStartRecording(dlg.GetAutoStartRecording());
    SetUseSpeedThreshold(dlg.GetUseSpeedThreshold());
    SetSpeedThreshold(dlg.GetSpeedThreshold());
    SetStopDelay(dlg.GetStopDelay());
    m_protocols = dlg.GetProtocolSettings();
    SaveConfig();

    // Update NMEA 2000 listeners if the setting changed
    if (previous_nmea2000_state != m_protocols.nmea2000) {
      UpdateNMEA2000Listeners();
    }
    if (previous_signal_k_state != m_protocols.signalK) {
      UpdateSignalKListeners();
    }

    // Update UI if needed
    if (m_control_gui) {
      m_control_gui->UpdateControls();
    }
  }
}

void RecordPlayMgr::CheckLogRotation() {
  if (!m_recording || !m_log_rotate) return;

  wxDateTime now = wxDateTime::Now().ToUTC();
  wxTimeSpan elapsed = now - m_recording_start;

  if (elapsed.GetHours() >= m_log_rotate_interval) {
    wxLogMessage("Rotating VDR file. Elapsed %d hours. Config: %d hours",
                 elapsed.GetHours(), m_log_rotate_interval);
    // Stop current recording.
    StopRecording("Log rotation");
    // Start new recording.
    StartRecording();
  }
}

bool RecordPlayMgr::ParseNmeaComponents(wxString nmea, wxString& talker_id,
                                        wxString& sentence_id,
                                        bool& has_timestamp) {
  // Basic length check - minimum NMEA sentence should be at least 10 chars
  // $GPGGA,*hh
  if (nmea.IsEmpty() || (nmea[0] != '$' && nmea[0] != '!')) {
    return false;
  }

  // Split the sentence into fields
  wxStringTokenizer tok(nmea, ",*");
  if (!tok.HasMoreTokens()) return false;

  wxString header = tok.GetNextToken();
  // Need exactly $GPXXX or !AIVDM format
  if (header.length() != 6) return false;

  // Extract talker ID (GP, GN, etc.) and sentence ID (RMC, ZDA, etc.)
  talker_id = header.Mid(1, 2);
  sentence_id = header.Mid(3);

  // Special handling for AIS messages starting with !
  bool is_ais = (nmea[0] == '!');

  // Validate talker ID:
  // - Must be exactly 2 chars
  // - Must be ASCII
  // - Must be alphabetic
  // - Must be uppercase
  if (talker_id.length() != 2 || !talker_id.IsAscii() || !talker_id.IsWord()) {
    return false;
  }

  if (is_ais) {
    // For AIS messages, only accept specific talker IDs.
    if (talker_id != "AI" && talker_id != "AB" && talker_id != "BS") {
      return false;
    }
  } else {
    // Standard NMEA.
    if (!talker_id.IsWord() || talker_id != talker_id.Upper()) {
      return false;
    }
  }

  // Validate sentence ID:
  // - Must be exactly 3 chars
  // - Must be ASCII
  // - Must be alphabetic
  // - Must be uppercase
  if (sentence_id.length() != 3 || !sentence_id.IsAscii() ||
      !sentence_id.IsWord()) {
    return false;
  }

  // Check if sentence_id is uppercase by comparing with its uppercase version
  if (sentence_id != sentence_id.Upper()) {
    return false;
  }

  // Additional validation: must contain comma after header and checksum after
  // data
  size_t last_comma = nmea.Find(',');
  size_t checksum_pos = nmea.Find('*');

  if (last_comma == wxString::npos || checksum_pos == wxString::npos ||
      checksum_pos < last_comma) {
    return false;
  }

  // Check for known sentence types containing timestamps.
  if (sentence_id == "RMC" || sentence_id == "ZDA" || sentence_id == "GGA" ||
      sentence_id == "GBS" || sentence_id == "GLL") {
    has_timestamp = true;
    return true;
  }
  // Unknown sentence type but valid NMEA format.
  has_timestamp = false;
  return true;
}

void RecordPlayMgr::SelectPrimaryTimeSource() {
  m_has_primary_time_source = false;
  if (m_time_sources.empty()) return;

  // Scoring criteria for each source
  struct SourceScore {
    TimeSource source;
    int score;
  };

  std::vector<SourceScore> scores;

  for (const auto& source : m_time_sources) {
    if (!source.second.is_chronological) {
      // Skip sources with non-chronological timestamps
      continue;
    }
    SourceScore score = {source.first, 0};
    // Prefer sources with complete date+time
    if (source.first.sentence_id.Contains("RMC") ||
        source.first.sentence_id.Contains("ZDA")) {
      score.score += 10;
    }

    // Prefer higher precision
    score.score += source.first.precision * 2;
    scores.push_back(score);
  }

  // Sort by score
  std::sort(scores.begin(), scores.end(),
            [](const SourceScore& a, const SourceScore& b) {
              return a.score > b.score;
            });

  // Select highest scoring source as primary
  if (!scores.empty()) {
    m_primary_time_source = scores[0].source;
    m_has_primary_time_source = true;
  }
}

bool RecordPlayMgr::ScanFileTimestamps(bool& has_valid_timestamps,
                                       wxString& error) {
  if (m_protocols.replay_mode == ReplayMode::kLoopback) return true;
  if (!m_istream.IsOpened()) {
    error = _("File not open");
    has_valid_timestamps = false;
    wxLogMessage("File not open");
    return false;
  }
  wxLogMessage("Scanning timestamps in %s", m_input_file);
  // Reset all state
  m_has_timestamps = false;
  m_first_timestamp = wxDateTime();
  m_last_timestamp = wxDateTime();
  m_current_timestamp = wxDateTime();
  m_time_sources.clear();
  m_has_primary_time_source = false;
  bool found_first = false;
  wxDateTime previous_timestamp;

  // Read first line to check format
  wxString line = GetNextNonEmptyLine(true);
  if (m_istream.Eof() && line.IsEmpty()) {
    wxLogMessage("File is empty or contains only empty lines");
    has_valid_timestamps = false;
    // Empty file is not an error.
    error = "";
    return true;
  }
  m_timestamp_parser.Reset();

  // Try to parse as CSV file
  m_is_csv_file = ParseCSVHeader(line);

  if (m_is_csv_file) {
    // CSV file - expect timestamp column and strict chronological order
    line = GetNextNonEmptyLine();
    while (!m_istream.Eof()) {
      if (!line.IsEmpty()) {
        wxDateTime timestamp;
        wxString nmea;
        bool success = ParseCSVLineTimestamp(line, &nmea, &timestamp);
        if (success && timestamp.IsValid()) {
          // For CSV files, we require chronological order
          if (previous_timestamp.IsValid() && timestamp < previous_timestamp) {
            m_has_timestamps = false;
            m_first_timestamp = wxDateTime();
            m_last_timestamp = wxDateTime();
            m_current_timestamp = wxDateTime();
            m_istream.GoToLine(0);
            has_valid_timestamps = false;
            error = _("Timestamps not in chronological order");
            wxLogMessage(
                "CSV file contains non-chronological timestamps. "
                "Previous: %s, Current: %s",
                FormatIsoDateTime(previous_timestamp),
                FormatIsoDateTime(timestamp));
            return false;
          }
          previous_timestamp = timestamp;
          m_last_timestamp = timestamp;

          if (!found_first) {
            m_first_timestamp = timestamp;
            m_current_timestamp = timestamp;
            found_first = true;
          }
          m_has_timestamps = true;  // Found at least one valid timestamp.
        }
      }
      line = GetNextNonEmptyLine();
    }
  } else {
    // Raw NMEA/AIS - scan for time sources and assess quality
    int precision = 0;
    int validSentences = 0;
    int invalidSentences = 0;
    while (!m_istream.Eof()) {
      if (!line.IsEmpty()) {
        wxString talkerId, sentenceId;
        bool hasTimestamp;
        if (!ParseNmeaComponents(line, talkerId, sentenceId, hasTimestamp)) {
          invalidSentences++;
          line = GetNextNonEmptyLine();
          continue;
        }
        // Valid sentence found
        validSentences++;

        if (hasTimestamp) {
          // Create time source entry
          TimeSource source;
          source.talker_id = talkerId;
          source.sentence_id = sentenceId;

          wxDateTime timestamp;
          if (m_timestamp_parser.ParseTimestamp(line, timestamp, precision)) {
            source.precision = precision;
            if (m_time_sources.find(source) == m_time_sources.end()) {
              TimeSourceDetails details;
              details.start_time = timestamp;
              details.current_time = timestamp;
              details.end_time = timestamp;
              details.is_chronological = true;
              m_time_sources[source] = details;
            } else {
              // Update existing source
              TimeSourceDetails& details = m_time_sources[source];
              // Check if timestamps are still chronological
              if (timestamp < details.current_time) {
                details.is_chronological = false;
              }
              details.current_time = timestamp;
              details.end_time = timestamp;
            }
            m_has_timestamps = true;
          }
        }
      }
      line = GetNextNonEmptyLine();
    }

    // Log statistics about file quality
    wxLogMessage("Found %d valid and %d invalid sentences in %s",
                 validSentences, invalidSentences, m_input_file);

    // Only fail if we found no valid sentences at all
    if (validSentences == 0) {
      has_valid_timestamps = false;
      error = _("Invalid file");
      return false;
    }

    // Analyze time sources and select primary.
    SelectPrimaryTimeSource();

    if (m_has_timestamps) {
      for (const auto& source : m_time_sources) {
        wxLogMessage(
            "  %s%s: precision=%d. is_chronological=%d. Start=%s. End=%s",
            source.first.talker_id, source.first.sentence_id,
            source.first.precision, source.second.is_chronological,
            FormatIsoDateTime(source.second.start_time),
            FormatIsoDateTime(source.second.end_time));
      }
      if (m_has_primary_time_source) {
        m_first_timestamp = m_time_sources[m_primary_time_source].start_time;
        m_current_timestamp = m_first_timestamp;
        m_last_timestamp = m_time_sources[m_primary_time_source].end_time;
        m_timestamp_parser.SetPrimaryTimeSource(
            m_primary_time_source.talker_id, m_primary_time_source.sentence_id,
            m_primary_time_source.precision);

        wxLogMessage(
            "Using %s%s (precision=%d) as primary time source. Start=%s. "
            "End=%s",
            m_primary_time_source.talker_id, m_primary_time_source.sentence_id,
            m_primary_time_source.precision,
            FormatIsoDateTime(m_first_timestamp),
            FormatIsoDateTime(m_last_timestamp));
      }
    } else {
      wxLogMessage("No timestamps found in NMEA file %s", m_input_file);
    }
  }

  // Reset file position to start
  m_istream.GoToLine(-1);

  // For CSV files, timestamps must be present and valid.
  // For NMEA files, we can still do line-based playback without timestamps
  // There is a possibility that the file contains non-monotonically
  // increasing timestamps, in which case we cannot use timestamps for
  // playback. In this case, we will still allow playback based on line
  // number.
  has_valid_timestamps = m_has_timestamps;
  error = "";
  return true;
}

wxString RecordPlayMgr::GetNextNonEmptyLine(bool from_start) {
  if (!m_istream.IsOpened()) return "";

  wxString line;
  if (from_start) {
    m_istream.GoToLine(-1);
    line = m_istream.GetFirstLine();
  } else {
    line = m_istream.GetNextLine();
  }
  line.Trim(true).Trim(false);

  // Keep reading until we find a non-empty line or reach EOF
  while ((line.IsEmpty() || line.StartsWith("#")) && !m_istream.Eof()) {
    line = m_istream.GetNextLine();
    line.Trim(true).Trim(false);
  }

  return line;
}

bool RecordPlayMgr::SeekToFraction(double fraction) {
  // Validate input
  if (fraction < 0.0 || fraction > 1.0) {
    wxLogWarning("Invalid seek fraction: %f", fraction);
    return false;
  }
  if (!m_istream.IsOpened()) {
    wxLogWarning("Cannot seek, no file open");
    return false;
  }

  // For files without timestamps, use line-based position.
  if (!HasValidTimestamps()) {
    int total_lines = m_istream.GetLineCount();
    if (total_lines > 0) {
      int target_line = static_cast<int>(fraction * total_lines);
      m_istream.GoToLine(target_line);
      return true;
    }
    return false;
  }

  // Handle seeking in CSV files
  if (m_is_csv_file) {
    if (!HasValidTimestamps()) {
      return false;
    }

    // Calculate target timestamp
    wxTimeSpan total_span = m_last_timestamp - m_first_timestamp;
    wxTimeSpan target_span =
        wxTimeSpan::Seconds((total_span.GetSeconds().ToDouble() * fraction));
    wxDateTime target_time = m_first_timestamp + target_span;

    // Scan file until we find first message after target time
    wxString line = GetNextNonEmptyLine(true);  // Skip header
    line = GetNextNonEmptyLine();               // Get first data line

    while (!m_istream.Eof()) {
      wxDateTime timestamp;
      wxString nmea;
      bool success = ParseCSVLineTimestamp(line, &nmea, &timestamp);
      if (success && timestamp.IsValid() && timestamp >= target_time) {
        // Found our position, prepare to play from here
        m_current_timestamp = timestamp;
        if (m_playing) {
          AdjustPlaybackBaseTime();
        }
        return true;
      }
      line = GetNextNonEmptyLine();
    }
    return false;
  }

  // Handle seeking in NMEA files
  else {
    // If we have valid timestamps in the NMEA file, use them.
    if (!HasValidTimestamps()) {
      return false;
    }
    wxTimeSpan totalSpan = m_last_timestamp - m_first_timestamp;
    wxTimeSpan targetSpan =
        wxTimeSpan::Seconds((totalSpan.GetSeconds().ToDouble() * fraction));
    wxDateTime targetTime = m_first_timestamp + targetSpan;

    // Scan file for closest timestamp
    m_istream.GoToLine(0);
    wxString line;
    wxDateTime lastTimestamp;
    bool foundPosition = false;
    int precision;

    while (!m_istream.Eof()) {
      line = GetNextNonEmptyLine();
      wxDateTime timestamp;
      if (m_timestamp_parser.ParseTimestamp(line, timestamp, precision)) {
        if (timestamp >= targetTime) {
          m_current_timestamp = timestamp;
          foundPosition = true;
          break;
        }
        // lastTimestamp = timestamp;
      }
    }

    if (foundPosition) {
      if (m_playing) {
        AdjustPlaybackBaseTime();
      }
      return true;
    }
  }

  return false;
}

bool RecordPlayMgr::HasValidTimestamps() const {
  return m_has_timestamps && m_first_timestamp.IsValid() &&
         m_last_timestamp.IsValid() && m_current_timestamp.IsValid();
}

double RecordPlayMgr::GetProgressFraction() const {
  if (m_protocols.replay_mode == ReplayMode::kLoopback)
    return m_dm_replay_mgr->GetProgressFraction();

  // For files with timestamps
  if (HasValidTimestamps()) {
    wxTimeSpan total_span = m_last_timestamp - m_first_timestamp;
    wxTimeSpan current_span = m_current_timestamp - m_first_timestamp;

    if (total_span.GetSeconds().ToLong() == 0) {
      return 0.0;
    }

    return current_span.GetSeconds().ToDouble() /
           total_span.GetSeconds().ToDouble();
  }

  // For files without timestamps, use line position.
  if (m_istream.IsOpened()) {
    int total_lines = m_istream.GetLineCount();
    int current_line = m_istream.GetCurrentLine();
    if (total_lines > 0) {
      // Clamp current line to total lines to ensure fraction doesn't
      // exceed 1.0.
      current_line = std::max(0, std::min(current_line, total_lines));
      return static_cast<double>(current_line) / total_lines;
    }
  }

  return 0.0;
}

void RecordPlayMgr::ClearInputFile() {
  m_input_file.Clear();
  if (m_istream.IsOpened()) {
    m_istream.Close();
  }
}

wxString RecordPlayMgr::GetInputFile() const {
  if (!m_input_file.IsEmpty()) {
    if (wxFileExists(m_input_file)) {
      return m_input_file;
    }
  }
  return "";
}

std::unique_ptr<DataMonitorReplayMgr> RecordPlayMgr::DmReplayMgrFactory() {
  auto update_controls = [&] { m_control_gui->UpdateControls(); };
  auto user_message = [&](VdrMsgType t, const std::string& s) {
    OnVdrMsg(t, s);
  };
  return std::make_unique<DataMonitorReplayMgr>(m_input_file.ToStdString(),
                                                update_controls, user_message);
}

bool RecordPlayMgr::LoadFile(const wxString& filename, wxString* error) {
  if (IsPlaying()) {
    StopPlayback();
  }

  m_input_file = filename;
  if (m_protocols.replay_mode == ReplayMode::kLoopback) {
    m_dm_replay_mgr = DmReplayMgrFactory();
  }

  // Reset all file-related state
  m_is_csv_file = false;
  m_timestamp_idx = static_cast<unsigned int>(-1);
  m_message_idx = static_cast<unsigned int>(-1);
  m_header_fields.Clear();
  m_at_file_end = false;

  // Close existing file if open
  if (m_istream.IsOpened()) {
    m_istream.Close();
  }
  if (!m_istream.Open(m_input_file)) {
    if (error) {
      *error = _("Failed to open file: ") + filename;
    }
    return false;
  }
  return true;
}

void RecordPlayMgr::SetToolbarToolStatus() {
  SetToolbarItemState(m_tb_item_id_play, IsPlaying());
  SetToolbarItemState(m_tb_item_id_record, IsRecording());
}
