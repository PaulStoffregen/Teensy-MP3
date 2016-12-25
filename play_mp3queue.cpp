/* 

play_mp3queue
 
MIT License

Copyright (c) f.boesing(at)gmx.de

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

 */

#include "play_mp3queue.h"
#include "mp3/assembly.h"
#include "mp3/mp3dec.h"
#include "mp3/mp3dec.c"
#include "mp3/mp3tabs.c"
#include "mp3/bitstream.c"
#include "mp3/buffers.c"
#include "mp3/dct32.c"
#include "mp3/dequant.c"
#include "mp3/dqchan.c"
//#include "mp3/polyphase.c"
#include "mp3/imdct.c"
#include "mp3/scalfact.c"
#include "mp3/stproc.c"
#include "mp3/subband.c"
#include "mp3/trigtabs.c"
#include "mp3/huffman.c"
#include "mp3/hufftabs.c"

extern "C" { void memcpy_frominterleaved(int16_t *dst1, int16_t *dst2, int16_t *src); }

void AudioPlayMP3Queue::init(void) {
	
	AudioNoInterrupts();
	decoded_frames = 0;
	played_frames = 0;
	play_pos = 0;
	decoded_length[0] = 0;
	decoded_length[1] = 0;
	outbuf_count = 0;
	outbuf_head = 0;
	outbuf_tail = 0;
	framebuf_BytesCount = 0;
	AudioInterrupts();
		
	memset(framebuf, 0, sizeof(framebuf));
	memset(outbuf, 0, sizeof(outbuf));

	initMP3();
}

void AudioPlayMP3Queue::initMP3(void) {
	//Inportant - initialize these with zero!
	memset((void*)&mp3DecInfo, 0, sizeof(mp3DecInfo));
	memset((void*)&mp3FrameInfo, 0, sizeof(mp3FrameInfo));
	memset((void*)&mp3DecInfoData, 0, sizeof(mp3DecInfoData));

	mp3DecInfo.FrameHeaderPS = (void*)&mp3DecInfoData.fh;
	mp3DecInfo.SideInfoPS = (void*)&mp3DecInfoData.si;
	mp3DecInfo.ScaleFactorInfoPS = (void*)&mp3DecInfoData.sfi;
	mp3DecInfo.HuffmanInfoPS = (void*)&mp3DecInfoData.hi;
	mp3DecInfo.DequantInfoPS = (void*)&mp3DecInfoData.di;
	mp3DecInfo.IMDCTInfoPS = (void*)&mp3DecInfoData.mi;
	mp3DecInfo.SubbandInfoPS = (void*)&mp3DecInfoData.sbi;
}

inline int myMP3FindSyncWord(unsigned char *buf, int nBytes)
{
	int i;

	/* find byte-aligned syncword - need MPEG1 Layer 3 */
	for (i = 0; i < nBytes - 1; i++) {
		if ( ((buf[i+0] & 0xff) == 0xff) && (((buf[i+1]^0x04) & 0xfe) == 0xfe) 
				&& ((buf[i+2] & 0xf0) != 0x00) //no free bitrate
				&& ((buf[i+2] & 0xf0) <= 0xa0) //not above 320k
				&& ((buf[i+2] & 0x0c) < 0x0c) //valid Sampling rate index
				 ) 				 
			return i;
	}
	
	return -1;
}

int AudioPlayMP3Queue::pushData(void) {
	if (cbGetData==NULL) return ERR_MP3_INDATA_UNDERFLOW;
	int len = free();
	if (len) {
		int rd = cbGetData(framebuf + framebuf_BytesCount, len);
		framebuf_BytesCount += rd;
	}
	return decode();
}

int AudioPlayMP3Queue::pushData(uint8_t* data, int* length) {
	
	if (*length <0) *length = 0;
		
	uint8_t* p = framebuf;
	if ( (int)(sizeof(framebuf) - framebuf_BytesCount) >= *length) {
		memcpy(p + framebuf_BytesCount, data, *length);
		framebuf_BytesCount += *length;
		*length = 0;
	}

	return decode();
}

