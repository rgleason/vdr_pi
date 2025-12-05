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

#include "wx/wxprec.h"

#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif  // precompiled headers
#include "wx/tokenzr.h"

#include <gtest/gtest.h>
#include "vdr_pi_time.h"
#include "vdr_pi.h"
#include "mock_plugin_api.h"

TEST(VDRPluginTests, ScanTimestampsBasic) {
  vdr_pi plugin(nullptr);
  TimestampParser parser;

  wxString testdataPath(TESTDATA);
  std::cout << "TESTDATA path: " << testdataPath << std::endl;

  // Test loading a valid file
  wxString testfile = wxString(TESTDATA) + wxString("/hakan.txt");
  ASSERT_TRUE(plugin.LoadFile(testfile)) << "Failed to load test file";

  // Test scanning timestamps
  bool hasValidTimestamps;
  wxString error;
  EXPECT_TRUE(plugin.ScanFileTimestamps(hasValidTimestamps, error))
      << wxString::Format("Failed to scan timestamps: %s", error);
  EXPECT_TRUE(hasValidTimestamps) << "Failed to scan timestamps";

  // Check we have valid timestamps
  EXPECT_TRUE(plugin.HasValidTimestamps())
      << "Expected HasValidTimestamps to return true";

  // Create expected timestamps in UTC
  wxDateTime expectedFirst;
  parser.ParseIso8601Timestamp("2015-07-20T09:22:11.000Z", &expectedFirst);

  wxDateTime expectedLast;
  parser.ParseIso8601Timestamp("2015-07-20T09:44:06.000Z", &expectedLast);

  // Verify the timestamps
  EXPECT_TRUE(plugin.GetFirstTimestamp().IsValid())
      << "First timestamp not valid";
  EXPECT_TRUE(plugin.GetLastTimestamp().IsValid())
      << "Last timestamp not valid";

  if (plugin.GetFirstTimestamp() != expectedFirst) {
    ADD_FAILURE() << "First timestamp has unexpected value.\n"
                  << "  Actual:   "
                  << plugin.GetFirstTimestamp().FormatISOCombined() << "\n"
                  << "  Expected: " << expectedFirst.FormatISOCombined();
  }
  if (plugin.GetLastTimestamp() != expectedLast) {
    ADD_FAILURE() << "Last timestamp has unexpected value.\n"
                  << "  Actual:   "
                  << plugin.GetLastTimestamp().FormatISOCombined() << "\n"
                  << "  Expected: " << expectedLast.FormatISOCombined();
  }
}

TEST(VDRPluginTests, ProgressFractionNoPlayback) {
  vdr_pi plugin(nullptr);

  // Test loading a valid file
  wxString testfile = wxString(TESTDATA) + wxString("/hakan.txt");
  ASSERT_TRUE(plugin.LoadFile(testfile)) << "Failed to load test file";
  bool hasValidTimestamps;
  wxString error;
  ASSERT_TRUE(plugin.ScanFileTimestamps(hasValidTimestamps, error))
      << wxString::Format("Failed to scan timestamps: %s", error);
  EXPECT_TRUE(hasValidTimestamps) << "Failed to scan timestamps";

  // Verify progress fraction starts at 0
  EXPECT_DOUBLE_EQ(plugin.GetProgressFraction(), 0.0)
      << "Expected progress fraction to be 0.0";

  // Try seeking without starting playback
  ASSERT_TRUE(plugin.SeekToFraction(0.5)) << "Failed to seek to fraction 0.5";

  // Verify new progress fraction
  EXPECT_NEAR(plugin.GetProgressFraction(), 0.5, 0.01)
      << "Expected progress fraction to be near 0.5";
}

TEST(VDRPluginTests, LoadFileErrors) {
  vdr_pi plugin(nullptr);

  // Test with invalid file
  wxString error;
  EXPECT_FALSE(plugin.LoadFile("nonexistent.txt", &error))
      << "Should fail with non-existent file";
  bool hasValidTimestamps;
  EXPECT_FALSE(plugin.ScanFileTimestamps(hasValidTimestamps, error))
      << "ScanFileTimestamps should fail when no file is loaded";
}

