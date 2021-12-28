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

#include "d3d12_video_enc.h"
#include "d3d12_video_enc_h264.h"

void d3d12_video_encoder_update_current_rate_control_h264(struct d3d12_video_encoder* pD3D12Enc, pipe_h264_enc_picture_desc *picture)
{
   pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc = { };
   pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_FrameRate.Numerator = picture->rate_ctrl[0].frame_rate_num;
   pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_FrameRate.Denominator = picture->rate_ctrl[0].frame_rate_den;

   switch (picture->rate_ctrl[0].rate_ctrl_method) 
   {
      case PIPE_H2645_ENC_RATE_CONTROL_METHOD_VARIABLE_SKIP:
      case PIPE_H2645_ENC_RATE_CONTROL_METHOD_VARIABLE:
      {
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Mode = D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_VBR;
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_VBR.TargetAvgBitRate = picture->rate_ctrl[0].target_bitrate;
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_VBR.PeakBitRate = picture->rate_ctrl[0].peak_bitrate;
      } break;
      case PIPE_H2645_ENC_RATE_CONTROL_METHOD_CONSTANT_SKIP:
      case PIPE_H2645_ENC_RATE_CONTROL_METHOD_CONSTANT:
      {
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Mode = D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_CBR;
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_CBR.TargetBitRate = picture->rate_ctrl[0].target_bitrate;
      } break;
      case PIPE_H2645_ENC_RATE_CONTROL_METHOD_DISABLE:
      {
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Mode = D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_CQP;
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_CQP.ConstantQP_FullIntracodedFrame = picture->quant_i_frames;
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_CQP.ConstantQP_InterPredictedFrame_PrevRefOnly = picture->quant_p_frames;
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_CQP.ConstantQP_InterPredictedFrame_BiDirectionalRef = picture->quant_b_frames;
      } break;
      default:
      {
         D3D12_LOG_DBG("[D3D12 Video Driver] d3d12_video_encoder_update_current_rate_control_h264 invalid RC config, using default RC CQP mode\n");
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Mode = D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_CQP;
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_CQP.ConstantQP_FullIntracodedFrame = 30;
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_CQP.ConstantQP_InterPredictedFrame_PrevRefOnly = 30;
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_CQP.ConstantQP_InterPredictedFrame_BiDirectionalRef = 30;
      } break;
   }

   // TODO: Add support for rest of advanced control flags and settings from pipe_h264_enc_rate_control and set the D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAGS m_Flags accordingly
}

void d3d12_video_encoder_update_current_h264_slices_configuration(struct d3d12_video_encoder* pD3D12Enc, pipe_h264_enc_picture_desc *picture)
{
   // There's no config filled for this from above layers, so default for now.
   pD3D12Enc->m_currentEncodeConfig.m_encoderSliceConfigDesc.m_SlicesPartition_H264 = { }; 
   pD3D12Enc->m_currentEncodeConfig.m_encoderSliceConfigDesc.m_SlicesPartition_H264.NumberOfSlicesPerFrame = 4;
   pD3D12Enc->m_currentEncodeConfig.m_encoderSliceConfigMode = D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_UNIFORM_PARTITIONING_SUBREGIONS_PER_FRAME;
}

D3D12_VIDEO_ENCODER_MOTION_ESTIMATION_PRECISION_MODE d3d12_video_encoder_convert_h264_motion_configuration(struct d3d12_video_encoder* pD3D12Enc, pipe_h264_enc_picture_desc *picture)
{
   return D3D12_VIDEO_ENCODER_MOTION_ESTIMATION_PRECISION_MODE_MAXIMUM;
}

