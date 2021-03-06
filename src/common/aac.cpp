/*
   mkvmerge -- utility for splicing together matroska files
   from component media subtypes

   Distributed under the GPL v2
   see the file COPYING for details
   or visit http://www.gnu.org/copyleft/gpl.html

   helper function for AAC data

   Written by Moritz Bunkus <moritz@bunkus.org>.
*/

#include "common/common_pch.h"

#include "common/aac.h"
#include "common/aac_x.h"
#include "common/at_scope_exit.h"
#include "common/codec.h"
#include "common/endian.h"
#include "common/mp4.h"
#include "common/strings/formatting.h"

namespace aac {

static unsigned int const s_sampling_freq[16] = {
  96000, 88200, 64000, 48000, 44100, 32000,
  24000, 22050, 16000, 12000, 11025,  8000,
   7350,     0,     0,     0 // filling
};

static debugging_option_c s_debug_parse_data{"aac_parse_audio_specific_config|aac_full"};

unsigned int
get_sampling_freq_idx(unsigned int sampling_freq) {
  for (auto i = 0; i < 16; i++)
    if (sampling_freq >= (s_sampling_freq[i] - 1000))
      return i;

  return 0;                     // should never happen
}

bool
parse_codec_id(const std::string &codec_id,
               int &id,
               int &profile) {
  std::string sprofile;

  if (codec_id.size() < strlen(MKV_A_AAC_2LC))
    return false;

  if (codec_id[10] == '2')
    id = AAC_ID_MPEG2;
  else if (codec_id[10] == '4')
    id = AAC_ID_MPEG4;
  else
    return false;

  sprofile = codec_id.substr(12);
  if (sprofile == "MAIN")
    profile = AAC_PROFILE_MAIN;
  else if (sprofile == "LC")
    profile = AAC_PROFILE_LC;
  else if (sprofile == "SSR")
    profile = AAC_PROFILE_SSR;
  else if (sprofile == "LTP")
    profile = AAC_PROFILE_LTP;
  else if (sprofile == "LC/SBR")
    profile = AAC_PROFILE_SBR;
  else
    return false;

  return true;
}

bool
parse_audio_specific_config(const unsigned char *data,
                            size_t size,
                            int &profile,
                            int &channels,
                            int &sample_rate,
                            int &output_sample_rate,
                            bool &sbr) {
  auto h = header_c::from_audio_specific_config(data, size);
  if (!h.is_valid)
    return false;

  profile            = h.profile;
  channels           = h.channels;
  sample_rate        = h.sample_rate;
  output_sample_rate = h.output_sample_rate;
  sbr                = h.is_sbr;

  return true;
}

int
create_audio_specific_config(unsigned char *data,
                             int profile,
                             int channels,
                             int sample_rate,
                             int output_sample_rate,
                             bool sbr) {
  int srate_idx = aac::get_sampling_freq_idx(sample_rate);
  data[0]       = ((profile + 1) << 3) | ((srate_idx & 0x0e) >> 1);
  data[1]       = ((srate_idx & 0x01) << 7) | (channels << 3);

  if (sbr) {
    srate_idx = aac::get_sampling_freq_idx(output_sample_rate);
    data[2]   = AAC_SYNC_EXTENSION_TYPE >> 3;
    data[3]   = ((AAC_SYNC_EXTENSION_TYPE & 0x07) << 5) | MP4AOT_SBR;
    data[4]   = (1 << 7) | (srate_idx << 3);

    return 5;
  }

  return 2;
}

// ------------------------------------------------------------

latm_parser_c::latm_parser_c()
  : m_audio_mux_version{}
  , m_audio_mux_version_a{}
  , m_fixed_frame_length{}
  , m_frame_length_type{}
  , m_frame_bit_offset{}
  , m_frame_length{}
  , m_header{}
  , m_bc{}
  , m_config_parsed{}
  , m_debug{"latm_parser"}
{
}

bool
latm_parser_c::config_parsed()
  const {
  return m_config_parsed;
}

header_c const &
latm_parser_c::get_header()
  const {
  return m_header;
}

size_t
latm_parser_c::get_frame_bit_offset()
  const {
  return m_frame_bit_offset;
}

size_t
latm_parser_c::get_frame_length()
  const {
  return m_frame_length;
}

unsigned int
latm_parser_c::get_value() {
  auto num_bytes = m_bc->get_bits(2) + 1;
  return m_bc->get_bits(8 * num_bytes);
}

void
latm_parser_c::parse(bit_reader_c &bc) {
  auto cleanup = at_scope_exit_c{[this]() { m_bc = nullptr; }};

  m_bc = &bc;
  parse_audio_mux_element();
}

void
latm_parser_c::parse_audio_specific_config(size_t asc_length) {
  auto look_for_sync_extension = asc_length > 0;
  asc_length                   = asc_length > 0 ? std::min<size_t>(asc_length, m_bc->get_remaining_bits()) : m_bc->get_remaining_bits();

  if (!asc_length)
    throw false;

  auto new_header = header_c{};
  new_header.parse_audio_specific_config(*m_bc, look_for_sync_extension);
  if (!new_header.is_valid)
    throw false;

  m_header = new_header;
}

void
latm_parser_c::parse_stream_mux_config() {
  mxdebug_if(m_debug, boost::format("Parsing stream mux config\n"));

  auto start_position = m_bc->get_bit_position();
  m_audio_mux_version = m_bc->get_bit();
  if (m_audio_mux_version)
    m_audio_mux_version_a = m_bc->get_bit();

  if (m_audio_mux_version_a != 0) {
    mxdebug_if(m_debug, boost::format("audio_mux_version_a is not 0; not supported\n"));
    throw false;
  }

  if (m_audio_mux_version)
    get_value();                // tara_buffer_fullness

  m_bc->skip_bits(1 + 6);        // all_stream_same_time_framing, num_sub_frames

  if (m_bc->get_bits(4) != 0) {
    // More than one program is not supported at the moment; DVB
    // always only uses a single program.
    mxdebug_if(m_debug, boost::format("more than one program in LOAS/LATM\n"));
    throw false;
  }

  if (m_bc->get_bits(3) != 0) {
    // More than one layer is not supported at the moment; DVB
    // always only uses a single layer.
    mxdebug_if(m_debug, boost::format("more than one layer in LOAS/LATM\n"));
    throw false;
  }

  if (m_audio_mux_version == 0)
    parse_audio_specific_config(0);

  else {
    auto asc_length     = get_value();
    auto prior_position = m_bc->get_bit_position();

    parse_audio_specific_config(asc_length);

    auto used_bits = m_bc->get_bit_position() - prior_position;

    if (static_cast<size_t>(used_bits) < asc_length)
      m_bc->skip_bits(asc_length - used_bits);
  }

  m_frame_length_type = m_bc->get_bits(3);

  switch (m_frame_length_type) {
    case 0:
      m_bc->skip_bits(8);        // buffer_fullness
      break;
    case 1:
      m_fixed_frame_length = m_bc->get_bits(9);
      break;
    case 3:
    case 4:
    case 5:
      m_bc->skip_bits(6);        // CELP frame length table index
      break;
    case 6:
    case 7:
      m_bc->skip_bits(1);        // HVXC frame length table index
      break;
  }

  if (m_bc->get_bit()) {         // other_data
    if (m_audio_mux_version)
      get_value();              // other_data_bits
    else {
      bool escape;
      do {
        escape = m_bc->get_bit();
        m_bc->skip_bits(8);
      } while (escape);
    }
  }

  if (m_bc->get_bit())           // crc_present
    m_bc->skip_bits(8);          // config_crc

  mxdebug_if(m_debug,
             boost::format("stream_mux_config: bit size %1% m_audio_mux_version %2% m_audio_mux_version_a %3% m_frame_length_type %4% m_fixed_frame_length %5% header %6%\n")
             % (m_bc->get_bit_position() - start_position) % m_audio_mux_version % m_audio_mux_version_a %  m_frame_length_type % m_fixed_frame_length % m_header);
}

size_t
latm_parser_c::parse_payload_length_info() {
  // if (m_all_streams_same_time_framing) {
  //   for (auto program = 0u; program < m_num_program; program++) {
  //     for (auto layer = 0u; layer < m_num_layer; layer++) {
  //       auto stream_id         = m_stream_id[program][layer];
  //       auto frame_length_type = m_frame_length_type[stream_id];

  //       if (frame_length_type == 0)
  //         m_mux_slot_length[stream_id] = parse_vint(bc);

  //       else if ((frame_length_type == 3) || (frame_length_type == 5) || (frame_length_type == 7))
  //         m_mux_slot_length[stream_id] = m_bc->get_bits(2);
  //     }
  //   }
  // } else {

  // }

  switch (m_frame_length_type) {
    case 0: {
      auto length = 0u, tmp = 0u;
      do {
        tmp     = m_bc->get_bits(8);
        length += tmp;
      } while (tmp == 255);
      return length;
    }
    case 1:
      return m_fixed_frame_length;

    case 3:
    case 5:
    case 7:
      return m_bc->get_bits(2);
  }

  return 0;
}

void
latm_parser_c::parse_payload_mux(size_t length) {
  m_frame_bit_offset = m_bc->get_bit_position();
  m_frame_length     = length;
}

void
latm_parser_c::parse_audio_mux_element() {
  auto use_same_stream_mux = m_bc->get_bit();
  if (!use_same_stream_mux) {
    parse_stream_mux_config();
    if (m_header.is_valid)
      m_config_parsed = true;
  }

  if (!m_config_parsed) {
    mxdebug_if(m_debug, boost::format("Configuration not parsed; not continuing with audio mux element parsing\n"));
    throw false;
  }

  if (m_audio_mux_version_a == 0) {
    // for (auto idx = 0; idx < m_num_sub_frames; ++idx) {
      auto length = parse_payload_length_info();
      parse_payload_mux(length);
    // }

    // if (m_other_data_present)
    //   m_bc->skip_bits(m_other_data_len_bits);

  } else
    throw false;
}

// ------------------------------------------------------------

frame_c::frame_c() {
  init();
}

void
frame_c::init() {
  m_header           = header_c{};
  m_stream_position  = 0;
  m_garbage_size     = 0;
  m_timecode.reset();
  m_data.reset();
}

std::string
frame_c::to_string(bool verbose)
  const {
  if (!verbose)
    return (boost::format("position %1% size %2% ID %3% profile %4%") % m_stream_position % m_header.bytes % m_header.id % m_header.profile).str();

  return (boost::format("position %1% size %2% garbage %3% ID %4% profile %5% sample rate %6% bit rate %7% channels %8%")
          % m_stream_position
          % m_header.bytes
          % m_garbage_size
          % m_header.id
          % m_header.profile
          % m_header.sample_rate
          % m_header.bit_rate
          % m_header.channels
          ).str();
}

// ------------------------------------------------------------

parser_c::parser_c()
  : m_fixed_buffer{}
  , m_fixed_buffer_size{}
  , m_parsed_stream_position{}
  , m_total_stream_position{}
  , m_garbage_size{}
  , m_num_frames_found{}
  , m_abort_after_num_frames{}
  , m_require_frame_at_first_byte{}
  , m_copy_data{true}
  , m_multiplex_type{unknown_multiplex}
  , m_debug{"aac_parser"}
{
}

void
parser_c::add_timecode(timestamp_c const &timecode) {
  m_provided_timecodes.push_back(timecode);
}

void
parser_c::add_bytes(memory_cptr const &mem) {
  add_bytes(mem->get_buffer(), mem->get_size());
}

void
parser_c::add_bytes(unsigned char const *buffer,
                    size_t size) {
  m_buffer.add(buffer, size);
  m_total_stream_position += size;
  parse();
}

void
parser_c::parse_fixed_buffer(unsigned char const *fixed_buffer,
                             size_t fixed_buffer_size) {
  auto cleanup = at_scope_exit_c{[this]() {
      m_fixed_buffer      = nullptr;
      m_fixed_buffer_size = 0;
    }};

  m_fixed_buffer      = fixed_buffer;
  m_fixed_buffer_size = fixed_buffer_size;
  parse();
}

void
parser_c::parse_fixed_buffer(memory_cptr const &fixed_buffer) {
  parse_fixed_buffer(fixed_buffer->get_buffer(), fixed_buffer->get_size());
}

void
parser_c::flush() {
  // no-op
}

void
parser_c::abort_after_num_frames(size_t num_frames) {
  m_abort_after_num_frames = num_frames;
}

void
parser_c::require_frame_at_first_byte(bool require) {
  m_require_frame_at_first_byte = require;
}

void
parser_c::copy_data(bool copy) {
  m_copy_data = copy;
}

size_t
parser_c::frames_available()
  const {
  return m_frames.size();
}

frame_c
parser_c::get_frame() {
  auto frame = m_frames.front();
  m_frames.pop_front();

  if (!frame.m_header.is_valid)
    frame.m_header = m_header;

  return frame;
}

uint64_t
parser_c::get_total_stream_position()
  const {
  return m_total_stream_position;
}

uint64_t
parser_c::get_parsed_stream_position()
  const {
  return m_parsed_stream_position;
}

bool
parser_c::headers_parsed()
  const {
  return m_header.is_valid;
}

std::pair<parser_c::parse_result_e, size_t>
parser_c::decode_adts_header(unsigned char const *buffer,
                             size_t buffer_size) {
  try {
    auto frame = frame_c{};
    auto bc    = bit_reader_c{buffer, static_cast<unsigned int>(buffer_size)};

    if (bc.get_bits(12) != 0xfff)                  // ADTS header
      return { failure, 1 };

    frame.m_header.id              = bc.get_bit(); // ID: 0 = MPEG-4, 1 = MPEG-2

    if (bc.get_bits(2) != 0)                       // layer == 0 !
      return { failure, 1 };

    bool protection_absent         = bc.get_bit();
    frame.m_header.profile         = bc.get_bits(2);
    int sfreq_index                = bc.get_bits(4);
    bc.skip_bits(1);                               // private
    frame.m_header.channels        = bc.get_bits(3);
    bc.skip_bits(1 + 1);                           // original/copy & home
    bc.skip_bits(1 + 1);                           // copyright_id_bit & copyright_id_start

    frame.m_header.bytes           = bc.get_bits(13);

    if (frame.m_header.bytes >  buffer_size)
      return { need_more_data, 0 };

    bc.skip_bits(11);                              // adts_buffer_fullness
    bc.skip_bits(2);                               // no_raw_blocks_in_frame
    if (!protection_absent)
      bc.skip_bits(16);

    frame.m_header.header_bit_size  = bc.get_bit_position();
    frame.m_header.sample_rate      = s_sampling_freq[sfreq_index];
    frame.m_header.bit_rate         = 1024;
    frame.m_header.header_byte_size = (bc.get_bit_position() + 7) / 8;
    frame.m_header.data_byte_size   = frame.m_header.bytes - frame.m_header.header_byte_size;
    frame.m_header.is_valid         = true;

    if (m_copy_data) {
      frame.m_data = memory_c::alloc(frame.m_header.data_byte_size);
      bc.get_bytes(frame.m_data->get_buffer(), frame.m_header.data_byte_size);
    }

    push_frame(frame);

    return { success, frame.m_header.bytes };

  } catch (mtx::mm_io::end_of_file_x &) {
    return { need_more_data, 0 };
  }

  return { failure, 1 };
}

std::pair<parser_c::parse_result_e, size_t>
parser_c::decode_loas_latm_header(unsigned char const *buffer,
                                  size_t buffer_size) {
  try {
    if (buffer_size < 3)
      return { need_more_data, 0 };

    auto value = get_uint24_be(buffer);
    if ((value & AAC_LOAS_SYNC_WORD_MASK) != AAC_LOAS_SYNC_WORD)
      return { failure, 1 };

    auto loas_frame_size = value & AAC_LOAS_FRAME_SIZE_MASK;
    auto loas_frame_end  = loas_frame_size + 3;
    if (loas_frame_end > buffer_size)
      return { need_more_data, 0 };

    auto bc = bit_reader_c{buffer, loas_frame_end};
    bc.skip_bits(3 * 8);

    m_latm_parser.parse(bc);

    auto end_of_header_bit_pos  = bc.get_bit_position();
    auto decoded_frame_length   = m_latm_parser.get_frame_length();
    auto decoded_frame_end_bits = end_of_header_bit_pos + (decoded_frame_length * 8);

    if (decoded_frame_end_bits > (loas_frame_end * 8)) {
      mxdebug_if(m_debug,
                 boost::format("decode_loas_latm_header: decoded_frame_end_bits (%1%) > loas_frame_end_bits (%2%); decoded_frame_length: %3% end_of_header_bit_pos %4%\n")
                 % decoded_frame_end_bits % (loas_frame_end * 8) % decoded_frame_length % end_of_header_bit_pos);
      return { failure, 2 };
    }

    auto &new_header = m_latm_parser.get_header();
    if (new_header.is_valid)
      m_header = new_header;

    auto frame                      = frame_c{};

    frame.m_header.header_bit_size  = end_of_header_bit_pos - 3 * 8;
    frame.m_header.header_byte_size = (frame.m_header.header_bit_size + 7) / 8;
    frame.m_header.data_byte_size   = decoded_frame_length;
    frame.m_header.bytes            = loas_frame_size + 3;

    unsigned char *dst_buffer       = nullptr;

    if (m_copy_data) {
      frame.m_data = memory_c::alloc(decoded_frame_length);
      dst_buffer   = frame.m_data->get_buffer();

      bc.get_bytes(dst_buffer, decoded_frame_length);
    }

    push_frame(frame);

    mxdebug_if(m_debug,
               boost::format("decode_loas_latm_header: headerok %6% buffer_size %1% loas_frame_size %2% header_byte_size %3% data_byte_size %4% bytes %5% decoded_frame_offset %7% decoded_frame_length %8% "
                             "first_four_bytes %|9$08x| end_of_header_bit_pos %10%\n")
               % buffer_size % loas_frame_size % frame.m_header.header_byte_size % frame.m_header.data_byte_size % frame.m_header.bytes % new_header.is_valid % m_latm_parser.get_frame_bit_offset() % decoded_frame_length
               % (dst_buffer && (decoded_frame_length >= 4) ? get_uint32_be(dst_buffer) : 0) % end_of_header_bit_pos);

    return { success, loas_frame_end };

  } catch (mtx::mm_io::end_of_file_x &) {
    return { need_more_data, 0 };

  } catch (bool) {
  }

  return { failure, 1 };
}

std::pair<parser_c::parse_result_e, size_t>
parser_c::decode_header(unsigned char const *buffer,
                        size_t buffer_size) {
  if (adif_multiplex == m_multiplex_type)
    return { failure, 0 };

  if (adts_multiplex == m_multiplex_type)
    return decode_adts_header(buffer, buffer_size);

  if (loas_latm_multiplex == m_multiplex_type)
    return decode_loas_latm_header(buffer, buffer_size);

  auto result = decode_adts_header(buffer, buffer_size);
  if (result.first == success) {
    m_multiplex_type = adts_multiplex;
    return result;
  }

  result = decode_loas_latm_header(buffer, buffer_size);
  if (result.first == success) {
    m_multiplex_type = loas_latm_multiplex;
    return result;
  }

  return result;
}

void
parser_c::push_frame(frame_c &frame) {
  if (!m_provided_timecodes.empty()) {
    frame.m_timecode = m_provided_timecodes.front();
    m_provided_timecodes.pop_front();
  }

  frame.m_stream_position  = m_parsed_stream_position;
  frame.m_garbage_size     = m_garbage_size;

  m_garbage_size           = 0;

  m_frames.push_back(frame);

  if (frame.m_header.is_valid)
    m_header = frame.m_header;

  ++m_num_frames_found;
}

void
parser_c::parse() {
  if (m_abort_after_num_frames && (m_num_frames_found >= m_abort_after_num_frames))
    return;

  auto buffer      = m_fixed_buffer ? m_fixed_buffer      : m_buffer.get_buffer();
  auto buffer_size = m_fixed_buffer ? m_fixed_buffer_size : m_buffer.get_size();
  auto position    = 0u;

  while (position < buffer_size) {
    auto remaining_bytes = buffer_size - position;
    auto result          = decode_header(&buffer[position], remaining_bytes);

    if (result.first == need_more_data)
      break;

    auto num_bytes            = std::max<size_t>(std::min(result.second, remaining_bytes), 1);
    position                 += num_bytes;
    m_parsed_stream_position += num_bytes;

    mxdebug_if(m_debug,
               boost::format("result_status %1% remainig_bytes %2% result_bytes %3% num_bytes %4% position before %5% after %6%\n")
               % (result.first == success ? "success" : result.first == failure ? "failure" : "need-more-data")
               % remaining_bytes % result.second % num_bytes % (position - num_bytes) % position);

    if (result.first == failure) {
      m_garbage_size += num_bytes;
      if (!m_num_frames_found && m_require_frame_at_first_byte)
        break;
    }

    if (m_abort_after_num_frames && (m_num_frames_found >= m_abort_after_num_frames))
      break;
  }

  if (!m_fixed_buffer)
    m_buffer.remove(position);
}

int
parser_c::find_consecutive_frames(unsigned char const *buffer,
                                  size_t buffer_size,
                                  size_t num_required_frames) {
  static auto s_debug = debugging_option_c{"aac_consecutive_frames"};

  for (size_t base = 0; (base + 8) < buffer_size; ++base) {
    mxdebug_if(s_debug, boost::format("Starting search for %2% headers with base %1%, buffer size %3%\n") % base % num_required_frames % buffer_size);

    auto value = get_uint24_be(&buffer[base]);
    // Speeding up checks by using shortcuts here instead of going
    // through the parser for each byte position: check for supported
    // header types (ADTS and LOAS/LATM).
    if (   ((value & AAC_ADTS_SYNC_WORD_MASK) != AAC_ADTS_SYNC_WORD)
        && ((value & AAC_LOAS_SYNC_WORD_MASK) != AAC_LOAS_SYNC_WORD))
      continue;

    if ((value & AAC_LOAS_SYNC_WORD_MASK) == AAC_LOAS_SYNC_WORD) {
      // Check for second LOAS header right after the current one.
      auto loas_frame_size = value & AAC_LOAS_FRAME_SIZE_MASK;
      if (!loas_frame_size || ((base + loas_frame_size + 3 + 3) > buffer_size))
        continue;

      value = get_uint24_be(&buffer[base + 3 + loas_frame_size]);
      if ((value & AAC_LOAS_SYNC_WORD_MASK) != AAC_LOAS_SYNC_WORD)
        continue;

    } else {
      // Check for second ADTS header right after the current one.
      // adts_frame_size (including the header) = 13 bits starting at
      // bit position 30; 2@b3 + 8@b4 + 3@b5
      auto adts_frame_size = (static_cast<unsigned int>(buffer[base + 3] & 0x03) << 11)
                           | (static_cast<unsigned int>(buffer[base + 4])        <<  3)
                           | (static_cast<unsigned int>(buffer[base + 5])        >>  5);

      if ((adts_frame_size < 7) || ((base + adts_frame_size + 8) > buffer_size))
        continue;

      value = get_uint24_be(&buffer[base + adts_frame_size]);
      if ((value & AAC_ADTS_SYNC_WORD_MASK) != AAC_ADTS_SYNC_WORD)
        continue;
    }

    auto parser = parser_c{};
    parser.require_frame_at_first_byte(true);
    parser.abort_after_num_frames(num_required_frames);
    parser.copy_data(false);
    parser.parse_fixed_buffer(&buffer[base], buffer_size - base);

    auto num_frames_found = parser.frames_available();
    if ((num_frames_found < num_required_frames) || !parser.headers_parsed())
      continue;

    if (num_frames_found == 1)
      return base;

    std::string garbage_sizes;
    bool garbage_found = false;

    std::vector<frame_c> frames;
    frames.reserve(num_frames_found);

    while (parser.frames_available()) {
      auto frame = parser.get_frame();
      frames.push_back(frame);

      garbage_sizes += (boost::format(" %1%") % frame.m_garbage_size).str();
      if (frame.m_garbage_size)
        garbage_found = true;
    }

    mxdebug_if(s_debug, boost::format("  Found enough headers at %1%; garbage sizes:%2% found garbage: %3%\n") % base % garbage_sizes % garbage_found);

    if (garbage_found)
      continue;

    bool mismatch_found = false;
    auto &first_frame   = frames[0];

    for (auto frame_idx = 1u; frame_idx < num_frames_found; ++frame_idx) {
      auto &current_frame = frames[frame_idx];
      if (   (current_frame.m_header.id          != first_frame.m_header.id)
          && (current_frame.m_header.profile     != first_frame.m_header.profile)
          && (current_frame.m_header.channels    != first_frame.m_header.channels)
          && (current_frame.m_header.sample_rate != first_frame.m_header.sample_rate)) {
        mxdebug_if(s_debug,
                   boost::format("Current frame number %9% at %10% differs from first frame. (first/current) ID: %1%/%2% profile: %3%/%4% channels: %5%/%6% sample rate: %7%/%8%\n")
                   % first_frame.m_header.id          % current_frame.m_header.id
                   % first_frame.m_header.profile     % current_frame.m_header.profile
                   % first_frame.m_header.channels    % current_frame.m_header.channels
                   % first_frame.m_header.sample_rate % current_frame.m_header.sample_rate
                   % frame_idx % (base + current_frame.m_stream_position));

        mismatch_found = true;

        break;
      }
    }

    if (!mismatch_found)
      return base;
  }

  return -1;
}

// ------------------------------------------------------------

header_c::header_c()
  : object_type{}
  , extension_object_type{}
  , profile{}
  , sample_rate{}
  , output_sample_rate{}
  , bit_rate{}
  , channels{}
  , bytes{}
  , id{}
  , header_bit_size{}
  , header_byte_size{}
  , data_byte_size{}
  , is_sbr{}
  , is_valid{}
{
}

std::string
header_c::to_string()
  const {
  return (boost::format("sample_rate: %1%; bit_rate: %2%; channels: %3%; bytes: %4%; id: %5%; profile: %6%; header_bit_size: %7%; header_byte_size: %8%; data_byte_size: %9%; is_sbr: %10%; is_valid: %11%")
          % sample_rate % bit_rate % channels % bytes % id % profile % header_bit_size % header_byte_size % data_byte_size % is_sbr % is_valid).str();
}

int
header_c::read_object_type() {
  int object_type = m_bc->get_bits(5);
  return 31 == object_type ? 32 + m_bc->get_bits(6) : object_type;
}

int
header_c::read_sample_rate() {
  int idx = m_bc->get_bits(4);
  return 0x0f == idx ? m_bc->get_bits(24) : s_sampling_freq[idx];
}

header_c
header_c::from_audio_specific_config(unsigned char const *data,
                                     size_t size) {
  header_c header;
  header.parse_audio_specific_config(data, size);
  return header;
}

void
header_c::read_eld_specific_config() {
  if (m_bc->get_bit())          // frame_length_flag
    throw false;

  auto res_flags = m_bc->get_bits(3);
  if (res_flags)                // resilience_flags
    throw false;

  if (m_bc->get_bit())          // low_delay_sbr_present_flag
    throw false;

  while (m_bc->get_bits(4) != 0) {
    auto length = m_bc->get_bits(4);
    if (15 == length)
      length += m_bc->get_bits(8);
    if ((15 + 255) == length)
      length += m_bc->get_bits(16);

    m_bc->skip_bits(length);
  }

  auto ep_config = m_bc->get_bits(2);
  if (ep_config)                // ep_config
    throw false;
}

void
header_c::read_ga_specific_config() {
  m_bc->skip_bit();             // frame_length_flag
  if (m_bc->get_bit())          // depends_on_core_coder
    m_bc->skip_bits(14);        // core_coder_delay
  bool extension_flag = m_bc->get_bit();

  if (!channels)
    read_program_config_element();

  if ((MP4AOT_AAC_SCALABLE == object_type) || (MP4AOT_ER_AAC_SCALABLE == object_type))
    m_bc->skip_bits(3);         // layer_nr

  if (!extension_flag)
    return;

  if (MP4AOT_ER_BSAC == object_type)
    m_bc->skip_bits(5 + 11);    // num_of_sub_frame, layer_length

  if ((MP4AOT_ER_AAC_LC == object_type) || (MP4AOT_ER_AAC_LTP == object_type) || (MP4AOT_ER_AAC_SCALABLE == object_type) || (MP4AOT_ER_AAC_LD == object_type))
    m_bc->skip_bits(1 + 1 + 1); // aac_section_data_resilience_flag, aac_scalefactor_data_resilience_flag, aac_spectral_data_resilience_flag

  m_bc->skip_bit();             // extension_flag3
}

void
header_c::read_error_protection_specific_config() {
  throw unsupported_feature_x{"AAC error specific configuration"};
}

void
header_c::read_program_config_element() {
  m_bc->skip_bits(4);           // element_instance_tag
  object_type        = m_bc->get_bits(2);
  sample_rate        = s_sampling_freq[m_bc->get_bits(4)];
  int num_front_chan = m_bc->get_bits(4);
  int num_side_chan  = m_bc->get_bits(4);
  int num_back_chan  = m_bc->get_bits(4);
  int num_lfe_chan   = m_bc->get_bits(2);
  int num_assoc_data = m_bc->get_bits(3);
  int num_valid_cc   = m_bc->get_bits(4);

  if (m_bc->get_bit())          // mono_mixdown_present_flag
    m_bc->skip_bits(4);         // mono_mixdown_element_number
  if (m_bc->get_bit())          // stereo_mixdown_present_flag
    m_bc->skip_bits(4);         // stereo_mixdown_element_number
  if (m_bc->get_bit())          // matrix_mixdown_idx_present_flag
    m_bc->skip_bits(2 + 1);     // matrix_mixdown_idx, pseudo_surround_enable

  channels = num_front_chan + num_side_chan + num_back_chan + num_lfe_chan;

  for (int idx = 0; idx < (num_front_chan + num_side_chan + num_back_chan); ++idx) {
    if (m_bc->get_bit())        // *_element_is_cpe
      ++channels;
    m_bc->skip_bits(4);         // *_element_tag_select
  }
  m_bc->skip_bits(num_lfe_chan   *      4);  //                       lfe_element_tag_select
  m_bc->skip_bits(num_assoc_data *      4);  //                       assoc_data_element_tag_select
  m_bc->skip_bits(num_valid_cc   * (1 + 4)); // cc_element_is_ind_sw, valid_cc_element_tag_select

  m_bc->byte_align();
  m_bc->skip_bits(m_bc->get_bits(8) * 8);    // comment_field_bytes, comment_field_data
}

void
header_c::parse_audio_specific_config(bit_reader_c &bc,
                                      bool look_for_sync_extension) {
  m_bc = &bc;

  try {
    object_type = read_object_type();

    if (!object_type)
      return;

    is_sbr      = false;
    profile     = object_type - 1;
    sample_rate = read_sample_rate();
    channels    = m_bc->get_bits(4);

    if (   (MP4AOT_SBR == object_type)
        || (    (MP4AOT_PS == object_type)
            && !(    (m_bc->peek_bits(3) & 0x03)
                 && !(m_bc->peek_bits(9) & 0x3f)))) {
      is_sbr                = true;
      output_sample_rate    = read_sample_rate();
      extension_object_type = object_type;
      object_type           = read_object_type();
    }

    if (   (MP4AOT_AAC_MAIN == object_type) || (MP4AOT_AAC_LC    == object_type) || (MP4AOT_AAC_SSR    == object_type) || (MP4AOT_AAC_LTP         == object_type) || (MP4AOT_AAC_SCALABLE == object_type)
        || (MP4AOT_TWINVQ   == object_type) || (MP4AOT_ER_AAC_LC == object_type) || (MP4AOT_ER_AAC_LTP == object_type) || (MP4AOT_ER_AAC_SCALABLE == object_type) || (MP4AOT_ER_TWINVQ    == object_type)
        || (MP4AOT_ER_BSAC  == object_type) || (MP4AOT_ER_AAC_LD == object_type))
      read_ga_specific_config();

    else if (MP4AOT_ER_AAC_ELD == object_type)
      read_eld_specific_config();

    else
      throw "aac_object_type_not_ga_specific. %1%\n";

    if ((MP4AOT_ER_AAC_LC == object_type) || ((MP4AOT_ER_AAC_LTP <= object_type) && (MP4AOT_ER_PARAM >= object_type))) {
      int ep_config = m_bc->get_bits(2);
      if ((2 == ep_config) || (3 == ep_config))
        read_error_protection_specific_config();
      if (3 == ep_config)
        m_bc->skip_bit();       // direct_mapping
    }

    if (   look_for_sync_extension
        && (MP4AOT_SBR != extension_object_type)
        && (m_bc->get_remaining_bits() >= 16)) {

      auto prior_position     = m_bc->get_bit_position();
      int sync_extension_type = m_bc->get_bits(11);

      if (0x2b7 == sync_extension_type) {
        extension_object_type = read_object_type();
        if (MP4AOT_SBR == extension_object_type) {
          is_sbr = m_bc->get_bit();
          if (is_sbr)
            output_sample_rate = read_sample_rate();
        }

      } else
        m_bc->set_bit_position(prior_position);
    }

    is_valid = true;

    if (sample_rate <= 24000) {
      output_sample_rate = 2 * sample_rate;
      is_sbr             = true;
    }

  } catch (mtx::exception &ex) {
    mxdebug_if(s_debug_parse_data, boost::format("aac::parse_audio_specific_config: exception: %1%\n") % ex);
  }

  m_bc = nullptr;
}

void
header_c::parse_audio_specific_config(const unsigned char *data,
                                      size_t size,
                                      bool look_for_sync_extension) {
  if (size < 2)
    return;

  mxdebug_if(s_debug_parse_data, boost::format("aac::parse_audio_specific_config: size %1%, data: %2%\n") % size % to_hex(data, size));

  bit_reader_c bc{data, static_cast<unsigned int>(size)};
  parse_audio_specific_config(bc, look_for_sync_extension);
}

bool
operator ==(const header_c &h1,
            const header_c &h2) {
  return (h1.sample_rate == h2.sample_rate)
      && (h1.bit_rate    == h2.bit_rate)
      && (h1.channels    == h2.channels)
      && (h1.id          == h2.id)
      && (h1.profile     == h2.profile);
}

} // namespace aac
