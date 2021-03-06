/*
 * huffc.c --
 *
 * Code for encoding JPEG data.  Large parts are grabbed from the IJG
 * software, so:
 *
 * Copyright (C) 1991, 1992, Thomas G. Lane.
 * Part of the Independent JPEG Group's software.
 * See the file Copyright for more details.
 *
 * Copyright (c) 1993 Brian C. Smith, The Regents of the University
 * of California
 * All rights reserved.
 * 
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose, without fee, and without written agreement is
 * hereby granted, provided that the above copyright notice and the following
 * two paragraphs appear in all copies of this software.
 * 
 * IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES ARISING OUT
 * OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF
 * CALIFORNIA HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 * THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
 * ON AN "AS IS" BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <assert.h>
#include "jpeg.h"
#include "rle.h"
#include "io.h"
#include "proto.h"

#define RST0 0xD0        /* RST0 marker code */
#define MIN_BUF_FREE 512 /* Min Buffer free for EncodeOneBlock */

/*
 * Lookup table for number of bits is a 8 bit value.  Initialized
 * in HuffEncoderInit.
 */
static int numBitsTable[256];

static int bmask[] = {0x0000,
                      0x00000001, 0x00000003, 0x00000007, 0x0000000F,
                      0x0000001F, 0x0000003F, 0x0000007F, 0x000000FF,
                      0x000001FF, 0x000003FF, 0x000007FF, 0x00000FFF,
                      0x00001FFF, 0x00003FFF, 0x00007FFF, 0x0000FFFF,
                      0x0001FFFF, 0x0003FFFF, 0x0007FFFF, 0x000FFFFF,
                      0x001FFFFF, 0x003FFFFF, 0x007FFFFF, 0x00FFFFFF,
                      0x01FFFFFF, 0x03FFFFFF, 0x07FFFFFF, 0x0FFFFFFF,
                      0x1FFFFFFF, 0x3FFFFFFF, 0x7FFFFFFF, 0xFFFFFFFF};

/*
 * Static variables for output buffering.
 */
static int huffPutBuffer; /* current bit-accumulation buffer */
static int huffPutBits;   /* # of bits now in it */

char outputBuffer[JPEG_BUF_SIZE]; /* output buffer */
int bytesInBuffer;

/*
 *--------------------------------------------------------------
 *
 * FlushBytes --
 *
 *	Output the bytes we've accumulated so far to the output
 *	file.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */
void FlushBytes()
{
    if (bytesInBuffer)
        WriteJpegData(outputBuffer, bytesInBuffer);
    bytesInBuffer = 0;
}

/*
 *--------------------------------------------------------------
 *
 * EmitByteNoFlush --
 *
 *	Write a single byte out to the output buffer.
 *	Assumes the caller is checking for flushing the buffer.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The outp[ut buffer may get flushed.
 *
 *--------------------------------------------------------------
 */
#define EmitByteNoFlush(val)                         \
    {                                                \
        outputBuffer[bytesInBuffer++] = (char)(val); \
    }

/*
 *--------------------------------------------------------------
 *
 * EmitBits --
 *
 *	Code for outputting bits to the file
 *
 *	Only the right 24 bits of huffPutBuffer are used; the valid
 *	bits are left-justified in this part.  At most 16 bits can be
 *	passed to EmitBits in one call, and we never retain more than 7
 *	bits in huffPutBuffer between calls, so 24 bits are
 *	sufficient.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	huffPutBuffer and huffPutBits are updated.
 *
 *--------------------------------------------------------------
 */
static void
    EmitBits(code, size)
        ushort code;
