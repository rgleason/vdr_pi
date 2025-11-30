/***************************************************************************
 *   Copyright (C) 2011  Jean-Eudes Onfray                                 *
 *   Copyright (C) 2025  Sebastian Rosset                                  *
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

#ifndef CONTROL_GUI_H_
#define CONTROL_GUI_H_

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include "ocpn_plugin.h"
#include "record_play_mgr.h"

class VdrPi;

/**
 * Limited GUI interface fully visible.
 */
class VdrControlGui {
public:
  virtual void SetColorScheme(PI_ColorScheme cs) = 0;

  /**
   * Update progress indication for playback position.
   * @param fraction Current position as fraction between 0-1
   */
  virtual void SetProgress(double fraction) = 0;

  /** Update state of UI controls based on playback status. */
  virtual void UpdateControls() = 0;

  /**
   * Update displayed filename in UI.
   * @param filename Path of currently loaded file
   */
  virtual void UpdateFileLabel(const wxString& filename) = 0;

  virtual void UpdateNetworkStatus(const wxString& status) = 0;

  virtual void OnToolbarToolCallback(int id) = 0;

  virtual void EnableSpeedSlider(bool enable) = 0;

  virtual double GetSpeedMultiplier() const = 0;
};

#endif  // CONTROL_GUI_H_
