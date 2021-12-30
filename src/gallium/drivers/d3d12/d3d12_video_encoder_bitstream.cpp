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

#include <climits>
#include "d3d12_video_encoder_bitstream.h"

D3D12VideoBitstream::D3D12VideoBitstream()
{
    m_pBitsBuffer = nullptr;
    m_uiBitsBufferSize = 0;
    m_iBitsToGo = 32;
    m_uintEncBuffer = 0;
    m_bExternalBuffer = false;
    m_bBufferOverflow = false;
    m_bPreventStartCode = false;
    m_bAllowReallocate = false;
}

D3D12VideoBitstream::~D3D12VideoBitstream()
{
    if (!m_bExternalBuffer)
    {
        DELETE_ARRAY(m_pBitsBuffer);
    }
}

int32_t
D3D12VideoBitstream::GetExpGolomb0CodeLength (uint32_t uiVal)
{
    int32_t iLen = 0;
    uiVal++;

    if (uiVal >= 0x10000)
    {
        uiVal >>= 16;
        iLen += 16;
    }
    if (uiVal >= 0x100)
    {
        uiVal >>= 8;
        iLen += 8;
    }

    VERIFY_IS_TRUE(uiVal < 256);

    return iLen + m_iLog_2_N[uiVal];
}

void
D3D12VideoBitstream::Exp_Golomb_ue (uint32_t uiVal)
{
    if (uiVal != UINT_MAX)
    {
        int32_t iLen = GetExpGolomb0CodeLength (uiVal);
        PutBits ((iLen << 1) + 1, uiVal + 1);
    }
    else
    {
        PutBits (32, 0);
        PutBits (1, 1);
        PutBits (32, 1);
    }
}

void
D3D12VideoBitstream::Exp_Golomb_se (int32_t iVal)
{
    if (iVal > 0)
    {
        Exp_Golomb_ue ((iVal << 1) - 1);
    }
    else
    {
        Exp_Golomb_ue (((-iVal) << 1) - (iVal == INT_MIN));
    }
}

void
D3D12VideoBitstream::SetupBitStream(
    uint32_t uiInitBufferSize,
    uint8_t *pBuffer
    )
{
    m_pBitsBuffer = pBuffer;
    m_uiBitsBufferSize = uiInitBufferSize;
    m_uiOffset = 0;
    memset(m_pBitsBuffer, 0, m_uiBitsBufferSize);
    m_bExternalBuffer = true;
    m_bAllowReallocate = false;
}

bool
D3D12VideoBitstream::CreateBitStream(uint32_t uiInitBufferSize)
{
    VERIFY_IS_TRUE((uiInitBufferSize) >= 4 && !(uiInitBufferSize & 3));

    m_pBitsBuffer = (uint8_t *)new uint8_t[uiInitBufferSize];

    if (nullptr == m_pBitsBuffer)
    {
        return false;
    }

    m_uiBitsBufferSize = uiInitBufferSize;
    m_uiOffset = 0;
    memset(m_pBitsBuffer, 0, m_uiBitsBufferSize);
    m_bExternalBuffer = false;

    return true;
}

bool
D3D12VideoBitstream::ReallocateBuffer()
{
    uint32_t uiBufferSize = m_uiBitsBufferSize * 3 / 2;
    uint8_t *pNewBuffer = (uint8_t *)new uint8_t[uiBufferSize];

    if (nullptr == pNewBuffer)
    {
        return false;
    }

    memcpy(pNewBuffer, m_pBitsBuffer, m_uiOffset * sizeof(uint8_t));
    DELETE_ARRAY(m_pBitsBuffer);
    m_pBitsBuffer = pNewBuffer;
    m_uiBitsBufferSize = uiBufferSize;
    return true;
}

bool
D3D12VideoBitstream::VerifyBuffer(uint32_t uiBytesToWrite)
{
    if (!m_bBufferOverflow)
    {
        if (m_uiOffset + uiBytesToWrite > m_uiBitsBufferSize)
        {
            if (!m_bAllowReallocate || !ReallocateBuffer())
            {
                m_bBufferOverflow = true;
                return false;
            }
        }

        return true;
    }

    return false;
}

