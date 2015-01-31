#ifndef GRAINC_DATA_TYPE_HPP
#define GRAINC_DATA_TYPE_HPP

#include <string>

namespace DataType
{
	enum Enum
	{
		Float,
		Vec2,
		Vec3,
		Vec4
	};

	bool parse(const std::string& str, Enum& out);
	const char* name(Enum type);
	size_t size(Enum type);
};

#endif