TEST(VDRPluginTests, HandleFileWithoutTimestamps) {
  vdr_pi plugin(nullptr);

  // Test loading a valid file
  wxString testfile = wxString(TESTDATA) + wxString("/no_timestamps.txt");
  ASSERT_TRUE(plugin.LoadFile(testfile)) << "Failed to load test file";

  // Test scanning timestamps - should succeed but find no timestamps
  bool hasValidTimestamps;
  wxString error;
  EXPECT_TRUE(plugin.ScanFileTimestamps(hasValidTimestamps, error))
      << wxString::Format("Failed to scan timestamps: %s", error);
  EXPECT_FALSE(hasValidTimestamps) << "File should not have timestamps";
  EXPECT_EQ(error, "") << "Unexpected error message";

  // Should report no valid timestamps
  EXPECT_FALSE(plugin.HasValidTimestamps())
      << "Expected HasValidTimestamps to return false for file without "
         "timestamps";

  // Progress should still work based on line numbers
  EXPECT_DOUBLE_EQ(plugin.GetProgressFraction(), 0.0)
      << "Expected initial progress to be 0.0";

  // Test seeking to middle of file
  ASSERT_TRUE(plugin.SeekToFraction(0.5)) << "Failed to seek to middle of file";

  // Should be at approximately halfway point
  EXPECT_NEAR(plugin.GetProgressFraction(), 0.5, 0.1)
      << "Expected progress fraction to be near 0.5 after seeking to middle";

  // First/last timestamps should be invalid
  EXPECT_FALSE(plugin.GetFirstTimestamp().IsValid())
      << "Expected invalid first timestamp";
  EXPECT_FALSE(plugin.GetLastTimestamp().IsValid())
      << "Expected invalid last timestamp";
  EXPECT_FALSE(plugin.GetCurrentTimestamp().IsValid())
      << "Expected invalid current timestamp";
}

/** Replay VDR file with raw NMEA sentences that do not contain any timestamp.
 */
TEST(VDRPluginTests, PlaybackNoTimestamps) {
  vdr_pi plugin(nullptr);

  // Initialize the plugin properly
  plugin.Init();

  // Clear any previous mock state
  ClearNMEASentences();

  // Load test file without timestamps
  wxString testfile = wxString(TESTDATA) + wxString("/no_timestamps.txt");
  ASSERT_TRUE(plugin.LoadFile(testfile)) << "Failed to load test file";
  bool hasValidTimestamps;
  wxString error;
  ASSERT_TRUE(plugin.ScanFileTimestamps(hasValidTimestamps, error))
      << wxString::Format("Failed to scan timestamps: %s", error);
  EXPECT_FALSE(hasValidTimestamps) << "File should not have timestamps";
  EXPECT_EQ(error, "") << "Unexpected error message";

  // Verify no timestamps found
  EXPECT_FALSE(plugin.HasValidTimestamps())
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
  plugin.StartPlayback();

  // Give some time for playback to process
  wxMilliSleep(500);

  wxLogMessage("PlaybackNoTimestamps test: Stopping playback");

  // Stop playback
  plugin.StopPlayback();

  // Wait a bit more to ensure any pending timer events are processed.
  wxMilliSleep(100);

  // Flush buffer to ensure PushNMEABuffer() has been called for each message.
  plugin.FlushSentenceBuffer();

  // Get sentences sent to NMEA buffer, obtained from mock interface.
  const auto& sentences = GetNMEASentences();

  // Verify number of sentences.
  EXPECT_EQ(sentences.size(), expected_sentences.size())
      << "Expected " << expected_sentences.size() << " sentences but got "
      << sentences.size();

  wxLogMessage("Expected sentences: %d", expected_sentences.size());
  // Verify each sentence
  for (size_t i = 0; i < std::min(sentences.size(), expected_sentences.size());
       i++) {
    EXPECT_EQ(sentences[i], expected_sentences[i].Strip(wxString::both))
        << "Mismatch at sentence " << i;
  }

  // Clean up
  wxLogMessage("PlaybackNoTimestamps test complete. Calling DeInit()");
  plugin.DeInit();
  wxLogMessage("PlaybackNoTimestamps test complete");
}

