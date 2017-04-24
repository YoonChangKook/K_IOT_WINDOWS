#pragma once

#ifndef K_IOT_H
#define K_IOT_H

#ifdef K_IOT_EXPORTS
#define K_IOT_API __declspec(dllexport)
#else
#define K_IOT_API __declspec(dllimport)
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <hash_map>
#include <thread>
#include <mutex>
#include <WS2tcpip.h>
#include <WinSock2.h>
#include <ws2bth.h>
#include <string>
#include <strsafe.h>
#include <initguid.h>

#pragma comment(lib,"ws2_32.lib")
#pragma comment(lib, "bluetoothapis.lib")

#define THIS_IP				"127.0.0.1"
#define THIS_PORT			5002		// from linux iot process
#define OUT_PORT			5001		// to linux iot process
#define INSTANCE_NAME		L"Robot Neck Control"
// {2da3e5b7-6289-d59b-ab30-bf7b16cce294}
DEFINE_GUID(K_IOT_GUID, 0x2da3e5b7, 0x6289, 0xd59b, 0xab, 0x30, 0xbf, 0x7b, 0x16, 0xcc, 0xe2, 0x94);

#pragma pack(push, 1)
typedef struct IOT_PACKET {
	unsigned char device_id;
	unsigned char control;
}IOT_PACKET, *LPIOT_PACKET;
#pragma pack(pop)

enum K_IOT_ERROR
{
	BLE_INIT_ERROR = 0,
	WSA_SERVICE_ERROR = 1,
	LISTEN_ERROR = 2,
	UDP_INIT_ERROR = 3,
	UDP_BIND_ERROR = 4,
	BLE_ACCEPT_ERROR = 5,
	UDP_SEND_ERROR = 6,
	UDP_RECV_ERROR = 7,
	BLE_RECV_ERROR = 8,
	NOT_IOT_ERROR = 100
};

class K_IOT_API K_IOT
{
public:
	K_IOT();
	virtual ~K_IOT();

private:
	WSADATA wsa_data;
	bool is_wsa_startup;
	std::mutex ble_mtx;
	// Process communication sockets (UDP)
	SOCKET sockfd_send;
	SOCKET sockfd_recv;
	struct sockaddr_in addr_send;
	struct sockaddr_in addr_recv;
	std::thread* udp_thread;
	// BLE sockets for devices
	SOCKET ble_listener;
	SOCKADDR_BTH bth_addr;
	WSAQUERYSET wsaQuerySet;
	CSADDR_INFO CSAddrInfo;
	wchar_t* instanceName;
	int ble_index;
	std::hash_map<int, SOCKET> ble_clients;
	std::thread* ble_thread;
	// error
	K_IOT_ERROR last_error;

	// BLE thread function
	void BLE_ListenFunction();
	// UDP thread function
	void UDP_ReceiveFunction();

public:
	bool StartIOT();
	bool StopIOT();
	bool TurnOn(int device_id);
	bool TurnOff(int device_id);
	int GetDeviceCount() const;
	K_IOT_ERROR GetLastError() const;
};

#endif