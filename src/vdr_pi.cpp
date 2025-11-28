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
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 **************************************************************************/

#include "wx/wxprec.h"

#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif  // precompiled headers

#include "wx/app.h"
#include "wx/tokenzr.h"
#include "wx/statline.h"
#include "wx/display.h"

#include <map>
#include <typeinfo>
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <memory>

#include "ocpn_plugin.h"

#include "vdr_pi_prefs.h"
#include "vdr_pi_control.h"
#include "vdr_pi.h"
#include "icons.h"
#include "dm_replay_mgr.h"

// the class factories, used to create and destroy instances of the PlugIn

extern "C" DECL_EXP opencpn_plugin* create_pi(void* ppimgr) {
  return new vdr_pi(ppimgr);
}

extern "C" DECL_EXP void destroy_pi(opencpn_plugin* p) { delete p; }

//---------------------------------------------------------------------------------------------------------
//
//    VDR PlugIn Implementation
//
//---------------------------------------------------------------------------------------------------------

wxDEFINE_EVENT(EVT_N2K, ObservedEvt);
wxDEFINE_EVENT(EVT_SIGNALK, ObservedEvt);

vdr_pi::vdr_pi(void* ppimgr)
    : opencpn_plugin_118(ppimgr),
      m_dm_replay_mgr(std::make_unique<DataMonitorReplayMgr>()) {
  // Create the PlugIn icons
  initialize_images();

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

  m_pvdrcontrol = nullptr;

  // Runtime variables
  m_recording = false;
  m_recording_paused = false;
  m_playing = false;
  m_is_csv_file = false;
  m_last_speed = 0.0;
  m_sentence_buffer.clear();
  m_messages_dropped = false;
}

int vdr_pi::Init(void) {
  m_eventHandler = new wxEvtHandler();
  m_timer = new TimerHandler(this);

  AddLocaleCatalog("opencpn-vdr_pi");

  // Get a pointer to the opencpn configuration object
  m_pconfig = GetOCPNConfigObject();
  m_pauimgr = GetFrameAuiManager();

  // Load the configuration items
  LoadConfig();

  // Set up NMEA 2000 listeners based on preferences
  UpdateNMEA2000Listeners();

  // If auto-start is enabled and we're not playing back and not using speed
  // threshold, start recording after initialization.
  m_recording_manually_disabled = false;
  if (m_auto_start_recording && !m_use_speed_threshold && !IsPlaying()) {
    wxLogMessage("Auto-starting recording on plugin initialization");
    StartRecording();
    SetToolbarToolStatus(m_tb_item_id_record, true);
  }

  m_tb_item_id_record =
      InsertPlugInToolSVG("VDR", _svg_vdr_record, _svg_record_toggled,
                          _svg_record_toggled, wxITEM_CHECK, _("VDR Record"),
                          "", NULL, VDR_TOOL_POSITION, 0, this);
  m_tb_item_id_play = InsertPlugInToolSVG(
      "VDR", _svg_vdr_play, _svg_play_toggled, _svg_play_toggled,
      wxITEM_CHECK, _("VDR Play"), "", NULL, VDR_TOOL_POSITION, 0, this);
  m_recording = false;

  return (WANTS_TOOLBAR_CALLBACK | INSTALLS_TOOLBAR_TOOL | WANTS_CONFIG |
          WANTS_NMEA_SENTENCES | WANTS_AIS_SENTENCES | WANTS_PREFERENCES);
}

bool vdr_pi::DeInit(void) {
  SaveConfig();
  if (m_timer) {
    if (m_timer->IsRunning()) {
      m_timer->Stop();
      m_istream.Close();
    }
    delete m_timer;
    m_timer = nullptr;
  }

  if (m_pvdrcontrol) {
    m_pauimgr->DetachPane(m_pvdrcontrol);
    m_pvdrcontrol->Close();
    m_pvdrcontrol->Destroy();
    m_pvdrcontrol = nullptr;
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
  m_networkServers.clear();

  RemovePlugInTool(m_tb_item_id_record);
  RemovePlugInTool(m_tb_item_id_play);

  if (m_eventHandler) {
    m_eventHandler->Unbind(EVT_N2K, &vdr_pi::OnN2KEvent, this);
    m_eventHandler->Unbind(EVT_SIGNALK, &vdr_pi::OnSignalKEvent, this);
    delete m_eventHandler;
    m_eventHandler = nullptr;
  }
  m_n2k_listeners.clear();
  m_signalk_listeners.clear();
  return true;
}

int vdr_pi::GetAPIVersionMajor() { return atoi(API_VERSION); }

int vdr_pi::GetAPIVersionMinor() {
  std::string v(API_VERSION);
  size_t dotpos = v.find('.');
  return atoi(v.substr(dotpos + 1).c_str());
}

int vdr_pi::GetPlugInVersionMajor() { return PLUGIN_VERSION_MAJOR; }

int vdr_pi::GetPlugInVersionMinor() { return PLUGIN_VERSION_MINOR; }

int vdr_pi::GetPlugInVersionPatch() { return PLUGIN_VERSION_PATCH; }

int vdr_pi::GetPlugInVersionPost() { return PLUGIN_VERSION_TWEAK; }

const char* vdr_pi::GetPlugInVersionPre() { return PKG_PRERELEASE; }

const char* vdr_pi::GetPlugInVersionBuild() { return PKG_BUILD_INFO; }

wxBitmap* vdr_pi::GetPlugInBitmap() { return &m_panelBitmap; }

wxString vdr_pi::GetCommonName() { return _("VDR"); }

wxString vdr_pi::GetShortDescription() {
  return _("Voyage Data Recorder plugin for OpenCPN");
}

wxString vdr_pi::GetLongDescription() {
  return _(
      "Voyage Data Recorder plugin for OpenCPN\n\
Provides NMEA stream save and replay.");
}

void vdr_pi::UpdateSignalKListeners() {
  m_eventHandler->Unbind(EVT_SIGNALK, &vdr_pi::OnSignalKEvent, this);
  m_signalk_listeners.clear();
  wxLogMessage("Configuring SignalK listeners. SignalK enabled: %d",
               m_protocols.signalK);
  if (m_protocols.signalK) {
    // TODO: Implement SignalK configuration.
  }
}

void vdr_pi::UpdateNMEA2000Listeners() {
  m_eventHandler->Unbind(EVT_N2K, &vdr_pi::OnN2KEvent, this);
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
          GetListener(NMEA2000Id(it.first), EVT_N2K, m_eventHandler));
    }

    m_eventHandler->Bind(EVT_N2K, &vdr_pi::OnN2KEvent, this);
  }
}

