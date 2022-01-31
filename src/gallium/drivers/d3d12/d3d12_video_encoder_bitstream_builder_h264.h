/*
 * Copyright © Microsoft Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef D3D12_VIDEO_ENC_BITSTREAM_BUILDER_H264_H
#define D3D12_VIDEO_ENC_BITSTREAM_BUILDER_H264_H

#include "d3d12_video_encoder_nalu_writer_h264.h"
#include "d3d12_video_encoder_bitstream_builder.h"

class d3d12_video_bitstream_builder_h264 : public d3d12_video_bitstream_builder_interface
{

 public:
   d3d12_video_bitstream_builder_h264() {};
   ~d3d12_video_bitstream_builder_h264() {};

   void build_sps(const D3D12_VIDEO_ENCODER_PROFILE_H264 &               profile,
                  const D3D12_VIDEO_ENCODER_LEVELS_H264 &                level,
                  const DXGI_FORMAT &                                    inputFmt,
                  const D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_H264 &   codecConfig,
                  const D3D12_VIDEO_ENCODER_SEQUENCE_GOP_STRUCTURE_H264 &gopConfig,
                  uint32_t                                               seq_parameter_set_id,
                  uint32_t                                               max_num_ref_frames,
                  D3D12_VIDEO_ENCODER_PICTURE_RESOLUTION_DESC            sequenceTargetResolution,
                  std::vector<uint8_t> &                                 headerBitstream,
                  std::vector<uint8_t>::iterator                         placingPositionStart,
                  size_t &                                               writtenBytes);

   void build_pps(const D3D12_VIDEO_ENCODER_PROFILE_H264 &                   profile,
                  const D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_H264 &       codecConfig,
                  const D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_H264 &pictureControl,
                  uint32_t                                                   pic_parameter_set_id,
                  uint32_t                                                   seq_parameter_set_id,
                  std::vector<uint8_t> &                                     headerBitstream,
                  std::vector<uint8_t>::iterator                             placingPositionStart,
                  size_t &                                                   writtenBytes);

   void write_end_of_stream_nalu(std::vector<uint8_t> &         headerBitstream,
                                 std::vector<uint8_t>::iterator placingPositionStart,
                                 size_t &                       writtenBytes);
   void write_end_of_sequence_nalu(std::vector<uint8_t> &         headerBitstream,
                                   std::vector<uint8_t>::iterator placingPositionStart,
                                   size_t &                       writtenBytes);

   void print_pps(const H264_PPS &pps);
   void print_sps(const H264_SPS &sps);

   uint32_t m_writtenSPSCount = 0;
   uint32_t m_writtenPPSCount = 0;

   uint32_t get_sps_count()
   {
      return m_writtenSPSCount;
   };
   uint32_t get_pps_count()
   {
      return m_writtenPPSCount;
   };

 private:
   d3d12_video_nalu_writer_h264 m_h264Encoder;
};

#endif