D3D12_VIDEO_ENCODER_LEVELS_H264 d3d12_video_encoder_convert_level_h264(UINT h264SpecLevel)
{
   switch(h264SpecLevel)
   {
      case 10:
      {
         return D3D12_VIDEO_ENCODER_LEVELS_H264_1;
      } break;
      case 11:
      {
         return D3D12_VIDEO_ENCODER_LEVELS_H264_11;
      } break;
      case 12:
      {
         return D3D12_VIDEO_ENCODER_LEVELS_H264_12;
      } break;
      case 13:
      {
         return D3D12_VIDEO_ENCODER_LEVELS_H264_13;
      } break;
      case 20:
      {
         return D3D12_VIDEO_ENCODER_LEVELS_H264_2;
      } break;
      case 21:
      {
         return D3D12_VIDEO_ENCODER_LEVELS_H264_21;
      } break;
      case 22:
      {
         return D3D12_VIDEO_ENCODER_LEVELS_H264_22;
      } break;
      case 30:
      {
         return D3D12_VIDEO_ENCODER_LEVELS_H264_3;
      } break;
      case 31:
      {
         return D3D12_VIDEO_ENCODER_LEVELS_H264_31;
      } break;
      case 32:
      {
         return D3D12_VIDEO_ENCODER_LEVELS_H264_32;
      } break;
      case 40:
      {
         return D3D12_VIDEO_ENCODER_LEVELS_H264_4;
      } break;
      case 41:
      {
         return D3D12_VIDEO_ENCODER_LEVELS_H264_41;
      } break;
      case 42:
      {
         return D3D12_VIDEO_ENCODER_LEVELS_H264_42;
      } break;
      case 50:
      {
         return D3D12_VIDEO_ENCODER_LEVELS_H264_5;
      } break;
      case 51:
      {
         return D3D12_VIDEO_ENCODER_LEVELS_H264_51;
      } break;
      case 52:
      {
         return D3D12_VIDEO_ENCODER_LEVELS_H264_52;
      } break;
      case 60:
      {
         return D3D12_VIDEO_ENCODER_LEVELS_H264_6;
      } break;
      case 61:
      {
         return D3D12_VIDEO_ENCODER_LEVELS_H264_61;
      } break;
      case 62:
      {
         return D3D12_VIDEO_ENCODER_LEVELS_H264_62;
      } break;
      default:
      {
         D3D12_LOG_ERROR("[D3D12 Video Driver Error] d3d12_video_encoder_convert_level_h264 - Unsupported level\n");
      } break;            
   }
}

void d3d12_video_encoder_convert_from_d3d12_level_h264(D3D12_VIDEO_ENCODER_LEVELS_H264 level12, UINT &specLevel, UINT &constraint_set3_flag)
{
    specLevel = 0;
    constraint_set3_flag = 0;

    switch(level12)
    {
        case D3D12_VIDEO_ENCODER_LEVELS_H264_1:
        {
            specLevel = 10;
        } break;
        case D3D12_VIDEO_ENCODER_LEVELS_H264_1b:
        {
            specLevel = 11;
            constraint_set3_flag = 1;
        } break;
        case D3D12_VIDEO_ENCODER_LEVELS_H264_11:
        {
            specLevel = 11;
        } break;
        case D3D12_VIDEO_ENCODER_LEVELS_H264_12:
        {
            specLevel = 12;
        } break;
        case D3D12_VIDEO_ENCODER_LEVELS_H264_13:
        {
            specLevel = 13;
        } break;
        case D3D12_VIDEO_ENCODER_LEVELS_H264_2:
        {
            specLevel = 20;
        } break;
        case D3D12_VIDEO_ENCODER_LEVELS_H264_21:
        {
            specLevel = 21;
        } break;
        case D3D12_VIDEO_ENCODER_LEVELS_H264_22:
        {
            specLevel = 22;
        } break;
        case D3D12_VIDEO_ENCODER_LEVELS_H264_3:
        {
            specLevel = 30;
        } break;
        case D3D12_VIDEO_ENCODER_LEVELS_H264_31:
        {
            specLevel = 31;
        } break;
        case D3D12_VIDEO_ENCODER_LEVELS_H264_32:
        {
            specLevel = 32;
        } break;
        case D3D12_VIDEO_ENCODER_LEVELS_H264_4:
        {
            specLevel = 40;
        } break;
        case D3D12_VIDEO_ENCODER_LEVELS_H264_41:
        {
            specLevel = 41;
        } break;
        case D3D12_VIDEO_ENCODER_LEVELS_H264_42:
        {
            specLevel = 42;
        } break;
        case D3D12_VIDEO_ENCODER_LEVELS_H264_5:
        {
            specLevel = 50;
        } break;
        case D3D12_VIDEO_ENCODER_LEVELS_H264_51:
        {
            specLevel = 51;
        } break;
        case D3D12_VIDEO_ENCODER_LEVELS_H264_52:
        {
            specLevel = 52;
        } break;
        case D3D12_VIDEO_ENCODER_LEVELS_H264_6:
        {
            specLevel = 60;
        } break;
        case D3D12_VIDEO_ENCODER_LEVELS_H264_61:
        {
            specLevel = 61;
        } break;
        case D3D12_VIDEO_ENCODER_LEVELS_H264_62:
        {
            specLevel = 62;
        } break;
        default:
        {
            D3D12_LOG_ERROR("Not a supported level\n"); // Not a supported level
        } break;            
    }
}

