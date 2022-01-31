
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

#ifndef D3D12_VIDEO_DEC_H264_H
#define D3D12_VIDEO_DEC_H264_H

#include "d3d12_video_types.h"

// From DXVA spec regarding DXVA_PicEntry_H264 entries:
// Entries that will not be used for decoding the current picture, or any subsequent pictures,
// are indicated by setting bPicEntry to 0xFF.
// If bPicEntry is not 0xFF, the entry may be used as a reference surface for decoding the current picture or
// a subsequent picture (in decoding order).
constexpr uint16_t DXVA_H264_INVALID_PICTURE_INDEX =
   0x7F;   // This corresponds to DXVA_PicEntry_H264.Index7Bits ; Not to be confused with the invalid value for
           // DXVA_PicEntry_H264.bPicEntry full char value
constexpr uint16_t DXVA_H264_INVALID_PICTURE_ENTRY_VALUE = 0xFF;   // This corresponds to DXVA_PicEntry_H264.bPicEntry

constexpr unsigned int DXVA_H264_START_CODE          = 0x000001;   // 3 byte start code
constexpr unsigned int DXVA_H264_START_CODE_LEN_BITS = 24;         // 3 byte start code

constexpr unsigned int D3D12_VIDEO_H264_MB_IN_PIXELS = 16;

/* H.264/AVC picture entry data structure */
typedef struct _DXVA_PicEntry_H264
{
   union
   {
      struct
      {
         uint8_t Index7Bits : 7;
         uint8_t AssociatedFlag : 1;
      };
      uint8_t bPicEntry;
   };
} DXVA_PicEntry_H264, *LPDXVA_PicEntry_H264; /* 1 byte */

/* H.264/AVC picture parameters structure */
typedef struct _DXVA_PicParams_H264
{
   uint16_t           wFrameWidthInMbsMinus1;
   uint16_t           wFrameHeightInMbsMinus1;
   DXVA_PicEntry_H264 CurrPic; /* flag is bot field flag */
   uint8_t            num_ref_frames;

   union
   {
      struct
      {
         uint16_t field_pic_flag : 1;
         uint16_t MbaffFrameFlag : 1;
         uint16_t residual_colour_transform_flag : 1;
         uint16_t sp_for_switch_flag : 1;
         uint16_t chroma_format_idc : 2;
         uint16_t RefPicFlag : 1;
         uint16_t constrained_intra_pred_flag : 1;

         uint16_t weighted_pred_flag : 1;
         uint16_t weighted_bipred_idc : 2;
         uint16_t MbsConsecutiveFlag : 1;
         uint16_t frame_mbs_only_flag : 1;
         uint16_t transform_8x8_mode_flag : 1;
         uint16_t MinLumaBipredSize8x8Flag : 1;
         uint16_t IntraPicFlag : 1;
      };
      uint16_t wBitFields;
   };
   uint8_t bit_depth_luma_minus8;
   uint8_t bit_depth_chroma_minus8;

   uint16_t Reserved16Bits;
   uint32_t StatusReportFeedbackNumber;

   DXVA_PicEntry_H264 RefFrameList[16]; /* flag LT */
   int32_t            CurrFieldOrderCnt[2];
   int32_t            FieldOrderCntList[16][2];

   char    pic_init_qs_minus26;
   char    chroma_qp_index_offset;        /* also used for QScb */
   char    second_chroma_qp_index_offset; /* also for QScr */
   uint8_t ContinuationFlag;

   /* remainder for parsing */
   char    pic_init_qp_minus26;
   uint8_t num_ref_idx_l0_active_minus1;
   uint8_t num_ref_idx_l1_active_minus1;
   uint8_t Reserved8BitsA;

   uint16_t FrameNumList[16];
   uint32_t UsedForReferenceFlags;
   uint16_t NonExistingFrameFlags;
   uint16_t frame_num;

   uint8_t log2_max_frame_num_minus4;
   uint8_t pic_order_cnt_type;
   uint8_t log2_max_pic_order_cnt_lsb_minus4;
   uint8_t delta_pic_order_always_zero_flag;

   uint8_t direct_8x8_inference_flag;
   uint8_t entropy_coding_mode_flag;
   uint8_t pic_order_present_flag;
   uint8_t num_slice_groups_minus1;

   uint8_t slice_group_map_type;
   uint8_t deblocking_filter_control_present_flag;
   uint8_t redundant_pic_cnt_present_flag;
   uint8_t Reserved8BitsB;

   uint16_t slice_group_change_rate_minus1;

   uint8_t SliceGroupMap[810]; /* 4b/sgmu, Size BT.601 */

} DXVA_PicParams_H264, *LPDXVA_PicParams_H264;

/* H.264/AVC quantization weighting matrix data structure */
typedef struct _DXVA_Qmatrix_H264
{
   uint8_t bScalingLists4x4[6][16];
   uint8_t bScalingLists8x8[2][64];

} DXVA_Qmatrix_H264, *LPDXVA_Qmatrix_H264;

/* H.264/AVC slice control data structure - short form */
typedef struct _DXVA_Slice_H264_Short
{
   uint32_t BSNALunitDataLocation; /* type 1..5 */
   uint32_t SliceBytesInBuffer;    /* for off-host parse */
   uint16_t wBadSliceChopping;     /* for off-host parse */
} DXVA_Slice_H264_Short, *LPDXVA_Slice_H264_Short;

DXVA_PicParams_H264
d3d12_video_decoder_dxva_picparams_from_pipe_picparams_h264(uint32_t                frameNum,
                                                            pipe_video_profile      profile,
                                                            uint32_t                frameWidth,
                                                            uint32_t                frameHeight,
                                                            pipe_h264_picture_desc *pipeDesc);
void
d3d12_video_decoder_get_frame_info_h264(
   struct d3d12_video_decoder *pD3D12Dec, uint32_t *pWidth, uint32_t *pHeight, uint16_t *pMaxDPB, bool &isInterlaced);
void
d3d12_video_decoder_prepare_current_frame_references_h264(struct d3d12_video_decoder *pD3D12Dec,
                                                          ID3D12Resource *            pTexture2D,
                                                          uint32_t                    subresourceIndex);
void
d3d12_video_decoder_dxva_qmatrix_from_pipe_picparams_h264(pipe_h264_picture_desc *pPipeDesc,
                                                          DXVA_Qmatrix_H264 &     outMatrixBuffer,
                                                          bool &                  outSeq_scaling_matrix_present_flag);
void
d3d12_video_decoder_refresh_dpb_active_references_h264(struct d3d12_video_decoder *pD3D12Dec);
bool
d3d12_video_decoder_get_slice_size_and_offset_h264(size_t                sliceIdx,
                                                   size_t                numSlices,
                                                   std::vector<uint8_t> &buf,
                                                   unsigned int          bufferOffset,
                                                   uint32_t &            outSliceSize,
                                                   uint32_t &            outSliceOffset);
void
d3d12_video_decoder_prepare_dxva_slices_control_h264(struct d3d12_video_decoder *        pD3D12Dec,
                                                     size_t                              numSlices,
                                                     std::vector<DXVA_Slice_H264_Short> &pOutSliceControlBuffers);
#endif
