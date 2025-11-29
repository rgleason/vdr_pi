/***************************************************************************
 *   Copyright (C) 2011 Jean-Eudes Onfray                                  *
 *   Copyright (C) 2025 Sebastian Rosset                                   *
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

#include "vdr_pi_control.h"
#include "vdr_pi.h"
#include "icons.h"

const char* const kBadVdrFormat =
    _(R"(This file seems to not be recorded by Data Monitor
in VDR mode. You might want to change the Replay
preferences to better match it )");

const char* const kBadNonVdrFormat =
    _(R"(This file seems to be recorded by Data Monitor
in VDR mode. You might want to adjust the Replay
preferences to "Use loopback driver" to be able to
play it.)");

static const int kDataFormatRadioBtnId = wxWindow::NewControlId();
static const int kLoadId = wxWindow::NewControlId();
static const int kPlayPauseId = wxWindow::NewControlId();
static const int kProgressId = wxWindow::NewControlId();
static const int kSettingsId = wxWindow::NewControlId();
static const int kSpeedSliderId = wxWindow::NewControlId();

BEGIN_EVENT_TABLE(VDRControl, wxWindow)
EVT_BUTTON(kLoadId, VDRControl::OnLoadButton)
EVT_RADIOBUTTON(kDataFormatRadioBtnId, VDRControl::OnDataFormatRadioButton)
EVT_BUTTON(kPlayPauseId, VDRControl::OnPlayPauseButton)
EVT_BUTTON(kSettingsId, VDRControl::OnSettingsButton)
EVT_SLIDER(kSpeedSliderId, VDRControl::OnSpeedSliderUpdated)
EVT_COMMAND_SCROLL_THUMBTRACK(kProgressId, VDRControl::OnProgressSliderUpdated)
EVT_COMMAND_SCROLL_THUMBRELEASE(kProgressId,
                                VDRControl::OnProgressSliderEndDrag)
END_EVENT_TABLE()

bool VDRControl::LoadFile(wxString current_file) {
  bool status = true;
  wxString error;
  UpdatePlaybackStatus(_("Stopped"));
  UpdateNetworkStatus("");
  if (m_pvdr->LoadFile(current_file, &error)) {
    bool hasValidTimestamps;
    wxString error;
    bool success = m_pvdr->ScanFileTimestamps(hasValidTimestamps, error);
    UpdateFileLabel(current_file);
    if (!success) {
      UpdateFileStatus(error);
      status = false;
    } else {
      UpdateFileStatus(_("File loaded successfully"));
    }
    m_progress_slider->SetValue(0);
    UpdateControls();
  } else {
    // If loading fails, clear the saved filename
    m_pvdr->ClearInputFile();
    UpdateFileLabel("");
    UpdateFileStatus(error);
    UpdateControls();
    status = false;
  }
  return status;
}

VDRControl::VDRControl(wxWindow* parent, wxWindowID id, VdrPi* vdr)
    : wxWindow(parent, id, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE,
               "VDR Control"),
      m_pvdr(vdr),
      m_is_dragging(false),
      m_was_playing_before_drag(false) {
  wxColour cl;
  GetGlobalColor("DILG1", &cl);
  wxWindow::SetBackgroundColour(cl);

  CreateControls();

  // Check if there's already a file loaded from config
  wxString currentFile = m_pvdr->GetInputFile();
  if (!currentFile.IsEmpty()) {
    // Try to load the file
    LoadFile(currentFile);
  } else {
    UpdateFileStatus(_("No file loaded"));
  }
  UpdatePlaybackStatus(_("Stopped"));
}

void VDRControl::CreateControls() {
  // Main vertical sizer
  wxBoxSizer* main_sizer = new wxBoxSizer(wxVERTICAL);

  // Ensure minimum button size of 7 mm for touch usability
  double pixel_per_mm = wxGetDisplaySize().x / PlugInGetDisplaySizeMM();
  m_button_size = 7 * pixel_per_mm;
#ifdef __WXQT__
  // A simple way to get touch-compatible tool size
  wxRect tbRect = GetMasterToolbarRect();
  m_button_size = std::max(m_button_size, tbRect.width / 2);
#endif
  wxSize buttonDimension(m_button_size, m_button_size);
  int svg_size = m_button_size * OCPN_GetWinDIPScaleFactor();

  // File information section
  wxBoxSizer* file_sizer = new wxBoxSizer(wxHORIZONTAL);

  // Settings button
  m_settings_btn = new wxBitmapButton(
      this, kSettingsId,
      GetBitmapFromSVGFile(g_svg_settings, svg_size, svg_size),
      wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
  m_settings_btn->SetToolTip(_("Settings"));
  file_sizer->Add(m_settings_btn, 0, wxALL, 2);

  // Load button
  m_load_btn = new wxBitmapButton(
      this, kLoadId, GetBitmapFromSVGFile(g_svg_file_open, svg_size, svg_size),
      wxDefaultPosition, buttonDimension, wxBU_EXACTFIT);
  m_load_btn->SetToolTip(_("Load VDR File"));
  file_sizer->Add(m_load_btn, 0, wxALL, 2);

  m_file_label =
      new wxStaticText(this, wxID_ANY, _("No file loaded"), wxDefaultPosition,
                       wxDefaultSize, wxST_ELLIPSIZE_START);
  file_sizer->Add(m_file_label, 1, wxALL | wxEXPAND, 2);

  main_sizer->Add(file_sizer, 0, wxALL, 4);

  // Play controls and progress in one row
  auto* control_sizer = new wxBoxSizer(wxHORIZONTAL);

  // Play button setup
  m_play_btn_tooltip = _("Start Playback");
  m_pause_btn_tooltip = _("Pause Playback");
  m_stop_btn_tooltip = _("End of File");

  m_play_pause_btn = new wxBitmapButton(
      this, kPlayPauseId,
      GetBitmapFromSVGFile(g_svg_play_circle, svg_size, svg_size),
      wxDefaultPosition, buttonDimension, wxBU_EXACTFIT);
  m_play_pause_btn->SetToolTip(m_play_btn_tooltip);
  control_sizer->Add(m_play_pause_btn, 0, wxALL, 3);

  // Progress slider in the same row as play button
  m_progress_slider =
      new wxSlider(this, kProgressId, 0, 0, 1000, wxDefaultPosition,
                   wxDefaultSize, wxSL_HORIZONTAL | wxSL_BOTTOM);
  control_sizer->Add(m_progress_slider, 1, wxALIGN_CENTER_VERTICAL, 0);
  main_sizer->Add(control_sizer, 0, wxEXPAND | wxALL, 4);

  // Time label
  m_time_label = new wxStaticText(this, wxID_ANY, _("Date and Time: --"),
                                  wxDefaultPosition, wxSize(200, -1));
  main_sizer->Add(m_time_label, 0, wxEXPAND | wxALL, 4);

  // Speed control
  auto* speed_sizer = new wxBoxSizer(wxHORIZONTAL);
  speed_sizer->Add(new wxStaticText(this, wxID_ANY, _("Speed:")), 0,
                   wxALIGN_CENTER_VERTICAL | wxRIGHT, 3);
  m_speed_slider =
      new wxSlider(this, wxID_ANY, 1, 1, 1000, wxDefaultPosition, wxDefaultSize,
                   wxSL_HORIZONTAL | wxSL_VALUE_LABEL);
  speed_sizer->Add(m_speed_slider, 1, wxALL | wxEXPAND, 0);
  main_sizer->Add(speed_sizer, 0, wxEXPAND | wxALL, 4);

  // Add status panel
  auto* status_box = new wxStaticBox(this, wxID_ANY, _("Status"));
  auto* status_sizer = new wxStaticBoxSizer(status_box, wxVERTICAL);

  // File status
  wxBoxSizer* file_status_sizer = new wxBoxSizer(wxHORIZONTAL);
  file_status_sizer->Add(new wxStaticText(this, wxID_ANY, _("File: ")), 0,
                         wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
  m_file_status_lbl = new wxStaticText(this, wxID_ANY, "");
  file_status_sizer->Add(m_file_status_lbl, 1, wxALIGN_CENTER_VERTICAL);
  status_sizer->Add(file_status_sizer, 0, wxEXPAND | wxALL, 5);

  // Network status
  auto* network_status_sizer = new wxBoxSizer(wxHORIZONTAL);
  network_status_sizer->Add(new wxStaticText(this, wxID_ANY, _("Network: ")), 0,
                            wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
  m_network_status_lbl = new wxStaticText(this, wxID_ANY, "");
  network_status_sizer->Add(m_network_status_lbl, 1, wxALIGN_CENTER_VERTICAL);
  status_sizer->Add(network_status_sizer, 0, wxEXPAND | wxALL, 5);

  // Playback status
  auto* playback_status_sizer = new wxBoxSizer(wxHORIZONTAL);
  playback_status_sizer->Add(new wxStaticText(this, wxID_ANY, _("Playback: ")),
                             0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
  m_playback_status_lbl = new wxStaticText(this, wxID_ANY, "");
  playback_status_sizer->Add(m_playback_status_lbl, 1, wxALIGN_CENTER_VERTICAL);
  status_sizer->Add(playback_status_sizer, 0, wxEXPAND | wxALL, 5);

  main_sizer->Add(status_sizer, 0, wxEXPAND | wxALL, 5);

  SetSizer(main_sizer);
  wxClientDC dc(m_time_label);
  wxSize text_extent =
      dc.GetTextExtent(_("Date and Time: YYYY-MM-DD HH:MM:SS"));
  int min_width = std::min(300, text_extent.GetWidth() + 20);  // 20px padding
  main_sizer->SetMinSize(wxSize(min_width, -1));
  Layout();
  main_sizer->Fit(this);

  // Initial state
  UpdateControls();
}

void VDRControl::SetSpeedMultiplier(int value) {
  value = std::max(value, m_speed_slider->GetMin());
  value = std::min(value, m_speed_slider->GetMax());
  m_speed_slider->SetValue(value);
}

void VDRControl::UpdateTimeLabel() {
  if (m_pvdr->GetCurrentTimestamp().IsValid()) {
    wxString time_str =
        m_pvdr->GetCurrentTimestamp().ToUTC().Format("%Y-%m-%d %H:%M:%S UTC");
    m_time_label->SetLabel("Date and Time: " + time_str);
  } else {
    m_time_label->SetLabel(_("Date and Time: --"));
  }
}

void VDRControl::OnLoadButton(wxCommandEvent& event) {
  // Stop any current playback
  if (m_pvdr->IsPlaying()) {
    StopPlayback();
  }

  wxString file;
  wxString init_directory = "";
#ifdef __WXQT__
  init_directory = *GetpPrivateApplicationDataLocation();
#endif

  int response = PlatformFileSelectorDialog(GetOCPNCanvasWindow(), &file,
                                            _("Select Playback File"),
                                            init_directory, "", "*.*");
  if (response != wxID_OK) return;

  bool is_vdrfile = DataMonitorReplayMgr::IsVdrFormat(file.ToStdString());
  if (m_pvdr->IsUsingLoopback()) {
    if (!is_vdrfile)
      OCPNMessageBox_PlugIn(GetOCPNCanvasWindow(), kBadVdrFormat);
  } else {
    if (is_vdrfile)
      OCPNMessageBox_PlugIn(GetOCPNCanvasWindow(), kBadNonVdrFormat);
  }
  LoadFile(file);
}

void VDRControl::OnProgressSliderUpdated(wxScrollEvent& event) {
  if (!m_is_dragging) {
    m_is_dragging = true;
    m_was_playing_before_drag = m_pvdr->IsPlaying();
    if (m_was_playing_before_drag) {
      PausePlayback();
    }
  }
  if (m_pvdr->GetFirstTimestamp().IsValid() &&
      m_pvdr->GetLastTimestamp().IsValid()) {
    // Update time display while dragging but don't seek yet
    double fraction = m_progress_slider->GetValue() / 1000.0;
    wxTimeSpan total_span =
        m_pvdr->GetLastTimestamp() - m_pvdr->GetFirstTimestamp();
    wxTimeSpan currentSpan =
        wxTimeSpan::Seconds((total_span.GetSeconds().ToDouble() * fraction));
    m_pvdr->SetCurrentTimestamp(m_pvdr->GetFirstTimestamp() + currentSpan);
    UpdateTimeLabel();
  }
  event.Skip();
}

void VDRControl::OnProgressSliderEndDrag(wxScrollEvent& event) {
  double fraction = m_progress_slider->GetValue() / 1000.0;
  m_pvdr->SeekToFraction(fraction);
  // Reset the end-of-file state when user drags the slider, the button should
  // change to "play" state.
  m_pvdr->ResetEndOfFile();
  if (m_was_playing_before_drag) {
    StartPlayback();
  }
  m_is_dragging = false;
  UpdateControls();
  event.Skip();
}

void VDRControl::UpdateControls() {
  bool has_file = !m_pvdr->GetInputFile().IsEmpty();
  bool is_recording = m_pvdr->IsRecording();
  bool is_playing = m_pvdr->IsPlaying();
  bool is_at_end = m_pvdr->IsAtFileEnd();

  // Update the play/pause/stop button appearance
  if (is_at_end) {
    m_play_pause_btn->SetBitmapLabel(
        GetBitmapFromSVGFile(g_svg_stop_circle, m_button_size, m_button_size));
    m_play_pause_btn->SetToolTip(m_stop_btn_tooltip);
    m_progress_slider->SetValue(1000);
    UpdateFileStatus(_("End of file"));
  } else {
    m_play_pause_btn->SetBitmapLabel(GetBitmapFromSVGFile(
        is_playing ? g_svg_pause_circle : g_svg_play_circle, m_button_size,
        m_button_size));
    m_play_pause_btn->SetToolTip(is_playing ? m_pause_btn_tooltip
                                            : m_play_btn_tooltip);
    if (m_pvdr->IsError()) UpdateFileStatus(_("Error"));
  }

  // Enable/disable controls based on state
  m_load_btn->Enable(!is_recording && !is_playing);
  m_play_pause_btn->Enable(has_file && !is_recording);
  m_settings_btn->Enable(!is_playing && !is_recording);
  m_progress_slider->Enable(has_file && !is_recording);

  // Update toolbar state
  m_pvdr->SetToolbarToolStatus(m_pvdr->GetPlayToolbarItemId(), is_playing);

  // Update time display
  if (has_file && m_pvdr->GetCurrentTimestamp().IsValid()) {
    wxString time_str =
        m_pvdr->GetCurrentTimestamp().ToUTC().Format("%Y-%m-%d %H:%M:%S UTC");
    m_time_label->SetLabel(_("Date and Time: ") + time_str);
  } else {
    m_time_label->SetLabel(_("Date and Time: ") + "--");
  }

  if (!is_playing && is_at_end) UpdatePlaybackStatus(_("Stopped"));
  Layout();
}

void VDRControl::UpdateFileLabel(const wxString& filename) {
  if (filename.IsEmpty()) {
    m_file_label->SetLabel(_("No file loaded"));
  } else {
    wxFileName fn(filename);
    m_file_label->SetLabel(fn.GetFullName());
  }
  m_file_label->GetParent()->Layout();
}

void VDRControl::StartPlayback() {
  m_pvdr->StartPlayback();
  if (m_pvdr->IsPlaying()) UpdatePlaybackStatus(_("Playing"));
}

void VDRControl::PausePlayback() {
  m_pvdr->PausePlayback();
  UpdatePlaybackStatus(_("Paused"));
}

void VDRControl::StopPlayback() {
  m_pvdr->StopPlayback();
  UpdatePlaybackStatus(_("Stopped"));
}

void VDRControl::OnPlayPauseButton(wxCommandEvent& event) {
  if (!m_pvdr->IsPlaying()) {
    if (m_pvdr->GetInputFile().IsEmpty()) {
      UpdateFileStatus(_("No file selected"));
      return;
    }

    // If we're at the end, restart from beginning
    if (m_pvdr->IsAtFileEnd()) {
      StopPlayback();
    }
    StartPlayback();
  } else {
    PausePlayback();
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
  int slider_pos = wxRound(fraction * 1000);
  m_progress_slider->SetValue(slider_pos);

  if (m_pvdr->GetFirstTimestamp().IsValid() &&
      m_pvdr->GetLastTimestamp().IsValid()) {
    // Calculate and set current timestamp based on the fraction
    wxTimeSpan total_span =
        m_pvdr->GetLastTimestamp() - m_pvdr->GetFirstTimestamp();
    wxTimeSpan current_span =
        wxTimeSpan::Seconds((total_span.GetSeconds().ToDouble() * fraction));
    m_pvdr->SetCurrentTimestamp(m_pvdr->GetFirstTimestamp() + current_span);

    // Update time display
    UpdateTimeLabel();
  }
}

void VDRControl::SetColorScheme(PI_ColorScheme cs) {
  wxColour cl;
  GetGlobalColor("DILG1", &cl);
  SetBackgroundColour(cl);

  Refresh(false);
}

void VDRControl::UpdateFileStatus(const wxString& status) {
  if (m_file_status_lbl) {
    m_file_status_lbl->SetLabel(status);
  }
}

void VDRControl::UpdateNetworkStatus(const wxString& status) {
  if (m_network_status_lbl) {
    m_network_status_lbl->SetLabel(status);
  }
}

void VDRControl::UpdatePlaybackStatus(const wxString& status) {
  if (m_playback_status_lbl) {
    m_playback_status_lbl->SetLabel(status);
  }
}
