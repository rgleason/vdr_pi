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

#include <gtest/gtest.h>
#include "vdr_pi_time.h"
#include "vdr_pi.h"
#include "mock_plugin_api.h"
#include "record_play_mgr.h"

static std::string MakeReadable(const wxString& str) {
  return "\"" + str.ToStdString() + "\"";
}

static wxArrayString ParseCSVLine(const wxString& line) {
  wxArrayString fields;
  wxString currentField;
  bool inQuotes = false;

  for (size_t i = 0; i < line.Length(); i++) {
    wxChar ch = line[i];

    if (ch == '"') {
      if (inQuotes && i + 1 < line.Length() && line[i + 1] == '"') {
        // Double quotes inside quoted field = escaped quote
        currentField += '"';
        i++;  // Skip next quote
      } else {
        // Toggle quote state
        inQuotes = !inQuotes;
      }
    } else if (ch == ',' && !inQuotes) {
      // End of field
      fields.Add(currentField);
      currentField.Clear();
    } else {
      currentField += ch;
    }
  }

  // Add the last field
  fields.Add(currentField);
  return fields;
}

class TestableRecordPlayMgr : public RecordPlayMgr {
public:
  TestableRecordPlayMgr(opencpn_plugin* parent, VdrControlGui* control_gui)
      : RecordPlayMgr(parent, control_gui) {}

  bool TestHasValidTimestamps() const { return HasValidTimestamps(); }

  const std::unordered_map<TimeSource, TimeSourceDetails, TimeSourceHash>&
  TestGetTimeSources() const {
    return GetTimeSources();
  }

  wxString TestGetNextNonEmptyLine(bool fromStart = false) {
    return GetNextNonEmptyLine(fromStart);
  }

  void TestFlushSentenceBuffer() { FlushSentenceBuffer(); }

  void TestSetRecordingDir(wxString dir) { SetRecordingDir(dir); }
};

class PlaybackNoTimestampsApp : public wxAppConsole {
public:
  PlaybackNoTimestampsApp() : wxAppConsole() {}

  void Run() {
    wxLog::SetLogLevel(wxLOG_Error);
    VdrPi plugin(nullptr);
    MockControlGui control_gui;
    TestableRecordPlayMgr record_play_mgr(&plugin, &control_gui);

    // Initialize the plugin properly
    record_play_mgr.TestSetRecordingDir(CMAKE_BINARY_DIR);
    record_play_mgr.Init();

    // Clear any previous mock state
    ClearNMEASentences();

    // Load test file without timestamps
    wxString testfile = wxString(TESTDATA) + wxString("/no_timestamps.txt");
    ASSERT_TRUE(record_play_mgr.LoadFile(testfile))
        << "Failed to load test file";
    bool hasValidTimestamps;
    wxString error;
    ASSERT_TRUE(record_play_mgr.ScanFileTimestamps(hasValidTimestamps, error))
        << wxString::Format("Failed to scan timestamps: %s", error);
    EXPECT_FALSE(hasValidTimestamps) << "File should not have timestamps";
    EXPECT_EQ(error, "") << "Unexpected error message";

    // Verify no timestamps found
    EXPECT_FALSE(record_play_mgr.TestHasValidTimestamps())
        << "Expected HasValidTimestamps to return false";

    // Read expected sentences from file
    wxTextFile expectedFile;
    ASSERT_TRUE(expectedFile.Open(testfile))
        << "Failed to open test file for reading expectations";

    std::vector<wxString> expected_sentences;
    for (wxString line = expectedFile.GetFirstLine(); !expectedFile.Eof();
         line = expectedFile.GetNextLine()) {
      if (!line.IsEmpty() && !line.StartsWith("#")) {
        expected_sentences.push_back(line + "\r\n");
        wxLogMessage("Expected: %s", line);
      }
    }
    expectedFile.Close();

    // Start playback
    wxLogMessage("PlaybackNoTimestamps test: Starting playback");
    wxString message;
    record_play_mgr.StartPlayback(message);

    // Give some time for playback to process
    wxMilliSleep(500);

    wxLogMessage("PlaybackNoTimestamps test: Stopping playback");

    // Stop playback
    record_play_mgr.StopPlayback();

    // Wait a bit more to ensure any pending timer events are processed.
    wxMilliSleep(100);

    // Flush buffer to ensure PushNMEABuffer() has been called for each message.
    record_play_mgr.TestFlushSentenceBuffer();

    // Get sentences sent to NMEA buffer, obtained from mock interface.
    const auto& sentences = GetNMEASentences();

    // Verify number of sentences.
    EXPECT_EQ(sentences.size(), expected_sentences.size())
        << "Expected " << expected_sentences.size() << " sentences but got "
        << sentences.size();

    wxLogMessage("Expected sentences: %d", expected_sentences.size());
    // Verify each sentence
    for (size_t i = 0;
         i < std::min(sentences.size(), expected_sentences.size()); i++) {
      EXPECT_EQ(sentences[i], expected_sentences[i].Strip(wxString::both))
          << "Mismatch at sentence " << i;
    }

    // Clean up
    wxLogMessage("PlaybackNoTimestamps test complete. Calling DeInit()");
    record_play_mgr.DeInit();
    wxLogMessage("PlaybackNoTimestamps test complete");
  }
};

