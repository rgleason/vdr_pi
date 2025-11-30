/***************************************************************************
 *   Copyright (C) 2011  Jean-Eudes Onfray                                 *
 *   Copyright (C) 2025  Sebastian Rosset                                  *
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

#ifndef VDR_PI_CONTROL_H_
#define VDR_PI_CONTROL_H_

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include "ocpn_plugin.h"
#include "record_play_mgr.h"

class VdrPi;

/**
 * UI control panel for VDR playback functionality.
 *
 * Provides controls for loading VDR files, starting/pausing playback,
 * adjusting playback speed, and monitoring playback progress.
 */
class VDRControl : public wxWindow, public VdrControlGui {
public:
  /**
   * Create a new VDR control panel.
   *
   * Initializes UI elements and loads any previously configured VDR file.
   * @param pparent Parent window for the control panel
   * @param id Window identifier
   * @param vdr Owner VDR plugin instance
   */
  VDRControl(wxWindow* parent, std::shared_ptr<RecordPlayMgr> record_play_mgr);

  void SetColorScheme(PI_ColorScheme cs) override;

  void SetProgress(double fraction) override;

  void UpdateControls() override;

  void UpdateFileLabel(const wxString& filename) override;

  void UpdateFileStatus(const wxString& status);

  void UpdateNetworkStatus(const wxString& status) override;

  double GetSpeedMultiplier() const override;

  void OnToolbarToolCallback(int id) override {
    if (m_pvdr) m_pvdr->OnToolbarToolCallback(id);
  }

  /** Update displayed timestamp in UI based on current playback position. */
  void UpdateTimeLabel();

  void EnableSpeedSlider(bool enable) override {
    m_speed_slider->Enable(enable);
  }

  void ShowPreferencesDialog(wxWindow* parent) {
    if (m_pvdr) m_pvdr->ShowPreferencesDialog(parent);
  }

  /** Update playback status label with given message. */
  void UpdatePlaybackStatus(const wxString& status);

  /** Set the speed multiplier settting. */
  void SetSpeedMultiplier(int value);

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

  bool LoadFile(wxString current_file);

  wxButton* m_load_btn;          //!< Button to load VDR file
  wxButton* m_settings_btn;      //!< Button to open settings dialog
  wxButton* m_play_pause_btn;    //!< Toggle button for play/pause
  wxString m_play_btn_tooltip;   //!< Tooltip text for play state
  wxString m_pause_btn_tooltip;  //!< Tooltip text for pause state
  wxString m_stop_btn_tooltip;   //!< Tooltip text for stop state

  wxSlider* m_speed_slider;     //!< Slider control for playback speed
  wxSlider* m_progress_slider;  //!< Slider control for playback position
  wxStaticText* m_file_label;   //!< Label showing current filename
  wxStaticText* m_time_label;   //!< Label showing current timestamp
  std::shared_ptr<RecordPlayMgr> m_pvdr;

  bool m_is_dragging;              //!< Flag indicating progress slider drag
  bool m_was_playing_before_drag;  //!< Playback state before drag started

  // Status labels
  wxStaticText* m_file_status_lbl;      //!< Label showing file status.
  wxStaticText* m_network_status_lbl;   //!< Label showing network status.
  wxStaticText* m_playback_status_lbl;  //!< Label showing playback status.

  int m_button_size;  //!< Size of SVG button icons.

  DECLARE_EVENT_TABLE()
};

#endif  // VDR_PI_CONTROL_H_
