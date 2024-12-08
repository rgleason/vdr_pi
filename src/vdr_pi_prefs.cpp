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

#include "vdr_pi_prefs.h"

enum { ID_VDR_DIR_BUTTON = wxID_HIGHEST + 1, ID_VDR_LOG_ROTATE_CHECK };

BEGIN_EVENT_TABLE(VDRPrefsDialog, wxDialog)
EVT_BUTTON(wxID_OK, VDRPrefsDialog::OnOK)
EVT_BUTTON(ID_VDR_DIR_BUTTON, VDRPrefsDialog::OnDirSelect)
EVT_CHECKBOX(ID_VDR_LOG_ROTATE_CHECK, VDRPrefsDialog::OnLogRotateCheck)
END_EVENT_TABLE()

VDRPrefsDialog::VDRPrefsDialog(wxWindow* parent, wxWindowID id,
                               VDRDataFormat format,
                               const wxString& recordingDir, bool logRotate,
                               int logRotateInterval)
    : wxDialog(parent, id, _("VDR Preferences"), wxDefaultPosition,
               wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      m_format(format),
      m_recording_dir(recordingDir),
      m_log_rotate(logRotate),
      m_log_rotate_interval(logRotateInterval) {
  CreateControls();
  GetSizer()->Fit(this);
  GetSizer()->SetSizeHints(this);
  Centre();
}

void VDRPrefsDialog::CreateControls() {
  wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
  SetSizer(mainSizer);

  // Add format choice
  wxStaticBox* formatBox =
      new wxStaticBox(this, wxID_ANY, _("Recording Format"));
  wxStaticBoxSizer* formatSizer = new wxStaticBoxSizer(formatBox, wxVERTICAL);

  m_nmeaRadio = new wxRadioButton(this, wxID_ANY, _("Raw NMEA"),
                                  wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
  m_csvRadio = new wxRadioButton(this, wxID_ANY, _("CSV with timestamps"));

  formatSizer->Add(m_nmeaRadio, 0, wxALL, 5);
  formatSizer->Add(m_csvRadio, 0, wxALL, 5);

  mainSizer->Add(formatSizer, 0, wxEXPAND | wxALL, 5);

  // Add recording directory controls
  wxStaticBox* dirBox =
      new wxStaticBox(this, wxID_ANY, _("Recording Directory"));
  wxStaticBoxSizer* dirSizer = new wxStaticBoxSizer(dirBox, wxHORIZONTAL);

  m_dirCtrl = new wxTextCtrl(this, wxID_ANY, m_recording_dir, wxDefaultPosition,
                             wxDefaultSize, wxTE_READONLY);
  m_dirButton = new wxButton(this, ID_VDR_DIR_BUTTON, _("Browse..."));

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

  wxStaticBox* logBox =
      new wxStaticBox(this, wxID_ANY, _("VDR File Management"));
  wxStaticBoxSizer* logSizer = new wxStaticBoxSizer(logBox, wxVERTICAL);

  m_logRotateCheck = new wxCheckBox(this, ID_VDR_LOG_ROTATE_CHECK,
                                    _("Create new VDR file every:"));
  m_logRotateCheck->SetValue(m_log_rotate);

  wxBoxSizer* intervalSizer = new wxBoxSizer(wxHORIZONTAL);
  m_logRotateIntervalCtrl = new wxSpinCtrl(
      this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
      wxSP_ARROW_KEYS, 1, 168, m_log_rotate_interval);
  intervalSizer->Add(m_logRotateIntervalCtrl, 0,
                     wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
  intervalSizer->Add(new wxStaticText(this, wxID_ANY, _("hours")), 0,
                     wxALIGN_CENTER_VERTICAL);

  logSizer->Add(m_logRotateCheck, 0, wxALL, 5);
  logSizer->Add(intervalSizer, 0, wxLEFT | wxRIGHT | wxBOTTOM, 5);

  mainSizer->Add(logSizer, 0, wxEXPAND | wxALL, 5);

  // Enable/disable interval control based on checkbox
  m_logRotateIntervalCtrl->Enable(m_log_rotate);

  // Standard dialog buttons
  wxStdDialogButtonSizer* buttonSizer = new wxStdDialogButtonSizer();
  buttonSizer->AddButton(new wxButton(this, wxID_OK));
  buttonSizer->AddButton(new wxButton(this, wxID_CANCEL));
  buttonSizer->Realize();

  mainSizer->Add(buttonSizer, 0, wxEXPAND | wxALL, 5);
}

void VDRPrefsDialog::OnOK(wxCommandEvent& event) {
  m_format =
      m_csvRadio->GetValue() ? VDRDataFormat::CSV : VDRDataFormat::RawNMEA;
  m_log_rotate = m_logRotateCheck->GetValue();
  m_log_rotate_interval = m_logRotateIntervalCtrl->GetValue();
  event.Skip();
}

void VDRPrefsDialog::OnDirSelect(wxCommandEvent& event) {
  wxDirDialog dlg(this, _("Choose a directory"), m_recording_dir,
                  wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);

  if (dlg.ShowModal() == wxID_OK) {
    m_recording_dir = dlg.GetPath();
    m_dirCtrl->SetValue(m_recording_dir);
  }
}

void VDRPrefsDialog::OnLogRotateCheck(wxCommandEvent& event) {
  m_logRotateIntervalCtrl->Enable(event.IsChecked());
}
