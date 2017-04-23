#include "K_IOT.h"

K_IOT::K_IOT()
	: wsa_data({ 0 }), is_wsa_startup(false), ble_mtx(), 
	sockfd_send(INVALID_SOCKET), sockfd_recv(INVALID_SOCKET), addr_send({ 0 }), addr_recv({ 0 }),
	ble_listener(INVALID_SOCKET), bth_addr({ 0 }), wsaQuerySet({ 0 }), CSAddrInfo({ 0 }), instanceName(NULL),
	ble_index(0), ble_clients(), ble_thread(NULL), last_error(K_IOT_ERROR::NOT_IOT_ERROR)
{
	if (WSAStartup(MAKEWORD(2, 2), &wsa_data))
	{
		ZeroMemory(&wsa_data, sizeof(wsa_data));
		return;
	}
	else
		this->is_wsa_startup = true;
}

K_IOT::~K_IOT()
{
	// close sockets
	if (this->sockfd_send != INVALID_SOCKET)
		closesocket(this->sockfd_send);
	if (this->sockfd_recv != INVALID_SOCKET)
		closesocket(this->sockfd_recv);
	if (this->ble_listener != INVALID_SOCKET)
		closesocket(this->ble_listener);
	for (std::hash_map<int, SOCKET>::iterator it = this->ble_clients.begin();
		it != this->ble_clients.end();
		it++)
		closesocket(this->ble_clients[it->first]);

	// finish thread
	if (this->ble_thread != NULL)
	{
		this->ble_thread->detach();
		delete this->ble_thread;
	}
	// check wsa
	if (this->is_wsa_startup != false)
		WSACleanup();
}

