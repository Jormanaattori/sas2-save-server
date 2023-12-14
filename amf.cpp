
#include <cstring>
#include "amf.h"

#ifdef AMF_DEBUG
#include <iostream>
#endif

int read_int16(char* c) { unsigned char* _c = (unsigned char*) c; return _c[0] << 8 | _c[1]; }
int read_int32(char* c) { unsigned char* _c = (unsigned char*) c; return _c[0] << 24 | _c[1] << 16 | _c[2] << 8 | _c[3]; }
int read_int16i(char*& c) { int r = read_int16(c); c += 2; return r; }
int read_int32i(char*& c) { int r = read_int32(c); c += 4; return r; }
void write_int16(char* c, int v) { c[0] = (v >> 8) & 0xFF; c[1] = v & 0xFF; }
void write_int32(char* c, int v) { c[0] = (v >> 24) & 0xFF; c[1] = (v >> 16) & 0xFF; c[2] = (v >> 8) & 0xFF; c[3] = v & 0xFF; }
void write_int16i(char*& c, int v) { write_int16(c, v); c += 2; }
void write_int32i(char*& c, int v) { write_int32(c, v); c += 4; }
void skip_string(char* &c) { int len = read_int16i(c); c += len; }
void add_int16(char* c, int v) { int old_v = read_int16(c); write_int16(c, old_v + v); }
void add_int32(char* c, int v) { int old_v = read_int32(c); write_int32(c, old_v + v); }

bool lstring_c::equals(lstring_c str)
{
	int len1 = read_int16(data);
	int len2 = read_int16(str.data);
	
	if (len1 != len2)
		return false;
	
	char* ptr1 = data + 2;
	char* ptr2 = str.data + 2;
	while (len1--)
		if (*ptr1++ != *ptr2++)
			return false;
	return true;
}

bool lstring_c::equals(const char* str)
{
	char* ptr = data;
	int len = read_int16i(ptr);
	
	while (len-- && *str)
		if (*ptr++ != *str++)
			return false;
	return *str == 0;
}

lstring_c lstring_c::initialize()
{
	write_int16(data, 0);
	return *this;
}

void lstring_c::append(lstring_c str)
{
	int len1 = read_int16(data);
	int len2 = read_int16(str.data);
	add_int16(data, len2);
	std::memcpy(data + len1 + 2, str.data + 2, len2);
}

void lstring_c::append(const char* str)
{
	int len1 = read_int16(data);
	int len2 = strlen(str);
	add_int16(data, len2);
	std::memcpy(data + len1 + 2, str, len2);
}


amf_type_c* amf_type_c::parse(char* s, char* d, char* class_buffer)
{
	switch ((unsigned char)*d)
	{
		case AMF_NUMBER:
			return new (class_buffer) amf_number_c(s, d);
		case AMF_STRING:
			return new (class_buffer) amf_string_c(s, d);
		case AMF_STRICT_ARRAY:
			return new (class_buffer) amf_strict_array_c(s, d);
		default:
			return new (class_buffer) amf_undefined_c(s, d);
	}
}

amf_type_c* amf_type_c::next(char* class_buffer)
{
	char* d = data + size();
	return amf_type_c::parse(size_tracker, d, class_buffer);
}

void amf_type_c::increase_size(int byte_count)
{
	add_int32(size_tracker, byte_count);
}

void amf_type_c::copy(const amf_type_c* src)
{
	if (src->data[0] != data[0])
		throw type_cast_exception;
	
	int len_dst = size();
	int len_src = src->size();
	
	increase_size(len_src - len_dst);
	memcpy(data, src->data, len_src);
}

void amf_type_c::serialize(std::fstream& out, int len)
{
	out.write(data, len);
}

void amf_type_c::deserialize(std::fstream& in, int len)
{
	int old_size = size();
	in.read(data, len);
	int new_size = size();
	
	increase_size(new_size - old_size);
}


void amf_number_c::initialize()
{
	data[0] = (char) AMF_NUMBER;
	memset(data + 1, 0, 8);
	increase_size(9);
}

int amf_number_c::size() const
{
	return 9;
}

void amf_number_c::set_value(double value)
{
	char* c = (char*) &value;
	data[1] = c[7]; data[2] = c[6];
	data[3] = c[5]; data[4] = c[4];
	data[5] = c[3]; data[6] = c[2];
	data[7] = c[1]; data[8] = c[0];
}

