#include "DataType.hpp"

namespace DataType
{

bool parse(const std::string& str, Enum& out)
{
	if(str == "float")
	{
		out = Float;
		return true;
	}
	else if(str == "vec2")
	{
		out = Vec2;
		return true;
	}
	else if(str == "vec3")
	{
		out = Vec3;
		return true;
	}
	else if(str == "vec4")
	{
		out = Vec4;
		return true;
	}
	else
	{
		return false;
	}
}

size_t size(DataType::Enum type)
{
	switch(type)
	{
		case DataType::Float:
			return 1;
		case DataType::Vec2:
			return 2;
		case DataType::Vec3:
			return 3;
		case DataType::Vec4:
			return 4;
		default:
			return 0;
	}
}

const char* name(DataType::Enum type)
{
	switch(type)
	{
		case DataType::Float:
			return "float";
		case DataType::Vec2:
			return "vec2";
		case DataType::Vec3:
			return "vec3";
		case DataType::Vec4:
			return "vec4";
		default:
			return 0;
	}
}

}
