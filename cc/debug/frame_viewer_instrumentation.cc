// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/debug/frame_viewer_instrumentation.h"

namespace cc {
namespace frame_viewer_instrumentation {
namespace {

const char kCategory[] = "cc," TRACE_DISABLED_BY_DEFAULT("devtools.timeline");
const char kTileData[] = "tileData";
const char kLayerId[] = "layerId";
const char kTileId[] = "tileId";
const char kTileResolution[] = "tileResolution";
const char kSourceFrameNumber[] = "sourceFrameNumber";

const char kAnalyzeTask[] = "AnalyzeTask";
const char kRasterTask[] = "RasterTask";

scoped_refptr<base::debug::ConvertableToTraceFormat> TileDataAsValue(
    const void* tile_id,
    TileResolution tile_resolution,
    int source_frame_number,
    int layer_id) {
  scoped_refptr<base::debug::TracedValue> res(new base::debug::TracedValue());
  TracedValue::SetIDRef(tile_id, res.get(), kTileId);
  res->SetString(kTileResolution, TileResolutionToString(tile_resolution));
  res->SetInteger(kSourceFrameNumber, source_frame_number);
  res->SetInteger(kLayerId, layer_id);
  return res;
}

}  // namespace

ScopedAnalyzeTask::ScopedAnalyzeTask(const void* tile_id,
                                     TileResolution tile_resolution,
                                     int source_frame_number,
                                     int layer_id) {
  TRACE_EVENT_BEGIN1(
      kCategory, kAnalyzeTask, kTileData,
      TileDataAsValue(tile_id, tile_resolution, source_frame_number, layer_id));
}

ScopedAnalyzeTask::~ScopedAnalyzeTask() {
  TRACE_EVENT_END0(kCategory, kAnalyzeTask);
}

ScopedRasterTask::ScopedRasterTask(const void* tile_id,
                                   TileResolution tile_resolution,
                                   int source_frame_number,
                                   int layer_id) {
  TRACE_EVENT_BEGIN1(
      kCategory, kRasterTask, kTileData,
      TileDataAsValue(tile_id, tile_resolution, source_frame_number, layer_id));
}

ScopedRasterTask::~ScopedRasterTask() {
  TRACE_EVENT_END0(kCategory, kRasterTask);
}

}  // namespace frame_viewer_instrumentation
}  // namespace cc