double amf_number_c::get_value()
{
	double ret;
	char* c = (char*) &ret;
	c[7] = data[1]; c[6] = data[2];
	c[5] = data[3]; c[4] = data[4];
	c[3] = data[5]; c[2] = data[6];
	c[1] = data[7]; c[0] = data[8];
	return ret;
}


void amf_string_c::initialize()
{
	data[0] = (char) AMF_STRING;
	memset(data + 1, 0, 2);
	increase_size(3);
}

int amf_string_c::size() const
{
	return read_int16(data + 1) + 3;
}


void amf_undefined_c::initialize()
{
	data[0] = (char) AMF_UNDEFINED;
	increase_size(1);
}

int amf_undefined_c::size() const
{
	return 1;
}


void amf_strict_array_c::initialize()
{
	data[0] = (char) AMF_STRICT_ARRAY;
	memset(data + 1, 0, 4);
	increase_size(5);
}

int amf_strict_array_c::size() const
{
	int size = 5;
	
	int items = read_int32(data + 1);
	while (items--)
	{
		char* c = data + size;
		type_buffer buf;
		amf_type_c* entry = amf_type_c::parse(size_tracker, c, buf);
		
		size += entry->size();
	}
	
	return size;
}

int amf_strict_array_c::array_size()
{
	return read_int32(data + 1);
}

amf_type_c* amf_strict_array_c::append(int type, char* class_buffer)
{
	char* type_ptr = data + size();
	type_ptr[0] = (char) type;
	amf_type_c* amf_type = amf_type_c::parse(size_tracker, type_ptr, class_buffer);
	amf_type->initialize();
	add_int32(data + 1, 1);
	return amf_type;
}

amf_type_c* amf_strict_array_c::get(int index, char* class_buffer)
{
	char* ptr = data + 5;
	amf_type_c* amf_type = amf_type_c::parse(size_tracker, ptr, class_buffer);
	
	while (index--)
	{
		ptr += amf_type->size();
		amf_type = amf_type_c::parse(size_tracker, ptr, class_buffer);
	}
	
	return amf_type;
}


void amf_data_c::initialize()
{
	memset(data, 0, 4);
}

amf_type_c* amf_data_c::append(int type, char* class_buffer)
{
	int len = read_int32(data);
	char* type_ptr = data + 4 + len;
	type_ptr[0] = (char) type;
	amf_type_c* amf_type = amf_type_c::parse(data, type_ptr, class_buffer);
	amf_type->initialize();
	return amf_type;
}

amf_type_c* amf_data_c::get(int index, char* class_buffer)
{
	char* ptr = data + 4;
	amf_type_c* amf_type = amf_type_c::parse(data, ptr, class_buffer);
	
	while (index--)
	{
		ptr += amf_type->size();
		amf_type = amf_type_c::parse(data, ptr, class_buffer);
	}
	
	return amf_type;
}


void amf_message_c::initialize()
{
	memset(data, 0, 8);
}

amf_message_c amf_message_c::next_message()
{
	char* c = data;
	skip_string(c);
	skip_string(c);
	int len = read_int32i(c);
	c += len;
	return amf_message_c(c);
}

lstring_c amf_message_c::target()
{
	return lstring_c(data);
}

lstring_c amf_message_c::response()
{
	char* c = data;
	skip_string(c);
	return lstring_c(c);
}

amf_data_c amf_message_c::amf_data()
{
	char* c = data;
	skip_string(c);
	skip_string(c);
	return amf_data_c(c);
}


void amf_header_c::initialize()
{
	memset(data, 0, 7);
}

amf_header_c amf_header_c::next_header()
{
	char* c = data;
	skip_string(c);
	c++;
	int len = read_int32i(c);
	c += len;
	return amf_header_c(c);
}


amf_c::amf_c() : data(new char[16 * 1024]), needs_cleanup(true)
{
	char* c = data;
	write_int16i(c, 0);
	write_int16i(c, 0);
	write_int16i(c, 0);
	
	header_data = data + 2;
	message_data = data + 4;
}

amf_c::amf_c(char* b) : data(b), needs_cleanup(false)
{
	header_data = data + 2;
	message_data = (char*) header(header_count());
}