bool K_IOT::StartIOT()
{
	// check wsa
	if (this->is_wsa_startup == false)
	{
		if (WSAStartup(MAKEWORD(2, 2), &this->wsa_data) != 0)
			return false;
		else
			this->is_wsa_startup = true;
	}

	StopIOT();

	// Create Bluetooth socket
	ZeroMemory(&bth_addr, sizeof(SOCKADDR_BTH));
	ZeroMemory(&CSAddrInfo, sizeof(CSADDR_INFO));
	ZeroMemory(&wsaQuerySet, sizeof(WSAQUERYSET));
	int bth_addr_len = sizeof(SOCKADDR_BTH);
	wchar_t comName[16];
	DWORD lenComName = 16;

	// get computer name
	if (!GetComputerNameW(comName, &lenComName))
	{
		this->last_error = K_IOT_ERROR::BLE_INIT_ERROR;
		return false;
	}

	// make socket
	if ((this->ble_listener = socket(AF_BTH, SOCK_STREAM, BTHPROTO_RFCOMM)) == INVALID_SOCKET)
	{
		this->last_error = K_IOT_ERROR::BLE_INIT_ERROR;
		return false;
	}

	bth_addr.addressFamily = AF_BTH;
	bth_addr.port = BT_PORT_ANY;

	// bind socket
	if (bind(this->ble_listener, (struct sockaddr*)&bth_addr, sizeof(SOCKADDR_BTH)) == SOCKET_ERROR)
	{
		this->last_error = K_IOT_ERROR::BLE_INIT_ERROR;
		return false;
	}

	// get socket name
	if (getsockname(this->ble_listener, (struct sockaddr *)&bth_addr, &bth_addr_len) == SOCKET_ERROR)
	{
		this->last_error = K_IOT_ERROR::BLE_INIT_ERROR;
		return false;
	}

	// set CSADDRInfo
	CSAddrInfo.LocalAddr.iSockaddrLength = sizeof(SOCKADDR_BTH);
	CSAddrInfo.LocalAddr.lpSockaddr = (LPSOCKADDR)&bth_addr;
	CSAddrInfo.RemoteAddr.iSockaddrLength = sizeof(SOCKADDR_BTH);
	CSAddrInfo.RemoteAddr.lpSockaddr = (LPSOCKADDR)&bth_addr;
	CSAddrInfo.iSocketType = SOCK_STREAM;
	CSAddrInfo.iProtocol = BTHPROTO_RFCOMM;

	ZeroMemory(&wsaQuerySet, sizeof(WSAQUERYSET));
	wsaQuerySet.dwSize = sizeof(WSAQUERYSET);
	wsaQuerySet.lpServiceClassId = (LPGUID)&K_IOT_GUID;

	size_t instanceNameSize;
	HRESULT res = StringCchLengthW(comName, sizeof(comName), &instanceNameSize);
	if (FAILED(res))
	{
		this->last_error = K_IOT_ERROR::BLE_INIT_ERROR;
		return false;
	}

	instanceNameSize += sizeof(INSTANCE_NAME) + 1;
	instanceName = (LPWSTR)HeapAlloc(GetProcessHeap(),
		HEAP_ZERO_MEMORY,
		instanceNameSize);

	StringCbPrintfW(instanceName, instanceNameSize, L"%s %s", comName, INSTANCE_NAME);
	wsaQuerySet.lpszServiceInstanceName = instanceName;
	wsaQuerySet.lpszComment = L"Control robot neck with bluetooth(BLE)";
	wsaQuerySet.dwNameSpace = NS_BTH;
	wsaQuerySet.dwNumberOfCsAddrs = 1;      // Must be 1.
	wsaQuerySet.lpcsaBuffer = &CSAddrInfo; // Req'd.

	if (SOCKET_ERROR == WSASetService(&wsaQuerySet, RNRSERVICE_REGISTER, 0))
	{
		this->last_error = K_IOT_ERROR::WSA_SERVICE_ERROR;
		return false;
	}

	// listen
	if (listen(this->ble_listener, SOMAXCONN) == SOCKET_ERROR)
	{
		this->last_error = K_IOT_ERROR::LISTEN_ERROR;
		return false;
	}

	memset((char*)&addr_send, 0, sizeof(addr_send));
	memset((char*)&addr_recv, 0, sizeof(addr_recv));
	// Create UDP, TCP Socket
	if ((sockfd_recv = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET)
	{
		this->last_error = K_IOT_ERROR::UDP_INIT_ERROR;
		return false;
	}
	if ((sockfd_send = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET)
	{
		this->last_error = K_IOT_ERROR::UDP_INIT_ERROR;
		return false;
	}

	addr_send.sin_family = AF_INET;
	addr_send.sin_port = htons(OUT_PORT);
	//inet_pton(AF_INET, THIS_IP, &(addr_send.sin_addr));
	addr_send.sin_addr.S_un.S_addr = inet_addr(THIS_IP);
	addr_recv.sin_family = AF_INET;
	addr_recv.sin_port = htons(THIS_PORT);
	//inet_pton(AF_INET, THIS_IP, &(addr_recv.sin_addr));
	addr_recv.sin_addr.S_un.S_addr = inet_addr(THIS_IP);

	// UDP bind
	if (bind(sockfd_recv, (SOCKADDR*)&addr_recv, sizeof(addr_recv)) == SOCKET_ERROR)
	{
		this->last_error = K_IOT_ERROR::UDP_BIND_ERROR;
		return false;
	}

	// Start thread
	this->ble_thread = new std::thread(&K_IOT::BLE_ListenFunction, this);
	this->udp_thread = new std::thread(&K_IOT::UDP_ReceiveFunction, this);

	return true;
}

bool K_IOT::StopIOT()
{
	// close sockets
	if (this->sockfd_send != INVALID_SOCKET)
		closesocket(this->sockfd_send);
	if (this->sockfd_recv != INVALID_SOCKET)
		closesocket(this->sockfd_recv);
	if (this->ble_listener != INVALID_SOCKET)
		closesocket(this->ble_listener);
	for (std::hash_map<int, SOCKET>::iterator it = this->ble_clients.begin();
		it != this->ble_clients.end();
		it++)
		closesocket(this->ble_clients[it->first]);

	// finish thread
	if (this->ble_thread != NULL)
	{
		this->ble_thread->detach();
		delete this->ble_thread;
		this->ble_thread = NULL;
	}
	if (this->udp_thread != NULL)
	{
		this->udp_thread->detach();
		delete this->udp_thread;
		this->udp_thread = NULL;
	}

	// initialize
	this->sockfd_send = INVALID_SOCKET;
	this->sockfd_recv = INVALID_SOCKET;
	this->ble_listener = INVALID_SOCKET;
	this->ble_index = 0;
	this->ble_clients.clear();
	this->last_error = K_IOT_ERROR::NOT_IOT_ERROR;
	this->ble_mtx.unlock();
}

bool K_IOT::TurnOn(int device_id)
{
	std::hash_map<int, SOCKET>::iterator it = ble_clients.find(device_id);
	if (it->first == device_id)
	{
		IOT_PACKET packet;
		packet.device_id = device_id;
		packet.control = 1;

		if (sendto(this->sockfd_send, (const char*)&packet, sizeof(packet), 0, (const SOCKADDR*)&this->addr_send, sizeof(this->addr_send)) == SOCKET_ERROR)
		{
			this->last_error = K_IOT_ERROR::UDP_SEND_ERROR;
			return false;
		}
		else
			return true;
	}
	else
		return false;
}

bool K_IOT::TurnOff(int device_id)
{
	std::hash_map<int, SOCKET>::iterator it = ble_clients.find(device_id);
	if (it->first == device_id)
	{
		IOT_PACKET packet;
		packet.device_id = device_id;
		packet.control = 0;

		if (sendto(this->sockfd_send, (const char*)&packet, sizeof(packet), 0, (const SOCKADDR*)&this->addr_send, sizeof(this->addr_send)) == SOCKET_ERROR)
		{
			this->last_error = K_IOT_ERROR::UDP_SEND_ERROR;
			return false;
		}
		else
			return true;
	}
	else
		return false;
}

// Thread Functions
void K_IOT::BLE_ListenFunction()
{
	SOCKET temp_client;

	while (true)
	{
		// accept one bluetooth client
		temp_client = accept(this->ble_listener, NULL, NULL);
		if (temp_client == INVALID_SOCKET)
		{
			this->last_error = K_IOT_ERROR::BLE_ACCEPT_ERROR;
			return;
		}
		ble_mtx.lock();
		this->ble_clients.insert(
			std::hash_map<int, SOCKET>::value_type(this->ble_index++, temp_client));
		ble_mtx.unlock();
	}
}
void K_IOT::UDP_ReceiveFunction()
{
	IOT_PACKET packet;

	while (true)
	{
		struct sockaddr_in temp_addr;
		int addr_len = sizeof(temp_addr);
		if (recvfrom(this->sockfd_recv, (char*)&packet, sizeof(packet), 0, (SOCKADDR*)&temp_addr, &addr_len) == SOCKET_ERROR)
		{
			this->last_error = K_IOT_ERROR::UDP_RECV_ERROR;
			return;
		}

		ble_mtx.lock();
		if (packet.control == 1)
			TurnOn(packet.device_id);
		else if (packet.control == 0)
			TurnOff(packet.device_id);
		ble_mtx.unlock();
	}
}