/** Replay a file that contains valid timestamps. */
TEST(VDRPluginTests, PlaybackTimestamps) {
  vdr_pi plugin(nullptr);

  // Initialize the plugin properly
  plugin.Init();

  // Clear any previous mock state
  ClearNMEASentences();

  // Load test file without timestamps
  wxString testfile = wxString(TESTDATA) + wxString("/with_timestamps.txt");
  ASSERT_TRUE(plugin.LoadFile(testfile)) << "Failed to load test file";
  bool hasValidTimestamps;
  wxString error;
  ASSERT_TRUE(plugin.ScanFileTimestamps(hasValidTimestamps, error))
      << wxString::Format("Failed to scan timestamps: %s", error);
  EXPECT_TRUE(hasValidTimestamps) << "Failed to scan timestamps";
  EXPECT_EQ(error, "") << "Unexpected error message";

  // Verify timestamps found
  EXPECT_TRUE(plugin.HasValidTimestamps())
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
  plugin.StartPlayback();

  // Give some time for playback to process
  wxMilliSleep(500);

  // Stop playback
  plugin.StopPlayback();

  // Wait a bit more to ensure any pending timer events are processed.
  wxMilliSleep(100);

  // Flush buffer to ensure PushNMEABuffer() has been called for each message.
  plugin.FlushSentenceBuffer();

  // Get sentences sent to NMEA buffer, obtained from mock interface.
  const auto& sentences = GetNMEASentences();

  // Verify number of sentences.
  EXPECT_EQ(sentences.size(), expected_sentences.size())
      << "Expected " << expected_sentences.size() << " sentences but got "
      << sentences.size();

  // Verify each sentence
  for (size_t i = 0; i < std::min(sentences.size(), expected_sentences.size());
       i++) {
    EXPECT_EQ(sentences[i], expected_sentences[i].Strip(wxString::both))
        << "Mismatch at sentence " << i;
  }

  // Clean up
  plugin.DeInit();
}

std::string MakeReadable(const wxString& str) {
  return "\"" + str.ToStdString() + "\"";
}

