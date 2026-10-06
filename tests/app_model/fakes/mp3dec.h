#pragma once

constexpr int MAINBUF_SIZE = 1940;
constexpr int MAX_NCHAN = 2;
constexpr int MAX_NGRAN = 2;
constexpr int MAX_NSAMP = 576;
constexpr int ERR_MP3_NONE = 0;
constexpr int ERR_MP3_INDATA_UNDERFLOW = -1;
constexpr int ERR_MP3_MAINDATA_UNDERFLOW = -2;
using HMP3Decoder = void*;
struct MP3FrameInfo {
    int outputSamps;
    int samprate;
    int nChans;
    int bitsPerSample;
};

// 解码不在本 fixture 范围内；不可用时安全失败。
inline HMP3Decoder MP3InitDecoder() { return nullptr; }
inline void MP3FreeDecoder(HMP3Decoder) {}
inline int MP3FindSyncWord(unsigned char*, int) { return -1; }
inline int MP3Decode(HMP3Decoder, unsigned char**, int*, short*, int) {
    return ERR_MP3_INDATA_UNDERFLOW;
}
inline void MP3GetLastFrameInfo(HMP3Decoder, MP3FrameInfo*) {}