amf_c::~amf_c()
{
	if (needs_cleanup)
		delete[] data;
}

int amf_c::header_count()
{
	return read_int16(header_data);
}

int amf_c::message_count()
{
	return read_int16(message_data);
}

amf_header_c amf_c::header(int index)
{
	amf_header_c header(header_data + 2);
	while (index--)
		header = header.next_header();
	return header;
}

amf_message_c amf_c::message(int index)
{
	amf_message_c message(message_data + 2);
	while (index--)
		message = message.next_message();
	return message;
}

amf_message_c amf_c::create_message()
{
	amf_message_c msg((char*) message(message_count()));
	msg.initialize();
	add_int16(message_data, 1);
	return msg;
}
	
int amf_c::size()
{
	int msg_count = message_count();
	amf_message_c msg = message(msg_count);
	return ((char*) msg) - data;
}

void amf_c::write_to(char* buffer, int len)
{
	std::memcpy(buffer, data, len);
}


#ifdef AMF_DEBUG
#define INDENTATION_STEP 2
int indentation = 0;

void print_indent()
{
	int i = indentation;
	while (i--)
		std::cout << ' ';
}

void print_hex(char c, int prefix = 0)
{
	const char* hex = "0123456789ABCDEF";
	if (prefix == 0)
		std::cout << "0x";
	else if (prefix == 1)
		std::cout << "\\x";
	std::cout << hex[(c>>8)&0xF] << hex[c&0xF];
}

void print_int8(char* base, char*& ptr, const char* label)
{
	print_indent();
	std::cout << label << ' ' << ptr[0] << "\t| " << (void*)(ptr - base) << ' ';
	print_hex(ptr[0], 2);
	std::cout << '\n';
	
	ptr++;
}

void print_int16(char* base, char*& ptr, const char* label)
{
	int value = read_int16(ptr);
	
	print_indent();
	std::cout << label << ' ' << value << "\t| " << (void*)(ptr - base) << ' ';
	print_hex(ptr[0], 2);
	print_hex(ptr[1], 2);
	std::cout << '\n';
	
	ptr += 2;
}

void print_int32(char* base, char*& ptr, const char* label)
{
	int value = read_int32(ptr);
	
	print_indent();
	std::cout << label << ' ' << value << "\t| " << (void*)(ptr - base) << ' ';
	
	print_hex(ptr[0], 2);
	print_hex(ptr[1], 2);
	print_hex(ptr[2], 2);
	print_hex(ptr[3], 2);
	std::cout << '\n';
	
	ptr += 4;
}

void lstring_c::debug_print(char* base, const char* label)
{
	print_indent();
	std::cout << label << " \"";
	
	int size = read_int16(data);
	char* d = data + 2;
	
	while (size--)
	{
		char c = *d++;
		if (c < ' ' || c > '~')
			print_hex(c, 1);
		else
			std::cout << c;
	}
	
	std::cout << "\"\t| " << (void*)(data - base) << '\n';
}

void amf_number_c::debug_print(char* base)
{
	int value = (int) get_value();
	
	print_indent();
	std::cout << '[';
	print_hex((char) AMF_NUMBER, 2);
	std::cout << "] " << (void*)(data - base) << " number (value=" << value << "): ";
	print_hex(data[0], 2);
	std::cout << ' ';
	print_hex(data[1], 2);
	print_hex(data[2], 2);
	print_hex(data[3], 2);
	print_hex(data[4], 2);
	print_hex(data[5], 2);
	print_hex(data[6], 2);
	print_hex(data[7], 2);
	print_hex(data[8], 2);
	std::cout << '\n';
}

void amf_string_c::debug_print(char* base)
{
	int size = read_int16(data + 1);
	print_indent();
	
	std::cout << '[';
	print_hex((char) AMF_STRING, 2);
	std::cout << "] " << (void*)(data - base) << " string (size=" << size << ",value=\"";
	
	char* d = data + 3;
	
	for (int i = 0; i < size; i++)
	{
		char c = *d++;
		if (c < ' ' || c > '~')
			print_hex(c, 1);
		else
			std::cout << c;
	}
	
	std::cout << "\"): ";
	print_hex(data[0], 2);
	std::cout << ' ';
	print_hex(data[1], 2);
	print_hex(data[2], 2);
	std::cout << ' ';
	for (int i = 0; i < size; i++)
		print_hex(data[3+i], 2);
	std::cout << '\n';
}

