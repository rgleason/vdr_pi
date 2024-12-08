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
                 int logRotateInterval);
  VDRDataFormat GetDataFormat() const { return m_format; }
  wxString GetRecordingDir() const { return m_recording_dir; }
  bool GetLogRotate() const { return m_log_rotate; }
  int GetLogRotateInterval() const { return m_log_rotate_interval; }

private:
  void OnOK(wxCommandEvent& event);
  void OnDirSelect(wxCommandEvent& event);
  void OnLogRotateCheck(wxCommandEvent& event);
  void CreateControls();

  wxRadioButton* m_nmeaRadio;
  wxRadioButton* m_csvRadio;
  wxTextCtrl* m_dirCtrl;
  wxButton* m_dirButton;
  wxCheckBox* m_logRotateCheck;
  wxSpinCtrl* m_logRotateIntervalCtrl;

  VDRDataFormat m_format;
  wxString m_recording_dir;
  bool m_log_rotate;
  int m_log_rotate_interval;

  DECLARE_EVENT_TABLE()
};

#endif