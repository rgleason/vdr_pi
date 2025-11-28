/***************************************************************************
 *
 * Project:  OpenCPN
 * Purpose:  VDR Plugin
 *
 ***************************************************************************
 *   Copyright (C) 2024 by David S. Register                               *
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
#include <cstdint>

#include "vdr_pi_prefs_net.h"
#include "vdr_pi_prefs.h"

enum {
  ID_VDR_DIR_BUTTON = wxID_HIGHEST + 1,
  ID_VDR_LOG_ROTATE_CHECK,       // ID for log rotation checkbox
  ID_VDR_AUTO_RECORD_CHECK,      // ID for auto recording checkbox
  ID_USE_SPEED_THRESHOLD_CHECK,  // ID for speed threshold checkbox
  ID_NMEA0183_CHECK,
  ID_NMEA2000_CHECK,
  ID_SIGNALK_CHECK,
  ID_NMEA0183_NETWORK_RADIO,
  ID_NMEA0183_INTERNAL_RADIO,
  ID_NMEA0183_LOOPBACK_RADIO
};

BEGIN_EVENT_TABLE(VDRPrefsDialog, wxDialog)
EVT_BUTTON(wxID_OK, VDRPrefsDialog::OnOK)
EVT_BUTTON(ID_VDR_DIR_BUTTON, VDRPrefsDialog::OnDirSelect)
EVT_CHECKBOX(ID_VDR_LOG_ROTATE_CHECK, VDRPrefsDialog::OnLogRotateCheck)
EVT_CHECKBOX(ID_VDR_AUTO_RECORD_CHECK, VDRPrefsDialog::OnAutoRecordCheck)
EVT_CHECKBOX(ID_USE_SPEED_THRESHOLD_CHECK,
             VDRPrefsDialog::OnUseSpeedThresholdCheck)
EVT_CHECKBOX(ID_NMEA0183_CHECK, VDRPrefsDialog::OnProtocolCheck)
EVT_CHECKBOX(ID_NMEA2000_CHECK, VDRPrefsDialog::OnProtocolCheck)
EVT_RADIOBUTTON(ID_NMEA0183_NETWORK_RADIO,
                VDRPrefsDialog::OnNMEA0183ReplayModeChanged)
EVT_RADIOBUTTON(ID_NMEA0183_INTERNAL_RADIO,
                VDRPrefsDialog::OnNMEA0183ReplayModeChanged)
EVT_RADIOBUTTON(ID_NMEA0183_LOOPBACK_RADIO,
                VDRPrefsDialog::OnNMEA0183ReplayModeChanged)
END_EVENT_TABLE()

VDRPrefsDialog::VDRPrefsDialog(wxWindow* parent, wxWindowID id,
                               VDRDataFormat format,
                               const wxString& recordingDir, bool logRotate,
                               int logRotateInterval, bool autoStartRecording,
                               bool useSpeedThreshold, double speedThreshold,
                               int stopDelay,
                               const VDRProtocolSettings& protocols)
    : wxDialog(parent, id, _("VDR Preferences"), wxDefaultPosition,
               wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      m_format(format),
      m_recording_dir(recordingDir),
      m_log_rotate(logRotate),
      m_log_rotate_interval(logRotateInterval),
      m_auto_start_recording(autoStartRecording),
      m_use_speed_threshold(useSpeedThreshold),
      m_speed_threshold(speedThreshold),
      m_stop_delay(stopDelay),
      m_protocols(protocols) {
  CreateControls();
  GetSizer()->Fit(this);
  GetSizer()->SetSizeHints(this);
  Centre();
}

void VDRPrefsDialog::OnProtocolCheck(wxCommandEvent& event) {
  UpdateControlStates();
}

void VDRPrefsDialog::UpdateControlStates() {
  // File rotation controls
  m_logRotateIntervalCtrl->Enable(m_logRotateCheck->GetValue());

  // Auto-recording controls
  bool autoRecordEnabled = m_autoStartRecordingCheck->GetValue();
  m_useSpeedThresholdCheck->Enable(autoRecordEnabled);

  // Speed threshold controls - only enabled if both auto-record and use-speed
  // are checked
  bool speedEnabled = autoRecordEnabled && m_useSpeedThresholdCheck->GetValue();
  m_speedThresholdCtrl->Enable(speedEnabled);
  m_stopDelayCtrl->Enable(speedEnabled);
}

void VDRPrefsDialog::CreateControls() {
  wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
  SetSizer(mainSizer);

  // Create notebook for tabs
  wxNotebook* notebook;
  notebook = new wxNotebook(this, wxID_ANY);
  mainSizer->Add(notebook, 1, wxEXPAND | wxALL, 5);

  // Add tabs
  wxPanel* recordingTab = CreateRecordingTab(notebook);
  wxPanel* replayTab = CreateReplayTab(notebook);

  notebook->AddPage(recordingTab, _("Recording"));
  notebook->AddPage(replayTab, _("Replay"));

  // Standard dialog buttons
  wxStdDialogButtonSizer* buttonSizer = new wxStdDialogButtonSizer();
  buttonSizer->AddButton(new wxButton(this, wxID_OK));
  buttonSizer->AddButton(new wxButton(this, wxID_CANCEL));
  buttonSizer->Realize();
  mainSizer->Add(buttonSizer, 0, wxEXPAND | wxALL, 5);

  mainSizer->SetSizeHints(this);

  // Set initial control states
  UpdateControlStates();
}

wxPanel* VDRPrefsDialog::CreateRecordingTab(wxWindow* parent) {
  wxPanel* panel = new wxPanel(parent);
  wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

  // Protocol selection section
  wxStaticBox* protocolBox =
      new wxStaticBox(panel, wxID_ANY, _("Recording Protocols"));
  wxStaticBoxSizer* protocolSizer =
      new wxStaticBoxSizer(protocolBox, wxVERTICAL);

  m_nmea0183Check = new wxCheckBox(panel, ID_NMEA0183_CHECK, _("NMEA 0183"));
  m_nmea0183Check->SetValue(m_protocols.nmea0183);
  protocolSizer->Add(m_nmea0183Check, 0, wxALL, 5);

  m_nmea2000Check = new wxCheckBox(panel, ID_NMEA2000_CHECK, _("NMEA 2000"));
  m_nmea2000Check->SetValue(m_protocols.nmea2000);
  protocolSizer->Add(m_nmea2000Check, 0, wxALL, 5);

#if 0
  m_signalKCheck = new wxCheckBox(panel, ID_SIGNALK_CHECK, _("Signal K"));
  m_signalKCheck->SetValue(m_protocols.signalK);
  protocolSizer->Add(m_signalKCheck, 0, wxALL, 5);
#endif

  mainSizer->Add(protocolSizer, 0, wxEXPAND | wxALL, 5);

  // Add format choice
  wxStaticBox* formatBox =
      new wxStaticBox(panel, wxID_ANY, _("Recording Format"));
  wxStaticBoxSizer* formatSizer = new wxStaticBoxSizer(formatBox, wxVERTICAL);

  m_nmeaRadio = new wxRadioButton(panel, wxID_ANY, _("Raw NMEA"),
                                  wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
  m_csvRadio = new wxRadioButton(panel, wxID_ANY, _("CSV with timestamps"));

  formatSizer->Add(m_nmeaRadio, 0, wxALL, 5);
  formatSizer->Add(m_csvRadio, 0, wxALL, 5);

  mainSizer->Add(formatSizer, 0, wxEXPAND | wxALL, 5);

  // Add recording directory controls
  wxStaticBox* dirBox =
      new wxStaticBox(panel, wxID_ANY, _("Recording Directory"));
  wxStaticBoxSizer* dirSizer = new wxStaticBoxSizer(dirBox, wxHORIZONTAL);

  m_dirCtrl = new wxTextCtrl(panel, wxID_ANY, m_recording_dir,
                             wxDefaultPosition, wxDefaultSize, wxTE_READONLY);
  m_dirButton = new wxButton(panel, ID_VDR_DIR_BUTTON, _("Browse..."));

  dirSizer->Add(m_dirCtrl, 1, wxALL | wxEXPAND, 5);
  dirSizer->Add(m_dirButton, 0, wxALL | wxEXPAND, 5);

  mainSizer->Add(dirSizer, 0, wxEXPAND | wxALL, 5);

  // Select current format
  switch (m_format) {
    case VDRDataFormat::CSV:
      m_csvRadio->SetValue(true);
      break;
    case VDRDataFormat::RawNMEA:
    default:
      m_nmeaRadio->SetValue(true);
      break;
  }

  // File management section.
  wxStaticBox* logBox =
      new wxStaticBox(panel, wxID_ANY, _("VDR File Management"));
  wxStaticBoxSizer* logSizer = new wxStaticBoxSizer(logBox, wxVERTICAL);

  m_logRotateCheck = new wxCheckBox(panel, ID_VDR_LOG_ROTATE_CHECK,
                                    _("Create new VDR file every:"));
  m_logRotateCheck->SetValue(m_log_rotate);

  wxBoxSizer* intervalSizer = new wxBoxSizer(wxHORIZONTAL);
  m_logRotateIntervalCtrl = new wxSpinCtrl(
      panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
      wxSP_ARROW_KEYS, 1, 168, m_log_rotate_interval);
  intervalSizer->Add(m_logRotateIntervalCtrl, 0,
                     wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
  intervalSizer->Add(new wxStaticText(panel, wxID_ANY, _("hours")), 0,
                     wxALIGN_CENTER_VERTICAL);

  logSizer->Add(m_logRotateCheck, 0, wxALL, 5);
  logSizer->Add(intervalSizer, 0, wxLEFT | wxRIGHT | wxBOTTOM, 5);

  mainSizer->Add(logSizer, 0, wxEXPAND | wxALL, 5);

  // Auto-recording section
  wxStaticBox* autoBox =
      new wxStaticBox(panel, wxID_ANY, _("Automatic Recording"));
  wxStaticBoxSizer* autoSizer = new wxStaticBoxSizer(autoBox, wxVERTICAL);

  // Auto-start option
  m_autoStartRecordingCheck = new wxCheckBox(
      panel, ID_VDR_AUTO_RECORD_CHECK, _("Automatically start recording"));
  m_autoStartRecordingCheck->SetValue(m_auto_start_recording);
  autoSizer->Add(m_autoStartRecordingCheck, 0, wxALL, 5);

  // Speed threshold option
  wxBoxSizer* speedSizer = new wxBoxSizer(wxHORIZONTAL);
  m_useSpeedThresholdCheck = new wxCheckBox(
      panel, ID_USE_SPEED_THRESHOLD_CHECK, _("When speed over ground exceeds"));
  m_useSpeedThresholdCheck->SetValue(m_use_speed_threshold);
  speedSizer->Add(m_useSpeedThresholdCheck, 0, wxALIGN_CENTER_VERTICAL);

  m_speedThresholdCtrl = new wxSpinCtrlDouble(
      panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
      wxSP_ARROW_KEYS, 0.0, 20.0, m_speed_threshold, 0.1);
  speedSizer->Add(m_speedThresholdCtrl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT,
                  5);
  speedSizer->Add(new wxStaticText(panel, wxID_ANY, _("knots")), 0,
                  wxALIGN_CENTER_VERTICAL);
  autoSizer->Add(speedSizer, 0, wxLEFT | wxRIGHT | wxBOTTOM, 5);

  // Pause delay control
  wxBoxSizer* delaySizer = new wxBoxSizer(wxHORIZONTAL);
  delaySizer->Add(new wxStaticText(panel, wxID_ANY, _("Pause recording after")),
                  0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
  m_stopDelayCtrl =
      new wxSpinCtrl(panel, wxID_ANY, wxEmptyString, wxDefaultPosition,
                     wxDefaultSize, wxSP_ARROW_KEYS, 1, 60, m_stop_delay);
  delaySizer->Add(m_stopDelayCtrl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
  delaySizer->Add(
      new wxStaticText(panel, wxID_ANY, _("minutes below speed threshold")), 0,
      wxALIGN_CENTER_VERTICAL);
  autoSizer->Add(delaySizer, 0, wxLEFT | wxRIGHT | wxBOTTOM, 5);
  mainSizer->Add(autoSizer, 0, wxEXPAND | wxALL, 5);

  panel->SetSizer(mainSizer);
  return panel;
}

wxPanel* VDRPrefsDialog::CreateReplayTab(wxWindow* parent) {
  wxPanel* panel = new wxPanel(parent);
  wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

  // Add network panels for each protocol
  // NMEA 0183 replay mode selection
  wxStaticBox* nmea0183Box =
      new wxStaticBox(panel, wxID_ANY, _("Replay Method"));
  wxStaticBoxSizer* nmea0183Sizer =
      new wxStaticBoxSizer(nmea0183Box, wxVERTICAL);

  m_nmea0183InternalRadio = new wxRadioButton(
      panel, ID_NMEA0183_INTERNAL_RADIO, _("NMEA 0183 using internal API"),
      wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
  m_nmea0183NetworkRadio = new wxRadioButton(
      panel, ID_NMEA0183_NETWORK_RADIO, _("NMEA 0183 using network connection (UDP/TCP)"));
  m_nmea0183LoopbackRadio =
      new wxRadioButton(panel, ID_NMEA0183_LOOPBACK_RADIO,
                        _("All messages using loopback driver (experimental)"));

  m_nmea0183InternalRadio->SetValue(m_protocols.nmea0183ReplayMode ==
                                    NMEA0183ReplayMode::INTERNAL_API);
  m_nmea0183NetworkRadio->SetValue(m_protocols.nmea0183ReplayMode ==
                                   NMEA0183ReplayMode::NETWORK);
  m_nmea0183LoopbackRadio->SetValue(m_protocols.nmea0183ReplayMode ==
                                    NMEA0183ReplayMode::LOOPBACK);

  nmea0183Sizer->Add(m_nmea0183InternalRadio, 0, wxALL, 5);
  nmea0183Sizer->Add(m_nmea0183NetworkRadio, 0, wxALL, 5);
  nmea0183Sizer->Add(m_nmea0183LoopbackRadio, 0, wxALL, 5);
  mainSizer->Add(nmea0183Sizer, 0, wxEXPAND | wxALL, 5);

  // Network settings

  // Add network panels for each protocol
  m_nmea0183NetPanel = new ConnectionSettingsPanel(panel, _("NMEA 0183"),
                                                   m_protocols.nmea0183Net);
  mainSizer->Add(m_nmea0183NetPanel, 0, wxEXPAND | wxALL, 5);
  // Enable/disable NMEA 0183 network panel based on replay mode
  m_nmea0183NetPanel->Enable(m_protocols.nmea0183ReplayMode ==
                             NMEA0183ReplayMode::NETWORK);
  m_nmea2000NetPanel =
      new ConnectionSettingsPanel(panel, _("NMEA 2000"), m_protocols.n2kNet);
  m_nmea2000NetPanel->Enable(m_protocols.nmea0183ReplayMode !=
                             NMEA0183ReplayMode::LOOPBACK);
  mainSizer->Add(m_nmea2000NetPanel, 0, wxEXPAND | wxALL, 5);

#if 0  // Signal K support disabled for now
  m_signalKNetPanel = new ConnectionSettingsPanel(panel, _("Signal K"),
                                              m_protocols.signalKNet);
  mainSizer->Add(m_signalKNetPanel, 0, wxEXPAND | wxALL, 5);
#endif

  panel->SetSizer(mainSizer);

  return panel;
}

void VDRPrefsDialog::OnOK(wxCommandEvent& event) {
  m_format =
      m_csvRadio->GetValue() ? VDRDataFormat::CSV : VDRDataFormat::RawNMEA;
  m_log_rotate = m_logRotateCheck->GetValue();
  m_log_rotate_interval = m_logRotateIntervalCtrl->GetValue();
  m_auto_start_recording = m_autoStartRecordingCheck->GetValue();
  m_use_speed_threshold = m_useSpeedThresholdCheck->GetValue();
  m_speed_threshold = m_speedThresholdCtrl->GetValue();
  m_stop_delay = m_stopDelayCtrl->GetValue();

  // Protocol settings
  m_protocols.nmea0183 = m_nmea0183Check->GetValue();
  m_protocols.nmea2000 = m_nmea2000Check->GetValue();
#if 0
  m_protocols.signalK = m_signalKCheck->GetValue();
#endif

  // Network settings
  m_protocols.nmea0183Net = m_nmea0183NetPanel->GetSettings();
  m_protocols.n2kNet = m_nmea2000NetPanel->GetSettings();
#if 0
  m_protocols.signalKNet = m_signalKNetPanel->GetSettings();
#endif
  if (m_nmea0183InternalRadio->GetValue())
    m_protocols.nmea0183ReplayMode = NMEA0183ReplayMode::INTERNAL_API;
  else if (m_nmea0183LoopbackRadio->GetValue())
    m_protocols.nmea0183ReplayMode = NMEA0183ReplayMode::LOOPBACK;
  else
    m_protocols.nmea0183ReplayMode = NMEA0183ReplayMode::NETWORK;

  event.Skip();
}

void VDRPrefsDialog::OnDirSelect(wxCommandEvent& event) {
  wxString dir_spec;
  int response = PlatformDirSelectorDialog(
      this, &dir_spec, _("Choose a directory"), m_recording_dir);

  if (response == wxID_OK) {
    m_recording_dir = dir_spec;
    m_dirCtrl->SetValue(m_recording_dir);
  }
}

void VDRPrefsDialog::OnLogRotateCheck(wxCommandEvent& event) {
  UpdateControlStates();
}

void VDRPrefsDialog::OnAutoRecordCheck(wxCommandEvent& event) {
  UpdateControlStates();
}

void VDRPrefsDialog::OnUseSpeedThresholdCheck(wxCommandEvent& event) {
  UpdateControlStates();
}

void VDRPrefsDialog::OnNMEA0183ReplayModeChanged(wxCommandEvent& event) {
  m_nmea0183NetPanel->Enable(event.GetId() == ID_NMEA0183_NETWORK_RADIO);
  m_nmea2000NetPanel->Enable(event.GetId() != ID_NMEA0183_LOOPBACK_RADIO);
}
