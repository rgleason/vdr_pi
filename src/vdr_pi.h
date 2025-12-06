/**************************************************************************
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

/**
 * \file
 *
 * Voyage Data Recorder plugin for OpenCPN
 *
 * Records maritime data including NMEA 0183, NMEA 2000 and SignalK messages to
 * files for later playback. Supports automatic recording based on vessel speed
 * and automatic file rotation for data management.
 */

#ifndef VDRPI_H_
#define VDRPI_H_

#include <memory>

#include "wx/wxprec.h"

#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif

#include <wx/aui/aui.h>
#include <wx/bitmap.h>
#include <wx/fileconf.h>
#include <wx/filepicker.h>
#include <wx/file.h>
#include <wx/string.h>

#include "commons.h"
#include "config.h"
#include "dm_replay_mgr.h"
#include "ocpn_plugin.h"
#include "vdr_network.h"
#include "vdr_pi_control.h"
#include "vdr_pi_time.h"

/**
 *  Main plugin class handles the plugin API implementation and top level
 *  components life cycle.
 */
class VdrPi : public opencpn_plugin_118 {
public:
  /** Create a new VDR plugin instance. */
  explicit VdrPi(void* ppimgr);

  /** Initialize the plugin and set up toolbar items. */
  int Init() override;

  /** Clean up resources and save configuration. */
  bool DeInit() override;

  int GetAPIVersionMajor() override;
  int GetAPIVersionMinor() override;
  int GetPlugInVersionMajor() override;
  int GetPlugInVersionMinor() override;
  int GetPlugInVersionPatch() override;
  int GetPlugInVersionPost() override;
  const char* GetPlugInVersionPre() override;
  const char* GetPlugInVersionBuild() override;
  wxBitmap* GetPlugInBitmap() override;
  wxString GetCommonName() override;
  wxString GetShortDescription() override;
  wxString GetLongDescription() override;

  /**
   * Process an incoming NMEA 0183 sentence for recording.
   *
   * Records the sentence if recording is active and NMEA 0183 is enabled.
   * For RMC sentences, also processes vessel speed for auto-recording.
   * @param sentence NMEA sentence to process
   */
  void SetNMEASentence(wxString& sentence) override {
    if (m_record_play_mgr) m_record_play_mgr->SetNMEASentence(sentence);
  }

  /**
   * Process an incoming AIS message for recording.
   *
   * Records AIS messages similarly to NMEA sentences if recording is active.
   * @param sentence AIS message to process
   */
  void SetAISSentence(wxString& sentence) override {
    if (m_record_play_mgr) m_record_play_mgr->SetAISSentence(sentence);
  }

  /**
   * Get number of toolbar items added by plugin.
   * @return Number of toolbar items
   */
  int GetToolbarToolCount() override { return 2; }

  /** Handle toolbar button clicks. */
  void OnToolbarToolCallback(int id) override {
    if (m_record_play_mgr) m_record_play_mgr->OnToolbarToolCallback(id);
  }

  /** Update the plugin's color scheme .*/
  void SetColorScheme(PI_ColorScheme cs) override {
    if (m_pvdrcontrol) m_pvdrcontrol->SetColorScheme(cs);
  }

private:
  std::shared_ptr<RecordPlayMgr> m_record_play_mgr;
  VdrControl* m_pvdrcontrol;
  wxAuiManager* m_pauimgr;
  wxBitmap m_panelBitmap;

  void SetupControl();
  void DestroyControl();
  VdrControl* GetControl() { return m_pvdrcontrol; }
};

#endif
