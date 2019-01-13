/***
 * The content of this file or document is CONFIDENTIAL and PROPRIETARY
 * to ChengZhen(Anyou).  It is subject to the terms of a
 * License Agreement between Licensee and ChengZhen(Anyou).
 * restricting among other things, the use, reproduction, distribution
 * and transfer.  Each of the embodiments, including this information and
 * any derivative work shall retain this copyright notice.
 *
 * Copyright (c) 2014-2015 ChengZhen(Anyou). All Rights Reserved.
 *
 */
#include "stdafx.h"
#include "rtp_decoder.h"
#include "media/vision_media_frame.h"

namespace rtp {

//_____________________________________________________________________________
///////////////////////////////////////////////////////////////////////////////
// RtpDecoder class

RtpDecoder::RtpDecoder()
{

}

IMediaBufferPtr RtpDecoder::DecodeAudioPacket( RtpHeader* header, UINT rtpLength )
{
	if (header == NULL || rtpLength <= 0) {
		return NULL;
	}

	BYTE* data  = (BYTE*)(header) + 12;
	int length  = rtpLength - 12;

	int paylod	=  header->fPayload & 0x7F;
	int marker	= (header->fPayload & 0x80) == 0x80;

	INT64 timestamp = ntohl(header->fTimestamp);
	timestamp = timestamp / 8;

	IMediaBufferPtr mediaBuffer = new MediaBuffer();
	mediaBuffer->SetFrameType(kAudioFrame);
	mediaBuffer->SetMarker(marker);
	mediaBuffer->SetPayloadType(paylod);
	mediaBuffer->SetSyncPoint(TRUE);
	mediaBuffer->SetSampleTime(timestamp);
	mediaBuffer->SetLength(length);

	BYTE* sampleData = data;
	UINT  sampleSize = MIN(1350, length);

	BYTE* buffer = mediaBuffer->GetData();
	memcpy(buffer, sampleData, sampleSize);
	return mediaBuffer;
}

IMediaBufferPtr RtpDecoder::DecodeVideoPacket( RtpHeader* header, UINT rtpLength )
{
	if (header == NULL || rtpLength <= 0) {
		return NULL;
	}

	BYTE* data  = (BYTE*)(header) + 12;
	int length  = rtpLength - 12;

	int paylod	=  header->fPayload & 0x7F;
	int marker	= (header->fPayload & 0x80) == 0x80;

	INT64 sampleTime = ntohl(header->fTimestamp);
	sampleTime = sampleTime / 90;

	IMediaBufferPtr mediaBuffer = new MediaBuffer();
	mediaBuffer->SetFrameType(kAudioFrame);
	mediaBuffer->SetLength(length);
	mediaBuffer->SetMarker(marker);
	mediaBuffer->SetPayloadType(paylod);
	mediaBuffer->SetSyncPoint(TRUE);
	mediaBuffer->SetSampleTime(sampleTime);

	// data
	BYTE* sampleData = data;
	UINT  sampleSize = MIN(1400, length);
	BYTE* buffer = mediaBuffer->GetData();
	memcpy(buffer, sampleData, sampleSize);

	return mediaBuffer;
}

IMediaBufferPtr RtpDecoder::DecodeH264Packet( RtpHeader* header, UINT rtpLength )
{
	const static BYTE RTP_FRAGMENT_START = 0x80;
	const static BYTE NALU_TYPE_MASK = 0x1F;

	BYTE* data = (BYTE*)(header) + 12;
	int length = rtpLength - 12;

	INT64 timestamp = ntohl(header->fTimestamp);
	int paylod	=  header->fPayload & 0x7F;
	int marker	= (header->fPayload & 0x80) == 0x80;

	BYTE* sampleData = data;
	UINT  sampleSize = length;

	BYTE nalHeader = data[0];
	BYTE nalType = data[0] & NALU_TYPE_MASK;
	if (nalType <= kRtp264NaluSingleMax) { // Single NALU mode
		sampleData = data;
		sampleSize = length;
		nalHeader = data[0];

	} else if (nalType == kRtp264NaluSTAP_A) { // ��ϰ� STAP-A
		sampleData = data + 3;
		sampleSize = MAKEWORD(data[2], data[1]);
		nalHeader = data[3];

	} else if (nalType == kRtp264NaluSTAP_B) { // ��ϰ� STAP-B
		sampleData = data + 5;
		sampleSize = MAKEWORD(data[4], data[3]);
		nalHeader = data[5];

	} else if (nalType == kRtp264NaluMTAP16) { // ��ϰ� MTAP16
		sampleData = data + 8;
		sampleSize = MAKEWORD(data[4], data[3]);
		nalHeader = data[8];

	} else if (nalType == kRtp264NaluMTAP24) { // ��ϰ� MTAP24
		sampleData = data + 9;
		sampleSize = MAKEWORD(data[4], data[3]);
		nalHeader = data[9];

	} else if (nalType == kRtp264NaluFU_A || nalType == kRtp264NaluFU_B) { // RTP ��Ƭ FU-A / FU-B
		if (data[1] & RTP_FRAGMENT_START) { // һ����Ƭ�Ŀ�ʼ
			sampleData = data + 1;
			sampleSize = length - 1;
			nalHeader = (data[0] & 0x60) | (data[1] & NALU_TYPE_MASK);

		} else {
			sampleData = data + 2;
			sampleSize = length - 2;
			nalHeader = 0;
		}

		// ���� decoding order number (16bit)
		if (nalType == kRtp264NaluFU_B) {
			sampleData += 2;
			sampleSize -= 2;
		}

	} else {
		return new MediaBuffer();
	}

	if (nalHeader != 0) {
		sampleData[0] = nalHeader;

		sampleData -= 3;
		sampleSize += 3;
		sampleData[0] = 0x00;
		sampleData[1] = 0x00;
		sampleData[2] = 0x01;
	}

	BOOL isSyncPoint = FALSE;
	UINT frameType = kVideoFrameP;
	if ((nalHeader & 0x1f) == 0x07) {
		frameType = kVideoFrameI;
		isSyncPoint = TRUE;
	}

	timestamp = timestamp / 90;

	IMediaBufferPtr mediaBuffer = new MediaBuffer();
	mediaBuffer->SetFrameType(frameType);
	mediaBuffer->SetMarker(marker);
	mediaBuffer->SetPayloadType(paylod);
	mediaBuffer->SetSyncPoint(isSyncPoint);
	mediaBuffer->SetSampleTime(timestamp);
	mediaBuffer->SetLength(sampleSize);

	BYTE* buffer = mediaBuffer->GetData();
	memcpy(buffer, sampleData, sampleSize);

	return mediaBuffer;
}


/**
 * ����ָ���� Sample ��ʼ��ĳ���.
 * @param sample ��� Sample ������
 * @return ���ؿ�ʼ��ĳ���.
 */
UINT RtpDecoder::GetStartCodeLength(const BYTE* sample)
{
	if (sample == NULL) {
		return 0;

	} else if (sample[0] != 0x00 && sample[1] != 0x00) {
		return 0;

	} else if (sample[2] == 0x01) {
		return 3; // [00 00 01]

	} else if (sample[2] == 0x00 && sample[3] == 0x01) {
		return 4; // [00 00 00 01]
	}

	return 0;
}


}

