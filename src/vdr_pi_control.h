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

#ifndef _VDR_PI_CONTROL_H_
#define _VDR_PI_CONTROL_H_

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include "ocpn_plugin.h"

#ifdef __ANDROID__
wxWindow* g_Window;
#endif

class vdr_pi;

/**
 * UI control panel for VDR playback functionality.
 *
 * Provides controls for loading VDR files, starting/pausing playback,
 * adjusting playback speed, and monitoring playback progress.
 */
class VDRControl : public wxWindow {
public:
  /**
   * Create a new VDR control panel.
   *
   * Initializes UI elements and loads any previously configured VDR file.
   * @param pparent Parent window for the control panel
   * @param id Window identifier
   * @param vdr Owner VDR plugin instance
   */
  VDRControl(wxWindow* pparent, wxWindowID id, vdr_pi* vdr);

  /**
   * Update UI elements for color scheme changes.
   * @param cs New color scheme to apply
   */
  void SetColorScheme(PI_ColorScheme cs);

  /**
   * Update progress indication for playback position.
   * @param fraction Current position as fraction between 0-1
   */
  void SetProgress(double fraction);

  /** Update state of UI controls based on playback status. */
  void UpdateControls();

  /**
   * Update displayed filename in UI.
   * @param filename Path of currently loaded file
   */
  void UpdateFileLabel(const wxString& filename);

  /** Update displayed timestamp in UI based on current playback position. */
  void UpdateTimeLabel();

  /** Get current playback speed multiplier setting. */
  int GetSpeedMultiplier() const { return m_speedSlider->GetValue(); }

  /** Set the speed multiplier settting. */
  void SetSpeedMultiplier(int value);

  /**
   * Update file status label with new message.
   */
  void UpdateFileStatus(const wxString& status);
  /**
   * Update network status label with new message.
   */
  void UpdateNetworkStatus(const wxString& status);
  /**
   * Update playback status label with new message.
   */
  void UpdatePlaybackStatus(const wxString& status);

  #ifdef __ANDROID__
  void OnContextMenu(wxContextMenuEvent& event);
  void OnContextMenuSelect(wxCommandEvent& event);

  void OnMouseEvent(wxMouseEvent& event);
  wxPoint m_resizeStartPoint;
  wxSize m_resizeStartSize;
  bool m_binResize;
  bool m_binResize2;

  void OnPopupClick(wxCommandEvent& evt);
  void OnDLeftClick(wxMouseEvent& event);

#endif

private:
  /** Create and layout UI controls. */
  void CreateControls();

  /**
   * Handle file load button clicks.
   *
   * Shows file selection dialog and loads selected VDR file.
   */
  void OnLoadButton(wxCommandEvent& event);

  /**
   * Handle play/pause button clicks.
   *
   * Toggles between playback and paused states.
   */
  void OnPlayPauseButton(wxCommandEvent& event);

  /**
   * Handle playback speed adjustment.
   *
   * Updates playback timing when speed multiplier changes.
   */
  void OnSpeedSliderUpdated(wxCommandEvent& event);

  /**
   * Handle progress slider dragging.
   *
   * Temporarily pauses playback while user drags position slider.
   */
  void OnProgressSliderUpdated(wxScrollEvent& even);

  /**
   * Handle progress slider release.
   *
   * Seeks to new position and resumes playback if previously playing.
   */
  void OnProgressSliderEndDrag(wxScrollEvent& event);

  /** Handle data format selection changes. */
  void OnDataFormatRadioButton(wxCommandEvent& event);

  /** Handle left-click on Settings button. */
  void OnSettingsButton(wxCommandEvent& event);

  /**
   * Start playback of loaded VDR file and update status.
   */
  void StartPlayback();

  /**
   * Pause playback of loaded VDR file and update status.
   */
  void PausePlayback();

  /**
   * Stop playback of loaded VDR file and update status.
   */
  void StopPlayback();

  bool LoadFile(wxString currentFile);

  wxButton* m_loadBtn;         //!< Button to load VDR file
  wxButton* m_settingsBtn;     //!< Button to open settings dialog
  wxButton* m_playPauseBtn;    //!< Toggle button for play/pause
  wxString m_playBtnTooltip;   //!< Tooltip text for play state
  wxString m_pauseBtnTooltip;  //!< Tooltip text for pause state
  wxString m_stopBtnTooltip;   //!< Tooltip text for stop state

  wxSlider* m_speedSlider;     //!< Slider control for playback speed
  wxSlider* m_progressSlider;  //!< Slider control for playback position
  wxStaticText* m_fileLabel;   //!< Label showing current filename
  wxStaticText* m_timeLabel;   //!< Label showing current timestamp
  vdr_pi* m_pvdr;              //!< Owner plugin instance

  bool m_isDragging;            //!< Flag indicating progress slider drag
  bool m_wasPlayingBeforeDrag;  //!< Playback state before drag started

  // Status labels
  wxStaticText* m_fileStatusLabel;      //!< Label showing file status.
  wxStaticText* m_networkStatusLabel;   //!< Label showing network status.
  wxStaticText* m_playbackStatusLabel;  //!< Label showing playback status.

  DECLARE_EVENT_TABLE()
};

#endif  // _VDR_PI_CONTROL_H_