class PlaybackTimestampsApp : public wxAppConsole {
public:
  PlaybackTimestampsApp() : wxAppConsole() {}

  void Run() {
    wxLog::SetLogLevel(wxLOG_Error);
    VdrPi plugin(nullptr);
    MockControlGui control_gui;
    TestableRecordPlayMgr record_play_mgr(&plugin, &control_gui);

    // Initialize the plugin properly
    record_play_mgr.TestSetRecordingDir(CMAKE_BINARY_DIR);
    record_play_mgr.Init();

    // Clear any previous mock state
    ClearNMEASentences();

    // Load test file without timestamps
    wxString testfile = wxString(TESTDATA) + wxString("/with_timestamps.txt");
    ASSERT_TRUE(record_play_mgr.LoadFile(testfile))
        << "Failed to load test file";
    bool hasValidTimestamps;
    wxString error;
    ASSERT_TRUE(record_play_mgr.ScanFileTimestamps(hasValidTimestamps, error))
        << wxString::Format("Failed to scan timestamps: %s", error);
    EXPECT_TRUE(hasValidTimestamps) << "Failed to scan timestamps";
    EXPECT_EQ(error, "") << "Unexpected error message";

    // Verify timestamps found
    EXPECT_TRUE(record_play_mgr.TestHasValidTimestamps())
        << "Expected HasValidTimestamps to return false";

    // Read expected sentences from file
    wxTextFile expectedFile;
    ASSERT_TRUE(expectedFile.Open(testfile))
        << "Failed to open test file for reading expectations";

    std::vector<wxString> expected_sentences;
    for (wxString line = expectedFile.GetFirstLine(); !expectedFile.Eof();
         line = expectedFile.GetNextLine()) {
      if (!line.IsEmpty()) {
        expected_sentences.push_back(line + "\r\n");
      }
    }
    expectedFile.Close();

    // Start playback
    wxString msg;
    record_play_mgr.StartPlayback(msg);

    // Give some time for playback to process
    wxMilliSleep(500);

    // Stop playback
    record_play_mgr.StopPlayback();

    // Wait a bit more to ensure any pending timer events are processed.
    wxMilliSleep(100);

    // Flush buffer to ensure PushNMEABuffer() has been called for each message.
    record_play_mgr.TestFlushSentenceBuffer();

    // Get sentences sent to NMEA buffer, obtained from mock interface.
    const auto& sentences = GetNMEASentences();

    // Verify number of sentences.
    EXPECT_EQ(sentences.size(), expected_sentences.size())
        << "Expected " << expected_sentences.size() << " sentences but got "
        << sentences.size();

    // Verify each sentence
    for (size_t i = 0;
         i < std::min(sentences.size(), expected_sentences.size()); i++) {
      EXPECT_EQ(sentences[i], expected_sentences[i].Strip(wxString::both))
          << "Mismatch at sentence " << i;
    }

    // Clean up
    record_play_mgr.DeInit();
  }
};

class PlaybackCsvFileApp : public wxAppConsole {
public:
  PlaybackCsvFileApp() : wxAppConsole() {}

