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

#include <typeinfo>
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

vdr_pi::vdr_pi(void* ppimgr) : opencpn_plugin_117(ppimgr), wxTimer(this) {
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

  // Initialize configuration parameters
  m_interval = 1000;  // Default 1 second interval
  m_log_rotate = false;
  m_log_rotate_interval = 24;
  m_auto_start_recording = false;
  m_auto_recording_active = false;
  m_speed_threshold = 0.5;  // Default 0.5 knots
  m_stop_delay = 10;        // Default 10 minutes
  m_data_format = VDRDataFormat::RawNMEA;

  // Runtime variables
  m_recording = false;
  m_is_csv_file = false;
  m_timestamp_idx = -1;
  m_message_idx = -1;
  m_last_speed = 0.0;
  m_auto_recording_active = false;
}

int vdr_pi::Init(void) {
  AddLocaleCatalog(_T("opencpn-vdr_pi"));

  // Get a pointer to the opencpn configuration object
  m_pconfig = GetOCPNConfigObject();
  m_pauimgr = GetFrameAuiManager();

  // Load the configuration items
  LoadConfig();

  // If auto-start is enabled and we're not playing back and not using speed
  // threshold, start recording after initialization.
  if (m_auto_start_recording && !m_use_speed_threshold && !IsPlaying()) {
    wxLogMessage(_T("VDR: Auto-starting recording on plugin initialization"));
    StartRecording();
    SetToolbarToolStatus(m_tb_item_id_record, true);
    m_auto_recording_active = true;
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
  if (IsRunning())  // Timer started?
  {
    Stop();  // Stop timer
    m_istream.Close();
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
    AndroidSecureCopyFile(m_temp_outfile, m_final_outfile);
    ::wxRemoveFile(m_temp_outfile);
#endif
  }

  RemovePlugInTool(m_tb_item_id_record);
  RemovePlugInTool(m_tb_item_id_play);
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

int GetPlugInVersionPatch() { return PLUGIN_VERSION_PATCH; }

int GetPlugInVersionPost() { return PLUGIN_VERSION_TWEAK; }

const char* GetPlugInVersionPre() { return PKG_PRERELEASE; }

const char* GetPlugInVersionBuild() { return PKG_BUILD_INFO; }

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

// Format timestamp: YYYY-MM-DDTHH:MM:SS.mmmZ
// The format combines ISO format with milliseconds in UTC.
wxString FormatIsoDateTime(const wxDateTime& ts) {
  wxDateTime ts1 = ts.ToUTC();
  wxString timestamp = ts1.Format("%Y-%m-%dT%H:%M:%S.");
  timestamp += wxString::Format("%03ldZ", ts1.GetMillisecond());
  return timestamp;
}

wxString vdr_pi::FormatNMEAAsCSV(const wxString& nmea) {
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
  // Check for RMC sentence to get speed
  if (sentence.StartsWith("$GPRMC") || sentence.StartsWith("$GNRMC")) {
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
  if (!m_recording) return;

  // Check if we need to rotate the VDR file
  CheckLogRotation();

  switch (m_data_format) {
    case VDRDataFormat::CSV:
      m_ostream.Write(FormatNMEAAsCSV(sentence));
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

/**
 * Check if auto-recording should be started or stopped based.
 *
 * Don't auto-record if:
 * 1. Auto-recording is disabled
 * 2. Manual recording is active (not auto-recording)
 * 3. Playback is active
 */
void vdr_pi::CheckAutoRecording(double speed) {
  if ((m_recording && !m_auto_recording_active) || IsPlaying()) {
    return;
  }

  // If we're not using speed threshold, nothing to check.
  if (!m_use_speed_threshold) {
    return;
  }

  if (speed >= m_speed_threshold) {
    // Reset the below-threshold timer when speed goes above threshold
    m_below_threshold_since = wxDateTime();

    if (!m_auto_recording_active) {
      // Start recording
      wxLogMessage(
          _T("VDR: Auto-starting recording - speed %.2f exceeds threshold ")
          _T("%.2f"),
          speed, m_speed_threshold);
      StartRecording();
      m_auto_recording_active = true;
      SetToolbarToolStatus(m_tb_item_id_record, true);
    }
  } else if (speed < m_speed_threshold && m_auto_recording_active) {
    // Add hysteresis to prevent rapid starting/stopping
    static const double HYSTERESIS = 0.2;  // 0.2 knots below threshold
    if (speed < (m_speed_threshold - HYSTERESIS)) {
      // Check if this is the first time we've gone below threshold
      if (!m_below_threshold_since.IsValid()) {
        m_below_threshold_since = wxDateTime::Now();
        wxLogMessage(
            _T("VDR: Speed dropped below threshold, starting stop delay ")
            _T("timer"));
      } else {
        // Check if enough time has passed
        wxTimeSpan timeBelow = wxDateTime::Now() - m_below_threshold_since;
        if (timeBelow.GetMinutes() >= m_stop_delay) {
          wxLogMessage(
              _T("VDR: Auto-stopping recording - speed %.2f below threshold ")
              _T("%.2f for %d minutes"),
              speed, m_speed_threshold, m_stop_delay);
          StopRecording();
          m_auto_recording_active = false;
          SetToolbarToolStatus(m_tb_item_id_record, false);
          m_below_threshold_since = wxDateTime();  // Reset timer
        }
      }
    }
  }
}

bool vdr_pi::IsNMEAOrAIS(const wxString& line) {
  // NMEA sentences start with $ or !
  return line.StartsWith("$") || line.StartsWith("!");
}

bool vdr_pi::ParseCSVHeader(const wxString& header) {
  // Reset indices
  m_timestamp_idx = -1;
  m_message_idx = -1;
  m_header_fields.Clear();

  // If it looks like NMEA/AIS, it's not a header
  if (IsNMEAOrAIS(header)) {
    m_is_csv_file = false;
    return false;
  }

  // Split the header line
  wxStringTokenizer tokens(header, ",");
  int idx = 0;

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

  // We need at least a message field to be valid
  m_is_csv_file = (m_message_idx >= 0);

  return m_is_csv_file;
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

wxString vdr_pi::ParseCSVLine(const wxString& line, wxDateTime* timestamp) {
  if (!m_is_csv_file || IsNMEAOrAIS(line)) return line;

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
  if (timestamp && m_timestamp_idx >= 0 &&
      m_timestamp_idx < fields.GetCount()) {
    if (!ParseTimestamp(fields[m_timestamp_idx], timestamp)) {
      return wxEmptyString;
    }
  }

  // Get message field
  if (m_message_idx >= fields.GetCount()) return wxEmptyString;

  // No need to unescape quotes here as we handled them during parsing
  return fields[m_message_idx];
}

void vdr_pi::Notify() {
  if (!m_istream.IsOpened()) return;

  wxString str;
  int pos = m_istream.GetCurrentLine();

  if (m_istream.Eof() || pos == -1) {
    // First line - check if it's CSV.
    str = m_istream.GetFirstLine();
    if (ParseCSVHeader(str)) {
      // It's CSV, get first data line.
      str = m_istream.GetNextLine();
      m_playback_base_time = m_firstTimestamp;
    }
  } else {
    str = m_istream.GetNextLine();
  }

  if (m_istream.Eof() && str.IsEmpty()) {
    m_atFileEnd = true;
    PausePlayback();
    if (m_pvdrcontrol) {
      m_pvdrcontrol->UpdateControls();
    }
    return;
  }

  // Parse the line according to detected format (CVS or raw NMEA/AIS)
  if (m_is_csv_file) {
    wxDateTime timestamp;
    wxString nmea = ParseCSVLine(str, &timestamp);
    if (!nmea.IsEmpty()) {
      m_currentTimestamp = timestamp;
      ScheduleNextPlayback();
      PushNMEABuffer(nmea + _T("\r\n"));
    }
    if (m_pvdrcontrol) {
      m_pvdrcontrol->SetProgress(GetProgressFraction());
    }
  } else {
    // Raw NMEA/AIS sentences.
    wxString nmea = ParseCSVLine(str, NULL);
    if (!nmea.IsEmpty()) {
      PushNMEABuffer(nmea + _T("\r\n"));
    }
    if (m_pvdrcontrol) {
      double progress = static_cast<double>(pos) / m_istream.GetLineCount();
      m_pvdrcontrol->SetProgress(progress);
    }
  }
}

void vdr_pi::ScheduleNextPlayback() {
  double speedMultiplier = 1.0;
  if (m_pvdrcontrol) {
    speedMultiplier = m_pvdrcontrol->GetSpeedMultiplier();
  }
  // Calculate when this message should be played relative to playback start
  wxTimeSpan elapsedTime = m_currentTimestamp - m_firstTimestamp;
  wxLongLong ms = elapsedTime.GetMilliseconds();
  double scaledMs = ms.ToDouble() / speedMultiplier;
  wxTimeSpan scaledElapsed =
      wxTimeSpan::Milliseconds(static_cast<long>(scaledMs));
  wxDateTime targetTime = m_playback_base_time + scaledElapsed;

  // Calculate how long to wait
  wxDateTime now = wxDateTime::UNow();
  wxLogMessage("Base time: %s, Target time: %s, Now: %s. scaledMs: %f",
               m_playback_base_time.FormatISOCombined(),
               targetTime.FormatISOCombined(), now.FormatISOCombined(),
               scaledMs);
  if (targetTime > now) {
    wxTimeSpan waitTime = targetTime - now;
    Start(static_cast<int>(waitTime.GetMilliseconds().ToDouble()),
          wxTIMER_ONE_SHOT);
  } else {
    // We're behind schedule, play immediately
    Start(1, wxTIMER_ONE_SHOT);
  }
}

void vdr_pi::SetInterval(int interval) {
  m_interval = interval;
  if (IsRunning())                          // Timer started?
    Start(m_interval, wxTIMER_CONTINUOUS);  // restart timer with new interval
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
      if (IsRunning()) {
        Stop();
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
      m_pvdrcontrol = new VDRControl(GetOCPNCanvasWindow(), wxID_ANY, this);
      wxAuiPaneInfo pane = wxAuiPaneInfo()
                               .Name(_T("VDR"))
                               .Caption(_("Voyage Data Recorder"))
                               .CaptionVisible(true)
                               .Float()
                               .FloatingPosition(100, 100)
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
    if (IsRunning()) {
      wxMessageBox(_("Stop playback before starting recording."),
                   _("VDR Plugin"), wxOK | wxICON_INFORMATION);
      SetToolbarItemState(id, false);
      return;
    }
    if (m_recording) {
      StopRecording();
      SetToolbarItemState(id, false);
    } else {
      StartRecording();
      if (m_recording) {
        // Only set button state if recording started
        // successfully
        SetToolbarItemState(id, true);
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

  return true;
}

void vdr_pi::StartRecording() {
  if (m_recording) return;

  // Don't start recording if playback is active
  if (IsPlaying()) {
    wxLogMessage(_T("VDR: Cannot start recording while playback is active"));
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

  // Write CSV header if needed
  if (m_data_format == VDRDataFormat::CSV) {
    m_ostream.Write("timestamp,type,message\n");
  }

  m_recording = true;
  m_recording_start = wxDateTime::Now();
}

void vdr_pi::StopRecording() {
  if (!m_recording) return;

  m_ostream.Close();
  m_recording = false;
  m_auto_recording_active = false;

#ifdef __ANDROID__
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

  Start(m_interval, wxTIMER_CONTINUOUS);
  m_playing = true;

  if (m_pvdrcontrol) {
    m_pvdrcontrol->SetProgress(GetProgressFraction());
    m_pvdrcontrol->UpdateControls();
    m_pvdrcontrol->UpdateFileLabel(m_ifilename);
  }
}

void vdr_pi::PausePlayback() {
  if (!m_playing) return;

  Stop();
  m_playing = false;
  if (m_pvdrcontrol) m_pvdrcontrol->UpdateControls();
}

void vdr_pi::StopPlayback() {
  if (!m_playing) return;

  Stop();
  m_playing = false;
  m_istream.Close();

  if (m_pvdrcontrol) {
    m_pvdrcontrol->SetProgress(0);
    m_pvdrcontrol->UpdateControls();
    m_pvdrcontrol->UpdateFileLabel(wxEmptyString);
  }
}

void vdr_pi::ShowPreferencesDialog(wxWindow* parent) {
  VDRPrefsDialog dlg(parent, wxID_ANY, m_data_format, m_recording_dir,
                     m_log_rotate, m_log_rotate_interval,
                     m_auto_start_recording, m_use_speed_threshold,
                     m_speed_threshold, m_stop_delay);
  if (dlg.ShowModal() == wxID_OK) {
    SetDataFormat(dlg.GetDataFormat());
    SetRecordingDir(dlg.GetRecordingDir());
    SetLogRotate(dlg.GetLogRotate());
    SetLogRotateInterval(dlg.GetLogRotateInterval());
    SetAutoStartRecording(dlg.GetAutoStartRecording());
    SetUseSpeedThreshold(dlg.GetUseSpeedThreshold());
    SetSpeedThreshold(dlg.GetSpeedThreshold());
    SetStopDelay(dlg.GetStopDelay());
    SaveConfig();

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
    // Stop current recording
    StopRecording();
    // Start new recording
    StartRecording();
  }
}

bool vdr_pi::ScanFileTimestamps() {
  if (!m_istream.IsOpened()) {
    return false;
  }

  // Initialize timestamps as invalid
  m_firstTimestamp = wxDateTime();
  m_lastTimestamp = wxDateTime();
  m_currentTimestamp = wxDateTime();
  bool foundFirst = false;

  // Read first line to check format
  m_istream.GoToLine(0);
  wxString line = m_istream.GetFirstLine();

  // Try to parse as CSV
  m_is_csv_file = ParseCSVHeader(line);

  // If CSV, start with second line, otherwise start with first
  if (m_is_csv_file) {
    line = m_istream.GetNextLine();
  } else {
    m_istream.GoToLine(0);  // Reset to handle first line for NMEA
  }

  // Scan through file
  while (!m_istream.Eof()) {
    if (!line.IsEmpty()) {
      wxDateTime timestamp;
      bool validTimestamp = false;

      if (m_is_csv_file) {
        wxString nmea = ParseCSVLine(line, &timestamp);
        validTimestamp = !nmea.IsEmpty() && timestamp.IsValid();
      } else {
        validTimestamp = ParseNMEATimestamp(line, &timestamp);
      }

      if (validTimestamp) {
        // Update last timestamp for each valid timestamp found
        m_lastTimestamp = timestamp;

        // Store first timestamp found
        if (!foundFirst) {
          m_firstTimestamp = timestamp;
          m_currentTimestamp = timestamp;  // Initialize current to first
          foundFirst = true;
        }
      }
    }
    line = m_istream.GetNextLine();
  }

  // Reset file position to start for playback
  m_istream.GoToLine(0);

  if (foundFirst) {
    wxLogMessage("Found timestamps in %s file from %s to %s",
                 m_is_csv_file ? "CSV" : "NMEA",
                 FormatIsoDateTime(m_firstTimestamp),
                 FormatIsoDateTime(m_lastTimestamp));
    return true;
  } else {
    wxLogMessage("No timestamps found in %s file",
                 m_is_csv_file ? "CSV" : "NMEA");
    // Return false for CSV (error condition)
    // Return true for NMEA (acceptable condition)
    return !m_is_csv_file;
  }
}

bool vdr_pi::SeekToFraction(double fraction) {
  if (!m_istream.IsOpened()) {
    return false;
  }

  // Handle seeking in CSV files
  if (m_is_csv_file) {
    if (!m_firstTimestamp.IsValid() || !m_lastTimestamp.IsValid()) {
      return false;
    }

    // Calculate target timestamp
    wxTimeSpan totalSpan = m_lastTimestamp - m_firstTimestamp;
    wxTimeSpan targetSpan =
        wxTimeSpan::Seconds((totalSpan.GetSeconds().ToDouble() * fraction));
    wxDateTime targetTime = m_firstTimestamp + targetSpan;

    // Scan file until we find first message after target time
    m_istream.GoToLine(0);
    wxString line = m_istream.GetFirstLine();  // Skip header
    line = m_istream.GetNextLine();

    while (!m_istream.Eof()) {
      wxDateTime timestamp;
      wxString nmea = ParseCSVLine(line, &timestamp);
      if (!nmea.IsEmpty() && timestamp.IsValid() && timestamp >= targetTime) {
        // Found our position, prepare to play from here
        m_currentTimestamp = timestamp;
        if (m_playing) {
          AdjustPlaybackBaseTime();
        }
        return true;
      }
      line = m_istream.GetNextLine();
    }
    return false;
  }

  // Handle seeking in NMEA files
  else {
    // If we have valid timestamps in the NMEA file, use them
    if (m_firstTimestamp.IsValid() && m_lastTimestamp.IsValid()) {
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
        line = m_istream.GetNextLine();
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

    // For NMEA files without timestamps or if timestamp seek failed,
    // fall back to line-based position
    int totalLines = m_istream.GetLineCount();
    if (totalLines > 0) {
      int targetLine = static_cast<int>(fraction * totalLines);
      m_istream.GoToLine(targetLine);

      // Get the line content at current position
      wxString line = m_istream.GetNextLine();
      wxDateTime timestamp;
      if (ParseNMEATimestamp(line, &timestamp)) {
        m_currentTimestamp = timestamp;
        if (m_playing) {
          AdjustPlaybackBaseTime();
        }
      }
      return true;
    }
  }

  return false;
}

double vdr_pi::GetProgressFraction() const {
  // For files with timestamps
  if (m_firstTimestamp.IsValid() && m_lastTimestamp.IsValid() &&
      m_currentTimestamp.IsValid()) {
    wxTimeSpan totalSpan = m_lastTimestamp - m_firstTimestamp;
    wxTimeSpan currentSpan = m_currentTimestamp - m_firstTimestamp;

    if (totalSpan.GetSeconds().ToLong() == 0) {
      return 0.0;
    }

    return currentSpan.GetSeconds().ToDouble() /
           totalSpan.GetSeconds().ToDouble();
  }

  // For NMEA files without timestamps, use line position
  if (!m_is_csv_file && m_istream.IsOpened()) {
    int totalLines = m_istream.GetLineCount();
    int currentLine = m_istream.GetCurrentLine();
    if (totalLines > 0 && currentLine >= 0) {
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

  if (sentenceId.Contains(wxT("RMC"))) {  // GPRMC, GNRMC etc
    // Format: $GPRMC,HHMMSS.ss,A,LLLL.LL,a,YYYYY.YY,a,x.x,x.x,DDMMYY,x.x,a*hh
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
    }

    // Parse time HHMMSS.ss
    if (timeStr.length() >= 6) {
      int hour = wxAtoi(timeStr.Mid(0, 2));
      int minute = wxAtoi(timeStr.Mid(2, 2));
      int second = wxAtoi(timeStr.Mid(4, 2));
      double subseconds = timeStr.length() > 7 ? wxAtof(timeStr.Mid(7)) : 0.0;

      timestamp->Set(day, static_cast<wxDateTime::Month>(month - 1), year, hour,
                     minute, second, static_cast<int>(subseconds * 1000));
      return true;
    }
  } else if (sentenceId.Contains(wxT("ZDA"))) {  // GPZDA, GNZDA etc
    // Format: $GPZDA,HHMMSS.ss,DD,MM,YYYY,xx,xx*hh
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
    }

    // Parse time HHMMSS.ss
    if (timeStr.length() >= 6) {
      int hour = wxAtoi(timeStr.Mid(0, 2));
      int minute = wxAtoi(timeStr.Mid(2, 2));
      int second = wxAtoi(timeStr.Mid(4, 2));
      double subseconds = timeStr.length() > 7 ? wxAtof(timeStr.Mid(7)) : 0.0;

      timestamp->Set(day, static_cast<wxDateTime::Month>(month - 1), year, hour,
                     minute, second, static_cast<int>(subseconds * 1000));
      return true;
    }
  } else if (sentenceId.Contains(wxT("GGA"))) {  // GPGGA, GNGGA etc
    // Format:
    // $GPGGA,HHMMSS.ss,LLLL.LL,a,YYYYY.YY,a,x,xx,x.x,x.x,M,x.x,M,x.x,xxxx*hh
    if (!tok.HasMoreTokens()) return false;
    wxString timeStr = tok.GetNextToken();

    // Parse time HHMMSS.ss
    if (timeStr.length() >= 6) {
      int hour = wxAtoi(timeStr.Mid(0, 2));
      int minute = wxAtoi(timeStr.Mid(2, 2));
      int second = wxAtoi(timeStr.Mid(4, 2));
      double subseconds = timeStr.length() > 7 ? wxAtof(timeStr.Mid(7)) : 0.0;

      timestamp->Set(day, static_cast<wxDateTime::Month>(month - 1), year, hour,
                     minute, second, static_cast<int>(subseconds * 1000));
      return true;
    }
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
  ID_VDR_PROGRESS
};

BEGIN_EVENT_TABLE(VDRControl, wxWindow)
EVT_BUTTON(ID_VDR_LOAD, VDRControl::OnLoadButton)
EVT_RADIOBUTTON(ID_VDR_DATA_FORMAT_RADIOBUTTON,
                VDRControl::OnDataFormatRadioButton)
EVT_BUTTON(ID_VDR_PLAY_PAUSE, VDRControl::OnPlayPauseButton)
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

  // File information section
  wxStaticBox* fileBox = new wxStaticBox(this, wxID_ANY, _("VDR File"));
  wxStaticBoxSizer* fileSizer = new wxStaticBoxSizer(fileBox, wxVERTICAL);
  m_loadBtn = new wxButton(this, ID_VDR_LOAD, _("Load"));
  fileSizer->Add(m_loadBtn, 0, wxALL | wxALIGN_CENTER, 5);
  m_fileLabel =
      new wxStaticText(this, wxID_ANY, _("No file loaded"), wxDefaultPosition,
                       wxDefaultSize, wxST_ELLIPSIZE_START);
  fileSizer->Add(m_fileLabel, 1, wxEXPAND | wxALL, 5);
  mainSizer->Add(fileSizer, 0, wxEXPAND | wxALL,
                 5);  // Add fileSizer, not fileBox

  // Playback section
  wxStaticBox* playBox = new wxStaticBox(this, wxID_ANY, _("Playback"));
  wxStaticBoxSizer* playSizer = new wxStaticBoxSizer(playBox, wxVERTICAL);

  m_playBtnTooltip = _("Start Playback");
  m_pauseBtnTooltip = _("Pause Playback");
  m_stopBtnTooltip = _("End of File");

  m_playPauseBtn =
      new wxButton(this, ID_VDR_PLAY_PAUSE, wxString::FromUTF8("▶"),
                   wxDefaultPosition, wxDefaultSize, 0);
  wxSize buttonSize(40, 40);
  m_playPauseBtn->SetMinSize(buttonSize);
  m_playPauseBtn->SetInitialSize(buttonSize);
  wxFont font = m_playPauseBtn->GetFont();
  font.SetPointSize(font.GetPointSize() * 1.5);
  m_playPauseBtn->SetFont(font);
  m_playPauseBtn->SetToolTip(m_playBtnTooltip);
  playSizer->Add(m_playPauseBtn, 0, wxALL | wxALIGN_CENTER | wxSHAPED, 5);

  // Speed control
  wxBoxSizer* speedSizer = new wxBoxSizer(wxHORIZONTAL);
  speedSizer->Add(new wxStaticText(this, wxID_ANY, _("Replay Speed:")), 0,
                  wxALIGN_CENTER_VERTICAL | wxALL, 5);

  m_speedSlider =
      new wxSlider(this, wxID_ANY, 1, 1, 100, wxDefaultPosition, wxDefaultSize,
                   wxSL_HORIZONTAL | wxSL_VALUE_LABEL);
  speedSizer->Add(m_speedSlider, 1, wxEXPAND | wxALL, 5);
  playSizer->Add(speedSizer, 0, wxEXPAND);

  // Progress gauge
  wxBoxSizer* timeSizer = new wxBoxSizer(wxVERTICAL);

  m_timeLabel = new wxStaticText(this, wxID_ANY, _("Date and Time: --"));
  timeSizer->Add(m_timeLabel, 0, wxALL | wxALIGN_LEFT, 5);

  m_progressSlider =
      new wxSlider(this, ID_VDR_PROGRESS, 0, 0, 1000, wxDefaultPosition,
                   wxDefaultSize, wxSL_HORIZONTAL | wxSL_BOTTOM);
  timeSizer->Add(m_progressSlider, 1, wxEXPAND | wxALL, 5);
  playSizer->Add(timeSizer, 0, wxEXPAND);

  mainSizer->Add(playSizer, 0, wxEXPAND | wxALL,
                 5);  // Add playSizer, not playBox

  SetSizer(mainSizer);
  mainSizer->Fit(this);
  Layout();

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
  int response = PlatformFileSelectorDialog(GetOCPNCanvasWindow(), &file,
                                            _("Select Playback File"),
                                            wxEmptyString, _T(""), _T("*.*"));

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
  m_timestamp_idx = -1;
  m_message_idx = -1;
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
    // For CSV files, timestamps are required
    if (m_is_csv_file) {
      wxMessageBox(_("No valid timestamps found in CSV file."), _("VDR Plugin"),
                   wxOK | wxICON_ERROR);
      m_istream.Close();
      return false;
    }
    // For NMEA files, timestamps are optional
    wxLogMessage(
        "No timestamps found in NMEA file - continuing with basic playback");
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

  // Update layout
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

void VDRControl::OnSpeedSliderUpdated(wxCommandEvent& event) {
  if (m_pvdr->IsPlaying()) {
    m_pvdr->AdjustPlaybackBaseTime();
  }
}

void VDRControl::SetProgress(double fraction) {
  if (m_pvdr->GetFirstTimestamp().IsValid() &&
      m_pvdr->GetLastTimestamp().IsValid()) {
    // Update slider position (0-1000 range)
    int sliderPos = wxRound(fraction * 1000);
    m_progressSlider->SetValue(sliderPos);

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