wxArrayString ParseCSVLine(const wxString& line) {
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

/**
 * Replay a CSV file that contains valid timestamps and compare with expected.
 */
TEST(VDRPluginTests, PlaybackCsvFile) {
  vdr_pi plugin(nullptr);

  // Initialize the plugin properly
  plugin.Init();

  // Clear any previous mock state
  ClearNMEASentences();

  // Load test CSV file
  wxString testfile = wxString(TESTDATA) + wxString("/test_recording.csv");
  ASSERT_TRUE(plugin.LoadFile(testfile)) << "Failed to load test file";
  bool hasValidTimestamps;
  wxString error;
  ASSERT_TRUE(plugin.ScanFileTimestamps(hasValidTimestamps, error))
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
  plugin.StartPlayback();

  // Give some time for playback to process
  wxMilliSleep(500);

  // Stop playback
  plugin.StopPlayback();

  // Wait a bit more to ensure any pending timer events are processed.
  wxMilliSleep(100);

  // Flush buffer to ensure PushNMEABuffer() has been called for each message.
  plugin.FlushSentenceBuffer();

  // Get sentences sent to NMEA buffer, obtained from mock interface.
  const auto& sentences = GetNMEASentences();

  // Verify number of sentences.
  EXPECT_EQ(sentences.size(), expected_sentences.size())
      << "Expected " << expected_sentences.size() << " sentences but got "
      << sentences.size();

  // Verify each sentence
  for (size_t i = 0; i < std::min(sentences.size(), expected_sentences.size());
       i++) {
    wxString sentence_stripped = sentences[i];
    sentence_stripped.Replace("\r\n", "");  // Remove CRLF for comparison
    EXPECT_EQ(MakeReadable(sentence_stripped),
              MakeReadable(expected_sentences[i]))
        << "Mismatch at sentence " << i;
  }

  // Clean up
  plugin.DeInit();
}

TEST(VDRPluginTests, CommentLineHandling) {
  vdr_pi plugin(nullptr);

  wxString testfile = wxString(TESTDATA) + wxString("/data_with_comments.txt");
  ASSERT_TRUE(plugin.LoadFile(testfile)) << "Failed to load test file";

  // Test reading from start
  wxString line = plugin.GetNextNonEmptyLine(true);
  EXPECT_TRUE(line.StartsWith("$GPRMC"))
      << "Expected first NMEA line, got: " << line;

  // Test reading next line
  line = plugin.GetNextNonEmptyLine();
  EXPECT_TRUE(line.StartsWith("$IIRMC"))
      << "Expected second NMEA line, got: " << line;

  line = plugin.GetNextNonEmptyLine();  // EOF.
  EXPECT_EQ(line, "") << "Expected empty line, got: " << line;
}

TEST(VDRPluginTests, PlaybackNonChronologicalTimestamps) {
  vdr_pi plugin(nullptr);

  // Initialize the plugin properly
  plugin.Init();

  wxString testfile = wxString(TESTDATA) + wxString("/not_chronological.txt");
  ASSERT_TRUE(plugin.LoadFile(testfile)) << "Failed to load test file";
  bool hasValidTimestamps;
  wxString error;
  ASSERT_TRUE(plugin.ScanFileTimestamps(hasValidTimestamps, error))
      << wxString::Format("Failed to scan timestamps: %s", error);
  EXPECT_TRUE(hasValidTimestamps) << "Failed to scan timestamps";
  EXPECT_EQ(error, "") << "Unexpected error message";

  // Verify no timestamps were found, because none of the time sources are
  // in chronological order.
  EXPECT_FALSE(plugin.HasValidTimestamps())
      << "Expected HasValidTimestamps to return true";

  // Clean up
  plugin.DeInit();
}

TEST(VDRPluginTests, TestRealRecordings) {
  vdr_pi plugin(nullptr);
  plugin.Init();

  struct TimeSourceExpectation {
    std::string talker;    //!< talker ID.
    std::string sentence;  //!< sentence type.
    int precision;         //!< time precision (number of decimal places).
    bool chronological;    //!< whether timestamps are in chronological order.
  };

  struct TestCase {
    std::string filename;          //!< name of the test file.
    bool expectedScanResult;       //!< expected result of ScanFileTimestamps().
    bool expectedValidTimestamps;  //! expected result of HasValidTimestamps().
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
                                 {
                                     "NavMonPC-Hartmut.txt",
                                     false,
                                     false,
                                     {}  // No valid time sources expected
                                 },
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

    ASSERT_TRUE(plugin.LoadFile(testfile));
    bool hasValidTimestamps;
    wxString error;
    EXPECT_EQ(plugin.ScanFileTimestamps(hasValidTimestamps, error),
              test.expectedScanResult)
        << wxString::Format("Failed to scan timestamps: %s", error);
    if (test.expectedScanResult) {
      EXPECT_EQ(error, "");
      EXPECT_TRUE(hasValidTimestamps);
    } else {
      EXPECT_NE(error, "");
    }
    EXPECT_EQ(plugin.HasValidTimestamps(), test.expectedValidTimestamps);

    const auto& timeSources = plugin.GetTimeSources();
    ASSERT_EQ(timeSources.size(), test.expectedSources.size());

    for (const auto& expected : test.expectedSources) {
      TimeSource ts;
      ts.talkerId = expected.talker;
      ts.sentenceId = expected.sentence;
      ts.precision = expected.precision;

      auto it = timeSources.find(ts);
      ASSERT_NE(it, timeSources.end())
          << "Missing time source: " << expected.talker << expected.sentence;

      EXPECT_EQ(it->first.precision, expected.precision)
          << "Incorrect precision for " << expected.talker << expected.sentence;
      EXPECT_EQ(it->second.isChronological, expected.chronological)
          << "Incorrect chronological flag for " << expected.talker
          << expected.sentence;
    }
  }
  plugin.DeInit();
}