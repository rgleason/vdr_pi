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

#include <gtest/gtest.h>
#include "vdr_pi_time.h"

class VDRTimeTest : public ::testing::Test {
protected:
  void SetUp() override { parser.Reset(); }

  TimestampParser parser;
};

/** Test parsing of time field. */
TEST_F(VDRTimeTest, TimeFieldParsing) {
  NmeaTimeInfo timeInfo;
  int precision;

  EXPECT_TRUE(parser.ParseTimeField("123519", timeInfo, precision));
  EXPECT_TRUE(timeInfo.has_time);
  EXPECT_EQ(timeInfo.tm.tm_hour, 12);
  EXPECT_EQ(timeInfo.tm.tm_min, 35);
  EXPECT_EQ(timeInfo.tm.tm_sec, 19);
  EXPECT_EQ(timeInfo.millisecond, 0);
  EXPECT_EQ(precision, 0);
}

/** Test parsing of time field with milliseconds. */
TEST_F(VDRTimeTest, TimeFieldParsingWithMs) {
  NmeaTimeInfo timeInfo;
  int precision;

  EXPECT_TRUE(parser.ParseTimeField("123519.123", timeInfo, precision));
  EXPECT_TRUE(timeInfo.has_time);
  EXPECT_EQ(timeInfo.tm.tm_hour, 12);
  EXPECT_EQ(timeInfo.tm.tm_min, 35);
  EXPECT_EQ(timeInfo.tm.tm_sec, 19);
  EXPECT_EQ(timeInfo.millisecond, 123);
  EXPECT_EQ(precision, 3);
}

/** Test RMC sentences with different years. */
TEST_F(VDRTimeTest, RMCSentenceParsing) {
  wxDateTime timestamp;
  int precision;

  // Test a 1994 date
  EXPECT_TRUE(parser.ParseTimestamp(
      "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A",
      timestamp, precision));
  EXPECT_EQ(timestamp.GetYear(), 1994);
  EXPECT_EQ(timestamp.GetMonth(), wxDateTime::Mar);
  EXPECT_EQ(timestamp.GetDay(), 23);

  // Test a valid 2015 date
  EXPECT_TRUE(parser.ParseTimestamp(
      "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230315,003.1,W*6A",
      timestamp, precision));
  EXPECT_EQ(timestamp.GetYear(), 2015);

  // Test a date with invalid month 0.
  EXPECT_FALSE(parser.ParseTimestamp(
      "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230015,003.1,W*6A",
      timestamp, precision));

  // Test a date with invalid month 13.
  EXPECT_FALSE(parser.ParseTimestamp(
      "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,231315,003.1,W*6A",
      timestamp, precision));

  // Test a date with invalid day-of-month 0.
  EXPECT_FALSE(parser.ParseTimestamp(
      "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,000015,003.1,W*6A",
      timestamp, precision));

  // Test a date with invalid day-of-month 32.
  EXPECT_FALSE(parser.ParseTimestamp(
      "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,320015,003.1,W*6A",
      timestamp, precision));

  // Test a date with invalid February 29 2015.
  EXPECT_FALSE(parser.ParseTimestamp(
      "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,290215,003.1,W*6A",
      timestamp, precision));

  // Test year at the boundary (1969/2069)
  EXPECT_TRUE(parser.ParseTimestamp(
      "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230369,003.1,W*6A",
      timestamp, precision));
  EXPECT_EQ(timestamp.GetYear(), 2069);

  EXPECT_TRUE(parser.ParseTimestamp(
      "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230370,003.1,W*6A",
      timestamp, precision));
  EXPECT_EQ(timestamp.GetYear(), 1970);
  EXPECT_EQ(timestamp.GetMonth(), wxDateTime::Mar);
  EXPECT_EQ(timestamp.GetDay(), 23);
  EXPECT_EQ(timestamp.GetHour(), 12);
  EXPECT_EQ(timestamp.GetMinute(), 35);
  EXPECT_EQ(timestamp.GetSecond(), 19);
  EXPECT_EQ(timestamp.GetMillisecond(), 0);
  EXPECT_EQ(precision, 0);

  EXPECT_TRUE(
      parser.ParseTimestamp("$GPRMC,123519.234,A,4807.038,N,01131.000,E,022.4,"
                            "084.4,230370,003.1,W*6A",
                            timestamp, precision));
  EXPECT_EQ(timestamp.GetYear(), 1970);
  EXPECT_EQ(timestamp.GetMonth(), wxDateTime::Mar);
  EXPECT_EQ(timestamp.GetDay(), 23);
  EXPECT_EQ(timestamp.GetHour(), 12);
  EXPECT_EQ(timestamp.GetMinute(), 35);
  EXPECT_EQ(timestamp.GetSecond(), 19);
  EXPECT_EQ(timestamp.GetMillisecond(), 234);
  EXPECT_EQ(precision, 3);
}