  void Run() {
    wxLog::SetLogLevel(wxLOG_Error);
    VdrPi plugin(nullptr);
    MockControlGui control_gui;
    TestableRecordPlayMgr record_play_mgr(&plugin, &control_gui);

    // Initialize the plugin properly
    record_play_mgr.TestSetRecordingDir(CMAKE_BINARY_DIR);
    record_play_mgr.Init();

    // Clear any previous mock state
    ClearNMEASentences();

    // Load test CSV file
    wxString testfile = wxString(TESTDATA) + wxString("/test_recording.csv");
    ASSERT_TRUE(record_play_mgr.LoadFile(testfile))
        << "Failed to load test file";
    bool hasValidTimestamps;
    wxString error;
    ASSERT_TRUE(record_play_mgr.ScanFileTimestamps(hasValidTimestamps, error))
        << wxString::Format("Failed to scan timestamps: %s", error);
    EXPECT_TRUE(hasValidTimestamps) << "Failed to scan timestamps";
    EXPECT_EQ(error, "") << "Unexpected error message";

    // Read expected sentences from CSV file
    wxTextFile expectedFile;
    ASSERT_TRUE(expectedFile.Open(testfile))
        << "Failed to open test file for reading expectations";

    std::vector<wxString> expected_sentences;

    // Skip header
    wxString line = expectedFile.GetFirstLine();
    ASSERT_TRUE(line.Contains("timestamp,type,id,message"))
        << "Missing CSV header";

    // Parse CSV content to get NMEA messages
    for (line = expectedFile.GetNextLine(); !expectedFile.Eof();
         line = expectedFile.GetNextLine()) {
      if (!line.IsEmpty()) {
        wxArrayString fields = ParseCSVLine(line);
        EXPECT_EQ(fields.size(), 4)
            << "Expected 4 CSV fields but got " << fields.size();
        if (fields.size() >= 4) {
          // Get NMEA message and remove quotes
          wxString message = fields[3].Trim(true).Trim(false);
          message.Replace("\"", "");
          expected_sentences.push_back(message);
        }
      }
    }
    expectedFile.Close();

    // Start playback
    wxString msg;
    record_play_mgr.StartPlayback(msg);

    // Give some time for playback to process
    wxMilliSleep(500);

    // Stop playback
    record_play_mgr.StopPlayback();

    // Wait a bit more to ensure any pending timer events are processed.
    wxMilliSleep(100);

    // Flush buffer to ensure PushNMEABuffer() has been called for each message.
    record_play_mgr.TestFlushSentenceBuffer();

    // Get sentences sent to NMEA buffer, obtained from mock interface.
    const auto& sentences = GetNMEASentences();

    // Verify number of sentences.
    EXPECT_EQ(sentences.size(), expected_sentences.size())
        << "Expected " << expected_sentences.size() << " sentences but got "
        << sentences.size();

    // Verify each sentence
    for (size_t i = 0;
         i < std::min(sentences.size(), expected_sentences.size()); i++) {
      wxString sentence_stripped = sentences[i];
      sentence_stripped.Replace("\r\n", "");  // Remove CRLF for comparison
      EXPECT_EQ(MakeReadable(sentence_stripped),
                MakeReadable(expected_sentences[i]))
          << "Mismatch at sentence " << i;
    }

    // Clean up
    record_play_mgr.DeInit();
  }
};
// PlaybackNonChronologicalTimestamps
class PlaybackNonChronologicalTimestampsApp : public wxAppConsole {
public:
  PlaybackNonChronologicalTimestampsApp() : wxAppConsole() {}

  void Run() {
    wxLog::SetLogLevel(wxLOG_Error);
    VdrPi plugin(nullptr);
    MockControlGui control_gui;
    TestableRecordPlayMgr record_play_mgr(&plugin, &control_gui);

    // Initialize the plugin properly
    record_play_mgr.TestSetRecordingDir(CMAKE_BINARY_DIR);
    record_play_mgr.Init();

    wxString testfile = wxString(TESTDATA) + wxString("/not_chronological.txt");
    ASSERT_TRUE(record_play_mgr.LoadFile(testfile))
        << "Failed to load test file";
    bool hasValidTimestamps;
    wxString error;
    ASSERT_TRUE(record_play_mgr.ScanFileTimestamps(hasValidTimestamps, error))
        << wxString::Format("Failed to scan timestamps: %s", error);
    EXPECT_TRUE(hasValidTimestamps) << "Failed to scan timestamps";
    EXPECT_EQ(error, "") << "Unexpected error message";

    // Verify no timestamps were found, because none of the time sources are
    // in chronological order.
    EXPECT_FALSE(record_play_mgr.TestHasValidTimestamps())
        << "Expected HasValidTimestamps to return true";

    // Clean up
    record_play_mgr.DeInit();
  }
};
class TestRealRecordingsApp : public wxAppConsole {
public:
  TestRealRecordingsApp() : wxAppConsole() {}

