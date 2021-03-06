/*
   mkvmerge -- utility for splicing together matroska files
   from component media subtypes

   Distributed under the GPL v2
   see the file COPYING for details
   or visit http://www.gnu.org/copyleft/gpl.html

   the timecode calculator implementation

   Written by Moritz Bunkus <moritz@bunkus.org>.
*/

#include "common/common_pch.h"

#include "common/strings/formatting.h"
#include "merge/timestamp_calculator.h"
#include "merge/packet.h"

timestamp_calculator_c::timestamp_calculator_c(int64_t samples_per_second)
  : m_reference_timecode{timestamp_c::ns(0)}
  , m_samples_per_second{samples_per_second}
  , m_samples_since_reference_timecode{}
  , m_samples_to_timestamp{1000000000, static_cast<int64_t>(samples_per_second)}
  , m_debug{"timestamp_calculator"}
{
}

void
timestamp_calculator_c::add_timecode(timestamp_c const &timecode) {
  if (!timecode.valid())
    return;

  if (   (!m_last_timecode_returned.valid() || (timecode > m_last_timecode_returned))
      && (m_available_timecodes.empty()     || (timecode > m_available_timecodes.back()))) {
    mxdebug_if(m_debug, boost::format("timestamp_calculator::add_timecode: adding %1%\n") % format_timestamp(timecode));
    m_available_timecodes.push_back(timecode);

  } else
    mxdebug_if(m_debug, boost::format("timestamp_calculator::add_timecode: dropping %1%\n") % format_timestamp(timecode));
}

void
timestamp_calculator_c::add_timecode(int64_t timecode) {
  if (-1 != timecode)
    add_timecode(timestamp_c::ns(timecode));
}

void
timestamp_calculator_c::add_timecode(packet_cptr const &packet) {
  if (packet->has_timecode())
    add_timecode(timestamp_c::ns(packet->timecode));
}

timestamp_c
timestamp_calculator_c::get_next_timecode(int64_t samples_in_frame) {
  if (!m_available_timecodes.empty()) {
    m_last_timecode_returned           = m_available_timecodes.front();
    m_reference_timecode               = m_last_timecode_returned;
    m_samples_since_reference_timecode = samples_in_frame;

    m_available_timecodes.pop_front();

    mxdebug_if(m_debug, boost::format("timestamp_calculator_c::get_next_timecode: returning available %1%\n") % format_timestamp(m_last_timecode_returned));

    return m_last_timecode_returned;
  }

  if (!m_samples_per_second)
    throw std::invalid_argument{"samples per second must not be 0"};

  m_last_timecode_returned           = m_reference_timecode + timestamp_c::ns(m_samples_to_timestamp * m_samples_since_reference_timecode);
  m_samples_since_reference_timecode += samples_in_frame;

  mxdebug_if(m_debug, boost::format("timestamp_calculator_c::get_next_timecode: returning calculated %1%\n") % format_timestamp(m_last_timecode_returned));

  return m_last_timecode_returned;
}

timestamp_c
timestamp_calculator_c::get_duration(int64_t samples) {
  if (!m_samples_per_second)
    throw std::invalid_argument{"samples per second must not be 0"};

  return timestamp_c::ns(m_samples_to_timestamp * samples);
}

void
timestamp_calculator_c::set_samples_per_second(int64_t samples_per_second) {
  if (!samples_per_second || (samples_per_second == m_samples_per_second))
    return;

  m_reference_timecode               += get_duration(m_samples_since_reference_timecode);
  m_samples_since_reference_timecode  = 0;
  m_samples_to_timestamp.set(1000000000, samples_per_second);
}