/** Test invalid inputs. */
TEST_F(VDRTimeTest, InvalidInputs) {
  NmeaTimeInfo timeInfo;
  int precision;

  // Invalid hour
  EXPECT_FALSE(parser.ParseTimeField("243519", timeInfo, precision));

  // Invalid length
  EXPECT_FALSE(parser.ParseTimeField("12345", timeInfo, precision));

  // Invalid format
  EXPECT_FALSE(parser.ParseTimeField("12:35:19", timeInfo, precision));
}

/** Test ZDA sentence parsing. */
TEST_F(VDRTimeTest, ZDAParsing) {
  wxDateTime timestamp;
  int precision;

  // Test ZDA with full date/time
  EXPECT_TRUE(parser.ParseTimestamp("$GPZDA,123519,23,03,1994,00,00*6A",
                                    timestamp, precision));
  EXPECT_EQ(timestamp.GetHour(), 12);
  EXPECT_EQ(timestamp.GetMinute(), 35);
  EXPECT_EQ(timestamp.GetSecond(), 19);
  EXPECT_EQ(timestamp.GetDay(), 23);
  EXPECT_EQ(timestamp.GetMonth(), wxDateTime::Mar);
  EXPECT_EQ(timestamp.GetYear(), 1994);

  // Test ZDA with missing fields
  EXPECT_FALSE(
      parser.ParseTimestamp("$GPZDA,123519,23,03*6A", timestamp, precision));
}

/** Test GGA/GBS/GLL sentence parsing (time only). */
TEST_F(VDRTimeTest, GxxParsing) {
  wxDateTime timestamp;
  int precision;

  // First set cached date with RMC
  EXPECT_TRUE(parser.ParseTimestamp(
      "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A",
      timestamp, precision));

  // Test GGA (time in field 1)
  EXPECT_TRUE(parser.ParseTimestamp(
      "$GPGGA,123520,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47",
      timestamp, precision));
  EXPECT_EQ(timestamp.GetHour(), 12);
  EXPECT_EQ(timestamp.GetMinute(), 35);
  EXPECT_EQ(timestamp.GetSecond(), 20);
  EXPECT_EQ(timestamp.GetDay(), 23);
  EXPECT_EQ(timestamp.GetMonth(), wxDateTime::Mar);
  EXPECT_EQ(timestamp.GetYear(), 1994);

  // Test GLL (time in field 5)
  EXPECT_TRUE(parser.ParseTimestamp("$GPGLL,4916.45,N,12311.12,W,123521,A*31",
                                    timestamp, precision));
  EXPECT_EQ(timestamp.GetHour(), 12);
  EXPECT_EQ(timestamp.GetMinute(), 35);
  EXPECT_EQ(timestamp.GetSecond(), 21);

  // Without cached date, GGA should fail
  parser.Reset();  // Clear cached date
  EXPECT_FALSE(parser.ParseTimestamp(
      "$GPGGA,123520,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47",
      timestamp, precision));
}