void amf_undefined_c::debug_print(char* base)
{
	print_indent();
	
	std::cout << '[';
	print_hex((char) AMF_UNDEFINED, 2);
	std::cout << "] " << (void*)(data - base) << " undefined ";
	print_hex(data[0], 2);
	std::cout << '\n';
}

void amf_strict_array_c::debug_print(char* base)
{
	int items = array_size();
	
	print_indent();
	std::cout << '[';
	print_hex((char) AMF_STRICT_ARRAY, 2);
	std::cout << "] " << (void*)(data - base) << " strict array (items=" << items << "): ";
	print_hex(data[0], 2);
	std::cout << '\n';
	
	indentation += INDENTATION_STEP;
	char* ptr = data + 5;
	type_buffer buffer;
	while (items--)
	{
		amf_type_c* type = amf_type_c::parse(size_tracker, ptr, buffer);
		type->debug_print(base);
		ptr += type->size();
	}
	indentation -= INDENTATION_STEP;
}

void amf_data_c::debug_print(char* base)
{
	char* c = data;
	print_int32(base, c, "Amf size:");
	//print_indent();
	//std::cout << '\n';
	
	int size = read_int32(data);
	int read = 0;
	int item = 0;
	
	while (read < size)
	{
		type_buffer buffer;
		amf_type_c* type = get(item++, buffer);
		type->debug_print(base);
		read += type->size();
	}
	
	if (read != size)
	{
		print_indent();
		std::cout << "Warning: amf0 item size total didn't match stored value!\n";
	}
}

void amf_message_c::debug_print(char* base)
{
	char* ptr = data;
	
	lstring_c name(ptr);
	name.debug_print(base, "Target:");
	skip_string(ptr);
	
	name = lstring_c(ptr);
	name.debug_print(base, "Response:");
	skip_string(ptr);
	
	char* amf_data = ptr;
	print_int32(base, ptr, "Message length:");
	
	indentation += INDENTATION_STEP;
	amf_data_c amf(amf_data);
	amf.debug_print(base);
	indentation -= INDENTATION_STEP;
}

void amf_header_c::debug_print(char* base)
{
	char* ptr = data;
	
	lstring_c name(ptr);
	name.debug_print(base, "Name:");
	
	skip_string(ptr);
	print_int8(base, ptr, "Must understand:");
	char* amf_data = ptr;
	print_int32(base, ptr, "Header length:");
	std::cout << "Amf0 data:\n";
	
	indentation += INDENTATION_STEP;
	amf_data_c amf(amf_data);
	amf.debug_print(base);
	indentation -= INDENTATION_STEP;
}

void amf_c::debug_print()
{
	char* ptr = data;
	int count;
	
	print_int16(data, ptr, "Version:");
	print_int16(data, ptr, "Header count:");
	std::cout << "Headers:\n";
	
	indentation += INDENTATION_STEP;
	count = header_count();
	amf_header_c hdr = header(0);
	
	for (int i = 0; i < count; i++)
	{
		hdr.debug_print(data);
		hdr = hdr.next_header();
	}
	indentation -= INDENTATION_STEP;
	
	ptr = (char*) hdr;
	if (ptr != message_data)
		std::cout << "Warning: headers corrupted! (pointer not at message_data)\n";
	
	print_int16(data, ptr, "Message count:");
	std::cout << "Messages:\n";
	
	indentation += INDENTATION_STEP;
	count = message_count();
	amf_message_c msg = message(0);
	
	for (int i = 0; i < count; i++)
	{
		msg.debug_print(data);
		msg = msg.next_message();
	}
	indentation -= INDENTATION_STEP;
}
#endif

