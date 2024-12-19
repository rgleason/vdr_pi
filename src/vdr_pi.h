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

struct VDRProtocolSettings {
  bool nmea0183;
  bool nmea2000;
  bool signalK;
};

struct CSVField {
  wxString name;
  int index;
  bool required;  // Is this field required for playback?
};

wxDECLARE_EVENT(EVT_N2K, ObservedEvt);
wxDECLARE_EVENT(EVT_SIGNALK, ObservedEvt);

/** Plugin class for the Voyage Data Recorder functionality. */
class vdr_pi : public opencpn_plugin_118 {
public:
  /** Creates a new VDR plugin instance. */
  vdr_pi(void* ppimgr);

  /** Initializes the plugin and sets up toolbar items. */
  int Init(void);
  /** Cleans up resources and saves configuration. */
  bool DeInit(void);

  int GetAPIVersionMajor() override;
  int GetAPIVersionMinor() override;
  int GetPlugInVersionMajor() override;
  int GetPlugInVersionMinor() override;
  int GetPlugInVersionPatch() override;
  int GetPlugInVersionPost() override;
  const char* GetPlugInVersionPre() override;
  const char* GetPlugInVersionBuild() override;
  wxBitmap* GetPlugInBitmap();
  wxString GetCommonName() override;
  wxString GetShortDescription();
  wxString GetLongDescription();

  void Notify();
  void SetInterval(int interval);

  /** Process incoming NMEA sentences during recording. */
  void SetNMEASentence(wxString& sentence);
  /** Process incoming AIS sentences during recording. */
  void SetAISSentence(wxString& sentence);

  int GetToolbarToolCount(void);
  /** Handle toolbar button clicks. */
  void OnToolbarToolCallback(int id);
  /** Update the plugin's color scheme .*/
  void SetColorScheme(PI_ColorScheme cs);

  /** Load a VDR file containing NMEA data, either in raw NMEA format or CSV. */
  bool LoadFile(const wxString& filename);
  /** Start recording VDR data. */
  void StartRecording();
  /** Stop recording VDR data and close the VDR file. */
  void StopRecording(const wxString& reason = wxEmptyString);
  /** Pause recording VDR data, retain the existing VDR file. */
  void PauseRecording(const wxString& reason = wxEmptyString);
  /** Resume recording using the same VDR file. */
  void ResumeRecording();
  /** Start playback of VDR data. */
  void StartPlayback();
  /** Pause playback of VDR data. */
  void PausePlayback();
  /** Stop playback of VDR data. */
  void StopPlayback();
  /** Return whether recording is currently active. */
  bool IsRecording() { return m_recording; }
  /** Return whether playback is currently active. */
  bool IsPlaying() { return m_playing; }
  /** Return whether the end of the playback file has been reached. */
  bool IsAtFileEnd() const { return m_atFileEnd; }
  void ResetEndOfFile() { m_atFileEnd = false; }
  /** Schedule the next playback message based on the message timestamp. */
  void ScheduleNextPlayback();

  /** Invoked during playback. */
  void OnTimer(wxTimerEvent& event);

  /** Show the plugin preferences dialog. */
  void ShowPreferencesDialog(wxWindow* parent);
  VDRDataFormat GetDataFormat() const { return m_data_format; }
  void SetDataFormat(VDRDataFormat dataFormat);
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

  bool IsAutoStartRecording() const { return m_auto_start_recording; }
  void SetAutoStartRecording(bool enable) { m_auto_start_recording = enable; }
  bool IsUseSpeedThreshold() const { return m_use_speed_threshold; }
  void SetUseSpeedThreshold(bool enable) { m_use_speed_threshold = enable; }
  double GetSpeedThreshold() const { return m_speed_threshold; }
  void SetSpeedThreshold(double threshold) { m_speed_threshold = threshold; }
  int GetStopDelay() const { return m_stop_delay; }
  void SetStopDelay(int minutes) { m_stop_delay = minutes; }
  /**
   * Check if auto-recording should be started or stopped based on boat speed.
   */
  void CheckAutoRecording(double speed);
  bool HasValidTimestamps() const;

private:
  class TimerHandler : public wxTimer {
  public:
    TimerHandler(vdr_pi* plugin) : m_plugin(plugin) {}
    void Notify() { m_plugin->Notify(); }

