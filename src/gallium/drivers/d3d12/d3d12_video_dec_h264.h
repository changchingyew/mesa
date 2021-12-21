
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

#include "d3d12_video_dec_types.h"

typedef unsigned short USHORT;


// From DXVA spec regarding DXVA_PicEntry_H264 entries: 
// Entries that will not be used for decoding the current picture, or any subsequent pictures,
// are indicated by setting bPicEntry to 0xFF. 
// If bPicEntry is not 0xFF, the entry may be used as a reference surface for decoding the current picture or
// a subsequent picture (in decoding order).
constexpr UINT16 DXVA_H264_INVALID_PICTURE_INDEX = 0x7F; // This corresponds to DXVA_PicEntry_H264.Index7Bits ; Not to be confused with the invalid value for DXVA_PicEntry_H264.bPicEntry full char value
constexpr UINT16 DXVA_H264_INVALID_PICTURE_ENTRY_VALUE = 0xFF; // This corresponds to DXVA_PicEntry_H264.bPicEntry

constexpr unsigned int DXVA_H264_START_CODE = 0x000001; // 3 byte start code
constexpr unsigned int DXVA_H264_START_CODE_LEN_BITS = 24; // 3 byte start code

constexpr unsigned int D3D12_VIDEO_H264_MB_IN_PIXELS = 16;

/* H.264/AVC picture entry data structure */
typedef struct _DXVA_PicEntry_H264 {
  union {
    struct {
      UCHAR  Index7Bits      : 7;
      UCHAR  AssociatedFlag  : 1;
    };
    UCHAR  bPicEntry;
  };
} DXVA_PicEntry_H264, *LPDXVA_PicEntry_H264;  /* 1 byte */

/* H.264/AVC picture parameters structure */
typedef struct _DXVA_PicParams_H264 {
  USHORT  wFrameWidthInMbsMinus1;
  USHORT  wFrameHeightInMbsMinus1;
  DXVA_PicEntry_H264  CurrPic; /* flag is bot field flag */
  UCHAR   num_ref_frames;

  union {
    struct {
      USHORT  field_pic_flag                 : 1;
      USHORT  MbaffFrameFlag                 : 1;
      USHORT  residual_colour_transform_flag : 1;
      USHORT  sp_for_switch_flag             : 1;
      USHORT  chroma_format_idc              : 2;
      USHORT  RefPicFlag                     : 1;
      USHORT  constrained_intra_pred_flag    : 1;

      USHORT  weighted_pred_flag             : 1;
      USHORT  weighted_bipred_idc            : 2;
      USHORT  MbsConsecutiveFlag             : 1;
      USHORT  frame_mbs_only_flag            : 1;
      USHORT  transform_8x8_mode_flag        : 1;
      USHORT  MinLumaBipredSize8x8Flag       : 1;
      USHORT  IntraPicFlag                   : 1;
    };
    USHORT  wBitFields;
  };
  UCHAR  bit_depth_luma_minus8;
  UCHAR  bit_depth_chroma_minus8;

  USHORT Reserved16Bits;
  UINT   StatusReportFeedbackNumber;

  DXVA_PicEntry_H264  RefFrameList[16]; /* flag LT */
  INT    CurrFieldOrderCnt[2];
  INT    FieldOrderCntList[16][2];

  CHAR   pic_init_qs_minus26;
  CHAR   chroma_qp_index_offset;   /* also used for QScb */
  CHAR   second_chroma_qp_index_offset; /* also for QScr */
  UCHAR  ContinuationFlag;

/* remainder for parsing */
  CHAR   pic_init_qp_minus26;
  UCHAR  num_ref_idx_l0_active_minus1;
  UCHAR  num_ref_idx_l1_active_minus1;
  UCHAR  Reserved8BitsA;

  USHORT FrameNumList[16];
  UINT   UsedForReferenceFlags;
  USHORT NonExistingFrameFlags;
  USHORT frame_num;

  UCHAR  log2_max_frame_num_minus4;
  UCHAR  pic_order_cnt_type;
  UCHAR  log2_max_pic_order_cnt_lsb_minus4;
  UCHAR  delta_pic_order_always_zero_flag;

  UCHAR  direct_8x8_inference_flag;
  UCHAR  entropy_coding_mode_flag;
  UCHAR  pic_order_present_flag;
  UCHAR  num_slice_groups_minus1;

  UCHAR  slice_group_map_type;
  UCHAR  deblocking_filter_control_present_flag;
  UCHAR  redundant_pic_cnt_present_flag;
  UCHAR  Reserved8BitsB;

  USHORT slice_group_change_rate_minus1;

  UCHAR  SliceGroupMap[810]; /* 4b/sgmu, Size BT.601 */

} DXVA_PicParams_H264, *LPDXVA_PicParams_H264;

/* H.264/AVC quantization weighting matrix data structure */
typedef struct _DXVA_Qmatrix_H264 {
  UCHAR  bScalingLists4x4[6][16];
  UCHAR  bScalingLists8x8[2][64];

} DXVA_Qmatrix_H264, *LPDXVA_Qmatrix_H264;

/* H.264/AVC slice control data structure - short form */
typedef struct _DXVA_Slice_H264_Short {
  UINT   BSNALunitDataLocation; /* type 1..5 */
  UINT   SliceBytesInBuffer; /* for off-host parse */
  USHORT wBadSliceChopping;  /* for off-host parse */
} DXVA_Slice_H264_Short, *LPDXVA_Slice_H264_Short;

DXVA_PicParams_H264 d3d12_dec_dxva_picparams_from_pipe_picparams_h264(UINT frameNum, pipe_video_profile profile, UINT frameWidth, UINT frameHeight, pipe_h264_picture_desc* pipeDesc);
void d3d12_decoder_get_frame_info_h264(struct d3d12_video_decoder *pD3D12Dec, UINT *pWidth, UINT *pHeight, UINT16 *pMaxDPB);
void d3d12_decoder_prepare_current_frame_references_h264(struct d3d12_video_decoder *pD3D12Dec, ID3D12Resource* pTexture2D, UINT subresourceIndex);
void d3d12_dec_dxva_qmatrix_from_pipe_picparams_h264 (pipe_h264_picture_desc* pPipeDesc, DXVA_Qmatrix_H264 & outMatrixBuffer, bool & outSeq_scaling_matrix_present_flag);
void d3d12_decoder_refresh_dpb_active_references_h264(struct d3d12_video_decoder *pD3D12Dec);
bool get_slice_size_and_offset_h264(size_t sliceIdx, size_t numSlices, std::vector<BYTE> &buf, unsigned int bufferOffset, UINT& outSliceSize, UINT& outSliceOffset);
void d3d12_prepare_dxva_slices_control_h264(struct d3d12_video_decoder *pD3D12Dec, size_t numSlices, std::vector<DXVA_Slice_H264_Short>& pOutSliceControlBuffers);
#endif