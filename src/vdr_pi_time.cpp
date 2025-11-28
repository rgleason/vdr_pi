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

#include <ctime>

#include "wx/wxprec.h"

#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif

#include "wx/tokenzr.h"

#include "vdr_pi_time.h"

bool TimestampParser::ParseTimeField(const wxString& timeStr,
                                     NmeaTimeInfo& info, int& precision) {
  if (timeStr.length() < 6) return false;

  // Parse base time components
  info.tm.tm_hour = wxAtoi(timeStr.Mid(0, 2));
  info.tm.tm_min = wxAtoi(timeStr.Mid(2, 2));
  info.tm.tm_sec = wxAtoi(timeStr.Mid(4, 2));

  // Parse optional milliseconds
  info.millisecond = 0;
  precision = 0;               // Default precision
  if (timeStr.length() > 7) {  // Has decimal point and subseconds
    // Check if we actually have a decimal point
    if (timeStr[6] != '.') return false;

    // Get the subseconds string
    wxString subsecStr = timeStr.Mid(7);
    if (subsecStr.empty()) return false;

    // Calculate precision from subseconds length
    precision = subsecStr.length();

    // Convert to milliseconds
    double subseconds = wxAtof("0." + subsecStr);
    info.millisecond = static_cast<int>(subseconds * 1000);
  }

  // Validate time components
  if (info.tm.tm_hour < 0 || info.tm.tm_hour > 23 || info.tm.tm_min < 0 ||
      info.tm.tm_min > 59 || info.tm.tm_sec < 0 || info.tm.tm_sec > 59 ||
      info.millisecond < 0 || info.millisecond >= 1000) {
    return false;
  }

  info.has_time = true;
  return true;
}

bool TimestampParser::ParseRMCDate(const wxString& dateStr,
                                   NmeaTimeInfo& info) {
  if (dateStr.length() < 6) return false;

  info.tm.tm_mday = wxAtoi(dateStr.Mid(0, 2));
  info.tm.tm_mon = wxAtoi(dateStr.Mid(2, 2));
  int twoDigitYear = wxAtoi(dateStr.Mid(4, 2));
  // Use sliding window: years 00-69 are 2000-2069, years 70-99 are 1970-1999
  info.tm.tm_year =
      ((twoDigitYear >= 70) ? 1900 + twoDigitYear : 2000 + twoDigitYear) - 1900;

  return ValidateAndSetDate(info);
}

bool TimestampParser::ValidateAndSetDate(NmeaTimeInfo& info) {
  if (info.tm.tm_mon < 1 || info.tm.tm_mon > 12 || info.tm.tm_mday < 1 ||
      info.tm.tm_mday > 31 || info.tm.tm_year < 0) {
    return false;
  }

  // Cache valid date components for sentences with only time
  m_last_valid_year = info.tm.tm_year + 1900;
  m_last_valid_month = info.tm.tm_mon;
  m_last_valid_day = info.tm.tm_mday;

  info.has_date = true;
  return true;
}

// Applies cached date if available
void TimestampParser::ApplyCachedDate(NmeaTimeInfo& info) const {
  if (m_last_valid_year > 0) {
    info.tm.tm_year = m_last_valid_year - 1900;
    info.tm.tm_mon = m_last_valid_month;
    info.tm.tm_mday = m_last_valid_day;
    info.has_date = true;
  }
}

bool TimestampParser::ParseIso8601Timestamp(const wxString& timeStr,
                                            wxDateTime* timestamp) {
  // Expected format: YYYY-MM-DDThh:mm:ss.sssZ

  // Parse the main date/time part using ISO format
  bool ret = timestamp->ParseFormat(timeStr, "%Y-%m-%dT%H:%M:%S.%l%z");
  if (!ret) {
    // Try without milliseconds
    timestamp->SetMillisecond(0);
    ret = timestamp->ParseFormat(timeStr, "%Y-%m-%dT%H:%M:%S%z");
  }
  timestamp->MakeUTC();
  return ret;
}

