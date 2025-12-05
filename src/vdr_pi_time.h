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

#ifndef VDR_PI_TIME_H_
#define VDR_PI_TIME_H_

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include <wx/datetime.h>
#include <wx/string.h>

struct NmeaTimeInfo {
  bool has_date;  // Whether date information is available
  bool has_time;  // Whether time information is available
  struct tm tm;
  int millisecond;

  NmeaTimeInfo() : has_date(false), has_time(false), tm{0}, millisecond(0) {}

  [[nodiscard]] bool IsComplete() const { return has_date && has_time; }
};

/**
 * Represents a unique source of time information from NMEA sentences or CSV
 * entry. This is used to track the time source for each NMEA sentence type.
 */
struct TimeSource {
  wxString talker_id;    // GP, GN, etc.
  wxString sentence_id;  // RMC, ZDA, etc.
  int precision;         // Millisecond precision (0, 1, 2, or 3 digits)

  TimeSource() : precision(0) {}

  TimeSource(const wxString& talker, const wxString& sentence, int precision)
      : talker_id(talker), sentence_id(sentence), precision(precision) {}

  bool operator==(const TimeSource& other) const {
    return talker_id == other.talker_id && sentence_id == other.sentence_id &&
           precision == other.precision;
  }
};

/**
 * Parses NMEA 0183 timestamps from various sentence types.
 */
class TimestampParser {
public:
  TimestampParser()
      : m_last_valid_year(0), m_last_valid_month(0), m_last_valid_day(0) {}
  /**
   * Parse a timestamp from a NMEA 0183 sentence.
   *
   * This method supports parsing timestamps from RMC, ZDA, and other sentence
   * types.
   *
   * @param sentence NMEA 0183 sentence to parse.
   * @param timestamp Output timestamp.
   * @param precision Output millisecond precision.
   * @return True if sentence contains a timestamp and that timestamp was
   * successfully parsed. The time is returned in UTC.
   */
  bool ParseTimestamp(const wxString& sentence, wxDateTime& timestamp,
                      int& precision);

  /**
   * Parse a timestamp from an ISO 8601 formatted string in UTC format.
   *
   * @param timeStr ISO 8601 timestamp string.
   * @param timestamp Output timestamp in UTC.
   * @return True if the timestamp was successfully parsed.
   */
  static bool ParseIso8601Timestamp(const wxString& timeStr,
                                    wxDateTime* timestamp);

  // Reset the cached date state
  void Reset();

  /** Parses HHMMSS or HHMMSS.sss format. */
  static bool ParseTimeField(const wxString& timeStr, NmeaTimeInfo& info,
                             int& precision);

  /** Set the desired primary time source. */
  void SetPrimaryTimeSource(const wxString& talkerId, const wxString& msgType,
                            int precision);
  /** Disable the desired primary time source, parse all sentences containing
   * timestamps. */
  void DisablePrimaryTimeSource();

  /**
   * Parse a timestamp from a CSV line.
   *
   * @param line CSV line to parse.
   * @param timestamp_idx Index of the timestamp field.
   * @param message_idx Index of the message field.
   * @param message Output message field.
   * @param timestamp Output timestamp.
   * @return True if the timestamp was successfully parsed.
   */
  static bool ParseCsvLineTimestamp(const wxString& line,
                                    unsigned int timestamp_idx,
                                    unsigned int message_idx, wxString* message,
                                    wxDateTime* timestamp);

private:
  // Cache the last valid date seen from NMEA sentences (RMC, ZDA...)
  int m_last_valid_year;
  int m_last_valid_month;
  int m_last_valid_day;

  /**
   * When true, timestamps are parsed only if they match the primary time source
   * (talker ID, message type and time precision).
   */
  bool m_useOnlyPrimarySource{false};
  /** Primary time source used when m_useOnlyPrimarySource is true. */
  TimeSource m_primary_source;

  // Parses DDMMYY format (used by RMC)
  bool ParseRMCDate(const wxString& dateStr, NmeaTimeInfo& info);

  // Validates date components and sets hasDate flag
  bool ValidateAndSetDate(NmeaTimeInfo& info);

  // Applies cached date if available
  void ApplyCachedDate(NmeaTimeInfo& info) const;
};

/**
 * Represents the details of a time source, including start and end times.
 */
struct TimeSourceDetails {
  wxDateTime start_time;
  wxDateTime current_time;
  wxDateTime end_time;
  /** Whether the time source is chronological or not. */
  bool is_chronological;

  TimeSourceDetails() : is_chronological(true) {}
};

/** Custom hash function for TimeSource to use in unordered_map. */
struct TimeSourceHash {
  size_t operator()(const TimeSource& ts) const {
    std::string talker_id(ts.talker_id.ToStdString());
    std::string sentence_id(ts.sentence_id.ToStdString());
    size_t h1 = std::hash<std::string>{}(talker_id);
    size_t h2 = std::hash<std::string>{}(sentence_id);
    size_t h3 = std::hash<int>{}(ts.precision);
    return h1 ^ (h2 << 1) ^ (h3 << 2);
  }
};

#endif  // VDR_PI_TIME_H_
