
#ifndef AMF_H
#define AMF_H

#include <fstream>

enum {
	AMF_NUMBER,
	AMF_BOOLEAN,
	AMF_STRING,
	AMF_OBJECT,
	AMF_NULL = 0x05,
	AMF_UNDEFINED,
	AMF_ECMA_ARRAY = 0x08,
	AMF_OBJECT_END,
	AMF_STRICT_ARRAY,
	AMF_DATE,
	AMF_LONG_STRING,
	AMF_XML = 0x0F,
	AMF_TYPED_OBJECT
};

enum {
	type_cast_exception = 1
};


class lstring_c {
	char* data;
	
public:
	lstring_c (char* c) : data(c) {}
	bool equals(lstring_c);
	bool equals(const char*);
	lstring_c initialize();
	void append(lstring_c);
	void append(const char*);
	
	#ifdef AMF_DEBUG
	void debug_print(char*, const char*);
	#endif
};

class amf_type_c {
protected:
	char* size_tracker;
	char* data;
	
public:
	explicit operator char*() const { return data; }
	amf_type_c(char* s, char* d) : size_tracker(s), data(d) {}
	static amf_type_c* parse(char* s, char* d, char* class_buffer);
	amf_type_c* next(char* class_buffer);
	void increase_size(int byte_count);
	virtual void initialize() = 0;
	virtual int size() const = 0;
	void copy(const amf_type_c* src);
	amf_type_c& operator=(const amf_type_c*& a) { copy(a); return *this; }
	void serialize(std::fstream& out, int len);
	void deserialize(std::fstream& in, int len);
	
	#ifdef AMF_DEBUG
	virtual void debug_print(char*) = 0;
	#endif
};

class amf_number_c : public amf_type_c {
public:
	amf_number_c(char* s, char* d) : amf_type_c(s, d) {}
	void initialize();
	int size() const;
	void set_value(double value);
	double get_value();
	
	#ifdef AMF_DEBUG
	void debug_print(char*);
	#endif
};

class amf_string_c : public amf_type_c {
public:
	amf_string_c(char* s, char* d) : amf_type_c(s, d) {}
	void initialize();
	int size() const;
	
	#ifdef AMF_DEBUG
	void debug_print(char*);
	#endif
};

class amf_undefined_c : public amf_type_c {
public:
	amf_undefined_c(char* s, char* d) : amf_type_c(s, d) {}
	void initialize();
	int size() const;
	
	#ifdef AMF_DEBUG
	void debug_print(char*);
	#endif
};

class amf_strict_array_c : public amf_type_c {
public:
	amf_strict_array_c(char* s, char* d) : amf_type_c(s, d) {}
	void initialize();
	int size() const;
	int array_size();
	amf_type_c* append(int type, char* class_buffer);
	amf_type_c* get(int index, char* class_buffer);
	
	#ifdef AMF_DEBUG
	void debug_print(char*);
	#endif
};

constexpr int max_type_class_size()
{
	int size = sizeof(amf_number_c);
	if (size < sizeof(amf_string_c)) size = sizeof(amf_string_c);
	if (size < sizeof(amf_undefined_c)) size = sizeof(amf_undefined_c);
	if (size < sizeof(amf_strict_array_c)) size = sizeof(amf_strict_array_c);
	return size;
}

struct alignas(8) type_buffer {
	char array[max_type_class_size()];
	operator char*() { return array; }
};

class amf_data_c {
	char* data;
	
public:
	amf_data_c(char* d) : data(d) {}
	void initialize();
	amf_type_c* append(int type, char* class_buffer);
	amf_type_c* get(int index, char* class_buffer);
	
	#ifdef AMF_DEBUG
	void debug_print(char*);
	#endif
};

class amf_message_c {
	char* data;
	
public:
	explicit operator char*() const { return data; }
	
	amf_message_c(char* d) : data(d) {}
	void initialize();
	amf_message_c next_message();
	lstring_c target();
	lstring_c response();
	amf_data_c amf_data();
	
	#ifdef AMF_DEBUG
	void debug_print(char*);
	#endif
};

class amf_header_c {
	char* data;
	
public:
	explicit operator char*() const { return data; }
	
	amf_header_c(char* d) : data(d) {}
	void initialize();
	amf_header_c next_header();
	
	#ifdef AMF_DEBUG
	void debug_print(char*);
	#endif
};

class amf_c {
	bool needs_cleanup;
	
	char* data;
	char* header_data;
	char* message_data;
	
public:
	explicit operator char*() const { return data; }
	
	amf_c();
	amf_c(char* b);
	~amf_c();
	
	int header_count();
	int message_count();
	
	amf_header_c header(int index);
	amf_message_c message(int index);
	amf_message_c create_message();
	
	int size();
	void write_to(char* buffer, int len);
	
	#ifdef AMF_DEBUG
	void debug_print();
	#endif
};

#endif
