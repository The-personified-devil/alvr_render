#include <stdint.h>
#include <string.h>

#include "alvr_binding.h"

enum ALVR_CODEC {
	ALVR_CODEC_H264 = 0,
	ALVR_CODEC_HEVC = 1,
	ALVR_CODEC_AV1 = 2,
};

enum ALVR_H264_PROFILE {
	ALVR_H264_PROFILE_HIGH = 0,
	ALVR_H264_PROFILE_MAIN = 1,
	ALVR_H264_PROFILE_BASELINE = 2,
};

enum ALVR_RATE_CONTROL_METHOD {
	ALVR_CBR = 0,
	ALVR_VBR = 1,
};

enum ALVR_ENTROPY_CODING {
	ALVR_CABAC = 0,
	ALVR_CAVLC = 1,
};

enum ALVR_ENCODER_QUALITY_PRESET {
	ALVR_QUALITY = 0,
	ALVR_BALANCED = 1,
	ALVR_SPEED = 2
};

static const char NAL_PREFIX_3B[] = {0x00, 0x00, 0x01};
static const char NAL_PREFIX_4B[] = {0x00, 0x00, 0x00, 0x01};

static const unsigned char H264_NAL_TYPE_SPS = 7;
static const unsigned char HEVC_NAL_TYPE_VPS = 32;

static const unsigned char H264_NAL_TYPE_AUD = 9;
static const unsigned char HEVC_NAL_TYPE_AUD = 35;

int8_t getNalPrefixSize(unsigned char *buf) {
    if (memcmp(buf, NAL_PREFIX_3B, sizeof(NAL_PREFIX_3B)) == 0) {
        return sizeof(NAL_PREFIX_3B);
    } else if (memcmp(buf, NAL_PREFIX_4B, sizeof(NAL_PREFIX_4B)) == 0) {
        return sizeof(NAL_PREFIX_4B);
    } else {
        return -1;
    }
}

/*
Sends the (VPS + )SPS + PPS video configuration headers from H.264 or H.265 stream as a sequence of
NALs. (VPS + )SPS + PPS have short size (8bytes + 28bytes in some environment), so we can assume
SPS + PPS is contained in first fragment.
*/
void sendHeaders(AlvrCodecType codec, unsigned char *&buf, int &len, int nalNum) {
    unsigned char *cursor = buf;
    int headersLen = 0;
    int foundHeaders = -1; // Offset by 1 header to find the length until the next header

    while (headersLen <= len) {
        if (headersLen + sizeof(NAL_PREFIX_4B) > (unsigned)len) {
            cursor++;
            headersLen++;
            continue;
        }
        int8_t prefixSize = getNalPrefixSize(cursor);
        if (prefixSize == -1) {
            cursor++;
            headersLen++;
            continue;
        }
        foundHeaders++;
        if (foundHeaders == nalNum) {
            break;
        }
        headersLen += prefixSize;
        cursor += prefixSize;
    }

    if (foundHeaders != nalNum) {
        return;
    }

    alvr_set_video_config_nals(codec, (const unsigned char *)buf, headersLen);

    // move the cursor forward excluding config NALs
    buf = cursor;
    len -= headersLen;
}

void processH264Nals(unsigned char *&buf, int &len) {
    unsigned char prefixSize = getNalPrefixSize(buf);
    unsigned char nalType = buf[prefixSize] & 0x1F;

    if (nalType == H264_NAL_TYPE_AUD && len > prefixSize * 2 + 2) {
        buf += prefixSize + 2;
        len -= prefixSize + 2;
        prefixSize = getNalPrefixSize(buf);
        nalType = buf[prefixSize] & 0x1F;
    }
    if (nalType == H264_NAL_TYPE_SPS) {
        sendHeaders(ALVR_CODEC_TYPE_H264, buf, len, 2); // 2 headers SPS and PPS
    }
}

void processHevcNals(unsigned char *&buf, int &len) {
    unsigned char prefixSize = getNalPrefixSize(buf);
    unsigned char nalType = (buf[prefixSize] >> 1) & 0x3F;

    if (nalType == HEVC_NAL_TYPE_AUD && len > prefixSize * 2 + 3) {
        buf += prefixSize + 3;
        len -= prefixSize + 3;
        prefixSize = getNalPrefixSize(buf);
        nalType = (buf[prefixSize] >> 1) & 0x3F;
    }
    if (nalType == HEVC_NAL_TYPE_VPS) {
        sendHeaders(ALVR_CODEC_TYPE_HEVC, buf, len, 3); // 3 headers VPS, SPS and PPS
    }
}

void ParseFrameNals(
    int codec, AlvrViewParams const* viewParams,  unsigned char *buf, int len, unsigned long long targetTimestampNs, bool isIdr) {
    static bool av1GotFrame = false;

    if ((unsigned)len < sizeof(NAL_PREFIX_4B)) {
        return;
    }

    if (codec == ALVR_CODEC_H264) {
        processH264Nals(buf, len);
    } else if (codec == ALVR_CODEC_HEVC) {
        processHevcNals(buf, len);
    } else if (codec == ALVR_CODEC_AV1 && !av1GotFrame) {
        av1GotFrame = true;
        alvr_set_video_config_nals(ALVR_CODEC_TYPE_AV1, 0, 0);
    }

    alvr_send_video_nal(targetTimestampNs, viewParams, isIdr, buf, len);
}