/** Parse sentence that does not match the desired primary time source. */
TEST_F(VDRTimeTest, ParseSecondaryTime) {
  wxDateTime timestamp;
  int precision;

  // Set desired primary time source.
  parser.SetPrimaryTimeSource("GP", "RMC", 3);

  // Test sentence with different talker ID.
  EXPECT_FALSE(parser.ParseTimestamp(
      "$GNRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A",
      timestamp, precision));

  // Test sentence with expected talker ID but different message type.
  EXPECT_FALSE(parser.ParseTimestamp(
      "$GPGSA,A,3,04,05,,09,12,,,24,,,,,2.5,1.3,2.1*39", timestamp, precision));

  // Test sentence with expected talker ID and message type, but different
  // precision. Expected precision is 3, but actual precision is 0.
  EXPECT_FALSE(parser.ParseTimestamp(
      "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A",
      timestamp, precision));

  // Test sentence with expected talker ID, message type, and precision.
  EXPECT_TRUE(
      parser.ParseTimestamp("$GPRMC,123519.789,A,4807.038,N,01131.000,E,022.4,"
                            "084.4,230394,003.1,W*6A7",
                            timestamp, precision));
  EXPECT_EQ(timestamp.GetHour(), 12);
  EXPECT_EQ(timestamp.GetMinute(), 35);
  EXPECT_EQ(timestamp.GetSecond(), 19);
  EXPECT_EQ(timestamp.GetMillisecond(), 789);
  EXPECT_EQ(timestamp.GetDay(), 23);
  EXPECT_EQ(timestamp.GetMonth(), wxDateTime::Mar);
  EXPECT_EQ(timestamp.GetYear(), 1994);
  EXPECT_EQ(precision, 3);
}

/** Test GGA/GBS/GLL sentence parsing with different date scenarios. */
TEST_F(VDRTimeTest, GxxParsingDateScenarios) {
  wxDateTime timestamp;
  int precision;

  // Scenario 1: No prior RMC or ZDA
  EXPECT_FALSE(parser.ParseTimestamp(
      "$GPGGA,123520,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47",
      timestamp, precision));
  EXPECT_FALSE(parser.ParseTimestamp("$GPGBS,123520,3.0,2.9,5.3,11,,,*6B",
                                     timestamp, precision));
  EXPECT_FALSE(parser.ParseTimestamp("$GPGLL,4916.45,N,12311.12,W,123520,A*31",
                                     timestamp, precision));

  // Scenario 2: Prior ZDA
  EXPECT_TRUE(parser.ParseTimestamp("$GPZDA,123519,23,03,1994,00,00*6A",
                                    timestamp, precision));

  // Test GGA after ZDA
  EXPECT_TRUE(parser.ParseTimestamp(
      "$GPGGA,123520,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47",
      timestamp, precision));
  EXPECT_EQ(timestamp.GetHour(), 12);
  EXPECT_EQ(timestamp.GetMinute(), 35);
  EXPECT_EQ(timestamp.GetSecond(), 20);
  EXPECT_EQ(timestamp.GetDay(), 23);
  EXPECT_EQ(timestamp.GetMonth(), wxDateTime::Mar);
  EXPECT_EQ(timestamp.GetYear(), 1994);

  // Test GBS after ZDA
  EXPECT_TRUE(parser.ParseTimestamp("$GPGBS,123521,3.0,2.9,5.3,11,,,*6B",
                                    timestamp, precision));
  EXPECT_EQ(timestamp.GetHour(), 12);
  EXPECT_EQ(timestamp.GetMinute(), 35);
  EXPECT_EQ(timestamp.GetSecond(), 21);

  // Test GLL after ZDA
  EXPECT_TRUE(parser.ParseTimestamp("$GPGLL,4916.45,N,12311.12,W,123522,A*31",
                                    timestamp, precision));
  EXPECT_EQ(timestamp.GetHour(), 12);
  EXPECT_EQ(timestamp.GetMinute(), 35);
  EXPECT_EQ(timestamp.GetSecond(), 22);
}