// Format timestamp: YYYY-MM-DDTHH:MM:SS.mmmZ
// The format combines ISO format with milliseconds in UTC.
// ts is assumed to be in UTC already.
wxString FormatIsoDateTime(const wxDateTime& ts) {
  return ts.Format("%Y-%m-%dT%H:%M:%S.%lZ");
}

void vdr_pi::OnSignalKEvent(wxCommandEvent& event) {
  if (!m_protocols.signalK) {
    // SignalK recording is disabled.
    return;
  }
  // TODO: Implement SignalK recording.
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
inline uint16_t N2KToInt16(const uint8_t* data) {
  return data[0] | (data[1] << 8);  // little-endian uint16
}

void vdr_pi::OnN2KEvent(wxCommandEvent& event) {
  if (!m_protocols.nmea2000) {
    // NMEA 2000 recording is disabled.
    return;
  }

  ObservedEvt& ev = dynamic_cast<ObservedEvt&>(event);
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
    case VDRDataFormat::CSV: {
      // CSV format: timestamp,type,id,payload
      // where "id" is the PGN number.
      wxString timestamp = FormatIsoDateTime(wxDateTime::UNow());
      formatted_message =
          wxString::Format("%s,NMEA2000,%d,%s\n", timestamp, pgn, log_payload);
      break;
    }
    case VDRDataFormat::RawNMEA:
      // PCDIN format: $PCDIN,<pgn>,<payload>
      formatted_message =
          wxString::Format("$PCDIN,%d,%s\r\n", pgn, log_payload);
      break;
  }

  // Check if we need to rotate the VDR file.
  CheckLogRotation();

  m_ostream.Write(formatted_message.ToStdString());
}

wxString vdr_pi::FormatNMEA0183AsCSV(const wxString& nmea) {
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

void vdr_pi::SetNMEASentence(wxString& sentence) {
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

  wxString normalizedSentence = sentence;
  normalizedSentence.Trim(true);

  switch (m_data_format) {
    case VDRDataFormat::CSV:
      m_ostream.Write(FormatNMEA0183AsCSV(normalizedSentence));
      break;
    case VDRDataFormat::RawNMEA:
    default:
      if (!normalizedSentence.EndsWith("\r\n")) {
        normalizedSentence += "\r\n";
      }
      m_ostream.Write(normalizedSentence);
      break;
  }
}

void vdr_pi::SetAISSentence(wxString& sentence) {
  SetNMEASentence(sentence);  // Handle the same way as NMEA
}

const ConnectionSettings& vdr_pi::GetNetworkSettings(
    const wxString& protocol) const {
  if (protocol == "N2K")
    return m_protocols.n2kNet;
  else if (protocol == "NMEA0183")
    return m_protocols.nmea0183Net;
  else if (protocol == "SignalK")
    return m_protocols.signalKNet;

  // Default to NMEA0183 if unknown protocol
  return m_protocols.nmea0183Net;
}

void vdr_pi::CheckAutoRecording(double speed) {
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
      SetToolbarToolStatus(m_tb_item_id_record, true);
    } else if (m_recording_paused) {
      wxLogMessage("Resume recording, speed %.2f exceeds threshold %.2f", speed,
                   m_speed_threshold);
      ResumeRecording();
    }
  } else if (m_recording) {
    // Add hysteresis to prevent rapid starting/stopping
    static const double HYSTERESIS = 0.2;  // 0.2 knots below threshold
    if (speed < (m_speed_threshold - HYSTERESIS)) {
      // If we're recording and it was auto-started, handle stop delay
      if (!m_below_threshold_since.IsValid()) {
        m_below_threshold_since = wxDateTime::Now().ToUTC();
        wxLogMessage(
            "Speed dropped below threshold, starting pause delay timer");
      } else {
        // Check if enough time has passed
        wxTimeSpan timeBelow =
            wxDateTime::Now().ToUTC() - m_below_threshold_since;
        if (timeBelow.GetMinutes() >= m_stop_delay) {
          wxLogMessage(
              "Pause recording, speed %.2f below threshold %.2f for %d minutes",
              speed, m_speed_threshold, m_stop_delay);
          PauseRecording("Speed dropped below threshold");
          m_below_threshold_since = wxDateTime();  // Reset timer
        }
      }
    }
  }
}

bool vdr_pi::IsNMEA0183OrAIS(const wxString& line) {
  // NMEA sentences start with $ or !
  return line.StartsWith("$") || line.StartsWith("!");
}