D3D12_VIDEO_ENCODER_SEQUENCE_GOP_STRUCTURE_H264 d3d12_video_encoder_convert_h264_gop_configuration(struct d3d12_video_encoder* pD3D12Enc, pipe_h264_enc_picture_desc *picture)
{
   assert(picture->pic_order_cnt_type != 1); // Not supported by D3D12 Encode.
   
   // Note that, for infinite GOPS, max_pic_order_cnt_lsb must be greater than the full video count - 1. Otherwise it might try to address previous reference pictures in between [0..255] intervals (if using max_pic_order_cnt_lsb = 256). 
   const UINT GOPLength = picture->gop_size; // TODO: This is multiplied by gop_coeff in above layer, it's not necessarily idr_period.
   uint PPicturePeriod = 2; // TODO: Figure out how to deduce this from gop_size and i/p_remaining.

   const UINT max_pic_order_cnt_lsb = (GOPLength > 0) ? 256u : 32768u;
   const UINT max_max_frame_num = (GOPLength > 0) ? 256u : 32768u;
   double log2_max_frame_num_minus4 = std::max(0.0, std::ceil(std::log2(max_max_frame_num)) - 4);
   double log2_max_pic_order_cnt_lsb_minus4 = std::max(0.0, std::ceil(std::log2(max_pic_order_cnt_lsb)) - 4);
   assert(log2_max_frame_num_minus4 < UCHAR_MAX);
   assert(log2_max_pic_order_cnt_lsb_minus4 < UCHAR_MAX);   
   assert(picture->pic_order_cnt_type < UCHAR_MAX);   
   
   D3D12_VIDEO_ENCODER_SEQUENCE_GOP_STRUCTURE_H264 config = 
   {
      GOPLength,
      PPicturePeriod,
      static_cast<UCHAR>(picture->pic_order_cnt_type),
      static_cast<UCHAR>(log2_max_frame_num_minus4),
      static_cast<UCHAR>(log2_max_pic_order_cnt_lsb_minus4)
   };
   return config;
}

D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_H264 d3d12_video_encoder_convert_h264_codec_configuration(struct d3d12_video_encoder* pD3D12Enc, pipe_h264_enc_picture_desc *picture)
{
   // TODO: Set flags and rest based on picture contents (ie. cabac/cavlc, etc), using defaults for now

   D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_H264 config = 
   {
      D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_H264_FLAG_NONE,
      D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_H264_DIRECT_MODES_DISABLED,
      D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_H264_SLICES_DEBLOCKING_MODE_0_ALL_LUMA_CHROMA_SLICE_BLOCK_EDGES_ALWAYS_FILTERED,
   };
   return config;
}

