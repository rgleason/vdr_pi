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

#include <deque>
#include <map>

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
#include "vdr_pi_time.h"
#include "vdr_network.h"
#include "config.h"
#include <wx/event.h>
#include "qtstylesheet.h"
#include <wx/window.h>



enum { ID_DASH_PREFS = 999, ID_DASH_RESIZE };

#ifdef __ANDROID__
wxWindow* g_Window;
#endif

#define VDR_TOOL_POSITION -1  // Request default positioning of toolbar tool

//----------------------------------------------------------------------------------------------------------
//    The PlugIn Class Definition
//----------------------------------------------------------------------------------------------------------

class VDRControl;

/**
 * Data storage formats supported by the VDR plugin.
 *
 * Controls how data is structured and stored in VDR files. Each format offers
 * different capabilities for data organization and playback control.
 */
enum class VDRDataFormat {
  RawNMEA,  //!< Raw NMEA sentences stored unmodified
  CSV,  //!< Structured CSV format with timestamps and message type metadata.
        // Future formats can be added here
};

enum class NMEA0183ReplayMode {
  NETWORK,      // Use network connection
  INTERNAL_API  // Use PushNMEABuffer()
};

/**
 * Network settings for protocol output.
 */
struct ConnectionSettings {
  bool enabled;  //!< Enable network output
  bool useTCP;   //!< Use TCP (true) or UDP (false)
  int port;      //!< Network port number

  ConnectionSettings() : enabled(false), useTCP(true), port(10111) {}
};

/**
 * Protocol recording configuration settings.
 *
 * Controls which maritime data protocols are captured during recording.
 * Multiple protocols can be enabled simultaneously.
 */
struct VDRProtocolSettings {
  bool nmea0183;                   //!< Enable NMEA 0183 sentence recording
  bool nmea2000;                   //!< Enable NMEA 2000 PGN message recording
  bool signalK;                    //!< Enable Signal K data recording
  ConnectionSettings nmea0183Net;  //!< NMEA 0183 connection settings
  ConnectionSettings n2kNet;       //!< NMEA 2000 connection settings
  ConnectionSettings signalKNet;   //!< Signal K connection settings

  NMEA0183ReplayMode nmea0183ReplayMode =
      NMEA0183ReplayMode::INTERNAL_API;  //!< NMEA 0183 replay method

  VDRProtocolSettings() : nmea0183(true), nmea2000(false), signalK(false) {}
};

/**
 * Column definition for CSV format files.
 *
 * Describes the structure and requirements of fields in CSV format
 * VDR files to support proper parsing during playback.
 */
struct CSVField {
  wxString name;  //!< Field name in CSV header
  int index;      //!< Zero-based column position in CSV file
  bool required;  //!< Field must be present for valid playback.
};

wxDECLARE_EVENT(EVT_N2K, ObservedEvt);
wxDECLARE_EVENT(EVT_SIGNALK, ObservedEvt);

/**
 * Voyage Data Recorder plugin for OpenCPN
 *
 * Records maritime data including NMEA 0183, NMEA 2000 and SignalK messages to
 * files for later playback. Supports automatic recording based on vessel speed
 * and automatic file rotation for data management.
 */
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

  #ifdef __ANDROID__
  void OnContextMenu(wxContextMenuEvent& event);
  void OnContextMenuSelect(wxCommandEvent& event);

  void OnMouseEvent(wxMouseEvent& event);
  wxPoint m_resizeStartPoint;
  wxSize m_resizeStartSize;
  bool m_binResize;
  bool m_binResize2;

  void OnPopupClick(wxCommandEvent& evt);
  void OnDLeftClick(wxMouseEvent& event);

