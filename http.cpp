
#include <winsock2.h>
#include <ws2tcpip.h>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include "amf.h"

using std::ios;

WSADATA wsaData;
SOCKET ListenSocket = INVALID_SOCKET;


/*
https://en.wikipedia.org/wiki/Action_Message_Format

amf-packet-structure
--------------------
Length					Name					Type	Default
16 bits					version					uimsbf	0 or 3
16 bits					header-count			uimsbf	0
header-count*56+ bits	header-type-structure	binary	free form
16 bits					message-count			uimsbf	1
message-count*64+ bits	message-type-structure	binary	free form

header-type-structure
---------------------
Length						Name				Type	Default
16 bits						header-name-length	uimsbf	0
header-name-length*8 bits	header-name-string	UTF-8	empty
8 bits						must-understand		uimsbf	0
32 bits						header-length		simsbf	variable
header-length*8 bits		AMF0 or AMF3		binary	free form

message-type-structure
----------------------
Length						Name				Type	Default
16 bits						target-uri-length	uimsbf	variable
target-uri-length*8 bits	target-uri-string	UTF-8	variable
16 bits						response-uri-length	uimsbf	2
response-uri-length*8 bits	response-uri-string	UTF-8	"/1"
32 bits						message-length		simsbf	variable
message-length*8 bits		AMF0 or AMF3		binary	free form

Number			- 0x00 (Encoded as IEEE 64-bit double-precision floating point number)
Boolean			- 0x01 (Encoded as a single byte of value 0x00 or 0x01)
String			- 0x02 (16-bit integer string length with UTF-8 string)
Object			- 0x03 (Set of key/value pairs)
Null			- 0x05
ECMA Array		- 0x08 (32-bit entry count)
Object End		- 0x09 (preceded by an empty 16-bit string length)
Strict Array	- 0x0a (32-bit entry count)
Date			- 0x0b (Encoded as IEEE 64-bit double-precision floating point number with 16-bit integer time zone offset)
Long String		- 0x0c (32-bit integer string length with UTF-8 string)
XML Document	- 0x0f (32-bit integer string length with UTF-8 string)
Typed Object	- 0x10 (16-bit integer name length with UTF-8 name, followed by entries)
Switch to AMF3	- 0x11
*/

std::unordered_map<std::string, std::string> documents({
	{
		"/crossdomain.xml"
		,
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: text/xml; charset=UTF-8\r\n"
		"Content-Length: 225\r\n"
		"\r\n"
		"<?xml version=\"1.0\" ?>\n"
		"<cross-domain-policy>\n"
		"  <site-control permitted-cross-domain-policies=\"master-only\"/>\n"
		"  <allow-access-from domain=\"*\"/>\n"
		"  <allow-http-request-headers-from domain=\"*\" headers=\"*\"/>\n"
		"</cross-domain-policy>\n"
	}
});

std::fstream save_file;

void cleanup(int signal)
{
	closesocket(ListenSocket);
	save_file.close();
	exit(0);
}

bool compare(const char* a, const char* b)
{
	while (*a > ' ' && *b > ' ')
	{
		if (*a++ != *b++)
			return false;
	}
	
	return *a <= ' ' && *b <= ' ';
}

char* word_end(char* c)
{
	while (*c > ' ')
		c++;
	return c;
}

void dump(char* buf, int count)
{
	char prbuf[72];
	for (int i = 0; i < count;)
	{
		const char* hex = "0123456789ABCDEF";
		
		memset(prbuf, ' ', sizeof(prbuf));
		prbuf[53] = '|';
		
		int line = i & ~0xF;
		prbuf[0] = hex[(line >> 3 * 4) & 0xF];
		prbuf[1] = hex[(line >> 2 * 4) & 0xF];
		prbuf[2] = hex[(line >> 1 * 4) & 0xF];
		prbuf[3] = hex[(line >> 0 * 4) & 0xF];
		
		int p = 0;
		while (i < count && p < 16)
		{
			int _h = 5 + p * 3;
			int _a = 55 + p;
			
			char v = buf[i];
			char c = buf[i];
			
			if (c < ' ' || c > '~')
				c = '.';
			
			prbuf[_h  ] = hex[(v >> 4) & 0xF];
			prbuf[_h+1] = hex[ v       & 0xF];
			
			prbuf[_a] = c;
			
			i++;
			p++;
			//"0000 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | 000000000000000"
		}
		
		prbuf[55+p] = '\n';
		prbuf[56+p] = '\0';
		std::cout << prbuf;
	}
}