bool TimestampParser::ParseTimestamp(const wxString& sentence,
                                     wxDateTime& timestamp, int& precision) {
  // Check for valid NMEA sentence
  if (sentence.IsEmpty() || sentence[0] != '$') {
    return false;
  }

  // Split the sentence into fields
  wxStringTokenizer tok(sentence, wxT(",*"));
  if (!tok.HasMoreTokens()) return false;

  wxString sentenceId = tok.GetNextToken();
  wxString talkerId = sentenceId.Mid(1, 2);
  wxString sentenceType = sentenceId.Mid(3);

  if (m_useOnlyPrimarySource &&
      (m_primary_source.talker_id != talkerId ||
       m_primary_source.sentence_id != sentenceType)) {
    return false;
  }
  NmeaTimeInfo timeInfo;

  // Handle different sentence types
  if (sentenceType == "RMC") {  // GPRMC, GNRMC, etc
    // Example:
    // $GPRMC,092211.00,A,5759.09700,N,01144.34344,E,5.257,28.27,200715,,,A*58
    // Time field
    if (!tok.HasMoreTokens()) return false;
    wxString timeStr = tok.GetNextToken();
    if (!ParseTimeField(timeStr, timeInfo, precision)) return false;
    // Skip to date field (field 9)
    for (int i = 0; i < 7 && tok.HasMoreTokens(); i++) {
      tok.GetNextToken();
    }

    // Parse date
    if (!tok.HasMoreTokens()) return false;
    wxString dateStr = tok.GetNextToken();
    if (!ParseRMCDate(dateStr, timeInfo)) return false;
  } else if (sentenceType == "ZDA") {  // GPZDA, GNZDA, etc
    // Parse time
    if (!tok.HasMoreTokens()) return false;
    wxString timeStr = tok.GetNextToken();
    if (!ParseTimeField(timeStr, timeInfo, precision)) return false;

    // Parse date components
    if (!tok.HasMoreTokens()) return false;
    timeInfo.tm.tm_mday = wxAtoi(tok.GetNextToken());
    if (!tok.HasMoreTokens()) return false;
    timeInfo.tm.tm_mon = wxAtoi(tok.GetNextToken());
    if (!tok.HasMoreTokens()) return false;
    // ZDA uses 4-digit year, tm_year is years since 1900.
    timeInfo.tm.tm_year = wxAtoi(tok.GetNextToken()) - 1900;

    if (!ValidateAndSetDate(timeInfo)) return false;
  } else if (sentenceType == "GLL") {
    // For GLL, time is in field 5
    for (int i = 0; i < 4 && tok.HasMoreTokens(); i++) {
      tok.GetNextToken();  // Skip lat/lon fields
    }
    if (!tok.HasMoreTokens()) return false;
    wxString timeStr = tok.GetNextToken();
    if (!ParseTimeField(timeStr, timeInfo, precision)) return false;

    // Try to use cached date information
    ApplyCachedDate(timeInfo);
  } else if (sentenceType == "GGA" || sentenceType == "GBS") {
    // These sentences have time in field 1
    if (!tok.HasMoreTokens()) return false;
    wxString timeStr = tok.GetNextToken();
    if (!ParseTimeField(timeStr, timeInfo, precision)) return false;

    // Try to use cached date information
    ApplyCachedDate(timeInfo);
  }
  if (m_useOnlyPrimarySource && precision != m_primary_source.precision) {
    return false;
  }

  // Return false if we don't have both date and time
  if (!timeInfo.IsComplete()) {
    return false;
  }

  wxString isoTime = wxString::Format(
      "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", timeInfo.tm.tm_year + 1900,
      timeInfo.tm.tm_mon, timeInfo.tm.tm_mday, timeInfo.tm.tm_hour,
      timeInfo.tm.tm_min, timeInfo.tm.tm_sec, timeInfo.millisecond);

  if (!timestamp.ParseFormat(isoTime, "%Y-%m-%dT%H:%M:%S.%l%z")) {
    return false;
  }
  timestamp.MakeUTC();
  return true;
}

void TimestampParser::SetPrimaryTimeSource(const wxString& talkerId,
                                           const wxString& msgType,
                                           int precision) {
  m_primary_source = TimeSource{talkerId, msgType, precision};
  m_useOnlyPrimarySource = true;
}

void TimestampParser::DisablePrimaryTimeSource() {
  m_useOnlyPrimarySource = false;
}

void TimestampParser::Reset() {
  m_last_valid_year = 0;
  m_last_valid_month = 0;
  m_last_valid_day = 0;
  m_useOnlyPrimarySource = false;
}

bool TimestampParser::ParseCsvLineTimestamp(const wxString& line,
                                            unsigned int timestamp_idx,
                                            unsigned int message_idx,
                                            wxString* message,
                                            wxDateTime* timestamp) {
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

  // Parse timestamp if requested and available
  if (timestamp && timestamp_idx != static_cast<unsigned int>(-1) &&
      timestamp_idx < fields.GetCount()) {
    if (!ParseIso8601Timestamp(fields[timestamp_idx], timestamp)) {
      return false;
    }
  }

  // Get message field
  if (message_idx == static_cast<unsigned int>(-1) ||
      message_idx >= fields.GetCount()) {
    return false;
  }

  // No need to unescape quotes here as we handled them during parsing
  *message = fields[message_idx];
  return true;
}
