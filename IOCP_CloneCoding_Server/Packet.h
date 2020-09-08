#pragma once
// MFC를 제외한 나머지 프로그램에서 해당 명령을 정의해두면 사용 되지 않는 (Windows 헤더 파일 크기를 줄임)
// 기능을 제거하여 빌드 속도를 빠르게 해준다
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

//TODO : PacketData[2주차]
// Stream 클래스가 추가될지 안될지 모르겠지만 현재 PacketData 구조체의 기능은
// 데이터를 Send/Recv 할 때 필요한 최소한의 데이터인 (크기,데이터,인덱스(식별용))만을 취하고 있다
// Stream 클래스가 추가되어진다면 내부 변수로 PacketData가 사용되지 않을까 생각해본다.
struct PacketData {
public:
	UINT32 SessionIndex = 0;
	UINT32 DataSize = 0;
	char* pPacketData = nullptr;

	void Set(PacketData& value) {
		SessionIndex = value.SessionIndex;
		DataSize = value.DataSize;

		pPacketData = new char[value.DataSize];
		CopyMemory(pPacketData, value.pPacketData, value.DataSize);
	}

	void Set(UINT32 sessionIndex_, UINT32 dataSize_, char* pData) {
		SessionIndex = sessionIndex_;
		DataSize = dataSize_;
		pPacketData = new char[dataSize_];
		CopyMemory(pPacketData, pData, dataSize_);
	}

	void Release() {
		delete pPacketData;
	}
};