#endif

  /**
   * Process timer notification for playback events.
   *
   * Handles timed playback of recorded data, managing message timing
   * and maintaining playback state.
   */
  void Notify();
  /**
   * Set the interval for timer notifications.
   * @param interval Timer interval in milliseconds
   */
  void SetInterval(int interval);

  /**
   * Process an incoming NMEA 0183 sentence for recording.
   *
   * Records the sentence if recording is active and NMEA 0183 is enabled.
   * For RMC sentences, also processes vessel speed for auto-recording.
   * @param sentence NMEA sentence to process
   */
  void SetNMEASentence(wxString& sentence);
  /**
   * Process an incoming AIS message for recording.
   *
   * Records AIS messages similarly to NMEA sentences if recording is active.
   * @param sentence AIS message to process
   */
  void SetAISSentence(wxString& sentence);
  /**
   * Get number of toolbar items added by plugin.
   * @return Number of toolbar items
   */
  int GetToolbarToolCount(void);
  /** Handle toolbar button clicks. */
  void OnToolbarToolCallback(int id);
  /** Update the plugin's color scheme .*/
  void SetColorScheme(PI_ColorScheme cs);

  /** Load a VDR file containing NMEA data, either in raw NMEA format or CSV. */
  bool LoadFile(const wxString& filename, wxString* error = nullptr);
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
  /** Return whether recording is currently paused. */
  bool IsRecordingPaused() { return m_recording_paused; }
  /** Return whether playback is currently active. */
  bool IsPlaying() { return m_playing; }
  /** Return whether the end of the playback file has been reached. */
  bool IsAtFileEnd() const { return m_atFileEnd; }
  void ResetEndOfFile() { m_atFileEnd = false; }
  /**
   * Calculate when the current NMEA/SignalK message should be played during
   * replay.
   *
   * This function determines the exact time when the current message should be
   * displayed, accounting for the playback speed multiplier. The time returned
   * is in terms of the computer's clock, not the original message timestamps.
   *
   * The calculation works by:
   * 1. Finding how much time elapsed between the first message and current
   * message
   * 2. Scaling this elapsed time based on the playback speed (e.g. half time at
   * 2x speed)
   * 3. Adding the scaled time to when playback started
   *
   * @return wxDateTime When to play the current message, relative to system
   * time. Returns invalid wxDateTime if any required timestamps are invalid.
   *
   * @see GetSpeedMultiplier() - Controls how fast messages are replayed
   */
  wxDateTime GetNextPlaybackTime() const;

  /** Invoked during playback. */
  void OnTimer(wxTimerEvent& event);

  /** Show the plugin preferences dialog. */
  void ShowPreferencesDialog(wxWindow* parent);
  void ShowPreferencesDialogNative(wxWindow* parent);
  /** Get current data format setting for VDR output */
  VDRDataFormat GetDataFormat() const { return m_data_format; }
  /**
   * Set data format for VDR recording.
   *
   * If recording is active, stops current recording and starts new one with
   * updated format.
   * @param dataFormat New format to use for recording.
   */
  void SetDataFormat(VDRDataFormat dataFormat);
  /** Get configured recording directory. */
  wxString GetRecordingDir() const { return m_recording_dir; }
  /**
   * Set directory for storing VDR recordings.
   * @param dir Path to recording directory
   */
  void SetRecordingDir(const wxString& dir) { m_recording_dir = dir; }
  /**
   * Generate filename for new recording based on current UTC time.
   *
   * Creates filename in format: vdr_YYYYMMDDTHHMMSSz with appropriate
   * extension.
   */
  wxString GenerateFilename() const;

  /** Check if automatic log rotation is enabled. */
  bool IsLogRotateEnabled() const { return m_log_rotate; }
  /**
   * Enable or disable automatic log rotation.
   * @param enable True to enable rotation
   */
  void SetLogRotate(bool enable) { m_log_rotate = enable; }
  /** Get configured interval between log rotations in hours. */
  int GetLogRotateInterval() const { return m_log_rotate_interval; }
  /**
   * Set interval for automatic log rotation.
   * @param hours Hours between rotations
   */
  void SetLogRotateInterval(int hours) { m_log_rotate_interval = hours; }
  /** Check if current recording file needs rotation based on elapsed time. */
  void CheckLogRotation();
  /**
   * Update toolbar button state.
   * @param id Toolbar item identifier
   * @param status New enabled/pressed state
   */
  void SetToolbarToolStatus(int id, bool status);
  /** Get identifier for play button toolbar item. */
  int GetPlayToolbarItemId() const { return m_tb_item_id_play; }

  /**
   * Scan loaded file for timestamp information.
   *
   * Analyzes file to determine if it contains valid timestamps and stores
   * first/last timestamps if found. Required for proper playback timing.
   *
   * @param hasValidTimestamps True if valid timestamps found in file
   * @param error Error message if an error occurs during scan.
   * @return True if scan completed successfully, false if error occurred.
   */
  bool ScanFileTimestamps(bool& hasValidTimestamps, wxString& error);
  /**
   * Seek playback position to specified fraction of file.
   *
   * For files with timestamps, seeks to matching timestamp.
   * For files without timestamps, seeks to line number.
   * @param fraction Position as fraction between 0-1
   * @return True if seek successful
   */
  bool SeekToFraction(double fraction);
  /**
   * Get current playback position as fraction of total.
   *
   * For files with timestamps, based on timestamp position.
   * For files without timestamps, based on line position.
   * @return Position as fraction between 0-1
   */
  double GetProgressFraction() const;
  /** Get timestamp of first message in file. */
  wxDateTime GetFirstTimestamp() const { return m_firstTimestamp; }
  /** Get timestamp of last message in file. */
  wxDateTime GetLastTimestamp() const { return m_lastTimestamp; }
  /** Get timestamp at current playback position. */
  wxDateTime GetCurrentTimestamp() const { return m_currentTimestamp; }
  /**
   * Set timestamp for current playback position.
   * @param timestamp New current timestamp
   */
  void SetCurrentTimestamp(const wxDateTime& timestamp) {
    m_currentTimestamp = timestamp;
  }
  /**
   * Get path of currently loaded input file.
   *
   * Returns empty string if no file loaded or file doesn't exist.
   */
  wxString GetInputFile() const;
  /** Clear current input file reference and close file if open. */
  void ClearInputFile();
  /**
   * Adjust playback timing based on speed multiplier setting.
   *
   * Updates base time to maintain proper message timing when
   * playback speed is changed.
   */
  void AdjustPlaybackBaseTime();
  /** Check if automatic recording start is enabled. */
  bool IsAutoStartRecording() const { return m_auto_start_recording; }
  /**
   * Enable or disable automatic recording start.
   * @param enable True to enable auto-start
   */
  void SetAutoStartRecording(bool enable) { m_auto_start_recording = enable; }
  /** Check if speed threshold for recording is enabled. */
  bool IsUseSpeedThreshold() const { return m_use_speed_threshold; }
  /**
   * Enable or disable speed threshold for recording.
   * @param enable True to enable threshold
   */
  void SetUseSpeedThreshold(bool enable) { m_use_speed_threshold = enable; }
  /** Get configured speed threshold in knots. */
  double GetSpeedThreshold() const { return m_speed_threshold; }
  /**
   * Set speed threshold for automatic recording.
   * @param threshold Speed threshold in knots
   */
  void SetSpeedThreshold(double threshold) { m_speed_threshold = threshold; }
  /** Get configured delay before stopping recording. */
  int GetStopDelay() const { return m_stop_delay; }
  /**
   * Set delay before stopping recording when speed drops.
   * @param minutes Minutes to wait before stopping
   */
  void SetStopDelay(int minutes) { m_stop_delay = minutes; }
  /**
   * Check if auto-recording should be started or stopped based on speed over
   * ground.
   *
   * Starts recording when speed over ground exceeds threshold and stops
   * recording after configured delay when speed drops below threshold.
   * @param speed Current speed over ground in knots
   */
  void CheckAutoRecording(double speed);
  /**
   * Check if current file contains at least one time source with valid message
   * timestamps.
   *
   * The time source must have monotonically increasing timestamps for
   * timestamp-based playback.
   */
  bool HasValidTimestamps() const;

  /** Helper function to extract NMEA sentence components. */
  bool ParseNMEAComponents(const wxString nmea, wxString& talkerId,
                           wxString& sentenceId, bool& hasTimestamp) const;

  /** Helper to flush the sentence buffer to NMEA stream. */
  void FlushSentenceBuffer();

  /**
   * Get the next non-empty line from the input stream. Empty lines are skipped.
   * A line is considered empty if it contains only whitespace.
   *
   * Lines starting with '#' are considered comments and are also skipped.
   *
   * @param fromStart If true, starts reading from the beginning of the file.
   *                 If false, continues from current position.
   * @return The next non-empty line with leading/trailing whitespace removed,
   *         or empty string if no more non-empty lines exist or file isn't
   * open.
   */
  wxString GetNextNonEmptyLine(bool fromStart = false);

  /**
   * Get details for all available time sources
   * @return Map of time sources and their details
   */
  const std::unordered_map<TimeSource, TimeSourceDetails, TimeSourceHash>&
  GetTimeSources() const {
    return m_timeSources;
  }

  /**
   * Format an NMEA 2000 message based on current format
   *
   * @param pgn PGN number
   * @param source Source address
   * @param payload Raw message payload as hex string
   * @return Formatted message
   */
  static wxString FormatN2KMessage(int pgn, const wxString& source,
                                   const wxString& payload);

  /**
   * Get the ConnectionSettings structure for a specific protocol.
   *
   * @param protocol Which protocol's settings to return
   * @return Network settings for the specified protocol
   */
  const ConnectionSettings& GetNetworkSettings(const wxString& protocol) const;

  /**
   * Process a protocol message for playback.
   * Handles sending via appropriate network server if enabled.
   *
   * @param protocol Protocol type ("NMEA0183", "N2K", "SignalK")
   * @param message The message to process
   * @return True if message was processed successfully
   */
  bool PlaybackMessage(const wxString& protocol, const wxString& message);

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
  bool ParseCSVLineTimestamp(const wxString& line, wxString* messages,
                             wxDateTime* timestamp);
  /** Return true if the message is a NMEA0183 or AIS message */
  bool IsNMEA0183OrAIS(const wxString& message);
  /** Update SignalK event listeners when preferences are changed. */
  void UpdateSignalKListeners();
  /** Update NMEA 2000 event listeners when preferences are changed. */
  void UpdateNMEA2000Listeners();
  /** Process incoming NMEA 2000 message from OpenCPN. */
  void OnN2KEvent(wxCommandEvent& ev);
  /** Process incoming SignalK message from OpenCPN. */
  void OnSignalKEvent(wxCommandEvent& ev);
  double GetSpeedMultiplier() const;

  /** Helper to select the best primary time source. */
  void SelectPrimaryTimeSource();

  /**
   * Get or create network server for a protocol.
   *
   * @param protocol Protocol identifier
   * @return Pointer to server instance
   */
  VDRNetworkServer* GetServer(const wxString& protocol);

  /**
   * Parse a PCDIN message into its components
   * Format: $PCDIN,<pgn>,<timestamp>,<src>,<data>
   *
   * @param message PCDIN message to parse
   * @param pgn [out] PGN number
   * @param source [out] Source address
   * @param payload [out] Message payload
   * @return True if parsing successful
   */
  bool ParsePCDINMessage(const wxString& message, int& pgn, wxString& source,
                         wxString& payload);

  /**
   * Helper to extract PGN from NMEA 2000 message in any supported format.
   *
   * @param message NMEA 2000 message to parse
   * @return PGN number or 0 if not recognized
   */
  int ExtractPGN(const wxString& message);

  /**
   * Initialize or update network servers based on current preferences.
   *
   * This function manages the lifecycle of NMEA0183 and NMEA2000 network
   * servers. For each protocol:
   * - If enabled in preferences, starts or reconfigures the server as needed
   * - If disabled in preferences, stops any running server
   * - Only reconfigures servers when settings have changed (protocol or port)
   *
   * The function is typically called when:
   * - Starting playback
   * - After preferences are updated
   * - When reconnection is needed
   *
   * @return true if all enabled servers were started successfully, false if any
   * server failed
   * @note: Returns true if a server is disabled (not an error condition)
   */
  bool InitializeNetworkServers();
  /**
   * Stop all running network servers.
   *
   * This function performs a clean shutdown of all network servers:
   * - Stops the NMEA0183 server if running
   * - Stops the NMEA2000 server if running
   * - Closes all client connections
   * - Logs the shutdown of each server
   *
   * The function is typically called when:
   * - Stopping playback
   * - Shutting down the plugin
   * - Before reconfiguring servers
   *
   * @note: This function is safe to call even if servers are not running
   */
  void StopNetworkServers();
  /**
   * Process and send data through appropriate network servers during playback.
   *
   * This function handles the routing of NMEA messages to the appropriate
   * network server based on message type and protocol settings. It supports:
   *
   * NMEA0183:
   * - Messages starting with '$' or '!'
   * - Only sends if NMEA0183 networking is enabled
   *
   * NMEA2000:
   * - SeaSmart format ($PCDIN)
   * - Actisense ASCII format (!AIVDM)
   * - MiniPlex format ($MXPGN)
   * - YD RAW format ($YDRAW)
   * - Only sends if NMEA2000 networking is enabled
   *
   * @param data The NMEA message to send
   *        Each message should be a complete NMEA sentence including any line
   * endings
   *
   */
  void HandleNetworkPlayback(const wxString& data);

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
   * This flag is used to pause recording when the speed over ground drops below
   * the threshold and then rises above it again.
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

  /** Current data format used for recording and playback. */
  VDRDataFormat m_data_format;
  /** Active protocol recording settings. */
  VDRProtocolSettings m_protocols;

  /** Network servers for each protocol */
  std::map<wxString, std::unique_ptr<VDRNetworkServer>> m_networkServers;

  /** Input file stream for playback. */
  wxTextFile m_istream;
  /** Output file stream for recording. */
  wxFile m_ostream;
  /** Plugin toolbar icon. */
  wxBitmap m_panelBitmap;

  /**
   * Active NMEA 2000 message listeners.
   *
   * Each listener monitors specific PGN messages from the network.
   */
  std::vector<std::shared_ptr<ObservableListener>> m_n2k_listeners;
  /**
   * Active Signal K data listeners.
   *
   * Each listener monitors specific Signal K data paths.
   */
  std::vector<std::shared_ptr<ObservableListener>> m_signalk_listeners;

  /** Flag indicating if current file is CSV format. */
  bool m_is_csv_file;
  /** Column headers when reading CSV format files. */
  wxArrayString m_header_fields;
  /**
   * Index of timestamp column in CSV format.
   *
   * Set to -1 if timestamp column not found.
   */
  unsigned int m_timestamp_idx;
  /**
   * Index of NMEA message column in CSV format.
   *
   * Set to -1 if message column not found.
   */
  unsigned int m_message_idx;

  /** Whether to automatically rotate log files. */
  bool m_log_rotate;
  /** Log rotation interval in hours. */
  int m_log_rotate_interval;
  /** When current recording started. */
  wxDateTime m_recording_start;
  /**
   * System time when VDR playback was started.
   *
   * Used as the reference point for calculating when each message should be
   * played. All playback times are calculated as an offset from this timestamp.
   */
  wxDateTime m_playback_base_time;
  /**
   * The first (earliest) timestamp from the primary time source in the VDR
   * file.
   */
  wxDateTime m_firstTimestamp;
  /** The last timestamp from the primary time source in the VDR file. */
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
  bool m_use_speed_threshold;  //!< Use speed threshold for auto recording.
  /**
   * Speed threshold for auto recording, in knots.
   */
  double m_speed_threshold;
  /**
   * Last known speed over ground, in knots.
   *
   * The speed is used to determine when to start and stop recording based on
   * the configured speed threshold. The speed is received from:
   * 1. The RMC sentence for NMEA 0183 recordings.
   * 2. The SOG field in NMEA 2000 PGN 129026 messages.
   */
  double m_last_speed;
  /**
   * Indicate user has manually disabled recording while auto-recording was in
   * progress.
   *
   * This flag is used to prevent auto-recording from starting again immediately
   * after the user manually stops recording.
   * If the user manually stops recording, auto-recording will only start again
   * if the speed over ground drops below the threshold and then rises above it
   * again.
   */
  bool m_recording_manually_disabled;
  int m_stop_delay;  //!< Minutes to wait before stopping.
  /** When speed first dropped below threshold. */
  wxDateTime m_below_threshold_since;

  /**
   * Maximum number of NMEA sentences to retain until messages are dropped
   * to maintain playback timing.
   */
  static const int MAX_MSG_BUFFER_SIZE = 1000;
  /**
   * Circular buffer for sentences.
   * Used to store incoming NMEA sentences for playback, especially
   * at high speeds where sentences may arrive faster than they can be played.
   * At high replay speeds, some sentences may be skipped to maintain timing.
   */
  std::deque<wxString> m_sentence_buffer;
  /** Flag indicating if messages have been dropped from the buffer. */
  bool m_messages_dropped;

  wxEvtHandler* m_eventHandler;
  TimerHandler* m_timer;
  TimestampParser m_timestampParser;  //!< Helper for timestamp parsing
  /**
   * The set of time sources in the VDR recording.
   * Each time source is identified by its NMEA sentence type, talker ID and
   * precision.
   */
  std::unordered_map<TimeSource, TimeSourceDetails, TimeSourceHash>
      m_timeSources;
  /** The primary time source in the VDR recording. */
  TimeSource m_primaryTimeSource;
  bool m_hasPrimaryTimeSource;

#ifdef __ANDROID__
  wxString m_temp_outfile;
  wxString m_final_outfile;
#endif
};

#endif