void
D3D12VideoBitstream::IncCurrentOffset(int32_t dwOffset)
{
    VERIFY_IS_TRUE(32 == m_iBitsToGo && m_uiOffset < m_uiBitsBufferSize);
    m_uiOffset += dwOffset;
}

void
D3D12VideoBitstream::GetCurrentBufferPostionAndSize(uint8_t **ppCurrBufPos,
        int32_t *pdwLeftBufSize)
{
    VERIFY_IS_TRUE(32 == m_iBitsToGo && m_uiOffset < m_uiBitsBufferSize);
    *ppCurrBufPos = m_pBitsBuffer + m_uiOffset;
    *pdwLeftBufSize = m_uiBitsBufferSize - m_uiOffset;
}

void
D3D12VideoBitstream::Attach(uint8_t *pBitsBuffer, uint32_t uiBufferSize)
{
    m_pBitsBuffer = pBitsBuffer;
    m_uiBitsBufferSize = uiBufferSize;
    m_bExternalBuffer = true;
    m_bBufferOverflow = false;
    m_bAllowReallocate = false;

    Clear();
}

void
D3D12VideoBitstream::WriteByteStartCodePrevention(uint8_t u8Val)
{
    int32_t iOffset = m_uiOffset;
    uint8_t *pBuffer = m_pBitsBuffer + iOffset;

    if (m_bPreventStartCode && iOffset > 1)
    {
        if (((u8Val & 0xfc) | pBuffer[-2] | pBuffer[-1]) == 0)
        {
            *pBuffer++ = 3;
            iOffset++;
        }
    }

    *pBuffer = u8Val;
    iOffset++;

    m_uiOffset = iOffset;
}

#define WRITE_BYTE(byte) WriteByteStartCodePrevention(byte)

void
D3D12VideoBitstream::PutBits(
    int32_t uiBitsCount,
    uint32_t iBitsVal
    )
{
    VERIFY_IS_TRUE(uiBitsCount <= 32);

    if (uiBitsCount < m_iBitsToGo)
    {
        m_uintEncBuffer |= (iBitsVal << (m_iBitsToGo - uiBitsCount));
        m_iBitsToGo -= uiBitsCount;
    }
    else if (VerifyBuffer(4))
    {
        int32_t iLeftOverBits = uiBitsCount - m_iBitsToGo;
        m_uintEncBuffer |= (iBitsVal >> iLeftOverBits);

        uint8_t *temp = (uint8_t *)(&m_uintEncBuffer);
        WRITE_BYTE(*(temp + 3));
        WRITE_BYTE(*(temp + 2));
        WRITE_BYTE(*(temp + 1));
        WRITE_BYTE(*temp);

        m_uintEncBuffer = 0;
        m_iBitsToGo = 32 - iLeftOverBits;

        if (iLeftOverBits > 0)
        {
            m_uintEncBuffer = (iBitsVal << (32 - iLeftOverBits));
        }
    }
}

void
D3D12VideoBitstream::Flush()
{
    VERIFY_IS_TRUE(IsByteAligned());

    uint32_t temp = (uint32_t)(32 - m_iBitsToGo);

    if (!VerifyBuffer(temp >> 3))
    {
        return;
    }

    while (temp > 0)
    {
        WRITE_BYTE((uint8_t)(m_uintEncBuffer >> 24));
        m_uintEncBuffer <<= 8;
        temp -= 8;
    }

    m_iBitsToGo = 32;
    m_uintEncBuffer = 0;
}

void
D3D12VideoBitstream::AppendByteStream(D3D12VideoBitstream *pStream)
{
    VERIFY_IS_TRUE(pStream->IsByteAligned() && IsByteAligned());
    VERIFY_IS_TRUE(m_iBitsToGo == 32);

    uint8_t *pDst = m_pBitsBuffer + m_uiOffset;
    uint8_t *pSrc = pStream->GetBitstreamBuffer();
    uint32_t uiLen = (uint32_t)pStream->GetByteCount();

    if (!VerifyBuffer(uiLen))
    {
        return;
    }

    memcpy(pDst, pSrc, uiLen);
    m_uiOffset += uiLen;
}