  private:
    vdr_pi* m_plugin;
  };
  bool LoadConfig(void);
  bool SaveConfig(void);
  wxString FormatNMEA0183AsCSV(const wxString& nmea);
  bool ParseCSVHeader(const wxString& header);
  /** Parse timestamp from a CSV line or raw NMEA sentence. */
  wxString ParseCSVLineTimestamp(const wxString& line, wxDateTime* timestamp);
  /** Return true if the message is a NMEA0183 or AIS message */
  bool IsNMEA0183OrAIS(const wxString& message);
  /** Parse timestamp from NMEA0183 sentence. */
  bool ParseNMEATimestamp(const wxString& nmea, wxDateTime* timestamp);
  /** Update SignalK event listeners when preferences are changed. */
  void UpdateSignalKListeners();
  /** Update NMEA 2000 event listeners when preferences are changed. */
  void UpdateNMEA2000Listeners();
  /** Process incoming NMEA 2000 message from OpenCPN. */
  void OnN2KEvent(wxCommandEvent& ev);
  /** Process incoming SignalK message from OpenCPN. */
  void OnSignalKEvent(wxCommandEvent& ev);

  /**
   * Get the next non-empty line from the input stream. Empty lines are skipped.
   * A line is considered empty if it contains only whitespace.
   *
   * @param fromStart If true, starts reading from the beginning of the file.
   *                 If false, continues from current position.
   * @return The next non-empty line with leading/trailing whitespace removed,
   *         or empty string if no more non-empty lines exist or file isn't
   * open.
   */
  wxString GetNextNonEmptyLine(bool fromStart = false);

  int m_tb_item_id_record;
  int m_tb_item_id_play;

  /** Configuration object for saving/loading settings. */
  wxFileConfig* m_pconfig;
  wxAuiManager* m_pauimgr;
  VDRControl* m_pvdrcontrol;
  /** Input filename for playback. */
  wxString m_ifilename;
  /** Output filename for recording. */
  wxString m_ofilename;
  /** Directory where recordings are saved. */
  wxString m_recording_dir;
  int m_interval;
  /** Flag indicating whether recording is active. */
  bool m_recording;
  /**
   * Flag to track if recording is temporarily paused.
   *
   * This flag is used to pause recording when the boat speed drops below the
   * threshold and then rises above it again.
   */
  bool m_recording_paused;
  /** Start time of the current recording session. */
  wxDateTime m_current_recording_start;
  /** When recording was last paused. */
  wxDateTime m_recording_pause_time;
  /** Flag indicating whether playback is active. */
  bool m_playing;
  /** Flag indicating whether end of file has been reached. */
  bool m_atFileEnd;

  VDRDataFormat m_data_format;
  VDRProtocolSettings m_protocols;

  /** Input file stream for playback. */
  wxTextFile m_istream;
  /** Output file stream for recording. */
  wxFile m_ostream;
  wxBitmap m_panelBitmap;

  std::vector<std::shared_ptr<ObservableListener>> m_n2k_listeners;
  std::vector<std::shared_ptr<ObservableListener>> m_signalk_listeners;

  /** Flag indicating if current file is CSV format. */
  bool m_is_csv_file;
  wxArrayString m_header_fields;
  unsigned int m_timestamp_idx;
  unsigned int m_message_idx;

  /** Whether to automatically rotate log files. */
  bool m_log_rotate;
  /** Log rotation interval in hours. */
  int m_log_rotate_interval;
  /** When current recording started. */
  wxDateTime m_recording_start;
  /**  Real time when playback started. */
  wxDateTime m_playback_base_time;

  /** The first (earliest) timestamp in the VDR file. */
  wxDateTime m_firstTimestamp;
  /** The last timestamp in the VDR file. */
  wxDateTime m_lastTimestamp;
  /** The current timestamp during VDR playback. */
  wxDateTime m_currentTimestamp;
  /** Track whether file has valid timestamps. */
  bool m_has_timestamps;

  /**
   * Configuration parameter to control whether auto-start recording is enabled
   * or not.
   *
   * If enabled, recording will start automatically when the plugin is loaded.
   * Optionally, there may be a speed threshold that must be met before
   * recording starts.
   */
  bool m_auto_start_recording;
  bool m_use_speed_threshold;  // Use speed threshold for auto recording.
  double m_speed_threshold;    // Speed threshold for auto recording.
  double m_last_speed;         // Last known boat speed.
  /**
   * Indicate user has manually disabled recording while auto-recording was in
   * progress.
   *
   * This flag is used to prevent auto-recording from starting again immediately
   * after the user manually stops recording.
   * If the user manually stops recording, auto-recording will only start again
   * if the boat speed drops below the threshold and then rises above it again.
   */
  bool m_recording_manually_disabled;
  int m_stop_delay;  // Minutes to wait before stopping.
  /** When speed first dropped below threshold. */
  wxDateTime m_below_threshold_since;

  wxEvtHandler* m_eventHandler;
  TimerHandler* m_timer;

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
