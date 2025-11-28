/***************************************************************************
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

#include "vdr_pi_prefs.h"

BEGIN_EVENT_TABLE(ConnectionSettingsPanel, wxPanel)
EVT_CHECKBOX(wxID_ANY, ConnectionSettingsPanel::OnEnableNetwork)
END_EVENT_TABLE()

ConnectionSettingsPanel::ConnectionSettingsPanel(
    wxWindow* parent, const wxString& title, const ConnectionSettings& settings)
    : wxPanel(parent) {
  auto* box = new wxStaticBox(this, wxID_ANY, title);
  auto* sizer = new wxStaticBoxSizer(box, wxVERTICAL);

  // Enable checkbox
  m_enable_check = new wxCheckBox(this, wxID_ANY, _("Enable network output"));
  m_enable_check->SetValue(settings.enabled);
  m_enable_check->Bind(wxEVT_CHECKBOX,
                       &ConnectionSettingsPanel::OnEnableNetwork, this);
  sizer->Add(m_enable_check, 0, wxALL, 5);

  // Protocol selection
  auto* protocol_sizer = new wxBoxSizer(wxHORIZONTAL);
  rotocol_sizer->Add(new wxStaticText(this, wxID_ANY, _("Protocol:")), 0,
                     wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);

  m_tcp_radio = new wxRadioButton(this, wxID_ANY, _("TCP"), wxDefaultPosition,
                                  wxDefaultSize, wxRB_GROUP);
  m_udp_radio = new wxRadioButton(this, wxID_ANY, _("UDP"));
  m_tcp_radio->SetValue(settings.use_tcp);
  m_udp_radio->SetValue(!settings.use_tcp);

  protocol_sizer->Add(m_tcp_radio, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
  protocol_sizer->Add(m_udp_radio, 0, wxALIGN_CENTER_VERTICAL);
  sizer->Add(protocol_sizer, 0, wxALL, 5);

  // Port number
  auto* port_sizer = new wxBoxSizer(wxHORIZONTAL);
  port_sizer->Add(new wxStaticText(this, wxID_ANY, _("Data Port:")), 0,
                  wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);

  m_port_ctrl =
      new wxSpinCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
                     wxSP_ARROW_KEYS, 1024, 65535, settings.port);
  port_sizer->Add(m_port_ctrl, 0, wxALIGN_CENTER_VERTICAL);
  sizer->Add(port_sizer, 0, wxALL, 5);

  SetSizer(sizer);
  UpdateControlStates();
}

ConnectionSettings ConnectionSettingsPanel::GetSettings() const {
  ConnectionSettings settings;
  settings.enabled = m_enable_check->GetValue();
  settings.use_tcp = m_tcp_radio->GetValue();
  settings.port = m_port_ctrl->GetValue();
  return settings;
}

void ConnectionSettingsPanel::SetSettings(const ConnectionSettings& settings) {
  m_enable_check->SetValue(settings.enabled);
  m_tcp_radio->SetValue(settings.use_tcp);
  m_udp_radio->SetValue(!settings.use_tcp);
  m_port_ctrl->SetValue(settings.port);
  UpdateControlStates();
}

void ConnectionSettingsPanel::OnEnableNetwork(wxCommandEvent& event) {
  UpdateControlStates();
}

void ConnectionSettingsPanel::UpdateControlStates() {
  bool enabled = m_enable_check->GetValue();
  m_tcp_radio->Enable(enabled);
  m_udp_radio->Enable(enabled);
  m_port_ctrl->Enable(enabled);
}