  void Run() {
    wxLog::SetLogLevel(wxLOG_Error);
    VdrPi plugin(nullptr);
    MockControlGui control_gui;
    TestableRecordPlayMgr record_play_mgr(&plugin, &control_gui);
    record_play_mgr.TestSetRecordingDir(CMAKE_BINARY_DIR);
    record_play_mgr.Init();

    struct TimeSourceExpectation {
      std::string talker;    //!< talker ID.
      std::string sentence;  //!< sentence type.
      int precision;         //!< time precision (number of decimal places).
      bool chronological;    //!< whether timestamps are in chronological order.
    };

    struct TestCase {
      std::string filename;     //!< name of the test file.
      bool expectedScanResult;  //!< expected result of ScanFileTimestamps().
      bool
          expectedValidTimestamps;  //! expected result of HasValidTimestamps().
      std::vector<TimeSourceExpectation>
          expectedSources;  //!< expected time sources.
    };

    std::vector<TestCase> tests = {{"Hakefjord-Sweden-1m.txt",
                                    true,
                                    true,
                                    {{"GP", "GGA", 0, true},
                                     {"GP", "RMC", 0, true},
                                     {"GP", "GBS", 2, true},
                                     {"GP", "GLL", 0, true},
                                     {"GP", "RMC", 2, true}}},
                                   {"Hartmut-AN-Markermeer-Wind-AIS-6m.txt",
                                    true,
                                    // No valid timestamps because none of the
                                    // time sources are in chronological order.
                                    false,
                                    {{"AI", "RMC", 2, false},
                                     {"II", "GLL", 0, false},
                                     {"II", "RMC", 0, false}}},
                                   {"PacCupStart.txt",
                                    true,
                                    true,
                                    {{"EC", "GGA", 0, true},
                                     {"EC", "RMC", 0, true},
                                     {"EC", "GLL", 0, true},
                                     {"EC", "ZDA", 0, true}}},
                                   {"Race-AIS-Sart-10m.txt",
                                    true,
                                    true,
                                    {{"GP", "GBS", 2, true},
                                     {"GP", "RMC", 2, true},
                                     {"GP", "GLL", 0, true},
                                     {"GP", "RMC", 0, true},
                                     {"II", "GLL", 0, false},
                                     {"GP", "GGA", 0, true}}},
                                   {"Tactics-sample1-12m.txt",
                                    true,
                                    true,
                                    {{"GP", "GLL", 0, true},
                                     {"II", "ZDA", 0, true},
                                     {"II", "GLL", 0, true},
                                     {"GP", "GGA", 3, true},
                                     {"GP", "RMC", 3, true}}},
                                   {"Tactics-sample2-5m.txt",
                                    true,
                                    true,
                                    {{"II", "ZDA", 0, true},
                                     {"II", "GLL", 0, true},
                                     {"GP", "GGA", 1, true},
                                     {"GP", "RMC", 1, true}}}};

    for (const auto& test : tests) {
      wxString testfile =
          wxString(TESTDATA) + wxFileName::GetPathSeparator() + test.filename;
      SCOPED_TRACE("Testing " + test.filename);

      ASSERT_TRUE(record_play_mgr.LoadFile(testfile));
      bool hasValidTimestamps;
      wxString error;
      EXPECT_EQ(record_play_mgr.ScanFileTimestamps(hasValidTimestamps, error),
                test.expectedScanResult)
          << wxString::Format("Failed to scan timestamps: %s", error);
      if (test.expectedScanResult) {
        EXPECT_EQ(error, "");
        EXPECT_TRUE(hasValidTimestamps);
      } else {
        EXPECT_NE(error, "");
      }
      EXPECT_EQ(record_play_mgr.TestHasValidTimestamps(),
                test.expectedValidTimestamps);

      const auto& timeSources = record_play_mgr.TestGetTimeSources();
      ASSERT_EQ(timeSources.size(), test.expectedSources.size());

      for (const auto& expected : test.expectedSources) {
        TimeSource ts;
        ts.talker_id = expected.talker;
        ts.sentence_id = expected.sentence;
        ts.precision = expected.precision;

        auto it = timeSources.find(ts);
        ASSERT_NE(it, timeSources.end())
            << "Missing time source: " << expected.talker << expected.sentence;

        EXPECT_EQ(it->first.precision, expected.precision)
            << "Incorrect precision for " << expected.talker
            << expected.sentence;
        EXPECT_EQ(it->second.is_chronological, expected.chronological)
            << "Incorrect chronological flag for " << expected.talker
            << expected.sentence;
      }
    }
    record_play_mgr.DeInit();
  }
};

