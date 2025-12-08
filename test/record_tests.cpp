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

#include "wx/wxprec.h"

#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif  // precompiled headers
#include "wx/tokenzr.h"
#include "wx/dir.h"
#include "wx/regex.h"

#include <gtest/gtest.h>
#include "control_gui.h"
#include "vdr_pi_time.h"
#include "vdr_pi.h"
#include "mock_plugin_api.h"
#include "record_play_mgr.h"

#include <wx/dir.h>
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/textfile.h>
#include <wx/tokenzr.h>

static bool ValidateTimestamp(const wxString& timestamp) {
  // The timestamp format from the plugin is: YYYY-MM-DDThh:mm:ss.sssZ
  // Example: 2025-02-04T12:05:36.748Z
  wxRegEx regex(
      wxT("[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}\\.[0-9]{3}Z"));

  bool matches = regex.Matches(timestamp);
  if (!matches) {
    std::cout << "Timestamp validation failed for: " << timestamp << std::endl;
  }
  return matches;
}

class TestableRecordPlayMgr : public RecordPlayMgr {
public:
  TestableRecordPlayMgr(opencpn_plugin* parent, VdrControlGui* control_gui)
      : RecordPlayMgr(parent, control_gui) {}

  void TestStopRecording() { StopRecording(); }
  void TestStartRecording() { StartRecording(); }
  void TestSetRecordingDir(const wxString& dir) { SetRecordingDir(dir); }
  void TestSetDataFormat(VdrDataFormat dataFormat) {
    SetDataFormat(dataFormat);
  }
  void TestSetLogRotate(bool enable) { SetLogRotate(enable); }
};

class RecordRawNmeaApp : public wxAppConsole {
public:
  RecordRawNmeaApp() : wxAppConsole() {}

  void Run() {
    wxLog::SetLogLevel(wxLOG_Error);
    // Create unique temporary directory for test files
    wxString tempDir = wxFileName::GetTempDir();
    wxString uniqueId = wxDateTime::Now().Format("%Y%m%d%H%M%S") +
                        wxString::Format("%d", rand());
    wxString testDir = tempDir + "/vdr_test_" + uniqueId;
    ASSERT_TRUE(wxFileName::Mkdir(testDir))
        << "Failed to create directory: " << testDir;

    VdrPi plugin(nullptr);
    MockControlGui gui;
    auto gui_ptr = static_cast<VdrControlGui*>(&gui);
    TestableRecordPlayMgr record_play_mgr(&plugin, gui_ptr);

    record_play_mgr.TestSetRecordingDir(CMAKE_BINARY_DIR);
    record_play_mgr.Init();

    // Configure plugin for recording
    record_play_mgr.TestSetRecordingDir(testDir);
    record_play_mgr.TestSetDataFormat(VdrDataFormat::kRawNmea);
    record_play_mgr.TestSetLogRotate(false);

    // Start recording
    record_play_mgr.TestStartRecording();
    ASSERT_TRUE(record_play_mgr.IsRecording()) << "Recording should be active";

    // Test NMEA sentences - using mutable strings to match API signature
    wxString sentences[] = {
        wxString(
            "$GPGGA,092750.000,5321.6802,N,00630.3372,W,1,8,1.03,61.7,M,55."
            "2,M,,*76\r\n"),
        // Create a sentence that does not end with CRLF. This is to validate
        // the VDR plugin can normalize the sentence before writing to file.
        wxString(
            "$GPRMC,092750.000,A,5321.6802,N,00630.3372,W,0.02,31.66,280511,"
            ",,A*43"),
        wxString("$HCHDT,284.3,T*23\r\n"),
        wxString("!AIVDM,1,1,,A,13Hj5J7000Od<fdKQJ3Iw`S>28FK,0*27\r\n")};

    // Send test sentences
    for (size_t i = 0; i < sizeof(sentences) / sizeof(sentences[0]); i++) {
      record_play_mgr.SetNMEASentence(sentences[i]);
    }

    // Stop recording
    record_play_mgr.TestStopRecording();

    // Verify recording file exists
    wxArrayString files;
    wxDir dir(testDir);
    bool found = dir.GetAllFiles(testDir, &files, "vdr_*.txt");
    ASSERT_TRUE(found) << "Failed to find recording files";
    ASSERT_EQ(files.size(), 1) << "Expected one recording file";

    // Read recorded content
    wxTextFile file;
    ASSERT_TRUE(file.Open(files[0])) << "Failed to open file: " << files[0];

    // Verify all sentences were recorded
    size_t sentenceIndex = 0;

    for (wxString line = file.GetFirstLine(); !file.Eof();
         line = file.GetNextLine()) {
      ASSERT_LT(sentenceIndex, sizeof(sentences) / sizeof(sentences[0]))
          << "Too many lines in recording file";

      EXPECT_EQ(line, sentences[sentenceIndex].Strip(wxString::both))
          << "Mismatch at line " << sentenceIndex;

      sentenceIndex++;
    }

    EXPECT_EQ(sentenceIndex, sizeof(sentences) / sizeof(sentences[0]))
        << "Not all sentences were recorded. Expected "
        << sizeof(sentences) / sizeof(sentences[0]) << " but got "
        << sentenceIndex;

    file.Close();
    record_play_mgr.DeInit();

    // Cleanup
    wxDir::Remove(testDir, wxPATH_RMDIR_RECURSIVE);
  }
};

class RecordNmeaWithCsvApp : public wxAppConsole {
public:
  RecordNmeaWithCsvApp() : wxAppConsole() {}

