/******************************************************************************
 * $Id: vdr_pi.cpp, v0.2 2011/05/23 SethDart Exp $
 *
 * Project:  OpenCPN
 * Purpose:  VDR Plugin
 * Author:   Jean-Eudes Onfray
 *
 ***************************************************************************
 *   Copyright (C) 2011 by Jean-Eudes Onfray   *
 *   $EMAIL$   *
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
 ***************************************************************************
 */

#include "wx/wxprec.h"

#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif  // precompiled headers

#include "wx/tokenzr.h"
#include "wx/statline.h"
#include "wx/display.h"

#include <map>
#include <typeinfo>

#include "ocpn_plugin.h"
#include "vdr_pi_prefs.h"
#include "vdr_pi.h"
#include "icons.h"

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

vdr_pi::vdr_pi(void* ppimgr) : opencpn_plugin_118(ppimgr) {
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

  m_pvdrcontrol = NULL;

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

  AddLocaleCatalog(_T("opencpn-vdr_pi"));

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

#ifdef VDR_USE_SVG
  m_tb_item_id_record =
      InsertPlugInToolSVG(_T( "VDR" ), _svg_vdr_record, _svg_record_toggled,
                          _svg_record_toggled, wxITEM_CHECK, _("VDR Record"),
                          _T( "" ), NULL, VDR_TOOL_POSITION, 0, this);
  m_tb_item_id_play = InsertPlugInToolSVG(
      _T( "VDR" ), _svg_vdr_play, _svg_play_toggled, _svg_play_toggled,
      wxITEM_CHECK, _("VDR Play"), _T( "" ), NULL, VDR_TOOL_POSITION, 0, this);
  m_recording = false;
#else
  m_tb_item_id_record = InsertPlugInTool(
      _T(""), _img_vdr_record, _img_vdr_record, wxITEM_CHECK, _("VDR Record"),
      _T(""), NULL, VDR_TOOL_POSITION, 0, this);
  m_tb_item_id_play =
      InsertPlugInTool(_T(""), _img_vdr_play, _img_vdr_play, wxITEM_CHECK,
                       _("VDR Play"), _T(""), NULL, VDR_TOOL_POSITION, 0, this);
  m_recording = false;
#endif

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
    m_pvdrcontrol = NULL;
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
    // The plugin API 1.19 is not available on Android.
    // When it is available, the ifndef condition can be removed.
    std::map<unsigned int, wxString> parameterGroupNumbers = {
        {59392, "ISO Acknowledgement"},
        {59904, "ISO Request"},
        {60160, "ISO Transport Protocol, Data Transfer"},
        {60416, "ISO Transport Protocol, Connection Management"},
        {60928, "ISO Address Claim"},
        {61184, "Manufacturer Proprietary Single Frame"},
        {65280, "Manufacturer Proprietary Single Frame"},
        {65305,
         "Manufacturer Proprietary Single Frame (B&G AC12 Autopilot Status)"},
        {65309,
         "Manufacturer Proprietary Single Frame (B&G WS320 Battery Status)"},
        {65312,
         "Manufacturer Proprietary Single Frame (B&G WS320 Wireless Status)"},
        {65340,
         "Manufacturer Proprietary Single Frame (B&G AC12 Autopilot Mode)"},
        {65341, "Manufacturer Proprietary Single Frame (B&G AC12 Wind Angle)"},
        {127245, "Rudder Angle"},
        {127250, "Vessel Heading"},
        {127257, "Attitude (Roll and Pitch)"},
        {128259, "Speed Through Water"},
        {128267, "Water Depth"},
        {128275, "Distance Log"},
        {128777, "Windlass Status"},
        {129029, "GNSS Position Data"},
        {129540, "GNSS Satellites in View"},
        {130306, "Wind Data"},
        {130310, "Environmental Parameters"},
        {130313, "Environmental Parameters"}};

    for (const auto& it : parameterGroupNumbers) {
      m_n2k_listeners.push_back(
          GetListener(NMEA2000Id(it.first), EVT_N2K, m_eventHandler));
    }

    m_eventHandler->Bind(EVT_N2K, &vdr_pi::OnN2KEvent, this);
  }
}

// Format timestamp: YYYY-MM-DDTHH:MM:SS.mmmZ
// The format combines ISO format with milliseconds in UTC.
wxString FormatIsoDateTime(const wxDateTime& ts) {
  wxDateTime ts1 = ts.ToUTC();
  wxString timestamp = ts1.Format("%Y-%m-%dT%H:%M:%S.");
  timestamp += wxString::Format("%03ldZ", ts1.GetMillisecond());
  return timestamp;
}

void vdr_pi::OnSignalKEvent(wxCommandEvent& event) {
  if (!m_protocols.signalK) {
    // SignalK recording is disabled.
    return;
  }
  // TODO: Implement SignalK recording.
}

