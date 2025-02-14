/***************************************************************************
 *   Copyright (C) 2024 by OpenCPN development team                        *
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

#include "vdr_pi_control.h"
#include "vdr_pi.h"

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
#ifdef __ANDROID__
EVT_LEFT_DOWN(VDRControl::OnMouseEvent)
EVT_LEFT_UP(VDRControl::OnMouseEvent)
EVT_MOTION(VDRControl::OnMouseEvent)
#endif

END_EVENT_TABLE()

#ifdef __ANDROID__

m_binResize = false;
g_Window = this;

#endif

#ifdef __ANDROID__
wxPoint g_startPos;
wxPoint g_startMouse;
wxPoint g_mouse_pos_screen;

void VDRControl::OnPopupClick(wxCommandEvent& evt) {
  switch (evt.GetId()) {
    case ID_DASH_RESIZE:
      m_binResize = true;
      break;
      // case ID_SOMETHING_ELSE:
      //   break;
  }
}

void VDRControl::OnDLeftClick(wxMouseEvent& event) {
  wxMenu mnu;
  mnu.Append(ID_DASH_RESIZE, "Resize...");
  // mnu.Append(ID_SOMETHING_ELSE, "Do something else");
  mnu.Connect(wxEVT_COMMAND_MENU_SELECTED,
              wxCommandEventHandler(VDRControl::OnPopupClick), NULL, this);
  PopupMenu(&mnu);
}

void VDRControl::OnMouseEvent(wxMouseEvent& event) {
  if (m_binResize) {
    wxSize currentSize = g_Window->GetSize();
    double aRatio = (double)currentSize.y / (double)currentSize.x;

    wxSize par_size = GetOCPNCanvasWindow()->GetClientSize();
    wxPoint par_pos = GetOCPNCanvasWindow()->GetPosition();

    if (event.LeftDown()) {
      m_resizeStartPoint = event.GetPosition();
      m_resizeStartSize = currentSize;
      m_binResize2 = true;
    }

    if (m_binResize2) {
      if (event.Dragging()) {
        wxPoint p = event.GetPosition();

        wxSize dragSize = m_resizeStartSize;

        dragSize.y += p.y - m_resizeStartPoint.y;
        dragSize.x += p.x - m_resizeStartPoint.x;
        ;

        if ((par_pos.y + dragSize.y) > par_size.y)
          dragSize.y = par_size.y - par_pos.y;

        if ((par_pos.x + dragSize.x) > par_size.x)
          dragSize.x = par_size.x - par_pos.x;

        /// vertical
        // dragSize.x = dragSize.y / aRatio;

        // not too small
        dragSize.x = wxMax(dragSize.x, 150);
        dragSize.y = wxMax(dragSize.y, 150);

        g_Window->SetSize(dragSize);
      }

      if (event.LeftUp()) {
        wxPoint p = event.GetPosition();

        wxSize dragSize = m_resizeStartSize;

        dragSize.y += p.y - m_resizeStartPoint.y;
        dragSize.x += p.x - m_resizeStartPoint.x;
        ;

        if ((par_pos.y + dragSize.y) > par_size.y)
          dragSize.y = par_size.y - par_pos.y;

        if ((par_pos.x + dragSize.x) > par_size.x)
          dragSize.x = par_size.x - par_pos.x;

        // not too small
        dragSize.x = wxMax(dragSize.x, 150);
        dragSize.y = wxMax(dragSize.y, 150);

        g_Window->SetSize(dragSize);

        m_binResize = false;
        m_binResize2 = false;
      }
    }
  } else {
    if (event.Dragging()) {
      m_resizeStartPoint = event.GetPosition();
      int x = wxMax(0, m_resizeStartPoint.x);
      int y = wxMax(0, m_resizeStartPoint.y);
      int xmax = ::wxGetDisplaySize().x - GetSize().x;
      x = wxMin(x, xmax);
      int ymax =
          ::wxGetDisplaySize().y - (GetSize().y);  // Some fluff at the bottom
      y = wxMin(y, ymax);

      g_Window->Move(x, y);
    }
  }
}

void VDRControl::OnContextMenuSelect(wxCommandEvent& event) {
  //
  switch (event.GetId()) {
    case ID_DASH_RESIZE: {
      /*
                  for( unsigned int i=0; i<m_ArrayOfInstrument.size(); i++ ) {
                      DashboardInstrument* inst =
         m_ArrayOfInstrument.Item(i)->m_pInstrument; inst->Hide();
                  }
      */
      m_binResize = true;
      wxMessageBox("here");

      return;
    }
  }  
}

void VDRControl::OnContextMenu(wxContextMenuEvent& event) {
  wxMenu* contextMenu = new wxMenu();
  wxFont* pf = OCPNGetFont(_T("Menu"), 0);

  // add stuff
  wxMenuItem* item1 =
      new wxMenuItem(contextMenu, ID_DASH_PREFS, _("Preferences..."));
  item1->SetFont(*pf);
  contextMenu->Append(item1);

  wxMenuItem* item2 =
      new wxMenuItem(contextMenu, ID_DASH_RESIZE, _("Resize..."));
  item2->SetFont(*pf);
  contextMenu->Append(item2);

  PopupMenu(contextMenu);
  delete contextMenu;
}
#endif  // End of Android functions for move/resize

