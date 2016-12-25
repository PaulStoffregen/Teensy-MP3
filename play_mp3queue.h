/* 

play_mp3queue
 
MIT License

Copyright (c) 2016 Frank Boesing, (c) f.boesing(at)gmx.de 2016

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

#ifndef play_mp3queue_h_
#define play_mp3queue_h_

#include <Arduino.h>
#include <Audio.h>
#include "mp3/coder.h"

#define MP3_FRAMEBUFFERS 		2 //# of input buffers. minimum 2
#define MP3_OUTPUTBUFFERS		2 //# of decoded samples buffers. minimum 2
#define MP3_CHANNELS_MAX		2 // 1=MONO 2=STEREO. minimum 1
#define MP3_BITRATE_MAX			320000 //320kbps
#define MP3_SAMPLERATE_MAX		48000
#define MP3_SAMPLES_PER_FRAME 	1152
#define MP3_FRAME_SIZE 			(int) ceil((MP3_SAMPLES_PER_FRAME / 8) * MP3_BITRATE_MAX / MP3_SAMPLERATE_MAX)
#define MP3_IN_BUFSIZE 			(MP3_FRAME_SIZE+1) * MP3_FRAMEBUFFERS

#define ERR_MP3_INVALID_FORMAT	-99

typedef int (*codecGetData)(uint8_t* p, int len);

struct _MP3DecInfo_data {
	FrameHeader fh;
	SideInfo si;
	ScaleFactorInfo sfi;
	HuffmanInfo hi;
	DequantInfo di;
	IMDCTInfo mi;
	SubbandInfo sbi;
};

class AudioPlayMP3Queue : public AudioStream
{
public:
	AudioPlayMP3Queue(void) : AudioStream(0, NULL) { paused = 0; init(); };
	void reset(void) { init(); }

	void setCallback(codecGetData getData) { cbGetData = getData; }; //set callback for pushData(void)
	int pushData(void);
	
	int pushData(uint8_t* data, int* length);
	inline void pause(uint8_t pause) { paused = pause;}

	inline int free(void) { return sizeof(framebuf) - framebuf_BytesCount; }
	inline int samplesInBuffer(void) { return outbuf_count * mp3FrameInfo.outputSamps - (mp3FrameInfo.outputSamps - play_pos); }
	inline int isPlaying(void) {return (samplesInBuffer() > 0); }
	inline int available(void) { return sizeof(framebuf) - framebuf_BytesCount; }
	inline int numChannels(void) { return mp3FrameInfo.nChans; }
	inline int samplerate(void) { return mp3FrameInfo.samprate; }
	inline int bitrate(void) { return mp3FrameInfo.bitrate; }
	inline int frames(void) { return decoded_frames; }
	inline int framesize(void) { return (mp3FrameInfo.outputSamps / 8 * mp3FrameInfo.bitrate) / mp3FrameInfo.samprate; }
	//void debugInfo(void);

private:

	void init(void);
	void initMP3(void);
	int decode(void);
	virtual void update(void);

	codecGetData cbGetData = NULL;
	uint8_t paused;

	uint8_t framebuf[MP3_IN_BUFSIZE] __attribute__((aligned(4)));
	int framebuf_BytesCount;

	int16_t outbuf[MP3_OUTPUTBUFFERS][MP3_SAMPLES_PER_FRAME * MP3_CHANNELS_MAX] __attribute__((aligned(4)));
	int16_t decoded_length[MP3_OUTPUTBUFFERS];
	unsigned outbuf_head,outbuf_tail;
	volatile int outbuf_count;

	uint32_t decoded_frames, played_frames;
	uintptr_t play_pos;

	// needed by decoder:
	MP3DecInfo mp3DecInfo;
	MP3FrameInfo mp3FrameInfo;
	_MP3DecInfo_data mp3DecInfoData;
};

#endif