TEST(VDRPluginTests, ScanTimestampsBasic) {
  wxLog::SetLogLevel(wxLOG_Error);
  VdrPi plugin(nullptr);
  wxLog::SetLogLevel(wxLOG_Error);
  MockControlGui control_gui;
  TestableRecordPlayMgr record_play_mgr(&plugin, &control_gui);

  TimestampParser parser;

  wxString testdataPath(TESTDATA);
  //  std::cout << "TESTDATA path: " << testdataPath << std::endl;

  // Test loading a valid file
  wxString testfile = wxString(TESTDATA) + wxString("/hakan.txt");
  ASSERT_TRUE(record_play_mgr.LoadFile(testfile)) << "Failed to load test file";

  // Test scanning timestamps
  bool hasValidTimestamps;
  wxString error;
  EXPECT_TRUE(record_play_mgr.ScanFileTimestamps(hasValidTimestamps, error))
      << wxString::Format("Failed to scan timestamps: %s", error);
  EXPECT_TRUE(hasValidTimestamps) << "Failed to scan timestamps";

  // Check we have valid timestamps
  EXPECT_TRUE(record_play_mgr.TestHasValidTimestamps())
      << "Expected HasValidTimestamps to return true";

  // Create expected timestamps in UTC
  wxDateTime expectedFirst;
  parser.ParseIso8601Timestamp("2015-07-20T09:22:11.000Z", &expectedFirst);

  wxDateTime expectedLast;
  parser.ParseIso8601Timestamp("2015-07-20T09:44:06.000Z", &expectedLast);

  // Verify the timestamps
  EXPECT_TRUE(record_play_mgr.GetFirstTimestamp().IsValid())
      << "First timestamp not valid";
  EXPECT_TRUE(record_play_mgr.GetLastTimestamp().IsValid())
      << "Last timestamp not valid";

  if (record_play_mgr.GetFirstTimestamp() != expectedFirst) {
    ADD_FAILURE() << "First timestamp has unexpected value.\n"
                  << "  Actual:   "
                  << record_play_mgr.GetFirstTimestamp().FormatISOCombined()
                  << "\n"
                  << "  Expected: " << expectedFirst.FormatISOCombined();
  }
  if (record_play_mgr.GetLastTimestamp() != expectedLast) {
    ADD_FAILURE() << "Last timestamp has unexpected value.\n"
                  << "  Actual:   "
                  << record_play_mgr.GetLastTimestamp().FormatISOCombined()
                  << "\n"
                  << "  Expected: " << expectedLast.FormatISOCombined();
  }
}

TEST(VDRPluginTests, ProgressFractionNoPlayback) {
  wxLog::SetLogLevel(wxLOG_Error);
  wxLog::SetLogLevel(wxLOG_Error);
  VdrPi plugin(nullptr);
  MockControlGui control_gui;
  RecordPlayMgr record_play_mgr(&plugin, &control_gui);

  // Test loading a valid file
  wxString testfile = wxString(TESTDATA) + wxString("/hakan.txt");
  ASSERT_TRUE(record_play_mgr.LoadFile(testfile)) << "Failed to load test file";
  bool hasValidTimestamps;
  wxString error;
  ASSERT_TRUE(record_play_mgr.ScanFileTimestamps(hasValidTimestamps, error))
      << wxString::Format("Failed to scan timestamps: %s", error);
  EXPECT_TRUE(hasValidTimestamps) << "Failed to scan timestamps";

  // Verify progress fraction starts at 0
  EXPECT_DOUBLE_EQ(record_play_mgr.GetProgressFraction(), 0.0)
      << "Expected progress fraction to be 0.0";

  // Try seeking without starting playback
  ASSERT_TRUE(record_play_mgr.SeekToFraction(0.5))
      << "Failed to seek to fraction 0.5";

  // Verify new progress fraction
  EXPECT_NEAR(record_play_mgr.GetProgressFraction(), 0.5, 0.01)
      << "Expected progress fraction to be near 0.5";
}

TEST(VDRPluginTests, LoadFileErrors) {
  VdrPi plugin(nullptr);
  MockControlGui control_gui;
  RecordPlayMgr record_play_mgr(&plugin, &control_gui);

  wxLog::EnableLogging(false);
  // Test with invalid file
  wxString error;
  EXPECT_FALSE(record_play_mgr.LoadFile("nonexistent.txt", &error))
      << "Should fail with non-existent file";
  bool hasValidTimestamps;
  EXPECT_FALSE(record_play_mgr.ScanFileTimestamps(hasValidTimestamps, error))
      << "ScanFileTimestamps should fail when no file is loaded";
  wxLog::EnableLogging(true);
}

