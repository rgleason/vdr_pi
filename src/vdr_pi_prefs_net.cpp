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
#include <cstdint>

#include "vdr_pi_prefs.h"

BEGIN_EVENT_TABLE(ConnectionSettingsPanel, wxPanel)
EVT_CHECKBOX(wxID_ANY, ConnectionSettingsPanel::OnEnableNetwork)
END_EVENT_TABLE()

ConnectionSettingsPanel::ConnectionSettingsPanel(
    wxWindow* parent, const wxString& title, const ConnectionSettings& settings)
    : wxPanel(parent) {
  wxStaticBox* box = new wxStaticBox(this, wxID_ANY, title);
  wxStaticBoxSizer* sizer = new wxStaticBoxSizer(box, wxVERTICAL);

  // Enable checkbox
  m_enableCheck = new wxCheckBox(this, wxID_ANY, _("Enable network output"));
  m_enableCheck->SetValue(settings.enabled);
  m_enableCheck->Bind(wxEVT_CHECKBOX, &ConnectionSettingsPanel::OnEnableNetwork,
                      this);
  sizer->Add(m_enableCheck, 0, wxALL, 5);

  // Protocol selection
  wxBoxSizer* protocolSizer = new wxBoxSizer(wxHORIZONTAL);
  protocolSizer->Add(new wxStaticText(this, wxID_ANY, _("Protocol:")), 0,
                     wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);

  m_tcpRadio = new wxRadioButton(this, wxID_ANY, _("TCP"), wxDefaultPosition,
                                 wxDefaultSize, wxRB_GROUP);
  m_udpRadio = new wxRadioButton(this, wxID_ANY, _("UDP"));
  m_tcpRadio->SetValue(settings.useTCP);
  m_udpRadio->SetValue(!settings.useTCP);

  protocolSizer->Add(m_tcpRadio, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
  protocolSizer->Add(m_udpRadio, 0, wxALIGN_CENTER_VERTICAL);
  sizer->Add(protocolSizer, 0, wxALL, 5);

  // Port number
  wxBoxSizer* portSizer = new wxBoxSizer(wxHORIZONTAL);
  portSizer->Add(new wxStaticText(this, wxID_ANY, _("Data Port:")), 0,
                 wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);

  m_portCtrl = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                              wxDefaultSize, wxSP_ARROW_KEYS, 1024, 65535,
                              settings.port);
  portSizer->Add(m_portCtrl, 0, wxALIGN_CENTER_VERTICAL);
  sizer->Add(portSizer, 0, wxALL, 5);

  SetSizer(sizer);
  UpdateControlStates();
}

ConnectionSettings ConnectionSettingsPanel::GetSettings() const {
  ConnectionSettings settings;
  settings.enabled = m_enableCheck->GetValue();
  settings.useTCP = m_tcpRadio->GetValue();
  settings.port = m_portCtrl->GetValue();
  return settings;
}

void ConnectionSettingsPanel::SetSettings(const ConnectionSettings& settings) {
  m_enableCheck->SetValue(settings.enabled);
  m_tcpRadio->SetValue(settings.useTCP);
  m_udpRadio->SetValue(!settings.useTCP);
  m_portCtrl->SetValue(settings.port);
  UpdateControlStates();
}

void ConnectionSettingsPanel::OnEnableNetwork(wxCommandEvent& event) {
  UpdateControlStates();
}

void ConnectionSettingsPanel::UpdateControlStates() {
  bool enabled = m_enableCheck->GetValue();
  m_tcpRadio->Enable(enabled);
  m_udpRadio->Enable(enabled);
  m_portCtrl->Enable(enabled);
}
