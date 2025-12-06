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

#ifndef VDR_PI_PREFS_H_
#define VDR_PI_PREFS_H_

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/dialog.h>
#include <wx/event.h>
#include <wx/panel.h>
#include <wx/radiobut.h>
#include <wx/spinctrl.h>
#include <wx/string.h>
#include <wx/textctrl.h>

#include "vdr_pi_prefs_net.h"
#include "commons.h"

/**
 * Preferences dialog for configuring VDR settings.
 *
 * Provides UI for setting recording format, directory, auto-recording behavior,
 * protocol selection, and file rotation options.
 */
class VdrPrefsDialog : public wxDialog {
public:
  /**
   * Create new dialog.
   *
   * Initializes dialog with current VDR configuration settings.
   * @param parent Parent window
   * @param id Dialog identifier
   * @param format Current data format setting
   * @param recordingDir Path to recording directory
   * @param logRotate Whether log rotation is enabled
   * @param logRotateInterval Hours between log rotations
   * @param autoStartRecording Enable automatic recording on startup
   * @param useSpeedThreshold Enable speed-based recording control
   * @param speedThreshold Speed threshold in knots
   * @param stopDelay Minutes to wait before stopping
   * @param protocols Active protocol settings
   */
  VdrPrefsDialog(wxWindow* parent, wxWindowID id, VdrDataFormat format,
                 const wxString& recordingDir, bool logRotate,
                 int logRotateInterval, bool autoStartRecording,
                 bool useSpeedThreshold, double speedThreshold, int stopDelay,
                 const VdrProtocolSettings& protocols);

  /** Get selected data format setting. */
  [[nodiscard]] VdrDataFormat GetDataFormat() const { return m_format; }

  /** Get configured recording directory path. */
  [[nodiscard]] wxString GetRecordingDir() const { return m_recording_dir; }

  /** Check if log rotation is enabled. */
  [[nodiscard]] bool GetLogRotate() const { return m_log_rotate; }

  /** Get log rotation interval in hours. */
  [[nodiscard]] int GetLogRotateInterval() const {
    return m_log_rotate_interval;
  }

  /** Check if auto-start recording is enabled. */
  [[nodiscard]] bool GetAutoStartRecording() const {
    return m_auto_start_recording;
  }

  /** Check if speed threshold is enabled. */
  [[nodiscard]] bool GetUseSpeedThreshold() const {
    return m_use_speed_threshold;
  }

  /** Get speed threshold in knots. */
  [[nodiscard]] double GetSpeedThreshold() const { return m_speed_threshold; }

  /** Get recording stop delay in minutes. */
  [[nodiscard]] int GetStopDelay() const { return m_stop_delay; }

  /** Get protocol recording settings. */
  [[nodiscard]] VdrProtocolSettings GetProtocolSettings() const {
    return m_protocols;
  }

private:
  /** Handle OK button click. */
  void OnOK(wxCommandEvent& event);

  /** Handle directory selection button click. */
  void OnDirSelect(wxCommandEvent& event);

  /** Handle log rotation checkbox changes. */
  void OnLogRotateCheck(wxCommandEvent& event);

  /** Handle auto-record checkbox changes. */
  void OnAutoRecordCheck(wxCommandEvent& event);

  /** Handle speed threshold checkbox changes. */
  void OnUseSpeedThresholdCheck(wxCommandEvent& event);

  /** Handle protocol checkbox changes. */
  void OnProtocolCheck(wxCommandEvent& event);

  void OnNMEA0183ReplayModeChanged(wxCommandEvent& event);

  /** Update enabled state of dependent controls. */
  void UpdateControlStates();

  /** Create and layout dialog controls. */
  void CreateControls();

  /** Create controls for recording settings tab */
  wxPanel* CreateRecordingTab(wxWindow* parent);

  /** Create controls for replay settings tab */
  wxPanel* CreateReplayTab(wxWindow* parent);

  // Recording tab controls
  wxRadioButton* m_nmea_radio;             //!< Raw NMEA format selection
  wxRadioButton* m_csv_radio;              //!< CSV format selection
  wxTextCtrl* m_dir_ctrl;                  //!< Recording directory display
  wxButton* m_dir_button;                  //!< Directory selection button
  wxCheckBox* m_log_rotate_check;          //!< Enable log rotation
  wxSpinCtrl* m_log_rotate_interval_ctrl;  //!< Hours between rotations

  // Auto record settings
  wxCheckBox* m_autoStartRecordingCheck;     //!< Enable auto-start recording
  wxCheckBox* m_use_speed_threshold_check;   //!< Enable speed threshold
  wxSpinCtrlDouble* m_speed_threshold_ctrl;  //!< Speed threshold value
  wxSpinCtrl* m_stop_delay_ctrl;             //!< Minutes before stop

  // Protocol selection
  wxCheckBox* m_nmea0183_check;  //!< Enable NMEA 0183 recording
  wxCheckBox* m_nmea2000_check;  //!< Enable NMEA 2000 recording
#if 0
   wxCheckBox* m_signalKCheck;      //!< Enable Signal K recording
#endif

  // Replay tab controls
  // NMEA 0183 replay mode
  wxRadioButton* m_nmea0183_network_radio;
  wxRadioButton* m_nmea0183_internal_radio;
  wxRadioButton* m_nmea0183_loopback_radio;

  // Network selection
  ConnectionSettingsPanel* m_nmea0183_net_panel;
  ConnectionSettingsPanel* m_nmea2000_net_panel;
#if 0
  ConnectionSettingsPanel* m_signalKNetPanel;
#endif

  VdrDataFormat m_format;       //!< Selected data format
  wxString m_recording_dir;     //!< Selected recording directory
  bool m_log_rotate;            //!< Log rotation enabled
  int m_log_rotate_interval;    //!< Hours between rotations
  bool m_auto_start_recording;  //!< Auto-start recording enabled
  bool m_use_speed_threshold;   //!< Speed threshold enabled
  double m_speed_threshold;     //!< Speed threshold in knots
  int m_stop_delay;             //!< Minutes before stopping

  VdrProtocolSettings m_protocols;  //!< Protocol selection settings
};

#endif
