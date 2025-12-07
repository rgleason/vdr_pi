/***************************************************************************
 *   Copyright (C) 2011 by Jean-Eudes Onfray                               *
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

#include <wx/app.h>
#include <wx/filename.h>
#include <wx/gdicmn.h>
#include <wx/image.h>
#include <wx/log.h>

#include "vdr_pi.h"

#include "commons.h"
#include "icons.h"
#include "ocpn_plugin.h"

const char* const kLongDescription = _(R"(Voyage Data Recorder
Supports NMEA input data save and replay. From OpenCPN
version 5.14 also supports replaying log files created
by the core's Data Monitor)");

// class factories, used to create and destroy instances of the PlugIn
extern "C" DECL_EXP opencpn_plugin* create_pi(void* ppimgr) {
  return new VdrPi(ppimgr);
}

extern "C" DECL_EXP void destroy_pi(opencpn_plugin* p) { delete p; }

//---------------------------------------------------------------------------------------------------------
//
//    VDR PlugIn Implementation
//
//---------------------------------------------------------------------------------------------------------

VdrPi::VdrPi(void* opencpn_plugin) : opencpn_plugin_118(opencpn_plugin) {
  // Create the PlugIn icons
  InitializeImages();

  wxFileName fn;

  auto path = GetPluginDataDir("vdr_pi");
  fn.SetPath(path);
  fn.AppendDir("data");
  fn.SetFullName("vdr_panel_icon.png");

  path = fn.GetFullPath();

  wxInitAllImageHandlers();

  wxLogDebug(wxString("Using icon path: ") + path);
  if (!wxImage::CanRead(path)) {
    wxLogDebug("Initiating image handlers.");
    wxInitAllImageHandlers();
  }
  wxImage panelIcon(path);
  if (panelIcon.IsOk())
    m_panel_bitmap = wxBitmap(panelIcon);
  else
    wxLogWarning("VDR panel icon has NOT been loaded");

  m_vdr_control = nullptr;
  m_auimgr = GetFrameAuiManager();
}

int VdrPi::Init() {
  AddLocaleCatalog("opencpn-vdr_pi");

  MockControlGui tmp_gui;

  m_record_play_mgr = std::make_shared<RecordPlayMgr>(this, &tmp_gui);
  m_vdr_control = new VdrControl(wxTheApp->GetTopWindow(), m_record_play_mgr);
  SetupControl();
  m_auimgr->Update();
  m_record_play_mgr->SetControlGui(m_vdr_control);
  m_record_play_mgr->Init();

  return (WANTS_TOOLBAR_CALLBACK | INSTALLS_TOOLBAR_TOOL | WANTS_CONFIG |
          WANTS_NMEA_SENTENCES | WANTS_AIS_SENTENCES | WANTS_PREFERENCES);
}

bool VdrPi::DeInit() {
  if (m_vdr_control) {
    m_auimgr->DetachPane(m_vdr_control);
    m_vdr_control->Close();
    m_vdr_control->Destroy();
    m_vdr_control = nullptr;
  }
  if (m_record_play_mgr) {
    m_record_play_mgr->DeInit();
    m_record_play_mgr = nullptr;
  }
  return true;
}

void VdrPi::DestroyControl() {
  m_auimgr->DetachPane(m_vdr_control);
  m_vdr_control->Close();
  m_vdr_control->Destroy();
  m_vdr_control = nullptr;  // FIXME (leamas) release
  m_record_play_mgr->SetControlGui(nullptr);
}

void VdrPi::SetupControl() {
  wxPoint position = wxPoint(100, 100);
  //  Dialog will be fixed position on Android, so position carefully
#ifdef __ANDROID__
  wxRect tbRect = GetMasterToolbarRect();
  position.y = 0;
  position.x = tbRect.x + tbRect.width + 2;
#endif
  wxAuiPaneInfo pane = wxAuiPaneInfo()
                           .Name(kControlWinName)
                           .Caption(_("Voyage Data Recorder"))
                           .CaptionVisible(true)
                           .Float()
                           .FloatingPosition(position)
                           .Dockable(false)
                           .Fixed()
                           .CloseButton(true)
                           .Show(true);
  m_auimgr->AddPane(m_vdr_control, pane);
  m_auimgr->Update();
  m_record_play_mgr->SetControlGui(m_vdr_control);
}

int VdrPi::GetAPIVersionMajor() { return atoi(API_VERSION); }

int VdrPi::GetAPIVersionMinor() {
  std::string v(API_VERSION);
  size_t dotpos = v.find('.');
  return atoi(v.substr(dotpos + 1).c_str());
}

int VdrPi::GetPlugInVersionMajor() { return PLUGIN_VERSION_MAJOR; }

int VdrPi::GetPlugInVersionMinor() { return PLUGIN_VERSION_MINOR; }

int VdrPi::GetPlugInVersionPatch() { return PLUGIN_VERSION_PATCH; }

int VdrPi::GetPlugInVersionPost() { return PLUGIN_VERSION_TWEAK; }

const char* VdrPi::GetPlugInVersionPre() { return PKG_PRERELEASE; }

const char* VdrPi::GetPlugInVersionBuild() { return PKG_BUILD_INFO; }

wxBitmap* VdrPi::GetPlugInBitmap() { return &m_panel_bitmap; }

wxString VdrPi::GetCommonName() { return _("VDR"); }

wxString VdrPi::GetShortDescription() { return _("Voyage Data RecorderN"); }

wxString VdrPi::GetLongDescription() { return kLongDescription; }
