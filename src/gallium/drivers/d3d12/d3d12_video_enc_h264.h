
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

#ifndef D3D12_VIDEO_ENC_H264_H
#define D3D12_VIDEO_ENC_H264_H
#include "d3d12_video_types.h"

void d3d12_video_encoder_update_current_encoder_config_state_h264(struct d3d12_video_encoder* pD3D12Enc, struct pipe_video_buffer *srcTexture, struct pipe_picture_desc *picture);
void d3d12_video_encoder_update_current_rate_control_h264(struct d3d12_video_encoder* pD3D12Enc, pipe_h264_enc_picture_desc *picture);
void d3d12_video_encoder_update_current_h264_slices_configuration(struct d3d12_video_encoder* pD3D12Enc, pipe_h264_enc_picture_desc *picture);
void d3d12_video_encoder_update_h264_gop_configuration(struct d3d12_video_encoder* pD3D12Enc, pipe_h264_enc_picture_desc *picture);
D3D12_VIDEO_ENCODER_MOTION_ESTIMATION_PRECISION_MODE d3d12_video_encoder_convert_h264_motion_configuration(struct d3d12_video_encoder* pD3D12Enc, pipe_h264_enc_picture_desc *picture);
D3D12_VIDEO_ENCODER_LEVELS_H264 d3d12_video_encoder_convert_level_h264(UINT h264SpecLevel);
void d3d12_video_encoder_convert_from_d3d12_level_h264(D3D12_VIDEO_ENCODER_LEVELS_H264 level12, UINT &specLevel, UINT &constraint_set3_flag);
D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_H264 d3d12_video_encoder_convert_h264_codec_configuration(struct d3d12_video_encoder* pD3D12Enc, pipe_h264_enc_picture_desc *picture);
D3D12_VIDEO_ENCODER_PROFILE_H264 d3d12_video_encoder_convert_profile_to_d3d12_enc_profile_h264(enum pipe_video_profile profile);

#endif