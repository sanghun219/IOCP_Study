#pragma once
#include "IOCompletionPort.h"
#include "Packet.h"

#include <vector>
#include <deque>
#include <thread>
#include <mutex>

class EchoServer : public IOCompletionPort {
public:
	EchoServer() = default;
	virtual ~EchoServer() = default;
	virtual void OnConnected(const UINT32 clientIndex_)override {
		printf("[OnConnect] : Index(%d)\n", clientIndex_);
	}
	virtual void OnClose(const UINT32 clientIndex_)override {
		printf("[OnClose] : Index(%d)\n", clientIndex_);
	}
	virtual void OnReceive(const UINT32 clientIndex_, const UINT32 size_, char* pData_)override {
		printf("[OnReceive] : Index(%d),DataSize(%d)\n", clientIndex_, size_);

		PacketData packet;
		packet.Set(clientIndex_, size_, pData_);

		std::lock_guard<std::mutex> guard(mLock);
		mPacketDataQueue.push_back(packet);
	}
	void Run(const UINT32 maxClient) {
		mIsRunProcessThread = true;
		mProcessThread = std::thread([this]() {ProcessPacket(); });
		StartServer(maxClient);
	}

	void End() {
		mIsRunProcessThread = false;
		if (mProcessThread.joinable())
			mProcessThread.join();
		DestroyThread();
	}
private:
	void ProcessPacket() {
		while (mIsRunProcessThread) {
			auto packetData = DequePacketData();
			if (packetData.DataSize != 0) {
				SendMsg(packetData.SessionIndex, packetData.DataSize, packetData.pPacketData);
			}
			else {
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
		}
	}

	PacketData DequePacketData() {
		PacketData packetData;

		std::lock_guard<std::mutex> guard(mLock);
		if (mPacketDataQueue.empty()) {
			return PacketData();
		}

		packetData.Set(mPacketDataQueue.front());
		mPacketDataQueue.front().Release();
		mPacketDataQueue.pop_front();

		return packetData;
	}
private:
	bool mIsRunProcessThread = false;
	std::thread mProcessThread;
	std::mutex mLock;
	std::deque<PacketData> mPacketDataQueue;
};