int size;
{
    int putBuffer;
    int putBits;

    /*
     * if size is 0, caller used an invalid Huffman table entry
     */
    assert(size != 0);

    /*
     * Mask off any excess bits in code.
     */
    putBits = (size);
    putBuffer = ((code)&bmask[putBits]);
    putBits += huffPutBits;
    putBuffer <<= 24 - putBits;
    putBuffer |= huffPutBuffer;

    while (putBits >= 8)
    {
        int c;
        c = (putBuffer >> 16) & 0xFF;

        /*
	 * Output whole bytes we've accumulated with byte stuffing
	 */
        EmitByteNoFlush(c);
        if (c == 0xFF)
        {
            EmitByteNoFlush(0);
        }
        putBuffer <<= 8;
        putBits -= 8;
    }

    /*
     * Update global variables
     */
    huffPutBuffer = putBuffer;
    huffPutBits = putBits;
}

/*
 *--------------------------------------------------------------
 *
 * FlushBits --
 *
 *	Flush any remaining bits in the bit buffer. Used before emitting
 *	a marker.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	huffPutBuffer and huffPutBits are reset
 *
 *--------------------------------------------------------------
 */

static void
FlushBits()
{
    /*
     * The first call forces output of any partial bytes.
     * We can then zero the buffer.
     */
    EmitBits((ushort)0x7F, 7);
    huffPutBuffer = 0;
    huffPutBits = 0;
}

/*
 *--------------------------------------------------------------
 *
 * EncodeOneBlock --
 *
 *	Encode a single block's worth of coefficients.
 *	Assume that the DC coefficient has already been
 *	converted to a difference
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */
static void
    EncodeOneBlock(rle, dctbl, actbl)
        RLE *rle;
HuffmanTable *dctbl;
HuffmanTable *actbl;
{
    register int temp, temp2;
    register int nbits;
    register int k, r, i;

    /*
     * Encode the DC coefficient difference per section F.1.2.1
     */
    temp = temp2 = rle->dc;
    if (temp < 0)
    {
        temp = -temp;
        /*
	 * For a negative input, want temp2 = bitwise complement of
	 * abs(input).  This code assumes we are on a two's complement
	 * machine.
	 */
        temp2--;
    }

    /*
     * Find the number of bits needed for the magnitude of the coefficient
     */
    if (temp)
    {
        nbits = numBitsTable[temp & 0xff];
        while (temp >= 256)
        {
            temp >>= 8;
            nbits += numBitsTable[temp & 0xff];
        }
    }
    else
    {
        nbits = 0;
    }

    /*
     * Emit the Huffman-coded symbol for the number of bits
     */
    EmitBits(dctbl->ehufco[nbits], dctbl->ehufsi[nbits]);

    /*
     * Emit that number of bits of the value, if positive,
     * or the complement of its magnitude, if negative.
     */
    if (nbits)
        EmitBits((ushort)temp2, nbits);

    /*
     * Encode the AC coefficients per section F.1.2.2
     */
    k = 1;
    for (i = 0; i < rle->numAC; i++)
    {
        temp = rle->ac[i].value;
        r = rle->ac[i].index - k;
        k = rle->ac[i].index + 1;

        /*
	 * if run length > 15, must emit special run-length-16 codes
	 * (0xF0)
	 */
        while (r > 15)
        {
            EmitBits(actbl->ehufco[0xF0], actbl->ehufsi[0xF0]);
            r -= 16;
        }

        temp2 = temp;
        if (temp < 0)
        {
            temp = -temp;
            temp2--;
        }

        /*
	 * Find the number of bits needed for the magnitude of the
	 * coefficient
	 */
        nbits = numBitsTable[temp & 0xff];
        while (temp >= 256)
        {
            temp >>= 8;
            nbits += numBitsTable[temp & 0xff];
        }

        /*
	 * Emit Huffman symbol for run length / number of bits
	 */
        r = (r << 4) + nbits;
        EmitBits(actbl->ehufco[r], actbl->ehufsi[r]);

        /*
	 * Emit that number of bits of the value, if positive,
	 * or the complement of its magnitude, if negative.
	 */
        EmitBits((ushort)temp2, nbits);
    }

    /*
     * If the last coef(s) wasn't index=63, send end-of-block code.
     */
    if (k != 64)
    {
        EmitBits(actbl->ehufco[0], actbl->ehufsi[0]);
    }
}