#define SKWS(x) while(*x <= ' ') x++

int find_free_space(int size)
{
	struct slot_t {
		int16_t index;
		int16_t len;
	} slots[10];
	
	save_file.seekg(0);
	save_file.read((char*) slots, sizeof(slots));
	
	for (int i = 9; i > 0; i--)
	{
		for (int j = 0; j < i; j++)
		{
			if ((uint16_t) slots[j].index > (uint16_t) slots[j+1].index)
			{
				slot_t tmp = slots[j+1];
				slots[j+1] = slots[j];
				slots[j] = tmp;
			}
		}
	}
	
	int pos = 40;
	
	if (slots[0].index < 0)
		return pos;
	
	for (int i = 0; i < 10; i++)
	{
		if (slots[i].index < 0)
			break;
		
		if (slots[i].index - pos >= size)
			return pos;
		
		pos = slots[i].index + slots[i].len;
	}
	
	return pos;
}

int main(int argc, const char* argv[])
{
	signal(SIGINT, cleanup);
	
	int iResult;

	// Initialize Winsock
	iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
	if (iResult != 0)
	{
		std::cerr << "WSAStartup failed: " << iResult << std::endl;
		return 1;
	}
	
	addrinfo *result = nullptr, *ptr = nullptr, hints;
	
	std::memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	// Resolve the local address and port to be used by the server
	iResult = getaddrinfo(nullptr, "80", &hints, &result);
	if (iResult != 0)
	{
		std::cerr << "getaddrinfo failed: " << iResult << std::endl;
		WSACleanup();
		return 1;
	}
	
	ListenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
	
	if (ListenSocket == INVALID_SOCKET)
	{
		std::cerr << "Error at socket(): " << WSAGetLastError() << std::endl;
		freeaddrinfo(result);
		WSACleanup();
		return 1;
	}
	
    iResult = bind(ListenSocket, result->ai_addr, (int)result->ai_addrlen);
	freeaddrinfo(result);
    if (iResult == SOCKET_ERROR)
	{
        std::cerr << "bind failed with error: " << WSAGetLastError() << std::endl;
        closesocket(ListenSocket);
        WSACleanup();
        return 1;
    }
	
	if (listen(ListenSocket, SOMAXCONN) == SOCKET_ERROR)
	{
		std::cerr << "Socket error on listen." << WSAGetLastError() << std::endl;
		return 1;
	}
	
	save_file.open("swords2.sav", ios::binary | ios::in | ios::out);
	
	if (!save_file.is_open())
	{
		save_file.open("swords2.sav", ios::binary | ios::in | ios::out | ios::trunc);
		
		if (!save_file.is_open())
		{
			closesocket(ListenSocket);
			return 1;
		}
		
		const int32_t n1 = -1;
		for (int i = 0; i < 10; i++)
			save_file.write((const char*) &n1, 4);
	}
	
	while (1)
	{
		SOCKET ClientSocket;
		SOCKADDR addr;
		int addr_len = sizeof(addr);
		
		ClientSocket = accept(ListenSocket, &addr, &addr_len);
		
		char buf[16 * 1024];
		int r = recv(ClientSocket, buf, sizeof(buf), 0);
		
		#ifdef AMF_DEBUG
		std::cout << "Received " << r << " bytes\n";
		dump(buf, r);
		std::cout << std::endl;
		#endif
		
		//while (1)
		{
			if (compare(buf, "GET"))
			{
				char* ptr = buf + 3;
				SKWS(ptr);
				
				word_end(ptr)[0] = '\0';
				
				std::string uri(ptr);
				auto iter = documents.find(uri);
				if (iter != documents.end())
				{
					const char* c_str = iter->second.c_str();
					int sent = send(ClientSocket, c_str, strlen(c_str), 0);
				}
			}
			else if (compare(buf, "POST"))
			{
				char* ptr = buf + 4;
				SKWS(ptr);
				
				if (compare(ptr, "/amfphp/gateway.php"))
				{
					while (1)
					{
						while (*ptr != '\r') ptr++;
						ptr += 2;
						if (*ptr == '\r')
						{
							ptr += 2;
							break;
						}
					}
					
					if (ptr >= buf + r)
					{
						//Content was sent in a different packet. I don't really know how many
						//other different ways http packets can be cut around. Hopefully there
						//won't be any more little surprises.
						r = recv(ClientSocket, buf, sizeof(buf), 0);
						ptr = buf;
					}
		
					#ifdef AMF_DEBUG
					std::cout << "Received " << r << " bytes\n";
					dump(buf, r);
					std::cout << std::endl;
					#endif
					
					amf_c amf(ptr);
					//amf.debug_print();
					amf_c amf_writer;
					
					for (int i = 0; i < amf.message_count(); i++)
					{
						amf_message_c message = amf.message(i);
						amf_message_c response = amf_writer.create_message();
						
						response.target().append(message.response());
						response.target().append("/onResult");
						response.response().initialize().append("null");
						
						lstring_c target = message.target();
						amf_data_c amf0_1 = message.amf_data();
						amf_data_c amf0_2 = response.amf_data();
						
						amf0_2.initialize();
						
						if (target.equals("swords2.get_character"))
						{
							type_buffer _tbuf;
							amf_strict_array_c* array = (amf_strict_array_c*) amf0_2.append(AMF_STRICT_ARRAY, _tbuf);
							
							for (int j = 0; j < 10; j++)
							{
								int16_t index;
								int16_t len;
								save_file.seekg(j * 4);
								save_file.read((char*) &index, 2);
								save_file.read((char*) &len, 2);
								
								if (index >= 0)
								{
									save_file.seekg(index);
									
									type_buffer _tbuf1;
									amf_strict_array_c* chardata = (amf_strict_array_c*) array->append(AMF_STRICT_ARRAY, _tbuf1);
									chardata->deserialize(save_file, len);
									
									type_buffer _tbuf2;
									amf_number_c* charid = (amf_number_c*) chardata->get(100, _tbuf2);
									
									charid->set_value(j);
								}
								else
								{
									type_buffer _tbuf1;
									array->append(AMF_UNDEFINED, _tbuf1);
								}
							}
						}
						else if (target.equals("swords2.update_character"))
						{
							type_buffer _tbuf;
							amf_strict_array_c* array = (amf_strict_array_c*) amf0_1.get(0, _tbuf);
							
							type_buffer _tbuf1;
							amf_strict_array_c* chardata = (amf_strict_array_c*) array->get(0, _tbuf1);
							
							//type_buffer tbuf2;
							amf_number_c* slot_num = (amf_number_c*) chardata->get(100, _tbuf);
							
							int len = chardata->size();
							int save_index = find_free_space(len);
							
							save_file.seekp(save_index);
							chardata->serialize(save_file, len);
							
							int slot = (int) slot_num->get_value();
							if (slot < 0) slot = 0;
							if (slot > 9) slot = 9;
							
							#ifdef AMF_DEBUG
							std::cout << "Saving (index " << save_index << ", size " << len << ", slot " << slot << ")\n";
							chardata->debug_print((char*) chardata);
							#endif
							
							save_file.seekp(slot * 4);
							save_file.write((char*) &save_index, 2);
							save_file.write((char*) &len, 2);
							
							amf_number_c* send_num = (amf_number_c*) amf0_2.append(AMF_NUMBER, _tbuf1);
							send_num->copy(slot_num);
						}
						else if (target.equals("swords2.delete_character"))
						{
							type_buffer _tbuf;
							type_buffer _tbuf1;
							amf_number_c* num = (amf_number_c*) ((amf_strict_array_c*) amf0_1.get(0, _tbuf))->get(0, _tbuf1);
							
							int slot = (int) num->get_value();
							if (slot < 0) slot = 0;
							if (slot > 9) slot = 9;
							
							const int32_t n1 = -1;
							char slice[40];
							int slice_len = 9 * 4 - slot * 4;
							
							save_file.seekg((slot + 1) * 4);
							save_file.read(slice, slice_len);
							
							save_file.seekp(slot * 4);
							save_file.write(slice, slice_len);
							
							save_file.seekp(9 * 4);
							save_file.write((char*) &n1, 4);
							
							amf_number_c* send_num = (amf_number_c*) amf0_2.append(AMF_NUMBER, _tbuf);
							send_num->set_value(slot);
						}
						else if (target.equals("swords2.new_character"))
						{
							int slot;
							save_file.seekg(0);
							for (slot = 0; slot < 10; slot++)
							{
								int32_t chardata;
								save_file.read((char*) &chardata, 4);
								if (chardata < 0)
									break;
							}
							
							if (slot < 10)
							{
								type_buffer _tbuf;
								amf_strict_array_c* chardata = (amf_strict_array_c*) amf0_2.append(AMF_STRICT_ARRAY, _tbuf);
								
								type_buffer _tbuf1;
								amf_strict_array_c* array = (amf_strict_array_c*) amf0_1.get(0, _tbuf1);
								
								type_buffer _tbuf2;
								array = (amf_strict_array_c*) array->get(0, _tbuf2);
								chardata->copy(array);
								
								amf_number_c* send_num = (amf_number_c*) chardata->append(AMF_NUMBER, _tbuf1);
								send_num->set_value(slot);
								
								int data_size = chardata->size();
								int save_index = find_free_space(data_size);
								
								save_file.seekp(save_index);
								chardata->serialize(save_file, data_size);
								
								save_file.seekp(slot * 4);
								save_file.write((char*) &save_index, 2);
								save_file.write((char*) &data_size, 2);
							}
						}
						else if (target.equals("swords2.max_character"))
						{
							int characters = 0;
							save_file.seekg(0);
							for (int j = 0; j < 10; j++)
							{
								int32_t chardata;
								save_file.read((char*) &chardata, 4);
								if (chardata >= 0)
									characters++;
							}
							
							type_buffer _tbuf1;
							amf_number_c* send_num = (amf_number_c*) amf0_2.append(AMF_NUMBER, _tbuf1);
							send_num->set_value(characters);
						}
					}
					
					char send_buffer[16 * 1024];
					
					int amf_response_len = amf_writer.size();
					int http_header_len = sprintf(
						send_buffer,
						"HTTP/1.1 200 OK\r\n"
						"Content-Type: application/x-amf\r\n"
						"Content-Length: %d\r\n"
						"\r\n",
						amf_response_len
					);
					
					amf_writer.write_to(send_buffer + http_header_len, amf_response_len);
					send(ClientSocket, send_buffer, http_header_len + amf_response_len, 0);
				}
			}
			else
			{
				send(ClientSocket, "HTTP/1.1 200 OK\r\n\r\n", 19, 0);
			}
		}
		
		closesocket(ClientSocket);
		
		
		/*printf("Received %d bytes from address %d.%d.%d.%d\n", r,
			addr.sa_data[2],
			addr.sa_data[3],
			addr.sa_data[4],
			addr.sa_data[5]
		);*/
		
		//buf[r] = '\0';
		//std::cout << buf << std::endl;
		
		//dump(buf, r);
		/*if (response_len)
		{
			std::cout << "\nResponse:" << std::endl;
			dump(response, response_len);
			response_len = 0;
		}*/
		//std::cout << std::endl;
	}
	
	return 0;
}
