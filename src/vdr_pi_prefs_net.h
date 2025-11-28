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

#ifndef VDR_PI_PREFS_NET_H_
#define VDR_PI_PREFS_NET_H_

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include <wx/dialog.h>
#include <wx/spinctrl.h>

struct ConnectionSettings;

/** UI component for connection settings */
class ConnectionSettingsPanel : public wxPanel {
public:
  /**
   * Create connection settings panel.
   *
   * @param parent Parent window
   * @param title Title for the static box
   * @param settings Initial connection settings
   */
  ConnectionSettingsPanel(wxWindow* parent, const wxString& title,
                          const ConnectionSettings& settings);

  /** Get current connection settings from controls */
  [[nodiscard]] ConnectionSettings GetSettings() const;

  /** Update controls with new settings */
  void SetSettings(const ConnectionSettings& settings);

private:
  /** Handle network enable checkbox */
  void OnEnableNetwork(wxCommandEvent& event);

  /** Update enabled state of controls */
  void UpdateControlStates();

  wxCheckBox* m_enable_check;  //!< Enable network output
  wxRadioButton* m_tcp_radio;  //!< Use TCP protocol
  wxRadioButton* m_udp_radio;  //!< Use UDP protocol
  wxSpinCtrl* m_port_ctrl;     //!< Port number control

  DECLARE_EVENT_TABLE()
};

#endif  // VDR_PI_PREFS_NET_H_
