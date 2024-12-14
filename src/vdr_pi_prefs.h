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

class VDRPrefsDialog : public wxDialog {
public:
  VDRPrefsDialog(wxWindow* parent, wxWindowID id, VDRDataFormat format,
                 const wxString& recordingDir, bool logRotate,
                 int logRotateInterval, bool autoStartRecording,
                 bool useSpeedThreshold, double speedThreshold, int stopDelay);
  VDRDataFormat GetDataFormat() const { return m_format; }
  wxString GetRecordingDir() const { return m_recording_dir; }
  bool GetLogRotate() const { return m_log_rotate; }
  int GetLogRotateInterval() const { return m_log_rotate_interval; }
  bool GetAutoStartRecording() const { return m_auto_start_recording; }
  bool GetUseSpeedThreshold() const { return m_use_speed_threshold; }
  double GetSpeedThreshold() const { return m_speed_threshold; }
  int GetStopDelay() const { return m_stop_delay; }

private:
  void OnOK(wxCommandEvent& event);
  void OnDirSelect(wxCommandEvent& event);
  void OnLogRotateCheck(wxCommandEvent& event);
  void OnAutoRecordCheck(wxCommandEvent& event);
  void OnUseSpeedThresholdCheck(wxCommandEvent& event);
  void UpdateControlStates();
  void CreateControls();

  wxRadioButton* m_nmeaRadio;
  wxRadioButton* m_csvRadio;
  wxTextCtrl* m_dirCtrl;
  wxButton* m_dirButton;
  wxCheckBox* m_logRotateCheck;
  wxSpinCtrl* m_logRotateIntervalCtrl;

  // Auto record settings
  wxCheckBox* m_autoStartRecordingCheck;
  wxCheckBox* m_useSpeedThresholdCheck;
  wxSpinCtrlDouble* m_speedThresholdCtrl;
  wxSpinCtrl* m_stopDelayCtrl;

  VDRDataFormat m_format;
  wxString m_recording_dir;
  bool m_log_rotate;
  int m_log_rotate_interval;
  bool m_auto_start_recording;  // Automatically start recording.
  bool m_use_speed_threshold;   // Use speed threshold for auto recording.
  double m_speed_threshold;     // Speed threshold for auto recording.
  int m_stop_delay;             // Minutes to wait before stopping.

  DECLARE_EVENT_TABLE()
};

#endif