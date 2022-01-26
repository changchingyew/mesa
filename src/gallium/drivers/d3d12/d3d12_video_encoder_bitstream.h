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

#ifndef D3D12_VIDEO_ENC_BITSTREAM_H
#define D3D12_VIDEO_ENC_BITSTREAM_H

#include "d3d12_video_types.h"

#define DELETE_ARRAY(ptr)                                                                                              \
   {                                                                                                                   \
      if (ptr) {                                                                                                       \
         delete[](ptr);                                                                                                \
         (ptr) = NULL;                                                                                                 \
      }                                                                                                                \
   }

class D3D12VideoBitstream
{
 public:
   D3D12VideoBitstream();
   ~D3D12VideoBitstream();

 public:
   void GetCurrentBufferPostionAndSize(uint8_t **ppCurrBufPos, int32_t *pdwLeftBufSize);
   void IncCurrentOffset(int32_t dwOffset);
   bool CreateBitStream(uint32_t uiInitBufferSize);
   void SetupBitStream(uint32_t uiInitBufferSize, uint8_t *pBuffer);
   void Attach(uint8_t *pBitsBuffer, uint32_t uiBufferSize);
   void PutBits(int32_t uiBitsCount, uint32_t iBitsVal);
   void Flush();
   void Exp_Golomb_ue(uint32_t uiVal);
   void Exp_Golomb_se(int32_t iVal);

   inline void Clear()
   {
      m_iBitsToGo     = 32;
      m_uiOffset      = 0;
      m_uintEncBuffer = 0;
   };

   void AppendByteStream(D3D12VideoBitstream *pStream);

   void SetStartCodePrevention(bool bSCP)
   {
      m_bPreventStartCode = bSCP;
   }
   int32_t GetBitsCount()
   {
      return m_uiOffset * 8 + (32 - m_iBitsToGo);
   }
   int32_t GetByteCount()
   {
      return m_uiOffset + ((32 - m_iBitsToGo) >> 3);
   }
   uint8_t *GetBitstreamBuffer()
   {
      return m_pBitsBuffer;
   }
   bool IsByteAligned()
   {
      if (m_bBufferOverflow) {
         m_iBitsToGo = 32;
      }
      return !(m_iBitsToGo & 7);
   }
   int32_t GetNumBitsForByteAlign()
   {
      return (m_iBitsToGo & 7);
   }
   bool GetStartCodePreventionStatus()
   {
      return m_bPreventStartCode;
   }
   bool VerifyBuffer(uint32_t uiBytesToWrite);

 public:
   bool m_bBufferOverflow;
   bool m_bAllowReallocate;

 private:
   void    WriteByteStartCodePrevention(uint8_t u8Val);
   bool    ReallocateBuffer();
   int32_t GetExpGolomb0CodeLength(uint32_t uiVal);

   const uint8_t m_iLog_2_N[256] = {
      0, 0, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5,
      5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
      6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
      6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
      7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
      7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
      7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7
   };

 private:
   uint8_t *m_pBitsBuffer;
   uint32_t m_uiBitsBufferSize;
   uint32_t m_uiOffset;

   bool     m_bExternalBuffer;
   uint32_t m_uintEncBuffer;
   int32_t  m_iBitsToGo;

   bool m_bPreventStartCode;
};

#endif