/** Test CSV line parsing. */
TEST_F(VDRTimeTest, CSVParsingISO8601) {
  wxDateTime timestamp;
  wxString msg =
      "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A";
  wxString nmea;
  bool success = parser.ParseCsvLineTimestamp(
      wxString::Format("2024-01-30T12:34:56.123Z,\"%s\"", msg), 0, 1, &nmea,
      &timestamp);
  EXPECT_TRUE(success);
  EXPECT_EQ(nmea, msg);
  EXPECT_EQ(timestamp.GetYear(), 2024);
  EXPECT_EQ(timestamp.GetMonth(), wxDateTime::Jan);
  EXPECT_EQ(timestamp.GetDay(), 30);
  EXPECT_EQ(timestamp.GetHour(), 12);
  EXPECT_EQ(timestamp.GetMinute(), 34);
  EXPECT_EQ(timestamp.GetSecond(), 56);
  EXPECT_EQ(timestamp.GetMillisecond(), 123);

  success = parser.ParseCsvLineTimestamp(
      wxString::Format("2024-01-30T12:34:56Z,\"%s\"", msg), 0, 1, &nmea,
      &timestamp);
  EXPECT_TRUE(success);
  EXPECT_EQ(nmea, msg);
  EXPECT_EQ(timestamp.GetYear(), 2024);
  EXPECT_EQ(timestamp.GetMonth(), wxDateTime::Jan);
  EXPECT_EQ(timestamp.GetDay(), 30);
  EXPECT_EQ(timestamp.GetHour(), 12);
  EXPECT_EQ(timestamp.GetMinute(), 34);
  EXPECT_EQ(timestamp.GetSecond(), 56);
  EXPECT_EQ(timestamp.GetMillisecond(), 0);
}

TEST(TimestampParserTests, ParseISO8601) {
  TimestampParser parser;
  for (int i = 0; i < 24; i++) {
    wxDateTime dt;
    // Without milliseconds
    wxString timeStr = wxString::Format("2024-02-03T%02d:22:11Z", i);
    EXPECT_TRUE(parser.ParseIso8601Timestamp(timeStr, &dt));
    EXPECT_EQ(dt.GetYear(), 2024);
    EXPECT_EQ(dt.GetMonth(), wxDateTime::Feb);
    EXPECT_EQ(dt.GetDay(), 3);
    EXPECT_EQ(dt.GetHour(), i);
    EXPECT_EQ(dt.GetMinute(), 22);
    EXPECT_EQ(dt.GetSecond(), 11);
    EXPECT_EQ(dt.GetValue() / 1000,
              1706916131 + 3600 * i);  // Seconds since epoch.
  }
  {
    wxDateTime dt;
    // With milliseconds
    EXPECT_TRUE(parser.ParseIso8601Timestamp("2024-02-03T09:22:11.123Z", &dt));
    EXPECT_EQ(dt.GetYear(), 2024);
    EXPECT_EQ(dt.GetMonth(), wxDateTime::Feb);
    EXPECT_EQ(dt.GetDay(), 3);
    EXPECT_EQ(dt.GetHour(), 9);
    EXPECT_EQ(dt.GetMinute(), 22);
    EXPECT_EQ(dt.GetSecond(), 11);
    EXPECT_EQ(dt.GetMillisecond(), 123);
    EXPECT_EQ(dt.GetValue(), 1706948531123);  // Milliseconds since epoch.
  }
  {
    wxDateTime dt;
    // Invalid formats should fail
    EXPECT_FALSE(
        parser.ParseIso8601Timestamp("2024-02-03", &dt));  // Missing time
    EXPECT_FALSE(
        parser.ParseIso8601Timestamp("2024-02-03T09:22:11", &dt));  // Missing Z
    EXPECT_FALSE(parser.ParseIso8601Timestamp("2024-02-03T24:00:00Z",
                                              &dt));  // Invalid hour
    EXPECT_FALSE(parser.ParseIso8601Timestamp("2024-02-03T09:22:11.1234Z",
                                              &dt));  // Too many ms digits
  }
}