int AudioPlayMP3Queue::decode(void) {
int result;	
int offset = 0; 	
uint8_t* p = framebuf;

	// ***** Check output-buffer *****
	if (outbuf_count >= MP3_OUTPUTBUFFERS ) {
			return 0; //no free output-buffers
	}

	do {		
		// ***** Check SYNC-START *****
		// find start of next MP3 frame, return error if not found
		offset = myMP3FindSyncWord(p, framebuf_BytesCount);
		if (offset < 0) {
			return ERR_MP3_INDATA_UNDERFLOW;
		}

		p += offset;
		framebuf_BytesCount -= offset;

		// ***** Check framebuffer *****
		result = MP3GetNextFrameInfo((HMP3Decoder)&mp3DecInfo, &mp3FrameInfo, p);						
		if (result == 0) break;

		p+=1; 
		framebuf_BytesCount-=1;			
	} while (1);

	if (framebuf_BytesCount < framesize() ) {			
			return ERR_MP3_INDATA_UNDERFLOW;
	}

	int oldCount = framebuf_BytesCount;
	uint8_t* oldP = p;
	result = MP3Decode( (HMP3Decoder)&mp3DecInfo, &p, &framebuf_BytesCount, &outbuf[outbuf_head][0], 0);

	//Serial.printf("%d Bytes decoded. Result: %d Framebuf_BytesCount:%d \n", oldCount - framebuf_BytesCount,result, framebuf_BytesCount);

	if (result == ERR_MP3_NONE) {
		MP3GetLastFrameInfo((HMP3Decoder)&mp3DecInfo, &mp3FrameInfo);
		//Serial.printf("Samples: %d Channels: d Samplerate: %d l:%d v:%d\n", mp3FrameInfo.outputSamps, mp3FrameInfo.nChans , mp3FrameInfo.samprate, mp3FrameInfo.layer, mp3FrameInfo.version );
		#if MP3_SAMPLERATE_MAX < 48000		
		if ( (mp3FrameInfo.samprate > MP3_SAMPLERATE_MAX) ) return ERR_MP3_INVALID_FORMAT;
		#endif

		//move unused part of framebuffer to the start
		memmove(framebuf, p, framebuf_BytesCount);

		decoded_length[outbuf_head] = mp3FrameInfo.outputSamps;
		outbuf_count++;
		outbuf_head++;
		if (outbuf_head >= MP3_OUTPUTBUFFERS) outbuf_head = 0;
		decoded_frames++;
		//Serial.println(" success.");
	}
	else if (result < ERR_MP3_MAINDATA_UNDERFLOW) {
		memmove(framebuf, oldP+1, oldCount-1);
		framebuf_BytesCount--;
		initMP3();
	}
	return result;
}


void AudioPlayMP3Queue::update(void) {
audio_block_t	*block_Count;
audio_block_t	*block_right;

	if (paused) return;
	if (outbuf_count < 1) return; //Buffer is empty

	
	// allocate the audio blocks to transmit
	block_Count = allocate();
	if (block_Count == NULL) return;
	int playing_block = outbuf_tail;
	if (mp3FrameInfo.nChans == 2) {
		// if we're playing stereo, allocate another
		// block for the right channel output
		block_right = allocate();
		if (block_right == NULL) {
			release(block_Count);
			return;
		}

		memcpy_frominterleaved(&block_Count->data[0], &block_right->data[0], &outbuf[playing_block][0] + play_pos);

		play_pos += AUDIO_BLOCK_SAMPLES * 2;
		transmit(block_Count, 0);
		transmit(block_right, 1);
		release(block_right);
		decoded_length[playing_block] -= AUDIO_BLOCK_SAMPLES * 2;

	} else if (mp3FrameInfo.nChans == 1) {
		// playing mono: no right-side block
		// let's do a (hopefully good optimized) simple memcpy
		memcpy(block_Count->data, &outbuf[playing_block][0] + play_pos, AUDIO_BLOCK_SAMPLES * sizeof(int16_t));

		play_pos += AUDIO_BLOCK_SAMPLES;
		transmit(block_Count, 0);
		transmit(block_Count, 1);
		decoded_length[playing_block] -= AUDIO_BLOCK_SAMPLES;
	}

	release(block_Count);

	//Switch to the next block if no data:
	if (decoded_length[playing_block] == 0) {
		played_frames++;
		playing_block++;
		outbuf_count--;
		play_pos = 0;
		if (playing_block >= MP3_OUTPUTBUFFERS) playing_block = 0;
		outbuf_tail = playing_block;
	}
}