  void Run() {
    wxLog::SetLogLevel(wxLOG_Error);
    // Create unique temporary directory for test files
    wxString tempDir = wxFileName::GetTempDir();
    wxString uniqueId = wxDateTime::Now().Format("%Y%m%d%H%M%S") +
                        wxString::Format("%d", rand());
    wxString testDir = tempDir + "/vdr_test_" + uniqueId;
    ASSERT_TRUE(wxFileName::Mkdir(testDir))
        << "Failed to create directory: " << testDir;

    VdrPi plugin(nullptr);
    MockControlGui control_gui;
    TestableRecordPlayMgr record_play_mgr(&plugin, &control_gui);
    record_play_mgr.TestSetRecordingDir(CMAKE_BINARY_DIR);
    record_play_mgr.Init();

    // Configure plugin for CSV recording
    record_play_mgr.TestSetRecordingDir(testDir);
    record_play_mgr.TestSetDataFormat(VdrDataFormat::kCsv);
    record_play_mgr.TestSetLogRotate(false);

    // Start recording
    record_play_mgr.TestStartRecording();
    ASSERT_TRUE(record_play_mgr.IsRecording()) << "Recording should be active";

    // Test NMEA sentences with mix of standard NMEA and AIS
    wxString sentences[] = {
        wxString(
            "$GPGGA,092750.000,5321.6802,N,00630.3372,W,1,8,1.03,61.7,M,55."
            "2,M,,*76"),
        wxString("!AIVDM,1,1,,A,13Hj5J7000Od<fdKQJ3Iw`S>28FK,0*27")};

    // Send test sentences
    for (size_t i = 0; i < sizeof(sentences) / sizeof(sentences[0]); i++) {
      record_play_mgr.SetNMEASentence(sentences[i]);
    }

    // Stop recording
    record_play_mgr.TestStopRecording();

    // Verify recording file exists
    wxArrayString files;
    wxDir dir(testDir);
    bool found = dir.GetAllFiles(testDir, &files, "vdr_*.csv");
    ASSERT_TRUE(found) << "Failed to find recording files";
    ASSERT_EQ(files.size(), 1) << "Expected one recording file";

    // Read recorded content
    wxTextFile file;
    ASSERT_TRUE(file.Open(files[0])) << "Failed to open file: " << files[0];

    // First line should be header
    wxString line = file.GetFirstLine();
    EXPECT_EQ(line, "timestamp,type,id,message") << "Incorrect CSV header";

    // Verify each recorded line
    int lineCount = 0;
    while (!file.Eof() && lineCount < 2) {
      line = file.GetNextLine();
      // std::cout << "Processing CSV line: " << line << std::endl;

      // Split the line handling quotes properly
      std::vector<wxString> fields;
      bool inQuotes = false;
      wxString currentField;

      for (size_t i = 0; i < line.Length(); i++) {
        wxChar ch = line[i];
        if (ch == '"') {
          if (inQuotes && i + 1 < line.Length() && line[i + 1] == '"') {
            // Double quote inside quoted field
            currentField += ch;
            i++;  // Skip next quote
          } else {
            // Toggle quote state
            inQuotes = !inQuotes;
          }
        } else if (ch == ',' && !inQuotes) {
          // Field separator outside quotes
          fields.push_back(currentField);
          currentField.Clear();
        } else {
          currentField += ch;
        }
      }
      fields.push_back(currentField);  // Add last field

      // Validate we got all fields
      ASSERT_EQ(fields.size(), 4)
          << "Expected 4 CSV fields but got " << fields.size();

      wxString timestamp = fields[0];
      wxString type = fields[1];
      wxString message = fields[3];

      // Remove surrounding quotes from message if present
      if (message.StartsWith("\"") && message.EndsWith("\"")) {
        message = message.Mid(1, message.Length() - 2);
      }

      // Debug output
      // std::cout << "\nParsed CSV fields:" << std::endl;
      // std::cout << "Timestamp: " << timestamp << std::endl;
      // std::cout << "Type: " << type << std::endl;
      // std::cout << "Message: " << message << std::endl;

      // Validate based on line number
      if (lineCount == 0) {
        EXPECT_EQ(type, "NMEA0183")
            << "Expected NMEA0183 type for first message";

        wxString expected = sentences[0];
        expected.Trim(true).Trim(false);

        // std::cout << "\nFirst message comparison:" << std::endl;
        // std::cout << "Expected: " << expected << std::endl;
        // std::cout << "Actual  : " << message << std::endl;

        EXPECT_EQ(message, expected) << "Incorrect first message";
      } else {
        EXPECT_EQ(type, "AIS") << "Expected AIS type for second message";

        wxString expected = sentences[1];
        expected.Trim(true).Trim(false);

        // std::cout << "\nSecond message comparison:" << std::endl;
        // std::cout << "Expected: " << expected << std::endl;
        // std::cout << "Actual  : " << message << std::endl;

        EXPECT_EQ(message, expected) << "Incorrect second message";
      }

      // Validate timestamp format with debug output
      EXPECT_TRUE(ValidateTimestamp(timestamp))
          << "Invalid timestamp format: " << timestamp;

      lineCount++;
    }

    EXPECT_EQ(lineCount, 2) << "Expected 2 data lines in CSV file";

    file.Close();
    record_play_mgr.DeInit();

    // Cleanup
    wxDir::Remove(testDir, wxPATH_RMDIR_RECURSIVE);
  }
};

/** Test recording NMEA 0183 data in raw NMEA format. */
TEST(VDRRecordTests, RecordRawNMEA) {
  RecordRawNmeaApp app;
  app.Run();
}

/** Test recording NMEA 0183 data in CSV format. */
TEST(VDRRecordTests, RecordNMEAWithCSV) {
  RecordNmeaWithCsvApp app;
  app.Run();
}

/** Test recording NMEA0183 with pause. */
