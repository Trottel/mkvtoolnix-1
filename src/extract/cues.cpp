/*
   mkvextract -- extract tracks from Matroska files into other files

   Distributed under the GPL v2
   see the file COPYING for details
   or visit http://www.gnu.org/copyleft/gpl.html

   extract cues from Matroska files into other files

   Written by Moritz Bunkus <moritz@bunkus.org>.
*/

#include "common/common_pch.h"

#include <ebml/EbmlStream.h>
#include <matroska/KaxCluster.h>
#include <matroska/KaxCues.h>
#include <matroska/KaxCuesData.h>
#include <matroska/KaxSegment.h>
#include <matroska/KaxTracks.h>

#include "common/ebml.h"
#include "common/kax_analyzer.h"
#include "common/mm_io_x.h"
#include "common/strings/formatting.h"
#include "extract/mkvextract.h"

using namespace libmatroska;

struct cue_point_t {
  uint64_t timecode;
  boost::optional<uint64_t> cluster_position, relative_position, duration;

  cue_point_t(uint64_t p_timecode)
    : timecode{p_timecode}
  {
  }
};

static void
write_cues(std::vector<track_spec_t> const &tracks,
           std::map<int64_t, int64_t> const &track_number_map,
           std::unordered_map<int64_t, std::vector<cue_point_t> > const &cue_points,
           uint64_t segment_data_start_pos,
           uint64_t timecode_scale) {
  for (auto const &track : tracks) {
    auto track_number_itr = track_number_map.find(track.tid);
    if (track_number_itr == track_number_map.end())
      mxerror(boost::format(Y("The file does not contain track ID %1%.\n")) % track.tid);

    auto cue_points_itr = cue_points.find(track_number_itr->second);
    if (cue_points_itr == cue_points.end())
      mxerror(boost::format(Y("There are no cues for track ID %1%.\n")) % track.tid);

    auto &track_cue_points = cue_points_itr->second;

    try {
      mxinfo(boost::format(Y("The cues for track %1% are written to '%2%'.\n")) % track.tid % track.out_name);

      auto out = mm_file_io_c{track.out_name, MODE_CREATE};

      for (auto const &p : track_cue_points) {
        auto line = (boost::format("timecode=%1% duration=%2% cluster_position=%3% relative_position=%4%\n")
                     % format_timestamp(p.timecode * timecode_scale, 9)
                     % (p.duration          ? format_timestamp(p.duration.get() * timecode_scale, 9)        : "-")
                     % (p.cluster_position  ? to_string(p.cluster_position.get() + segment_data_start_pos) : "-")
                     % (p.relative_position ? to_string(p.relative_position.get())                         : "-")
                     ).str();
        out.puts(line);
      }

    } catch (mtx::mm_io::exception &ex) {
      mxerror(boost::format(Y("The file '%1%' could not be opened for writing: %2%.\n")) % track.out_name % ex);
    }
  }
}

static std::map<int64_t, int64_t>
generate_track_number_map(kax_analyzer_c &analyzer) {
  auto track_number_map = std::map<int64_t, int64_t>{};
  auto tracks_m         = analyzer.read_all(EBML_INFO(KaxTracks));
  auto tracks           = dynamic_cast<KaxTracks *>(tracks_m.get());

  if (!tracks)
    return track_number_map;

  auto tid = 0;

  for (auto const &elt : *tracks) {
    auto ktrack_entry = dynamic_cast<KaxTrackEntry *>(elt);
    if (!ktrack_entry)
      continue;

    auto ktrack_number = FindChild<KaxTrackNumber>(ktrack_entry);
    if (ktrack_number)
      track_number_map[tid++] = ktrack_number->GetValue();
  }

  return track_number_map;
}

static uint64_t
find_timecode_scale(kax_analyzer_c &analyzer) {
  auto info_m = analyzer.read_all(EBML_INFO(KaxInfo));
  auto info   = dynamic_cast<KaxInfo *>(info_m.get());

  return info ? FindChildValue<KaxTimecodeScale>(info, 1000000ull) : 1000000ull;
}

static std::unordered_map<int64_t, std::vector<cue_point_t> >
parse_cue_points(kax_analyzer_c &analyzer) {
  auto cues_m = analyzer.read_all(EBML_INFO(KaxCues));
  auto cues   = dynamic_cast<KaxCues *>(cues_m.get());

  if (!cues)
    mxerror(Y("No cues were found.\n"));

  auto cue_points = std::unordered_map<int64_t, std::vector<cue_point_t> >{};

  for (auto const &elt : *cues) {
    auto kcue_point = dynamic_cast<KaxCuePoint *>(elt);
    if (!kcue_point)
      continue;

    auto ktime = FindChild<KaxCueTime>(*kcue_point);
    if (!ktime)
      continue;

    auto p = cue_point_t{ktime->GetValue()};
    auto ktrack_pos = FindChild<KaxCueTrackPositions>(*kcue_point);
    if (!ktrack_pos)
      continue;

    for (auto const &pos_elt : *ktrack_pos) {
      if (Is<KaxCueClusterPosition>(pos_elt))
        p.cluster_position.reset(static_cast<KaxCueClusterPosition *>(pos_elt)->GetValue());

      else if (Is<KaxCueRelativePosition>(pos_elt))
        p.relative_position.reset(static_cast<KaxCueRelativePosition *>(pos_elt)->GetValue());

      else if (Is<KaxCueDuration>(pos_elt))
        p.duration.reset(static_cast<KaxCueDuration *>(pos_elt)->GetValue());
    }

    for (auto const &pos_elt : *ktrack_pos)
      if (Is<KaxCueTrack>(pos_elt))
        cue_points[ static_cast<KaxCueTrack *>(pos_elt)->GetValue() ].push_back(p);
  }

  return cue_points;
}

static void
determine_cluster_data_start_positions(mm_file_io_c &file,
                                       uint64_t segment_data_start_pos,
                                       std::unordered_map<int64_t, std::vector<cue_point_t> > &cue_points) {
  auto es           = std::make_shared<EbmlStream>(file);
  auto upper_lvl_el = 0;

  for (auto &track_cue_points_pair : cue_points) {
    for (auto &cue_point : track_cue_points_pair.second) {
      if (!cue_point.cluster_position || !cue_point.relative_position)
        continue;

      try {
        file.setFilePointer(segment_data_start_pos + cue_point.cluster_position.get());
        auto elt = std::shared_ptr<EbmlElement>(es->FindNextElement(EBML_CLASS_CONTEXT(KaxSegment), upper_lvl_el, std::numeric_limits<int64_t>::max(), true));

        if (elt && Is<KaxCluster>(*elt))
          cue_point.relative_position = cue_point.relative_position.get() + elt->HeadSize();

      } catch (mtx::mm_io::exception &) {
      }
    }
  }
}

void
extract_cues(std::string const &file_name,
             std::vector<track_spec_t> const &tracks,
             kax_analyzer_c::parse_mode_e parse_mode) {
  if (tracks.empty())
    mxerror(Y("Nothing to do.\n"));

  auto analyzer               = open_and_analyze(file_name, parse_mode);

  auto cue_points             = parse_cue_points(*analyzer);
  auto timecode_scale         = find_timecode_scale(*analyzer);
  auto track_number_map       = generate_track_number_map(*analyzer);
  auto segment_data_start_pos = analyzer->get_segment_data_start_pos();

  determine_cluster_data_start_positions(analyzer->get_file(), segment_data_start_pos, cue_points);
  write_cues(tracks, track_number_map, cue_points, segment_data_start_pos, timecode_scale);
}
