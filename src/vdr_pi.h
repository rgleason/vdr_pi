/******************************************************************************
 * $Id: vdr_pi.h, v0.2 2011/05/23 SethDart Exp $
 *
 * Project:  OpenCPN
 * Purpose:  VDR Plugin
 * Author:   Jean-Eudes Onfray
 *
 ***************************************************************************
 *   Copyright (C) 2011 by Jean-Eudes Onfray   *
 *   $EMAIL$   *
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
 ***************************************************************************
 */

#ifndef _VDRPI_H_
#define _VDRPI_H_

#include "wx/wxprec.h"

#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif  // precompiled headers

#include <wx/fileconf.h>
#include <wx/filepicker.h>
#include <wx/file.h>
#include <wx/aui/aui.h>
#include <wx/radiobut.h>
#include "ocpn_plugin.h"
#include "config.h"

#define VDR_TOOL_POSITION -1  // Request default positioning of toolbar tool

//----------------------------------------------------------------------------------------------------------
//    The PlugIn Class Definition
//----------------------------------------------------------------------------------------------------------

class VDRControl;

enum class VDRDataFormat {
  RawNMEA,  // Default raw NMEA format
  CSV,      // CSV with timestamps
            // Future formats can be added here
};

struct CSVField {
  wxString name;
  int index;
  bool required;  // Is this field required for playback?
};

class vdr_pi : public opencpn_plugin_117, wxTimer {
public:
  vdr_pi(void* ppimgr);

  //    The required PlugIn Methods
  int Init(void);
  bool DeInit(void);

  int GetAPIVersionMajor();
  int GetAPIVersionMinor();
  int GetPlugInVersionMajor();
  int GetPlugInVersionMinor();
  wxBitmap* GetPlugInBitmap();
  wxString GetCommonName();
  wxString GetShortDescription();
  wxString GetLongDescription();

  void Notify();
  void SetInterval(int interval);

  //    The optional method overrides
  void SetNMEASentence(wxString& sentence);
  void SetAISSentence(wxString& sentence);
  int GetToolbarToolCount(void);
  void OnToolbarToolCallback(int id);
  void SetColorScheme(PI_ColorScheme cs);

  bool LoadFile(const wxString& filename);
  void StartRecording();
  void StopRecording();
  void StartPlayback();
  void PausePlayback();
  void StopPlayback();
  bool IsRecording() { return m_recording; }
  bool IsPlaying() { return m_playing; }
  bool IsAtFileEnd() const { return m_atFileEnd; }
  /**
   * Schedule the next playback message based on the message timestamp.
   */
  void ScheduleNextPlayback();

  void ShowPreferencesDialog(wxWindow* parent);
  VDRDataFormat GetDataFormat() const { return m_data_format; }
  void SetDataFormat(VDRDataFormat dataFormat) { m_data_format = dataFormat; };
  wxString GetRecordingDir() const { return m_recording_dir; }
  void SetRecordingDir(const wxString& dir) { m_recording_dir = dir; }
  wxString GenerateFilename() const;

  bool IsLogRotateEnabled() const { return m_log_rotate; }
  void SetLogRotate(bool enable) { m_log_rotate = enable; }
  int GetLogRotateInterval() const { return m_log_rotate_interval; }
  void SetLogRotateInterval(int hours) { m_log_rotate_interval = hours; }
  void CheckLogRotation();
  void SetToolbarToolStatus(int id, bool status);
  int GetPlayToolbarItemId() const { return m_tb_item_id_play; }

  bool ScanFileTimestamps();
  bool SeekToFraction(double fraction);
  double GetProgressFraction() const;

  wxDateTime GetFirstTimestamp() const { return m_firstTimestamp; }
  wxDateTime GetLastTimestamp() const { return m_lastTimestamp; }
  wxDateTime GetCurrentTimestamp() const { return m_currentTimestamp; }
  void SetCurrentTimestamp(const wxDateTime& timestamp) {
    m_currentTimestamp = timestamp;
  }
  wxString GetInputFile() const;
  void ClearInputFile();
  void AdjustPlaybackBaseTime();

private:
  bool LoadConfig(void);
  bool SaveConfig(void);
  wxString FormatNMEAAsCSV(const wxString& nmea);
  bool ParseCSVHeader(const wxString& header);
  wxString ParseCSVLine(const wxString& line, wxDateTime* timestamp);
  bool IsNMEAOrAIS(const wxString& line);

  int m_tb_item_id_record;
  int m_tb_item_id_play;

  wxFileConfig* m_pconfig;
  wxAuiManager* m_pauimgr;
  VDRControl* m_pvdrcontrol;
  wxString m_ifilename;
  wxString m_ofilename;
  wxString m_recording_dir;  // Directory where recordings are saved
  int m_interval;
  bool m_recording;
  bool m_playing;
  bool m_atFileEnd;

  VDRDataFormat m_data_format;
  wxTextFile m_istream;
  wxFile m_ostream;
  wxBitmap m_panelBitmap;

  bool m_is_csv_file;
  wxArrayString m_header_fields;
  int m_timestamp_idx;
  int m_message_idx;

  bool m_log_rotate;             // Whether to automatically rotate log files
  int m_log_rotate_interval;     // Log rotation interval in hours
  wxDateTime m_recording_start;  // When current recording started

  wxDateTime m_playback_base_time;  // Real time when playback started

  wxDateTime m_firstTimestamp;
  wxDateTime m_lastTimestamp;
  wxDateTime m_currentTimestamp;

#ifdef __ANDROID__
  wxString m_temp_outfile;
  wxString m_final_outfile;
#endif
};

class VDRControl : public wxWindow {
public:
  VDRControl(wxWindow* pparent, wxWindowID id, vdr_pi* vdr);
  void SetColorScheme(PI_ColorScheme cs);
  void SetProgress(double fraction);
  void UpdateControls();
  void UpdateFileLabel(const wxString& filename);
  void UpdateTimeLabel();
  double GetSpeedMultiplier() const { return m_speedSlider->GetValue(); }

private:
  void CreateControls();
  void OnLoadButton(wxCommandEvent& event);
  void OnPlayPauseButton(wxCommandEvent& event);
  void OnSpeedSliderUpdated(wxCommandEvent& event);

  void OnProgressSliderUpdated(wxScrollEvent& even);
  void OnProgressSliderEndDrag(wxScrollEvent& event);

  void OnDataFormatRadioButton(wxCommandEvent& event);

  wxButton* m_loadBtn;
  wxButton* m_playPauseBtn;
  wxString m_playBtnTooltip;
  wxString m_pauseBtnTooltip;
  wxString m_stopBtnTooltip;

  wxSlider* m_speedSlider;
  wxSlider* m_progressSlider;
  wxStaticText* m_fileLabel;
  wxStaticText* m_timeLabel;
  vdr_pi* m_pvdr;

  bool m_isDragging;
  bool m_wasPlayingBeforeDrag;

  DECLARE_EVENT_TABLE()
};

#endif