/*unsigned char example_amf[] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x15, 0x73, 0x77, 0x6f, 0x72, 0x64, 0x73, 0x32, 0x2e,
	0x6e, 0x65, 0x77, 0x5f, 0x63, 0x68, 0x61, 0x72, 0x61, 0x63, 0x74, 0x65, 0x72, 0x00, 0x02, 0x2f,
	0x34, 0x00, 0x00, 0x01, 0x1b, 0x0a, 0x00, 0x00, 0x00, 0x01, 0x0a, 0x00, 0x00, 0x00, 0x64, 0x02,
	0x00, 0x08, 0x4e, 0x61, 0x6d, 0x65, 0x6c, 0x65, 0x73, 0x73, 0x02, 0x00, 0x01, 0x39, 0x02, 0x00,
	0x02, 0x31, 0x36, 0x02, 0x00, 0x02, 0x31, 0x31, 0x02, 0x00, 0x02, 0x32, 0x39, 0x02, 0x00, 0x01,
	0x37, 0x02, 0x00, 0x01, 0x30, 0x02, 0x00, 0x01, 0x30, 0x02, 0x00, 0x01, 0x30, 0x02, 0x00, 0x01,
	0x30, 0x02, 0x00, 0x01, 0x30, 0x02, 0x00, 0x01, 0x30, 0x02, 0x00, 0x01, 0x30, 0x02, 0x00, 0x01,
	0x30, 0x02, 0x00, 0x01, 0x30, 0x02, 0x00, 0x01, 0x30, 0x02, 0x00, 0x01, 0x34, 0x02, 0x00, 0x01,
	0x31, 0x02, 0x00, 0x01, 0x34, 0x02, 0x00, 0x01, 0x32, 0x02, 0x00, 0x01, 0x31, 0x02, 0x00, 0x01,
	0x31, 0x02, 0x00, 0x01, 0x31, 0x02, 0x00, 0x01, 0x33, 0x02, 0x00, 0x01, 0x31, 0x02, 0x00, 0x01,
	0x30, 0x02, 0x00, 0x03, 0x31, 0x32, 0x35, 0x02, 0x00, 0x01, 0x31, 0x02, 0x00, 0x01, 0x30, 0x02,
	0x00, 0x04, 0x32, 0x35, 0x30, 0x30, 0x02, 0x00, 0x01, 0x31, 0x02, 0x00, 0x01, 0x30, 0x02, 0x00,
	0x01, 0x30, 0x02, 0x00, 0x01, 0x30, 0x02, 0x00, 0x01, 0x31, 0x02, 0x00, 0x01, 0x31, 0x02, 0x00,
	0x01, 0x31, 0x02, 0x00, 0x01, 0x31, 0x02, 0x00, 0x01, 0x31, 0x02, 0x00, 0x01, 0x31, 0x02, 0x00,
	0x01, 0x31, 0x02, 0x00, 0x01, 0x30, 0x02, 0x00, 0x01, 0x31, 0x02, 0x00, 0x01, 0x30, 0x02, 0x00,
	0x01, 0x30, 0x02, 0x00, 0x01, 0x30, 0x02, 0x00, 0x01, 0x30, 0x02, 0x00, 0x01, 0x30, 0x02, 0x00,
	0x01, 0x35, 0x02, 0x00, 0x01, 0x31, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
	0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
	0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
	0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x00, 0x40, 0x5e, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00
};

int main()
{
	std::cout
		<< sizeof(amf_number_c) << '\n'
		<< sizeof(amf_string_c) << '\n'
		<< sizeof(amf_undefined_c) << '\n'
		<< sizeof(amf_strict_array_c) << '\n'
		<< sizeof(type_buffer) << '\n'
		<< std::endl;
	
	amf_c amf1((char*) example_amf);
	amf_c amf2;
	
	amf_message_c message = amf1.message(0);
	amf_message_c response = amf2.create_message();
	response.target().append(message.response());
	response.target().append("/onResult");
	response.response().initialize().append("null");
	
	amf_data_c amf0_1 = message.amf_data();
	amf_data_c amf0_2 = response.amf_data();
	amf0_2.initialize();
	
	type_buffer buffer;
	amf_strict_array_c* array = (amf_strict_array_c*) amf0_1.get(0, buffer);
	
	type_buffer buffer1;
	amf_strict_array_c* array1 = (amf_strict_array_c*) array->get(0, buffer1);
	
	array = (amf_strict_array_c*) amf0_2.append(AMF_STRICT_ARRAY, buffer);
	array->copy(array1);
	
	amf_number_c* number = (amf_number_c*) array->append(AMF_NUMBER, buffer1);
	number->set_value(9);
	
	amf1.debug_print();
	std::cout << std::endl;
	amf2.debug_print();
	
	return 0;
}*/
