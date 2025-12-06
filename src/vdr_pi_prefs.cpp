/**************************************************************************
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
 *   along with this program; if not, see <https://www.gnu.org/licenses/>. *
 **************************************************************************/
#include <cstdint>

#include <wx/log.h>
#include <wx/notebook.h>
#include <wx/sizer.h>
#include <wx/statbox.h>

#include "vdr_pi_prefs.h"

#include "ocpn_plugin.h"
#include "vdr_pi_prefs_net.h"

static const int kInternalRadioId = wxWindow::NewControlId();
static const int kLoopbackRadioId = wxWindow::NewControlId();
static const int kNetworkRadioId = wxWindow::NewControlId();

VdrPrefsDialog::VdrPrefsDialog(wxWindow* parent, wxWindowID id,
                               VdrDataFormat format,
                               const wxString& recordingDir, bool logRotate,
                               int logRotateInterval, bool autoStartRecording,
                               bool useSpeedThreshold, double speedThreshold,
                               int stopDelay,
                               const VdrProtocolSettings& protocols)
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

void VdrPrefsDialog::OnProtocolCheck(wxCommandEvent& event) {
  UpdateControlStates();
}

void VdrPrefsDialog::UpdateControlStates() {
  // File rotation controls
  m_log_rotate_interval_ctrl->Enable(m_log_rotate_check->GetValue());

  // Auto-recording controls
  bool auto_record_enabled = m_autoStartRecordingCheck->GetValue();
  m_use_speed_threshold_check->Enable(auto_record_enabled);

  // Speed threshold controls - only enabled if both auto-record and use-speed
  // are checked
  bool speed_enabled =
      auto_record_enabled && m_use_speed_threshold_check->GetValue();
  m_speed_threshold_ctrl->Enable(speed_enabled);
  m_stop_delay_ctrl->Enable(speed_enabled);
}

void VdrPrefsDialog::CreateControls() {
  auto* main_sizer = new wxBoxSizer(wxVERTICAL);
  SetSizer(main_sizer);

  // Create notebook for tabs
  wxNotebook* notebook;
  notebook = new wxNotebook(this, wxID_ANY);
  main_sizer->Add(notebook, 1, wxEXPAND | wxALL, 5);

  // Add tabs
  wxPanel* recording_tab = CreateRecordingTab(notebook);
  wxPanel* replay_tab = CreateReplayTab(notebook);

  notebook->AddPage(recording_tab, _("Recording"));
  notebook->AddPage(replay_tab, _("Replay"));

  // Standard dialog buttons
  auto* button_sizer = new wxStdDialogButtonSizer();
  auto ok_button = new wxButton(this, wxID_OK);
  ok_button->Bind(wxEVT_COMMAND_BUTTON_CLICKED,
                  [&](wxCommandEvent& ev) { OnOK(ev); });
  button_sizer->AddButton(ok_button);
  button_sizer->AddButton(new wxButton(this, wxID_CANCEL));
  button_sizer->Realize();
  main_sizer->Add(button_sizer, 0, wxEXPAND | wxALL, 5);

  main_sizer->SetSizeHints(this);

  // Set initial control states
  UpdateControlStates();
}