bool vdr_pi::ParseCSVHeader(const wxString& header) {
  // Reset indices
  const unsigned int INVALID_INDEX = std::numeric_limits<unsigned int>::max();
  m_timestamp_idx = INVALID_INDEX;
  m_message_idx = INVALID_INDEX;
  m_header_fields.Clear();

  // If it looks like NMEA/AIS, it's not a header
  if (IsNMEA0183OrAIS(header)) {
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
  return (m_timestamp_idx != INVALID_INDEX && m_message_idx != INVALID_INDEX);
}

bool vdr_pi::ParseCSVLineTimestamp(const wxString& line, wxString* message,
                                   wxDateTime* timestamp) {
  assert(m_is_csv_file);
  return m_timestampParser.ParseCSVLineTimestamp(
      line, m_timestamp_idx, m_message_idx, message, timestamp);
}

void vdr_pi::FlushSentenceBuffer() {
  for (const auto& sentence : m_sentence_buffer) {
    PushNMEABuffer(sentence + "\r\n");
  }
  m_sentence_buffer.clear();
}

double vdr_pi::GetSpeedMultiplier() const {
  return m_pvdrcontrol ? m_pvdrcontrol->GetSpeedMultiplier() : 1.0;
}

void vdr_pi::Notify() {
  if (m_protocols.nmea0183ReplayMode == NMEA0183ReplayMode::LOOPBACK) {
    if (m_pvdrcontrol) m_pvdrcontrol->SetProgress(GetProgressFraction());
    int delay = m_dm_replay_mgr->Notify();
    if (delay >= 0) m_timer->Start(delay, wxTIMER_ONE_SHOT);
    return;
  }
  if (!m_istream.IsOpened()) return;

  wxDateTime now = wxDateTime::UNow();
  wxDateTime targetTime;
  bool behindSchedule = true;
  int precision;

  // For non-timestamped files, base rate of 10 messages/second
  const int BASE_MESSAGES_PER_BATCH = 10;
  const int BASE_INTERVAL_MS = 1000;  // 1 second

  // Keep processing messages until we catch up with scheduled time.
  while (behindSchedule && !m_istream.Eof()) {
    wxString line;
    int pos = m_istream.GetCurrentLine();

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
      m_atFileEnd = true;
      PausePlayback();
      if (m_pvdrcontrol) {
        m_pvdrcontrol->UpdateControls();
      }
      return;
    }

    // Parse the line according to detected format (CSV or raw NMEA/AIS).
    wxDateTime timestamp;
    wxString nmea;
    bool msgHasTimestamp = false;

    if (m_is_csv_file) {
      bool success = ParseCSVLineTimestamp(line, &nmea, &timestamp);
      if (success) {
        nmea += "\r\n";
        msgHasTimestamp = true;
      }
    } else {
      nmea = line + "\r\n";
      msgHasTimestamp =
          m_timestampParser.ParseTimestamp(line, timestamp, precision);
    }

    if (!nmea.IsEmpty()) {
      if (m_protocols.nmea0183ReplayMode == NMEA0183ReplayMode::INTERNAL_API) {
        // Add sentence to buffer, maintaining max size.
        m_sentence_buffer.push_back(nmea);
      }

      // Send through network if enabled.
      HandleNetworkPlayback(nmea);

      if (msgHasTimestamp) {
        // The current sentence has a timestamp from the primary time source.
        m_currentTimestamp = timestamp;
        targetTime = GetNextPlaybackTime();
        // Check if we've caught up to schedule.
        if (targetTime.IsValid() && targetTime > now) {
          behindSchedule = false;  // This will break the loop.
          // Before scheduling next update, flush our sentence buffer.
          FlushSentenceBuffer();
          // Schedule next notification.
          wxTimeSpan waitTime = targetTime - now;
          m_timer->Start(
              static_cast<int>(waitTime.GetMilliseconds().ToDouble()),
              wxTIMER_ONE_SHOT);
        }
      } else if (!HasValidTimestamps() &&
                 m_sentence_buffer.size() >= BASE_MESSAGES_PER_BATCH) {
        // For files that do not have timestamped records (or timestamps are not
        // in chronological order), use batch processing.
        behindSchedule = false;  // This will break the loop.
        FlushSentenceBuffer();

        // Calculate interval based on speed multiplier
        int interval =
            static_cast<int>(BASE_INTERVAL_MS / GetSpeedMultiplier());

        // Schedule next batch.
        m_timer->Start(interval, wxTIMER_ONE_SHOT);
      }

      if (m_sentence_buffer.size() > MAX_MSG_BUFFER_SIZE) {
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
  if (m_pvdrcontrol) {
    m_pvdrcontrol->SetProgress(GetProgressFraction());
  }
}

void vdr_pi::OnVdrMsg(VdrMsgType type, const std::string msg) {
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

wxDateTime vdr_pi::GetNextPlaybackTime() const {
  if (!m_currentTimestamp.IsValid() || !m_firstTimestamp.IsValid() ||
      !m_playback_base_time.IsValid()) {
    return wxDateTime();  // Return invalid time if we don't have valid
                          // timestamps
  }
  // Calculate when this message should be played relative to playback start.
  wxTimeSpan elapsedTime = m_currentTimestamp - m_firstTimestamp;
  wxLongLong ms = elapsedTime.GetMilliseconds();
  double scaledMs = ms.ToDouble() / GetSpeedMultiplier();
  wxTimeSpan scaledElapsed =
      wxTimeSpan::Milliseconds(static_cast<long>(scaledMs));
  return m_playback_base_time + scaledElapsed;
}

int vdr_pi::GetToolbarToolCount(void) { return 2; }

void vdr_pi::OnToolbarToolCallback(int id) {
  if (id == m_tb_item_id_play) {
    // Don't allow playback while recording
    if (m_recording) {
      wxMessageBox(_("Stop recording before starting playback."),
                   _("VDR Plugin"), wxOK | wxICON_INFORMATION);
      SetToolbarItemState(id, false);
      return;
    }
    // Check if the toolbar button is being toggled off
    if (m_pvdrcontrol) {
      // Stop any active playback
      if (m_timer->IsRunning()) {
        m_timer->Stop();
        m_istream.Close();
      }

      // Close and destroy the control window
      m_pauimgr->DetachPane(m_pvdrcontrol);
      m_pvdrcontrol->Close();
      m_pvdrcontrol->Destroy();
      m_pvdrcontrol = nullptr;

      // Update toolbar state
      SetToolbarItemState(id, false);
      return;
    }

    if (!m_pvdrcontrol) {
      wxPoint dialog_position = wxPoint(100, 100);
      //  Dialog will be fixed position on Android, so position carefully
#ifdef __WXQT__  // a surrogate for Android
      wxRect tbRect = GetMasterToolbarRect();
      dialog_position.y = 0;
      dialog_position.x = tbRect.x + tbRect.width + 2;
#endif

      m_pvdrcontrol = new VDRControl(GetOCPNCanvasWindow(), wxID_ANY, this);
      wxAuiPaneInfo pane = wxAuiPaneInfo()
                               .Name("VDR")
                               .Caption(_("Voyage Data Recorder"))
                               .CaptionVisible(true)
                               .Float()
                               .FloatingPosition(dialog_position)
                               .Dockable(false)
                               .Fixed()
                               .CloseButton(true)
                               .Show(true);
      m_pauimgr->AddPane(m_pvdrcontrol, pane);
      m_pauimgr->Update();
      if (m_protocols.nmea0183ReplayMode == NMEA0183ReplayMode::LOOPBACK) {
        m_pvdrcontrol->EnableSpeedSlider(false);
      }
    } else {
      m_pauimgr->GetPane(m_pvdrcontrol)
          .Show(!m_pauimgr->GetPane(m_pvdrcontrol).IsShown());
      m_pauimgr->Update();
    }
    SetToolbarItemState(id, true);
  } else if (id == m_tb_item_id_record) {
    // Don't allow recording while playing
    if (m_timer->IsRunning()) {
      wxMessageBox(_("Stop playback before starting recording."),
                   _("VDR Plugin"), wxOK | wxICON_INFORMATION);
      SetToolbarItemState(id, false);
      return;
    }
    if (m_recording) {
      StopRecording("Recording stopped manually");
      SetToolbarItemState(id, false);
      // Recording was stopped manually, so disable auto-recording
      m_recording_manually_disabled = true;
    } else {
      StartRecording();
      if (m_recording) {
        // Only set button state if recording started
        // successfully
        SetToolbarItemState(id, true);
        m_recording_manually_disabled = false;
      }
    }
  }
}

bool vdr_pi::IsPlaying() const {
  if (m_protocols.nmea0183ReplayMode == NMEA0183ReplayMode::LOOPBACK)
    return m_dm_replay_mgr->IsPlaying();
  return m_playing;
}

bool vdr_pi::IsError() const {
  if (m_protocols.nmea0183ReplayMode == NMEA0183ReplayMode::LOOPBACK)
    return m_dm_replay_mgr->IsError();
  return false;
}

bool vdr_pi::IsAtFileEnd() const {
  if (m_protocols.nmea0183ReplayMode == NMEA0183ReplayMode::LOOPBACK)
    return m_dm_replay_mgr->IsAtEnd();
  return m_atFileEnd;
}

wxDateTime vdr_pi::GetCurrentTimestamp() const {
  if (m_protocols.nmea0183ReplayMode != NMEA0183ReplayMode::LOOPBACK)
    return m_currentTimestamp;

  uint64_t stamp = m_dm_replay_mgr->GetCurrentTimestamp();
  wxDateTime date_time(time_t(stamp / 1000));
  date_time.SetMillisecond(stamp % 1000);
  return date_time;
}

void vdr_pi::SetColorScheme(PI_ColorScheme cs) {
  if (m_pvdrcontrol) {
    m_pvdrcontrol->SetColorScheme(cs);
  }
}

wxString vdr_pi::GenerateFilename() const {
  wxDateTime now = wxDateTime::Now().ToUTC();
  wxString timestamp = now.Format("%Y%m%dT%H%M%SZ");
  wxString extension = (m_data_format == VDRDataFormat::CSV) ? ".csv" : ".txt";
  return "vdr_" + timestamp + extension;
}

bool vdr_pi::LoadConfig(void) {
  wxFileConfig* pConf = (wxFileConfig*)m_pconfig;

  if (!pConf) return false;

  pConf->SetPath("/PlugIns/VDR");
  pConf->Read("InputFilename", &m_ifilename, wxEmptyString);
  pConf->Read("OutputFilename", &m_ofilename, wxEmptyString);

  // Default directory handling based on platform
#ifdef __ANDROID__
  wxString defaultDir =
      "/storage/emulated/0/Android/data/org.opencpn.opencpn/files";
#else
  wxString defaultDir = *GetpPrivateApplicationDataLocation();
#endif

  // Recording preferences.
  pConf->Read("RecordingDirectory", &m_recording_dir, defaultDir);
  pConf->Read("Interval", &m_interval, 1000);
  pConf->Read("LogRotate", &m_log_rotate, false);
  pConf->Read("LogRotateInterval", &m_log_rotate_interval, 24);
  pConf->Read("AutoStartRecording", &m_auto_start_recording, false);
  pConf->Read("UseSpeedThreshold", &m_use_speed_threshold, false);
  pConf->Read("SpeedThreshold", &m_speed_threshold, 0.5);
  pConf->Read("StopDelay", &m_stop_delay, 10);  // Default 10 minutes

  pConf->Read("EnableNMEA0183", &m_protocols.nmea0183, true);
  pConf->Read("EnableNMEA2000", &m_protocols.nmea2000, false);
  pConf->Read("EnableSignalK", &m_protocols.signalK, false);

  int format;
  pConf->Read("DataFormat", &format,
              static_cast<int>(VDRDataFormat::RawNMEA));
  m_data_format = static_cast<VDRDataFormat>(format);

  // Replay preferences.
  int replayMode;
  pConf->Read("NMEA0183ReplayMode", &replayMode,
              static_cast<int>(NMEA0183ReplayMode::INTERNAL_API));
  m_protocols.nmea0183ReplayMode = static_cast<NMEA0183ReplayMode>(replayMode);

  // NMEA 0183 network settings
  pConf->Read("NMEA0183_UseTCP", &m_protocols.nmea0183Net.useTCP, false);
  pConf->Read("NMEA0183_Port", &m_protocols.nmea0183Net.port, 10111);
  pConf->Read("NMEA0183_Enabled", &m_protocols.nmea0183Net.enabled, false);

  // NMEA 2000 network settings
  pConf->Read("NMEA2000_UseTCP", &m_protocols.n2kNet.useTCP, false);
  pConf->Read("NMEA2000_Port", &m_protocols.n2kNet.port, 10112);
  pConf->Read("NMEA2000_Enabled", &m_protocols.n2kNet.enabled, false);

#if 0
  // Signal K network settings
  pConf->Read("SignalK_UseTCP", &m_protocols.signalKNet.useTCP, true);
  pConf->Read("SignalK_Port", &m_protocols.signalKNet.port, 8375);
  pConf->Read("SignalK_Enabled", &m_protocols.signalKNet.enabled, false);
#endif

  return true;
}

bool vdr_pi::SaveConfig(void) {
  wxFileConfig* pConf = (wxFileConfig*)m_pconfig;

  if (!pConf) return false;

  pConf->SetPath("/PlugIns/VDR");

  // Recording preferences.
  pConf->Write("InputFilename", m_ifilename);
  pConf->Write("OutputFilename", m_ofilename);
  pConf->Write("RecordingDirectory", m_recording_dir);
  pConf->Write("Interval", m_interval);
  pConf->Write("LogRotate", m_log_rotate);
  pConf->Write("LogRotateInterval", m_log_rotate_interval);
  pConf->Write("AutoStartRecording", m_auto_start_recording);
  pConf->Write("UseSpeedThreshold", m_use_speed_threshold);
  pConf->Write("SpeedThreshold", m_speed_threshold);
  pConf->Write("StopDelay", m_stop_delay);
  pConf->Write("DataFormat", static_cast<int>(m_data_format));

  pConf->Write("EnableNMEA0183", m_protocols.nmea0183);
  pConf->Write("EnableNMEA2000", m_protocols.nmea2000);
  pConf->Write("EnableSignalK", m_protocols.signalK);

  // Replay preferences.
  pConf->Write("NMEA0183ReplayMode",
               static_cast<int>(m_protocols.nmea0183ReplayMode));

  // NMEA 0183 network settings
  pConf->Write("NMEA0183_UseTCP", m_protocols.nmea0183Net.useTCP);
  pConf->Write("NMEA0183_Port", m_protocols.nmea0183Net.port);
  pConf->Write("NMEA0183_Enabled", m_protocols.nmea0183Net.enabled);

  // NMEA 2000 network settings
  pConf->Write("NMEA2000_UseTCP", m_protocols.n2kNet.useTCP);
  pConf->Write("NMEA2000_Port", m_protocols.n2kNet.port);
  pConf->Write("NMEA2000_Enabled", m_protocols.n2kNet.enabled);

#if 0
  // Signal K network settings
  pConf->Write("SignalK_UseTCP", m_protocols.signalKNet.useTCP);
  pConf->Write("SignalK_Port", m_protocols.signalKNet.port);
  pConf->Write("SignalK_Enabled", m_protocols.signalKNet.enabled);
#endif

  return true;
}

void vdr_pi::StartRecording() {
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
                    (m_data_format == VDRDataFormat::CSV ? ".csv" : ".txt");
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
  if (m_data_format == VDRDataFormat::CSV) {
    m_ostream.Write("timestamp,type,id,message\n");
  }

  m_recording = true;
  m_recording_paused = false;
  m_recording_start = wxDateTime::Now().ToUTC();
  m_current_recording_start = m_recording_start;
}

void vdr_pi::PauseRecording(const wxString& reason) {
  if (!m_recording || m_recording_paused) return;

  wxLogMessage("Pause recording. Reason: %s", reason);
  m_recording_paused = true;
  m_recording_pause_time = wxDateTime::Now().ToUTC();
}

void vdr_pi::ResumeRecording() {
  if (!m_recording_paused) return;
  m_recording_paused = false;
}

void vdr_pi::StopRecording(const wxString& reason) {
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

void vdr_pi::AdjustPlaybackBaseTime() {
  if (!m_firstTimestamp.IsValid() || !m_currentTimestamp.IsValid()) {
    return;
  }

  // Calculate how much time has "elapsed" in the recording up to our current
  // position.
  wxTimeSpan elapsed = m_currentTimestamp - m_firstTimestamp;

  // Set base time so that current playback position corresponds to current wall
  // clock.
  m_playback_base_time =
      wxDateTime::UNow() -
      wxTimeSpan::Milliseconds(static_cast<long>(
          (elapsed.GetMilliseconds().ToDouble() / GetSpeedMultiplier())));
}

void vdr_pi::StartPlayback() {
  if (m_ifilename.IsEmpty()) {
    if (m_pvdrcontrol) {
      m_pvdrcontrol->UpdateFileStatus(_("No file selected."));
    }
    return;
  }
  if (!wxFileExists(m_ifilename)) {
    if (m_pvdrcontrol) {
      m_pvdrcontrol->UpdateFileStatus(_("File does not exist."));
    }
    return;
  }
  if (m_protocols.nmea0183ReplayMode == NMEA0183ReplayMode::LOOPBACK) {
    if (!m_dm_replay_mgr->IsPaused())
      m_dm_replay_mgr = std::move(DmReplayMgrFactory());
    m_dm_replay_mgr->Start();
    if (m_dm_replay_mgr->IsPlaying())
      m_pvdrcontrol->UpdateFileStatus(_("File successfully loaded"));
    Notify();
    return;
  }

  // Reset end-of-file state when starting playback
  m_atFileEnd = false;

  // Always adjust base time when starting playback, whether from pause or seek
  AdjustPlaybackBaseTime();

  if (!m_istream.IsOpened()) {
    if (!m_istream.Open(m_ifilename)) {
      if (m_pvdrcontrol) {
        m_pvdrcontrol->UpdateFileStatus(_("Failed to open file."));
      }
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

  if (m_pvdrcontrol) {
    m_pvdrcontrol->SetProgress(GetProgressFraction());
    m_pvdrcontrol->UpdateControls();
    m_pvdrcontrol->UpdateFileLabel(m_ifilename);
  }
  wxLogMessage(
      "Start playback from file: %s. Progress: %.2f. Has timestamps: %d",
      m_ifilename, GetProgressFraction(), m_has_timestamps);
  // Process first line immediately.
  m_istream.GoToLine(-1);

  Notify();
}

void vdr_pi::PausePlayback() {
  if (m_protocols.nmea0183ReplayMode == NMEA0183ReplayMode::LOOPBACK) {
    m_dm_replay_mgr->Pause();
    if (m_pvdrcontrol) m_pvdrcontrol->UpdateControls();
    return;
  }

  if (!m_playing) return;

  m_timer->Stop();
  m_playing = false;
  if (m_pvdrcontrol) m_pvdrcontrol->UpdateControls();
}

void vdr_pi::StopPlayback() {
  if (!m_playing) return;

  m_timer->Stop();
  m_playing = false;
  m_istream.Close();

  // Stop all network servers
  StopNetworkServers();

  if (m_pvdrcontrol) {
    m_pvdrcontrol->SetProgress(0);
    m_pvdrcontrol->UpdateControls();
    m_pvdrcontrol->UpdateFileLabel(wxEmptyString);
  }
}

VDRNetworkServer* vdr_pi::GetServer(const wxString& protocol) {
  auto it = m_networkServers.find(protocol);
  if (it == m_networkServers.end()) {
    // Create new server instance if it doesn't exist.
    auto server = std::make_unique<VDRNetworkServer>();
    VDRNetworkServer* serverPtr = server.get();
    m_networkServers[protocol] = std::move(server);
    return serverPtr;
  }
  return it->second.get();
}

bool vdr_pi::InitializeNetworkServers() {
  bool success = true;
  wxString errors;

  // Initialize NMEA0183 network server if needed
  if (m_protocols.nmea0183Net.enabled) {
    VDRNetworkServer* server = GetServer("NMEA0183");
    if (!server->IsRunning() ||
        server->IsTCP() != m_protocols.nmea0183Net.useTCP ||
        server->GetPort() != m_protocols.nmea0183Net.port) {
      server->Stop();  // Stop existing server if running
      wxString error;
      if (!server->Start(m_protocols.nmea0183Net.useTCP,
                         m_protocols.nmea0183Net.port, error)) {
        success = false;
        errors += error;
      } else {
        wxLogMessage("Started NMEA0183 server: %s on port %d",
                     m_protocols.nmea0183Net.useTCP ? "TCP" : "UDP",
                     m_protocols.nmea0183Net.port);
      }
    }
  } else {
    VDRNetworkServer* server = GetServer("NMEA0183");
    if (server->IsRunning()) {
      server->Stop();
      wxLogMessage("Stopped NMEA0183 network server (disabled in preferences)");
    }
  }

  // Initialize NMEA2000 network server if needed
  if (m_protocols.n2kNet.enabled) {
    VDRNetworkServer* server = GetServer("N2K");
    if (!server->IsRunning() || server->IsTCP() != m_protocols.n2kNet.useTCP ||
        server->GetPort() != m_protocols.n2kNet.port) {
      server->Stop();  // Stop existing server if running
      wxString error;
      if (!server->Start(m_protocols.n2kNet.useTCP, m_protocols.n2kNet.port,
                         error)) {
        success = false;
        errors += error;
      } else {
        wxLogMessage("Started NMEA2000 server: %s on port %d",
                     m_protocols.n2kNet.useTCP ? "TCP" : "UDP",
                     m_protocols.n2kNet.port);
      }
    }
  } else {
    VDRNetworkServer* server = GetServer("N2K");
    if (server->IsRunning()) {
      server->Stop();
      wxLogMessage("Stopped NMEA2000 network server (disabled in preferences)");
    }
  }

  if (m_pvdrcontrol) {
    if (!success) {
      m_pvdrcontrol->UpdateNetworkStatus(errors);
    } else {
      m_pvdrcontrol->UpdateNetworkStatus(wxEmptyString);
    }
  }

  return success;
}

void vdr_pi::StopNetworkServers() {
  // Stop NMEA0183 server if running
  if (VDRNetworkServer* server = GetServer("NMEA0183")) {
    if (server->IsRunning()) {
      server->Stop();
      wxLogMessage("Stopped NMEA0183 network server");
    }
  }

  // Stop NMEA2000 server if running
  if (VDRNetworkServer* server = GetServer("N2K")) {
    if (server->IsRunning()) {
      server->Stop();
      wxLogMessage("Stopped NMEA2000 network server");
    }
  }
}

void vdr_pi::HandleNetworkPlayback(const wxString& data) {
  // For NMEA 0183 data
  if (m_protocols.nmea0183Net.enabled &&
      (data.StartsWith("$") || data.StartsWith("!"))) {
    VDRNetworkServer* server = GetServer("NMEA0183");
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
    VDRNetworkServer* server = GetServer("N2K");
    if (server && server->IsRunning()) {
      server->SendText(data);  // Use SendText() for text-based formats
    }
  }
}

void vdr_pi::SetDataFormat(VDRDataFormat format) {
  // If format hasn't changed, do nothing.
  if (format == m_data_format) {
    return;
  }

  if (m_recording) {
    // If recording is active, we need to handle the transition,
    // e.g., from CSV to raw NMEA. A new file will be created.
    wxDateTime recordingStart = m_recording_start;
    wxString currentDir = m_recording_dir;
    StopRecording("Changing output data format");
    m_data_format = format;
    // Start new recording
    m_recording_start = recordingStart;  // Preserve original start time
    m_recording_dir = currentDir;
    StartRecording();
  } else {
    // Simply update the format if not recording.
    m_data_format = format;
  }
}

void vdr_pi::ShowPreferencesDialog(wxWindow* parent) {
  VDRPrefsDialog dlg(parent, wxID_ANY, m_data_format, m_recording_dir,
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
    bool previousNMEA2000State = m_protocols.nmea2000;
    bool previousSignalKState = m_protocols.signalK;
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
    if (previousNMEA2000State != m_protocols.nmea2000) {
      UpdateNMEA2000Listeners();
    }
    if (previousSignalKState != m_protocols.signalK) {
      UpdateSignalKListeners();
    }

    // Update UI if needed
    if (m_pvdrcontrol) {
      m_pvdrcontrol->UpdateControls();
    }
  }
}

void vdr_pi::ShowPreferencesDialogNative(wxWindow* parent) {
  VDRPrefsDialog dlg(parent, wxID_ANY, m_data_format, m_recording_dir,
                     m_log_rotate, m_log_rotate_interval,
                     m_auto_start_recording, m_use_speed_threshold,
                     m_speed_threshold, m_stop_delay, m_protocols);

  if (dlg.ShowModal() == wxID_OK) {
    bool previousNMEA2000State = m_protocols.nmea2000;
    bool previousSignalKState = m_protocols.signalK;
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
    if (previousNMEA2000State != m_protocols.nmea2000) {
      UpdateNMEA2000Listeners();
    }
    if (previousSignalKState != m_protocols.signalK) {
      UpdateSignalKListeners();
    }

    // Update UI if needed
    if (m_pvdrcontrol) {
      m_pvdrcontrol->UpdateControls();
    }
  }
}

void vdr_pi::CheckLogRotation() {
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

bool vdr_pi::ParseNMEAComponents(wxString nmea, wxString& talkerId,
                                 wxString& sentenceId,
                                 bool& hasTimestamp) const {
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
  talkerId = header.Mid(1, 2);
  sentenceId = header.Mid(3);

  // Special handling for AIS messages starting with !
  bool isAIS = (nmea[0] == '!');

  // Validate talker ID:
  // - Must be exactly 2 chars
  // - Must be ASCII
  // - Must be alphabetic
  // - Must be uppercase
  if (talkerId.length() != 2 || !talkerId.IsAscii() || !talkerId.IsWord()) {
    return false;
  }

  if (isAIS) {
    // For AIS messages, only accept specific talker IDs.
    if (talkerId != "AI" && talkerId != "AB" && talkerId != "BS") {
      return false;
    }
  } else {
    // Standard NMEA.
    if (!talkerId.IsWord() || talkerId != talkerId.Upper()) {
      return false;
    }
  }

  // Validate sentence ID:
  // - Must be exactly 3 chars
  // - Must be ASCII
  // - Must be alphabetic
  // - Must be uppercase
  if (sentenceId.length() != 3 || !sentenceId.IsAscii() ||
      !sentenceId.IsWord()) {
    return false;
  }

  // Check if sentenceId is uppercase by comparing with its uppercase version
  if (sentenceId != sentenceId.Upper()) {
    return false;
  }

  // Additional validation: must contain comma after header and checksum after
  // data
  size_t lastComma = nmea.Find(',');
  size_t checksumPos = nmea.Find('*');

  if (lastComma == wxString::npos || checksumPos == wxString::npos ||
      checksumPos < lastComma) {
    return false;
  }

  // Check for known sentence types containing timestamps.
  if (sentenceId == "RMC" || sentenceId == "ZDA" || sentenceId == "GGA" ||
      sentenceId == "GBS" || sentenceId == "GLL") {
    hasTimestamp = true;
    return true;
  }
  // Unknown sentence type but valid NMEA format.
  hasTimestamp = false;
  return true;
}

void vdr_pi::SelectPrimaryTimeSource() {
  m_hasPrimaryTimeSource = false;
  if (m_timeSources.empty()) return;

  // Scoring criteria for each source
  struct SourceScore {
    TimeSource source;
    int score;
  };

  std::vector<SourceScore> scores;

  for (const auto& source : m_timeSources) {
    if (!source.second.isChronological) {
      // Skip sources with non-chronological timestamps
      continue;
    }
    SourceScore score = {source.first, 0};
    // Prefer sources with complete date+time
    if (source.first.sentenceId.Contains("RMC") ||
        source.first.sentenceId.Contains("ZDA")) {
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
    m_primaryTimeSource = scores[0].source;
    m_hasPrimaryTimeSource = true;
  }
}

bool vdr_pi::ScanFileTimestamps(bool& hasValidTimestamps, wxString& error) {
  if (m_protocols.nmea0183ReplayMode == NMEA0183ReplayMode::LOOPBACK)
    return true;
  if (!m_istream.IsOpened()) {
    error = _("File not open");
    hasValidTimestamps = false;
    wxLogMessage("File not open");
    return false;
  }
  wxLogMessage("Scanning timestamps in %s", m_ifilename);
  // Reset all state
  m_has_timestamps = false;
  m_firstTimestamp = wxDateTime();
  m_lastTimestamp = wxDateTime();
  m_currentTimestamp = wxDateTime();
  m_timeSources.clear();
  m_hasPrimaryTimeSource = false;
  bool foundFirst = false;
  wxDateTime previousTimestamp;

  // Read first line to check format
  wxString line = GetNextNonEmptyLine(true);
  if (m_istream.Eof() && line.IsEmpty()) {
    wxLogMessage("File is empty or contains only empty lines");
    hasValidTimestamps = false;
    // Empty file is not an error.
    error = wxEmptyString;
    return true;
  }
  m_timestampParser.Reset();

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
          if (previousTimestamp.IsValid() && timestamp < previousTimestamp) {
            m_has_timestamps = false;
            m_firstTimestamp = wxDateTime();
            m_lastTimestamp = wxDateTime();
            m_currentTimestamp = wxDateTime();
            m_istream.GoToLine(0);
            hasValidTimestamps = false;
            error = _("Timestamps not in chronological order");
            wxLogMessage(
                "CSV file contains non-chronological timestamps. "
                "Previous: %s, Current: %s",
                FormatIsoDateTime(previousTimestamp),
                FormatIsoDateTime(timestamp));
            return false;
          }
          previousTimestamp = timestamp;
          m_lastTimestamp = timestamp;

          if (!foundFirst) {
            m_firstTimestamp = timestamp;
            m_currentTimestamp = timestamp;
            foundFirst = true;
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
    wxString lastInvalidLine;  // Store for error reporting
    while (!m_istream.Eof()) {
      if (!line.IsEmpty()) {
        wxString talkerId, sentenceId;
        bool hasTimestamp;
        if (!ParseNMEAComponents(line, talkerId, sentenceId, hasTimestamp)) {
          invalidSentences++;
          lastInvalidLine = line;
          line = GetNextNonEmptyLine();
          continue;
        }
        // Valid sentence found
        validSentences++;

        if (hasTimestamp) {
          // Create time source entry
          TimeSource source;
          source.talkerId = talkerId;
          source.sentenceId = sentenceId;

          wxDateTime timestamp;
          if (m_timestampParser.ParseTimestamp(line, timestamp, precision)) {
            source.precision = precision;
            if (m_timeSources.find(source) == m_timeSources.end()) {
              TimeSourceDetails details;
              details.startTime = timestamp;
              details.currentTime = timestamp;
              details.endTime = timestamp;
              details.isChronological = true;
              m_timeSources[source] = details;
            } else {
              // Update existing source
              TimeSourceDetails& details = m_timeSources[source];
              // Check if timestamps are still chronological
              if (timestamp < details.currentTime) {
                details.isChronological = false;
              }
              details.currentTime = timestamp;
              details.endTime = timestamp;
            }
            m_has_timestamps = true;
          }
        }
      }
      line = GetNextNonEmptyLine();
    }

    // Log statistics about file quality
    wxLogMessage("Found %d valid and %d invalid sentences in %s",
                 validSentences, invalidSentences, m_ifilename);

    // Only fail if we found no valid sentences at all
    if (validSentences == 0) {
      hasValidTimestamps = false;
      error = _("Invalid file");
      return false;
    }

    // Analyze time sources and select primary.
    SelectPrimaryTimeSource();

    if (m_has_timestamps) {
      for (const auto& source : m_timeSources) {
        wxLogMessage(
            "  %s%s: precision=%d. isChronological=%d. Start=%s. End=%s",
            source.first.talkerId, source.first.sentenceId,
            source.first.precision, source.second.isChronological,
            FormatIsoDateTime(source.second.startTime),
            FormatIsoDateTime(source.second.endTime));
      }
      if (m_hasPrimaryTimeSource) {
        m_firstTimestamp = m_timeSources[m_primaryTimeSource].startTime;
        m_currentTimestamp = m_firstTimestamp;
        m_lastTimestamp = m_timeSources[m_primaryTimeSource].endTime;
        m_timestampParser.SetPrimaryTimeSource(m_primaryTimeSource.talkerId,
                                               m_primaryTimeSource.sentenceId,
                                               m_primaryTimeSource.precision);

        wxLogMessage(
            "Using %s%s (precision=%d) as primary time source. Start=%s. "
            "End=%s",
            m_primaryTimeSource.talkerId, m_primaryTimeSource.sentenceId,
            m_primaryTimeSource.precision, FormatIsoDateTime(m_firstTimestamp),
            FormatIsoDateTime(m_lastTimestamp));
      }
    } else {
      wxLogMessage("No timestamps found in NMEA file %s", m_ifilename);
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
  hasValidTimestamps = m_has_timestamps;
  error = wxEmptyString;
  return true;
}

wxString vdr_pi::GetNextNonEmptyLine(bool fromStart) {
  if (!m_istream.IsOpened()) return wxEmptyString;

  wxString line;
  if (fromStart) {
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

bool vdr_pi::SeekToFraction(double fraction) {
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
    int totalLines = m_istream.GetLineCount();
    if (totalLines > 0) {
      int targetLine = static_cast<int>(fraction * totalLines);
      m_istream.GoToLine(targetLine);
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
    wxTimeSpan totalSpan = m_lastTimestamp - m_firstTimestamp;
    wxTimeSpan targetSpan =
        wxTimeSpan::Seconds((totalSpan.GetSeconds().ToDouble() * fraction));
    wxDateTime targetTime = m_firstTimestamp + targetSpan;

    // Scan file until we find first message after target time
    wxString line = GetNextNonEmptyLine(true);  // Skip header
    line = GetNextNonEmptyLine();               // Get first data line

    while (!m_istream.Eof()) {
      wxDateTime timestamp;
      wxString nmea;
      bool success = ParseCSVLineTimestamp(line, &nmea, &timestamp);
      if (success && timestamp.IsValid() && timestamp >= targetTime) {
        // Found our position, prepare to play from here
        m_currentTimestamp = timestamp;
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
    wxTimeSpan totalSpan = m_lastTimestamp - m_firstTimestamp;
    wxTimeSpan targetSpan =
        wxTimeSpan::Seconds((totalSpan.GetSeconds().ToDouble() * fraction));
    wxDateTime targetTime = m_firstTimestamp + targetSpan;

    // Scan file for closest timestamp
    m_istream.GoToLine(0);
    wxString line;
    wxDateTime lastTimestamp;
    bool foundPosition = false;
    int precision;

    while (!m_istream.Eof()) {
      line = GetNextNonEmptyLine();
      wxDateTime timestamp;
      if (m_timestampParser.ParseTimestamp(line, timestamp, precision)) {
        if (timestamp >= targetTime) {
          m_currentTimestamp = timestamp;
          foundPosition = true;
          break;
        }
        lastTimestamp = timestamp;
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

bool vdr_pi::HasValidTimestamps() const {
  return m_has_timestamps && m_firstTimestamp.IsValid() &&
         m_lastTimestamp.IsValid() && m_currentTimestamp.IsValid();
}

double vdr_pi::GetProgressFraction() const {
  if (m_protocols.nmea0183ReplayMode == NMEA0183ReplayMode::LOOPBACK)
    return m_dm_replay_mgr->GetProgressFraction();

  // For files with timestamps
  if (HasValidTimestamps()) {
    wxTimeSpan totalSpan = m_lastTimestamp - m_firstTimestamp;
    wxTimeSpan currentSpan = m_currentTimestamp - m_firstTimestamp;

    if (totalSpan.GetSeconds().ToLong() == 0) {
      return 0.0;
    }

    return currentSpan.GetSeconds().ToDouble() /
           totalSpan.GetSeconds().ToDouble();
  }

  // For files without timestamps, use line position.
  if (m_istream.IsOpened()) {
    int totalLines = m_istream.GetLineCount();
    int currentLine = m_istream.GetCurrentLine();
    if (totalLines > 0) {
      // Clamp current line to total lines to ensure fraction doesn't
      // exceed 1.0.
      currentLine = std::max(0, std::min(currentLine, totalLines));
      return static_cast<double>(currentLine) / totalLines;
    }
  }

  return 0.0;
}

void vdr_pi::ClearInputFile() {
  m_ifilename.Clear();
  if (m_istream.IsOpened()) {
    m_istream.Close();
  }
}

wxString vdr_pi::GetInputFile() const {
  if (!m_ifilename.IsEmpty()) {
    if (wxFileExists(m_ifilename)) {
      return m_ifilename;
    }
  }
  return wxEmptyString;
}

std::unique_ptr<DataMonitorReplayMgr> vdr_pi::DmReplayMgrFactory() {
  auto update_controls = [&] { m_pvdrcontrol->UpdateControls(); };
  auto user_message = [&](VdrMsgType t, const std::string& s) {
    OnVdrMsg(t, s);
  };
  return std::make_unique<DataMonitorReplayMgr>(m_ifilename.ToStdString(),
                                                update_controls, user_message);
}

bool vdr_pi::LoadFile(const wxString& filename, wxString* error) {
  if (IsPlaying()) {
    StopPlayback();
  }

  m_ifilename = filename;
  if (m_protocols.nmea0183ReplayMode == NMEA0183ReplayMode::LOOPBACK) {
    m_dm_replay_mgr = std::move(DmReplayMgrFactory());
  }

  // Reset all file-related state
  m_is_csv_file = false;
  m_timestamp_idx = static_cast<unsigned int>(-1);
  m_message_idx = static_cast<unsigned int>(-1);
  m_header_fields.Clear();
  m_atFileEnd = false;

  // Close existing file if open
  if (m_istream.IsOpened()) {
    m_istream.Close();
  }
  if (!m_istream.Open(m_ifilename)) {
    if (error) {
      *error = _("Failed to open file: ") + filename;
    }
    return false;
  }
  return true;
}

void vdr_pi::SetToolbarToolStatus(int id, bool status) {
  if (id == m_tb_item_id_play || id == m_tb_item_id_record) {
    SetToolbarItemState(id, status);
  }
}