bool VDRControl::LoadFile(wxString currentFile) {
  bool status = true;
  wxString error;
  UpdatePlaybackStatus(_("Stopped"));
  UpdateNetworkStatus(wxEmptyString);
  if (m_pvdr->LoadFile(currentFile, &error)) {
    bool hasValidTimestamps;
    wxString error;
    bool success = m_pvdr->ScanFileTimestamps(hasValidTimestamps, error);
    UpdateFileLabel(currentFile);
    if (!success) {
      UpdateFileStatus(error);
      status = false;
    } else {
      UpdateFileStatus(_("File loaded successfully"));
    }
    m_progressSlider->SetValue(0);
    UpdateControls();
  } else {
    // If loading fails, clear the saved filename
    m_pvdr->ClearInputFile();
    UpdateFileLabel(wxEmptyString);
    UpdateFileStatus(error);
    UpdateControls();
    status = false;
  }
  return status;
}

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
    LoadFile(currentFile);
  } else {
    UpdateFileStatus(_("No file loaded"));
  }
  UpdatePlaybackStatus(_("Stopped"));
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

  if (IsTouchInterface_PlugIn()) {
    // Ensure minimum button size of 7 mm for touch usability
    double pixel_per_mm = wxGetDisplaySize().x / PlugInGetDisplaySizeMM();
    int min_touch_size = 7 * pixel_per_mm;
    buttonSize = std::max(buttonSize, min_touch_size);
  } else
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

  // Add status panel
  wxStaticBox* statusBox = new wxStaticBox(this, wxID_ANY, _("Status"));
  wxStaticBoxSizer* statusSizer = new wxStaticBoxSizer(statusBox, wxVERTICAL);

  // File status
  wxBoxSizer* fileStatusSizer = new wxBoxSizer(wxHORIZONTAL);
  fileStatusSizer->Add(new wxStaticText(this, wxID_ANY, _("File: ")), 0,
                       wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
  m_fileStatusLabel = new wxStaticText(this, wxID_ANY, wxEmptyString);
  fileStatusSizer->Add(m_fileStatusLabel, 1, wxALIGN_CENTER_VERTICAL);
  statusSizer->Add(fileStatusSizer, 0, wxEXPAND | wxALL, 5);

  // Network status
  wxBoxSizer* networkStatusSizer = new wxBoxSizer(wxHORIZONTAL);
  networkStatusSizer->Add(new wxStaticText(this, wxID_ANY, _("Network: ")), 0,
                          wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
  m_networkStatusLabel = new wxStaticText(this, wxID_ANY, wxEmptyString);
  networkStatusSizer->Add(m_networkStatusLabel, 1, wxALIGN_CENTER_VERTICAL);
  statusSizer->Add(networkStatusSizer, 0, wxEXPAND | wxALL, 5);

  // Playback status
  wxBoxSizer* playbackStatusSizer = new wxBoxSizer(wxHORIZONTAL);
  playbackStatusSizer->Add(new wxStaticText(this, wxID_ANY, _("Playback: ")), 0,
                           wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
  m_playbackStatusLabel = new wxStaticText(this, wxID_ANY, wxEmptyString);
  playbackStatusSizer->Add(m_playbackStatusLabel, 1, wxALIGN_CENTER_VERTICAL);
  statusSizer->Add(playbackStatusSizer, 0, wxEXPAND | wxALL, 5);

  mainSizer->Add(statusSizer, 0, wxEXPAND | wxALL, 5);

  SetSizer(mainSizer);
  wxClientDC dc(m_timeLabel);
  wxSize textExtent = dc.GetTextExtent(_("Date and Time: YYYY-MM-DD HH:MM:SS"));
  int minWidth = std::min(300, textExtent.GetWidth() + 20);  // 20px padding
  mainSizer->SetMinSize(wxSize(minWidth, -1));
  Layout();
  mainSizer->Fit(this);

  // Initial state
  UpdateControls();
}

void VDRControl::SetSpeedMultiplier(int value) {
  value = std::max(value, m_speedSlider->GetMin());
  value = std::min(value, m_speedSlider->GetMax());
  m_speedSlider->SetValue(value);
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
    StopPlayback();
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
    LoadFile(file);
  }
}

void VDRControl::OnProgressSliderUpdated(wxScrollEvent& event) {
  if (!m_isDragging) {
    m_isDragging = true;
    m_wasPlayingBeforeDrag = m_pvdr->IsPlaying();
    if (m_wasPlayingBeforeDrag) {
      PausePlayback();
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
    StartPlayback();
  }
  m_isDragging = false;
  UpdateControls();
  event.Skip();
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
    m_progressSlider->SetValue(1000);
    UpdateFileStatus(_("End of file"));
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

void VDRControl::StartPlayback() {
  m_pvdr->StartPlayback();
  UpdatePlaybackStatus(_("Playing"));
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

void VDRControl::UpdateFileStatus(const wxString& status) {
  if (m_fileStatusLabel) {
    m_fileStatusLabel->SetLabel(status);
  }
}

void VDRControl::UpdateNetworkStatus(const wxString& status) {
  if (m_networkStatusLabel) {
    m_networkStatusLabel->SetLabel(status);
  }
}

void VDRControl::UpdatePlaybackStatus(const wxString& status) {
  if (m_playbackStatusLabel) {
    m_playbackStatusLabel->SetLabel(status);
  }
}