void vdr_pi::OnN2KEvent(wxCommandEvent& event) {
  if (!m_protocols.nmea2000) {
    // NMEA 2000 recording is disabled.
    return;
  }

  ObservedEvt& ev = dynamic_cast<ObservedEvt&>(event);
  // Get PGN from event
  NMEA2000Id id(ev.GetId());

  // Check for Speed Through Water PGN (128259)
  if (id.id == 128259) {
    // Get binary payload
    std::vector<uint8_t> payload = GetN2000Payload(id, ev);

    // Speed Through Water message format:
    // Byte 0-1: SID and reserved
    // Byte 2-5: Speed Through Water (float, knots)
    if (payload.size() >= 6) {
      // Extract speed value (float, 4 bytes, little-endian)
      float speed;
      memcpy(&speed, &payload[2], 4);

      // Convert to double for consistency with NMEA 0183 handling
      double speed_knots = static_cast<double>(speed);

      // Update last known speed
      m_last_speed = speed_knots;

      // Check if we should start/stop recording based on speed
      CheckAutoRecording(speed_knots);
    }
  }
  if (!m_recording) {
    return;
  }

  // Get binary payload and source
  std::vector<uint8_t> payload = GetN2000Payload(id, ev);
  std::string source = GetN2000Source(id, ev);

  // Convert binary payload to hex string for logging
  wxString hex_payload;
  for (const auto& byte : payload) {
    hex_payload += wxString::Format("%02X", byte);
  }

  // Format N2K message for recording.
  wxString formatted_message;
  switch (m_data_format) {
    case VDRDataFormat::CSV: {
      // CSV format: timestamp,type,source,pgn,payload
      wxString timestamp = FormatIsoDateTime(wxDateTime::UNow());
      formatted_message = wxString::Format("%s,NMEA2000,%s,%d,%s\n", timestamp,
                                           source, id.id, hex_payload);
      break;
    }
    case VDRDataFormat::RawNMEA:
      // PCDIN format: $PCDIN,<pgn>,<source>,<payload>
      formatted_message =
          wxString::Format("$PCDIN,%d,%s,%s\r\n", id.id, source, hex_payload);
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

  return wxString::Format("%s,%s,%s\n", timestamp, type, escaped);
}

void vdr_pi::SetNMEASentence(wxString& sentence) {
  if (!m_protocols.nmea0183) {
    // Recording of NMEA 0183 is disabled.
    return;
  }
  // Check for RMC sentence to get speed and check for auto-recording.
  // There can be different talkers on the stream so look at the message type
  // irespective of the talker
  if (sentence.size() >= 6 && sentence.substr(3, 3) == "RMC") {
    wxStringTokenizer tkz(sentence, wxT(","));
    wxString token;

    // Skip to speed field (field 7)
    for (int i = 0; i < 7 && tkz.HasMoreTokens(); i++) {
      token = tkz.GetNextToken();
    }

    if (tkz.HasMoreTokens()) {
      token = tkz.GetNextToken();
      if (!token.IsEmpty()) {
        double speed;
        if (token.ToDouble(&speed)) {
          // Convert from knots to desired units if needed
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

  switch (m_data_format) {
    case VDRDataFormat::CSV:
      m_ostream.Write(FormatNMEA0183AsCSV(sentence));
      break;
    case VDRDataFormat::RawNMEA:
    default:
      m_ostream.Write(sentence);
      break;
  }
}

void vdr_pi::SetAISSentence(wxString& sentence) {
  SetNMEASentence(sentence);  // Handle the same way as NMEA
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
  m_timestamp_idx = static_cast<unsigned int>(-1);
  m_message_idx = static_cast<unsigned int>(-1);
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
  // We need at least a message field to be valid.
  bool is_csv_file = (m_message_idx >= 0);

  return is_csv_file;
}

/**
 * Parse a timestamp from a string in ISO 8601 format.
 */
bool ParseTimestamp(const wxString& timeStr, wxDateTime* timestamp) {
  wxString::const_iterator end;
  wxDateTime tempTime;

  // Try formats with milliseconds
  if (tempTime.ParseFormat(timeStr, "%Y-%m-%dT%H:%M:%S.%l%z", &end)) {
    *timestamp = tempTime;
    return true;
  }

  // Try formats without milliseconds
  if (tempTime.ParseFormat(timeStr, "%Y-%m-%dT%H:%M:%S%z", &end)) {
    *timestamp = tempTime;
    return true;
  }

  return false;
}

wxString vdr_pi::ParseCSVLineTimestamp(const wxString& line,
                                       wxDateTime* timestamp) {
  assert(m_is_csv_file);

  wxArrayString fields;
  wxString currentField;
  bool inQuotes = false;

  for (size_t i = 0; i < line.Length(); i++) {
    wxChar ch = line[i];

    if (ch == '"') {
      if (inQuotes && i + 1 < line.Length() && line[i + 1] == '"') {
        // Double quotes inside quoted field = escaped quote
        currentField += '"';
        i++;  // Skip next quote
      } else {
        // Toggle quote state
        inQuotes = !inQuotes;
      }
    } else if (ch == ',' && !inQuotes) {
      // End of field
      fields.Add(currentField);
      currentField.Clear();
    } else {
      currentField += ch;
    }
  }

  // Add the last field
  fields.Add(currentField);

  // Parse timestamp if requested and available
  if (timestamp && m_timestamp_idx != static_cast<unsigned int>(-1) &&
      m_timestamp_idx < fields.GetCount()) {
    if (!ParseTimestamp(fields[m_timestamp_idx], timestamp)) {
      return wxEmptyString;
    }
  }

  // Get message field
  if (m_message_idx == static_cast<unsigned int>(-1) ||
      m_message_idx >= fields.GetCount()) {
    return wxEmptyString;
  }

  // No need to unescape quotes here as we handled them during parsing
  return fields[m_message_idx];
}

void vdr_pi::FlushSentenceBuffer() {
  for (const auto& sentence : m_sentence_buffer) {
    PushNMEABuffer(sentence + "\r\n");
  }
  m_sentence_buffer.clear();
}

void vdr_pi::Notify() {
  if (!m_istream.IsOpened()) return;

  wxDateTime now = wxDateTime::UNow();
  wxDateTime targetTime;
  bool behindSchedule = true;

  // Keep processing messages until we catch up with scheduled time.
  while (behindSchedule && !m_istream.Eof()) {
    wxString line;
    int pos = m_istream.GetCurrentLine();

    if (m_istream.Eof() || pos == -1) {
      // First line - check if it's CSV.
      line = GetNextNonEmptyLine(true);
      m_is_csv_file = ParseCSVHeader(line);
      if (m_is_csv_file) {
        // Get first data line.
        line = GetNextNonEmptyLine();
        m_playback_base_time = m_firstTimestamp;
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
    bool hasTimestamp = false;

    if (m_is_csv_file) {
      nmea = ParseCSVLineTimestamp(line, &timestamp);
      if (!nmea.IsEmpty()) {
        nmea += "\r\n";
        hasTimestamp = true;
      }
    } else {
      nmea = line + "\r\n";
      hasTimestamp = ParseNMEATimestamp(line, &timestamp);
    }
    if (!nmea.IsEmpty()) {
      if (hasTimestamp) {
        m_currentTimestamp = timestamp;
        targetTime = GetNextPlaybackTime();

        // Check if we've caught up to schedule
        if (targetTime.IsValid() && targetTime > now) {
          behindSchedule = false;
          // Before scheduling next update, flush our sentence buffer
          FlushSentenceBuffer();
          // Schedule next notification
          wxTimeSpan waitTime = targetTime - now;
          m_timer->Start(
              static_cast<int>(waitTime.GetMilliseconds().ToDouble()),
              wxTIMER_ONE_SHOT);
        }
      }
      // Add sentence to buffer, maintaining max size.
      m_sentence_buffer.push_back(nmea);
      if (m_sentence_buffer.size() > MAX_MSG_BUFFER_SIZE) {
        if (!m_messages_dropped) {
          double speedMultiplier =
              m_pvdrcontrol ? m_pvdrcontrol->GetSpeedMultiplier() : 1.0;
          wxLogMessage(
              "Playback dropping messages to maintain timing at %.0fx speed",
              speedMultiplier);
          m_messages_dropped = true;
        }
        m_sentence_buffer.pop_front();
      }
    }
    // If we're still behind or have no timestamps, schedule immediate next
    // message
    if (behindSchedule) {
      m_timer->Start(1, wxTIMER_ONE_SHOT);
    }
  }
  // Update progress regardless of file type
  if (m_pvdrcontrol) {
    m_pvdrcontrol->SetProgress(GetProgressFraction());
  }
}

wxDateTime vdr_pi::GetNextPlaybackTime() const {
  if (!m_currentTimestamp.IsValid() || !m_firstTimestamp.IsValid() ||
      !m_playback_base_time.IsValid()) {
    return wxDateTime();  // Return invalid time if we don't have valid
                          // timestamps
  }
  double speedMultiplier =
      m_pvdrcontrol ? m_pvdrcontrol->GetSpeedMultiplier() : 1.0;
  // Calculate when this message should be played relative to playback start.
  wxTimeSpan elapsedTime = m_currentTimestamp - m_firstTimestamp;
  wxLongLong ms = elapsedTime.GetMilliseconds();
  double scaledMs = ms.ToDouble() / speedMultiplier;
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
      m_pvdrcontrol = NULL;

      // Update toolbar state
      SetToolbarItemState(id, false);
      return;
    }

    if (!m_pvdrcontrol) {

      wxPoint dialog_position = wxPoint(100, 100);
      //  Dialog will be fixed position on Android, so position carefully
#ifdef __WXQT__       // a surrogate for Android
      wxRect tbRect = GetMasterToolbarRect();
      dialog_position.y = 0;
      dialog_position.x = tbRect.x + tbRect.width + 2;
#endif

      m_pvdrcontrol = new VDRControl(GetOCPNCanvasWindow(), wxID_ANY, this);
      wxAuiPaneInfo pane = wxAuiPaneInfo()
                               .Name(_T("VDR"))
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

  pConf->SetPath(_T("/PlugIns/VDR"));
  pConf->Read(_T("InputFilename"), &m_ifilename, wxEmptyString);
  pConf->Read(_T("OutputFilename"), &m_ofilename, wxEmptyString);

  // Default directory handling based on platform
#ifdef __ANDROID__
  wxString defaultDir =
      "/storage/emulated/0/Android/data/org.opencpn.opencpn/files";
#else
  wxString defaultDir = *GetpPrivateApplicationDataLocation();
#endif

  pConf->Read(_T("RecordingDirectory"), &m_recording_dir, defaultDir);
  pConf->Read(_T("Interval"), &m_interval, 1000);
  pConf->Read(_T("LogRotate"), &m_log_rotate, false);
  pConf->Read(_T("LogRotateInterval"), &m_log_rotate_interval, 24);
  pConf->Read(_T("AutoStartRecording"), &m_auto_start_recording, false);
  pConf->Read(_T("UseSpeedThreshold"), &m_use_speed_threshold, false);
  pConf->Read(_T("SpeedThreshold"), &m_speed_threshold, 0.5);
  pConf->Read(_T("StopDelay"), &m_stop_delay, 10);  // Default 10 minutes

  pConf->Read(_T("EnableNMEA0183"), &m_protocols.nmea0183, true);
  pConf->Read(_T("EnableNMEA2000"), &m_protocols.nmea2000, false);
  pConf->Read(_T("EnableSignalK"), &m_protocols.signalK, false);

  int format;
  pConf->Read(_T("DataFormat"), &format,
              static_cast<int>(VDRDataFormat::RawNMEA));
  m_data_format = static_cast<VDRDataFormat>(format);

  return true;
}

bool vdr_pi::SaveConfig(void) {
  wxFileConfig* pConf = (wxFileConfig*)m_pconfig;

  if (!pConf) return false;

  pConf->SetPath(_T("/PlugIns/VDR"));
  pConf->Write(_T("InputFilename"), m_ifilename);
  pConf->Write(_T("OutputFilename"), m_ofilename);
  pConf->Write(_T("RecordingDirectory"), m_recording_dir);
  pConf->Write(_T("Interval"), m_interval);
  pConf->Write(_T("LogRotate"), m_log_rotate);
  pConf->Write(_T("LogRotateInterval"), m_log_rotate_interval);
  pConf->Write(_T("AutoStartRecording"), m_auto_start_recording);
  pConf->Write(_T("UseSpeedThreshold"), m_use_speed_threshold);
  pConf->Write(_T("SpeedThreshold"), m_speed_threshold);
  pConf->Write(_T("StopDelay"), m_stop_delay);
  pConf->Write(_T("DataFormat"), static_cast<int>(m_data_format));

  pConf->Write(_T("EnableNMEA0183"), m_protocols.nmea0183);
  pConf->Write(_T("EnableNMEA2000"), m_protocols.nmea2000);
  pConf->Write(_T("EnableSignalK"), m_protocols.signalK);
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
      wxLogError(_("Failed to create recording directory: %s"),
                 m_recording_dir);
      return;
    }
  }

  if (!m_ostream.Open(fullpath, wxFile::write)) {
    wxLogError(_("Failed to create recording file: %s"), fullpath);
    return;
  }
  wxLogMessage("Start recording to file: %s", fullpath);

  // Write CSV header if needed
  if (m_data_format == VDRDataFormat::CSV) {
    m_ostream.Write("timestamp,type,message\n");
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

  // Get current speed multiplier from control
  double speedMultiplier = 1.0;
  if (m_pvdrcontrol) {
    speedMultiplier = m_pvdrcontrol->GetSpeedMultiplier();
  }

  // Calculate how much time has "elapsed" in the recording up to our current
  // position
  wxTimeSpan elapsed = m_currentTimestamp - m_firstTimestamp;

  // Set base time so that current playback position corresponds to current wall
  // clock
  m_playback_base_time =
      wxDateTime::UNow() -
      wxTimeSpan::Milliseconds(static_cast<long>(
          (elapsed.GetMilliseconds().ToDouble() / speedMultiplier)));
}

void vdr_pi::StartPlayback() {
  if (m_ifilename.IsEmpty()) {
    wxMessageBox(_("No file selected."), _("VDR Plugin"),
                 wxOK | wxICON_INFORMATION);
    return;
  }
  if (!wxFileExists(m_ifilename)) {
    wxMessageBox(_("File does not exist."), _("VDR Plugin"),
                 wxOK | wxICON_INFORMATION);
    return;
  }

  // Reset end-of-file state when starting playback
  m_atFileEnd = false;

  // Always adjust base time when starting playback, whether from pause or seek
  AdjustPlaybackBaseTime();

  if (!m_istream.IsOpened()) {
    if (!m_istream.Open(m_ifilename)) {
      wxMessageBox(_("Failed to open file."), _("VDR Plugin"),
                   wxOK | wxICON_INFORMATION);
      return;
    }
  }
  m_messages_dropped = false;
  m_playing = true;

  if (m_pvdrcontrol) {
    m_pvdrcontrol->SetProgress(GetProgressFraction());
    m_pvdrcontrol->UpdateControls();
    m_pvdrcontrol->UpdateFileLabel(m_ifilename);
  }
  wxLogMessage(
      "Start playback from file: %s. Progress: %.2f. Has timestamps: %d",
      m_ifilename, GetProgressFraction(), m_has_timestamps);
  // Process first line immediately.
  Notify();
}

void vdr_pi::PausePlayback() {
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

  if (m_pvdrcontrol) {
    m_pvdrcontrol->SetProgress(0);
    m_pvdrcontrol->UpdateControls();
    m_pvdrcontrol->UpdateFileLabel(wxEmptyString);
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
#ifdef __WXQT__     // Android
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

bool vdr_pi::ScanFileTimestamps() {
  if (!m_istream.IsOpened()) {
    return false;
  }

  // Initialize timestamps as invalid.
  m_has_timestamps = false;
  m_firstTimestamp = wxDateTime();
  m_lastTimestamp = wxDateTime();
  m_currentTimestamp = wxDateTime();
  bool foundFirst = false;  // Flag to track first valid timestamp found.
  wxDateTime previousTimestamp;

  // Read first line to check format.
  wxString line = GetNextNonEmptyLine(true);
  if (m_istream.Eof() && line.IsEmpty()) {
    wxLogMessage("File is empty or contains only empty lines");
    return false;
  }

  // Try to parse as CSV file.
  m_is_csv_file = ParseCSVHeader(line);

  // If CSV, start with second line, otherwise start with first.
  if (m_is_csv_file) {
    line = GetNextNonEmptyLine();
  } else {
    // Raw NMEA/AIS, reset to start.
    line = GetNextNonEmptyLine(true);
  }

  // Scan through file
  while (!m_istream.Eof()) {
    if (!line.IsEmpty()) {
      wxDateTime timestamp;
      bool validTimestamp = false;

      if (m_is_csv_file) {
        wxString nmea = ParseCSVLineTimestamp(line, &timestamp);
        validTimestamp = !nmea.IsEmpty() && timestamp.IsValid();
      } else {
        validTimestamp = ParseNMEATimestamp(line, &timestamp);
      }

      if (validTimestamp) {
        if (previousTimestamp.IsValid()) {
          wxTimeSpan diff = timestamp - previousTimestamp;
          // Be lenient with small jumps, but warn about large ones.
          // One scenario to consider is a RMC sentence with milliseconds
          // precision, followed by a ZDA sentence with only seconds precision.
          // Example:
          // RMC timestamp: 2016-05-06T15:53:51.600Z
          // ZDA timestamp: 2016-05-06T15:53:51.000Z
          // A naive comparison would show a 600ms backwards jump, but we should
          // ignore it. On the other hand, if the time goes backwards by a large
          // amount (e.g., 3 days), we cannot use the timestamps in the file.
          if (diff.GetSeconds().ToLong() < -5) {
            wxLogMessage(
                "VDR file contains significant timestamp jump backwards. "
                "Previous ts: %s, Current ts: %s",
                FormatIsoDateTime(previousTimestamp),
                FormatIsoDateTime(timestamp));
            // If the VDR file contains non-monotonically increasing timestamps,
            // (i.e., the file is not in chronological order), we cannot use
            // timestamps for playback. We will still allow playback based on
            // line number for NMEA files without timestamps.
            m_has_timestamps = false;
            m_firstTimestamp = wxDateTime();
            m_lastTimestamp = wxDateTime();
            m_currentTimestamp = wxDateTime();
            // Reset file position
            m_istream.GoToLine(0);
            m_fileStatus = _("Timestamps are not in chronological order");
            return !m_is_csv_file;
          }
        }
        previousTimestamp = timestamp;

        // Update last timestamp for each valid timestamp found.
        m_lastTimestamp = timestamp;

        // Store first timestamp found
        if (!foundFirst) {
          m_firstTimestamp = timestamp;
          m_currentTimestamp = timestamp;  // Initialize current to first.
          foundFirst = true;
        }
      }
    }
    line = GetNextNonEmptyLine();
  }

  // Reset file position to start for playback
  m_istream.GoToLine(0);
  // Set timestamp validity flag
  m_has_timestamps = foundFirst;

  if (m_has_timestamps) {
    m_fileStatus.Clear();
    wxLogMessage("Found timestamps in %s file from %s to %s",
                 m_is_csv_file ? "CSV" : "NMEA",
                 FormatIsoDateTime(m_firstTimestamp),
                 FormatIsoDateTime(m_lastTimestamp));
  } else {
    m_fileStatus = _("No timestamps found (missing RMC/ZDA)");
    wxLogMessage("No timestamps found in %s file",
                 m_is_csv_file ? "CSV" : "NMEA");
  }
  // Return false only for CSV files without timestamps (error condition)
  return !m_is_csv_file || m_has_timestamps;
}

wxString vdr_pi::GetNextNonEmptyLine(bool fromStart) {
  if (!m_istream.IsOpened()) return wxEmptyString;

  wxString line;
  if (fromStart) {
    m_istream.GoToLine(0);
    line = m_istream.GetFirstLine();
  } else {
    line = m_istream.GetNextLine();
  }
  line.Trim(true).Trim(false);

  // Keep reading until we find a non-empty line or reach EOF
  while (line.IsEmpty() && !m_istream.Eof()) {
    line = m_istream.GetNextLine();
    line.Trim(true).Trim(false);
  }

  return line;
}

bool vdr_pi::SeekToFraction(double fraction) {
  if (!m_istream.IsOpened()) {
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
      wxString nmea = ParseCSVLineTimestamp(line, &timestamp);
      if (!nmea.IsEmpty() && timestamp.IsValid() && timestamp >= targetTime) {
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

    while (!m_istream.Eof()) {
      line = GetNextNonEmptyLine();
      wxDateTime timestamp;
      if (ParseNMEATimestamp(line, &timestamp)) {
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
      currentLine = std::min(currentLine, totalLines);
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

bool vdr_pi::ParseNMEATimestamp(const wxString& nmea, wxDateTime* timestamp) {
  // Check for valid NMEA sentence
  if (nmea.IsEmpty() || nmea[0] != '$') {
    return false;
  }

  // Split the sentence into fields
  wxStringTokenizer tok(nmea, wxT(",*"));
  if (!tok.HasMoreTokens()) return false;

  wxString sentenceId = tok.GetNextToken();

  // Get current date for sentences with only time
  wxDateTime currentDate = wxDateTime::Now();
  int year = currentDate.GetYear();
  int month = currentDate.GetMonth() + 1;  // wxDateTime months are 0-11
  int day = currentDate.GetDay();

  // Function to parse time HHMMSS or HHMMSS.sss (millisecond precision
  // optional)
  auto parseTimeField = [](const wxString& timeStr, int* hour, int* minute,
                           int* second, int* millisecond) -> bool {
    if (timeStr.length() < 6) return false;

    // Parse base time components
    *hour = wxAtoi(timeStr.Mid(0, 2));
    *minute = wxAtoi(timeStr.Mid(2, 2));
    *second = wxAtoi(timeStr.Mid(4, 2));

    // Parse optional milliseconds
    *millisecond = 0;
    if (timeStr.length() > 7) {  // ".xxx" format
      double subseconds = wxAtof(timeStr.Mid(6));
      *millisecond = static_cast<int>(subseconds * 1000);
    }

    // Validate
    return *hour >= 0 && *hour <= 23 && *minute >= 0 && *minute <= 59 &&
           *second >= 0 && *second <= 59 && *millisecond >= 0 &&
           *millisecond < 1000;
  };

  if (sentenceId.Contains(wxT("RMC"))) {  // GPRMC, GNRMC etc
    // Format: $GPRMC,HHMMSS.sss,A,LLLL.LL,a,YYYYY.YY,a,x.x,x.x,DDMMYY,x.x,a*hh
    // Millisecond precision is optional.
    if (!tok.HasMoreTokens()) return false;
    wxString timeStr = tok.GetNextToken();

    // Skip to date field (field 9)
    for (int i = 0; i < 7 && tok.HasMoreTokens(); i++) {
      tok.GetNextToken();
    }

    if (!tok.HasMoreTokens()) return false;
    wxString dateStr = tok.GetNextToken();

    // Parse date DDMMYY
    if (dateStr.length() >= 6) {
      day = wxAtoi(dateStr.Mid(0, 2));
      month = wxAtoi(dateStr.Mid(2, 2));
      year = 2000 + wxAtoi(dateStr.Mid(4, 2));  // Assuming 20xx
                                                // Validate date components
      if (month < 1 || month > 12 || day < 1 || day > 31 || year < 1980 ||
          year > 2100) {
        return false;
      }
    }

    // Parse time HHMMSS or HHMMSS.sss (millisecond precision optional)
    int hour, minute, second, millisecond;
    if (!parseTimeField(timeStr, &hour, &minute, &second, &millisecond)) {
      return false;
    }
    timestamp->Set(day, static_cast<wxDateTime::Month>(month - 1), year, hour,
                   minute, second, millisecond);
    return timestamp->IsValid();
  } else if (sentenceId.Contains(wxT("ZDA"))) {  // GPZDA, GNZDA etc
    // Format: $GPZDA,HHMMSS.sss,DD,MM,YYYY,xx,xx*hh
    if (!tok.HasMoreTokens()) return false;
    wxString timeStr = tok.GetNextToken();

    // Get date fields
    if (!tok.HasMoreTokens()) return false;
    wxString dayStr = tok.GetNextToken();
    if (!tok.HasMoreTokens()) return false;
    wxString monthStr = tok.GetNextToken();
    if (!tok.HasMoreTokens()) return false;
    wxString yearStr = tok.GetNextToken();

    if (!yearStr.IsEmpty() && !monthStr.IsEmpty() && !dayStr.IsEmpty()) {
      year = wxAtoi(yearStr);
      month = wxAtoi(monthStr);
      day = wxAtoi(dayStr);
      // Validate date components
      if (month < 1 || month > 12 || day < 1 || day > 31 || year < 1980 ||
          year > 2100) {
        return false;
      }
    }

    // Parse time HHMMSS or HHMMSS.sss (millisecond precision optional)
    int hour, minute, second, millisecond;
    if (!parseTimeField(timeStr, &hour, &minute, &second, &millisecond)) {
      return false;
    }
    timestamp->Set(day, static_cast<wxDateTime::Month>(month - 1), year, hour,
                   minute, second, millisecond);
    return timestamp->IsValid();
  }

  return false;
}

//----------------------------------------------------------------
//
//    VDR replay control Implementation
//
//----------------------------------------------------------------

enum {
  ID_VDR_LOAD = wxID_HIGHEST + 1,
  ID_VDR_PLAY_PAUSE,
  ID_VDR_DATA_FORMAT_RADIOBUTTON,
  ID_VDR_SPEED_SLIDER,
  ID_VDR_PROGRESS,
  ID_VDR_SETTINGS
};

BEGIN_EVENT_TABLE(VDRControl, wxWindow)
EVT_BUTTON(ID_VDR_LOAD, VDRControl::OnLoadButton)
EVT_RADIOBUTTON(ID_VDR_DATA_FORMAT_RADIOBUTTON,
                VDRControl::OnDataFormatRadioButton)
EVT_BUTTON(ID_VDR_PLAY_PAUSE, VDRControl::OnPlayPauseButton)
EVT_BUTTON(ID_VDR_SETTINGS, VDRControl::OnSettingsButton)
EVT_SLIDER(ID_VDR_SPEED_SLIDER, VDRControl::OnSpeedSliderUpdated)
EVT_COMMAND_SCROLL_THUMBTRACK(ID_VDR_PROGRESS,
                              VDRControl::OnProgressSliderUpdated)
EVT_COMMAND_SCROLL_THUMBRELEASE(ID_VDR_PROGRESS,
                                VDRControl::OnProgressSliderEndDrag)
END_EVENT_TABLE()

VDRControl::VDRControl(wxWindow* parent, wxWindowID id, vdr_pi* vdr)
    : wxWindow(parent, id, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE,
               _T("VDR Control")),
      m_pvdr(vdr),
      m_isDragging(false),
      m_wasPlayingBeforeDrag(false) {
  wxColour cl;
  GetGlobalColor(_T("DILG1"), &cl);
  SetBackgroundColour(cl);

  CreateControls();

  // Check if there's already a file loaded from config
  wxString currentFile = m_pvdr->GetInputFile();
  if (!currentFile.IsEmpty()) {
    // Try to load the file
    if (m_pvdr->LoadFile(currentFile)) {
      UpdateFileLabel(currentFile);
      UpdateControls();
    } else {
      // If loading fails, clear the saved filename
      m_pvdr->ClearInputFile();
      UpdateFileLabel(wxEmptyString);
      UpdateControls();
    }
  }
}

void VDRControl::CreateControls() {
  // Main vertical sizer
  wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

  wxFont* baseFont = GetOCPNScaledFont_PlugIn("Dialog", 0);
  SetFont(*baseFont);
  wxFont* buttonFont = FindOrCreateFont_PlugIn(
      baseFont->GetPointSize() * GetContentScaleFactor(), baseFont->GetFamily(),
      baseFont->GetStyle(), baseFont->GetWeight());
  // Calculate button dimensions based on font height
  int fontHeight = buttonFont->GetPointSize();
  int buttonSize = fontHeight * 1.2;  // Adjust multiplier as needed
  // Ensure minimum size of 32 pixels for touch usability
  buttonSize = std::max(buttonSize, 32);
#ifdef __WXQT__
  // A simple way to get touch-compatible tool size
  wxRect tbRect = GetMasterToolbarRect();
  buttonSize = std::max(buttonSize, tbRect.width / 2);
#endif
  wxSize buttonDimension(buttonSize, buttonSize);

  // File information section
  wxBoxSizer* fileSizer = new wxBoxSizer(wxHORIZONTAL);

  // Settings button
  m_settingsBtn =
      new wxButton(this, ID_VDR_SETTINGS, wxString::FromUTF8("⚙️"),
                   wxDefaultPosition, buttonDimension, wxBU_EXACTFIT);
  m_settingsBtn->SetFont(*buttonFont);
  m_settingsBtn->SetMinSize(buttonDimension);
  m_settingsBtn->SetMaxSize(buttonDimension);
  m_settingsBtn->SetToolTip(_("Settings"));
  fileSizer->Add(m_settingsBtn, 0, wxALL, 2);

  // Load button
  m_loadBtn = new wxButton(this, ID_VDR_LOAD, wxString::FromUTF8("📂"),
                           wxDefaultPosition, buttonDimension, wxBU_EXACTFIT);
  m_loadBtn->SetFont(*buttonFont);
  m_loadBtn->SetMinSize(buttonDimension);
  m_loadBtn->SetMaxSize(buttonDimension);
  m_loadBtn->SetToolTip(_("Load VDR File"));
  fileSizer->Add(m_loadBtn, 0, wxALL, 2);

  m_fileLabel =
      new wxStaticText(this, wxID_ANY, _("No file loaded"), wxDefaultPosition,
                       wxDefaultSize, wxST_ELLIPSIZE_START);
  fileSizer->Add(m_fileLabel, 1, wxALL | wxALIGN_CENTER_VERTICAL, 2);

  mainSizer->Add(fileSizer, 0, wxALL, 4);

  // Play controls and progress in one row
  wxBoxSizer* controlSizer = new wxBoxSizer(wxHORIZONTAL);

  // Play button setup
  m_playBtnTooltip = _("Start Playback");
  m_pauseBtnTooltip = _("Pause Playback");
  m_stopBtnTooltip = _("End of File");

  m_playPauseBtn =
      new wxButton(this, ID_VDR_PLAY_PAUSE, wxString::FromUTF8("▶"),
                   wxDefaultPosition, buttonDimension, wxBU_EXACTFIT);
  m_playPauseBtn->SetFont(*buttonFont);
  m_playPauseBtn->SetMinSize(buttonDimension);
  m_playPauseBtn->SetMaxSize(buttonDimension);
  m_playPauseBtn->SetToolTip(m_playBtnTooltip);
  controlSizer->Add(m_playPauseBtn, 0, wxALL, 3);

  // Progress slider in the same row as play button
  m_progressSlider =
      new wxSlider(this, ID_VDR_PROGRESS, 0, 0, 1000, wxDefaultPosition,
                   wxDefaultSize, wxSL_HORIZONTAL | wxSL_BOTTOM);
  controlSizer->Add(m_progressSlider, 1, wxALIGN_CENTER_VERTICAL, 0);
  mainSizer->Add(controlSizer, 0, wxEXPAND | wxALL, 4);

  // Time label
  m_timeLabel = new wxStaticText(this, wxID_ANY, _("Date and Time: --"),
                                 wxDefaultPosition, wxSize(200, -1));
  mainSizer->Add(m_timeLabel, 0, wxEXPAND | wxALL, 4);

  // Speed control
  wxBoxSizer* speedSizer = new wxBoxSizer(wxHORIZONTAL);
  speedSizer->Add(new wxStaticText(this, wxID_ANY, _("Speed:")), 0,
                  wxALIGN_CENTER_VERTICAL | wxRIGHT, 3);
  m_speedSlider =
      new wxSlider(this, wxID_ANY, 1, 1, 1000, wxDefaultPosition, wxDefaultSize,
                   wxSL_HORIZONTAL | wxSL_VALUE_LABEL);
  speedSizer->Add(m_speedSlider, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL, 0);
  mainSizer->Add(speedSizer, 0, wxEXPAND | wxALL, 4);

  // Status label (info/error messages).
  m_statusLabel = new wxStaticText(this, wxID_ANY, wxEmptyString);
  mainSizer->Add(m_statusLabel, 0, wxEXPAND | wxALL, 4);

  SetSizer(mainSizer);
  wxClientDC dc(m_timeLabel);
  wxSize textExtent = dc.GetTextExtent(_("Date and Time: YYYY-MM-DD HH:MM:SS"));
  int minWidth = std::max(100, textExtent.GetWidth() + 20);  // 20px padding
  mainSizer->SetMinSize(wxSize(minWidth, -1));
  Layout();
  mainSizer->Fit(this);

  // Initial state
  UpdateControls();
}

void VDRControl::UpdateTimeLabel() {
  if (m_pvdr->GetCurrentTimestamp().IsValid()) {
    wxString timeStr =
        m_pvdr->GetCurrentTimestamp().ToUTC().Format("%Y-%m-%d %H:%M:%S UTC");
    m_timeLabel->SetLabel("Date and Time: " + timeStr);
  } else {
    m_timeLabel->SetLabel(_("Date and Time: --"));
  }
}

void VDRControl::OnLoadButton(wxCommandEvent& event) {
  // Stop any current playback
  if (m_pvdr->IsPlaying()) {
    m_pvdr->StopPlayback();
  }

  wxString file;
  wxString init_directory = wxEmptyString;
#ifdef __WXQT__
  init_directory = *GetpPrivateApplicationDataLocation();
#endif

  int response = PlatformFileSelectorDialog(GetOCPNCanvasWindow(), &file,
                                            _("Select Playback File"),
                                            init_directory, _T(""), _T("*.*"));

  if (response == wxID_OK) {
    if (m_pvdr->LoadFile(file)) {  // We'll add this method to vdr_pi
      UpdateFileLabel(file);
      m_progressSlider->SetValue(0);
      UpdateControls();
    }
  }
}

bool vdr_pi::LoadFile(const wxString& filename) {
  if (IsPlaying()) {
    StopPlayback();
  }

  // Reset all file-related state
  m_ifilename = filename;
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
    wxMessageBox(_("Failed to open file: ") + filename, _("VDR Plugin"),
                 wxOK | wxICON_ERROR);
    return false;
  }

  // Try to scan for timestamps
  if (!ScanFileTimestamps()) {
    // For CSV files, timestamps should be present.
    // However, there is a possibility that the file contains non-monotonically
    // increasing timestamps, in which case we cannot use timestamps for
    // playback. In this case, we will still allow playback based on line
    // number.
    wxLogMessage(
        "No timestamps found in file - continuing with basic playback");
  }

  return true;
}

void VDRControl::OnProgressSliderUpdated(wxScrollEvent& event) {
  if (!m_isDragging) {
    m_isDragging = true;
    m_wasPlayingBeforeDrag = m_pvdr->IsPlaying();
    if (m_wasPlayingBeforeDrag) {
      m_pvdr->PausePlayback();
    }
  }
  if (m_pvdr->GetFirstTimestamp().IsValid() &&
      m_pvdr->GetLastTimestamp().IsValid()) {
    // Update time display while dragging but don't seek yet
    double fraction = m_progressSlider->GetValue() / 1000.0;
    wxTimeSpan totalSpan =
        m_pvdr->GetLastTimestamp() - m_pvdr->GetFirstTimestamp();
    wxTimeSpan currentSpan =
        wxTimeSpan::Seconds((totalSpan.GetSeconds().ToDouble() * fraction));
    m_pvdr->SetCurrentTimestamp(m_pvdr->GetFirstTimestamp() + currentSpan);
    UpdateTimeLabel();
  }
  event.Skip();
}

void VDRControl::OnProgressSliderEndDrag(wxScrollEvent& event) {
  double fraction = m_progressSlider->GetValue() / 1000.0;
  m_pvdr->SeekToFraction(fraction);
  // Reset the end-of-file state when user drags the slider, the button should
  // change to "play" state.
  m_pvdr->ResetEndOfFile();
  if (m_wasPlayingBeforeDrag) {
    m_pvdr->StartPlayback();
  }
  m_isDragging = false;
  UpdateControls();
  event.Skip();
}

void vdr_pi::SetToolbarToolStatus(int id, bool status) {
  if (id == m_tb_item_id_play || id == m_tb_item_id_record) {
    SetToolbarItemState(id, status);
  }
}

void VDRControl::UpdateControls() {
  bool hasFile = !m_pvdr->GetInputFile().IsEmpty();
  bool isRecording = m_pvdr->IsRecording();
  bool isPlaying = m_pvdr->IsPlaying();
  bool isAtEnd = m_pvdr->IsAtFileEnd();

  // Update the play/pause/stop button appearance
  if (isAtEnd) {
    m_playPauseBtn->SetLabel(wxString::FromUTF8("⏹"));
    m_playPauseBtn->SetToolTip(m_stopBtnTooltip);
  } else {
    m_playPauseBtn->SetLabel(isPlaying ? wxString::FromUTF8("⏸")
                                       : wxString::FromUTF8("▶"));
    m_playPauseBtn->SetToolTip(isPlaying ? m_pauseBtnTooltip
                                         : m_playBtnTooltip);
  }

  // Enable/disable controls based on state
  m_loadBtn->Enable(!isRecording && !isPlaying);
  m_playPauseBtn->Enable(hasFile && !isRecording);
  m_settingsBtn->Enable(!isPlaying && !isRecording);
  m_progressSlider->Enable(hasFile && !isRecording);

  // Update toolbar state
  m_pvdr->SetToolbarToolStatus(m_pvdr->GetPlayToolbarItemId(), isPlaying);

  // Update time display
  if (hasFile && m_pvdr->GetCurrentTimestamp().IsValid()) {
    wxString timeStr =
        m_pvdr->GetCurrentTimestamp().ToUTC().Format("%Y-%m-%d %H:%M:%S UTC");
    m_timeLabel->SetLabel("Date and Time: " + timeStr);
  } else {
    m_timeLabel->SetLabel(_("Date and Time: --"));
  }
  // Update status message if present
  const wxString& status = m_pvdr->GetFileStatus();
  m_statusLabel->SetLabel(status);
  m_statusLabel->Show(!status.IsEmpty());
  Layout();
}

void VDRControl::UpdateFileLabel(const wxString& filename) {
  if (filename.IsEmpty()) {
    m_fileLabel->SetLabel(_("No file loaded"));
  } else {
    wxFileName fn(filename);
    m_fileLabel->SetLabel(fn.GetFullName());
  }
  m_fileLabel->GetParent()->Layout();
}

void VDRControl::OnPlayPauseButton(wxCommandEvent& event) {
  if (!m_pvdr->IsPlaying()) {
    if (m_pvdr->GetInputFile().IsEmpty()) {
      wxMessageBox(_("Please load a file first."), _("VDR Plugin"),
                   wxOK | wxICON_INFORMATION);
      return;
    }

    // If we're at the end, restart from beginning
    if (m_pvdr->IsAtFileEnd()) {
      m_pvdr->StopPlayback();
    }

    m_pvdr->StartPlayback();
  } else {
    m_pvdr->PausePlayback();
  }
  UpdateControls();
}

void VDRControl::OnDataFormatRadioButton(wxCommandEvent& event) {
  // Radio button state is tracked by wx, we just need to handle any
  // format-specific UI updates here if needed in the future
}

void VDRControl::OnSettingsButton(wxCommandEvent& event) {
  m_pvdr->ShowPreferencesDialogNative(this);
  event.Skip();
}

void VDRControl::OnSpeedSliderUpdated(wxCommandEvent& event) {
  if (m_pvdr->IsPlaying()) {
    m_pvdr->AdjustPlaybackBaseTime();
  }
}

void VDRControl::SetProgress(double fraction) {
  // Update slider position (0-1000 range)
  int sliderPos = wxRound(fraction * 1000);
  m_progressSlider->SetValue(sliderPos);

  if (m_pvdr->GetFirstTimestamp().IsValid() &&
      m_pvdr->GetLastTimestamp().IsValid()) {
    // Calculate and set current timestamp based on the fraction
    wxTimeSpan totalSpan =
        m_pvdr->GetLastTimestamp() - m_pvdr->GetFirstTimestamp();
    wxTimeSpan currentSpan =
        wxTimeSpan::Seconds((totalSpan.GetSeconds().ToDouble() * fraction));
    m_pvdr->SetCurrentTimestamp(m_pvdr->GetFirstTimestamp() + currentSpan);

    // Update time display
    UpdateTimeLabel();
  }
}

void VDRControl::SetColorScheme(PI_ColorScheme cs) {
  wxColour cl;
  GetGlobalColor(_T("DILG1"), &cl);
  SetBackgroundColour(cl);

  Refresh(false);
}