TEST(VDRPluginTests, HandleFileWithoutTimestamps) {
  wxLog::SetLogLevel(wxLOG_Error);
  VdrPi plugin(nullptr);
  MockControlGui control_gui;
  TestableRecordPlayMgr record_play_mgr(&plugin, &control_gui);

  // Test loading a valid file
  wxString testfile = wxString(TESTDATA) + wxString("/no_timestamps.txt");
  ASSERT_TRUE(record_play_mgr.LoadFile(testfile)) << "Failed to load test file";

  // Test scanning timestamps - should succeed but find no timestamps
  bool hasValidTimestamps;
  wxString error;
  EXPECT_TRUE(record_play_mgr.ScanFileTimestamps(hasValidTimestamps, error))
      << wxString::Format("Failed to scan timestamps: %s", error);
  EXPECT_FALSE(hasValidTimestamps) << "File should not have timestamps";
  EXPECT_EQ(error, "") << "Unexpected error message";

  // Should report no valid timestamps
  EXPECT_FALSE(record_play_mgr.TestHasValidTimestamps())
      << "Expected HasValidTimestamps to return false for file without "
         "timestamps";

  // Progress should still work based on line numbers
  EXPECT_DOUBLE_EQ(record_play_mgr.GetProgressFraction(), 0.0)
      << "Expected initial progress to be 0.0";

  // Test seeking to middle of file
  ASSERT_TRUE(record_play_mgr.SeekToFraction(0.5))
      << "Failed to seek to middle of file";

  // Should be at approximately halfway point
  EXPECT_NEAR(record_play_mgr.GetProgressFraction(), 0.5, 0.1)
      << "Expected progress fraction to be near 0.5 after seeking to middle";

  // First/last timestamps should be invalid
  EXPECT_FALSE(record_play_mgr.GetFirstTimestamp().IsValid())
      << "Expected invalid first timestamp";
  EXPECT_FALSE(record_play_mgr.GetLastTimestamp().IsValid())
      << "Expected invalid last timestamp";
  EXPECT_FALSE(record_play_mgr.GetCurrentTimestamp().IsValid())
      << "Expected invalid current timestamp";
}

/** Replay VDR file with raw NMEA sentences that do not contain any timestamp.
 */
TEST(VDRPluginTests, PlaybackNoTimestamps) {
  PlaybackNoTimestampsApp app;
  app.Run();
}

/** Replay a file that contains valid timestamps. */
TEST(VDRPluginTests, PlaybackTimestamps) {
  PlaybackTimestampsApp app;
  app.Run();
}

/**
 * Replay a CSV file that contains valid timestamps and compare with expected.
 */
TEST(VDRPluginTests, PlaybackCsvFile) {
  PlaybackCsvFileApp app;
  app.Run();
}

TEST(VDRPluginTests, CommentLineHandling) {
  wxLog::SetLogLevel(wxLOG_Error);
  VdrPi plugin(nullptr);
  MockControlGui control_gui;
  TestableRecordPlayMgr record_play_mgr(&plugin, &control_gui);

  wxString testfile = wxString(TESTDATA) + wxString("/data_with_comments.txt");
  ASSERT_TRUE(record_play_mgr.LoadFile(testfile)) << "Failed to load test file";

  // Test reading from start
  wxString line = record_play_mgr.TestGetNextNonEmptyLine(true);
  EXPECT_TRUE(line.StartsWith("$GPRMC"))
      << "Expected first NMEA line, got: " << line;

  // Test reading next line
  line = record_play_mgr.TestGetNextNonEmptyLine();
  EXPECT_TRUE(line.StartsWith("$IIRMC"))
      << "Expected second NMEA line, got: " << line;

  line = record_play_mgr.TestGetNextNonEmptyLine();  // EOF.
  EXPECT_EQ(line, "") << "Expected empty line, got: " << line;
}

TEST(VDRPluginTests, PlaybackNonChronologicalTimestamps) {
  PlaybackNonChronologicalTimestampsApp app;
  app.Run();
}

TEST(VDRPluginTests, TestRealRecordings) {
  TestRealRecordingsApp app;
  app.Run();
}