/*
 *--------------------------------------------------------------
 *
 * HuffEncoderInit --
 *
 *	Initialize for a Huffman-compressed scan.
 *	This is invoked after writing the SOS marker.
 *
 * Results:
 *	None
 *
 * Side effects:
 *	None
 *
 *--------------------------------------------------------------
 */
void
    HuffEncoderInit(cPtr)
        CompressInfo *cPtr;
{
    short ci, i, nbits, temp;
    JpegComponentInfo *compptr;

    /*
     * Initialize static variables
     */
    huffPutBuffer = 0;
    huffPutBits = 0;

    /*
     * Initialize the output buffer
     */
    bytesInBuffer = 0;

    /*
     * Initialize DC predictions to 0
     */
    for (ci = 0; ci < cPtr->compsInScan; ci++)
    {
        cPtr->lastDcVal[ci] = 0;
    }

    /*
     * Initialize restart stuff
     */
    cPtr->restartsToGo = cPtr->restartInterval;
    cPtr->nextRestartNum = 0;

    /*
     * Initialize number of bits lookup table.
     */
    for (i = 0; i < 256; i++)
    {
        temp = i;
        nbits = 1;
        while (temp >>= 1)
        {
            nbits++;
        }
        numBitsTable[i] = nbits;
    }
}

/*
 *--------------------------------------------------------------
 *
 * EmitRestart --
 *
 *	Emit a restart marker & resynchronize predictions.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Any remaining bits are flushed.
 *
 *--------------------------------------------------------------
 */
static void
    EmitRestart(cPtr)
        CompressInfo *cPtr;
{
    short ci;

    FlushBits();

    EmitByteNoFlush(0xFF);
    EmitByteNoFlush(RST0 + cPtr->nextRestartNum);

    /*
     * Re-initialize DC predictions to 0
     */
    for (ci = 0; ci < cPtr->compsInScan; ci++)
        cPtr->lastDcVal[ci] = 0;

    /*
     * Update restart state
     */
    cPtr->restartsToGo = cPtr->restartInterval;
    cPtr->nextRestartNum++;
    cPtr->nextRestartNum &= 7;
}

/*
 *--------------------------------------------------------------
 *
 * HuffEncode --
 *
 *	Encode and output one MCU's worth of Huffman-compressed
 *	coefficients.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */
void
    HuffEncode(cPtr, mcu)
        CompressInfo *cPtr;
MCU mcu;
{
    short blkn, ci;
    JpegComponentInfo *compptr;
    short temp;
    RLE *rle;

    /*
     * Account for restart interval, emit restart marker if needed
     */
    if (cPtr->restartInterval)
    {
        if (cPtr->restartsToGo == 0)
            EmitRestart(cPtr);
        cPtr->restartsToGo--;
    }
    if (bytesInBuffer >= JPEG_BUF_SIZE - MIN_BUF_FREE)
    {
        FlushBytes();
    }

    for (blkn = 0; blkn < cPtr->blocksInMCU; blkn++)
    {
        ci = cPtr->MCUmembership[blkn];
        compptr = cPtr->curCompInfo[ci];
        rle = mcu + blkn;

        /*
	 * Convert DC value to difference, update lastDcVal
	 */
        temp = rle->dc;
        rle->dc -= cPtr->lastDcVal[ci];
        cPtr->lastDcVal[ci] = temp;
        EncodeOneBlock(rle,
                       cPtr->dcHuffTblPtrs[compptr->dcTblNo],
                       cPtr->acHuffTblPtrs[compptr->acTblNo]);
    }
}

/*
 *--------------------------------------------------------------
 *
 * HuffEncoderTerm --
 *
 *	Finish up at the end of a Huffman-compressed scan.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Any remaing bits are flushed.
 *
 *--------------------------------------------------------------
 */
void HuffEncoderTerm()
{
    /*
     * Flush out the last data
     */
    FlushBits();
    FlushBytes();
}
