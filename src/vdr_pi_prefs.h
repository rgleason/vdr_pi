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

#ifndef _VDR_PI_PREFS_H_
#define _VDR_PI_PREFS_H_

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include <wx/dialog.h>
#include <wx/spinctrl.h>

#include "vdr_pi.h"
#include "vdr_pi_prefs_net.h"

/**
 * Preferences dialog for configuring VDR settings.
 *
 * Provides UI for setting recording format, directory, auto-recording behavior,
 * protocol selection, and file rotation options.
 */
class VDRPrefsDialog : public wxDialog {
public:
  /**
   * Create new preferences dialog.
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
  VDRPrefsDialog(wxWindow* parent, wxWindowID id, VDRDataFormat format,
                 const wxString& recordingDir, bool logRotate,
                 int logRotateInterval, bool autoStartRecording,
                 bool useSpeedThreshold, double speedThreshold, int stopDelay,
                 const VDRProtocolSettings& protocols);

  /** Get selected data format setting. */
  VDRDataFormat GetDataFormat() const { return m_format; }

  /** Get configured recording directory path. */
  wxString GetRecordingDir() const { return m_recording_dir; }

  /** Check if log rotation is enabled. */
  bool GetLogRotate() const { return m_log_rotate; }

  /** Get log rotation interval in hours. */
  int GetLogRotateInterval() const { return m_log_rotate_interval; }

  /** Check if auto-start recording is enabled. */
  bool GetAutoStartRecording() const { return m_auto_start_recording; }

  /** Check if speed threshold is enabled. */
  bool GetUseSpeedThreshold() const { return m_use_speed_threshold; }

  /** Get speed threshold in knots. */
  double GetSpeedThreshold() const { return m_speed_threshold; }

  /** Get recording stop delay in minutes. */
  int GetStopDelay() const { return m_stop_delay; }

  /** Get protocol recording settings. */
  VDRProtocolSettings GetProtocolSettings() const { return m_protocols; }

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
  wxRadioButton* m_nmeaRadio;           //!< Raw NMEA format selection
  wxRadioButton* m_csvRadio;            //!< CSV format selection
  wxTextCtrl* m_dirCtrl;                //!< Recording directory display
  wxButton* m_dirButton;                //!< Directory selection button
  wxCheckBox* m_logRotateCheck;         //!< Enable log rotation
  wxSpinCtrl* m_logRotateIntervalCtrl;  //!< Hours between rotations

  // Auto record settings
  wxCheckBox* m_autoStartRecordingCheck;   //!< Enable auto-start recording
  wxCheckBox* m_useSpeedThresholdCheck;    //!< Enable speed threshold
  wxSpinCtrlDouble* m_speedThresholdCtrl;  //!< Speed threshold value
  wxSpinCtrl* m_stopDelayCtrl;             //!< Minutes before stop

  // Protocol selection
  wxCheckBox* m_nmea0183Check;  //!< Enable NMEA 0183 recording
  wxCheckBox* m_nmea2000Check;  //!< Enable NMEA 2000 recording
#if 0
   wxCheckBox* m_signalKCheck;      //!< Enable Signal K recording
#endif

  // Replay tab controls
  // NMEA 0183 replay mode
  wxRadioButton* m_nmea0183NetworkRadio;
  wxRadioButton* m_nmea0183InternalRadio;
  wxRadioButton* m_nmea0183LoopbackRadio;

  // Network selection
  ConnectionSettingsPanel* m_nmea0183NetPanel;
  ConnectionSettingsPanel* m_nmea2000NetPanel;
#if 0
  ConnectionSettingsPanel* m_signalKNetPanel;
#endif

  VDRDataFormat m_format;       //!< Selected data format
  wxString m_recording_dir;     //!< Selected recording directory
  bool m_log_rotate;            //!< Log rotation enabled
  int m_log_rotate_interval;    //!< Hours between rotations
  bool m_auto_start_recording;  //!< Auto-start recording enabled
  bool m_use_speed_threshold;   //!< Speed threshold enabled
  double m_speed_threshold;     //!< Speed threshold in knots
  int m_stop_delay;             //!< Minutes before stopping

  VDRProtocolSettings m_protocols;  //!< Protocol selection settings

  DECLARE_EVENT_TABLE()
};

#endif