void d3d12_video_encoder_update_current_encoder_config_state_h264(struct d3d12_video_encoder* pD3D12Enc, struct pipe_video_buffer *srcTexture, struct pipe_picture_desc *picture)
{
   // TODO: Check for caps and have debug messages if requested config not supported, otherwise it might fail execution without useful info with unsupported configs.
   // TODO: Check resolution caps including ratios and multiple requirements

   struct pipe_h264_enc_picture_desc *h264Pic = (struct pipe_h264_enc_picture_desc *)picture;

   // Set requested config
   pD3D12Enc->m_currentEncodeConfig.m_currentRequestedConfig = *h264Pic;

   // Set codec
   pD3D12Enc->m_currentEncodeConfig.m_encoderCodecDesc = D3D12_VIDEO_ENCODER_CODEC_H264;

   // Set input format
   pD3D12Enc->m_currentEncodeConfig.m_encodeFormatInfo = { };
   pD3D12Enc->m_currentEncodeConfig.m_encodeFormatInfo.Format = D3D12VideoFormatHelper::d3d12_convert_pipe_video_profile_to_dxgi_format(pD3D12Enc->base.profile);
   VERIFY_SUCCEEDED(pD3D12Enc->m_pD3D12Screen->dev->CheckFeatureSupport(D3D12_FEATURE_FORMAT_INFO, &pD3D12Enc->m_currentEncodeConfig.m_encodeFormatInfo, sizeof(pD3D12Enc->m_currentEncodeConfig.m_encodeFormatInfo)));

   // Set resolution
   pD3D12Enc->m_currentEncodeConfig.m_currentResolution.Width = srcTexture->width;
   pD3D12Enc->m_currentEncodeConfig.m_currentResolution.Height = srcTexture->height;

   // Set profile
   pD3D12Enc->m_currentEncodeConfig.m_encoderProfileDesc.m_H264Profile = d3d12_video_encoder_convert_profile_to_d3d12_enc_profile_h264(pD3D12Enc->base.profile);

   // Set level
   pD3D12Enc->m_currentEncodeConfig.m_encoderLevelDesc.m_H264LevelSetting = d3d12_video_encoder_convert_level_h264(pD3D12Enc->base.level);

   // Set codec config
   pD3D12Enc->m_currentEncodeConfig.m_encoderCodecSpecificConfigDesc.m_H264Config = d3d12_video_encoder_convert_h264_codec_configuration(pD3D12Enc, h264Pic);

   // Set slices config
   d3d12_video_encoder_update_current_h264_slices_configuration(pD3D12Enc, h264Pic);

   // Set GOP config
   pD3D12Enc->m_currentEncodeConfig.m_encoderGOPConfigDesc.m_H264GroupOfPictures = d3d12_video_encoder_convert_h264_gop_configuration(pD3D12Enc, h264Pic);

   // Set rate control
   d3d12_video_encoder_update_current_rate_control_h264(pD3D12Enc, h264Pic);
   
   // m_currentEncodeConfig.m_encoderPicParamsDesc pic params are set in d3d12_video_encoder_reconfigure_encoder_objects after re-allocating objects if needed

   // Set motion estimation config
   pD3D12Enc->m_currentEncodeConfig.m_encoderMotionPrecisionLimit = d3d12_video_encoder_convert_h264_motion_configuration(pD3D12Enc, h264Pic);
}

D3D12_VIDEO_ENCODER_PROFILE_H264 d3d12_video_encoder_convert_profile_to_d3d12_enc_profile_h264(enum pipe_video_profile profile)
{
   switch (profile)
   {
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_CONSTRAINED_BASELINE:
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_MAIN:
      {
         return D3D12_VIDEO_ENCODER_PROFILE_H264_MAIN;
         
      } break;
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH:
      {
         return D3D12_VIDEO_ENCODER_PROFILE_H264_HIGH;
      } break;
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH10:
      {
         return D3D12_VIDEO_ENCODER_PROFILE_H264_HIGH_10;
      } break;
      // No support for other profiles below
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_BASELINE:
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_EXTENDED:
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH422:
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH444:
      default:
         D3D12_LOG_ERROR("[D3D12 Video Driver Error] d3d12_video_encoder_update_current_encoder_config_state_h264 - Unsupported profile\n");
         return static_cast<D3D12_VIDEO_ENCODER_PROFILE_H264>(0);
   }
}