wxPanel* VdrPrefsDialog::CreateRecordingTab(wxWindow* parent) {
  auto* panel = new wxPanel(parent);
  auto* main_sizer = new wxBoxSizer(wxVERTICAL);

  // Protocol selection section
  auto* protocol_box =
      new wxStaticBox(panel, wxID_ANY, _("Recording Protocols"));
  auto* protocol_sizer = new wxStaticBoxSizer(protocol_box, wxVERTICAL);

  m_nmea0183_check = new wxCheckBox(panel, wxID_ANY, _("NMEA 0183"));
  m_nmea0183_check->SetValue(m_protocols.nmea0183);
  m_nmea0183_check->Bind(wxEVT_CHECKBOX,
                         [&](wxCommandEvent ev) { OnProtocolCheck(ev); });

  protocol_sizer->Add(m_nmea0183_check, 0, wxALL, 5);

  m_nmea2000_check = new wxCheckBox(panel, wxID_ANY, _("NMEA 2000"));
  m_nmea2000_check->SetValue(m_protocols.nmea2000);
  m_nmea2000_check->Bind(wxEVT_CHECKBOX,
                         [&](wxCommandEvent ev) { OnProtocolCheck(ev); });
  protocol_sizer->Add(m_nmea2000_check, 0, wxALL, 5);

#if 0
  m_signalKCheck = new wxCheckBox(panel, kSignalkCheckId, _("Signal K"));
  m_signalKCheck->SetValue(m_protocols.signalK);
  protocol_sizer->Add(m_signalKCheck, 0, wxALL, 5);
#endif

  main_sizer->Add(protocol_sizer, 0, wxEXPAND | wxALL, 5);

  // Add format choice
  auto* format_box = new wxStaticBox(panel, wxID_ANY, _("Recording Format"));
  auto* format_sizer = new wxStaticBoxSizer(format_box, wxVERTICAL);

  m_nmea_radio =
      new wxRadioButton(panel, wxID_ANY, _("Raw NMEA"), wxDefaultPosition,
                        wxDefaultSize, wxRB_GROUP);
  m_csv_radio = new wxRadioButton(panel, wxID_ANY, _("CSV with timestamps"));

  format_sizer->Add(m_nmea_radio, 0, wxALL, 5);
  format_sizer->Add(m_csv_radio, 0, wxALL, 5);

  main_sizer->Add(format_sizer, 0, wxEXPAND | wxALL, 5);

  // Add recording directory controls
  auto* dir_box = new wxStaticBox(panel, wxID_ANY, _("Recording Directory"));
  auto* dir_sizer = new wxStaticBoxSizer(dir_box, wxHORIZONTAL);

  m_dir_ctrl = new wxTextCtrl(panel, wxID_ANY, m_recording_dir,
                              wxDefaultPosition, wxDefaultSize, wxTE_READONLY);
  m_dir_button = new wxButton(panel, wxID_ANY, _("Browse..."));
  m_dir_button->Bind(wxEVT_COMMAND_BUTTON_CLICKED,
                     [&](wxCommandEvent& ev) { OnDirSelect(ev); });

  dir_sizer->Add(m_dir_ctrl, 1, wxALL | wxEXPAND, 5);
  dir_sizer->Add(m_dir_button, 0, wxALL | wxEXPAND, 5);

  main_sizer->Add(dir_sizer, 0, wxEXPAND | wxALL, 5);

  // Select current format
  switch (m_format) {
    case VdrDataFormat::kCsv:
      m_csv_radio->SetValue(true);
      break;
    case VdrDataFormat::kRawNmea:
    default:
      m_nmea_radio->SetValue(true);
      break;
  }

  // File management section.
  auto* log_box = new wxStaticBox(panel, wxID_ANY, _("VDR File Management"));
  auto* log_sizer = new wxStaticBoxSizer(log_box, wxVERTICAL);

  m_log_rotate_check =
      new wxCheckBox(panel, wxID_ANY, _("Create new VDR file every:"));
  m_log_rotate_check->SetValue(m_log_rotate);
  m_log_rotate_check->Bind(wxEVT_CHECKBOX,
                           [&](wxCommandEvent ev) { OnLogRotateCheck(ev); });

  auto* interval_sizer = new wxBoxSizer(wxHORIZONTAL);
  m_log_rotate_interval_ctrl =
      new wxSpinCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
                     wxSP_ARROW_KEYS, 1, 168, m_log_rotate_interval);
  interval_sizer->Add(m_log_rotate_interval_ctrl, 0,
                      wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
  interval_sizer->Add(new wxStaticText(panel, wxID_ANY, _("hours")), 0,
                      wxALIGN_CENTER_VERTICAL);

  log_sizer->Add(m_log_rotate_check, 0, wxALL, 5);
  log_sizer->Add(interval_sizer, 0, wxLEFT | wxRIGHT | wxBOTTOM, 5);

  main_sizer->Add(log_sizer, 0, wxEXPAND | wxALL, 5);

  // Auto-recording section
  auto* auto_box = new wxStaticBox(panel, wxID_ANY, _("Automatic Recording"));
  auto* auto_sizer = new wxStaticBoxSizer(auto_box, wxVERTICAL);

  // Auto-start option
  m_autoStartRecordingCheck =
      new wxCheckBox(panel, wxID_ANY, _("Automatically start recording"));
  m_autoStartRecordingCheck->SetValue(m_auto_start_recording);
  m_autoStartRecordingCheck->Bind(
      wxEVT_CHECKBOX, [&](wxCommandEvent ev) { OnAutoRecordCheck(ev); });

  auto_sizer->Add(m_autoStartRecordingCheck, 0, wxALL, 5);

  // Speed threshold option
  auto* speed_sizer = new wxBoxSizer(wxHORIZONTAL);
  m_use_speed_threshold_check =
      new wxCheckBox(panel, wxID_ANY, _("When speed over ground exceeds"));
  m_use_speed_threshold_check->SetValue(m_use_speed_threshold);
  m_use_speed_threshold_check->Bind(
      wxEVT_CHECKBOX, [&](wxCommandEvent ev) { OnUseSpeedThresholdCheck(ev); });

  speed_sizer->Add(m_use_speed_threshold_check, 0, wxALIGN_CENTER_VERTICAL);

  m_speed_threshold_ctrl = new wxSpinCtrlDouble(
      panel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS,
      0.0, 20.0, m_speed_threshold, 0.1);
  speed_sizer->Add(m_speed_threshold_ctrl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT,
                   5);
  speed_sizer->Add(new wxStaticText(panel, wxID_ANY, _("knots")), 0,
                   wxALIGN_CENTER_VERTICAL);
  auto_sizer->Add(speed_sizer, 0, wxLEFT | wxRIGHT | wxBOTTOM, 5);

  // Pause delay control
  auto* delay_sizer = new wxBoxSizer(wxHORIZONTAL);
  delay_sizer->Add(
      new wxStaticText(panel, wxID_ANY, _("Pause recording after")), 0,
      wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
  m_stop_delay_ctrl =
      new wxSpinCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
                     wxSP_ARROW_KEYS, 1, 60, m_stop_delay);
  delay_sizer->Add(m_stop_delay_ctrl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
  delay_sizer->Add(
      new wxStaticText(panel, wxID_ANY, _("minutes below speed threshold")), 0,
      wxALIGN_CENTER_VERTICAL);
  auto_sizer->Add(delay_sizer, 0, wxLEFT | wxRIGHT | wxBOTTOM, 5);
  main_sizer->Add(auto_sizer, 0, wxEXPAND | wxALL, 5);

  panel->SetSizer(main_sizer);
  return panel;
}

wxPanel* VdrPrefsDialog::CreateReplayTab(wxWindow* parent) {
  auto* panel = new wxPanel(parent);
  auto* main_sizer = new wxBoxSizer(wxVERTICAL);

  // Add network panels for each protocol
  // NMEA 0183 replay mode selection
  auto* nmea0183_box = new wxStaticBox(panel, wxID_ANY, _("Replay Method"));
  auto* nmea0183_sizer = new wxStaticBoxSizer(nmea0183_box, wxVERTICAL);

  m_nmea0183_internal_radio = new wxRadioButton(
      panel, kInternalRadioId, _("NMEA 0183 using internal API"),
      wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
  m_nmea0183_network_radio =
      new wxRadioButton(panel, kNetworkRadioId,
                        _("NMEA 0183 using network connection (UDP/TCP)"));
  m_nmea0183_loopback_radio =
      new wxRadioButton(panel, kLoopbackRadioId,
                        _("All messages using loopback driver (experimental)"));

  m_nmea0183_internal_radio->SetValue(m_protocols.replay_mode ==
                                      ReplayMode::kInternalApi);
  m_nmea0183_network_radio->SetValue(m_protocols.replay_mode ==
                                     ReplayMode::kNetwork);
  m_nmea0183_loopback_radio->SetValue(m_protocols.replay_mode ==
                                      ReplayMode::kLoopback);
  m_nmea0183_internal_radio->Bind(wxEVT_RADIOBUTTON, [&](wxCommandEvent& ev) {
    OnNMEA0183ReplayModeChanged(ev);
  });
  m_nmea0183_network_radio->Bind(wxEVT_RADIOBUTTON, [&](wxCommandEvent& ev) {
    OnNMEA0183ReplayModeChanged(ev);
  });
  m_nmea0183_loopback_radio->Bind(wxEVT_RADIOBUTTON, [&](wxCommandEvent& ev) {
    OnNMEA0183ReplayModeChanged(ev);
  });

  nmea0183_sizer->Add(m_nmea0183_internal_radio, 0, wxALL, 5);
  nmea0183_sizer->Add(m_nmea0183_network_radio, 0, wxALL, 5);
  nmea0183_sizer->Add(m_nmea0183_loopback_radio, 0, wxALL, 5);
  main_sizer->Add(nmea0183_sizer, 0, wxEXPAND | wxALL, 5);

  // Network settings

  // Add network panels for each protocol
  m_nmea0183_net_panel = new ConnectionSettingsPanel(panel, _("NMEA 0183"),
                                                     m_protocols.nmea0183Net);
  main_sizer->Add(m_nmea0183_net_panel, 0, wxEXPAND | wxALL, 5);
  // Enable/disable NMEA 0183 network panel based on replay mode
  m_nmea0183_net_panel->Enable(m_protocols.replay_mode == ReplayMode::kNetwork);
  m_nmea2000_net_panel =
      new ConnectionSettingsPanel(panel, _("NMEA 2000"), m_protocols.n2kNet);
  m_nmea2000_net_panel->Enable(m_protocols.replay_mode !=
                               ReplayMode::kLoopback);
  main_sizer->Add(m_nmea2000_net_panel, 0, wxEXPAND | wxALL, 5);

#if 0  // Signal K support disabled for now
  m_signalKNetPanel = new ConnectionSettingsPanel(panel, _("Signal K"),
                                              m_protocols.signalkNet);
  main_sizer->Add(m_signalKNetPanel, 0, wxEXPAND | wxALL, 5);
#endif

  panel->SetSizer(main_sizer);

  return panel;
}

void VdrPrefsDialog::OnOK(wxCommandEvent& event) {
  m_format =
      m_csv_radio->GetValue() ? VdrDataFormat::kCsv : VdrDataFormat::kRawNmea;
  m_log_rotate = m_log_rotate_check->GetValue();
  m_log_rotate_interval = m_log_rotate_interval_ctrl->GetValue();
  m_auto_start_recording = m_autoStartRecordingCheck->GetValue();
  m_use_speed_threshold = m_use_speed_threshold_check->GetValue();
  m_speed_threshold = m_speed_threshold_ctrl->GetValue();
  m_stop_delay = m_stop_delay_ctrl->GetValue();

  // Protocol settings
  m_protocols.nmea0183 = m_nmea0183_check->GetValue();
  m_protocols.nmea2000 = m_nmea2000_check->GetValue();
#if 0
  m_protocols.signalK = m_signalKCheck->GetValue();
#endif

  // Network settings
  m_protocols.nmea0183Net = m_nmea0183_net_panel->GetSettings();
  m_protocols.n2kNet = m_nmea2000_net_panel->GetSettings();
#if 0
  m_protocols.signalkNet = m_signalKNetPanel->GetSettings();
#endif
  if (m_nmea0183_internal_radio->GetValue())
    m_protocols.replay_mode = ReplayMode::kInternalApi;
  else if (m_nmea0183_loopback_radio->GetValue())
    m_protocols.replay_mode = ReplayMode::kLoopback;
  else
    m_protocols.replay_mode = ReplayMode::kNetwork;

  event.Skip();
}

void VdrPrefsDialog::OnDirSelect(wxCommandEvent& event) {
  wxString dir_spec;
  int response = PlatformDirSelectorDialog(
      this, &dir_spec, _("Choose a directory"), m_recording_dir);

  if (response == wxID_OK) {
    m_recording_dir = dir_spec;
    m_dir_ctrl->SetValue(m_recording_dir);
  }
}

void VdrPrefsDialog::OnLogRotateCheck(wxCommandEvent& event) {
  UpdateControlStates();
}

void VdrPrefsDialog::OnAutoRecordCheck(wxCommandEvent& event) {
  UpdateControlStates();
}

void VdrPrefsDialog::OnUseSpeedThresholdCheck(wxCommandEvent& event) {
  UpdateControlStates();
}

void VdrPrefsDialog::OnNMEA0183ReplayModeChanged(wxCommandEvent& event) {
  m_nmea0183_net_panel->Enable(event.GetId() == kNetworkRadioId);
  m_nmea2000_net_panel->Enable(event.GetId() != kLoopbackRadioId);
}
